#ifndef ROMMETADATABASE_H
#define ROMMETADATABASE_H

#include "Gfx.h"

#include <vector>

namespace ROMMetaDatabase
{

enum
{
    titleLang_Japanese = 0,
    titleLang_English,
    titleLang_French,
    titleLang_German,
    titleLang_Italian,
    titleLang_Spanish,
    titlesLang_Count,
};

struct ROMMeta
{
    u32 TitlesOffset[titlesLang_Count];
    Gfx::PackedQuad Icon;
    bool HasIcon;

    const char* Title(int lang);
};


extern Gfx::Atlas IconAtlas;
extern std::vector<ROMMeta> Database;

extern int TitleLanguage;

int QueryMeta(const char* romname, const char* path);

void Init();

void UpdateTexture();

}

#endif