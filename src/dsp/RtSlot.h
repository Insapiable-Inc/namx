#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

/// Single-writer / single-reader publication slot for objects that a
/// real-time audio thread reads and a control (GUI) thread replaces.
///
/// The audio thread calls acquire() once at the top of a block and endBlock()
/// once at the bottom — on every path, bypassed or not. The owner thread
/// calls publish() to install a new object (nullptr = "nothing loaded").
/// The previous object is not freed immediately: it is parked on a retired
/// list tagged with the block counter and deleted only once the audio thread
/// has provably completed a block that began after the swap. publish() never
/// blocks, so back-to-back publishes are safe regardless of how long the
/// audio thread takes; retired objects simply accumulate until reclaimable.
///
/// Memory: endBlock() releases, acquire()/publish()/reclaim() acquire, so the
/// audio thread sees a fully constructed object and the owner never frees
/// one that a still-running block could be reading.
template <typename T>
class RtSlot
{
public:
    RtSlot() {
        retired.reserve(4);
    }
    ~RtSlot() {
        // No audio thread may be running.
        delete active.exchange(nullptr, std::memory_order_acq_rel);
    }

    RtSlot(const RtSlot&) = delete;
    RtSlot& operator=(const RtSlot&) = delete;

    // ---- audio thread ----
    T* acquire() const {
        return active.load(std::memory_order_acquire);
    }
    void end_block() {
        blocks.fetch_add(1, std::memory_order_release);
    }

    // ---- owner thread ----
    /// Owner-side view of what is currently published (may be nullptr).
    T* current() const {
        return active.load(std::memory_order_acquire);
    }

    /// Install `next` (or nullptr) and retire whatever was there before.
    void publish(std::unique_ptr<T> next) {
        T* prev = active.exchange(next.release(), std::memory_order_acq_rel);
        // Tag read AFTER the swap: any block that saw `prev` either completed
        // before this read or was in flight and will bump the count past it.
        const uint64_t tag = blocks.load(std::memory_order_acquire);
        if (prev) {
            retired.push_back({tag, std::unique_ptr<T>(prev)});
        }
        reclaim(false);
    }

    /// Free retired objects the audio thread has left behind. `force` frees
    /// everything and is only valid when no audio thread can be running.
    void reclaim(bool force) {
        const uint64_t done = blocks.load(std::memory_order_acquire);
        size_t keep = 0;
        for (size_t i = 0; i < retired.size(); ++i) {
            if (!force && done <= retired[i].tag) {
                retired[keep++] = std::move(retired[i]);
            }
        }
        retired.resize(keep);
    }

    size_t retired_count() const {
        return retired.size();
    }

private:
    struct Retired
    {
        uint64_t tag;
        std::unique_ptr<T> obj;
    };

    std::atomic<T*> active{nullptr};
    std::atomic<uint64_t> blocks{0};
    std::vector<Retired> retired; // owner thread only
};
