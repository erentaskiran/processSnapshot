#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <sys/types.h>
#include <sys/user.h>

namespace checkpoint {
namespace real_process {

// ============================================================================
// Linux Process States
// ============================================================================
enum class LinuxProcessState {
    RUNNING,        // R - Running
    SLEEPING,       // S - Interruptible sleep
    DISK_SLEEP,     // D - Uninterruptible sleep
    STOPPED,        // T - Stopped (by signal or trace)
    ZOMBIE,         // Z - Zombie
    DEAD,           // X - Dead
    UNKNOWN
};

std::string linuxStateToString(LinuxProcessState state);
LinuxProcessState charToLinuxState(char c);

// ============================================================================
// Memory Region - /proc/<pid>/maps'teki bir satır
// ============================================================================
struct MemoryRegion {
    uint64_t startAddr;
    uint64_t endAddr;
    bool readable;
    bool writable;
    bool executable;
    bool isPrivate;     // p=private, s=shared
    uint64_t offset;
    std::string device;
    uint64_t inode;
    std::string pathname;
    
    uint64_t size() const { return endAddr - startAddr; }
    bool isAnonymous() const { return pathname.empty() || pathname[0] != '/'; }
    bool isStack() const { return pathname.find("[stack") != std::string::npos; }
    bool isHeap() const { return pathname == "[heap]"; }
    bool isVdso() const { return pathname == "[vdso]" || pathname == "[vvar]"; }
};

// ============================================================================
// Linux Register Set (x86_64)
// ============================================================================
struct LinuxRegisters {
    // General purpose registers
    uint64_t r15, r14, r13, r12;
    uint64_t rbp, rbx;
    uint64_t r11, r10, r9, r8;
    uint64_t rax, rcx, rdx, rsi, rdi;
    uint64_t orig_rax;
    uint64_t rip;           // Instruction pointer
    uint64_t cs;
    uint64_t eflags;
    uint64_t rsp;           // Stack pointer
    uint64_t ss;
    uint64_t fs_base, gs_base;
    uint64_t ds, es, fs, gs;
    
    // Floating point registers (opsiyonel)
    bool hasFPU;
    std::vector<uint8_t> fpuState;  // fxsave/fxrstor data (512 bytes)
    
    LinuxRegisters() : hasFPU(false) {
        r15 = r14 = r13 = r12 = 0;
        rbp = rbx = 0;
        r11 = r10 = r9 = r8 = 0;
        rax = rcx = rdx = rsi = rdi = 0;
        orig_rax = 0;
        rip = cs = eflags = rsp = ss = 0;
        fs_base = gs_base = ds = es = fs = gs = 0;
    }
};

// ============================================================================
// Process Info - /proc/<pid>/stat ve /proc/<pid>/status'tan
// ============================================================================
struct RealProcessInfo {
    pid_t pid;
    pid_t ppid;             // Parent PID
    pid_t pgid;             // Process group ID
    pid_t sid;              // Session ID
    
    std::string name;       // Process name (comm)
    std::string cmdline;    // Full command line
    std::string exe;        // Executable path
    std::string cwd;        // Current working directory
    
    LinuxProcessState state;
    
    // User/Group
    uid_t uid, euid, suid;
    gid_t gid, egid, sgid;
    
    // CPU Stats
    uint64_t utime;         // User time (clock ticks)
    uint64_t stime;         // System time (clock ticks)
    uint64_t cutime;        // Children user time
    uint64_t cstime;        // Children system time
    int priority;
    int nice;
    int numThreads;
    
    // Memory Stats
    uint64_t vmSize;        // Virtual memory size (bytes)
    uint64_t vmRss;         // Resident set size (bytes)
    uint64_t vmPeak;        // Peak virtual memory
    uint64_t vmData;        // Data segment size
    uint64_t vmStack;       // Stack size
    uint64_t vmExe;         // Code size
    
    // Start time
    uint64_t startTime;     // Process start time (clock ticks since boot)
    
    RealProcessInfo() : pid(0), ppid(0), pgid(0), sid(0),
                        state(LinuxProcessState::UNKNOWN),
                        uid(0), euid(0), suid(0),
                        gid(0), egid(0), sgid(0),
                        utime(0), stime(0), cutime(0), cstime(0),
                        priority(0), nice(0), numThreads(0),
                        vmSize(0), vmRss(0), vmPeak(0),
                        vmData(0), vmStack(0), vmExe(0),
                        startTime(0) {}
};

// ============================================================================
// Memory Content - Bellek içeriği dump'ı
// ============================================================================
struct MemoryDump {
    MemoryRegion region;
    std::vector<uint8_t> data;
    bool isValid;
    
    MemoryDump() : isValid(false) {}
};

// ============================================================================
// File Descriptor Info
// ============================================================================
struct FileDescriptorInfo {
    int fd;
    std::string path;       // /proc/<pid>/fd/<fd> -> path
    int flags;              // O_RDONLY, O_WRONLY, etc.
    off_t pos;              // Current position (from /proc/<pid>/fdinfo/<fd>)
    
    FileDescriptorInfo() : fd(-1), flags(0), pos(0) {}
};

// ============================================================================
// Signal Info
// ============================================================================
struct SignalInfo {
    uint64_t pending;       // Pending signals (bitmask)
    uint64_t blocked;       // Blocked signals (bitmask)
    uint64_t ignored;       // Ignored signals (bitmask)
    uint64_t caught;        // Caught signals (bitmask)
};

// ============================================================================
// Real Process Checkpoint - Tam Process Snapshot
// ============================================================================
struct RealProcessCheckpoint {
    // Metadata
    uint64_t checkpointId;
    std::string name;
    uint64_t timestamp;
    
    // Process Info
    RealProcessInfo info;
    
    // CPU State
    LinuxRegisters registers;
    
    // Memory State
    std::vector<MemoryRegion> memoryMap;
    std::vector<MemoryDump> memoryDumps;    // Sadece writable/anonymous regions
    
    // File Descriptors (opsiyonel - tam restore için)
    std::vector<FileDescriptorInfo> fileDescriptors;
    
    // Signals
    SignalInfo signals;
    
    // Environment variables
    std::vector<std::string> environ;
    
    // Serialization
    std::vector<uint8_t> serialize() const;
    static RealProcessCheckpoint deserialize(const std::vector<uint8_t>& data);
    
    // Helper methods
    uint64_t totalMemorySize() const;
    uint64_t dumpedMemorySize() const;
    
    RealProcessCheckpoint() : checkpointId(0), timestamp(0) {}
};

// ============================================================================
// Checkpoint Options - Hangi bilgilerin kaydedileceği
// ============================================================================
struct CheckpointOptions {
    bool saveRegisters;         // CPU register'ları
    bool saveMemory;            // Memory içeriği
    bool saveFileDescriptors;   // Açık dosyalar
    bool saveEnvironment;       // Environment variables
    bool saveSignals;           // Signal durumu
    
    // Memory filtering
    bool dumpHeap;              // [heap] bölgesini dump et
    bool dumpStack;             // [stack] bölgesini dump et
    bool dumpAnonymous;         // Anonymous mappings
    bool dumpFileBacked;        // File-backed mappings (dikkat: büyük olabilir!)
    bool skipReadOnly;          // Read-only bölgeleri atla
    bool skipVdso;              // [vdso], [vvar] atla
    
    uint64_t maxMemoryDump;     // Maksimum dump boyutu (0 = sınırsız)
    
    CheckpointOptions() 
        : saveRegisters(true), saveMemory(true),
          saveFileDescriptors(true), saveEnvironment(true),
          saveSignals(true), dumpHeap(true), dumpStack(true),
          dumpAnonymous(true), dumpFileBacked(false),
          skipReadOnly(true), skipVdso(true),
          maxMemoryDump(0) {}
    
    // Preset configurations
    static CheckpointOptions minimal() {
        CheckpointOptions opt;
        opt.saveMemory = false;
        opt.saveFileDescriptors = false;
        opt.saveEnvironment = false;
        return opt;
    }
    
    static CheckpointOptions full() {
        CheckpointOptions opt;
        opt.dumpFileBacked = true;
        opt.skipReadOnly = false;
        return opt;
    }
};

// ============================================================================
// Restore Options
// ============================================================================
struct RestoreOptions {
    // What to restore
    bool restoreRegisters;
    bool restoreMemory;
    bool restoreFileDescriptors;
    bool restoreSignals;
    
    // Control flow
    bool continueAfterRestore;      // PTRACE_CONT after restore
    bool stopOnError;               // Stop immediately on first error
    bool dryRun;                    // Validate only, don't apply changes
    
    // Memory handling
    bool allocateMissingRegions;    // Create memory regions if missing
    bool handleASLR;                // Try to handle ASLR address differences
    bool validateBeforeRestore;     // Check all regions exist before starting
    
    // Error tolerance
    bool ignoreMemoryErrors;        // Continue if memory write fails
    bool ignoreFDErrors;            // Continue if FD restoration fails
    
    RestoreOptions()
        : restoreRegisters(true), restoreMemory(true),
          restoreFileDescriptors(false),  // Tehlikeli, dikkatli kullan
          restoreSignals(true),
          continueAfterRestore(true),
          stopOnError(true),
          dryRun(false),
          allocateMissingRegions(true),
          handleASLR(false),
          validateBeforeRestore(true),
          ignoreMemoryErrors(false),
          ignoreFDErrors(true) {}
    
    // Preset: Safe restore (validates everything, stops on error)
    static RestoreOptions safe() {
        RestoreOptions opt;
        opt.stopOnError = true;
        opt.validateBeforeRestore = true;
        opt.ignoreMemoryErrors = false;
        return opt;
    }
    
    // Preset: Best effort (tries to restore as much as possible)
    static RestoreOptions bestEffort() {
        RestoreOptions opt;
        opt.stopOnError = false;
        opt.ignoreMemoryErrors = true;
        opt.ignoreFDErrors = true;
        return opt;
    }
    
    // Preset: Dry run (validation only)
    static RestoreOptions validation() {
        RestoreOptions opt;
        opt.dryRun = true;
        opt.validateBeforeRestore = true;
        return opt;
    }
};

// ============================================================================
// Restore Result
// ============================================================================
struct RestoreResult {
    bool success;
    std::string errorMessage;
    
    // Statistics
    int registersRestored;
    int memoryRegionsRestored;
    int memoryRegionsFailed;
    int fdsRestored;
    int fdsFailed;
    
    // Warnings (non-fatal issues)
    std::vector<std::string> warnings;
    
    // ASLR info
    bool aslrDetected;
    int64_t aslrOffset;
    
    RestoreResult() : success(false), registersRestored(0),
                      memoryRegionsRestored(0), memoryRegionsFailed(0),
                      fdsRestored(0), fdsFailed(0),
                      aslrDetected(false), aslrOffset(0) {}
};

} // namespace real_process
} // namespace checkpoint

