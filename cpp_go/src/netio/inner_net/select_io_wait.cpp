#include <signal.h>
#include "select_io_wait.h"
#include "scheduler/scheduler.h"

namespace co
{
	int SelectIoWait::InitModule()
	{
		printf("USE_SELECT\n");
		InnerIoWait::InitModule();
		maxfd = 0;
		FD_ZERO(&fd_read_set);
		FD_ZERO(&fd_write_set);
		FD_ZERO(&fd_exception_set);
		//把接收通知的fd添加到read_set中
		FD_SET(interrupter_->notify_socket(), &fd_read_set);
		if (maxfd < interrupter_->notify_socket())
			maxfd = interrupter_->notify_socket();

#if defined(SYS_Windows)
		//tmp_sock = socket(AF_INET, SOCK_STREAM, 0);
		//FD_SET(tmp_sock, &fd_exception_set);
#endif
		return 0;
	}
	int SelectIoWait::WaitLoop(int wait_milliseconds)
	{
		struct timeval timeout,*tvp;
		if (wait_milliseconds == -1)
			tvp = nullptr;
		else
		{
			timeout.tv_sec = (wait_milliseconds / 1000);
			timeout.tv_usec = (wait_milliseconds % 1000) * 1000;
			tvp = &timeout;
		}

		int nfd;
		fd_set read_set = fd_read_set;
		fd_set write_set = fd_write_set;
		fd_set exception_set = fd_exception_set;

		nfd = select((int)maxfd + 1,&read_set,&write_set,&exception_set,tvp);
		//有事件发生
		if (nfd > 0)
		{
			int events;
			SOCKET osfd;
			FD_MAP::iterator itr_tmp;

			//共享fd时这个fd上的一个事件到来时会把共享这个fd的所有task唤醒，这些task自己决定是否需要竞争
			//判断fd_map中的哪些有事件到来
			for (FD_MAP::iterator itr = fd_map.begin();itr != fd_map.end();)
			{
				events = itr->second->osfd.events;
				osfd = itr->second->osfd.fd;
				if ((events & POLLIN) && FD_ISSET(osfd,&read_set))
				{
					itr->second->osfd.revents |= POLLIN;
				}
				if ((events & POLLOUT) && FD_ISSET(osfd, &write_set))
				{
					itr->second->osfd.revents |= POLLOUT;
				}
				if ((events & POLLPRI) && FD_ISSET(osfd, &exception_set))
				{
					itr->second->osfd.revents |= POLLPRI;
				}
				//确认有事件到来
				if (itr->second->osfd.revents && (itr->second->osfd.revents & events) )
				{
					//先移动到下个itr位置
					itr_tmp = itr;
					itr++;

					NetFD* netfd = (NetFD*)itr_tmp->second;
					//task状态==io_block表示是poll中的task切换
					//if (netfd->owner_task->state_ == TaskState::io_block)
					//移除有事件的NetFD，并把它对应的task加入运行队列
					fd_map.erase(itr_tmp);
					/*
					如果共享fd，如果多个task相关同一个事件(比如2个task都read)
					当这个事件到来时对同一个fd会多次调用PollsetDel，对同个fd
					多次进行FD_CLR操作没有影响
					*/
					PollsetDel(*netfd, netfd->osfd.revents);
					RemoveNetFD2TaskRunnable(*netfd, false);
					//如果有定时器，则需要停止定时器，不然定时器的函数会被执行
					StopTimer(*netfd);
				}
				else
					itr++;
			}
			//有interrupter来的事件
			if (FD_ISSET(interrupter_->notify_socket(), &read_set))
			{
				//printf("Interrupter\n");
				interrupter_->reset();
			}
		}
		else if (nfd < 0)
		{
			int err = get_errno();
			if (err == EBADF)
				assert(0);
			printf("select error: %d\n", err);
		}

		return nfd;
	}
	int SelectIoWait::PollsetAdd(NetFD& net_fd, int events)
	{
		assert(!net_fd.owner_task);
		CheckRepeatNetFD(net_fd);

		if (net_fd.osfd.fd < 0 || !events || (events & ~(POLLIN | POLLOUT | POLLPRI)))
		{
			set_errno(EINVAL);
			return -1;
		}
#if defined(SYS_Unix)
		//而linux的FD_SETSIZE指的是最大文件描述符,如果轮询的描述符大于等于该值,可能就出问题
		if(net_fd.osfd.fd >= FD_SETSIZE)
		{
			set_errno(EINVAL);
			return -1;
		}
#endif
#if defined(SYS_Windows)
		//windows下的FD_SETSIZE指最大可以轮询的个数,
		if (fd_map.size() >= FD_SETSIZE)
		{
			set_errno(EINVAL);
			return -1;
		}
#endif
		net_fd.osfd.events |= events;

		if (net_fd.osfd.events & POLLIN) 
		{
			FD_SET(net_fd.osfd.fd, &fd_read_set);
		}
		if (net_fd.osfd.events & POLLOUT)
		{
			FD_SET(net_fd.osfd.fd, &fd_write_set);
		}
		if (net_fd.osfd.events & POLLPRI)
		{
			FD_SET(net_fd.osfd.fd, &fd_exception_set);
		}

		if (maxfd < net_fd.osfd.fd)
			maxfd = net_fd.osfd.fd;

		return 0;
	}
	void SelectIoWait::PollsetDel(NetFD& net_fd, int events)
	{
		if (!net_fd.osfd.events)
			return;
		//events == 0表示是超时的调用，需要把fd从所有SET中移除
		if (events & POLLIN || !events)
		{
			FD_CLR(net_fd.osfd.fd, &fd_read_set);
		}
		if (events & POLLOUT || !events)
		{
			FD_CLR(net_fd.osfd.fd, &fd_write_set);
		}
		//if (!events)
		//{
		//	FD_CLR(net_fd.osfd.fd, &fd_exception_set);
		//}

#if defined(SYS_Windows)
		//windows下这种情况表示是在进行connect操作
		//连接失败只收到POLLPRI事件，连接成功只收到POLLOUT事件
		if (net_fd.osfd.revents & (POLLPRI | POLLOUT))
		{
			FD_CLR(net_fd.osfd.fd, &fd_write_set);
			//连接成功时需要主动清除POLLPRI，不然空闲时select会出错
			FD_CLR(net_fd.osfd.fd, &fd_exception_set);
			net_fd.osfd.events &= ~(POLLPRI | POLLOUT);
		}
		if (events & POLLPRI)
		{
			FD_CLR(net_fd.osfd.fd, &fd_exception_set);
		}
#else
		if (events & POLLPRI)
		{
			FD_CLR(net_fd.osfd.fd, &fd_exception_set);
		}
#endif
		net_fd.osfd.events &= ~events;
	}
	void SelectIoWait::PollsetTimeoutDel(NetFD& net_fd)
	{
		auto itr = fd_map.find(net_fd.osfd.fd);
		assert(itr != fd_map.end());
		uint32_t modEvents = 0;
		NetFD* netFound = nullptr;
		while (itr != fd_map.end() && itr->first == net_fd.osfd.fd)
		{
			netFound = itr->second;
			assert(netFound);
			if (netFound == &net_fd)
			{
				//map中删除这个task(itr++加1后返回加1前的值itr不会失效)
				fd_map.erase(itr++);
				continue;
			}
			else
				itr++;
			//找到除net_fd以外的其它NetFD在这个fd上所有的请求事件
			modEvents = modEvents | netFound->osfd.events;
		}
		/*
		fd上是否还有超时task相关的事件
		如果还有表示共享fd中还有其它task在关注这个事件，不能从fd_set中清除
		如果没有表示无task在关注这个fd，需要从fd_set中清除
		*/
		if ((modEvents & net_fd.osfd.events) == 0)
		{
			PollsetDel(net_fd, net_fd.osfd.events);
		}
	}
	int SelectIoWait::Poll(NetFD& net_fd, int events, int timeout)
	{
		if (PollsetAdd(net_fd, events) < 0)
			return -1;
		//切换前必须要设置NetFD的task
		net_fd.owner_task = Processer::GetCurrentTask();
		assert(net_fd.owner_task);
		//存储等待事件的NetFD
		//fd_map[net_fd.osfd.fd] = &net_fd;
		fd_map.emplace(std::make_pair(net_fd.osfd.fd, &net_fd));
		//需要超时退出
		if (timeout > 0)
		{
			CreateTimer(net_fd, events, timeout);
		}

		//切换task
		this->CoSwitch(net_fd.owner_task);

		//task切换回来时说明有事件或超时
		//清空已到来的事件，因为调用函数inner_send、inner_recv等未处理revents所以这里统一处理
		//如果调用函数需要使用revents就不能在这里处理
		net_fd.osfd.revents = 0;

		//判断是否是超时切换回来的
		if (timeout > 0 && net_fd.timer)
		{
			//超时后尽快释放timer
			net_fd.timer.reset();
			//超时设置errno并返回-1,用户需要通过errno值判断是否为超时
			set_errno(ETIME);
			return -1;
		}

		//清除task引用
		net_fd.owner_task = nullptr;

		return 0;
	}
}
