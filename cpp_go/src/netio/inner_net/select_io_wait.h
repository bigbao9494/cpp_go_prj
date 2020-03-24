#pragma once
#include "inner_io_wait.h"

namespace co
{
	//select模型实现
	class SelectIoWait : public InnerIoWait
	{
	public:
		SelectIoWait() {}
		~SelectIoWait() {}
		virtual int InitModule();
		virtual int WaitLoop(int wait_milliseconds);
		virtual int Poll(NetFD& net_fd, int events, int timeout);
	private:
		virtual int PollsetAdd(NetFD& net_fd, int events);
		virtual void PollsetDel(NetFD& net_fd, int events);
		virtual void PollsetTimeoutDel(NetFD& net_fd);
	private:
		fd_set fd_read_set;
		fd_set fd_write_set;
		fd_set fd_exception_set;
		SOCKET maxfd;
#if defined(SYS_Windows)
		//connect server时，windows下select时如果所有fd_set都为空则会立即返回-1，起不到超时的作用
		//没有找到这个问题的原因，通过创建一个无效socket来避免
		//SOCKET tmp_sock;
#endif
	};
}
