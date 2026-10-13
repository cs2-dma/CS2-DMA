#pragma once

#include "app/UI/MenuShell/tab_page.h"

namespace ui::tabs
{
    class TargetTab final : public ui::ITabPage
    {
    public:
        static void CancelKeyCapture() noexcept;
        const char* Label() const override;
        void Render(MenuState& state, IStatusSink& statusSink) override;
    };
}
