#pragma once
#include <algorithm>
#include <stdio.h>
#include "../common/config.h"
#include "fcontext.h"

namespace co
{
	//#define _ST_PAGE_SIZE	(1024 * 4)	//暂定4K
	///* How much space to leave between the stacks, at each end */
	//#define REDZONE	_ST_PAGE_SIZE
#define DEFAULT_STACK_SIZE (64*1024)

	class Context
	{
	public:
		Context(int stack_size, lg_thread_func fn, void* priv)
			: fn_(fn), priv_(priv), vaddr_size_(stack_size)
		{
			//协程stack_size==-1代表主协程，主协程不需要创建私有堆栈，它用的是物理线程的栈
			if (stack_size >= 0)
			{
				CreateStack();
			}
		}
		~Context()
		{
			CoroutineOptions::getInstance().stack_free_fn(vaddr_, vaddr_size_);
		}
		////更新堆栈指针和数据
		//void update_ctx(fcontext_t newCtx)
		//{
		//	sp_ = newCtx;
		//};
		//保存调用者的堆栈
		void update_caller_sp(fcontext_t newCtx)
		{
			caller_sp_ = newCtx;
		};
		//跳转到当前对象的context
		ALWAYS_INLINE bool SwapIn()
		{
			//进入到Task::StaticRun(transfer_t vp),函数参数 vp.fctx==调用者的栈地址,vp.data==priv_==task*
			//Task::StaticRun中需要使用vp.fctx调用者的新栈地址，才能切换回调用者
			transfer_t ctx = jump_fcontext(sp_,priv_);
			//切换回来时需要保存新栈地址，这样下次再切换到这个协程才有正确的栈
			//Task::StaticRun内部调用SwapOut后会从上面的jump_fcontext函数返回
			sp_ = ctx.fctx;
			return true;
		}
		//切换des_ctx对应的context去，切换回调用SwapIn的那个调用者
		//ALWAYS_INLINE bool SwapOut(Context& des_ctx)
		ALWAYS_INLINE bool SwapOut()
		{
			//assert(des_ctx.sp_);
			//执行后SwapIn中会从jump_fcontext返回
			transfer_t ctx = jump_fcontext(caller_sp_,nullptr/*des_ctx.priv_*/);
			//再次调用SwapIn时jump_fcontext函数会返回,返回值transfer_t是调用者的新栈地址
			//des_ctx.sp_ = ctx.fctx;
			caller_sp_ = ctx.fctx;
			return true;
		}
		//重置堆栈，task缓存的时候使用
		ALWAYS_INLINE void ReInit()
		{
			caller_sp_ = nullptr;
			//处理向上生长的栈
			sp_ = make_fcontext(stk_bottom_ + vaddr_size_ - 1 * protect_size, vaddr_size_ - 1 * protect_size, fn_);
		}
	private:
		/*
		| REDZONE |              stack                | REDZONE |
		+---------+-----------------------------------+---------+
		|    4k   |                                   |   4k    |
		+---------+-----------------------------------+---------+
		vaddr     bottom                              top
		*/
		int CreateStack();
	private:
		static int protect_size;			//REDZONE大小
		lg_thread_func fn_;					//Task::ThreadCB的函数指针
		void* priv_ = nullptr;				//外部task指针，需要传递给Task::ThreadCB
		char* vaddr_ = nullptr;             //Base of stack's allocated memory
		int  vaddr_size_ = 0;				//Size of stack's allocated memory
		char* stk_bottom_ = nullptr;        //Lowest address of stack's usable portion
		//char* stk_top_;					//Highest address of stack's usable portion
		fcontext_t sp_ = nullptr;           //Stack pointer from C's point of view
		fcontext_t caller_sp_ = nullptr;	//调用者的堆栈
	};

} //namespace co

