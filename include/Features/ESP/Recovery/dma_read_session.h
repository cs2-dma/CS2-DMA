#pragma once

#include <cstdint>
#include <shared_mutex>

namespace esp::recovery
{
    class DmaReadSession
    {
    public:
        DmaReadSession();
        bool Valid() const noexcept;

    private:
        std::shared_lock<std::shared_timed_mutex> lock_;
        uint64_t generation_ = 0;
    };
}
