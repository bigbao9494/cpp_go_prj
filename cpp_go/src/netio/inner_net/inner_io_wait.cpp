#include "common/config.h"
#if defined(SYS_Unix)
#include <sys/stat.h>
#include <time.h>
#include <signal.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include <fcntl.h>
#endif
#include <algorithm>
#include "inner_io_wait.h"
#include "../../common/error.h"
#include "../../scheduler/scheduler.h"
#if defined(SYS_Windows)
#pragma comment(lib, "WS2_32")
#else
#include <sys/resource.h>
#endif

namespace co 
{
	bool set_no_blocking(SOCKET sockfd, bool is_nonblocking)
	{
#if defined(SYS_Windows)
		unsigned long flags = is_nonblocking ? 1 : 0;
		if (ioctlsocket(sockfd, FIONBIO, &flags) < 0)
		{
			printf("set noblock error: %d\n", get_errno());
			return false;
		}
		return true;
#else
		int flags = is_nonblocking ? 1 : 0;
		if (ioctl(sockfd, FIONBIO, &flags) == -1)
			return false;
		if ((flags = fcntl(sockfd, F_GETFL, 0)) < 0 ||
			fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) < 0)
		{
			printf("set noblock error: %d\n", get_errno());
			return false;
		}
		return true;
#endif
	}
	//fd的限制个数，之所以要放在这里是因为InnerIoWait是thread_local的
	//而rlim_max是只需要设置一次就可以了，如果在InnerIoWait中会每个线程都设置
	int rlim_max = 0;
	int InnerIoWait::inner_io_init()
	{
#if defined(SYS_Unix)
			if (rlim_max == 0)
			{
				struct rlimit rlim;
				if (getrlimit(RLIMIT_NOFILE, &rlim) < 0)
					return -1;

				if (FD_LIMIT_SIZE == 0)
				{
					//进程自己不限制，由硬件和/proc/sys/fs/file-max来限制
					rlim_max = rlim.rlim_cur = rlim.rlim_max;
				}
				else//进程自己限制
					rlim_max = rlim.rlim_cur = rlim.rlim_max = FD_LIMIT_SIZE;
				if (setrlimit(RLIMIT_NOFILE, &rlim) < 0)
				{
					printf("rlim error\n");
					return -1;
				}
				printf("rlim: %lu,%lu\n", rlim.rlim_cur, rlim.rlim_max);
			}
#endif
			//忽略SIGPIPE信号
			IgnoreSigPipe();
			return 0;
		}

	InnerIoWait::~InnerIoWait()
	{
		delete interrupter_;
		interrupter_ = nullptr;
#if defined(SYS_Windows)
		WSACleanup();
#endif
	}
	int InnerIoWait::InitModule()
	{
#if defined(SYS_Windows)
		//每个线程都做一次初始化
		{WSADATA data;	WSAStartup(MAKEWORD(2, 0), &data); }
#endif
		interrupter_ = new Interrupter();
		return 0;
	}
	bool InnerIoWait::IgnoreSigPipe()
	{
#if defined(SYS_Unix)
		DebugPrint(dbg_ioblock, "Ignore signal SIGPIPE");
		/* Ignore SIGPIPE */
		struct sigaction sigact;
		sigact.sa_handler = SIG_IGN;
		sigemptyset(&sigact.sa_mask);
		sigact.sa_flags = 0;
		if (sigaction(SIGPIPE, &sigact, NULL) < 0)
			return false;
		return true;
#elif defined(SYS_Windows)
		return true;
#else
		return false;
#endif
	}
	void InnerIoWait::SchedulerSwitch(Task* tk)
	{
	}
	void InnerIoWait::CoSwitch(Task* tk)
	{
		//把自己从Processer::runnableQueue_中移除，并设置状态为block
		tk->proc_->SuspendBySelfIO(tk);

		DebugPrint(dbg_ioblock, "task(%s) enter io_block", tk->DebugInfo());
		Processer::StaticCoYield();
	}
	void InnerIoWait::RemoveNetFD2TaskRunnable(NetFD& net_fd, bool remove)
	{
		net_fd.owner_task->state_ = TaskState::runnable;//设置运行状态
		//把task加入运行队列，让它从Poll返回
		net_fd.owner_task->proc_->WakeupBySelfIO(net_fd.owner_task);
	}
	void InnerIoWait::CreateTimer(NetFD& net_fd, int events, int timeout)
	{
		//g_Scheduler.CreateMilliTimer
		net_fd.timer = Processer::GetCurrentScheduler()->CreateMilliTimer(
			timeout,
			[this,&net_fd](void* userParam = nullptr) {//可以&net_fd引用捕获，因为net_fd的生命周期是有效的
			//移除有事件的NetFD，并把它对应的task加入运行队列,超时osfd.revents==0
			//net_fd.osfd.revents = 0;
			PollsetTimeoutDel(net_fd);
			RemoveNetFD2TaskRunnable(net_fd);
		});
	}
	void InnerIoWait::StopTimer(NetFD& net_fd)
	{
		if(net_fd.timer)
		{
			Processer::GetCurrentScheduler()->StopTimer(net_fd.timer);
			net_fd.timer.reset();
		}
	}
	std::size_t InnerIoWait::GetFDMapSize()
	{
		return fd_map.size();
	}
	InnerIoWait::Interrupter::Interrupter()
	{
		SOCKET accepter_ = socket(AF_INET, SOCK_STREAM, 0);
		assert(accepter_ != -1);

		sockaddr_in addr;
		socklen_t addrLen = sizeof(addr);
		addr.sin_family = AF_INET;
		addr.sin_port = 0;
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		int res = ::bind(accepter_, (const sockaddr*)&addr, sizeof(addr));
		(void)res;
		assert(res == 0);

		res = getsockname(accepter_, (sockaddr*)&addr, &addrLen);
		assert(res == 0);

		if (addr.sin_addr.s_addr == htonl(INADDR_ANY))
			addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

		listen(accepter_, 1);

		reader_ = socket(AF_INET, SOCK_STREAM, 0);
		assert(reader_ != -1);

		res = connect(reader_, (const sockaddr*)&addr, addrLen);
		assert(res == 0);

		writer_ = accept(accepter_, (sockaddr*)&addr, &addrLen);
		assert(writer_ != -1);

		bool bRet = set_no_blocking(writer_, true);
		assert(bRet);
		bRet = set_no_blocking(reader_, true);
		assert(bRet);

		close_socket(accepter_);
		accepter_ = -1;
	}
	InnerIoWait::Interrupter::~Interrupter()
	{
		close_socket(accepter_);
		close_socket(writer_);
		close_socket(reader_);
	}
	void InnerIoWait::Interrupter::interrupter()
	{
		int ret = send(writer_, "A", 1, 0);
		assert(ret > 0);
	}
	void InnerIoWait::Interrupter::reset()
	{
		char buf[1024];
		while (recv(reader_, buf, sizeof(buf), 0) < sizeof(buf));
	}
	SOCKET InnerIoWait::Interrupter::notify_socket()
	{
		return reader_;
	}

#if 0
	/*
	2、函数返回key==net_fd.osfd.fd的第1项的位置，同时判断是否有value重复的项，如果有则打印错误后结束程序
	*/
	InnerIoWait::FD_MAP::iterator InnerIoWait::CheckRepeatNetFD(NetFD& net_fd)
	{
		auto itr = fd_map.find(net_fd.osfd.fd);
		auto ret = itr;

		NetFD* netFound = nullptr;
		while (itr != fd_map.end() && itr->first == net_fd.osfd.fd)
		{
			netFound = itr->second;
			if (netFound == &net_fd)
			{
				printf("fatal error: can not use the same NetFD!! fd: %d,addr: 0x%x\n",net_fd.get_fd(),&net_fd);
				exit(0);
			}
		}

		//返回第1个位置
		return ret;
	}
#endif
} //namespace co
