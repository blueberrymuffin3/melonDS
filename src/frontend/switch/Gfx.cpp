#define _USE_MATH_DEFINES
#include <math.h>

#include "Gfx.h"

#include <deko3d.hpp>

#include <assert.h>
#include <string.h>

#include <array>
#include <unordered_map>
#include <algorithm>
#include <optional>

#include <stdio.h>

#include <stdarg.h>

#include "stb_truetype/stb_truetype.h"

#include "mm_vec/mm_vec.h"

namespace Gfx
{

dk::Device Device;
dk::Queue Queue;
dk::CmdBuf CmdBuf;
dk::Swapchain Swapchain;

class GpuMemHeap
{
    struct Block
    {
        bool Free;
        u32 Offset, Size;
        // it would probably be smarter to make those indices because that's smaller
        Block* SiblingLeft, *SiblingRight;
        Block* Next, *Prev;
    };

    // this is a home made memory allocator based on TLSF (http://www.gii.upv.es/tlsf/)
    // I hope it doesn't have to many bugs

    // Free List
    u32 FirstFreeList = 0;
    u32* SecondFreeListBits;
    Block** SecondFreeList;

    Block* BlockPool;
    Block* BlockPoolUnused = nullptr;

    void BlockListPushFront(Block*& head, Block* block)
    {
        if (head)
        {
            assert(head->Prev == nullptr);
            head->Prev = block;
        }

        block->Prev = nullptr;
        block->Next = head;
        head = block;
    }

    Block* BlockListPopFront(Block*& head)
    {
        Block* result = head;
        assert(result && "popping from empty block list");

        head = result->Next;
        if (head)
            head->Prev = nullptr;

        return result;
    }

    void BlockListRemove(Block*& head, Block* block)
    {
        assert((head == block) == !block->Prev);
        if (!block->Prev)
            head = block->Next;

        if (block->Prev)
            block->Prev->Next = block->Next;
        if (block->Next)
            block->Next->Prev = block->Prev;
    }

    void MapSizeToSecondLevel(u32 size, u32& fl, u32& sl)
    {
        assert(size >= 32);
        fl = 31 - __builtin_clz(size);
        sl = (size - (1 << fl)) >> (fl - 5);
    }

    void MarkFree(Block* block)
    {
        assert(!block->Free);
        block->Free = true;
        u32 fl, sl;
        MapSizeToSecondLevel(block->Size, fl, sl);

        BlockListPushFront(SecondFreeList[(fl - 5) * 32 + sl], block);

        FirstFreeList |= 1 << (fl - 5);
        SecondFreeListBits[fl - 5] |= 1 << sl;
    }

    void UnmarkFree(Block* block)
    {
        assert(block->Free);
        block->Free = false;
        u32 fl, sl;
        MapSizeToSecondLevel(block->Size, fl, sl);

        BlockListRemove(SecondFreeList[(fl - 5) * 32 + sl], block);

        if (!SecondFreeList[(fl - 5) * 32 + sl])
        {
            SecondFreeListBits[fl - 5] &= ~(1 << sl);

            if (SecondFreeListBits[fl - 5] == 0)
                FirstFreeList &= ~(1 << (fl - 5));
        }
    }

    // makes a new block to the right and returns it
    Block* SplitBlockRight(Block* block, u32 offset)
    {
        assert(!block->Free);
        Block* newBlock = BlockListPopFront(BlockPoolUnused);

        newBlock->Offset = block->Offset + offset;
        newBlock->Size = block->Size - offset;
        newBlock->SiblingLeft = block;
        newBlock->SiblingRight = block->SiblingRight;
        newBlock->Free = false;
        if (newBlock->SiblingRight)
        {
            assert(newBlock->SiblingRight->SiblingLeft == block);
            newBlock->SiblingRight->SiblingLeft = newBlock;
        }

        block->Size -= newBlock->Size;
        block->SiblingRight = newBlock;

        return newBlock;
    }

    Block* MergeBlocksLeft(Block* block, Block* other)
    {
        assert(block->SiblingRight == other);
        assert(other->SiblingLeft == block);
        assert(!block->Free);
        assert(!other->Free);
        assert(block->Offset + block->Size == other->Offset);
        block->Size += other->Size;
        block->SiblingRight = other->SiblingRight;
        if (block->SiblingRight)
        {
            assert(block->SiblingRight->SiblingLeft == other);
            block->SiblingRight->SiblingLeft = block;
        }

        BlockListPushFront(BlockPoolUnused, other);

        return block;
    }
public:
    dk::MemBlock MemBlock;

    struct Allocation
    {
        u32 BlockIdx;
        u32 Offset, Size;
    };

    GpuMemHeap(u32 size, u32 flags, u32 blockPoolSize)
    {
        assert((size & (DK_MEMBLOCK_ALIGNMENT - 1)) == 0 && "block size not properly aligned");
        u32 sizeLog2 = 31 - __builtin_clz(size);
        if (((u32)1 << sizeLog2) > size)
            sizeLog2++; // round up to the next power of two
        assert(sizeLog2 >= 5);

        u32 rows = sizeLog2 - 4; // remove rows below 32 bytes and round up to next

        SecondFreeListBits = new u32[rows];
        memset(SecondFreeListBits, 0, rows*4);
        SecondFreeList = new Block*[rows * 32];
        memset(SecondFreeList, 0, rows*32*8);

        BlockPool = new Block[blockPoolSize];
        memset(BlockPool, 0, sizeof(Block)*blockPoolSize);
        for (u32 i = 0; i < blockPoolSize; i++)
            BlockListPushFront(BlockPoolUnused, &BlockPool[i]);

        // insert heap into the free list
        Block* heap = BlockListPopFront(BlockPoolUnused);
        heap->Offset = 0;
        heap->Size = size - (flags & DkMemBlockFlags_Code ? DK_SHADER_CODE_UNUSABLE_SIZE : 0);
        heap->SiblingLeft = nullptr;
        heap->SiblingRight = nullptr;
        heap->Free = false;
        MarkFree(heap);

        MemBlock = dk::MemBlockMaker{Device, size}
            .setFlags(flags).create();
    }

    void Destroy()
    {
        MemBlock.destroy();

        delete[] BlockPool;
        delete[] SecondFreeList;
        delete[] SecondFreeListBits;
    }

    Allocation Alloc(u32 size, u32 align)
    {
        assert(size > 0);
        assert((align & (align - 1)) == 0 && "alignment must be a power of two");
        // minimum alignment (and thus size) is 32 bytes
        align = std::max((u32)32, align);
        size = (size + align - 1) & ~(align - 1);

        u32 fl, sl;
        MapSizeToSecondLevel(size + (align > 32 ? align : 0), fl, sl);

        u32 secondFreeListBits = SecondFreeListBits[fl - 5] & (0xFFFFFFFF << (sl + 1));

        if (sl == 31 || secondFreeListBits == 0)
        {
            u32 firstFreeListBits = FirstFreeList & (0xFFFFFFFF << (fl - 5 + 1));
            assert(firstFreeListBits && "out of memory :(");

            fl = __builtin_ctz(firstFreeListBits) + 5;
            secondFreeListBits = SecondFreeListBits[fl - 5];
            assert(secondFreeListBits);
            sl = __builtin_ctz(secondFreeListBits);
        }
        assert(secondFreeListBits && "out of memory :(");

        sl = __builtin_ctz(secondFreeListBits);

        Block* block = SecondFreeList[(fl - 5) * 32 + sl];
        UnmarkFree(block);

        // align within the block and put that back
        if ((block->Offset & (align - 1)) > 0)
        {
            assert(align > 32);
            Block* newBlock = SplitBlockRight(block, ((block->Offset + align - 1) & ~(align - 1)) - block->Offset);
            MarkFree(block);
            block = newBlock;
        }
        // put remaining data back
        if (block->Size > size)
        {
            MarkFree(SplitBlockRight(block, size));
        }

        assert((block->Offset & (align - 1)) == 0);
        return {(u32)(block - BlockPool), block->Offset, block->Size};
    }

    void Free(Allocation allocation)
    {
        Block* block = &BlockPool[allocation.BlockIdx];

        if (block->SiblingLeft && block->SiblingLeft->Free)
        {
            UnmarkFree(block->SiblingLeft);
            block = MergeBlocksLeft(block->SiblingLeft, block);
        }
        if (block->SiblingRight && block->SiblingRight->Free)
        {
            UnmarkFree(block->SiblingRight);
            block = MergeBlocksLeft(block, block->SiblingRight);
        }

        MarkFree(block);
    }

    DkGpuAddr GpuAddr(Allocation allocation)
    {
        return MemBlock.getGpuAddr() + allocation.Offset;
    }

    template <typename T>
    T* CpuAddr(Allocation allocation)
    {
        return (T*)((u8*)MemBlock.getCpuAddr() + allocation.Offset);
    }
};

struct Vertex
{
    float Position[2];
    float UV[2];
    u8 Color[4];
};

struct Transformation
{
    float Projection[4*4];
};

int SwapchainSlot = 0;

std::optional<GpuMemHeap> TextureHeap;
std::optional<GpuMemHeap> ShaderCodeHeap;
std::optional<GpuMemHeap> DataHeap;

GpuMemHeap::Allocation CmdBufData[2];

dk::Fence ImageUploadFence[2];

dk::Image Framebuffers[2];

const u32 MaxVertices = 1024*64;
const u32 MaxIndices = MaxVertices * 6;

Vertex VertexDataClient[MaxVertices];
u16 IndexDataClient[MaxIndices];

GpuMemHeap::Allocation ImageDescriptors[2];
GpuMemHeap::Allocation SamplerDescriptor;

std::vector<u64> ImageDescriptorsDirty;
u32 ImageDescriptorsAllocated;

u32 CurClientVertex = 0;
u32 CurClientIndex = 0;

u32 CurSampler = 0;
bool SmoothEdges = false;

enum
{
    drawCallDirty_Texture = 1 << 0,
    drawCallDirty_Sampler = 1 << 1,
    drawCallDirty_Scissor = 1 << 2,
    drawCallDirty_Shader = 1 << 3
};
struct DrawCall
{
    u32 Dirty;
    u32 TextureIdx, Sampler;
    u32 Count;
    DkScissor Scissor;
    bool SmoothEdges;
};
std::vector<DrawCall> DrawCalls;

GpuMemHeap::Allocation VertexData[2];
GpuMemHeap::Allocation IndexData[2];
GpuMemHeap::Allocation UniformBuffer;

GpuMemHeap::Allocation TextureStagingBuffer[2];

dk::Shader VertexShader, FragmentShader, FragmentShaderSmoothEdges;

NWindow* Window;

template <typename T>
struct Registry
{
    std::vector<T> Items;
    std::vector<u32> FreeItems;

    u32 Alloc()
    {
        u32 result;
        if (FreeItems.size() > 0)
        {
            result =  FreeItems[FreeItems.size() - 1];
            FreeItems.pop_back();
        }
        else
        {
            result = Items.size();
            Items.push_back(T());
        }
        return result;
    }

    void Free(u32 index)
    {
        FreeItems.push_back(index);
    }

    T& operator[](u32 index)
    {
//        assert(index < Items.size());
        return Items[index];
    }
};

struct Texture
{
    u32 Width, Height;
    DkImageFormat Format;
    GpuMemHeap::Allocation GpuMem;
    dk::Image Image;
    DkImageSwizzle ComponentSwizzle[4];
    int ImageDescriptorIdx = -1;
};

struct PendingTextureUpload
{
    u32 TextureIdx;
    u32 X, Y, Width, Height;
    void* Data;
    u32 DataStrideBytes;
};

Registry<Texture> Textures;

std::vector<u32> UsedTextures;
std::vector<PendingTextureUpload> TextureUploadsPending;

u32 TextureCreate(u32 width, u32 height, DkImageFormat format)
{
    u32 idx = Textures.Alloc();

    Texture& texture = Textures[idx];
    texture.Width = width;
    texture.Height = height;
    texture.Format = format;

    dk::ImageLayout layout;
    dk::ImageLayoutMaker{Device}
        .setFormat(format)
        .setDimensions(width, height)
        .initialize(layout);
    texture.ComponentSwizzle[0] = DkImageSwizzle_Red;
    texture.ComponentSwizzle[1] = DkImageSwizzle_Green;
    texture.ComponentSwizzle[2] = DkImageSwizzle_Blue;
    texture.ComponentSwizzle[3] = DkImageSwizzle_Alpha;
    texture.GpuMem = TextureHeap->Alloc(layout.getSize(), layout.getAlignment());

    texture.Image.initialize(layout, TextureHeap->MemBlock, texture.GpuMem.Offset);

    return idx;
}

void TextureDelete(u32 idx)
{
    Texture& texture = Textures[idx];

    TextureHeap->Free(texture.GpuMem);

    Textures.Free(idx);
}

void TextureUpload(u32 index, u32 x, u32 y, u32 width, u32 height, void* data, u32 dataStride)
{
    TextureUploadsPending.push_back({index, x, y, width, height, data, dataStride});
}

void TextureSetSwizzle(u32 idx, DkImageSwizzle red, DkImageSwizzle green, DkImageSwizzle blue, DkImageSwizzle alpha)
{
    Texture& texture = Textures[idx];
    texture.ComponentSwizzle[0] = red;
    texture.ComponentSwizzle[1] = green;
    texture.ComponentSwizzle[2] = blue;
    texture.ComponentSwizzle[3] = alpha;
}

u32 Atlas::GetCurrent(int width, int height)
{
    bool full;
    if (Atlases.size() == 0)
    {
        full = true;
    }
    else
    {
        AtlasTexture& lastAtlas = Atlases[Atlases.size() - 1];
        
        full = lastAtlas.PackingX + width > AtlasSize
            && lastAtlas.PackingY + lastAtlas.PackingRowHeight + height > AtlasSize;
    }

    if (full)
    {
        AtlasTexture newAtlas;
        newAtlas.Texture = TextureCreate(AtlasSize, AtlasSize, TexFmt);
        TextureSetSwizzle(newAtlas.Texture, Swizzle[0], Swizzle[1], Swizzle[2], Swizzle[3]);
        newAtlas.ClientImage = new u8[AtlasSize * AtlasSize * BytesPerPixel];
        memset(newAtlas.ClientImage, 0, AtlasSize * AtlasSize * BytesPerPixel);

        Atlases.push_back(newAtlas);
    }
    return Atlases.size() - 1;
}

u8* Atlas::Pack(int width, int height, PackedQuad& quad)
{
    AtlasTexture& atlas = Atlases[GetCurrent(width, height)];
    quad.AtlasTexture = atlas.Texture;

    if (atlas.PackingX + width > AtlasSize)
    {
        atlas.PackingX = 0;
        atlas.PackingY += atlas.PackingRowHeight + 1;
        atlas.PackingRowHeight = 0;
    }

    quad.PackX = atlas.PackingX;
    quad.PackY = atlas.PackingY;

    atlas.DirtyX1 = std::min(atlas.DirtyX1, quad.PackX);
    atlas.DirtyY1 = std::min(atlas.DirtyY1, quad.PackY);
    atlas.DirtyX2 = std::clamp(atlas.DirtyX2, quad.PackX + width + 1, AtlasSize);
    atlas.DirtyY2 = std::clamp(atlas.DirtyY2, quad.PackY + height + 1, AtlasSize);

    atlas.PackingX += width + 1;
    atlas.PackingRowHeight = std::max(atlas.PackingRowHeight, height);

    return atlas.ClientImage + quad.PackX * BytesPerPixel + quad.PackY * PackStride();
}

void Atlas::IssueUpload()
{
    for (u32 i = 0; i < Atlases.size(); i++)
    {
        AtlasTexture& atlas = Atlases[i];
        int dirtyWidth = atlas.DirtyX2 - atlas.DirtyX1;
        int dirtyHeight = atlas.DirtyY2 - atlas.DirtyY1;

        if (dirtyWidth > 0 && dirtyHeight > 0)
        {
            TextureUpload(atlas.Texture,
                atlas.DirtyX1, atlas.DirtyY1,
                dirtyWidth, dirtyHeight,
                &atlas.ClientImage[atlas.DirtyX1 * BytesPerPixel + atlas.DirtyY1 * PackStride()],
                PackStride());

            atlas.DirtyX1 = atlas.DirtyY1 = atlas.DirtyX2 = atlas.DirtyY2 = 0;
        }
    }
}

void Atlas::Destroy()
{
    for (int i = 0; i < Atlases.size(); i++)
    {
        delete[] Atlases[i].ClientImage;
        TextureDelete(Atlases[i].Texture);
    }
}

struct PackedGlyph
{
    PackedQuad Quad;
    int BoxX1, BoxY1, BoxX2, BoxY2;
    float AdvanceWidth, LeftSideBearing;
};

struct GlyphKey
{
    u32 Codepoint;
    float Scale;

    bool operator==(const Gfx::GlyphKey& other) const
    {
        return Codepoint == other.Codepoint && (int)(Scale * (1<<12)) == (int)(other.Scale * (1<<12));
    }
};

struct GlyphKeyHash
{
    std::size_t operator()(const Gfx::GlyphKey& key) const
    {
        // float is converted to fixpoint so that small differences won't end in a glyph rendered twice
        return (std::size_t)key.Codepoint ^ ((std::size_t)(key.Scale * (1<<12)) << 4);
    }
};

struct Font
{
    stbtt_fontinfo Info;
    int Ascent, Descent, LineGap;

    std::unordered_map<GlyphKey, PackedGlyph, GlyphKeyHash> PackedGlyphs;
};

Registry<Font> Fonts;
Atlas FontAtlas{DkImageFormat_R8_Unorm, 1, {DkImageSwizzle_One, DkImageSwizzle_One, DkImageSwizzle_One, DkImageSwizzle_Red}};

u32 FontLoad(u8* data)
{
    u32 idx = Fonts.Alloc();
    Font& font = Fonts[idx];

    stbtt_InitFont(&font.Info, data, 0);
    stbtt_GetFontVMetrics(&font.Info, &font.Ascent, &font.Descent, &font.LineGap);

    return idx;
}

void FontDelete(u32 idx)
{
    Font& font = Fonts[idx];
    font.PackedGlyphs.clear();

    Fonts.Free(idx);
}

float FontGetScale(u32 idx, float pixelHeight)
{
    return stbtt_ScaleForPixelHeight(&Fonts[idx].Info, pixelHeight);
}

float FontGetAscent(u32 idx, float scale)
{
    return (float)Fonts[idx].Ascent * scale;
}

float FontGetDescent(u32 idx, float scale)
{
    return (float)Fonts[idx].Descent * scale;
}

float FontGetLineGap(u32 idx, float scale)
{
    return (float)Fonts[idx].LineGap * scale;
}

PackedGlyph& FontGetGlyph(u32 idx, u32 codepoint, float scale)
{
    Font& font = Fonts[idx];

    auto existingRender = font.PackedGlyphs.find({codepoint, scale});
    if (existingRender == font.PackedGlyphs.end())
    {
        PackedGlyph result;

        int glyphIndex = stbtt_FindGlyphIndex(&font.Info, codepoint);

        stbtt_GetGlyphBitmapBox(&font.Info,
            glyphIndex,
            scale, scale,
            &result.BoxX1, &result.BoxY1,
            &result.BoxX2, &result.BoxY2);
        int advanceWidth, leftSideBearing;
        stbtt_GetGlyphHMetrics(&font.Info, glyphIndex, &advanceWidth, &leftSideBearing);
        result.AdvanceWidth = advanceWidth * scale;
        result.LeftSideBearing = leftSideBearing * scale;

        // terrible packing algorithm ahead
        int glyphBoxW = result.BoxX2 - result.BoxX1;
        int glyphBoxH = result.BoxY2 - result.BoxY1;

        u8* outPtr = FontAtlas.Pack(glyphBoxW, glyphBoxH, result.Quad);

        stbtt_MakeGlyphBitmap(&font.Info,
            outPtr, 
            glyphBoxW, glyphBoxH,
            FontAtlas.PackStride(),
            scale, scale,
            glyphIndex);

        font.PackedGlyphs[{codepoint, scale}] = result;
        return font.PackedGlyphs[{codepoint, scale}];
    }
    else
    {
        return existingRender->second;
    }
}

u32 SystemFontStandard;
u32 SystemFontNintendoExt;

u8* SystemFontStandardData;
u8* SystemFontNintendoExtData;

u32 WhiteTexture;

struct DkshHeader
{
    uint32_t magic; // DKSH_MAGIC
    uint32_t header_sz; // sizeof(DkshHeader)
    uint32_t control_sz;
    uint32_t code_sz;
    uint32_t programs_off;
    uint32_t num_programs;
};

void LoadShader(const char* path, dk::Shader& out)
{
    FILE* f = fopen(path, "rb");
    if (f)
    {
        DkshHeader header;
        fread(&header, sizeof(DkshHeader), 1, f);

        rewind(f);
        u8* ctrlmem = new u8[header.control_sz];
        size_t read = fread(ctrlmem, header.control_sz, 1, f);
        assert(read);

        GpuMemHeap::Allocation data = ShaderCodeHeap->Alloc(header.code_sz, DK_SHADER_CODE_ALIGNMENT);
        read = fread(ShaderCodeHeap->CpuAddr<void>(data), header.code_sz, 1, f);
        assert(read);

        dk::ShaderMaker{ShaderCodeHeap->MemBlock, data.Offset}
            .setControl(ctrlmem)
            .setProgramId(0)
            .initialize(out);

        delete[] ctrlmem;
        fclose(f);
    }
    else
    {
        printf("couldn't open shader file %s\n", path);
        assert(false);
    }
}

std::vector<DkScissor> ScissorStack;
void PushScissor(u32 x, u32 y, u32 w, u32 h)
{
    ScissorStack.push_back({x, y, w, h});
}

void PopScissor()
{
    ScissorStack.pop_back();
}

void Init()
{
    Window = nwindowGetDefault();
    nwindowSetDimensions(Window, 1920, 1080);

    Device = dk::DeviceMaker{}.create();
    Queue = dk::QueueMaker{Device}.setFlags(DkQueueFlags_Graphics).create();

    TextureHeap = GpuMemHeap(1024*1024*128, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image, 128);
    ShaderCodeHeap = GpuMemHeap(1024*1024,
        DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code, 32);
    DataHeap = GpuMemHeap(1024*1024*16, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached, 128);

    CmdBuf = dk::CmdBufMaker{Device}.create();
    for (int i = 0; i < 2; i++)
        CmdBufData[i] = DataHeap->Alloc(0x10000*2, 0x10000);

    dk::ImageLayout fbLayout;
    dk::ImageLayoutMaker{Device}
        .setFlags(DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression)
        .setFormat(DkImageFormat_RGBA8_Unorm)
        .setDimensions(1920, 1080)
        .initialize(fbLayout);
    std::array<DkImage const*, 2> fbArray;
    for (int i = 0; i < 2; i++)
    {
        GpuMemHeap::Allocation block = TextureHeap->Alloc(fbLayout.getSize(), fbLayout.getAlignment());
        Framebuffers[i].initialize(fbLayout, TextureHeap->MemBlock, block.Offset);
        fbArray[i] = &Framebuffers[i];
    }

    Swapchain = dk::SwapchainMaker{Device, Window, fbArray}.create();

    LoadShader("romfs:/shaders/Default_vsh.dksh", VertexShader);
    LoadShader("romfs:/shaders/Default_fsh.dksh", FragmentShader);
    LoadShader("romfs:/shaders/SmoothEdges_fsh.dksh", FragmentShaderSmoothEdges);

    for (int i = 0; i < 2; i++)
    {
        VertexData[i] = DataHeap->Alloc(MaxVertices * sizeof(Vertex), alignof(Vertex));
        IndexData[i] = DataHeap->Alloc(MaxIndices * sizeof(u16), alignof(u16));

        TextureStagingBuffer[i] = DataHeap->Alloc(1024*1024*4, DK_MEMBLOCK_ALIGNMENT);

        ImageDescriptors[i] = DataHeap->Alloc(sizeof(dk::ImageDescriptor) * 1024, DK_IMAGE_DESCRIPTOR_ALIGNMENT);
    }
    UniformBuffer = DataHeap->Alloc(sizeof(UniformBuffer), DK_UNIFORM_BUF_ALIGNMENT);

    {
        SamplerDescriptor = DataHeap->Alloc(sizeof(dk::SamplerDescriptor) * 4, DK_SAMPLER_DESCRIPTOR_ALIGNMENT);
        dk::SamplerDescriptor* descriptors = DataHeap->CpuAddr<dk::SamplerDescriptor>(SamplerDescriptor);

        for (int i = 0; i < 4; i++)
        {
            DkFilter filter = (i & sampler_FilterMask) == sampler_Linear ? DkFilter_Linear : DkFilter_Nearest;
            DkWrapMode wrapMode = (i & sampler_WrapMask) == sampler_Repeat ? DkWrapMode_Repeat : DkWrapMode_ClampToEdge;
            descriptors[i].initialize(dk::Sampler{}
                .setFilter(DkFilter_Linear, filter)
                .setWrapMode(wrapMode, wrapMode));
        }
    }

    // load system font
    plInitialize(PlServiceType_User);
    PlFontData font;

    plGetSharedFontByType(&font, PlSharedFontType_Standard);
    SystemFontStandardData = new u8[font.size];
    memcpy(SystemFontStandardData, font.address, font.size);
    SystemFontStandard = FontLoad(SystemFontStandardData);

    plGetSharedFontByType(&font, PlSharedFontType_NintendoExt);
    SystemFontNintendoExtData = new u8[font.size];
    memcpy(SystemFontNintendoExtData, font.address, font.size);
    SystemFontNintendoExt = FontLoad(SystemFontNintendoExtData);
    plExit();

    WhiteTexture = TextureCreate(8, 8, DkImageFormat_R8_Unorm);
    TextureSetSwizzle(WhiteTexture, DkImageSwizzle_One, DkImageSwizzle_One, DkImageSwizzle_One, DkImageSwizzle_One);
}

void DeInit()
{
    FontDelete(SystemFontNintendoExt);
    FontDelete(SystemFontStandard);
    delete[] SystemFontStandardData;
    delete[] SystemFontNintendoExtData;

    FontAtlas.Destroy();

    Queue.waitIdle();

    Swapchain.destroy();

    CmdBuf.destroy();
    Queue.destroy();

    TextureHeap->Destroy();
    ShaderCodeHeap->Destroy();
    DataHeap->Destroy();

    Device.destroy();
}

double AnimationTimestamp;
double AnimationTimestep, AnimationLastTimestamp;
bool DoSkipTimestep = false;

void SkipTimestep()
{
    DoSkipTimestep = true;
}

void Rotate90Deg(u32& outX, u32& outY, u32 inX, u32 inY, int rotation)
{
    switch (rotation)
    {
    case 0: outX = inX; outY = inY; break;
    case 1: outX = 1280 - inY; outY = inX; break;
    case 2: outX = 1280 - inX; outY = 720 - inY; break;
    case 3: outX = inY; outY = 720 - inX; break;
    }
}

void StartFrame()
{
    // make sure we can access the double buffered vertex and index buffers
    SwapchainSlot = Queue.acquireImage(Swapchain);

    AnimationTimestamp = armTicksToNs(armGetSystemTick()) * 0.000000001;
    if (!DoSkipTimestep)
    {
        AnimationTimestep = AnimationTimestamp - AnimationLastTimestamp;
    }
    else
    {
        DoSkipTimestep = false;
    }
    AnimationLastTimestamp = AnimationTimestamp;
}

void EndFrame(Color clearColor, int rotation)
{
    assert(ScissorStack.size() == 0);

    u32 fbPixelWidth, fbPixelHeight;
    if (appletGetOperationMode() == AppletOperationMode_Docked)
    {
        fbPixelWidth = 1920;
        fbPixelHeight = 1080;
    }
    else
    {
        fbPixelWidth = 1280;
        fbPixelHeight = 720;
    }
    Swapchain.setCrop(0, 0, fbPixelWidth, fbPixelHeight);

    FontAtlas.IssueUpload();

    CmdBuf.clear();
    CmdBuf.addMemory(DataHeap->MemBlock, CmdBufData[SwapchainSlot].Offset, CmdBufData[SwapchainSlot].Size);

    DkGpuAddr stagingBufferGpuAddr = DataHeap->GpuAddr(TextureStagingBuffer[SwapchainSlot]);
    u8* stagingBufferCpuAddr = DataHeap->CpuAddr<u8>(TextureStagingBuffer[SwapchainSlot]);
    for (u32 i = 0; i < TextureUploadsPending.size(); i++)
    {
        PendingTextureUpload& upload = TextureUploadsPending[i];
        Texture& texture = Textures[upload.TextureIdx];
        dk::ImageView view{texture.Image};
        memcpy(stagingBufferCpuAddr, upload.Data, upload.DataStrideBytes * upload.Height);
        CmdBuf.copyBufferToImage({stagingBufferGpuAddr, upload.DataStrideBytes, upload.Height}, view, {upload.X, upload.Y, 0, upload.Width, upload.Height, 1});

        u32 stride = upload.DataStrideBytes * upload.Height;
        stagingBufferCpuAddr += stride;
        stagingBufferGpuAddr += stride;
    }
    if (TextureUploadsPending.size() > 0)
    {
        CmdBuf.signalFence(ImageUploadFence[SwapchainSlot]);
        CmdBuf.waitFence(ImageUploadFence[SwapchainSlot]);
    }
    TextureUploadsPending.clear();

    dk::ImageView colorTarget{Framebuffers[SwapchainSlot]};
    CmdBuf.bindRenderTargets(&colorTarget);

    CmdBuf.setScissors(0, {{0, 0, fbPixelWidth, fbPixelHeight}});
    CmdBuf.setViewports(0, {{0, 0, (float)fbPixelWidth, (float)fbPixelHeight}});
    CmdBuf.clearColor(0, DkColorMask_RGBA, clearColor.R, clearColor.G, clearColor.B, clearColor.A);

    CmdBuf.bindColorState(dk::ColorState{}.setBlendEnable(0, true));
    CmdBuf.bindBlendStates(0, dk::BlendState{}
            .setColorBlendOp(DkBlendOp_Add)
            .setSrcColorBlendFactor(DkBlendFactor_SrcAlpha)
            .setDstColorBlendFactor(DkBlendFactor_InvSrcAlpha));

    CmdBuf.bindVtxBuffer(0, DataHeap->GpuAddr(VertexData[SwapchainSlot]), VertexData[SwapchainSlot].Size);
    CmdBuf.bindVtxAttribState({
        DkVtxAttribState{0, 0, offsetof(Vertex, Position), DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0},
        DkVtxAttribState{0, 0, offsetof(Vertex, UV), DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0},
        DkVtxAttribState{0, 0, offsetof(Vertex, Color), DkVtxAttribSize_4x8, DkVtxAttribType_Unorm, 0},
    });
    CmdBuf.bindVtxBufferState({{sizeof(Vertex), 0}});
    CmdBuf.bindIdxBuffer(DkIdxFormat_Uint16, DataHeap->GpuAddr(IndexData[SwapchainSlot]));

    {
        CmdBuf.bindUniformBuffer(DkStage_Vertex, 0, DataHeap->GpuAddr(UniformBuffer), UniformBuffer.Size);
        Transformation transformation;
        xm4_orthographic(transformation.Projection, -1280.f/2, 1280.f/2, 720.f/2.f, -720.f/2, -1.f, 1.f);
        float rot[16];
        float trans[16];
        int screenWidth = 1280, screenHeight = 720;
        if ((rotation % 2) == 1)
            std::swap(screenWidth, screenHeight);
        xm4_translatev(trans, -screenWidth/2, -screenHeight/2, 0.f);
        xm4_rotatef(rot, -M_PI_2 * rotation, 0.f, 0.f, 1.f);
        xm4_mul(rot, trans, rot);
        xm4_mul(transformation.Projection, rot, transformation.Projection);
        CmdBuf.pushConstants(DataHeap->GpuAddr(UniformBuffer), UniformBuffer.Size, 0, sizeof(Transformation), &transformation);
    }

    memcpy(DataHeap->CpuAddr<void>(VertexData[SwapchainSlot]), VertexDataClient, sizeof(Vertex)*CurClientVertex);
    memcpy(DataHeap->CpuAddr<void>(IndexData[SwapchainSlot]), IndexDataClient, sizeof(u16)*CurClientIndex);
    CurClientVertex = CurClientIndex = 0;

    CmdBuf.bindSamplerDescriptorSet(DataHeap->GpuAddr(SamplerDescriptor), 4);
    CmdBuf.bindImageDescriptorSet(DataHeap->GpuAddr(ImageDescriptors[SwapchainSlot]), 1024);

    assert(UsedTextures.size() <= 1024 && "use less textures at once!");
    dk::ImageDescriptor* imageDescriptors = DataHeap->CpuAddr<dk::ImageDescriptor>(ImageDescriptors[SwapchainSlot]);
    for (u32 i = 0; i < UsedTextures.size(); i++)
    {
        Texture& texture = Textures[UsedTextures[i]];

        dk::ImageView view{texture.Image};
        view.setSwizzle(texture.ComponentSwizzle[0], texture.ComponentSwizzle[1], texture.ComponentSwizzle[2], texture.ComponentSwizzle[3]);
        imageDescriptors[i].initialize(view);
    }

    u32 indexBufferOffset = 0;
    for (u32 i = 0; i < DrawCalls.size(); i++)
    {
        DrawCall& drawCall = DrawCalls[i];
        if (drawCall.Dirty & drawCallDirty_Scissor)
        {
            // hacky
            DkScissor scissor = drawCall.Scissor;
            u32 x1, y1, x2, y2;
            Rotate90Deg(x1, y1, scissor.x, scissor.y, rotation);
            Rotate90Deg(x2, y2, scissor.x + scissor.width, scissor.y + scissor.height, rotation);

            scissor.x = std::min(x1, x2);
            scissor.y = std::min(y1, y2);
            scissor.width = abs((s32)x1 - (s32)x2);
            scissor.height = abs((s32)y1 - (s32)y2);
            if (fbPixelHeight == 1080)
            {
                scissor.x = scissor.x * 3 / 2;
                scissor.y = scissor.y * 3 / 2;
                scissor.width = scissor.width * 3 / 2;
                scissor.height = scissor.height * 3 / 2;
            }
            CmdBuf.setScissors(0, {scissor});
        }
        if (drawCall.Dirty & (drawCallDirty_Texture|drawCallDirty_Sampler))
        {
            CmdBuf.bindTextures(DkStage_Fragment, 0,
                dkMakeTextureHandle(Textures[drawCall.TextureIdx].ImageDescriptorIdx, drawCall.Sampler));
        }
        if (drawCall.Dirty & drawCallDirty_Shader)
        {
            CmdBuf.bindShaders(DkStageFlag_GraphicsMask, {&VertexShader, drawCall.SmoothEdges
                ? &FragmentShaderSmoothEdges
                : &FragmentShader});
        }

        CmdBuf.drawIndexed(DkPrimitive_Triangles, drawCall.Count, 1, indexBufferOffset, 0, 0);
        indexBufferOffset += drawCall.Count;
    }

    Queue.submitCommands(CmdBuf.finishList());
    Queue.presentImage(Swapchain, SwapchainSlot);

    DrawCalls.clear();
    for (u32 i = 0; i < UsedTextures.size(); i++)
        Textures[UsedTextures[i]].ImageDescriptorIdx = -1;
    UsedTextures.clear();
}

void IssueDrawCall(u32 texture, u32 count)
{
    u32 dirty = 0;
    DkScissor& curScissor = ScissorStack[ScissorStack.size() - 1];
    if (DrawCalls.size() > 0)
    {
        DrawCall& prevDrawCall = DrawCalls[DrawCalls.size() - 1];
        if (prevDrawCall.TextureIdx != texture)
            dirty |= drawCallDirty_Texture;
        if (prevDrawCall.Sampler != CurSampler)
            dirty |= drawCallDirty_Sampler;
        if (prevDrawCall.SmoothEdges != SmoothEdges)
            dirty |= drawCallDirty_Shader;

        if (prevDrawCall.Scissor.x != curScissor.x
            || prevDrawCall.Scissor.y != curScissor.y
            || prevDrawCall.Scissor.width != curScissor.width
            || prevDrawCall.Scissor.height != curScissor.height)
        {
            dirty |= drawCallDirty_Scissor;
        }
    }
    else
    {
        dirty = ~0;
    }

    if (Textures[texture].ImageDescriptorIdx == -1)
    {
        Textures[texture].ImageDescriptorIdx = UsedTextures.size();
        UsedTextures.push_back(texture);
    }

    if (dirty)
        DrawCalls.push_back({dirty, texture, CurSampler, count, curScissor, SmoothEdges});
    else
        DrawCalls[DrawCalls.size() - 1].Count += count;
}

void SetSampler(u32 sampler)
{
    CurSampler = sampler;
}

void SetSmoothEdges(bool enabled)
{
    SmoothEdges = enabled;
}

void DrawRectangle(u32 texIdx, 
    Vector2f position, Vector2f size,
    Vector2f subPosition, Vector2f subSize,
    Color tint)
{
    Texture& texture = Textures[texIdx];

    Vector2f rcpTexSize{1.f / texture.Width, 1.f / texture.Height};
    Vector2f uvMin = subPosition * rcpTexSize;
    Vector2f uvMax = uvMin + subSize * rcpTexSize;

    u8 tintR8 = tint.R * 255;
    u8 tintG8 = tint.G * 255;
    u8 tintB8 = tint.B * 255;
    u8 tintA8 = tint.A * 255;

    Vector2f outerBounds = position + size;

    VertexDataClient[CurClientVertex + 0] = {position.X, position.Y, uvMin.X, uvMin.Y, tintR8, tintG8, tintB8, tintA8};
    VertexDataClient[CurClientVertex + 1] = {outerBounds.X, position.Y, uvMax.X, uvMin.Y, tintR8, tintG8, tintB8, tintA8};
    VertexDataClient[CurClientVertex + 2] = {position.X, outerBounds.Y, uvMin.X, uvMax.Y, tintR8, tintG8, tintB8, tintA8};
    VertexDataClient[CurClientVertex + 3] = {outerBounds.X, outerBounds.Y, uvMax.X, uvMax.Y, tintR8, tintG8, tintB8, tintA8};

    IndexDataClient[CurClientIndex + 0] = CurClientVertex;
    IndexDataClient[CurClientIndex + 1] = CurClientVertex + 2;
    IndexDataClient[CurClientIndex + 2] = CurClientVertex + 3;
    IndexDataClient[CurClientIndex + 3] = CurClientVertex;
    IndexDataClient[CurClientIndex + 4] = CurClientVertex + 3;
    IndexDataClient[CurClientIndex + 5] = CurClientVertex + 1;

    IssueDrawCall(texIdx, 6);

    CurClientVertex += 4;
    CurClientIndex += 6;
}

void DrawRectangle(Vector2f position, Vector2f size, Color tint)
{
    DrawRectangle(WhiteTexture, position, size, Vector2f{}, Vector2f{}, tint);
}

void DrawRectangle(u32 texIdx, Vector2f position, Vector2f size, Vector2f subPosition, Color tint)
{
    DrawRectangle(texIdx, position, size, subPosition, subPosition + size, tint);
}

void DrawRectangle(u32 texIdx,
    Vector2f p0, Vector2f p1, Vector2f p2, Vector2f p3,
    Vector2f subPosition, Vector2f subSize)
{
    Texture& texture = Textures[texIdx];

    Vector2f rcpTexSize{1.f / texture.Width, 1.f / texture.Height};
    Vector2f uvMin = subPosition * rcpTexSize;
    Vector2f uvMax = uvMin + subSize * rcpTexSize;

    VertexDataClient[CurClientVertex + 0] = {p0.X, p0.Y, uvMin.X, uvMin.Y, 255, 255, 255, 255};
    VertexDataClient[CurClientVertex + 1] = {p1.X, p1.Y, uvMax.X, uvMin.Y, 255, 255, 255, 255};
    VertexDataClient[CurClientVertex + 2] = {p2.X, p2.Y, uvMin.X, uvMax.Y, 255, 255, 255, 255};
    VertexDataClient[CurClientVertex + 3] = {p3.X, p3.Y, uvMax.X, uvMax.Y, 255, 255, 255, 255};

    IndexDataClient[CurClientIndex + 0] = CurClientVertex;
    IndexDataClient[CurClientIndex + 1] = CurClientVertex + 2;
    IndexDataClient[CurClientIndex + 2] = CurClientVertex + 3;
    IndexDataClient[CurClientIndex + 3] = CurClientVertex;
    IndexDataClient[CurClientIndex + 4] = CurClientVertex + 3;
    IndexDataClient[CurClientIndex + 5] = CurClientVertex + 1;

    IssueDrawCall(texIdx, 6);

    CurClientVertex += 4;
    CurClientIndex += 6;
}

Vector2f DrawText(u32 fontIdx, Vector2f position, float size, Color color, int horizontalAlign, int verticalAlign, const char* text)
{
    float scale = FontGetScale(fontIdx, size);
    float ascent = FontGetAscent(fontIdx, scale);
    float lineGap = FontGetLineGap(fontIdx, scale);

    SetSampler(sampler_Linear|sampler_ClampToEdge);

    if (verticalAlign != align_Left || horizontalAlign != align_Left)
    {
        u32 lines = 1;
        float lineWidth = 0.f, textWidth = 0.f;
        const char* textPtr = text;
        while (*textPtr)
        {
            u32 codepoint;
            textPtr += decode_utf8(&codepoint, (u8*)textPtr);

            if (codepoint == '\n')
            {
                lines++;
                textWidth = std::max(textWidth, lineWidth);
                lineWidth = 0.f;
            }
            else if (horizontalAlign != align_Left)
            {
                lineWidth += FontGetGlyph(fontIdx, codepoint, scale).AdvanceWidth;
            }
        }
        textWidth = std::max(textWidth, lineWidth);

        float textHeight = lines * size + lineGap * (lines - 1);

        if (horizontalAlign == align_Right)
            position.X -= textWidth;
        else
            position.X -= textWidth / 2.f;

        if (verticalAlign == align_Right)
            position.Y -= textHeight;
        else
            position.Y -= textHeight / 2.f;
    }

    Vector2f offset;
    Vector2f bounds{0.f, size};

    while (*text)
    {
        u32 codepoint;
        text += decode_utf8(&codepoint, (u8*)text);

        if (codepoint == '\n')
        {
            bounds.X = std::max(bounds.X, offset.X);
            bounds.Y += size + lineGap;

            offset.X = 0.f;
            offset.Y += size + lineGap;
        }
        else
        {
            PackedGlyph& glyph = FontGetGlyph(fontIdx, codepoint, scale);

            if (codepoint != ' ')
            {
                int glyphWidth = glyph.BoxX2 - glyph.BoxX1;
                int glyphHeight = glyph.BoxY2 - glyph.BoxY1;

                DrawRectangle(glyph.Quad.AtlasTexture,
                    position + offset + Vector2f{(float)glyph.BoxX1, (float)glyph.BoxY1 + ascent},
                    {(float)glyphWidth, (float)glyphHeight},
                    {(float)glyph.Quad.PackX, (float)glyph.Quad.PackY},
                    {(float)glyphWidth, (float)glyphHeight},
                    color);
            }
            offset.X += glyph.AdvanceWidth;
        }
    }

    bounds.X = std::max(bounds.X, offset.X);

    return bounds;
}

Vector2f DrawText(u32 fontIdx, Vector2f position, float size, Color color, const char* format, ...)
{
    va_list vargs;
    va_start(vargs, format);
    int requiredLength = vsnprintf(NULL, 0, format, vargs) + 1;
    char formattedText[requiredLength];
    vsnprintf(formattedText, requiredLength, format, vargs);
    va_end(vargs);

    return DrawText(fontIdx, position, size, color, align_Left, align_Left, formattedText);
}

}