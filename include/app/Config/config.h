#pragma once
#include <string>
#include <vector>

namespace config {
    void Save();
    void SaveAsync();
    void SaveIfDirty();
    void FlushAsyncSaves();
    void Load();

    bool SaveNamed(const std::string& profileName);
    bool LoadNamed(const std::string& profileName);
    std::vector<std::string> ListProfiles();
    std::string GetActiveProfile();
}
