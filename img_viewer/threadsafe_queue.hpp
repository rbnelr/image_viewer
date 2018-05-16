#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>

// with help from https://stackoverflow.com/questions/15278343/c11-thread-safe-queue

template <typename T>
class threadsafe_queue {
public:
	// simply push one element onto the queue
	// can be called from multiple threads (multiple producer)
	void push (T elem) {
		std::lock_guard<std::mutex> lock(m);
		q.emplace( std::move(elem) );
		c.notify_one();
	}

	// deque one element from the queue or wait if there are none
	T pop () {
		std::unique_lock<std::mutex> lock(m);
		
		while(q.empty()) {
			c.wait(lock); // release lock as long as the wait and reaquire it afterwards.
		}
		
		T val = q.front();
		q.pop();
		return val;
	}

	// deque one element from the queue if there is one
	bool try_pop (T* out) {
		std::lock_guard<std::mutex> lock(m);
		
		if (q.empty())
			return false;

		*out = std::move(q.front());
		q.pop();

		return true;
	}

private:
	std::queue<T> q;
	mutable std::mutex m;
	std::condition_variable c;
};
