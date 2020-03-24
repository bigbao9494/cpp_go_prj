#pragma once
#include <map>
#include "../common/clock.h"
#include "../task/task.h"

namespace co
{
	//Processor��תʱepoll_waite�ĳ�ʱʱ��
	static const int MIN_WAITE_TIME = 10;

	typedef std::function<void(void*)> TimerCallBack;
	enum TimerType
	{
		TimerTimeout = 0,	//��ʱ��
		TaskTimeout = 1		//task_sleep��ʱ
	};
	struct TimerElement
	{
		TimerType timer_type_;			//timer����
		TimerCallBack timer_call_back_;	//��ʱ��������û��ص�����
		Task* task_;					//task_sleep�����Processer::SuspendBySelfIO��task�����ж������Ƴ���������״̬����Ҫ�������task��ʱ�䵽��ʱ������
		void* user_param;				//C�ӿڳ�ʱ�ص��ĺ�������
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
		//timer���ڵ�Processer
		Processer* processer_ = nullptr;
		//����processer_����
		//��Ϊ��ͬһtimer�Ĳ������ܴ����ڶ���߳��У����磺һ���̴߳�����timer����һ���߳���ȥstop
		//���������������Ϊ�޷���StopTimerʱȷ�ϵ������Ƿ���Ч(Ҳ����ͨ���쳣������)
		//����ֻ������һ���߳���ͨ��processer_�Ƿ�Ϊ����ȷ��timer�Ƿ������timer_map_��
		LFLock lock_;
	};
	typedef std::shared_ptr<Timer> TimerID;

	/*
	��ʱ������ͳ�ʱ�������
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

			//���ɱ����ѵ�ʱ��
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
			element.task_ = nullptr;//TimerTimeout������task
			element.user_param = userParam;

			//�ѵ�ǰ�����ص���ӵ�timer_map_�У�������һ��Timer��ʹ����
			TimerID id(new Timer);
			//id.id_ = timer_map_.end();
			//���ɱ����ѵ�ʱ��
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

			//�ѵ�ǰ�����ص���ӵ�timer_map_�У�������һ��Timer��ʹ����
			TimerID id(new Timer);
			{
				map_lock_.lock();
				//���ɱ����ѵ�ʱ��
				id->id_ = timer_map_.insert(std::make_pair(timepoint, element));
				map_lock_.unlock();
			}
			return id;
		}

		//���ж���ʱ�����Ԫ�س�ʱ�� = ���ȳ�ʱ��Ԫ�ص�ʱ�� - ϵͳʱ��
		int GetLastDuration()
		{
			std::lock_guard<LFLock> locker(map_lock_);

			if (timer_map_.empty())
				return MIN_WAITE_TIME;
			std::chrono::milliseconds dt = std::chrono::duration_cast<std::chrono::milliseconds>(timer_map_.begin()->first - FastSteadyClock::now());
			int dtm = (int)dt.count();
			//�Ѿ���Ԫ�س�ʱ�ˣ���Ҫ�������WakeupTimers
			if (dtm <= 0)
				return 0;
			return dtm;
		}
		//���ѳ�ʱ��timer��task
		void WakeupTimers();
		//ֹͣtimer��������Ѿ��Ǳ������˵�timer����false��δ�����ѵķ���true
		bool StopTimer(TimerID& id);
	private:
		//��time_point�����map
		std::multimap<FastSteadyClock::time_point,TimerElement> timer_map_;
		//timer_map_���߳�������Ϊ���ܳ���һ��processor����ӵ�timer��������һ��processor�б�ȡ��
		LFLock map_lock_;
#ifdef DEBUG
		int max_diff = 0;
#endif
	};

}