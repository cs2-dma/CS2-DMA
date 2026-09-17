if (g::espOffscreenArrows && IsFiniteVec(renderLocalPos) && std::isfinite(yawRad)) {
    const float dx = renderPlayerPos.x - renderLocalPos.x;
    const float dy = renderPlayerPos.y - renderLocalPos.y;
    const float enemyYaw = atan2f(dy, dx);
    const float relativeAngle = enemyYaw - yawRad;

    const float dirX = sinf(relativeAngle);
    const float dirY = -cosf(relativeAngle);

    const float arrowSize = std::clamp(g::espOffscreenSize, 6.0f, 36.0f);
    const float radius = std::max(0.0f, (std::min(screenW, screenH) * 0.47f) - arrowSize * 2.5f);
    const ImVec2 center(screenW * 0.5f, screenH * 0.5f);
    const ImVec2 base(center.x + dirX * radius, center.y + dirY * radius);
    
    const float perpX = -dirY;
    const float perpY = dirX;
    
    const ImVec2 tip(base.x + dirX * arrowSize, base.y + dirY * arrowSize);
    const ImVec2 left(
        base.x - dirX * arrowSize * 0.35f + perpX * arrowSize * 0.50f,
        base.y - dirY * arrowSize * 0.35f + perpY * arrowSize * 0.50f);
    const ImVec2 right(
        base.x - dirX * arrowSize * 0.35f - perpX * arrowSize * 0.50f,
        base.y - dirY * arrowSize * 0.35f - perpY * arrowSize * 0.50f);

    const float* baseColor = g::espOffscreenColor;
    if (g::espVisibilityColoring)
        baseColor = p.visible ? g::espVisibleColor : g::espHiddenColor;

    const float time = static_cast<float>(ImGui::GetTime());
    const float pulse = 0.8f + 0.2f * sinf(time * 6.0f);

    ImU32 renderArrowCol = IM_COL32(
        static_cast<int>(std::clamp(baseColor[0], 0.0f, 1.0f) * 255.0f),
        static_cast<int>(std::clamp(baseColor[1], 0.0f, 1.0f) * 255.0f),
        static_cast<int>(std::clamp(baseColor[2], 0.0f, 1.0f) * 255.0f),
        static_cast<int>(std::clamp(baseColor[3], 0.0f, 1.0f) * pulse * 255.0f)
    );

    drawList->AddTriangleFilled(tip, left, right, renderArrowCol);
}
