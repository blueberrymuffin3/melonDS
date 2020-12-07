#include "PlatformConfig.h"

namespace Config
{

int GlobalRotation;

int TouchscreenMode;
int TouchscreenClickMode;

int ScreenRotation;
int ScreenGap;
int ScreenLayout;
int ScreenSizing;
int Filtering;
int IntegerScaling;

char LastROMFolder[512];

int SwitchOverclock;

int ConsoleType;
int DirectBoot;

int LimitFramerate;

int ShowPerformanceMetrics;

ConfigEntry PlatformConfigFile[] =
{
    {"GlobalRotation",          0, &GlobalRotation,         0, NULL, 0},
    
    {"TouchscreenMode",         0, &TouchscreenMode,        0, NULL, 0},
    {"TouchscreenClickMode",    0, &TouchscreenClickMode,   0, NULL, 0},

    {"ScreenRotation",          0, &ScreenRotation,         0, NULL, 0},
    {"ScreenGap",               0, &ScreenGap,              0, NULL, 0},
    {"ScreenLayout",            0, &ScreenLayout,           0, NULL, 0},
    {"ScreenSizing",            0, &ScreenSizing,           0, NULL, 0},
    {"Filtering",               0, &Filtering,              1, NULL, 0},
    {"IntegerScaling",          0, &IntegerScaling,         0, NULL, 0},

    {"LastROMFolder",           1, LastROMFolder,           0, "/", 511},

    {"SwitchOverclock",         0, &SwitchOverclock,        0, NULL, 0},

    {"ConsoleType",             0, &ConsoleType,            0, NULL, 0},
    {"DirectBoot",              0, &DirectBoot,             1, NULL, 0},

    {"ShowPerformanceMetrics",  0, &ShowPerformanceMetrics, 0, NULL, 0},

    {"LimitFramerate",          0, &LimitFramerate,         1, NULL, 0},

    {"", -1, NULL, 0, NULL, 0}
};

}