#pragma once

#include "real_process/real_process_types.hpp"
#include <sys/types.h>
#include <vector>
#include <string>
#include <optional>
#include <functional>

namespace checkpoint {
namespace real_process {

// ============================================================================
// File Descriptor Restoration Errors
// ============================================================================
enum class FDError {
    SUCCESS = 0,
    PERMISSION_DENIED,
    FILE_NOT_FOUND,
    INVALID_FD,
    DUP_FAILED,
    OPEN_FAILED,
    SEEK_FAILED,
    SOCKET_UNSUPPORTED,
    PIPE_UNSUPPORTED,
    SPECIAL_FILE,
    PROCESS_NOT_ATTACHED,
    UNKNOWN_ERROR
};

std::string fdErrorToString(FDError err);

// ============================================================================
// File Descriptor Type
// ============================================================================
enum class FDType {
    REGULAR_FILE,      // Normal file
    DIRECTORY,         // Directory (opendir)
    SOCKET,            // Network socket
    PIPE,              // Pipe or FIFO
    CHAR_DEVICE,       // Character device (/dev/null, /dev/tty)
    BLOCK_DEVICE,      // Block device
    EVENT_FD,          // eventfd
    TIMER_FD,          // timerfd
    SIGNAL_FD,         // signalfd
    EPOLL_FD,          // epoll
    INOTIFY_FD,        // inotify
    UNKNOWN
};

std::string fdTypeToString(FDType type);
FDType detectFDType(const std::string& path, int flags);

// ============================================================================
// Extended File Descriptor Info
// ============================================================================
struct ExtendedFDInfo {
    int fd;
    std::string path;           // Symlink target from /proc/<pid>/fd/<fd>
    std::string realPath;       // Real path after resolving symlinks
    FDType type;
    int flags;                  // O_RDONLY, O_WRONLY, O_RDWR, etc.
    int mode;                   // File mode (permissions)
    off_t pos;                  // Current file position
    bool closeOnExec;           // FD_CLOEXEC flag
    
    // For sockets
    std::string socketType;     // tcp, udp, unix, etc.
    std::string localAddr;
    std::string remoteAddr;
    
    // Status
    bool canRestore;
    std::string restoreWarning;
    
    ExtendedFDInfo() : fd(-1), type(FDType::UNKNOWN), flags(0), mode(0), 
                       pos(0), closeOnExec(false), canRestore(true) {}
};

// ============================================================================
// File Descriptor Restorer
// ============================================================================
class FDRestorer {
public:
    FDRestorer();
    ~FDRestorer();
    
    // ========================================================================
    // Process Binding
    // ========================================================================
    
    // Bind to a process (for restoration via syscall injection)
    FDError bindProcess(pid_t pid);
    void unbindProcess();
    bool isBound() const { return m_pid > 0; }
    
    // ========================================================================
    // FD Information Gathering
    // ========================================================================
    
    // Get detailed info for all file descriptors
    std::vector<ExtendedFDInfo> gatherFDInfo(pid_t pid);
    
    // Get info for a specific FD
    std::optional<ExtendedFDInfo> getFDInfo(pid_t pid, int fd);
    
    // Analyze checkpoint FDs for restorability
    struct FDAnalysis {
        std::vector<ExtendedFDInfo> restorable;
        std::vector<ExtendedFDInfo> unrestorable;
        std::vector<std::string> warnings;
    };
    
    FDAnalysis analyzeCheckpointFDs(const std::vector<FileDescriptorInfo>& checkpointFDs);
    
    // ========================================================================
    // File Descriptor Restoration
    // ========================================================================
    
    // Restore file descriptors to a process
    // Uses syscall injection (open, dup2, lseek via ptrace)
    struct RestoreFDResult {
        FDError error;
        int fdNumber;
        std::string message;
    };
    
    RestoreFDResult restoreFD(const ExtendedFDInfo& fdInfo);
    
    // Restore all restorable FDs
    struct BatchRestoreResult {
        int totalFDs;
        int restoredFDs;
        int failedFDs;
        std::vector<RestoreFDResult> results;
        std::vector<std::string> warnings;
    };
    
    BatchRestoreResult restoreAllFDs(
        const std::vector<ExtendedFDInfo>& fdInfos,
        bool stopOnError = false
    );
    
    // ========================================================================
    // Individual Operations (via syscall injection)
    // ========================================================================
    
    // Open a file in target process
    FDError injectOpen(const std::string& path, int flags, int mode, int& resultFD);
    
    // Duplicate FD to specific number
    FDError injectDup2(int oldFD, int newFD);
    
    // Seek to position
    FDError injectLseek(int fd, off_t offset, int whence);
    
    // Close FD
    FDError injectClose(int fd);
    
    // Get current FD flags
    FDError injectFcntl(int fd, int cmd, int& result);
    
    // ========================================================================
    // Special File Handling
    // ========================================================================
    
    // Handle stdin/stdout/stderr
    FDError restoreStandardFDs(
        const ExtendedFDInfo* stdin_info,
        const ExtendedFDInfo* stdout_info,
        const ExtendedFDInfo* stderr_info
    );
    
    // Handle /dev/null, /dev/tty, etc.
    bool isRestorableDevice(const std::string& path);
    
    // ========================================================================
    // Utilities
    // ========================================================================
    
    std::string getLastError() const { return m_lastError; }
    
    // Progress callback
    using ProgressCallback = std::function<void(const std::string&, double)>;
    void setProgressCallback(ProgressCallback cb) { m_progressCallback = cb; }
    
private:
    pid_t m_pid;
    std::string m_lastError;
    ProgressCallback m_progressCallback;
    
    void reportProgress(const std::string& stage, double progress);
    
    // Syscall injection helper
    int64_t injectSyscall(
        uint64_t syscallNum,
        uint64_t arg1 = 0,
        uint64_t arg2 = 0,
        uint64_t arg3 = 0,
        uint64_t arg4 = 0,
        uint64_t arg5 = 0,
        uint64_t arg6 = 0
    );
    
    // Helper to inject string into process memory
    bool injectString(const std::string& str, uint64_t& addr);
    
    // Parse /proc/<pid>/fdinfo/<fd>
    std::optional<ExtendedFDInfo> parseFDInfo(pid_t pid, int fd);
    
    // Detect FD type from symlink
    FDType detectType(const std::string& linkTarget);
};

// ============================================================================
// Convenience Functions
// ============================================================================

// Check if all checkpoint FDs can be restored
bool canRestoreAllFDs(const std::vector<FileDescriptorInfo>& checkpointFDs);

// Get list of FDs that cannot be restored
std::vector<int> getUnrestorableFDs(const std::vector<FileDescriptorInfo>& checkpointFDs);

} // namespace real_process
} // namespace checkpoint
