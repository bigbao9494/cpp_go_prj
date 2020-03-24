#pragma once
#include "../common/config.h"
#include "timer/timer.h"
#include "channel_impl.h"
#include "ringbuffer.h"
#include "wait_queue.h"
#include <list>
#include <assert.h>

namespace co
{
	class Task;
	class Processer;
template <typename T>
class FastChannelImpl : public ChannelImpl<T>
{
	struct Entry : public WaitQueueHook
	{
		Entry(Task* task, T* val) : task_(task), value_(val) {};
		//��������task
		Task* task_;
		//taskҪ��д�����ݵ�ַ
		T* value_;
		//��д�ɹ���־
		bool success_;
	};
    //typedef std::mutex lock_t;
	typedef LFLock lock_t;
    typedef FastSteadyClock::time_point time_point_t;
	typedef std::function<void(T&)> ChanFunctor;

	//q_��lq_����
    lock_t lock_;
	//buffer��ʼ����
    const std::size_t capacity_;
    bool closed_;
    uint64_t dbg_mask_;

    bool useRingBuffer_;
	//ringBuffer��ʽ����
    RingBuffer<T> q_;
	//����ʽ����
    std::list<T> lq_;

	//дʧ�ܺ�task��������
    WaitQueue1<Entry> wq_;
	//��ʧ�ܺ�task��������
	WaitQueue1<Entry> rq_;

public:
    explicit FastChannelImpl(std::size_t capacity, bool useRingBuffer)
        : capacity_(capacity), closed_(false), dbg_mask_(dbg_all)
        , useRingBuffer_(useRingBuffer)
        , q_(useRingBuffer ? capacity : 1)
    {
#ifdef SYS_Linux
		assert(capacity < std::numeric_limits<std::size_t>::max());
#endif
        DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Channel init. capacity=%lu", this->getId(), capacity);
    }
    
    // write
    bool Push(T t,int timeout = INNER_UTIME_NO_TIMEOUT)
    {
        DebugPrint(dbg_channel, "[id=%ld] Push ->", this->getId());
        std::unique_lock<lock_t> lock(lock_);
		if (closed_) return false;

		//��buffer�����ֱ�ӻ���rq_�еȴ���Э�̣��ɹ��󷵻أ�ʧ�ܱ�ʾrq_����Э�̵ȴ���Ҫwq_.wait�����Լ��ȴ���pop���������߻���
        if (!capacity_)
		{
			//ֱ�Ӱ�����д���ȴ��Ķ���
			if (ReadNotifyOne([&t](T& p) {p = t; }))
			{
				DebugPrint(dbg_channel, "[id=%ld] Push Notify", this->getId());
				return true;
			}
        }
		else
		{
			if (push(t))
			{
				//push���ݺ�buffer�ж�ֻ��1�����ݣ�˵��֮ǰbufferΪ�գ�������pop��Э���ڵȴ�������
				if (Size() == 1)
				{
					//�����е�����pop���ȴ��еĶ���
					if (ReadNotifyOne([this](T& p) {pop(p); }))
					{
						DebugPrint(dbg_channel, "[id=%ld] Push Notify", this->getId());
					}
				}
				DebugPrint(dbg_channel, "[id=%ld] Push complete queued", this->getId());
				return true;
			}
		}

        if (timeout == 0) 
		{
            DebugPrint(dbg_channel, "[id=%ld] TryPush failed.", this->getId());
            return false;
        }

        DebugPrint(dbg_channel, "[id=%ld] Push wait", this->getId());

		//δ�ܳɹ�д�����ݣ������Լ��ȴ�������
		return WriteWaite(t,timeout, lock);
    }

    // read
    bool Pop(T & t, int timeout = INNER_UTIME_NO_TIMEOUT)
    {
        DebugPrint(dbg_channel, "[id=%ld] Pop ->", this->getId());
        std::unique_lock<lock_t> lock(lock_);
        if (closed_) return false;
		//��buffer
        if (capacity_ > 0) 
		{
			//��buffer��ȡ����
            if (pop(t)) 
			{
				//ȡ�ɹ�˵��buffer�������ݣ��ж�ȡ����ǰbuffer������״̬����������������wq_�У�֪ͨwq_������
                if (Size() == capacity_ - 1) 
				{
					//��wq_�еĵ�1��Э����ִ��push(p)�����Լ�������д�룬Ȼ��������ʾ�������ݳɹ�д��
					if (WriteNotifyOne([this](T& p) {push(p); }))
					{
                        DebugPrint(dbg_channel, "[id=%ld] Pop Notify size=%lu.", this->getId(), Size());
                    }
                }
                DebugPrint(dbg_channel, "[id=%ld] Pop complete unqueued.", this->getId());
				//�ɹ�ȡ�����ݷ���
                return true;
            }
        } 
		else 
		{
			//��buffer��ֱ��֪ͨwq_����.��ʱ�������д�ڵȴ�д�룬��ֱ�Ӱ�����д��t
            if (WriteNotifyOne([&t](T& p) {t = p;}))
			{
                DebugPrint(dbg_channel, "[id=%ld] Pop Notify ...", this->getId());
				//���ѳɹ�˵������������wq_�ϣ����ݻᱻ��д����Լ�( t = *p)�����سɹ�
                return true;
            }
        }

		if (timeout == 0)
		{
            DebugPrint(dbg_channel, "[id=%ld] TryPop failed.", this->getId());
            return false;
        }

        DebugPrint(dbg_channel, "[id=%ld] Pop wait.", this->getId());

		//δ�ܳɹ�ȡ�����ݣ������Լ��ȴ�������
		return ReadWaite(t, timeout, lock);
    }

    ~FastChannelImpl() {
        DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Channel destory.", this->getId());

        assert(lock_.try_lock());
    }

    void SetDbgMask(uint64_t mask) {
        dbg_mask_ = mask;
    }

    bool Empty()
    {
        return Size() == 0;
    }

    std::size_t Size()
    {
        return useRingBuffer_ ? q_.size() : lq_.size();
    }

    void Close()
    {
        std::unique_lock<lock_t> lock(lock_);
        if (closed_) return ;
        DebugPrint(dbg_mask_ & dbg_channel, "[id=%ld] Channel Closed. size=%d", this->getId(), (int)Size());
        closed_ = true;
		NotifyAll();
    }
private:
	bool push(T const& t)
	{
		if (useRingBuffer_)
			return q_.push(t);
		else {
			if (lq_.size() >= capacity_)
				return false;

			lq_.emplace_back(t);
			return true;
		}
	}
	bool pop(T & t) 
	{
		if (useRingBuffer_)
			return q_.pop(t);
		else {
			if (lq_.empty())
				return false;

			t = lq_.front();
			lq_.pop_front();
			return true;
		}
	}
	//�޷�����д�����ݣ�������ȴ�������task���ѻ�ʱ����
	bool WriteWaite(T& t,int timeout,std::unique_lock<lock_t>& lock)
	{
		//��ʱ���ѱ�־
		bool isTimeOut = false;
		TimerID timer;
		Entry entry(Processer::GetCurrentTask(),&t);
		//�ŵ�д�ȴ�����
		wq_.push(&entry);
		//��Ҫ��ʱ���������õȴ�
		if (timeout > 0)
		{
			timer = Processer::GetCurrentScheduler()->CreateMilliTimer(
				timeout,
				[&isTimeOut,&entry](void* userParam = nullptr){
				isTimeOut = true;
				entry.task_->state_ = TaskState::runnable;//��������״̬
				entry.task_->proc_->WakeupBySelfIO(entry.task_);//��task�������ж���
			});
		}
		//���Լ���Processer::runnableQueue_���Ƴ���������״̬Ϊblock
		entry.task_->proc_->SuspendBySelfIO(entry.task_);
		//�л�ǰһ��Ҫ�ͷ�������Ȼ����task��Զ�޷��õ���
		lock.unlock();
		lock.release();//��Ϊ�Ѿ��ͷ�������Ҫ�Ͽ���ϵ��������unlock

		//�л���ȥ
		Processer::StaticCoYield();

		//task�л�������Ҫô��ʱҪô����д��ɹ�
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
		
		return entry.success_;
	}
	//֪ͨ�ڵȴ�д��task
	bool WriteNotifyOne(ChanFunctor const& func)
	{
		Entry* entry = nullptr;
		//�Ƴ�һ��������д�����ϵ�task�����д����Ϊ���򷵻�false
		if (!wq_.pop(entry))
			return false;
		assert(entry);
		//д�����ݵ�buffer�������ı�����
		if (func)
		{
			func(*entry->value_);
			//��ʾ����д��ɹ�
			entry->success_ = true;
		}
		else
			entry->success_ = false;

		entry->task_->state_ = TaskState::runnable;//��������״̬
		//��task�������ж��У�������WriteWaite����
		entry->task_->proc_->WakeupBySelfIO(entry->task_);
		//����������WaitLoop������Processer���������̽��е���
		entry->task_->proc_->InterrupteWaitLoop();

		return true;
	}
	//�޷�������ȡ���ݣ�������ȴ�������task���ѻ�ʱ����
	bool ReadWaite(T& t, int timeout,std::unique_lock<lock_t>& lock)
	{
		//��ʱ���ѱ�־
		bool isTimeOut = false;
		TimerID timer;
		Entry entry(Processer::GetCurrentTask(), &t);
		//�ŵ����ȴ�����
		rq_.push(&entry);
		//��Ҫ��ʱ���������õȴ�
		if (timeout > 0)
		{
			timer = Processer::GetCurrentScheduler()->CreateMilliTimer(
				timeout,
				[&isTimeOut,&entry](void* userParam = nullptr){
				isTimeOut = true;
				entry.task_->state_ = TaskState::runnable;//��������״̬
				entry.task_->proc_->WakeupBySelfIO(entry.task_);//��task�������ж���
			});
		}
		//���Լ���Processer::runnableQueue_���Ƴ���������״̬Ϊblock
		entry.task_->proc_->SuspendBySelfIO(entry.task_);
		//�л�ǰһ��Ҫ�ͷ�������Ȼ����task��Զ�޷��õ���
		lock.unlock();
		lock.release();//��Ϊ�Ѿ��ͷ�������Ҫ�Ͽ���ϵ��������unlock

		//�л���ȥ
		Processer::StaticCoYield();

		//task�л�������Ҫô��ʱҪô���ݶ�ȡ�ɹ�
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
		
		return entry.success_;
	}
	//֪ͨ�ڵȴ�����task
	bool ReadNotifyOne(ChanFunctor const& func)
	{
		Entry* entry = nullptr;
		//�Ƴ�һ�������ڶ������ϵ�task�����д����Ϊ���򷵻�false
		if (!rq_.pop(entry))
			return false;
		assert(entry);
		//��ȡ���ݵ�buffer�������ı�����
		if (func)
		{
			func(*entry->value_);
			//��ʾ���ݶ�ȡ�ɹ�
			entry->success_ = true;
		}
		else
			entry->success_ = false;

		entry->task_->state_ = TaskState::runnable;//��������״̬
		//��task�������ж��У�������ReadWaite����
		entry->task_->proc_->WakeupBySelfIO(entry->task_);
		//����������WaitLoop������Processer���������̽��е���
		entry->task_->proc_->InterrupteWaitLoop();

		return true;
	}
	//֪ͨ�����ڵȴ�����д��task����
	void NotifyAll()
	{
		//�������������ڶ��ϵ�task�������Ƿ���ʧ��
		while (ReadNotifyOne(nullptr))
		{
		}
		//��������������д�ϵ�task�������Ƿ���ʧ��
		while (WriteNotifyOne(nullptr))
		{
		}
	}
};

} //namespace co
