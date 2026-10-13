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
    using runtime_offsets::resolver_policy::AddRvaOffset;
    using runtime_offsets::resolver_policy::FindPatternMatches;
    using runtime_offsets::resolver_policy::PatternByte;
    using runtime_offsets::resolver_policy::ResolveRelativeRva;

    constexpr std::size_t kReadChunkSize = 256u * 1024u;
    constexpr std::size_t kCodeReadChunkSize = 64u * 1024u;
    constexpr std::size_t kCodePageSize = 4u * 1024u;
    constexpr std::size_t kMaximumModuleSize = 256u * 1024u * 1024u;
    constexpr std::size_t kExpectedResolvedOffsets = 18;
    constexpr std::size_t kMaximumSchemaFieldsPerClass = 512;

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

    class Scanner
    {
    public:
        explicit Scanner(const runtime_offsets::ResolveControl& control) : control_(control) {}

        bool Run(const std::vector<SchemaRequest>& requests, Result& result)
        {
            control_.Check();
            ResolveSignatures(result);
            const bool schemaOk = ResolveSchemaFields(requests, result);
            control_.Check();
            return !result.offsets.empty() && schemaOk;
        }

    private:
        const runtime_offsets::ResolveControl& control_;

        bool ReadFresh(std::uintptr_t address, void* buffer, std::size_t size)
        {
            return control_.Run([&] { return mem.Read(address, buffer, size); });
        }

        bool ReadResolverMemory(
            std::uintptr_t address,
            void* buffer,
            std::size_t size)
        {
            return control_.Run([&] { return mem.ReadCached(address, buffer, size); }) ||
                   ReadFresh(address, buffer, size);
        }

        void ReadBatch(std::span<runtime_offsets::resolver_policy::SchemaMemoryRead> requests, Result& result)
        {
            control_.Check();
            std::vector<DWORD> completed(requests.size(), 0);
            const auto handle = mem.CreateScatterHandle();
            struct BatchGuard {
                VMMDLL_SCATTER_HANDLE handle;
                ~BatchGuard() { mem.CloseScatterHandle(handle); }
            } guard{handle};
            const auto validRead = [](const auto& request) {
                return app::memory_address::IsCanonicalUserPointer(request.address) && request.output &&
                    request.size != 0 && request.size <= app::memory_address::kMaximumUserAddress - request.address;
            };
            if (handle) {
                for (std::size_t i = 0; i < requests.size(); ++i) {
                    control_.Check();
                    auto& request = requests[i];
                    request.completed = 0;
                    if (validRead(request))
                        mem.AddScatterReadRequest(handle, request.address, request.output, request.size, &completed[i]);
                }
                control_.Run([&] { return mem.ExecuteReadScatter(handle); });
            }
            for (std::size_t i = 0; i < requests.size(); ++i) {
                auto& request = requests[i];
                request.completed = completed[i];
                result.bytesRead += completed[i];
                if (request.completed != request.size && validRead(request) &&
                    ReadFresh(request.address, request.output, request.size)) {
                    request.completed = request.size;
                    result.bytesRead += request.size;
                }
            }
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
            return runtime_offsets::resolver_policy::ReadSchemaName(address, std::min<std::size_t>(capacity, 256),
                [&](std::uintptr_t current, void* buffer, std::size_t count) {
                    if (!ReadFresh(current, buffer, count)) return false;
                    result.bytesRead += count;
                    return true;
                });
        }

        bool ReadModuleCode(const char* moduleName, ModuleCode& output, Result& result)
        {
            output = {};
            const std::uintptr_t base = control_.Run([&] { return mem.GetModuleBase(moduleName); });
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
                control_.Check();
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

        std::optional<PatternMatch> FindUniqueInModule(
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
                const auto search = FindPatternMatches(section.bytes, pattern,
                    [&] { control_.Check(); });
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
            if (totalMatches > 1) {
                result.duplicatePatterns += totalMatches - 1;
                return std::nullopt;
            }
            return found;
        }

        std::optional<std::ptrdiff_t> ResolveRipPattern(
            const ModuleCode& module,
            std::string_view pattern,
            std::size_t displacementOffset,
            Result& result)
        {
            const auto match = FindUniqueInModule(module, pattern, result);
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
            const auto match = FindUniqueInModule(module, pattern, result);
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
                const auto duplicateCount = result.duplicatePatterns;
                auto sensitivityAnchor = ResolveRipPattern(client,
                    runtime_offsets::resolver_policy::kSensitivityPattern, 3, result);
                if (!sensitivityAnchor && result.duplicatePatterns == duplicateCount)
                    sensitivityAnchor = ResolveRipPattern(client,
                        runtime_offsets::resolver_policy::kLegacySensitivityPattern, 3, result);
                const auto sensitivity = AddRvaOffset(sensitivityAnchor, 8);
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
                result.startupPending = true;
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
                result.startupPending = true;
                result.diagnostics.emplace_back("engine2.dll executable sections unavailable");
            }

            ModuleCode matchmaking;
            if (ReadModuleCode("matchmaking.dll", matchmaking, result)) {
                AddResolved(result, "dwGameTypes", ResolveRipPattern(
                    matchmaking, "48 8D 0D ?? ?? ?? ?? FF 90", 3, result));
            } else {
                result.startupPending = true;
            }
        }

        std::vector<std::uintptr_t> ReadSchemaClassBindings(
            std::uintptr_t hashAddress,
            Result& result)
        {
            std::vector<std::uint8_t> hash(runtime_offsets::resolver_policy::kSchemaHashBytes);
            const auto read = [&](std::uintptr_t address, void* output, std::size_t size) {
                if (!ReadFresh(address, output, size)) return false;
                result.bytesRead += size;
                return true;
            };
            if (!read(hashAddress, hash.data(), hash.size())) return {};
            return runtime_offsets::resolver_policy::ReadSchemaBindingsBatched(hash,
                [&](auto requests) { ReadBatch(requests, result); });
        }

        bool ResolveSchemaFields(
            const std::vector<SchemaRequest>& requests,
            Result& result)
        {
            if (requests.empty())
                return true;

            ModuleCode schemaModule;
            if (!ReadModuleCode("schemasystem.dll", schemaModule, result)) {
                result.startupPending = true;
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

            const auto scopes = runtime_offsets::resolver_policy::ReadSchemaScopes(schemaSystem,
                [&](std::uintptr_t address, void* output, std::size_t size) {
                    if (!ReadFresh(address, output, size)) return false;
                    result.bytesRead += size;
                    return true;
                });
            if (scopes.empty()) {
                result.startupPending = true;
                result.diagnostics.emplace_back("SchemaSystem type scopes are not ready or changed during the read");
                return false;
            }

            std::uintptr_t clientScope = 0;
            for (const std::uintptr_t scope : scopes) {
                if (!IsGamePointer(scope))
                    continue;
                const auto name = ReadCString(scope + 0x08, 256, result);
                if (_stricmp(name.c_str(), "client.dll") == 0) {
                    clientScope = scope;
                    break;
                }
            }
            if (!clientScope) {
                result.startupPending = true;
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
                result.startupPending = true;
                result.diagnostics.emplace_back("client.dll schema class hash is empty");
                return false;
            }

            std::vector<std::uintptr_t> retryBindings;
            std::unordered_set<std::string> matchedClasses;
            const auto scanBinding = [&](std::uintptr_t binding, bool fresh,
                std::span<const std::uint8_t> sampledHeader, std::string_view sampledName) {
                const auto read = [&](std::uintptr_t address, void* output, std::size_t size) {
                    const bool ok = fresh ? ReadFresh(address, output, size) : ReadResolverMemory(address, output, size);
                    if (ok) result.bytesRead += size;
                    return ok;
                };
                const auto readName = [&](std::uintptr_t address) {
                    return runtime_offsets::resolver_policy::ReadSchemaName(address, 128, read);
                };
                ++result.classesVisited;
                std::array<std::uint8_t, 0x38> header = {};
                if (sampledHeader.size() == header.size())
                    std::memcpy(header.data(), sampledHeader.data(), header.size());
                else if (!read(binding, header.data(), header.size())) {
                    if (!fresh && retryBindings.size() < 128) retryBindings.push_back(binding);
                    return;
                }

                std::uintptr_t namePointer = 0;
                std::uintptr_t fieldsPointer = 0;
                std::int16_t fieldCount = 0;
                std::memcpy(&namePointer, header.data() + 0x08, sizeof(namePointer));
                std::memcpy(&fieldCount, header.data() + 0x24, sizeof(fieldCount));
                std::memcpy(&fieldsPointer, header.data() + 0x30, sizeof(fieldsPointer));
                const std::string className = sampledName.empty() ? readName(namePointer) : std::string(sampledName);
                if (className.empty()) {
                    if (!fresh && retryBindings.size() < 128) retryBindings.push_back(binding);
                    return;
                }
                const auto requestIt = requestByClass.find(className);
                if (requestIt == requestByClass.end()) return;
                matchedClasses.insert(className);
                const auto requiredMissing = [&] {
                    return std::any_of(requestIt->second.begin(), requestIt->second.end(),
                        [&](const SchemaRequest* request) {
                            return !request->optionalLayoutProbe && !result.schemas.contains(request->outputKey);
                        });
                };
                if (fieldCount <= 0 ||
                    fieldCount > static_cast<std::int16_t>(kMaximumSchemaFieldsPerClass) ||
                    !IsGamePointer(fieldsPointer)) {
                    if (!fresh && requiredMissing()) retryBindings.push_back(binding);
                    return;
                }
                std::vector<std::pair<std::uintptr_t, std::int32_t>> fields;
                fields.reserve(static_cast<std::size_t>(fieldCount));
                runtime_offsets::resolver_policy::ReadSchemaFields(fieldsPointer, fieldCount, read,
                    [&](std::span<const std::uint8_t> field) {
                        std::uintptr_t fieldNamePointer = 0;
                        std::int32_t offset = 0;
                        std::memcpy(&fieldNamePointer, field.data(), sizeof(fieldNamePointer));
                        std::memcpy(&offset, field.data() + 0x10, sizeof(offset));
                        if (offset < 0 || offset > 0x100000) return;
                        fields.emplace_back(fieldNamePointer, offset);
                    });
                std::vector<std::uintptr_t> fieldNames;
                fieldNames.reserve(fields.size());
                for (const auto& field : fields) fieldNames.push_back(field.first);
                const auto names = runtime_offsets::resolver_policy::ReadSchemaNamesBatched(fieldNames,
                    [&](auto batch) { ReadBatch(batch, result); }, read);
                std::array<std::uint8_t, 0x38> verifiedHeader{};
                if (!ReadFresh(binding, verifiedHeader.data(), verifiedHeader.size()) ||
                    !runtime_offsets::resolver_policy::AreSchemaClassHeadersCoherent(header, verifiedHeader)) {
                    if (!fresh) retryBindings.push_back(binding);
                    return;
                }
                result.bytesRead += verifiedHeader.size();
                for (std::size_t i = 0; i < names.size(); ++i)
                    for (const auto* request : requestIt->second)
                        if (request->fieldName == names[i])
                            result.schemas.emplace(request->outputKey, fields[i].second);
                if (!fresh && requiredMissing()) retryBindings.push_back(binding);
            };
            constexpr std::size_t kBindingBatchSize = 256;
            for (std::size_t first = 0; first < bindings.size(); first += kBindingBatchSize) {
                control_.Check();
                if (result.schemas.size() >= requests.size()) break;
                const auto count = (std::min)(kBindingBatchSize, bindings.size() - first);
                std::vector<std::array<std::uint8_t, 0x38>> headers(count);
                std::vector<runtime_offsets::resolver_policy::SchemaMemoryRead> reads(count);
                std::vector<std::uintptr_t> namePointers(count, 0);
                for (std::size_t i = 0; i < count; ++i)
                    reads[i] = {bindings[first + i], headers[i].data(), headers[i].size(), 0};
                ReadBatch(reads, result);
                for (std::size_t i = 0; i < count; ++i)
                    if (reads[i].completed == reads[i].size)
                        std::memcpy(&namePointers[i], headers[i].data() + 0x08, sizeof(std::uintptr_t));
                const auto names = runtime_offsets::resolver_policy::ReadSchemaNamesBatched(namePointers,
                    [&](auto batch) { ReadBatch(batch, result); },
                    [&](auto address, auto output, auto size) {
                        if (!ReadFresh(address, output, size)) return false;
                        result.bytesRead += size;
                        return true;
                    });
                for (std::size_t i = 0; i < count; ++i) {
                    if (reads[i].completed != reads[i].size || names[i].empty()) {
                        if (retryBindings.size() < 128) retryBindings.push_back(bindings[first + i]);
                        continue;
                    }
                    scanBinding(bindings[first + i], false, headers[i], names[i]);
                }
            }
            if (std::any_of(requests.begin(), requests.end(), [&](const SchemaRequest& request) {
                return !request.optionalLayoutProbe && !result.schemas.contains(request.outputKey);
            })) {
                for (const auto binding : retryBindings) scanBinding(binding, true, {}, {});
            }
            for (const auto& request : requests) {
                if (!request.optionalLayoutProbe && !result.schemas.contains(request.outputKey)) {
                    result.startupPending = true;
                    result.diagnostics.push_back(request.className + "::" + request.fieldName +
                        (matchedClasses.contains(request.className)
                            ? ": field unavailable in discovered class" : ": class binding not discovered"));
                }
            }

            return !result.schemas.empty();
        }
    };
}

bool runtime_offsets::resolver::ResolveAttachedProcess(
    const std::vector<SchemaRequest>& schemaRequests,
    Result& result,
    std::string* error,
    const ResolveControl* requestedControl)
{
    const auto started = std::chrono::steady_clock::now();
    const ResolveControl defaultControl{started + std::chrono::seconds(15), {}};
    const auto& control = requestedControl ? *requestedControl : defaultControl;
    control.Check();
    result = {};
    result.expectedOffsets = kExpectedResolvedOffsets;
    result.expectedSchemas = schemaRequests.size();
    if (!mem.vHandle || mem.GetAttachedPid() == 0) {
        result.startupPending = true;
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

    const bool resolved = Scanner(control).Run(schemaRequests, result);
    result.elapsedMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();

    if (!resolved) {
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
