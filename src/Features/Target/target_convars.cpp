#include "Features/Target/target_convars.h"
#include "Features/Target/convar_snapshot_worker.h"
#include "Features/ESP/Recovery/dma_read_session.h"
#include "app/Core/remote_string.h"

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
        uint8_t readFailures = 0;
    };

    std::array<RequestedConVar, 11> s_requested = {{
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
        {"weapon_recoil_scale", ValueKind::Float},
    }};

    struct RemoteSection
    {
        uintptr_t address = 0;
        uint32_t size = 0;
        uint32_t characteristics = 0;
        char name[9] = {};
        std::vector<uint8_t> bytes;
    };

    uintptr_t s_tier0Base = 0;
    uintptr_t s_cvarInstance = 0;
    uint64_t s_lastResolveAttemptUs = 0;
    uint8_t s_optionalResolveAttempts = 0;
    uint8_t s_resolutionReadFailures = 0;

    uint64_t NowUs()
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    bool IsGamePointer(uintptr_t value)
    {
        return app::memory_address::IsCanonicalUserPointer(value);
    }

    struct ReadContext
    {
        const target::convars::detail::SnapshotWorker::Cancellation& canceled;

        bool Read(uintptr_t address, void* output, size_t size) const
        {
            if (!IsGamePointer(address) || !output || size == 0 ||
                size > app::memory_address::kMaximumUserAddress - address)
                return false;
            auto* bytes = static_cast<uint8_t*>(output);
            for (size_t offset = 0; offset < size;) {
                const size_t count = (std::min)(size - offset, size_t{1024 * 1024});
                if (canceled() || !mem.Read(address + offset, bytes + offset, count)) return false;
                offset += count;
            }
            return !canceled();
        }
    };

    bool ReadRemoteSections(
        const ReadContext& context,
        uintptr_t module,
        std::vector<RemoteSection>& sections)
    {
        sections.clear();
        if (!IsGamePointer(module))
            return false;
        IMAGE_DOS_HEADER dos = {};
        if (!context.Read(module, &dos, sizeof(dos)) ||
            dos.e_magic != IMAGE_DOS_SIGNATURE ||
            dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000) {
            return false;
        }
        IMAGE_NT_HEADERS64 nt = {};
        if (!context.Read(
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
        if (!context.Read(
                sectionTable,
                headers.data(),
                headers.size() * sizeof(IMAGE_SECTION_HEADER))) {
            return false;
        }
        for (const IMAGE_SECTION_HEADER& header : headers) {
            if (context.canceled()) return false;
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
            if (!context.Read(
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
        const ReadContext& context,
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
                if ((offset & 4095u) == 0 && context.canceled()) return 0;
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
        const ReadContext& context,
        uintptr_t module,
        const std::vector<RemoteSection>& sections,
        std::string_view className)
    {
        const std::string descriptor =
            ".?AV" + std::string(className) + "@@";
        uintptr_t typeDescriptor = 0;
        for (const RemoteSection& section : sections) {
            if (context.canceled()) return 0;
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
                if ((offset & 4095u) == 0 && context.canceled()) return 0;
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
            context,
            sections,
            completeObjectLocator,
            IMAGE_SCN_MEM_READ);
        if (!locatorReference)
            return 0;
        const uintptr_t vtable = locatorReference + sizeof(uintptr_t);
        return FindQword(
            context,
            sections,
            vtable,
            IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE);
    }

    bool ResolveConVars(const ReadContext& context, uint64_t nowUs)
    {
        if (context.canceled()) return false;
        if (s_lastResolveAttemptUs != 0 && nowUs >= s_lastResolveAttemptUs &&
            nowUs - s_lastResolveAttemptUs < target::convars::policy::kResolveRetryUs) {
            return false;
        }
        s_lastResolveAttemptUs = nowUs;
        const uintptr_t tier0 = mem.GetModuleBase("tier0.dll");
        if (!IsGamePointer(tier0))
            return false;
        if (tier0 != s_tier0Base) {
            s_tier0Base = tier0;
            s_cvarInstance = 0;
            s_optionalResolveAttempts = 0;
            s_resolutionReadFailures = 0;
            for (RequestedConVar& entry : s_requested) {
                entry.pointer = 0;
                entry.readFailures = 0;
            }
        }
        uintptr_t cvarInstance = s_cvarInstance;
        if (!IsGamePointer(cvarInstance)) {
            std::vector<RemoteSection> sections;
            if (!ReadRemoteSections(context, tier0, sections))
                return false;
            cvarInstance = FindVtableInstance(context, tier0, sections, "CCvar");
        }
        if (!IsGamePointer(cvarInstance))
            return false;
        const auto readFailure = [&] {
            s_resolutionReadFailures = target::convars::policy::NextReadFailureCount(
                s_resolutionReadFailures, false);
            if (s_resolutionReadFailures >= target::convars::policy::kReadFailuresBeforeResolve)
                s_cvarInstance = 0;
            return false;
        };
        uintptr_t list = 0;
        if (!context.Read(
                cvarInstance + 0x50u,
                &list,
                sizeof(list)) ||
            !IsGamePointer(list))
            return readFailure();
        std::unordered_set<uint16_t> visited;
        std::array<uintptr_t, s_requested.size()> resolvedPointers{};
        bool namesComplete = true;
        uint16_t current = 0;
        for (size_t iteration = 0; iteration < 65536u; ++iteration) {
            if (context.canceled()) return false;
            if (current == UINT16_MAX)
                break;
            if (!visited.insert(current).second)
                return readFailure();
            const uintptr_t entryAddress = list +
                static_cast<uintptr_t>(current) * 16u;
            uintptr_t convar = 0;
            uint16_t next = UINT16_MAX;
            if (!context.Read(entryAddress, &convar, sizeof(convar)) ||
                !context.Read(entryAddress + 10u, &next, sizeof(next))) {
                return readFailure();
            }
            if (IsGamePointer(convar)) {
                uintptr_t namePointer = 0;
                if (!context.Read(convar, &namePointer, sizeof(namePointer))) {
                    namesComplete = false;
                } else if (IsGamePointer(namePointer)) {
                    const auto name = app::remote_string::ReadName(namePointer, 128,
                        [&](uintptr_t address, void* output, size_t size) {
                            return context.Read(address, output, size);
                        });
                    if (!name.text.empty()) {
                        for (size_t index = 0; index < s_requested.size(); ++index) {
                            if (resolvedPointers[index] == 0 &&
                                std::string_view(s_requested[index].name) ==
                                    name.text) {
                                resolvedPointers[index] = convar;
                            }
                        }
                    }
                    if (name.readFailed) namesComplete = false;
                }
            }
            current = next;
        }
        if (current != UINT16_MAX)
            return readFailure();
        s_cvarInstance = cvarInstance;
        s_resolutionReadFailures = 0;
        if (namesComplete) {
            s_optionalResolveAttempts = std::min<uint8_t>(
                static_cast<uint8_t>(s_optionalResolveAttempts + 1),
                target::convars::policy::kMaximumOptionalResolveAttempts);
        }
        for (size_t index = 0; index < s_requested.size(); ++index) {
            if (IsGamePointer(resolvedPointers[index]) &&
                resolvedPointers[index] != s_requested[index].pointer) {
                s_requested[index].pointer = resolvedPointers[index];
                s_requested[index].readFailures = 0;
            }
        }
        return std::all_of(
            s_requested.begin(),
            s_requested.begin() + 7,
            [](const RequestedConVar& entry) {
                return IsGamePointer(entry.pointer);
            });
    }

    template <typename T>
    bool ReadValue(const ReadContext& context, size_t index, T& value)
    {
        if (index >= s_requested.size() ||
            !IsGamePointer(s_requested[index].pointer)) {
            return false;
        }
        const bool succeeded = context.Read(
            s_requested[index].pointer + 0x58u,
            &value,
            sizeof(value));
        s_requested[index].readFailures = target::convars::policy::NextReadFailureCount(
            s_requested[index].readFailures, succeeded);
        return succeeded;
    }

    target::convars::Values Refresh(const ReadContext& context)
    {
        const uint64_t nowUs = NowUs();
        bool requiredMissing = false;
        bool optionalMissing = false;
        bool repeatedReadFailure = false;
        for (size_t index = 0; index < s_requested.size(); ++index) {
            if (!IsGamePointer(s_requested[index].pointer)) {
                if (index < 7) requiredMissing = true;
                else optionalMissing = true;
            }
            repeatedReadFailure = repeatedReadFailure || s_requested[index].readFailures >=
                target::convars::policy::kReadFailuresBeforeResolve;
        }
        if (target::convars::policy::ShouldResolve(nowUs, s_lastResolveAttemptUs,
                IsGamePointer(s_cvarInstance), requiredMissing, optionalMissing,
                s_optionalResolveAttempts, repeatedReadFailure))
            ResolveConVars(context, nowUs);
        target::convars::Values next;
        if (context.canceled()) return next;
        next.updatedAtUs = NowUs();
        float forceSpread = 0.0f;
        uint8_t noSpread = 0;
        float jumpImpulse = 0.0f;
        float ctHead = 0.0f;
        float tHead = 0.0f;
        float ctBody = 0.0f;
        float tBody = 0.0f;
        next.accuracyValid =
            ReadValue(context, 0, forceSpread) && ReadValue(context, 1, noSpread) &&
            std::isfinite(forceSpread) && forceSpread >= -1.0f &&
            forceSpread <= 1.0f && noSpread <= 1u;
        if (next.accuracyValid) {
            next.weaponAccuracyForceSpread = forceSpread;
            next.weaponAccuracyNoSpread = noSpread != 0u;
        }
        next.jumpValid = ReadValue(context, 2, jumpImpulse) &&
            std::isfinite(jumpImpulse) && jumpImpulse >= 50.0f &&
            jumpImpulse <= 1000.0f;
        if (next.jumpValid)
            next.jumpImpulse = jumpImpulse;
        next.damageScaleValid =
            ReadValue(context, 3, ctHead) && ReadValue(context, 4, tHead) &&
            ReadValue(context, 5, ctBody) && ReadValue(context, 6, tBody) &&
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
            ReadValue(context, 7, interpolation) && ReadValue(context, 8, ratio) &&
            ReadValue(context, 9, updateRate) && std::isfinite(interpolation) &&
            std::isfinite(ratio) && interpolation >= 0.0f &&
            interpolation <= 0.25f && ratio >= 0.0f && ratio <= 10.0f &&
            updateRate >= 16 && updateRate <= 1024;
        if (next.interpolationValid) {
            next.clientInterpolation = interpolation;
            next.clientInterpolationRatio = ratio;
            next.clientUpdateRate = updateRate;
        }
        float recoilScale = 2.0f;
        next.recoilScaleValid = ReadValue(context, 10, recoilScale) && std::isfinite(recoilScale) &&
            recoilScale >= 0.0f && recoilScale <= 10.0f;
        if (next.recoilScaleValid) next.recoilScale = recoilScale;
        next.resolved = next.accuracyValid && next.jumpValid &&
            next.damageScaleValid;
        return context.canceled() ? target::convars::Values{} : next;
    }

    void ResetResolver()
    {
        s_tier0Base = 0;
        s_cvarInstance = 0;
        s_lastResolveAttemptUs = 0;
        s_optionalResolveAttempts = 0;
        s_resolutionReadFailures = 0;
        for (RequestedConVar& entry : s_requested) {
            entry.pointer = 0;
            entry.readFailures = 0;
        }
    }

    target::convars::detail::SnapshotWorker s_worker(
        [](const auto& canceled) {
            esp::recovery::DmaReadSession session;
            if (!session.Valid()) return target::convars::Values{};
            const target::convars::detail::SnapshotWorker::Cancellation unavailable = [&] {
                return canceled() || !session.Valid();
            };
            return Refresh(ReadContext{unavailable});
        }, ResetResolver, NowUs);
}

void target::convars::Start()
{
    s_worker.Start();
}

target::convars::Values target::convars::Read(uint64_t sceneSerial)
{
    return s_worker.Read(sceneSerial);
}

void target::convars::Reset()
{
    s_worker.Stop();
}
