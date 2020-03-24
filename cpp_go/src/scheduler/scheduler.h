#pragma once
#include "../common/config.h"
#include "../common/deque.h"
#include "../common/spinlock.h"
//#include "../common/timer.h"
#include "../task/task.h"
#include "../debug/listener.h"
#include "processer.h"
#include "ThreadPhyInfo.h"
#include <mutex>

namespace co {

struct TaskOpt
{
    bool affinity_ = false;
    int lineno_ = 0;
	int stack_size_ = 0;
    const char* file_ = nullptr;
	//传递用户的参数到协程函数中去
	void* user_param_ = nullptr;
};

/*
创建Scheduler时就会产生一个默认的Processer，Start中指定的线程个数是包括这个默认线程的
也就是每个Scheduler至少有一个默认的物理线程，比如Start(4)会再生成3个线程，保持总共4个
Processer来执行协程。注意执行Start的那个就是默认线程，只有退出协程库时才会从Start中返回。
*/
// 协程调度器
// 负责管理1到N个调度线程, 调度从属协程.
// 可以调用Create接口创建更多额外的调度器
class Scheduler
{
    friend class Processer;

public:
	//会创建全局默认的调度器
    ALWAYS_INLINE static Scheduler& getInstance();
	/*
	创建单独的调度器，用来实现业务间的隔离，不同的Scheduler独自完成自己的工作
	有自己的process，就像是多个独立的协程库，在同一进程中使用了多个独立协程库
	除非有特殊的需求一般是不需要创建额外Scheduler，使用默认Scheduler就可以了
	*/
    static Scheduler* Create();

    // 创建一个协程
    void CreateTask(TaskF const& fn, TaskOpt const& opt);
	//协程函数有一个参数
	void CreateTask(TaskF1 const& fn, TaskOpt const& opt);

    // 当前是否处于协程中
    bool IsCoroutine();

    // 是否没有协程可执行
    bool IsEmpty();

    // 启动调度器
    // @minThreadNumber : 最小调度线程数, 为0时, 设置为cpu核心数.
    // @maxThreadNumber : 最大调度线程数, 为0时, 设置为minThreadNumber.
    //          如果maxThreadNumber大于minThreadNumber, 则当协程产生长时间阻塞时,
    //          可以自动扩展调度线程数.
    void Start(int minThreadNumber = 1, int maxThreadNumber = 0);
    void goStart(int minThreadNumber = 1, int maxThreadNumber = 0);
    static const int s_ulimitedMaxThreadNumber = 40960;

    // 停止调度 
    // 注意: 停止后无法恢复, 仅用于安全退出main函数, 不保证终止所有线程.
    //       如果某个调度线程被协程阻塞, 必须等待阻塞结束才能退出.
    void Stop();

    // 当前调度器中的协程数量
    uint32_t TaskCount();

    // 当前协程ID, ID从1开始（不在协程中则返回0）
    uint32_t GetCurrentTaskID();
	//得到当前协程所在Processer的ID
	int GetCurrentProcesserID();

    // 当前协程切换的次数
    uint64_t GetCurrentTaskYieldCount();

    // 设置当前协程调试信息, 打印调试信息时将回显
    void SetCurrentTaskDebugInfo(std::string const& info);
	//创建毫秒级的timer
	TimerID CreateMilliTimer(int duration, TimerCallBack const& cb, void* userParam = nullptr);
	//停止timer，如果是已经是被唤醒了的timer返回false，未被唤醒的返回true
	bool StopTimer(TimerID& id);
#ifdef USE_TASK_CACHE
	//缓存不用的Task
	ALWAYS_INLINE bool Add2FreeTaskList(Task* tk)
	{
		assert(tk);
		tk->Reset();

		std::lock_guard<LFLock> locker(lock_free_task_map_);
		free_task_map_.emplace(tk->stack_size_, tk);
		return true;
	}
	//从缓存task中找一个适合stack_size的
	Task* GetFreeTaskList(TaskF const& fn, int stack_size);
	Task* GetFreeTaskList(TaskF1 const& fn, int stack_size,void* userParam = nullptr);
#endif
    bool IsStop() { return stop_; }
    static bool& IsExiting();

private:
    Scheduler();
    ~Scheduler();
    Scheduler(Scheduler const&) = delete;
    Scheduler(Scheduler &&) = delete;
    Scheduler& operator=(Scheduler const&) = delete;
    Scheduler& operator=(Scheduler &&) = delete;
    // 将一个协程加入可执行队列中
    void AddTask(Task* tk);
	//减少task计数
	void DecreaseTaskCounter() { taskCount_--; };
    // dispatcher线程函数
    // 1.根据待执行协程计算负载, 将高负载的P中的协程steal一些给空载的P
    // 2.侦测到阻塞的P(单个协程运行时间超过阀值), 将P中的其他协程steal给其他P
    void DispatcherThread();
    void NewProcessThread();
	//按task数量来做全局平均分配
	void AvgTotalTasks();
private:
    // deque of Processer, write by start or dispatch thread
    Deque<Processer*> processers_;
    LFLock started_;
    atomic_t<uint32_t> taskCount_{0};
    volatile uint32_t lastActive_ = 0;
    int minThreadNumber_ = 1;
    int maxThreadNumber_ = 1;
    std::thread dispatchThread_;
    std::mutex stopMtx_;
    volatile bool stop_ = false;
#ifdef USE_TASK_CACHE
	//以stack_size_升序的空闲Task集合
	std::multimap<int, Task*> free_task_map_;
	//free_task_map_的线程锁
	LFLock lock_free_task_map_;
#endif
	//线程调度的物理繁忙程度
	ThreadsPhyInfo threadPhyInfo_;
};

} //namespace co

#define g_Scheduler ::co::Scheduler::getInstance()

namespace co
{
    ALWAYS_INLINE Scheduler& Scheduler::getInstance()
    {
        static Scheduler obj;
        return obj;
    }

} //namespace co
