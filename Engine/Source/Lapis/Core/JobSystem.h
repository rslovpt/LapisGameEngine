#pragma once

// Lapis job system: a fixed pool of worker threads used by the engine to run
// work in parallel automatically (physics narrow phase, culling, ...).
//
//   JobSystem::ParallelFor(count, minBatch, [&](u32 begin, u32 end) { ... });
//
// The calling thread always helps with the work, so ParallelFor never
// deadlocks even when called from inside a job. Small workloads (fewer than
// minBatch * 2 items) run inline on the caller with zero overhead.
//
// Manual control: SetWorkerCount(0) = automatic (hardware threads - 1),
// SetWorkerCount(n) = exactly n workers, SetEnabled(false) = everything runs
// on the calling thread (useful for debugging / determinism comparisons).

#include "Lapis/Core/Base.h"

#include <functional>

namespace Lapis {

class JobSystem {
public:
    using RangeFn = std::function<void(u32 begin, u32 end)>;

    static void Init(u32 workerCount = 0);   // 0 = automatic
    static void Shutdown();

    static void SetWorkerCount(u32 workerCount);  // 0 = automatic; restarts the pool
    static u32  GetWorkerCount();                 // worker threads (not counting the caller)
    static u32  GetHardwareThreads();

    static void SetEnabled(bool enabled);
    static bool IsEnabled();

    // Runs fn over [0, count) split in batches of at least minBatch items.
    static void ParallelFor(u32 count, u32 minBatch, const RangeFn& fn);

    // Number of ParallelFor calls that actually went wide (for stats/tests).
    static u64 GetParallelDispatches();
};

} // namespace Lapis
