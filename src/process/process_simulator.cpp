#include "process/process_simulator.hpp"
#include "logger/operation_logger.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <set>
#include <map>

namespace checkpoint {
namespace process {

// ============================================================================
// ProcessSimulator Implementation Details
// ============================================================================

struct ProcessSimulator::Impl {
    SimulatorConfig config;
    
    // Process storage
    std::map<uint32_t, PCB> processes;
    std::map<uint32_t, ProcessMemory> memories;
    std::map<uint32_t, std::set<uint32_t>> breakpoints;
    
    // PID counter
    uint32_t nextPid = 1;
    
    // Statistics
    uint64_t totalInstructions = 0;
    uint64_t totalCycles = 0;
    
    // Callbacks
    InstructionCallback instructionCallback;
    StateChangeCallback stateChangeCallback;
    SyscallHandler syscallHandler;
    IOHandler ioHandler;
    
    // Default syscall handler
    int32_t defaultSyscallHandler(int32_t num, const RegisterSet& regs) {
        switch (num) {
            case 0: // exit
                return 0;
            case 1: // print (R0 = value to print)
                std::cout << "[SYSCALL] Output: " << regs.general[0] << std::endl;
                return 0;
            case 2: // read (simulated - always returns 42)
                return 42;
            default:
                return -1;
        }
    }
    
    // Default I/O handler
    int32_t defaultIOHandler(uint8_t port, bool isRead, int32_t value) {
        if (isRead) {
            std::cout << "[I/O] Read from port " << static_cast<int>(port) 
                      << " -> 0" << std::endl;
            return 0;
        } else {
            std::cout << "[I/O] Write to port " << static_cast<int>(port) 
                      << " <- " << value << std::endl;
            return 0;
        }
    }
};

// ============================================================================
// Constructor / Destructor
// ============================================================================

ProcessSimulator::ProcessSimulator() 
    : m_impl(std::make_unique<Impl>()) {
}

ProcessSimulator::ProcessSimulator(const SimulatorConfig& config)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->config = config;
}

ProcessSimulator::~ProcessSimulator() = default;

ProcessSimulator::ProcessSimulator(ProcessSimulator&&) noexcept = default;
ProcessSimulator& ProcessSimulator::operator=(ProcessSimulator&&) noexcept = default;

// ============================================================================
// Process Yönetimi
// ============================================================================

uint32_t ProcessSimulator::createProcess(const std::string& name, uint8_t priority) {
    uint32_t pid = m_impl->nextPid++;
    
    PCB pcb;
    pcb.pid = pid;
    pcb.parentPid = 0;
    pcb.name = name;
    pcb.state = ProcessState::NEW;
    pcb.priority = priority;
    pcb.cpuTimeUsed = 0;
    pcb.creationTime = static_cast<uint64_t>(
        std::chrono::system_clock::now().time_since_epoch().count());
    pcb.lastScheduledTime = 0;
    pcb.contextSwitchCount = 0;
    pcb.instructionCount = 0;
    
    // Initialize registers
    pcb.registers = RegisterSet();
    
    m_impl->processes[pid] = pcb;
    m_impl->memories[pid] = ProcessMemory();
    m_impl->breakpoints[pid] = {};
    
    if (m_impl->config.enableLogging) {
        LOG_INFO("ProcessSimulator", "Created process '" + name + "' with PID " + std::to_string(pid));
    }
    
    return pid;
}

bool ProcessSimulator::loadProgram(uint32_t pid, const std::vector<Instruction>& instructions) {
    auto binary = SimpleAssembler::toBinary(instructions);
    return loadProgram(pid, binary);
}

bool ProcessSimulator::loadProgram(uint32_t pid, const std::vector<uint8_t>& binary) {
    auto memIt = m_impl->memories.find(pid);
    if (memIt == m_impl->memories.end()) {
        return false;
    }
    
    if (binary.size() > CODE_SIZE) {
        if (m_impl->config.enableLogging) {
            LOG_ERROR("ProcessSimulator", "Program too large for code segment");
        }
        return false;
    }
    
    // Code segment'e yükle
    std::copy(binary.begin(), binary.end(), memIt->second.codeSegment.begin());
    
    // Process'i READY durumuna getir
    auto pcbIt = m_impl->processes.find(pid);
    if (pcbIt != m_impl->processes.end()) {
        pcbIt->second.state = ProcessState::READY;
        pcbIt->second.registers.pc = 0;
    }
    
    if (m_impl->config.enableLogging) {
        LOG_INFO("ProcessSimulator", "Loaded " + std::to_string(binary.size()) + 
                " bytes into process " + std::to_string(pid));
    }
    
    return true;
}

const PCB* ProcessSimulator::getProcess(uint32_t pid) const {
    auto it = m_impl->processes.find(pid);
    return it != m_impl->processes.end() ? &it->second : nullptr;
}

PCB* ProcessSimulator::getProcess(uint32_t pid) {
    auto it = m_impl->processes.find(pid);
    return it != m_impl->processes.end() ? &it->second : nullptr;
}

const ProcessMemory* ProcessSimulator::getProcessMemory(uint32_t pid) const {
    auto it = m_impl->memories.find(pid);
    return it != m_impl->memories.end() ? &it->second : nullptr;
}

ProcessMemory* ProcessSimulator::getProcessMemory(uint32_t pid) {
    auto it = m_impl->memories.find(pid);
    return it != m_impl->memories.end() ? &it->second : nullptr;
}

std::vector<uint32_t> ProcessSimulator::listProcesses() const {
    std::vector<uint32_t> pids;
    for (const auto& [pid, pcb] : m_impl->processes) {
        pids.push_back(pid);
    }
    return pids;
}

std::vector<const PCB*> ProcessSimulator::listAllPCBs() const {
    std::vector<const PCB*> pcbs;
    for (const auto& [pid, pcb] : m_impl->processes) {
        pcbs.push_back(&pcb);
    }
    return pcbs;
}

bool ProcessSimulator::terminateProcess(uint32_t pid) {
    auto it = m_impl->processes.find(pid);
    if (it == m_impl->processes.end()) {
        return false;
    }
    
    ProcessState oldState = it->second.state;
    it->second.state = ProcessState::TERMINATED;
    
    if (m_impl->stateChangeCallback) {
        m_impl->stateChangeCallback(oldState, ProcessState::TERMINATED);
    }
    
    if (m_impl->config.enableLogging) {
        LOG_INFO("ProcessSimulator", "Terminated process " + std::to_string(pid));
    }
    
    return true;
}

// ============================================================================
// Execution
// ============================================================================

ExecutionResult ProcessSimulator::step(uint32_t pid) {
    ExecutionResult result;
    
    auto pcbIt = m_impl->processes.find(pid);
    auto memIt = m_impl->memories.find(pid);
    
    if (pcbIt == m_impl->processes.end() || memIt == m_impl->memories.end()) {
        result.success = false;
        result.errorMessage = "Process not found";
        return result;
    }
    
    PCB& pcb = pcbIt->second;
    ProcessMemory& memory = memIt->second;
    
    // Process terminated ise çalıştırma
    if (pcb.state == ProcessState::TERMINATED) {
        result.success = false;
        result.halted = true;
        result.errorMessage = "Process is terminated";
        return result;
    }
    
    // Process'i RUNNING durumuna al
    if (pcb.state != ProcessState::RUNNING) {
        ProcessState oldState = pcb.state;
        pcb.state = ProcessState::RUNNING;
        if (m_impl->stateChangeCallback) {
            m_impl->stateChangeCallback(oldState, ProcessState::RUNNING);
        }
    }
    
    // Breakpoint kontrolü
    if (m_impl->config.enableBreakpoints) {
        auto& bps = m_impl->breakpoints[pid];
        if (bps.find(pcb.registers.pc) != bps.end()) {
            result.breakpoint = true;
            if (m_impl->config.enableLogging) {
                LOG_DEBUG("ProcessSimulator", "Breakpoint hit at address " + 
                         std::to_string(pcb.registers.pc));
            }
            return result;
        }
    }
    
    // Instruction'ı oku
    if (pcb.registers.pc + INSTRUCTION_SIZE > memory.codeSegment.size()) {
        result.success = false;
        result.errorMessage = "PC out of bounds";
        terminateProcess(pid);
        return result;
    }
    
    Instruction inst = Instruction::decode(memory.codeSegment.data() + pcb.registers.pc);
    
    // Callback
    if (m_impl->instructionCallback) {
        m_impl->instructionCallback(inst, pcb);
    }
    
    // Instruction'ı çalıştır
    result = executeInstruction(pid, inst);
    
    if (result.success && !result.halted) {
        pcb.instructionCount++;
        pcb.cpuTimeUsed++;
        m_impl->totalInstructions++;
        m_impl->totalCycles++;
        result.instructionsExecuted = 1;
    }
    
    return result;
}

ExecutionResult ProcessSimulator::run(uint32_t pid, uint32_t maxInstructions) {
    ExecutionResult totalResult;
    uint32_t limit = maxInstructions > 0 ? maxInstructions : m_impl->config.maxInstructions;
    
    for (uint32_t i = 0; i < limit; ++i) {
        ExecutionResult stepResult = step(pid);
        totalResult.instructionsExecuted++;
        
        if (!stepResult.success || stepResult.halted || stepResult.breakpoint) {
            totalResult.success = stepResult.success;
            totalResult.halted = stepResult.halted;
            totalResult.breakpoint = stepResult.breakpoint;
            totalResult.errorMessage = stepResult.errorMessage;
            break;
        }
    }
    
    return totalResult;
}

ExecutionResult ProcessSimulator::runUntilHalt(uint32_t pid) {
    return run(pid, m_impl->config.maxInstructions);
}

ExecutionResult ProcessSimulator::runUntilAddress(uint32_t pid, uint32_t address) {
    ExecutionResult totalResult;
    uint32_t limit = m_impl->config.maxInstructions;
    
    for (uint32_t i = 0; i < limit; ++i) {
        auto pcb = getProcess(pid);
        if (pcb && pcb->registers.pc == address) {
            break;
        }
        
        ExecutionResult stepResult = step(pid);
        totalResult.instructionsExecuted++;
        
        if (!stepResult.success || stepResult.halted || stepResult.breakpoint) {
            totalResult.success = stepResult.success;
            totalResult.halted = stepResult.halted;
            totalResult.breakpoint = stepResult.breakpoint;
            totalResult.errorMessage = stepResult.errorMessage;
            break;
        }
    }
    
    return totalResult;
}

Instruction ProcessSimulator::getCurrentInstruction(uint32_t pid) const {
    auto pcb = getProcess(pid);
    auto mem = getProcessMemory(pid);
    
    if (!pcb || !mem) {
        return Instruction(Opcode::NOP);
    }
    
    if (pcb->registers.pc + INSTRUCTION_SIZE > mem->codeSegment.size()) {
        return Instruction(Opcode::NOP);
    }
    
    return Instruction::decode(mem->codeSegment.data() + pcb->registers.pc);
}

// ============================================================================
// Instruction Execution
// ============================================================================

ExecutionResult ProcessSimulator::executeInstruction(uint32_t pid, const Instruction& inst) {
    ExecutionResult result;
    
    auto pcbIt = m_impl->processes.find(pid);
    auto memIt = m_impl->memories.find(pid);
    
    if (pcbIt == m_impl->processes.end() || memIt == m_impl->memories.end()) {
        result.success = false;
        result.errorMessage = "Process not found";
        return result;
    }
    
    PCB& pcb = pcbIt->second;
    ProcessMemory& memory = memIt->second;
    RegisterSet& regs = pcb.registers;
    
    bool pcModified = false;
    
    try {
        switch (inst.opcode) {
            case Opcode::NOP:
                break;
                
            case Opcode::LOAD_IMM:
                regs.general[inst.dest] = inst.immediate;
                break;
                
            case Opcode::LOAD_MEM:
                regs.general[inst.dest] = memory.readWord(SegmentType::DATA, 
                                                         static_cast<uint32_t>(inst.immediate));
                break;
                
            case Opcode::STORE:
                memory.writeWord(SegmentType::DATA, 
                               static_cast<uint32_t>(inst.immediate),
                               regs.general[inst.dest]);
                break;
                
            case Opcode::MOV:
                regs.general[inst.dest] = regs.general[inst.src1];
                break;
                
            case Opcode::PUSH:
                memory.push(regs.general[inst.dest], regs.sp);
                break;
                
            case Opcode::POP:
                regs.general[inst.dest] = memory.pop(regs.sp);
                break;
                
            case Opcode::ADD: {
                int64_t res = static_cast<int64_t>(regs.general[inst.src1]) + 
                             static_cast<int64_t>(regs.general[inst.src2]);
                regs.general[inst.dest] = static_cast<int32_t>(res);
                updateFlags(pcb, res);
                break;
            }
                
            case Opcode::SUB: {
                int64_t res = static_cast<int64_t>(regs.general[inst.src1]) - 
                             static_cast<int64_t>(regs.general[inst.src2]);
                regs.general[inst.dest] = static_cast<int32_t>(res);
                updateFlags(pcb, res);
                break;
            }
                
            case Opcode::MUL: {
                int64_t res = static_cast<int64_t>(regs.general[inst.src1]) * 
                             static_cast<int64_t>(regs.general[inst.src2]);
                regs.general[inst.dest] = static_cast<int32_t>(res);
                updateFlags(pcb, res);
                break;
            }
                
            case Opcode::DIV:
                if (regs.general[inst.src2] == 0) {
                    result.success = false;
                    result.errorMessage = "Division by zero";
                    terminateProcess(pid);
                    return result;
                }
                regs.general[inst.dest] = regs.general[inst.src1] / regs.general[inst.src2];
                break;
                
            case Opcode::MOD:
                if (regs.general[inst.src2] == 0) {
                    result.success = false;
                    result.errorMessage = "Division by zero";
                    terminateProcess(pid);
                    return result;
                }
                regs.general[inst.dest] = regs.general[inst.src1] % regs.general[inst.src2];
                break;
                
            case Opcode::INC:
                regs.general[inst.dest]++;
                updateFlags(pcb, regs.general[inst.dest]);
                break;
                
            case Opcode::DEC:
                regs.general[inst.dest]--;
                updateFlags(pcb, regs.general[inst.dest]);
                break;
                
            case Opcode::NEG:
                regs.general[inst.dest] = -regs.general[inst.dest];
                updateFlags(pcb, regs.general[inst.dest]);
                break;
                
            case Opcode::AND:
                regs.general[inst.dest] = regs.general[inst.src1] & regs.general[inst.src2];
                updateFlags(pcb, regs.general[inst.dest]);
                break;
                
            case Opcode::OR:
                regs.general[inst.dest] = regs.general[inst.src1] | regs.general[inst.src2];
                updateFlags(pcb, regs.general[inst.dest]);
                break;
                
            case Opcode::XOR:
                regs.general[inst.dest] = regs.general[inst.src1] ^ regs.general[inst.src2];
                updateFlags(pcb, regs.general[inst.dest]);
                break;
                
            case Opcode::NOT:
                regs.general[inst.dest] = ~regs.general[inst.dest];
                updateFlags(pcb, regs.general[inst.dest]);
                break;
                
            case Opcode::SHL:
                regs.general[inst.dest] = regs.general[inst.src1] << inst.src2;
                updateFlags(pcb, regs.general[inst.dest]);
                break;
                
            case Opcode::SHR:
                regs.general[inst.dest] = regs.general[inst.src1] >> inst.src2;
                updateFlags(pcb, regs.general[inst.dest]);
                break;
                
            case Opcode::CMP: {
                int64_t res = static_cast<int64_t>(regs.general[inst.dest]) - 
                             static_cast<int64_t>(regs.general[inst.src1]);
                updateFlags(pcb, res);
                break;
            }
                
            case Opcode::TEST: {
                int64_t res = regs.general[inst.dest] & regs.general[inst.src1];
                updateFlags(pcb, res);
                break;
            }
                
            case Opcode::JMP:
                regs.pc = static_cast<uint32_t>(inst.immediate);
                pcModified = true;
                break;
                
            case Opcode::JZ:
                if (regs.flags.zero) {
                    regs.pc = static_cast<uint32_t>(inst.immediate);
                    pcModified = true;
                }
                break;
                
            case Opcode::JNZ:
                if (!regs.flags.zero) {
                    regs.pc = static_cast<uint32_t>(inst.immediate);
                    pcModified = true;
                }
                break;
                
            case Opcode::JG:
                if (!regs.flags.zero && !regs.flags.negative) {
                    regs.pc = static_cast<uint32_t>(inst.immediate);
                    pcModified = true;
                }
                break;
                
            case Opcode::JL:
                if (regs.flags.negative) {
                    regs.pc = static_cast<uint32_t>(inst.immediate);
                    pcModified = true;
                }
                break;
                
            case Opcode::JGE:
                if (!regs.flags.negative) {
                    regs.pc = static_cast<uint32_t>(inst.immediate);
                    pcModified = true;
                }
                break;
                
            case Opcode::JLE:
                if (regs.flags.zero || regs.flags.negative) {
                    regs.pc = static_cast<uint32_t>(inst.immediate);
                    pcModified = true;
                }
                break;
                
            case Opcode::CALL:
                memory.push(regs.pc + INSTRUCTION_SIZE, regs.sp);
                regs.pc = static_cast<uint32_t>(inst.immediate);
                pcModified = true;
                break;
                
            case Opcode::RET: {
                uint32_t retAddr = static_cast<uint32_t>(memory.pop(regs.sp));
                regs.pc = retAddr;
                pcModified = true;
                break;
            }
                
            case Opcode::SYSCALL: {
                int32_t res;
                if (m_impl->syscallHandler) {
                    res = m_impl->syscallHandler(inst.dest, regs);
                } else {
                    res = m_impl->defaultSyscallHandler(inst.dest, regs);
                }
                regs.general[0] = res;
                break;
            }
                
            case Opcode::HALT:
                result.halted = true;
                terminateProcess(pid);
                break;
                
            case Opcode::IN: {
                int32_t value;
                if (m_impl->ioHandler) {
                    value = m_impl->ioHandler(inst.src1, true, 0);
                } else {
                    value = m_impl->defaultIOHandler(inst.src1, true, 0);
                }
                regs.general[inst.dest] = value;
                break;
            }
                
            case Opcode::OUT:
                if (m_impl->ioHandler) {
                    m_impl->ioHandler(inst.dest, false, regs.general[inst.src1]);
                } else {
                    m_impl->defaultIOHandler(inst.dest, false, regs.general[inst.src1]);
                }
                break;
                
            case Opcode::BREAK:
                result.breakpoint = true;
                break;
                
            case Opcode::PRINT:
                if (m_impl->config.verboseMode) {
                    std::cout << "[PRINT] R" << static_cast<int>(inst.dest) 
                              << " = " << regs.general[inst.dest] << std::endl;
                }
                break;
                
            default:
                result.success = false;
                result.errorMessage = "Unknown opcode";
                return result;
        }
    } catch (const std::exception& e) {
        result.success = false;
        result.errorMessage = e.what();
        terminateProcess(pid);
        return result;
    }
    
    // PC'yi ilerlet (jump instruction'ları hariç)
    if (!pcModified && !result.halted) {
        regs.pc += INSTRUCTION_SIZE;
    }
    
    return result;
}

void ProcessSimulator::updateFlags(PCB& pcb, int64_t result) {
    pcb.registers.flags.zero = (result == 0);
    pcb.registers.flags.negative = (result < 0);
    pcb.registers.flags.carry = (result > INT32_MAX || result < INT32_MIN);
    pcb.registers.flags.overflow = (result > INT32_MAX || result < INT32_MIN);
}

// ============================================================================
// State Management
// ============================================================================

bool ProcessSimulator::setProcessState(uint32_t pid, ProcessState newState) {
    auto it = m_impl->processes.find(pid);
    if (it == m_impl->processes.end()) {
        return false;
    }
    
    ProcessState oldState = it->second.state;
    it->second.state = newState;
    
    if (m_impl->stateChangeCallback) {
        m_impl->stateChangeCallback(oldState, newState);
    }
    
    return true;
}

int32_t ProcessSimulator::getRegister(uint32_t pid, Register reg) const {
    auto pcb = getProcess(pid);
    if (!pcb) return 0;
    
    if (static_cast<uint8_t>(reg) < NUM_GENERAL_REGISTERS) {
        return pcb->registers.general[static_cast<uint8_t>(reg)];
    }
    return 0;
}

bool ProcessSimulator::setRegister(uint32_t pid, Register reg, int32_t value) {
    auto pcb = getProcess(pid);
    if (!pcb) return false;
    
    if (static_cast<uint8_t>(reg) < NUM_GENERAL_REGISTERS) {
        m_impl->processes[pid].registers.general[static_cast<uint8_t>(reg)] = value;
        return true;
    }
    return false;
}

uint32_t ProcessSimulator::getPC(uint32_t pid) const {
    auto pcb = getProcess(pid);
    return pcb ? pcb->registers.pc : 0;
}

bool ProcessSimulator::setPC(uint32_t pid, uint32_t value) {
    auto it = m_impl->processes.find(pid);
    if (it == m_impl->processes.end()) return false;
    it->second.registers.pc = value;
    return true;
}

int32_t ProcessSimulator::readMemory(uint32_t pid, SegmentType segment, uint32_t offset) const {
    auto mem = getProcessMemory(pid);
    if (!mem) return 0;
    return mem->readWord(segment, offset);
}

bool ProcessSimulator::writeMemory(uint32_t pid, SegmentType segment, uint32_t offset, int32_t value) {
    auto mem = getProcessMemory(pid);
    if (!mem) return false;
    m_impl->memories[pid].writeWord(segment, offset, value);
    return true;
}

// ============================================================================
// Checkpoint & Rollback
// ============================================================================

ProcessSnapshot ProcessSimulator::takeSnapshot(uint32_t pid, const std::string& name) const {
    ProcessSnapshot snapshot;
    
    auto pcb = getProcess(pid);
    auto mem = getProcessMemory(pid);
    
    if (!pcb || !mem) {
        return snapshot;
    }
    
    snapshot.pcb = *pcb;
    snapshot.memory = *mem;
    snapshot.checkpointName = name.empty() ? 
        ("Checkpoint_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count())) : name;
    snapshot.timestamp = static_cast<uint64_t>(
        std::chrono::system_clock::now().time_since_epoch().count());
    
    if (m_impl->config.enableLogging) {
        LOG_INFO("ProcessSimulator", "Snapshot taken for process " + std::to_string(pid) + 
                ": " + snapshot.checkpointName);
    }
    
    return snapshot;
}

bool ProcessSimulator::restoreFromSnapshot(uint32_t pid, const ProcessSnapshot& snapshot) {
    auto pcbIt = m_impl->processes.find(pid);
    auto memIt = m_impl->memories.find(pid);
    
    if (pcbIt == m_impl->processes.end() || memIt == m_impl->memories.end()) {
        return false;
    }
    
    // PCB'yi restore et (PID'yi koru)
    uint32_t currentPid = pcbIt->second.pid;
    pcbIt->second = snapshot.pcb;
    pcbIt->second.pid = currentPid;
    
    // Memory'yi restore et
    memIt->second = snapshot.memory;
    
    if (m_impl->config.enableLogging) {
        LOG_INFO("ProcessSimulator", "Restored process " + std::to_string(pid) + 
                " from snapshot: " + snapshot.checkpointName);
    }
    
    return true;
}

bool ProcessSimulator::saveCheckpoint(uint32_t pid, const std::string& name, 
                                     const std::string& path) {
    ProcessSnapshot snapshot = takeSnapshot(pid, name);
    StateData data = snapshot.serialize();
    
    std::string filePath = path.empty() ? 
        ("process_checkpoints/" + std::to_string(pid) + "_" + name + ".pchkpt") : path;
    
    // Ensure directory exists
    std::filesystem::path dir = std::filesystem::path(filePath).parent_path();
    if (!dir.empty()) {
        std::filesystem::create_directories(dir);
    }
    
    std::ofstream file(filePath, std::ios::binary);
    if (!file) {
        return false;
    }
    
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    return file.good();
}

bool ProcessSimulator::loadCheckpoint(uint32_t pid, const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    StateData data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);
    
    if (!file.good()) {
        return false;
    }
    
    ProcessSnapshot snapshot = ProcessSnapshot::deserialize(data);
    return restoreFromSnapshot(pid, snapshot);
}

CheckpointId ProcessSimulator::createCheckpoint(uint32_t pid, const std::string& name,
                                                StateManager& stateManager) {
    ProcessSnapshot snapshot = takeSnapshot(pid, name);
    StateData data = snapshot.serialize();
    
    auto result = stateManager.createCheckpoint(name, data);
    if (result.isSuccess()) {
        return *result.value;
    }
    return 0;
}

bool ProcessSimulator::rollbackToCheckpoint(uint32_t pid, CheckpointId checkpointId,
                                           StateManager& stateManager) {
    auto result = stateManager.getCheckpoint(checkpointId);
    if (!result.isSuccess()) {
        return false;
    }
    
    const StateData& data = result.value->getData();
    ProcessSnapshot snapshot = ProcessSnapshot::deserialize(data);
    return restoreFromSnapshot(pid, snapshot);
}

// ============================================================================
// Breakpoints
// ============================================================================

bool ProcessSimulator::addBreakpoint(uint32_t pid, uint32_t address) {
    auto it = m_impl->breakpoints.find(pid);
    if (it == m_impl->breakpoints.end()) {
        return false;
    }
    it->second.insert(address);
    return true;
}

bool ProcessSimulator::removeBreakpoint(uint32_t pid, uint32_t address) {
    auto it = m_impl->breakpoints.find(pid);
    if (it == m_impl->breakpoints.end()) {
        return false;
    }
    return it->second.erase(address) > 0;
}

void ProcessSimulator::clearBreakpoints(uint32_t pid) {
    auto it = m_impl->breakpoints.find(pid);
    if (it != m_impl->breakpoints.end()) {
        it->second.clear();
    }
}

std::vector<uint32_t> ProcessSimulator::getBreakpoints(uint32_t pid) const {
    auto it = m_impl->breakpoints.find(pid);
    if (it == m_impl->breakpoints.end()) {
        return {};
    }
    return std::vector<uint32_t>(it->second.begin(), it->second.end());
}

// ============================================================================
// Callbacks
// ============================================================================

void ProcessSimulator::setInstructionCallback(InstructionCallback callback) {
    m_impl->instructionCallback = callback;
}

void ProcessSimulator::setStateChangeCallback(StateChangeCallback callback) {
    m_impl->stateChangeCallback = callback;
}

void ProcessSimulator::setSyscallHandler(SyscallHandler handler) {
    m_impl->syscallHandler = handler;
}

void ProcessSimulator::setIOHandler(IOHandler handler) {
    m_impl->ioHandler = handler;
}

// ============================================================================
// Debug & Info
// ============================================================================

std::string ProcessSimulator::dumpRegisters(uint32_t pid) const {
    auto pcb = getProcess(pid);
    if (!pcb) {
        return "Process not found";
    }
    
    std::stringstream ss;
    ss << "=== Registers for Process " << pid << " ===\n";
    
    // General registers
    for (size_t i = 0; i < NUM_GENERAL_REGISTERS; ++i) {
        ss << "R" << std::setw(2) << i << ": " 
           << std::setw(12) << pcb->registers.general[i];
        if ((i + 1) % 4 == 0) ss << "\n";
        else ss << "  ";
    }
    
    ss << "\nPC:  " << std::setw(12) << pcb->registers.pc;
    ss << "  SP:  " << std::setw(12) << pcb->registers.sp;
    ss << "  BP:  " << std::setw(12) << pcb->registers.bp << "\n";
    
    ss << "Flags: Z=" << pcb->registers.flags.zero
       << " C=" << pcb->registers.flags.carry
       << " N=" << pcb->registers.flags.negative
       << " O=" << pcb->registers.flags.overflow << "\n";
    
    return ss.str();
}

std::string ProcessSimulator::dumpMemory(uint32_t pid, SegmentType segment,
                                        uint32_t start, uint32_t length) const {
    auto mem = getProcessMemory(pid);
    if (!mem) {
        return "Process not found";
    }
    
    std::stringstream ss;
    ss << "=== Memory Dump ===\n";
    ss << std::hex << std::setfill('0');
    
    for (uint32_t i = 0; i < length; i += 16) {
        ss << std::setw(8) << (start + i) << ": ";
        
        for (uint32_t j = 0; j < 16 && (i + j) < length; ++j) {
            try {
                uint8_t byte = mem->readByte(segment, start + i + j);
                ss << std::setw(2) << static_cast<int>(byte) << " ";
            } catch (...) {
                ss << "?? ";
            }
        }
        ss << "\n";
    }
    
    return ss.str();
}

std::string ProcessSimulator::getStackTrace(uint32_t pid) const {
    auto pcb = getProcess(pid);
    auto mem = getProcessMemory(pid);
    
    if (!pcb || !mem) {
        return "Process not found";
    }
    
    std::stringstream ss;
    ss << "=== Stack Trace for Process " << pid << " ===\n";
    ss << "SP: " << pcb->registers.sp << "\n";
    
    uint32_t sp = pcb->registers.sp;
    int frameCount = 0;
    
    while (sp < STACK_SIZE - 4 && frameCount < 10) {
        int32_t value = mem->readWord(SegmentType::STACK, sp);
        ss << "  [" << std::setw(4) << sp << "]: " << value << "\n";
        sp += 4;
        frameCount++;
    }
    
    return ss.str();
}

std::string ProcessSimulator::disassemble(uint32_t pid, uint32_t start, uint32_t count) const {
    auto mem = getProcessMemory(pid);
    auto pcb = getProcess(pid);
    
    if (!mem || !pcb) {
        return "Process not found";
    }
    
    std::stringstream ss;
    ss << "=== Disassembly for Process " << pid << " ===\n";
    
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t addr = start + i * INSTRUCTION_SIZE;
        if (addr + INSTRUCTION_SIZE > mem->codeSegment.size()) {
            break;
        }
        
        Instruction inst = Instruction::decode(mem->codeSegment.data() + addr);
        
        // PC marker
        ss << (addr == pcb->registers.pc ? ">>>" : "   ");
        ss << " " << std::setw(4) << std::setfill('0') << std::hex << addr << ": ";
        ss << std::dec << inst.toString() << "\n";
    }
    
    return ss.str();
}

uint64_t ProcessSimulator::getTotalInstructions() const {
    return m_impl->totalInstructions;
}

uint64_t ProcessSimulator::getTotalCycles() const {
    return m_impl->totalCycles;
}

void ProcessSimulator::setConfig(const SimulatorConfig& config) {
    m_impl->config = config;
}

SimulatorConfig ProcessSimulator::getConfig() const {
    return m_impl->config;
}

} // namespace process
} // namespace checkpoint
