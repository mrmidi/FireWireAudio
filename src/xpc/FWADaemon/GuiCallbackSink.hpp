#ifndef GUI_CALLBACK_SINK_HPP
#define GUI_CALLBACK_SINK_HPP

#include <spdlog/sinks/base_sink.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/details/null_mutex.h>
#include <spdlog/formatter.h>
#include <mutex>

// Forward declare FWADaemon
@class FWADaemon;

template<typename Mutex = std::mutex>
class GuiCallbackSink : public spdlog::sinks::base_sink<Mutex> {
public:
    // Constructor now accepts a FWADaemon* instance (weak reference)
    explicit GuiCallbackSink(FWADaemon* daemon_ptr) : daemon_instance_(daemon_ptr) {}
    GuiCallbackSink(const GuiCallbackSink&) = delete;
    GuiCallbackSink& operator=(const GuiCallbackSink&) = delete;
protected:
    void sink_it_(const spdlog::details::log_msg &msg) override {
        // Create a strong reference from the weak one within the method scope
        FWADaemon *strongDaemon = daemon_instance_;
        if (!strongDaemon) { // Now this check is fully safe
            // Optionally log to stderr ONLY if daemon is gone, avoid infinite loop
            static bool warned = false;
            if (!warned) {
                fprintf(stderr, "[GuiCallbackSink] Warning: FWADaemon instance weak pointer is nil. Logs cannot be forwarded.\n");
                warned = true;
            }
            return;
        }

        // Use strongDaemon for the rest of the method
        if (![strongDaemon hasActiveGuiClients]) {
            return;
        }
        spdlog::memory_buf_t formatted_buf;
        try {
            spdlog::sinks::base_sink<Mutex>::formatter_->format(msg, formatted_buf);
        } catch (const std::exception& e) {
            fprintf(stderr, "[GuiCallbackSink] Formatting error: %s\n", e.what());
            return;
        }
        int32_t xpcLevel;
        switch (msg.level) {
            case spdlog::level::trace:    xpcLevel = 0; break;
            case spdlog::level::debug:    xpcLevel = 1; break;
            case spdlog::level::info:     xpcLevel = 2; break;
            case spdlog::level::warn:     xpcLevel = 4; break;
            case spdlog::level::err:      xpcLevel = 5; break;
            case spdlog::level::critical: xpcLevel = 6; break;
            default:                      xpcLevel = 2; break;
        }
        @autoreleasepool {
            NSString *logMessage = [[NSString alloc] initWithBytes:formatted_buf.data()
                                                            length:formatted_buf.size()
                                                          encoding:NSUTF8StringEncoding];
            if (logMessage) {
                [strongDaemon forwardLogMessageToClients:@"FWADaemon" level:xpcLevel message:logMessage];
            } else {
                fprintf(stderr, "[GuiCallbackSink] Failed to create NSString from log buffer (encoding issue?).\n");
            }
        }
    }
    void flush_() override {}
private:
    __weak FWADaemon* daemon_instance_; // Weak reference
};

using gui_callback_sink_mt = GuiCallbackSink<std::mutex>;
using gui_callback_sink_st = GuiCallbackSink<spdlog::details::null_mutex>;

#endif // GUI_CALLBACK_SINK_HPP
