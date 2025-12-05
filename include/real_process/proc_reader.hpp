#pragma once

#include "real_process/real_process_types.hpp"
#include <string>
#include <vector>
#include <optional>
#include <sys/types.h>

namespace checkpoint {
namespace real_process {

// ============================================================================
// ProcFS Reader - /proc Filesystem Okuyucu
// ============================================================================
class ProcFSReader {
public:
    ProcFSReader() = default;
    ~ProcFSReader() = default;
    
    // ========================================================================
    // Process Discovery
    // ========================================================================
    
    // Tüm process PID'lerini listele
    std::vector<pid_t> listAllPids();
    
    // Belirli kullanıcının process'lerini listele
    std::vector<pid_t> listUserPids(uid_t uid);
    
    // Process var mı kontrol et
    bool processExists(pid_t pid);
    
    // ========================================================================
    // Process Info - /proc/<pid>/stat, status, cmdline
    // ========================================================================
    
    // Tam process bilgisi al
    std::optional<RealProcessInfo> getProcessInfo(pid_t pid);
    
    // Sadece temel bilgiler
    std::string getProcessName(pid_t pid);
    std::string getProcessCmdline(pid_t pid);
    std::string getProcessExe(pid_t pid);
    std::string getProcessCwd(pid_t pid);
    LinuxProcessState getProcessState(pid_t pid);
    
    // ========================================================================
    // Memory Map - /proc/<pid>/maps
    // ========================================================================
    
    // Tüm memory bölgelerini oku
    std::vector<MemoryRegion> getMemoryMaps(pid_t pid);
    
    // Belirli tipte memory bölgelerini filtrele
    std::vector<MemoryRegion> getStackRegions(pid_t pid);
    std::vector<MemoryRegion> getHeapRegions(pid_t pid);
    std::vector<MemoryRegion> getWritableRegions(pid_t pid);
    std::vector<MemoryRegion> getAnonymousRegions(pid_t pid);
    
    // ========================================================================
    // File Descriptors - /proc/<pid>/fd, /proc/<pid>/fdinfo
    // ========================================================================
    
    std::vector<FileDescriptorInfo> getFileDescriptors(pid_t pid);
    std::optional<FileDescriptorInfo> getFileDescriptor(pid_t pid, int fd);
    
    // ========================================================================
    // Environment - /proc/<pid>/environ
    // ========================================================================
    
    std::vector<std::string> getEnvironment(pid_t pid);
    
    // ========================================================================
    // Signal Info - /proc/<pid>/status'tan
    // ========================================================================
    
    std::optional<SignalInfo> getSignalInfo(pid_t pid);
    
    // ========================================================================
    // Statistics
    // ========================================================================
    
    // Memory kullanımı
    uint64_t getVirtualMemorySize(pid_t pid);
    uint64_t getResidentMemorySize(pid_t pid);
    
    // CPU kullanımı
    double getCpuUsage(pid_t pid, double intervalSeconds = 1.0);
    
    // Thread sayısı
    int getThreadCount(pid_t pid);
    std::vector<pid_t> getThreadIds(pid_t pid);
    
private:
    // Helper methods
    std::string readFile(const std::string& path);
    std::vector<std::string> readFileLines(const std::string& path);
    std::string procPath(pid_t pid, const std::string& file);
    
    // Parsing helpers
    bool parseStatFile(pid_t pid, RealProcessInfo& info);
    bool parseStatusFile(pid_t pid, RealProcessInfo& info);
    bool parseMapsLine(const std::string& line, MemoryRegion& region);
    
    // Cache (opsiyonel, performans için)
    // std::map<pid_t, RealProcessInfo> m_infoCache;
};

// ============================================================================
// Helper Functions
// ============================================================================

// Readable memory size formatı
std::string formatMemorySize(uint64_t bytes);

// Process tree
struct ProcessTreeNode {
    pid_t pid;
    std::string name;
    std::vector<ProcessTreeNode> children;
};

ProcessTreeNode buildProcessTree(pid_t rootPid = 1);
void printProcessTree(const ProcessTreeNode& node, int depth = 0);

} // namespace real_process
} // namespace checkpoint
