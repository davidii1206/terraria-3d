#pragma once
// Worker thread pool for everything that must not block the main/render thread:
//   - world generation of new chunks
//   - chunk meshing (client)
//   - lighting propagation (see world/lighting.hpp)
//   - chunk (de)serialisation and disk IO
//   - network packet compression
//
// Design notes:
//   * N = hardware_concurrency - 2 workers, FIFO queue with priorities
//     (chunks near the player > far chunks; remesh after a block edit > initial mesh).
//   * Jobs return results through a thread-safe "completed" queue that the main thread drains
//     once per frame, so game state is only ever mutated on the owning thread.
//   * Cancellation: a job for a chunk that has since been unloaded must be cheap to drop
//     (check a generation counter on the chunk when the job finishes).
//   * Later option: replace with a work-stealing scheduler (e.g. enkiTS) if profiling demands.

#include <functional>

namespace t3d
{
    class JobSystem
    {
      public:
        using Job = std::function<void()>;

        explicit JobSystem(unsigned worker_count = 0 /* 0 = auto */);
        ~JobSystem();

        // Queue a job; lower `priority` value runs first.
        void submit(Job job, int priority = 0);
        // Block until all queued jobs have finished (used on shutdown / world save).
        void wait_idle();
    };
} // namespace t3d
