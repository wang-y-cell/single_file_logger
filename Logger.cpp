#include "Logger.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <thread>

extern "C" {
#include <libavutil/log.h>
}

namespace {
std::mutex g_time_mutex;

//清理日志消息中的换行符
std::string trimMessage(const std::string& input) {
    std::string result = input;
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
    }
    return result;
}

//显示当前时间
std::string buildTimestamp() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto time = system_clock::to_time_t(now);
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm_value{};
    {
        std::lock_guard<std::mutex> lock(g_time_mutex);
        const std::tm* local_tm = std::localtime(&time);
        if (!local_tm) {
            return {};
        }
        tm_value = *local_tm;
    }

    std::ostringstream stream;
    stream << std::put_time(&tm_value, "%Y-%m-%d %H:%M:%S") << '.'
           << std::setw(3) << std::setfill('0') << ms.count();
    return stream.str();
}

std::string buildThreadId() {
    std::ostringstream stream;
    stream << std::this_thread::get_id();
    return stream.str();
}
} // namespace

Logger::Logger() {
    format_callback_ = [this](const LogRecord& rec) { return writeFormat(rec); };
    worker_ = std::thread([this] { workerLoop(); });
}

Logger::~Logger() {
    shutdown();
}

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::shutdown() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        stop_.store(true);
    }
    queue_cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void Logger::workerLoop() {
    for (;;) {
        LogRecord rec;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !queue_.empty() || stop_.load();
            });
            if (queue_.empty()) {
                if (stop_.load()) {
                    return;
                }
                continue;
            }
            rec = std::move(queue_.front());
            queue_.pop();
        }
        FormatCallback fmt;
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            fmt = format_callback_;
        }
        const std::string line = fmt ? fmt(rec) : writeFormat(rec);
        writeRecord(line);
    }
}

void Logger::setFormatCallback(FormatCallback cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (cb) {
        format_callback_ = std::move(cb);
    } else {
        format_callback_ = [this](const LogRecord& rec) { return writeFormat(rec); };
    }
}

std::string Logger::writeFormat(const LogRecord& rec) const {
    std::ostringstream stream;
    stream << rec.timestamp << " [" << levelName(rec.level) << "]"
           << " [" << rec.module << "]"
           << " [tid:" << rec.thread_id << "] "
           << rec.message;
    return stream.str();
}

void Logger::writeRecord(const std::string& line) {
    if (line.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(io_mutex_);
    if (console_enabled_) {
        std::cerr << line << '\n';
    }
    if (file_.is_open()) {
        file_ << line << '\n';
        file_.flush();
    }
}

void Logger::setLevel(Level level) {
    level_.store(static_cast<int>(level));
}

Logger::Level Logger::level() const {
    return static_cast<Level>(level_.load());
}

bool Logger::shouldLog(Level level) const {
    const int current_level = level_.load();
    return current_level != static_cast<int>(Level::Off) && static_cast<int>(level) >= current_level;
}

void Logger::enableConsole(bool enabled) {
    std::lock_guard<std::mutex> lock(io_mutex_);
    console_enabled_ = enabled;
}

bool Logger::setFile(const std::string& file_path, FileMode mode) {
    std::lock_guard<std::mutex> lock(io_mutex_);
    std::filesystem::path path(file_path);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    file_.close();
    file_.clear();
    const std::ios_base::openmode open_mode =
        (mode == FileMode::Append) ? (std::ios::out | std::ios::app) : (std::ios::out | std::ios::trunc);
    file_.open(file_path, open_mode);
    return file_.is_open();
}

void Logger::closeFile() {
    std::lock_guard<std::mutex> lock(io_mutex_);
    if (file_.is_open()) {
        file_.close();
    }
}

void Logger::log(Level level, const char* module, const std::string& message) {
    if (!shouldLog(level)) {
        return;
    }

    const std::string text = trimMessage(message);
    if (text.empty()) {
        return;
    }

    if (stop_.load()) {
        return;
    }

    LogRecord rec;
    rec.level = level;
    rec.module = module ? module : "Unknown";
    rec.message = text;
    rec.timestamp = buildTimestamp();
    rec.thread_id = buildThreadId();

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (stop_.load()) {
            return;
        }
        queue_.push(std::move(rec));
    }
    queue_cv_.notify_one();
}

void Logger::logFfmpeg(int ffmpeg_level, const std::string& message) {
    Level mapped_level = Level::Debug;
    if (ffmpeg_level <= AV_LOG_ERROR) {
        mapped_level = Level::Error;
    } else if (ffmpeg_level <= AV_LOG_WARNING) {
        mapped_level = Level::Warn;
    } else if (ffmpeg_level <= AV_LOG_INFO) {
        mapped_level = Level::Info;
    }
    log(mapped_level, "FFmpeg", message);
}

const char* Logger::levelName(Level level) {
    switch (level) {
        case Level::Debug:
            return "DEBUG";
        case Level::Info:
            return "INFO";
        case Level::Warn:
            return "WARN";
        case Level::Error:
            return "ERROR";
        case Level::Off:
            return "OFF";
    }
    return "INFO";
}
