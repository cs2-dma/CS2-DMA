#include "Features/Target/target_convars.h"

#include <Windows.h>
#include <DMALibrary/Memory/Memory.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace
{
    constexpr uint64_t kRefreshIntervalUs = 250000u;
    constexpr uint64_t kResolveRetryUs = 5000000u;

    enum class ValueKind : uint8_t
    {
        Boolean,
        Float,
        Integer,
    };

    struct RequestedConVar
    {
        const char* name;
        ValueKind kind;
        uintptr_t pointer = 0;
    };

    std::array<RequestedConVar, 10> s_requested = {{
        {"weapon_accuracy_forcespread", ValueKind::Float},
        {"weapon_accuracy_nospread", ValueKind::Boolean},
        {"sv_jump_impulse", ValueKind::Float},
        {"mp_damage_scale_ct_head", ValueKind::Float},
        {"mp_damage_scale_t_head", ValueKind::Float},
        {"mp_damage_scale_ct_body", ValueKind::Float},
        {"mp_damage_scale_t_body", ValueKind::Float},
        {"cl_interp", ValueKind::Float},
        {"cl_interp_ratio", ValueKind::Float},
        {"cl_updaterate", ValueKind::Integer},
    }};

    struct RemoteSection
    {
        uintptr_t address = 0;
        uint32_t size = 0;
        uint32_t characteristics = 0;
        char name[9] = {};
        std::vector<uint8_t> bytes;
    };

    std::mutex s_mutex;
    target::convars::Values s_values;
    uintptr_t s_tier0Base = 0;
    uintptr_t s_cvarInstance = 0;
    uint64_t s_lastRefreshUs = 0;
    uint64_t s_lastResolveAttemptUs = 0;

    uint64_t NowUs()
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    bool IsGamePointer(uintptr_t value)
    {
        return value >= 0x10000ull && value < 0x000F000000000000ull;
    }

    bool ReadRemoteSections(
        uintptr_t module,
        std::vector<RemoteSection>& sections)
    {
        sections.clear();
        if (!IsGamePointer(module))
            return false;
        IMAGE_DOS_HEADER dos = {};
        if (!mem.Read(module, &dos, sizeof(dos)) ||
            dos.e_magic != IMAGE_DOS_SIGNATURE ||
            dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000) {
            return false;
        }
        IMAGE_NT_HEADERS64 nt = {};
        if (!mem.Read(
                module + static_cast<uintptr_t>(dos.e_lfanew),
                &nt,
                sizeof(nt)) ||
            nt.Signature != IMAGE_NT_SIGNATURE ||
            nt.FileHeader.NumberOfSections == 0 ||
            nt.FileHeader.NumberOfSections > 96) {
            return false;
        }
        const uintptr_t sectionTable = module +
            static_cast<uintptr_t>(dos.e_lfanew) +
            offsetof(IMAGE_NT_HEADERS64, OptionalHeader) +
            nt.FileHeader.SizeOfOptionalHeader;
        std::vector<IMAGE_SECTION_HEADER> headers(
            nt.FileHeader.NumberOfSections);
        if (!mem.Read(
                sectionTable,
                headers.data(),
                headers.size() * sizeof(IMAGE_SECTION_HEADER))) {
            return false;
        }
        for (const IMAGE_SECTION_HEADER& header : headers) {
            const uint32_t size = std::max(
                header.Misc.VirtualSize,
                header.SizeOfRawData);
            if (size == 0 || size > 64u * 1024u * 1024u)
                continue;
            RemoteSection section;
            section.address = module + header.VirtualAddress;
            section.size = size;
            section.characteristics = header.Characteristics;
            std::memcpy(section.name, header.Name, 8u);
            section.bytes.resize(size);
            if (!mem.Read(
                    section.address,
                    section.bytes.data(),
                    section.bytes.size())) {
                continue;
            }
            sections.push_back(std::move(section));
        }
        return !sections.empty();
    }

    uintptr_t FindQword(
        const std::vector<RemoteSection>& sections,
        uintptr_t value,
        uint32_t requiredCharacteristics)
    {
        for (const RemoteSection& section : sections) {
            if ((section.characteristics & requiredCharacteristics) !=
                    requiredCharacteristics ||
                section.bytes.size() < sizeof(uintptr_t)) {
                continue;
            }
            for (size_t offset = 0;
                 offset + sizeof(uintptr_t) <= section.bytes.size();
                 offset += sizeof(uintptr_t)) {
                uintptr_t candidate = 0;
                std::memcpy(
                    &candidate,
                    section.bytes.data() + offset,
                    sizeof(candidate));
                if (candidate == value)
                    return section.address + offset;
            }
        }
        return 0;
    }

    uintptr_t FindVtableInstance(
        uintptr_t module,
        const std::vector<RemoteSection>& sections,
        std::string_view className)
    {
        const std::string descriptor =
            ".?AV" + std::string(className) + "@@";
        uintptr_t typeDescriptor = 0;
        for (const RemoteSection& section : sections) {
            constexpr uint32_t required =
                IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ;
            if ((section.characteristics & required) != required ||
                section.bytes.size() <= descriptor.size()) {
                continue;
            }
            const auto found = std::search(
                section.bytes.begin(),
                section.bytes.end(),
                descriptor.begin(),
                descriptor.end());
            if (found == section.bytes.end())
                continue;
            const size_t offset = static_cast<size_t>(
                found - section.bytes.begin());
            if (offset < 0x10u)
                continue;
            typeDescriptor = section.address + offset - 0x10u;
            break;
        }
        if (!typeDescriptor || typeDescriptor < module ||
            typeDescriptor - module > UINT32_MAX) {
            return 0;
        }
        const uint32_t descriptorRva = static_cast<uint32_t>(
            typeDescriptor - module);
        uintptr_t completeObjectLocator = 0;
        for (const RemoteSection& section : sections) {
            if (std::string_view(section.name).find(".rdata") ==
                    std::string_view::npos ||
                section.bytes.size() < 0x30u) {
                continue;
            }
            for (size_t offset = 0;
                 offset + 0x30u <= section.bytes.size();
                 offset += 8u) {
                uint32_t candidate = 0;
                std::memcpy(
                    &candidate,
                    section.bytes.data() + offset + 12u,
                    sizeof(candidate));
                if (candidate == descriptorRva) {
                    completeObjectLocator = section.address + offset;
                    break;
                }
            }
            if (completeObjectLocator)
                break;
        }
        if (!completeObjectLocator)
            return 0;
        const uintptr_t locatorReference = FindQword(
            sections,
            completeObjectLocator,
            IMAGE_SCN_MEM_READ);
        if (!locatorReference)
            return 0;
        const uintptr_t vtable = locatorReference + sizeof(uintptr_t);
        return FindQword(
            sections,
            vtable,
            IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE);
    }

    bool ResolveConVars(uint64_t nowUs)
    {
        if (s_lastResolveAttemptUs != 0 && nowUs >= s_lastResolveAttemptUs &&
            nowUs - s_lastResolveAttemptUs < kResolveRetryUs) {
            return false;
        }
        s_lastResolveAttemptUs = nowUs;
        const uintptr_t tier0 = mem.GetModuleBase("tier0.dll");
        if (!IsGamePointer(tier0))
            return false;
        if (tier0 != s_tier0Base) {
            s_tier0Base = tier0;
            s_cvarInstance = 0;
            for (RequestedConVar& entry : s_requested)
                entry.pointer = 0;
        }
        std::vector<RemoteSection> sections;
        if (!ReadRemoteSections(tier0, sections))
            return false;
        s_cvarInstance = FindVtableInstance(tier0, sections, "CCvar");
        if (!IsGamePointer(s_cvarInstance))
            return false;
        uintptr_t list = 0;
        if (!mem.Read(
                s_cvarInstance + 0x50u,
                &list,
                sizeof(list)) ||
            !IsGamePointer(list))
            return false;
        std::unordered_set<uint16_t> visited;
        uint16_t current = 0;
        for (size_t iteration = 0; iteration < 65536u; ++iteration) {
            if (current == UINT16_MAX || !visited.insert(current).second)
                break;
            const uintptr_t entryAddress = list +
                static_cast<uintptr_t>(current) * 16u;
            uintptr_t convar = 0;
            uint16_t next = UINT16_MAX;
            if (!mem.Read(entryAddress, &convar, sizeof(convar)) ||
                !mem.Read(entryAddress + 10u, &next, sizeof(next))) {
                break;
            }
            if (IsGamePointer(convar)) {
                uintptr_t namePointer = 0;
                if (mem.Read(convar, &namePointer, sizeof(namePointer)) &&
                    IsGamePointer(namePointer)) {
                    char name[128] = {};
                    if (mem.Read(namePointer, name, sizeof(name) - 1u)) {
                        for (RequestedConVar& requested : s_requested) {
                            if (requested.pointer == 0 &&
                                std::string_view(requested.name) ==
                                    std::string_view(name)) {
                                requested.pointer = convar;
                            }
                        }
                    }
                }
            }
            current = next;
        }
        return std::all_of(
            s_requested.begin(),
            s_requested.begin() + 7,
            [](const RequestedConVar& entry) {
                return IsGamePointer(entry.pointer);
            });
    }

    template <typename T>
    bool ReadValue(size_t index, T& value)
    {
        if (index >= s_requested.size() ||
            !IsGamePointer(s_requested[index].pointer)) {
            return false;
        }
        return mem.Read(
            s_requested[index].pointer + 0x58u,
            &value,
            sizeof(value));
    }

    void Refresh(uint64_t nowUs)
    {
        if (!IsGamePointer(s_cvarInstance))
            ResolveConVars(nowUs);
        target::convars::Values next;
        next.updatedAtUs = nowUs;
        float forceSpread = 0.0f;
        uint8_t noSpread = 0;
        float jumpImpulse = 0.0f;
        float ctHead = 0.0f;
        float tHead = 0.0f;
        float ctBody = 0.0f;
        float tBody = 0.0f;
        next.accuracyValid =
            ReadValue(0, forceSpread) && ReadValue(1, noSpread) &&
            std::isfinite(forceSpread) && forceSpread >= -1.0f &&
            forceSpread <= 1.0f && noSpread <= 1u;
        if (next.accuracyValid) {
            next.weaponAccuracyForceSpread = forceSpread;
            next.weaponAccuracyNoSpread = noSpread != 0u;
        }
        next.jumpValid = ReadValue(2, jumpImpulse) &&
            std::isfinite(jumpImpulse) && jumpImpulse >= 50.0f &&
            jumpImpulse <= 1000.0f;
        if (next.jumpValid)
            next.jumpImpulse = jumpImpulse;
        next.damageScaleValid =
            ReadValue(3, ctHead) && ReadValue(4, tHead) &&
            ReadValue(5, ctBody) && ReadValue(6, tBody) &&
            std::isfinite(ctHead) && std::isfinite(tHead) &&
            std::isfinite(ctBody) && std::isfinite(tBody) &&
            ctHead >= 0.0f && ctHead <= 10.0f &&
            tHead >= 0.0f && tHead <= 10.0f &&
            ctBody >= 0.0f && ctBody <= 10.0f &&
            tBody >= 0.0f && tBody <= 10.0f;
        if (next.damageScaleValid) {
            next.damageScaleCtHead = ctHead;
            next.damageScaleTHead = tHead;
            next.damageScaleCtBody = ctBody;
            next.damageScaleTBody = tBody;
        }
        float interpolation = 0.0f;
        float ratio = 0.0f;
        int updateRate = 0;
        next.interpolationValid =
            ReadValue(7, interpolation) && ReadValue(8, ratio) &&
            ReadValue(9, updateRate) && std::isfinite(interpolation) &&
            std::isfinite(ratio) && interpolation >= 0.0f &&
            interpolation <= 0.25f && ratio >= 0.0f && ratio <= 10.0f &&
            updateRate >= 16 && updateRate <= 1024;
        if (next.interpolationValid) {
            next.clientInterpolation = interpolation;
            next.clientInterpolationRatio = ratio;
            next.clientUpdateRate = updateRate;
        }
        next.resolved = next.accuracyValid && next.jumpValid &&
            next.damageScaleValid;
        s_values = next;
        s_lastRefreshUs = nowUs;
    }
}

target::convars::Values target::convars::Read()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    const uint64_t nowUs = NowUs();
    if (s_lastRefreshUs == 0 || nowUs < s_lastRefreshUs ||
        nowUs - s_lastRefreshUs >= kRefreshIntervalUs) {
        Refresh(nowUs);
    }
    return s_values;
}

void target::convars::Reset()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_values = {};
    s_tier0Base = 0;
    s_cvarInstance = 0;
    s_lastRefreshUs = 0;
    s_lastResolveAttemptUs = 0;
    for (RequestedConVar& entry : s_requested)
        entry.pointer = 0;
}
