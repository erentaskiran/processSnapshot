#include "real_process/real_process_types.hpp"
#include <sstream>
#include <cstring>
#include <chrono>

namespace checkpoint {
namespace real_process {

// ============================================================================
// LinuxProcessState helpers
// ============================================================================

std::string linuxStateToString(LinuxProcessState state) {
    switch (state) {
        case LinuxProcessState::RUNNING:    return "Running";
        case LinuxProcessState::SLEEPING:   return "Sleeping";
        case LinuxProcessState::DISK_SLEEP: return "Disk Sleep";
        case LinuxProcessState::STOPPED:    return "Stopped";
        case LinuxProcessState::ZOMBIE:     return "Zombie";
        case LinuxProcessState::DEAD:       return "Dead";
        default:                            return "Unknown";
    }
}

LinuxProcessState charToLinuxState(char c) {
    switch (c) {
        case 'R': return LinuxProcessState::RUNNING;
        case 'S': return LinuxProcessState::SLEEPING;
        case 'D': return LinuxProcessState::DISK_SLEEP;
        case 'T': 
        case 't': return LinuxProcessState::STOPPED;
        case 'Z': return LinuxProcessState::ZOMBIE;
        case 'X':
        case 'x': return LinuxProcessState::DEAD;
        default:  return LinuxProcessState::UNKNOWN;
    }
}

// ============================================================================
// RealProcessCheckpoint serialization
// ============================================================================

std::vector<uint8_t> RealProcessCheckpoint::serialize() const {
    std::vector<uint8_t> data;
    
    // Magic number: "RCHK" (Real Checkpoint)
    data.push_back('R');
    data.push_back('C');
    data.push_back('H');
    data.push_back('K');
    
    // Version
    uint32_t version = 1;
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&version),
                reinterpret_cast<uint8_t*>(&version) + sizeof(version));
    
    // Checkpoint ID
    data.insert(data.end(), reinterpret_cast<const uint8_t*>(&checkpointId),
                reinterpret_cast<const uint8_t*>(&checkpointId) + sizeof(checkpointId));
    
    // Timestamp
    data.insert(data.end(), reinterpret_cast<const uint8_t*>(&timestamp),
                reinterpret_cast<const uint8_t*>(&timestamp) + sizeof(timestamp));
    
    // Name (length + data)
    uint32_t nameLen = static_cast<uint32_t>(name.size());
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&nameLen),
                reinterpret_cast<uint8_t*>(&nameLen) + sizeof(nameLen));
    data.insert(data.end(), name.begin(), name.end());
    
    // Process Info
    data.insert(data.end(), reinterpret_cast<const uint8_t*>(&info.pid),
                reinterpret_cast<const uint8_t*>(&info.pid) + sizeof(info.pid));
    data.insert(data.end(), reinterpret_cast<const uint8_t*>(&info.ppid),
                reinterpret_cast<const uint8_t*>(&info.ppid) + sizeof(info.ppid));
    
    // Process name
    uint32_t procNameLen = static_cast<uint32_t>(info.name.size());
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&procNameLen),
                reinterpret_cast<uint8_t*>(&procNameLen) + sizeof(procNameLen));
    data.insert(data.end(), info.name.begin(), info.name.end());
    
    // Cmdline
    uint32_t cmdlineLen = static_cast<uint32_t>(info.cmdline.size());
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&cmdlineLen),
                reinterpret_cast<uint8_t*>(&cmdlineLen) + sizeof(cmdlineLen));
    data.insert(data.end(), info.cmdline.begin(), info.cmdline.end());
    
    // Registers (raw dump)
    data.insert(data.end(), reinterpret_cast<const uint8_t*>(&registers),
                reinterpret_cast<const uint8_t*>(&registers) + sizeof(LinuxRegisters) - sizeof(std::vector<uint8_t>));
    
    // FPU state
    uint8_t hasFPU = registers.hasFPU ? 1 : 0;
    data.push_back(hasFPU);
    if (registers.hasFPU) {
        uint32_t fpuSize = static_cast<uint32_t>(registers.fpuState.size());
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&fpuSize),
                    reinterpret_cast<uint8_t*>(&fpuSize) + sizeof(fpuSize));
        data.insert(data.end(), registers.fpuState.begin(), registers.fpuState.end());
    }
    
    // Memory regions count
    uint32_t regionCount = static_cast<uint32_t>(memoryMap.size());
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&regionCount),
                reinterpret_cast<uint8_t*>(&regionCount) + sizeof(regionCount));
    
    // Each memory region
    for (const auto& region : memoryMap) {
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&region.startAddr),
                    reinterpret_cast<const uint8_t*>(&region.startAddr) + sizeof(region.startAddr));
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&region.endAddr),
                    reinterpret_cast<const uint8_t*>(&region.endAddr) + sizeof(region.endAddr));
        
        uint8_t flags = (region.readable ? 1 : 0) | 
                       (region.writable ? 2 : 0) |
                       (region.executable ? 4 : 0) |
                       (region.isPrivate ? 8 : 0);
        data.push_back(flags);
        
        uint32_t pathLen = static_cast<uint32_t>(region.pathname.size());
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&pathLen),
                    reinterpret_cast<uint8_t*>(&pathLen) + sizeof(pathLen));
        data.insert(data.end(), region.pathname.begin(), region.pathname.end());
    }
    
    // Memory dumps count
    uint32_t dumpCount = static_cast<uint32_t>(memoryDumps.size());
    data.insert(data.end(), reinterpret_cast<uint8_t*>(&dumpCount),
                reinterpret_cast<uint8_t*>(&dumpCount) + sizeof(dumpCount));
    
    // Each memory dump
    for (const auto& dump : memoryDumps) {
        // Region info
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&dump.region.startAddr),
                    reinterpret_cast<const uint8_t*>(&dump.region.startAddr) + sizeof(dump.region.startAddr));
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&dump.region.endAddr),
                    reinterpret_cast<const uint8_t*>(&dump.region.endAddr) + sizeof(dump.region.endAddr));
        
        // Data
        uint64_t dataSize = dump.data.size();
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&dataSize),
                    reinterpret_cast<uint8_t*>(&dataSize) + sizeof(dataSize));
        data.insert(data.end(), dump.data.begin(), dump.data.end());
    }
    
    // Signals
    data.insert(data.end(), reinterpret_cast<const uint8_t*>(&signals),
                reinterpret_cast<const uint8_t*>(&signals) + sizeof(signals));
    
    return data;
}

RealProcessCheckpoint RealProcessCheckpoint::deserialize(const std::vector<uint8_t>& data) {
    RealProcessCheckpoint checkpoint;
    
    if (data.size() < 8) {
        return checkpoint;  // Invalid data
    }
    
    size_t offset = 0;
    
    // Check magic number
    if (data[0] != 'R' || data[1] != 'C' || data[2] != 'H' || data[3] != 'K') {
        return checkpoint;  // Invalid magic
    }
    offset += 4;
    
    // Version
    uint32_t version;
    std::memcpy(&version, &data[offset], sizeof(version));
    offset += sizeof(version);
    
    if (version != 1) {
        return checkpoint;  // Unsupported version
    }
    
    // Checkpoint ID
    std::memcpy(&checkpoint.checkpointId, &data[offset], sizeof(checkpoint.checkpointId));
    offset += sizeof(checkpoint.checkpointId);
    
    // Timestamp
    std::memcpy(&checkpoint.timestamp, &data[offset], sizeof(checkpoint.timestamp));
    offset += sizeof(checkpoint.timestamp);
    
    // Name
    uint32_t nameLen;
    std::memcpy(&nameLen, &data[offset], sizeof(nameLen));
    offset += sizeof(nameLen);
    checkpoint.name = std::string(reinterpret_cast<const char*>(&data[offset]), nameLen);
    offset += nameLen;
    
    // Process Info - pid, ppid
    std::memcpy(&checkpoint.info.pid, &data[offset], sizeof(checkpoint.info.pid));
    offset += sizeof(checkpoint.info.pid);
    std::memcpy(&checkpoint.info.ppid, &data[offset], sizeof(checkpoint.info.ppid));
    offset += sizeof(checkpoint.info.ppid);
    
    // Process name
    uint32_t procNameLen;
    std::memcpy(&procNameLen, &data[offset], sizeof(procNameLen));
    offset += sizeof(procNameLen);
    checkpoint.info.name = std::string(reinterpret_cast<const char*>(&data[offset]), procNameLen);
    offset += procNameLen;
    
    // Cmdline
    uint32_t cmdlineLen;
    std::memcpy(&cmdlineLen, &data[offset], sizeof(cmdlineLen));
    offset += sizeof(cmdlineLen);
    checkpoint.info.cmdline = std::string(reinterpret_cast<const char*>(&data[offset]), cmdlineLen);
    offset += cmdlineLen;
    
    // Registers
    size_t regSize = sizeof(LinuxRegisters) - sizeof(std::vector<uint8_t>);
    std::memcpy(&checkpoint.registers, &data[offset], regSize);
    offset += regSize;
    
    // FPU state
    uint8_t hasFPU = data[offset++];
    checkpoint.registers.hasFPU = (hasFPU != 0);
    if (checkpoint.registers.hasFPU) {
        uint32_t fpuSize;
        std::memcpy(&fpuSize, &data[offset], sizeof(fpuSize));
        offset += sizeof(fpuSize);
        checkpoint.registers.fpuState.resize(fpuSize);
        std::memcpy(checkpoint.registers.fpuState.data(), &data[offset], fpuSize);
        offset += fpuSize;
    }
    
    // Memory regions
    uint32_t regionCount;
    std::memcpy(&regionCount, &data[offset], sizeof(regionCount));
    offset += sizeof(regionCount);
    
    for (uint32_t i = 0; i < regionCount; ++i) {
        MemoryRegion region;
        std::memcpy(&region.startAddr, &data[offset], sizeof(region.startAddr));
        offset += sizeof(region.startAddr);
        std::memcpy(&region.endAddr, &data[offset], sizeof(region.endAddr));
        offset += sizeof(region.endAddr);
        
        uint8_t flags = data[offset++];
        region.readable = (flags & 1) != 0;
        region.writable = (flags & 2) != 0;
        region.executable = (flags & 4) != 0;
        region.isPrivate = (flags & 8) != 0;
        
        uint32_t pathLen;
        std::memcpy(&pathLen, &data[offset], sizeof(pathLen));
        offset += sizeof(pathLen);
        region.pathname = std::string(reinterpret_cast<const char*>(&data[offset]), pathLen);
        offset += pathLen;
        
        checkpoint.memoryMap.push_back(region);
    }
    
    // Memory dumps
    uint32_t dumpCount;
    std::memcpy(&dumpCount, &data[offset], sizeof(dumpCount));
    offset += sizeof(dumpCount);
    
    for (uint32_t i = 0; i < dumpCount; ++i) {
        MemoryDump dump;
        std::memcpy(&dump.region.startAddr, &data[offset], sizeof(dump.region.startAddr));
        offset += sizeof(dump.region.startAddr);
        std::memcpy(&dump.region.endAddr, &data[offset], sizeof(dump.region.endAddr));
        offset += sizeof(dump.region.endAddr);
        
        uint64_t dataSize;
        std::memcpy(&dataSize, &data[offset], sizeof(dataSize));
        offset += sizeof(dataSize);
        
        dump.data.resize(dataSize);
        std::memcpy(dump.data.data(), &data[offset], dataSize);
        offset += dataSize;
        dump.isValid = true;
        
        checkpoint.memoryDumps.push_back(dump);
    }
    
    // Signals
    std::memcpy(&checkpoint.signals, &data[offset], sizeof(checkpoint.signals));
    
    return checkpoint;
}

uint64_t RealProcessCheckpoint::totalMemorySize() const {
    uint64_t total = 0;
    for (const auto& region : memoryMap) {
        total += region.size();
    }
    return total;
}

uint64_t RealProcessCheckpoint::dumpedMemorySize() const {
    uint64_t total = 0;
    for (const auto& dump : memoryDumps) {
        total += dump.data.size();
    }
    return total;
}

} // namespace real_process
} // namespace checkpoint
