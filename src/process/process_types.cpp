#include "process/process_types.hpp"
#include <cstring>
#include <chrono>
#include <stdexcept>

namespace checkpoint {
namespace process {

// ============================================================================
// RegisterSet Implementasyonu
// ============================================================================

StateData RegisterSet::serialize() const {
    StateData data;
    
    // General registers (16 * 4 = 64 bytes)
    for (const auto& reg : general) {
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&reg);
        data.insert(data.end(), ptr, ptr + sizeof(int32_t));
    }
    
    // PC, SP, BP (3 * 4 = 12 bytes)
    const uint8_t* pcPtr = reinterpret_cast<const uint8_t*>(&pc);
    data.insert(data.end(), pcPtr, pcPtr + sizeof(uint32_t));
    
    const uint8_t* spPtr = reinterpret_cast<const uint8_t*>(&sp);
    data.insert(data.end(), spPtr, spPtr + sizeof(uint32_t));
    
    const uint8_t* bpPtr = reinterpret_cast<const uint8_t*>(&bp);
    data.insert(data.end(), bpPtr, bpPtr + sizeof(uint32_t));
    
    // Flags (1 byte)
    data.push_back(flags.toByte());
    
    return data;
}

RegisterSet RegisterSet::deserialize(const StateData& data, size_t& offset) {
    RegisterSet regs;
    
    // General registers
    for (size_t i = 0; i < NUM_GENERAL_REGISTERS; ++i) {
        std::memcpy(&regs.general[i], data.data() + offset, sizeof(int32_t));
        offset += sizeof(int32_t);
    }
    
    // PC, SP, BP
    std::memcpy(&regs.pc, data.data() + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    std::memcpy(&regs.sp, data.data() + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    std::memcpy(&regs.bp, data.data() + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    // Flags
    regs.flags.fromByte(data[offset++]);
    
    return regs;
}

// ============================================================================
// PCB Implementasyonu
// ============================================================================

StateData PCB::serialize() const {
    StateData data;
    
    // PID (4 bytes)
    const uint8_t* pidPtr = reinterpret_cast<const uint8_t*>(&pid);
    data.insert(data.end(), pidPtr, pidPtr + sizeof(uint32_t));
    
    // Parent PID (4 bytes)
    const uint8_t* ppidPtr = reinterpret_cast<const uint8_t*>(&parentPid);
    data.insert(data.end(), ppidPtr, ppidPtr + sizeof(uint32_t));
    
    // Name (length + string)
    uint32_t nameLen = static_cast<uint32_t>(name.size());
    const uint8_t* nameLenPtr = reinterpret_cast<const uint8_t*>(&nameLen);
    data.insert(data.end(), nameLenPtr, nameLenPtr + sizeof(uint32_t));
    data.insert(data.end(), name.begin(), name.end());
    
    // State (1 byte)
    data.push_back(static_cast<uint8_t>(state));
    
    // Priority (1 byte)
    data.push_back(priority);
    
    // Registers
    StateData regData = registers.serialize();
    data.insert(data.end(), regData.begin(), regData.end());
    
    // Timing info
    const uint8_t* cpuTimePtr = reinterpret_cast<const uint8_t*>(&cpuTimeUsed);
    data.insert(data.end(), cpuTimePtr, cpuTimePtr + sizeof(uint64_t));
    
    const uint8_t* creationPtr = reinterpret_cast<const uint8_t*>(&creationTime);
    data.insert(data.end(), creationPtr, creationPtr + sizeof(uint64_t));
    
    const uint8_t* lastSchedPtr = reinterpret_cast<const uint8_t*>(&lastScheduledTime);
    data.insert(data.end(), lastSchedPtr, lastSchedPtr + sizeof(uint64_t));
    
    // Counters
    const uint8_t* ctxSwitchPtr = reinterpret_cast<const uint8_t*>(&contextSwitchCount);
    data.insert(data.end(), ctxSwitchPtr, ctxSwitchPtr + sizeof(uint32_t));
    
    const uint8_t* instCntPtr = reinterpret_cast<const uint8_t*>(&instructionCount);
    data.insert(data.end(), instCntPtr, instCntPtr + sizeof(uint32_t));
    
    return data;
}

PCB PCB::deserialize(const StateData& data, size_t& offset) {
    PCB pcb;
    
    // PID
    std::memcpy(&pcb.pid, data.data() + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    // Parent PID
    std::memcpy(&pcb.parentPid, data.data() + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    // Name
    uint32_t nameLen;
    std::memcpy(&nameLen, data.data() + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    pcb.name = std::string(data.begin() + offset, data.begin() + offset + nameLen);
    offset += nameLen;
    
    // State
    pcb.state = static_cast<ProcessState>(data[offset++]);
    
    // Priority
    pcb.priority = data[offset++];
    
    // Registers
    pcb.registers = RegisterSet::deserialize(data, offset);
    
    // Timing info
    std::memcpy(&pcb.cpuTimeUsed, data.data() + offset, sizeof(uint64_t));
    offset += sizeof(uint64_t);
    
    std::memcpy(&pcb.creationTime, data.data() + offset, sizeof(uint64_t));
    offset += sizeof(uint64_t);
    
    std::memcpy(&pcb.lastScheduledTime, data.data() + offset, sizeof(uint64_t));
    offset += sizeof(uint64_t);
    
    // Counters
    std::memcpy(&pcb.contextSwitchCount, data.data() + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    std::memcpy(&pcb.instructionCount, data.data() + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    return pcb;
}

// ============================================================================
// ProcessMemory Implementasyonu
// ============================================================================

uint8_t ProcessMemory::readByte(SegmentType segment, uint32_t offset) const {
    switch (segment) {
        case SegmentType::CODE:
            if (offset >= codeSegment.size()) 
                throw std::out_of_range("Code segment offset out of range");
            return codeSegment[offset];
        case SegmentType::DATA:
            if (offset >= dataSegment.size()) 
                throw std::out_of_range("Data segment offset out of range");
            return dataSegment[offset];
        case SegmentType::HEAP:
            if (offset >= heapSegment.size()) 
                throw std::out_of_range("Heap segment offset out of range");
            return heapSegment[offset];
        case SegmentType::STACK:
            if (offset >= stackSegment.size()) 
                throw std::out_of_range("Stack segment offset out of range");
            return stackSegment[offset];
        default:
            throw std::invalid_argument("Invalid segment type");
    }
}

void ProcessMemory::writeByte(SegmentType segment, uint32_t offset, uint8_t value) {
    switch (segment) {
        case SegmentType::CODE:
            if (offset >= codeSegment.size()) 
                throw std::out_of_range("Code segment offset out of range");
            codeSegment[offset] = value;
            break;
        case SegmentType::DATA:
            if (offset >= dataSegment.size()) 
                throw std::out_of_range("Data segment offset out of range");
            dataSegment[offset] = value;
            break;
        case SegmentType::HEAP:
            if (offset >= heapSegment.size()) 
                throw std::out_of_range("Heap segment offset out of range");
            heapSegment[offset] = value;
            break;
        case SegmentType::STACK:
            if (offset >= stackSegment.size()) 
                throw std::out_of_range("Stack segment offset out of range");
            stackSegment[offset] = value;
            break;
        default:
            throw std::invalid_argument("Invalid segment type");
    }
    
    // Page dirty flag'ini güncelle
    size_t pageIndex = offset / PAGE_SIZE;
    if (pageIndex < pageTable.size()) {
        pageTable[pageIndex].dirty = true;
        pageTable[pageIndex].accessed = true;
    }
}

int32_t ProcessMemory::readWord(SegmentType segment, uint32_t offset) const {
    int32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value |= static_cast<int32_t>(readByte(segment, offset + i)) << (i * 8);
    }
    return value;
}

void ProcessMemory::writeWord(SegmentType segment, uint32_t offset, int32_t value) {
    for (int i = 0; i < 4; ++i) {
        writeByte(segment, offset + i, (value >> (i * 8)) & 0xFF);
    }
}

void ProcessMemory::push(int32_t value, uint32_t& sp) {
    if (sp < 4) {
        throw std::runtime_error("Stack overflow");
    }
    sp -= 4;
    writeWord(SegmentType::STACK, sp, value);
}

int32_t ProcessMemory::pop(uint32_t& sp) {
    if (sp >= STACK_SIZE - 4) {
        throw std::runtime_error("Stack underflow");
    }
    int32_t value = readWord(SegmentType::STACK, sp);
    sp += 4;
    return value;
}

StateData ProcessMemory::serialize() const {
    StateData data;
    
    // Helper lambda for segment serialization
    auto serializeSegment = [&data](const std::vector<uint8_t>& segment) {
        uint32_t size = static_cast<uint32_t>(segment.size());
        const uint8_t* sizePtr = reinterpret_cast<const uint8_t*>(&size);
        data.insert(data.end(), sizePtr, sizePtr + sizeof(uint32_t));
        data.insert(data.end(), segment.begin(), segment.end());
    };
    
    // Serialize all segments
    serializeSegment(codeSegment);
    serializeSegment(dataSegment);
    serializeSegment(heapSegment);
    serializeSegment(stackSegment);
    
    // Serialize page table
    uint32_t pageCount = static_cast<uint32_t>(pageTable.size());
    const uint8_t* pageCountPtr = reinterpret_cast<const uint8_t*>(&pageCount);
    data.insert(data.end(), pageCountPtr, pageCountPtr + sizeof(uint32_t));
    
    for (const auto& entry : pageTable) {
        const uint8_t* framePtr = reinterpret_cast<const uint8_t*>(&entry.frameNumber);
        data.insert(data.end(), framePtr, framePtr + sizeof(uint32_t));
        
        uint8_t flags = (entry.valid ? 1 : 0) |
                       (entry.dirty ? 2 : 0) |
                       (entry.accessed ? 4 : 0) |
                       (entry.readOnly ? 8 : 0);
        data.push_back(flags);
    }
    
    return data;
}

ProcessMemory ProcessMemory::deserialize(const StateData& data, size_t& offset) {
    ProcessMemory memory;
    
    // Helper lambda for segment deserialization
    auto deserializeSegment = [&data, &offset](std::vector<uint8_t>& segment) {
        uint32_t size;
        std::memcpy(&size, data.data() + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);
        segment.assign(data.begin() + offset, data.begin() + offset + size);
        offset += size;
    };
    
    // Deserialize all segments
    deserializeSegment(memory.codeSegment);
    deserializeSegment(memory.dataSegment);
    deserializeSegment(memory.heapSegment);
    deserializeSegment(memory.stackSegment);
    
    // Deserialize page table
    uint32_t pageCount;
    std::memcpy(&pageCount, data.data() + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    memory.pageTable.resize(pageCount);
    for (uint32_t i = 0; i < pageCount; ++i) {
        std::memcpy(&memory.pageTable[i].frameNumber, data.data() + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);
        
        uint8_t flags = data[offset++];
        memory.pageTable[i].valid = flags & 1;
        memory.pageTable[i].dirty = flags & 2;
        memory.pageTable[i].accessed = flags & 4;
        memory.pageTable[i].readOnly = flags & 8;
    }
    
    return memory;
}

// ============================================================================
// ProcessSnapshot Implementasyonu
// ============================================================================

StateData ProcessSnapshot::serialize() const {
    StateData data;
    
    // PCB
    StateData pcbData = pcb.serialize();
    uint32_t pcbSize = static_cast<uint32_t>(pcbData.size());
    const uint8_t* pcbSizePtr = reinterpret_cast<const uint8_t*>(&pcbSize);
    data.insert(data.end(), pcbSizePtr, pcbSizePtr + sizeof(uint32_t));
    data.insert(data.end(), pcbData.begin(), pcbData.end());
    
    // Memory
    StateData memData = memory.serialize();
    uint32_t memSize = static_cast<uint32_t>(memData.size());
    const uint8_t* memSizePtr = reinterpret_cast<const uint8_t*>(&memSize);
    data.insert(data.end(), memSizePtr, memSizePtr + sizeof(uint32_t));
    data.insert(data.end(), memData.begin(), memData.end());
    
    // Checkpoint name
    uint32_t nameLen = static_cast<uint32_t>(checkpointName.size());
    const uint8_t* nameLenPtr = reinterpret_cast<const uint8_t*>(&nameLen);
    data.insert(data.end(), nameLenPtr, nameLenPtr + sizeof(uint32_t));
    data.insert(data.end(), checkpointName.begin(), checkpointName.end());
    
    // Timestamp
    const uint8_t* tsPtr = reinterpret_cast<const uint8_t*>(&timestamp);
    data.insert(data.end(), tsPtr, tsPtr + sizeof(uint64_t));
    
    return data;
}

ProcessSnapshot ProcessSnapshot::deserialize(const StateData& data) {
    ProcessSnapshot snapshot;
    size_t offset = 0;
    
    // PCB size (skip, we don't need it)
    uint32_t pcbSize;
    std::memcpy(&pcbSize, data.data() + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    // PCB
    snapshot.pcb = PCB::deserialize(data, offset);
    
    // Memory size (skip)
    uint32_t memSize;
    std::memcpy(&memSize, data.data() + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    // Memory
    snapshot.memory = ProcessMemory::deserialize(data, offset);
    
    // Checkpoint name
    uint32_t nameLen;
    std::memcpy(&nameLen, data.data() + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    snapshot.checkpointName = std::string(data.begin() + offset, 
                                          data.begin() + offset + nameLen);
    offset += nameLen;
    
    // Timestamp
    std::memcpy(&snapshot.timestamp, data.data() + offset, sizeof(uint64_t));
    
    return snapshot;
}

} // namespace process
} // namespace checkpoint
