/************************************************
* libgo sample1
*************************************************/
#include "coroutine.h"
//#include "win_exit.h"
#include <stdio.h>
#include <thread>
#include <iostream>
#include "task/task.h"
#include "sync/co_condition_variable.h"
using namespace std;

static void user_function1()
{
	for (int i = 0; i < 100; i++)
	{
		printf("user_function1: %d\n", i);
		co_yield;
	}
}
static void user_function2(void* userParam)
{
	//֧��һ������
	printf("0x%x\n", *(int*)userParam);

	for (int i = 0; i < 100; i++)
	{
		printf("user_function2: %d\n", i);
		co_sleep(1000);
	}
}
int test_coroutine_1()
{
	//����һ��Э�̣�Ĭ��ջ��С
	go user_function1;
	//������2��Э�̣�ָ��32Kջ
	go_stack(1024 * 32)user_function1;
	//������3��Э�̣�ָ��32Kջ���������
	int data1 = 0x1122;
	go1_stack(1024 * 32, user_function2,&data1);

	//Ĭ��ֻ��һ���߳�ִ�о������̣߳���������ǰ�߳�
	co_sched.Start();
	return 0;
}
int test_coroutine_2()
{
	//����100��Э��
	for (int i = 0;i < 100;i++)
	{
		go user_function1;
	}
	//����4���߳�ִ��,��������ǰ�߳�
	//��100������ĸ��ؾ�����ɿ����CPUʵ�ʸ���������ж�̬����
	co_sched.Start(4);
	return 0;
}
int test_coroutine_3()
{
	//����100��Э��
	for (int i = 0; i < 100; i++)
	{
		//ʹ��lambda���ʽ
		go []{
			for (int i = 0; i < 100; i++)
			{
				printf("lambda func: %d\n", i);
				co_sleep(1000);//ʹ��co_sleep���ó�CPU
			}
		};
	}
	//����4���߳�ִ��,���������߳�
	//��100������ĸ��ؾ�����ɿ����CPUʵ�ʸ���������ж�̬����
	std::thread t([] { co_sched.Start(4); });
	t.detach();

	//���߳̿�����һЩ�������
	while (1)
	{
		usleep(1000 * 1000);
		printf("task_cout: %u\n", co_sched.TaskCount());
		//���������������˳�
		if (0)
		{
			co_sched.Stop();
			break;
		}
	}
	return 0;
}
static void timer_callback1(void* userParam)
{
	printf("timer1\n");
}
int test_timer1()
{
	//�ֱ𴴽�5000ms��10000ms��200��500�Ķ�ʱ��
	//c����Աʹ�ú�����call_back
	co::TimerID t1 = co_sched.CreateMilliTimer(5000, timer_callback1);
	//c++����Աʹ��lambda���ʽ��call_back
	co::TimerID t2 = co_sched.CreateMilliTimer(10000, [](void* userParam) {printf("timer2\n"); });
	co::TimerID t3 = co_sched.CreateMilliTimer(200, [](void* userParam) {printf("timer3\n"); });
	co::TimerID t4 = co_sched.CreateMilliTimer(500, [](void* userParam) {printf("timer4\n"); });

	co_sched.Start(4);
	return 0;
}
void timer_fun1(void* userParam)
{
	printf("%s: %x\n", __FUNCTION__, (uint32_t*)userParam);
}
int test_timer2()
{
	//������ʱ��
	co::TimerID t1 = co_sched.CreateMilliTimer(5000, timer_fun1, (void*)0x12345);
	co::TimerID t2 = co_sched.CreateMilliTimer(10000, [](void* userParam) {printf("timer1\n"); });
	co_sched.Start();
	return 0;
}
int test_timer3()
{
	/*
	��ʾ��Э���д�����ʱ��
	ͬʱ���Զ�����ֹͣʱ����
		t2��t3����������ʱ�䵽ʱֹͣt1
	*/
	go_stack(1024 * 32) []() {
		co::TimerID t1 = co_sched.CreateMilliTimer(10000, [](void* userParam) {printf("timer1\n"); });

		auto f1 = [t1](void* userParam) mutable {
			printf("timer2\n");
			printf("stop timer1: %d\n", co_sched.StopTimer(t1));
		};
		auto f2 = [t1](void* userParam) mutable {
			printf("timer3\n");
			printf("stop timer1: %d\n", co_sched.StopTimer(t1));
			//���������ͷ�timerռ�õ��ڴ�
			co::TimerID t4 = co_sched.CreateMilliTimer(10000, [](void* userParam) {printf("timer4\n"); });
			t4.reset();
		};
		co::TimerID t2 = co_sched.CreateMilliTimer(1000, f1);
		co::TimerID t3 = co_sched.CreateMilliTimer(5000, f2);
	};

	co_sched.Start();
	return 0;
}

int test_mutex1()
{
	co_mutex cm;

	go [&cm]{
		for (int i = 0; i < 10; i++)
		{
			{
				std::lock_guard<co_mutex> lock(cm);
				printf("thread: %d,%lld: %d\n", co::GetCurrentThreadID(), co_sched.GetCurrentTaskID(), i);
				co_sleep(1000);
			}
		}
	};
	go [&cm]{
		for (int i = 0; i < 10; i++)
		{
			{
				std::lock_guard<co_mutex> lock(cm);
				printf("thread: %d,%lld: %d\n", co::GetCurrentThreadID(), co_sched.GetCurrentTaskID(), i);
				co_sleep(1000);
			}
		}
	};

	co_sched.Start(2);
	return 0;
}
static int c1 = 0;
#define TASK_NUMBER	500000
int test_mutex2()
{
	for (int i = 0; i < TASK_NUMBER; i++)
	{
		go_stack(1024 * 8)[&] {
			for (int i = 0; i < 1; ++i)
			{
			}
		};
	}

	std::thread t([] { co_sched.Start(4); });
	t.detach();

	auto s = std::chrono::steady_clock::now();
	while (1)
	{
		usleep(1000 * 10);
		if (co_sched.TaskCount() == 0)
			break;
	}
	int dt = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - s).count();
	printf("times: %d\n", dt); //������ʱ1����Э��ʱ��


	co_mutex cm;
	for (int i = 0; i < TASK_NUMBER; i++)
	{
		go_stack(1024 * 8)[&] {
			for (int i = 0; i < 1; ++i) {
				cm.lock();
				c1++;
				//printf("coroutine %d lock: %lld\n",c1,co_sched.GetCurrentTaskID());
				cm.unlock();
			}
		};
	}
	printf("%u\n", co_sched.TaskCount());

	s = std::chrono::steady_clock::now();
	while (1)
	{
		usleep(1000 * 10);
		//printf("%u\n", co_sched.TaskCount());
		if (co_sched.TaskCount() == 0)
			break;
	}
	dt = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - s).count();
	printf("times: %d\n", dt);//����ͬһ����ʱ1����Э��ʱ��

	co_sched.Stop();
	assert(c1 == TASK_NUMBER);

	return 0;
}
int test_channel_1()
{
	// Channel����ͬʱ�ɶ���̶߳�д.
	// ChannelҲ��һ��ģ����,
	// ʹ�����´��뽫����һ���޻������ġ����ڴ���������Channel��
	co_chan<int> ch_0;
	// channel����������, ��Э�̼乲��ֱ��copy����.
	go [=]{
		// ��Э����, ��ch_0д��һ������1.
		// ����ch_0û�л�����, ��˻�������ǰЭ��, ֱ�����˴�ch_0�ж�ȡ����:
		ch_0 << 1;
	};
	go [=]{
		// Channel�����ü�����, ����Channel����������µ�Channel, ֻ�����þɵ�Channel.
		// ���, ����Э��ʱ����ֱ�ӿ���Channel.
		// Channel��mutable��, ��˿���ֱ��ʹ��const Channel��д����, 
		// ����ʹ��lambda���ʽʱ�Ǽ�Ϊ���������.
		// ��ch_0�ж�ȡ����:
		int i;
		ch_0 >> i;
		printf("i = %d\n", i);
	};

	// ��������������Ϊ1��Channel, ��������ָ��:
	co_chan<std::shared_ptr<int>> ch_1(1);
	go [=]{
		std::shared_ptr<int> p1(new int(1));
		// ��ch_1��д��һ������, ����ch_1��һ����������λ, ��˿���ֱ��д�������������ǰЭ��.
		ch_1 << p1;
		std::shared_ptr<int> p2(new int(2));
		// �ٴ���ch_1��д������2, ����ch_1����������, ���������ǰЭ��, �ȴ����������ֿ�λ.
		ch_1 << p2;
	};
	go [=]{
		std::shared_ptr<int> ptr;
		// ����ch_1��ִ��ǰһ��Э��ʱ��д����һ��Ԫ��, ������������ȡ���ݵĲ������������.
		ch_1 >> ptr;
		printf("*ptr = %d\n", *ptr);
		// ����ch_1�������ѿ�, �������������ʹ��ǰЭ�̷���ִ��Ȩ, �ȴ���һ��Э��д���������.
		ch_1 >> ptr;
		printf("*ptr = %d\n", *ptr);
	};

	co_sched.Start(2);
	return 0;
}
int test_channel_2()
{
	// Channel��֧�ִ���ʱ�ĵȴ�����, �ͷ�������ģʽ
	co_chan<int> ch_2;
	go [=]{
		// ʹ��TryPop��TryPush�ӿ�, ����������������ȴ�.
		// ��ChannelΪ��ʱ, TryPop��ʧ��; ��Channelд��ʱ, TryPush��ʧ��.
		// ��������ɹ�, ����true, ���򷵻�false.
		int val = 0;
		bool isSuccess = ch_2.TryPop(val);

		// ʹ��TimedPop��TimedPush�ӿ�, �����ڵڶ����������õȴ��ĳ�ʱʱ��
		// �����ʱ, ����false, ���򷵻�true.
		isSuccess = ch_2.TimedPush(1,(1000 * 10));
		printf("isSuccess: %d\n", isSuccess);
	};

	co_sched.Start(2);
	return 0;
}
int test_channel_3()
{
#define COUNTS	(100000)

	//�����޻���channel���ٶ�
	co_chan<int> ch_0(0);
	for (int i = 0;i < 1;i++)
	{
		go_stack(16 * 1024) [&ch_0]() {
			printf("procID: %d\n", co_sched.GetCurrentProcesserID());
			//д��N������
			for (int i = 0;i < COUNTS;i++)
			{
				//printf("ch_0 write: %d\n", i);
				ch_0 << i;
			}
		};
	}
	for (int i = 0; i < 1; i++)
	{
		go_stack(16 * 1024) [&ch_0]() {
			printf("procID: %d\n", co_sched.GetCurrentProcesserID());
			auto s = std::chrono::steady_clock::now();
			//����N������
			int v;
			for (int i = 0; i < COUNTS; i++)
			{
				//v = 0;
				ch_0 >> v;
				//printf("ch_0 read: %d\n", v);
			}
			printf("read times: %lld ms\n", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - s).count());
		};
	}

	//co_sched.Start(2);

	std::thread t([] { co_sched.Start(2); });
	t.detach();

	usleep(1000 * 1000);
	int v = 0;
	while (co_sched.TaskCount() > 0)
	{
		printf("task_cout: %u\n", co_sched.TaskCount());
		usleep(1000 * 1000);
		if (v > 0)
		{
			ch_0 >> v;//test only
			v = 0;
		}
	}

	co_sched.Stop();
	return 0;
}
void go_f()
{
	//printf("%lld\n", co_sched.GetCurrentTaskID());

	//co_sleep(10);
}
void test_huge_go()
{
	//co::CoroutineOptions::getInstance().debug = co::dbg_task;
	for (int i = 0; i < 500000; i++)
		go_stack(16*1024) go_f;
	co_sched.Start(2);
}
#if 0
void test_huge_go1()
{
	//co::CoroutineOptions::getInstance().debug = co::dbg_task;
	#define task_count (10000)
	::co::__go_option<::co::opt_stack_size> s[task_count];
	co::Task* tasks[task_count] = { nullptr };
	for (int i = 0; i < task_count; i++)
	{
		tasks[i] = new co::Task(go_f,10*1024);
	}
	printf("go: %d,task: %d\n", sizeof(::co::__go)+sizeof(s[0]),sizeof(co::Task));
	for (int i = 0; i < task_count; i++)
	{
		tasks[i]->DecrementRef();
	}
	co_sched.Start();//��������ǰ�߳�
}
void CoYield1()
{
	//�õ�1���̲߳������ִ�����е�task�����������̲߳��л���ȥsteal_task����2���߳�
	//usleep(1000 * 1000 * 2);

	for (int i = 0; i < 10; i++)
	{
		printf("---------Processer%d task%d: %d\n", co_sched.GetCurrentProcesserID(), co_sched.GetCurrentTaskID(), i + 1);
		//��֤ÿ��task��������ʼ������߳��ȿ��ٵ�ִ��һ��
		if (i > 0)
		{
			if (co_sched.GetCurrentProcesserID() == 0)
			{
				usleep(1000 * 1000);//ģ��ҵ�������
			}
			else
				usleep(1000 * 100);
		}
		co_yield;
	}
}
//��Ҫ����޸�DispatcherThread��Ƶ���ƶ�task
void test_task_move()
{
	for (int i = 0; i < 10; i++)
		go_stack(8 * 1024) CoYield1;

	std::thread t([] { co_sched.Start(2); });
	t.detach();

	usleep(1000 * 1000 * 2);
	while (co_sched.TaskCount() > 0)
	{
		usleep(1000 * 1000 * 1);
		printf("task_cout: %u\n", co_sched.TaskCount());
	}
	co_sched.Stop();
}
//////////////////////////////////////////////////////////////////////////
#include "task/task.h"
using namespace co;
void co_yield_1(void* p);
uint64_t tmp1 = 0x1;
uint64_t tmp2 = 0x2;
Task* tk1 = new Task(co_yield_1, 8 * 1024, &tmp1);
//Task* tk2 = new Task(co_yield_1, 8 * 1024, &tmp2);
//Task* main_task_1 = new Task(NULL, -1, nullptr);
//Task* main_task_2 = new Task(NULL, -1, nullptr);

void co_yield_1(void* p)
{
	Task* currentTk = (Task*)(*(uint64_t*)p);
	for (int i = 0; i < 10; i++)
	{
		printf("co_yield_1: %d\n", i);
		usleep(1000 * 100);//ģ��ҵ�������
		currentTk->SwapOut();//�л����߳�
	}
}
void thread_fun1(void*)
{
	for (int i = 0; i < 2; i++)
	{
		printf("thread1: %d\n", i);
		tk1->SwapIn();
	}
}
void thread_fun2(void*)
{
	for (int i = 0; i < 2; i++)
	{
		printf("thread2: %d\n", i);
		//if (i == 0)
		{
			tk1->SwapIn();
		}
	}
}
void test_task_move1()
{
	tmp1 = (uint64_t)tk1;
	//tmp2 = (uint64_t)tk2;

	//task�����߳�1����������
	std::thread thd1(thread_fun1, nullptr);
	thd1.join();
	//�ٵ��߳�2����������
	std::thread thd2(thread_fun2, nullptr);
	thd2.join();

}
//////////////////////////////////////////////////////////////////////////
#endif

#if 0 //std::condition_variable
std::mutex g_cvMutex;
std::condition_variable g_cv;
//������
std::deque<int> g_data_deque;
//�����������Ŀ
const int MAX_NUM = 30;
//����
int g_next_index = 0;
//�����ߣ��������̸߳���
const int PRODUCER_THREAD_NUM = 3;
const int CONSUMER_THREAD_NUM = 3;
void producer_thread(int thread_id)
{
	while (true)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		//����
		std::unique_lock <std::mutex> lk(g_cvMutex);
		//������δ��ʱ�������������
		g_cv.wait(lk, []() { return g_data_deque.size() <= MAX_NUM; });
		g_next_index++;
		g_data_deque.push_back(g_next_index);
		std::cout << "producer_thread: " << thread_id << " producer data: " << g_next_index;
		std::cout << " queue size: " << g_data_deque.size() << std::endl;
		//���������߳�
		g_cv.notify_all();
		//�Զ��ͷ���
	}
} 
void consumer_thread(int thread_id)
{
	while (true)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(550));
		//����
		std::unique_lock <std::mutex> lk(g_cvMutex);
		//��������Ƿ���
		g_cv.wait(lk, [] { return !g_data_deque.empty(); });
		//�����������Ϣ����
		int data = g_data_deque.front();
		g_data_deque.pop_front();
		std::cout << "\tconsumer_thread: " << thread_id << " consumer data: ";
		std::cout << data << " deque size: " << g_data_deque.size() << std::endl;
		//���������߳�
		g_cv.notify_all();
		//�Զ��ͷ���
	}
}
void test_condition_variable()
{
	std::thread arrRroducerThread[PRODUCER_THREAD_NUM];
	std::thread arrConsumerThread[CONSUMER_THREAD_NUM];
	for (int i = 0; i < PRODUCER_THREAD_NUM; i++)
	{
		arrRroducerThread[i] = std::thread(producer_thread, i);
	}
	for (int i = 0; i < CONSUMER_THREAD_NUM; i++)
	{
		arrConsumerThread[i] = std::thread(consumer_thread, i);
	}
	for (int i = 0; i < PRODUCER_THREAD_NUM; i++)
	{
		arrRroducerThread[i].join();
	}
	for (int i = 0; i < CONSUMER_THREAD_NUM; i++)
	{
		arrConsumerThread[i].join();
	}
}
#else

co_mutex g_cvMutex;
co::CoConditionVariable g_cv;
//������
std::deque<int> g_data_deque;
//�����������Ŀ
const int MAX_NUM = 30;
//����
int g_next_index = 0;
//�����ߣ��������̸߳���
const int PRODUCER_THREAD_NUM = 2;
const int CONSUMER_THREAD_NUM = 2;
bool exitFlag = false;
void producer_thread()
{
	while (!exitFlag)
	{
		//����
		std::unique_lock <co_mutex> lk(g_cvMutex);
		//������δ��ʱ�������������
		while(g_data_deque.size() > MAX_NUM)
			g_cv.Wait(g_cvMutex);
		g_next_index++;
		g_data_deque.push_back(g_next_index);
		//std::cout << " queue size: " << g_data_deque.size() << std::endl;
		//���������߳�
		g_cv.NotifyAll();
		//�Զ��ͷ���
	}
}
void consumer_thread()
{
	while (!exitFlag)
	{
		//����
		std::unique_lock <co_mutex> lk(g_cvMutex);
		//��������Ƿ���
		while(g_data_deque.empty())
			g_cv.Wait(g_cvMutex);
		//�����������Ϣ����
		int data = g_data_deque.front();
		g_data_deque.pop_front();
		//std::cout << data << " deque size: " << g_data_deque.size() << std::endl;
		//���������߳�
		g_cv.NotifyAll();
		//�Զ��ͷ���
	}
}
//��������ʹ�÷�ʽ��std����ͬ��CoConditionVariable��Ҫһ��co_mutex
void test_condition_variable()
{
	for (int i = 0; i < PRODUCER_THREAD_NUM; i++)
		go_stack(8 * 1024) producer_thread;
	for (int i = 0; i < CONSUMER_THREAD_NUM; i++)
		go_stack(8 * 1024) consumer_thread;

	std::thread t([] { co_sched.Start(4); });
	t.detach();

	usleep(1000 * 1000 * 1);
	int second = 0;
	while (second++ < 10)
	{
		usleep(1000 * 1000 * 1);
		//printf("task_cout: %u\n", co_sched.TaskCount());
	}

	exitFlag = true;
	//�ȴ����е�task�˳�
	while (co_sched.TaskCount() > 0)
	{
		usleep(1000 * 1000);
	}

	co_sched.Stop();
}
#endif
