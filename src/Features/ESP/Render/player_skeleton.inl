if (g::espSkeleton && canRenderRealSkeleton) {
    const ImU32 skeletonRenderCol = g::espVisibilityColoring && g::espSettings.presentation.visibilitySkeleton
        ? visibilityCol : skelCol;
    const float thickness = std::clamp(g::espSkeletonThickness,0.5f,4.0f);
    const auto segment = [&](int from,int to) {
        if (!boneWorldValid[from] || !boneWorldValid[to]) return;
        ScreenPos a{},b{};
        if (!esp::render::ClipProjectedSegment(boneWorld[from],boneWorld[to],viewMatrix,screenW,screenH,a,b)) return;
        drawList->AddLine(ImVec2(a.x,a.y),ImVec2(b.x,b.y),IM_COL32(0,0,0,180),thickness+1.4f);
        drawList->AddLine(ImVec2(a.x,a.y),ImVec2(b.x,b.y),skeletonRenderCol,thickness);
    };
    for (int pair=0;pair<skeletonPairCount;++pair) segment(skeletonPairs[pair].from,skeletonPairs[pair].to);
    if (hasLeftToeBone && leftToeBoneId>=0) segment(esp::FOOT_HEEL_L,leftToeBoneId);
    if (hasRightToeBone && rightToeBoneId>=0) segment(esp::FOOT_HEEL_R,rightToeBoneId);
    if (g::espSkeletonHeadCircle && boneWorldValid[esp::HEAD])
        esp::render::DrawHeadContour(*drawList,p,poseRenderOffset,boneWorld[esp::HEAD],nowUs,
            viewMatrix,screenW,screenH,g::espSkeletonHeadScale,thickness,skeletonRenderCol);
    if (g::espSkeletonDots) {
        for (int id:esp::kPlayerStoredBoneIds) {
            if (!boneWorldValid[id] || !boneScreen[id].onScreen) continue;
            const ImVec2 point(boneScreen[id].x,boneScreen[id].y);
            drawList->AddCircleFilled(point,2.2f,IM_COL32(0,0,0,180),8);
            drawList->AddCircleFilled(point,1.4f,skeletonRenderCol,8);
        }
    }
}
