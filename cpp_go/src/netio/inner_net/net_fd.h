#pragma once
#include "common/config.h"
#if defined(SYS_Unix)
#include <unistd.h>
#endif

#include <vector>
#include <list>
#include <set>

#if defined(SYS_Windows)
//放到config.h中
//#undef FD_SETSIZE
//#define FD_SETSIZE	(1024) //必须在WinSock2.h前添加否则不生效
//#include <WinSock2.h>
//#include <Windows.h>
#include<ws2tcpip.h>
#elif (SYS_Unix)
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#endif

//前向声明
namespace co
{
	struct Timer;
	struct Task;
	typedef std::shared_ptr<Timer> TimerID;
}

namespace co
{
	struct Timer;
	struct Task;

	int get_errno();
	void set_errno(int err);

	class EpollIoWait;
	class SelectIoWait;
	class InnerIoWait;
	//内部对socket的包装类
	class NetFD
	{
		friend class InnerIoWait;
		friend class EpollIoWait;
		friend class SelectIoWait;
		friend bool inner_setnonblocking(NetFD& sockfd, bool is_nonblocking);
		friend NetFD inner_socket(int domain, int type, int protocol, bool share);
		friend int inner_connect(NetFD& sockfd, const struct sockaddr* addr, socklen_t addrlen, int timeout);
		friend int inner_bind(NetFD& sockfd, const struct sockaddr* addr, socklen_t addrlen);
		friend int inner_listen(NetFD& sockfd, int backlog);
		friend NetFD inner_accept(NetFD& sockfd, struct sockaddr* addr, socklen_t* addrlen, int timeout);
		friend int inner_recv(NetFD& sockfd, void *buf, size_t count, int flags, int timeout);
		friend int inner_send(NetFD& sockfd, const void *buf, size_t count, int flags, int timeout);
		friend int inner_close(NetFD& sockfd);
		friend int inner_sendto(NetFD& sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen, int timeout);
		friend int inner_recvfrom(NetFD& sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen, int timeout);
		//////////////////////////////////////////////////////////////////////////
		friend ssize_t st_readv(NetFD& sockfd, const struct iovec *iov, int iov_size, int timeout);
		friend int st_read(NetFD& sockfd, void *buf, size_t count,int timeout);
		friend int st_readv_resid(NetFD& sockfd, struct iovec **iov, int *iov_size, int timeout);
		friend ssize_t st_writev(NetFD& sockfd, const struct iovec *iov, int iov_size, int timeout);
		friend int st_write(NetFD& sockfd, const void *buf, size_t count, int timeout);
		friend int st_writev_resid(NetFD& sockfd, struct iovec **iov, int *iov_size, int timeout);
		friend int st_write_resid(NetFD& sockfd, const void *buf, size_t *resid, int timeout);
	public:
		/*
		share表示是否可以被多个协程共享:
		共享FD典型应用是同一个FD存在于2个NetFD中(即被2个task使用)，这2个NetFD可能在不同Processor中执行
		可以NetFD1进行读操作，NetFD2进行写操作
		不可以NetFD1和NetFD2都进行读或都进行写操作(这样行为是未定义的)
		*/
		NetFD(bool share = false);
		~NetFD();
		//赋值函数决定socket是否可以在task间复制
		NetFD& operator=(const NetFD& fd);
		//拷贝构造函数决定socket是否可以在task间复制
		NetFD(const NetFD& rfd);
		//判断是否有效
		bool is_valid();
		//得到fd的值
		int get_fd();
		//是否是共享的
		bool is_shared();
		void set_tcp_no_delay(bool on);
	private:
		//socket_fd
		pollfd osfd;
		//并且一个task在一次只会等待在一个FD上，因为没有事件时它会被切换出运行队列而得不到执行
		//task在被执行时说明FD上有事件或FD等待超时
		Task* owner_task;
		//超时用定时器，它是shared_ptr不用考虑复制问题
		TimerID timer;
		std::shared_ptr<int>* share_fd = nullptr;
	private:
		static void close_share_fd(int* fd);
	};

	/*
	所有带超时的函数调用int timeout = INNER_UTIME_NO_TIMEOUT的返回值:
	0：对端关闭连接
	>0：收发数据大小
	-1：网络出错或超时，如果用户使用了超时值，返回-1后需要调用get_errno() == ETIME判断是超时还是错误
	*/

	//调用系统socket创建一个socket并使用NetFD来包装
	NetFD inner_socket(int domain, int type, int protocol,bool share = false);
	//相同于系统connect，传递socket为NetFD类型,timeout超时毫秒
	int inner_connect(NetFD& sockfd, const struct sockaddr* addr,socklen_t addrlen,int timeout = INNER_UTIME_NO_TIMEOUT);
	int inner_bind(NetFD& sockfd, const struct sockaddr* addr, socklen_t addrlen);
	int inner_listen(NetFD& sockfd, int backlog);
	NetFD inner_accept(NetFD& sockfd, struct sockaddr* addr, socklen_t* addrlen,int timeout = INNER_UTIME_NO_TIMEOUT);
	//收到数据就返回，或出错返回
	int inner_recv(NetFD& sockfd, void *buf, size_t count,int flags,int timeout = INNER_UTIME_NO_TIMEOUT);
	//把缓冲区数据发送完成才退出，或出错退出
	int inner_send(NetFD& sockfd, const void *buf, size_t count,int flags,int timeout = INNER_UTIME_NO_TIMEOUT);
	int inner_close(NetFD& sockfd);
	//发送一次就返回，或出错返回，可能只发送部分数据
	int inner_sendto(NetFD& sockfd,const void *buf, size_t len, int flags,
		const struct sockaddr *dest_addr, socklen_t addrlen, int timeout = INNER_UTIME_NO_TIMEOUT);
	//收到数据就返回，或出错返回
	int inner_recvfrom(NetFD& sockfd, void *buf, size_t len, int flags,
		struct sockaddr *src_addr, socklen_t *addrlen, int timeout = INNER_UTIME_NO_TIMEOUT);
	//////////////////////////////////////////////////////////////////////////
	/*
	与ST的差异：处理timeout == 0时直接在函数中返回
	*/
	ssize_t st_readv(NetFD& sockfd, const struct iovec *iov, int iov_size, int timeout = INNER_UTIME_NO_TIMEOUT);
	int st_read(NetFD& sockfd, void *buf, size_t count,int timeout = INNER_UTIME_NO_TIMEOUT);
	int st_readv_resid(NetFD& sockfd, struct iovec **iov, int *iov_size, int timeout = INNER_UTIME_NO_TIMEOUT);
	ssize_t st_writev(NetFD& sockfd, const struct iovec *iov, int iov_size, int timeout = INNER_UTIME_NO_TIMEOUT);
	int st_write(NetFD& sockfd, const void *buf, size_t count,int timeout = INNER_UTIME_NO_TIMEOUT);
	int st_writev_resid(NetFD& sockfd, struct iovec **iov, int *iov_size, int timeout = INNER_UTIME_NO_TIMEOUT);
	int st_write_resid(NetFD& sockfd, const void *buf, size_t *resid, int timeout = INNER_UTIME_NO_TIMEOUT);
}
