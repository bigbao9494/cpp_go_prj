#pragma once
#include <unordered_map>
#include <list>
#include <errno.h>
#include <string.h>
#include <cstdlib>
#include <stdio.h>
#include <atomic>
#include <mutex>
#include <assert.h>
#include <deque>
#include <string>
#include <type_traits>
#include <stddef.h>
#include <exception>
#include <vector>
#include <set>
#include <map>
#include <functional>
#include <chrono>
#include <memory>
#include <queue>
#include <algorithm>

#define CPP_GO_DEBUG 0
#define ENABLE_DEBUGGER 0

//决定TaskF是std::function还是函数指针.
//根据测试结果来看visualStudio中使用function效率低下
//g++中使用function反而比函数指针更快
#define USE_FUNCTION

//网络模型选择
//#define USE_SELECT
#define USE_EPOLL
#define FD_LIMIT_SIZE	(0)	//打开最大FD个数：0表示不限制，由硬件和系统的file-max限制

#if defined(_WIN32) || defined(_WIN64)//确保windows下只使用select
#ifdef USE_EPOLL
#undef USE_EPOLL
#define USE_SELECT
#endif
#endif

//#define USE_TASK_GC		//使用task释放GC功能
#define USE_TASK_CACHE	//启用task缓存功能
#ifdef USE_TASK_GC		//启用GC功能时就不能使用USE_TASK_CACHE
#undef USE_TASK_CACHE
#endif

#define TSQueue_DEBUG 0
#if defined(__linux__)
#define SYS_Linux 1
#define SYS_Unix 1
#elif defined(_WIN32) || defined(_WIN64)
#define SYS_Windows 1
#endif

// VS2013不支持thread_local
#if defined(_MSC_VER) && _MSC_VER < 1900
# define thread_local __declspec(thread)
#endif

#if defined(__GNUC__) && (__GNUC__ > 3 ||(__GNUC__ == 3 && __GNUC_MINOR__ >= 1))
# define ALWAYS_INLINE __attribute__ ((always_inline)) inline 
#else
# define ALWAYS_INLINE inline
#endif

#if defined(SYS_Unix)
# define LIKELY(x) __builtin_expect(!!(x), 1)
# define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
# define LIKELY(x) x
# define UNLIKELY(x) x
#endif

#if defined(SYS_Linux)
# define ATTRIBUTE_WEAK __attribute__((weak))
#elif defined(LIBGO_SYS_FreeBSD)
# define ATTRIBUTE_WEAK __attribute__((weak_import))
#endif

#if defined(SYS_Windows)
#pragma warning(disable : 4996)
#endif

//#if defined(SYS_Windows)
//# define FCONTEXT_CALL __stdcall
//#else
//# define FCONTEXT_CALL
//#endif

#if defined(SYS_Unix)
#include <unistd.h>
#include <sys/types.h>
#endif

#if defined(SYS_Windows)
#undef FD_SETSIZE
#define FD_SETSIZE	(1024) //必须在WinSock2.h前添加否则不生效
#include <Winsock2.h>	//注意：整个工程只能有一个地方包含
#include <Windows.h>
#include <stdint.h>
typedef int64_t ssize_t;
#define SHUT_WR	SD_SEND
#define SHUT_RD	SD_RECEIVE
#define SHUT_RDWR	SD_BOTH 
#endif

////////////////////////////////内存泄漏//////////////////////////////////////////
#define MEMORY_LEAK_DETECT			//开启内存泄漏检测
#if defined(MEMORY_LEAK_DETECT) && defined(SYS_Windows)
//可以定位到发生内存泄露 所在的文件和具体那一行，用于检测 malloc 分配的内存
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#include <memory> //std::allocator
//把分配内存的信息保存下来，可以定位到那一行发生了内存泄露。用于检测 new 分配的内存
#ifdef _DEBUG
#define new new(_NORMAL_BLOCK, __FILE__, __LINE__)
#endif
//有用
inline void EnableMemLeakCheck()
{
	//该语句在程序退出时自动调用 _CrtDumpMemoryLeaks(),用于多个退出出口的情况.
	//如果只有一个退出位置，可以在程序退出之前调用 _CrtDumpMemoryLeaks()
	_CrtSetDbgFlag(_CrtSetDbgFlag(_CRTDBG_REPORT_FLAG) | _CRTDBG_LEAK_CHECK_DF);
}
#endif

#ifndef INNER_UTIME_NO_TIMEOUT
#define INNER_UTIME_NO_TIMEOUT (-1)
#endif

namespace co
{

	template <typename T>
	using atomic_t = std::atomic<T>;

	///---- debugger flags
	static const uint64_t dbg_none = 0;
	static const uint64_t dbg_all = ~(uint64_t)0;
	static const uint64_t dbg_hook = 0x1;
	static const uint64_t dbg_yield = 0x1 << 1;
	static const uint64_t dbg_scheduler = 0x1 << 2;
	static const uint64_t dbg_task = 0x1 << 3;
	static const uint64_t dbg_switch = 0x1 << 4;
	static const uint64_t dbg_ioblock = 0x1 << 5;
	static const uint64_t dbg_suspend = 0x1 << 6;
	static const uint64_t dbg_exception = 0x1 << 7;
	static const uint64_t dbg_syncblock = 0x1 << 8;
	static const uint64_t dbg_timer = 0x1 << 9;
	static const uint64_t dbg_scheduler_sleep = 0x1 << 10;
	static const uint64_t dbg_sleepblock = 0x1 << 11;
	static const uint64_t dbg_spinlock = 0x1 << 12;
	static const uint64_t dbg_fd_ctx = 0x1 << 13;
	static const uint64_t dbg_debugger = 0x1 << 14;
	static const uint64_t dbg_signal = 0x1 << 15;
	static const uint64_t dbg_channel = 0x1 << 16;
	static const uint64_t dbg_thread = 0x1 << 17;
	static const uint64_t dbg_sys_max = dbg_debugger;
	///-------------------

	// 协程中抛出未捕获异常时的处理方式
	enum class eCoExHandle : uint8_t
	{
		immedaitely_throw,  // 立即抛出
		on_listener,        // 使用listener处理, 如果没设置listener则立刻抛出
	};

	typedef void*(*stack_malloc_fn_t)(size_t size);
	typedef void(*stack_free_fn_t)(void *ptr, size_t size);

	///---- 配置选项
	struct CoroutineOptions
	{
		/*********************** Debug options **********************/
		// 调试选项, 例如: dbg_switch 或 dbg_hook|dbg_task|dbg_wait
		uint64_t debug = 0;

		// 调试信息输出位置，改写这个配置项可以重定向输出位置
		FILE* debug_output = stdout;
		/************************************************************/

		/**************** Stack and Exception options ***************/
		// 协程中抛出未捕获异常时的处理方式
		eCoExHandle exception_handle = eCoExHandle::immedaitely_throw;

		// 协程栈大小上限, 只会影响在此值设置之后新创建的P, 建议在首次Run前设置.
		// stack_size建议设置不超过1MB
		// Linux系统下, 设置2MB的stack_size会导致提交内存的使用量比1MB的stack_size多10倍.
		int stack_size = 1 * 1024 * 1024;
		/************************************************************/

		// epoll每次触发的event数量(Windows下无效)
		uint32_t epoll_event_size = 10240;

		// 是否启用协程统计功能(会有一点性能损耗, 默认不开启)
		bool enable_coro_stat = false;

		// 单协程执行超时时长(单位：微秒) (超过时长会强制steal剩余任务, 派发到其他线程)
		uint32_t cycle_timeout_us = 100 * 1000;

		// 调度线程的触发频率(单位：微秒)
		uint32_t dispatcher_thread_cycle_us = 1000 * 10;//1000;

		/*
		栈顶设置保护内存段的内存页数量(仅linux下有效)(默认为0, 即:不设置)
		在栈顶内存对齐后的前几页设置为protect属性.
		所以开启此选项后实际分配的栈大小 = stack_size + (protect_stack_page + 0或1) * stack_page_size
		具体实现见CreateStack函数
		特别注意:
		linux下如果开启页保护，感觉会在调用mprotect函数时就需要直接确定大量的物理内存，如果内存不够则会失败
		不清楚mprotect的行为是怎样的(难道被保护的页不允许被交换出去)
		如果不调用mprotect在task中去大量使用栈内存也没有出现内存不够异常
		*/
#if __linux__
		int protect_stack_page = 0;
#else
		int protect_stack_page = 1;
#endif
		//页大小
		int& stack_page_size;

		// 设置栈内存管理(malloc/free)
		// 使用fiber做协程底层时无效
		stack_malloc_fn_t & stack_malloc_fn;
		stack_free_fn_t & stack_free_fn;

		CoroutineOptions();

		ALWAYS_INLINE static CoroutineOptions& getInstance()
		{
			static CoroutineOptions obj;
			return obj;
		}
	};

	int GetCurrentProcessID();
	int GetCurrentThreadID();
	uint32_t GetCurrentCoroID();
	std::string GetCurrentTimeStr();
	const char* BaseFile(const char* file);
	const char* PollEvent2Str(short int event);
	unsigned long NativeThreadID();

#if defined(SYS_Unix)
# define GCC_FORMAT_CHECK __attribute__((format(printf,1,2)))
#else
# define GCC_FORMAT_CHECK
#endif
	std::string Format(const char* fmt, ...) GCC_FORMAT_CHECK;
	std::string P(const char* fmt, ...) GCC_FORMAT_CHECK;
	std::string P();

	class ErrnoStore {
	public:
		ErrnoStore() : restored_(false) {
#if defined(SYS_Windows)
			wsaErr_ = WSAGetLastError();
#endif
			errno_ = errno;
		}
		~ErrnoStore() {
			Restore();
		}
		void Restore() {
			if (restored_) return;
			restored_ = true;
#if defined(SYS_Windows)
			WSASetLastError(wsaErr_);
#endif
			errno = errno_;
		}
	private:
		int errno_;
#if defined(SYS_Windows)
		int wsaErr_;
#endif
		bool restored_;
	};

	extern std::mutex gDbgLock;

} //namespace co

#if defined(SYS_Windows)
int WinPrintf(const char *fmt, ...);
#define DebugPrint(type, fmt, ...) \
    do { \
        if (UNLIKELY(::co::CoroutineOptions::getInstance().debug & (type))) { \
            ::co::ErrnoStore es; \
            std::unique_lock<std::mutex> lock(::co::gDbgLock); \
            WinPrintf("[%s][%05d][%04d][%06d]%s:%d:(%s)\t " fmt "\n", \
                    ::co::GetCurrentTimeStr().c_str(),\
                    ::co::GetCurrentProcessID(), ::co::GetCurrentThreadID(), ::co::GetCurrentCoroID(), \
                    ::co::BaseFile(__FILE__), __LINE__, __FUNCTION__, ##__VA_ARGS__); \
            fflush(::co::CoroutineOptions::getInstance().debug_output); \
        } \
    } while(0)
#define PRINT	//WinPrintf
#else
#define DebugPrint(type, fmt, ...) \
    do { \
        if (UNLIKELY(::co::CoroutineOptions::getInstance().debug & (type))) { \
            ::co::ErrnoStore es; \
            std::unique_lock<std::mutex> lock(::co::gDbgLock); \
            fprintf(::co::CoroutineOptions::getInstance().debug_output, "[%s][%05d][%04d][%06d]%s:%d:(%s)\t " fmt "\n", \
                    ::co::GetCurrentTimeStr().c_str(),\
                    ::co::GetCurrentProcessID(), ::co::GetCurrentThreadID(), ::co::GetCurrentCoroID(), \
                    ::co::BaseFile(__FILE__), __LINE__, __FUNCTION__, ##__VA_ARGS__); \
            fflush(::co::CoroutineOptions::getInstance().debug_output); \
        } \
    } while(0)
#define PRINT	printf
#endif

#define LIBGO_E2S_DEFINE(x) \
    case x: return #x

#if defined(_WIN32) || defined(_WIN64)
inline void usleep(uint64_t microseconds)
{
	::Sleep((uint32_t)(microseconds / 1000));
}
inline unsigned int sleep(unsigned int seconds)
{
	::Sleep(seconds * 1000);
	return seconds;
}
//windows下定义一个与WSABUF相同的iovec结构体
struct iovec {
	u_long iov_len;
	void *iov_base;
};
inline int writev(int sock, struct iovec *iov, int nvecs)
{
	DWORD ret;
	if (WSASend(sock, (LPWSABUF)iov, nvecs, &ret, 0, NULL, NULL) == 0) {
		return ret;
	}
	return -1;
}
//WSARecv(SOCKET s,LPWSABUF lpBuffers,DWORD dwBufferCount,LPDWORD lpNumberOfBytesRecvd,LPDWORD lpFlags,LPWSAOVERLAPPED lpOverlapped,LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine);
inline int readv(int sock,const struct iovec *iov, int nvecs)
{
	DWORD ret;
	if (WSARecv(sock, (LPWSABUF)iov, nvecs, &ret, 0, NULL, NULL) == 0) {
		return ret;
	}
	return -1;
}
#endif
