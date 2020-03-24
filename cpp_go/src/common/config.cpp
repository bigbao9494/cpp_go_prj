#include "config.h"
#include "../context/fcontext.h"
#include "../scheduler/processer.h"
#include <string.h>
#include <stdarg.h>
#if defined(SYS_Unix)
#include <sys/time.h>
#include <sys/poll.h>
#include <pthread.h>
#endif

namespace co 
{
	std::mutex gDbgLock;
	CoroutineOptions::CoroutineOptions()
		:stack_malloc_fn(StackTraits::MallocFunc()),
		stack_free_fn(StackTraits::FreeFunc()),
		stack_page_size(StackTraits::get_sys_page_size())
	{
	}
	const char* BaseFile(const char* file)
	{
		const char* p = strrchr(file, '/');
		if (p) return p + 1;

		p = strrchr(file, '\\');
		if (p) return p + 1;

		return file;
	}
	int GetCurrentProcessID()
	{
#if defined(SYS_Unix)
		return getpid();
#else
		return 0;
#endif 
	}
	int GetCurrentThreadID()
	{
		auto proc = Processer::GetCurrentProcesser();
		return proc ? proc->Id() : -1;
	}

	uint32_t GetCurrentCoroID()
	{
		Task* tk = Processer::GetCurrentTask();
		return tk ? tk->id_ : 0;
	}

	std::string GetCurrentTimeStr()
	{
#if defined(SYS_Unix)
		struct tm local;
		struct timeval tv;
		gettimeofday(&tv, NULL);
		localtime_r(&tv.tv_sec, &local);
		char buffer[128];
		snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d.%06d",
			local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
			local.tm_hour, local.tm_min, local.tm_sec, (int)tv.tv_usec);
		return std::string(buffer);
#else
		return std::string();
#endif
	}

	const char* PollEvent2Str(short int event)
	{
		event &= POLLIN | POLLOUT | POLLERR;
		switch (event) {
			LIBGO_E2S_DEFINE(POLLIN);
			LIBGO_E2S_DEFINE(POLLOUT);
			LIBGO_E2S_DEFINE(POLLERR);
			LIBGO_E2S_DEFINE(POLLNVAL);
			LIBGO_E2S_DEFINE(POLLIN | POLLOUT);
			LIBGO_E2S_DEFINE(POLLIN | POLLERR);
			LIBGO_E2S_DEFINE(POLLOUT | POLLERR);
			LIBGO_E2S_DEFINE(POLLIN | POLLOUT | POLLERR);
		default:
			return "Zero";
		}
	}
	unsigned long NativeThreadID()
	{
#if defined(SYS_Unix)
		return reinterpret_cast<unsigned long>(pthread_self());
#else
		return (unsigned long)GetCurrentThreadId();
#endif
	}

	std::string Format(const char* fmt, ...)
	{
		va_list ap;
		va_start(ap, fmt);
		char buf[4096];
		int len = vsnprintf(buf, sizeof(buf), fmt, ap);
		va_end(ap);
		return std::string(buf, len);
	}

	std::string P(const char* fmt, ...)
	{
		va_list ap;
		va_start(ap, fmt);
		char buf[4096];
		int len = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
		buf[len] = '\n';
		buf[len + 1] = '\0';
		va_end(ap);
		return std::string(buf, len + 1);
	}
	std::string P()
	{
		return "\n";
	}
} //namespace co

#if defined(SYS_Windows)

//windows下打印输出到VS的输出窗口
int WinPrintf(const char *fmt, ...)
{
	int i;
	char buf[2048];

	va_list arg = (va_list)((char*)(&fmt) + sizeof(void*));
	i = vsprintf(buf, fmt, arg);
	//write(buf, i);
	OutputDebugStringA(buf);
	printf(buf);//同时输出到窗口

	return i;
}
#endif
