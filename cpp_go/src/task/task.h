#pragma once
#include "../common/config.h"
#include "../common/ts_queue.h"
//#include "../common/anys.h"
#include "../context/context.h"
#include "../debug/debugger.h"

namespace co
{

enum class TaskState
{
    runnable,
    block,//表示阻塞状态：IO阻塞、休眠、协程锁阻塞、条件变量阻塞，都使用此状态
    done,
};
const char* GetTaskStateName(TaskState state);
typedef std::function<void()> TaskF;
//函数对象增加额外参数，让用户可以在协程函数中使用外部传递的参数
typedef std::function<void(void* user_param)> TaskF1;
class Processer;

struct Task
    : public TSQueueHook,public CoDebugger::DebuggerBase<Task>
{
	//使用union来存储函数对象，因为使用者可以使用无参数和有参数的协程函数
	union
	{
		TaskF fn_;
		TaskF1 fn1_;
	};

    TaskState state_ = TaskState::runnable;
	uint32_t id_ = 0;
    Processer* proc_ = nullptr;
    Context ctx_;
	// 保存exception的指针,暂时未使用
    //std::exception_ptr eptr_;
    uint64_t yieldCount_ = 0;
    atomic_t<uint32_t> suspendId_ {0};
	//外部请求栈大小，stack_size=-1表示主协程不创建私有堆栈空间
	int  stack_size_ = 0;
	//传递给协程函数的参数
	void* user_param_ = nullptr;

    Task(TaskF const& fn, int stack_size);
	Task(TaskF1 const& fn, int stack_size, void* user_param = nullptr);
    ~Task();

	///*
	//决定自已能否被移动到其它线程中
	//当task已经被一个Processer执行过就不能被移动到其它Processer去，因为它的Context已经固定
	//*/
	//virtual bool moveable()
	//{
	//	return !parent_task_ ? true : false;
	//}

	//runing为当前正在执行的协程,切换到目的协程时需要保存当前正在运行协程的context
	ALWAYS_INLINE bool SwapIn()
	{
		return ctx_.SwapIn();
	}
	//切换回主协程
	ALWAYS_INLINE bool SwapOut()
	{
		//目前这种设计中不应该出现主协程中调用SwapOut
		//主协程只会调用SwapIn切换到其它协程，其它协程才会调用SwapOut
		assert(!IsMainTask());
		return ctx_.SwapOut();
	}
	/*
	判断是否为主协程
	Linux在开启hook时由于hook了sleep等函数而主协程中又调用了这些函数(Scheduler::Run)
	必须在linux_glibc_hook.cpp的hook函数里使用IsMainTask判断是否为主协程避免主协程执行SwapOut
	*/
	bool IsMainTask()
	{
		return stack_size_ < 0 ? true : false;
	}
    const char* DebugInfo();
#ifdef USE_TASK_CACHE
	//缓存的时候重置一些数据
	void Reset();
	//重新设置用户协程函数
	void ReInit(TaskF const& fn);
	void ReInit(TaskF1 const& fn,void* userParam = nullptr);
#endif
private:
    void Run();
    //static void FCONTEXT_CALL StaticRun(lg_context_from_t vp);
	static void BOOST_CONTEXT_CALLDECL StaticRun(transfer_t vp);
    Task(Task const&) = delete;
    Task(Task &&) = delete;
    Task& operator=(Task const&) = delete;
    Task& operator=(Task &&) = delete;
};

#define TaskInitPtr reinterpret_cast<Task*>(0x1)
#define TaskRefDefine(type, name) \
    ALWAYS_INLINE type& TaskRef ## name(Task *tk) \
    { \
        typedef type T; \
        static int idx = -1; \
        if (UNLIKELY(tk == TaskInitPtr)) { \
            if (idx == -1) \
                idx = TaskAnys::Register<T>(); \
            static T ignore{}; \
            return ignore; \
        } \
        return tk->anys_.get<T>(idx); \
    }
#define TaskRefInit(name) do { TaskRef ## name(TaskInitPtr); } while(0)

} //namespace co
