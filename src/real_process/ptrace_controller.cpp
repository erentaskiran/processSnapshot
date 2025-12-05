#include "real_process/ptrace_controller.hpp"
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/uio.h>
#include <elf.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <chrono>
#include <thread>
#include <fstream>

namespace checkpoint {
namespace real_process {

// ============================================================================
// Error Handling
// ============================================================================

std::string ptraceErrorToString(PtraceError err) {
    switch (err) {
        case PtraceError::SUCCESS:          return "Success";
        case PtraceError::PERMISSION_DENIED: return "Permission denied (need root or same user)";
        case PtraceError::NO_SUCH_PROCESS:  return "No such process";
        case PtraceError::ALREADY_TRACED:   return "Process already being traced";
        case PtraceError::NOT_STOPPED:      return "Process not stopped";
        case PtraceError::MEMORY_ERROR:     return "Memory access error";
        case PtraceError::INVALID_ARGUMENT: return "Invalid argument";
        default:                            return "Unknown error";
    }
}

PtraceError errnoToPtraceError() {
    switch (errno) {
        case EPERM:  return PtraceError::PERMISSION_DENIED;
        case ESRCH:  return PtraceError::NO_SUCH_PROCESS;
        case EBUSY:  return PtraceError::ALREADY_TRACED;
        case EFAULT: return PtraceError::MEMORY_ERROR;
        case EINVAL: return PtraceError::INVALID_ARGUMENT;
        case EIO:    return PtraceError::MEMORY_ERROR;
        default:     return PtraceError::UNKNOWN_ERROR;
    }
}

// ============================================================================
// PtraceController Implementation
// ============================================================================

PtraceController::PtraceController()
    : m_pid(0), m_attached(false), m_seized(false), m_memFd(-1) {
}

PtraceController::~PtraceController() {
    if (m_attached || m_seized) {
        detach();
    }
    closeMemFd();
}

PtraceController::PtraceController(PtraceController&& other) noexcept
    : m_pid(other.m_pid), m_attached(other.m_attached), 
      m_seized(other.m_seized), m_memFd(other.m_memFd) {
    other.m_pid = 0;
    other.m_attached = false;
    other.m_seized = false;
    other.m_memFd = -1;
}

PtraceController& PtraceController::operator=(PtraceController&& other) noexcept {
    if (this != &other) {
        if (m_attached || m_seized) {
            detach();
        }
        closeMemFd();
        
        m_pid = other.m_pid;
        m_attached = other.m_attached;
        m_seized = other.m_seized;
        m_memFd = other.m_memFd;
        
        other.m_pid = 0;
        other.m_attached = false;
        other.m_seized = false;
        other.m_memFd = -1;
    }
    return *this;
}

// ============================================================================
// Attach/Detach
// ============================================================================

PtraceError PtraceController::attach(pid_t pid) {
    if (m_attached || m_seized) {
        detach();
    }
    
    if (ptrace(PTRACE_ATTACH, pid, nullptr, nullptr) == -1) {
        return errnoToPtraceError();
    }
    
    m_pid = pid;
    m_attached = true;
    
    // Wait for process to stop
    int status;
    if (waitpid(pid, &status, 0) == -1) {
        m_attached = false;
        m_pid = 0;
        return errnoToPtraceError();
    }
    
    // Open /proc/pid/mem for faster memory access
    openMemFd();
    
    return PtraceError::SUCCESS;
}

PtraceError PtraceController::detach() {
    if (!m_attached && !m_seized) {
        return PtraceError::SUCCESS;
    }
    
    closeMemFd();
    
    if (ptrace(PTRACE_DETACH, m_pid, nullptr, nullptr) == -1) {
        // If process is not stopped, try to continue first
        if (errno == ESRCH) {
            m_attached = false;
            m_seized = false;
            m_pid = 0;
            return PtraceError::NO_SUCH_PROCESS;
        }
        return errnoToPtraceError();
    }
    
    m_attached = false;
    m_seized = false;
    m_pid = 0;
    return PtraceError::SUCCESS;
}

PtraceError PtraceController::seize(pid_t pid) {
    if (m_attached || m_seized) {
        detach();
    }
    
    if (ptrace(PTRACE_SEIZE, pid, nullptr, nullptr) == -1) {
        return errnoToPtraceError();
    }
    
    m_pid = pid;
    m_seized = true;
    
    // Open /proc/pid/mem
    openMemFd();
    
    return PtraceError::SUCCESS;
}

// ============================================================================
// Process Control
// ============================================================================

PtraceError PtraceController::stop() {
    if (!m_attached && !m_seized) {
        return PtraceError::NOT_STOPPED;
    }
    
    if (m_seized) {
        // Use PTRACE_INTERRUPT for seized processes
        if (ptrace(PTRACE_INTERRUPT, m_pid, nullptr, nullptr) == -1) {
            return errnoToPtraceError();
        }
    } else {
        // Send SIGSTOP
        if (kill(m_pid, SIGSTOP) == -1) {
            return errnoToPtraceError();
        }
    }
    
    return waitForStop() ? PtraceError::SUCCESS : PtraceError::UNKNOWN_ERROR;
}

PtraceError PtraceController::cont(int signal) {
    if (!m_attached && !m_seized) {
        return PtraceError::NOT_STOPPED;
    }
    
    if (ptrace(PTRACE_CONT, m_pid, nullptr, signal) == -1) {
        return errnoToPtraceError();
    }
    
    return PtraceError::SUCCESS;
}

PtraceError PtraceController::singleStep() {
    if (!m_attached && !m_seized) {
        return PtraceError::NOT_STOPPED;
    }
    
    if (ptrace(PTRACE_SINGLESTEP, m_pid, nullptr, nullptr) == -1) {
        return errnoToPtraceError();
    }
    
    int status;
    waitpid(m_pid, &status, 0);
    
    return PtraceError::SUCCESS;
}

PtraceError PtraceController::syscall() {
    if (!m_attached && !m_seized) {
        return PtraceError::NOT_STOPPED;
    }
    
    if (ptrace(PTRACE_SYSCALL, m_pid, nullptr, nullptr) == -1) {
        return errnoToPtraceError();
    }
    
    return PtraceError::SUCCESS;
}

bool PtraceController::isStopped() const {
    if (m_pid == 0) return false;
    
    std::string statPath = "/proc/" + std::to_string(m_pid) + "/stat";
    std::ifstream file(statPath);
    if (!file.is_open()) return false;
    
    std::string content;
    std::getline(file, content);
    
    // Find state after ')'
    size_t pos = content.rfind(')');
    if (pos == std::string::npos || pos + 2 >= content.size()) {
        return false;
    }
    
    char state = content[pos + 2];
    return state == 'T' || state == 't';
}

bool PtraceController::waitForStop(int timeoutMs) {
    int status;
    
    if (timeoutMs < 0) {
        // Blocking wait
        if (waitpid(m_pid, &status, 0) == -1) {
            return false;
        }
        return WIFSTOPPED(status);
    }
    
    // Non-blocking wait with timeout
    auto start = std::chrono::steady_clock::now();
    while (true) {
        pid_t result = waitpid(m_pid, &status, WNOHANG);
        if (result == m_pid) {
            return WIFSTOPPED(status);
        }
        if (result == -1) {
            return false;
        }
        
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= timeoutMs) {
            return false;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

// ============================================================================
// Register Access
// ============================================================================

PtraceError PtraceController::getRegisters(LinuxRegisters& regs) {
    if (!m_attached && !m_seized) {
        return PtraceError::NOT_STOPPED;
    }
    
    struct user_regs_struct uregs;
    
    if (ptrace(PTRACE_GETREGS, m_pid, nullptr, &uregs) == -1) {
        return errnoToPtraceError();
    }
    
    // Copy to our structure
    regs.r15 = uregs.r15;
    regs.r14 = uregs.r14;
    regs.r13 = uregs.r13;
    regs.r12 = uregs.r12;
    regs.rbp = uregs.rbp;
    regs.rbx = uregs.rbx;
    regs.r11 = uregs.r11;
    regs.r10 = uregs.r10;
    regs.r9 = uregs.r9;
    regs.r8 = uregs.r8;
    regs.rax = uregs.rax;
    regs.rcx = uregs.rcx;
    regs.rdx = uregs.rdx;
    regs.rsi = uregs.rsi;
    regs.rdi = uregs.rdi;
    regs.orig_rax = uregs.orig_rax;
    regs.rip = uregs.rip;
    regs.cs = uregs.cs;
    regs.eflags = uregs.eflags;
    regs.rsp = uregs.rsp;
    regs.ss = uregs.ss;
    regs.fs_base = uregs.fs_base;
    regs.gs_base = uregs.gs_base;
    regs.ds = uregs.ds;
    regs.es = uregs.es;
    regs.fs = uregs.fs;
    regs.gs = uregs.gs;
    
    return PtraceError::SUCCESS;
}

PtraceError PtraceController::setRegisters(const LinuxRegisters& regs) {
    if (!m_attached && !m_seized) {
        return PtraceError::NOT_STOPPED;
    }
    
    struct user_regs_struct uregs;
    
    // Copy from our structure
    uregs.r15 = regs.r15;
    uregs.r14 = regs.r14;
    uregs.r13 = regs.r13;
    uregs.r12 = regs.r12;
    uregs.rbp = regs.rbp;
    uregs.rbx = regs.rbx;
    uregs.r11 = regs.r11;
    uregs.r10 = regs.r10;
    uregs.r9 = regs.r9;
    uregs.r8 = regs.r8;
    uregs.rax = regs.rax;
    uregs.rcx = regs.rcx;
    uregs.rdx = regs.rdx;
    uregs.rsi = regs.rsi;
    uregs.rdi = regs.rdi;
    uregs.orig_rax = regs.orig_rax;
    uregs.rip = regs.rip;
    uregs.cs = regs.cs;
    uregs.eflags = regs.eflags;
    uregs.rsp = regs.rsp;
    uregs.ss = regs.ss;
    uregs.fs_base = regs.fs_base;
    uregs.gs_base = regs.gs_base;
    uregs.ds = regs.ds;
    uregs.es = regs.es;
    uregs.fs = regs.fs;
    uregs.gs = regs.gs;
    
    if (ptrace(PTRACE_SETREGS, m_pid, nullptr, &uregs) == -1) {
        return errnoToPtraceError();
    }
    
    return PtraceError::SUCCESS;
}

PtraceError PtraceController::getFPURegisters(std::vector<uint8_t>& fpuState) {
    if (!m_attached && !m_seized) {
        return PtraceError::NOT_STOPPED;
    }
    
    // x86_64 FPU state is 512 bytes (fxsave format)
    fpuState.resize(512);
    
    struct iovec iov;
    iov.iov_base = fpuState.data();
    iov.iov_len = fpuState.size();
    
    if (ptrace(PTRACE_GETREGSET, m_pid, NT_FPREGSET, &iov) == -1) {
        fpuState.clear();
        return errnoToPtraceError();
    }
    
    fpuState.resize(iov.iov_len);
    return PtraceError::SUCCESS;
}

PtraceError PtraceController::setFPURegisters(const std::vector<uint8_t>& fpuState) {
    if (!m_attached && !m_seized) {
        return PtraceError::NOT_STOPPED;
    }
    
    struct iovec iov;
    iov.iov_base = const_cast<uint8_t*>(fpuState.data());
    iov.iov_len = fpuState.size();
    
    if (ptrace(PTRACE_SETREGSET, m_pid, NT_FPREGSET, &iov) == -1) {
        return errnoToPtraceError();
    }
    
    return PtraceError::SUCCESS;
}

// ============================================================================
// Memory Access
// ============================================================================

uint64_t PtraceController::peekData(uint64_t addr, PtraceError* err) {
    if (!m_attached && !m_seized) {
        if (err) *err = PtraceError::NOT_STOPPED;
        return 0;
    }
    
    errno = 0;
    long data = ptrace(PTRACE_PEEKDATA, m_pid, addr, nullptr);
    if (errno != 0) {
        if (err) *err = errnoToPtraceError();
        return 0;
    }
    
    if (err) *err = PtraceError::SUCCESS;
    return static_cast<uint64_t>(data);
}

PtraceError PtraceController::pokeData(uint64_t addr, uint64_t data) {
    if (!m_attached && !m_seized) {
        return PtraceError::NOT_STOPPED;
    }
    
    if (ptrace(PTRACE_POKEDATA, m_pid, addr, data) == -1) {
        return errnoToPtraceError();
    }
    
    return PtraceError::SUCCESS;
}

PtraceError PtraceController::readMemory(uint64_t addr, void* buffer, size_t size) {
    if (!m_attached && !m_seized) {
        return PtraceError::NOT_STOPPED;
    }
    
    // Try using /proc/pid/mem first (faster)
    if (m_memFd >= 0) {
        if (pread(m_memFd, buffer, size, addr) == static_cast<ssize_t>(size)) {
            return PtraceError::SUCCESS;
        }
        // Fall back to ptrace
    }
    
    // Use PTRACE_PEEKDATA (slower but more reliable)
    uint8_t* buf = static_cast<uint8_t*>(buffer);
    size_t offset = 0;
    
    // Read word by word
    while (offset < size) {
        size_t remaining = size - offset;
        PtraceError err;
        uint64_t word = peekData(addr + offset, &err);
        
        if (err != PtraceError::SUCCESS) {
            return err;
        }
        
        size_t toCopy = std::min(remaining, sizeof(uint64_t));
        std::memcpy(buf + offset, &word, toCopy);
        offset += sizeof(uint64_t);
    }
    
    return PtraceError::SUCCESS;
}

PtraceError PtraceController::writeMemory(uint64_t addr, const void* buffer, size_t size) {
    if (!m_attached && !m_seized) {
        return PtraceError::NOT_STOPPED;
    }
    
    const uint8_t* buf = static_cast<const uint8_t*>(buffer);
    size_t offset = 0;
    
    // Write word by word
    while (offset < size) {
        size_t remaining = size - offset;
        uint64_t word = 0;
        
        // If not aligned or partial, read first
        if (remaining < sizeof(uint64_t)) {
            PtraceError err;
            word = peekData(addr + offset, &err);
            if (err != PtraceError::SUCCESS) {
                return err;
            }
            std::memcpy(&word, buf + offset, remaining);
        } else {
            std::memcpy(&word, buf + offset, sizeof(uint64_t));
        }
        
        PtraceError err = pokeData(addr + offset, word);
        if (err != PtraceError::SUCCESS) {
            return err;
        }
        
        offset += sizeof(uint64_t);
    }
    
    return PtraceError::SUCCESS;
}

MemoryDump PtraceController::dumpMemoryRegion(const MemoryRegion& region) {
    MemoryDump dump;
    dump.region = region;
    dump.isValid = false;
    
    if (!m_attached && !m_seized) {
        return dump;
    }
    
    // Don't dump vsyscall, vdso, vvar
    if (region.isVdso()) {
        return dump;
    }
    
    // Don't dump non-readable regions
    if (!region.readable) {
        return dump;
    }
    
    size_t size = region.size();
    dump.data.resize(size);
    
    PtraceError err = readMemory(region.startAddr, dump.data.data(), size);
    if (err == PtraceError::SUCCESS) {
        dump.isValid = true;
    } else {
        dump.data.clear();
    }
    
    return dump;
}

PtraceError PtraceController::restoreMemoryRegion(const MemoryDump& dump) {
    if (!dump.isValid || dump.data.empty()) {
        return PtraceError::INVALID_ARGUMENT;
    }
    
    if (!m_attached && !m_seized) {
        return PtraceError::NOT_STOPPED;
    }
    
    // Only restore writable regions
    if (!dump.region.writable) {
        return PtraceError::PERMISSION_DENIED;
    }
    
    return writeMemory(dump.region.startAddr, dump.data.data(), dump.data.size());
}

// ============================================================================
// Signal Handling
// ============================================================================

PtraceError PtraceController::injectSignal(int signal) {
    if (!m_attached && !m_seized) {
        return PtraceError::NOT_STOPPED;
    }
    
    if (kill(m_pid, signal) == -1) {
        return errnoToPtraceError();
    }
    
    return PtraceError::SUCCESS;
}

uint64_t PtraceController::getPendingSignals() {
    ProcFSReader reader;
    auto signals = reader.getSignalInfo(m_pid);
    return signals ? signals->pending : 0;
}

// ============================================================================
// Private Helpers
// ============================================================================

PtraceError PtraceController::openMemFd() {
    if (m_memFd >= 0) {
        return PtraceError::SUCCESS;
    }
    
    std::string path = "/proc/" + std::to_string(m_pid) + "/mem";
    m_memFd = open(path.c_str(), O_RDONLY);
    
    if (m_memFd < 0) {
        return errnoToPtraceError();
    }
    
    return PtraceError::SUCCESS;
}

void PtraceController::closeMemFd() {
    if (m_memFd >= 0) {
        close(m_memFd);
        m_memFd = -1;
    }
}

PtraceError PtraceController::waitForSignal(int* status) {
    if (waitpid(m_pid, status, 0) == -1) {
        return errnoToPtraceError();
    }
    return PtraceError::SUCCESS;
}

// ============================================================================
// ScopedAttach
// ============================================================================

PtraceController::ScopedAttach::ScopedAttach(PtraceController& ctrl, pid_t pid)
    : m_ctrl(ctrl), m_valid(false) {
    m_error = ctrl.attach(pid);
    m_valid = (m_error == PtraceError::SUCCESS);
}

PtraceController::ScopedAttach::~ScopedAttach() {
    if (m_valid) {
        m_ctrl.detach();
    }
}

// ============================================================================
// RealProcessCheckpointer Implementation
// ============================================================================

RealProcessCheckpointer::RealProcessCheckpointer() {
}

RealProcessCheckpointer::~RealProcessCheckpointer() {
}

void RealProcessCheckpointer::reportProgress(const std::string& stage, double progress) {
    if (m_progressCallback) {
        m_progressCallback(stage, progress);
    }
}

std::optional<RealProcessInfo> RealProcessCheckpointer::getProcessInfo(pid_t pid) {
    return m_procReader.getProcessInfo(pid);
}

std::optional<RealProcessCheckpoint> RealProcessCheckpointer::createCheckpoint(
    pid_t pid, 
    const std::string& name,
    const CheckpointOptions& options) {
    
    reportProgress("Starting checkpoint", 0.0);
    
    // Verify process exists
    if (!m_procReader.processExists(pid)) {
        m_lastError = "Process " + std::to_string(pid) + " does not exist";
        return std::nullopt;
    }
    
    RealProcessCheckpoint checkpoint;
    checkpoint.checkpointId = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    checkpoint.name = name.empty() ? "checkpoint_" + std::to_string(pid) : name;
    checkpoint.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // Get process info
    reportProgress("Reading process info", 0.1);
    auto info = m_procReader.getProcessInfo(pid);
    if (!info) {
        m_lastError = "Failed to read process info";
        return std::nullopt;
    }
    checkpoint.info = *info;
    
    // Attach to process
    reportProgress("Attaching to process", 0.2);
    PtraceController ptrace;
    PtraceError err = ptrace.attach(pid);
    if (err != PtraceError::SUCCESS) {
        m_lastError = "Failed to attach: " + ptraceErrorToString(err);
        return std::nullopt;
    }
    
    // Get registers
    if (options.saveRegisters) {
        reportProgress("Reading registers", 0.3);
        err = ptrace.getRegisters(checkpoint.registers);
        if (err != PtraceError::SUCCESS) {
            m_lastError = "Failed to read registers: " + ptraceErrorToString(err);
            // Continue anyway
        }
        
        // FPU registers
        std::vector<uint8_t> fpuState;
        if (ptrace.getFPURegisters(fpuState) == PtraceError::SUCCESS) {
            checkpoint.registers.hasFPU = true;
            checkpoint.registers.fpuState = std::move(fpuState);
        }
    }
    
    // Get memory maps
    reportProgress("Reading memory maps", 0.4);
    checkpoint.memoryMap = m_procReader.getMemoryMaps(pid);
    
    // Dump memory
    if (options.saveMemory) {
        reportProgress("Dumping memory", 0.5);
        
        uint64_t totalDumped = 0;
        int regionIndex = 0;
        int totalRegions = checkpoint.memoryMap.size();
        
        for (const auto& region : checkpoint.memoryMap) {
            // Skip based on options
            if (options.skipReadOnly && !region.writable) continue;
            if (options.skipVdso && region.isVdso()) continue;
            if (!options.dumpFileBacked && !region.isAnonymous()) continue;
            if (!options.dumpHeap && region.isHeap()) continue;
            if (!options.dumpStack && region.isStack()) continue;
            
            // Check size limit
            if (options.maxMemoryDump > 0 && totalDumped >= options.maxMemoryDump) {
                break;
            }
            
            // Dump region
            MemoryDump dump = ptrace.dumpMemoryRegion(region);
            if (dump.isValid) {
                totalDumped += dump.data.size();
                checkpoint.memoryDumps.push_back(std::move(dump));
            }
            
            // Update progress
            regionIndex++;
            reportProgress("Dumping memory", 0.5 + 0.3 * (double(regionIndex) / totalRegions));
        }
    }
    
    // Get signals
    if (options.saveSignals) {
        reportProgress("Reading signals", 0.85);
        auto signals = m_procReader.getSignalInfo(pid);
        if (signals) {
            checkpoint.signals = *signals;
        }
    }
    
    // Get environment
    if (options.saveEnvironment) {
        reportProgress("Reading environment", 0.9);
        checkpoint.environ = m_procReader.getEnvironment(pid);
    }
    
    // Get file descriptors
    if (options.saveFileDescriptors) {
        reportProgress("Reading file descriptors", 0.95);
        checkpoint.fileDescriptors = m_procReader.getFileDescriptors(pid);
    }
    
    // Detach (done automatically by destructor)
    reportProgress("Complete", 1.0);
    
    return checkpoint;
}

bool RealProcessCheckpointer::saveCheckpoint(const RealProcessCheckpoint& checkpoint, 
                                             const std::string& filepath) {
    auto data = checkpoint.serialize();
    
    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        m_lastError = "Failed to open file for writing: " + filepath;
        return false;
    }
    
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    if (!file.good()) {
        m_lastError = "Failed to write checkpoint data";
        return false;
    }
    
    return true;
}

std::optional<RealProcessCheckpoint> RealProcessCheckpointer::loadCheckpoint(
    const std::string& filepath) {
    
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        m_lastError = "Failed to open file: " + filepath;
        return std::nullopt;
    }
    
    size_t size = file.tellg();
    file.seekg(0);
    
    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);
    
    if (!file.good()) {
        m_lastError = "Failed to read checkpoint data";
        return std::nullopt;
    }
    
    return RealProcessCheckpoint::deserialize(data);
}

PtraceError RealProcessCheckpointer::restoreCheckpoint(
    pid_t pid,
    const RealProcessCheckpoint& checkpoint,
    const RestoreOptions& options) {
    
    // Attach to process
    PtraceController ptrace;
    PtraceError err = ptrace.attach(pid);
    if (err != PtraceError::SUCCESS) {
        m_lastError = "Failed to attach: " + ptraceErrorToString(err);
        return err;
    }
    
    // Restore registers
    if (options.restoreRegisters) {
        err = ptrace.setRegisters(checkpoint.registers);
        if (err != PtraceError::SUCCESS) {
            m_lastError = "Failed to restore registers: " + ptraceErrorToString(err);
            return err;
        }
        
        // FPU registers
        if (checkpoint.registers.hasFPU) {
            ptrace.setFPURegisters(checkpoint.registers.fpuState);
        }
    }
    
    // Restore memory
    if (options.restoreMemory) {
        for (const auto& dump : checkpoint.memoryDumps) {
            err = ptrace.restoreMemoryRegion(dump);
            if (err != PtraceError::SUCCESS) {
                // Log but continue
                m_lastError = "Warning: Failed to restore region at " + 
                             std::to_string(dump.region.startAddr);
            }
        }
    }
    
    // Continue process
    if (options.continueAfterRestore) {
        ptrace.cont();
    }
    
    return PtraceError::SUCCESS;
}

RealProcessCheckpointer::CheckpointDiff RealProcessCheckpointer::compareCheckpoints(
    const RealProcessCheckpoint& cp1,
    const RealProcessCheckpoint& cp2) {
    
    CheckpointDiff diff{};
    
    // Compare registers
    if (std::memcmp(&cp1.registers, &cp2.registers, 
                    sizeof(LinuxRegisters) - sizeof(std::vector<uint8_t>)) != 0) {
        diff.registersChanged = true;
        
        // Detailed register comparison
        if (cp1.registers.rax != cp2.registers.rax) diff.changedRegisters.push_back("rax");
        if (cp1.registers.rbx != cp2.registers.rbx) diff.changedRegisters.push_back("rbx");
        if (cp1.registers.rcx != cp2.registers.rcx) diff.changedRegisters.push_back("rcx");
        if (cp1.registers.rdx != cp2.registers.rdx) diff.changedRegisters.push_back("rdx");
        if (cp1.registers.rsp != cp2.registers.rsp) diff.changedRegisters.push_back("rsp");
        if (cp1.registers.rbp != cp2.registers.rbp) diff.changedRegisters.push_back("rbp");
        if (cp1.registers.rip != cp2.registers.rip) diff.changedRegisters.push_back("rip");
        // ... more registers
    }
    
    // Compare memory - simplified
    if (cp1.memoryDumps.size() != cp2.memoryDumps.size()) {
        diff.memoryChanged = true;
    } else {
        for (size_t i = 0; i < cp1.memoryDumps.size(); ++i) {
            if (cp1.memoryDumps[i].data != cp2.memoryDumps[i].data) {
                diff.memoryChanged = true;
                diff.modifiedRegions.push_back(cp1.memoryDumps[i].region);
                diff.totalBytesChanged += cp1.memoryDumps[i].data.size();
            }
        }
    }
    
    // Compare file descriptors
    if (cp1.fileDescriptors.size() != cp2.fileDescriptors.size()) {
        diff.filesChanged = true;
    }
    
    return diff;
}

} // namespace real_process
} // namespace checkpoint
