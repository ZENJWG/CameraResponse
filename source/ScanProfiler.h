#pragma once
#include <array>
#include <chrono>
#include <cstdint>

namespace cr {

enum class ScanKind : unsigned {
    NearbyVehicles, NearbyGunfire, Doors, WorldObjects, PedWorld,
    LargeVehicles, GroundSurface, IndoorRay, Count
};

inline const char* ScanKindName(ScanKind kind) {
    switch (kind) {
    case ScanKind::NearbyVehicles: return "nearby-vehicles";
    case ScanKind::NearbyGunfire: return "nearby-gunfire";
    case ScanKind::Doors: return "doors";
    case ScanKind::WorldObjects: return "world-objects";
    case ScanKind::PedWorld: return "ped-world";
    case ScanKind::LargeVehicles: return "large-vehicles";
    case ScanKind::GroundSurface: return "ground-surface";
    case ScanKind::IndoorRay: return "indoor-ray";
    default: return "unknown";
    }
}

struct ScanProfile {
    std::uint64_t calls = 0;
    std::uint64_t totalUs = 0;
    std::uint64_t peakUs = 0;
};

// Off by default: no clock calls unless profiling has been enabled.
class ScanProfiler {
public:
    static constexpr unsigned kCount = static_cast<unsigned>(ScanKind::Count);

    void Record(ScanKind kind, std::uint64_t microseconds) {
        const unsigned index = static_cast<unsigned>(kind);
        if (index >= kCount)
            return;
        auto& sample = samples_[index];
        ++sample.calls;
        sample.totalUs += microseconds;
        if (microseconds > sample.peakUs)
            sample.peakUs = microseconds;
    }

    const ScanProfile& Get(ScanKind kind) const {
        return samples_[static_cast<unsigned>(kind)];
    }

    void Reset() { samples_ = {}; }

private:
    std::array<ScanProfile, kCount> samples_{};
};

class ScopedScanProfile {
public:
    using Clock = std::chrono::steady_clock;

    ScopedScanProfile(ScanProfiler& profiler, ScanKind kind, bool enabled)
        : profiler_(profiler), kind_(kind), enabled_(enabled) {
        if (enabled_)
            start_ = Clock::now();
    }

    ~ScopedScanProfile() {
        if (!enabled_)
            return;
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now() - start_).count();
        profiler_.Record(kind_, static_cast<std::uint64_t>(elapsed < 0 ? 0 : elapsed));
    }

    ScopedScanProfile(const ScopedScanProfile&) = delete;
    ScopedScanProfile& operator=(const ScopedScanProfile&) = delete;

private:
    ScanProfiler& profiler_;
    ScanKind kind_;
    bool enabled_;
    Clock::time_point start_{};
};

} // namespace cr
