#include "co_mutex.h"
#include "../scheduler/scheduler.h"
#include "../scheduler/processer.h"

using namespace std;

namespace co
{
	/*
	��ԭʼ��ƱȽϣ�
		1��ÿ��CoMutexֻ��һ��LFLock��ԭʼ�����std::mutex��atomic_long��ConditionVariableAny
		2��ԭʼ����е�ConditionVariableAnyһ�����ÿ��task�ද̬����һ��Entry
		3��ÿ��Entry����LFLock��atomic_t<int>����������Ŀռ俪��
		4��ԭʼ��ƶ�task�Ļ���ʹ���˶�������(Entry�еĶ��⿪��)������Ͽ�Ч�ʸ��ߵ��ǲ���Ч���൱
	*/
	CoMutex::CoMutex()
	{
	}
	CoMutex::~CoMutex()
	{
		//��ʱ�򵥴�����������Ӧ����task��ʹ�������
		assert(waite_queue_.empty());
		assert(!owner_task_);
	}
	void CoMutex::lock()
	{
		//ֻ����task��ʹ��
		assert(Processer::IsCoroutine());
		if (!Processer::IsCoroutine())
			return;

		Task* currentTask = Processer::GetCurrentTask();
		assert(currentTask);
		unique_lock<lock_t> lk(lock_);
		if (!owner_task_)
		{
			//����owner_task_��ʾ�õ���
			owner_task_ = currentTask;
			assert(owner_task_);
			return;
		}
		else//����ռ��
		{
			lk.unlock();
			Processer::StaticCoYield();//�ó�CPU���߳��е�����task�л���ִ��
			for (int i = 0;i < 1;i++)//�´ε��ȵ��Լ���ʱ��������������
			{
				//�����ͷ�����
				if (lk.try_lock())
				{
					if (!owner_task_)
					{
						//����owner_task_��ʾ�õ���
						owner_task_ = currentTask;
						return;
					}
					lk.unlock();
				}
			}

			//��ʾ��������ʧ����Ҫ��task������
			currentTask->proc_->SuspendBySelfIO(currentTask);//�����ж������Ƴ�,Ϊ�˼�С�����Ȱ�SuspendBySelfIO�ŵ�����
			lk.lock();
			/*
			���ܳ���CoMutex::unlockʱwaite_queue_��û��task
			����ǰ��task�ֱ���ӵ�waite_queue_�У�û��task������CoMutex::unlock��������������task��Զ������waite_queue_
			*/
			if (!owner_task_)
			{
				owner_task_ = currentTask;
				//�������task
				currentTask->proc_->WakeupBySelfIO(currentTask);
				return;
			}
			//�ѵ�ǰtask��ӵ�waite_queue_
			waite_queue_.emplace(currentTask);
			//waite_queue_�Ĳ�������ͷ�����������������
			lk.unlock();
			//�л���ȥ�ȴ�������
			DebugPrint(dbg_ioblock, "task(%s) enter io_block", currentTask->DebugInfo());
			Processer::StaticCoYield();
		}
	}
	bool CoMutex::try_lock()
	{
		//ֻ����task��ʹ��
		assert(Processer::IsCoroutine());
		if (!Processer::IsCoroutine())
			return false;

		Task* currentTask = Processer::GetCurrentTask();
		assert(currentTask);
		unique_lock<lock_t> lk(lock_);
		if (!owner_task_)
		{
			//����owner_task_��ʾ�õ���
			owner_task_ = currentTask;
			return true;
		}
		else //������taskռ�ã�ֱ�ӷ��ص�ǰtask���ᱻ����
			return false;
	}
	bool CoMutex::is_lock()
	{
		return owner_task_ != nullptr;
	}
	void CoMutex::unlock()
	{
		//ֻ����task��ʹ��
		assert(Processer::IsCoroutine());
		if (!Processer::IsCoroutine())
			return;

		Task* currentTask = Processer::GetCurrentTask();
		assert(currentTask);
		unique_lock<lock_t> lk(lock_);
		if (!owner_task_)
		{
			//�ͷ�һ��δ��ռ�õ���
			return;
		}
		else//����ռ��
		{
			//ֻ������ռ������taskȥ�ͷ���
			assert(currentTask == owner_task_);
			//waite_queue_����һ��task
			if (!waite_queue_.empty())
			{
				Task* wakeup = waite_queue_.front();
				waite_queue_.pop();
				//wakeupӵ����
				owner_task_ = wakeup;
				//waite_queue_�Ĳ�������ͷ�����������������
				lk.unlock();
				//�������task
				wakeup->proc_->WakeupBySelfIO(wakeup);
				//����������WaitLoop������Processer���������̽��е���
				wakeup->proc_->InterrupteWaitLoop();
			}
			else//�п��ܵȴ�����task����������ʱ��waite_queue_Ϊ��
				owner_task_ = nullptr;
		}
	}
}
