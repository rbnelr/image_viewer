#pragma once

#include <thread>

#include "threadsafe_queue.hpp"

template <typename Job, typename Result, typename Job_Processor>
class Threadpool {
public:
	Threadsafe_Queue<Job>		jobs;
	Threadsafe_Queue<Result>	results;

	void start_threads (int thread_count) {
		for (int i=0; i<thread_count; ++i) {
			threads.emplace_back( &Threadpool::img_loader_thread_pool_thread, this, i );
		}
	}
	int get_thread_count () { return (int)threads.size(); }

	~Threadpool () {

		jobs.stop_all();

		for (auto& t : threads)
			t.join();
	}

private:
	std::vector< std::thread >	threads;

	void img_loader_thread_pool_thread (int thread_indx) {
		
		Job job;
		
		while (jobs.pop_or_stop(&job) != decltype(jobs)::STOP) {
			
			Result res = Job_Processor::process_job(std::move(job));
			
			results.push( std::move(res) );
		}
	}
};
