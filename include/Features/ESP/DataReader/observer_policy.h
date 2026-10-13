#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace esp::data
{
    using ObserverReadBytes = std::conditional_t<sizeof(unsigned long) == 4, unsigned long, uint32_t>;
    struct ObserverOffsets {
        ptrdiff_t observerPawn = 0, basePawn = 0, playerPawn = 0;
        ptrdiff_t services = 0, mode = 0, target = 0;
    };

    struct ObserverSample {
        uintptr_t controller = 0, pawn = 0, services = 0, confirmedServices = 0;
        uint32_t handles[3] = {};
        ObserverReadBytes handleBytes[3] = {};
        uint32_t selected = 0, confirmedHandle = 0, target = 0;
        ObserverReadBytes servicesBytes = 0, modeBytes = 0, targetBytes = 0;
        ObserverReadBytes confirmedServicesBytes = 0, confirmedHandleBytes = 0;
        uint8_t mode = 0;
        bool resolved = false;
    };

    struct ObserverState {
        uintptr_t controller = 0;
        uint32_t pawnHandle = 0, target = 0;
        uint8_t mode = 0;
        uint64_t sampleUs = 0;
    };

    inline bool IsObserverHandle(uint32_t handle) noexcept
    {
        const auto index = handle & 0x7FFFu;
        return handle != 0 && handle != 0xFFFFFFFFu && index != 0 && index != 0x7FFFu;
    }

    inline bool IsFollowingObserver(uint8_t mode) noexcept
    {
        return mode == 2u || mode == 3u;
    }

    inline bool IsObserverStateFresh(const ObserverState& state,
        uintptr_t controller, uint64_t nowUs) noexcept
    {
        return controller != 0 && state.controller == controller && state.sampleUs != 0 &&
            nowUs >= state.sampleUs && nowUs - state.sampleUs <= 500000u;
    }

    template<size_t N, class Queue, class Execute, class Resolve, class ValidPointer>
    void ReadObserverBatch(ObserverSample (&samples)[N], const ObserverOffsets& offsets,
        Queue queue, Execute execute, Resolve resolve, ValidPointer validPointer)
    {
        const ptrdiff_t handleOffsets[] = {offsets.observerPawn, offsets.basePawn, offsets.playerPawn};
        bool queued = false;
        for (auto& sample : samples) {
            if (!validPointer(sample.controller))
                continue;
            for (size_t field = 0; field < 3; ++field) {
                if (handleOffsets[field] <= 0)
                    continue;
                queue(sample.controller + handleOffsets[field], &sample.handles[field],
                    sizeof(uint32_t), &sample.handleBytes[field]);
                queued = true;
            }
        }
        if (!queued || !execute())
            return;
        queued = false;
        for (auto& sample : samples) {
            if (!validPointer(sample.controller))
                continue;
            bool complete = true;
            for (size_t field = 0; field < 3; ++field)
                complete &= handleOffsets[field] <= 0 || sample.handleBytes[field] == sizeof(uint32_t);
            if (!complete)
                continue;
            size_t field = 0;
            while (field < 3 && !IsObserverHandle(sample.handles[field]))
                ++field;
            if (field == 3) {
                sample.resolved = true;
                continue;
            }
            sample.selected = static_cast<uint32_t>(field);
            sample.pawn = resolve(sample.handles[field]);
            if (!validPointer(sample.pawn))
                continue;
            queue(sample.pawn + offsets.services, &sample.services,
                sizeof(uintptr_t), &sample.servicesBytes);
            queued = true;
        }
        if (!queued || !execute())
            return;
        queued = false;
        for (auto& sample : samples) {
            if (sample.servicesBytes != sizeof(uintptr_t) || !validPointer(sample.services))
                continue;
            queue(sample.services + offsets.mode, &sample.mode, sizeof(uint8_t), &sample.modeBytes);
            queue(sample.services + offsets.target, &sample.target, sizeof(uint32_t), &sample.targetBytes);
            queue(sample.pawn + offsets.services, &sample.confirmedServices,
                sizeof(uintptr_t), &sample.confirmedServicesBytes);
            queue(sample.controller + handleOffsets[sample.selected], &sample.confirmedHandle,
                sizeof(uint32_t), &sample.confirmedHandleBytes);
            queued = true;
        }
        if (!queued || !execute())
            return;
        for (auto& sample : samples) {
            if (sample.modeBytes != sizeof(uint8_t) || sample.targetBytes != sizeof(uint32_t) ||
                sample.confirmedServicesBytes != sizeof(uintptr_t) ||
                sample.confirmedHandleBytes != sizeof(uint32_t))
                continue;
            sample.resolved = sample.services == sample.confirmedServices &&
                sample.handles[sample.selected] == sample.confirmedHandle && sample.mode < 5u;
        }
    }

    inline void CommitObserverSample(ObserverState& state, const ObserverSample& sample,
        uint64_t nowUs) noexcept
    {
        if (state.controller != sample.controller)
            state = {};
        if (sample.resolved) {
            state = {sample.controller, sample.handles[sample.selected], sample.target, sample.mode, nowUs};
        } else if (!IsObserverStateFresh(state, sample.controller, nowUs)) {
            state = {};
        }
    }
}
