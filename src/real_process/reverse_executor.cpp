#include "real_process/reverse_executor.hpp"
#include <fstream>
#include <cstring>
#include <sys/stat.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <chrono>

namespace checkpoint {
namespace real_process {

// ============================================================================
// ReverseExecutor Implementation
// ============================================================================

struct ReverseExecutor::Impl {
    ReverseOptions options;
    ProgressCallback progressCallback;
    std::mutex mutex;
    
    // Statistics
    std::atomic<size_t> totalReversed{0};
    std::atomic<size_t> totalBytesRestored{0};
};

ReverseExecutor::ReverseExecutor() 
    : m_impl(std::make_unique<Impl>()) {}

ReverseExecutor::ReverseExecutor(const ReverseOptions& options)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->options = options;
}

ReverseExecutor::~ReverseExecutor() = default;

void ReverseExecutor::setOptions(const ReverseOptions& options) {
    m_impl->options = options;
}

const ReverseOptions& ReverseExecutor::getOptions() const {
    return m_impl->options;
}

void ReverseExecutor::setProgressCallback(ProgressCallback callback) {
    m_impl->progressCallback = std::move(callback);
}

size_t ReverseExecutor::getTotalReversedCount() const {
    return m_impl->totalReversed;
}

size_t ReverseExecutor::getTotalBytesRestored() const {
    return m_impl->totalBytesRestored;
}

bool ReverseExecutor::shouldProcess(const FileOperation& op) const {
    // Check type filters
    if (!m_impl->options.includeTypes.empty()) {
        bool found = std::find(m_impl->options.includeTypes.begin(),
                               m_impl->options.includeTypes.end(),
                               op.type) != m_impl->options.includeTypes.end();
        if (!found) return false;
    }
    
    for (const auto& excl : m_impl->options.excludeTypes) {
        if (op.type == excl) return false;
    }
    
    // Custom filter
    if (m_impl->options.filter && !m_impl->options.filter(op)) {
        return false;
    }
    
    return true;
}

bool ReverseExecutor::canReverse(const FileOperation& op) const {
    if (!op.isReversible) return false;
    if (op.wasReversed) return false;
    
    switch (op.type) {
        case FileOperationType::CREATE:
            // Can reverse by deleting the file
            return std::filesystem::exists(op.path);
            
        case FileOperationType::WRITE:
        case FileOperationType::TRUNCATE:
            // Need either full backup or diffs
            return op.hasFullBackup() || op.hasDiffs();
            
        case FileOperationType::APPEND:
            // Can reverse by truncating to original size (no backup needed)
            return std::filesystem::exists(op.path) && op.originalSize < op.newSize;
            
        case FileOperationType::DELETE:
            // Need full backup to recreate
            return op.hasFullBackup();
            
        case FileOperationType::RENAME:
            // Can reverse by renaming back
            return std::filesystem::exists(op.path);
            
        case FileOperationType::CHMOD:
        case FileOperationType::CHOWN:
            // Need original metadata
            return op.originalMode != 0 || op.originalUid != 0 || op.originalGid != 0;
            
        case FileOperationType::MKDIR:
            // Can reverse by removing empty directory
            return std::filesystem::exists(op.path) && 
                   std::filesystem::is_directory(op.path) &&
                   std::filesystem::is_empty(op.path);
            
        case FileOperationType::RMDIR:
            // Can recreate directory
            return true;
            
        case FileOperationType::SYMLINK:
            // Can remove symlink
            return std::filesystem::exists(op.path) && 
                   std::filesystem::is_symlink(op.path);
            
        default:
            return false;
    }
}

std::vector<std::string> ReverseExecutor::validateReverse(const FileOperation& op) const {
    std::vector<std::string> issues;
    
    if (!op.isReversible) {
        issues.push_back("Operation marked as not reversible");
    }
    
    if (op.wasReversed) {
        issues.push_back("Operation already reversed");
    }
    
    switch (op.type) {
        case FileOperationType::CREATE:
            if (!std::filesystem::exists(op.path)) {
                issues.push_back("File no longer exists: " + op.path);
            }
            break;
            
        case FileOperationType::WRITE:
        case FileOperationType::TRUNCATE:
            if (!op.hasFullBackup() && !op.hasDiffs()) {
                issues.push_back("No backup data available");
            }
            if (!std::filesystem::exists(op.path)) {
                issues.push_back("Target file no longer exists: " + op.path);
            }
            break;
            
        case FileOperationType::APPEND:
            // APPEND can be reversed by truncating - no backup needed
            if (!std::filesystem::exists(op.path)) {
                issues.push_back("Target file no longer exists: " + op.path);
            }
            if (op.originalSize >= op.newSize) {
                issues.push_back("Invalid size information for append reversal");
            }
            break;
            
        case FileOperationType::DELETE:
            if (!op.hasFullBackup()) {
                issues.push_back("No backup data to restore deleted file");
            }
            if (std::filesystem::exists(op.path)) {
                issues.push_back("File already exists at: " + op.path);
            }
            break;
            
        case FileOperationType::RENAME:
            if (!std::filesystem::exists(op.path)) {
                issues.push_back("Renamed file no longer exists: " + op.path);
            }
            if (std::filesystem::exists(op.originalPath)) {
                issues.push_back("Original path already exists: " + op.originalPath);
            }
            break;
            
        default:
            break;
    }
    
    return issues;
}

bool ReverseExecutor::createBackup(const std::string& path) {
    if (!std::filesystem::exists(path)) return true;
    
    try {
        std::filesystem::create_directories(m_impl->options.backupDir);
        
        auto filename = std::filesystem::path(path).filename().string();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        std::string backupPath = m_impl->options.backupDir + "/" + 
                                  filename + "." + std::to_string(timestamp) + ".bak";
        
        std::filesystem::copy_file(path, backupPath,
            std::filesystem::copy_options::overwrite_existing);
        
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

ReverseResult ReverseExecutor::reverseOperation(const FileOperation& op) {
    if (!shouldProcess(op)) {
        return ReverseResult::error(op.operationId, "Operation filtered out");
    }
    
    if (m_impl->options.validateBeforeReverse) {
        auto issues = validateReverse(op);
        if (!issues.empty()) {
            return ReverseResult::error(op.operationId, issues[0]);
        }
    }
    
    if (m_impl->options.dryRun) {
        return ReverseResult::ok(op.operationId, op.path, 0);
    }
    
    if (m_impl->options.createBackups && !op.path.empty()) {
        if (!createBackup(op.path)) {
            return ReverseResult::error(op.operationId, "Failed to create backup");
        }
    }
    
    ReverseResult result;
    
    switch (op.type) {
        case FileOperationType::CREATE:
            result = reverseCreate(op);
            break;
        case FileOperationType::WRITE:
            result = reverseWrite(op);
            break;
        case FileOperationType::TRUNCATE:
            result = reverseTruncate(op);
            break;
        case FileOperationType::DELETE:
            result = reverseDelete(op);
            break;
        case FileOperationType::RENAME:
            result = reverseRename(op);
            break;
        case FileOperationType::CHMOD:
            result = reverseChmod(op);
            break;
        case FileOperationType::CHOWN:
            result = reverseChown(op);
            break;
        case FileOperationType::MKDIR:
            result = reverseMkdir(op);
            break;
        case FileOperationType::RMDIR:
            result = reverseRmdir(op);
            break;
        case FileOperationType::SYMLINK:
            result = reverseSymlink(op);
            break;
        case FileOperationType::APPEND:
            result = reverseAppend(op);
            break;
        default:
            result = ReverseResult::error(op.operationId, "Unknown operation type");
    }
    
    if (result.success) {
        m_impl->totalReversed++;
        m_impl->totalBytesRestored += result.bytesRestored;
    }
    
    return result;
}

ReverseResult ReverseExecutor::reverseCreate(const FileOperation& op) {
    // Reverse CREATE by deleting the file
    try {
        if (std::filesystem::remove(op.path)) {
            return ReverseResult::ok(op.operationId, op.path);
        }
        return ReverseResult::error(op.operationId, "Failed to remove file");
    } catch (const std::exception& e) {
        return ReverseResult::error(op.operationId, e.what());
    }
}

ReverseResult ReverseExecutor::reverseWrite(const FileOperation& op) {
    // Reverse WRITE by restoring original content
    try {
        if (op.hasFullBackup()) {
            // Full restore
            std::ofstream file(op.path, std::ios::binary | std::ios::trunc);
            if (!file) {
                return ReverseResult::error(op.operationId, "Cannot open file for writing");
            }
            file.write(reinterpret_cast<const char*>(op.originalContent->data()),
                       op.originalContent->size());
            return ReverseResult::ok(op.operationId, op.path, op.originalContent->size());
        }
        
        if (op.hasDiffs()) {
            // Apply diffs in reverse
            std::fstream file(op.path, std::ios::binary | std::ios::in | std::ios::out);
            if (!file) {
                return ReverseResult::error(op.operationId, "Cannot open file for writing");
            }
            
            size_t bytesRestored = 0;
            
            // Apply diffs in reverse order
            for (auto it = op.diffs.rbegin(); it != op.diffs.rend(); ++it) {
                const auto& diff = *it;
                file.seekp(diff.offset, std::ios::beg);
                file.write(reinterpret_cast<const char*>(diff.oldData.data()),
                          diff.oldData.size());
                bytesRestored += diff.oldData.size();
            }
            
            // Restore original size if needed
            if (op.originalSize != op.newSize) {
                std::filesystem::resize_file(op.path, op.originalSize);
            }
            
            return ReverseResult::ok(op.operationId, op.path, bytesRestored);
        }
        
        return ReverseResult::error(op.operationId, "No backup data available");
    } catch (const std::exception& e) {
        return ReverseResult::error(op.operationId, e.what());
    }
}

ReverseResult ReverseExecutor::reverseTruncate(const FileOperation& op) {
    // Reverse TRUNCATE by restoring truncated content
    try {
        if (op.hasFullBackup()) {
            std::ofstream file(op.path, std::ios::binary | std::ios::trunc);
            if (!file) {
                return ReverseResult::error(op.operationId, "Cannot open file for writing");
            }
            file.write(reinterpret_cast<const char*>(op.originalContent->data()),
                       op.originalContent->size());
            return ReverseResult::ok(op.operationId, op.path, op.originalContent->size());
        }
        
        if (op.hasDiffs()) {
            // Restore to original size first
            std::filesystem::resize_file(op.path, op.originalSize);
            
            // Write back truncated data
            std::fstream file(op.path, std::ios::binary | std::ios::in | std::ios::out);
            if (!file) {
                return ReverseResult::error(op.operationId, "Cannot open file for writing");
            }
            
            size_t bytesRestored = 0;
            for (const auto& diff : op.diffs) {
                file.seekp(diff.offset, std::ios::beg);
                file.write(reinterpret_cast<const char*>(diff.oldData.data()),
                          diff.oldData.size());
                bytesRestored += diff.oldData.size();
            }
            
            return ReverseResult::ok(op.operationId, op.path, bytesRestored);
        }
        
        return ReverseResult::error(op.operationId, "No backup data available");
    } catch (const std::exception& e) {
        return ReverseResult::error(op.operationId, e.what());
    }
}

ReverseResult ReverseExecutor::reverseDelete(const FileOperation& op) {
    // Reverse DELETE by recreating the file
    try {
        if (!op.hasFullBackup()) {
            return ReverseResult::error(op.operationId, "No backup data to restore");
        }
        
        // Create parent directories if needed
        auto parentDir = std::filesystem::path(op.path).parent_path();
        if (!parentDir.empty()) {
            std::filesystem::create_directories(parentDir);
        }
        
        // Write content
        std::ofstream file(op.path, std::ios::binary);
        if (!file) {
            return ReverseResult::error(op.operationId, "Cannot create file");
        }
        file.write(reinterpret_cast<const char*>(op.originalContent->data()),
                   op.originalContent->size());
        file.close();
        
        // Restore permissions
        if (op.originalMode != 0) {
            chmod(op.path.c_str(), op.originalMode);
        }
        
        // Restore ownership (requires root)
        if (op.originalUid != 0 || op.originalGid != 0) {
            chown(op.path.c_str(), op.originalUid, op.originalGid);
        }
        
        return ReverseResult::ok(op.operationId, op.path, op.originalContent->size());
    } catch (const std::exception& e) {
        return ReverseResult::error(op.operationId, e.what());
    }
}

ReverseResult ReverseExecutor::reverseRename(const FileOperation& op) {
    // Reverse RENAME by renaming back
    try {
        std::filesystem::rename(op.path, op.originalPath);
        
        // If there was content at newPath that was overwritten, restore it
        if (op.hasFullBackup()) {
            std::ofstream file(op.path, std::ios::binary);
            if (file) {
                file.write(reinterpret_cast<const char*>(op.originalContent->data()),
                           op.originalContent->size());
            }
        }
        
        return ReverseResult::ok(op.operationId, op.originalPath);
    } catch (const std::exception& e) {
        return ReverseResult::error(op.operationId, e.what());
    }
}

ReverseResult ReverseExecutor::reverseChmod(const FileOperation& op) {
    try {
        if (chmod(op.path.c_str(), op.originalMode) == 0) {
            return ReverseResult::ok(op.operationId, op.path);
        }
        return ReverseResult::error(op.operationId, strerror(errno));
    } catch (const std::exception& e) {
        return ReverseResult::error(op.operationId, e.what());
    }
}

ReverseResult ReverseExecutor::reverseChown(const FileOperation& op) {
    try {
        if (chown(op.path.c_str(), op.originalUid, op.originalGid) == 0) {
            return ReverseResult::ok(op.operationId, op.path);
        }
        return ReverseResult::error(op.operationId, strerror(errno));
    } catch (const std::exception& e) {
        return ReverseResult::error(op.operationId, e.what());
    }
}

ReverseResult ReverseExecutor::reverseMkdir(const FileOperation& op) {
    try {
        if (std::filesystem::remove(op.path)) {
            return ReverseResult::ok(op.operationId, op.path);
        }
        return ReverseResult::error(op.operationId, "Failed to remove directory");
    } catch (const std::exception& e) {
        return ReverseResult::error(op.operationId, e.what());
    }
}

ReverseResult ReverseExecutor::reverseRmdir(const FileOperation& op) {
    try {
        std::filesystem::create_directories(op.path);
        
        // Restore original permissions
        if (op.originalMode != 0) {
            chmod(op.path.c_str(), op.originalMode);
        }
        
        return ReverseResult::ok(op.operationId, op.path);
    } catch (const std::exception& e) {
        return ReverseResult::error(op.operationId, e.what());
    }
}

ReverseResult ReverseExecutor::reverseSymlink(const FileOperation& op) {
    try {
        if (std::filesystem::remove(op.path)) {
            return ReverseResult::ok(op.operationId, op.path);
        }
        return ReverseResult::error(op.operationId, "Failed to remove symlink");
    } catch (const std::exception& e) {
        return ReverseResult::error(op.operationId, e.what());
    }
}

ReverseResult ReverseExecutor::reverseAppend(const FileOperation& op) {
    // Reverse APPEND by truncating to original size
    try {
        std::filesystem::resize_file(op.path, op.originalSize);
        return ReverseResult::ok(op.operationId, op.path, op.newSize - op.originalSize);
    } catch (const std::exception& e) {
        return ReverseResult::error(op.operationId, e.what());
    }
}

std::vector<FileOperation> ReverseExecutor::sortOperations(
    const std::vector<FileOperation>& ops) const {
    
    std::vector<FileOperation> sorted = ops;
    
    if (m_impl->options.reverseNewestFirst) {
        // Sort by timestamp descending (newest first) - LIFO order
        std::sort(sorted.begin(), sorted.end(),
            [](const FileOperation& a, const FileOperation& b) {
                return a.timestamp > b.timestamp;
            });
    }
    
    return sorted;
}

BatchReverseResult ReverseExecutor::reverseOperations(const std::vector<FileOperation>& ops) {
    BatchReverseResult result;
    result.totalOperations = ops.size();
    
    auto startTime = std::chrono::steady_clock::now();
    auto sortedOps = sortOperations(ops);
    
    for (size_t i = 0; i < sortedOps.size(); ++i) {
        const auto& op = sortedOps[i];
        
        if (m_impl->progressCallback) {
            m_impl->progressCallback(i + 1, sortedOps.size(), 
                "Reversing: " + fileOpTypeToString(op.type) + " " + op.path);
        }
        
        if (!shouldProcess(op)) {
            result.skippedCount++;
            continue;
        }
        
        auto reverseResult = reverseOperation(op);
        result.results.push_back(reverseResult);
        
        if (reverseResult.success) {
            result.successCount++;
        } else {
            result.failCount++;
            if (m_impl->options.stopOnError) {
                break;
            }
        }
    }
    
    auto endTime = std::chrono::steady_clock::now();
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime);
    
    result.allSucceeded = (result.failCount == 0);
    
    return result;
}

BatchReverseResult ReverseExecutor::reverseToCheckpoint(
    const FileOperationLog& log, uint64_t checkpointId) {
    
    auto ops = log.getOperationsSince(checkpointId);
    return reverseOperations(ops);
}

BatchReverseResult ReverseExecutor::reverseAll(const FileOperationLog& log) {
    auto ops = log.getAllOperations();
    return reverseOperations(ops);
}

std::vector<std::string> ReverseExecutor::previewReverse(
    const std::vector<FileOperation>& ops) {
    
    std::vector<std::string> preview;
    auto sortedOps = sortOperations(ops);
    
    for (const auto& op : sortedOps) {
        if (!shouldProcess(op)) continue;
        
        std::string action;
        switch (op.type) {
            case FileOperationType::CREATE:
                action = "DELETE " + op.path;
                break;
            case FileOperationType::WRITE:
                action = "RESTORE content of " + op.path + " (offset: " + 
                         std::to_string(op.offset) + ")";
                break;
            case FileOperationType::TRUNCATE:
                action = "RESTORE " + op.path + " to size " + 
                         std::to_string(op.originalSize);
                break;
            case FileOperationType::DELETE:
                action = "RECREATE " + op.path + " (" + 
                         std::to_string(op.originalSize) + " bytes)";
                break;
            case FileOperationType::RENAME:
                action = "RENAME " + op.path + " -> " + op.originalPath;
                break;
            case FileOperationType::CHMOD:
                action = "CHMOD " + op.path + " to " + 
                         std::to_string(op.originalMode);
                break;
            case FileOperationType::CHOWN:
                action = "CHOWN " + op.path + " to " + 
                         std::to_string(op.originalUid) + ":" + 
                         std::to_string(op.originalGid);
                break;
            case FileOperationType::MKDIR:
                action = "RMDIR " + op.path;
                break;
            case FileOperationType::RMDIR:
                action = "MKDIR " + op.path;
                break;
            case FileOperationType::SYMLINK:
                action = "UNLINK " + op.path;
                break;
            case FileOperationType::APPEND:
                action = "TRUNCATE " + op.path + " to " + 
                         std::to_string(op.originalSize);
                break;
        }
        
        auto issues = validateReverse(op);
        if (!issues.empty()) {
            action += " [WARNING: " + issues[0] + "]";
        }
        
        preview.push_back(action);
    }
    
    return preview;
}

// ============================================================================
// SyscallInterceptor Implementation
// ============================================================================

struct SyscallInterceptor::Impl {
    std::atomic<bool> attached{false};
    std::atomic<bool> intercepting{false};
    pid_t targetPid{0};
    std::shared_ptr<FileOperationTracker> tracker;
    SyscallCallback callback;
    
    bool interceptWrite{true};
    bool interceptOpen{true};
    bool interceptUnlink{true};
    bool interceptRename{true};
    bool interceptTruncate{true};
    
    std::mutex mutex;
};

SyscallInterceptor::SyscallInterceptor()
    : m_impl(std::make_unique<Impl>()) {}

SyscallInterceptor::~SyscallInterceptor() {
    detach();
}

bool SyscallInterceptor::attach(pid_t pid) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    if (m_impl->attached) {
        detach();
    }
    
    // Attach to process using ptrace
    if (ptrace(PTRACE_ATTACH, pid, nullptr, nullptr) == -1) {
        return false;
    }
    
    // Wait for the process to stop
    int status;
    if (waitpid(pid, &status, 0) == -1) {
        ptrace(PTRACE_DETACH, pid, nullptr, nullptr);
        return false;
    }
    
    m_impl->targetPid = pid;
    m_impl->attached = true;
    
    return true;
}

void SyscallInterceptor::detach() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    if (m_impl->attached && m_impl->targetPid > 0) {
        stopInterception();
        ptrace(PTRACE_DETACH, m_impl->targetPid, nullptr, nullptr);
        m_impl->attached = false;
        m_impl->targetPid = 0;
    }
}

bool SyscallInterceptor::isAttached() const {
    return m_impl->attached;
}

void SyscallInterceptor::setTracker(std::shared_ptr<FileOperationTracker> tracker) {
    m_impl->tracker = std::move(tracker);
}

void SyscallInterceptor::startInterception() {
    if (!m_impl->attached) return;
    
    // Set ptrace options for syscall interception
    ptrace(PTRACE_SETOPTIONS, m_impl->targetPid, nullptr, 
           PTRACE_O_TRACESYSGOOD);
    
    m_impl->intercepting = true;
    
    // Continue the process
    ptrace(PTRACE_SYSCALL, m_impl->targetPid, nullptr, nullptr);
}

void SyscallInterceptor::stopInterception() {
    if (!m_impl->intercepting) return;
    
    m_impl->intercepting = false;
    
    // Continue without syscall stops
    ptrace(PTRACE_CONT, m_impl->targetPid, nullptr, nullptr);
}

bool SyscallInterceptor::isIntercepting() const {
    return m_impl->intercepting;
}

void SyscallInterceptor::interceptWrite(bool enable) {
    m_impl->interceptWrite = enable;
}

void SyscallInterceptor::interceptOpen(bool enable) {
    m_impl->interceptOpen = enable;
}

void SyscallInterceptor::interceptUnlink(bool enable) {
    m_impl->interceptUnlink = enable;
}

void SyscallInterceptor::interceptRename(bool enable) {
    m_impl->interceptRename = enable;
}

void SyscallInterceptor::interceptTruncate(bool enable) {
    m_impl->interceptTruncate = enable;
}

void SyscallInterceptor::interceptAll(bool enable) {
    m_impl->interceptWrite = enable;
    m_impl->interceptOpen = enable;
    m_impl->interceptUnlink = enable;
    m_impl->interceptRename = enable;
    m_impl->interceptTruncate = enable;
}

void SyscallInterceptor::setSyscallCallback(SyscallCallback callback) {
    m_impl->callback = std::move(callback);
}

void SyscallInterceptor::handleSyscall(pid_t pid, long syscallNum,
                                        const std::vector<uint64_t>& args) {
    // Call user callback if set
    if (m_impl->callback) {
        m_impl->callback(pid, syscallNum, args);
    }
    
    // Handle with tracker if available
    if (!m_impl->tracker) return;
    
    // Handle specific syscalls
    switch (syscallNum) {
#ifdef SYS_write
        case SYS_write:
            if (m_impl->interceptWrite && args.size() >= 3) {
                // args: fd, buf, count
                // Note: Would need to resolve fd to path via /proc/<pid>/fd/<fd>
            }
            break;
#endif
            
#ifdef SYS_unlink
        case SYS_unlink:
            if (m_impl->interceptUnlink) {
                // args: pathname
            }
            break;
#endif

#ifdef SYS_rename
        case SYS_rename:
            if (m_impl->interceptRename && args.size() >= 2) {
                // args: oldpath, newpath
            }
            break;
#endif
            
#ifdef SYS_truncate
        case SYS_truncate:
            if (m_impl->interceptTruncate && args.size() >= 2) {
                // args: path, length
            }
            break;
#endif
            
        default:
            break;
    }
}

} // namespace real_process
} // namespace checkpoint
