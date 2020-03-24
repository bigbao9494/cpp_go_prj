#pragma once
#include <unordered_map>
#include <vector>
#include <stdint.h>
#include <thread>
#include "../common/config.h"

struct TidInfo
{
	union
	{
		int tid_;									//linux使用 
		std::thread::native_handle_type handle_;	//windows使用 
	};
	float cpuUseAge_;							//CPU的繁忙程度: -n ~ +n 小于0表示饥饿，大于0表示饱和
};
//每个线程的CPU占用相关信息 
//struct TidCpuInfo
//{
//union
//{
//	double CpuUseAge_;		//占用率0~100 %
//	uint64_t cycleTime_;
//};
//	double MemUseAge_ = 0.0f;
//	int VIRT_ = 0;					//单位K
//	int RES_ = 0;					//单位K
//};
class InterfaceThreadsPhyInfo
{
public:
	InterfaceThreadsPhyInfo() { tids_.reserve(8); };
	virtual ~InterfaceThreadsPhyInfo() {};
	//开始记录信息
	virtual bool StartTrack() = 0;
	//停止记录信息,true表示需要调度
	virtual bool StopTrack() = 0;
	//添加一个要监控的线程,必须要线程函数内调用
	void AddThreadTid(int index, std::thread* thd = nullptr);
	float operator [](int index);
protected:
	std::vector<TidInfo> tids_;
	//每个线程的CPU使用信息
	//std::unordered_map<int,TidCpuInfo> tids_;
};

#if defined(SYS_Windows)
//windows下从系统得到的是线程的cycle_time，通过两次间隔cycle_time来计算线程执行的cycle_time的多少
//这样只能比较出两个线程的相对繁忙程度，而不能计算出每个线程占用的CPU百分比
class ThreadsPhyInfo : public InterfaceThreadsPhyInfo
{
public:
	ThreadsPhyInfo() { firstTidTimes_.reserve(8); secondTidTimes_.reserve(8); };
	virtual ~ThreadsPhyInfo() {};
	bool StartTrack();
	//停止记录信息
	bool StopTrack();
private:
	//缓存StartTrack时每个线程的cycle_time
	std::vector<int64_t> firstTidTimes_;
	//缓存StopTrack时每个线程的cycle_time
	std::vector<int64_t> secondTidTimes_;
	//要调度的阀值，具体看StopTrack函数
	const float taskLoadage_ = 0.5;
};
#endif


#if defined(SYS_Unix)
//#define OTHER_INFO		//除了CPU使用率外的其它数据
//linux是可以根据GetThreadTime的值计算出每个线程占用CPU的百分比，这比计算线程相对繁忙程度可能更准确

/*
* 获取线程的物理信息，主要是线程占用CPU百分比
* StopTrack - StartTrack 这段间隔时间的CPU的占用率
Cpu(s):  26.09% usage,1.63% syage,0.00% niage,72.28% idage,0.00% ioage,0.00% irage,0.27% soage,0.27% stage
Mem: 5048120k total,3429460k used,1618660k free,2886720k buffer
Swap: 740020k total,739988k used, 32k  free,208044k cached
PID=6157  TID=6157  0.00%CPU  0.00%MEM VIRT=6157KB RES=4815KB
PID=6157  TID=6158  8.70%CPU  0.00%MEM VIRT=6157KB RES=4815KB
PID=6157  TID=6159  105.43%CPU  0.00%MEM VIRT=6157KB RES=4815KB
*
*/

struct CpuWholeInfo
{
	float UseAge_ = 0.0f;		//占用率0~100 %
	float Syage_ = 0.0f;
	float Niage_ = 0.0f;
	float Idage_ = 0.0f;
	float Ioage_ = 0.0f;
	float Irage_ = 0.0f;
	float Soage_ = 0.0f;
	float Stage_ = 0.0f;
};
struct MemWholeInfo
{
	int Total_ = 0;			//单位K
	int Used_ = 0;
	int Free_ = 0;
	int Cached_ = 0;

	int SwapTotal_ = 0;		//单位K
	int SwapUsed_ = 0;
	int SwapFree_ = 0;
	int SwapCached_ = 0;
};
struct ElementData
{
	uint32_t user_;
	uint32_t nice_;
	uint32_t sys_;
	uint32_t idle_;
	uint32_t iowait_;
	uint32_t irq_;
	uint32_t softirq_;
	uint32_t steal_;
};
class ThreadsPhyInfo : public InterfaceThreadsPhyInfo
{
public:
	ThreadsPhyInfo();
	virtual ~ThreadsPhyInfo();
	//开始记录信息
	bool StartTrack();
	//停止记录信息
	bool StopTrack();
private:
	//每个线程的CPU运行时间，单位为jiffies(内核节拍数)
	int GetThreadTime(pid_t pid, int tid);
#ifdef OTHER_INFO
	//整个系统内存大小
	int GetSysMem();
	//进程占用内存大小
	int GetPhyMem(pid_t pid);
	//进程占用系统内存比率
	double GetPidMemUseage(pid_t pid);
#endif
	//缓存StartTrack时每个线程的cpu_time
	std::vector<int> firstTidTimes_;

	//StartTrack时暂存的数据
	ElementData firstData_;
	//进程PID
	pid_t pid_ = -1;
	//要调度的阀值，具体看StopTrack函数
	const float taskLoadage_ = 30.0f;
#ifdef OTHER_INFO
	//CPU全局信息
	CpuWholeInfo cpuWholeInfo_;
	//内存全局信息
	MemWholeInfo memWholeInfo_;
#endif
};
#endif
