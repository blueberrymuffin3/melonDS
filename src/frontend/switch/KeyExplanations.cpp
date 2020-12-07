#include "KeyExplanations.h"
#include "Style.h"

#include <string>

namespace KeyExplanation
{

int Hold = 0;
u32 KeysExplained, KeysExplainedThisFrame;
std::string KeyExplanations[buttons_Count];

void Explain(int button, const char* explanation)
{
    Hold = 2;
    KeysExplainedThisFrame |= 1 << button;
    KeyExplanations[button] = explanation;
}

void DoGui(BoxGui::Frame& parent)
{
    // hack: when switching between gui states it can happen that the currently selected items is reset
    // so for one frame there's no item selected which results in a small flicker because there's no key explanation
    if (KeysExplainedThisFrame == 0)
    {
        Hold--;
        if (Hold < 0)
            KeysExplained = 0;
    }
    else
    {
        KeysExplained = KeysExplainedThisFrame;
    }
    KeysExplainedThisFrame = 0;

    if (KeysExplained == 0)
        return;

    const float tabSize = 140.f;

    Gfx::Vector2f size = {__builtin_popcount(KeysExplained) * tabSize, TextLineHeight * 2.f};
    BoxGui::Frame explanationFrame{parent, BoxGui::Rect{parent.Area.Size - size, size}};
    Gfx::DrawRectangle(explanationFrame.Area.Position, explanationFrame.Area.Size, DarkColor);
    u32 i = 0;
    u32 keysExplained = KeysExplained;
    while (keysExplained)
    {
        int button = __builtin_ctz(keysExplained);
        keysExplained &= ~(1 << button);

        const char* buttonIcon = "\uE009";
        switch (button)
        {
        case button_A: buttonIcon = GFX_NINTENDOFONT_A_BUTTON; break;
        case button_B: buttonIcon = GFX_NINTENDOFONT_B_BUTTON; break;
        case button_X: buttonIcon = GFX_NINTENDOFONT_X_BUTTON; break;
        case button_Y: buttonIcon = GFX_NINTENDOFONT_Y_BUTTON; break;
        case button_Plus: buttonIcon = GFX_NINTENDOFONT_PLUS_BUTTON; break;
        case button_Minus: buttonIcon = GFX_NINTENDOFONT_MINUS_BUTTON; break;
        default: break;
        }
        Gfx::DrawText(Gfx::SystemFontNintendoExt, explanationFrame.Area.Position + Gfx::Vector2f{tabSize * i + 10.f, size.Y/2.f-TextLineHeight/2.f},
            TextLineHeight, WidgetColorBright, Gfx::align_Left, Gfx::align_Left, buttonIcon);

        Gfx::DrawText(Gfx::SystemFontStandard, explanationFrame.Area.Position + Gfx::Vector2f{tabSize * i + 10.f + 30.f, size.Y/2.f-TextLineHeight/2.f},
            TextLineHeight, WidgetColorBright, Gfx::align_Left, Gfx::align_Left, KeyExplanations[button].c_str());

        i++;
    }
}

void Reset()
{
    KeysExplainedThisFrame = 0;
}

}
