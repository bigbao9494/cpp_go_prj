#include "../scheduler/scheduler.h"
#include "../scheduler/processer.h"
#include "co_condition_variable.h"

namespace co
{
	CoConditionVariable::CoConditionVariable()
	{
	}
	CoConditionVariable::~CoConditionVariable()
	{
	}
	bool CoConditionVariable::Wait(CoMutex& mutex, int timeout)
	{
		assert(timeout != 0);
		//只能在task里使用
		assert(Processer::IsCoroutine());
		lock_ = &mutex;

		//超时唤醒标志
		bool isTimeOut = false;
		TimerID timer;
		Task* currentTask = Processer::GetCurrentTask();
		assert(currentTask);

		//需要超时而不是永久等待
		if (timeout > 0)
		{
			timer = Processer::GetCurrentScheduler()->CreateMilliTimer(
				timeout,
				[&isTimeOut, currentTask](void* userParam = nullptr) {
				isTimeOut = true;
				currentTask->state_ = TaskState::runnable;//设置运行状态
				currentTask->proc_->WakeupBySelfIO(currentTask);//把task加入运行队列
			});
		}

		//从运行队列中移除
		currentTask->proc_->SuspendBySelfIO(currentTask);
		//把当前task添加到waite_queue_
		waite_queue_.emplace(currentTask);
		//waite_queue_的操作后就释放锁，尽量减少粒度
		mutex.unlock();
		//切换出去等待被唤醒
		Processer::StaticCoYield();

		//被唤醒时需要占有锁
		mutex.lock();
		//task切换回来，要么超时要么被唤醒
		if (timer)//使用了定时器
		{
			//还没有超时需要手动停止
			if (!isTimeOut)
			{
				Processer::GetCurrentScheduler()->StopTimer(timer);
			}
			else //超时返回false
				return false;
			timer.reset();
		}
		return true;//返回成功
	}
	void CoConditionVariable::NotifyOne()
	{
		//必须在拿到锁后才调用
		assert(lock_);
		//只能在task里使用
		assert(Processer::IsCoroutine());

		//waite_queue_的下一个task
		if (!waite_queue_.empty())
		{
			Task* wakeup = waite_queue_.front();
			waite_queue_.pop();
			//唤醒这个task
			wakeup->proc_->WakeupBySelfIO(wakeup);
			//唤醒阻塞在WaitLoop函数的Processer，让它立刻进行调度
			wakeup->proc_->InterrupteWaitLoop();
		}
	}
	void CoConditionVariable::NotifyAll()
	{
		////必须在拿到锁后才调用
		//assert(lock_);
		//只能在task里使用
		assert(Processer::IsCoroutine());

		//waite_queue_中所有的task
		while (!waite_queue_.empty())
		{
			Task* wakeup = waite_queue_.front();
			waite_queue_.pop();
			//唤醒这个task
			wakeup->proc_->WakeupBySelfIO(wakeup);
			//唤醒阻塞在WaitLoop函数的Processer，让它立刻进行调度
			wakeup->proc_->InterrupteWaitLoop();
		}
	}
}