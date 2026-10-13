void TestObserverAndInfernoReads()
{
    using namespace esp::data;
    const ObserverOffsets offsets{0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
    for (int scenario = 0; scenario < 8; ++scenario) {
        PlayerRepairReplay::Reader reader;
        ObserverSample samples[2] = {};
        samples[0].controller = 0x100000;
        reader.Set(0x100010, uint32_t{0x8080});
        reader.Set(0x100020, uint32_t{0x8090});
        reader.Set(0x100030, uint32_t{0x8090});
        reader.Set(0x200040, uintptr_t{0x300000});
        reader.Set(0x300050, uint8_t{2});
        reader.Set(0x300060, uint32_t{0x8099});
        if (scenario == 1) reader.shortAddress = 0x300060;
        if (scenario == 2) {
            reader.mutateAddress = 0x100010;
            reader.mutateValue = 0x10080;
            reader.mutateAtBatch = 2;
        }
        if (scenario == 3) {
            reader.mutateAddress = 0x200040;
            reader.mutateValue = 0x310000;
            reader.mutateAtBatch = 2;
        }
        if (scenario == 4) reader.Set(0x300050, uint8_t{4});
        if (scenario == 5) reader.Set(0x100010, uint32_t{0});
        if (scenario == 6) reader.Set(0x300050, uint8_t{255});
        if (scenario == 7) reader.Set(0x300050, uint8_t{3});
        uint32_t resolvedHandle = 0;
        ReadObserverBatch(samples, offsets,
            [&](uintptr_t address, void* output, size_t size, ObserverReadBytes* bytes) {
                reader.AddScatterReadRequest(0, address, output, size, bytes);
            }, [&]() { return reader.Execute(); },
            [&](uint32_t handle) { resolvedHandle = handle; return uintptr_t{0x200000}; },
            [](uintptr_t pointer) { return pointer >= 0x10000; });
        CHECK(resolvedHandle == (scenario == 5 ? 0x8090u : 0x8080u));
        CHECK(reader.batches == 3);
        const bool expectedResolved = scenario == 0 || scenario == 4 || scenario == 5 || scenario == 7;
        CHECK(samples[0].resolved == expectedResolved);
        CHECK(!samples[1].resolved);
        ObserverState state{0x100000, 0x8080, 0x8099, 2, 1000000};
        CommitObserverSample(state, samples[0], 1100000);
        CHECK(state.sampleUs == (expectedResolved ? 1100000u : 1000000u));
        if (scenario == 4) CHECK(!IsFollowingObserver(state.mode));
        if (scenario == 7) CHECK(IsFollowingObserver(state.mode));
        ObserverSample gap;
        gap.controller = 0x100000;
        CommitObserverSample(state, gap, 1700000);
        CHECK(state.sampleUs == 0);
    }
    ObserverState state{0x100000, 0x8080, 0x8099, 2, 1000000};
    CHECK(IsObserverStateFresh(state, 0x100000, 1500000));
    CHECK(!IsObserverStateFresh(state, 0x100000, 1500001));
    CHECK(!IsObserverStateFresh(state, 0x100001, 1100000));
    CHECK(!IsObserverStateFresh(state, 0x100000, 900000));
    ObserverSample replacement;
    replacement.controller = 0x100001;
    CommitObserverSample(state, replacement, 1100000);
    CHECK(state.sampleUs == 0);
    CHECK(IsFollowingObserver(2) && IsFollowingObserver(3));
    CHECK(!IsFollowingObserver(1) && !IsFollowingObserver(4) && !IsFollowingObserver(5));
    CHECK(IsObserverHandle(0x8080) && !IsObserverHandle(0xFFFFFFFFu));
    Vector3 points[64] = {};
    points[0] = {NAN, 0, 0};
    points[1] = {200, 300, 50};
    points[63] = {400, 500, 50};
    Vector3 origin = {};
    const auto valid = [](const Vector3& p) {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
            (std::fabs(p.x) + std::fabs(p.y) + std::fabs(p.z)) > 1.0f;
    };
    CHECK(ResolveInfernoOrigin(points, sizeof(points), 2, true, origin, valid));
    CHECK(origin.x == 200 && origin.y == 300);
    CHECK(ResolveInfernoOrigin(points, sizeof(points) - 1, 2, true, origin, valid));
    CHECK(ResolveInfernoOrigin(points, sizeof(Vector3) * 2, 2, true, origin, valid));
    CHECK(!ResolveInfernoOrigin(points, sizeof(Vector3) * 2 - 1, 2, true, origin, valid));
    CHECK(!ResolveInfernoOrigin(points, sizeof(points), 2, false, origin, valid));
    CHECK(!ResolveInfernoOrigin(points, sizeof(points), 1, true, origin, valid));
    CHECK(!ResolveInfernoOrigin(points, sizeof(points), 0, true, origin, valid));
    CHECK(!ResolveInfernoOrigin(points, sizeof(points), 65, true, origin, valid));
    CHECK(!ResolveInfernoOrigin(points, sizeof(points), -1, true, origin, valid));
    points[1] = {};
    CHECK(ResolveInfernoOrigin(points, sizeof(points), 64, true, origin, valid));
    CHECK(origin.x == 400);
    CHECK(CalculateUtilityRemainingFromTick(6400, 7, 1.0f / 64, 102) == 5.0f);
    CHECK(IsUtilityTickExpired(6400, 7, 1.0f / 64, 108));
}
