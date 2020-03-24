#pragma once
#include "common/config.h"
//#include "common/pp.h"
#include "common/syntax_helper.h"
#include "sync/channel.h"
#include "sync/co_mutex.h"
#include "scheduler/processer.h"
#include "debug/listener.h"
#include "debug/debugger.h"

#define LIBGO_VERSION 300
#define go_alias ::co::__go(__FILE__, __LINE__)-
#define go go_alias
//可适应C程序员习惯，给协程函数传递一个参数user_param
#define go1(function,user_param) ::co::__go(__FILE__, __LINE__)(function,user_param)

// create coroutine options
#define co_stack(size) ::co::__go_option<::co::opt_stack_size>{size}-
#define co_scheduler(pScheduler) ::co::__go_option<::co::opt_scheduler>{pScheduler}-

#define go_stack(size) go co_stack(size)
//可适应C程序员习惯，给协程函数传递一个参数user_param
#define go1_stack(size,function,user_param) ::co::__go(__FILE__, __LINE__)(size,function,user_param)

/* co_stack(size) xxx 解释
::co::__go(__FILE__, __LINE__) - (s)，调用临时的结构体__go的操作符 - 参数是s，它返回__go对象的引用，再调用操作符 - 参数是lambda表达式
对操作符的调用可以不用加(): ::co::__go(__FILE__, __LINE__) - (s) - (xxx) 等同于 ::co::__go(__FILE__, __LINE__) - s - xxx
::co::__go_option<::co::opt_stack_size> s{100};
::co::__go(__FILE__, __LINE__) - (s) - ([b] {
int a;
});
go s- [b] {
int a;
};
go co_stack(100) [b] {
int a;
};
*/

#define co_yield do { ::co::Processer::StaticCoYield(); } while (0)

// coroutine sleep, never blocks current thread if run in coroutine.
#if 0
#if defined(SYS_Unix)
#define co_sleep(milliseconds) do { usleep(1000 * milliseconds); } while (0)
#else
#define co_sleep(milliseconds) do { ::Sleep(milliseconds); } while (0)
#endif
#else
#define co_sleep(milliseconds) do { co::Processer::sleep(milliseconds); } while (0)
#endif

// co_sched
#define co_sched g_Scheduler

#define co_opt ::co::CoroutineOptions::getInstance()

// co_mutex
using ::co::co_mutex;

// co_chan
using ::co::co_chan;