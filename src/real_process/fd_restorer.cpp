#include "real_process/fd_restorer.hpp"
#include "real_process/proc_reader.hpp"
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace checkpoint {
namespace real_process {

// ============================================================================
// Error Handling
// ============================================================================

std::string fdErrorToString(FDError err) {
    switch (err) {
        case FDError::SUCCESS:            return "Success";
        case FDError::PERMISSION_DENIED:  return "Permission denied";
        case FDError::FILE_NOT_FOUND:     return "File not found";
        case FDError::INVALID_FD:         return "Invalid file descriptor";
        case FDError::DUP_FAILED:         return "dup2 failed";
        case FDError::OPEN_FAILED:        return "open failed";
        case FDError::SEEK_FAILED:        return "lseek failed";
        case FDError::SOCKET_UNSUPPORTED: return "Socket restoration not supported";
        case FDError::PIPE_UNSUPPORTED:   return "Pipe restoration not supported";
        case FDError::SPECIAL_FILE:       return "Special file cannot be restored";
        case FDError::PROCESS_NOT_ATTACHED: return "Process not attached";
        default:                          return "Unknown error";
    }
}

std::string fdTypeToString(FDType type) {
    switch (type) {
        case FDType::REGULAR_FILE:  return "Regular file";
        case FDType::DIRECTORY:     return "Directory";
        case FDType::SOCKET:        return "Socket";
        case FDType::PIPE:          return "Pipe";
        case FDType::CHAR_DEVICE:   return "Character device";
        case FDType::BLOCK_DEVICE:  return "Block device";
        case FDType::EVENT_FD:      return "eventfd";
        case FDType::TIMER_FD:      return "timerfd";
        case FDType::SIGNAL_FD:     return "signalfd";
        case FDType::EPOLL_FD:      return "epoll";
        case FDType::INOTIFY_FD:    return "inotify";
        default:                    return "Unknown";
    }
}

FDType detectFDType(const std::string& path, int /*flags*/) {
    if (path.empty()) return FDType::UNKNOWN;
    
    // Check for special pseudo-files
    if (path.find("socket:") == 0) return FDType::SOCKET;
    if (path.find("pipe:") == 0) return FDType::PIPE;
    if (path.find("anon_inode:") == 0) {
        if (path.find("[eventfd]") != std::string::npos) return FDType::EVENT_FD;
        if (path.find("[timerfd]") != std::string::npos) return FDType::TIMER_FD;
        if (path.find("[signalfd]") != std::string::npos) return FDType::SIGNAL_FD;
        if (path.find("[eventpoll]") != std::string::npos) return FDType::EPOLL_FD;
        if (path.find("[inotify]") != std::string::npos) return FDType::INOTIFY_FD;
        return FDType::UNKNOWN;
    }
    
    // Check real path
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        if (S_ISREG(st.st_mode)) return FDType::REGULAR_FILE;
        if (S_ISDIR(st.st_mode)) return FDType::DIRECTORY;
        if (S_ISCHR(st.st_mode)) return FDType::CHAR_DEVICE;
        if (S_ISBLK(st.st_mode)) return FDType::BLOCK_DEVICE;
        if (S_ISFIFO(st.st_mode)) return FDType::PIPE;
        if (S_ISSOCK(st.st_mode)) return FDType::SOCKET;
    }
    
    return FDType::UNKNOWN;
}

// ============================================================================
// FDRestorer Implementation
// ============================================================================

FDRestorer::FDRestorer()
    : m_pid(0) {
}

FDRestorer::~FDRestorer() {
    unbindProcess();
}

void FDRestorer::reportProgress(const std::string& stage, double progress) {
    if (m_progressCallback) {
        m_progressCallback(stage, progress);
    }
}

// ============================================================================
// Process Binding
// ============================================================================

FDError FDRestorer::bindProcess(pid_t pid) {
    if (m_pid > 0) {
        unbindProcess();
    }
    
    // Verify process exists
    std::string procPath = "/proc/" + std::to_string(pid);
    if (access(procPath.c_str(), F_OK) != 0) {
        m_lastError = "Process does not exist: " + std::to_string(pid);
        return FDError::INVALID_FD;
    }
    
    m_pid = pid;
    return FDError::SUCCESS;
}

void FDRestorer::unbindProcess() {
    m_pid = 0;
}

// ============================================================================
// Syscall Injection
// ============================================================================

int64_t FDRestorer::injectSyscall(
    uint64_t syscallNum,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3,
    uint64_t arg4,
    uint64_t arg5,
    uint64_t arg6) {
    
    if (m_pid <= 0) {
        m_lastError = "No process bound";
        return -1;
    }
    
    // Save current registers
    struct user_regs_struct savedRegs, regs;
    
    if (ptrace(PTRACE_GETREGS, m_pid, nullptr, &savedRegs) == -1) {
        m_lastError = "Failed to save registers: " + std::string(strerror(errno));
        return -1;
    }
    
    // Get current instruction pointer
    uint64_t originalRip = savedRegs.rip;
    
    // Read original instruction at RIP
    errno = 0;
    uint64_t originalCode = ptrace(PTRACE_PEEKTEXT, m_pid, originalRip, nullptr);
    if (errno != 0) {
        m_lastError = "Failed to read original code: " + std::string(strerror(errno));
        return -1;
    }
    
    // Write syscall instruction (0x0F 0x05)
    uint64_t syscallCode = (originalCode & ~0xFFFFULL) | 0x050F;
    if (ptrace(PTRACE_POKETEXT, m_pid, originalRip, syscallCode) == -1) {
        m_lastError = "Failed to inject syscall: " + std::string(strerror(errno));
        return -1;
    }
    
    // Set up syscall arguments
    regs = savedRegs;
    regs.rax = syscallNum;
    regs.rdi = arg1;
    regs.rsi = arg2;
    regs.rdx = arg3;
    regs.r10 = arg4;
    regs.r8 = arg5;
    regs.r9 = arg6;
    regs.rip = originalRip;
    
    if (ptrace(PTRACE_SETREGS, m_pid, nullptr, &regs) == -1) {
        ptrace(PTRACE_POKETEXT, m_pid, originalRip, originalCode);
        m_lastError = "Failed to set registers for syscall";
        return -1;
    }
    
    // Execute the syscall
    if (ptrace(PTRACE_SINGLESTEP, m_pid, nullptr, nullptr) == -1) {
        ptrace(PTRACE_POKETEXT, m_pid, originalRip, originalCode);
        ptrace(PTRACE_SETREGS, m_pid, nullptr, &savedRegs);
        m_lastError = "Failed to execute syscall";
        return -1;
    }
    
    // Wait for process to stop
    int status;
    waitpid(m_pid, &status, 0);
    
    // Get syscall result
    if (ptrace(PTRACE_GETREGS, m_pid, nullptr, &regs) == -1) {
        ptrace(PTRACE_POKETEXT, m_pid, originalRip, originalCode);
        ptrace(PTRACE_SETREGS, m_pid, nullptr, &savedRegs);
        m_lastError = "Failed to get syscall result";
        return -1;
    }
    
    int64_t result = static_cast<int64_t>(regs.rax);
    
    // Restore original code
    ptrace(PTRACE_POKETEXT, m_pid, originalRip, originalCode);
    
    // Restore original registers
    ptrace(PTRACE_SETREGS, m_pid, nullptr, &savedRegs);
    
    return result;
}

bool FDRestorer::injectString(const std::string& str, uint64_t& addr) {
    // Get stack pointer to use for temporary string storage
    struct user_regs_struct regs;
    if (ptrace(PTRACE_GETREGS, m_pid, nullptr, &regs) == -1) {
        return false;
    }
    
    // Use space below stack pointer (safe area)
    addr = regs.rsp - 256 - str.size() - 1;
    addr &= ~7ULL;  // Align to 8 bytes
    
    // Write string to process memory
    size_t len = str.size() + 1;  // Include null terminator
    const char* data = str.c_str();
    
    for (size_t i = 0; i < len; i += sizeof(uint64_t)) {
        uint64_t word = 0;
        size_t copyLen = std::min(sizeof(uint64_t), len - i);
        memcpy(&word, data + i, copyLen);
        
        if (ptrace(PTRACE_POKEDATA, m_pid, addr + i, word) == -1) {
            return false;
        }
    }
    
    return true;
}

// ============================================================================
// FD Information Gathering
// ============================================================================

std::optional<ExtendedFDInfo> FDRestorer::parseFDInfo(pid_t pid, int fd) {
    ExtendedFDInfo info;
    info.fd = fd;
    
    // Read fd link
    std::string fdLink = "/proc/" + std::to_string(pid) + "/fd/" + std::to_string(fd);
    char linkTarget[PATH_MAX];
    ssize_t len = readlink(fdLink.c_str(), linkTarget, sizeof(linkTarget) - 1);
    if (len == -1) {
        return std::nullopt;
    }
    linkTarget[len] = '\0';
    info.path = linkTarget;
    
    // Try to resolve real path
    char realPath[PATH_MAX];
    if (realpath(linkTarget, realPath) != nullptr) {
        info.realPath = realPath;
    } else {
        info.realPath = linkTarget;
    }
    
    // Detect type
    info.type = detectFDType(info.path, 0);
    
    // Read fdinfo
    std::string fdinfoPath = "/proc/" + std::to_string(pid) + "/fdinfo/" + std::to_string(fd);
    std::ifstream fdinfo(fdinfoPath);
    if (fdinfo.is_open()) {
        std::string line;
        while (std::getline(fdinfo, line)) {
            if (line.find("pos:") == 0) {
                info.pos = std::stoll(line.substr(5));
            } else if (line.find("flags:") == 0) {
                info.flags = std::stoi(line.substr(7), nullptr, 8);
            }
        }
    }
    
    // Check FD_CLOEXEC
    int fdFlags = fcntl(fd, F_GETFD);
    if (fdFlags != -1) {
        info.closeOnExec = (fdFlags & FD_CLOEXEC) != 0;
    }
    
    // Determine restorability
    switch (info.type) {
        case FDType::REGULAR_FILE:
        case FDType::DIRECTORY:
            info.canRestore = true;
            break;
        case FDType::CHAR_DEVICE:
            info.canRestore = isRestorableDevice(info.path);
            if (!info.canRestore) {
                info.restoreWarning = "Device may have different state";
            }
            break;
        case FDType::SOCKET:
            info.canRestore = false;
            info.restoreWarning = "Sockets cannot be restored (connection state lost)";
            break;
        case FDType::PIPE:
            info.canRestore = false;
            info.restoreWarning = "Pipes cannot be restored (other end may be gone)";
            break;
        case FDType::EVENT_FD:
        case FDType::TIMER_FD:
        case FDType::SIGNAL_FD:
        case FDType::EPOLL_FD:
        case FDType::INOTIFY_FD:
            info.canRestore = false;
            info.restoreWarning = "Special FD cannot be restored (kernel state lost)";
            break;
        default:
            info.canRestore = false;
            info.restoreWarning = "Unknown FD type";
            break;
    }
    
    return info;
}

std::vector<ExtendedFDInfo> FDRestorer::gatherFDInfo(pid_t pid) {
    std::vector<ExtendedFDInfo> result;
    
    std::string fdDir = "/proc/" + std::to_string(pid) + "/fd";
    DIR* dir = opendir(fdDir.c_str());
    if (!dir) {
        return result;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;
        
        int fd = std::atoi(entry->d_name);
        auto info = parseFDInfo(pid, fd);
        if (info) {
            result.push_back(*info);
        }
    }
    
    closedir(dir);
    
    // Sort by FD number
    std::sort(result.begin(), result.end(), 
              [](const ExtendedFDInfo& a, const ExtendedFDInfo& b) {
                  return a.fd < b.fd;
              });
    
    return result;
}

std::optional<ExtendedFDInfo> FDRestorer::getFDInfo(pid_t pid, int fd) {
    return parseFDInfo(pid, fd);
}

FDRestorer::FDAnalysis FDRestorer::analyzeCheckpointFDs(
    const std::vector<FileDescriptorInfo>& checkpointFDs) {
    
    FDAnalysis analysis;
    
    for (const auto& fdInfo : checkpointFDs) {
        ExtendedFDInfo extInfo;
        extInfo.fd = fdInfo.fd;
        extInfo.path = fdInfo.path;
        extInfo.realPath = fdInfo.path;
        extInfo.flags = fdInfo.flags;
        extInfo.pos = fdInfo.pos;
        extInfo.type = detectFDType(fdInfo.path, fdInfo.flags);
        
        // Check if file still exists
        if (extInfo.type == FDType::REGULAR_FILE || 
            extInfo.type == FDType::DIRECTORY) {
            if (access(fdInfo.path.c_str(), F_OK) != 0) {
                extInfo.canRestore = false;
                extInfo.restoreWarning = "File no longer exists: " + fdInfo.path;
            } else {
                extInfo.canRestore = true;
            }
        } else {
            extInfo.canRestore = false;
            extInfo.restoreWarning = "Non-file FD cannot be restored";
        }
        
        if (extInfo.canRestore) {
            analysis.restorable.push_back(extInfo);
        } else {
            analysis.unrestorable.push_back(extInfo);
            analysis.warnings.push_back("FD " + std::to_string(extInfo.fd) + 
                                        ": " + extInfo.restoreWarning);
        }
    }
    
    return analysis;
}

// ============================================================================
// File Descriptor Restoration
// ============================================================================

FDError FDRestorer::injectOpen(const std::string& path, int flags, int mode, int& resultFD) {
    if (m_pid <= 0) {
        return FDError::PROCESS_NOT_ATTACHED;
    }
    
    // Inject path string into process memory
    uint64_t pathAddr;
    if (!injectString(path, pathAddr)) {
        m_lastError = "Failed to inject path string";
        return FDError::OPEN_FAILED;
    }
    
    // Call open syscall
    int64_t result = injectSyscall(SYS_open, pathAddr, flags, mode);
    
    if (result < 0) {
        m_lastError = "open failed with error: " + std::to_string(-result);
        return FDError::OPEN_FAILED;
    }
    
    resultFD = static_cast<int>(result);
    return FDError::SUCCESS;
}

FDError FDRestorer::injectDup2(int oldFD, int newFD) {
    if (m_pid <= 0) {
        return FDError::PROCESS_NOT_ATTACHED;
    }
    
    int64_t result = injectSyscall(SYS_dup2, oldFD, newFD);
    
    if (result < 0) {
        m_lastError = "dup2 failed with error: " + std::to_string(-result);
        return FDError::DUP_FAILED;
    }
    
    return FDError::SUCCESS;
}

FDError FDRestorer::injectLseek(int fd, off_t offset, int whence) {
    if (m_pid <= 0) {
        return FDError::PROCESS_NOT_ATTACHED;
    }
    
    int64_t result = injectSyscall(SYS_lseek, fd, offset, whence);
    
    if (result < 0) {
        m_lastError = "lseek failed with error: " + std::to_string(-result);
        return FDError::SEEK_FAILED;
    }
    
    return FDError::SUCCESS;
}

FDError FDRestorer::injectClose(int fd) {
    if (m_pid <= 0) {
        return FDError::PROCESS_NOT_ATTACHED;
    }
    
    int64_t result = injectSyscall(SYS_close, fd);
    
    if (result < 0) {
        // Close can fail, but usually not a problem
        m_lastError = "close failed with error: " + std::to_string(-result);
    }
    
    return FDError::SUCCESS;
}

FDError FDRestorer::injectFcntl(int fd, int cmd, int& result) {
    if (m_pid <= 0) {
        return FDError::PROCESS_NOT_ATTACHED;
    }
    
    int64_t res = injectSyscall(SYS_fcntl, fd, cmd);
    
    if (res < 0) {
        m_lastError = "fcntl failed with error: " + std::to_string(-res);
        return FDError::UNKNOWN_ERROR;
    }
    
    result = static_cast<int>(res);
    return FDError::SUCCESS;
}

FDRestorer::RestoreFDResult FDRestorer::restoreFD(const ExtendedFDInfo& fdInfo) {
    RestoreFDResult result;
    result.fdNumber = fdInfo.fd;
    result.error = FDError::SUCCESS;
    
    if (!fdInfo.canRestore) {
        result.error = FDError::SPECIAL_FILE;
        result.message = fdInfo.restoreWarning;
        return result;
    }
    
    if (m_pid <= 0) {
        result.error = FDError::PROCESS_NOT_ATTACHED;
        result.message = "No process attached";
        return result;
    }
    
    // Determine open flags
    int openFlags = fdInfo.flags & (O_ACCMODE | O_APPEND | O_NONBLOCK);
    if (fdInfo.type == FDType::DIRECTORY) {
        openFlags |= O_DIRECTORY;
    }
    
    // Open the file
    int tempFD;
    FDError err = injectOpen(fdInfo.path, openFlags, 0, tempFD);
    if (err != FDError::SUCCESS) {
        result.error = err;
        result.message = "Failed to open: " + m_lastError;
        return result;
    }
    
    // Move to correct FD number if different
    if (tempFD != fdInfo.fd) {
        // First close any existing FD at target number
        injectClose(fdInfo.fd);
        
        // Duplicate to correct number
        err = injectDup2(tempFD, fdInfo.fd);
        if (err != FDError::SUCCESS) {
            injectClose(tempFD);
            result.error = err;
            result.message = "Failed to dup2: " + m_lastError;
            return result;
        }
        
        // Close temporary FD
        injectClose(tempFD);
    }
    
    // Restore file position
    if (fdInfo.type == FDType::REGULAR_FILE && fdInfo.pos > 0) {
        err = injectLseek(fdInfo.fd, fdInfo.pos, SEEK_SET);
        if (err != FDError::SUCCESS) {
            result.message = "Warning: Failed to restore file position";
            // Continue anyway, not critical
        }
    }
    
    result.message = "Restored FD " + std::to_string(fdInfo.fd) + 
                     " -> " + fdInfo.path;
    return result;
}

FDRestorer::BatchRestoreResult FDRestorer::restoreAllFDs(
    const std::vector<ExtendedFDInfo>& fdInfos,
    bool stopOnError) {
    
    BatchRestoreResult result;
    result.totalFDs = fdInfos.size();
    result.restoredFDs = 0;
    result.failedFDs = 0;
    
    int done = 0;
    for (const auto& fdInfo : fdInfos) {
        reportProgress("Restoring FDs", double(done) / result.totalFDs);
        
        auto fdResult = restoreFD(fdInfo);
        result.results.push_back(fdResult);
        
        if (fdResult.error == FDError::SUCCESS) {
            result.restoredFDs++;
        } else {
            result.failedFDs++;
            result.warnings.push_back("FD " + std::to_string(fdInfo.fd) + 
                                      ": " + fdResult.message);
            
            if (stopOnError) {
                break;
            }
        }
        
        done++;
    }
    
    reportProgress("FD restoration complete", 1.0);
    return result;
}

// ============================================================================
// Special File Handling
// ============================================================================

FDError FDRestorer::restoreStandardFDs(
    const ExtendedFDInfo* stdin_info,
    const ExtendedFDInfo* stdout_info,
    const ExtendedFDInfo* stderr_info) {
    
    // Standard FDs are special - they might point to /dev/null, /dev/tty, etc.
    
    if (stdin_info && stdin_info->canRestore) {
        auto result = restoreFD(*stdin_info);
        if (result.error != FDError::SUCCESS) {
            return result.error;
        }
    }
    
    if (stdout_info && stdout_info->canRestore) {
        auto result = restoreFD(*stdout_info);
        if (result.error != FDError::SUCCESS) {
            return result.error;
        }
    }
    
    if (stderr_info && stderr_info->canRestore) {
        auto result = restoreFD(*stderr_info);
        if (result.error != FDError::SUCCESS) {
            return result.error;
        }
    }
    
    return FDError::SUCCESS;
}

bool FDRestorer::isRestorableDevice(const std::string& path) {
    // List of devices that can be safely reopened
    static const std::vector<std::string> restorableDevices = {
        "/dev/null",
        "/dev/zero",
        "/dev/full",
        "/dev/random",
        "/dev/urandom",
        "/dev/tty",
        "/dev/pts/"
    };
    
    for (const auto& dev : restorableDevices) {
        if (path.find(dev) == 0) {
            return true;
        }
    }
    
    return false;
}

FDType FDRestorer::detectType(const std::string& linkTarget) {
    return detectFDType(linkTarget, 0);
}

// ============================================================================
// Convenience Functions
// ============================================================================

bool canRestoreAllFDs(const std::vector<FileDescriptorInfo>& checkpointFDs) {
    FDRestorer restorer;
    auto analysis = restorer.analyzeCheckpointFDs(checkpointFDs);
    return analysis.unrestorable.empty();
}

std::vector<int> getUnrestorableFDs(const std::vector<FileDescriptorInfo>& checkpointFDs) {
    FDRestorer restorer;
    auto analysis = restorer.analyzeCheckpointFDs(checkpointFDs);
    
    std::vector<int> result;
    for (const auto& fd : analysis.unrestorable) {
        result.push_back(fd.fd);
    }
    return result;
}

} // namespace real_process
} // namespace checkpoint
