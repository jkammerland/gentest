#pragma once

#include <memory>
#include <string>

namespace fixture {

struct Token {
    explicit Token(int value) : value(value) {}
    int value = 0;
};

struct Job {
    int id = 0;
};

struct Buf {
    explicit Buf(int size) : size(size) {}
    Buf(Buf &&other) noexcept : size(other.size) { other.size = -1; }
    Buf &operator=(Buf &&) = delete;
    int  size              = 0;
};

struct Factory {
    virtual ~Factory()                                                    = default;
    virtual Token                make_token()                             = 0; // not default-constructible
    virtual std::unique_ptr<Job> make_job()                               = 0; // move-only
    virtual int                  next()                                   = 0;
    virtual bool                 parse(const std::string &text, int &out) = 0;
    virtual void                 consume(Buf &&buffer)                    = 0;
    virtual void                 submit(std::unique_ptr<Job> job)         = 0;
    virtual int                 &slot()                                   = 0;
};

} // namespace fixture
