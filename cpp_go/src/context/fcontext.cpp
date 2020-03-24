#include "fcontext.h"
#include <memory>
#include <string.h>

#if defined(SYS_Unix)
#include <sys/mman.h>
#endif

namespace co
{
#if defined(SYS_Windows)
	//windows下的内存分配函数
	void* MallocFuncWin(size_t size_)
	{
		void *start_ptr = ::VirtualAlloc(0, size_, MEM_COMMIT, PAGE_READWRITE);
		assert(start_ptr);
		return start_ptr;
	}
	void FreeFuncWin(void *ptr, size_t size_)
	{
		::VirtualFree(ptr, 0, MEM_RELEASE);
	}
#endif
#if defined(SYS_Unix)
	//linux下的内存分配函数
	void* MallocFuncLinux(size_t size_)
	{
		//使用mmap分配内存，如果开启页保护时(ProtectStack)内存分配会出现奇怪的异常
		//void *start_ptr = ::mmap(0, size_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		void *start_ptr = malloc(size_);
		assert(start_ptr);
		return start_ptr;
	}
	void FreeFuncLinux(void *ptr, size_t size_)
	{
		//::munmap(ptr,size_);
		free(ptr);
	}
#endif
	stack_malloc_fn_t& StackTraits::MallocFunc()
	{
#if defined(SYS_Windows)
		static stack_malloc_fn_t fn = MallocFuncWin;
#elif defined(SYS_Unix)
		static stack_malloc_fn_t fn = MallocFuncLinux;
#else
		assert(0);
#endif
		return fn;
	}
	stack_free_fn_t& StackTraits::FreeFunc()
	{
#if defined(SYS_Windows)
		static stack_free_fn_t fn = FreeFuncWin;
#elif defined(SYS_Unix)
		static stack_free_fn_t fn = FreeFuncLinux;
#else
		assert(0);
#endif
		return fn;
	}
	int& StackTraits::get_sys_page_size()
	{
		static int sys_page_size = 1024 * 4;
#if defined(SYS_Windows)
		SYSTEM_INFO sSysInfo;
		GetSystemInfo(&sSysInfo);
		sys_page_size = (int)sSysInfo.dwPageSize;
#elif defined(SYS_Unix)
		sys_page_size = getpagesize();
#endif
		return sys_page_size;
	}
	bool StackTraits::ProtectStack(void *top, int protectSize)
	{
#if defined(SYS_Windows)
		DWORD old_options;
		BOOL ret = ::VirtualProtect(top, protectSize, PAGE_READWRITE | PAGE_GUARD, &old_options);
		assert(ret);
		return (bool)ret;
#endif
#if defined(SYS_Unix)
#if 1
		//保证地址页对齐
		void *protect_page_addr = ((std::size_t)top & 0xfff) ? (void*)(((std::size_t)top & ~(std::size_t)0xfff) + 0x1000) : top;
		//会造成一定空间浪费0~(4096-1),protect_page_addr >= top
		if (-1 == mprotect(protect_page_addr, protectSize, PROT_NONE)) {
			DebugPrint(dbg_task, "origin_addr:%p, align_addr:%p, page_size:%d,protect stack top error: %s",
				top, protect_page_addr, protectSize, strerror(errno));
			printf("origin_addr:%p, align_addr:%p, page_size:%d,protect stack top error: %s\n", top, protect_page_addr, protectSize, strerror(errno));
			return false;
		}
		else {
			DebugPrint(dbg_task, "origin_addr:%p, align_addr:%p, page_size:%d, protect stack success.",
				top, protect_page_addr, protectSize);
			return true;
		}
#else
		//使用mmap分配内存不需要处理页对齐
		return !::mprotect(top, protectSize, PROT_NONE);
#endif
#endif
	}
	void StackTraits::UnprotectStack(void *top, int protectSize)
	{
#if defined(SYS_Windows)
#endif
#if defined(SYS_Unix)
#if 1
		void *protect_page_addr = ((std::size_t)top & 0xfff) ? (void*)(((std::size_t)top & ~(std::size_t)0xfff) + 0x1000) : top;
		if (-1 == mprotect(protect_page_addr, protectSize, PROT_READ | PROT_WRITE)) {
			DebugPrint(dbg_task, "origin_addr:%p, align_addr:%p, page_size:%d,protect stack top error: %s",
				top, protect_page_addr, protectSize, strerror(errno));
		}
		else {
			DebugPrint(dbg_task, "origin_addr:%p, align_addr:%p, page_size:%d,protect stack success.",
				top, protect_page_addr, protectSize);
		}
#else
		//使用mmap分配内存不需要处理页对齐
		mprotect(top, protectSize, PROT_READ | PROT_WRITE);
#endif
#endif
	}

} //namespace co

