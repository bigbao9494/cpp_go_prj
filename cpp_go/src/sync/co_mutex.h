#pragma once
#include "../common/config.h"
#include "../common/spinlock.h"
#include <queue>

namespace co
{
	class Task;
	//协程锁
	class CoMutex
	{
		typedef LFLock lock_t;
	private:
		//waite_queue_和owner_task_的锁
		lock_t lock_;
		std::queue<Task*> waite_queue_;
		Task* owner_task_ = nullptr;
	public:
		CoMutex();
		~CoMutex();
		void lock();
		bool try_lock();
		bool is_lock();
		void unlock();
	private:
		//禁止复制
		CoMutex& operator=(const CoMutex& fd) = delete;
		CoMutex(const CoMutex& rfd) = delete;
	};
	typedef CoMutex co_mutex;
} //namespace co
