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
	//支持一个参数
	printf("0x%x\n", *(int*)userParam);

	for (int i = 0; i < 100; i++)
	{
		printf("user_function2: %d\n", i);
		co_sleep(1000);
	}
}
int test_coroutine_1()
{
	//创建一个协程，默认栈大小
	go user_function1;
	//创建第2个协程，指定32K栈
	go_stack(1024 * 32)user_function1;
	//创建第3个协程，指定32K栈，传入参数
	int data1 = 0x1122;
	go1_stack(1024 * 32, user_function2,&data1);

	//默认只有一个线程执行就是主线程，会阻塞当前线程
	co_sched.Start();
	return 0;
}
int test_coroutine_2()
{
	//创建100个协程
	for (int i = 0;i < 100;i++)
	{
		go user_function1;
	}
	//启动4个线程执行,会阻塞当前线程
	//这100个任务的负载均衡会由库根据CPU实际负载情况进行动态调度
	co_sched.Start(4);
	return 0;
}
int test_coroutine_3()
{
	//创建100个协程
	for (int i = 0; i < 100; i++)
	{
		//使用lambda表达式
		go []{
			for (int i = 0; i < 100; i++)
			{
				printf("lambda func: %d\n", i);
				co_sleep(1000);//使用co_sleep来让出CPU
			}
		};
	}
	//启动4个线程执行,不阻塞主线程
	//这100个任务的负载均衡会由库根据CPU实际负载情况进行动态调度
	std::thread t([] { co_sched.Start(4); });
	t.detach();

	//主线程可以做一些监控任务
	while (1)
	{
		usleep(1000 * 1000);
		printf("task_cout: %u\n", co_sched.TaskCount());
		//满足条件，控制退出
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
	//分别创建5000ms、10000ms、200、500的定时器
	//c程序员使用函数做call_back
	co::TimerID t1 = co_sched.CreateMilliTimer(5000, timer_callback1);
	//c++程序员使用lambda表达式做call_back
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
	//创建定时器
	co::TimerID t1 = co_sched.CreateMilliTimer(5000, timer_fun1, (void*)0x12345);
	co::TimerID t2 = co_sched.CreateMilliTimer(10000, [](void* userParam) {printf("timer1\n"); });
	co_sched.Start();
	return 0;
}
int test_timer3()
{
	/*
	演示在协程中创建定时器
	同时测试定主动停止时器：
		t2和t3都会在它们时间到时停止t1
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
			//测试主动释放timer占用的内存
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
	printf("times: %d\n", dt); //不竞争时1百万协程时间


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
	printf("times: %d\n", dt);//竞争同一个锁时1百万协程时间

	co_sched.Stop();
	assert(c1 == TASK_NUMBER);

	return 0;
}
int test_channel_1()
{
	// Channel可以同时由多个线程读写.
	// Channel也是一个模板类,
	// 使用以下代码将创建一个无缓冲区的、用于传递整数的Channel：
	co_chan<int> ch_0;
	// channel是引用语义, 在协程间共享直接copy即可.
	go [=]{
		// 在协程中, 向ch_0写入一个整数1.
		// 由于ch_0没有缓冲区, 因此会阻塞当前协程, 直到有人从ch_0中读取数据:
		ch_0 << 1;
	};
	go [=]{
		// Channel是引用计数的, 复制Channel并不会产生新的Channel, 只会引用旧的Channel.
		// 因此, 创建协程时可以直接拷贝Channel.
		// Channel是mutable的, 因此可以直接使用const Channel读写数据, 
		// 这在使用lambda表达式时是极为方便的特性.
		// 从ch_0中读取数据:
		int i;
		ch_0 >> i;
		printf("i = %d\n", i);
	};

	// 创建缓冲区容量为1的Channel, 传递智能指针:
	co_chan<std::shared_ptr<int>> ch_1(1);
	go [=]{
		std::shared_ptr<int> p1(new int(1));
		// 向ch_1中写入一个数据, 由于ch_1有一个缓冲区空位, 因此可以直接写入而不会阻塞当前协程.
		ch_1 << p1;
		std::shared_ptr<int> p2(new int(2));
		// 再次向ch_1中写入整数2, 由于ch_1缓冲区已满, 因此阻塞当前协程, 等待缓冲区出现空位.
		ch_1 << p2;
	};
	go [=]{
		std::shared_ptr<int> ptr;
		// 由于ch_1在执行前一个协程时被写入了一个元素, 因此下面这个读取数据的操作会立即完成.
		ch_1 >> ptr;
		printf("*ptr = %d\n", *ptr);
		// 由于ch_1缓冲区已空, 下面这个操作会使当前协程放弃执行权, 等待第一个协程写入数据完成.
		ch_1 >> ptr;
		printf("*ptr = %d\n", *ptr);
	};

	co_sched.Start(2);
	return 0;
}
int test_channel_2()
{
	// Channel还支持带超时的等待机制, 和非阻塞的模式
	co_chan<int> ch_2;
	go [=]{
		// 使用TryPop和TryPush接口, 可以立即返回无需等待.
		// 当Channel为空时, TryPop会失败; 当Channel写满时, TryPush会失败.
		// 如果操作成功, 返回true, 否则返回false.
		int val = 0;
		bool isSuccess = ch_2.TryPop(val);

		// 使用TimedPop和TimedPush接口, 可以在第二个参数设置等待的超时时间
		// 如果超时, 返回false, 否则返回true.
		isSuccess = ch_2.TimedPush(1,(1000 * 10));
		printf("isSuccess: %d\n", isSuccess);
	};

	co_sched.Start(2);
	return 0;
}
int test_channel_3()
{
#define COUNTS	(100000)

	//测试无缓冲channel的速度
	co_chan<int> ch_0(0);
	for (int i = 0;i < 1;i++)
	{
		go_stack(16 * 1024) [&ch_0]() {
			printf("procID: %d\n", co_sched.GetCurrentProcesserID());
			//写入N个数据
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
			//读出N个数据
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
	co_sched.Start();//会阻塞当前线程
}
void CoYield1()
{
	//让第1个线程不会快速执行所有的task，这样调度线程才有机会去steal_task给第2个线程
	//usleep(1000 * 1000 * 2);

	for (int i = 0; i < 10; i++)
	{
		printf("---------Processer%d task%d: %d\n", co_sched.GetCurrentProcesserID(), co_sched.GetCurrentTaskID(), i + 1);
		//保证每个task都在它初始分配的线程先快速地执行一次
		if (i > 0)
		{
			if (co_sched.GetCurrentProcesserID() == 0)
			{
				usleep(1000 * 1000);//模拟业务计算量
			}
			else
				usleep(1000 * 100);
		}
		co_yield;
	}
}
//需要配合修改DispatcherThread来频繁移动task
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
		usleep(1000 * 100);//模拟业务计算量
		currentTk->SwapOut();//切换回线程
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

	//task先在线程1环境中运行
	std::thread thd1(thread_fun1, nullptr);
	thd1.join();
	//再到线程2环境中运行
	std::thread thd2(thread_fun2, nullptr);
	thd2.join();

}
//////////////////////////////////////////////////////////////////////////
#endif

#if 0 //std::condition_variable
std::mutex g_cvMutex;
std::condition_variable g_cv;
//缓存区
std::deque<int> g_data_deque;
//缓存区最大数目
const int MAX_NUM = 30;
//数据
int g_next_index = 0;
//生产者，消费者线程个数
const int PRODUCER_THREAD_NUM = 3;
const int CONSUMER_THREAD_NUM = 3;
void producer_thread(int thread_id)
{
	while (true)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		//加锁
		std::unique_lock <std::mutex> lk(g_cvMutex);
		//当队列未满时，继续添加数据
		g_cv.wait(lk, []() { return g_data_deque.size() <= MAX_NUM; });
		g_next_index++;
		g_data_deque.push_back(g_next_index);
		std::cout << "producer_thread: " << thread_id << " producer data: " << g_next_index;
		std::cout << " queue size: " << g_data_deque.size() << std::endl;
		//唤醒其他线程
		g_cv.notify_all();
		//自动释放锁
	}
} 
void consumer_thread(int thread_id)
{
	while (true)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(550));
		//加锁
		std::unique_lock <std::mutex> lk(g_cvMutex);
		//检测条件是否达成
		g_cv.wait(lk, [] { return !g_data_deque.empty(); });
		//互斥操作，消息数据
		int data = g_data_deque.front();
		g_data_deque.pop_front();
		std::cout << "\tconsumer_thread: " << thread_id << " consumer data: ";
		std::cout << data << " deque size: " << g_data_deque.size() << std::endl;
		//唤醒其他线程
		g_cv.notify_all();
		//自动释放锁
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
//缓存区
std::deque<int> g_data_deque;
//缓存区最大数目
const int MAX_NUM = 30;
//数据
int g_next_index = 0;
//生产者，消费者线程个数
const int PRODUCER_THREAD_NUM = 2;
const int CONSUMER_THREAD_NUM = 2;
bool exitFlag = false;
void producer_thread()
{
	while (!exitFlag)
	{
		//加锁
		std::unique_lock <co_mutex> lk(g_cvMutex);
		//当队列未满时，继续添加数据
		while(g_data_deque.size() > MAX_NUM)
			g_cv.Wait(g_cvMutex);
		g_next_index++;
		g_data_deque.push_back(g_next_index);
		//std::cout << " queue size: " << g_data_deque.size() << std::endl;
		//唤醒其他线程
		g_cv.NotifyAll();
		//自动释放锁
	}
}
void consumer_thread()
{
	while (!exitFlag)
	{
		//加锁
		std::unique_lock <co_mutex> lk(g_cvMutex);
		//检测条件是否达成
		while(g_data_deque.empty())
			g_cv.Wait(g_cvMutex);
		//互斥操作，消息数据
		int data = g_data_deque.front();
		g_data_deque.pop_front();
		//std::cout << data << " deque size: " << g_data_deque.size() << std::endl;
		//唤醒其他线程
		g_cv.NotifyAll();
		//自动释放锁
	}
}
//条件变量使用方式和std中相同，CoConditionVariable需要一个co_mutex
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
	//等待所有的task退出
	while (co_sched.TaskCount() > 0)
	{
		usleep(1000 * 1000);
	}

	co_sched.Stop();
}
#endif
