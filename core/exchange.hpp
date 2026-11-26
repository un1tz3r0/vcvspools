#pragma once
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace spools {

// Single-producer single-consumer ring of pointers. push() and pop() never block or allocate.
template <typename T, size_t N = 64>
class SpscQueue {
	std::array<T*, N> buf{};
	std::atomic<size_t> head{0}, tail{0};

public:
	bool push(T* p) {
		size_t t = tail.load(std::memory_order_relaxed);
		if (t - head.load(std::memory_order_acquire) == N) return false;
		buf[t % N] = p;
		tail.store(t + 1, std::memory_order_release);
		return true;
	}
	T* pop() {
		size_t h = head.load(std::memory_order_relaxed);
		if (h == tail.load(std::memory_order_acquire)) return nullptr;
		T* p = buf[h % N];
		head.store(h + 1, std::memory_order_release);
		return p;
	}
};

// Hands immutable objects from a producer thread to the audio thread, and hands retired ones
// back, so the audio thread never allocates, frees or locks. One producer, one audio consumer.
template <typename T>
class Exchange {
	std::mutex mutex;
	std::unordered_map<const T*, std::shared_ptr<const T>> owned;  // keeps everything the audio thread may see alive
	std::shared_ptr<const T> newest;
	std::atomic<const T*> pending{nullptr};
	const T* current = nullptr;  // audio thread only
	SpscQueue<const T> retired;

	void drop(const T* p) { if (p && p != newest.get()) owned.erase(p); }

public:
	// Producer side.
	void publish(std::shared_ptr<const T> next) {
		std::lock_guard<std::mutex> lock(mutex);
		publishLocked(std::move(next));
	}
	// Publishes only if nothing else was published since `expected`; for results derived from it.
	bool replace(const std::shared_ptr<const T>& expected, std::shared_ptr<const T> next) {
		std::lock_guard<std::mutex> lock(mutex);
		if (newest != expected) return false;
		publishLocked(std::move(next));
		return true;
	}
	void publishLocked(std::shared_ptr<const T> next) {
		collectLocked();
		const T* raw = next.get();
		if (raw) owned[raw] = next;
		newest = std::move(next);
		const T* skipped = pending.exchange(raw, std::memory_order_acq_rel);
		if (skipped != raw) drop(skipped);  // never reached the audio thread
	}
	void collect() { std::lock_guard<std::mutex> lock(mutex); collectLocked(); }
	void collectLocked() { while (const T* p = retired.pop()) drop(p); }

	// UI side: the most recently published object (may be ahead of the audio thread).
	std::shared_ptr<const T> latest() { std::lock_guard<std::mutex> lock(mutex); return newest; }

	// Audio thread: lock-free. Returns the object to use for this block, or nullptr.
	const T* acquire() {
		const T* p = pending.exchange(nullptr, std::memory_order_acq_rel);
		if (p && p != current) {
			if (current) retired.push(current);  // if the queue is full it simply stays owned until destruction
			current = p;
		}
		return current;
	}
	size_t ownedCount() { std::lock_guard<std::mutex> lock(mutex); return owned.size(); }
};

}
