#pragma once
#include "common/config.h"
#if defined(SYS_Unix)
#include <unistd.h>
#endif
#include <vector>
#include <list>
#include <map>
#include <unordered_map>
#include "task/task.h"
#include "net_fd.h"

using namespace std;

#define _LOCAL_MAXIOV  16
#if defined(SYS_Windows)
#include <io.h>
#include <stdlib.h>
#include <stdio.h>
#define close_socket(s)	closesocket(s)
typedef int	socklen_t;
#elif defined(SYS_Unix)
#include <netinet/in.h>
#include <netinet/tcp.h>
#define close_socket(s)	close(s)
typedef int SOCKET;
#endif

namespace co
{
	bool set_no_blocking(SOCKET sockfd, bool is_nonblocking);
	extern int rlim_max;
	//基类，不同网络模型继承IoWait
    class InnerIoWait
    {
	private:
		class Interrupter
		{
			SOCKET writer_;
			SOCKET reader_;
			SOCKET accepter_;
		public:
			Interrupter();
			~Interrupter();

			void interrupter();

			void reset();

			SOCKET notify_socket();
		};

    public:
		static int inner_io_init();
    	InnerIoWait(){}
		virtual ~InnerIoWait();
		//初始化模型
		virtual int InitModule();
		// 在协程中调用的switch, 暂存状态并yield
		void CoSwitch(Task* tk);
		// 在调度器中调用的switch, 如果成功则进入等待队列，如果失败则重新加回runnable队列
		void SchedulerSwitch(Task* tk);
		//事件循环
		virtual int WaitLoop(int wait_milliseconds) = 0;
		//同步socket操作转换成异步poll/select/epoll
		//现在的设计是:每次进入poll会PollsetAdd，退出poll时会PollsetDel
		//这样是会造成频繁地事件add_del操作，比如每次recv(fd)失败后都会进行一次add_del
		//没有“缓存事件”功能
		virtual int Poll(NetFD& net_fd,int events,int timeout) = 0;
		//得到在io_waite的个数
		std::size_t GetFDMapSize();
		//唤醒线程
		void InterrupteWaitLoop() { interrupter_->interrupter();};
	protected:
		static bool IgnoreSigPipe();

		//添加fd事件监听
		virtual int PollsetAdd(NetFD& net_fd,int events) = 0;
		//删除事件监听
		virtual void PollsetDel(NetFD& net_fd, int events) {};
		//把NetFD从map中删除并把对应的task添加到运行队列中去
		void RemoveNetFD2TaskRunnable(NetFD& net_fd,bool remove = true);
		//创建定时器
		void CreateTimer(NetFD& net_fd, int events, int timeout);
		//停止定时器
		void StopTimer(NetFD& net_fd);
		//task超时后修改监听
		virtual void PollsetTimeoutDel(NetFD& net_fd) = 0;
		/*
		一个NetFD只能存在于一个task中:
		1、设计不允许同一个NetFD对象同时出现在fd_map中，因为这样会造成只有最后一个使用NetFD对象的task会有效
		还可能出现事件混乱，比如第1个task关心POLLOUT第2个task关注POLLIN，当POLLOUT事件到来时无法唤醒第1
		个task(net_fd.owner_task被覆盖了),却把第2个task唤醒了(fd上关注事件=POLLOUT|POLLIN)，但第2个task并没有关注POLLOUT
		*/
		inline void CheckRepeatNetFD(NetFD& net_fd)
		{
			if (net_fd.owner_task)
			{
				printf("fatal error: can not use the same NetFD!! fd: %d,addr: %p\n", net_fd.get_fd(),&net_fd);
				exit(0);
			}
		}
	protected:
		//typedef unordered_map<int64_t, NetFD*> FD_MAP;
		//支持共享fd必须使用multimap
		typedef multimap<int64_t, NetFD*> FD_MAP;
		//fd索引的NetFD
		FD_MAP fd_map;
		//唤醒阻塞在WaitLoop函数中的线程
		Interrupter* interrupter_ = nullptr;
    };

} //namespace co
