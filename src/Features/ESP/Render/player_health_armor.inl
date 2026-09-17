struct CompactBarLabel {
    bool active = false;
    char text[8] = {};
    ImVec2 textPos = {};
    ImVec2 bgMin = {};
    ImVec2 bgMax = {};
    ImU32 textColor = 0;
    ImU32 accentColor = 0;
};

ImFont* barValueFont = ImGui::GetFont();
const float barValueFontSize =
    barValueFont ? std::max(7.5f, ImGui::GetFontSize() - 4.5f) : 0.0f;
auto calcBarValueSize = [&](const char* text) -> ImVec2 {
    if (barValueFont && barValueFontSize > 0.0f)
        return barValueFont->CalcTextSizeA(barValueFontSize, FLT_MAX, 0.0f, text, nullptr);
    return ImGui::CalcTextSize(text);
};

const float visualBarWidth = 2.0f;
const float barInset = std::max(0.0f, (sideBarWidth - visualBarWidth) * 0.5f);
const float healthBarWidth = visualBarWidth;
const float healthBarLeft = primaryLeftBarX + barInset;
const float armorBarWidth = visualBarWidth;
const float armorBarGap = 3.0f;
const float armorBarLeft = g::espHealth ? (healthBarLeft - armorBarWidth - armorBarGap) : healthBarLeft;
const float barOutlinePad = 0.75f;

auto drawBarTrack = [&](float barLeft, float barWidth) {
    drawList->AddRectFilled(
        ImVec2(barLeft - barOutlinePad, boxTop - barOutlinePad),
        ImVec2(barLeft + barWidth + barOutlinePad, boxTop + boxHeight + barOutlinePad),
        IM_COL32(10, 10, 10, 185),
        1.5f);
    drawList->AddRect(
        ImVec2(barLeft - barOutlinePad, boxTop - barOutlinePad),
        ImVec2(barLeft + barWidth + barOutlinePad, boxTop + boxHeight + barOutlinePad),
        IM_COL32(0, 0, 0, 160),
        1.5f,
        0,
        1.0f);
};

auto drawBarFill = [&](float barLeft, float barWidth, float barFrac, ImU32 topCol, ImU32 bottomCol) {
    if (barFrac <= 0.0f)
        return;
    const float barHeight = boxHeight * barFrac;
    const ImVec2 fillMin(barLeft, boxTop + boxHeight - barHeight);
    const ImVec2 fillMax(barLeft + barWidth, boxTop + boxHeight);
    if (topCol == bottomCol) {
        drawList->AddRectFilled(fillMin, fillMax, topCol, 1.0f);
    } else {
        drawList->AddRectFilledMultiColor(fillMin, fillMax, topCol, topCol, bottomCol, bottomCol);
    }
};

auto barColorU32 = [](const esp::render::BarColor& color) {
    return IM_COL32(
        static_cast<int>(color.r * 255.0f),
        static_cast<int>(color.g * 255.0f),
        static_cast<int>(color.b * 255.0f),
        static_cast<int>(color.a * 255.0f));
};

auto resolveBarColors = [&](int mode, float fraction, const float* primary, const float* low, bool armor) {
    const auto normalizedMode = esp::render::NormalizeBarColorMode(mode);
    const ImU32 resolved = barColorU32(
        armor
            ? esp::render::ResolveArmorBarColor(mode, fraction, primary, low)
            : esp::render::ResolveBarColor(mode, fraction, primary, low));
    if (normalizedMode != esp::render::BarColorMode::Gradient)
        return std::pair<ImU32, ImU32>(resolved, resolved);
    return std::pair<ImU32, ImU32>(
        barColorU32(esp::render::ReadBarColor(primary)),
        barColorU32(esp::render::ReadBarColor(low)));
};

auto makeBarLabel = [&](CompactBarLabel& label, int value, float barLeft, float barWidth, ImU32 textColor, ImU32 accentColor) {
    { auto [ptr, ec] = std::to_chars(label.text, label.text + sizeof(label.text) - 1, value); *ptr = '\0'; }
    const ImVec2 textSize = calcBarValueSize(label.text);
    const float padX = 2.5f;
    const float padY = 1.0f;
    const float labelWidth = textSize.x + padX * 2.0f;
    const float labelHeight = textSize.y + padY * 2.0f;
    float bgX = (barLeft + barWidth * 0.5f) - labelWidth * 0.5f;
    float bgY = boxTop - labelHeight - 3.0f;
    if (bgX < 2.0f)
        bgX = 2.0f;
    if (bgX + labelWidth > screenW - 2.0f)
        bgX = screenW - labelWidth - 2.0f;
    if (bgY < 2.0f)
        bgY = 2.0f;

    label.active = true;
    label.textPos = ImVec2(bgX + padX, bgY + padY - 0.25f);
    label.bgMin = ImVec2(bgX, bgY);
    label.bgMax = ImVec2(bgX + labelWidth, bgY + labelHeight);
    label.textColor = textColor;
    label.accentColor = accentColor;
};

auto labelsOverlap = [](const CompactBarLabel& a, const CompactBarLabel& b) -> bool {
    if (!a.active || !b.active)
        return false;
    return !(a.bgMax.x <= b.bgMin.x || a.bgMin.x >= b.bgMax.x ||
             a.bgMax.y <= b.bgMin.y || a.bgMin.y >= b.bgMax.y);
};

auto shiftLabelY = [&](CompactBarLabel& label, float deltaY) {
    label.bgMin.y += deltaY;
    label.bgMax.y += deltaY;
    label.textPos.y += deltaY;
};

auto shiftLabelX = [&](CompactBarLabel& label, float deltaX) {
    label.bgMin.x += deltaX;
    label.bgMax.x += deltaX;
    label.textPos.x += deltaX;
};

auto setLabelX = [&](CompactBarLabel& label, float bgX) {
    const float deltaX = bgX - label.bgMin.x;
    shiftLabelX(label, deltaX);
};

auto setLabelY = [&](CompactBarLabel& label, float bgY) {
    const float deltaY = bgY - label.bgMin.y;
    shiftLabelY(label, deltaY);
};

auto labelWidth = [](const CompactBarLabel& label) -> float {
    return label.bgMax.x - label.bgMin.x;
};

auto drawBarLabel = [&](const CompactBarLabel& label) {
    if (!label.active || label.text[0] == '\0')
        return;
    drawList->AddRectFilled(label.bgMin, label.bgMax, IM_COL32(8, 8, 8, 205), 2.5f);
    drawList->AddRect(label.bgMin, label.bgMax, IM_COL32(0, 0, 0, 150), 2.5f, 1.0f, 0);
    drawList->AddRectFilled(
        ImVec2(label.bgMin.x + 1.0f, label.bgMax.y - 2.0f),
        ImVec2(label.bgMax.x - 1.0f, label.bgMax.y - 1.0f),
        label.accentColor,
        1.0f);

    if (!barValueFont || barValueFontSize <= 0.0f) {
        drawList->AddText(ImVec2(label.textPos.x + 1.0f, label.textPos.y + 1.0f), IM_COL32(0, 0, 0, 210), label.text);
        drawList->AddText(label.textPos, label.textColor, label.text);
        return;
    }

    drawList->AddText(
        barValueFont,
        barValueFontSize,
        ImVec2(label.textPos.x + 1.0f, label.textPos.y + 1.0f),
        IM_COL32(0, 0, 0, 210),
        label.text);
    drawList->AddText(barValueFont, barValueFontSize, label.textPos, label.textColor, label.text);
};

CompactBarLabel hpLabel = {};
CompactBarLabel apLabel = {};


if (g::espArmor) {
    const float armorFrac = Clamp01(static_cast<float>(p.armor) / 100.0f);
    const auto [armorTopCol, armorBottomCol] = resolveBarColors(
        g::espArmorColorMode,
        armorFrac,
        g::espArmorColor,
        g::espArmorLowColor,
        true);
    drawBarTrack(armorBarLeft, armorBarWidth);
    drawBarFill(armorBarLeft, armorBarWidth, armorFrac, armorTopCol, armorBottomCol);

    if (g::espArmorText && p.armor > 0 && p.armor < 100) {
        makeBarLabel(
            apLabel,
            p.armor,
            armorBarLeft,
            armorBarWidth,
            IM_COL32(225, 245, 255, 255),
            armorTopCol);
    }
}


if (g::espHealth) {
    const float healthFrac = Clamp01(static_cast<float>(p.health) / 100.0f);
    const auto [healthTopCol, healthBottomCol] = resolveBarColors(
        g::espHealthColorMode,
        healthFrac,
        g::espHealthColor,
        g::espHealthLowColor,
        false);
    drawBarTrack(healthBarLeft, healthBarWidth);
    drawBarFill(healthBarLeft, healthBarWidth, healthFrac, healthTopCol, healthBottomCol);

    if (g::espHealthText && p.health < 100) {
        makeBarLabel(
            hpLabel,
            p.health,
            healthBarLeft,
            healthBarWidth,
            IM_COL32(255, 255, 255, 255),
            healthTopCol);
    }
}

if (hpLabel.active && apLabel.active) {
    const float sharedLabelY = std::min(hpLabel.bgMin.y, apLabel.bgMin.y);
    setLabelY(hpLabel, sharedLabelY);
    setLabelY(apLabel, sharedLabelY);

    const float pairGap = 3.0f;
    const float totalWidth = labelWidth(apLabel) + pairGap + labelWidth(hpLabel);
    const float pairCenterX =
        ((armorBarLeft + armorBarWidth * 0.5f) + (healthBarLeft + healthBarWidth * 0.5f)) * 0.5f;
    float startX = pairCenterX - totalWidth * 0.5f;
    if (startX < 2.0f)
        startX = 2.0f;
    if (startX + totalWidth > screenW - 2.0f)
        startX = screenW - totalWidth - 2.0f;

    setLabelX(apLabel, startX);
    setLabelX(hpLabel, startX + labelWidth(apLabel) + pairGap);

    if (labelsOverlap(hpLabel, apLabel)) {
        const float pushApart = ((apLabel.bgMax.x - hpLabel.bgMin.x) * 0.5f) + 1.0f;
        shiftLabelX(apLabel, -pushApart);
        shiftLabelX(hpLabel, pushApart);
    }
}

drawBarLabel(hpLabel);
drawBarLabel(apLabel);
