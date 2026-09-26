#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>
#include "Game/Offsets/runtime_resolve_control.h"

namespace runtime_offsets::resolver
{
    struct SchemaRequest {
        std::string className;
        std::string fieldName;
        std::string outputKey;
        bool optionalLayoutProbe = false;
    };

    struct Result {
        std::unordered_map<std::string, std::ptrdiff_t> offsets;
        std::unordered_map<std::string, std::ptrdiff_t> schemas;
        std::vector<std::string> diagnostics;
        std::size_t bytesRead = 0;
        std::size_t classesVisited = 0;
        std::size_t duplicatePatterns = 0;
        std::size_t expectedOffsets = 0;
        std::size_t expectedSchemas = 0;
        std::size_t modulesRead = 0;
        std::size_t executableSectionsRead = 0;
        std::size_t unreadableCodePages = 0;
        double elapsedMs = 0.0;
        bool startupPending = false;
    };

    struct SchemaCoverage {
        std::size_t expected = 0;
        std::size_t resolved = 0;
        std::size_t optionalExpected = 0;
        std::size_t optionalResolved = 0;
    };

    inline SchemaCoverage GetSchemaCoverage(
        const std::vector<SchemaRequest>& requests,
        const Result& result)
    {
        SchemaCoverage coverage;
        for (const SchemaRequest& request : requests) {
            auto& expected = request.optionalLayoutProbe
                ? coverage.optionalExpected : coverage.expected;
            auto& resolved = request.optionalLayoutProbe
                ? coverage.optionalResolved : coverage.resolved;
            ++expected;
            const auto found = result.schemas.find(request.outputKey);
            if (found != result.schemas.end() && found->second >= 0 &&
                found->second <= 0x100000) {
                ++resolved;
            }
        }
        return coverage;
    }

    bool ResolveAttachedProcess(
        const std::vector<SchemaRequest>& schemaRequests,
        Result& result,
        std::string* error = nullptr,
        const ResolveControl* control = nullptr);
}
