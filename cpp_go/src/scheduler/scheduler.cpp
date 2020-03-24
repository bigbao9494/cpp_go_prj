#include "scheduler.h"
#include "../common/error.h"
#include "../common/clock.h"
//#include "ref.h"
#include "../netio/inner_net/inner_io_wait.h"
#include <stdio.h>
#include <system_error>
//#include <unistd.h>
#include <time.h>
#include <thread>

namespace co
{

inline atomic_t<uint32_t> & GetTaskIdFactory()
{
    static atomic_t<uint32_t> factory;
    return factory;
}

std::mutex& ExitListMtx()
{
    static std::mutex mtx;
    return mtx;
}
std::vector<std::function<void()>>* ExitList()
{
    static std::vector<std::function<void()>> *vec = new std::vector<std::function<void()>>;
    return vec;
}

static void onExit(void) {
    auto vec = ExitList();
    for (auto fn : *vec) {
        fn();
    }
    vec->clear();
//    return 0;
}

static int InitOnExit() {
    atexit(&onExit);
    return 0;
}

bool& Scheduler::IsExiting() {
    static bool exiting = false;
    return exiting;
}

Scheduler* Scheduler::Create()
{
    static int ignore = InitOnExit();
    (void)ignore;

	/*
	创建的调度器放在全局vector中vector<std::function<void()>>
	这个vector是类型std::function<void()>，这样在全局函数onExit
	中遍历vector来执行其中的std::function来删除每个Scheduler
	这样的设计感觉就是为了套c11的lamda表达式，完全可以简化设计
	直接在vector中保存Scheduler*，不需要多余的lamda开销
	*/
    Scheduler* sched = new Scheduler;
    std::unique_lock<std::mutex> lock(ExitListMtx());
    auto vec = ExitList();
    vec->push_back([=] { delete sched; });
    return sched;
}

Scheduler::Scheduler()
{
	InnerIoWait::inner_io_init();
	//默认Processer使用的主线程并没有产生新线程,它的thread_==nullptr
    processers_.push_back(new Processer(this, 0));
}

Scheduler::~Scheduler()
{
    IsExiting() = true;
    Stop();
	while (!processers_.empty())
	{
		delete processers_.front();
		processers_.pop_front();
	}

	//释放cache的task
#if defined(USE_TASK_CACHE)
	std::lock_guard<LFLock> locker(lock_free_task_map_);
	//找到stack_size_>=stack_size的空闲task
	for(auto itr = free_task_map_.begin(); itr != free_task_map_.end();)
	{
		Task* task = itr->second;
		delete task;
		//map中删除这个task(itr++加1后返回加1前的值itr不会失效)
		free_task_map_.erase(itr++);
	}
#endif
}

void Scheduler::CreateTask(TaskF const& fn, TaskOpt const& opt)
{
	Task* tk = nullptr;
#ifdef USE_TASK_CACHE
	tk = GetFreeTaskList(fn, opt.stack_size_ ? opt.stack_size_ : CoroutineOptions::getInstance().stack_size);
#endif
	//空闲中没有合适的，重新生成一个
	if (!tk)
	{
		tk = new Task(fn, opt.stack_size_ ? opt.stack_size_ : CoroutineOptions::getInstance().stack_size);
		tk->id_ = ++GetTaskIdFactory();
	}
    ++taskCount_;

    DebugPrint(dbg_task, "task created in scheduler(%p).",(void*)this);
#if ENABLE_DEBUGGER
    if (Listener::GetTaskListener()) {
        Listener::GetTaskListener()->onCreated(tk->id_);
    }
#endif

	//调用Process::AddTask会把Task添加到TSQueue
    AddTask(tk);
}
void Scheduler::CreateTask(TaskF1 const& fn, TaskOpt const& opt)
{
	Task* tk = nullptr;
#ifdef USE_TASK_CACHE
	tk = GetFreeTaskList(fn, opt.stack_size_ ? opt.stack_size_ : CoroutineOptions::getInstance().stack_size,opt.user_param_);
#endif
	//空闲中没有合适的，重新生成一个
	if (!tk)
	{
		tk = new Task(fn, opt.stack_size_ ? opt.stack_size_ : CoroutineOptions::getInstance().stack_size, opt.user_param_);
		tk->id_ = ++GetTaskIdFactory();
	}
	++taskCount_;

	DebugPrint(dbg_task, "task created in scheduler(%p).", (void*)this);
#if ENABLE_DEBUGGER
	if (Listener::GetTaskListener()) {
		Listener::GetTaskListener()->onCreated(tk->id_);
	}
#endif

	//调用Process::AddTask会把Task添加到TSQueue中
	AddTask(tk);
}
#ifdef USE_TASK_CACHE
Task* Scheduler::GetFreeTaskList(TaskF const& fn, int stack_size)
{
	std::lock_guard<LFLock> locker(lock_free_task_map_);
	std::multimap<int, Task*>::iterator itr = free_task_map_.lower_bound(stack_size);
	//找到stack_size_>=stack_size的空闲task
	if (itr != free_task_map_.end())
	{
		Task* task = itr->second;
		free_task_map_.erase(itr);
		//需要重新初始化
		task->ReInit(fn);
		return task;
	}
	return nullptr;
}
Task* Scheduler::GetFreeTaskList(TaskF1 const& fn,int stack_size, void* userParam)
{
	std::lock_guard<LFLock> locker(lock_free_task_map_);
	std::multimap<int, Task*>::iterator itr = free_task_map_.lower_bound(stack_size);
	//找到stack_size_>=stack_size的空闲task
	if (itr != free_task_map_.end())
	{
		Task* task = itr->second;
		free_task_map_.erase(itr);
		//需要重新初始化
		task->ReInit(fn,userParam);
		return task;
	}
	return nullptr;
}
#endif

bool Scheduler::IsCoroutine()
{
    return !!Processer::GetCurrentTask();
}

bool Scheduler::IsEmpty()
{
    return taskCount_ == 0;
}

void Scheduler::Start(int minThreadNumber, int maxThreadNumber)
{
    if (!started_.try_lock())
        throw std::logic_error("libgo repeated call Scheduler::Start");

    if (minThreadNumber < 1)
       minThreadNumber = std::thread::hardware_concurrency();

    if (maxThreadNumber == 0 || maxThreadNumber < minThreadNumber)
        maxThreadNumber = minThreadNumber;

    minThreadNumber_ = minThreadNumber;
    maxThreadNumber_ = maxThreadNumber;

    auto mainProc = processers_[0];
	threadPhyInfo_.AddThreadTid(0);//主线程总是第1个，必须在其它线程产生前调用
    for (int i = 0; i < minThreadNumber_ - 1; i++) {
        NewProcessThread();
    }
	//默认平均一下task
	AvgTotalTasks();

    // 调度线程
    if (maxThreadNumber_ > 1) {
        DebugPrint(dbg_scheduler, "---> Create DispatcherThread");
        std::thread t([this]{
                DebugPrint(dbg_thread, "Start dispatcher(sched=%p) thread id: %lu", (void*)this, NativeThreadID());
                this->DispatcherThread();
                });
        dispatchThread_.swap(t);
    } else {
        DebugPrint(dbg_scheduler, "---> No DispatcherThread");
    }

    std::thread(FastSteadyClock::ThreadRun).detach();

    DebugPrint(dbg_scheduler, "Scheduler::Start minThreadNumber_=%d, maxThreadNumber_=%d", minThreadNumber_, maxThreadNumber_);
    mainProc->Process();
}
void Scheduler::goStart(int minThreadNumber, int maxThreadNumber)
{
    std::thread([=]{ this->Start(minThreadNumber, maxThreadNumber); }).detach();
}
void Scheduler::Stop()
{
    std::unique_lock<std::mutex> lock(stopMtx_);

    if (stop_) return;
    stop_ = true;

    if (dispatchThread_.joinable())
        dispatchThread_.join();
    size_t n = processers_.size();
    for (size_t i = 0; i < n; ++i) {
        auto p = processers_[i];
        if (p)
            p->Stop();
    }
}

void Scheduler::NewProcessThread()
{
    auto p = new Processer(this,(int)processers_.size());
    DebugPrint(dbg_scheduler, "---> Create Processer(%d)", p->id_);
    std::thread* pthread = new std::thread([this, p]{
            DebugPrint(dbg_thread, "Start process(sched=%p) thread id: %lu", (void*)this, NativeThreadID());
#if defined(LIBGO_SYS_Unix)
			//让ThreadsPhyInfo知道新线程产生了
			this->threadPhyInfo_.AddThreadTid(processers_.size());
#endif
            p->Process();
            });
#if defined(LIBGO_SYS_Windows)
	//让ThreadsPhyInfo知道新线程产生了
	this->threadPhyInfo_.AddThreadTid((int)processers_.size(), pthread);
#endif
	//保存线程指针
	p->SetThread(pthread);
    //t.detach(); //不能detach，会造成ThreadsPhyInfo中无效
    processers_.push_back(p);
}
#define PrintProcesserTaskCounts(type,cacheTaskCounts,processers) \
    do { \
        if (UNLIKELY(::co::CoroutineOptions::getInstance().debug & (type))) \
		{ \
			PRINT("Processer tasks: "); \
			for (int i = 0; i < processers.size(); i++) \
			{ \
				PRINT("%d,", cacheTaskCounts[i]); \
			} \
			PRINT("\n"); \
        } \
    } while(0)
#define PrintProcesserTaskCounts1(type,processers) \
    do { \
        if (UNLIKELY(::co::CoroutineOptions::getInstance().debug & (type))) \
		{ \
			PRINT("Processer tasks: "); \
			for (int i = 0; i < processers.size(); i++) \
			{ \
				PRINT("%d,", processers[i]->RunnableSize()); \
			} \
			PRINT("\n"); \
        } \
    } while(0)
#if 1
void Scheduler::DispatcherThread()
{
	CoroutineOptions::getInstance().dispatcher_thread_cycle_us = 1000 * 1000;//test 1秒
	while (!stop_)
	{
		threadPhyInfo_.StartTrack();
		//用condition_variable降低cpu使用率
		std::this_thread::sleep_for(std::chrono::microseconds(CoroutineOptions::getInstance().dispatcher_thread_cycle_us));
		//PrintProcesserTaskCounts1(dbg_scheduler, processers_);
		if (threadPhyInfo_.StopTrack())//返回true表示需要调度
		{
			//for (int i = 0; i < processers_.size(); i++)
			//	PRINT("----------thead %d,%.4f\n", i, threadPhyInfo_[i]);
			//全局平均
			AvgTotalTasks();
		}
		//PRINT("\n\n");
	}
	PRINT("%s exit\n", __FUNCTION__);
}
#else
//测试故意移动TASK
void Scheduler::DispatcherThread()
{
	CoroutineOptions::getInstance().dispatcher_thread_cycle_us = 1000 * 1000 * 2;//test 1秒
	while (!stop_)
	{
		//用condition_variable降低cpu使用率
		std::this_thread::sleep_for(std::chrono::microseconds(CoroutineOptions::getInstance().dispatcher_thread_cycle_us));
		//全局平均
		AvgTotalTasks();
	}
	PRINT("%s exit\n", __FUNCTION__);
}
#endif

void Scheduler::AddTask(Task* tk)
{
    DebugPrint(dbg_scheduler, "Add task(%s) to runnable list.", tk->DebugInfo());
	//如果task指定了processer则使用这个processer来执行
    auto proc = tk->proc_;
    if (proc && proc->active_) {
        proc->AddTask(tk);
        return ;
    }

//    proc = Processer::GetCurrentProcesser();
//	//task里创建的task放到当前task所在的Processer来执行
//    if (proc && proc->active_ && proc->GetScheduler() == this)
//	{
//        proc->AddTask(tk);
//        return ;
//    }

	//在调用Start前pcount始终为1
    std::size_t pcount = processers_.size();
	uint32_t idx = lastActive_;
	//这里的目的是想均匀分配给每个processer
	//但是只会一种情况执行到这里：co_sched.Start(n)前创建的协程
	//co_sched.Start(n)前pcount始终为1，所以这里有问题,调用AvgTotalTasks来处理
    for (int i = 0; i < pcount; ++i, ++idx)
	{
        idx = idx % pcount;
        proc = processers_[idx];
        if (proc && proc->active_)
            break;
    }
	//保存最后一个添加了task的processer索引
	lastActive_ = idx + 1;

    proc->AddTask(tk);
}

void Scheduler::AvgTotalTasks()
{
	//只有一个线程不需要处理
	if (processers_.size() < 2)
		return;
	//缓存task数量，避免频繁调用RunnableSize
	std::vector<std::size_t> cacheTaskCounts(processers_.size());
	PRINT("AvgTotalTasks\n");
	//总task个数
	std::size_t total = 0;
	for (int i = 0; i < processers_.size(); i++)
	{
		cacheTaskCounts[i] = processers_[i]->RunnableSize();
		total += cacheTaskCounts[i];
		PRINT("before: %d,%lu,%lu\n",i, cacheTaskCounts[i],processers_[i]->IOWaiteSize());
	}

	std::size_t avg = total / processers_.size();
#if 0
	//平均数量都<线程数，也不处理
	if (avg < processers_.size())
		return;
#else
	if (avg <= 0)
		return;
#endif

	SList<Task> tasks;
	for (int i = 0; i < processers_.size(); i++)
	{
		//需要减少task
		if (avg < cacheTaskCounts[i])
		{
			auto tmp = processers_[i]->Steal(cacheTaskCounts[i] - avg);
			//这个processer多出来的task移动到tasks
			tasks.append(std::move(tmp));
			printf("steal %d from %d\n", tasks.count_,i);
		}
	}
	for (int i = 0; i < processers_.size(); i++)
	{
		//需要增加task
		if (avg > cacheTaskCounts[i])
		{
			SList<Task> in = tasks.cut(avg - cacheTaskCounts[i]);
			if (in.empty())
				break;
			printf("move %d to %d\n", in.count_, i);
			//task移动到processer中
			processers_[i]->AddTask((in));
		}
	}
	//平均分配时可能未除净，剩下的给主线程
	printf("left %d to 0\n", tasks.count_);
	if (!tasks.empty())
		processers_[0]->AddTask((tasks));

	for (int i = 0; i < processers_.size(); i++)
	{
		PRINT("after: %d,%lu,%lu\n", i, processers_[i]->RunnableSize(),processers_[i]->IOWaiteSize());
	}
}
uint32_t Scheduler::TaskCount()
{
    return taskCount_;
}

uint32_t Scheduler::GetCurrentTaskID()
{
    Task* tk = Processer::GetCurrentTask();
    return tk ? tk->id_ : 0;
}
int Scheduler::GetCurrentProcesserID()
{
	auto proc = Processer::GetCurrentProcesser();
	return proc ? proc->Id() : -1;
}

uint64_t Scheduler::GetCurrentTaskYieldCount()
{
    Task* tk = Processer::GetCurrentTask();
    return tk ? tk->yieldCount_ : 0;
}

void Scheduler::SetCurrentTaskDebugInfo(std::string const& info)
{
    Task* tk = Processer::GetCurrentTask();
    if (!tk) return ;
}
TimerID Scheduler::CreateMilliTimer(int duration, TimerCallBack const& cb, void* userParam)
{
	Processer* proc = Processer::GetCurrentProcesser();
	if (proc && proc->active_ && proc->GetScheduler() == this) 
	{
		return proc->AddMilliTimer(duration, cb, userParam);
	}

	std::size_t pcount = processers_.size();
	uint32_t idx = lastActive_;
	for (int i = 0; i < pcount; ++i, ++idx)
	{
		idx = idx % pcount;
		proc = processers_[idx];
		if (proc && proc->active_)
			break;
	}
	//保存最后一个添加了task的processer索引
	lastActive_ = idx;

	return proc->AddMilliTimer(duration, cb, userParam);
}
bool Scheduler::StopTimer(TimerID& id)
{
	//assert(id->processer_);
	//timer已经被释放了
	if (!id)
		return false;
	std::lock_guard<LFLock> locker(id->lock_);
	//timer已经无效
	if (!id->processer_)
		return false;
	return id->processer_->StopTimer(id);
}
} //namespace co
