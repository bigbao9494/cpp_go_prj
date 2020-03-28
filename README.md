介绍：

	cpp_go是用C/C++实现的类似golang的协程和网络库，让C/C++程序员可以轻松写出高并发的网络程序，
	可以用同步的方式(每个连接一个线程)写出异步的高性能，支持协程栈大小设置，每个协程可以
	根据业务逻辑的需求在创建协程时设置不同栈大小，可以指定任意多个CPU核心(物理线程)
	来执行协程，任务的调度和golang一样是动态调度而不像有些协程库简单地做平均分配，
	同时它会根据每个物理线程的实际cpu使用率来进行任务均衡，这才是最真实的均衡不会
	出现平均分配时有些核心上的协程由于业务的简单被很快地执行完了而其它核心上的协程
	却还在等待被执行。实现了shared_fd功能(通过创建协程指定参数来开启此功能，
	默认此功能是关闭的，不允许一个socket存在多个协程中，为了异步IO和动态调度时确保数据的完整性)，
	同一个socket可以同时存在于2个协程中，一个负责写一个负责读，在实际业务中会有这样的
	需求，比如在同一个socket上99%时间都在写偶尔也需要去读，像流媒体协议中rtmp连接就是大部分
	时间都是在向客户端转发数据但偶尔需要读取客户端状态数据，如果只在一个协程中处理这个
	连接的话代码实现起来就会比较奇怪且效率低下，每次写都要去做一次读或者间隔读，每次读感觉效率低下
	间隔读又不能及时响应对端数据，读写分开在2个协程中就能很好解决这样的问题，剩下的只是在这2个
	协程中进行同步和通信而已。同时实现了必要的同步和通信模块co_mutex、co_condition_variable、
	channel、co_timer、co_sleep这些最重要最基础的模块，也可以在这些基础模块上衍生出像
	golang中的RWMutex、channel_select等高级操作。另外还实现了task_cache功能，协程结束时对应的栈
	不释放而缓存起来供下次创建协程时使用这样对高频率创建协程的应用时性能更友好，
	不需要反复地向系统申请栈释放栈(编译时打开宏USE_TASK_CACHE)，协程函数兼容C程序员函数指针
	习惯的同时支持C++的函数对象。
使用特别注意：

	cpp_go实现的是静态栈，就是创建协程时就分配好栈大小，在这个协程结束前都不会改变的
	和golang的动态栈是不同的，原因是C/C++是会直接使用指针的所以不能做成动态栈，既然是
	静态栈就可能出现栈溢出问题，在创建协程前就需要确定最大栈大小，或者少使用大的栈变量。
参考其它库：

	代码实现时部分参考了libgo和boost.context，感谢libgo和boost。虽然参考了这两个开源但设计思想却是很
	大的不同，特别是和libgo其实有质的区别，因为libgo设计的初衷是为了解决把第3方同步方式的库转换到协程
	中来使用，去HOOK了socket相关的API，这样一个复杂的需求可能导致了设计上的复杂最终性能欠佳，而cpp_go
	的设计目的就是为了让C/C++程序员能够像golang那样写出高并发和高性能的网络程序，而不考虑第3方库的转换
	这样简洁的设计确实也达到了自己要的结果。
支持平台：

	linux_x64
	linux_arm32(理论上支持android_arm未测试过)
	windows_x64
	因为栈切换使用的是boost.context所以理论上boost.context支持的CPU架构cpp_go
	都支持(x86,arm64,mips,power_pc等等具体见源码中asm文件夹)，上面列出的3个平台
	只是我做开发时测试和使用过的而已，不过不同操作系统会使用特定的IO模型所以这点
	上看如果还要扩展到其它系统是需要适配的(macOS,unix等)，linux下使用EPOLL和SELECT
	windows下只实现了SELECT没有没有实现IOCP模型，如果需要支持更多IO模型只需要实现
	接口InnerIoWait即可。
性能测试：

	主要进行了ping_pong(对应测试代码在test_cpp_go/src/bench_pingpong.cpp)方式测试系统吞吐，
	因为没有10G/100G口的机器，所有的测试均是在本机回环下进行的
	比如4核心机器服务端使用2核心客户端使用2核心，16核心机器服务端使用8核心客户端使用8核心
	测试对比对象是ST库和muduo，ST是轻量协程实现，muduo是异步回调实现
	ST对比:
		因为ST只支持单线程，所以和ST也对比测试单线程，分别测试了10k和20k的连接数量，因为ST超过
		20k连接数量后出现异常所以未做更多连接测试，结论是ST总体高于cpp_go 4%左右吞吐，ST是C的
		轻量协程实现出现这样结果是预料中的
	muduo对比：
		多核心下分别测试了10k/20k/40k/60k数量的连接，因为muduo在更多连接时会出现异常退出
		所以更多连接未测试，大致结论是muduo总体高于cpp_go 12%左右吞吐，这样结果也是预料中的
		muduo效率非常高，它自己与其它异步网络库做过对比测试它明显好与其它库，所以这里就只和它
		对比就算是与其它库也对比了。
	测试结论分析:
		因为ST和muduo库的一些原因并未做更多连接的对比测试，但是做了cpp_go自己150k的pingpong连接数量测试
		这里测试了150k并不表示它只支持150k连接数量，因为做pingpong测试吞吐时越多的连接需要消耗
		越大的内存，在没有大内存情况下测试没有意义，如果不做吞吐带宽测试修改系统参数后它可以支持
		百万千万连接的。
	ST的4%差距是因为ST是C实现单线程的协程，所以ST会有更佳的执行效率这样的结果算是能接受，但并不
		表示cpp_go没有优化空间，只能说是它没有犯低级错误而已，其实还未刻意做过优化应该有进步的空间。
	muduo差距可以引出另外一个话题，异步回调和协程哪个性能好：
		协程虽然是用户态调度，实际上还是需要调度的，既然调度就会存在上下文切换，
		所以协程虽然比操作系统线程性能要好，但总还是有额外消耗的。而异步回调是没有切换开销的，
		它等同于顺序执行代码，所以异步回调程序的性能是要优于协程模型的。
		muduo效率更高个人分析其它原因：
			1、读写都有Buffer
			2、Buffer::readFd复用栈空间做readv操作减少系统调用次数，一次会读取更多数据
			3、读写直接回调，更少的代码执行量
			4、默认关注读事件，关注后不epoll_del/mod事件，不会反复地epoll_add/del
		muduo一个缺点：
			每个连接在生成时决定在哪个Eventloop中，不能动态变化，这样不能根据线程负载动态调度任务
		
编译方法：

	cd /home
	git clone https://github.com/bigbao9494/cpp_go_prj.git
	所有平台是同一套代码，不同平台会编译不同个别文件(主要是栈切换的asm文件)
	编译需要放到/home目录中(因为直接使用Makefile依赖了路径)，完整的目录结构如下：
		/home/cpp_go_prj/cpp_go/src           cpp_go.a源文件
		/home/cpp_go_prj/test_cpp_go/src      测试应用程序源文件
		
	cpp_go的所有makefile相关文件都和cpp_go/src同一目录
	test_cpp_go的所有makefile文件都和test_cpp_go/src同一目录
	---------------------------------linux_pc_x64---------------------------------------------
	cpp_go.a 编译PC_LINUX_debug:
		cd /home/cpp_go_prj/cpp_go
		make -f pc_debug_cpp_go.msbuild-mak
		生成文件在/home/cpp_go_prj/VisualGDB/Debug目录下
	cpp_go.a 编译PC_LINUX_release:
		cd /home/cpp_go_prj/cpp_go
		make -f pc_release_cpp_go.msbuild-mak
		生成文件在/home/cpp_go_prj/VisualGDB/Release目录下
	test_cpp_go 编译PC_LINUX_debug:
		cd /home/cpp_go_prj/test_cpp_go
		make -f pc_debug_test_cpp_go.msbuild-mak
		生成文件在/home/cpp_go_prj/VisualGDB/Debug目录下
	test_cpp_go 编译PC_LINUX_release:
		cd /home/cpp_go_prj/test_cpp_go
		make -f pc_release_test_cpp_go.msbuild-mak
		生成文件在/home/cpp_go_prj/VisualGDB/Release目录下
		
	-----------------------------------linux_arm32-------------------------------------------
	cpp_go.a 编译debug:
		cd /home/cpp_go_prj/cpp_go
		make -f arm_debug_Makefile
		生成文件在/home/cpp_go_prj/cpp_go/Debug目录下
	cpp_go.a 编译release:
		cd /home/cpp_go_prj/cpp_go
		make CONFIG=Release -f arm_release_Makefile
		生成文件在/home/cpp_go_prj/cpp_go/Release目录下
	test_cpp_go 编译debug:
		cd /home/cpp_go_prj/test_cpp_go
		make -f arm_debug_Makefile
		生成文件在/home/cpp_go_prj/test_cpp_go/Debug目录下
	test_cpp_go 编译release:
		cd /home/cpp_go_prj/test_cpp_go
		make CONFIG=Release -f arm_release_Makefile
		生成文件在/home/cpp_go_prj/test_cpp_go/Release目录下
	-----------------------------------windows_x64-------------------------------------------
	windows版本使用VS工程，没有做成CMAKE编译，所以单独上传一个完整的VS工程,连接https://github.com/bigbao9494/cpp_go_prj/commit/334cb52a105eeff347e919bcdebb2b4710212cae

示例代码(更多的测试在test_cpp_go/src中)：
```cpp
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
```
