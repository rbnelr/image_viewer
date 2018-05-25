#pragma once

#include <deque>
#include <mutex>
#include <condition_variable>

#include <algorithm>

// with help from https://stackoverflow.com/questions/15278343/c11-thread-safe-queue

template <typename T>
class Threadsafe_Queue {
public:
	// simply push one element onto the queue
	// can be called from multiple threads (multiple producer)
	void push (T elem) {
		std::lock_guard<std::mutex> lock(m);
		q.emplace_back( std::move(elem) );
		c.notify_one();
	}

	// wait to deque one element from the queue
	T pop () {
		std::unique_lock<std::mutex> lock(m);
		
		while(q.empty()) {
			c.wait(lock); // release lock as long as the wait and reaquire it afterwards.
		}
		
		T val = q.front();
		q.pop_front();
		return val;
	}

	// wait to deque one element from the queue or until stop is set
	enum { STOP=0, POP } pop_or_stop (T* out) {
		std::unique_lock<std::mutex> lock(m);

		while(!stop && q.empty()) {

			c.wait(lock); // release lock as long as the wait and reaquire it afterwards.
		}

		if (stop)
			return STOP;

		*out = std::move(q.front());
		q.pop_front();

		return POP;
	}

	// deque one element from the queue if there is one
	bool try_pop (T* out) {
		std::lock_guard<std::mutex> lock(m);
		
		if (q.empty())
			return false;

		*out = std::move(q.front());
		q.pop_front();

		return true;
	}

	void stop_all () {
		std::lock_guard<std::mutex> lock(m);
		stop = true;
		c.notify_all();
	}

	template <typename FOREACH>
	void iterate_queue_front_to_back (FOREACH callback) { // front == next to be popped, back == most recently pushed
		std::lock_guard<std::mutex> lock(m);
		
		for (auto it=q.begin(); it!=q.end(); ++it) {
			callback(*it);
		}
	}
	template <typename FOREACH>
	void iterate_queue_back_to_front (FOREACH callback) { // front == next to be popped, back == most recently pushed
		std::lock_guard<std::mutex> lock(m);

		for (auto it=q.rbegin(); it!=q.rend(); ++it) {
			callback(*it);
		}
	}

	template <typename NEED_TO_CANCEL>
	void cancel (NEED_TO_CANCEL need_to_cancel) {
		std::lock_guard<std::mutex> lock(m);

		for (auto it=q.begin(); it!=q.end();) {
			if (need_to_cancel(*it)) {
				it = q.erase(it);
			} else {
				++it;
			}
		}
	}
	void cancel_all () {
		std::lock_guard<std::mutex> lock(m);

		q.clear();
	}

	template <typename FOREACH>
	void cancel_all_and_call_foreach (FOREACH func) {
		std::lock_guard<std::mutex> lock(m);

		for (auto it=q.begin(); it!=q.end(); ++it) {
			func(*it);
		}

		q.clear();
	}

	template <typename COMPARATOR>
	void sort (COMPARATOR cmp) {
		std::lock_guard<std::mutex> lock(m);

		std::sort(q.begin(), q.end(), cmp);
	}

private:
	mutable std::mutex		m;
	std::condition_variable	c;

	std::deque<T>			q; // to support iteration (a queue is just wrapper around a deque, so it is not less efficient to use a deque over a queue)
	bool					stop = false; // use is optional (makes sense to use this to stop threads of thread pools (ie. use this on the job queue), but does not make sense to use this on the results queue)
};
