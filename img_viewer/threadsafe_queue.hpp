#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>

// with help from https://stackoverflow.com/questions/15278343/c11-thread-safe-queue

template <typename T>
class Threadsafe_Queue {
public:
	// simply push one element onto the queue
	// can be called from multiple threads (multiple producer)
	void push (T elem) {
		std::lock_guard<std::mutex> lock(m);
		q.emplace( std::move(elem) );
		c.notify_one();
	}

	// wait to deque one element from the queue
	T pop () {
		std::unique_lock<std::mutex> lock(m);
		
		while(q.empty()) {
			c.wait(lock); // release lock as long as the wait and reaquire it afterwards.
		}
		
		T val = q.front();
		q.pop();
		return val;
	}

	// wait to deque one element from the queue or until stop is set
	enum { STOP=0, POP } pop_or_stop (T* out) {
		std::unique_lock<std::mutex> lock(m);

		while(q.empty()) {
			if (stop)
				return STOP;

			c.wait(lock); // release lock as long as the wait and reaquire it afterwards.
		}

		*out = std::move(q.front());
		q.pop();

		return POP;
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

	void stop_all () {
		std::lock_guard<std::mutex> lock(m);
		stop = true;
		c.notify_all();
	}

private:
	mutable std::mutex		m;
	std::condition_variable	c;

	std::queue<T>			q;
	bool					stop = false; // use is optional (makes sense to use this to stop threads of thread pools (ie. use this on the job queue), but does not make sense to use this on the results queue)
};
