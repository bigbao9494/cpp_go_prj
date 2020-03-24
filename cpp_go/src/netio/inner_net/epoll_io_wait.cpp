#include <signal.h>
#include <stdlib.h>
//#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <fcntl.h>
#include "epoll_io_wait.h"
#include "../../scheduler/scheduler.h"

/*
Linux Epoll模型中EPOLLIN和EPOLLOUT触发机制
	Epoll的ET模式是边沿触发模式，其中有两个事件：EPOLLOUT和EPOLLIN事件。
	EPOLLOUT事件:
	只有在连接是触发一次，表示该套接字进入可写状态。在下面的几种情况下也可以触发：
	1、某次write写操作，写满了缓冲区，返回错误码为EAGAIN。
	2、对端读取了一些数据，缓冲区又变为可写状态了，此时会触发EPOLLOUT事件。
	3、也可以通过epoll_ctl重新设置一下event，设置成与原来的触发状态一致即可，
	当然需要包含EPOLLOUT事件，设置后会立马触发一次EPOLLOUT事件。
	EPOLLIN事件:
	只有当该套接字对端的数据可读时才会触发，触发一次后需要不断读取对端的数据直到返回EAGAIN错误码为止。
关于epoll_ctl的线程安全：
	不同线程可以同时对epll_fd进行操作，多线程同时使用epoll_ctl操作一个epoll_fd进行ADD操作是没有问题的，
	因为epoll_ctl函数的添加行为是线程安全的，epoll_ctl操作一个epoll_fd进行DEL操作是非线程安全的，
	它只能在epll_wait所在线程被调用因为如果一个文件描述符正被监听，其他线程关闭了的话，表现是未定义的。
*/

#define POLL_EXCEPTION	(POLLERR | POLLHUP | POLLRDHUP | POLLNVAL)
namespace co
{
	int EpollIoWait::InitModule()
	{
		assert(POLLERR == EPOLLERR);
		assert(POLLHUP == EPOLLHUP);
		assert(POLLRDHUP == EPOLLRDHUP);
		//assert(POLLNVAL == EPOLLERR);
		assert(POLLIN == EPOLLIN);
		assert(POLLOUT == EPOLLOUT);
		assert(POLLPRI == EPOLLPRI);
		//printf("POLL_EXCEPTION: %x,%x-%x-%x-%x\n", POLL_EXCEPTION, POLLERR,POLLHUP,POLLRDHUP,POLLNVAL);
		printf("USE_EPOLL\n");
		InnerIoWait::InitModule();		
		int epoll_event_size = CoroutineOptions::getInstance().epoll_event_size;
		event_list.resize(epoll_event_size);		
		epoll_fd = epoll_create(epoll_event_size);
		assert(epoll_fd > 0);
		if(epoll_fd < 0)
			return -1;
		PollInterrupter();
		return 0;
	}
	int EpollIoWait::WaitLoop(int wait_milliseconds)
	{
		int nfd = 0;
	    nfd = epoll_wait(epoll_fd,event_list.data(),event_list.size(),wait_milliseconds);
		if(nfd > 0)
			{
				NetFD* net_fd = nullptr;
				//处理所有有事件的fd
				for(int i = 0;i < nfd;i++)
					{
						//有interrupter来的事件
						if (event_list[i].data.fd == interrupter_->notify_socket())
						{
							//printf("Interrupter\n");
							interrupter_->reset();
							continue;
						}

						//net_fd = fd_map[event_list[i].data.fd];
						auto itr = fd_map.find(event_list[i].data.fd);
						assert(itr != fd_map.end());
						uint32_t comeEvents = event_list[i].events;
						uint32_t modEvents = 0;
						//共享fd时这个fd上的一个事件到来时会把共享这个fd的所有task唤醒，这些task自己决定是否需要竞争
						while (itr != fd_map.end() && itr->first == event_list[i].data.fd)
						{
							net_fd = itr->second;
							assert(net_fd);
							//这个fd上所有task的请求事件(task1_POLLIN,task2_POLLOUT)
							modEvents = modEvents | net_fd->osfd.events;
							//当前发生的事件和所有的NetFD事件匹配,如果错误发生需要唤醒task
							if ((comeEvents & net_fd->osfd.events) || (comeEvents & POLL_EXCEPTION))
							{
								//返回fd对应的这个task上触发的事件: task关注事件 + 异常事件
								net_fd->osfd.revents = (net_fd->osfd.events & comeEvents) | (comeEvents & POLL_EXCEPTION);
								//把对应的task添加到运行队列
								RemoveNetFD2TaskRunnable(*net_fd);
								//map中删除这个task(itr++加1后返回加1前的值itr不会失效)
								fd_map.erase(itr++);
								//如果有定时器，则需要停止定时器，不然定时器的函数会被执行
								StopTimer(*net_fd);
								////wait到的事件被处理了
								//events = events & (~net_fd->osfd.events);
							}
							else
								itr++;
						}
						//assert(events == 0);//到来的事件都应该被处理,可能有错误发生这里就不为0

						//fd上是否还有需要监听的事件
						modEvents = modEvents & (~comeEvents);
						if (modEvents == 0)
						{
							//这个fd的所有监听事件都处理了把这个fd从epoll_fd中删除
							struct epoll_event ev;
							ev.events = modEvents;
							ev.data.fd = event_list[i].data.fd;
							if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL,event_list[i].data.fd, &ev) < 0)
							{
								printf("EPOLL_CTL_DEL error\n");
								assert(0);
							}
							//减少监听计数器
							evtlist_cnt--;
						}
						else
						{
							//这个fd的还有未触发的事件,只能修改
							struct epoll_event ev;
							ev.events = modEvents;
							ev.data.fd = event_list[i].data.fd;
							if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, event_list[i].data.fd, &ev) < 0)
							{
								printf("EPOLL_CTL_DEL error\n");
								assert(0);
							}
						}
					}
			}
		
		return nfd;
	}
	int EpollIoWait::PollsetAddHelp(NetFD& net_fd, int events)
	{
		//如果增加 |= EPOLLONESHOT，在PollsetDel是可以少一次系统调用
		net_fd.osfd.events = events;
		struct epoll_event ev;
		ev.events = events;
		ev.data.fd = net_fd.osfd.fd;
		if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, net_fd.osfd.fd, &ev) < 0)
		{
			printf("EPOLL_CTL_ADD error: %d\n", get_errno());
			return -1;
		}
		//需要监听的fd个数计数
		evtlist_cnt++;
		//ST的做法，监听fd数大于event_list，进行2倍扩展
		//这种做法当并发连接数很多时会让event_list变得巨大
		if (evtlist_cnt > event_list.size() && (evtlist_cnt * 2 <= rlim_max))
		{
			event_list.resize(evtlist_cnt * 2);
		}
		return 0;
	}
	int EpollIoWait::PollsetAdd(NetFD& net_fd, int events)
	{
		if (net_fd.osfd.fd < 0 || !events || (events & ~(POLLIN | POLLOUT | POLLPRI)))
		{
			set_errno(EINVAL);
			return -1;
		}
		events |= EPOLL_TRIGGERED;
		if (!net_fd.is_shared())
		{
			//不是共享fd直接添加
			return PollsetAddHelp(net_fd, events);
		}
		else
		{
			//共享fd需要判断是添加还是修改
			auto itr = fd_map.find(net_fd.osfd.fd);
			if (itr == fd_map.end())
			{
				//map中没有相同的fd表示这是第一次或者当前task和一起共享FD的其它task不在同一个线程中
				return PollsetAddHelp(net_fd, events);
			}
			else
			{
				//当前task和一起共享FD的其它task处于同一个线程中
				net_fd.osfd.events = events;
				//map中有相同的fd
				int oldEvents = 0;
				while (itr != fd_map.end() && itr->first == net_fd.osfd.fd)
				{
					//找到这个fd上正在监听的事件
					oldEvents = oldEvents | itr->second->osfd.events;
					itr++;
				}
				//需要添加的事件和已经存在的事件不同则修改
				if (oldEvents != events)
				{
					oldEvents |= events;
					PollsetModify(net_fd.osfd.fd, oldEvents);
				}
			}
		}

		return 0;
	}
#if 0
	void EpollIoWait::PollsetDel(NetFD& net_fd, int events)
	{
		if (!net_fd.osfd.events)
			return;
		
		struct epoll_event ev;
		ev.events = events;
		ev.data.fd = net_fd.osfd.fd;
		/*
		如果在PollsetAdd中使用EPOLLONESHOT，这里就可以少一次epoll_ctl的调用
		EPOLLONESHOT：只监听一次事件，当监听完这次事件之后，如果还需要继续监听这个socket的话，
		需要再次把这个socket加入到EPOLL队列里
		*/
		if(epoll_ctl(epoll_fd, EPOLL_CTL_DEL, net_fd.osfd.fd, &ev) < 0)
			{
				printf("EPOLL_CTL_DEL error\n");
				assert(0);
			}
		//减少监听计数器
		evtlist_cnt--;
		net_fd.osfd.events &= ~events;
	}
#endif
	void EpollIoWait::PollsetTimeoutDel(NetFD& net_fd)
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

		//fd上是否还有需要监听的事件
		if (modEvents == 0)
		{
			//这个fd的所有监听事件都处理了把这个fd从epoll_fd中删除
			struct epoll_event ev;
			ev.events = net_fd.osfd.events;
			ev.data.fd = net_fd.osfd.fd;
			if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, ev.data.fd, &ev) < 0)
			{
				printf("EPOLL_CTL_DEL error\n");
				assert(0);
			}
			//减少监听计数器
			evtlist_cnt--;
		}
		else
		{
			//这个fd的还有未触发的事件,只能修改
			struct epoll_event ev;
			ev.events = modEvents;
			ev.data.fd = net_fd.osfd.fd;
			if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, ev.data.fd, &ev) < 0)
			{
				printf("EPOLL_CTL_DEL error\n");
				assert(0);
			}
		}
	}
	void EpollIoWait::PollsetModify(int fd, int events)
	{
		assert(events > 0);
		struct epoll_event ev;
		ev.events = events;
		ev.data.fd = fd;
		if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev) < 0)
		{
			printf("EPOLL_CTL_MOD error\n");
			assert(0);
		}
	}
	int EpollIoWait::Poll(NetFD& net_fd, int events, int timeout)
	{
		assert(!net_fd.owner_task);
		CheckRepeatNetFD(net_fd);

		if (PollsetAdd(net_fd, events) < 0)
			return -1;
		//切换前必须要设置NetFD的task
		net_fd.owner_task = Processer::GetCurrentTask();
		assert(net_fd.owner_task);
		//存储等待事件的NetFD
		//fd_map[net_fd.osfd.fd] = &net_fd;
		fd_map.emplace(net_fd.osfd.fd, &net_fd);
		//需要超时退出
		if (timeout > 0)
		{
			CreateTimer(net_fd, events, timeout);
		}


		//切换task
		this->CoSwitch(net_fd.owner_task);

		//task切换回来时说明有事件或超时
		//不能清空revents，调用函数需要使用revents来处理错误
		////net_fd.osfd.revents = 0;

		//清除task引用
		net_fd.owner_task = nullptr;
		//判断是否是超时切换回来的
		if (timeout > 0 && net_fd.timer)
		{
			//超时后尽快释放timer
			net_fd.timer.reset();
			//超时设置errno并返回-1,用户需要通过errno值判断是否为超时
			set_errno(ETIME);
			return -1;
		}
		//处理bad_fd
		if (net_fd.osfd.revents & POLLNVAL)
		{
			set_errno(EBADF);
			return -1;
		}
		if ((net_fd.osfd.revents & POLL_EXCEPTION))
		{
			printf("%d-----error:%d-e2\n", net_fd.osfd.fd, (net_fd.osfd.revents & POLL_EXCEPTION));
			return -1;
		}

		return 0;
	}
	void EpollIoWait::PollInterrupter()
	{
		struct epoll_event ev;
		ev.events = POLLIN | EPOLL_TRIGGERED;
		ev.data.fd = interrupter_->notify_socket();
		if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, ev.data.fd, &ev) < 0)
		{
			printf("EPOLL_CTL_ADD error: %d\n", get_errno());
			return;
		}
	}
}
