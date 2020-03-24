#include "task.h"
#include "../common/config.h"
#include <iostream>
#include <string.h>
#include <string>
#include <algorithm>
#include "../debug/listener.h"
#include "../scheduler/scheduler.h"
//#include "../scheduler/ref.h"

namespace co
{

const char* GetTaskStateName(TaskState state)
{
    switch (state) {
    case TaskState::runnable:
        return "Runnable";
    case TaskState::block:
        return "Block";
    case TaskState::done:
        return "Done";
    default:
        return "Unkown";
    }
}

void Task::Run()
{
    auto call_fn = [this]() {
#if ENABLE_DEBUGGER
        if (Listener::GetTaskListener()) {
            Listener::GetTaskListener()->onStart(this->id_);
        }
#endif
		//根据用户的使用选择fn_或fn1_
		if(!this->user_param_)
			this->fn_();
		else
			this->fn1_(this->user_param_);
        this->fn_ = TaskF(); //让协程function对象的析构也在协程中执行

#if ENABLE_DEBUGGER
        if (Listener::GetTaskListener()) {
            Listener::GetTaskListener()->onCompleted(this->id_);
        }
#endif
    };

    if (CoroutineOptions::getInstance().exception_handle == eCoExHandle::immedaitely_throw) {
        call_fn();
    } else {
        try {
            call_fn();
        } catch (...) {
            this->fn_ = TaskF();
#if !defined(__arm__)//ARM下无法连接exception_ptr
            std::exception_ptr eptr = std::current_exception();
            DebugPrint(dbg_exception, "task(%s) catched exception.", DebugInfo());

			#if ENABLE_DEBUGGER
            if (Listener::GetTaskListener()) {
                Listener::GetTaskListener()->onException(this->id_, eptr);
            }
			#endif
#endif
        }
    }

#if ENABLE_DEBUGGER
    if (Listener::GetTaskListener()) {
        Listener::GetTaskListener()->onFinished(this->id_);
    }
#endif

    state_ = TaskState::done;
    Processer::StaticCoYield();
}

void BOOST_CONTEXT_CALLDECL Task::StaticRun(transfer_t vp)
{
	Task* tk = (Task*)vp.data;
	tk->ctx_.update_caller_sp(vp.fctx);
    tk->Run();
}

Task::Task(TaskF const& fn, int stack_size)
    : ctx_(stack_size,&Task::StaticRun,this), fn_(fn), stack_size_(stack_size),user_param_(nullptr)
{
//    DebugPrint(dbg_task, "task(%s) construct. this=%p", DebugInfo(), this);
}
Task::Task(TaskF1 const& fn, int stack_size, void* user_param)
	: ctx_(stack_size, &Task::StaticRun, this), fn1_(fn), stack_size_(stack_size), user_param_(user_param)
{
	//    DebugPrint(dbg_task, "task(%s) construct. this=%p", DebugInfo(), this);
}
Task::~Task()
{
    //printf("delete Task = %p, impl = %p, weak = %ld\n", this, this->impl_, (long)this->impl_->weak_);
    assert(!this->prev);
    assert(!this->next);
//    DebugPrint(dbg_task, "task(%s) destruct. this=%p", DebugInfo(), this);
}

const char* Task::DebugInfo()
{
    if (reinterpret_cast<void*>(this) == nullptr) return "nil";

    return "";
}
#ifdef USE_TASK_CACHE
//缓存的时候重置一些数据
void Task::Reset()
{
	state_ = TaskState::runnable;
	//id_ = 0;				//id保留原来的
	proc_ = nullptr;
	user_param_ = nullptr;
}
//使用缓存的task时需要重新初始化
void Task::ReInit(TaskF const& fn)
{
	fn_ = fn;
	//task结束时已经退出用户函数，下次需要重新建立堆栈
	ctx_.ReInit();
}
void Task::ReInit(TaskF1 const& fn,void* userParam)
{
	fn1_ = fn;
	user_param_ = userParam;
	ctx_.ReInit();
}
#endif

} //namespace co
