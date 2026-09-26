esp::render::DrawPlayerFlags(*drawList,g::fontOverlayText ? g::fontOverlayText : ImGui::GetFont(),
    g::fontUiIcons,p,renderPlayerPos,renderLocalPos,
    bombState.beingDefused ? bombState.defuseEndTime-bombState.currentGameTime : -1,
    nowUs,ImVec2(boxLeft,boxTop),ImVec2(0,0),ImVec2(screenW,screenH),g::espSettings,barLayout,
    bombState.beingDefused && bombState.blowTime>bombState.currentGameTime
        ? bombState.blowTime-bombState.defuseEndTime : std::numeric_limits<float>::quiet_NaN(),
    distanceReadouts[i].Update(p.pawn,p.pawnHandle,snap.sceneSerial,(renderPlayerPos-renderLocalPos).Length()/39.37f));
