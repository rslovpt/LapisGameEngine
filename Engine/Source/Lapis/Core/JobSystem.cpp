#include "Lapis/Core/JobSystem.h"

#include "Lapis/Core/Log.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace Lapis {

namespace {

// One ParallelFor invocation shared by the caller and the workers.
struct Batch {
    const JobSystem::RangeFn* Fn = nullptr;
    u32 Count = 0;
    u32 Chunk = 1;
    std::atomic<u32> Next{0};
    std::atomic<u32> Done{0};
    std::atomic<int> Users{0};

    // Grab and run chunks until none are left.
    void Work() {
        for (;;) {
            const u32 begin = Next.fetch_add(Chunk);
            if (begin >= Count) return;
            const u32 end = std::min(Count, begin + Chunk);
            (*Fn)(begin, end);
            Done.fetch_add(end - begin);
        }
    }
};

struct Pool {
    std::vector<std::thread> Workers;
    std::mutex Mutex;
    std::condition_variable Wake;
    std::vector<Batch*> Queue;      // batches that still have unclaimed chunks
    bool Quit = false;
    bool Enabled = true;
    u32  Requested = 0;
    std::atomic<u64> Dispatches{0};

    // Static teardown at process exit: always join workers (never leak or
    // destroy a joinable std::thread).
    ~Pool() { Stop(); }

    void WorkerLoop() {
        for (;;) {
            Batch* batch = nullptr;
            {
                std::unique_lock<std::mutex> lock(Mutex);
                Wake.wait(lock, [&] { return Quit || !Queue.empty(); });
                if (Quit) return;
                batch = Queue.back();
                if (batch->Next.load() >= batch->Count) { Queue.pop_back(); continue; }
                batch->Users.fetch_add(1);
            }
            batch->Work();
            batch->Users.fetch_sub(1);
        }
    }

    void Start(u32 count) {
        Quit = false;
        for (u32 i = 0; i < count; ++i) Workers.emplace_back([this] { WorkerLoop(); });
    }

    void Stop() {
        {
            std::lock_guard<std::mutex> lock(Mutex);
            Quit = true;
        }
        Wake.notify_all();
        for (std::thread& t : Workers) t.join();
        Workers.clear();
        Queue.clear();
    }
};

Pool& GetPool() {
    static Pool pool;
    return pool;
}

u32 AutoWorkers() {
    const u32 hw = std::max(1u, std::thread::hardware_concurrency());
    return std::min(hw > 1 ? hw - 1 : 0u, 31u);
}

} // namespace

void JobSystem::Init(u32 workerCount) {
    Pool& p = GetPool();
    if (!p.Workers.empty()) return;
    p.Requested = workerCount;
    p.Start(workerCount ? std::min(workerCount, 63u) : AutoWorkers());
    LAPIS_INFO("Job system: {} worker threads ({} hardware threads)", p.Workers.size(), GetHardwareThreads());
}

void JobSystem::Shutdown() { GetPool().Stop(); }

void JobSystem::SetWorkerCount(u32 workerCount) {
    Pool& p = GetPool();
    p.Stop();
    p.Requested = workerCount;
    p.Start(workerCount ? std::min(workerCount, 63u) : AutoWorkers());
}

u32 JobSystem::GetWorkerCount() { return (u32)GetPool().Workers.size(); }
u32 JobSystem::GetHardwareThreads() { return std::max(1u, std::thread::hardware_concurrency()); }
void JobSystem::SetEnabled(bool enabled) { GetPool().Enabled = enabled; }
bool JobSystem::IsEnabled() { return GetPool().Enabled; }
u64 JobSystem::GetParallelDispatches() { return GetPool().Dispatches.load(); }

void JobSystem::ParallelFor(u32 count, u32 minBatch, const RangeFn& fn) {
    if (count == 0) return;
    Pool& p = GetPool();
    minBatch = std::max(minBatch, 1u);
    const u32 threads = (u32)p.Workers.size() + 1;
    if (!p.Enabled || threads == 1 || count < minBatch * 2) {
        fn(0, count);
        return;
    }

    // ~4 chunks per thread for load balancing, but never below minBatch.
    Batch batch;
    batch.Fn = &fn;
    batch.Count = count;
    batch.Chunk = std::max(minBatch, (count + threads * 4 - 1) / (threads * 4));
    {
        std::lock_guard<std::mutex> lock(p.Mutex);
        p.Queue.push_back(&batch);
    }
    p.Wake.notify_all();
    p.Dispatches.fetch_add(1);

    batch.Work();

    // Remove the batch from the queue so no new worker picks it up, then wait
    // for all items and for every worker that is still inside it.
    {
        std::lock_guard<std::mutex> lock(p.Mutex);
        p.Queue.erase(std::remove(p.Queue.begin(), p.Queue.end(), &batch), p.Queue.end());
    }
    while (batch.Done.load() < count || batch.Users.load() > 0) std::this_thread::yield();
}

} // namespace Lapis
