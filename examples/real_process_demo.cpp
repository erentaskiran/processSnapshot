#include <iostream>
#include <iomanip>
#include <string>
#include <csignal>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <sys/wait.h>

#include "real_process/real_process_types.hpp"
#include "real_process/proc_reader.hpp"
#include "real_process/ptrace_controller.hpp"

using namespace checkpoint::real_process;

// ============================================================================
// Yardımcı Fonksiyonlar
// ============================================================================

void printHeader(const std::string& title) {
    std::cout << "\n";
    std::cout << "╔" << std::string(70, '═') << "╗\n";
    std::cout << "║" << std::setw(70) << std::left << (" " + title) << "║\n";
    std::cout << "╚" << std::string(70, '═') << "╝\n\n";
}

void printSubHeader(const std::string& title) {
    std::cout << "\n┌── " << title << " ──────────────────────────────────────────────┐\n";
}

void printProcessInfo(const RealProcessInfo& info) {
    std::cout << "┌─────────────────────────────────────────────────────────────────────┐\n";
    std::cout << "│ Process Information                                                 │\n";
    std::cout << "├─────────────────────────────────────────────────────────────────────┤\n";
    std::cout << "│ PID: " << std::setw(10) << info.pid 
              << "  PPID: " << std::setw(10) << info.ppid 
              << "  State: " << std::setw(12) << linuxStateToString(info.state) << " │\n";
    std::cout << "│ Name: " << std::setw(20) << std::left << info.name << std::right
              << "  Threads: " << std::setw(5) << info.numThreads << "                     │\n";
    std::cout << "├─────────────────────────────────────────────────────────────────────┤\n";
    std::cout << "│ Command: " << std::setw(60) << std::left 
              << (info.cmdline.size() > 57 ? info.cmdline.substr(0, 57) + "..." : info.cmdline)
              << std::right << "│\n";
    std::cout << "├─────────────────────────────────────────────────────────────────────┤\n";
    std::cout << "│ Memory:                                                             │\n";
    std::cout << "│   Virtual: " << std::setw(12) << formatMemorySize(info.vmSize)
              << "  Resident: " << std::setw(12) << formatMemorySize(info.vmRss) 
              << "             │\n";
    std::cout << "│   Peak: " << std::setw(12) << formatMemorySize(info.vmPeak)
              << "  Data: " << std::setw(12) << formatMemorySize(info.vmData) 
              << "                 │\n";
    std::cout << "├─────────────────────────────────────────────────────────────────────┤\n";
    std::cout << "│ User: " << std::setw(8) << info.uid 
              << "  Priority: " << std::setw(4) << info.priority
              << "  Nice: " << std::setw(4) << info.nice << "                       │\n";
    std::cout << "└─────────────────────────────────────────────────────────────────────┘\n";
}

void printRegisters(const LinuxRegisters& regs) {
    std::cout << "┌─────────────────────────────────────────────────────────────────────┐\n";
    std::cout << "│ CPU Registers (x86_64)                                              │\n";
    std::cout << "├─────────────────────────────────────────────────────────────────────┤\n";
    std::cout << "│ RIP: " << std::hex << std::setw(16) << std::setfill('0') << regs.rip 
              << "  RSP: " << std::setw(16) << regs.rsp << std::setfill(' ') << std::dec << "  │\n";
    std::cout << "│ RBP: " << std::hex << std::setw(16) << std::setfill('0') << regs.rbp
              << "  FLAGS: " << std::setw(16) << regs.eflags << std::setfill(' ') << std::dec << "│\n";
    std::cout << "├─────────────────────────────────────────────────────────────────────┤\n";
    std::cout << "│ RAX: " << std::hex << std::setw(16) << std::setfill('0') << regs.rax
              << "  RBX: " << std::setw(16) << regs.rbx << std::setfill(' ') << std::dec << "  │\n";
    std::cout << "│ RCX: " << std::hex << std::setw(16) << std::setfill('0') << regs.rcx
              << "  RDX: " << std::setw(16) << regs.rdx << std::setfill(' ') << std::dec << "  │\n";
    std::cout << "│ RSI: " << std::hex << std::setw(16) << std::setfill('0') << regs.rsi
              << "  RDI: " << std::setw(16) << regs.rdi << std::setfill(' ') << std::dec << "  │\n";
    std::cout << "├─────────────────────────────────────────────────────────────────────┤\n";
    std::cout << "│ R8:  " << std::hex << std::setw(16) << std::setfill('0') << regs.r8
              << "  R9:  " << std::setw(16) << regs.r9 << std::setfill(' ') << std::dec << "  │\n";
    std::cout << "│ R10: " << std::hex << std::setw(16) << std::setfill('0') << regs.r10
              << "  R11: " << std::setw(16) << regs.r11 << std::setfill(' ') << std::dec << "  │\n";
    std::cout << "│ R12: " << std::hex << std::setw(16) << std::setfill('0') << regs.r12
              << "  R13: " << std::setw(16) << regs.r13 << std::setfill(' ') << std::dec << "  │\n";
    std::cout << "│ R14: " << std::hex << std::setw(16) << std::setfill('0') << regs.r14
              << "  R15: " << std::setw(16) << regs.r15 << std::setfill(' ') << std::dec << "  │\n";
    std::cout << "└─────────────────────────────────────────────────────────────────────┘\n";
}

void printMemoryMaps(const std::vector<MemoryRegion>& maps, int maxLines = 10) {
    std::cout << "┌─────────────────────────────────────────────────────────────────────┐\n";
    std::cout << "│ Memory Map                                                          │\n";
    std::cout << "├─────────────────────────────────────────────────────────────────────┤\n";
    std::cout << "│ Address Range               Perms   Size       Path                 │\n";
    std::cout << "├─────────────────────────────────────────────────────────────────────┤\n";
    
    int count = 0;
    for (const auto& region : maps) {
        if (count >= maxLines) {
            std::cout << "│ ... and " << (maps.size() - maxLines) << " more regions"
                      << std::string(45, ' ') << "│\n";
            break;
        }
        
        std::string perms;
        perms += region.readable ? 'r' : '-';
        perms += region.writable ? 'w' : '-';
        perms += region.executable ? 'x' : '-';
        perms += region.isPrivate ? 'p' : 's';
        
        std::string path = region.pathname;
        if (path.size() > 25) {
            path = "..." + path.substr(path.size() - 22);
        }
        
        std::cout << "│ " << std::hex << std::setw(12) << std::setfill('0') << region.startAddr
                  << "-" << std::setw(12) << region.endAddr << std::setfill(' ') << std::dec
                  << " " << perms 
                  << " " << std::setw(10) << std::right << formatMemorySize(region.size())
                  << " " << std::setw(25) << std::left << path << std::right << "│\n";
        count++;
    }
    
    std::cout << "└─────────────────────────────────────────────────────────────────────┘\n";
}

void waitForUser() {
    std::cout << "\n[Enter'a basın devam etmek için...]";
    std::cin.get();
}

// ============================================================================
// Test Process - Fork ile basit bir child process oluştur
// ============================================================================

pid_t createTestProcess() {
    pid_t pid = fork();
    
    if (pid == 0) {
        // Child process
        int counter = 0;
        while (true) {
            counter++;
            // Basit bir iş yap
            volatile int x = counter * 2;
            (void)x;
            usleep(100000);  // 100ms
        }
        exit(0);
    }
    
    return pid;
}

// ============================================================================
// Demo 1: Process Listeleme ve Bilgi Okuma
// ============================================================================

void demoProcessListing() {
    printHeader("Demo 1: Process Listeleme ve Bilgi Okuma");
    
    ProcFSReader reader;
    
    std::cout << "Sistemdeki process'ler okunuyor...\n\n";
    
    // Kullanıcının process'lerini listele
    uid_t uid = getuid();
    auto pids = reader.listUserPids(uid);
    
    std::cout << "Sizin UID'niz: " << uid << "\n";
    std::cout << "Sizin process sayınız: " << pids.size() << "\n\n";
    
    // İlk 5 process'i göster
    std::cout << "İlk 5 process:\n";
    std::cout << "─────────────────────────────────────────────────────\n";
    std::cout << std::setw(8) << "PID" << std::setw(20) << "Name" 
              << std::setw(12) << "State" << std::setw(15) << "Memory" << "\n";
    std::cout << "─────────────────────────────────────────────────────\n";
    
    int count = 0;
    for (pid_t pid : pids) {
        if (count >= 5) break;
        
        auto info = reader.getProcessInfo(pid);
        if (info) {
            std::cout << std::setw(8) << info->pid
                      << std::setw(20) << (info->name.size() > 18 ? info->name.substr(0, 18) : info->name)
                      << std::setw(12) << linuxStateToString(info->state)
                      << std::setw(15) << formatMemorySize(info->vmRss) << "\n";
        }
        count++;
    }
    
    waitForUser();
}

// ============================================================================
// Demo 2: Belirli Bir Process'in Detaylı Bilgisi
// ============================================================================

void demoProcessDetails(pid_t targetPid) {
    printHeader("Demo 2: Process Detayları");
    
    ProcFSReader reader;
    
    std::cout << "PID " << targetPid << " için bilgiler okunuyor...\n\n";
    
    auto info = reader.getProcessInfo(targetPid);
    if (!info) {
        std::cout << "❌ Process bulunamadı!\n";
        return;
    }
    
    printProcessInfo(*info);
    
    // Memory maps
    printSubHeader("Memory Regions");
    auto maps = reader.getMemoryMaps(targetPid);
    printMemoryMaps(maps);
    
    // File descriptors
    printSubHeader("Open File Descriptors");
    auto fds = reader.getFileDescriptors(targetPid);
    std::cout << "Açık dosya sayısı: " << fds.size() << "\n";
    for (size_t i = 0; i < std::min(fds.size(), size_t(5)); ++i) {
        std::cout << "  fd " << fds[i].fd << " -> " << fds[i].path << "\n";
    }
    
    // Environment (ilk 3)
    printSubHeader("Environment Variables");
    auto env = reader.getEnvironment(targetPid);
    std::cout << "Toplam: " << env.size() << " değişken\n";
    for (size_t i = 0; i < std::min(env.size(), size_t(3)); ++i) {
        std::string var = env[i];
        if (var.size() > 60) var = var.substr(0, 60) + "...";
        std::cout << "  " << var << "\n";
    }
    
    waitForUser();
}

// ============================================================================
// Demo 3: Checkpoint Alma
// ============================================================================

void demoCheckpoint(pid_t targetPid, RealProcessCheckpoint& checkpoint) {
    printHeader("Demo 3: Process Checkpoint Alma");
    
    std::cout << "⚠️  DİKKAT: Bu işlem process'i geçici olarak durduracak!\n\n";
    std::cout << "PID " << targetPid << " için checkpoint alınıyor...\n\n";
    
    RealProcessCheckpointer checkpointer;
    
    // Progress callback
    checkpointer.setProgressCallback([](const std::string& stage, double progress) {
        std::cout << "\r[" << std::setw(3) << int(progress * 100) << "%] " << stage 
                  << std::string(30, ' ') << std::flush;
    });
    
    CheckpointOptions options;
    options.saveRegisters = true;
    options.saveMemory = true;
    options.saveFileDescriptors = true;
    options.saveEnvironment = true;
    options.dumpHeap = true;
    options.dumpStack = true;
    options.dumpAnonymous = true;
    options.skipReadOnly = true;
    
    auto result = checkpointer.createCheckpoint(targetPid, "test_checkpoint", options);
    std::cout << "\n\n";
    
    if (!result) {
        std::cout << "❌ Checkpoint başarısız: " << checkpointer.getLastError() << "\n";
        return;
    }
    
    checkpoint = *result;
    
    std::cout << "✅ Checkpoint başarılı!\n\n";
    
    std::cout << "┌─────────────────────────────────────────────────────────────────────┐\n";
    std::cout << "│ Checkpoint Özeti                                                    │\n";
    std::cout << "├─────────────────────────────────────────────────────────────────────┤\n";
    std::cout << "│ ID: " << std::setw(20) << checkpoint.checkpointId 
              << "                                      │\n";
    std::cout << "│ Name: " << std::setw(20) << std::left << checkpoint.name << std::right
              << "                                    │\n";
    std::cout << "│ Process: " << std::setw(20) << std::left << checkpoint.info.name << std::right
              << " (PID: " << std::setw(8) << checkpoint.info.pid << ")       │\n";
    std::cout << "├─────────────────────────────────────────────────────────────────────┤\n";
    std::cout << "│ Total Memory Map: " << std::setw(12) << formatMemorySize(checkpoint.totalMemorySize())
              << "                                   │\n";
    std::cout << "│ Dumped Memory: " << std::setw(12) << formatMemorySize(checkpoint.dumpedMemorySize())
              << "                                      │\n";
    std::cout << "│ Memory Regions: " << std::setw(8) << checkpoint.memoryMap.size()
              << "                                           │\n";
    std::cout << "│ Dumped Regions: " << std::setw(8) << checkpoint.memoryDumps.size()
              << "                                           │\n";
    std::cout << "│ File Descriptors: " << std::setw(8) << checkpoint.fileDescriptors.size()
              << "                                         │\n";
    std::cout << "│ Environment Vars: " << std::setw(8) << checkpoint.environ.size()
              << "                                         │\n";
    std::cout << "└─────────────────────────────────────────────────────────────────────┘\n";
    
    // Register'ları göster
    printSubHeader("Saved Registers");
    printRegisters(checkpoint.registers);
    
    waitForUser();
}

// ============================================================================
// Demo 4: Checkpoint Karşılaştırma
// ============================================================================

void demoCheckpointDiff(pid_t targetPid, const RealProcessCheckpoint& oldCheckpoint) {
    printHeader("Demo 4: Checkpoint Karşılaştırma");
    
    std::cout << "Process çalışmaya devam ediyor...\n";
    std::cout << "3 saniye bekleniyor...\n\n";
    
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    std::cout << "Yeni checkpoint alınıyor...\n\n";
    
    RealProcessCheckpointer checkpointer;
    CheckpointOptions options;
    options.saveMemory = true;
    options.saveRegisters = true;
    
    auto newCheckpoint = checkpointer.createCheckpoint(targetPid, "new_checkpoint", options);
    
    if (!newCheckpoint) {
        std::cout << "❌ Yeni checkpoint başarısız!\n";
        return;
    }
    
    std::cout << "✅ Yeni checkpoint alındı!\n\n";
    
    // Karşılaştır
    auto diff = checkpointer.compareCheckpoints(oldCheckpoint, *newCheckpoint);
    
    std::cout << "┌─────────────────────────────────────────────────────────────────────┐\n";
    std::cout << "│ Checkpoint Diff (Farklar)                                           │\n";
    std::cout << "├─────────────────────────────────────────────────────────────────────┤\n";
    std::cout << "│ Registers Changed: " << (diff.registersChanged ? "YES" : "NO")
              << std::string(47, ' ') << "│\n";
    
    if (diff.registersChanged && !diff.changedRegisters.empty()) {
        std::cout << "│   Changed: ";
        for (size_t i = 0; i < std::min(diff.changedRegisters.size(), size_t(5)); ++i) {
            std::cout << diff.changedRegisters[i] << " ";
        }
        std::cout << std::string(50 - diff.changedRegisters.size() * 5, ' ') << "│\n";
    }
    
    std::cout << "│ Memory Changed: " << (diff.memoryChanged ? "YES" : "NO")
              << std::string(50, ' ') << "│\n";
    std::cout << "│   Bytes Changed: " << std::setw(15) << formatMemorySize(diff.totalBytesChanged)
              << std::string(34, ' ') << "│\n";
    std::cout << "│   Regions Modified: " << std::setw(8) << diff.modifiedRegions.size()
              << std::string(38, ' ') << "│\n";
    std::cout << "└─────────────────────────────────────────────────────────────────────┘\n";
    
    // RIP değişimini göster
    std::cout << "\nRIP (Instruction Pointer) değişimi:\n";
    std::cout << "  Eski: 0x" << std::hex << oldCheckpoint.registers.rip << std::dec << "\n";
    std::cout << "  Yeni: 0x" << std::hex << newCheckpoint->registers.rip << std::dec << "\n";
    
    waitForUser();
}

// ============================================================================
// Demo 5: Checkpoint'i Dosyaya Kaydetme
// ============================================================================

void demoSaveCheckpoint(const RealProcessCheckpoint& checkpoint) {
    printHeader("Demo 5: Checkpoint Kaydetme");
    
    std::string filepath = "real_checkpoint_" + std::to_string(checkpoint.info.pid) + ".chkpt";
    
    std::cout << "Checkpoint dosyaya kaydediliyor: " << filepath << "\n\n";
    
    RealProcessCheckpointer checkpointer;
    
    if (checkpointer.saveCheckpoint(checkpoint, filepath)) {
        std::cout << "✅ Checkpoint kaydedildi!\n";
        std::cout << "   Dosya: " << filepath << "\n";
        
        // Dosya boyutunu göster
        auto serialized = checkpoint.serialize();
        std::cout << "   Boyut: " << formatMemorySize(serialized.size()) << "\n";
    } else {
        std::cout << "❌ Kaydetme başarısız: " << checkpointer.getLastError() << "\n";
    }
    
    waitForUser();
}

// ============================================================================
// Ana Demo
// ============================================================================

void runInteractiveDemo() {
    printHeader("Real Process Checkpoint & Rollback Demo");
    
    std::cout << "Bu demo GERÇEK Linux process'lerinde checkpoint alma işlemini gösterir.\n\n";
    
    std::cout << "⚠️  UYARILAR:\n";
    std::cout << "  • Bu demo ptrace() syscall'ı kullanır\n";
    std::cout << "  • Kendi process'lerinizde çalışır (aynı UID)\n";
    std::cout << "  • Başka kullanıcıların process'leri için root gerekir\n";
    std::cout << "  • Checkpoint sırasında process geçici olarak durur\n\n";
    
    std::cout << "Seçenekler:\n";
    std::cout << "  1. Test process oluştur ve checkpoint al\n";
    std::cout << "  2. Mevcut bir PID'ye checkpoint al\n";
    std::cout << "  3. Sadece process listele\n";
    std::cout << "  0. Çıkış\n\n";
    
    std::cout << "Seçiminiz: ";
    
    int choice;
    std::cin >> choice;
    std::cin.ignore();
    
    if (choice == 1) {
        // Test process oluştur
        printSubHeader("Test Process Oluşturuluyor");
        
        pid_t childPid = createTestProcess();
        std::cout << "Test process oluşturuldu! PID: " << childPid << "\n";
        
        // Biraz bekle
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Demo'ları çalıştır
        demoProcessDetails(childPid);
        
        RealProcessCheckpoint checkpoint;
        demoCheckpoint(childPid, checkpoint);
        
        if (checkpoint.checkpointId != 0) {
            demoCheckpointDiff(childPid, checkpoint);
            demoSaveCheckpoint(checkpoint);
        }
        
        // Child process'i temizle
        kill(childPid, SIGKILL);
        waitpid(childPid, nullptr, 0);
        std::cout << "\nTest process sonlandırıldı.\n";
        
    } else if (choice == 2) {
        // Mevcut PID
        std::cout << "PID girin: ";
        pid_t targetPid;
        std::cin >> targetPid;
        std::cin.ignore();
        
        demoProcessDetails(targetPid);
        
        RealProcessCheckpoint checkpoint;
        demoCheckpoint(targetPid, checkpoint);
        
        if (checkpoint.checkpointId != 0) {
            demoSaveCheckpoint(checkpoint);
        }
        
    } else if (choice == 3) {
        demoProcessListing();
    }
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    std::cout << R"(
    ╔════════════════════════════════════════════════════════════════════╗
    ║                                                                    ║
    ║   🔬  REAL PROCESS CHECKPOINT & ROLLBACK SYSTEM  🔬               ║
    ║                                                                    ║
    ║         Linux ptrace() ile Gerçek Process Checkpoint              ║
    ║                                                                    ║
    ╚════════════════════════════════════════════════════════════════════╝
    )" << std::endl;
    
    // Eğer komut satırından PID verilmişse direkt onu kullan
    if (argc > 1) {
        pid_t targetPid = std::stoi(argv[1]);
        std::cout << "Hedef PID: " << targetPid << "\n";
        
        RealProcessCheckpoint checkpoint;
        demoProcessDetails(targetPid);
        demoCheckpoint(targetPid, checkpoint);
        
        if (checkpoint.checkpointId != 0) {
            demoSaveCheckpoint(checkpoint);
        }
    } else {
        runInteractiveDemo();
    }
    
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                        DEMO TAMAMLANDI                             ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════════╝\n";
    
    return 0;
}
