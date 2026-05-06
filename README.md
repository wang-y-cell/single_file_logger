# 单文件日志模块
## 可直接使用的单文件日志模块
四个日志等级
- info
- debug
- warning
- error
可设置**文本格式**`setFormatCallback`：签名为 `std::string(const LogRecord&)`，只负责拼出一行字符串；是否写终端/文件由内部根据 `enableConsole` / `setFile` 决定。

```cpp
struct LogRecord {
        Level level = Level::Info;
        std::string module;
        std::string message;
        std::string timestamp;
        std::string thread_id;
    };
```
示例：只改行内容，输出目标仍由 Logger 管理。
```cpp
Logger::instance().setFormatCallback([](const Logger::LogRecord& rec) {
    return rec.timestamp + " | " + rec.module + " | " + rec.message;
});
```