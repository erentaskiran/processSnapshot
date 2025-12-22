#include "real_process/file_operation.hpp"
#include <fstream>
#include <sstream>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <mutex>
#include <atomic>
#include <map>
#include <algorithm>

namespace checkpoint {
namespace real_process {

// ============================================================================
// Helper Functions
// ============================================================================

std::string fileOpTypeToString(FileOperationType type) {
    switch (type) {
        case FileOperationType::CREATE:   return "CREATE";
        case FileOperationType::WRITE:    return "WRITE";
        case FileOperationType::TRUNCATE: return "TRUNCATE";
        case FileOperationType::DELETE:   return "DELETE";
        case FileOperationType::RENAME:   return "RENAME";
        case FileOperationType::CHMOD:    return "CHMOD";
        case FileOperationType::CHOWN:    return "CHOWN";
        case FileOperationType::MKDIR:    return "MKDIR";
        case FileOperationType::RMDIR:    return "RMDIR";
        case FileOperationType::SYMLINK:  return "SYMLINK";
        case FileOperationType::APPEND:   return "APPEND";
        default:                          return "UNKNOWN";
    }
}

FileOperationType stringToFileOpType(const std::string& str) {
    if (str == "CREATE")   return FileOperationType::CREATE;
    if (str == "WRITE")    return FileOperationType::WRITE;
    if (str == "TRUNCATE") return FileOperationType::TRUNCATE;
    if (str == "DELETE")   return FileOperationType::DELETE;
    if (str == "RENAME")   return FileOperationType::RENAME;
    if (str == "CHMOD")    return FileOperationType::CHMOD;
    if (str == "CHOWN")    return FileOperationType::CHOWN;
    if (str == "MKDIR")    return FileOperationType::MKDIR;
    if (str == "RMDIR")    return FileOperationType::RMDIR;
    if (str == "SYMLINK")  return FileOperationType::SYMLINK;
    if (str == "APPEND")   return FileOperationType::APPEND;
    return FileOperationType::WRITE;  // default
}

// ============================================================================
// FileOperation Implementation
// ============================================================================

std::vector<uint8_t> FileOperation::serialize() const {
    std::vector<uint8_t> data;
    
    // Helper to append data
    auto appendU64 = [&data](uint64_t val) {
        for (int i = 0; i < 8; i++) {
            data.push_back((val >> (i * 8)) & 0xFF);
        }
    };
    
    auto appendU32 = [&data](uint32_t val) {
        for (int i = 0; i < 4; i++) {
            data.push_back((val >> (i * 8)) & 0xFF);
        }
    };
    
    auto appendString = [&data, &appendU32](const std::string& str) {
        appendU32(static_cast<uint32_t>(str.size()));
        data.insert(data.end(), str.begin(), str.end());
    };
    
    auto appendBytes = [&data, &appendU64](const std::vector<uint8_t>& bytes) {
        appendU64(bytes.size());
        data.insert(data.end(), bytes.begin(), bytes.end());
    };
    
    // Serialize all fields
    appendU64(operationId);
    appendU32(static_cast<uint32_t>(type));
    
    auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
        timestamp.time_since_epoch()).count();
    appendU64(static_cast<uint64_t>(ts));
    
    appendString(path);
    appendString(originalPath);
    appendU32(static_cast<uint32_t>(fd));
    appendU64(static_cast<uint64_t>(offset));
    appendU64(originalSize);
    appendU64(newSize);
    
    // Original content
    data.push_back(originalContent.has_value() ? 1 : 0);
    if (originalContent.has_value()) {
        appendBytes(*originalContent);
    }
    
    // Diffs
    appendU32(static_cast<uint32_t>(diffs.size()));
    for (const auto& diff : diffs) {
        appendU64(static_cast<uint64_t>(diff.offset));
        appendBytes(diff.oldData);
        appendBytes(diff.newData);
    }
    
    // Metadata
    appendU32(originalMode);
    appendU32(newMode);
    appendU32(originalUid);
    appendU32(originalGid);
    appendU32(newUid);
    appendU32(newGid);
    
    // Status
    data.push_back(isReversible ? 1 : 0);
    data.push_back(wasReversed ? 1 : 0);
    appendString(description);
    appendU32(static_cast<uint32_t>(pid));
    
    return data;
}

FileOperation FileOperation::deserialize(const std::vector<uint8_t>& data) {
    FileOperation op;
    size_t pos = 0;
    
    auto readU64 = [&data, &pos]() -> uint64_t {
        uint64_t val = 0;
        for (int i = 0; i < 8 && pos < data.size(); i++, pos++) {
            val |= static_cast<uint64_t>(data[pos]) << (i * 8);
        }
        return val;
    };
    
    auto readU32 = [&data, &pos]() -> uint32_t {
        uint32_t val = 0;
        for (int i = 0; i < 4 && pos < data.size(); i++, pos++) {
            val |= static_cast<uint32_t>(data[pos]) << (i * 8);
        }
        return val;
    };
    
    auto readString = [&data, &pos, &readU32]() -> std::string {
        uint32_t len = readU32();
        std::string str(data.begin() + pos, data.begin() + pos + len);
        pos += len;
        return str;
    };
    
    auto readBytes = [&data, &pos, &readU64]() -> std::vector<uint8_t> {
        uint64_t len = readU64();
        std::vector<uint8_t> bytes(data.begin() + pos, data.begin() + pos + len);
        pos += len;
        return bytes;
    };
    
    // Deserialize all fields
    op.operationId = readU64();
    op.type = static_cast<FileOperationType>(readU32());
    
    auto ts = std::chrono::milliseconds(readU64());
    op.timestamp = std::chrono::system_clock::time_point(ts);
    
    op.path = readString();
    op.originalPath = readString();
    op.fd = static_cast<int>(readU32());
    op.offset = static_cast<off_t>(readU64());
    op.originalSize = readU64();
    op.newSize = readU64();
    
    // Original content
    bool hasContent = data[pos++] != 0;
    if (hasContent) {
        op.originalContent = readBytes();
    }
    
    // Diffs
    uint32_t diffCount = readU32();
    for (uint32_t i = 0; i < diffCount; i++) {
        FileContentDiff diff;
        diff.offset = static_cast<off_t>(readU64());
        diff.oldData = readBytes();
        diff.newData = readBytes();
        op.diffs.push_back(std::move(diff));
    }
    
    // Metadata
    op.originalMode = readU32();
    op.newMode = readU32();
    op.originalUid = static_cast<uid_t>(readU32());
    op.originalGid = static_cast<gid_t>(readU32());
    op.newUid = static_cast<uid_t>(readU32());
    op.newGid = static_cast<gid_t>(readU32());
    
    // Status
    op.isReversible = data[pos++] != 0;
    op.wasReversed = data[pos++] != 0;
    op.description = readString();
    op.pid = static_cast<pid_t>(readU32());
    
    return op;
}

size_t FileOperation::estimatedMemoryUsage() const {
    size_t usage = sizeof(FileOperation);
    usage += path.size();
    usage += originalPath.size();
    if (originalContent.has_value()) {
        usage += originalContent->size();
    }
    for (const auto& diff : diffs) {
        usage += sizeof(FileContentDiff);
        usage += diff.oldData.size();
        usage += diff.newData.size();
    }
    usage += description.size();
    return usage;
}

// ============================================================================
// FileOperationLog Implementation
// ============================================================================

struct FileOperationLog::Impl {
    std::vector<FileOperation> operations;
    std::map<uint64_t, size_t> checkpointMarkers;  // checkpointId -> operations index
    std::mutex mutex;
    std::atomic<uint64_t> nextOpId{1};
};

FileOperationLog::FileOperationLog() : m_impl(std::make_unique<Impl>()) {}

FileOperationLog::~FileOperationLog() = default;

FileOperationLog::FileOperationLog(FileOperationLog&&) noexcept = default;
FileOperationLog& FileOperationLog::operator=(FileOperationLog&&) noexcept = default;

uint64_t FileOperationLog::recordOperation(const FileOperation& op) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    FileOperation newOp = op;
    if (newOp.operationId == 0) {
        newOp.operationId = m_impl->nextOpId++;
    }
    m_impl->operations.push_back(std::move(newOp));
    return m_impl->operations.back().operationId;
}

void FileOperationLog::recordOperations(const std::vector<FileOperation>& ops) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (const auto& op : ops) {
        FileOperation newOp = op;
        if (newOp.operationId == 0) {
            newOp.operationId = m_impl->nextOpId++;
        }
        m_impl->operations.push_back(std::move(newOp));
    }
}

std::vector<FileOperation> FileOperationLog::getOperationsSince(uint64_t checkpointId) const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    auto it = m_impl->checkpointMarkers.find(checkpointId);
    if (it == m_impl->checkpointMarkers.end()) {
        return m_impl->operations;  // Return all if checkpoint not found
    }
    
    size_t startIndex = it->second;
    if (startIndex >= m_impl->operations.size()) {
        return {};
    }
    
    return std::vector<FileOperation>(
        m_impl->operations.begin() + startIndex,
        m_impl->operations.end()
    );
}

std::vector<FileOperation> FileOperationLog::getOperationsForFile(const std::string& path) const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    std::vector<FileOperation> result;
    
    for (const auto& op : m_impl->operations) {
        if (op.path == path || op.originalPath == path) {
            result.push_back(op);
        }
    }
    
    return result;
}

std::vector<FileOperation> FileOperationLog::getOperationsByType(FileOperationType type) const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    std::vector<FileOperation> result;
    
    for (const auto& op : m_impl->operations) {
        if (op.type == type) {
            result.push_back(op);
        }
    }
    
    return result;
}

std::vector<FileOperation> FileOperationLog::getAllOperations() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->operations;
}

std::optional<FileOperation> FileOperationLog::getOperation(uint64_t operationId) const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    for (const auto& op : m_impl->operations) {
        if (op.operationId == operationId) {
            return op;
        }
    }
    
    return std::nullopt;
}

std::vector<FileOperation> FileOperationLog::filterOperations(
    std::function<bool(const FileOperation&)> predicate) const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    std::vector<FileOperation> result;
    
    for (const auto& op : m_impl->operations) {
        if (predicate(op)) {
            result.push_back(op);
        }
    }
    
    return result;
}

void FileOperationLog::markCheckpoint(uint64_t checkpointId) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->checkpointMarkers[checkpointId] = m_impl->operations.size();
}

void FileOperationLog::clearOperationsBeforeCheckpoint(uint64_t checkpointId) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    auto it = m_impl->checkpointMarkers.find(checkpointId);
    if (it == m_impl->checkpointMarkers.end()) {
        return;
    }
    
    size_t removeCount = it->second;
    if (removeCount > 0 && removeCount <= m_impl->operations.size()) {
        m_impl->operations.erase(
            m_impl->operations.begin(),
            m_impl->operations.begin() + removeCount
        );
        
        // Update markers
        std::map<uint64_t, size_t> newMarkers;
        for (auto& marker : m_impl->checkpointMarkers) {
            if (marker.second >= removeCount) {
                newMarkers[marker.first] = marker.second - removeCount;
            }
        }
        m_impl->checkpointMarkers = std::move(newMarkers);
    }
}

size_t FileOperationLog::getOperationCount() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->operations.size();
}

void FileOperationLog::clear() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->operations.clear();
    m_impl->checkpointMarkers.clear();
}

std::vector<uint8_t> FileOperationLog::serialize() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    std::vector<uint8_t> data;
    
    // Header: magic + version
    data.push_back('F');
    data.push_back('O');
    data.push_back('L');
    data.push_back('G');
    data.push_back(1);  // version
    
    // Operation count
    uint64_t count = m_impl->operations.size();
    for (int i = 0; i < 8; i++) {
        data.push_back((count >> (i * 8)) & 0xFF);
    }
    
    // Serialize each operation
    for (const auto& op : m_impl->operations) {
        auto opData = op.serialize();
        uint64_t opSize = opData.size();
        for (int i = 0; i < 8; i++) {
            data.push_back((opSize >> (i * 8)) & 0xFF);
        }
        data.insert(data.end(), opData.begin(), opData.end());
    }
    
    // Serialize checkpoint markers
    uint32_t markerCount = static_cast<uint32_t>(m_impl->checkpointMarkers.size());
    for (int i = 0; i < 4; i++) {
        data.push_back((markerCount >> (i * 8)) & 0xFF);
    }
    for (const auto& marker : m_impl->checkpointMarkers) {
        for (int i = 0; i < 8; i++) {
            data.push_back((marker.first >> (i * 8)) & 0xFF);
        }
        uint64_t idx = marker.second;
        for (int i = 0; i < 8; i++) {
            data.push_back((idx >> (i * 8)) & 0xFF);
        }
    }
    
    return data;
}

FileOperationLog FileOperationLog::deserialize(const std::vector<uint8_t>& data) {
    FileOperationLog log;
    size_t pos = 0;
    
    // Check header
    if (data.size() < 5 || data[0] != 'F' || data[1] != 'O' || 
        data[2] != 'L' || data[3] != 'G') {
        return log;
    }
    pos = 5;  // Skip header
    
    auto readU64 = [&data, &pos]() -> uint64_t {
        uint64_t val = 0;
        for (int i = 0; i < 8 && pos < data.size(); i++, pos++) {
            val |= static_cast<uint64_t>(data[pos]) << (i * 8);
        }
        return val;
    };
    
    auto readU32 = [&data, &pos]() -> uint32_t {
        uint32_t val = 0;
        for (int i = 0; i < 4 && pos < data.size(); i++, pos++) {
            val |= static_cast<uint32_t>(data[pos]) << (i * 8);
        }
        return val;
    };
    
    // Read operations
    uint64_t count = readU64();
    for (uint64_t i = 0; i < count && pos < data.size(); i++) {
        uint64_t opSize = readU64();
        if (pos + opSize > data.size()) break;
        
        std::vector<uint8_t> opData(data.begin() + pos, data.begin() + pos + opSize);
        pos += opSize;
        
        auto op = FileOperation::deserialize(opData);
        log.recordOperation(op);
    }
    
    // Read checkpoint markers
    if (pos < data.size()) {
        uint32_t markerCount = readU32();
        for (uint32_t i = 0; i < markerCount && pos < data.size(); i++) {
            uint64_t checkpointId = readU64();
            uint64_t idx = readU64();
            log.m_impl->checkpointMarkers[checkpointId] = idx;
        }
    }
    
    return log;
}

bool FileOperationLog::saveToFile(const std::filesystem::path& path) const {
    auto data = serialize();
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    return file.good();
}

bool FileOperationLog::loadFromFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);
    
    if (!file) return false;
    
    *this = std::move(FileOperationLog::deserialize(data));
    return true;
}

// ============================================================================
// FileOperationTracker Implementation
// ============================================================================

struct FileOperationTracker::Impl {
    FileOperationLog log;
    FileTrackingOptions options;
    std::mutex mutex;
    std::atomic<bool> isTracking{false};
    pid_t trackedPid{0};
    std::atomic<uint64_t> nextOpId{1};
    
    // Pending operations (before -> after)
    struct PendingOp {
        FileOperation op;
        std::chrono::system_clock::time_point startTime;
    };
    std::map<std::string, PendingOp> pendingWrites;     // path -> pending
    std::map<std::string, PendingOp> pendingTruncates;
    std::map<std::string, PendingOp> pendingUnlinks;
    std::map<std::string, PendingOp> pendingRenames;    // oldPath -> pending
    std::map<std::string, PendingOp> pendingCreates;
};

FileOperationTracker::FileOperationTracker() 
    : m_impl(std::make_unique<Impl>()) {}

FileOperationTracker::FileOperationTracker(const FileTrackingOptions& options)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->options = options;
}

FileOperationTracker::~FileOperationTracker() {
    stopTracking();
}

void FileOperationTracker::startTracking(pid_t pid) {
    m_impl->trackedPid = pid;
    m_impl->isTracking = true;
}

void FileOperationTracker::stopTracking() {
    m_impl->isTracking = false;
}

bool FileOperationTracker::isTracking() const {
    return m_impl->isTracking;
}

bool FileOperationTracker::shouldTrackFile(const std::string& path) const {
    // Check exclude paths first
    for (const auto& excl : m_impl->options.excludePaths) {
        if (path.find(excl) == 0) {
            return false;
        }
    }
    
    // Check include paths (if specified)
    if (!m_impl->options.includePaths.empty()) {
        bool included = false;
        for (const auto& incl : m_impl->options.includePaths) {
            if (path.find(incl) == 0) {
                included = true;
                break;
            }
        }
        if (!included) return false;
    }
    
    // Check extensions
    std::string ext;
    auto dotPos = path.rfind('.');
    if (dotPos != std::string::npos) {
        ext = path.substr(dotPos + 1);
    }
    
    // Check exclude extensions
    for (const auto& excl : m_impl->options.excludeExtensions) {
        if (ext == excl) return false;
    }
    
    // Check include extensions (if specified)
    if (!m_impl->options.includeExtensions.empty()) {
        bool included = false;
        for (const auto& incl : m_impl->options.includeExtensions) {
            if (ext == incl) {
                included = true;
                break;
            }
        }
        if (!included) return false;
    }
    
    return true;
}

uint64_t FileOperationTracker::generateOperationId() {
    return m_impl->nextOpId++;
}

std::vector<uint8_t> FileOperationTracker::captureFileContent(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    
    // Boyut sınırlaması
    if (size > m_impl->options.maxFileSize) {
        return {};  // Too large for full backup
    }
    
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> content(size);
    file.read(reinterpret_cast<char*>(content.data()), size);
    
    return content;
}

std::vector<uint8_t> FileOperationTracker::captureFileContent(
    const std::string& path, off_t offset, size_t size) {
    
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    
    file.seekg(offset, std::ios::beg);
    std::vector<uint8_t> content(size);
    file.read(reinterpret_cast<char*>(content.data()), size);
    
    return content;
}

void FileOperationTracker::beforeWrite(int fd, const std::string& path, 
                                        off_t offset, size_t size) {
    if (!m_impl->isTracking || !shouldTrackFile(path)) return;
    
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    FileOperation op;
    op.operationId = generateOperationId();
    op.type = FileOperationType::WRITE;
    op.timestamp = std::chrono::system_clock::now();
    op.path = path;
    op.fd = fd;
    op.offset = offset;
    op.pid = m_impl->trackedPid;
    
    // Get file stats before write
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        op.originalSize = st.st_size;
        op.originalMode = st.st_mode;
        op.originalUid = st.st_uid;
        op.originalGid = st.st_gid;
    }
    
    // Capture content that will be overwritten
    if (size <= m_impl->options.maxFileSize) {
        // For small writes, capture the affected region
        auto oldContent = captureFileContent(path, offset, size);
        if (!oldContent.empty()) {
            FileContentDiff diff;
            diff.offset = offset;
            diff.oldData = std::move(oldContent);
            op.diffs.push_back(std::move(diff));
        }
    }
    
    Impl::PendingOp pending{op, std::chrono::system_clock::now()};
    m_impl->pendingWrites[path] = std::move(pending);
}

void FileOperationTracker::afterWrite(int fd, const std::string& path,
                                       off_t offset, size_t size, bool success) {
    if (!m_impl->isTracking) return;
    
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    auto it = m_impl->pendingWrites.find(path);
    if (it == m_impl->pendingWrites.end()) return;
    
    FileOperation op = std::move(it->second.op);
    m_impl->pendingWrites.erase(it);
    
    if (!success) {
        op.isReversible = false;
        op.description = "Write failed";
    } else {
        // Get new file size
        struct stat st;
        if (stat(path.c_str(), &st) == 0) {
            op.newSize = st.st_size;
        }
        
        // Capture new content for diff
        if (!op.diffs.empty() && size <= m_impl->options.maxFileSize) {
            auto newContent = captureFileContent(path, offset, size);
            op.diffs[0].newData = std::move(newContent);
        }
    }
    
    m_impl->log.recordOperation(op);
}

void FileOperationTracker::beforeTruncate(const std::string& path, off_t length) {
    if (!m_impl->isTracking || !shouldTrackFile(path)) return;
    
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    FileOperation op;
    op.operationId = generateOperationId();
    op.type = FileOperationType::TRUNCATE;
    op.timestamp = std::chrono::system_clock::now();
    op.path = path;
    op.offset = length;
    op.pid = m_impl->trackedPid;
    
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        op.originalSize = st.st_size;
        op.originalMode = st.st_mode;
        
        // If truncating, capture content that will be removed
        if (static_cast<size_t>(length) < st.st_size) {
            size_t removeSize = st.st_size - length;
            if (removeSize <= m_impl->options.maxFileSize) {
                auto content = captureFileContent(path, length, removeSize);
                if (!content.empty()) {
                    FileContentDiff diff;
                    diff.offset = length;
                    diff.oldData = std::move(content);
                    op.diffs.push_back(std::move(diff));
                }
            }
        }
    }
    
    // Full backup for small files
    if (op.originalSize <= m_impl->options.maxFileSize) {
        op.originalContent = captureFileContent(path);
    }
    
    Impl::PendingOp pending{op, std::chrono::system_clock::now()};
    m_impl->pendingTruncates[path] = std::move(pending);
}

void FileOperationTracker::afterTruncate(const std::string& path, off_t length, bool success) {
    if (!m_impl->isTracking) return;
    
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    auto it = m_impl->pendingTruncates.find(path);
    if (it == m_impl->pendingTruncates.end()) return;
    
    FileOperation op = std::move(it->second.op);
    m_impl->pendingTruncates.erase(it);
    
    op.isReversible = success && op.hasFullBackup();
    if (success) {
        op.newSize = length;
    }
    
    m_impl->log.recordOperation(op);
}

void FileOperationTracker::beforeUnlink(const std::string& path) {
    if (!m_impl->isTracking || !shouldTrackFile(path)) return;
    
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    FileOperation op;
    op.operationId = generateOperationId();
    op.type = FileOperationType::DELETE;
    op.timestamp = std::chrono::system_clock::now();
    op.path = path;
    op.pid = m_impl->trackedPid;
    
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        op.originalSize = st.st_size;
        op.originalMode = st.st_mode;
        op.originalUid = st.st_uid;
        op.originalGid = st.st_gid;
        
        // Full backup before delete
        if (st.st_size <= static_cast<off_t>(m_impl->options.maxFileSize)) {
            op.originalContent = captureFileContent(path);
        }
    }
    
    Impl::PendingOp pending{op, std::chrono::system_clock::now()};
    m_impl->pendingUnlinks[path] = std::move(pending);
}

void FileOperationTracker::afterUnlink(const std::string& path, bool success) {
    if (!m_impl->isTracking) return;
    
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    auto it = m_impl->pendingUnlinks.find(path);
    if (it == m_impl->pendingUnlinks.end()) return;
    
    FileOperation op = std::move(it->second.op);
    m_impl->pendingUnlinks.erase(it);
    
    op.isReversible = success && op.hasFullBackup();
    op.newSize = 0;
    
    m_impl->log.recordOperation(op);
}

void FileOperationTracker::beforeRename(const std::string& oldPath, const std::string& newPath) {
    if (!m_impl->isTracking) return;
    if (!shouldTrackFile(oldPath) && !shouldTrackFile(newPath)) return;
    
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    FileOperation op;
    op.operationId = generateOperationId();
    op.type = FileOperationType::RENAME;
    op.timestamp = std::chrono::system_clock::now();
    op.path = newPath;
    op.originalPath = oldPath;
    op.pid = m_impl->trackedPid;
    
    struct stat st;
    if (stat(oldPath.c_str(), &st) == 0) {
        op.originalSize = st.st_size;
        op.originalMode = st.st_mode;
        op.originalUid = st.st_uid;
        op.originalGid = st.st_gid;
    }
    
    // Check if newPath exists (will be overwritten)
    if (stat(newPath.c_str(), &st) == 0) {
        // Capture content that will be overwritten
        if (st.st_size <= static_cast<off_t>(m_impl->options.maxFileSize)) {
            op.originalContent = captureFileContent(newPath);
        }
    }
    
    Impl::PendingOp pending{op, std::chrono::system_clock::now()};
    m_impl->pendingRenames[oldPath] = std::move(pending);
}

void FileOperationTracker::afterRename(const std::string& oldPath, 
                                        const std::string& newPath, bool success) {
    if (!m_impl->isTracking) return;
    
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    auto it = m_impl->pendingRenames.find(oldPath);
    if (it == m_impl->pendingRenames.end()) return;
    
    FileOperation op = std::move(it->second.op);
    m_impl->pendingRenames.erase(it);
    
    op.isReversible = success;
    if (success) {
        struct stat st;
        if (stat(newPath.c_str(), &st) == 0) {
            op.newSize = st.st_size;
        }
    }
    
    m_impl->log.recordOperation(op);
}

void FileOperationTracker::beforeCreate(const std::string& path, mode_t mode) {
    if (!m_impl->isTracking || !shouldTrackFile(path)) return;
    
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    FileOperation op;
    op.operationId = generateOperationId();
    op.type = FileOperationType::CREATE;
    op.timestamp = std::chrono::system_clock::now();
    op.path = path;
    op.newMode = mode;
    op.originalSize = 0;
    op.newSize = 0;
    op.pid = m_impl->trackedPid;
    
    Impl::PendingOp pending{op, std::chrono::system_clock::now()};
    m_impl->pendingCreates[path] = std::move(pending);
}

void FileOperationTracker::afterCreate(const std::string& path, int fd, bool success) {
    if (!m_impl->isTracking) return;
    
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    auto it = m_impl->pendingCreates.find(path);
    if (it == m_impl->pendingCreates.end()) return;
    
    FileOperation op = std::move(it->second.op);
    m_impl->pendingCreates.erase(it);
    
    op.fd = fd;
    op.isReversible = success;  // Can reverse by deleting
    
    m_impl->log.recordOperation(op);
}

FileOperationLog& FileOperationTracker::getLog() {
    return m_impl->log;
}

const FileOperationLog& FileOperationTracker::getLog() const {
    return m_impl->log;
}

void FileOperationTracker::setOptions(const FileTrackingOptions& options) {
    m_impl->options = options;
}

const FileTrackingOptions& FileOperationTracker::getOptions() const {
    return m_impl->options;
}

void FileOperationTracker::onCheckpointCreated(uint64_t checkpointId) {
    m_impl->log.markCheckpoint(checkpointId);
}

size_t FileOperationTracker::getTrackedOperationCount() const {
    return m_impl->log.getOperationCount();
}

size_t FileOperationTracker::getTotalBackupSize() const {
    size_t total = 0;
    for (const auto& op : m_impl->log.getAllOperations()) {
        total += op.estimatedMemoryUsage();
    }
    return total;
}

} // namespace real_process
} // namespace checkpoint
