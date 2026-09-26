#pragma warning(push)
#pragma warning(disable: 4200)
#include "DMALibrary/pch.h"
#pragma warning(pop)

#include "../../../src/Features/Target/physics_bvh.cpp"
#include <iostream>
#include <map>

namespace fixture
{
    std::map<uintptr_t, std::vector<uint8_t>> memory;
    std::shared_timed_mutex lifecycle;
    int failures = 0;
    int reads = 0;
    size_t largestRead = 0;
    int deferredReads = 0;
    template <typename T>
    void Put(uintptr_t base, size_t offset, const T& value)
    {
        auto& bytes = memory[base];
        bytes.resize((std::max)(bytes.size(), offset + sizeof(value)));
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }
    void Check(bool result, const char* expression, int line)
    {
        if (!result) { ++failures; std::cerr << line << ": " << expression << '\n'; }
    }
}
#define CHECK(value) fixture::Check((value), #value, __LINE__)

Memory::~Memory() = default;
size_t Memory::GetModuleBase(const std::string&) { return 0; }
bool Memory::Read(uintptr_t address, void* output, size_t size) const
{
    ++fixture::reads;
    fixture::largestRead = (std::max)(fixture::largestRead, size);
    auto it = fixture::memory.upper_bound(address);
    if (it == fixture::memory.begin()) return false;
    --it;
    const size_t offset = address - it->first;
    if (offset > it->second.size() || size > it->second.size() - offset) return false;
    std::memcpy(output, it->second.data() + offset, size);
    return true;
}
bool Memory::ReadCached(uintptr_t address, void* output, size_t size) const { return Read(address, output, size); }
bool Memory::TryReadBackground(uintptr_t address, void* output, size_t size, bool, bool& deferred) const
{
    deferred = fixture::deferredReads > 0;
    if (deferred) { --fixture::deferredReads; return false; }
    return Read(address, output, size);
}
esp::recovery::DmaReadSession::DmaReadSession() : lock_(fixture::lifecycle) {}
bool esp::recovery::DmaReadSession::Valid() const noexcept { return lock_.owns_lock(); }

int main()
{
    {
        dma::ReadPriority priority;
        CHECK(priority.TryBackground().owns_lock());
        auto background = priority.TryBackground();
        CHECK(background.owns_lock());
        std::atomic<bool> entered{false}, release{false};
        std::thread foreground([&] {
            dma::ReadPriority::Foreground read(priority);
            entered.store(true);
            while (!release.load()) std::this_thread::yield();
        });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (priority.ForegroundCount() == 0 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        CHECK(priority.ForegroundCount() == 1);
        CHECK(!entered.load());
        CHECK(!priority.TryBackground().owns_lock());
        background.unlock();
        while (!entered.load() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        CHECK(entered.load());
        CHECK(!priority.TryBackground().owns_lock());
        release.store(true);
        foreground.join();
        CHECK(priority.ForegroundCount() == 0);
        CHECK(priority.TryBackground().owns_lock());
    }
    {
        target::physics::read_policy::BackgroundBudget budget;
        CHECK(budget.DelayUs(1000) == 0);
        budget.Account(1000, 1500);
        CHECK(budget.DelayUs(1500) == 3500);
        CHECK(budget.DelayUs(5000) == 0);
        budget.Account(5000, 5500, true);
        CHECK(budget.DelayUs(5500) == 7500);
        budget.Account(13000, 72000);
        CHECK(budget.DelayUs(72000) == 100000);
        budget.Account(1000, 500);
        CHECK(budget.DelayUs(500) == 0);

        fixture::memory[0x100000].resize(100000, 0xA5);
        std::vector<uint8_t> result(100000);
        BuildContext context;
        fixture::reads = 0;
        CHECK(context.Read(0x100000, result.data(), result.size()));
        CHECK(fixture::reads == 13 && fixture::largestRead <= 8192);
        CHECK(context.bytesRead == result.size());
        CHECK(std::all_of(result.begin(), result.end(), [](uint8_t byte) { return byte == 0xA5; }));
        fixture::reads = 0;
        context.canceled = [] { return fixture::reads >= 1; };
        CHECK(!context.Read(0x100000, result.data(), result.size()));
        CHECK(fixture::reads == 1);
        context.canceled = [] { return true; };
        CHECK(!context.Read(0x100000, result.data(), result.size()));
        CHECK(fixture::reads == 1);
        fixture::reads = 0;
        fixture::deferredReads = 3;
        context.canceled = [] { return fixture::deferredReads == 0; };
        CHECK(!context.Read(0x100000, result.data(), result.size()));
        CHECK(fixture::reads == 0);
        context.canceled = {};
        fixture::deferredReads = 2;
        CHECK(context.Read(0x100000, result.data(), result.size()));
        CHECK(fixture::reads == 13);
        fixture::memory.clear();
    }
    using fixture::Put;
    for (const bool mesh : {false, true}) {
        for (const size_t scaleOffset : {size_t{0xB0}, size_t{0xB8}}) {
            fixture::memory.clear();
            fixture::reads = 0;
            fixture::memory[0x50000].resize(0x140);
            fixture::memory[0x60000].resize(0x100);
            Put(0x50000, 0, mesh ? uintptr_t{0x11000} : uintptr_t{0x10000});
            Put(0x50000, 0x18, mesh ? uint8_t{3} : uint8_t{2});
            Put(0x50000, 0x28, 1.0f);
            Put(0x50000, 0x50, uint64_t{1});
            Put(0x50000, scaleOffset, 2.0f);
            Put(0x50000, scaleOffset + (mesh ? 16 : 8), uintptr_t{0x60000});
            const float vertices[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
            Put(0x70000, 0, vertices);
            if (mesh) {
                Put(0x50000, scaleOffset + 4, 3.0f);
                Put(0x50000, scaleOffset + 8, 4.0f);
                Put(0x50000, 0x100, Vector3{10, 20, 30});
                Put(0x50000, 0x130, Quaternion{});
                Put(0x60000, 0x18, int{1}); Put(0x60000, 0x20, uintptr_t{0x80000});
                Put(0x60000, 0x30, int{3}); Put(0x60000, 0x38, uintptr_t{0x70000});
                Put(0x60000, 0x48, int{1}); Put(0x60000, 0x50, uintptr_t{0x90000});
                InnerNode node{};
                node.packed0 = (3u << 30) | 1u;
                Put(0x80000, 0, node);
                const int indices[3] = {0, 1, 2};
                Put(0x90000, 0, indices);
            } else {
                Put(0x60000, 0x88, int{3}); Put(0x60000, 0x90, uintptr_t{0x70000});
                Put(0x60000, 0xA0, int{3}); Put(0x60000, 0xA8, uintptr_t{0x80000});
                Put(0x60000, 0xB8, int{1}); Put(0x60000, 0xC0, uintptr_t{0x90000});
                const HalfEdge edges[3] = {{1, 0, 0, 0}, {2, 0, 1, 0}, {0, 0, 2, 0}};
                Put(0x80000, 0, edges);
                Put(0x90000, 0, uint8_t{0});
            }
            BuildContext context;
            std::vector<Triangle> triangles;
            ProcessShape(context, 0x50000, 0x10000, 0x11000, {}, triangles);
            CHECK(triangles.size() == 1);
            CHECK(context.shiftedShapes == (scaleOffset == 0xB8 ? 1u : 0u));
            CHECK(context.legacyShapes == (scaleOffset == 0xB0 ? 1u : 0u));
            if (!triangles.empty()) {
                const auto& triangle = triangles[0];
                CHECK(triangle.v0.z == (mesh ? 30.0f : 0.0f));
                CHECK(triangle.v1.x == (mesh ? 12.0f : 2.0f));
                CHECK(triangle.v2.y == (mesh ? 23.0f : 2.0f));
            }
            BvhData data;
            data.triangles = triangles;
            std::string error;
            CHECK(data.BuildAcceleration(error));
            const float x = mesh ? 10.5f : 0.5f;
            const float y = mesh ? 20.5f : 0.5f;
            const float z = mesh ? 30.0f : 0.0f;
            CHECK(data.Trace({x, y, z + 10}, {x, y, z - 10}).hit);
            triangles.clear();
            if (mesh) Put(0x90000, 8, int{3});
            else Put(0x80000, 8, uint8_t{1});
            ProcessShape(context, 0x50000, 0x10000, 0x11000, {}, triangles);
            CHECK(triangles.empty());
            const int before = fixture::reads;
            context.canceled = [] { return true; };
            ProcessShape(context, 0x50000, 0x10000, 0x11000, {}, triangles);
            CHECK(fixture::reads == before);
        }
    }
    if (fixture::failures != 0) return 1;
    std::cout << "Physics shape tests passed: legacy/shifted hull and mesh extraction, transforms, BVH traces, invalid topology and cancellation.\n";
}
