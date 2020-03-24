#include "co_mutex.h"
#include "../scheduler/scheduler.h"
#include "../scheduler/processer.h"

using namespace std;

namespace co
{
	/*
	与原始设计比较：
		1、每个CoMutex只有一个LFLock，原始设计有std::mutex、atomic_long、ConditionVariableAny
		2、原始设计中的ConditionVariableAny一定会给每个task多动态分配一个Entry
		3、每个Entry中有LFLock、atomic_t<int>，带来更多的空间开销
		4、原始设计对task的唤醒使用了独立的锁(Entry中的额外开销)，设计上看效率更高但是测试效果相当
	*/
	CoMutex::CoMutex()
	{
	}
	CoMutex::~CoMutex()
	{
		//暂时简单处理析构，不应该有task在使用这个锁
		assert(waite_queue_.empty());
		assert(!owner_task_);
	}
	void CoMutex::lock()
	{
		//只能在task里使用
		assert(Processer::IsCoroutine());
		if (!Processer::IsCoroutine())
			return;

		Task* currentTask = Processer::GetCurrentTask();
		assert(currentTask);
		unique_lock<lock_t> lk(lock_);
		if (!owner_task_)
		{
			//设置owner_task_表示得到锁
			owner_task_ = currentTask;
			assert(owner_task_);
			return;
		}
		else//锁被占用
		{
			lk.unlock();
			Processer::StaticCoYield();//让出CPU本线程中的其它task有机会执行
			for (int i = 0;i < 1;i++)//下次调度到自己的时候，自旋尝试拿锁
			{
				//有人释放了锁
				if (lk.try_lock())
				{
					if (!owner_task_)
					{
						//设置owner_task_表示得到锁
						owner_task_ = currentTask;
						return;
					}
					lk.unlock();
				}
			}

			//表示自旋拿锁失败需要把task挂起了
			currentTask->proc_->SuspendBySelfIO(currentTask);//从运行队列中移除,为了减小锁粒度把SuspendBySelfIO放到锁外
			lk.lock();
			/*
			可能出现CoMutex::unlock时waite_queue_中没有task
			而当前的task又被添加到waite_queue_中，没有task来调用CoMutex::unlock唤醒它，出现死task永远存在于waite_queue_
			*/
			if (!owner_task_)
			{
				owner_task_ = currentTask;
				//唤醒这个task
				currentTask->proc_->WakeupBySelfIO(currentTask);
				return;
			}
			//把当前task添加到waite_queue_
			waite_queue_.emplace(currentTask);
			//waite_queue_的操作后就释放锁，尽量减少粒度
			lk.unlock();
			//切换出去等待被唤醒
			DebugPrint(dbg_ioblock, "task(%s) enter io_block", currentTask->DebugInfo());
			Processer::StaticCoYield();
		}
	}
	bool CoMutex::try_lock()
	{
		//只能在task里使用
		assert(Processer::IsCoroutine());
		if (!Processer::IsCoroutine())
			return false;

		Task* currentTask = Processer::GetCurrentTask();
		assert(currentTask);
		unique_lock<lock_t> lk(lock_);
		if (!owner_task_)
		{
			//设置owner_task_表示得到锁
			owner_task_ = currentTask;
			return true;
		}
		else //被其它task占用，直接返回当前task不会被阻塞
			return false;
	}
	bool CoMutex::is_lock()
	{
		return owner_task_ != nullptr;
	}
	void CoMutex::unlock()
	{
		//只能在task里使用
		assert(Processer::IsCoroutine());
		if (!Processer::IsCoroutine())
			return;

		Task* currentTask = Processer::GetCurrentTask();
		assert(currentTask);
		unique_lock<lock_t> lk(lock_);
		if (!owner_task_)
		{
			//释放一个未被占用的锁
			return;
		}
		else//锁被占用
		{
			//只可能是占用锁的task去释放锁
			assert(currentTask == owner_task_);
			//waite_queue_的下一个task
			if (!waite_queue_.empty())
			{
				Task* wakeup = waite_queue_.front();
				waite_queue_.pop();
				//wakeup拥有锁
				owner_task_ = wakeup;
				//waite_queue_的操作后就释放锁，尽量减少粒度
				lk.unlock();
				//唤醒这个task
				wakeup->proc_->WakeupBySelfIO(wakeup);
				//唤醒阻塞在WaitLoop函数的Processer，让它立刻进行调度
				wakeup->proc_->InterrupteWaitLoop();
			}
			else//有可能等待锁的task在自旋，这时候waite_queue_为空
				owner_task_ = nullptr;
		}
	}
}
