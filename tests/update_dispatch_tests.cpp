#include "../source/UpdateDispatch.h"

#include <cassert>
#include <cstdint>
#include <iostream>

using cr::UpdateDispatch;

int main() {
    {
        UpdateDispatch d;
        assert(!d.OnDrawing(1, false)); // menu / pause / no player
        assert(d.ActiveSource() == UpdateDispatch::Source::None);
        assert(d.OnDrawing(2, true));   // primary callback never fired
        assert(d.ActiveSource() == UpdateDispatch::Source::DrawingFallback);
        assert(!d.OnDrawing(2, true));  // repeated draw in same frame
        assert(d.OnDrawing(3, true));
        assert(!d.OnGameProcess(3));    // late normal callback; no double update
        assert(d.ObservedPrimary());
        assert(d.OnGameProcess(4));
        assert(d.ActiveSource() == UpdateDispatch::Source::GameProcess);
        assert(!d.OnDrawing(4, true));
        assert(!d.OnDrawing(5, true));  // only one missing game frame
        assert(!d.OnDrawing(6, true));  // two missing frames
        assert(d.OnDrawing(7, true));   // stalled primary: fallback resumes
        assert(d.ActiveSource() == UpdateDispatch::Source::DrawingFallback);
        assert(!d.OnDrawing(8, false)); // menu: no fallback
        assert(d.OnGameProcess(9));     // primary recovers
        assert(d.ActiveSource() == UpdateDispatch::Source::GameProcess);
        d.Reset();
        assert(!d.ObservedPrimary());
        assert(d.ActiveSource() == UpdateDispatch::Source::None);
        assert(d.OnDrawing(1, true));
    }
    {
        // Wraparound-safe missed-frame accounting.
        UpdateDispatch d;
        assert(d.OnGameProcess(0xfffffffeu));
        assert(!d.OnDrawing(0xffffffffu, true));
        assert(!d.OnDrawing(0u, true));
        assert(d.OnDrawing(1u, true));
    }
    std::cout << "update dispatch tests passed\n";
    return 0;
}
