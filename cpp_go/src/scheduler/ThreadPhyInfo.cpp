#include<stdlib.h>
#include<string.h>
#include <string>
#include<fcntl.h>
#include<ctype.h>
#include <sys/types.h>
#include <errno.h>
#include <vector>
#include <assert.h>
#include <sys/stat.h>
#include "ThreadPhyInfo.h"
#if defined(SYS_Unix)
#include <sys/syscall.h>
#include <dirent.h>
#endif
#if defined(SYS_Windows)
//#include <WinSock2.h>
//#include <windows.h>
#endif

//using namespace std;


void InterfaceThreadsPhyInfo::AddThreadTid(int index, std::thread* thd)
{
	TidInfo tmp;
#if defined(SYS_Unix)
	tmp.tid_ = syscall(SYS_gettid);
#endif
#if defined(SYS_Windows)
	if (thd)
		tmp.handle_ = thd->native_handle();
	else
	{
		//主线程所在的Processer没有thread对象
		HANDLE h = 0x0;
		//将伪句柄转换成真正的句柄
		DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &h, 0, FALSE, DUPLICATE_SAME_ACCESS);
		tmp.handle_ = h;
	}
#endif
	tids_.push_back(tmp);
	//tids_[index] = tmp;
}
float InterfaceThreadsPhyInfo::operator [](int index)
{
	assert(index < tids_.size());
	if (index >= tids_.size())
		return 0.0f;

	return tids_[index].cpuUseAge_;
}
#if defined(SYS_Windows)
bool ThreadsPhyInfo::StartTrack()
{
	//单线程无需计算
	if (tids_.size() == 1)
		return false;
	if (secondTidTimes_.size() < tids_.size())
	{
		secondTidTimes_.reserve(tids_.size());
		secondTidTimes_.clear();
		secondTidTimes_.insert(secondTidTimes_.begin(), tids_.size(), 0);

		firstTidTimes_.reserve(tids_.size());
		firstTidTimes_.clear();
		firstTidTimes_.insert(firstTidTimes_.begin(), tids_.size(), 0);
	}

	uint64_t cycleTime = 0;
	for (int i = 0; i < tids_.size(); i++)
	{
		//得到每个线程的cycle_time
		QueryThreadCycleTime(tids_[i].handle_, &cycleTime);
		firstTidTimes_[i] = (int64_t)cycleTime;
		assert(firstTidTimes_[i] == cycleTime);
	}
	return true;
}
bool ThreadsPhyInfo::StopTrack()
{
	//单线程无需计算
	if (tids_.size() == 1)
		return false;
	if (secondTidTimes_.size() < tids_.size() || firstTidTimes_.size() < tids_.size())
		return false;

	int64_t total = 0;
	uint64_t cycleTime = 0;
	for (int i = 0; i < tids_.size(); i++)
	{
		//得到每个线程的cycle_time
		QueryThreadCycleTime(tids_[i].handle_, &cycleTime);
		secondTidTimes_[i] = (int64_t)((int64_t)cycleTime - firstTidTimes_[i]);
		//assert(secondTidTimes_[i] >= 0);
		//cycle_time差值总和 
		total += secondTidTimes_[i];
		//重置结果为0，表示无需调度 
		tids_[i].cpuUseAge_ = 0.0f;
	}
	assert(total > 0);
	if (total == 0)
	{
		PRINT("QueryThreadCycleTime failed ????????????\n");
		return false;
	}

	//平均值 
	int64_t avg = total / tids_.size();
	//临界值 
	int64_t critical = (int64_t)(avg * taskLoadage_);
	//计算最终结果 
	int i;
	for (i = 0; i < tids_.size(); i++)
	{
		//PRINT("**********thead %d,%.4f avg pecent\n", i, secondTidTimes_[i] / (double)avg);
		//只要有一个线程差异大于平均值的taskLoadage_时就做调度
		if (llabs(secondTidTimes_[i] - avg) > critical)
		{
			//PRINT("**********thead %d,%lld - %lld > %lld\n", i, secondTidTimes_[i], avg, critical);
			break;
		}
	}
	//不做调度 
	if (i >= tids_.size())
		return false;

	//计算调度数据 
	int counter1 = 0; //统计cpuUseAge_为非负的个数 
	for (i = 0; i < tids_.size(); i++)
	{
		//计算应该减少或增加线程的负载，负数表要减少，正数表示要增加 
		double v = (double)(avg - secondTidTimes_[i]);
		//线程相对它自己应该变化的百分比 
		tids_[i].cpuUseAge_ = (float)(v / secondTidTimes_[i]);
		if (tids_[i].cpuUseAge_ > 0.00001f)
			counter1++;
	}

	//所有线程都处于饥饿或饱和状态不需要调度 
	if (counter1 <= 0 || counter1 == tids_.size())
		return false;
	return true;
}

#endif

#if defined(SYS_Unix)
char * skip_token(const char *p)
{
	while (isspace(*p)) p++;
	while (*p && !isspace(*p)) p++;
	return (char *)p;
}
ThreadsPhyInfo::ThreadsPhyInfo()
{
	firstTidTimes_.reserve(8);
	pid_ = getpid();
}

ThreadsPhyInfo::~ThreadsPhyInfo()
{
}

bool ThreadsPhyInfo::StartTrack()
{
	//单线程无需计算
	//	if (tids_.size() == 1)
	//		return false;
	if (firstTidTimes_.size() < tids_.size())
	{
		firstTidTimes_.reserve(tids_.size());
		firstTidTimes_.clear();
		firstTidTimes_.insert(firstTidTimes_.begin(), tids_.size(), 0);
	}

	FILE *fp;
	char buf[128];
	char tcpu[7];

	if ((fp = fopen("/proc/stat", "r")) == NULL)
	{
		//printf("Can't open file\n");
		assert(0);
		return false;
	}
	fgets(buf, sizeof(buf), fp);
	sscanf(buf, "%s%d%d%d%d%d%d%d%d", tcpu, &firstData_.user_, &firstData_.nice_, &firstData_.sys_,
		&firstData_.idle_, &firstData_.iowait_, &firstData_.irq_, &firstData_.softirq_, &firstData_.steal_);

	//获取每个线程的CPU时间
	for (int i = 0; i < tids_.size(); i++)
	{
		firstTidTimes_[i] = GetThreadTime(pid_, tids_[i].tid_);
		if (firstTidTimes_[i] < 0)
			return false;
	}
	fclose(fp);
	return true;
}
bool ThreadsPhyInfo::StopTrack()
{
	FILE *fp;
	char buf[128];
	char tcpu[7];

	if ((fp = fopen("/proc/stat", "r")) == NULL)
	{
		//printf("Can't open file\n");
		assert(0);
		return false;
	}
	fgets(buf, sizeof(buf), fp);
	ElementData secondData = { 0 };
	sscanf(buf, "%s%d%d%d%d%d%d%d%d", tcpu, &secondData.user_, &secondData.nice_, &secondData.sys_,
		&secondData.idle_, &secondData.iowait_, &secondData.irq_, &secondData.softirq_, &secondData.steal_);
	fclose(fp);

	//统计整个CPU使用率
	uint32_t all1 = firstData_.user_ + firstData_.nice_ + firstData_.sys_ + firstData_.idle_ + firstData_.iowait_ + firstData_.irq_ + firstData_.softirq_ + firstData_.steal_;
	uint32_t all2 = secondData.user_ + secondData.nice_ + secondData.sys_ + secondData.idle_ + secondData.iowait_ + secondData.irq_ + secondData.softirq_ + secondData.steal_;
	double all = (double)(all2 - all1) / 100;
#ifdef OTHER_INFO
	cpuWholeInfo_.UseAge_ = ((secondData.user_ - firstData_.user_) + (secondData.nice_ - firstData_.nice_)) / all;
	cpuWholeInfo_.Syage_ = ((secondData.sys_ - firstData_.sys_) + (secondData.irq_ - firstData_.irq_) + (secondData.softirq_ - firstData_.softirq_)) / all;
	cpuWholeInfo_.Niage_ = (secondData.idle_ - firstData_.idle_) / all;
	cpuWholeInfo_.Idage_ = (secondData.nice_ - firstData_.nice_) / all;
	cpuWholeInfo_.Ioage_ = (secondData.iowait_ - firstData_.iowait_) / all;
	cpuWholeInfo_.Irage_ = (secondData.irq_ - firstData_.irq_) / all;
	cpuWholeInfo_.Soage_ = (secondData.softirq_ - firstData_.softirq_) / all;
	cpuWholeInfo_.Stage_ = (secondData.steal_ - firstData_.steal_) / all;
#endif

	//计算每个线程的CPU使用率
	pid_t pid = getpid();
	int NUM_PROCS = sysconf(_SC_NPROCESSORS_CONF);
	float total = 0.0f;
	for (int i = 0; i < tids_.size(); i++)
	{
		int second = GetThreadTime(pid_, tids_[i].tid_);
		if (second < 0)
			return false;
		//每个线程两个时间差值来计算出CPU使用率0~100%
		tids_[i].cpuUseAge_ = (second - firstTidTimes_[i]) / all * NUM_PROCS;
		total += tids_[i].cpuUseAge_;
		//printf("thread %d ,cpu: %.5f\n", i, tids_[i].cpuUseAge_);
	}

	//平均值
	float avg = total / tids_.size();
	//计算最终结果
	int i;
	for (i = 0; i < tids_.size(); i++)
	{
		//只要有一个线程CPU使用率超过平均值的taskLoadage_时就做调度
		if (llabs(tids_[i].cpuUseAge_ - avg) > taskLoadage_)
		{
			printf("**********thead %d,%.5f - %.5f > %.5f\n", i, tids_[i].cpuUseAge_, avg, taskLoadage_);
			break;
		}
	}
	//不做调度
	if (i >= tids_.size())
		return false;

	return true;
}
int ThreadsPhyInfo::GetThreadTime(pid_t pid, int tid)
{
	char szStatStr[1024];
	char pname[64];
	char state;
	int ppid, pgrp, session, tty, tpgid;
	unsigned int    flags, minflt, cminflt, majflt, cmajflt;
	int utime, stime, cutime, cstime, counter, priority;
	unsigned int  timeout, itrealvalue;
	int           starttime;
	unsigned int  vsize, rss, rlim, startcode, endcode, startstack, kstkesp, kstkeip;
	int signal, blocked, sigignore, sigcatch;
	unsigned int  wchan;

	char file_stat[1024];
	if (tid == 0)
	{
		sprintf(file_stat, "/proc/%d/stat", pid);
	}
	else if (tid != -1)
	{
		sprintf(file_stat, "/proc/%d/task/%d/stat", pid, tid);
	}

	FILE* fid = nullptr;
	if ((fid = fopen(file_stat, "r")) == nullptr)
	{
		//printf("Can't open file\n");
		//assert(0);
		return -1;
	}

	fgets(szStatStr, sizeof(szStatStr), fid);
	fclose(fid);

	sscanf(szStatStr, "%u", &pid);
	char *sp, *t;
	sp = strchr(szStatStr, '(') + 1;
	t = strchr(szStatStr, ')');
	strncpy(pname, sp, t - sp);
	sscanf(t + 2, "%c %d %d %d %d %d %u %u %u %u %u %d %d %d %d %d %d %u %u %d %u %u %u %u %u %u %u %u %d %d %d %d %u",
		/*     1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33*/
		&state, &ppid, &pgrp, &session, &tty, &tpgid, &flags, &minflt, &cminflt, &majflt, &cmajflt, &utime, &stime, &cutime, &cstime, &counter,
		&priority, &timeout, &itrealvalue, &starttime, &vsize, &rss, &rlim, &startcode, &endcode, &startstack,
		&kstkesp, &kstkeip, &signal, &blocked, &sigignore, &sigcatch, &wchan);
	/*
	utime 该任务在用户态运行的时间，单位为jiffies
	stime 该任务在核心态运行的时间，单位为jiffies
	cutime 所有已死线程在用户态运行的时间，单位为jiffies
	cstime 所有已死在核心态运行的时间，单位为jiffies
	*/
	int p_cpu = utime + stime + cutime + cstime;
	return p_cpu;
}
#ifdef OTHER_INFO
int ThreadsPhyInfo::GetPhyMem(pid_t pid)
{
	char file[64] = { 0 };//文件名
	FILE *fd;         //定义文件指针fd
	char line_buff[256] = { 0 };  //读取行的缓冲区
	sprintf(file, "/proc/%d/status", pid);

	if ((fd = fopen(file, "r")) == NULL)
	{
		//printf("Can't open file\n");
		assert(0);
		return -1;
	}

	//获取vmrss:实际物理内存占用
	int i;
	char name1[32];//存放项目名称
	int vmrss;//存放内存峰值大小
	char name2[32];
	int vmsize;
	for (i = 0; i<12; i++)
	{
		fgets(line_buff, sizeof(line_buff), fd);
	}
	fgets(line_buff, sizeof(line_buff), fd);
	sscanf(line_buff, "%s %d", name2, &vmsize);

	for (i = 0; i<2; i++)
	{
		fgets(line_buff, sizeof(line_buff), fd);
	}

	fgets(line_buff, sizeof(line_buff), fd);//读取VmRSS这一行的数据,VmRSS在第15行
	sscanf(line_buff, "%s %d", name1, &vmrss);

	fclose(fd);     //关闭文件fd
					//sprintf(ph,"VIRT=%dKB RES=%dKB",vmsize,vmrss);
	return vmrss;
}
int ThreadsPhyInfo::GetSysMem()
{
	int tm, fm, bm, cm, ts, fs;
	char buffer[4096 + 1];
	int fd, len;
	char *p;

	if ((fd = open("/proc/meminfo", O_RDONLY)) < 0)
	{
		perror("open /proc/meminfo file failed");
		assert(0);
		return -1;
	}
	len = read(fd, buffer, sizeof(buffer) - 1);
	close(fd);

	buffer[len] = '\0';
	p = buffer;
	p = skip_token(p);
	tm = strtoul(p, &p, 10); /* total memory */

	p = strchr(p, '\n');
	p = skip_token(p);
	fm = strtoul(p, &p, 10); /* free memory */

	p = strchr(p, '\n');
	p = skip_token(p);
	bm = strtoul(p, &p, 10); /* buffer memory */

	p = strchr(p, '\n');
	p = skip_token(p);
	cm = strtoul(p, &p, 10); /* cached memory */

	for (int i = 0; i< 8; i++)
	{
		p++;
		p = strchr(p, '\n');
	}
	p = skip_token(p);
	ts = strtoul(p, &p, 10); /* total swap */

	p = strchr(p, '\n');
	p = skip_token(p);
	fs = strtoul(p, &p, 10); /* free swap */

							 //    sprintf(mem,"Mem: %luk total,%luk used,%luk free,%luk buffer\nSwap: %luk total,%luk used, %luk  free,%luk cached\n",
							 //            tm,tm-fm,fm,bm,ts,ts-fs,fs,cm);
	memWholeInfo_.Total_ = tm;
	memWholeInfo_.Used_ = tm - fm;
	memWholeInfo_.Free_ = fm;
	memWholeInfo_.Cached_ = bm;
	memWholeInfo_.SwapTotal_ = ts;
	memWholeInfo_.SwapUsed_ = ts - fs;
	memWholeInfo_.SwapFree_ = fs;
	memWholeInfo_.SwapCached_ = cm;
	//返回总物理内存
	return tm;
}
double ThreadsPhyInfo::GetPidMemUseage(pid_t pid)
{
	long page_size = sysconf(_SC_PAGESIZE) >> 10;
	return (GetPhyMem(pid)*page_size) / GetSysMem() * 100;
}
#endif

#endif
