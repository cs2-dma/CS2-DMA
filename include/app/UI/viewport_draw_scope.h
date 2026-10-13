#pragma once

#include "app/Platform/display_mapping.h"
#include <imgui.h>

namespace app::display {
    class ViewportDrawScope {
    public:
        ViewportDrawScope(ImDrawList& list, const Rect& viewport)
            : list_(list), viewport_(viewport), firstVertex_(list.VtxBuffer.Size)
        {
            if (viewport_.x != 0.0f || viewport_.y != 0.0f) {
                splitter_.Split(&list_, 2);
                splitter_.SetCurrentChannel(&list_, 1);
            }
            list_.PushClipRect(ImVec2(0, 0), ImVec2(viewport.width, viewport.height), false);
        }

        ~ViewportDrawScope()
        {
            if (viewport_.x != 0.0f || viewport_.y != 0.0f) {
                for (int i = firstVertex_; i < list_.VtxBuffer.Size; ++i) {
                    list_.VtxBuffer[i].pos.x += viewport_.x;
                    list_.VtxBuffer[i].pos.y += viewport_.y;
                }
                for (int i = 0; i < list_.CmdBuffer.Size; ++i) {
                    auto& clip = list_.CmdBuffer[i].ClipRect;
                    clip.x += viewport_.x;
                    clip.z += viewport_.x;
                    clip.y += viewport_.y;
                    clip.w += viewport_.y;
                }
            }
            list_.PopClipRect();
            if (viewport_.x != 0.0f || viewport_.y != 0.0f)
                splitter_.Merge(&list_);
        }

        ViewportDrawScope(const ViewportDrawScope&) = delete;
        ViewportDrawScope& operator=(const ViewportDrawScope&) = delete;

    private:
        ImDrawList& list_;
        Rect viewport_;
        int firstVertex_;
        ImDrawListSplitter splitter_;
    };
}
