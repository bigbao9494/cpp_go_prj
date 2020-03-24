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
		//ֻ����task��ʹ��
		assert(Processer::IsCoroutine());
		lock_ = &mutex;

		//��ʱ���ѱ�־
		bool isTimeOut = false;
		TimerID timer;
		Task* currentTask = Processer::GetCurrentTask();
		assert(currentTask);

		//��Ҫ��ʱ���������õȴ�
		if (timeout > 0)
		{
			timer = Processer::GetCurrentScheduler()->CreateMilliTimer(
				timeout,
				[&isTimeOut, currentTask](void* userParam = nullptr) {
				isTimeOut = true;
				currentTask->state_ = TaskState::runnable;//��������״̬
				currentTask->proc_->WakeupBySelfIO(currentTask);//��task�������ж���
			});
		}

		//�����ж������Ƴ�
		currentTask->proc_->SuspendBySelfIO(currentTask);
		//�ѵ�ǰtask��ӵ�waite_queue_
		waite_queue_.emplace(currentTask);
		//waite_queue_�Ĳ�������ͷ�����������������
		mutex.unlock();
		//�л���ȥ�ȴ�������
		Processer::StaticCoYield();

		//������ʱ��Ҫռ����
		mutex.lock();
		//task�л�������Ҫô��ʱҪô������
		if (timer)//ʹ���˶�ʱ��
		{
			//��û�г�ʱ��Ҫ�ֶ�ֹͣ
			if (!isTimeOut)
			{
				Processer::GetCurrentScheduler()->StopTimer(timer);
			}
			else //��ʱ����false
				return false;
			timer.reset();
		}
		return true;//���سɹ�
	}
	void CoConditionVariable::NotifyOne()
	{
		//�������õ�����ŵ���
		assert(lock_);
		//ֻ����task��ʹ��
		assert(Processer::IsCoroutine());

		//waite_queue_����һ��task
		if (!waite_queue_.empty())
		{
			Task* wakeup = waite_queue_.front();
			waite_queue_.pop();
			//�������task
			wakeup->proc_->WakeupBySelfIO(wakeup);
			//����������WaitLoop������Processer���������̽��е���
			wakeup->proc_->InterrupteWaitLoop();
		}
	}
	void CoConditionVariable::NotifyAll()
	{
		////�������õ�����ŵ���
		//assert(lock_);
		//ֻ����task��ʹ��
		assert(Processer::IsCoroutine());

		//waite_queue_�����е�task
		while (!waite_queue_.empty())
		{
			Task* wakeup = waite_queue_.front();
			waite_queue_.pop();
			//�������task
			wakeup->proc_->WakeupBySelfIO(wakeup);
			//����������WaitLoop������Processer���������̽��е���
			wakeup->proc_->InterrupteWaitLoop();
		}
	}
}