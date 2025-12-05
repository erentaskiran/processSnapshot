#pragma once

#include "core/types.hpp"
#include "state/state_manager.hpp"
#include <string>
#include <vector>
#include <fstream>
#include <mutex>
#include <queue>
#include <thread>
#include <condition_variable>

namespace checkpoint {

// Log entry yapısı
struct LogEntry {
    OperationId id;
    LogLevel level;
    Timestamp timestamp;
    std::string category;
    std::string message;
    std::string file;
    int line;
    std::map<std::string, std::string> context;
    
    std::string toString() const;
    StateData serialize() const;
    static LogEntry deserialize(const StateData& data);
};

// Log formatı arayüzü
class ILogFormatter {
public:
    virtual ~ILogFormatter() = default;
    virtual std::string format(const LogEntry& entry) = 0;
};

// Basit text formatter
class TextLogFormatter : public ILogFormatter {
public:
    std::string format(const LogEntry& entry) override;
};

// JSON formatter
class JsonLogFormatter : public ILogFormatter {
public:
    std::string format(const LogEntry& entry) override;
};

// Log output arayüzü
class ILogOutput {
public:
    virtual ~ILogOutput() = default;
    virtual void write(const std::string& formattedEntry) = 0;
    virtual void flush() = 0;
};

// Console output
class ConsoleLogOutput : public ILogOutput {
private:
    bool m_useColors;
    
public:
    explicit ConsoleLogOutput(bool useColors = true);
    void write(const std::string& formattedEntry) override;
    void flush() override;
};

// File output
class FileLogOutput : public ILogOutput {
private:
    std::ofstream m_file;
    std::filesystem::path m_filePath;
    size_t m_maxFileSize;
    int m_maxBackupCount;
    std::mutex m_mutex;
    
    void rotateIfNeeded();
    
public:
    FileLogOutput(const std::filesystem::path& filePath, 
                  size_t maxFileSize = 10 * 1024 * 1024,
                  int maxBackupCount = 5);
    ~FileLogOutput();
    
    void write(const std::string& formattedEntry) override;
    void flush() override;
};

// İşlem Logger'ı - ana logger sınıfı
class OperationLogger {
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    
public:
    OperationLogger();
    ~OperationLogger();
    
    // Singleton pattern için
    static OperationLogger& getInstance();
    
    // Konfigürasyon
    void setMinLevel(LogLevel level);
    void addOutput(std::shared_ptr<ILogOutput> output);
    void setFormatter(std::shared_ptr<ILogFormatter> formatter);
    void enableAsync(bool enable);
    
    // Logging metodları
    void log(LogLevel level, const std::string& category, 
             const std::string& message, const char* file = nullptr, int line = 0);
    
    void trace(const std::string& category, const std::string& message);
    void debug(const std::string& category, const std::string& message);
    void info(const std::string& category, const std::string& message);
    void warning(const std::string& category, const std::string& message);
    void error(const std::string& category, const std::string& message);
    void critical(const std::string& category, const std::string& message);
    
    // İşlem kaydı
    OperationId logOperation(OperationType type, const std::string& description,
                            CheckpointId relatedCheckpoint = 0);
    void logOperationComplete(OperationId opId, bool success);
    
    // Sorgulama
    std::vector<LogEntry> getEntries(LogLevel minLevel = LogLevel::Trace, 
                                     size_t maxCount = 100);
    std::vector<LogEntry> getEntriesBetween(Timestamp start, Timestamp end);
    std::vector<LogEntry> getEntriesForCheckpoint(CheckpointId checkpointId);
    
    // İşlem geçmişi
    std::vector<OperationRecord> getOperationHistory(size_t maxCount = 100);
    std::vector<OperationRecord> getOperationsSince(CheckpointId checkpointId);
    
    // Temizlik
    void clear();
    void flush();
};

// Makro tanımları - kolay kullanım için
#define LOG_TRACE(category, message) \
    checkpoint::OperationLogger::getInstance().log(checkpoint::LogLevel::Trace, category, message, __FILE__, __LINE__)

#define LOG_DEBUG(category, message) \
    checkpoint::OperationLogger::getInstance().log(checkpoint::LogLevel::Debug, category, message, __FILE__, __LINE__)

#define LOG_INFO(category, message) \
    checkpoint::OperationLogger::getInstance().log(checkpoint::LogLevel::Info, category, message, __FILE__, __LINE__)

#define LOG_WARNING(category, message) \
    checkpoint::OperationLogger::getInstance().log(checkpoint::LogLevel::Warning, category, message, __FILE__, __LINE__)

#define LOG_ERROR(category, message) \
    checkpoint::OperationLogger::getInstance().log(checkpoint::LogLevel::Error, category, message, __FILE__, __LINE__)

#define LOG_CRITICAL(category, message) \
    checkpoint::OperationLogger::getInstance().log(checkpoint::LogLevel::Critical, category, message, __FILE__, __LINE__)

} // namespace checkpoint
