#include "timer.h"
#include "../scheduler/processer.h"
using namespace std;

namespace co
{

	TimerManager::TimerManager()
	{
	}
	TimerManager::~TimerManager()
	{

	}
	void TimerManager::WakeupTimers()
	{
		std::unique_lock<LFLock> locker(map_lock_);
		if (timer_map_.empty())
			return;

		FastSteadyClock::time_point now = FastSteadyClock::now();
		auto it = timer_map_.begin();
		for (; it != timer_map_.end(); ++it)
		{
			//找到所有超时的元素,升序map中的第1个>当前系统的clock，这个之前的所有元素都是超时的
			//比如：从1,1,2,3,4中找到第1个>2.5  --> 3，3之前的所有都是超时的
			if (it->first > now) 
			{
				break;
			}
			else
			{
				//超时的应该被处理
				if (it->second.timer_type_ == TimerType::TimerTimeout)
				{
#ifdef DEBUG
					int diff = chrono::duration_cast<chrono::milliseconds>(now - it->first).count();
					if (max_diff < diff)
					{
						max_diff = diff;
						printf("timer max diff: %d\n", /*it->second.task_->id_,*/ max_diff);
					}
#endif
					//在用户回调函数里面有可能访问timer_map_，所以先释放锁
					locker.unlock();
					//调用超时用户回调函数
					it->second.timer_call_back_(it->second.user_param);
					locker.lock();
				}
				else if (it->second.timer_type_ == TimerType::TaskTimeout)
				{
					assert(it->second.task_);
					assert(it->second.task_->proc_);
#ifdef DEBUG
					int diff = chrono::duration_cast<chrono::milliseconds>(now - it->first).count();
					if (max_diff < diff)
					{
						max_diff = diff;
						printf("task: %u sleep max diff: %d\n", it->second.task_->id_, max_diff);
					}
#endif
					//唤醒task,把task加入运行队列
					it->second.task_->proc_->WakeupBySelfIO(it->second.task_);
				}
			}
		}

		//删除已经超时的元素
		if (it != timer_map_.begin())
		{
			timer_map_.erase(timer_map_.begin(), it);
		}

	}

	bool TimerManager::StopTimer(TimerID& id)
	{
		std::lock_guard<LFLock> locker(map_lock_);
		timer_map_.erase(id->id_);
		//表示timer已经被处理了
		id->processer_ = nullptr;

		return true;
	}
}
