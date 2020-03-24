#pragma once
#include <map>
#include "../common/clock.h"
#include "../task/task.h"

namespace co
{
	//Processor空转时epoll_waite的超时时间
	static const int MIN_WAITE_TIME = 10;

	typedef std::function<void(void*)> TimerCallBack;
	enum TimerType
	{
		TimerTimeout = 0,	//定时器
		TaskTimeout = 1		//task_sleep超时
	};
	struct TimerElement
	{
		TimerType timer_type_;			//timer类型
		TimerCallBack timer_call_back_;	//定时器超后的用户回调函数
		Task* task_;					//task_sleep会调用Processer::SuspendBySelfIO把task从运行队列中移除后处于游离状态，需要保存这个task在时间到达时唤醒它
		void* user_param;				//C接口超时回调的函数参数
		TimerElement() {};
		~TimerElement() {};
	};
	struct Timer
	{
		friend class TimerManager;
		friend class Processer;
		friend class Scheduler;
	public:
		Timer() {};
		~Timer() { /*printf("~Timer()\n");*/ };
	private:
		std::multimap<FastSteadyClock::time_point, TimerElement>::iterator id_;
		//timer所在的Processer
		Processer* processer_ = nullptr;
		//访问processer_的锁
		//因为对同一timer的操作可能存在于多个线程中，比如：一个线程创建了timer，另一个线程中去stop
		//现在这样的设计因为无法在StopTimer时确认迭代器是否有效(也不能通过异常来捕获)
		//所以只能增加一个线程锁通过processer_是否为空来确认timer是否存在于timer_map_中
		LFLock lock_;
	};
	typedef std::shared_ptr<Timer> TimerID;

	/*
	定时器管理和超时任务管理
	*/
	class TimerManager
	{
		friend class Processer;
	private:
		TimerManager();
		~TimerManager();
		template <typename Rep, typename Period>
		bool AddSleeper(std::chrono::duration<Rep, Period> duration,Task* task)
		{
			TimerElement element;
			element.timer_type_ = TimerType::TaskTimeout;
			element.task_ = task;

			//生成被唤醒的时间
			FastSteadyClock::time_point now = FastSteadyClock::now() + duration;
			map_lock_.lock();
			timer_map_.insert(std::make_pair(now, element));
			map_lock_.unlock();
			return true;
		}

		template <typename Rep, typename Period>
		TimerID AddTimer(std::chrono::duration<Rep, Period> duration,TimerCallBack const& cb, void* userParam = nullptr)
		{
			TimerElement element;
			element.timer_type_ = TimerType::TimerTimeout;
			element.timer_call_back_ = cb;
			element.task_ = nullptr;//TimerTimeout不操作task
			element.user_param = userParam;

			//把当前函数回调添加到timer_map_中，并生成一个Timer给使用者
			TimerID id(new Timer);
			//id.id_ = timer_map_.end();
			//生成被唤醒的时间
			FastSteadyClock::time_point now = FastSteadyClock::now() + duration;
			{
				map_lock_.lock();
				id->id_ = timer_map_.insert(std::make_pair(now, element));
				map_lock_.unlock();
			}
			return id;
		}
		TimerID AddTimer(FastSteadyClock::time_point timepoint,TimerCallBack const& cb, void* userParam = nullptr)
		{
			TimerElement element;
			element.timer_type_ = TimerType::TimerTimeout;
			element.timer_call_back_ = cb;
			element.task_ = nullptr;
			element.user_param = userParam;

			//把当前函数回调添加到timer_map_中，并生成一个Timer给使用者
			TimerID id(new Timer);
			{
				map_lock_.lock();
				//生成被唤醒的时间
				id->id_ = timer_map_.insert(std::make_pair(timepoint, element));
				map_lock_.unlock();
			}
			return id;
		}

		//还有多少时间就有元素超时了 = 最先超时的元素的时间 - 系统时间
		int GetLastDuration()
		{
			std::lock_guard<LFLock> locker(map_lock_);

			if (timer_map_.empty())
				return MIN_WAITE_TIME;
			std::chrono::milliseconds dt = std::chrono::duration_cast<std::chrono::milliseconds>(timer_map_.begin()->first - FastSteadyClock::now());
			int dtm = (int)dt.count();
			//已经有元素超时了，需要尽快调用WakeupTimers
			if (dtm <= 0)
				return 0;
			return dtm;
		}
		//唤醒超时的timer和task
		void WakeupTimers();
		//停止timer，如果是已经是被唤醒了的timer返回false，未被唤醒的返回true
		bool StopTimer(TimerID& id);
	private:
		//以time_point升序的map
		std::multimap<FastSteadyClock::time_point,TimerElement> timer_map_;
		//timer_map_的线程锁，因为可能出现一个processor中添加的timer会在另外一个processor中被取消
		LFLock map_lock_;
#ifdef DEBUG
		int max_diff = 0;
#endif
	};

}