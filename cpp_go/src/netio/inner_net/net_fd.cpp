#include "net_fd.h"
#include "inner_io_wait.h"
#include "scheduler/scheduler.h"

#if EAGAIN != EWOULDBLOCK
#define _IO_NOT_READY_ERROR(err)  ((err == EAGAIN) || (err == EWOULDBLOCK))
#else
#define _IO_NOT_READY_ERROR(err)  (err == EAGAIN)
#endif

namespace co
{
	NetFD::NetFD(bool share) :owner_task(nullptr)
	{
		osfd.fd = -1;
		osfd.events = osfd.revents = 0;
		//共享fd需要用户显示指定
		if (share)
		{
			share_fd = new std::shared_ptr<int>(new int(-1), close_share_fd);
		}
	}
	bool NetFD::is_valid()
	{
		return !(osfd.fd < 0);
	}
	int NetFD::get_fd()
	{
		return osfd.fd;
	}
	NetFD& NetFD::operator=(const NetFD& rfd)
	{
		//printf("%s -> %d\n", __FUNCTION__, __LINE__);
		if (!rfd.share_fd)
		{
			//如果socket已经在一个协程中使用，不能复制
			if (rfd.owner_task && rfd.owner_task != Processer::GetCurrentTask())
			{
				this->osfd.fd = -1;
				printf("NetFD can't be share in task\n");
				assert(0);
				return *this;
			}
			this->osfd = rfd.osfd;
			this->owner_task = rfd.owner_task;
			//this->timer = rfd.timer;
			//this->timer_id = rfd.timer_id;
		}
		else
		{
			this->osfd = rfd.osfd;
			//共享产生的新对象暂时无owner_task,因为这时它还没有被使用
			this->owner_task = nullptr;
			if (!share_fd)
			{
				share_fd = new std::shared_ptr<int>;
				*share_fd = *rfd.share_fd;
			}
			else
				*share_fd = *rfd.share_fd;
		}
		return *this;
	}
	NetFD::NetFD(const NetFD& rfd)
	{
		//printf("%s -> %d\n", __FUNCTION__, __LINE__);
		if (!rfd.share_fd)
		{
			//如果socket已经在一个协程中使用，不能复制
			if (rfd.owner_task && rfd.owner_task != Processer::GetCurrentTask())
			{
				this->osfd.fd = -1;
				printf("NetFD can't be share in task\n");
				assert(0);
			}
			this->osfd = rfd.osfd;
			this->owner_task = rfd.owner_task;
			//this->timer = rfd.timer;
			//this->timer_id = rfd.timer_id;
		}
		else
		{
			this->osfd = rfd.osfd;
			//共享产生的新对象暂时无owner_task,因为这时它还没有被使用
			this->owner_task = nullptr;
			if (!share_fd)
			{
				share_fd = new std::shared_ptr<int>;
				*share_fd = *rfd.share_fd;
			}
			else
				*share_fd = *rfd.share_fd;
		}
	}
	NetFD::~NetFD()
	{
		if (share_fd)
			delete share_fd;
	}
	bool NetFD::is_shared()
	{
		return !(share_fd == nullptr);
	}
	void NetFD::close_share_fd(int* fd)
	{
		printf("close_share_fd: %d in: task_%u\n", *fd, g_Scheduler.GetCurrentTaskID());
		close_socket(*fd);
		delete fd;
	}
	void NetFD::set_tcp_no_delay(bool on)
	{
		int optval = on ? 1 : 0;
		::setsockopt(osfd.fd, IPPROTO_TCP, TCP_NODELAY,(const char*)&optval, static_cast<socklen_t>(sizeof optval));
	}

	int get_errno()
	{
#if defined(SYS_Windows)
	#if 0
		int winErr = GetLastError();
		// Convert to a POSIX errorcode. The *major* assumption is that
		// the meaning of these codes is 1-1 and each Winsock, etc, etc
		// function is equivalent in errors to the POSIX standard. This is 
		// a big assumption, but the server only checks for a small subset of
		// the real errors, on only a small number of functions, so this is probably ok.
		switch (winErr)
		{
		case ERROR_FILE_NOT_FOUND: return ENOENT;
		case ERROR_PATH_NOT_FOUND: return ENOENT;
		case WSAEINTR:      return EINTR;
		case WSAENETRESET:  return EPIPE;
		case WSAENOTCONN:   return ENOTCONN;
		case WSAEWOULDBLOCK:return EAGAIN;
		case WSAECONNRESET: return EPIPE;
		case WSAEADDRINUSE: return EADDRINUSE;
		case WSAEMFILE:     return EMFILE;
		case WSAEINPROGRESS:return EINPROGRESS;
		case WSAEADDRNOTAVAIL: return EADDRNOTAVAIL;
		case WSAECONNABORTED: return EPIPE;
		case WSAEBADF:		return EBADF;
		case WSAENOTSOCK:	return ENOTSOCK;
		case 0:             return 0;
		default:            return ENOTCONN;
		}
	#else
		int syserr = GetLastError();
		if (syserr < WSABASEERR)
			return(syserr);
		//printf("win socket error:%d", syserr);
		switch (syserr)
		{
		case WSAEINTR:   syserr = EINTR;
			break;
		case WSAEBADF:   syserr = EBADF;
			break;
		case WSAEACCES:  syserr = EACCES;
			break;
		case WSAEFAULT:  syserr = EFAULT;
			break;
		case WSAEINVAL:  syserr = EINVAL;
			break;
		case WSAEMFILE:  syserr = EMFILE;
			break;
		case WSAEWOULDBLOCK:  syserr = EAGAIN;
			break;
		case WSAEINPROGRESS:  syserr = EINTR;
			break;
		case WSAEALREADY:  syserr = EINTR;
			break;
		case WSAENOTSOCK:  syserr = ENOTSOCK;
			break;
		case WSAEDESTADDRREQ: syserr = EDESTADDRREQ;
			break;
		case WSAEMSGSIZE: syserr = EMSGSIZE;
			break;
		case WSAEPROTOTYPE: syserr = EPROTOTYPE;
			break;
		case WSAENOPROTOOPT: syserr = ENOPROTOOPT;
			break;
		case WSAEOPNOTSUPP: syserr = EOPNOTSUPP;
			break;
		case WSAEADDRINUSE: syserr = EADDRINUSE;
			break;
		case WSAEADDRNOTAVAIL: syserr = EADDRNOTAVAIL;
			break;
		case WSAECONNABORTED: syserr = ECONNABORTED;
			break;
		case WSAECONNRESET: syserr = ECONNRESET;
			break;
		case WSAEISCONN: syserr = EISCONN;
			break;
		case WSAENOTCONN: syserr = ENOTCONN;
			break;
		case WSAETIMEDOUT: syserr = ETIMEDOUT;
			break;
		case WSAECONNREFUSED: syserr = ECONNREFUSED;
			break;
		case WSAEHOSTUNREACH: syserr = EHOSTUNREACH;
			break;
		}
		return syserr;
	#endif
#else
		return errno;//errno是线程安全的
#endif
	}
	void set_errno(int err)
	{
#if defined(SYS_Windows)
		SetLastError(err);
#else
		errno = err;
#endif
	}
	bool inner_setnonblocking(NetFD& sockfd, bool is_nonblocking)
	{
		return set_no_blocking(sockfd.osfd.fd, is_nonblocking);
	}
	NetFD inner_socket(int domain, int type, int protocol, bool share)
	{
		NetFD fd(share);

		Task* current_task = Processer::GetCurrentTask();
		//只能在工作协程中创建socket，主协程不允许创建
		if (!current_task || current_task->IsMainTask())
		{
			fd.osfd.fd = -1;
			printf("create socket must in Task\n");
			assert(0);
			return fd;
		}

		fd.osfd.fd = socket(AF_INET, type, protocol);
		if (fd.osfd.fd < 0)
		{
			printf("socket error\n");
			assert(0);
			fd.osfd.fd = -1;
			return fd;
		}
		//设置非阻塞
		if (!inner_setnonblocking(fd, true))
			inner_close(fd);

		//共享fd需要设置share_fd中的fd这样才会在所有的NetFd结束时真正地关闭fd
		if (share)
			**fd.share_fd = fd.osfd.fd;

		return fd;
	}
	int inner_close(NetFD& sockfd)
	{
		//非共享fd直接关闭
		if (!sockfd.share_fd)
		{
			#if 1
			//直接close由用户业务层来保证数据的完整,详见《unix网络编程卷3》P150/175
			close_socket(sockfd.osfd.fd);
			sockfd.osfd.fd = -1;
			#else
			//保存写缓冲中的数据发送到对端，但是读缓冲是否收完取决用户inner_close前的recv逻辑
			if (shutdown(sockfd.osfd.fd,SHUT_WR))
			{
				printf("shutdown error: %d\n", get_errno());
				close_socket(sockfd.osfd.fd);
			}
			else
			{
				//read() == 0 表示对端已经关闭它的写(自己的读),不需要再去收
				close_socket(sockfd.osfd.fd);
			}
			#endif
		}
		return 0;
	}
	int inner_connect(NetFD& sockfd, const struct sockaddr* addr, socklen_t addrlen, int timeout)
	{
		int err = 0;
		socklen_t n;

		while (connect(sockfd.osfd.fd, addr, addrlen) < 0)
		{
			//timeout == 0表示立即返回
			if (timeout == 0)
				return -1;
			int err_no = get_errno();
			if (err_no != EINTR)
			{
				/*
				* On some platforms, if connect() is interrupted (errno == EINTR)
				* after the kernel binds the socket, a subsequent connect()
				* attempt will fail with errno == EADDRINUSE.  Ignore EADDRINUSE
				* iff connect() was previously interrupted.  See Rich Stevens'
				* "UNIX Network Programming," Vol. 1, 2nd edition, p. 413
				* ("Interrupted connect").
				*/
#if defined(SYS_Windows)
				if (err_no != EAGAIN)
					return -1;
#else
				if (err_no != EINPROGRESS && (err_no != EADDRINUSE || err == 0))
					return -1;
#endif
				//如果连接成功触发写，如果连接失败，会触发读写事件
				//这里只关心写事件，后面通过SO_ERROR来判断错误发生
				//这个和《UNIX网络编程：卷1》的描述一致，在Linux下测试也正常
				//但事实是在Windows下，非阻塞连接失败后，读写事件都没有触发
				//windows处理和Linux不一样,对于非阻塞连接失败，触发的是异常事件
				int events = POLLOUT;
#if defined(SYS_Windows)
				//windows下连接失败有机会触发事件
				events |= POLLPRI;
#endif
				/* Wait until the socket becomes writable */
				if ((Processer::GetIoWait())->Poll(sockfd, events, timeout) < 0)
					return -1;

				/* Try to find out whether the connection setup succeeded or failed */
				n = sizeof(int);
				if (getsockopt(sockfd.osfd.fd, SOL_SOCKET, SO_ERROR, (char *)&err, &n) < 0)
					return -1;
				if (err)
				{
					set_errno(err);
					return -1;
				}
				break;
			}
			err = 1;
		}

		return 0;
	}
	int inner_recv(NetFD& sockfd, void *buf, size_t count, int flags, int timeout)
	{
		int n;
#if defined(SYS_Windows)
		while ((n = recv(sockfd.osfd.fd, (char*)buf, count, flags)) < 0)
#else
		while ((n = recv(sockfd.osfd.fd, buf, count, flags)) < 0)
#endif
		{
			int err_no = get_errno();
			if (err_no == EINTR)
				continue;
			//出错返回
			if (!_IO_NOT_READY_ERROR(err_no))
			{
				printf("_IO_NOT_READY_ERROR error\n");
				return -1;
			}
			else
			{
				//timeout == 0表示立即返回
				if (timeout == 0)
					return 0;
			}
			//Wait until the socket becomes readable 
			if (Processer::GetIoWait()->Poll(sockfd, POLLIN, timeout) < 0)
			{
				printf("Poll error\n");
				return -1;
			}
		}
		return n;
	}
	int inner_send(NetFD& sockfd, const void *buf, size_t count, int flags, int timeout)
	{
		int n = 0;
		int offset = 0;

		for (;;)
		{
#if defined(SYS_Windows)
			n = send(sockfd.osfd.fd, (const char*)buf + offset, count, flags);
#else
			n = send(sockfd.osfd.fd, buf, count, flags);
#endif

			if (n < 0)
			{
				int err_no = get_errno();
				if (err_no == EINTR)
					continue;
				//出错返回
				if (!_IO_NOT_READY_ERROR(err_no))
					return -1;
				//timeout == 0表示立即返回
				if (timeout == 0)
					return n;
			}
			else
			{
				//发送缓冲区偏移
				offset += n;
				//timeout == 0表示立即返回
				if (timeout == 0)
					return n;
				////所有数据发送完成
				//if(offset == count)
				//	return offset;
				//剩余多少未发送
				count -= n;
				//所有数据发送完成
				if (count <= 0)
					return offset;
			}
			//Wait until the socket becomes writable
			if (Processer::GetIoWait()->Poll(sockfd, POLLOUT, timeout) < 0)
				return -1;
		}

		return -1;
	}
	int inner_sendto(NetFD& sockfd, const void *buf, size_t len, int flags,
		const struct sockaddr *dest_addr, socklen_t addrlen, int timeout)
	{
		int n;

#if defined(SYS_Windows)
		while ((n = sendto(sockfd.osfd.fd, (const char*)buf, len, flags, dest_addr, addrlen)) < 0)
#else
		while ((n = sendto(sockfd.osfd.fd, buf, len, flags, dest_addr, addrlen)) < 0)
#endif
		{
			int err_no = get_errno();
			if (err_no == EINTR)
				continue;
			//出错返回
			if (!_IO_NOT_READY_ERROR(err_no))
				return -1;

			//timeout == 0表示立即返回
			if (timeout == 0)
				return n;

			//Wait until the socket becomes writable
			if (Processer::GetIoWait()->Poll(sockfd, POLLOUT, timeout) < 0)
				return -1;
		}

		return n;
	}
	int inner_recvfrom(NetFD& sockfd, void *buf, size_t len, int flags,
		struct sockaddr *src_addr, socklen_t *addrlen, int timeout)
	{
		int n;

#if defined(SYS_Windows)
		while ((n = recvfrom(sockfd.osfd.fd, (char*)buf, len, flags, src_addr, addrlen)) < 0)
#else
		while ((n = recvfrom(sockfd.osfd.fd, buf, len, flags, src_addr, addrlen)) < 0)
#endif
		{
			int err_no = get_errno();
			if (err_no == EINTR)
				continue;
			//出错返回
			if (!_IO_NOT_READY_ERROR(err_no))
				return -1;
			//timeout == 0表示立即返回
			if (timeout == 0)
				return n;

			//Wait until the socket becomes writable
			if (Processer::GetIoWait()->Poll(sockfd, POLLIN, timeout) < 0)
				return -1;
		}

		return n;
	}
	int inner_listen(NetFD& sockfd, int backlog)
	{
		return listen(sockfd.osfd.fd, backlog);
	}
	int inner_bind(NetFD& sockfd, const struct sockaddr* addr, socklen_t addrlen)
	{
		return ::bind(sockfd.osfd.fd, addr, addrlen);
	}
	NetFD inner_accept(NetFD& sockfd, struct sockaddr* addr, socklen_t* addrlen, int timeout)
	{
		int osfd;
		NetFD clientfd;

		clientfd.osfd.fd = -1;
		while ((osfd = accept(sockfd.osfd.fd, addr, addrlen)) < 0)
		{
			int err_no = get_errno();
			//printf("accept_err: %d\n", err_no);
			assert(err_no == EAGAIN);
			if (err_no == EINTR)
				continue;
			//出错返回
			if (!_IO_NOT_READY_ERROR(err_no))
				return clientfd;

			//timeout == 0表示立即返回
			if (timeout == 0)
				return clientfd;

			//Wait until the socket becomes readable
			if (Processer::GetIoWait()->Poll(sockfd, POLLIN, timeout) < 0)
				return clientfd;
		}
		clientfd.osfd.fd = osfd;
		//设置非阻塞
		if (!inner_setnonblocking(clientfd, true))
			inner_close(clientfd);

		return clientfd;
	}
	////////////////////////////////////////////////////////////////////////////////////////////////////
	ssize_t st_readv(NetFD& sockfd, const struct iovec *iov, int iov_size, int timeout)
	{
		ssize_t n;

		while ((n = readv(sockfd.osfd.fd, iov, iov_size)) < 0)
		{
			if (errno == EINTR)
				continue;
			int err_no = get_errno();
			//出错返回
			if (!_IO_NOT_READY_ERROR(err_no))
				return -1;
			else
			{
				//timeout == 0表示立即返回
				if (timeout == 0)
					return 0;
			}

			/* Wait until the socket becomes readable */
			if (Processer::GetIoWait()->Poll(sockfd, POLLIN, timeout) < 0)
				return -1;
		}

		return n;
	}
	int st_read(NetFD& sockfd, void * buf, size_t count, int timeout)
	{
		//inner_recv对timeout的处理与ST有点不同
		return inner_recv(sockfd, buf, count, 0, timeout);
	}
	int st_readv_resid(NetFD& sockfd, struct iovec **iov, int *iov_size, int timeout)
	{
		ssize_t n;

		while (*iov_size > 0)
		{
			//只有一个iov直接读数据到它缓冲区数据中
			if (*iov_size == 1)
				//注意: windows下使用stdio.h中read
				n = read(sockfd.osfd.fd, (*iov)->iov_base, (*iov)->iov_len);
			else//有多个iov时调用readv
				n = readv(sockfd.osfd.fd, *iov, *iov_size);
			if (n < 0)
			{
				int err_no = get_errno();
				if (err_no == EINTR)
					continue;
				if (!_IO_NOT_READY_ERROR(err_no))
					return -1;
				//timeout == 0表示立即返回
				if (timeout == 0)
					return 0;
			}
			else if (n == 0)
				break;
			else
			{
				//读取的数据总大小n是到哪个iov处结束
				while ((size_t)n >= (*iov)->iov_len)
				{
					n -= (*iov)->iov_len;
					(*iov)->iov_base = (char *)(*iov)->iov_base + (*iov)->iov_len;
					(*iov)->iov_len = 0;
					(*iov)++;
					(*iov_size)--;
					//找到结束位置
					if (n == 0)
						break;
				}
				//所有数据读完
				if (*iov_size == 0)
					break;
				//还有未读完的数据，准备好iov
				(*iov)->iov_base = (char *)(*iov)->iov_base + n;
				(*iov)->iov_len -= n;
			}
			/* Wait until the socket becomes readable */
			if (Processer::GetIoWait()->Poll(sockfd, POLLIN, timeout) < 0)
				return -1;
		}

		return 0;
	}
	int st_write(NetFD& sockfd, const void *buf, size_t count,int timeout)
	{
		#if 0
		//inner_recv对timeout的处理与ST有点不同
		return inner_send(sockfd, buf, count, 0, timeout);
		#else
		size_t resid = count;
		//返回成功发送的数据大小
		return st_write_resid(sockfd, buf, &resid, timeout) == 0 ? (ssize_t)(count - resid) : -1;
		#endif
	}
	ssize_t st_writev(NetFD& sockfd, const struct iovec *iov, int iov_size, int timeout)
	{
		ssize_t n, rv;
		size_t nleft, nbyte;
		int index, iov_cnt;
		struct iovec *tmp_iov;
		struct iovec local_iov[_LOCAL_MAXIOV];

		//计算要发送的总大小
		nbyte = 0;
		for (index = 0; index < iov_size; index++)
			nbyte += iov[index].iov_len;

		rv = (ssize_t)nbyte;
		nleft = nbyte;
		//保证不会修改iov
		tmp_iov = (struct iovec *) iov;
		iov_cnt = iov_size;

		while (nleft > 0) 
		{
			if (iov_cnt == 1) 
			{
				//只有一个iov直接发送它的缓冲区数据后退出循环
				if (st_write(sockfd, tmp_iov[0].iov_base, nleft, timeout) != (ssize_t)nleft)
					rv = -1;
				break;
			}
			//有多个iov时调用writev
			if ((n = writev(sockfd.osfd.fd, tmp_iov, iov_cnt)) < 0)
			{
				int err_no = get_errno();
				if (err_no == EINTR)
					continue;
				if (!_IO_NOT_READY_ERROR(err_no))
				{
					rv = -1;
					break;
				}
				//timeout == 0表示立即返回
				if (timeout == 0)
				{
					rv = 0;
					break;
				}
			}
			else 
			{
				//所有数据发送完退出循环
				if ((size_t)n == nleft)
					break;
				nleft -= n;
				//找到下一个没有发送的iov
				n = (ssize_t)(nbyte - nleft);
				for (index = 0; (size_t)n >= iov[index].iov_len; index++)
					n -= iov[index].iov_len;
				//如果是第1次执行到这里，确保tmp_iov指向的空间足够保存所有未发送的iov
				if (tmp_iov == iov) 
				{
					//栈中的local_iov空间足够，使用local_iov
					if (iov_size - index <= _LOCAL_MAXIOV) 
					{
						tmp_iov = local_iov;
					}
					else 
					{
						//不够空间动态分配
						tmp_iov = (struct iovec*)calloc(1, (iov_size - index) * sizeof(struct iovec));
						if (tmp_iov == NULL)
							return -1;
					}
				}

				//复制第1个未完成的iov的数据
				tmp_iov[0].iov_base = &(((char *)iov[index].iov_base)[n]);
				tmp_iov[0].iov_len = iov[index].iov_len - n;
				index++;
				//复制其它的iov数据
				for (iov_cnt = 1; index < iov_size; iov_cnt++, index++) 
				{
					tmp_iov[iov_cnt].iov_base = iov[index].iov_base;
					tmp_iov[iov_cnt].iov_len = iov[index].iov_len;
				}
			}
			/* Wait until the socket becomes writable */
			if (Processer::GetIoWait()->Poll(sockfd, POLLOUT, timeout) < 0)
			{
				rv = -1;
				break;
			}
		}

		if (tmp_iov != iov && tmp_iov != local_iov)
			free(tmp_iov);

		return rv;
	}
	int st_writev_resid(NetFD& sockfd, struct iovec **iov, int *iov_size, int timeout)
	{
		ssize_t n;

		while (*iov_size > 0) 
		{
			//只有一个iov直接发送它的缓冲区数据
			if (*iov_size == 1)
				//注意: windows下使用stdio.h中write
				n = write(sockfd.osfd.fd, (*iov)->iov_base, (*iov)->iov_len);
			else//多个iov一起发送
				n = writev(sockfd.osfd.fd, *iov, *iov_size);
			if (n < 0) 
			{
				int err_no = get_errno();
				if (err_no == EINTR)
					continue;
				if (!_IO_NOT_READY_ERROR(err_no))
					return -1;
				//timeout == 0表示立即返回
				if (timeout == 0)
					return 0;
			}
			else
			{
				//发送的数据总大小n是到哪个iov处结束
				while ((size_t)n >= (*iov)->iov_len) 
				{
					n -= (*iov)->iov_len;
					(*iov)->iov_base = (char *)(*iov)->iov_base + (*iov)->iov_len;
					(*iov)->iov_len = 0;
					(*iov)++;
					(*iov_size)--;
					//找到n结束的那个iov
					if (n == 0)
						break;
				}
				//所有的iov都发送完，退出最外面循环，退出函数
				if (*iov_size == 0)
					break;
				//还有iov数据未成功发送
				(*iov)->iov_base = (char *)(*iov)->iov_base + n;
				(*iov)->iov_len -= n;
			}
			/* Wait until the socket becomes writable */
			if (Processer::GetIoWait()->Poll(sockfd, POLLOUT, timeout) < 0)
				return -1;
		}

		return 0;
	}
	int st_write_resid(NetFD& sockfd, const void *buf, size_t *resid, int timeout)
	{
		struct iovec iov, *riov;
		int riov_size, rv;

		iov.iov_base = (void *)buf;	    /* we promise not to modify buf */
		iov.iov_len = *resid;
		riov = &iov;
		riov_size = 1;
		rv = st_writev_resid(sockfd, &riov, &riov_size, timeout);
		//未发送字节数
		*resid = iov.iov_len;
		return rv;
	}
}
