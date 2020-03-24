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

//����TaskF��std::function���Ǻ���ָ��.
//���ݲ��Խ������visualStudio��ʹ��functionЧ�ʵ���
//g++��ʹ��function�����Ⱥ���ָ�����
#define USE_FUNCTION

//����ģ��ѡ��
//#define USE_SELECT
#define USE_EPOLL
#define FD_LIMIT_SIZE	(0)	//�����FD������0��ʾ�����ƣ���Ӳ����ϵͳ��file-max����

#if defined(_WIN32) || defined(_WIN64)//ȷ��windows��ֻʹ��select
#ifdef USE_EPOLL
#undef USE_EPOLL
#define USE_SELECT
#endif
#endif

//#define USE_TASK_GC		//ʹ��task�ͷ�GC����
#define USE_TASK_CACHE	//����task���湦��
#ifdef USE_TASK_GC		//����GC����ʱ�Ͳ���ʹ��USE_TASK_CACHE
#undef USE_TASK_CACHE
#endif

#define TSQueue_DEBUG 0
#if defined(__linux__)
#define SYS_Linux 1
#define SYS_Unix 1
#elif defined(_WIN32) || defined(_WIN64)
#define SYS_Windows 1
#endif

// VS2013��֧��thread_local
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
#define FD_SETSIZE	(1024) //������WinSock2.hǰ��ӷ�����Ч
#include <Winsock2.h>	//ע�⣺��������ֻ����һ���ط�����
#include <Windows.h>
#include <stdint.h>
typedef int64_t ssize_t;
#define SHUT_WR	SD_SEND
#define SHUT_RD	SD_RECEIVE
#define SHUT_RDWR	SD_BOTH 
#endif

////////////////////////////////�ڴ�й©//////////////////////////////////////////
#define MEMORY_LEAK_DETECT			//�����ڴ�й©���
#if defined(MEMORY_LEAK_DETECT) && defined(SYS_Windows)
//���Զ�λ�������ڴ�й¶ ���ڵ��ļ��;�����һ�У����ڼ�� malloc ������ڴ�
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#include <memory> //std::allocator
//�ѷ����ڴ����Ϣ�������������Զ�λ����һ�з������ڴ�й¶�����ڼ�� new ������ڴ�
#ifdef _DEBUG
#define new new(_NORMAL_BLOCK, __FILE__, __LINE__)
#endif
//����
inline void EnableMemLeakCheck()
{
	//������ڳ����˳�ʱ�Զ����� _CrtDumpMemoryLeaks(),���ڶ���˳����ڵ����.
	//���ֻ��һ���˳�λ�ã������ڳ����˳�֮ǰ���� _CrtDumpMemoryLeaks()
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

	// Э�����׳�δ�����쳣ʱ�Ĵ���ʽ
	enum class eCoExHandle : uint8_t
	{
		immedaitely_throw,  // �����׳�
		on_listener,        // ʹ��listener����, ���û����listener�������׳�
	};

	typedef void*(*stack_malloc_fn_t)(size_t size);
	typedef void(*stack_free_fn_t)(void *ptr, size_t size);

	///---- ����ѡ��
	struct CoroutineOptions
	{
		/*********************** Debug options **********************/
		// ����ѡ��, ����: dbg_switch �� dbg_hook|dbg_task|dbg_wait
		uint64_t debug = 0;

		// ������Ϣ���λ�ã���д�������������ض������λ��
		FILE* debug_output = stdout;
		/************************************************************/

		/**************** Stack and Exception options ***************/
		// Э�����׳�δ�����쳣ʱ�Ĵ���ʽ
		eCoExHandle exception_handle = eCoExHandle::immedaitely_throw;

		// Э��ջ��С����, ֻ��Ӱ���ڴ�ֵ����֮���´�����P, �������״�Runǰ����.
		// stack_size�������ò�����1MB
		// Linuxϵͳ��, ����2MB��stack_size�ᵼ���ύ�ڴ��ʹ������1MB��stack_size��10��.
		int stack_size = 1 * 1024 * 1024;
		/************************************************************/

		// epollÿ�δ�����event����(Windows����Ч)
		uint32_t epoll_event_size = 10240;

		// �Ƿ�����Э��ͳ�ƹ���(����һ���������, Ĭ�ϲ�����)
		bool enable_coro_stat = false;

		// ��Э��ִ�г�ʱʱ��(��λ��΢��) (����ʱ����ǿ��stealʣ������, �ɷ��������߳�)
		uint32_t cycle_timeout_us = 100 * 1000;

		// �����̵߳Ĵ���Ƶ��(��λ��΢��)
		uint32_t dispatcher_thread_cycle_us = 1000 * 10;//1000;

		/*
		ջ�����ñ����ڴ�ε��ڴ�ҳ����(��linux����Ч)(Ĭ��Ϊ0, ��:������)
		��ջ���ڴ������ǰ��ҳ����Ϊprotect����.
		���Կ�����ѡ���ʵ�ʷ����ջ��С = stack_size + (protect_stack_page + 0��1) * stack_page_size
		����ʵ�ּ�CreateStack����
		�ر�ע��:
		linux���������ҳ�������о����ڵ���mprotect����ʱ����Ҫֱ��ȷ�������������ڴ棬����ڴ治�����ʧ��
		�����mprotect����Ϊ��������(�ѵ���������ҳ������������ȥ)
		���������mprotect��task��ȥ����ʹ��ջ�ڴ�Ҳû�г����ڴ治���쳣
		*/
#if __linux__
		int protect_stack_page = 0;
#else
		int protect_stack_page = 1;
#endif
		//ҳ��С
		int& stack_page_size;

		// ����ջ�ڴ����(malloc/free)
		// ʹ��fiber��Э�̵ײ�ʱ��Ч
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
//windows�¶���һ����WSABUF��ͬ��iovec�ṹ��
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
