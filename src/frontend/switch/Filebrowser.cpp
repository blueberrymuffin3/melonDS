// for strcasestr
#define _GNU_SOURCE
#include <string.h>

#include "Filebrowser.h"

#include "Style.h"
#include "ROMMetaDatabase.h"
#include "BackButton.h"
#include "KeyExplanations.h"

#include "PlatformConfig.h"
#include "main.h"

#include <vector>
#include <string>
#include <dirent.h>

namespace Filebrowser
{

struct Entry
{
    int Name, ROMDBEntry;
    bool IsDirectory;
};
std::vector<char> CurrentEntryNames;
std::vector<Entry> CurrentEntries;
std::string CurrentPath;
std::string SearchText;
int CurrentSelection = -1;

int TitleLanguage;

void Init()
{
    InferSystemLanguage();

    EnterDirectory(Config::LastROMFolder);
}

void DeInit()
{
    strcpy(Config::LastROMFolder, CurrentPath.c_str());
}

void InferSystemLanguage()
{
    u64 langCode;
    setGetSystemLanguage(&langCode);
    char langCodeStr[8];
    memcpy(langCodeStr, &langCode, 8);
    if (!strcmp(langCodeStr, "ja"))
        TitleLanguage = ROMMetaDatabase::titleLang_Japanese;
    else if (!strcmp(langCodeStr, "fr") || !strcmp(langCodeStr, "fr-CA"))
        TitleLanguage = ROMMetaDatabase::titleLang_French;
    else if (!strcmp(langCodeStr, "de"))
        TitleLanguage = ROMMetaDatabase::titleLang_German;
    else if (!strcmp(langCodeStr, "it"))
        TitleLanguage = ROMMetaDatabase::titleLang_Italian;
    else if (!strcmp(langCodeStr, "es") || !strcmp(langCodeStr, "es-419"))
        TitleLanguage = ROMMetaDatabase::titleLang_Spanish;
    else // the rest of you get English
        TitleLanguage = ROMMetaDatabase::titleLang_English;
}

int PushString(std::vector<char>& container, const char* str)
{
    u32 stringLength = strlen(str);
    u32 offset = container.size();
    container.resize(offset + stringLength + 1);
    memcpy(&container[offset], str, stringLength + 1);
    return offset;
}

const char* FileBrowserPrefix = "filebrowser_entries";

void EnterDirectory(const char* path)
{
    BoxGui::ForceSelecton(BoxGui::MakeUniqueName(FileBrowserPrefix, 0));
    CurrentSelection = 0;

    DIR* dir = opendir(path);
    if (dir == nullptr)
    {
        path = "/";
        dir = opendir(path);
    }

    CurrentPath = path;

    CurrentEntryNames.clear();
    CurrentEntries.clear();
    if (CurrentPath != "/")
        CurrentEntries.push_back({PushString(CurrentEntryNames, ".."), -1, true});

    struct dirent* cur;
    while (cur = readdir(dir))
    {
        int nameLen = strlen(cur->d_name);
        if (nameLen == 1 && cur->d_name[0] == '.')
            continue;

        Entry entry;
        entry.ROMDBEntry = -1; 
        if (cur->d_type == DT_REG)
        {
            if (nameLen < 4)
                continue;

            const char* extension = cur->d_name + nameLen - 4;
            int notNdsFile = strcmp(extension, ".nds");
            int notBinFile = strcmp(extension, ".bin");
            if (notNdsFile && notBinFile)
                continue;
            // we'll get to you later
            /*int notGbaFile = strcmp(extension, ".gba");
            if (notGbaFile)
                continue;*/

            if (!notNdsFile)
                entry.ROMDBEntry = ROMMetaDatabase::QueryMeta(cur->d_name, CurrentPath.c_str());
        }
        else if (cur->d_type != DT_DIR)
        {
            continue;
        }

        entry.Name = PushString(CurrentEntryNames, cur->d_name);
        entry.IsDirectory = cur->d_type == DT_DIR;
        CurrentEntries.push_back(entry);
    }

    closedir(dir);

    std::sort(CurrentEntries.begin(), CurrentEntries.end(), [](const Entry& a, const Entry& b)
    {
        if (a.IsDirectory == b.IsDirectory)
            return strcasecmp(&CurrentEntryNames[a.Name], &CurrentEntryNames[b.Name]) < 0;
        else
            return a.IsDirectory;
    });
}

void GoUpwards()
{
    std::string newPath = CurrentPath;
    if (newPath != "/")
    {
        newPath.resize(newPath.rfind('/'));
        if (newPath.size() == 0)
            newPath = "/";
        EnterDirectory(newPath.c_str());
    }
}

void EnterDirectory(u32 entry)
{
    std::string newPath = CurrentPath;
    if (newPath != "/")
        newPath += '/';
    newPath += &CurrentEntryNames[CurrentEntries[entry].Name];
    EnterDirectory(newPath.c_str());
}

int FindNextOccurence(const char* searchText)
{
    if (CurrentEntries.size() == 0)
        return -1;
    if (CurrentSelection == -1)
        CurrentSelection = 0;
    int i = CurrentSelection + 1;
    if (i == CurrentEntries.size())
        i = 0;
    while (i != CurrentSelection)
    {
        // if there's a proper title first search it
        if (CurrentEntries[i].ROMDBEntry != -1
            && strcasestr(ROMMetaDatabase::Database[CurrentEntries[i].ROMDBEntry].Title(TitleLanguage),
                searchText))
            return i;

        // always search by filename as well
        // this saves us a bunch of work, otherwise people would complain if they can't
        // find Pokémon when searching without the accent haha
        if (strcasestr(&CurrentEntryNames[CurrentEntries[i].Name], searchText))
            return i;

        i++;
        if (i == CurrentEntries.size())
            i = 0;
    }

    return -1;
}

std::string CurrentSelectionPath()
{
    std::string result(CurrentPath);
    result += '/';
    result += &CurrentEntryNames[CurrentEntries[CurrentSelection].Name];
    return result;
}

SwkbdTextCheckResult CheckSearch(char* tmpString, size_t tmpSize)
{
    if (FindNextOccurence(tmpString) == -1)
    {
        strcpy(tmpString, "Couldn't find any matching file or directory :(");
        return SwkbdTextCheckResult_Prompt;
    }
    return SwkbdTextCheckResult_OK;
}

void DoGui(BoxGui::Frame& parent)
{
    BackButton::DoGui(parent, CurrentPath.c_str());

    BoxGui::Frame entriesFrame{parent, {{0.f, BackButtonHeight}, {parent.Area.Size.X, parent.Area.Size.Y-BackButtonHeight}},
        {0.f, 0.f}, {0.f, 0.f},
        1, BoxGui::MakeUniqueName(FileBrowserPrefix, -1),
        false, true};
    BoxGui::Skewer entrySkewer{entriesFrame, 0.f, BoxGui::direction_Vertical};

    Gfx::PushScissor(entriesFrame.Area.Position.X, entriesFrame.Area.Position.Y, entriesFrame.Area.Size.X, entriesFrame.Area.Size.Y);

    CurrentSelection = -1;
    for (int i = 0; i < CurrentEntries.size(); i++)
    {
        Entry& entry = CurrentEntries[i];

        BoxGui::Frame entryFrame{entriesFrame, entrySkewer.Spit({entriesFrame.Area.Size.X, UIRowHeight}, Gfx::align_Right), {5.f, 5.f}, {5.f, 5.f}};

        if (BoxGui::InputElement(entryFrame, BoxGui::MakeUniqueName(FileBrowserPrefix, i)))
            CurrentSelection = i;

        if (!entryFrame.IsVisible())
            continue;

        Gfx::DrawRectangle(entryFrame.Area.Position - Gfx::Vector2f{0.f, 5.f}, entryFrame.Area.Size + Gfx::Vector2f{0.f, 5.f*2.f}, WidgetColorBright);
        if (CurrentSelection == i)
            Gfx::DrawRectangle(entryFrame.Area.Position, entryFrame.Area.Size, WidgetColorVibrant);

        BoxGui::Skewer skewer{entryFrame, entryFrame.Area.Size.Y/2.f, BoxGui::direction_Horizontal};
        skewer.AlignLeft(20.f);

        if (!entry.IsDirectory && entry.ROMDBEntry != -1)
        {
            ROMMetaDatabase::ROMMeta& meta = ROMMetaDatabase::Database[entry.ROMDBEntry];
            if (meta.HasIcon)
            {
                BoxGui::Frame imageFrame{entryFrame, skewer.Spit({entryFrame.Area.Size.Y * 0.8f, entryFrame.Area.Size.Y * 0.8f})};
                Gfx::DrawRectangle(meta.Icon.AtlasTexture, 
                    imageFrame.Area.Position, imageFrame.Area.Size, 
                    {(float)meta.Icon.PackX, (float)meta.Icon.PackY}, {32.f, 32.f},
                    {1.f, 1.f, 1.f, 1.f});
            }

            skewer.Advance(20.f); 

            Gfx::DrawText(Gfx::SystemFontStandard, skewer.CurrentPosition(), TextLineHeight, DarkColor, Gfx::align_Left, Gfx::align_Center, meta.Title(TitleLanguage));
        }
        else
        {
            Gfx::DrawText(Gfx::SystemFontStandard, skewer.CurrentPosition(), TextLineHeight, DarkColor, Gfx::align_Left, Gfx::align_Center, &CurrentEntryNames[entry.Name]);
        }

        if (i > 0)
        {
            // draw separator
            Gfx::DrawRectangle(entryFrame.Area.Position + Gfx::Vector2f{10.f, -(5.f + 1.f)},
                {entryFrame.Area.Size.X - 2*10.f, 2.f},
                SeparatorColor);
        }
    }

    Gfx::PopScissor();

    KeyExplanation::Explain(KeyExplanation::button_Y, "Search");
    if (BoxGui::SearchPressed())
    {
        SwkbdConfig keyboard;
        swkbdCreate(&keyboard, 0);
    
        swkbdConfigSetOkButtonText(&keyboard, "Go");
        swkbdConfigSetHeaderText(&keyboard, "Enter text to search for");
        swkbdConfigSetTextCheckCallback(&keyboard, CheckSearch);
        swkbdConfigSetInitialCursorPos(&keyboard, 1); // 1 = end
        swkbdConfigSetInitialText(&keyboard, SearchText.c_str());
        swkbdConfigSetBlurBackground(&keyboard, 1);

        char searchText[256];
        Result r = swkbdShow(&keyboard, searchText, 256);
        SearchText = searchText;
        Gfx::SkipTimestep();

        if (R_SUCCEEDED(r))
        {
            int newSelection = FindNextOccurence(SearchText.c_str());
            if (newSelection != -1)
            {
                CurrentSelection = newSelection;
                BoxGui::ForceSelecton(BoxGui::MakeUniqueName(FileBrowserPrefix, newSelection), false);
            }
        }

        swkbdClose(&keyboard);
    }

    if (CurrentSelection != -1)
    {    
        if (BoxGui::LeftPressed())
        {
            BoxGui::ForceSelecton(BoxGui::MakeUniqueName(FileBrowserPrefix,
                std::max(CurrentSelection - 5, 0)), true);
        }
        if (BoxGui::RightPressed())
        {
            BoxGui::ForceSelecton(BoxGui::MakeUniqueName(FileBrowserPrefix,
                std::min(CurrentSelection + 5, (int)CurrentEntries.size() - 1)), true);
        }

        if (CurrentEntries[CurrentSelection].IsDirectory)
        {
            KeyExplanation::Explain(KeyExplanation::button_A, "Enter");

            if (BoxGui::ConfirmPressed())
            {
                if (CurrentSelection == 0)
                    GoUpwards();
                else
                    EnterDirectory(CurrentSelection);
            }
        }
        else
        {
            KeyExplanation::Explain(KeyExplanation::button_A, "Start");   

            if (BoxGui::ConfirmPressed())
            {
                std::string romPath = CurrentSelectionPath();
                Emulation::LoadROM(romPath.c_str());
            }
        }
    
        if (CurrentPath != "/")
        {
            KeyExplanation::Explain(KeyExplanation::button_B, "Upwards");
    
            if (BoxGui::CancelPressed())
            {
                BoxGui::ForceSelecton(BoxGui::MakeUniqueName(FileBrowserPrefix, 0), false);
            }
        }
    }

    ROMMetaDatabase::IconAtlas.IssueUpload();
}

}
