#include "common/config.h"
#ifdef SYS_Linux
#include <sys/stat.h>
#include <time.h>
#include <signal.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include <fcntl.h>
#endif
#include <signal.h>
#include <fcntl.h>
#include <stdio.h>
#include <thread>
#include<errno.h>
#include <set>
#include "coroutine.h"
#include "netio/inner_net/net_fd.h"
using namespace co;

/*
ping_pong数据测试，包含服务端和客户端，输入不同参数运行服务器和客户端
用来测试库的极限带宽，测试时代码中每次收发数据的buffer大小会对结束有影响
在和其它库对比测试时需要用相同buffer大小

测试大连连接时需要修改系统参数：
	ulimit -n 100000 单进程最大文件描述符
	echo 1000000 > /proc/sys/fs/file-max 整个系统打开文件最大个数
	echo 1000000 > /proc/sys/fs/nr_open 单进程最大个数
	echo 99999 > /proc/sys/net/core/somaxconn 系统的最大全连接队列大小
	echo 99999 > /proc/sys/net/ipv4/tcp_max_syn_backlog 系统半连接队列大小
	echo 1 > /proc/sys/net/ipv4/tcp_tw_reuse 设为1复用TIME_WAIT状态socket
	echo "1024 62000" > /proc/sys/net/ipv4/ip_local_port_range 增大可用端口的范围(最大65535) 
	echo "45780 61042 9999999" > /proc/sys/net/ipv4/tcp_mem 协议栈使用内存大小单位是页
	cat /proc/net/sockstat 查看协议栈使用内存情况tcp_mem的值
*/
static volatile bool exit_flag = false;
static co_mutex connection_counter_mutx;
static volatile int connection_counter = 0;
static int clients_bytes[100000 + 2] = { 0 };//每个连接发送的字节总数
static int hold_counts = 0;
static void client_thread_fun(void* p)
{
	assert(p);
	NetFD* connect_fd = (NetFD*)p;
	connect_fd->set_tcp_no_delay(true);
	auto s = std::chrono::steady_clock::now();

	int n;
	char buff[1024 * 8];

	//每个协程做同样的事情，循环收发数据
	for (;;)
	{
		//memset(buff, 0x0, sizeof(buff));
		//接受客户端传过来的数据
		n = inner_recv(*connect_fd, buff, sizeof(buff), 0);
		if (n < 0)
		{
			//出错后退出协程
			printf("%d error: %d\n", connect_fd->get_fd(), get_errno());
			//assert(0);
			break;
		}
		else if (n == 0)
			break;

		//向客户端发送回应数据
		if (inner_send(*connect_fd, buff, n, 0) == -1)
		{
			printf("%d send error: %d\n", connect_fd->get_fd(), get_errno());
			//assert(0);
			break;
		}
	}
	//printf("task_count: %d,CacheTaskCount: %d\n", co_sched.TaskCount(), -1);
	printf("%d,task: %u,time: %ld\n", connect_fd->get_fd(), co_sched.GetCurrentTaskID(), std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - s).count());
	inner_close(*connect_fd);
	delete connect_fd;
}
static int pingpong_server()
{
	NetFD socket_fd, *connect_fd;
	struct sockaddr_in servaddr;
	//创建SOCKET
	socket_fd = inner_socket(AF_INET, SOCK_STREAM, 0);
	//初始化Socket
	if (!socket_fd.is_valid())
	{
		printf("inner_socket error: %d\n", errno);
		return -1;
	}
	//初始化
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(8000);
	//绑定本地端口
	if (inner_bind(socket_fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) == -1)
	{
		printf("inner_bind error: %d\n", errno);
		return -1;
	}
	//开始监听是否有客户端连接
	if (inner_listen(socket_fd, 2000) == -1)
	{
		printf("inner_listen error: %d\n", errno);
		return -1;
	}

	//显示FD个数
	//go[]{
	//	for (;;)
	//	{
	//		printf("GetEpollWaitCount: %d\n",co_debugger.GetEpollWaitCount());
	//		co_sleep(1000);
	//	}
	//};

	printf("======waiting for client's request======\n");
	while (1)
	{
		////测试客户端connet后服务端不accept
		//for (;;)
		//{
		//	co_sleep(1000);
		//	static int a = 0;
		//	if (a++ >= 1800)
		//	{
		//		break;
		//	}
		//}

		//循环等待客户端连接到来
		connect_fd = new NetFD;
		*connect_fd = inner_accept(socket_fd, (struct sockaddr*)NULL, NULL);
		if (!connect_fd->is_valid())
		{
			printf("accept socket (errno: %d)", get_errno());
			continue;
		}
		//printf("%x come in\n", connect_fd->get_fd());

		//每来一个连接创建一个协程来服务它，新的SOCKET通过参数connect_fd传递给新协程
		go1_stack(1024 * 32, client_thread_fun, connect_fd);
	}

	inner_close(socket_fd);
}
static int pingpong_server_hold()
{
	std::set<NetFD*> clients;
	NetFD socket_fd, *connect_fd;
	struct sockaddr_in servaddr;

	socket_fd = inner_socket(AF_INET, SOCK_STREAM, 0);
	//初始化Socket
	if (!socket_fd.is_valid())
	{
		printf("inner_socket error: %d\n", errno);
		return -1;
	}

	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(8000);

	if (inner_bind(socket_fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) == -1)
	{
		printf("inner_bind error: %d\n", errno);
		return -1;
	}
	//开始监听是否有客户端连接
	if (inner_listen(socket_fd, 2000) == -1)
	{
		printf("inner_listen error: %d\n", errno);
		return -1;
	}

	printf("======waiting for client's request======\n");
	while (!exit_flag)
	{
		while (!exit_flag)
		{
			connect_fd = new NetFD;
			*connect_fd = inner_accept(socket_fd, (struct sockaddr*)NULL, NULL);
			if (!connect_fd->is_valid())
			{
				printf("accept socket (errno: %d)", get_errno());
				continue;
			}
			printf("%x come in\n", connect_fd->get_fd());
			//只保存连接不收发数据
			clients.emplace(connect_fd);

			if (clients.size() >= hold_counts)
			{
				break;
			}
		}

		printf("%d connected\n", hold_counts);
		auto s = std::chrono::steady_clock::now();
		for (;;)
		{
			if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - s).count() >= 30)
				break;
			else
			{
				//printf("task_cout: %u\n", co_sched.TaskCount());
				co_sleep(1000);
			}
		}
		for (auto itr = clients.begin(); itr != clients.end(); itr++)
		{
			connect_fd = (NetFD*)*itr;
			inner_close(*connect_fd);
			delete connect_fd;
		}
		clients.clear();
		printf("%d client exit\n", hold_counts);
	}
	inner_close(socket_fd);
}
static void client_connect_fun(void* p)
{
	auto s = std::chrono::steady_clock::now();
	struct sockaddr_in    servaddr;
	NetFD sockfd = inner_socket(AF_INET, SOCK_STREAM, 0);
	if (!sockfd.is_valid())
	{
		printf("create socket error: %d\n", get_errno());
		return;
	}
	sockfd.set_tcp_no_delay(true);

	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(8000);
	if (inet_pton(AF_INET,(char*)p, &servaddr.sin_addr) <= 0) {
		printf("inet_pton error\n");
		inner_close(sockfd);
		return;
	}

	if (inner_connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0)
	{
		printf("connect error: %d\n", get_errno());
		inner_close(sockfd);
		return;
	}

	////连接成功后增加全局计数
	//connection_counter_mutx.lock();
	//connection_counter++;
	//connection_counter_mutx.unlock();

	bool first = true;
	int &bytes = clients_bytes[co_sched.GetCurrentTaskID()];
	int n;
	char buff[1024 * 8];
	while (!exit_flag)
	{
		//memset(buff, 0x0, sizeof(buff));
		//向服务端发送数据
		bytes += 4096;
		if (inner_send(sockfd, buff,4096, 0) == -1)
		{
			printf("%d send error\n", sockfd.get_fd());
			break;
		}

		//第一次发送数据成功后才增加计数，避免连接后一直没有被accept而计数却在inner_connect后增加了
		//connect返回表示服务器对应的这个socket已经进入内核的完成队列中，等待accept来取走。这时客户端已经可以向服务器发送数据了，数据是缓存在socket缓冲区中。
		//原理上理解，计数放到这里和connect后都是一样的
		if (first)
		{
			//连接成功后增加全局计数
			connection_counter_mutx.lock();
			connection_counter++;
			connection_counter_mutx.unlock();
			first = false;
		}

		//if (bytes >= sizeof(buff)*10)
		//{
		//	if ((sockfd.get_fd() % 2) == 0)
		//		break;
		//	//break;
		//}

		//printf("%s -> %d\n",__FUNCTION__,__LINE__);
		//接受服务端传过来的数据
		n = inner_recv(sockfd, buff, sizeof(buff), 0);
		bytes += n;
		if (n < 0)
		{
			printf("%d error: %d\n", sockfd.get_fd(), get_errno());
			break;
		}
		else if (n == 0)
			break;
	}
	//printf("task_count: %d,CacheTaskCount: %d\n", co_sched.TaskCount(), -1);
	//printf("%d,task: %u,time: %ld\n", sockfd.get_fd(), co_sched.GetCurrentTaskID(), std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - s).count());
	inner_close(sockfd);
}
static void client_connect_fun_hold(void* p)
{
	char* ip = (char*)p;
	auto s = std::chrono::steady_clock::now();
	struct sockaddr_in    servaddr;
	NetFD sockfd = inner_socket(AF_INET, SOCK_STREAM, 0);
	if (!sockfd.is_valid())
	{
		printf("create socket error: %d\n", get_errno());
		return;
	}
	sockfd.set_tcp_no_delay(true);

	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(8000);
	if (!ip)
	{
		ip = "127.0.0.1";
	}
	if (inet_pton(AF_INET, ip, &servaddr.sin_addr) <= 0) {
		printf("inet_pton error\n");
		inner_close(sockfd);
		return;
	}

	if (inner_connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0)
	{
		printf("connect error: %d\n", get_errno());
		inner_close(sockfd);
		return;
	}

	int n;
	char buff[1024 * 1];
	while (!exit_flag)
	{
		n = inner_recv(sockfd, buff, sizeof(buff), 0);
		if (n < 0)
		{
			printf("%d error: %d\n", sockfd.get_fd(), get_errno());
			break;
		}
		else if (n == 0)
			break;
	}
	inner_close(sockfd);
}
static void signal_handler(int sig_num)
{
	exit_flag = sig_num;
}
void test_pingpong(int argc, char *argv[])
{
	if (argc < 2)
	{
		/*
		test_pingpong 1 2			运行服务端使用2个物理线程
		test_pingpong 1 2 1000	60	运行服务端使用2个物理线程创建1000个客户端连接（协程数）收发数据60秒
		*/
		printf("test_pingpong type(1-server,2-client) thread(number) sessions(number) time(s)\n");
		return;
	}
	int tp = atoi(argv[1]);
	int thread = atoi(argv[2]);
	int times,sessions;
	char* connectIP = "127.0.0.1";
	if (tp == 2)
	{
		sessions = atoi(argv[3]);
		times = atoi(argv[4]);
		//无IP参数则是连接本机服务器
		if (argc == 6)
		{
			connectIP = argv[5];
		}
	}
	else if (tp == 11 || tp == 22)
	{
		hold_counts = atoi(argv[3]);
		if (tp == 22)
		{
			printf("ip: %s\n", argv[4]);
		}
	}

	signal(SIGTERM, signal_handler);
	signal(SIGINT, signal_handler);

	if (tp == 1)
	{
		//服务端程序
		go1_stack(1024 * 10, pingpong_server, nullptr);
		std::thread t([thread] { co_sched.Start(thread); });
		t.detach();
		while (!exit_flag)
		{
			usleep(1000 * 1000 * 1);
			printf("server_task_cout: %u\n", co_sched.TaskCount());
		}
		co_sched.Stop();
	}
	else if (tp == 2)
	{
		//创建N个客户端连接，每个连接一个协程
		assert(sizeof(clients_bytes) / sizeof(clients_bytes[0]) > sessions);
		for (int i = 0; i < sessions; i++)
		{
			go1_stack(1024 * 32, client_connect_fun, connectIP);
		}

		std::thread t([thread] { co_sched.Start(thread); });
		t.detach();
		auto s = std::chrono::steady_clock::now();
#if 1
		printf("waite all connect\n");
		//等待所有连接都建立
		while (!exit_flag && connection_counter != sessions)
		{
			usleep(1000 * 10);
		}
		printf("all connect: %d ms\n", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - s).count());
		//所有连接建立后重新计数，因为先建立的连接会先发数据导致统计过多
		memset(clients_bytes, 0x0, sizeof(clients_bytes));//不严谨仅测试
#endif
		s = std::chrono::steady_clock::now();
		auto s1 = std::chrono::steady_clock::now();
		int dt;
		while (!exit_flag)
		{
			usleep(1000 * 1000);
			printf("task_cout: %u\n", co_sched.TaskCount());

			//运行时间到，断开所有连接
			dt = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - s).count();
			if (dt >= times)
			{
				if (!exit_flag)
				{
					printf("exit time up\n");
					s1 = std::chrono::steady_clock::now();
				}
				exit_flag = true;
				//等待所有的task退出
				while (co_sched.TaskCount() > 0)
				{
					usleep(1000 * 1000);
				}
			}
		}
		//统计收发数据总数，计算带宽
		int64_t total = 0;
		for (int i = 0; i < sizeof(clients_bytes) / sizeof(clients_bytes[0]); i++)
		{
			total += clients_bytes[i];
		}
		printf("total_bytes: %lld, %.4f M/S,exit_time: %d ms\n", total, (double)total / (1024.f * 1024.f * dt),
			std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - s1).count());

		co_sched.Stop();
	}
	else if (tp == 11)
	{
#if 0
		go1_stack(1024 * 10, pingpong_server_hold, nullptr);
		std::thread t([thread] { co_sched.Start(thread); });
		t.detach();
		while (!exit_flag)
		{
			usleep(1000 * 1000 * 1);
			//printf("server_task_cout: %u\n", co_sched.TaskCount());
		}
		co_sched.Stop();
#endif
	}
	else if (tp == 22)
	{
#if 0
		for (int i = 0; i < hold_counts; i++)
		{
			go1_stack(1024 * 8, client_connect_fun_hold, argv[4]);
		}
		std::thread t([thread] { co_sched.Start(thread); });
		t.detach();

		while (!exit_flag)
		{
			usleep(1000 * 1000 * 1);
			printf("task_cout: %u\n", co_sched.TaskCount());
		}
		co_sched.Stop();
#endif
	}

}