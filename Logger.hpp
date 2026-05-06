#pragma once

#include <atomic>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <functional>

class Logger {
public:
    enum class Level {
        Debug = 0,
        Info = 1,
        Warn = 2,
        Error = 3,
        Off = 4
    };

    /**
     * @brief 获取日志记录器实例
     * @return 日志记录器实例引用
     */
    static Logger& instance();

    /**
    * @brief 设置日志等级
    * @param level 日志等级
    */
    void setLevel(Level level);

    /**
    * @brief 返回日志等级 
    */
    Level level() const;

    /**
    * @brief 判断是否可以打印日志 
    */
    bool shouldLog(Level level) const;

    /**
    * @brief 设置控制台输出 
    * @param enabled 是否启用控制台输出
    */
    void enableConsole(bool enabled);

    /**
    * @brief 日志文件打开方式
    */
    enum class FileMode {
        Append,   ///< 追加写入（std::ios::app）
        Truncate  ///< 从头写入，若文件已存在则清空（std::ios::trunc）
    };

    /**
    * @brief 设置日志输出文件
    * @param file_path 日志文件路径
    * @param mode 打开模式：追加或清空后从头写
    * @return true 成功
    * @return false 失败
    */
    bool setFile(const std::string& file_path, FileMode mode = FileMode::Append);

    /**
    * @brief 关闭日志文件
    */
    void closeFile();

    /**
    * @brief 停止后台日志线程：不再接受新日志，排空队列后 join。
    *        进程退出前建议调用；也可依赖单例析构（静态销毁阶段调用）。
    */
    void shutdown();

    /**
    * @brief 记录日志
    * @param level 日志级别
    * @param module 模块名
    * @param message 日志消息
    */ 
    void log(Level level, const char* module, const std::string& message);

    /**
    * @brief 记录FFmpeg日志
    * @param ffmpeg_level FFmpeg日志级别
    * @param message FFmpeg日志消息
    */
    void logFfmpeg(int ffmpeg_level, const std::string& message);

    struct LogRecord {
        Level level = Level::Info;
        std::string module;
        std::string message;
        std::string timestamp;
        std::string thread_id;
    };

    using OutputCallback = std::function<void(const LogRecord&)>;

    /**
    * @brief 设置日志输出回调（在后台输出线程中调用）。默认可用内置格式。
    * @param cb 回调；传入空 function 则恢复为内置 writeRecord 行为（控制台/文件）。
    */
    void setOutputCallback(OutputCallback cb);

private:

    Logger();
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    static const char* levelName(Level level);

    void workerLoop();
    void writeRecord(const LogRecord& rec);

    std::atomic<int> level_{static_cast<int>(Level::Info)};
    std::mutex io_mutex_;
    bool console_enabled_ = true;
    std::ofstream file_;

    std::thread worker_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<LogRecord> queue_;
    std::atomic<bool> stop_{false};

    std::mutex callback_mutex_;
    OutputCallback output_callback_;
};

#define LOG_DEBUG(module, message_expr)                                                                                 \
    do {                                                                                                                \
        if (Logger::instance().shouldLog(Logger::Level::Debug)) {                                                       \
            std::ostringstream _logger_stream;                                                                          \
            _logger_stream << message_expr;                                                                             \
            Logger::instance().log(Logger::Level::Debug, module, _logger_stream.str());                                 \
        }                                                                                                               \
    } while (0)

#define LOG_INFO(module, message_expr)                                                                                   \
    do {                                                                                                                 \
        if (Logger::instance().shouldLog(Logger::Level::Info)) {                                                         \
            std::ostringstream _logger_stream;                                                                           \
            _logger_stream << message_expr;                                                                              \
            Logger::instance().log(Logger::Level::Info, module, _logger_stream.str());                                   \
        }                                                                                                                \
    } while (0)

#define LOG_WARN(module, message_expr)                                                                                   \
    do {                                                                                                                 \
        if (Logger::instance().shouldLog(Logger::Level::Warn)) {                                                         \
            std::ostringstream _logger_stream;                                                                           \
            _logger_stream << message_expr;                                                                              \
            Logger::instance().log(Logger::Level::Warn, module, _logger_stream.str());                                   \
        }                                                                                                                \
    } while (0)

#define LOG_ERROR(module, message_expr)                                                                                  \
    do {                                                                                                                 \
        if (Logger::instance().shouldLog(Logger::Level::Error)) {                                                        \
            std::ostringstream _logger_stream;                                                                           \
            _logger_stream << message_expr;                                                                              \
            Logger::instance().log(Logger::Level::Error, module, _logger_stream.str());                                  \
        }                                                                                                                \
    } while (0)
