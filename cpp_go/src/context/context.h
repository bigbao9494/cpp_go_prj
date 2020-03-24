#pragma once
#include <algorithm>
#include <stdio.h>
#include "../common/config.h"
#include "fcontext.h"

namespace co
{
	//#define _ST_PAGE_SIZE	(1024 * 4)	//�ݶ�4K
	///* How much space to leave between the stacks, at each end */
	//#define REDZONE	_ST_PAGE_SIZE
#define DEFAULT_STACK_SIZE (64*1024)

	class Context
	{
	public:
		Context(int stack_size, lg_thread_func fn, void* priv)
			: fn_(fn), priv_(priv), vaddr_size_(stack_size)
		{
			//Э��stack_size==-1������Э�̣���Э�̲���Ҫ����˽�ж�ջ�����õ��������̵߳�ջ
			if (stack_size >= 0)
			{
				CreateStack();
			}
		}
		~Context()
		{
			CoroutineOptions::getInstance().stack_free_fn(vaddr_, vaddr_size_);
		}
		////���¶�ջָ�������
		//void update_ctx(fcontext_t newCtx)
		//{
		//	sp_ = newCtx;
		//};
		//��������ߵĶ�ջ
		void update_caller_sp(fcontext_t newCtx)
		{
			caller_sp_ = newCtx;
		};
		//��ת����ǰ�����context
		ALWAYS_INLINE bool SwapIn()
		{
			//���뵽Task::StaticRun(transfer_t vp),�������� vp.fctx==�����ߵ�ջ��ַ,vp.data==priv_==task*
			//Task::StaticRun����Ҫʹ��vp.fctx�����ߵ���ջ��ַ�������л��ص�����
			transfer_t ctx = jump_fcontext(sp_,priv_);
			//�л�����ʱ��Ҫ������ջ��ַ�������´����л������Э�̲�����ȷ��ջ
			//Task::StaticRun�ڲ�����SwapOut���������jump_fcontext��������
			sp_ = ctx.fctx;
			return true;
		}
		//�л�des_ctx��Ӧ��contextȥ���л��ص���SwapIn���Ǹ�������
		//ALWAYS_INLINE bool SwapOut(Context& des_ctx)
		ALWAYS_INLINE bool SwapOut()
		{
			//assert(des_ctx.sp_);
			//ִ�к�SwapIn�л��jump_fcontext����
			transfer_t ctx = jump_fcontext(caller_sp_,nullptr/*des_ctx.priv_*/);
			//�ٴε���SwapInʱjump_fcontext�����᷵��,����ֵtransfer_t�ǵ����ߵ���ջ��ַ
			//des_ctx.sp_ = ctx.fctx;
			caller_sp_ = ctx.fctx;
			return true;
		}
		//���ö�ջ��task�����ʱ��ʹ��
		ALWAYS_INLINE void ReInit()
		{
			caller_sp_ = nullptr;
			//��������������ջ
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
		static int protect_size;			//REDZONE��С
		lg_thread_func fn_;					//Task::ThreadCB�ĺ���ָ��
		void* priv_ = nullptr;				//�ⲿtaskָ�룬��Ҫ���ݸ�Task::ThreadCB
		char* vaddr_ = nullptr;             //Base of stack's allocated memory
		int  vaddr_size_ = 0;				//Size of stack's allocated memory
		char* stk_bottom_ = nullptr;        //Lowest address of stack's usable portion
		//char* stk_top_;					//Highest address of stack's usable portion
		fcontext_t sp_ = nullptr;           //Stack pointer from C's point of view
		fcontext_t caller_sp_ = nullptr;	//�����ߵĶ�ջ
	};

} //namespace co

