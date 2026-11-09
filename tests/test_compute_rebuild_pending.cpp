// Synthetic regression for asynchronous compute pipeline replacement (#969).
// No game assets or Vulkan device required. The state machine mirrors CurrentComputePipeline.
// c++ -std=c++20 -O1 -Wall -Wextra -fsanitize=undefined -fno-sanitize-recover=all tests/test_compute_rebuild_pending.cpp -o /tmp/compute-pending && /tmp/compute-pending
#include <atomic>
#include <cassert>
#include <memory>
#include <utility>

struct Pipeline {
    std::atomic_bool built{false};
    std::atomic_bool worker_done{false};
    bool handle{};
    bool IsBuilt() const { return built.load(std::memory_order_acquire); }
    bool IsBuildTaskDone() const { return worker_done.load(std::memory_order_acquire); }
    bool HasPipeline() const { return handle; }
};
struct Entry {
    std::shared_ptr<Pipeline> pipeline;
    std::unique_ptr<Entry> pending;
    unsigned rebuilds{};
};
constexpr unsigned MaxComputeRebuilds = 4;

void Advance(Entry& entry) {
    if (entry.pending && entry.pending->pipeline->IsBuilt() &&
        entry.pending->pipeline->IsBuildTaskDone()) {
        if (entry.pending->pipeline->HasPipeline()) {
            auto rebuilt = std::move(entry.pending);
            entry.pipeline = std::move(rebuilt->pipeline);
            entry.rebuilds = rebuilt->rebuilds;
        } else {
            entry.pending.reset();
            entry.rebuilds = MaxComputeRebuilds;
        }
    }
}
std::shared_ptr<Pipeline> MakePipeline(bool built, bool worker_done, bool handle) {
    auto p = std::make_shared<Pipeline>();
    p->handle = handle;
    p->built.store(built, std::memory_order_release);
    p->worker_done.store(worker_done, std::memory_order_release);
    return p;
}
Entry MakeEntry(std::shared_ptr<Pipeline> current, std::shared_ptr<Pipeline> next) {
    Entry e;
    e.pipeline = std::move(current);
    e.pending = std::make_unique<Entry>();
    e.pending->pipeline = std::move(next);
    e.pending->rebuilds = 2;
    return e;
}
int main() {
    const auto working = MakePipeline(true, true, true);

    // Driver rejection signals IsBuilt even though the Vulkan handle is null.
    auto rejected = MakePipeline(false, false, false);
    auto e = MakeEntry(working, rejected);
    Advance(e);
    assert(e.pipeline == working && e.pending);
    rejected->built.store(true, std::memory_order_release);
    Advance(e);
    assert(e.pipeline == working && e.pending); // Worker still owns the object.
    rejected->worker_done.store(true, std::memory_order_release);
    Advance(e);
    assert(e.pipeline == working && !e.pending && e.rebuilds == MaxComputeRebuilds);

    // A finished, valid replacement does switch.
    const auto good = MakePipeline(true, true, true);
    auto s = MakeEntry(working, good);
    Advance(s);
    assert(s.pipeline == good && !s.pending && s.rebuilds == 2);

    // A valid pipeline whose worker has not returned is not destroyed/switched yet.
    auto finishing = MakePipeline(true, false, true);
    auto f = MakeEntry(working, finishing);
    Advance(f);
    assert(f.pipeline == working && f.pending);
    finishing->worker_done.store(true, std::memory_order_release);
    Advance(f);
    assert(f.pipeline == finishing && !f.pending);
}
