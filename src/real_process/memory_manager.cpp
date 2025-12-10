#include "real_process/memory_manager.hpp"
#include "real_process/proc_reader.hpp"
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <set>
#include <map>

namespace checkpoint {
namespace real_process {

// ============================================================================
// Error Handling
// ============================================================================

std::string memoryErrorToString(MemoryError err) {
    switch (err) {
        case MemoryError::SUCCESS:            return "Success";
        case MemoryError::PERMISSION_DENIED:  return "Permission denied";
        case MemoryError::INVALID_ADDRESS:    return "Invalid address";
        case MemoryError::REGION_EXISTS:      return "Region already exists";
        case MemoryError::REGION_NOT_FOUND:   return "Region not found";
        case MemoryError::ALLOCATION_FAILED:  return "Memory allocation failed";
        case MemoryError::MMAP_FAILED:        return "mmap failed";
        case MemoryError::MUNMAP_FAILED:      return "munmap failed";
        case MemoryError::MPROTECT_FAILED:    return "mprotect failed";
        case MemoryError::PROCESS_NOT_ATTACHED: return "Process not attached";
        case MemoryError::ASLR_MISMATCH:      return "ASLR address mismatch";
        default:                              return "Unknown error";
    }
}

// ============================================================================
// Helper Functions
// ============================================================================

int toProtFlags(bool readable, bool writable, bool executable) {
    int prot = PROT_NONE;
    if (readable)   prot |= PROT_READ;
    if (writable)   prot |= PROT_WRITE;
    if (executable) prot |= PROT_EXEC;
    return prot;
}

void fromProtFlags(int prot, bool& readable, bool& writable, bool& executable) {
    readable   = (prot & PROT_READ)  != 0;
    writable   = (prot & PROT_WRITE) != 0;
    executable = (prot & PROT_EXEC)  != 0;
}

bool regionsOverlap(const MemoryRegion& a, const MemoryRegion& b) {
    return !(a.endAddr <= b.startAddr || b.endAddr <= a.startAddr);
}

bool regionsAdjacent(const MemoryRegion& a, const MemoryRegion& b) {
    return a.endAddr == b.startAddr || b.endAddr == a.startAddr;
}

// ============================================================================
// ASLR Configuration
// ============================================================================

ASLRConfig ASLRConfig::detect() {
    ASLRConfig config;
    config.enabled = false;
    config.level = 0;
    config.processIsPIE = false;
    
    std::ifstream file("/proc/sys/kernel/randomize_va_space");
    if (file.is_open()) {
        file >> config.level;
        config.enabled = (config.level > 0);
    }
    
    return config;
}

bool ASLRConfig::disable() {
    std::ofstream file("/proc/sys/kernel/randomize_va_space");
    if (!file.is_open()) {
        return false;  // Need root
    }
    file << "0";
    return file.good();
}

bool ASLRConfig::enable() {
    std::ofstream file("/proc/sys/kernel/randomize_va_space");
    if (!file.is_open()) {
        return false;  // Need root
    }
    file << "2";  // Full ASLR
    return file.good();
}

// ============================================================================
// MemoryManager Implementation
// ============================================================================

MemoryManager::MemoryManager()
    : m_pid(0) {
}

MemoryManager::~MemoryManager() {
    unbindProcess();
}

void MemoryManager::reportProgress(const std::string& stage, double progress) {
    if (m_progressCallback) {
        m_progressCallback(stage, progress);
    }
}

// ============================================================================
// Process Binding
// ============================================================================

MemoryError MemoryManager::bindProcess(pid_t pid) {
    if (m_pid > 0) {
        unbindProcess();
    }
    
    // Verify process exists
    std::string procPath = "/proc/" + std::to_string(pid);
    if (access(procPath.c_str(), F_OK) != 0) {
        m_lastError = "Process does not exist: " + std::to_string(pid);
        return MemoryError::INVALID_ADDRESS;
    }
    
    m_pid = pid;
    return MemoryError::SUCCESS;
}

void MemoryManager::unbindProcess() {
    m_pid = 0;
}

// ============================================================================
// Address Space Analysis
// ============================================================================

std::vector<MemoryRegion> MemoryManager::getCurrentMemoryMap() {
    if (m_pid <= 0) {
        return {};
    }
    
    ProcFSReader reader;
    return reader.getMemoryMaps(m_pid);
}

bool MemoryManager::regionExists(uint64_t addr, size_t size) {
    auto maps = getCurrentMemoryMap();
    
    for (const auto& region : maps) {
        // Check if the requested range is fully contained in this region
        if (addr >= region.startAddr && (addr + size) <= region.endAddr) {
            return true;
        }
    }
    
    return false;
}

std::optional<MemoryRegion> MemoryManager::findRegion(uint64_t addr) {
    auto maps = getCurrentMemoryMap();
    
    for (const auto& region : maps) {
        if (addr >= region.startAddr && addr < region.endAddr) {
            return region;
        }
    }
    
    return std::nullopt;
}

AddressSpaceComparison MemoryManager::compareAddressSpace(
    const std::vector<MemoryRegion>& checkpointMap,
    const std::vector<MemoryRegion>& currentMap) {
    
    AddressSpaceComparison result;
    result.aslrDetected = false;
    result.stackOffset = 0;
    result.heapOffset = 0;
    result.baseOffset = 0;
    
    // Build lookup map for current regions
    std::map<uint64_t, MemoryRegion> currentByAddr;
    for (const auto& region : currentMap) {
        currentByAddr[region.startAddr] = region;
    }
    
    // Find checkpoint stack and heap for ASLR detection
    const MemoryRegion* checkpointStack = nullptr;
    const MemoryRegion* checkpointHeap = nullptr;
    const MemoryRegion* currentStack = nullptr;
    const MemoryRegion* currentHeap = nullptr;
    
    for (const auto& region : checkpointMap) {
        if (region.isStack()) checkpointStack = &region;
        if (region.isHeap()) checkpointHeap = &region;
    }
    
    for (const auto& region : currentMap) {
        if (region.isStack()) currentStack = &region;
        if (region.isHeap()) currentHeap = &region;
    }
    
    // Detect ASLR offsets
    if (checkpointStack && currentStack) {
        result.stackOffset = static_cast<int64_t>(currentStack->startAddr) - 
                            static_cast<int64_t>(checkpointStack->startAddr);
        if (result.stackOffset != 0) {
            result.aslrDetected = true;
        }
    }
    
    if (checkpointHeap && currentHeap) {
        result.heapOffset = static_cast<int64_t>(currentHeap->startAddr) - 
                           static_cast<int64_t>(checkpointHeap->startAddr);
        if (result.heapOffset != 0) {
            result.aslrDetected = true;
        }
    }
    
    // Compare regions
    std::set<uint64_t> matchedCurrent;
    
    for (const auto& cpRegion : checkpointMap) {
        auto it = currentByAddr.find(cpRegion.startAddr);
        
        if (it != currentByAddr.end()) {
            // Found at same address
            const auto& curRegion = it->second;
            
            if (cpRegion.size() == curRegion.size()) {
                result.matching.push_back(cpRegion);
            } else {
                // Same address but different size - treat as modified
                result.missing.push_back(cpRegion);
            }
            
            matchedCurrent.insert(cpRegion.startAddr);
        } else {
            // Region not found at checkpoint address
            // Check if it might have moved (ASLR)
            bool foundMoved = false;
            
            if (result.aslrDetected) {
                // Try to find by applying offset
                int64_t offset = cpRegion.isStack() ? result.stackOffset :
                                cpRegion.isHeap() ? result.heapOffset : result.baseOffset;
                
                uint64_t expectedAddr = cpRegion.startAddr + offset;
                auto movedIt = currentByAddr.find(expectedAddr);
                
                if (movedIt != currentByAddr.end() && 
                    movedIt->second.size() == cpRegion.size()) {
                    result.moved.push_back({cpRegion, movedIt->second});
                    matchedCurrent.insert(expectedAddr);
                    foundMoved = true;
                }
            }
            
            if (!foundMoved) {
                result.missing.push_back(cpRegion);
            }
        }
    }
    
    // Find extra regions (in current but not in checkpoint)
    for (const auto& curRegion : currentMap) {
        if (matchedCurrent.find(curRegion.startAddr) == matchedCurrent.end()) {
            result.extra.push_back(curRegion);
        }
    }
    
    return result;
}

// ============================================================================
// ASLR Detection
// ============================================================================

std::optional<int64_t> MemoryManager::detectASLROffset(
    const std::vector<MemoryRegion>& checkpointMap,
    const std::vector<MemoryRegion>& currentMap) {
    
    auto comparison = compareAddressSpace(checkpointMap, currentMap);
    
    if (!comparison.aslrDetected) {
        return 0;  // No ASLR offset
    }
    
    // Use heap offset as primary (more reliable than stack)
    if (comparison.heapOffset != 0) {
        return comparison.heapOffset;
    }
    
    return comparison.stackOffset;
}

bool MemoryManager::isASLREnabled() {
    return ASLRConfig::detect().enabled;
}

int MemoryManager::getASLRLevel() {
    return ASLRConfig::detect().level;
}

// ============================================================================
// Register Save/Restore for Syscall Injection
// ============================================================================

bool MemoryManager::saveRegisters(LinuxRegisters& saved) {
    struct user_regs_struct regs;
    
    if (ptrace(PTRACE_GETREGS, m_pid, nullptr, &regs) == -1) {
        m_lastError = "Failed to save registers: " + std::string(strerror(errno));
        return false;
    }
    
    // Copy to our structure
    saved.r15 = regs.r15;
    saved.r14 = regs.r14;
    saved.r13 = regs.r13;
    saved.r12 = regs.r12;
    saved.rbp = regs.rbp;
    saved.rbx = regs.rbx;
    saved.r11 = regs.r11;
    saved.r10 = regs.r10;
    saved.r9 = regs.r9;
    saved.r8 = regs.r8;
    saved.rax = regs.rax;
    saved.rcx = regs.rcx;
    saved.rdx = regs.rdx;
    saved.rsi = regs.rsi;
    saved.rdi = regs.rdi;
    saved.orig_rax = regs.orig_rax;
    saved.rip = regs.rip;
    saved.cs = regs.cs;
    saved.eflags = regs.eflags;
    saved.rsp = regs.rsp;
    saved.ss = regs.ss;
    saved.fs_base = regs.fs_base;
    saved.gs_base = regs.gs_base;
    saved.ds = regs.ds;
    saved.es = regs.es;
    saved.fs = regs.fs;
    saved.gs = regs.gs;
    
    return true;
}

bool MemoryManager::restoreRegisters(const LinuxRegisters& saved) {
    struct user_regs_struct regs;
    
    // Copy from our structure
    regs.r15 = saved.r15;
    regs.r14 = saved.r14;
    regs.r13 = saved.r13;
    regs.r12 = saved.r12;
    regs.rbp = saved.rbp;
    regs.rbx = saved.rbx;
    regs.r11 = saved.r11;
    regs.r10 = saved.r10;
    regs.r9 = saved.r9;
    regs.r8 = saved.r8;
    regs.rax = saved.rax;
    regs.rcx = saved.rcx;
    regs.rdx = saved.rdx;
    regs.rsi = saved.rsi;
    regs.rdi = saved.rdi;
    regs.orig_rax = saved.orig_rax;
    regs.rip = saved.rip;
    regs.cs = saved.cs;
    regs.eflags = saved.eflags;
    regs.rsp = saved.rsp;
    regs.ss = saved.ss;
    regs.fs_base = saved.fs_base;
    regs.gs_base = saved.gs_base;
    regs.ds = saved.ds;
    regs.es = saved.es;
    regs.fs = saved.fs;
    regs.gs = saved.gs;
    
    if (ptrace(PTRACE_SETREGS, m_pid, nullptr, &regs) == -1) {
        m_lastError = "Failed to restore registers: " + std::string(strerror(errno));
        return false;
    }
    
    return true;
}

// ============================================================================
// Syscall Injection
// ============================================================================

int64_t MemoryManager::injectSyscall(
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
    LinuxRegisters savedRegs;
    if (!saveRegisters(savedRegs)) {
        return -1;
    }
    
    // Get current instruction pointer
    uint64_t originalRip = savedRegs.rip;
    
    // Read original instruction at RIP (we'll restore it)
    uint64_t originalCode = ptrace(PTRACE_PEEKTEXT, m_pid, originalRip, nullptr);
    if (errno != 0 && originalCode == static_cast<uint64_t>(-1)) {
        m_lastError = "Failed to read original code: " + std::string(strerror(errno));
        return -1;
    }
    
    // Write syscall instruction (0x0F 0x05 = syscall on x86_64)
    uint64_t syscallCode = (originalCode & ~0xFFFFULL) | 0x050F;
    if (ptrace(PTRACE_POKETEXT, m_pid, originalRip, syscallCode) == -1) {
        m_lastError = "Failed to inject syscall: " + std::string(strerror(errno));
        return -1;
    }
    
    // Set up syscall arguments
    // x86_64 syscall ABI: rax=syscall#, rdi=arg1, rsi=arg2, rdx=arg3, r10=arg4, r8=arg5, r9=arg6
    struct user_regs_struct regs;
    if (ptrace(PTRACE_GETREGS, m_pid, nullptr, &regs) == -1) {
        // Restore original code before returning
        ptrace(PTRACE_POKETEXT, m_pid, originalRip, originalCode);
        m_lastError = "Failed to get registers for syscall";
        return -1;
    }
    
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
    
    // Execute the syscall (single step)
    if (ptrace(PTRACE_SINGLESTEP, m_pid, nullptr, nullptr) == -1) {
        ptrace(PTRACE_POKETEXT, m_pid, originalRip, originalCode);
        restoreRegisters(savedRegs);
        m_lastError = "Failed to execute syscall";
        return -1;
    }
    
    // Wait for process to stop
    int status;
    waitpid(m_pid, &status, 0);
    
    // Get syscall result
    if (ptrace(PTRACE_GETREGS, m_pid, nullptr, &regs) == -1) {
        ptrace(PTRACE_POKETEXT, m_pid, originalRip, originalCode);
        restoreRegisters(savedRegs);
        m_lastError = "Failed to get syscall result";
        return -1;
    }
    
    int64_t result = static_cast<int64_t>(regs.rax);
    
    // Restore original code
    if (ptrace(PTRACE_POKETEXT, m_pid, originalRip, originalCode) == -1) {
        m_lastError = "Warning: Failed to restore original code";
    }
    
    // Restore original registers
    if (!restoreRegisters(savedRegs)) {
        m_lastError = "Warning: Failed to restore original registers";
    }
    
    return result;
}

// ============================================================================
// Memory Mapping Operations
// ============================================================================

MemoryError MemoryManager::createMapping(
    uint64_t addr,
    size_t size,
    bool readable,
    bool writable,
    bool executable,
    bool fixed) {
    
    if (m_pid <= 0) {
        m_lastError = "No process bound";
        return MemoryError::PROCESS_NOT_ATTACHED;
    }
    
    // Calculate mmap flags
    int prot = toProtFlags(readable, writable, executable);
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    if (fixed && addr != 0) {
        flags |= MAP_FIXED;
    }
    
    // Inject mmap syscall
    // mmap(addr, length, prot, flags, fd, offset)
    int64_t result = injectSyscall(
        SYS_mmap,
        addr,
        size,
        prot,
        flags,
        static_cast<uint64_t>(-1),  // fd = -1 for anonymous
        0                            // offset = 0
    );
    
    if (result == -1 || (result < 0 && result > -4096)) {
        m_lastError = "mmap failed with error: " + std::to_string(-result);
        return MemoryError::MMAP_FAILED;
    }
    
    // If not fixed and we got a different address, that's okay
    if (fixed && addr != 0 && static_cast<uint64_t>(result) != addr) {
        m_lastError = "mmap returned different address than requested";
        // Try to unmap the accidental mapping
        injectSyscall(SYS_munmap, result, size);
        return MemoryError::MMAP_FAILED;
    }
    
    return MemoryError::SUCCESS;
}

MemoryError MemoryManager::removeMapping(uint64_t addr, size_t size) {
    if (m_pid <= 0) {
        return MemoryError::PROCESS_NOT_ATTACHED;
    }
    
    // Inject munmap syscall
    int64_t result = injectSyscall(SYS_munmap, addr, size);
    
    if (result != 0) {
        m_lastError = "munmap failed with error: " + std::to_string(-result);
        return MemoryError::MUNMAP_FAILED;
    }
    
    return MemoryError::SUCCESS;
}

MemoryError MemoryManager::changeProtection(
    uint64_t addr,
    size_t size,
    bool readable,
    bool writable,
    bool executable) {
    
    if (m_pid <= 0) {
        return MemoryError::PROCESS_NOT_ATTACHED;
    }
    
    int prot = toProtFlags(readable, writable, executable);
    
    // Inject mprotect syscall
    int64_t result = injectSyscall(SYS_mprotect, addr, size, prot);
    
    if (result != 0) {
        m_lastError = "mprotect failed with error: " + std::to_string(-result);
        return MemoryError::MPROTECT_FAILED;
    }
    
    return MemoryError::SUCCESS;
}

// ============================================================================
// Batch Operations for Restore
// ============================================================================

MemoryManager::PrepareResult MemoryManager::prepareForRestore(
    const std::vector<MemoryRegion>& checkpointMap,
    bool allocateMissing,
    bool removeExtra) {
    
    PrepareResult result;
    result.error = MemoryError::SUCCESS;
    result.needsRelocation = false;
    result.relocationOffset = 0;
    
    if (m_pid <= 0) {
        result.error = MemoryError::PROCESS_NOT_ATTACHED;
        return result;
    }
    
    reportProgress("Analyzing address space", 0.1);
    
    // Get current memory map
    auto currentMap = getCurrentMemoryMap();
    
    // Compare address spaces
    auto comparison = compareAddressSpace(checkpointMap, currentMap);
    
    // Check for ASLR
    if (comparison.aslrDetected) {
        result.warnings.push_back("ASLR detected: addresses have shifted");
        result.needsRelocation = true;
        
        // Use heap offset if available, otherwise stack offset
        result.relocationOffset = (comparison.heapOffset != 0) ? 
                                  comparison.heapOffset : comparison.stackOffset;
        
        result.warnings.push_back("Relocation offset: " + 
                                  std::to_string(result.relocationOffset));
    }
    
    reportProgress("Preparing memory regions", 0.3);
    
    // Allocate missing regions
    if (allocateMissing && !comparison.missing.empty()) {
        int total = comparison.missing.size();
        int done = 0;
        
        for (const auto& region : comparison.missing) {
            // Skip non-essential regions
            if (region.isVdso()) {
                result.warnings.push_back("Skipping vdso/vvar region");
                continue;
            }
            
            // Calculate target address (apply relocation if needed)
            uint64_t targetAddr = region.startAddr;
            if (result.needsRelocation) {
                // For now, we can't relocate - just warn
                result.warnings.push_back(
                    "Cannot allocate region at " + std::to_string(region.startAddr) + 
                    " due to ASLR. Consider disabling ASLR.");
                result.failedRegions.push_back(region);
                continue;
            }
            
            // Try to allocate
            MemoryError err = createMapping(
                targetAddr,
                region.size(),
                region.readable,
                region.writable,
                region.executable,
                true  // MAP_FIXED
            );
            
            if (err == MemoryError::SUCCESS) {
                result.createdRegions.push_back(region);
            } else {
                result.warnings.push_back(
                    "Failed to allocate region at " + std::to_string(targetAddr) +
                    ": " + m_lastError);
                result.failedRegions.push_back(region);
            }
            
            done++;
            reportProgress("Allocating memory", 0.3 + 0.4 * (double(done) / total));
        }
    }
    
    // Remove extra regions if requested
    if (removeExtra && !comparison.extra.empty()) {
        for (const auto& region : comparison.extra) {
            // Be careful not to unmap essential regions
            if (region.isStack() || region.isVdso()) {
                result.warnings.push_back("Skipping removal of essential region: " + 
                                          region.pathname);
                continue;
            }
            
            MemoryError err = removeMapping(region.startAddr, region.size());
            if (err != MemoryError::SUCCESS) {
                result.warnings.push_back("Failed to remove extra region at " +
                                          std::to_string(region.startAddr));
            }
        }
    }
    
    reportProgress("Preparation complete", 1.0);
    
    // Set overall error if we had critical failures
    if (!result.failedRegions.empty()) {
        result.error = MemoryError::ALLOCATION_FAILED;
    }
    
    return result;
}

} // namespace real_process
} // namespace checkpoint
