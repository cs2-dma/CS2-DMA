#pragma once

#include <Windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace esp::data
{
    template <std::size_t Capacity>
    class CheckedScatterBatch
    {
        struct Request {
            uintptr_t address = 0;
            void* output = nullptr;
            std::size_t size = 0;
            DWORD bytes = 0;
        };
        std::array<Request, Capacity> requests_ = {};
        std::size_t count_ = 0;
        bool executed_ = false;
        bool succeeded_ = false;

    public:
        static constexpr std::size_t invalid = Capacity;

        std::size_t Add(uintptr_t address, void* output, std::size_t size)
        {
            if (executed_ || count_ == Capacity || !address || !output || !size ||
                size > (std::numeric_limits<DWORD>::max)() ||
                address > (std::numeric_limits<uintptr_t>::max)() - (size - 1))
                return invalid;
            requests_[count_] = {address, output, size, 0};
            return count_++;
        }

        template <typename Reader, typename Handle>
        void Execute(Reader& reader, Handle handle)
        {
            if (executed_ || count_ == 0)
                return;
            executed_ = true;
            for (std::size_t i = 0; i < count_; ++i) {
                auto& request = requests_[i];
                reader.AddScatterReadRequest(handle, request.address, request.output,
                    request.size, &request.bytes);
            }
            succeeded_ = reader.ExecuteReadScatter(handle);
        }

        bool Complete(std::size_t index) const
        {
            return executed_ && succeeded_ && index < count_ &&
                requests_[index].bytes == requests_[index].size;
        }
    };
}
