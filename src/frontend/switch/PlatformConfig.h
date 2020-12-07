#ifndef PLATFORMCONFIG_H
#define PLATOFRMCONFIG_H

#include "../../Config.h"

namespace Config
{

extern int GlobalRotation;

extern int TouchscreenMode;
extern int TouchscreenClickMode;

extern int ScreenRotation;
extern int ScreenGap;
extern int ScreenLayout;
extern int ScreenSizing;
extern int Filtering;
extern int IntegerScaling;

extern char LastROMFolder[512];

extern int SwitchOverclock;

extern int ConsoleType;
extern int DirectBoot;

extern int LimitFramerate;

extern int ShowPerformanceMetrics;

}

#endif