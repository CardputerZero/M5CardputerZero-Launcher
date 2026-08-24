#include "cp0_lvgl_log.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

#ifndef CP0_LVGL_USE_ZMQ_LOG
#define CP0_LVGL_USE_ZMQ_LOG 0
#endif

#if CP0_LVGL_USE_ZMQ_LOG
#include <zmq.hpp>
#endif

namespace {

#if CP0_LVGL_USE_ZMQ_LOG

static constexpr const char *kLogEndpoint = "tcp://*:5556";

class ZmqLogPublisher
{
public:
    void init()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ensure_locked();
    }

    void publish(const char *topic, const char *message)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ensure_locked() || !socket_)
            return;

        const std::string topic_text = topic && topic[0] ? topic : "log";
        const std::string line = make_line(message ? message : "");
        try {
            socket_->send(zmq::buffer(topic_text), zmq::send_flags::sndmore | zmq::send_flags::dontwait);
            socket_->send(zmq::buffer(line), zmq::send_flags::dontwait);
        } catch (const zmq::error_t &) {
        }
    }

private:
    std::mutex mutex_;
    bool tried_ = false;
    std::unique_ptr<zmq::context_t> context_;
    std::unique_ptr<zmq::socket_t> socket_;

    bool ensure_locked()
    {
        if (socket_)
            return true;
        if (tried_)
            return false;
        tried_ = true;

        try {
            context_.reset(new zmq::context_t(1));
            socket_.reset(new zmq::socket_t(*context_, zmq::socket_type::pub));
            int linger_ms = 0;
            socket_->set(zmq::sockopt::linger, linger_ms);
            socket_->bind(kLogEndpoint);
            return true;
        } catch (const zmq::error_t &) {
            socket_.reset();
            context_.reset();
            return false;
        }
    }

    static std::string make_line(const char *message)
    {
        using clock = std::chrono::steady_clock;
        static const auto start = clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - start).count();
        std::ostringstream oss;
        oss << ms << "ms " << message;
        return oss.str();
    }
};

static ZmqLogPublisher &publisher()
{
    static ZmqLogPublisher pub;
    return pub;
}

#endif

} // namespace

void cp0_zmq_log_init(void)
{
#if CP0_LVGL_USE_ZMQ_LOG
    publisher().init();
#endif
}

void cp0_zmq_log(const char *topic, const char *message)
{
#if CP0_LVGL_USE_ZMQ_LOG
    publisher().publish(topic, message);
#else
    std::fprintf(stderr, "[%s] %s\n", topic && topic[0] ? topic : "log", message ? message : "");
#endif
}

void cp0_zmq_logf(const char *topic, const char *fmt, ...)
{
#if CP0_LVGL_USE_ZMQ_LOG
    if (!fmt)
        return;

    char buf[1024] = {};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    cp0_zmq_log(topic, buf);
#else
    if (!fmt)
        return;
    char buf[1024] = {};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    cp0_zmq_log(topic, buf);
#endif
}
