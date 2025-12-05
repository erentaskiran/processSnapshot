#pragma once

#include "core/types.hpp"
#include <chrono>
#include <string>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <random>
#include <atomic>

namespace checkpoint {
namespace utils {

// Zaman yardımcıları
class TimeUtils {
public:
    static Timestamp now() {
        return std::chrono::system_clock::now();
    }
    
    static std::string formatTimestamp(const Timestamp& ts) {
        auto time = std::chrono::system_clock::to_time_t(ts);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }
    
    static std::string formatDuration(const Duration& duration) {
        auto ms = duration.count();
        if (ms < 1000) return std::to_string(ms) + "ms";
        if (ms < 60000) return std::to_string(ms / 1000) + "s";
        return std::to_string(ms / 60000) + "m " + std::to_string((ms % 60000) / 1000) + "s";
    }
    
    static int64_t toUnixTimestamp(const Timestamp& ts) {
        return std::chrono::duration_cast<std::chrono::seconds>(
            ts.time_since_epoch()).count();
    }
    
    static Timestamp fromUnixTimestamp(int64_t unix_ts) {
        return Timestamp(std::chrono::seconds(unix_ts));
    }
};

// ID üreteci
class IdGenerator {
private:
    static inline std::atomic<uint64_t> s_counter{1};  // 1'den başla, 0 geçersiz ID olarak kullanılabilir
    
public:
    static CheckpointId generateCheckpointId() {
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        return static_cast<CheckpointId>((now << 16) | (s_counter++ & 0xFFFF));
    }
    
    static OperationId generateOperationId() {
        return s_counter++;
    }
    
    static SessionId generateSessionId() {
        std::random_device rd;
        std::mt19937_64 gen(rd());
        return gen();
    }
};

// Dosya sistemi yardımcıları
class FileUtils {
public:
    static bool createDirectory(const std::filesystem::path& path) {
        return std::filesystem::create_directories(path);
    }
    
    static bool fileExists(const std::filesystem::path& path) {
        return std::filesystem::exists(path);
    }
    
    static size_t fileSize(const std::filesystem::path& path) {
        return std::filesystem::file_size(path);
    }
    
    static bool removeFile(const std::filesystem::path& path) {
        return std::filesystem::remove(path);
    }
    
    static std::vector<std::filesystem::path> listFiles(
        const std::filesystem::path& dir, 
        const std::string& extension = "") {
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                if (extension.empty() || entry.path().extension() == extension) {
                    files.push_back(entry.path());
                }
            }
        }
        return files;
    }
};

// String yardımcıları
class StringUtils {
public:
    static std::string trim(const std::string& str) {
        auto start = str.find_first_not_of(" \t\n\r");
        auto end = str.find_last_not_of(" \t\n\r");
        return (start == std::string::npos) ? "" : str.substr(start, end - start + 1);
    }
    
    static std::vector<std::string> split(const std::string& str, char delimiter) {
        std::vector<std::string> tokens;
        std::stringstream ss(str);
        std::string token;
        while (std::getline(ss, token, delimiter)) {
            tokens.push_back(token);
        }
        return tokens;
    }
    
    static std::string join(const std::vector<std::string>& parts, const std::string& delimiter) {
        std::string result;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) result += delimiter;
            result += parts[i];
        }
        return result;
    }
};

} // namespace utils
} // namespace checkpoint
