#pragma once
#include "../common/config.h"
#include "../common/clock.h"
#include "../task/task.h"
#include "../common/ts_queue.h"
#include "../timer/timer.h"

#if ENABLE_DEBUGGER
#include "../debug/listener.h"
#endif
#include <condition_variable>
#include <mutex>
#include <atomic>

namespace co {

class Scheduler;
class InnerIoWait;

// 协程执行器
// 对应一个线程, 负责本线程的协程调度, 非线程安全.
class Processer
{
    friend class Scheduler;
public:
	void SetThread(std::thread* thd) { thread_ = thd; }
    ALWAYS_INLINE int Id() { return id_; }
    static Processer* & GetCurrentProcesser();
    static Scheduler* GetCurrentScheduler();
    inline Scheduler* GetScheduler() { return scheduler_; }
    // 获取当前正在执行的协程
    static Task* GetCurrentTask();
    // 是否在协程中
    static bool IsCoroutine();
    // 协程切出
    ALWAYS_INLINE static void StaticCoYield();
	//休眠协程
	static void sleep(int milliseconds);
	static InnerIoWait* GetIoWait();
	//把因调用SuspendBySelfIO从runnableQueue_队列的移除task重新添加进runnableQueue_
	bool WakeupBySelfIO(Task* tk);
	//因为IO把自己从runnableQueue_中移除，这个task变成一个游离的task
	//调用者负责保存这个task，确保适当时间会重新添加到runnableQueue_中
	bool SuspendBySelfIO(Task* tk);
	//添加毫秒级的timer
	TimerID AddMilliTimer(int duration, TimerCallBack const& cb, void* userParam = nullptr);
	//停止timer，如果是已经是被唤醒了的timer返回false，未被唤醒的返回true
	bool StopTimer(TimerID& id);
	//唤醒线程避免runnableQueue_中没有task时在WaitLoop中等待固定超时时间
	void InterrupteWaitLoop();
private:
    explicit Processer(Scheduler * scheduler, int id);
	~Processer();
    // 待执行的协程数量，会使用runnableQueue_的线程锁，应避免频繁调用
    // 暂兼用于负载指数
    std::size_t RunnableSize();
    //得到在io_waite的个数
    std::size_t IOWaiteSize();
    ALWAYS_INLINE void CoYield();
    // 新创建、阻塞后触发的协程add进来
    void AddTask(Task *tk);
    // 调度
    void Process();
    // 偷来的协程add进来
    void AddTask(SList<Task> & slist);
	//退出Processor，会等待线程函数退出
    void Stop();
    // 单个协程执行时长超过预设值, 则判定为阻塞状态
    // 阻塞状态不再加入新的协程, 并由调度线程steal走所有协程(正在执行的除外)
    bool IsBlocking();
    // 偷协程
    SList<Task> Steal(std::size_t n);
#ifdef USE_TASK_GC
    void GC();
#endif
	//把newQueue_队列中的task添加到runnableQueue_队列尾
    bool AddNewTasks();
    //调度线程打标记, 用于检测阻塞,仅在DispatcherThread中调用
    void Mark();
	//steady_clock时间，微妙
    int64_t NowMicrosecond();
	//删除runnableQueue_,newQueue_,gcQueue_中的task
	void ReleaseTask();

private:
	Scheduler * scheduler_;
	//Processer对应的线程
	std::thread* thread_ = nullptr;
	// 线程ID
	int id_;
	//激活态,非激活的P仅仅是不能接受新的协程加入, 仍然可以强行AddTask并正常处理.
	//只会在DispatcherThread中读写它
	volatile bool active_ = true;
	// 当前正在运行的协程
	Task* runningTask_{ nullptr };
	Task* nextTask_{ nullptr };
	//每个物理线程对应的主协程，作用相当于st的idle_thread.传递stack_size=-1表示不创建私有堆栈空间
	//它代表物理线程的栈空间，切换到其它协程时需要用它来保存物理线程的栈信息
	Task* main_task_ = nullptr;

	/*
	1、为了防止出现task中递归创建task的情况，这种情况就会一直执行新的task而无法执行队列中的其它task
	比如：f1(){go f1};
	2、决定在执行完一次完整调度后能够调用AddNewTasks的次数，防止task1->创建task2->task3...taskN，这种情况发生
	这种错误使用会导致runnableQueue_队列中其它task永远得到不执行
	3、因为新创建的task是放在newQueue_中而不是runnableQueue_中的，只有通过AddNewTasks才会把新的task添加到
	runnableQueue_，所以这里要控制第2个循环中对AddNewTasks的调用次数，如果新task一直往runnableQueue_
	中添加，那么runnableQueue_中的其它task是无法被执行的
	*/
	// 每轮调度只加有限次数新协程, 防止新协程创建新协程产生死循环
	//int addNewQuota_ = 0;

	// 当前正在运行的协程本次调度开始的时间戳(Dispatch线程专用)
	volatile int64_t markTick_ = 0;
	volatile uint64_t markSwitch_ = 0;
	//协程调度次数
	volatile uint64_t switchCount_ = 0;
	//协程队列
	typedef TSQueue<Task, true> TaskQueue;
	TaskQueue runnableQueue_;
	/*
	结束了的Task会被放到这个队列中，队列中Task个数达到一定数量时就会
	调用GC()函数来删除这些Task
	*/
#if defined(USE_TASK_GC)
	TSQueue<Task, false> gcQueue_;
#endif
	/*
	新创建的task和steal的task都是先放到这个队列中，会在process()中选择合适时间把它移到runnableQueue_中去
	*/
	TaskQueue newQueue_;
	static int s_check_;
	//原生网络支持每个线程独立的IoWait
	InnerIoWait* io_wait_ = nullptr;
	//每个线程独立的超时管理对象，Processer自己的task中的timer和sleep自己处理
	TimerManager timer_manager_;
};

ALWAYS_INLINE void Processer::StaticCoYield()
{
    auto proc = GetCurrentProcesser();
    if (proc) proc->CoYield();
}

ALWAYS_INLINE void Processer::CoYield()
{
    Task *tk = GetCurrentTask();
    assert(tk);

    ++tk->yieldCount_;

#if ENABLE_DEBUGGER
    DebugPrint(dbg_yield, "yield task(%s) state = %s", tk->DebugInfo(), GetTaskStateName(tk->state_));
    if (Listener::GetTaskListener())
        Listener::GetTaskListener()->onSwapOut(tk->id_);
#endif

    tk->SwapOut();
}


} //namespace co