#pragma once
#include "../common/config.h"

#undef BOOST_CONTEXT_CALLDECL
#if (defined(i386) || defined(__i386__) || defined(__i386) \
     || defined(__i486__) || defined(__i586__) || defined(__i686__) \
     || defined(__X86__) || defined(_X86_) || defined(__THW_INTEL__) \
     || defined(__I86__) || defined(__INTEL__) || defined(__IA32__) \
     || defined(_M_IX86) || defined(_I86_)) && defined(SYS_Windows)
# define BOOST_CONTEXT_CALLDECL __cdecl
#else
# define BOOST_CONTEXT_CALLDECL
#endif

namespace co {
	//��boost.context��ͬ
	typedef void*   fcontext_t;
	struct transfer_t 
	{
		fcontext_t  fctx;
		void    *   data;
	};
	typedef void(*lg_thread_func)(transfer_t from);
	extern "C"
	{
		/*
		1����ת��Ŀ��Э��ջtoȥִ�У�Э��ִ�к󷵻�������ջ��ַ(�´����������Э����Ҫ�����ջ��ַ)
		2����һ�ν���Э����ں���ʱ����transfer_t�б�������ߵ�ջ��ַ���Զ������vp
		*/
		transfer_t BOOST_CONTEXT_CALLDECL jump_fcontext(fcontext_t const to, void * vp);
		//���ش���Э�̵�ջ��ַ
		fcontext_t BOOST_CONTEXT_CALLDECL make_fcontext(void * sp, std::size_t size, lg_thread_func);
		transfer_t BOOST_CONTEXT_CALLDECL ontop_fcontext(fcontext_t const to, void * vp, transfer_t(*fn)(transfer_t));
	}

	struct StackTraits
	{
		static stack_malloc_fn_t& MallocFunc();

		static stack_free_fn_t& FreeFunc();

		static int& get_sys_page_size();
		static bool ProtectStack(void *top, int protectSize);
		static void UnprotectStack(void *top, int protectSize);

		//static bool ProtectStack(void* stack, std::size_t size, int pageSize);
		//static void UnprotectStack(void* stack, int pageSize);
	};

} // namespace co

