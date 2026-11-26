#include "worker.hpp"

namespace spools {

Worker::Worker() : thread([this] { run(); }) {}

Worker::~Worker() {
	{
		std::lock_guard<std::mutex> lock(mutex);
		stopping = true;
	}
	cv.notify_all();
	thread.join();
}

void Worker::post(std::function<void()> job) {
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (stopping) return;
		jobs.push_back(std::move(job));
	}
	cv.notify_all();
}

void Worker::wait() {
	std::unique_lock<std::mutex> lock(mutex);
	cv.wait(lock, [&] { return jobs.empty() && !busy; });
}

void Worker::run() {
	std::unique_lock<std::mutex> lock(mutex);
	for (;;) {
		cv.wait(lock, [&] { return stopping || !jobs.empty(); });
		if (stopping) return;
		auto job = std::move(jobs.front());
		jobs.pop_front();
		busy = true;
		lock.unlock();
		job();
		lock.lock();
		busy = false;
		cv.notify_all();
	}
}

}
