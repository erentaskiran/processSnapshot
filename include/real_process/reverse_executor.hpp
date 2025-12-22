#pragma once

#include "real_process/file_operation.hpp"
#include "core/types.hpp"
#include <memory>
#include <functional>
#include <filesystem>

namespace checkpoint {
namespace real_process {

// ============================================================================
// Reverse Execution Result
// ============================================================================
struct ReverseResult {
    bool success;
    std::string errorMessage;
    uint64_t operationId;
    
    // Details
    size_t bytesRestored;
    std::string affectedPath;
    
    static ReverseResult ok(uint64_t opId, const std::string& path = "", size_t bytes = 0) {
        return {true, "", opId, bytes, path};
    }
    
    static ReverseResult error(uint64_t opId, const std::string& msg) {
        return {false, msg, opId, 0, ""};
    }
};

// ============================================================================
// Batch Reverse Result - Birden fazla işlemin geri alınması sonucu
// ============================================================================
struct BatchReverseResult {
    bool allSucceeded;
    size_t totalOperations;
    size_t successCount;
    size_t failCount;
    size_t skippedCount;
    
    std::vector<ReverseResult> results;
    std::vector<std::string> warnings;
    
    // Timing
    std::chrono::milliseconds duration;
    
    BatchReverseResult() : allSucceeded(false), totalOperations(0),
                          successCount(0), failCount(0), skippedCount(0) {}
};

// ============================================================================
// Reverse Execution Options
// ============================================================================
struct ReverseOptions {
    bool stopOnError;               // İlk hatada dur
    bool dryRun;                    // Sadece simülasyon (gerçekten değiştirme)
    bool createBackups;             // Geri almadan önce yedekle
    std::string backupDir;          // Yedekleme dizini
    bool validateBeforeReverse;     // Tersine çevirmeden önce doğrula
    bool preserveTimestamps;        // Dosya zaman damgalarını koru
    bool reverseNewestFirst;        // En yeni işlemden başla (LIFO)
    
    // Filtering
    std::vector<FileOperationType> includeTypes;  // Sadece bu türleri geri al
    std::vector<FileOperationType> excludeTypes;  // Bu türleri atla
    std::function<bool(const FileOperation&)> filter;  // Özel filtre
    
    ReverseOptions() 
        : stopOnError(false),
          dryRun(false),
          createBackups(true),
          backupDir("/tmp/checkpoint_backups"),
          validateBeforeReverse(true),
          preserveTimestamps(true),
          reverseNewestFirst(true) {}
    
    // Presets
    static ReverseOptions safe() {
        ReverseOptions opt;
        opt.stopOnError = true;
        opt.createBackups = true;
        opt.validateBeforeReverse = true;
        return opt;
    }
    
    static ReverseOptions fast() {
        ReverseOptions opt;
        opt.createBackups = false;
        opt.validateBeforeReverse = false;
        return opt;
    }
    
    static ReverseOptions preview() {
        ReverseOptions opt;
        opt.dryRun = true;
        return opt;
    }
};

// ============================================================================
// Reverse Executor - Dosya işlemlerini tersine çeviren sınıf
// ============================================================================
class ReverseExecutor {
public:
    ReverseExecutor();
    explicit ReverseExecutor(const ReverseOptions& options);
    ~ReverseExecutor();
    
    // Single operation reverse
    ReverseResult reverseOperation(const FileOperation& op);
    
    // Batch reverse - multiple operations
    BatchReverseResult reverseOperations(const std::vector<FileOperation>& ops);
    
    // Reverse operations since checkpoint
    BatchReverseResult reverseToCheckpoint(const FileOperationLog& log, 
                                           uint64_t checkpointId);
    
    // Reverse all operations in log
    BatchReverseResult reverseAll(const FileOperationLog& log);
    
    // Preview what would be reversed
    std::vector<std::string> previewReverse(const std::vector<FileOperation>& ops);
    
    // Validation
    bool canReverse(const FileOperation& op) const;
    std::vector<std::string> validateReverse(const FileOperation& op) const;
    
    // Configuration
    void setOptions(const ReverseOptions& options);
    const ReverseOptions& getOptions() const;
    
    // Progress callback
    using ProgressCallback = std::function<void(size_t current, size_t total, 
                                                const std::string& status)>;
    void setProgressCallback(ProgressCallback callback);
    
    // Statistics
    size_t getTotalReversedCount() const;
    size_t getTotalBytesRestored() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    
    // Individual operation reversers
    ReverseResult reverseCreate(const FileOperation& op);
    ReverseResult reverseWrite(const FileOperation& op);
    ReverseResult reverseTruncate(const FileOperation& op);
    ReverseResult reverseDelete(const FileOperation& op);
    ReverseResult reverseRename(const FileOperation& op);
    ReverseResult reverseChmod(const FileOperation& op);
    ReverseResult reverseChown(const FileOperation& op);
    ReverseResult reverseMkdir(const FileOperation& op);
    ReverseResult reverseRmdir(const FileOperation& op);
    ReverseResult reverseSymlink(const FileOperation& op);
    ReverseResult reverseAppend(const FileOperation& op);
    
    // Helpers
    bool createBackup(const std::string& path);
    bool shouldProcess(const FileOperation& op) const;
    std::vector<FileOperation> sortOperations(const std::vector<FileOperation>& ops) const;
};

// ============================================================================
// Syscall Interceptor - ptrace tabanlı syscall yakalayıcı
// ============================================================================
class SyscallInterceptor {
public:
    SyscallInterceptor();
    ~SyscallInterceptor();
    
    // Attach to process
    bool attach(pid_t pid);
    void detach();
    bool isAttached() const;
    
    // Set tracker for automatic operation recording
    void setTracker(std::shared_ptr<FileOperationTracker> tracker);
    
    // Start/stop interception
    void startInterception();
    void stopInterception();
    bool isIntercepting() const;
    
    // Syscall filtering
    void interceptWrite(bool enable);
    void interceptOpen(bool enable);
    void interceptUnlink(bool enable);
    void interceptRename(bool enable);
    void interceptTruncate(bool enable);
    void interceptAll(bool enable);
    
    // Manual syscall handling callback
    using SyscallCallback = std::function<void(pid_t pid, long syscallNum, 
                                               const std::vector<uint64_t>& args)>;
    void setSyscallCallback(SyscallCallback callback);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    
    void handleSyscall(pid_t pid, long syscallNum, const std::vector<uint64_t>& args);
};

} // namespace real_process
} // namespace checkpoint
