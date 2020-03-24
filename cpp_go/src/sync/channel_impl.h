#pragma once
#include "../common/config.h"
#include "../common/clock.h"

namespace co
{

template <typename T>
struct ChannelImpl : public IdCounter<ChannelImpl<T>>
{
    virtual ~ChannelImpl() {}

    virtual bool Push(T t, int timeout = INNER_UTIME_NO_TIMEOUT) = 0;
    virtual bool Pop(T & t, int timeout = INNER_UTIME_NO_TIMEOUT) = 0;
    virtual void Close() = 0;
    virtual std::size_t Size() = 0;
    virtual bool Empty() = 0;
};

} // namespace co
