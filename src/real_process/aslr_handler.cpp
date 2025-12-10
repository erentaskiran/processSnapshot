#include "real_process/aslr_handler.hpp"
#include "real_process/proc_reader.hpp"
#include <sys/personality.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <elf.h>
#include <linux/limits.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>

namespace checkpoint {
namespace real_process {

// ============================================================================
// String Conversions
// ============================================================================

std::string aslrModeToString(ASLRMode mode) {
    switch (mode) {
        case ASLRMode::DISABLED:     return "Disabled";
        case ASLRMode::CONSERVATIVE: return "Conservative (stack/mmap/VDSO)";
        case ASLRMode::FULL:         return "Full (all + heap + PIE)";
        default:                     return "Unknown";
    }
}

// ============================================================================
// ASLRHandler Implementation
// ============================================================================

ASLRHandler::ASLRHandler() {
}

ASLRHandler::~ASLRHandler() {
}

// ============================================================================
// System ASLR Configuration
// ============================================================================

ASLRMode ASLRHandler::getSystemASLRMode() {
    std::ifstream file("/proc/sys/kernel/randomize_va_space");
    if (!file.is_open()) {
        return ASLRMode::FULL;  // Assume full if can't read
    }
    
    int value = 2;
    file >> value;
    
    switch (value) {
        case 0:  return ASLRMode::DISABLED;
        case 1:  return ASLRMode::CONSERVATIVE;
        default: return ASLRMode::FULL;
    }
}

bool ASLRHandler::setSystemASLRMode(ASLRMode mode) {
    std::ofstream file("/proc/sys/kernel/randomize_va_space");
    if (!file.is_open()) {
        return false;  // Need root
    }
    
    file << static_cast<int>(mode);
    return file.good();
}

// ============================================================================
// Process-Specific ASLR Control
// ============================================================================

unsigned long ASLRHandler::getNoASLRPersonality() {
    // ADDR_NO_RANDOMIZE disables ASLR for a process
    return ADDR_NO_RANDOMIZE;
}

pid_t ASLRHandler::execWithoutASLR(const std::string& program, 
                                    const std::vector<std::string>& args) {
    pid_t pid = fork();
    
    if (pid == -1) {
        return -1;  // Fork failed
    }
    
    if (pid == 0) {
        // Child process
        
        // Disable ASLR for this process
        if (personality(ADDR_NO_RANDOMIZE) == -1) {
            _exit(1);
        }
        
        // Prepare arguments
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(program.c_str()));
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        
        // Execute
        execv(program.c_str(), argv.data());
        _exit(1);  // If exec fails
    }
    
    // Parent process
    return pid;
}

// ============================================================================
// Executable Analysis
// ============================================================================

ExecutableInfo ASLRHandler::analyzeExecutable(const std::string& path) {
    ExecutableInfo info;
    info.path = path;
    
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return info;
    }
    
    // Read ELF header
    Elf64_Ehdr ehdr;
    if (read(fd, &ehdr, sizeof(ehdr)) != sizeof(ehdr)) {
        close(fd);
        return info;
    }
    
    // Verify ELF magic
    if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0) {
        close(fd);
        return info;
    }
    
    // Check if PIE
    info.isPIE = (ehdr.e_type == ET_DYN);
    
    // Get entry point
    info.entryPoint = ehdr.e_entry;
    
    // Check for interpreter (dynamic linker)
    info.isStaticLinked = true;  // Assume static until we find PT_INTERP
    
    // Read program headers
    lseek(fd, ehdr.e_phoff, SEEK_SET);
    for (int i = 0; i < ehdr.e_phnum; i++) {
        Elf64_Phdr phdr;
        if (read(fd, &phdr, sizeof(phdr)) != sizeof(phdr)) {
            break;
        }
        
        if (phdr.p_type == PT_INTERP) {
            info.isStaticLinked = false;
        }
        
        // Get base address from first PT_LOAD
        if (phdr.p_type == PT_LOAD && info.baseAddress == 0) {
            info.baseAddress = phdr.p_vaddr;
        }
    }
    
    close(fd);
    return info;
}

bool ASLRHandler::isProcessPIE(pid_t pid) {
    // Read executable path
    std::string exePath = "/proc/" + std::to_string(pid) + "/exe";
    char realPath[PATH_MAX];
    
    ssize_t len = readlink(exePath.c_str(), realPath, sizeof(realPath) - 1);
    if (len == -1) {
        return false;
    }
    realPath[len] = '\0';
    
    // Analyze the executable
    auto info = analyzeExecutable(realPath);
    return info.isPIE;
}

uint64_t ASLRHandler::getExecutableBaseAddress(pid_t pid) {
    ProcFSReader reader;
    auto maps = reader.getMemoryMaps(pid);
    
    // Get executable path
    std::string exePath = "/proc/" + std::to_string(pid) + "/exe";
    char realPath[PATH_MAX];
    ssize_t len = readlink(exePath.c_str(), realPath, sizeof(realPath) - 1);
    if (len == -1) {
        return 0;
    }
    realPath[len] = '\0';
    
    // Find first mapping of the executable
    for (const auto& region : maps) {
        if (region.pathname == realPath && region.executable) {
            return region.startAddr;
        }
    }
    
    return 0;
}

// ============================================================================
// Address Translation
// ============================================================================

ASLRHandler::AddressOffset ASLRHandler::calculateOffsets(
    const std::vector<MemoryRegion>& checkpointMap,
    const std::vector<MemoryRegion>& currentMap) {
    
    AddressOffset offsets = {0, 0, 0, 0, 0};
    
    // Find known regions in both maps
    const MemoryRegion* cpHeap = nullptr;
    const MemoryRegion* cpStack = nullptr;
    const MemoryRegion* cpCode = nullptr;
    
    const MemoryRegion* curHeap = nullptr;
    const MemoryRegion* curStack = nullptr;
    const MemoryRegion* curCode = nullptr;
    
    for (const auto& region : checkpointMap) {
        if (region.isHeap()) cpHeap = &region;
        if (region.isStack()) cpStack = &region;
        if (region.executable && !region.isVdso() && !region.pathname.empty() && 
            region.pathname[0] == '/') {
            cpCode = &region;
        }
    }
    
    for (const auto& region : currentMap) {
        if (region.isHeap()) curHeap = &region;
        if (region.isStack()) curStack = &region;
        if (region.executable && !region.isVdso() && !region.pathname.empty() && 
            region.pathname[0] == '/') {
            curCode = &region;
        }
    }
    
    // Calculate offsets
    if (cpHeap && curHeap) {
        offsets.heapOffset = static_cast<int64_t>(curHeap->startAddr) - 
                            static_cast<int64_t>(cpHeap->startAddr);
    }
    
    if (cpStack && curStack) {
        offsets.stackOffset = static_cast<int64_t>(curStack->startAddr) - 
                             static_cast<int64_t>(cpStack->startAddr);
    }
    
    if (cpCode && curCode) {
        offsets.codeOffset = static_cast<int64_t>(curCode->startAddr) - 
                            static_cast<int64_t>(cpCode->startAddr);
        // Data is usually at fixed offset from code
        offsets.dataOffset = offsets.codeOffset;
    }
    
    // mmap regions typically follow heap behavior
    offsets.mmapOffset = offsets.heapOffset;
    
    return offsets;
}

const MemoryRegion* ASLRHandler::findContainingRegion(
    uint64_t addr, 
    const std::vector<MemoryRegion>& regions) {
    
    for (const auto& region : regions) {
        if (addr >= region.startAddr && addr < region.endAddr) {
            return &region;
        }
    }
    return nullptr;
}

ASLRHandler::RegionType ASLRHandler::identifyRegionType(const MemoryRegion& region) {
    if (region.isStack()) return RegionType::STACK;
    if (region.isHeap()) return RegionType::HEAP;
    if (region.isVdso()) return RegionType::VDSO;
    
    if (!region.pathname.empty() && region.pathname[0] == '/') {
        if (region.executable) return RegionType::CODE;
        return RegionType::DATA;
    }
    
    if (region.isAnonymous()) {
        return RegionType::MMAP;
    }
    
    return RegionType::OTHER;
}

uint64_t ASLRHandler::translateAddress(
    uint64_t checkpointAddr,
    const std::vector<MemoryRegion>& checkpointMap,
    const AddressOffset& offsets) {
    
    // Find which region the address belongs to
    const MemoryRegion* region = findContainingRegion(checkpointAddr, checkpointMap);
    if (!region) {
        return checkpointAddr;  // Can't translate, return as-is
    }
    
    // Apply appropriate offset based on region type
    RegionType type = identifyRegionType(*region);
    int64_t offset = 0;
    
    switch (type) {
        case RegionType::CODE:
            offset = offsets.codeOffset;
            break;
        case RegionType::DATA:
            offset = offsets.dataOffset;
            break;
        case RegionType::HEAP:
            offset = offsets.heapOffset;
            break;
        case RegionType::STACK:
            offset = offsets.stackOffset;
            break;
        case RegionType::MMAP:
            offset = offsets.mmapOffset;
            break;
        default:
            offset = 0;
            break;
    }
    
    return checkpointAddr + offset;
}

LinuxRegisters ASLRHandler::translateRegisters(
    const LinuxRegisters& checkpointRegs,
    const AddressOffset& offsets) {
    
    LinuxRegisters translated = checkpointRegs;
    
    // Translate instruction pointer (RIP) - usually in code segment
    translated.rip = checkpointRegs.rip + offsets.codeOffset;
    
    // Translate stack pointer (RSP) and base pointer (RBP) - stack segment
    translated.rsp = checkpointRegs.rsp + offsets.stackOffset;
    translated.rbp = checkpointRegs.rbp + offsets.stackOffset;
    
    // Note: Other registers that might contain pointers (RDI, RSI, etc.)
    // cannot be reliably translated without knowing their context
    
    return translated;
}

// ============================================================================
// Memory Content Relocation
// ============================================================================

ASLRHandler::RelocationResult ASLRHandler::relocatePointers(
    MemoryDump& dump,
    const std::vector<MemoryRegion>& checkpointMap,
    const AddressOffset& offsets,
    bool conservative) {
    
    RelocationResult result = {0, 0, {}};
    
    if (!offsets.hasOffset()) {
        return result;  // No relocation needed
    }
    
    // Calculate valid address range from checkpoint
    uint64_t minAddr = UINT64_MAX;
    uint64_t maxAddr = 0;
    
    for (const auto& region : checkpointMap) {
        if (region.startAddr < minAddr) minAddr = region.startAddr;
        if (region.endAddr > maxAddr) maxAddr = region.endAddr;
    }
    
    // Scan memory for potential pointers
    // Pointers are 8 bytes on x86_64 and must be 8-byte aligned
    uint64_t* data = reinterpret_cast<uint64_t*>(dump.data.data());
    size_t numWords = dump.data.size() / sizeof(uint64_t);
    
    for (size_t i = 0; i < numWords; i++) {
        uint64_t value = data[i];
        
        // Check if value looks like a pointer (within valid range)
        if (value >= minAddr && value < maxAddr) {
            result.pointersFound++;
            
            // Find containing region to determine appropriate offset
            const MemoryRegion* region = findContainingRegion(value, checkpointMap);
            if (!region) {
                continue;
            }
            
            // In conservative mode, only relocate pointers to code/stack/heap
            if (conservative) {
                RegionType type = identifyRegionType(*region);
                if (type != RegionType::CODE && type != RegionType::STACK && 
                    type != RegionType::HEAP) {
                    continue;
                }
            }
            
            // Translate the pointer
            uint64_t translated = translateAddress(value, checkpointMap, offsets);
            
            if (translated != value) {
                data[i] = translated;
                result.pointersRelocated++;
                result.relocatedAddresses.push_back(dump.region.startAddr + i * sizeof(uint64_t));
            }
        }
    }
    
    return result;
}

// ============================================================================
// Strategy Recommendation
// ============================================================================

ASLRHandler::RestoreStrategy ASLRHandler::recommendStrategy(
    const std::vector<MemoryRegion>& checkpointMap,
    const std::vector<MemoryRegion>& currentMap,
    bool canDisableASLR,
    bool canRestartProcess) {
    
    // Calculate offsets
    auto offsets = calculateOffsets(checkpointMap, currentMap);
    
    // If no offsets, direct restore is possible
    if (!offsets.hasOffset()) {
        return RestoreStrategy::DIRECT;
    }
    
    // If only code offset and it's consistent, relocation might work
    bool onlyCodeOffset = (offsets.codeOffset != 0 &&
                          offsets.heapOffset == 0 &&
                          offsets.stackOffset == 0);
    
    // If we can disable ASLR and restart, that's the safest option
    if (canDisableASLR && canRestartProcess) {
        return RestoreStrategy::DISABLE_ASLR;
    }
    
    // If offsets are simple and consistent, try relocation
    if (onlyCodeOffset || 
        (offsets.codeOffset == offsets.dataOffset && 
         offsets.heapOffset == offsets.stackOffset)) {
        return RestoreStrategy::RELOCATE;
    }
    
    // Complex offset patterns are hard to handle
    return RestoreStrategy::UNSUPPORTED;
}

std::string ASLRHandler::strategyToString(RestoreStrategy strategy) {
    switch (strategy) {
        case RestoreStrategy::DIRECT:
            return "Direct restore (addresses match)";
        case RestoreStrategy::DISABLE_ASLR:
            return "Disable ASLR and restart process";
        case RestoreStrategy::RELOCATE:
            return "Apply address relocation";
        case RestoreStrategy::UNSUPPORTED:
            return "Unsupported (complex ASLR differences)";
        default:
            return "Unknown";
    }
}

// ============================================================================
// Convenience Functions
// ============================================================================

bool canDirectRestore(
    const std::vector<MemoryRegion>& checkpointMap,
    const std::vector<MemoryRegion>& currentMap) {
    
    ASLRHandler handler;
    auto offsets = handler.calculateOffsets(checkpointMap, currentMap);
    return !offsets.hasOffset();
}

std::string getASLRStatusReport() {
    std::ostringstream oss;
    
    ASLRMode mode = ASLRHandler::getSystemASLRMode();
    
    oss << "=== ASLR Status Report ===\n";
    oss << "System ASLR Mode: " << aslrModeToString(mode) << "\n";
    oss << "ASLR Config File: " << ASLRHandler::getASLRConfigPath() << "\n";
    oss << "ASLR Enabled: " << (ASLRHandler::isASLREnabled() ? "Yes" : "No") << "\n";
    oss << "\n";
    
    if (mode != ASLRMode::DISABLED) {
        oss << "To disable ASLR for restore:\n";
        oss << "  Option 1: echo 0 | sudo tee /proc/sys/kernel/randomize_va_space\n";
        oss << "  Option 2: setarch $(uname -m) -R ./program\n";
        oss << "  Option 3: Use personality(ADDR_NO_RANDOMIZE) before exec\n";
    }
    
    return oss.str();
}

} // namespace real_process
} // namespace checkpoint
