#pragma once
#include "../common/config.h"
#include "../scheduler/processer.h"
#include "../scheduler/scheduler.h"
#include "channel_impl.h"
#include "fast_channel_impl.h"

namespace co
{

template <typename T>
class Channel
{
private:
    mutable std::shared_ptr<ChannelImpl<T>> impl_;

public:
    // @capacity: capacity of channel.
    // @choose1: use CASChannelImpl if capacity less than choose1
    // @choose2: if capacity less than choose2, use ringbuffer. else use std::list.
    explicit Channel(std::size_t capacity = 0,
            std::size_t choose1 = 0, //16,
            std::size_t choose2 = 100001)
    {
	    impl_.reset(new FastChannelImpl<T>(capacity, capacity < choose2));
    }

    void SetDbgMask(uint64_t mask)
    {
        impl_->SetDbgMask(mask);
    }

    Channel const& operator<<(T t) const
    {
        impl_->Push(t, -1);
        return *this;
    }

    Channel const& operator>>(T & t) const
    {
        impl_->Pop(t, -1);
        return *this;
    }

    Channel const& operator>>(std::nullptr_t ignore) const
    {
        T t;
        impl_->Pop(t, -1);
        return *this;
    }

    bool TryPush(T t) const
    {
        return impl_->Push(t, 0);
    }

    bool TryPop(T & t) const
    {
        return impl_->Pop(t, 0);
    }

    bool TryPop(std::nullptr_t ignore) const
    {
        T t;
        return impl_->Pop(t, 0);
    }

    bool TimedPush(T t, int timeout) const
    {
        return impl_->Push(t,timeout);
    }

	bool TimedPop(T & t, int timeout) const
	{
		return impl_->Pop(t, timeout);
	}

    void Close() const {
        impl_->Close();
    }
};


//template <>
//class Channel<void> : public Channel<std::nullptr_t>
//{
//public:
//    explicit Channel(std::size_t capacity = 0)
//        : Channel<std::nullptr_t>(capacity)
//    {}
//};

template <typename T>
using co_chan = Channel<T>;

} //namespace co
