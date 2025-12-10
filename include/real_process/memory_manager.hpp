#pragma once

#include "real_process/real_process_types.hpp"
#include <sys/types.h>
#include <vector>
#include <optional>
#include <functional>

namespace checkpoint {
namespace real_process {

// ============================================================================
// Memory Manager Error Codes
// ============================================================================
enum class MemoryError {
    SUCCESS = 0,
    PERMISSION_DENIED,
    INVALID_ADDRESS,
    REGION_EXISTS,
    REGION_NOT_FOUND,
    ALLOCATION_FAILED,
    MMAP_FAILED,
    MUNMAP_FAILED,
    MPROTECT_FAILED,
    PROCESS_NOT_ATTACHED,
    ASLR_MISMATCH,
    UNKNOWN_ERROR
};

std::string memoryErrorToString(MemoryError err);

// ============================================================================
// Address Space Comparison Result
// ============================================================================
struct AddressSpaceComparison {
    std::vector<MemoryRegion> matching;       // Same address and size
    std::vector<MemoryRegion> missing;        // In checkpoint but not in current
    std::vector<MemoryRegion> extra;          // In current but not in checkpoint
    std::vector<std::pair<MemoryRegion, MemoryRegion>> moved;  // Same content, different address
    
    bool hasChanges() const {
        return !missing.empty() || !extra.empty() || !moved.empty();
    }
    
    // ASLR detection
    bool aslrDetected;
    int64_t stackOffset;     // Current stack base - Checkpoint stack base
    int64_t heapOffset;      // Current heap base - Checkpoint heap base
    int64_t baseOffset;      // General offset for PIE executables
};

// ============================================================================
// ASLR Configuration
// ============================================================================
struct ASLRConfig {
    bool enabled;                    // Is ASLR enabled on system?
    int level;                       // 0=disabled, 1=conservative, 2=full
    bool processIsPIE;               // Is target executable PIE?
    
    static ASLRConfig detect();
    static bool disable();           // Requires root, affects entire system
    static bool enable();
};

// ============================================================================
// Memory Manager - Handles memory mapping operations for restore
// ============================================================================
class MemoryManager {
public:
    MemoryManager();
    ~MemoryManager();
    
    // ========================================================================
    // Process Binding
    // ========================================================================
    
    // Bind to a process (via ptrace)
    MemoryError bindProcess(pid_t pid);
    void unbindProcess();
    bool isBound() const { return m_pid > 0; }
    pid_t getBoundPid() const { return m_pid; }
    
    // ========================================================================
    // Address Space Analysis
    // ========================================================================
    
    // Compare checkpoint memory map with current process memory map
    AddressSpaceComparison compareAddressSpace(
        const std::vector<MemoryRegion>& checkpointMap,
        const std::vector<MemoryRegion>& currentMap
    );
    
    // Get current memory map from /proc/<pid>/maps
    std::vector<MemoryRegion> getCurrentMemoryMap();
    
    // Check if a region exists at the given address
    bool regionExists(uint64_t addr, size_t size);
    
    // Find region containing address
    std::optional<MemoryRegion> findRegion(uint64_t addr);
    
    // ========================================================================
    // Memory Mapping Operations (via syscall injection)
    // ========================================================================
    
    // Create a new anonymous memory mapping in target process
    // Uses syscall injection (mmap via ptrace)
    MemoryError createMapping(
        uint64_t addr,          // Desired address (use 0 for any)
        size_t size,
        bool readable,
        bool writable, 
        bool executable,
        bool fixed = false      // MAP_FIXED - force exact address
    );
    
    // Remove a memory mapping
    MemoryError removeMapping(uint64_t addr, size_t size);
    
    // Change memory protection
    MemoryError changeProtection(
        uint64_t addr,
        size_t size,
        bool readable,
        bool writable,
        bool executable
    );
    
    // ========================================================================
    // Batch Operations for Restore
    // ========================================================================
    
    // Prepare address space for restore
    // - Creates missing regions
    // - Adjusts permissions as needed
    // - Handles ASLR offsets
    struct PrepareResult {
        MemoryError error;
        std::vector<std::string> warnings;
        std::vector<MemoryRegion> createdRegions;
        std::vector<MemoryRegion> failedRegions;
        bool needsRelocation;
        int64_t relocationOffset;
    };
    
    PrepareResult prepareForRestore(
        const std::vector<MemoryRegion>& checkpointMap,
        bool allocateMissing = true,
        bool removeExtra = false
    );
    
    // ========================================================================
    // ASLR Handling
    // ========================================================================
    
    // Detect ASLR offset by comparing known regions (stack, heap)
    std::optional<int64_t> detectASLROffset(
        const std::vector<MemoryRegion>& checkpointMap,
        const std::vector<MemoryRegion>& currentMap
    );
    
    // Check if system has ASLR enabled
    static bool isASLREnabled();
    
    // Get ASLR level (0=disabled, 1=conservative, 2=full)
    static int getASLRLevel();
    
    // ========================================================================
    // Syscall Injection
    // ========================================================================
    
    // Execute a syscall in the target process
    // Saves and restores registers
    int64_t injectSyscall(
        uint64_t syscallNum,
        uint64_t arg1 = 0,
        uint64_t arg2 = 0,
        uint64_t arg3 = 0,
        uint64_t arg4 = 0,
        uint64_t arg5 = 0,
        uint64_t arg6 = 0
    );
    
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
    
    // Ptrace helpers
    bool saveRegisters(LinuxRegisters& saved);
    bool restoreRegisters(const LinuxRegisters& saved);
    
    void reportProgress(const std::string& stage, double progress);
    
    // Memory map parsing
    std::vector<MemoryRegion> parseMemoryMaps(const std::string& content);
};

// ============================================================================
// Helper Functions
// ============================================================================

// Convert protection flags to mmap prot value
int toProtFlags(bool readable, bool writable, bool executable);

// Convert mmap prot value to protection flags
void fromProtFlags(int prot, bool& readable, bool& writable, bool& executable);

// Check if two regions overlap
bool regionsOverlap(const MemoryRegion& a, const MemoryRegion& b);

// Check if two regions are adjacent
bool regionsAdjacent(const MemoryRegion& a, const MemoryRegion& b);

} // namespace real_process
} // namespace checkpoint
