#pragma once
#include "../common/config.h"
#include "timer/timer.h"
#include "co_mutex.h"

namespace co
{
	class CoConditionVariable
	{
	private:
		//禁止复制
		CoConditionVariable& operator=(const CoConditionVariable& fd) = delete;
		CoConditionVariable(const CoConditionVariable& rfd) = delete;
	public:
		CoConditionVariable();
		~CoConditionVariable();
		/*
		等待条件发生，如果条件不满足被阻塞，当条件满足时会被唤醒
		mutex与条件变量配合的互斥锁(协程锁)
		timeout超时时间：-1默认表示永远等待，>0表示超时ms值
		*/
		bool Wait(CoMutex& mutex, int timeout = INNER_UTIME_NO_TIMEOUT);
		void NotifyOne();
		void NotifyAll();
	private:
		//等待队列
		std::queue<Task*> waite_queue_;
		//条件变量对应的锁
		CoMutex* lock_ = nullptr;
	};
}