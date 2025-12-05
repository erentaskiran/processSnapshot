#pragma once

#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <map>
#include "process/process_types.hpp"
#include "process/instruction_set.hpp"
#include "state/state_manager.hpp"
#include "logger/operation_logger.hpp"

namespace checkpoint {
namespace process {

// ============================================================================
// Execution Result - Çalıştırma Sonucu
// ============================================================================
struct ExecutionResult {
    bool success;
    bool halted;
    bool breakpoint;
    uint32_t instructionsExecuted;
    std::string errorMessage;
    
    ExecutionResult() : success(true), halted(false), breakpoint(false), 
                       instructionsExecuted(0) {}
};

// ============================================================================
// Callback Türleri
// ============================================================================
using InstructionCallback = std::function<void(const Instruction&, const PCB&)>;
using StateChangeCallback = std::function<void(ProcessState oldState, ProcessState newState)>;
using BreakpointCallback = std::function<bool(uint32_t address)>;
using SyscallHandler = std::function<int32_t(int32_t syscallNum, const RegisterSet& regs)>;
using IOHandler = std::function<int32_t(uint8_t port, bool isRead, int32_t value)>;

// ============================================================================
// Simulator Config - Simülatör Ayarları
// ============================================================================
struct SimulatorConfig {
    bool enableLogging;
    bool enableBreakpoints;
    bool verboseMode;
    uint32_t maxInstructions;   // Sonsuz döngü koruması
    uint32_t stackSize;
    uint32_t heapSize;
    
    SimulatorConfig() 
        : enableLogging(true), enableBreakpoints(true), 
          verboseMode(false), maxInstructions(100000),
          stackSize(STACK_SIZE), heapSize(HEAP_SIZE) {}
};

// ============================================================================
// Process Simulator - Ana Simülatör Sınıfı
// ============================================================================
class ProcessSimulator {
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    
public:
    ProcessSimulator();
    explicit ProcessSimulator(const SimulatorConfig& config);
    ~ProcessSimulator();
    
    // Move semantics
    ProcessSimulator(ProcessSimulator&&) noexcept;
    ProcessSimulator& operator=(ProcessSimulator&&) noexcept;
    
    // ========================================================================
    // Process Yönetimi
    // ========================================================================
    
    // Yeni process oluştur
    uint32_t createProcess(const std::string& name, uint8_t priority = 128);
    
    // Program yükle
    bool loadProgram(uint32_t pid, const std::vector<Instruction>& instructions);
    bool loadProgram(uint32_t pid, const std::vector<uint8_t>& binary);
    
    // Process durumunu al
    const PCB* getProcess(uint32_t pid) const;
    PCB* getProcess(uint32_t pid);
    
    // Process memory'sine eriş
    const ProcessMemory* getProcessMemory(uint32_t pid) const;
    ProcessMemory* getProcessMemory(uint32_t pid);
    
    // Tüm process'leri listele
    std::vector<uint32_t> listProcesses() const;
    std::vector<const PCB*> listAllPCBs() const;
    
    // Process sonlandır
    bool terminateProcess(uint32_t pid);
    
    // ========================================================================
    // Execution - Çalıştırma
    // ========================================================================
    
    // Tek instruction çalıştır
    ExecutionResult step(uint32_t pid);
    
    // N instruction çalıştır
    ExecutionResult run(uint32_t pid, uint32_t maxInstructions = 0);
    
    // Halt'a kadar çalıştır
    ExecutionResult runUntilHalt(uint32_t pid);
    
    // Belirli adrese kadar çalıştır
    ExecutionResult runUntilAddress(uint32_t pid, uint32_t address);
    
    // Mevcut instruction'ı al
    Instruction getCurrentInstruction(uint32_t pid) const;
    
    // ========================================================================
    // State Management - Durum Yönetimi
    // ========================================================================
    
    // Process state'ini değiştir
    bool setProcessState(uint32_t pid, ProcessState newState);
    
    // Register değeri al/ayarla
    int32_t getRegister(uint32_t pid, Register reg) const;
    bool setRegister(uint32_t pid, Register reg, int32_t value);
    
    // PC'yi al/ayarla
    uint32_t getPC(uint32_t pid) const;
    bool setPC(uint32_t pid, uint32_t value);
    
    // Memory oku/yaz
    int32_t readMemory(uint32_t pid, SegmentType segment, uint32_t offset) const;
    bool writeMemory(uint32_t pid, SegmentType segment, uint32_t offset, int32_t value);
    
    // ========================================================================
    // Checkpoint & Rollback
    // ========================================================================
    
    // Process snapshot al
    ProcessSnapshot takeSnapshot(uint32_t pid, const std::string& name = "") const;
    
    // Snapshot'tan geri yükle
    bool restoreFromSnapshot(uint32_t pid, const ProcessSnapshot& snapshot);
    
    // Checkpoint kaydet (dosyaya)
    bool saveCheckpoint(uint32_t pid, const std::string& name, 
                       const std::string& path = "");
    
    // Checkpoint yükle (dosyadan)
    bool loadCheckpoint(uint32_t pid, const std::string& path);
    
    // StateManager entegrasyonu
    CheckpointId createCheckpoint(uint32_t pid, const std::string& name,
                                  StateManager& stateManager);
    bool rollbackToCheckpoint(uint32_t pid, CheckpointId checkpointId,
                             StateManager& stateManager);
    
    // ========================================================================
    // Breakpoints
    // ========================================================================
    
    bool addBreakpoint(uint32_t pid, uint32_t address);
    bool removeBreakpoint(uint32_t pid, uint32_t address);
    void clearBreakpoints(uint32_t pid);
    std::vector<uint32_t> getBreakpoints(uint32_t pid) const;
    
    // ========================================================================
    // Callbacks & Handlers
    // ========================================================================
    
    void setInstructionCallback(InstructionCallback callback);
    void setStateChangeCallback(StateChangeCallback callback);
    void setSyscallHandler(SyscallHandler handler);
    void setIOHandler(IOHandler handler);
    
    // ========================================================================
    // Debug & Info
    // ========================================================================
    
    // Register durumunu yazdır
    std::string dumpRegisters(uint32_t pid) const;
    
    // Memory dump
    std::string dumpMemory(uint32_t pid, SegmentType segment, 
                          uint32_t start, uint32_t length) const;
    
    // Stack trace
    std::string getStackTrace(uint32_t pid) const;
    
    // Disassembly
    std::string disassemble(uint32_t pid, uint32_t start = 0, uint32_t count = 10) const;
    
    // İstatistikler
    uint64_t getTotalInstructions() const;
    uint64_t getTotalCycles() const;
    
    // ========================================================================
    // Configuration
    // ========================================================================
    
    void setConfig(const SimulatorConfig& config);
    SimulatorConfig getConfig() const;
    
private:
    // İç metodlar
    ExecutionResult executeInstruction(uint32_t pid, const Instruction& inst);
    void updateFlags(PCB& pcb, int64_t result);
};

// ============================================================================
// ProcessSimulator Builder - Kolay oluşturma için
// ============================================================================
class ProcessSimulatorBuilder {
private:
    SimulatorConfig m_config;
    
public:
    ProcessSimulatorBuilder& withLogging(bool enable) {
        m_config.enableLogging = enable;
        return *this;
    }
    
    ProcessSimulatorBuilder& withBreakpoints(bool enable) {
        m_config.enableBreakpoints = enable;
        return *this;
    }
    
    ProcessSimulatorBuilder& withVerbose(bool enable) {
        m_config.verboseMode = enable;
        return *this;
    }
    
    ProcessSimulatorBuilder& withMaxInstructions(uint32_t max) {
        m_config.maxInstructions = max;
        return *this;
    }
    
    ProcessSimulatorBuilder& withStackSize(uint32_t size) {
        m_config.stackSize = size;
        return *this;
    }
    
    ProcessSimulatorBuilder& withHeapSize(uint32_t size) {
        m_config.heapSize = size;
        return *this;
    }
    
    std::unique_ptr<ProcessSimulator> build() {
        return std::make_unique<ProcessSimulator>(m_config);
    }
};

} // namespace process
} // namespace checkpoint
