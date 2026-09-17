#pragma once
#include <cstdint>
#include <stop_token>

namespace esp {
    class PlayerDataReader {
    public:
        PlayerDataReader() = default;
        ~PlayerDataReader() = default;

        // Processes one complete loop of memory scanning
        bool UpdateData();

        // Main thread loops
        void DataWorkerLoop(std::stop_token stopToken);
        void CameraWorkerLoop(std::stop_token stopToken);
    };
}
