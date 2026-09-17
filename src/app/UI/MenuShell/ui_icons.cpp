#include "app/UI/MenuShell/ui_icons.h"

#include <cmath>

bool ui::icons::DrawCentered(
    ImDrawList* drawList,
    ImFont* font,
    Icon icon,
    const ImVec2& boxMin,
    float boxSize,
    float glyphSize,
    ImU32 color) noexcept
{
    if (!drawList || !font || boxSize <= 0.0f || glyphSize <= 0.0f)
        return false;

    ImFontBaked* baked = font->GetFontBaked(glyphSize);
    ImFontGlyph* glyph = baked ? baked->FindGlyphNoFallback(Codepoint(icon)) : nullptr;
    if (!glyph || !glyph->Visible || baked->Size <= 0.0f)
        return false;

    const float scale = glyphSize / baked->Size;
    const float glyphWidth = (glyph->X1 - glyph->X0) * scale;
    const float glyphHeight = (glyph->Y1 - glyph->Y0) * scale;
    const ImVec2 origin(
        std::round(boxMin.x + (boxSize - glyphWidth) * 0.5f - glyph->X0 * scale),
        std::round(boxMin.y + (boxSize - glyphHeight) * 0.5f - glyph->Y0 * scale));

    font->RenderChar(drawList, glyphSize, origin, color, Codepoint(icon));
    return true;
}
