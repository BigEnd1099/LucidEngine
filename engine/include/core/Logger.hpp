#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <mutex>

namespace engine::core {
    class Logger {
    public:
        static void info(const std::string& msg) { 
            logMessage("[INFO] " + msg);
            std::cerr << "[INFO] " << msg << std::endl; 
        }
        static void error(const std::string& msg) { 
            logMessage("[ERROR] " + msg);
            std::cerr << "[ERROR] " << msg << std::endl; 
        }
        static void warn(const std::string& msg) { 
            logMessage("[WARN] " + msg);
            std::cerr << "[WARN] " << msg << std::endl; 
        }

        static std::vector<std::string> getRecentLogs(size_t maxCount = 50) {
            std::lock_guard<std::mutex> lock(s_mutex);
            if (s_logs.size() <= maxCount) return s_logs;
            return std::vector<std::string>(s_logs.end() - maxCount, s_logs.end());
        }

    private:
        static inline std::mutex s_mutex;
        static inline std::vector<std::string> s_logs;

        static void logMessage(const std::string& line) {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_logs.push_back(line);
            if (s_logs.size() > 500) {
                s_logs.erase(s_logs.begin(), s_logs.begin() + 100);
            }
        }
    };
}
