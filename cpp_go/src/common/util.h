#pragma once
#include "config.h"
#include <string.h>

namespace co
{

// 可被优化的lock guard
struct fake_lock_guard
{
    template <typename Mutex>
    explicit fake_lock_guard(Mutex&) {}
};

// 全局对象计数器
template <typename T>
struct ObjectCounter
{
    ObjectCounter() { ++counter(); }
    ObjectCounter(ObjectCounter const&) { ++counter(); }
    ObjectCounter(ObjectCounter &&) { ++counter(); }
    ~ObjectCounter() { --counter(); }

    static long getCount() {
        return counter();
    }

private:
    static atomic_t<long>& counter() {
        static atomic_t<long> c;
        return c;
    }
};

// ID
template <typename T>
struct IdCounter
{
    IdCounter() { id_ = ++counter(); }
    IdCounter(IdCounter const&) { id_ = ++counter(); }
    IdCounter(IdCounter &&) { id_ = ++counter(); }

    long getId() const {
        return id_;
    }

private:
    static atomic_t<long>& counter() {
        static atomic_t<long> c;
        return c;
    }

    long id_;
};

///////////////////////////////////////

// 创建协程的源码文件位置
struct SourceLocation
{
    const char* file_ = nullptr;
    int lineno_ = 0;

    void Init(const char* file, int lineno)
    {
        file_ = file, lineno_ = lineno;
    }

    friend bool operator<(SourceLocation const& lhs, SourceLocation const& rhs)
    {
        if (lhs.lineno_ != rhs.lineno_)
            return lhs.lineno_ < rhs.lineno_;

        if (lhs.file_ == rhs.file_) return false;

        if (lhs.file_ == nullptr)
            return true;

        if (rhs.file_ == nullptr)
            return false;
        
        return strcmp(lhs.file_, rhs.file_) == -1;
    }

    std::string ToString() const
    {
        std::string s("{file:");
        if (file_) s += file_;
        s += ", line:";
        s += std::to_string(lineno_) + "}";
        return s;
    }
};


} //namespace co
