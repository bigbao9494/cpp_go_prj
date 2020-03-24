#pragma once
#include "../common/config.h"
#include "timer/timer.h"
#include "co_mutex.h"

namespace co
{
	class CoConditionVariable
	{
	private:
		//��ֹ����
		CoConditionVariable& operator=(const CoConditionVariable& fd) = delete;
		CoConditionVariable(const CoConditionVariable& rfd) = delete;
	public:
		CoConditionVariable();
		~CoConditionVariable();
		/*
		�ȴ�����������������������㱻����������������ʱ�ᱻ����
		mutex������������ϵĻ�����(Э����)
		timeout��ʱʱ�䣺-1Ĭ�ϱ�ʾ��Զ�ȴ���>0��ʾ��ʱmsֵ
		*/
		bool Wait(CoMutex& mutex, int timeout = INNER_UTIME_NO_TIMEOUT);
		void NotifyOne();
		void NotifyAll();
	private:
		//�ȴ�����
		std::queue<Task*> waite_queue_;
		//����������Ӧ����
		CoMutex* lock_ = nullptr;
	};
}