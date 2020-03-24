#include "debugger.h"
//#include "../scheduler/ref.h"
#include "../scheduler/scheduler.h"
#include "../scheduler/processer.h"
#include "../task/task.h"

namespace co
{

CoDebugger & CoDebugger::getInstance()
{
    static CoDebugger obj;
    return obj;
}
std::string CoDebugger::GetAllInfo()
{
    std::string s;
    s += P("==============================================");
    s += P("TaskCount: %d", TaskCount());
    s += P("CurrentTaskID: %u", GetCurrentTaskID());
    s += P("CurrentTaskInfo: %s", GetCurrentTaskDebugInfo());
    s += P("CurrentTaskYieldCount: %lu", GetCurrentTaskYieldCount());
    s += P("CurrentThreadID: %d", GetCurrentThreadID());

    s += P("--------------------------------------------");

    return s;
}
int CoDebugger::TaskCount()
{
#if ENABLE_DEBUGGER
    return Task::getCount();
#else
    return -1;
#endif
}
uint32_t CoDebugger::GetCurrentTaskID()
{
    return g_Scheduler.GetCurrentTaskID();
}
uint64_t CoDebugger::GetCurrentTaskYieldCount()
{
    return g_Scheduler.GetCurrentTaskYieldCount();
}
void CoDebugger::SetCurrentTaskDebugInfo(const std::string & info)
{
    g_Scheduler.SetCurrentTaskDebugInfo(info);
}
const char * CoDebugger::GetCurrentTaskDebugInfo()
{
    return Processer::GetCurrentTask()->DebugInfo();
}

} //namespace co
