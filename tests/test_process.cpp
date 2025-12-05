#include <gtest/gtest.h>
#include "process/process_simulator.hpp"
#include "process/instruction_set.hpp"
#include "process/process_types.hpp"
#include "state/state_manager.hpp"

using namespace checkpoint;
using namespace checkpoint::process;

// ============================================================================
// Process Types Tests
// ============================================================================

class ProcessTypesTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(ProcessTypesTest, RegisterSetInit) {
    RegisterSet regs;
    
    // Tüm general registerlar 0 olmalı
    for (size_t i = 0; i < NUM_GENERAL_REGISTERS; ++i) {
        EXPECT_EQ(regs.general[i], 0);
    }
    
    // PC 0, SP ve BP stack'in sonunda olmalı
    EXPECT_EQ(regs.pc, 0u);
    EXPECT_EQ(regs.sp, STACK_SIZE - 1);
    EXPECT_EQ(regs.bp, STACK_SIZE - 1);
    
    // Flags temiz olmalı
    EXPECT_FALSE(regs.flags.zero);
    EXPECT_FALSE(regs.flags.carry);
    EXPECT_FALSE(regs.flags.negative);
    EXPECT_FALSE(regs.flags.overflow);
}

TEST_F(ProcessTypesTest, RegisterSetSerialize) {
    RegisterSet regs;
    regs.general[0] = 42;
    regs.general[5] = -100;
    regs.pc = 128;
    regs.sp = 1000;
    regs.flags.zero = true;
    regs.flags.negative = true;
    
    StateData data = regs.serialize();
    EXPECT_GT(data.size(), 0u);
    
    size_t offset = 0;
    RegisterSet restored = RegisterSet::deserialize(data, offset);
    
    EXPECT_EQ(restored.general[0], 42);
    EXPECT_EQ(restored.general[5], -100);
    EXPECT_EQ(restored.pc, 128u);
    EXPECT_EQ(restored.sp, 1000u);
    EXPECT_TRUE(restored.flags.zero);
    EXPECT_TRUE(restored.flags.negative);
}

TEST_F(ProcessTypesTest, CPUFlagsOperations) {
    CPUFlags flags;
    flags.zero = true;
    flags.carry = true;
    
    uint8_t byte = flags.toByte();
    
    CPUFlags restored;
    restored.fromByte(byte);
    
    EXPECT_TRUE(restored.zero);
    EXPECT_TRUE(restored.carry);
    EXPECT_FALSE(restored.negative);
    EXPECT_FALSE(restored.overflow);
}

TEST_F(ProcessTypesTest, PCBSerialize) {
    PCB pcb;
    pcb.pid = 1;
    pcb.parentPid = 0;
    pcb.name = "TestProcess";
    pcb.state = ProcessState::RUNNING;
    pcb.priority = 50;
    pcb.registers.general[0] = 100;
    pcb.cpuTimeUsed = 1000;
    
    StateData data = pcb.serialize();
    EXPECT_GT(data.size(), 0u);
    
    size_t offset = 0;
    PCB restored = PCB::deserialize(data, offset);
    
    EXPECT_EQ(restored.pid, 1u);
    EXPECT_EQ(restored.parentPid, 0u);
    EXPECT_EQ(restored.name, "TestProcess");
    EXPECT_EQ(restored.state, ProcessState::RUNNING);
    EXPECT_EQ(restored.priority, 50);
    EXPECT_EQ(restored.registers.general[0], 100);
    EXPECT_EQ(restored.cpuTimeUsed, 1000u);
}

TEST_F(ProcessTypesTest, ProcessMemoryReadWrite) {
    ProcessMemory memory;
    
    // Byte write/read
    memory.writeByte(SegmentType::DATA, 0, 0xAB);
    EXPECT_EQ(memory.readByte(SegmentType::DATA, 0), 0xAB);
    
    // Word write/read
    memory.writeWord(SegmentType::DATA, 4, 0x12345678);
    EXPECT_EQ(memory.readWord(SegmentType::DATA, 4), 0x12345678);
    
    // Negative value
    memory.writeWord(SegmentType::DATA, 8, -42);
    EXPECT_EQ(memory.readWord(SegmentType::DATA, 8), -42);
}

TEST_F(ProcessTypesTest, ProcessMemoryStack) {
    ProcessMemory memory;
    uint32_t sp = STACK_SIZE - 1;
    
    memory.push(100, sp);
    memory.push(200, sp);
    memory.push(300, sp);
    
    EXPECT_EQ(memory.pop(sp), 300);
    EXPECT_EQ(memory.pop(sp), 200);
    EXPECT_EQ(memory.pop(sp), 100);
}

TEST_F(ProcessTypesTest, ProcessMemorySerialize) {
    ProcessMemory memory;
    memory.writeWord(SegmentType::DATA, 0, 42);
    memory.writeWord(SegmentType::HEAP, 100, 999);
    
    StateData data = memory.serialize();
    EXPECT_GT(data.size(), 0u);
    
    size_t offset = 0;
    ProcessMemory restored = ProcessMemory::deserialize(data, offset);
    
    EXPECT_EQ(restored.readWord(SegmentType::DATA, 0), 42);
    EXPECT_EQ(restored.readWord(SegmentType::HEAP, 100), 999);
}

TEST_F(ProcessTypesTest, ProcessSnapshotSerialize) {
    ProcessSnapshot snapshot;
    snapshot.pcb.pid = 1;
    snapshot.pcb.name = "SnapshotTest";
    snapshot.pcb.registers.general[0] = 42;
    snapshot.memory.writeWord(SegmentType::DATA, 0, 100);
    snapshot.checkpointName = "TestCheckpoint";
    snapshot.timestamp = 123456789;
    
    StateData data = snapshot.serialize();
    ProcessSnapshot restored = ProcessSnapshot::deserialize(data);
    
    EXPECT_EQ(restored.pcb.pid, 1u);
    EXPECT_EQ(restored.pcb.name, "SnapshotTest");
    EXPECT_EQ(restored.pcb.registers.general[0], 42);
    EXPECT_EQ(restored.memory.readWord(SegmentType::DATA, 0), 100);
    EXPECT_EQ(restored.checkpointName, "TestCheckpoint");
    EXPECT_EQ(restored.timestamp, 123456789u);
}

// ============================================================================
// Instruction Set Tests
// ============================================================================

class InstructionSetTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(InstructionSetTest, InstructionEncodeDecode) {
    Instruction inst(Opcode::ADD, 0, 1, 2);
    
    auto encoded = inst.encode();
    EXPECT_EQ(encoded.size(), INSTRUCTION_SIZE);
    
    Instruction decoded = Instruction::decode(encoded.data());
    EXPECT_EQ(decoded.opcode, Opcode::ADD);
    EXPECT_EQ(decoded.dest, 0);
    EXPECT_EQ(decoded.src1, 1);
    EXPECT_EQ(decoded.src2, 2);
}

TEST_F(InstructionSetTest, InstructionWithImmediate) {
    Instruction inst(Opcode::LOAD_IMM, 0, 12345);
    
    auto encoded = inst.encode();
    Instruction decoded = Instruction::decode(encoded.data());
    
    EXPECT_EQ(decoded.opcode, Opcode::LOAD_IMM);
    EXPECT_EQ(decoded.dest, 0);
    EXPECT_EQ(decoded.immediate, 12345);
}

TEST_F(InstructionSetTest, InstructionToString) {
    Instruction load(Opcode::LOAD_IMM, 0, 42);
    EXPECT_TRUE(load.toString().find("LOAD") != std::string::npos);
    
    Instruction add(Opcode::ADD, 0, 1, 2);
    EXPECT_TRUE(add.toString().find("ADD") != std::string::npos);
}

TEST_F(InstructionSetTest, OpcodeInfo) {
    auto addInfo = getOpcodeInfo(Opcode::ADD);
    EXPECT_EQ(addInfo.mnemonic, "ADD");
    EXPECT_TRUE(addInfo.modifiesFlags);
    
    auto jmpInfo = getOpcodeInfo(Opcode::JMP);
    EXPECT_EQ(jmpInfo.mnemonic, "JMP");
    EXPECT_FALSE(jmpInfo.modifiesFlags);
}

TEST_F(InstructionSetTest, StringToOpcode) {
    EXPECT_EQ(stringToOpcode("ADD"), Opcode::ADD);
    EXPECT_EQ(stringToOpcode("add"), Opcode::ADD);
    EXPECT_EQ(stringToOpcode("JMP"), Opcode::JMP);
    EXPECT_EQ(stringToOpcode("HALT"), Opcode::HALT);
}

TEST_F(InstructionSetTest, AssemblerToBinary) {
    std::vector<Instruction> instructions = {
        Instruction(Opcode::LOAD_IMM, 0, 10),
        Instruction(Opcode::INC, 0),
        Instruction(Opcode::HALT)
    };
    
    auto binary = SimpleAssembler::toBinary(instructions);
    EXPECT_EQ(binary.size(), instructions.size() * INSTRUCTION_SIZE);
    
    auto restored = SimpleAssembler::fromBinary(binary);
    EXPECT_EQ(restored.size(), instructions.size());
    EXPECT_EQ(restored[0].opcode, Opcode::LOAD_IMM);
    EXPECT_EQ(restored[1].opcode, Opcode::INC);
    EXPECT_EQ(restored[2].opcode, Opcode::HALT);
}

// ============================================================================
// Process Simulator Tests
// ============================================================================

class ProcessSimulatorTest : public ::testing::Test {
protected:
    ProcessSimulator simulator;
    
    void SetUp() override {
        SimulatorConfig config;
        config.enableLogging = false;
        config.verboseMode = false;
        simulator.setConfig(config);
    }
    
    void TearDown() override {}
};

TEST_F(ProcessSimulatorTest, CreateProcess) {
    uint32_t pid = simulator.createProcess("TestProcess", 100);
    EXPECT_GT(pid, 0u);
    
    auto pcb = simulator.getProcess(pid);
    ASSERT_NE(pcb, nullptr);
    EXPECT_EQ(pcb->pid, pid);
    EXPECT_EQ(pcb->name, "TestProcess");
    EXPECT_EQ(pcb->priority, 100);
    EXPECT_EQ(pcb->state, ProcessState::NEW);
}

TEST_F(ProcessSimulatorTest, LoadProgram) {
    uint32_t pid = simulator.createProcess("Test");
    
    std::vector<Instruction> program = {
        Instruction(Opcode::LOAD_IMM, 0, 42),
        Instruction(Opcode::HALT)
    };
    
    bool loaded = simulator.loadProgram(pid, program);
    EXPECT_TRUE(loaded);
    
    auto pcb = simulator.getProcess(pid);
    EXPECT_EQ(pcb->state, ProcessState::READY);
}

TEST_F(ProcessSimulatorTest, StepExecution) {
    uint32_t pid = simulator.createProcess("Test");
    
    std::vector<Instruction> program = {
        Instruction(Opcode::LOAD_IMM, 0, 42),
        Instruction(Opcode::INC, 0),
        Instruction(Opcode::HALT)
    };
    
    simulator.loadProgram(pid, program);
    
    // Step 1: LOAD_IMM
    auto result = simulator.step(pid);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(simulator.getRegister(pid, Register::R0), 42);
    
    // Step 2: INC
    result = simulator.step(pid);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(simulator.getRegister(pid, Register::R0), 43);
    
    // Step 3: HALT
    result = simulator.step(pid);
    EXPECT_TRUE(result.halted);
}

TEST_F(ProcessSimulatorTest, ArithmeticOperations) {
    uint32_t pid = simulator.createProcess("Test");
    
    std::vector<Instruction> program = {
        Instruction(Opcode::LOAD_IMM, 0, 10),   // R0 = 10
        Instruction(Opcode::LOAD_IMM, 1, 5),    // R1 = 5
        Instruction(Opcode::ADD, 2, 0, 1),      // R2 = R0 + R1 = 15
        Instruction(Opcode::SUB, 3, 0, 1),      // R3 = R0 - R1 = 5
        Instruction(Opcode::MUL, 4, 0, 1),      // R4 = R0 * R1 = 50
        Instruction(Opcode::HALT)
    };
    
    simulator.loadProgram(pid, program);
    simulator.runUntilHalt(pid);
    
    EXPECT_EQ(simulator.getRegister(pid, Register::R0), 10);
    EXPECT_EQ(simulator.getRegister(pid, Register::R1), 5);
    EXPECT_EQ(simulator.getRegister(pid, Register::R2), 15);
    EXPECT_EQ(simulator.getRegister(pid, Register::R3), 5);
    EXPECT_EQ(simulator.getRegister(pid, Register::R4), 50);
}

TEST_F(ProcessSimulatorTest, JumpInstructions) {
    uint32_t pid = simulator.createProcess("Test");
    
    // Simple loop: R0 = 0, while R0 < 3: R0++
    std::vector<Instruction> program;
    program.push_back(Instruction(Opcode::LOAD_IMM, 0, 0));      // 0: R0 = 0
    program.push_back(Instruction(Opcode::LOAD_IMM, 1, 3));      // 8: R1 = 3 (limit)
    
    // loop (address 16):
    // CMP R0, R1 - dest=0, src1=1 (use 2-register constructor)
    Instruction cmp(Opcode::CMP);
    cmp.dest = 0;
    cmp.src1 = 1;
    program.push_back(cmp);                                       // 16: CMP R0, R1
    
    Instruction jge(Opcode::JGE);                                // 24: JGE end (48)
    jge.immediate = 48;
    program.push_back(jge);
    
    program.push_back(Instruction(Opcode::INC, 0));              // 32: INC R0
    
    Instruction jmp(Opcode::JMP);                                // 40: JMP loop (16)
    jmp.immediate = 16;
    program.push_back(jmp);
    
    program.push_back(Instruction(Opcode::HALT));                // 48: HALT
    
    simulator.loadProgram(pid, program);
    
    // Step-by-step debug
    // LOAD R0, 0 -> PC=8
    simulator.step(pid);
    EXPECT_EQ(simulator.getRegister(pid, Register::R0), 0);
    
    // LOAD R1, 3 -> PC=16
    simulator.step(pid);
    EXPECT_EQ(simulator.getRegister(pid, Register::R1), 3);
    
    // İlk döngü: R0=0, CMP 0-3=-3 (negative), JGE atlamaz
    simulator.step(pid); // CMP
    simulator.step(pid); // JGE (atlamaz çünkü negative=true)
    EXPECT_EQ(simulator.getPC(pid), 32u); // INC'e gelmeli
    
    simulator.step(pid); // INC R0 -> R0=1
    EXPECT_EQ(simulator.getRegister(pid, Register::R0), 1);
    
    // Geri kalan döngüleri çalıştır
    auto result = simulator.runUntilHalt(pid);
    EXPECT_TRUE(result.halted);
    
    // R0 en az 3 olmalı (döngüden çıktığında)
    EXPECT_GE(simulator.getRegister(pid, Register::R0), 3);
}

TEST_F(ProcessSimulatorTest, StackOperations) {
    uint32_t pid = simulator.createProcess("Test");
    
    std::vector<Instruction> program = {
        Instruction(Opcode::LOAD_IMM, 0, 100),
        Instruction(Opcode::LOAD_IMM, 1, 200),
        Instruction(Opcode::PUSH, 0),
        Instruction(Opcode::PUSH, 1),
        Instruction(Opcode::POP, 2),       // R2 = 200
        Instruction(Opcode::POP, 3),       // R3 = 100
        Instruction(Opcode::HALT)
    };
    
    simulator.loadProgram(pid, program);
    simulator.runUntilHalt(pid);
    
    EXPECT_EQ(simulator.getRegister(pid, Register::R2), 200);
    EXPECT_EQ(simulator.getRegister(pid, Register::R3), 100);
}

TEST_F(ProcessSimulatorTest, MemoryOperations) {
    uint32_t pid = simulator.createProcess("Test");
    
    std::vector<Instruction> program;
    
    program.push_back(Instruction(Opcode::LOAD_IMM, 0, 42));
    
    Instruction store(Opcode::STORE, 0);
    store.immediate = 0;
    program.push_back(store);
    
    Instruction load(Opcode::LOAD_MEM, 1);
    load.immediate = 0;
    program.push_back(load);
    
    program.push_back(Instruction(Opcode::HALT));
    
    simulator.loadProgram(pid, program);
    simulator.runUntilHalt(pid);
    
    EXPECT_EQ(simulator.getRegister(pid, Register::R1), 42);
}

// ============================================================================
// Checkpoint & Rollback Tests
// ============================================================================

class CheckpointRollbackTest : public ::testing::Test {
protected:
    ProcessSimulator simulator;
    
    void SetUp() override {
        SimulatorConfig config;
        config.enableLogging = false;
        config.verboseMode = false;
        simulator.setConfig(config);
    }
};

TEST_F(CheckpointRollbackTest, TakeSnapshot) {
    uint32_t pid = simulator.createProcess("Test");
    
    std::vector<Instruction> program = {
        Instruction(Opcode::LOAD_IMM, 0, 100),
        Instruction(Opcode::HALT)
    };
    simulator.loadProgram(pid, program);
    simulator.step(pid);  // R0 = 100
    
    ProcessSnapshot snapshot = simulator.takeSnapshot(pid, "TestSnapshot");
    
    EXPECT_EQ(snapshot.pcb.pid, pid);
    EXPECT_EQ(snapshot.pcb.registers.general[0], 100);
    EXPECT_EQ(snapshot.checkpointName, "TestSnapshot");
}

TEST_F(CheckpointRollbackTest, RestoreFromSnapshot) {
    uint32_t pid = simulator.createProcess("Test");
    
    std::vector<Instruction> program = {
        Instruction(Opcode::LOAD_IMM, 0, 100),
        Instruction(Opcode::INC, 0),
        Instruction(Opcode::INC, 0),
        Instruction(Opcode::HALT)
    };
    simulator.loadProgram(pid, program);
    
    // R0 = 100
    simulator.step(pid);
    ProcessSnapshot snapshot = simulator.takeSnapshot(pid, "Before");
    EXPECT_EQ(simulator.getRegister(pid, Register::R0), 100);
    
    // R0 = 101, 102
    simulator.step(pid);
    simulator.step(pid);
    EXPECT_EQ(simulator.getRegister(pid, Register::R0), 102);
    
    // Rollback
    bool restored = simulator.restoreFromSnapshot(pid, snapshot);
    EXPECT_TRUE(restored);
    EXPECT_EQ(simulator.getRegister(pid, Register::R0), 100);
}

TEST_F(CheckpointRollbackTest, StateManagerIntegration) {
    uint32_t pid = simulator.createProcess("Test");
    
    std::vector<Instruction> program = {
        Instruction(Opcode::LOAD_IMM, 0, 42),
        Instruction(Opcode::HALT)
    };
    simulator.loadProgram(pid, program);
    simulator.step(pid);
    
    StateManager stateManager("test_process_checkpoints/");
    
    CheckpointId cpId = simulator.createCheckpoint(pid, "IntegrationTest", stateManager);
    EXPECT_NE(cpId, 0u);
    
    // Değişiklik yap
    simulator.setRegister(pid, Register::R0, 999);
    EXPECT_EQ(simulator.getRegister(pid, Register::R0), 999);
    
    // Rollback
    bool restored = simulator.rollbackToCheckpoint(pid, cpId, stateManager);
    EXPECT_TRUE(restored);
    EXPECT_EQ(simulator.getRegister(pid, Register::R0), 42);
    
    // Cleanup
    std::filesystem::remove_all("test_process_checkpoints/");
}

TEST_F(CheckpointRollbackTest, MultipleCheckpoints) {
    uint32_t pid = simulator.createProcess("Test");
    
    std::vector<Instruction> program = {
        Instruction(Opcode::LOAD_IMM, 0, 0),
        Instruction(Opcode::INC, 0),
        Instruction(Opcode::INC, 0),
        Instruction(Opcode::INC, 0),
        Instruction(Opcode::INC, 0),
        Instruction(Opcode::HALT)
    };
    simulator.loadProgram(pid, program);
    
    simulator.step(pid);  // R0 = 0
    ProcessSnapshot snap1 = simulator.takeSnapshot(pid, "Snap1");
    
    simulator.step(pid);  // R0 = 1
    ProcessSnapshot snap2 = simulator.takeSnapshot(pid, "Snap2");
    
    simulator.step(pid);  // R0 = 2
    simulator.step(pid);  // R0 = 3
    EXPECT_EQ(simulator.getRegister(pid, Register::R0), 3);
    
    // Snap1'e dön (R0 = 0)
    simulator.restoreFromSnapshot(pid, snap1);
    EXPECT_EQ(simulator.getRegister(pid, Register::R0), 0);
    
    // Snap2'ye dön (R0 = 1)
    simulator.restoreFromSnapshot(pid, snap2);
    EXPECT_EQ(simulator.getRegister(pid, Register::R0), 1);
}

TEST_F(CheckpointRollbackTest, MemoryRestoration) {
    uint32_t pid = simulator.createProcess("Test");
    
    // Memory'ye yaz
    simulator.writeMemory(pid, SegmentType::DATA, 0, 42);
    simulator.writeMemory(pid, SegmentType::DATA, 4, 100);
    
    ProcessSnapshot snapshot = simulator.takeSnapshot(pid, "MemSnap");
    
    // Memory'yi değiştir
    simulator.writeMemory(pid, SegmentType::DATA, 0, 999);
    simulator.writeMemory(pid, SegmentType::DATA, 4, 888);
    
    EXPECT_EQ(simulator.readMemory(pid, SegmentType::DATA, 0), 999);
    EXPECT_EQ(simulator.readMemory(pid, SegmentType::DATA, 4), 888);
    
    // Restore
    simulator.restoreFromSnapshot(pid, snapshot);
    
    EXPECT_EQ(simulator.readMemory(pid, SegmentType::DATA, 0), 42);
    EXPECT_EQ(simulator.readMemory(pid, SegmentType::DATA, 4), 100);
}

// ============================================================================
// Process State Tests
// ============================================================================

class ProcessStateTest : public ::testing::Test {
protected:
    ProcessSimulator simulator;
    
    void SetUp() override {
        SimulatorConfig config;
        config.enableLogging = false;
        simulator.setConfig(config);
    }
};

TEST_F(ProcessStateTest, StateTransitions) {
    uint32_t pid = simulator.createProcess("Test");
    
    auto pcb = simulator.getProcess(pid);
    EXPECT_EQ(pcb->state, ProcessState::NEW);
    
    std::vector<Instruction> program = {
        Instruction(Opcode::NOP),
        Instruction(Opcode::HALT)
    };
    simulator.loadProgram(pid, program);
    
    pcb = simulator.getProcess(pid);
    EXPECT_EQ(pcb->state, ProcessState::READY);
    
    simulator.step(pid);
    pcb = simulator.getProcess(pid);
    EXPECT_EQ(pcb->state, ProcessState::RUNNING);
    
    simulator.step(pid);  // HALT
    pcb = simulator.getProcess(pid);
    EXPECT_EQ(pcb->state, ProcessState::TERMINATED);
}

TEST_F(ProcessStateTest, ProcessStateToString) {
    EXPECT_EQ(processStateToString(ProcessState::NEW), "NEW");
    EXPECT_EQ(processStateToString(ProcessState::READY), "READY");
    EXPECT_EQ(processStateToString(ProcessState::RUNNING), "RUNNING");
    EXPECT_EQ(processStateToString(ProcessState::WAITING), "WAITING");
    EXPECT_EQ(processStateToString(ProcessState::TERMINATED), "TERMINATED");
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
