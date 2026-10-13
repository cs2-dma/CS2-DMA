#include "app/Core/globals.h"
namespace esp {
    EntitySnapshot source;
    CameraFrame camera;
    uint64_t clockUs = 1000000;
    bool readable = true;
    std::atomic<uint64_t> s_sceneResetSerial{1};
    std::atomic<uint64_t> s_lastBulkEvictionUs{0};
    bool ReadCurrentSnapshot(EntitySnapshot& out) { out = source; return readable; }
    bool ReadCameraFrame(CameraFrame& out) { out = camera; return true; }
    uint64_t TickNowUs() { return clockUs; }
    uint64_t TickNowMs() { return clockUs / 1000u; }
    bool isValidWorldPos(const Vector3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
            std::max({std::fabs(v.x), std::fabs(v.y), std::fabs(v.z)}) < 32768.0f;
    }
    void RecordEspEvent(EspEventDescriptor) {}
    bool GetWebRadarSnapshot(WebRadarSnapshot* out);
}
#include "../../../src/Features/ESP/Core/get_web_radar_snapshot.inl"

void TestSnapshotConsumers()
{
    using namespace esp;
    const bool savedWorld = g::espWorld;
    g::espWorld = true;
    CHECK(!esp::render::HasOverlayDrawDemand(false, false, false, false, false, false, false));
    for (int bit = 0; bit < 7; ++bit) {
        CHECK(esp::render::HasOverlayDrawDemand(bit == 0, bit == 1, bit == 2,
            bit == 3, bit == 4, bit == 5, bit == 6));
    }
    CHECK(esp::state::IsSceneSnapshotFresh(1, 1, 1000000, 1500000));
    CHECK(!esp::state::IsSceneSnapshotFresh(1, 1, 0, 1));
    CHECK(!esp::state::IsSceneSnapshotFresh(1, 2, 1000000, 1000000));
    CHECK(!esp::state::IsSceneSnapshotFresh(1, 1, 1000000, 1500001));
    CHECK(!esp::state::IsSceneSnapshotFresh(1, 1, 1000001, 1000000));
    const auto reset = [] {
        source = {};
        camera = {};
        clockUs = 1000000;
        s_sceneResetSerial = 1;
        readable = true;
        source.sceneSerial = 1;
        source.captureTimeUs = clockUs;
        source.localPawn = 0x10000;
        source.localPosValid = true;
        source.localPos = {100, 200, 10};
        source.localPosUpdatedAtUs = clockUs;
        source.localTeam = 3;
        source.localHealth = 100;
        strcpy_s(source.activeMapKey, "de_inferno");
        source.hasMinimapBounds = true;
        source.bombState.dropped = true;
        source.bombState.position = {10, 20, 30};
        source.bombState.positionSampleTimeUs = clockUs;
        source.worldMarkerCount = 1;
        auto& marker = source.worldMarkers[0];
        marker = {};
        marker.valid = true;
        marker.type = WorldMarkerType::Inferno;
        marker.position = {50, 60, 70};
        marker.expiresUs = clockUs + 5000000;
        marker.sourceSampleUs = clockUs;
        auto& player = source.webRadarPlayers[0];
        player.valid = true;
        player.pawn = 0x20000;
        player.team = 2;
        player.health = 100;
        player.position = {200, 100, 10};
        player.coreUpdatedAtUs = clockUs;
    };
    WebRadarSnapshot out;
    reset();
    CHECK(GetWebRadarSnapshot(&out));
    CHECK(out.players[0].valid && out.bomb.dropped && out.worldMarkerCount == 1);
    CHECK(out.localHealth == 100 && out.hasMinimapBounds);
    s_sceneResetSerial = 2;
    CHECK(GetWebRadarSnapshot(&out));
    CHECK(!out.players[0].valid && !out.bomb.dropped && out.worldMarkerCount == 0);
    CHECK(out.mapKey[0] == 0 && !out.hasMinimapBounds && out.localTeam == 0);
    CHECK(!std::isfinite(out.localPos.x));
    reset();
    clockUs += 500001;
    CHECK(GetWebRadarSnapshot(&out));
    CHECK(!out.players[0].valid && !out.bomb.dropped && out.worldMarkerCount == 0);
    CHECK(out.localHealth == 0);
    reset();
    source.captureTimeUs = clockUs + 1;
    CHECK(GetWebRadarSnapshot(&out));
    CHECK(!out.bomb.dropped && out.worldMarkerCount == 0);
    reset();
    source.worldMarkers[0].position.x = NAN;
    CHECK(GetWebRadarSnapshot(&out));
    CHECK(out.worldMarkerCount == 0);
    reset();
    source.worldMarkers[0].expiresUs = clockUs;
    CHECK(GetWebRadarSnapshot(&out) && out.worldMarkerCount == 0);
    reset();
    readable = false;
    CHECK(!GetWebRadarSnapshot(&out));
    CHECK(!GetWebRadarSnapshot(nullptr));
    g::espWorld = savedWorld;
}
