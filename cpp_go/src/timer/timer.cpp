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
			//�ҵ����г�ʱ��Ԫ��,����map�еĵ�1��>��ǰϵͳ��clock�����֮ǰ������Ԫ�ض��ǳ�ʱ��
			//���磺��1,1,2,3,4���ҵ���1��>2.5  --> 3��3֮ǰ�����ж��ǳ�ʱ��
			if (it->first > now) 
			{
				break;
			}
			else
			{
				//��ʱ��Ӧ�ñ�����
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
					//���û��ص����������п��ܷ���timer_map_���������ͷ���
					locker.unlock();
					//���ó�ʱ�û��ص�����
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
					//����task,��task�������ж���
					it->second.task_->proc_->WakeupBySelfIO(it->second.task_);
				}
			}
		}

		//ɾ���Ѿ���ʱ��Ԫ��
		if (it != timer_map_.begin())
		{
			timer_map_.erase(timer_map_.begin(), it);
		}

	}

	bool TimerManager::StopTimer(TimerID& id)
	{
		std::lock_guard<LFLock> locker(map_lock_);
		timer_map_.erase(id->id_);
		//��ʾtimer�Ѿ���������
		id->processer_ = nullptr;

		return true;
	}
}
