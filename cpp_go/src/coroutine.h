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
//����ӦC����Աϰ�ߣ���Э�̺�������һ������user_param
#define go1(function,user_param) ::co::__go(__FILE__, __LINE__)(function,user_param)

// create coroutine options
#define co_stack(size) ::co::__go_option<::co::opt_stack_size>{size}-
#define co_scheduler(pScheduler) ::co::__go_option<::co::opt_scheduler>{pScheduler}-

#define go_stack(size) go co_stack(size)
//����ӦC����Աϰ�ߣ���Э�̺�������һ������user_param
#define go1_stack(size,function,user_param) ::co::__go(__FILE__, __LINE__)(size,function,user_param)

/* co_stack(size) xxx ����
::co::__go(__FILE__, __LINE__) - (s)��������ʱ�Ľṹ��__go�Ĳ����� - ������s��������__go��������ã��ٵ��ò����� - ������lambda���ʽ
�Բ������ĵ��ÿ��Բ��ü�(): ::co::__go(__FILE__, __LINE__) - (s) - (xxx) ��ͬ�� ::co::__go(__FILE__, __LINE__) - s - xxx
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