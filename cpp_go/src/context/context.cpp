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
		//vaddr_size_ = ((vaddr_size_ + CoroutineOptions::getInstance().stack_page_size - 1) / CoroutineOptions::getInstance().stack_page_size) * CoroutineOptions::getInstance().stack_page_size;//ҳ����
		vaddr_size_ = round_to_page_size(vaddr_size_);//ҳ����
		vaddr_size_ += protect_size * 1;//���Բ��ñ���ջ������ʡһ��ҳ�ռ�

		vaddr_ = (char*)CoroutineOptions::getInstance().stack_malloc_fn(vaddr_size_);

		if (!vaddr_)
		{
			DebugPrint(dbg_task, "task(%s) destruct. this=%p", "memory error", this);
			return -1;
		}
		stk_bottom_ = vaddr_ + protect_size;
		//stk_top_ = vaddr_ + vaddr_size_ - protect_size;
		//stk_top_ = vaddr_ + vaddr_size_;//���Բ��ñ���ջ������ʡһ��ҳ�ռ�

		if (protect_size)
		{
			//��ֹ��д
			if (!StackTraits::ProtectStack(vaddr_, protect_size))
			{
				printf("error: 0x%x\n", strerror(errno));
				assert(0);
			}
#if 0 //���Բ��ñ���ջ������ʡһ��ҳ�ռ�
			if (!StackTraits::ProtectStack(stk_top_, protect_size))
			{
				assert(0);
			}
#endif
		}

		// make context,ֻ��������������ջ
		//sp_ = lg_context_make(stk_bottom_, vaddr_size_ - 1 * protect_size, fn_);
		//��������������ջ
		sp_ = make_fcontext(stk_bottom_ + vaddr_size_ - 1 * protect_size, vaddr_size_ - 1 * protect_size,fn_);

		return 0;
	}

}
