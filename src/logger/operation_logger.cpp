#include "logger/operation_logger.hpp"
#include "utils/helpers.hpp"
#include <iostream>
#include <queue>
#include <thread>
#include <condition_variable>
#include <iomanip>

namespace checkpoint {

// ==================== LogEntry ====================

std::string LogEntry::toString() const {
    std::stringstream ss;
    ss << "[" << utils::TimeUtils::formatTimestamp(timestamp) << "] ";
    
    switch (level) {
        case LogLevel::Trace:    ss << "[TRACE]   "; break;
        case LogLevel::Debug:    ss << "[DEBUG]   "; break;
        case LogLevel::Info:     ss << "[INFO]    "; break;
        case LogLevel::Warning:  ss << "[WARNING] "; break;
        case LogLevel::Error:    ss << "[ERROR]   "; break;
        case LogLevel::Critical: ss << "[CRITICAL]"; break;
    }
    
    ss << "[" << category << "] " << message;
    
    if (!file.empty()) {
        ss << " (" << file << ":" << line << ")";
    }
    
    return ss.str();
}

StateData LogEntry::serialize() const {
    // Basit serileştirme
    std::string str = toString();
    return StateData(str.begin(), str.end());
}

LogEntry LogEntry::deserialize(const StateData& data) {
    LogEntry entry;
    // Basit deserialize - gerçek uygulamada daha detaylı olmalı
    entry.message = std::string(data.begin(), data.end());
    entry.timestamp = utils::TimeUtils::now();
    entry.level = LogLevel::Info;
    return entry;
}

// ==================== TextLogFormatter ====================

std::string TextLogFormatter::format(const LogEntry& entry) {
    return entry.toString() + "\n";
}

// ==================== JsonLogFormatter ====================

std::string JsonLogFormatter::format(const LogEntry& entry) {
    std::stringstream ss;
    ss << "{";
    ss << "\"id\":" << entry.id << ",";
    ss << "\"timestamp\":\"" << utils::TimeUtils::formatTimestamp(entry.timestamp) << "\",";
    ss << "\"level\":" << static_cast<int>(entry.level) << ",";
    ss << "\"category\":\"" << entry.category << "\",";
    ss << "\"message\":\"" << entry.message << "\"";
    
    if (!entry.file.empty()) {
        ss << ",\"file\":\"" << entry.file << "\",\"line\":" << entry.line;
    }
    
    if (!entry.context.empty()) {
        ss << ",\"context\":{";
        bool first = true;
        for (const auto& [key, value] : entry.context) {
            if (!first) ss << ",";
            ss << "\"" << key << "\":\"" << value << "\"";
            first = false;
        }
        ss << "}";
    }
    
    ss << "}\n";
    return ss.str();
}

// ==================== ConsoleLogOutput ====================

ConsoleLogOutput::ConsoleLogOutput(bool useColors) : m_useColors(useColors) {}

void ConsoleLogOutput::write(const std::string& formattedEntry) {
    std::cout << formattedEntry;
}

void ConsoleLogOutput::flush() {
    std::cout.flush();
}

// ==================== FileLogOutput ====================

FileLogOutput::FileLogOutput(const std::filesystem::path& filePath, 
                             size_t maxFileSize, int maxBackupCount)
    : m_filePath(filePath)
    , m_maxFileSize(maxFileSize)
    , m_maxBackupCount(maxBackupCount) {
    // Dizini oluştur
    std::filesystem::create_directories(filePath.parent_path());
    m_file.open(filePath, std::ios::app);
}

FileLogOutput::~FileLogOutput() {
    if (m_file.is_open()) {
        m_file.close();
    }
}

void FileLogOutput::rotateIfNeeded() {
    if (!std::filesystem::exists(m_filePath)) return;
    
    if (std::filesystem::file_size(m_filePath) >= m_maxFileSize) {
        m_file.close();
        
        // Eski backup'ları sil
        for (int i = m_maxBackupCount - 1; i >= 0; --i) {
            auto oldPath = m_filePath.string() + "." + std::to_string(i);
            auto newPath = m_filePath.string() + "." + std::to_string(i + 1);
            
            if (i == m_maxBackupCount - 1) {
                std::filesystem::remove(oldPath);
            } else if (std::filesystem::exists(oldPath)) {
                std::filesystem::rename(oldPath, newPath);
            }
        }
        
        // Mevcut dosyayı .0 yap
        std::filesystem::rename(m_filePath, m_filePath.string() + ".0");
        
        // Yeni dosya aç
        m_file.open(m_filePath, std::ios::out);
    }
}

void FileLogOutput::write(const std::string& formattedEntry) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    rotateIfNeeded();
    
    if (m_file.is_open()) {
        m_file << formattedEntry;
    }
}

void FileLogOutput::flush() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_file.is_open()) {
        m_file.flush();
    }
}

// ==================== OperationLogger Implementation ====================

struct OperationLogger::Impl {
    LogLevel minLevel = LogLevel::Info;
    std::vector<std::shared_ptr<ILogOutput>> outputs;
    std::shared_ptr<ILogFormatter> formatter;
    
    std::vector<LogEntry> entries;
    std::vector<OperationRecord> operations;
    
    std::mutex mutex;
    
    // Async logging
    bool asyncEnabled = false;
    std::queue<LogEntry> pendingEntries;
    std::thread asyncThread;
    std::condition_variable cv;
    std::atomic<bool> running{false};
    
    void asyncLoop() {
        while (running) {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [this] { return !pendingEntries.empty() || !running; });
            
            while (!pendingEntries.empty()) {
                auto entry = pendingEntries.front();
                pendingEntries.pop();
                lock.unlock();
                
                std::string formatted = formatter->format(entry);
                for (auto& output : outputs) {
                    output->write(formatted);
                }
                
                lock.lock();
            }
        }
    }
};

OperationLogger::OperationLogger() : m_impl(std::make_unique<Impl>()) {
    m_impl->formatter = std::make_shared<TextLogFormatter>();
    m_impl->outputs.push_back(std::make_shared<ConsoleLogOutput>());
}

OperationLogger::~OperationLogger() {
    if (m_impl->running) {
        m_impl->running = false;
        m_impl->cv.notify_all();
        if (m_impl->asyncThread.joinable()) {
            m_impl->asyncThread.join();
        }
    }
}

OperationLogger& OperationLogger::getInstance() {
    static OperationLogger instance;
    return instance;
}

void OperationLogger::setMinLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->minLevel = level;
}

void OperationLogger::addOutput(std::shared_ptr<ILogOutput> output) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->outputs.push_back(output);
}

void OperationLogger::setFormatter(std::shared_ptr<ILogFormatter> formatter) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->formatter = formatter;
}

void OperationLogger::enableAsync(bool enable) {
    if (enable && !m_impl->running) {
        m_impl->running = true;
        m_impl->asyncEnabled = true;
        m_impl->asyncThread = std::thread(&Impl::asyncLoop, m_impl.get());
    } else if (!enable && m_impl->running) {
        m_impl->running = false;
        m_impl->cv.notify_all();
        if (m_impl->asyncThread.joinable()) {
            m_impl->asyncThread.join();
        }
        m_impl->asyncEnabled = false;
    }
}

void OperationLogger::log(LogLevel level, const std::string& category, 
                          const std::string& message, const char* file, int line) {
    if (level < m_impl->minLevel) return;
    
    LogEntry entry;
    entry.id = utils::IdGenerator::generateOperationId();
    entry.level = level;
    entry.timestamp = utils::TimeUtils::now();
    entry.category = category;
    entry.message = message;
    if (file) entry.file = file;
    entry.line = line;
    
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->entries.push_back(entry);
    
    if (m_impl->asyncEnabled) {
        m_impl->pendingEntries.push(entry);
        m_impl->cv.notify_one();
    } else {
        std::string formatted = m_impl->formatter->format(entry);
        for (auto& output : m_impl->outputs) {
            output->write(formatted);
        }
    }
}

void OperationLogger::trace(const std::string& category, const std::string& message) {
    log(LogLevel::Trace, category, message);
}

void OperationLogger::debug(const std::string& category, const std::string& message) {
    log(LogLevel::Debug, category, message);
}

void OperationLogger::info(const std::string& category, const std::string& message) {
    log(LogLevel::Info, category, message);
}

void OperationLogger::warning(const std::string& category, const std::string& message) {
    log(LogLevel::Warning, category, message);
}

void OperationLogger::error(const std::string& category, const std::string& message) {
    log(LogLevel::Error, category, message);
}

void OperationLogger::critical(const std::string& category, const std::string& message) {
    log(LogLevel::Critical, category, message);
}

OperationId OperationLogger::logOperation(OperationType type, const std::string& description,
                                          CheckpointId relatedCheckpoint) {
    OperationRecord record;
    record.id = utils::IdGenerator::generateOperationId();
    record.type = type;
    record.timestamp = utils::TimeUtils::now();
    record.relatedCheckpoint = relatedCheckpoint;
    record.description = description;
    record.canUndo = true;
    
    // Log mesajını hazırla
    std::string typeStr;
    switch (type) {
        case OperationType::Create: typeStr = "CREATE"; break;
        case OperationType::Update: typeStr = "UPDATE"; break;
        case OperationType::Delete: typeStr = "DELETE"; break;
        case OperationType::Checkpoint: typeStr = "CHECKPOINT"; break;
        case OperationType::Rollback: typeStr = "ROLLBACK"; break;
        case OperationType::Custom: typeStr = "CUSTOM"; break;
    }
    
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        m_impl->operations.push_back(record);
    }
    
    // Mutex dışında log yaz (deadlock önleme)
    info("Operation", "[" + typeStr + "] " + description);
    
    return record.id;
}

void OperationLogger::logOperationComplete(OperationId opId, bool success) {
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        
        for (auto& op : m_impl->operations) {
            if (op.id == opId) {
                if (!success) {
                    op.canUndo = false;
                }
                break;
            }
        }
    }
    
    // Mutex dışında log yaz (deadlock önleme)
    info("Operation", "Operation " + std::to_string(opId) + 
         (success ? " completed successfully" : " failed"));
}

std::vector<LogEntry> OperationLogger::getEntries(LogLevel minLevel, size_t maxCount) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    std::vector<LogEntry> result;
    for (auto it = m_impl->entries.rbegin(); 
         it != m_impl->entries.rend() && result.size() < maxCount; 
         ++it) {
        if (it->level >= minLevel) {
            result.push_back(*it);
        }
    }
    
    return result;
}

std::vector<LogEntry> OperationLogger::getEntriesBetween(Timestamp start, Timestamp end) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    std::vector<LogEntry> result;
    for (const auto& entry : m_impl->entries) {
        if (entry.timestamp >= start && entry.timestamp <= end) {
            result.push_back(entry);
        }
    }
    
    return result;
}

std::vector<LogEntry> OperationLogger::getEntriesForCheckpoint(CheckpointId checkpointId) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    std::vector<LogEntry> result;
    // Checkpoint ID'yi context'ten ara
    for (const auto& entry : m_impl->entries) {
        auto it = entry.context.find("checkpointId");
        if (it != entry.context.end() && std::stoull(it->second) == checkpointId) {
            result.push_back(entry);
        }
    }
    
    return result;
}

std::vector<OperationRecord> OperationLogger::getOperationHistory(size_t maxCount) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    std::vector<OperationRecord> result;
    size_t count = std::min(maxCount, m_impl->operations.size());
    
    for (size_t i = m_impl->operations.size() - count; i < m_impl->operations.size(); ++i) {
        result.push_back(m_impl->operations[i]);
    }
    
    return result;
}

std::vector<OperationRecord> OperationLogger::getOperationsSince(CheckpointId checkpointId) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    std::vector<OperationRecord> result;
    bool found = false;
    
    for (const auto& op : m_impl->operations) {
        if (op.relatedCheckpoint == checkpointId) {
            found = true;
        }
        if (found) {
            result.push_back(op);
        }
    }
    
    return result;
}

void OperationLogger::clear() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->entries.clear();
    m_impl->operations.clear();
}

void OperationLogger::flush() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& output : m_impl->outputs) {
        output->flush();
    }
}

} // namespace checkpoint
