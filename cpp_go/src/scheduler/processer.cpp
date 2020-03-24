#include "processer.h"
#include "scheduler.h"
#include "../common/error.h"
#include "../common/clock.h"
#include <assert.h>
//#include "ref.h"
//#include "unistd.h"
#if defined(USE_SELECT)
#include "netio/inner_net/select_io_wait.h"
#endif
#if defined(USE_EPOLL)
#include "../netio/inner_net/epoll_io_wait.h"
#endif
#define TaskRefSuspendId(tk) tk->suspendId_

namespace co {

int Processer::s_check_ = 0;

Processer::Processer(Scheduler * scheduler, int id)
    : scheduler_(scheduler), id_(id)
{
	main_task_ = new Task(NULL, -1, nullptr);

#if defined(USE_SELECT)
	io_wait_ = new SelectIoWait;
#elif defined(USE_EPOLL)
	io_wait_ = new EpollIoWait;
#endif
}
Processer::~Processer()
{
	//还有其它资源未释放
	if (io_wait_)
		delete (InnerIoWait*)io_wait_;
	io_wait_ = nullptr;
	if (thread_)
		delete thread_;
	thread_ = nullptr;
}
Processer* & Processer::GetCurrentProcesser()
{
    static thread_local Processer *proc = nullptr;
    return proc;
}

Scheduler* Processer::GetCurrentScheduler()
{
    auto proc = GetCurrentProcesser();
    return proc ? proc->scheduler_ : nullptr;
}

void Processer::AddTask(Task *tk)
{
    DebugPrint(dbg_task | dbg_scheduler, "task(%s) add into proc(%u)(%p)", tk->DebugInfo(), id_, (void*)this);
    std::unique_lock<TaskQueue::lock_t> lock(newQueue_.LockRef());
    newQueue_.pushWithoutLock(tk);
    newQueue_.AssertLink();
}

void Processer::AddTask(SList<Task> & slist)
{
    DebugPrint(dbg_scheduler, "task(num=%d) add into proc(%u)", (int)slist.size(), id_);
    std::unique_lock<TaskQueue::lock_t> lock(newQueue_.LockRef());
    newQueue_.pushWithoutLock((slist));
    newQueue_.AssertLink();
}

void Processer::Stop()
{
	//不是第1个Processer需要等待线程函数退出
	if (id_ > 0 && thread_->joinable())
		thread_->join();
	else
	{
		//主线程需要等待主线程退出Process函数
		while (active_)
			usleep(1000);
	}
}
void Processer::Process()
{
	GetCurrentProcesser() = this;
	//在每个线程的线程函数中进行初始化
	io_wait_->InitModule();

	while (!scheduler_->IsStop())
	{
		/*
		引用运行队列中的第1个task，注意不是pop，这个task仍然在队列的第1个位置
		如果这个task被执行后的状态仍然是runnable则它在队列中的位置保持不变
		下次再front仍然会是这个task，task执行后状态为TaskState::done后才会被移除
		runnableQueue_队列，每次执行这里说明runnableQueue_中的task都已经执行了一次
		这里又重头开始执行所有的task
		*/
		runnableQueue_.front(runningTask_);
		if (!runningTask_)
		{
			//runnableQueue_中没有数据，把newQueue_中的移动过来
			if (AddNewTasks())
				runnableQueue_.front(runningTask_);
			#if 0
			if (!runningTask_)
			{
				//newQueue_中也没有数据则会在条件变量上等待
				WaitCondition();
				AddNewTasks();
				continue;
			}
			#else
			//如果开启USE_TASK_GC在gcQueue_中可能有分部task不会被及时释放，WaitCondition中才会调用GC()做最后释放
			if (!runningTask_)
			{
				//所有的task都处于block状态: 处理epoll_wait
				//这时表示系统可以进入短暂阻塞状态，因为没有task需要执行
				//epoll_wait超时时间timer_manager_中的最近一个sleep时间
				int ep_count = io_wait_->WaitLoop(timer_manager_.GetLastDuration());
				//处理timer
				timer_manager_.WakeupTimers();
				continue;
			}
			#endif
		}

		//////////////////////////////////////////////////////////////////////////
		//这个循环会把runnableQueue_中的所有task都执行一次才会退出
		while (runningTask_ && !scheduler_->IsStop())
		{
			runningTask_->state_ = TaskState::runnable;
			runningTask_->proc_ = this;
			#if ENABLE_DEBUGGER
			DebugPrint(dbg_switch, "enter task(%s)", runningTask_->DebugInfo());
			if (Listener::GetTaskListener())
				Listener::GetTaskListener()->onSwapIn(runningTask_->id_);
			#endif
			++switchCount_;
			//每次都是由主协程切换到其它协程(星行切换方式)
			runningTask_->SwapIn();
			if (runningTask_->state_ == TaskState::block)
			{
				//task是通过调用Suspend相关函数变成block状态的，Suspend中已经把它从runnableQueue_队列移除了
				//同时nextTask_会保存它的下一个task
				std::unique_lock<TaskQueue::lock_t> lock(runnableQueue_.LockRef());
				runningTask_ = nextTask_;
				nextTask_ = nullptr;
			}
			else if (runningTask_->state_ == TaskState::runnable)
			{
				//assert(!nextTask_);
				std::unique_lock<TaskQueue::lock_t> lock(runnableQueue_.LockRef());
				//当前task的下一个
				runningTask_ = (Task*)runningTask_->next;
			}
			else if (runningTask_->state_ == TaskState::done)
			{
				//直接从当前task位置得到下一个task
				runnableQueue_.next(runningTask_, nextTask_);

				DebugPrint(dbg_task, "task(%s) done.", runningTask_->DebugInfo());
				//当前task从runnableQueue_队列中移除
				runnableQueue_.erase(runningTask_);
				//减小scheduler中的task计数
				scheduler_->DecreaseTaskCounter();
				#ifdef USE_TASK_GC
				//缓存死亡的task再释放
				if (gcQueue_.size() > 16)
					GC();
				//会增加一次对runningTask_的计数
				gcQueue_.push(runningTask_);
				std::unique_lock<TaskQueue::lock_t> lock(runnableQueue_.LockRef());
				//准备执行下一个task
				runningTask_ = nextTask_;
				nextTask_ = nullptr;
				#else
					#ifdef USE_TASK_CACHE
					std::unique_lock<TaskQueue::lock_t> lock(runnableQueue_.LockRef());
					//缓存而不删除这个task
					Scheduler::getInstance().Add2FreeTaskList(runningTask_);
					//准备执行下一个task
					runningTask_ = nextTask_;
					nextTask_ = nullptr;
					#else
					std::unique_lock<TaskQueue::lock_t> lock(runnableQueue_.LockRef());
					//直接释放结束的task
					delete runningTask_;
					//准备执行下一个task
					runningTask_ = nextTask_;
					nextTask_ = nullptr;
					#endif
				#endif
			}
		}

		//执行到这里runningTask_一定为空,说明runnableQueue_已经遍历完一次
		//把runningTask_中的task执行完一遍后就检查有没有新task产生
		if (!scheduler_->IsStop())
		{
			//添加新task到runnableQueue_
			AddNewTasks();
		}
	}

	//线程结束时删除task
	ReleaseTask();
	//表示退出线程函数
	active_ = false;

	PRINT("%s %d exit\n", __FUNCTION__, id_);
}
Task* Processer::GetCurrentTask()
{
    auto proc = GetCurrentProcesser();
    return proc ? proc->runningTask_ : nullptr;
}

bool Processer::IsCoroutine()
{
    return !!GetCurrentTask();
}

std::size_t Processer::RunnableSize()
{
	//size的调用会使用线程锁
    return runnableQueue_.size() + newQueue_.size();
}
std::size_t Processer::IOWaiteSize()
{
	return io_wait_->GetFDMapSize();
}
void Processer::InterrupteWaitLoop()
{ 
	io_wait_->InterrupteWaitLoop(); 
}
#ifdef USE_TASK_GC
void Processer::GC()
{
    auto list = gcQueue_.pop_all();
    //for (Task & tk : list) {
    //    tk.DecrementRef();
    //}
    list.clear();
	//printf("task_count: %d\n", scheduler_->TaskCount());
}
#endif
bool Processer::AddNewTasks()
{
	if (newQueue_.empty())
		return false;

    runnableQueue_.push(newQueue_.pop_all());
    newQueue_.AssertLink();
    return true;
}

bool Processer::IsBlocking()
{
	//markSwitch_ != switchCount_说明task执行很快，未阻塞在task上
	//markSwitch_==0是默认值，第1次调用没有阻塞状态
	//没有正在执行的task表明完全空闲不应该block
    if (!markSwitch_ || markSwitch_ != switchCount_ || !runningTask_)
		return false;
	//如果switchCount_未及时变化，可能有task耗时
	//计算这个task占用Processer是否超时：当前时间 > 开始时间 + task超时时间
	//DispatcherThread对IsBlocking和Mark的调用来判定Processer是否处于block状态
	//是有可能对一个无task的Processer判定成block的，DispatcherThread对IsBlocking和Mark
	//的调用顺序会决定是否造成这种情况
    return NowMicrosecond() > markTick_ + CoroutineOptions::getInstance().cycle_timeout_us;
}

void Processer::Mark()
{
	/*
	如果Processer的task执行很快，switchCount_会不停增加，同时runningTask_不为空
	DispatcherThread对Mark的调用间隔(上次Mark调用和这次Mark调用)的这段时间，switchCount_可能已经增加了
	如果增加了说明task未长时间占用Processer，更新标志和记录时间
	如果switchCount_未及时变化，那markSwitch_ == switchCount_并且markTick_无法更新
	*/
    if (runningTask_ && markSwitch_ != switchCount_) 
	{
		//更新标志
        markSwitch_ = switchCount_;
		//记录下正在执行的task的开始时间，这是在DispatcherThread中的相对时间
        markTick_ = NowMicrosecond();
    }
}

int64_t Processer::NowMicrosecond()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(FastSteadyClock::now().time_since_epoch()).count();
}

SList<Task> Processer::Steal(std::size_t n)
{
    if (n > 0) {
        newQueue_.AssertLink();
		//优先从本processer的newQueue_尾部取出n个，这部分是还未被放入runnableQueue_的
        auto slist = newQueue_.pop_back((uint32_t)n);
        newQueue_.AssertLink();
		//取够了直接返回
        if (slist.size() >= n)
            return slist;

		/*
		必须保存把runningTask_和nextTask_从它们所在的那个Processor的runnableQueue_移出来，才能保证不会被runnableQueue_.pop_backWithoutLock偷走
		因为调用Steal的是DispatcherThread线程，这个时候的runningTask_正在被Processor执行会因为IO或SLEEP时状态变成TaskState::block，同时
		会在退出Steal时它会被放到InnerIoWait::FD_MAP或TimerManager中，如果它被偷走就导致task处于2个Processor中
		*/
        std::unique_lock<TaskQueue::lock_t> lock(runnableQueue_.LockRef());
        bool pushRunningTask = false, pushNextTask = false;
		if (runningTask_) //把正在运行的task先从runnableQueue_移出来
			pushRunningTask = runnableQueue_.eraseWithoutLock(runningTask_, false);// || slist.erase(runningTask_, newQueue_.check_);
		if (nextTask_) //把nextTask_先从runnableQueue_移出来，注意nextTask_可能为空
			pushNextTask = runnableQueue_.eraseWithoutLock(nextTask_, false);// || slist.erase(nextTask_, newQueue_.check_);

		//从runnableQueue_的尾部取task
        auto slist2 = runnableQueue_.pop_backWithoutLock((uint32_t)(n - slist.size()));
		//再把runningTask_和nextTask_放回去，这样避免它们两个被偷走
        if (pushRunningTask)
            runnableQueue_.pushWithoutLock(runningTask_);
        if (pushNextTask)
            runnableQueue_.pushWithoutLock(nextTask_);
        lock.unlock();

		//合并两次的task
        slist2.append(std::move(slist));
        if (!slist2.empty())
            DebugPrint(dbg_scheduler, "Proc(%d).Stealed = %d", id_, (int)slist2.size());
        return slist2;
    } 
	else 
	{
		// steal all
		newQueue_.AssertLink();
		auto slist = newQueue_.pop_all();
		newQueue_.AssertLink();

		/*
		必须保存把runningTask_和nextTask_从它们所在的那个Processor的runnableQueue_移出来，才能保证不会被runnableQueue_.pop_backWithoutLock偷走
		因为调用Steal的是DispatcherThread线程，这个时候的runningTask_正在被Processor执行会因为IO或SLEEP时状态变成TaskState::block，同时
		会在退出Steal时它会被放到InnerIoWait::FD_MAP或TimerManager中，如果它被偷走就导致task处于2个Processor中
		*/
		std::unique_lock<TaskQueue::lock_t> lock(runnableQueue_.LockRef());
		bool pushRunningTask = false, pushNextTask = false;
		if (runningTask_) //把正在运行的task先从runnableQueue_移出来
			pushRunningTask = runnableQueue_.eraseWithoutLock(runningTask_, false);// || slist.erase(runningTask_, newQueue_.check_);
		if (nextTask_) //把nextTask_先从runnableQueue_移出来，注意nextTask_可能为空
			pushNextTask = runnableQueue_.eraseWithoutLock(nextTask_, false);// || slist.erase(nextTask_, newQueue_.check_);

		auto slist2 = runnableQueue_.pop_allWithoutLock();
		//再把runningTask_和nextTask_放回去，这样避免它们两个被偷走
		if (pushRunningTask)
			runnableQueue_.pushWithoutLock(runningTask_);
		if (pushNextTask)
			runnableQueue_.pushWithoutLock(nextTask_);
		lock.unlock();

		//合并两次的task
		slist2.append(std::move(slist));
		if (!slist2.empty())
			DebugPrint(dbg_scheduler, "Proc(%d).Stealed = %d", id_, (int)slist2.size());
		return slist2;
    }
}
bool Processer::SuspendBySelfIO(Task* tk)
{
	assert(tk == runningTask_);
	assert(tk->state_ == TaskState::runnable);
	tk->state_ = TaskState::block;
	uint64_t id = ++TaskRefSuspendId(tk);

	std::unique_lock<TaskQueue::lock_t> lock(runnableQueue_.LockRef());
	assert(tk == runningTask_);
	runnableQueue_.nextWithoutLock(runningTask_, nextTask_);
	//不修改引用计数
	bool ret = runnableQueue_.eraseWithoutLock(runningTask_, false);
	assert(ret);

	return true;
}
bool Processer::WakeupBySelfIO(Task* tk)
{
	//WakeupBySelfIO的调用只会在tk自己的Processor中
	//它所在的那个Processor在执行io_wait_->WaitLoop或timer_manager_.WakeupTimers调用此函数
	tk->state_ = TaskState::runnable;
	size_t sizeAfterPush = runnableQueue_.push(tk);

	return true;
}
void Processer::sleep(int milliseconds)
{
	Task *tk = Processer::GetCurrentTask();
	//DebugPrint(dbg_hook, "task(%s) Hook Sleep(dwMilliseconds=%lu).", tk->DebugInfo(), dwMilliseconds);
	//在非协程中休眠，直接休眠线程
	if (!tk) {
		usleep(milliseconds * 1000);
		return;
	}

#if 0
	//用timer来实现
	if (milliseconds > 0)
		Processer::Suspend(std::chrono::milliseconds(milliseconds));
#else
	//用sleeper来实现
	if (milliseconds > 0)
	{
		assert(tk->proc_);
		//先把task从运行队列中移除,里面没有修改引用计数,因为马上会被timer_manager_使用
		tk->proc_->SuspendBySelfIO(tk);
		tk->proc_->timer_manager_.AddSleeper(std::chrono::milliseconds(milliseconds),tk);
	}
#endif

	Processer::StaticCoYield();
}
InnerIoWait* Processer::GetIoWait()
{
	auto proc = GetCurrentProcesser();
	return proc->io_wait_;
}
TimerID Processer::AddMilliTimer(int duration, TimerCallBack const& cb, void* userParam)
{
	//使用milliseconds单位
	TimerID timer = timer_manager_.AddTimer(std::chrono::milliseconds(duration),cb, userParam);
	//创建timer的时候不需要保护，创建只会在一个线程操作
	timer->processer_ = this;
	return timer;
}
bool Processer::StopTimer(TimerID& id)
{
	return timer_manager_.StopTimer(id);
}
void Processer::ReleaseTask()
{
	Task* task;
	while (!runnableQueue_.empty())
	{
		task = runnableQueue_.pop();
		delete task;
	}
	while (!newQueue_.empty())
	{
		task = newQueue_.pop();
		delete task;
	}
#if defined(USE_TASK_GC)
	while (!gcQueue_.empty())
	{
		task = gcQueue_.pop();
		delete task;
	}
#endif
	//删除主task
	delete main_task_;
}

} //namespace co

