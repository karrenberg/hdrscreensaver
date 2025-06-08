#pragma once
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>

class Logger {
public:
    static Logger& Instance() {
        static Logger instance;
        return instance;
    }

    template<typename T>
    void Log(const T& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::wcout << msg << std::endl;
        if (logFileEnabled_ && logfile_.is_open()) {
            logfile_ << msg << std::endl;
        }
    }

    // Overload for wide strings
    void Log(const wchar_t* msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::wcout << msg << std::endl;
        if (logFileEnabled_ && logfile_.is_open()) {
            logfile_ << msg << std::endl;
        }
    }

    // Overload for std::wstring
    void Log(const std::wstring& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::wcout << msg << std::endl;
        if (logFileEnabled_ && logfile_.is_open()) {
            logfile_ << msg << std::endl;
        }
    }

    // For stream-style logging
    template<typename... Args>
    void LogMany(const Args&... args) {
        std::lock_guard<std::mutex> lock(mutex_);
        (log_stream(args), ...);
        log_stream_end();
    }

    void Configure(bool enableLogFile, const std::wstring& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (logfile_.is_open()) logfile_.close();
        logFileEnabled_ = enableLogFile;
        if (logFileEnabled_ && !path.empty()) {
            logfile_.open(path, std::ios::out | std::ios::app);
            if (!logfile_.is_open()) {
                std::wcout << L"[Logger] Failed to open log file: " << path << std::endl;
            }
        }
    }

private:
    Logger() = default;
    ~Logger() { if (logfile_.is_open()) logfile_.close(); }
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    bool logFileEnabled_ = false;
    std::wofstream logfile_;
    std::mutex mutex_;

    template<typename T>
    void log_stream(const T& val) { std::wcout << val; if (logFileEnabled_ && logfile_.is_open()) logfile_ << val; }
    void log_stream_end() { std::wcout << std::endl; if (logFileEnabled_ && logfile_.is_open()) logfile_ << std::endl; }
};

#define LOG_MSG(...) Logger::Instance().LogMany(__VA_ARGS__)
