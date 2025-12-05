#include "real_process/proc_reader.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <functional>
#include <dirent.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>
#include <sys/stat.h>
#include <iomanip>

namespace checkpoint {
namespace real_process {

// ============================================================================
// Helper Functions
// ============================================================================

std::string ProcFSReader::procPath(pid_t pid, const std::string& file) {
    return "/proc/" + std::to_string(pid) + "/" + file;
}

std::string ProcFSReader::readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::vector<std::string> ProcFSReader::readFileLines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream file(path);
    if (!file.is_open()) {
        return lines;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    return lines;
}

// ============================================================================
// Process Discovery
// ============================================================================

std::vector<pid_t> ProcFSReader::listAllPids() {
    std::vector<pid_t> pids;
    
    DIR* dir = opendir("/proc");
    if (!dir) {
        return pids;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        // Check if entry name is numeric (PID)
        bool isNumeric = true;
        for (const char* p = entry->d_name; *p; ++p) {
            if (*p < '0' || *p > '9') {
                isNumeric = false;
                break;
            }
        }
        
        if (isNumeric && entry->d_name[0] != '\0') {
            pids.push_back(std::stoi(entry->d_name));
        }
    }
    
    closedir(dir);
    std::sort(pids.begin(), pids.end());
    return pids;
}

std::vector<pid_t> ProcFSReader::listUserPids(uid_t uid) {
    std::vector<pid_t> pids;
    auto allPids = listAllPids();
    
    for (pid_t pid : allPids) {
        struct stat st;
        std::string path = "/proc/" + std::to_string(pid);
        if (stat(path.c_str(), &st) == 0 && st.st_uid == uid) {
            pids.push_back(pid);
        }
    }
    
    return pids;
}

bool ProcFSReader::processExists(pid_t pid) {
    std::string path = "/proc/" + std::to_string(pid);
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

// ============================================================================
// Process Info Parsing
// ============================================================================

bool ProcFSReader::parseStatFile(pid_t pid, RealProcessInfo& info) {
    std::string content = readFile(procPath(pid, "stat"));
    if (content.empty()) {
        return false;
    }
    
    // Format: pid (comm) state ppid pgid sid ...
    // comm can contain spaces and parentheses, so we need to find the last ')'
    size_t commStart = content.find('(');
    size_t commEnd = content.rfind(')');
    
    if (commStart == std::string::npos || commEnd == std::string::npos) {
        return false;
    }
    
    info.name = content.substr(commStart + 1, commEnd - commStart - 1);
    
    // Parse the rest after ')'
    std::istringstream iss(content.substr(commEnd + 2));
    
    char state;
    iss >> state;
    info.state = charToLinuxState(state);
    
    iss >> info.ppid >> info.pgid >> info.sid;
    
    // Skip tty_nr, tpgid, flags, minflt, cminflt, majflt, cmajflt
    int dummy;
    for (int i = 0; i < 7; ++i) iss >> dummy;
    
    // utime, stime, cutime, cstime
    iss >> info.utime >> info.stime >> info.cutime >> info.cstime;
    
    // priority, nice
    iss >> info.priority >> info.nice;
    
    // num_threads
    iss >> info.numThreads;
    
    // Skip itrealvalue
    iss >> dummy;
    
    // starttime
    iss >> info.startTime;
    
    // vsize (virtual memory size in bytes)
    iss >> info.vmSize;
    
    // rss (pages) - convert to bytes
    long rss;
    iss >> rss;
    info.vmRss = rss * sysconf(_SC_PAGESIZE);
    
    return true;
}

bool ProcFSReader::parseStatusFile(pid_t pid, RealProcessInfo& info) {
    auto lines = readFileLines(procPath(pid, "status"));
    
    for (const auto& line : lines) {
        size_t colonPos = line.find(':');
        if (colonPos == std::string::npos) continue;
        
        std::string key = line.substr(0, colonPos);
        std::string value = line.substr(colonPos + 1);
        
        // Trim whitespace
        size_t start = value.find_first_not_of(" \t");
        if (start != std::string::npos) {
            value = value.substr(start);
        }
        
        if (key == "Uid") {
            std::istringstream iss(value);
            iss >> info.uid >> info.euid >> info.suid;
        } else if (key == "Gid") {
            std::istringstream iss(value);
            iss >> info.gid >> info.egid >> info.sgid;
        } else if (key == "VmPeak") {
            info.vmPeak = std::stoull(value) * 1024;  // kB to bytes
        } else if (key == "VmSize") {
            info.vmSize = std::stoull(value) * 1024;
        } else if (key == "VmRSS") {
            info.vmRss = std::stoull(value) * 1024;
        } else if (key == "VmData") {
            info.vmData = std::stoull(value) * 1024;
        } else if (key == "VmStk") {
            info.vmStack = std::stoull(value) * 1024;
        } else if (key == "VmExe") {
            info.vmExe = std::stoull(value) * 1024;
        } else if (key == "Threads") {
            info.numThreads = std::stoi(value);
        }
    }
    
    return true;
}

bool ProcFSReader::parseMapsLine(const std::string& line, MemoryRegion& region) {
    // Format: address perms offset dev inode pathname
    // Example: 00400000-00452000 r-xp 00000000 08:01 1234567 /usr/bin/program
    
    std::istringstream iss(line);
    std::string addrRange, perms, offset, dev;
    uint64_t inode;
    
    iss >> addrRange >> perms >> offset >> dev >> inode;
    
    // Parse address range
    size_t dashPos = addrRange.find('-');
    if (dashPos == std::string::npos) return false;
    
    region.startAddr = std::stoull(addrRange.substr(0, dashPos), nullptr, 16);
    region.endAddr = std::stoull(addrRange.substr(dashPos + 1), nullptr, 16);
    
    // Parse permissions
    region.readable = (perms.size() > 0 && perms[0] == 'r');
    region.writable = (perms.size() > 1 && perms[1] == 'w');
    region.executable = (perms.size() > 2 && perms[2] == 'x');
    region.isPrivate = (perms.size() > 3 && perms[3] == 'p');
    
    region.offset = std::stoull(offset, nullptr, 16);
    region.device = dev;
    region.inode = inode;
    
    // Pathname (rest of line, may have leading spaces)
    std::getline(iss, region.pathname);
    size_t start = region.pathname.find_first_not_of(" \t");
    if (start != std::string::npos) {
        region.pathname = region.pathname.substr(start);
    } else {
        region.pathname.clear();
    }
    
    return true;
}

// ============================================================================
// Process Info
// ============================================================================

std::optional<RealProcessInfo> ProcFSReader::getProcessInfo(pid_t pid) {
    if (!processExists(pid)) {
        return std::nullopt;
    }
    
    RealProcessInfo info;
    info.pid = pid;
    
    if (!parseStatFile(pid, info)) {
        return std::nullopt;
    }
    
    parseStatusFile(pid, info);
    
    // Additional info
    info.cmdline = getProcessCmdline(pid);
    info.exe = getProcessExe(pid);
    info.cwd = getProcessCwd(pid);
    
    return info;
}

std::string ProcFSReader::getProcessName(pid_t pid) {
    std::string comm = readFile(procPath(pid, "comm"));
    // Remove trailing newline
    if (!comm.empty() && comm.back() == '\n') {
        comm.pop_back();
    }
    return comm;
}

std::string ProcFSReader::getProcessCmdline(pid_t pid) {
    std::string cmdline = readFile(procPath(pid, "cmdline"));
    // Replace null bytes with spaces
    std::replace(cmdline.begin(), cmdline.end(), '\0', ' ');
    // Remove trailing space
    if (!cmdline.empty() && cmdline.back() == ' ') {
        cmdline.pop_back();
    }
    return cmdline;
}

std::string ProcFSReader::getProcessExe(pid_t pid) {
    char buf[PATH_MAX];
    std::string path = procPath(pid, "exe");
    ssize_t len = readlink(path.c_str(), buf, sizeof(buf) - 1);
    if (len == -1) {
        return "";
    }
    buf[len] = '\0';
    return std::string(buf);
}

std::string ProcFSReader::getProcessCwd(pid_t pid) {
    char buf[PATH_MAX];
    std::string path = procPath(pid, "cwd");
    ssize_t len = readlink(path.c_str(), buf, sizeof(buf) - 1);
    if (len == -1) {
        return "";
    }
    buf[len] = '\0';
    return std::string(buf);
}

LinuxProcessState ProcFSReader::getProcessState(pid_t pid) {
    std::string stat = readFile(procPath(pid, "stat"));
    if (stat.empty()) {
        return LinuxProcessState::UNKNOWN;
    }
    
    size_t pos = stat.rfind(')');
    if (pos == std::string::npos || pos + 2 >= stat.size()) {
        return LinuxProcessState::UNKNOWN;
    }
    
    return charToLinuxState(stat[pos + 2]);
}

// ============================================================================
// Memory Maps
// ============================================================================

std::vector<MemoryRegion> ProcFSReader::getMemoryMaps(pid_t pid) {
    std::vector<MemoryRegion> regions;
    
    auto lines = readFileLines(procPath(pid, "maps"));
    for (const auto& line : lines) {
        MemoryRegion region;
        if (parseMapsLine(line, region)) {
            regions.push_back(region);
        }
    }
    
    return regions;
}

std::vector<MemoryRegion> ProcFSReader::getStackRegions(pid_t pid) {
    std::vector<MemoryRegion> result;
    auto maps = getMemoryMaps(pid);
    for (const auto& region : maps) {
        if (region.isStack()) {
            result.push_back(region);
        }
    }
    return result;
}

std::vector<MemoryRegion> ProcFSReader::getHeapRegions(pid_t pid) {
    std::vector<MemoryRegion> result;
    auto maps = getMemoryMaps(pid);
    for (const auto& region : maps) {
        if (region.isHeap()) {
            result.push_back(region);
        }
    }
    return result;
}

std::vector<MemoryRegion> ProcFSReader::getWritableRegions(pid_t pid) {
    std::vector<MemoryRegion> result;
    auto maps = getMemoryMaps(pid);
    for (const auto& region : maps) {
        if (region.writable) {
            result.push_back(region);
        }
    }
    return result;
}

std::vector<MemoryRegion> ProcFSReader::getAnonymousRegions(pid_t pid) {
    std::vector<MemoryRegion> result;
    auto maps = getMemoryMaps(pid);
    for (const auto& region : maps) {
        if (region.isAnonymous() && !region.isVdso()) {
            result.push_back(region);
        }
    }
    return result;
}

// ============================================================================
// File Descriptors
// ============================================================================

std::vector<FileDescriptorInfo> ProcFSReader::getFileDescriptors(pid_t pid) {
    std::vector<FileDescriptorInfo> fds;
    
    std::string fdPath = procPath(pid, "fd");
    DIR* dir = opendir(fdPath.c_str());
    if (!dir) {
        return fds;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;
        
        FileDescriptorInfo info;
        info.fd = std::stoi(entry->d_name);
        
        // Read symlink to get path
        char buf[PATH_MAX];
        std::string linkPath = fdPath + "/" + entry->d_name;
        ssize_t len = readlink(linkPath.c_str(), buf, sizeof(buf) - 1);
        if (len != -1) {
            buf[len] = '\0';
            info.path = buf;
        }
        
        // Read fdinfo for position and flags
        std::string fdinfoPath = procPath(pid, "fdinfo/" + std::string(entry->d_name));
        auto lines = readFileLines(fdinfoPath);
        for (const auto& line : lines) {
            if (line.substr(0, 4) == "pos:") {
                info.pos = std::stoll(line.substr(5));
            } else if (line.substr(0, 6) == "flags:") {
                info.flags = std::stoi(line.substr(7), nullptr, 8);
            }
        }
        
        fds.push_back(info);
    }
    
    closedir(dir);
    std::sort(fds.begin(), fds.end(), [](const auto& a, const auto& b) {
        return a.fd < b.fd;
    });
    
    return fds;
}

std::optional<FileDescriptorInfo> ProcFSReader::getFileDescriptor(pid_t pid, int fd) {
    auto fds = getFileDescriptors(pid);
    for (const auto& info : fds) {
        if (info.fd == fd) {
            return info;
        }
    }
    return std::nullopt;
}

// ============================================================================
// Environment
// ============================================================================

std::vector<std::string> ProcFSReader::getEnvironment(pid_t pid) {
    std::vector<std::string> env;
    
    std::string content = readFile(procPath(pid, "environ"));
    
    // Split by null bytes
    std::string current;
    for (char c : content) {
        if (c == '\0') {
            if (!current.empty()) {
                env.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    
    return env;
}

// ============================================================================
// Signal Info
// ============================================================================

std::optional<SignalInfo> ProcFSReader::getSignalInfo(pid_t pid) {
    SignalInfo info{};
    
    auto lines = readFileLines(procPath(pid, "status"));
    for (const auto& line : lines) {
        if (line.substr(0, 7) == "SigPnd:") {
            info.pending = std::stoull(line.substr(8), nullptr, 16);
        } else if (line.substr(0, 7) == "SigBlk:") {
            info.blocked = std::stoull(line.substr(8), nullptr, 16);
        } else if (line.substr(0, 7) == "SigIgn:") {
            info.ignored = std::stoull(line.substr(8), nullptr, 16);
        } else if (line.substr(0, 7) == "SigCgt:") {
            info.caught = std::stoull(line.substr(8), nullptr, 16);
        }
    }
    
    return info;
}

// ============================================================================
// Statistics
// ============================================================================

uint64_t ProcFSReader::getVirtualMemorySize(pid_t pid) {
    auto info = getProcessInfo(pid);
    return info ? info->vmSize : 0;
}

uint64_t ProcFSReader::getResidentMemorySize(pid_t pid) {
    auto info = getProcessInfo(pid);
    return info ? info->vmRss : 0;
}

int ProcFSReader::getThreadCount(pid_t pid) {
    auto info = getProcessInfo(pid);
    return info ? info->numThreads : 0;
}

std::vector<pid_t> ProcFSReader::getThreadIds(pid_t pid) {
    std::vector<pid_t> tids;
    
    std::string taskPath = procPath(pid, "task");
    DIR* dir = opendir(taskPath.c_str());
    if (!dir) {
        return tids;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;
        tids.push_back(std::stoi(entry->d_name));
    }
    
    closedir(dir);
    std::sort(tids.begin(), tids.end());
    return tids;
}

// ============================================================================
// Helper Functions
// ============================================================================

std::string formatMemorySize(uint64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unitIndex = 0;
    double size = static_cast<double>(bytes);
    
    while (size >= 1024 && unitIndex < 4) {
        size /= 1024;
        unitIndex++;
    }
    
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << size << " " << units[unitIndex];
    return oss.str();
}

ProcessTreeNode buildProcessTree(pid_t rootPid) {
    ProcFSReader reader;
    ProcessTreeNode root;
    root.pid = rootPid;
    root.name = reader.getProcessName(rootPid);
    
    // Build a map of ppid -> children
    std::map<pid_t, std::vector<pid_t>> children;
    auto allPids = reader.listAllPids();
    
    for (pid_t pid : allPids) {
        auto info = reader.getProcessInfo(pid);
        if (info) {
            children[info->ppid].push_back(pid);
        }
    }
    
    // Recursive lambda to build tree
    std::function<void(ProcessTreeNode&)> buildChildren = [&](ProcessTreeNode& node) {
        auto it = children.find(node.pid);
        if (it != children.end()) {
            for (pid_t childPid : it->second) {
                ProcessTreeNode child;
                child.pid = childPid;
                child.name = reader.getProcessName(childPid);
                buildChildren(child);
                node.children.push_back(child);
            }
        }
    };
    
    buildChildren(root);
    return root;
}

void printProcessTree(const ProcessTreeNode& node, int depth) {
    std::string indent(depth * 2, ' ');
    std::cout << indent << "[" << node.pid << "] " << node.name << "\n";
    
    for (const auto& child : node.children) {
        printProcessTree(child, depth + 1);
    }
}

} // namespace real_process
} // namespace checkpoint
