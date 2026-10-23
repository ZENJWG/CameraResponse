#pragma once
#include <cstdint>

namespace cr {

// Keeps the normal game-process callback authoritative. Drawing is a recovery path
// only when the primary callback has been silent for more than two game frames.
class UpdateDispatch {
public:
    enum class Source { None, GameProcess, DrawingFallback };

    bool OnGameProcess(std::uint32_t frame) {
        observedPrimary_ = true;
        lastPrimaryFrame_ = frame;
        return Reserve(frame, Source::GameProcess);
    }

    bool OnDrawing(std::uint32_t frame, bool gameplayReady) {
        if (!gameplayReady)
            return false;
        if (observedPrimary_ && frame - lastPrimaryFrame_ <= 2u)
            return false;
        return Reserve(frame, Source::DrawingFallback);
    }

    Source ActiveSource() const { return activeSource_; }
    bool ObservedPrimary() const { return observedPrimary_; }

    void Reset() {
        observedPrimary_ = false;
        lastPrimaryFrame_ = 0;
        hasUpdated_ = false;
        lastUpdateFrame_ = 0;
        activeSource_ = Source::None;
    }

private:
    bool Reserve(std::uint32_t frame, Source source) {
        if (hasUpdated_ && lastUpdateFrame_ == frame)
            return false;
        hasUpdated_ = true;
        lastUpdateFrame_ = frame;
        activeSource_ = source;
        return true;
    }

    bool observedPrimary_ = false;
    std::uint32_t lastPrimaryFrame_ = 0;
    bool hasUpdated_ = false;
    std::uint32_t lastUpdateFrame_ = 0;
    Source activeSource_ = Source::None;
};

} // namespace cr
