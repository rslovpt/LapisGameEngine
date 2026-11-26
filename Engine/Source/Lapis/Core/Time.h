#pragma once

#include <chrono>

namespace Lapis {

// High resolution stopwatch.
class Timer {
public:
    Timer() { Reset(); }

    void Reset() { m_Start = Clock::now(); }

    // Seconds since construction / last Reset().
    double Elapsed() const {
        return std::chrono::duration<double>(Clock::now() - m_Start).count();
    }
    double ElapsedMillis() const { return Elapsed() * 1000.0; }

private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point m_Start;
};

// Per-frame timing information handed to game code.
struct FrameTime {
    float  DeltaTime     = 0.0f; // seconds since last frame (clamped)
    double TotalTime     = 0.0;  // seconds since the application started
    unsigned long long FrameIndex = 0;
    float  FPS           = 0.0f; // smoothed frames per second
};

} // namespace Lapis
