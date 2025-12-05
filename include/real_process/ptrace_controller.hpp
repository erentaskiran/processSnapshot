#pragma once

#include "real_process/real_process_types.hpp"
#include "real_process/proc_reader.hpp"
#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <memory>
#include <functional>

namespace checkpoint {
namespace real_process {

// ============================================================================
// Ptrace Error Codes
// ============================================================================
enum class PtraceError {
    SUCCESS = 0,
    PERMISSION_DENIED,      // EPERM - root gerekli veya yetki yok
    NO_SUCH_PROCESS,        // ESRCH - process yok
    ALREADY_TRACED,         // process zaten trace ediliyor
    NOT_STOPPED,            // process durdurulmamış
    MEMORY_ERROR,           // memory okuma/yazma hatası
    INVALID_ARGUMENT,       // geçersiz argüman
    UNKNOWN_ERROR
};

std::string ptraceErrorToString(PtraceError err);

// ============================================================================
// Ptrace Controller - Process Kontrolü ve Memory Erişimi
// ============================================================================
class PtraceController {
public:
    PtraceController();
    ~PtraceController();
    
    // Move only (no copy)
    PtraceController(const PtraceController&) = delete;
    PtraceController& operator=(const PtraceController&) = delete;
    PtraceController(PtraceController&&) noexcept;
    PtraceController& operator=(PtraceController&&) noexcept;
    
    // ========================================================================
    // Attach/Detach
    // ========================================================================
    
    // Process'e attach ol (PTRACE_ATTACH)
    // Process otomatik olarak durdurulur
    PtraceError attach(pid_t pid);
    
    // Process'ten detach ol (PTRACE_DETACH)
    // Process devam eder
    PtraceError detach();
    
    // Seize (PTRACE_SEIZE) - daha modern, process durmaz
    PtraceError seize(pid_t pid);
    
    // Attached mi?
    bool isAttached() const { return m_attached; }
    pid_t getAttachedPid() const { return m_pid; }
    
    // ========================================================================
    // Process Control
    // ========================================================================
    
    // Process'i durdur (SIGSTOP)
    PtraceError stop();
    
    // Process'i devam ettir (PTRACE_CONT)
    PtraceError cont(int signal = 0);
    
    // Tek instruction çalıştır (PTRACE_SINGLESTEP)
    PtraceError singleStep();
    
    // Syscall'da dur (PTRACE_SYSCALL)
    PtraceError syscall();
    
    // Process durdurulmuş mu?
    bool isStopped() const;
    
    // Wait for stop
    bool waitForStop(int timeoutMs = -1);
    
    // ========================================================================
    // Register Access
    // ========================================================================
    
    // Tüm register'ları oku (PTRACE_GETREGS)
    PtraceError getRegisters(LinuxRegisters& regs);
    
    // Tüm register'ları yaz (PTRACE_SETREGS)
    PtraceError setRegisters(const LinuxRegisters& regs);
    
    // Tek register oku/yaz
    uint64_t getRegister(int regNum);
    PtraceError setRegister(int regNum, uint64_t value);
    
    // FPU register'ları (PTRACE_GETFPREGS / PTRACE_SETFPREGS)
    PtraceError getFPURegisters(std::vector<uint8_t>& fpuState);
    PtraceError setFPURegisters(const std::vector<uint8_t>& fpuState);
    
    // ========================================================================
    // Memory Access
    // ========================================================================
    
    // Tek word oku (PTRACE_PEEKDATA)
    uint64_t peekData(uint64_t addr, PtraceError* err = nullptr);
    
    // Tek word yaz (PTRACE_POKEDATA)
    PtraceError pokeData(uint64_t addr, uint64_t data);
    
    // Bellek bloğu oku (/proc/<pid>/mem kullanarak - daha hızlı)
    PtraceError readMemory(uint64_t addr, void* buffer, size_t size);
    
    // Bellek bloğu yaz
    PtraceError writeMemory(uint64_t addr, const void* buffer, size_t size);
    
    // Memory region dump
    MemoryDump dumpMemoryRegion(const MemoryRegion& region);
    
    // Memory region restore
    PtraceError restoreMemoryRegion(const MemoryDump& dump);
    
    // ========================================================================
    // Signal Handling
    // ========================================================================
    
    // Signal inject
    PtraceError injectSignal(int signal);
    
    // Pending signals
    uint64_t getPendingSignals();
    
    // ========================================================================
    // Convenience Methods
    // ========================================================================
    
    // RAII-style scoped attach
    class ScopedAttach {
    public:
        ScopedAttach(PtraceController& ctrl, pid_t pid);
        ~ScopedAttach();
        bool isValid() const { return m_valid; }
        PtraceError error() const { return m_error; }
    private:
        PtraceController& m_ctrl;
        bool m_valid;
        PtraceError m_error;
    };
    
private:
    pid_t m_pid;
    bool m_attached;
    bool m_seized;
    int m_memFd;            // /proc/<pid>/mem file descriptor
    
    PtraceError openMemFd();
    void closeMemFd();
    PtraceError waitForSignal(int* status);
};

// ============================================================================
// Checkpoint Manager - Ana Checkpoint/Restore Sınıfı
// ============================================================================
class RealProcessCheckpointer {
public:
    RealProcessCheckpointer();
    ~RealProcessCheckpointer();
    
    // ========================================================================
    // Checkpoint Operations
    // ========================================================================
    
    // Full checkpoint al
    std::optional<RealProcessCheckpoint> createCheckpoint(
        pid_t pid, 
        const std::string& name = "",
        const CheckpointOptions& options = CheckpointOptions()
    );
    
    // Checkpoint'i dosyaya kaydet
    bool saveCheckpoint(const RealProcessCheckpoint& checkpoint, 
                       const std::string& filepath);
    
    // Checkpoint'i dosyadan yükle
    std::optional<RealProcessCheckpoint> loadCheckpoint(const std::string& filepath);
    
    // ========================================================================
    // Restore Operations
    // ========================================================================
    
    // Process'i checkpoint'ten restore et
    // DİKKAT: Bu tehlikeli bir operasyon!
    PtraceError restoreCheckpoint(
        pid_t pid,
        const RealProcessCheckpoint& checkpoint,
        const RestoreOptions& options = RestoreOptions()
    );
    
    // ========================================================================
    // Comparison / Diff
    // ========================================================================
    
    // İki checkpoint arasındaki farkları bul
    struct CheckpointDiff {
        bool registersChanged;
        std::vector<std::string> changedRegisters;
        
        bool memoryChanged;
        std::vector<MemoryRegion> addedRegions;
        std::vector<MemoryRegion> removedRegions;
        std::vector<MemoryRegion> modifiedRegions;
        uint64_t totalBytesChanged;
        
        bool filesChanged;
        std::vector<FileDescriptorInfo> addedFds;
        std::vector<FileDescriptorInfo> removedFds;
    };
    
    CheckpointDiff compareCheckpoints(
        const RealProcessCheckpoint& cp1,
        const RealProcessCheckpoint& cp2
    );
    
    // ========================================================================
    // Utilities
    // ========================================================================
    
    // Process bilgisi al (checkpoint olmadan)
    std::optional<RealProcessInfo> getProcessInfo(pid_t pid);
    
    // Son hata mesajı
    std::string getLastError() const { return m_lastError; }
    
    // Callback'ler
    using ProgressCallback = std::function<void(const std::string& stage, double progress)>;
    void setProgressCallback(ProgressCallback cb) { m_progressCallback = cb; }
    
private:
    ProcFSReader m_procReader;
    std::string m_lastError;
    ProgressCallback m_progressCallback;
    
    void reportProgress(const std::string& stage, double progress);
};

} // namespace real_process
} // namespace checkpoint
