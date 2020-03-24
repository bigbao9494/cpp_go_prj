#if __linux__
#include <sys/stat.h>
#include <time.h>
#include <signal.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include <fcntl.h>
#endif
#include "coroutine.h"
#include <stdio.h>
#include <thread>
#include<errno.h>
#include "netio/inner_net/net_fd.h"
using namespace co;

/*
2个协程共享SOCKET：
	在写网络程序时经常会遇到一种需求，同一个SOCKET对它写的同时还会进行读取，有时99%时间是写数据
	1%时间去读，如果使用的网络库要求你把代码实现成“每次写后都去try一次读”这样效率其实是低下的
	如果把读和写分开到不同协程，那就不会相互影响了，负责写数据的任务只管写，负责读数据的任务也
	只管读（没有数据时它会被调度），这样的业务逻辑写起来就很轻松了。
*/
#define MAXLINE (1024 + 1)
#define COUNTER	(500)
void test_share_fd1()
{
	for (int i = 0; i < 1; i++)
	{
		go[]{
			//创建SOCKET并连接服务器，对方服务器的行为是ping_pong，读到数据后把数据发送给客户端
			NetFD fd1 = inner_socket(AF_INET, SOCK_STREAM, 0,true);
			printf("%d\n",fd1.get_fd());

			char* pAddr = "192.168.100.19";
			int rec_len, send_len;
			char   sendline[MAXLINE];
			char    buf[MAXLINE];
			struct sockaddr_in    servaddr;
			memset(&servaddr, 0, sizeof(servaddr));
			servaddr.sin_family = AF_INET;
			servaddr.sin_port = htons(8000);
			if (inet_pton(AF_INET, pAddr, &servaddr.sin_addr) <= 0) {
				printf("inet_pton error for %s\n", pAddr);
				inner_close(fd1);
				return -1;//出错退出协程
				//exit(0);
			}
			if (inner_connect(fd1, (struct sockaddr*)&servaddr, sizeof(servaddr)/*,5000*/) < 0)
			{
				printf("connect error: %d\n", errno);
				inner_close(fd1);
				return -1;//出错退出协程
				//exit(0);
			}
			//////////////////////////////////////////////////////////////////////////
			#if 1
			//创建一个只读数据的任务
			go[fd1]{
				NetFD fd2 = fd1;
				char buf[MAXLINE];
				int rec_len;
				for (int counter = 0; counter <= COUNTER; counter++)
				{
					memset(buf, 0x0, sizeof(buf));
					if ((rec_len = inner_recv(fd2, buf, MAXLINE - 1, 0)) == -1)
					{
						printf("recv error\n");
						break;
						//exit(1);
					}
					else if (rec_len == 0)
					{
						printf("recv failed in: %d\n", counter);
					}
					else
					{
						//if (strlen(buf) < 4)
						//	printf("");
						//buf[rec_len] = '\0';
						printf("%d:%lld Received %d: %s\n", co_sched.GetCurrentProcesserID(), co_sched.GetCurrentTaskID(), rec_len, buf);
					}
				}
				printf("%d:%lld recved done\n", co_sched.GetCurrentProcesserID(), co_sched.GetCurrentTaskID());

				////co_sleep(500);
				while (1)
				{
					memset(buf, 0x0, sizeof(buf));
					if ((rec_len = inner_recv(fd2, buf, MAXLINE - 1, 0, 500)) > 0)
					{
						printf("%d:%lld Received %d: %s\n", co_sched.GetCurrentProcesserID(), co_sched.GetCurrentTaskID(), rec_len, buf);
					}
					else
						break;
				}
			};
			#endif
			//////////////////////////////////////////////////////////////////////////
			//go[fd1]{
			//	NetFD fd3 = fd1;
			//	char buf[MAXLINE];
			//	int rec_len;
			//	for (int counter = 0; counter <= 250; counter++)
			//	{
			//		memset(buf, 0x0, sizeof(buf));
			//		if ((rec_len = inner_recv(fd3, buf, MAXLINE - 1, 0)) == -1)
			//		{
			//			printf("recv error\n");
			//			break;
			//			//exit(1);
			//		}
			//		else if (rec_len == 0)
			//		{
			//			printf("recv failed in: %d\n", counter);
			//		}
			//		else
			//		{
			//			//if (strlen(buf) < 4)
			//			//	printf("");
			//			//buf[rec_len] = '\0';
			//			printf("%d:%lld Received %d: %s\n", co_sched.GetCurrentProcesserID(), co_sched.GetCurrentTaskID(), rec_len, buf);
			//		}
			//	}
			//	printf("%d:%lld recved done\n", co_sched.GetCurrentProcesserID(), co_sched.GetCurrentTaskID());
			//};
			//////////////////////////////////////////////////////////////////////////

			//创建一个只写数据的任务
			for (int counter = 0; counter <= COUNTER; counter++)
			{
				//memset(sendline, 0x0, sizeof(sendline));
				snprintf(sendline, sizeof(sendline), "%lld -> %d ",co_sched.GetCurrentTaskID(), counter);
				//printf("client send: %s\n",sendline);
				//send_len = inner_send(fd1, sendline, sizeof(sendline) - 1, 0);
				send_len = st_write(fd1, sendline, sizeof(sendline) - 1);
				if (send_len < 0)
				{
					printf("inner_send error: %d\n", errno);
					break;
					//exit(0);
				}
				else if (send_len == 0)
				{
					printf("inner_send failed in: %d\n", counter);
				}
				else
				{
					////buf[rec_len] = '\0';
					printf("%d:%lld inner_send : %s \n", co_sched.GetCurrentProcesserID(), co_sched.GetCurrentTaskID(),sendline);
				}
				//co_sleep(50);

				#if 0
				memset(buf, 0x0, sizeof(buf));
				if ((rec_len = inner_recv(fd1, buf, MAXLINE - 1, 0)) == -1)
				{
					printf("recv error\n");
					break;
					//exit(1);
				}
				else if (rec_len == 0)
				{
					printf("recv failed in: %d\n", counter);
				}
				else
				{
					////buf[rec_len] = '\0';
					//printf("Received : %s \n", buf);
				}
				#endif
			}
			printf("send done\n");

			//printf("close: %x\n", fd1.get_fd());
			//inner_close(fd1);
		};
	}

	co_sched.Start(2);
}