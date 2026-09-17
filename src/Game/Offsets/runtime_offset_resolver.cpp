#include "Game/Offsets/runtime_offset_resolver.h"

#include "Game/Offsets/runtime_resolver_policy.h"
#include "app/Core/memory_address.h"

#include <DMALibrary/Memory/Memory.h>
#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
    using runtime_offsets::resolver::Result;
    using runtime_offsets::resolver::SchemaRequest;
    using runtime_offsets::resolver_policy::CompilePattern;
    using runtime_offsets::resolver_policy::DecodeSchemaHashNode;
    using runtime_offsets::resolver_policy::AddRvaOffset;
    using runtime_offsets::resolver_policy::FindPatternMatches;
    using runtime_offsets::resolver_policy::PatternByte;
    using runtime_offsets::resolver_policy::ResolveRelativeRva;

    constexpr std::size_t kReadChunkSize = 256u * 1024u;
    constexpr std::size_t kCodeReadChunkSize = 64u * 1024u;
    constexpr std::size_t kCodePageSize = 4u * 1024u;
    constexpr std::size_t kMaximumModuleSize = 256u * 1024u * 1024u;
    constexpr std::size_t kExpectedResolvedOffsets = 18;
    constexpr std::size_t kMaximumSchemaClasses = 8192;
    constexpr std::size_t kMaximumSchemaFieldsPerClass = 512;
    constexpr std::size_t kSchemaHashBucketCount = 256;
    constexpr std::size_t kSchemaHashBucketSize = 0x18;
    constexpr std::size_t kSchemaHashBucketsOffset = 0x60;

    struct CodeSection {
        std::uint32_t rva = 0;
        std::vector<std::uint8_t> bytes;
    };

    struct ModuleCode {
        std::string name;
        std::uintptr_t base = 0;
        std::uint32_t imageSize = 0;
        std::vector<CodeSection> sections;
    };

    bool ReadResolverMemory(
        std::uintptr_t address,
        void* buffer,
        std::size_t size)
    {
        return mem.ReadCached(address, buffer, size) ||
               mem.Read(address, buffer, size);
    }

    template <typename T>
    bool ReadValue(std::uintptr_t address, T& value, Result& result)
    {
        value = {};
        if (!address || !ReadResolverMemory(address, &value, sizeof(value)))
            return false;
        result.bytesRead += sizeof(value);
        return true;
    }

    bool ReadRange(
        std::uintptr_t address,
        std::span<std::uint8_t> output,
        Result& result)
    {
        if (!address || output.empty())
            return false;

        std::size_t completed = 0;
        while (completed < output.size()) {
            const std::size_t chunk = std::min(kReadChunkSize, output.size() - completed);
            if (!ReadResolverMemory(address + completed, output.data() + completed, chunk))
                return false;
            completed += chunk;
            result.bytesRead += chunk;
        }
        return true;
    }

    bool ReadCodeRange(
        std::uintptr_t address,
        std::span<std::uint8_t> output,
        Result& result)
    {
        if (!address || output.empty())
            return false;

        bool readAnyPage = false;
        std::fill(output.begin(), output.end(), std::uint8_t{0});
        for (std::size_t chunkOffset = 0;
             chunkOffset < output.size();
             chunkOffset += kCodeReadChunkSize) {
            const std::size_t chunkSize = std::min(
                kCodeReadChunkSize,
                output.size() - chunkOffset);
            if (ReadResolverMemory(
                    address + chunkOffset,
                    output.data() + chunkOffset,
                    chunkSize)) {
                result.bytesRead += chunkSize;
                readAnyPage = true;
                continue;
            }

            std::fill_n(output.data() + chunkOffset, chunkSize, std::uint8_t{0});
            for (std::size_t pageOffset = 0;
                 pageOffset < chunkSize;
                 pageOffset += kCodePageSize) {
                const std::size_t pageSize = std::min(
                    kCodePageSize,
                    chunkSize - pageOffset);
                if (ReadResolverMemory(
                        address + chunkOffset + pageOffset,
                        output.data() + chunkOffset + pageOffset,
                        pageSize)) {
                    result.bytesRead += pageSize;
                    readAnyPage = true;
                } else {
                    std::fill_n(
                        output.data() + chunkOffset + pageOffset,
                        pageSize,
                        std::uint8_t{0});
                    ++result.unreadableCodePages;
                }
            }
        }
        return readAnyPage;
    }

    template <typename T>
    bool ReadObjects(
        std::uintptr_t address,
        std::span<T> output,
        Result& result)
    {
        return ReadRange(
            address,
            std::span<std::uint8_t>(
                reinterpret_cast<std::uint8_t*>(output.data()),
                output.size_bytes()),
            result);
    }

    bool IsGamePointer(std::uintptr_t value) noexcept
    {
        return app::memory_address::IsLikelyGamePointer(value);
    }

    std::string ReadCString(
        std::uintptr_t address,
        std::size_t capacity,
        Result& result)
    {
        if (!IsGamePointer(address) || capacity < 2)
            return {};
        capacity = std::min<std::size_t>(capacity, 256);
        std::array<char, 256> buffer = {};
        if (!ReadResolverMemory(address, buffer.data(), capacity))
            return {};
        result.bytesRead += capacity;
        buffer[capacity - 1] = '\0';
        const std::size_t length = std::char_traits<char>::length(buffer.data());
        if (length == 0 || length >= capacity)
            return {};
        for (std::size_t i = 0; i < length; ++i) {
            const unsigned char ch = static_cast<unsigned char>(buffer[i]);
            if (ch < 0x20 || ch == 0x7F)
                return {};
        }
        return std::string(buffer.data(), length);
    }

    bool ReadModuleCode(const char* moduleName, ModuleCode& output, Result& result)
    {
        output = {};
        const std::uintptr_t base = mem.GetModuleBase(moduleName);
        if (!base)
            return false;

        IMAGE_DOS_HEADER dos = {};
        if (!ReadValue(base, dos, result) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
            dos.e_lfanew <= 0 || dos.e_lfanew > 0x4000) {
            return false;
        }

        IMAGE_NT_HEADERS64 nt = {};
        if (!ReadValue(base + static_cast<std::uintptr_t>(dos.e_lfanew), nt, result) ||
            nt.Signature != IMAGE_NT_SIGNATURE ||
            nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt.FileHeader.NumberOfSections == 0 ||
            nt.FileHeader.NumberOfSections > 96 ||
            nt.OptionalHeader.SizeOfImage == 0 ||
            nt.OptionalHeader.SizeOfImage > kMaximumModuleSize) {
            return false;
        }

        const std::uintptr_t sectionTable =
            base + static_cast<std::uintptr_t>(dos.e_lfanew) +
            sizeof(std::uint32_t) + sizeof(IMAGE_FILE_HEADER) +
            nt.FileHeader.SizeOfOptionalHeader;
        std::vector<IMAGE_SECTION_HEADER> headers(nt.FileHeader.NumberOfSections);
        if (!ReadObjects(sectionTable, std::span(headers), result)) {
            return false;
        }

        output.name = moduleName;
        output.base = base;
        output.imageSize = nt.OptionalHeader.SizeOfImage;
        for (const IMAGE_SECTION_HEADER& header : headers) {
            if ((header.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
                continue;
            const std::uint32_t rva = header.VirtualAddress;
            const std::uint32_t virtualSize = header.Misc.VirtualSize;
            if (rva == 0 || virtualSize == 0 || rva >= output.imageSize)
                continue;
            const std::size_t size = std::min<std::size_t>(
                virtualSize,
                static_cast<std::size_t>(output.imageSize - rva));
            CodeSection section;
            section.rva = rva;
            section.bytes.resize(size);
            if (!ReadCodeRange(base + rva, section.bytes, result))
                continue;
            output.sections.push_back(std::move(section));
            ++result.executableSectionsRead;
        }
        if (output.sections.empty())
            return false;
        ++result.modulesRead;
        return true;
    }

    struct PatternMatch {
        const CodeSection* section = nullptr;
        std::size_t offset = 0;
        std::uint32_t rva = 0;
    };

    std::optional<PatternMatch> FindFirstInModule(
        const ModuleCode& module,
        std::string_view patternText,
        Result& result)
    {
        const std::vector<PatternByte> pattern = CompilePattern(patternText);
        if (pattern.empty())
            return std::nullopt;

        std::optional<PatternMatch> found;
        std::size_t totalMatches = 0;
        for (const CodeSection& section : module.sections) {
            const auto search = FindPatternMatches(section.bytes, pattern);
            totalMatches += search.matchCount;
            if (!search.firstOffset || found)
                continue;
            found = PatternMatch{
                &section,
                *search.firstOffset,
                static_cast<std::uint32_t>(
                    section.rva + *search.firstOffset)
            };
        }
        if (totalMatches > 1)
            result.duplicatePatterns += totalMatches - 1;
        return found;
    }

    std::optional<std::ptrdiff_t> ResolveRipPattern(
        const ModuleCode& module,
        std::string_view pattern,
        std::size_t displacementOffset,
        Result& result)
    {
        const auto match = FindFirstInModule(module, pattern, result);
        if (!match)
            return std::nullopt;
        const auto rva = ResolveRelativeRva(
            match->rva,
            displacementOffset,
            match->section->bytes,
            match->offset,
            module.imageSize);
        if (!rva)
            return std::nullopt;
        return static_cast<std::ptrdiff_t>(*rva);
    }

    std::optional<std::ptrdiff_t> ResolveImmediatePattern(
        const ModuleCode& module,
        std::string_view pattern,
        std::size_t immediateOffset,
        Result& result)
    {
        const auto match = FindFirstInModule(module, pattern, result);
        if (!match ||
            match->offset > match->section->bytes.size() ||
            match->section->bytes.size() - match->offset < immediateOffset + sizeof(std::uint32_t)) {
            return std::nullopt;
        }
        std::uint32_t value = 0;
        std::memcpy(
            &value,
            match->section->bytes.data() + match->offset + immediateOffset,
            sizeof(value));
        if (value == 0)
            return std::nullopt;
        return static_cast<std::ptrdiff_t>(value);
    }

    void AddResolved(
        Result& result,
        std::string key,
        const std::optional<std::ptrdiff_t>& value)
    {
        if (value && *value > 0) {
            result.offsets.emplace(std::move(key), *value);
            return;
        }
        result.diagnostics.emplace_back("signature unresolved: " + key);
    }

    void ResolveSignatures(Result& result)
    {
        ModuleCode client;
        if (ReadModuleCode("client.dll", client, result)) {
            AddResolved(result, "dwEntityList", ResolveRipPattern(
                client, "48 89 0D ?? ?? ?? ?? E9 ?? ?? ?? ?? CC", 3, result));
            AddResolved(result, "dwGameRules", ResolveRipPattern(
                client, "F6 C1 01 0F 85 ?? ?? ?? ?? 4C 8B 05 ?? ?? ?? ?? 4D 85", 12, result));
            AddResolved(result, "dwGlobalVars", ResolveRipPattern(
                client, "48 89 15 ?? ?? ?? ?? 48 89 42", 3, result));
            AddResolved(result, "dwLocalPlayerController", ResolveRipPattern(
                client, "48 8B 05 ?? ?? ?? ?? 41 89 BE", 3, result));
            AddResolved(result, "dwPlantedC4", ResolveRipPattern(
                client, "48 8B 1D ?? ?? ?? ?? 45 32 F6", 3, result));
            AddResolved(result, "dwViewMatrix", ResolveRipPattern(
                client, "48 8D 0D ?? ?? ?? ?? 48 C1 E0 06", 3, result));
            AddResolved(result, "dwWeaponC4", ResolveRipPattern(
                client,
                "48 8B 15 ?? ?? ?? ?? 48 8B 5C 24 ?? FF C0 89 05 ?? ?? ?? ?? 48 8B C6 48 89 34 EA 80 BE",
                3,
                result));
            const auto sensitivity = AddRvaOffset(
                ResolveRipPattern(
                    client,
                    "48 8D 0D ?? ?? ?? ?? 66 0F 6E CD",
                    3,
                    result),
                8);
            AddResolved(result, "dwSensitivity", sensitivity);
            if (sensitivity)
                result.offsets.emplace("dwSensitivity_sensitivity", 0x58);

            const auto input = ResolveRipPattern(
                client, "48 89 05 ?? ?? ?? ?? 0F 57 C0 0F 11 05", 3, result);
            const auto viewAngleDelta = ResolveImmediatePattern(
                client, "F2 42 0F 10 84 28 ?? ?? ?? ??", 6, result);
            std::optional<std::ptrdiff_t> viewAngles;
            if (input && viewAngleDelta &&
                *input <= std::numeric_limits<std::ptrdiff_t>::max() - *viewAngleDelta) {
                viewAngles = *input + *viewAngleDelta;
            }
            AddResolved(result, "dwViewAngles", viewAngles);

            const auto prediction = ResolveRipPattern(
                client,
                "48 8D 05 ?? ?? ?? ?? C3 CC CC CC CC CC CC CC CC 40 53 56 41 54",
                3,
                result);
            const auto pawnDelta = ResolveImmediatePattern(
                client, "4C 39 B6 ?? ?? ?? ?? 74 ?? 44 88 BE", 3, result);
            std::optional<std::ptrdiff_t> localPlayerPawn;
            if (prediction && pawnDelta &&
                *prediction <= std::numeric_limits<std::ptrdiff_t>::max() - *pawnDelta) {
                localPlayerPawn = *prediction + *pawnDelta;
            }
            AddResolved(result, "dwLocalPlayerPawn", localPlayerPawn);
            AddResolved(result, "dwGameEntitySystem_highestEntityIndex", ResolveImmediatePattern(
                client, "FF 81 ?? ?? ?? ?? 48 85 D2", 2, result));
        } else {
            result.diagnostics.emplace_back("client.dll executable sections unavailable");
        }

        ModuleCode engine;
        if (ReadModuleCode("engine2.dll", engine, result)) {
            AddResolved(result, "dwNetworkGameClient", ResolveRipPattern(
                engine, "48 89 3D ?? ?? ?? ?? FF 87", 3, result));
            AddResolved(result, "dwNetworkGameClient_signOnState", ResolveImmediatePattern(
                engine, "44 8B 81 ?? ?? ?? ?? 48 8D 0D", 3, result));
            AddResolved(result, "dwNetworkGameClient_localPlayer", ResolveImmediatePattern(
                engine,
                "42 8B 94 D3 ?? ?? ?? ?? 5B 49 FF E3 32 C0 5B C3 CC CC CC CC CC CC CC CC 40 53",
                4,
                result));
            AddResolved(result, "dwNetworkGameClient_maxClients", ResolveImmediatePattern(
                engine,
                "8B 81 ?? ?? ?? ?? C3 ?? ?? ?? ?? ?? ?? ?? ?? ?? 8B 81 ?? ?? ?? ?? C3 ?? ?? ?? ?? ?? ?? ?? ?? ?? 8B 81",
                2,
                result));
            AddResolved(result, "dwNetworkGameClient_isBackgroundMap", ResolveImmediatePattern(
                engine,
                "0F B6 81 ?? ?? ?? ?? C3 CC CC CC CC CC CC CC CC 0F B6 81 ?? ?? ?? ?? C3 CC CC CC CC CC CC CC CC 48 83 EC",
                3,
                result));
        } else {
            result.diagnostics.emplace_back("engine2.dll executable sections unavailable");
        }

        ModuleCode matchmaking;
        if (ReadModuleCode("matchmaking.dll", matchmaking, result)) {
            AddResolved(result, "dwGameTypes", ResolveRipPattern(
                matchmaking, "48 8D 0D ?? ?? ?? ?? FF 90", 3, result));
        }
    }

    void AppendHashNodeData(
        std::uintptr_t nodeAddress,
        std::size_t nextOffset,
        std::vector<std::uintptr_t>& output,
        std::unordered_set<std::uintptr_t>& seenNodes,
        std::size_t maximumElements,
        Result& result)
    {
        while (IsGamePointer(nodeAddress) &&
               output.size() < maximumElements) {
            if (!seenNodes.insert(nodeAddress).second)
                break;
            std::array<std::uint8_t, 0x18> node = {};
            if (!ReadResolverMemory(nodeAddress, node.data(), node.size()))
                break;
            result.bytesRead += node.size();
            const auto decoded = DecodeSchemaHashNode(node, nextOffset);
            if (!decoded)
                break;
            if (IsGamePointer(decoded->data))
                output.push_back(decoded->data);
            nodeAddress = decoded->next;
        }
    }

    std::vector<std::uintptr_t> ReadSchemaClassBindings(
        std::uintptr_t hashAddress,
        Result& result)
    {
        constexpr std::size_t kHeaderAndBucketsSize =
            kSchemaHashBucketsOffset + kSchemaHashBucketCount * kSchemaHashBucketSize;
        std::vector<std::uint8_t> hash(kHeaderAndBucketsSize);
        if (!ReadRange(hashAddress, hash, result))
            return {};

        std::int32_t blocksAllocated = 0;
        std::int32_t peakAllocated = 0;
        std::memcpy(&blocksAllocated, hash.data() + 0x0C, sizeof(blocksAllocated));
        std::memcpy(&peakAllocated, hash.data() + 0x10, sizeof(peakAllocated));
        if (blocksAllocated < 0 || peakAllocated < 0 ||
            blocksAllocated > static_cast<std::int32_t>(kMaximumSchemaClasses) ||
            peakAllocated > static_cast<std::int32_t>(kMaximumSchemaClasses)) {
            return {};
        }

        const std::size_t allocatedLimit =
            static_cast<std::size_t>(blocksAllocated);
        std::vector<std::uintptr_t> allocated;
        allocated.reserve(allocatedLimit);
        std::unordered_set<std::uintptr_t> allocatedNodes;
        for (std::size_t i = 0; i < kSchemaHashBucketCount; ++i) {
            const std::size_t bucket = kSchemaHashBucketsOffset + i * kSchemaHashBucketSize;
            std::uintptr_t node = 0;
            std::memcpy(&node, hash.data() + bucket + 0x10, sizeof(node));
            AppendHashNodeData(
                node,
                0x08,
                allocated,
                allocatedNodes,
                allocatedLimit,
                result);
        }

        const std::size_t freeLimit =
            static_cast<std::size_t>(peakAllocated);
        std::vector<std::uintptr_t> freeEntries;
        freeEntries.reserve(freeLimit);
        std::unordered_set<std::uintptr_t> freeNodes;
        std::uintptr_t freeNode = 0;
        std::memcpy(&freeNode, hash.data() + 0x20, sizeof(freeNode));
        AppendHashNodeData(
            freeNode,
            0x00,
            freeEntries,
            freeNodes,
            freeLimit,
            result);

        std::vector<std::uintptr_t> output;
        output.reserve(allocated.size() + freeEntries.size());
        output.insert(output.end(), allocated.begin(), allocated.end());
        output.insert(output.end(), freeEntries.begin(), freeEntries.end());
        std::unordered_set<std::uintptr_t> seenData;
        std::erase_if(output, [&](std::uintptr_t value) {
            return !seenData.insert(value).second;
        });
        if (output.size() > kMaximumSchemaClasses)
            output.resize(kMaximumSchemaClasses);
        return output;
    }

    bool ResolveSchemaFields(
        const std::vector<SchemaRequest>& requests,
        Result& result)
    {
        if (requests.empty())
            return true;

        ModuleCode schemaModule;
        if (!ReadModuleCode("schemasystem.dll", schemaModule, result)) {
            result.diagnostics.emplace_back("schemasystem.dll executable sections unavailable");
            return false;
        }
        const auto schemaRva = ResolveRipPattern(
            schemaModule, "4C 8D 35 ?? ?? ?? ?? 0F 28 45", 3, result);
        if (!schemaRva) {
            result.diagnostics.emplace_back("SchemaSystem signature not found uniquely");
            return false;
        }
        const std::uintptr_t schemaSystem =
            schemaModule.base + static_cast<std::uintptr_t>(*schemaRva);

        std::int32_t scopeCount = 0;
        std::int32_t registrationCount = 0;
        std::uintptr_t scopeData = 0;
        if (!ReadValue(schemaSystem + 0x190, scopeCount, result) ||
            !ReadValue(schemaSystem + 0x198, scopeData, result) ||
            !ReadValue(schemaSystem + 0x280, registrationCount, result) ||
            scopeCount <= 0 || scopeCount > 64 ||
            registrationCount <= 0 ||
            !IsGamePointer(scopeData)) {
            result.diagnostics.emplace_back("SchemaSystem type-scope vector is invalid");
            return false;
        }

        std::vector<std::uintptr_t> scopes(static_cast<std::size_t>(scopeCount));
        if (!ReadObjects(scopeData, std::span(scopes), result))
            return false;

        std::uintptr_t clientScope = 0;
        for (const std::uintptr_t scope : scopes) {
            if (!IsGamePointer(scope))
                continue;
            std::array<char, 256> name = {};
            if (!ReadResolverMemory(scope + 0x08, name.data(), name.size()))
                continue;
            result.bytesRead += name.size();
            name.back() = '\0';
            if (_stricmp(name.data(), "client.dll") == 0) {
                clientScope = scope;
                break;
            }
        }
        if (!clientScope) {
            result.diagnostics.emplace_back("client.dll SchemaSystem type scope is missing");
            return false;
        }

        std::unordered_map<std::string, std::vector<const SchemaRequest*>> requestByClass;
        requestByClass.reserve(requests.size());
        for (const SchemaRequest& request : requests)
            requestByClass[request.className].push_back(&request);

        const std::vector<std::uintptr_t> bindings =
            ReadSchemaClassBindings(clientScope + 0x560, result);
        if (bindings.empty()) {
            result.diagnostics.emplace_back("client.dll schema class hash is empty");
            return false;
        }

        for (const std::uintptr_t binding : bindings) {
            if (result.schemas.size() >= requests.size())
                break;
            ++result.classesVisited;
            std::array<std::uint8_t, 0x38> header = {};
            if (!ReadResolverMemory(binding, header.data(), header.size()))
                continue;
            result.bytesRead += header.size();

            std::uintptr_t namePointer = 0;
            std::uintptr_t fieldsPointer = 0;
            std::int16_t fieldCount = 0;
            std::memcpy(&namePointer, header.data() + 0x08, sizeof(namePointer));
            std::memcpy(&fieldCount, header.data() + 0x24, sizeof(fieldCount));
            std::memcpy(&fieldsPointer, header.data() + 0x30, sizeof(fieldsPointer));
            if (fieldCount <= 0 ||
                fieldCount > static_cast<std::int16_t>(kMaximumSchemaFieldsPerClass) ||
                !IsGamePointer(fieldsPointer)) {
                continue;
            }
            const std::string className = ReadCString(namePointer, 128, result);
            const auto requestIt = requestByClass.find(className);
            if (requestIt == requestByClass.end())
                continue;

            const std::size_t fieldsSize = static_cast<std::size_t>(fieldCount) * 0x20u;
            std::vector<std::uint8_t> fields(fieldsSize);
            if (!ReadRange(fieldsPointer, fields, result))
                continue;
            for (std::int16_t i = 0; i < fieldCount; ++i) {
                const std::size_t fieldOffset = static_cast<std::size_t>(i) * 0x20u;
                std::uintptr_t fieldNamePointer = 0;
                std::int32_t offset = 0;
                std::memcpy(&fieldNamePointer, fields.data() + fieldOffset, sizeof(fieldNamePointer));
                std::memcpy(&offset, fields.data() + fieldOffset + 0x10, sizeof(offset));
                if (offset < 0 || offset > 0x100000)
                    continue;
                const std::string fieldName = ReadCString(fieldNamePointer, 128, result);
                for (const SchemaRequest* request : requestIt->second) {
                    if (request->fieldName == fieldName)
                        result.schemas.emplace(request->outputKey, offset);
                }
            }
        }

        return !result.schemas.empty();
    }
}

bool runtime_offsets::resolver::ResolveAttachedProcess(
    const std::vector<SchemaRequest>& schemaRequests,
    Result& result,
    std::string* error)
{
    const auto started = std::chrono::steady_clock::now();
    result = {};
    result.expectedOffsets = kExpectedResolvedOffsets;
    result.expectedSchemas = schemaRequests.size();
    if (!mem.vHandle || mem.GetAttachedPid() == 0) {
        if (error)
            *error = "DMA is not attached to a process";
        return false;
    }

    const bool warningsAlreadySuppressed =
        mem.IsDirectReadWarningSuppressed();
    mem.SetDirectReadWarningSuppressed(true);
    struct WarningGuard {
        bool restoreSuppressed = false;
        ~WarningGuard()
        {
            mem.SetDirectReadWarningSuppressed(restoreSuppressed);
        }
    } warningGuard{warningsAlreadySuppressed};

    ResolveSignatures(result);
    const bool schemaOk = ResolveSchemaFields(schemaRequests, result);
    result.elapsedMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();

    if (result.offsets.empty() || !schemaOk) {
        if (error) {
            *error = result.diagnostics.empty()
                ? "runtime offset resolution was incomplete"
                : result.diagnostics.front();
        }
        return false;
    }
    if (error)
        error->clear();
    return true;
}
