#pragma once

#include "real_process/real_process_types.hpp"
#include <sys/types.h>
#include <vector>
#include <optional>
#include <string>

namespace checkpoint {
namespace real_process {

// ============================================================================
// ASLR (Address Space Layout Randomization) Handler
// ============================================================================

// ASLR Modes
enum class ASLRMode {
    DISABLED = 0,           // No randomization
    CONSERVATIVE = 1,       // Stack, mmap, VDSO randomized
    FULL = 2                // Above + heap, PIE binary base
};

std::string aslrModeToString(ASLRMode mode);

// ============================================================================
// Process Type Detection
// ============================================================================
struct ExecutableInfo {
    std::string path;
    bool isPIE;              // Position Independent Executable
    bool isStaticLinked;     // No dynamic loader
    uint64_t baseAddress;    // Original base address (0 for PIE)
    uint64_t entryPoint;     // Entry point address
    
    ExecutableInfo() : isPIE(false), isStaticLinked(false), 
                       baseAddress(0), entryPoint(0) {}
};

// ============================================================================
// ASLR Handler Class
// ============================================================================
class ASLRHandler {
public:
    ASLRHandler();
    ~ASLRHandler();
    
    // ========================================================================
    // System ASLR Configuration
    // ========================================================================
    
    // Get current system ASLR mode
    static ASLRMode getSystemASLRMode();
    
    // Set system ASLR mode (requires root)
    static bool setSystemASLRMode(ASLRMode mode);
    
    // Check if ASLR is enabled
    static bool isASLREnabled() { return getSystemASLRMode() != ASLRMode::DISABLED; }
    
    // Get ASLR config file path
    static std::string getASLRConfigPath() { return "/proc/sys/kernel/randomize_va_space"; }
    
    // ========================================================================
    // Process-Specific ASLR Control
    // ========================================================================
    
    // Disable ASLR for a specific command (uses personality syscall)
    // Returns the personality flags to use
    static unsigned long getNoASLRPersonality();
    
    // Execute a command with ASLR disabled
    // Equivalent to: setarch $(uname -m) -R ./program
    static pid_t execWithoutASLR(const std::string& program, 
                                  const std::vector<std::string>& args);
    
    // ========================================================================
    // Executable Analysis
    // ========================================================================
    
    // Analyze an executable file
    static ExecutableInfo analyzeExecutable(const std::string& path);
    
    // Check if a process is running as PIE
    static bool isProcessPIE(pid_t pid);
    
    // Get the base address where executable is loaded
    static uint64_t getExecutableBaseAddress(pid_t pid);
    
    // ========================================================================
    // Address Translation
    // ========================================================================
    
    // Calculate offset between checkpoint and current addresses
    struct AddressOffset {
        int64_t codeOffset;      // .text segment offset
        int64_t dataOffset;      // .data/.bss segment offset
        int64_t heapOffset;      // heap offset
        int64_t stackOffset;     // stack offset
        int64_t mmapOffset;      // mmap region offset
        
        bool hasOffset() const {
            return codeOffset != 0 || dataOffset != 0 || 
                   heapOffset != 0 || stackOffset != 0 || mmapOffset != 0;
        }
    };
    
    // Calculate address offsets between checkpoint and current process state
    AddressOffset calculateOffsets(
        const std::vector<MemoryRegion>& checkpointMap,
        const std::vector<MemoryRegion>& currentMap
    );
    
    // Translate a checkpoint address to current address
    uint64_t translateAddress(
        uint64_t checkpointAddr,
        const std::vector<MemoryRegion>& checkpointMap,
        const AddressOffset& offsets
    );
    
    // Translate register values (RIP, RSP, RBP, etc.)
    LinuxRegisters translateRegisters(
        const LinuxRegisters& checkpointRegs,
        const AddressOffset& offsets
    );
    
    // ========================================================================
    // Memory Content Relocation
    // ========================================================================
    
    // Scan and relocate pointers in memory dump
    // WARNING: This is heuristic and may corrupt data!
    struct RelocationResult {
        size_t pointersFound;
        size_t pointersRelocated;
        std::vector<uint64_t> relocatedAddresses;
    };
    
    RelocationResult relocatePointers(
        MemoryDump& dump,
        const std::vector<MemoryRegion>& checkpointMap,
        const AddressOffset& offsets,
        bool conservative = true  // Only relocate likely pointers
    );
    
    // ========================================================================
    // Utilities
    // ========================================================================
    
    // Get recommended approach for restore
    enum class RestoreStrategy {
        DIRECT,              // Direct restore, addresses match
        DISABLE_ASLR,        // Disable ASLR, restart process, then restore
        RELOCATE,            // Apply relocation to addresses
        UNSUPPORTED          // Cannot restore (complex ASLR differences)
    };
    
    RestoreStrategy recommendStrategy(
        const std::vector<MemoryRegion>& checkpointMap,
        const std::vector<MemoryRegion>& currentMap,
        bool canDisableASLR = false,
        bool canRestartProcess = false
    );
    
    std::string strategyToString(RestoreStrategy strategy);
    
    // Get last error message
    std::string getLastError() const { return m_lastError; }
    
private:
    std::string m_lastError;
    
    // Helper to find region containing address
    const MemoryRegion* findContainingRegion(
        uint64_t addr, 
        const std::vector<MemoryRegion>& regions
    );
    
    // Helper to identify region type
    enum class RegionType {
        CODE,
        DATA,
        HEAP,
        STACK,
        MMAP,
        VDSO,
        OTHER
    };
    
    RegionType identifyRegionType(const MemoryRegion& region);
};

// ============================================================================
// Convenience Functions
// ============================================================================

// Quick check if restore is possible without ASLR issues
bool canDirectRestore(
    const std::vector<MemoryRegion>& checkpointMap,
    const std::vector<MemoryRegion>& currentMap
);

// Get human-readable ASLR status report
std::string getASLRStatusReport();

} // namespace real_process
} // namespace checkpoint
