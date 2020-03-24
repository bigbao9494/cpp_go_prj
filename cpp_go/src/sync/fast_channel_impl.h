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
		//被阻塞的task
		Task* task_;
		//task要读写的数据地址
		T* value_;
		//读写成功标志
		bool success_;
	};
    //typedef std::mutex lock_t;
	typedef LFLock lock_t;
    typedef FastSteadyClock::time_point time_point_t;
	typedef std::function<void(T&)> ChanFunctor;

	//q_和lq_的锁
    lock_t lock_;
	//buffer初始容量
    const std::size_t capacity_;
    bool closed_;
    uint64_t dbg_mask_;

    bool useRingBuffer_;
	//ringBuffer方式缓存
    RingBuffer<T> q_;
	//链表方式缓存
    std::list<T> lq_;

	//写失败后task阻塞队列
    WaitQueue1<Entry> wq_;
	//读失败后task阻塞队列
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

		//无buffer的情况直接唤醒rq_中等待的协程，成功后返回，失败表示rq_中无协程等待需要wq_.wait挂起自己等待被pop函数调用者唤醒
        if (!capacity_)
		{
			//直接把数据写给等待的读者
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
				//push数据后buffer中都只有1个数据，说明之前buffer为空，可能有pop的协程在等待读数据
				if (Size() == 1)
				{
					//缓存中的数据pop给等待中的读者
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

		//未能成功写入数据，阻塞自己等待被唤醒
		return WriteWaite(t,timeout, lock);
    }

    // read
    bool Pop(T & t, int timeout = INNER_UTIME_NO_TIMEOUT)
    {
        DebugPrint(dbg_channel, "[id=%ld] Pop ->", this->getId());
        std::unique_lock<lock_t> lock(lock_);
        if (closed_) return false;
		//有buffer
        if (capacity_ > 0) 
		{
			//从buffer中取数据
            if (pop(t)) 
			{
				//取成功说明buffer中有数据，判断取数据前buffer中是满状态，可能有人阻塞在wq_中，通知wq_唤醒它
                if (Size() == capacity_ - 1) 
				{
					//让wq_中的第1个协程来执行push(p)把它自己的数据写入，然唤醒它表示它的数据成功写入
					if (WriteNotifyOne([this](T& p) {push(p); }))
					{
                        DebugPrint(dbg_channel, "[id=%ld] Pop Notify size=%lu.", this->getId(), Size());
                    }
                }
                DebugPrint(dbg_channel, "[id=%ld] Pop complete unqueued.", this->getId());
				//成功取出数据返回
                return true;
            }
        } 
		else 
		{
			//无buffer，直接通知wq_唤醒.这时如果有人写在等待写入，则直接把数据写给t
            if (WriteNotifyOne([&t](T& p) {t = p;}))
			{
                DebugPrint(dbg_channel, "[id=%ld] Pop Notify ...", this->getId());
				//唤醒成功说明有人阻塞在wq_上，数据会被它写入给自己( t = *p)，返回成功
                return true;
            }
        }

		if (timeout == 0)
		{
            DebugPrint(dbg_channel, "[id=%ld] TryPop failed.", this->getId());
            return false;
        }

        DebugPrint(dbg_channel, "[id=%ld] Pop wait.", this->getId());

		//未能成功取到数据，阻塞自己等待被唤醒
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
	//无法立即写入数据，阻塞后等待被其它task唤醒或超时唤醒
	bool WriteWaite(T& t,int timeout,std::unique_lock<lock_t>& lock)
	{
		//超时唤醒标志
		bool isTimeOut = false;
		TimerID timer;
		Entry entry(Processer::GetCurrentTask(),&t);
		//放到写等待队列
		wq_.push(&entry);
		//需要超时而不是永久等待
		if (timeout > 0)
		{
			timer = Processer::GetCurrentScheduler()->CreateMilliTimer(
				timeout,
				[&isTimeOut,&entry](void* userParam = nullptr){
				isTimeOut = true;
				entry.task_->state_ = TaskState::runnable;//设置运行状态
				entry.task_->proc_->WakeupBySelfIO(entry.task_);//把task加入运行队列
			});
		}
		//把自己从Processer::runnableQueue_中移除，并设置状态为block
		entry.task_->proc_->SuspendBySelfIO(entry.task_);
		//切换前一定要释放锁，不然其它task永远无法得到锁
		lock.unlock();
		lock.release();//因为已经释放所以需要断开联系，避免多次unlock

		//切换出去
		Processer::StaticCoYield();

		//task切换回来，要么超时要么数据写入成功
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
		
		return entry.success_;
	}
	//通知在等待写的task
	bool WriteNotifyOne(ChanFunctor const& func)
	{
		Entry* entry = nullptr;
		//移除一个阻塞在写队列上的task，如果写队列为空则返回false
		if (!wq_.pop(entry))
			return false;
		assert(entry);
		//写入数据到buffer或读请求的变量中
		if (func)
		{
			func(*entry->value_);
			//表示数据写入成功
			entry->success_ = true;
		}
		else
			entry->success_ = false;

		entry->task_->state_ = TaskState::runnable;//设置运行状态
		//把task加入运行队列，让它从WriteWaite返回
		entry->task_->proc_->WakeupBySelfIO(entry->task_);
		//唤醒阻塞在WaitLoop函数的Processer，让它立刻进行调度
		entry->task_->proc_->InterrupteWaitLoop();

		return true;
	}
	//无法立即读取数据，阻塞后等待被其它task唤醒或超时唤醒
	bool ReadWaite(T& t, int timeout,std::unique_lock<lock_t>& lock)
	{
		//超时唤醒标志
		bool isTimeOut = false;
		TimerID timer;
		Entry entry(Processer::GetCurrentTask(), &t);
		//放到读等待队列
		rq_.push(&entry);
		//需要超时而不是永久等待
		if (timeout > 0)
		{
			timer = Processer::GetCurrentScheduler()->CreateMilliTimer(
				timeout,
				[&isTimeOut,&entry](void* userParam = nullptr){
				isTimeOut = true;
				entry.task_->state_ = TaskState::runnable;//设置运行状态
				entry.task_->proc_->WakeupBySelfIO(entry.task_);//把task加入运行队列
			});
		}
		//把自己从Processer::runnableQueue_中移除，并设置状态为block
		entry.task_->proc_->SuspendBySelfIO(entry.task_);
		//切换前一定要释放锁，不然其它task永远无法得到锁
		lock.unlock();
		lock.release();//因为已经释放所以需要断开联系，避免多次unlock

		//切换出去
		Processer::StaticCoYield();

		//task切换回来，要么超时要么数据读取成功
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
		
		return entry.success_;
	}
	//通知在等待读的task
	bool ReadNotifyOne(ChanFunctor const& func)
	{
		Entry* entry = nullptr;
		//移除一个阻塞在读队列上的task，如果写队列为空则返回false
		if (!rq_.pop(entry))
			return false;
		assert(entry);
		//读取数据到buffer或读请求的变量中
		if (func)
		{
			func(*entry->value_);
			//表示数据读取成功
			entry->success_ = true;
		}
		else
			entry->success_ = false;

		entry->task_->state_ = TaskState::runnable;//设置运行状态
		//把task加入运行队列，让它从ReadWaite返回
		entry->task_->proc_->WakeupBySelfIO(entry->task_);
		//唤醒阻塞在WaitLoop函数的Processer，让它立刻进行调度
		entry->task_->proc_->InterrupteWaitLoop();

		return true;
	}
	//通知所有在等待读和写的task返回
	void NotifyAll()
	{
		//唤醒所有阻塞在读上的task，让它们返回失败
		while (ReadNotifyOne(nullptr))
		{
		}
		//唤醒所有阻塞在写上的task，让它们返回失败
		while (WriteNotifyOne(nullptr))
		{
		}
	}
};

} //namespace co
