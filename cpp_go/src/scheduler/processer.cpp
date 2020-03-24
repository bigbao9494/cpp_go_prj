#include "processer.h"
#include "scheduler.h"
#include "../common/error.h"
#include "../common/clock.h"
#include <assert.h>
//#include "ref.h"
//#include "unistd.h"
#if defined(USE_SELECT)
#include "netio/inner_net/select_io_wait.h"
#endif
#if defined(USE_EPOLL)
#include "../netio/inner_net/epoll_io_wait.h"
#endif
#define TaskRefSuspendId(tk) tk->suspendId_

namespace co {

int Processer::s_check_ = 0;

Processer::Processer(Scheduler * scheduler, int id)
    : scheduler_(scheduler), id_(id)
{
	main_task_ = new Task(NULL, -1, nullptr);

#if defined(USE_SELECT)
	io_wait_ = new SelectIoWait;
#elif defined(USE_EPOLL)
	io_wait_ = new EpollIoWait;
#endif
}
Processer::~Processer()
{
	//����������Դδ�ͷ�
	if (io_wait_)
		delete (InnerIoWait*)io_wait_;
	io_wait_ = nullptr;
	if (thread_)
		delete thread_;
	thread_ = nullptr;
}
Processer* & Processer::GetCurrentProcesser()
{
    static thread_local Processer *proc = nullptr;
    return proc;
}

Scheduler* Processer::GetCurrentScheduler()
{
    auto proc = GetCurrentProcesser();
    return proc ? proc->scheduler_ : nullptr;
}

void Processer::AddTask(Task *tk)
{
    DebugPrint(dbg_task | dbg_scheduler, "task(%s) add into proc(%u)(%p)", tk->DebugInfo(), id_, (void*)this);
    std::unique_lock<TaskQueue::lock_t> lock(newQueue_.LockRef());
    newQueue_.pushWithoutLock(tk);
    newQueue_.AssertLink();
}

void Processer::AddTask(SList<Task> & slist)
{
    DebugPrint(dbg_scheduler, "task(num=%d) add into proc(%u)", (int)slist.size(), id_);
    std::unique_lock<TaskQueue::lock_t> lock(newQueue_.LockRef());
    newQueue_.pushWithoutLock((slist));
    newQueue_.AssertLink();
}

void Processer::Stop()
{
	//���ǵ�1��Processer��Ҫ�ȴ��̺߳����˳�
	if (id_ > 0 && thread_->joinable())
		thread_->join();
	else
	{
		//���߳���Ҫ�ȴ����߳��˳�Process����
		while (active_)
			usleep(1000);
	}
}
void Processer::Process()
{
	GetCurrentProcesser() = this;
	//��ÿ���̵߳��̺߳����н��г�ʼ��
	io_wait_->InitModule();

	while (!scheduler_->IsStop())
	{
		/*
		�������ж����еĵ�1��task��ע�ⲻ��pop�����task��Ȼ�ڶ��еĵ�1��λ��
		������task��ִ�к��״̬��Ȼ��runnable�����ڶ����е�λ�ñ��ֲ���
		�´���front��Ȼ�������task��taskִ�к�״̬ΪTaskState::done��Żᱻ�Ƴ�
		runnableQueue_���У�ÿ��ִ������˵��runnableQueue_�е�task���Ѿ�ִ����һ��
		��������ͷ��ʼִ�����е�task
		*/
		runnableQueue_.front(runningTask_);
		if (!runningTask_)
		{
			//runnableQueue_��û�����ݣ���newQueue_�е��ƶ�����
			if (AddNewTasks())
				runnableQueue_.front(runningTask_);
			#if 0
			if (!runningTask_)
			{
				//newQueue_��Ҳû��������������������ϵȴ�
				WaitCondition();
				AddNewTasks();
				continue;
			}
			#else
			//�������USE_TASK_GC��gcQueue_�п����зֲ�task���ᱻ��ʱ�ͷţ�WaitCondition�вŻ����GC()������ͷ�
			if (!runningTask_)
			{
				//���е�task������block״̬: ����epoll_wait
				//��ʱ��ʾϵͳ���Խ����������״̬����Ϊû��task��Ҫִ��
				//epoll_wait��ʱʱ��timer_manager_�е����һ��sleepʱ��
				int ep_count = io_wait_->WaitLoop(timer_manager_.GetLastDuration());
				//����timer
				timer_manager_.WakeupTimers();
				continue;
			}
			#endif
		}

		//////////////////////////////////////////////////////////////////////////
		//���ѭ�����runnableQueue_�е�����task��ִ��һ�βŻ��˳�
		while (runningTask_ && !scheduler_->IsStop())
		{
			runningTask_->state_ = TaskState::runnable;
			runningTask_->proc_ = this;
			#if ENABLE_DEBUGGER
			DebugPrint(dbg_switch, "enter task(%s)", runningTask_->DebugInfo());
			if (Listener::GetTaskListener())
				Listener::GetTaskListener()->onSwapIn(runningTask_->id_);
			#endif
			++switchCount_;
			//ÿ�ζ�������Э���л�������Э��(�����л���ʽ)
			runningTask_->SwapIn();
			if (runningTask_->state_ == TaskState::block)
			{
				//task��ͨ������Suspend��غ������block״̬�ģ�Suspend���Ѿ�������runnableQueue_�����Ƴ���
				//ͬʱnextTask_�ᱣ��������һ��task
				std::unique_lock<TaskQueue::lock_t> lock(runnableQueue_.LockRef());
				runningTask_ = nextTask_;
				nextTask_ = nullptr;
			}
			else if (runningTask_->state_ == TaskState::runnable)
			{
				//assert(!nextTask_);
				std::unique_lock<TaskQueue::lock_t> lock(runnableQueue_.LockRef());
				//��ǰtask����һ��
				runningTask_ = (Task*)runningTask_->next;
			}
			else if (runningTask_->state_ == TaskState::done)
			{
				//ֱ�Ӵӵ�ǰtaskλ�õõ���һ��task
				runnableQueue_.next(runningTask_, nextTask_);

				DebugPrint(dbg_task, "task(%s) done.", runningTask_->DebugInfo());
				//��ǰtask��runnableQueue_�������Ƴ�
				runnableQueue_.erase(runningTask_);
				//��Сscheduler�е�task����
				scheduler_->DecreaseTaskCounter();
				#ifdef USE_TASK_GC
				//����������task���ͷ�
				if (gcQueue_.size() > 16)
					GC();
				//������һ�ζ�runningTask_�ļ���
				gcQueue_.push(runningTask_);
				std::unique_lock<TaskQueue::lock_t> lock(runnableQueue_.LockRef());
				//׼��ִ����һ��task
				runningTask_ = nextTask_;
				nextTask_ = nullptr;
				#else
					#ifdef USE_TASK_CACHE
					std::unique_lock<TaskQueue::lock_t> lock(runnableQueue_.LockRef());
					//�������ɾ�����task
					Scheduler::getInstance().Add2FreeTaskList(runningTask_);
					//׼��ִ����һ��task
					runningTask_ = nextTask_;
					nextTask_ = nullptr;
					#else
					std::unique_lock<TaskQueue::lock_t> lock(runnableQueue_.LockRef());
					//ֱ���ͷŽ�����task
					delete runningTask_;
					//׼��ִ����һ��task
					runningTask_ = nextTask_;
					nextTask_ = nullptr;
					#endif
				#endif
			}
		}

		//ִ�е�����runningTask_һ��Ϊ��,˵��runnableQueue_�Ѿ�������һ��
		//��runningTask_�е�taskִ����һ���ͼ����û����task����
		if (!scheduler_->IsStop())
		{
			//�����task��runnableQueue_
			AddNewTasks();
		}
	}

	//�߳̽���ʱɾ��task
	ReleaseTask();
	//��ʾ�˳��̺߳���
	active_ = false;

	PRINT("%s %d exit\n", __FUNCTION__, id_);
}
Task* Processer::GetCurrentTask()
{
    auto proc = GetCurrentProcesser();
    return proc ? proc->runningTask_ : nullptr;
}

bool Processer::IsCoroutine()
{
    return !!GetCurrentTask();
}

std::size_t Processer::RunnableSize()
{
	//size�ĵ��û�ʹ���߳���
    return runnableQueue_.size() + newQueue_.size();
}
std::size_t Processer::IOWaiteSize()
{
	return io_wait_->GetFDMapSize();
}
void Processer::InterrupteWaitLoop()
{ 
	io_wait_->InterrupteWaitLoop(); 
}
#ifdef USE_TASK_GC
void Processer::GC()
{
    auto list = gcQueue_.pop_all();
    //for (Task & tk : list) {
    //    tk.DecrementRef();
    //}
    list.clear();
	//printf("task_count: %d\n", scheduler_->TaskCount());
}
#endif
bool Processer::AddNewTasks()
{
	if (newQueue_.empty())
		return false;

    runnableQueue_.push(newQueue_.pop_all());
    newQueue_.AssertLink();
    return true;
}

bool Processer::IsBlocking()
{
	//markSwitch_ != switchCount_˵��taskִ�кܿ죬δ������task��
	//markSwitch_==0��Ĭ��ֵ����1�ε���û������״̬
	//û������ִ�е�task������ȫ���в�Ӧ��block
    if (!markSwitch_ || markSwitch_ != switchCount_ || !runningTask_)
		return false;
	//���switchCount_δ��ʱ�仯��������task��ʱ
	//�������taskռ��Processer�Ƿ�ʱ����ǰʱ�� > ��ʼʱ�� + task��ʱʱ��
	//DispatcherThread��IsBlocking��Mark�ĵ������ж�Processer�Ƿ���block״̬
	//���п��ܶ�һ����task��Processer�ж���block�ģ�DispatcherThread��IsBlocking��Mark
	//�ĵ���˳�������Ƿ�����������
    return NowMicrosecond() > markTick_ + CoroutineOptions::getInstance().cycle_timeout_us;
}

void Processer::Mark()
{
	/*
	���Processer��taskִ�кܿ죬switchCount_�᲻ͣ���ӣ�ͬʱrunningTask_��Ϊ��
	DispatcherThread��Mark�ĵ��ü��(�ϴ�Mark���ú����Mark����)�����ʱ�䣬switchCount_�����Ѿ�������
	���������˵��taskδ��ʱ��ռ��Processer�����±�־�ͼ�¼ʱ��
	���switchCount_δ��ʱ�仯����markSwitch_ == switchCount_����markTick_�޷�����
	*/
    if (runningTask_ && markSwitch_ != switchCount_) 
	{
		//���±�־
        markSwitch_ = switchCount_;
		//��¼������ִ�е�task�Ŀ�ʼʱ�䣬������DispatcherThread�е����ʱ��
        markTick_ = NowMicrosecond();
    }
}

int64_t Processer::NowMicrosecond()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(FastSteadyClock::now().time_since_epoch()).count();
}

SList<Task> Processer::Steal(std::size_t n)
{
    if (n > 0) {
        newQueue_.AssertLink();
		//���ȴӱ�processer��newQueue_β��ȡ��n�����ⲿ���ǻ�δ������runnableQueue_��
        auto slist = newQueue_.pop_back((uint32_t)n);
        newQueue_.AssertLink();
		//ȡ����ֱ�ӷ���
        if (slist.size() >= n)
            return slist;

		/*
		���뱣���runningTask_��nextTask_���������ڵ��Ǹ�Processor��runnableQueue_�Ƴ��������ܱ�֤���ᱻrunnableQueue_.pop_backWithoutLock͵��
		��Ϊ����Steal����DispatcherThread�̣߳����ʱ���runningTask_���ڱ�Processorִ�л���ΪIO��SLEEPʱ״̬���TaskState::block��ͬʱ
		�����˳�Stealʱ���ᱻ�ŵ�InnerIoWait::FD_MAP��TimerManager�У��������͵�߾͵���task����2��Processor��
		*/
        std::unique_lock<TaskQueue::lock_t> lock(runnableQueue_.LockRef());
        bool pushRunningTask = false, pushNextTask = false;
		if (runningTask_) //���������е�task�ȴ�runnableQueue_�Ƴ���
			pushRunningTask = runnableQueue_.eraseWithoutLock(runningTask_, false);// || slist.erase(runningTask_, newQueue_.check_);
		if (nextTask_) //��nextTask_�ȴ�runnableQueue_�Ƴ�����ע��nextTask_����Ϊ��
			pushNextTask = runnableQueue_.eraseWithoutLock(nextTask_, false);// || slist.erase(nextTask_, newQueue_.check_);

		//��runnableQueue_��β��ȡtask
        auto slist2 = runnableQueue_.pop_backWithoutLock((uint32_t)(n - slist.size()));
		//�ٰ�runningTask_��nextTask_�Ż�ȥ��������������������͵��
        if (pushRunningTask)
            runnableQueue_.pushWithoutLock(runningTask_);
        if (pushNextTask)
            runnableQueue_.pushWithoutLock(nextTask_);
        lock.unlock();

		//�ϲ����ε�task
        slist2.append(std::move(slist));
        if (!slist2.empty())
            DebugPrint(dbg_scheduler, "Proc(%d).Stealed = %d", id_, (int)slist2.size());
        return slist2;
    } 
	else 
	{
		// steal all
		newQueue_.AssertLink();
		auto slist = newQueue_.pop_all();
		newQueue_.AssertLink();

		/*
		���뱣���runningTask_��nextTask_���������ڵ��Ǹ�Processor��runnableQueue_�Ƴ��������ܱ�֤���ᱻrunnableQueue_.pop_backWithoutLock͵��
		��Ϊ����Steal����DispatcherThread�̣߳����ʱ���runningTask_���ڱ�Processorִ�л���ΪIO��SLEEPʱ״̬���TaskState::block��ͬʱ
		�����˳�Stealʱ���ᱻ�ŵ�InnerIoWait::FD_MAP��TimerManager�У��������͵�߾͵���task����2��Processor��
		*/
		std::unique_lock<TaskQueue::lock_t> lock(runnableQueue_.LockRef());
		bool pushRunningTask = false, pushNextTask = false;
		if (runningTask_) //���������е�task�ȴ�runnableQueue_�Ƴ���
			pushRunningTask = runnableQueue_.eraseWithoutLock(runningTask_, false);// || slist.erase(runningTask_, newQueue_.check_);
		if (nextTask_) //��nextTask_�ȴ�runnableQueue_�Ƴ�����ע��nextTask_����Ϊ��
			pushNextTask = runnableQueue_.eraseWithoutLock(nextTask_, false);// || slist.erase(nextTask_, newQueue_.check_);

		auto slist2 = runnableQueue_.pop_allWithoutLock();
		//�ٰ�runningTask_��nextTask_�Ż�ȥ��������������������͵��
		if (pushRunningTask)
			runnableQueue_.pushWithoutLock(runningTask_);
		if (pushNextTask)
			runnableQueue_.pushWithoutLock(nextTask_);
		lock.unlock();

		//�ϲ����ε�task
		slist2.append(std::move(slist));
		if (!slist2.empty())
			DebugPrint(dbg_scheduler, "Proc(%d).Stealed = %d", id_, (int)slist2.size());
		return slist2;
    }
}
bool Processer::SuspendBySelfIO(Task* tk)
{
	assert(tk == runningTask_);
	assert(tk->state_ == TaskState::runnable);
	tk->state_ = TaskState::block;
	uint64_t id = ++TaskRefSuspendId(tk);

	std::unique_lock<TaskQueue::lock_t> lock(runnableQueue_.LockRef());
	assert(tk == runningTask_);
	runnableQueue_.nextWithoutLock(runningTask_, nextTask_);
	//���޸����ü���
	bool ret = runnableQueue_.eraseWithoutLock(runningTask_, false);
	assert(ret);

	return true;
}
bool Processer::WakeupBySelfIO(Task* tk)
{
	//WakeupBySelfIO�ĵ���ֻ����tk�Լ���Processor��
	//�����ڵ��Ǹ�Processor��ִ��io_wait_->WaitLoop��timer_manager_.WakeupTimers���ô˺���
	tk->state_ = TaskState::runnable;
	size_t sizeAfterPush = runnableQueue_.push(tk);

	return true;
}
void Processer::sleep(int milliseconds)
{
	Task *tk = Processer::GetCurrentTask();
	//DebugPrint(dbg_hook, "task(%s) Hook Sleep(dwMilliseconds=%lu).", tk->DebugInfo(), dwMilliseconds);
	//�ڷ�Э�������ߣ�ֱ�������߳�
	if (!tk) {
		usleep(milliseconds * 1000);
		return;
	}

#if 0
	//��timer��ʵ��
	if (milliseconds > 0)
		Processer::Suspend(std::chrono::milliseconds(milliseconds));
#else
	//��sleeper��ʵ��
	if (milliseconds > 0)
	{
		assert(tk->proc_);
		//�Ȱ�task�����ж������Ƴ�,����û���޸����ü���,��Ϊ���ϻᱻtimer_manager_ʹ��
		tk->proc_->SuspendBySelfIO(tk);
		tk->proc_->timer_manager_.AddSleeper(std::chrono::milliseconds(milliseconds),tk);
	}
#endif

	Processer::StaticCoYield();
}
InnerIoWait* Processer::GetIoWait()
{
	auto proc = GetCurrentProcesser();
	return proc->io_wait_;
}
TimerID Processer::AddMilliTimer(int duration, TimerCallBack const& cb, void* userParam)
{
	//ʹ��milliseconds��λ
	TimerID timer = timer_manager_.AddTimer(std::chrono::milliseconds(duration),cb, userParam);
	//����timer��ʱ����Ҫ����������ֻ����һ���̲߳���
	timer->processer_ = this;
	return timer;
}
bool Processer::StopTimer(TimerID& id)
{
	return timer_manager_.StopTimer(id);
}
void Processer::ReleaseTask()
{
	Task* task;
	while (!runnableQueue_.empty())
	{
		task = runnableQueue_.pop();
		delete task;
	}
	while (!newQueue_.empty())
	{
		task = newQueue_.pop();
		delete task;
	}
#if defined(USE_TASK_GC)
	while (!gcQueue_.empty())
	{
		task = gcQueue_.pop();
		delete task;
	}
#endif
	//ɾ����task
	delete main_task_;
}

} //namespace co

