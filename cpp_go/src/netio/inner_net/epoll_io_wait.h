#pragma once
#include <sys/epoll.h>
#include <vector>
#include "inner_io_wait.h"
//EPOLL触发方式
#define EPOLL_TRIGGERED		(EPOLLET)

namespace co
{	
	class EpollIoWait : public InnerIoWait
	{
	public:
		EpollIoWait() {}
		~EpollIoWait() {}
		virtual int InitModule();
		virtual int WaitLoop(int wait_milliseconds);
		virtual int Poll(NetFD& net_fd, int events, int timeout);
	private:
		virtual int PollsetAdd(NetFD& net_fd, int events);
		virtual void PollsetTimeoutDel(NetFD& net_fd);
		//修改事件监听
		void PollsetModify(int fd, int events);
		int PollsetAddHelp(NetFD& net_fd, int events);
		//把interrupter添加到epoll_fd中，这里只需要调用一次，在有事件的时间不做EPOLL_CTL_DEL操作
		void PollInterrupter();
	private:
		int epoll_fd = -1;
		//EPOLL_CTL_ADD到epoll_fd的计数
		int evtlist_cnt = 0;
		//epoll_event* event_list = nullptr;
		std::vector<epoll_event> event_list;
	};
}
