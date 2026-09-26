if (g::espSnaplines && snaplineSlots[i]) {
    const auto& options=g::espSettings.presentation;
    const int origin=options.snapOrigin<0 ? (g::espSnaplineFromTop ? 0 : 1) : std::clamp(options.snapOrigin,0,2);
    const ImVec2 from(screenW*0.5f,origin==0 ? 0 : origin==1 ? screenH : screenH*0.5f);
    const ImVec2 to=options.snapEndpoint==1 ? ImVec2(boxCenterX,boxTop+boxHeight*0.5f) : ImVec2(screenFeet.x,screenFeet.y);
    const float opacity=std::isfinite(options.snapOpacity) ? std::clamp(options.snapOpacity,0.0f,1.0f) : 1;
    const float thickness=std::isfinite(options.snapThickness) ? std::clamp(options.snapThickness,0.5f,4.0f) : 1;
    const ImU32 color=(snapCol & ~IM_COL32_A_MASK) |
        (static_cast<ImU32>(((snapCol>>IM_COL32_A_SHIFT)&255)*opacity)<<IM_COL32_A_SHIFT);
    drawList->AddLine(from,to,IM_COL32(0,0,0,static_cast<int>(140*opacity)),thickness+1.5f);
    drawList->AddLine(from,to,color,thickness);
}
