#include "StartMenu.h"

#include "Style.h"
#include "KeyExplanations.h"
#include "main.h"

namespace StartMenu
{

bool SideBarEntry(BoxGui::Frame& optionsFrame, BoxGui::Skewer& optionSkewer, const char* name, bool last = false)
{
    BoxGui::Frame buttonFrame{optionsFrame, optionSkewer.Spit({optionsFrame.Area.Size.X, UIRowHeight}, Gfx::align_Right),
        {0.f, 5.f}, {0.f, 5.f}};

    bool selected = BoxGui::InputElement(buttonFrame, BoxGui::MakeUniqueName("sidebar", (u64)name));

    if (selected)
    {
        Gfx::DrawRectangle(buttonFrame.Area.Position, buttonFrame.Area.Size, WidgetColorVibrant);
        KeyExplanation::Explain(KeyExplanation::button_A, "Select");
    }

    BoxGui::Skewer buttonSkewer{buttonFrame, buttonFrame.Area.Size.Y/2.f, BoxGui::direction_Horizontal};
    buttonSkewer.AlignLeft(20.f);
    Gfx::DrawText(Gfx::SystemFontStandard, buttonSkewer.CurrentPosition(), TextLineHeight, DarkColor,
        Gfx::align_Left, Gfx::align_Center,
        name);

    Gfx::DrawRectangle(buttonFrame.Area.Position + Gfx::Vector2f{10.f, -(5.f + 1.f)},
        {buttonFrame.Area.Size.X - 2*10.f, 2.f},
        SeparatorColor);
    if (last)
    {
        Gfx::DrawRectangle(buttonFrame.Area.Position + Gfx::Vector2f{10.f, buttonFrame.Area.Size.Y + 5.f - 1.f},
            {buttonFrame.Area.Size.X - 2*10.f, 2.f},
            SeparatorColor);
    }

    return selected && BoxGui::ConfirmPressed();
}

void DoGui(BoxGui::Frame& parent)
{
    BoxGui::Skewer skewer{parent, 0.f, BoxGui::direction_Horizontal};

    BoxGui::Frame sideBarFrame{parent, skewer.Spit({320.f, parent.Area.Size.Y}, Gfx::align_Right), {5.f, 0.f}, {5.f, 0.f}};
    Gfx::DrawRectangle(sideBarFrame.Area.Position, sideBarFrame.Area.Size, WidgetColorBright);

    BoxGui::Skewer sideBarSkewer{sideBarFrame, 0.f, BoxGui::direction_Vertical};

    const float spacing = UIRowHeight/2.f;
    sideBarSkewer.AlignLeft(spacing);

    if (Emulation::State == Emulation::emuState_Paused)
    {
        if (SideBarEntry(sideBarFrame, sideBarSkewer, "Continue", true))
        {
            Emulation::SetPause(false);
        }
        sideBarSkewer.Advance(spacing);
        if (SideBarEntry(sideBarFrame, sideBarSkewer, Emulation::LidClosed ? "Open lid" : "Close lid"))
        {
            Emulation::LidClosed ^= true;
            Emulation::SetPause(false);
        }
        if (SideBarEntry(sideBarFrame, sideBarSkewer, "Reset", true))
        {
            Emulation::Reset();
        }
        sideBarSkewer.Advance(spacing);
        if (SideBarEntry(sideBarFrame, sideBarSkewer, "Display settings"))
        {
            CurrentUiScreen = uiScreen_DisplaySettings;
        }
        if (SideBarEntry(sideBarFrame, sideBarSkewer, "Input settings", true))
        {
            CurrentUiScreen = uiScreen_InputSettings;
        }
        sideBarSkewer.Advance(spacing);
        if (SideBarEntry(sideBarFrame, sideBarSkewer, "Close", true))
        {
            Emulation::Stop();
        }
    }
    else
    {
        if (SideBarEntry(sideBarFrame, sideBarSkewer, "Browse", true))
        {
            CurrentUiScreen = uiScreen_BrowseROM;
        }
        sideBarSkewer.Advance(spacing);
        if (SideBarEntry(sideBarFrame, sideBarSkewer, "Emulation settings"))
        {
            CurrentUiScreen = uiScreen_EmulationSettings;
        }
        if (SideBarEntry(sideBarFrame, sideBarSkewer, "Display settings"))
        {
            CurrentUiScreen = uiScreen_DisplaySettings;
        }
        if (SideBarEntry(sideBarFrame, sideBarSkewer, "Input settings", true))
        {
            CurrentUiScreen = uiScreen_InputSettings;
        }
        sideBarSkewer.Advance(spacing);
        if (SideBarEntry(sideBarFrame, sideBarSkewer, "Exit", true))
        {
            Done = true;
        }
    }
}

}