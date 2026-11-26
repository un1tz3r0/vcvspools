#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace spools {

// One background thread running jobs in order. Jobs posted after shutdown begins are dropped.
class Worker {
	std::mutex mutex;
	std::condition_variable cv;
	std::deque<std::function<void()>> jobs;
	bool stopping = false, busy = false;
	std::thread thread;

	void run();

public:
	Worker();
	~Worker();
	void post(std::function<void()> job);
	void wait();  // blocks until the queue is empty and no job is running (tests, shutdown)
};

}
