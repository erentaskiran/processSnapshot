#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>

#include "process/process_simulator.hpp"
#include "process/instruction_set.hpp"
#include "state/state_manager.hpp"
#include "logger/operation_logger.hpp"

using namespace checkpoint;
using namespace checkpoint::process;

// ============================================================================
// Yardımcı Fonksiyonlar
// ============================================================================

void printHeader(const std::string& title) {
    std::cout << "\n";
    std::cout << "╔" << std::string(60, '═') << "╗\n";
    std::cout << "║" << std::setw(60) << std::left << (" " + title) << "║\n";
    std::cout << "╚" << std::string(60, '═') << "╝\n\n";
}

void printSubHeader(const std::string& title) {
    std::cout << "\n┌── " << title << " ──┐\n";
}

void printSeparator() {
    std::cout << "\n" << std::string(62, '─') << "\n";
}

void printPCB(const PCB& pcb) {
    std::cout << "┌─────────────────────────────────────────────────────────────┐\n";
    std::cout << "│ PCB - Process Control Block                                 │\n";
    std::cout << "├─────────────────────────────────────────────────────────────┤\n";
    std::cout << "│ PID: " << std::setw(10) << pcb.pid 
              << "  Parent PID: " << std::setw(10) << pcb.parentPid 
              << "                  │\n";
    std::cout << "│ Name: " << std::setw(20) << std::left << pcb.name 
              << std::right << "                                  │\n";
    std::cout << "│ State: " << std::setw(12) << processStateToString(pcb.state)
              << "  Priority: " << std::setw(3) << static_cast<int>(pcb.priority)
              << "                      │\n";
    std::cout << "├─────────────────────────────────────────────────────────────┤\n";
    std::cout << "│ CPU Context:                                                │\n";
    std::cout << "│   PC: " << std::setw(8) << pcb.registers.pc 
              << "  SP: " << std::setw(8) << pcb.registers.sp
              << "  BP: " << std::setw(8) << pcb.registers.bp << "        │\n";
    std::cout << "│   Flags: Z=" << pcb.registers.flags.zero 
              << " C=" << pcb.registers.flags.carry
              << " N=" << pcb.registers.flags.negative
              << " O=" << pcb.registers.flags.overflow << "                                │\n";
    std::cout << "├─────────────────────────────────────────────────────────────┤\n";
    std::cout << "│ Registers (R0-R7):                                          │\n";
    std::cout << "│   ";
    for (int i = 0; i < 8; ++i) {
        std::cout << "R" << i << "=" << std::setw(4) << pcb.registers.general[i] << " ";
    }
    std::cout << "  │\n";
    std::cout << "│ Registers (R8-R15):                                         │\n";
    std::cout << "│   ";
    for (int i = 8; i < 16; ++i) {
        std::cout << "R" << i << "=" << std::setw(3) << pcb.registers.general[i] << " ";
    }
    std::cout << " │\n";
    std::cout << "├─────────────────────────────────────────────────────────────┤\n";
    std::cout << "│ Statistics:                                                 │\n";
    std::cout << "│   CPU Time: " << std::setw(10) << pcb.cpuTimeUsed 
              << "  Instructions: " << std::setw(10) << pcb.instructionCount << "     │\n";
    std::cout << "│   Context Switches: " << std::setw(10) << pcb.contextSwitchCount 
              << "                            │\n";
    std::cout << "└─────────────────────────────────────────────────────────────┘\n";
}

void waitForUser() {
    std::cout << "\n[Enter'a basın devam etmek için...]";
    std::cin.get();
}

// ============================================================================
// Demo Programları
// ============================================================================

// Basit sayaç programı
std::vector<Instruction> createCounterProgram() {
    std::vector<Instruction> program;
    
    // R0 = counter (0'dan başla)
    // R1 = limit (10)
    // Her döngüde R0'ı artır ve yazdır
    
    program.push_back(Instruction(Opcode::LOAD_IMM, 0, 0));      // 0: R0 = 0
    program.push_back(Instruction(Opcode::LOAD_IMM, 1, 10));     // 8: R1 = 10
    
    // loop başlangıcı (address 16)
    program.push_back(Instruction(Opcode::CMP, 0, 1));           // 16: CMP R0, R1
    
    Instruction jge(Opcode::JGE);                                // 24: JGE end
    jge.immediate = 56;  // end adresine atla
    program.push_back(jge);
    
    program.push_back(Instruction(Opcode::PRINT, 0));            // 32: PRINT R0
    program.push_back(Instruction(Opcode::INC, 0));              // 40: INC R0
    
    Instruction jmp(Opcode::JMP);                                // 48: JMP loop
    jmp.immediate = 16;
    program.push_back(jmp);
    
    // end (address 56)
    program.push_back(Instruction(Opcode::HALT));                // 56: HALT
    
    return program;
}

// Faktöriyel hesaplayan program
std::vector<Instruction> createFactorialProgram(int n) {
    std::vector<Instruction> program;
    
    // R0 = sonuç (1'den başla)
    // R1 = counter (1'den başla)
    // R2 = n
    
    program.push_back(Instruction(Opcode::LOAD_IMM, 0, 1));      // 0: R0 = 1 (result)
    program.push_back(Instruction(Opcode::LOAD_IMM, 1, 1));      // 8: R1 = 1 (counter)
    program.push_back(Instruction(Opcode::LOAD_IMM, 2, n));      // 16: R2 = n
    
    // loop (address 24)
    program.push_back(Instruction(Opcode::CMP, 1, 2));           // 24: CMP R1, R2
    
    Instruction jg(Opcode::JG);                                  // 32: JG end
    jg.immediate = 72;
    program.push_back(jg);
    
    program.push_back(Instruction(Opcode::MUL, 0, 0, 1));        // 40: R0 = R0 * R1
    program.push_back(Instruction(Opcode::PRINT, 0));            // 48: PRINT R0
    program.push_back(Instruction(Opcode::INC, 1));              // 56: INC R1
    
    Instruction jmp(Opcode::JMP);                                // 64: JMP loop
    jmp.immediate = 24;
    program.push_back(jmp);
    
    // end (address 72)
    program.push_back(Instruction(Opcode::HALT));                // 72: HALT
    
    return program;
}

// Memory işlemleri gösteren program
std::vector<Instruction> createMemoryDemoProgram() {
    std::vector<Instruction> program;
    
    // Data segment'e değerler yaz ve oku
    // R0 = değer, R1 = adres
    
    program.push_back(Instruction(Opcode::LOAD_IMM, 0, 42));     // 0: R0 = 42
    
    Instruction store1(Opcode::STORE, 0);                        // 8: STORE R0, [0]
    store1.immediate = 0;
    program.push_back(store1);
    
    program.push_back(Instruction(Opcode::LOAD_IMM, 0, 100));    // 16: R0 = 100
    
    Instruction store2(Opcode::STORE, 0);                        // 24: STORE R0, [4]
    store2.immediate = 4;
    program.push_back(store2);
    
    Instruction load1(Opcode::LOAD_MEM, 1);                      // 32: LOAD R1, [0]
    load1.immediate = 0;
    program.push_back(load1);
    
    Instruction load2(Opcode::LOAD_MEM, 2);                      // 40: LOAD R2, [4]
    load2.immediate = 4;
    program.push_back(load2);
    
    program.push_back(Instruction(Opcode::ADD, 3, 1, 2));        // 48: R3 = R1 + R2
    program.push_back(Instruction(Opcode::PRINT, 3));            // 56: PRINT R3 (142)
    program.push_back(Instruction(Opcode::HALT));                // 64: HALT
    
    return program;
}

// ============================================================================
// Ana Demo
// ============================================================================

void runFullDemo() {
    printHeader("Process State Checkpoint & Rollback Demo");
    
    std::cout << "Bu demo, işletim sistemleri dersinde process yönetimi,\n";
    std::cout << "checkpoint ve rollback kavramlarını göstermektedir.\n";
    std::cout << "\nÖzellikler:\n";
    std::cout << "  • PCB (Process Control Block) yapısı\n";
    std::cout << "  • Register'lar (R0-R15, PC, SP, BP, Flags)\n";
    std::cout << "  • Memory segmentleri (Code, Data, Stack, Heap)\n";
    std::cout << "  • Instruction set (LOAD, STORE, ADD, JMP, vb.)\n";
    std::cout << "  • Checkpoint alma ve rollback yapma\n";
    
    waitForUser();
    
    // ========================================================================
    // Bölüm 1: Process Oluşturma
    // ========================================================================
    printHeader("Bölüm 1: Process Oluşturma");
    
    SimulatorConfig config;
    config.enableLogging = true;
    config.verboseMode = true;
    config.maxInstructions = 1000;
    
    ProcessSimulator simulator(config);
    
    std::cout << "Process simülatörü oluşturuldu.\n";
    std::cout << "Yeni bir process oluşturuluyor...\n\n";
    
    uint32_t pid = simulator.createProcess("DemoProcess", 100);
    std::cout << "Process oluşturuldu! PID: " << pid << "\n";
    
    auto pcb = simulator.getProcess(pid);
    if (pcb) {
        printPCB(*pcb);
    }
    
    waitForUser();
    
    // ========================================================================
    // Bölüm 2: Program Yükleme
    // ========================================================================
    printHeader("Bölüm 2: Program Yükleme");
    
    std::cout << "Faktöriyel hesaplayan program yükleniyor (5! hesaplayacak)...\n\n";
    
    auto program = createFactorialProgram(5);
    
    std::cout << "Program instruction'ları:\n";
    for (size_t i = 0; i < program.size(); ++i) {
        std::cout << "  " << std::setw(4) << (i * 8) << ": " 
                  << program[i].toString() << "\n";
    }
    
    simulator.loadProgram(pid, program);
    std::cout << "\nProgram yüklendi. Process READY durumunda.\n";
    
    pcb = simulator.getProcess(pid);
    if (pcb) {
        std::cout << "Process State: " << processStateToString(pcb->state) << "\n";
    }
    
    waitForUser();
    
    // ========================================================================
    // Bölüm 3: Step-by-Step Execution
    // ========================================================================
    printHeader("Bölüm 3: Adım Adım Çalıştırma");
    
    std::cout << "İlk 5 instruction step-by-step çalıştırılıyor...\n\n";
    
    for (int i = 0; i < 5; ++i) {
        auto currentInst = simulator.getCurrentInstruction(pid);
        pcb = simulator.getProcess(pid);
        
        std::cout << "Step " << (i + 1) << ": PC=" << pcb->registers.pc 
                  << " -> " << currentInst.toString() << "\n";
        
        simulator.step(pid);
        
        pcb = simulator.getProcess(pid);
        std::cout << "  R0=" << pcb->registers.general[0]
                  << " R1=" << pcb->registers.general[1]
                  << " R2=" << pcb->registers.general[2] << "\n\n";
    }
    
    waitForUser();
    
    // ========================================================================
    // Bölüm 4: Checkpoint Alma
    // ========================================================================
    printHeader("Bölüm 4: Checkpoint Alma");
    
    std::cout << "Şu anki process durumu için checkpoint alınıyor...\n\n";
    
    pcb = simulator.getProcess(pid);
    printPCB(*pcb);
    
    ProcessSnapshot checkpoint1 = simulator.takeSnapshot(pid, "AfterInit");
    
    std::cout << "\n✓ Checkpoint alındı: '" << checkpoint1.checkpointName << "'\n";
    std::cout << "  Timestamp: " << checkpoint1.timestamp << "\n";
    std::cout << "  PCB PID: " << checkpoint1.pcb.pid << "\n";
    std::cout << "  PCB State: " << processStateToString(checkpoint1.pcb.state) << "\n";
    std::cout << "  PC: " << checkpoint1.pcb.registers.pc << "\n";
    std::cout << "  R0: " << checkpoint1.pcb.registers.general[0] << "\n";
    
    waitForUser();
    
    // ========================================================================
    // Bölüm 5: Programı Sonuna Kadar Çalıştırma
    // ========================================================================
    printHeader("Bölüm 5: Programı Tamamlama");
    
    std::cout << "Program sonuna kadar çalıştırılıyor...\n\n";
    
    auto result = simulator.runUntilHalt(pid);
    
    std::cout << "Program tamamlandı!\n";
    std::cout << "  Instructions executed: " << result.instructionsExecuted << "\n";
    std::cout << "  Halted: " << (result.halted ? "Yes" : "No") << "\n";
    std::cout << "  Success: " << (result.success ? "Yes" : "No") << "\n\n";
    
    pcb = simulator.getProcess(pid);
    if (pcb) {
        std::cout << "Sonuç: 5! = " << pcb->registers.general[0] << "\n\n";
        printPCB(*pcb);
    }
    
    waitForUser();
    
    // ========================================================================
    // Bölüm 6: Rollback
    // ========================================================================
    printHeader("Bölüm 6: Rollback - Checkpoint'e Geri Dönme");
    
    std::cout << "Process terminated durumda. Checkpoint'e geri dönülüyor...\n\n";
    
    std::cout << "Geri dönülecek checkpoint:\n";
    std::cout << "  Name: " << checkpoint1.checkpointName << "\n";
    std::cout << "  PC: " << checkpoint1.pcb.registers.pc << "\n";
    std::cout << "  R0: " << checkpoint1.pcb.registers.general[0] << "\n";
    std::cout << "  State: " << processStateToString(checkpoint1.pcb.state) << "\n\n";
    
    bool restored = simulator.restoreFromSnapshot(pid, checkpoint1);
    
    if (restored) {
        std::cout << "✓ Rollback başarılı!\n\n";
        
        pcb = simulator.getProcess(pid);
        if (pcb) {
            printPCB(*pcb);
            
            std::cout << "\nProcess eski haline döndü ve çalışmaya devam edebilir.\n";
            std::cout << "PC: " << pcb->registers.pc << " (checkpoint'teki değer)\n";
            std::cout << "State: " << processStateToString(pcb->state) << "\n";
        }
    } else {
        std::cout << "✗ Rollback başarısız!\n";
    }
    
    waitForUser();
    
    // ========================================================================
    // Bölüm 7: StateManager Entegrasyonu
    // ========================================================================
    printHeader("Bölüm 7: StateManager ile Checkpoint");
    
    std::cout << "StateManager kullanarak checkpoint kaydediliyor...\n\n";
    
    StateManager stateManager("process_checkpoints/");
    
    // Process'i tekrar çalıştır
    std::cout << "Process tekrar birkaç adım çalıştırılıyor...\n";
    simulator.run(pid, 3);
    
    pcb = simulator.getProcess(pid);
    std::cout << "Güncel durum - R0: " << pcb->registers.general[0] 
              << ", PC: " << pcb->registers.pc << "\n\n";
    
    // StateManager ile checkpoint
    CheckpointId cpId = simulator.createCheckpoint(pid, "StateManagerCheckpoint", stateManager);
    
    if (cpId != 0) {
        std::cout << "✓ Checkpoint StateManager'a kaydedildi! ID: " << cpId << "\n";
        
        // Checkpointleri listele
        auto checkpoints = stateManager.listCheckpoints();
        std::cout << "\nMevcut checkpointler:\n";
        for (const auto& meta : checkpoints) {
            std::cout << "  - ID: " << meta.id << ", Name: " << meta.name 
                      << ", Size: " << meta.dataSize << " bytes\n";
        }
    }
    
    waitForUser();
    
    // ========================================================================
    // Bölüm 8: İstatistikler
    // ========================================================================
    printHeader("Bölüm 8: Simülasyon İstatistikleri");
    
    std::cout << "Toplam çalıştırılan instruction: " << simulator.getTotalInstructions() << "\n";
    std::cout << "Toplam CPU cycle: " << simulator.getTotalCycles() << "\n";
    
    pcb = simulator.getProcess(pid);
    if (pcb) {
        std::cout << "\nProcess '" << pcb->name << "' (PID: " << pcb->pid << "):\n";
        std::cout << "  CPU Time Used: " << pcb->cpuTimeUsed << " cycles\n";
        std::cout << "  Instructions Executed: " << pcb->instructionCount << "\n";
        std::cout << "  Context Switches: " << pcb->contextSwitchCount << "\n";
    }
    
    printSeparator();
    
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                    DEMO TAMAMLANDI!                           ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    std::cout << "Bu demoda gösterilen kavramlar:\n";
    std::cout << "  1. Process Control Block (PCB) yapısı\n";
    std::cout << "  2. Process state transitions (NEW → READY → RUNNING → TERMINATED)\n";
    std::cout << "  3. Register ve memory yönetimi\n";
    std::cout << "  4. Instruction fetch-decode-execute cycle\n";
    std::cout << "  5. Checkpoint alma (snapshot)\n";
    std::cout << "  6. Rollback (state restoration)\n";
    std::cout << "  7. StateManager entegrasyonu\n";
    std::cout << "\n";
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << R"(
    ╔═══════════════════════════════════════════════════════════════╗
    ║                                                               ║
    ║     🖥️  PROCESS STATE CHECKPOINT & ROLLBACK SYSTEM  🖥️        ║
    ║                                                               ║
    ║              İşletim Sistemleri Dersi Projesi                 ║
    ║                                                               ║
    ╚═══════════════════════════════════════════════════════════════╝
    )" << std::endl;
    
    std::cout << "Demo başlatılıyor...\n";
    std::cout << "[Enter'a basın başlamak için]";
    std::cin.get();
    
    try {
        runFullDemo();
    } catch (const std::exception& e) {
        std::cerr << "Hata: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
