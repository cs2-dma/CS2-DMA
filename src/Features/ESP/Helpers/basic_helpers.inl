inline float Clamp01(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

inline ImU32 ColorToImU32(const float c[4]) {
    return IM_COL32(
        static_cast<int>(std::clamp(c[0], 0.0f, 1.0f) * 255.0f),
        static_cast<int>(std::clamp(c[1], 0.0f, 1.0f) * 255.0f),
        static_cast<int>(std::clamp(c[2], 0.0f, 1.0f) * 255.0f),
        static_cast<int>(std::clamp(c[3], 0.0f, 1.0f) * 255.0f));
}

inline float NormalizeYawDeltaRad(float value)
{
    constexpr float tau = 2.0f * std::numbers::pi_v<float>;
    return std::remainder(value, tau);
}

inline bool isValidWorldPos(const Vector3& v)
{
    constexpr float kMaxWorldXY = 32768.0f;
    constexpr float kMaxWorldZ = 16384.0f;
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
           std::fabs(v.x) <= kMaxWorldXY &&
           std::fabs(v.y) <= kMaxWorldXY &&
           std::fabs(v.z) <= kMaxWorldZ &&
           (std::fabs(v.x) + std::fabs(v.y) + std::fabs(v.z) > 1.0f);
}

namespace esp::validation {
    
    

    inline bool IsValidVelocity(const Vector3& value)
    {
        return IsFiniteVec(value) && value.Length() < 5000.0f;
    }
}

inline ImFont* GetEspNameFont()
{
    if (g::fontEspName)
        return g::fontEspName;

    if (g::fontDefault)
        return g::fontDefault;
    return ImGui::GetFont();
}

inline const char* WorldMarkerName(WorldMarkerType type, uint16_t weaponId)
{
    switch (type) {
    case WorldMarkerType::DroppedWeapon: return WeaponNameFromItemId(weaponId);
    case WorldMarkerType::Smoke: return "Smoke";
    case WorldMarkerType::Inferno:
        if (weaponId == 48) return "Incendiary";
        if (weaponId == 46) return "Molotov";
        return "Fire";
    case WorldMarkerType::Decoy: return "Decoy";
    case WorldMarkerType::Explosive: return "HE";
    case WorldMarkerType::SmokeProjectile: return "Smoke Projectile";
    case WorldMarkerType::MolotovProjectile: return (weaponId == 48) ? "Incendiary Projectile" : "Molotov Projectile";
    case WorldMarkerType::DecoyProjectile: return "Decoy Projectile";
    default:
        break;
    }
    return nullptr;
}
