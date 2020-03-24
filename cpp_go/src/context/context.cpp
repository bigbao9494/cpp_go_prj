#include "common/config.h"
#if defined(SYS_Unix)
#include <sys/mman.h>
#endif
#include "context.h"
#include "fcontext.h"

namespace co
{
	int Context::protect_size = CoroutineOptions::getInstance().protect_stack_page * CoroutineOptions::getInstance().stack_page_size;
	//int Context::protect_size = 0;
	int round_to_page_size(std::size_t stacksize)
	{
		// page size must be 2^N
		return static_cast<std::size_t>((stacksize + CoroutineOptions::getInstance().stack_page_size - 1) & (~(CoroutineOptions::getInstance().stack_page_size - 1)));
	}
	int Context::CreateStack()
	{
		if (vaddr_size_ == 0)
			vaddr_size_ = DEFAULT_STACK_SIZE;
		//vaddr_size_ = ((vaddr_size_ + CoroutineOptions::getInstance().stack_page_size - 1) / CoroutineOptions::getInstance().stack_page_size) * CoroutineOptions::getInstance().stack_page_size;//页对齐
		vaddr_size_ = round_to_page_size(vaddr_size_);//页对齐
		vaddr_size_ += protect_size * 1;//可以不用保护栈顶，节省一个页空间

		vaddr_ = (char*)CoroutineOptions::getInstance().stack_malloc_fn(vaddr_size_);

		if (!vaddr_)
		{
			DebugPrint(dbg_task, "task(%s) destruct. this=%p", "memory error", this);
			return -1;
		}
		stk_bottom_ = vaddr_ + protect_size;
		//stk_top_ = vaddr_ + vaddr_size_ - protect_size;
		//stk_top_ = vaddr_ + vaddr_size_;//可以不用保护栈顶，节省一个页空间

		if (protect_size)
		{
			//禁止读写
			if (!StackTraits::ProtectStack(vaddr_, protect_size))
			{
				printf("error: 0x%x\n", strerror(errno));
				assert(0);
			}
#if 0 //可以不用保护栈顶，节省一个页空间
			if (!StackTraits::ProtectStack(stk_top_, protect_size))
			{
				assert(0);
			}
#endif
		}

		// make context,只处理向下生长的栈
		//sp_ = lg_context_make(stk_bottom_, vaddr_size_ - 1 * protect_size, fn_);
		//处理向上生长的栈
		sp_ = make_fcontext(stk_bottom_ + vaddr_size_ - 1 * protect_size, vaddr_size_ - 1 * protect_size,fn_);

		return 0;
	}

}
