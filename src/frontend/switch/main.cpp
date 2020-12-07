#include "main.h"

#include <stdio.h>
#include <dirent.h>
#include <assert.h>
#include <malloc.h>
#include <atomic>

#include <switch.h>

#include "NDS.h"
#include "GPU.h"
#include "SPU.h"
#include "FrontendUtil.h"
#include "Config.h"

#include "PlatformConfig.h"

#include "Gfx.h"
#include "BoxGui.h"

#include "Style.h"
#include "Filebrowser.h"
#include "StartMenu.h"
#include "KeyExplanations.h"
#include "SettingsDialog.h"

#include "mm_vec/mm_vec.h"

#include <string.h>

bool Done = false;
int CurrentUiScreen = uiScreen_Start;

namespace Emulation
{

u32 KeyMask;
u64 PlatformKeysHeld;

u32 FramebufferTextures[2];
// we double buffer a doubled buffered variable, yes
int FrontBuffer = 0;
bool NewFrameReady = false;
u32 Framebuffers[2][2][256*192];

int FrametimeHistogramNextValue;
float FrametimeHistogram[120];

Gfx::Vector2f ScreenPoints[2][4];

Mutex EmuThreadLock;
CondVar FrameStartCond;
Thread EmuThread, AudioThread;

int State;
std::atomic<int> StateInternal;
std::atomic<int> StateAtomic;

const int AudioFrameSize = 768 * 2 * sizeof(s16);
AudioDriver AudioDrv;
void* AudMemPool = NULL;

bool LimitFramerate;

bool MissedFrame;

bool LidClosed;

bool TouchHeld;
Gfx::Vector2f TouchCursorVelocity;
Gfx::Vector2f TouchCursorPosition;

bool TouchDown;
int TouchFinalPositionX, TouchFinalPositionY;

u32 JoyconSixaxisHandles[2];
u32 ConsoleSixAxisHandle;

float GyroCalibrationUp[3];
float GyroCalibrationRight[3];
float GyroCalibrationForward[3];

void RecalibrateGyro(SixAxisSensorValues& values)
{
    GyroCalibrationRight[0] = values.orientation[0].x;
    GyroCalibrationRight[1] = values.orientation[0].y;
    GyroCalibrationRight[2] = values.orientation[0].z;
    GyroCalibrationForward[0] = values.orientation[1].x;
    GyroCalibrationForward[1] = values.orientation[1].y;
    GyroCalibrationForward[2] = values.orientation[1].z;
    GyroCalibrationUp[0] = values.orientation[2].x;
    GyroCalibrationUp[1] = values.orientation[2].y;
    GyroCalibrationUp[2] = values.orientation[2].z;

    xv_norm(GyroCalibrationUp, GyroCalibrationUp, 3);
    xv_norm(GyroCalibrationRight, GyroCalibrationRight, 3);
    xv_norm(GyroCalibrationForward, GyroCalibrationForward, 3);
}

void AudioOutput(void *args)
{
    int internalState = StateAtomic;

    AudioDriverWaveBuf buffers[2];
    memset(&buffers[0], 0, sizeof(AudioDriverWaveBuf) * 2);
    for (int i = 0; i < 2; i++)
    {
        buffers[i].data_pcm16 = (s16*)AudMemPool;
        buffers[i].size = AudioFrameSize;
        buffers[i].start_sample_offset = i * AudioFrameSize / 2 / sizeof(s16);
        buffers[i].end_sample_offset = buffers[i].start_sample_offset + AudioFrameSize / 2 / sizeof(s16);
    }

    while (internalState != emuState_Quit)
    {
        while (internalState != emuState_Running && internalState != emuState_Quit)
        {
            svcSleepThread(17000000); // a bit more than a frame...
            internalState = StateAtomic;
        }
        while (internalState == emuState_Running)
        {
            AudioDriverWaveBuf* refillBuf = NULL;
            for (int i = 0; i < 2; i++)
            {
                if (buffers[i].state == AudioDriverWaveBufState_Free || buffers[i].state == AudioDriverWaveBufState_Done)
                {
                    refillBuf = &buffers[i];
                    break;
                }
            }

            if (refillBuf)
            {
                s16* data = (s16*)AudMemPool + refillBuf->start_sample_offset * 2;

                int nSamples = 0;
                while (internalState == emuState_Running && !(nSamples = SPU::ReadOutput(data, 768)))
                {
                    svcSleepThread(10000);
                    internalState = StateAtomic;
                }

                u32 last = ((u32*)data)[nSamples - 1];
                while (nSamples < 768)
                    ((u32*)data)[nSamples++] = last;

                armDCacheFlush(data, nSamples * 2 * sizeof(u16));
                refillBuf->end_sample_offset = refillBuf->start_sample_offset + nSamples;

                audrvVoiceAddWaveBuf(&AudioDrv, 0, refillBuf);
                audrvVoiceStart(&AudioDrv, 0);
            }

            audrvUpdate(&AudioDrv);
            audrenWaitFrame();

            internalState = StateAtomic;
        }
    }
}

void EmuThreadFunc(void*)
{    
    NDS::Init();

    GPU::InitRenderer(0);
    GPU::RenderSettings settings{true, 1, false};
    GPU::SetRenderSettings(0, settings);

    bool missedFrame = false;

    while (StateAtomic != emuState_Quit)
    {
        StateInternal = StateAtomic.load(std::memory_order_acquire);

        if (StateInternal.load(std::memory_order_relaxed) == emuState_Running)
        {
            mutexLock(&EmuThreadLock);
            if (!missedFrame && LimitFramerate)
            {
                condvarWait(&FrameStartCond, &EmuThreadLock);
                if (StateAtomic != emuState_Running)
                {
                    mutexUnlock(&EmuThreadLock);
                    continue;
                }
            }
            NDS::SetKeyMask(KeyMask);
            if (TouchDown)
                NDS::TouchScreen((u16)TouchFinalPositionX, (u16)TouchFinalPositionY);
            else
                NDS::ReleaseScreen();
            if (LidClosed != NDS::IsLidClosed())
                NDS::SetLidClosed(LidClosed);
            mutexUnlock(&EmuThreadLock);

            u64 frameStart = armGetSystemTick();

            NDS::RunFrame();

            u64 frameLength = armTicksToNs(armGetSystemTick() - frameStart);
            missedFrame = frameLength > 16 * 1000 * 1000;

            mutexLock(&EmuThreadLock);
            MissedFrame = missedFrame;

            FrametimeHistogram[FrametimeHistogramNextValue] = (float)frameLength * 0.000001f;
            FrametimeHistogramNextValue++;
            if (FrametimeHistogramNextValue >= 120)
                FrametimeHistogramNextValue = 0;

            if (GPU::Framebuffer[GPU::FrontBuffer][0] && GPU::Framebuffer[GPU::FrontBuffer][1])
            {
                memcpy(Framebuffers[FrontBuffer ^ 1][0], GPU::Framebuffer[GPU::FrontBuffer][0], 256*192*4);
                memcpy(Framebuffers[FrontBuffer ^ 1][1], GPU::Framebuffer[GPU::FrontBuffer][1], 256*192*4);
            }

            NewFrameReady = true;

            mutexUnlock(&EmuThreadLock);
        }
        else
        {
            svcSleepThread(1000 * 1000);
            missedFrame = false;
        }
    }

    GPU::DeInitRenderer();
    NDS::DeInit();
}

void Init()
{
    mutexInit(&EmuThreadLock);
    condvarInit(&FrameStartCond);

    threadCreate(&EmuThread, EmuThreadFunc, NULL, NULL, 1024*1024*8, 0x20, 1);
    threadStart(&EmuThread);

    for (u32 i = 0; i < 2; i++)
        FramebufferTextures[i] = Gfx::TextureCreate(256, 192, DkImageFormat_BGRX8_Unorm);

    UpdateScreenLayout();

    const AudioRendererConfig arConfig =
    {
        .output_rate     = AudioRendererOutputRate_48kHz,
        .num_voices      = 4,
        .num_effects     = 0,
        .num_sinks       = 1,
        .num_mix_objs    = 1,
        .num_mix_buffers = 2,
    };

    Result code;
    if (!R_SUCCEEDED(code = audrenInitialize(&arConfig)))
    {
        printf("audren init failed! %d\n", code);
        abort();
    }

    if (!R_SUCCEEDED(code = audrvCreate(&AudioDrv, &arConfig, 2)))
    {
        printf("audrv create failed! %d\n", code);
        abort();
    }

    const int poolSize = (AudioFrameSize * 2 + (AUDREN_MEMPOOL_ALIGNMENT-1)) & ~(AUDREN_MEMPOOL_ALIGNMENT-1);
    AudMemPool = memalign(AUDREN_MEMPOOL_ALIGNMENT, poolSize);

    int mpid = audrvMemPoolAdd(&AudioDrv, AudMemPool, poolSize);
    audrvMemPoolAttach(&AudioDrv, mpid);

    static const u8 sink_channels[] = { 0, 1 };
    audrvDeviceSinkAdd(&AudioDrv, AUDREN_DEFAULT_DEVICE_NAME, 2, sink_channels);

    audrvUpdate(&AudioDrv);

    if (!R_SUCCEEDED(code = audrenStartAudioRenderer()))
        printf("audrv create failed! %d\n", code);

    if (!audrvVoiceInit(&AudioDrv, 0, 2, PcmFormat_Int16, 32823)) // cheating
        printf("failed to create voice\n");

    audrvVoiceSetDestinationMix(&AudioDrv, 0, AUDREN_FINAL_MIX_ID);
    audrvVoiceSetMixFactor(&AudioDrv, 0, 1.0f, 0, 0);
    audrvVoiceSetMixFactor(&AudioDrv, 0, 1.0f, 1, 1);
    audrvVoiceStart(&AudioDrv, 0);

    threadCreate(&AudioThread, AudioOutput, nullptr, nullptr, 1024*32, 0x20, 0);
    threadStart(&AudioThread);

    hidGetSixAxisSensorHandles(JoyconSixaxisHandles, 2, CONTROLLER_PLAYER_1, TYPE_JOYCON_PAIR);
    hidStartSixAxisSensor(JoyconSixaxisHandles[0]);
    hidStartSixAxisSensor(JoyconSixaxisHandles[1]);
    hidGetSixAxisSensorHandles(&ConsoleSixAxisHandle, 1, CONTROLLER_HANDHELD, TYPE_HANDHELD);
    hidStartSixAxisSensor(ConsoleSixAxisHandle);

    SixAxisSensorValues sixaxisValues;
    hidSixAxisSensorValuesRead(&sixaxisValues, CONTROLLER_P1_AUTO, 1);
    RecalibrateGyro(sixaxisValues);
}

void DeInit()
{
    hidStopSixAxisSensor(JoyconSixaxisHandles[0]);
    hidStopSixAxisSensor(JoyconSixaxisHandles[1]);
    hidStopSixAxisSensor(ConsoleSixAxisHandle);

    State = emuState_Quit;
    StateAtomic = State;

    threadWaitForExit(&AudioThread);
    threadClose(&AudioThread);

    threadWaitForExit(&EmuThread);
    threadClose(&EmuThread);

    audrvClose(&AudioDrv);
    audrenExit();

    free(AudMemPool);

    for (u32 i = 0; i < 2; i++)
        Gfx::TextureDelete(FramebufferTextures[i]);
}

const u32 KeyMappings[] =
{
    KEY_A,
    KEY_B,
    KEY_MINUS,
    KEY_PLUS,
    KEY_DRIGHT | KEY_LSTICK_RIGHT,
    KEY_DLEFT  | KEY_LSTICK_LEFT,
    KEY_DUP    | KEY_LSTICK_UP,
    KEY_DDOWN  | KEY_LSTICK_DOWN,
    KEY_R,
    KEY_L,
    KEY_X,
    KEY_Y
};

void UpdateAndDraw(u64& keysDown, u64& keysUp)
{
    bool touchUseCursor = false;

    if (State == emuState_Running)
    {
        // delay until the end of the frame
        svcSleepThread(1000 * 1000 * 10);

        hidScanInput();
        keysDown = hidKeysDown(CONTROLLER_P1_AUTO);
        keysUp = hidKeysUp(CONTROLLER_P1_AUTO);

        PlatformKeysHeld |= keysDown;
        PlatformKeysHeld &= ~keysUp;

        if ((PlatformKeysHeld & (KEY_ZL|KEY_ZR)) == (KEY_ZL|KEY_ZR))
        {
            SetPause(true);
        }
        else
        {
            u32 rotatedKeyMappings[12];
            memcpy(rotatedKeyMappings, KeyMappings, 12*4);
            switch (Config::ScreenRotation)
            {
            case 0: // nothing needs to be handled
                break;
            case 1: // 90 degrees
                rotatedKeyMappings[4] = KeyMappings[6]; // right -> up
                rotatedKeyMappings[5] = KeyMappings[7]; // left -> down
                rotatedKeyMappings[6] = KeyMappings[5]; // up -> left
                rotatedKeyMappings[7] = KeyMappings[4]; // down -> right

                rotatedKeyMappings[0] = KeyMappings[10]; // X -> A
                rotatedKeyMappings[1] = KeyMappings[0]; // A -> B
                rotatedKeyMappings[10] = KeyMappings[11]; // X -> Y
                rotatedKeyMappings[11] = KeyMappings[1]; // Y -> B
                break;
            case 2: // 180 degrees
                rotatedKeyMappings[4] = KeyMappings[5]; // right -> left
                rotatedKeyMappings[5] = KeyMappings[4]; // left -> right
                rotatedKeyMappings[6] = KeyMappings[7]; // up -> down
                rotatedKeyMappings[7] = KeyMappings[6]; // down -> up

                rotatedKeyMappings[0] = KeyMappings[11]; // Y -> A
                rotatedKeyMappings[1] = KeyMappings[10]; // X -> B
                rotatedKeyMappings[10] = KeyMappings[1]; // B -> X
                rotatedKeyMappings[11] = KeyMappings[0]; // A -> Y
                break;
            case 3: // 270 degrees
                rotatedKeyMappings[4] = KeyMappings[7]; // right -> down
                rotatedKeyMappings[5] = KeyMappings[6]; // left -> up
                rotatedKeyMappings[6] = KeyMappings[4]; // up -> right
                rotatedKeyMappings[7] = KeyMappings[5]; // down -> left

                rotatedKeyMappings[0] = KeyMappings[1]; // A -> B
                rotatedKeyMappings[1] = KeyMappings[11]; // B -> Y
                rotatedKeyMappings[10] = KeyMappings[0]; // X -> A
                rotatedKeyMappings[11] = KeyMappings[10]; // Y -> X
                break;
            }

            Gfx::Vector2f botScreenSize = {ScreenPoints[1][1].X - ScreenPoints[1][0].X, ScreenPoints[1][2].Y - ScreenPoints[1][0].Y};
            Gfx::Vector2f botScreenCenter = ScreenPoints[1][0] + botScreenSize * 0.5f;
            if (Config::TouchscreenMode < 2)
            {
                JoystickPosition rstick;
                hidJoystickRead(&rstick, CONTROLLER_P1_AUTO, JOYSTICK_RIGHT);

                Gfx::Vector2f rstickVec = {(float)rstick.dx / JOYSTICK_MAX, -(float)rstick.dy / JOYSTICK_MAX};

                touchUseCursor = true;
                if (Config::TouchscreenMode == 0) // mouse mode
                {
                    if (rstickVec.LengthSqr() < 0.1f * 0.1f)
                        TouchCursorVelocity = {0.f, 0.f};
                    
                    float maxSpeed = std::max(botScreenSize.X, botScreenSize.Y) * 1.5f;
                    TouchCursorVelocity += rstickVec * std::max(botScreenSize.X, botScreenSize.Y) * 0.125f;
                    TouchCursorVelocity = TouchCursorVelocity.Clamp({-maxSpeed, -maxSpeed}, {maxSpeed, maxSpeed});

                    // allow for quick turns
                    if ((TouchCursorVelocity.X > 0.f && rstick.dx < 0) || (TouchCursorVelocity.X < 0.f && rstick.dx > 0))
                        TouchCursorVelocity.X = 0.f;
                    if ((TouchCursorVelocity.Y > 0.f && rstick.dy > 0) || (TouchCursorVelocity.Y < 0.f && rstick.dy < 0))
                        TouchCursorVelocity.Y = 0.f;
                    TouchCursorPosition += TouchCursorVelocity * Gfx::AnimationTimestep;
                }
                else // offset mode
                {
                    // project ray from screen origin with
                    Gfx::Vector2f direction = rstickVec;
                    Gfx::Vector2f origin = botScreenCenter;

                    if (rstickVec.LengthSqr() < 0.1f * 0.1f)
                    {
                        TouchCursorPosition = origin;
                    }
                    else
                    {
                        // we project the ray from the center of the bottom screen with the direction of the analog stick
                        // onto the border of the bottom screen so we can calculate the distance
                        float topX = origin.X + direction.X * ((ScreenPoints[1][0].Y - origin.Y) / direction.Y);
                        float bottomX = origin.X + direction.X * ((ScreenPoints[1][2].Y - origin.Y) / direction.Y);
                        float leftY = origin.Y + direction.Y * ((ScreenPoints[1][0].X - origin.X) / direction.X);
                        float rightY = origin.Y + direction.Y * ((ScreenPoints[1][1].X - origin.X) / direction.X);

                        Gfx::Vector2f hitPoint;
                        if (topX >= ScreenPoints[1][0].X && topX < ScreenPoints[1][1].X)
                            hitPoint = {topX, ScreenPoints[1][0].Y};
                        else if (bottomX >= ScreenPoints[1][0].X && bottomX < ScreenPoints[1][1].X)
                            hitPoint = {bottomX, ScreenPoints[1][2].Y};
                        else if (leftY >= ScreenPoints[1][0].Y && leftY < ScreenPoints[1][2].Y)
                            hitPoint = {ScreenPoints[1][0].X, leftY};
                        else if (rightY >= ScreenPoints[1][0].Y && rightY < ScreenPoints[1][2].Y)
                            hitPoint = {ScreenPoints[1][1].X, rightY};

                        TouchCursorPosition = origin + rstickVec * sqrtf((hitPoint - origin).LengthSqr());
                    }
                }
            }
            else
            {
                touchUseCursor = true;

                SixAxisSensorValues sixaxisValues;
                hidSixAxisSensorValuesRead(&sixaxisValues, CONTROLLER_P1_AUTO, 1);

                float xAngle, yAngle;
                if (hidGetHandheldMode())
                {
                    float up[] = {sixaxisValues.orientation[2].x, sixaxisValues.orientation[2].y, sixaxisValues.orientation[2].z};
                    xv_norm(up, up, 3);

                    xAngle = xv3_dot(up, GyroCalibrationRight);
                    yAngle = xv3_dot(up, GyroCalibrationForward);
                }
                else
                {
                    float forward[] = {sixaxisValues.orientation[1].x, sixaxisValues.orientation[1].y, sixaxisValues.orientation[1].z};
                    xv_norm(forward, forward, 3);

                    xAngle = xv3_dot(forward, GyroCalibrationRight);
                    yAngle = xv3_dot(forward, GyroCalibrationUp);
                }

                TouchCursorPosition = botScreenCenter
                    + Gfx::Vector2f{xAngle / (float)M_PI * 10.f, -yAngle / (float)M_PI * 10.f} * std::max(botScreenSize.X, botScreenSize.Y);

                if (keysDown & KEY_ZL)
                    RecalibrateGyro(sixaxisValues);
            }

            touchPosition touchPosition;
            if (hidTouchCount() > 0)
            {
                hidTouchRead(&touchPosition, 0);

                TouchCursorPosition = {-1.f, -1.f};
            }

            if (TouchCursorPosition.X < ScreenPoints[1][0].X)
            {
                TouchCursorPosition.X = ScreenPoints[1][0].X - 1.f;
                touchUseCursor = false;
            }
            if (TouchCursorPosition.X >= ScreenPoints[1][1].X)
            {
                TouchCursorPosition.X = ScreenPoints[1][1].X + 1.f;
                touchUseCursor = false;
            }
            if (TouchCursorPosition.Y < ScreenPoints[1][0].Y)
            {
                TouchCursorPosition.Y = ScreenPoints[1][0].Y - 1.f;
                touchUseCursor = false;
            }
            if (TouchCursorPosition.Y >= ScreenPoints[1][2].Y)
            {
                TouchCursorPosition.Y = ScreenPoints[1][2].Y + 1.f;
                touchUseCursor = false;
            }

            u32 keyMask = 0xFFF;
            for (int i = 0; i < 12; i++)
                keyMask &= ~(!!(PlatformKeysHeld & rotatedKeyMappings[i]) << i);

            mutexLock(&EmuThreadLock);
            if (touchUseCursor)
            {
                if (Config::TouchscreenClickMode == 0)
                    TouchHeld = PlatformKeysHeld & KEY_ZR;
                else if (keysDown & KEY_ZR)
                    TouchHeld ^= true;

                TouchFinalPositionX = (int)TouchCursorPosition.X;
                TouchFinalPositionY = (int)TouchCursorPosition.Y;
                Frontend::GetTouchCoords(TouchFinalPositionX, TouchFinalPositionY);
                TouchDown = TouchHeld
                    && TouchFinalPositionX >= 0 && TouchFinalPositionX < 256
                    && TouchFinalPositionY >= 0 && TouchFinalPositionY < 192;
            }
            else
            {
                TouchDown = true;
                Gfx::Rotate90Deg(touchPosition.px, touchPosition.py, touchPosition.px, touchPosition.py, Config::GlobalRotation);
                TouchFinalPositionX = touchPosition.px;
                TouchFinalPositionY = touchPosition.py;
                Frontend::GetTouchCoords(TouchFinalPositionX, TouchFinalPositionY);
                TouchDown = TouchFinalPositionX >= 0 && TouchFinalPositionX < 256
                    && TouchFinalPositionY >= 0 && TouchFinalPositionY < 192;
            }

            LimitFramerate = Config::LimitFramerate;

            KeyMask = keyMask;

            if (NewFrameReady)
            {
                NewFrameReady = false;
                FrontBuffer ^= 1;
                for (u32 i = 0; i < 2; i++)
                    Gfx::TextureUpload(FramebufferTextures[i], 0, 0, 256, 192, GPU::Framebuffer[GPU::FrontBuffer][i], 256*4);
            }
            mutexUnlock(&EmuThreadLock);
            // request a new frame
            condvarWakeOne(&FrameStartCond);
        }
    }

    if (State != emuState_Nothing)
    {
        if (Config::Filtering == 1)
            Gfx::SetSmoothEdges(true);
        Gfx::SetSampler((Config::Filtering == 0 ? Gfx::sampler_Nearest : Gfx::sampler_Linear) | Gfx::sampler_ClampToEdge);
        for (int i = 0; i < 2; i++)
        {
            Gfx::DrawRectangle(FramebufferTextures[i], 
                ScreenPoints[i][0], ScreenPoints[i][1],
                ScreenPoints[i][2], ScreenPoints[i][3],
                {0.f, 0.f}, {256.f, 192.f});
        }
        Gfx::SetSmoothEdges(false);
    }

    if (State == emuState_Running && touchUseCursor)
    {
        // draw shadow first
        Gfx::DrawText(Gfx::SystemFontNintendoExt,
            TouchCursorPosition + Gfx::Vector2f{2.f, 3.f}, TextLineHeight * 2.f, DarkColorTransparent,
            Gfx::align_Center, Gfx::align_Center,
            TouchDown
                ? GFX_NINTENDOFONT_WII_HAND_HOLD
                : GFX_NINTENDOFONT_WII_HAND);
        Gfx::DrawText(Gfx::SystemFontNintendoExt,
            TouchCursorPosition, TextLineHeight * 2.f, WidgetColorBright,
            Gfx::align_Center, Gfx::align_Center,
            TouchDown
                ? GFX_NINTENDOFONT_WII_HAND_HOLD
                : GFX_NINTENDOFONT_WII_HAND);
    }

    if (Config::ShowPerformanceMetrics && State == emuState_Running)
    {
        // locking the mutex again, eh not so great
        mutexLock(&EmuThreadLock);
        Gfx::DrawRectangle({0.f, 0.f}, {3*120.f, TextLineHeight * 2.f}, DarkColorTransparent);
        float sum = 0.f, max = 0.f;    
        for (int i = 0; i < 120; i++)
        {
            sum += FrametimeHistogram[i];
            max = std::max(max, FrametimeHistogram[i]);
        }

        int idx = FrametimeHistogramNextValue;
        for (int i = 0; i < 120; i++)
        {
            float frametime = FrametimeHistogram[idx];
            Gfx::DrawRectangle({i * 3.f, TextLineHeight}, {3.f, frametime / max * TextLineHeight}, WidgetColorBright);
            idx++;
            if (idx == 120)
                idx = 0;
        }

        float averageFrametime = sum / 120.f;
        Gfx::DrawText(Gfx::SystemFontStandard, {0.f, 0.f}, TextLineHeight, WidgetColorBright, "avg: %.2fms max: %.2fms %d", averageFrametime, max, MissedFrame);
        mutexUnlock(&EmuThreadLock);
    }

    if (State == emuState_Paused)
    {
        // blend down
        Gfx::DrawRectangle({0.f, 0.f}, {1280.f, 1280.f}, DarkColorTransparent);
    }
}

void LoadROM(const char* file)
{
    assert(State == emuState_Nothing);
    mutexLock(&EmuThreadLock);
    int res = Frontend::LoadROM(file, 0);
    printf("loading %s %d\n", file, res);
    State = emuState_Running;
    StateAtomic = State;
    CurrentUiScreen = uiScreen_Start;
    mutexUnlock(&EmuThreadLock);
}

void SetPause(bool pause)
{
    PlatformKeysHeld = 0;
    mutexLock(&EmuThreadLock);
    assert(State == (pause ? emuState_Running : emuState_Paused));
    State = pause ? emuState_Paused : emuState_Running;
    StateAtomic = State;
    mutexUnlock(&EmuThreadLock);
    if (pause)
        condvarWakeOne(&FrameStartCond);
    while (StateAtomic != StateInternal);
}

void Stop()
{
    State = emuState_Nothing;
    StateAtomic = State;
}

void Reset()
{
    assert(State != emuState_Running);
    mutexLock(&EmuThreadLock);
    Frontend::Reset();
    State = emuState_Running;
    StateAtomic = State;
    mutexUnlock(&EmuThreadLock);
}

void DeriveScreenPoints(float* mat, Gfx::Vector2f* points)
{
    points[0] = {0.f, 0.f};
    points[1] = {256.f, 0.f};
    points[2] = {0.f, 192.f};
    points[3] = {256.f, 192.f};
    for (int i = 0; i < 4; i++)
        Frontend::M23_Transform(mat, points[i].X, points[i].Y);
}

void UpdateScreenLayout()
{
    int screenWidth = 1280;
    int screenHeight = 720;
    if ((Config::GlobalRotation % 2) == 1)
        std::swap(screenWidth, screenHeight);
    Frontend::SetupScreenLayout(screenWidth, screenHeight,
        Config::ScreenLayout, Config::ScreenRotation, Config::ScreenSizing, Config::ScreenGap, Config::IntegerScaling);
    float topMatrix[6], bottomMatrix[6];
    Frontend::GetScreenTransforms(topMatrix, bottomMatrix);
    DeriveScreenPoints(topMatrix, ScreenPoints[0]);
    DeriveScreenPoints(bottomMatrix, ScreenPoints[1]);
}

}

namespace Platform
{
extern char ExecutableDir[];
}

int main(int argc, const char* argv[])
{
    socketInitializeDefault();
    nxlinkStdio();

    romfsInit();
    setInitialize();

    Gfx::Init();

    Config::Load();
    strcpy(Config::FirmwarePath, "firmware.bin");
    strcpy(Config::BIOS9Path, "bios9.bin");
    strcpy(Config::BIOS7Path, "bios7.bin");

    Filebrowser::Init();

    Frontend::Init_ROM();
    Emulation::Init();

    while (appletMainLoop() && !Done)
    {
        Gfx::StartFrame();

        int rotation = Config::GlobalRotation;
        int screenWidth = 1280, screenHeight = 720;
        if ((rotation % 2) == 1)
            std::swap(screenWidth, screenHeight);

        Gfx::PushScissor(0, 0, screenWidth, screenHeight);
        BoxGui::Frame rootFrame{Gfx::Vector2f{(float)screenWidth, (float)screenHeight}};

        u64 keysUp, keysDown;
        if (Emulation::State != Emulation::emuState_Nothing)
        {
            Emulation::UpdateAndDraw(keysDown, keysUp);
        }
        // the way we process input is weird, but we want to poll late
        // so we have to do some gymnastics a bit
        if (Emulation::State != Emulation::emuState_Running)
        {
            hidScanInput();
            keysUp = hidKeysUp(CONTROLLER_P1_AUTO);
            keysDown = hidKeysDown(CONTROLLER_P1_AUTO);
        }

        switch (CurrentUiScreen)
        {
        case uiScreen_Start:
            if (Emulation::State != Emulation::emuState_Running)
                StartMenu::DoGui(rootFrame);
            break;
        case uiScreen_BrowseROM:
            Filebrowser::DoGui(rootFrame);
            break;
        case uiScreen_EmulationSettings:
        case uiScreen_DisplaySettings:
        case uiScreen_InputSettings:
            SettingsDialog::DoGui(rootFrame);
            break;
        }

        if (!BoxGui::HasModalDialog())
            KeyExplanation::DoGui(rootFrame);
        else
            KeyExplanation::Reset();

        BoxGui::Update(rootFrame, keysDown, keysUp);

        Gfx::PopScissor();

        Gfx::EndFrame(Emulation::State == Emulation::emuState_Running
            ? Gfx::Color() : WallpaperColor, rotation);
    }

    Emulation::DeInit();
    Frontend::DeInit_ROM();
    Filebrowser::DeInit();

    Config::Save();

    Gfx::DeInit();

    setExit();
    romfsExit();

    socketExit();

    return 0;
}