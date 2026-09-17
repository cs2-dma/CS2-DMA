#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace runtime_offsets::resolver
{
    struct SchemaRequest {
        std::string className;
        std::string fieldName;
        std::string outputKey;
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
    };

    bool ResolveAttachedProcess(
        const std::vector<SchemaRequest>& schemaRequests,
        Result& result,
        std::string* error = nullptr);
}
