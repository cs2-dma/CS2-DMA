#include <Windows.h>
#include "Features/ESP/esp_renderer.h"
#include "Features/ESP/esp_local_state.h"
#include "Features/ESP/esp_helpers.h"
#include "Features/ESP/weapon_catalog.h"
#include "Features/ESP/Render/draw_policy.h"
#include "Features/ESP/Render/skeleton_gate.h"
#include "Features/ESP/Render/weapon_icon_atlas.h"
#include "Features/ESP/State/snapshot_ring.h"
#include "app/Core/globals.h"
#include "app/Localization/localization.h"
#include "app/Config/config.h"
#include "app/Config/project_paths.h"
#include "app/Config/user_state.h"
#include "app/Platform/overlay.h"
#include "Features/Radar/map_registry.h"
#include "Game/Offsets/runtime_offsets.h"
#include "Game/Schema/structs.h"
#include <DMALibrary/Memory/Memory.h>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <numbers>
#include <string>
#include <vector>
using namespace esp;

using esp::weapons::IsKnifeItemId;
using esp::weapons::PickWeaponIconFont;
using esp::weapons::WeaponIconFallbackTokenFromItemId;
using esp::weapons::WeaponIconFromItemId;
using esp::weapons::WeaponMaxClipFromItemId;
using esp::weapons::WeaponNameFromItemId;
using esp::weapons::WeaponVisualKeyFromItemId;

#include "Helpers/basic_helpers.inl"
#include "Helpers/draw_corner_box.inl"
#include "Helpers/draw_text_shadow.inl"
#include "Helpers/project_world_aabb_to_screen.inl"

#include "Core/draw.inl"
