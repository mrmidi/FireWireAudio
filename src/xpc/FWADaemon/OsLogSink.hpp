#ifndef OS_LOG_SINK_HPP
#define OS_LOG_SINK_HPP

#include <spdlog/sinks/base_sink.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/details/null_mutex.h>
#include <spdlog/formatter.h>
#include <os/log.h>
#include <mutex>

// OsLogSink: forwards spdlog messages to Apple's os_log
// Thread safety is provided by the Mutex template parameter (default: std::mutex)
template<typename Mutex = std::mutex>
class OsLogSink : public spdlog::sinks::base_sink<Mutex> {
public:
    // Constructor allowing specification of the os_log handle
    explicit OsLogSink(os_log_t oslog = OS_LOG_DEFAULT) : oslog_(oslog) {}

    // Prevent copying/moving
    OsLogSink(const OsLogSink&) = delete;
    OsLogSink& operator=(const OsLogSink&) = delete;

protected:
    // Called by spdlog for each log record
    void sink_it_(const spdlog::details::log_msg &msg) override {
        // Format the message using the assigned formatter
        spdlog::memory_buf_t formatted_buf;
        spdlog::sinks::base_sink<Mutex>::formatter_->format(msg, formatted_buf);
        // Ensure null-termination for os_log
        if (formatted_buf.size() == 0 || formatted_buf.data()[formatted_buf.size() - 1] != '\0') {
            formatted_buf.push_back('\0');
        }
        // Map spdlog level to os_log_type_t
        os_log_type_t log_type;
        switch (msg.level) {
            case spdlog::level::trace:      log_type = OS_LOG_TYPE_DEBUG;   break;
            case spdlog::level::debug:      log_type = OS_LOG_TYPE_DEBUG;   break;
            case spdlog::level::info:       log_type = OS_LOG_TYPE_INFO;    break;
            case spdlog::level::warn:       log_type = OS_LOG_TYPE_DEFAULT; break;
            case spdlog::level::err:        log_type = OS_LOG_TYPE_ERROR;   break;
            case spdlog::level::critical:   log_type = OS_LOG_TYPE_FAULT;   break;
            default:                        log_type = OS_LOG_TYPE_DEFAULT; break;
        }
        // Use os_log_with_type; %{public}s ensures visibility in Console.app
        os_log_with_type(oslog_, log_type, "%{public}s", formatted_buf.data());
    }

    // Called when spdlog flushes logs
    void flush_() override {
        // os_log does its own buffering/flushing; nothing needed here
    }

private:
    os_log_t oslog_;
};

// Type aliases for common usage
using os_log_sink_mt = OsLogSink<std::mutex>;
using os_log_sink_st = OsLogSink<spdlog::details::null_mutex>;

#endif // OS_LOG_SINK_HPP
