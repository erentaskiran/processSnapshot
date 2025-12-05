/**
 * Process Checkpoint CLI
 * 
 * İnteraktif CLI aracı ile gerçek process'lerin checkpoint'ini alıp restore edebilirsiniz.
 * 
 * Kullanım:
 *   process_checkpoint_cli [--help]
 *   process_checkpoint_cli --pid <pid>    # Belirli process ile başla
 *   process_checkpoint_cli --list         # Process listesi göster
 * 
 * Komutlar:
 *   snapshot <pid> [name]    - Process'in snapshot'ını al
 *   restore <id/file>        - Checkpoint'i restore et
 *   list                     - Alınan checkpoint'leri listele
 *   info <pid>               - Process bilgisi göster
 *   ps                       - Tüm process'leri listele
 *   diff <id1> <id2>         - İki checkpoint'i karşılaştır
 *   export <id> <file>       - Checkpoint'i dosyaya kaydet
 *   import <file>            - Checkpoint'i dosyadan yükle
 *   help                     - Yardım göster
 *   quit                     - Çıkış
 */

#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>
#include <vector>
#include <map>
#include <filesystem>
#include <chrono>
#include <csignal>
#include <cstring>
#include <unistd.h>

#include "real_process/real_process_types.hpp"
#include "real_process/proc_reader.hpp"
#include "real_process/ptrace_controller.hpp"

using namespace checkpoint::real_process;
namespace fs = std::filesystem;

// ============================================================================
// ANSI Color Codes
// ============================================================================
namespace Color {
    const std::string RESET   = "\033[0m";
    const std::string RED     = "\033[31m";
    const std::string GREEN   = "\033[32m";
    const std::string YELLOW  = "\033[33m";
    const std::string BLUE    = "\033[34m";
    const std::string MAGENTA = "\033[35m";
    const std::string CYAN    = "\033[36m";
    const std::string WHITE   = "\033[37m";
    const std::string BOLD    = "\033[1m";
    const std::string DIM     = "\033[2m";
}

// ============================================================================
// Global State
// ============================================================================
struct CLIState {
    std::map<uint64_t, RealProcessCheckpoint> checkpoints;
    uint64_t nextCheckpointId = 1;
    std::string checkpointDir = "./cli_checkpoints";
    ProcFSReader procReader;
    RealProcessCheckpointer checkpointer;
    bool running = true;
    bool verbose = false;
};

CLIState g_state;

// ============================================================================
// Utility Functions
// ============================================================================

void printHeader() {
    std::cout << Color::CYAN << Color::BOLD;
    std::cout << R"(
╔═══════════════════════════════════════════════════════════════════════════════╗
║                    Process Checkpoint CLI v1.0                                ║
║                    Real Process Snapshot & Restore                            ║
╚═══════════════════════════════════════════════════════════════════════════════╝
)" << Color::RESET;
}

void printPrompt() {
    std::cout << Color::GREEN << "checkpoint" << Color::RESET 
              << Color::YELLOW << " > " << Color::RESET;
}

void printError(const std::string& msg) {
    std::cout << Color::RED << "[ERROR] " << Color::RESET << msg << "\n";
}

void printSuccess(const std::string& msg) {
    std::cout << Color::GREEN << "[OK] " << Color::RESET << msg << "\n";
}

void printInfo(const std::string& msg) {
    std::cout << Color::CYAN << "[INFO] " << Color::RESET << msg << "\n";
}

void printWarning(const std::string& msg) {
    std::cout << Color::YELLOW << "[WARN] " << Color::RESET << msg << "\n";
}

std::vector<std::string> splitCommand(const std::string& input) {
    std::vector<std::string> tokens;
    std::istringstream iss(input);
    std::string token;
    while (iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

std::string formatTimestamp(uint64_t ts) {
    auto time = std::chrono::system_clock::from_time_t(ts);
    auto now = std::chrono::system_clock::to_time_t(time);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::string formatSize(uint64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB"};
    int unit = 0;
    double size = bytes;
    while (size >= 1024 && unit < 3) {
        size /= 1024;
        unit++;
    }
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << size << " " << units[unit];
    return ss.str();
}

// ============================================================================
// Command: help
// ============================================================================
void cmdHelp() {
    std::cout << Color::BOLD << "\nKullanılabilir Komutlar:\n" << Color::RESET;
    std::cout << "═══════════════════════════════════════════════════════════════════\n\n";
    
    std::cout << Color::CYAN << "  Process Komutları:\n" << Color::RESET;
    std::cout << "    ps                          Tüm process'leri listele\n";
    std::cout << "    info <pid>                  Process detaylı bilgisi\n";
    std::cout << "    regs <pid>                  Process register'larını göster\n";
    std::cout << "    maps <pid>                  Process memory map'ini göster\n";
    std::cout << "\n";
    
    std::cout << Color::CYAN << "  Checkpoint Komutları:\n" << Color::RESET;
    std::cout << "    snapshot <pid> [name]       Process checkpoint'i al\n";
    std::cout << "    restore <id> [pid]          Checkpoint'i restore et\n";
    std::cout << "    list                        Alınmış checkpoint'leri listele\n";
    std::cout << "    show <id>                   Checkpoint detaylarını göster\n";
    std::cout << "    delete <id>                 Checkpoint'i sil\n";
    std::cout << "    diff <id1> <id2>            İki checkpoint'i karşılaştır\n";
    std::cout << "\n";
    
    std::cout << Color::CYAN << "  Dosya İşlemleri:\n" << Color::RESET;
    std::cout << "    export <id> <file>          Checkpoint'i dosyaya kaydet\n";
    std::cout << "    import <file>               Dosyadan checkpoint yükle\n";
    std::cout << "\n";
    
    std::cout << Color::CYAN << "  Diğer:\n" << Color::RESET;
    std::cout << "    verbose [on|off]            Detaylı çıktı aç/kapa\n";
    std::cout << "    clear                       Ekranı temizle\n";
    std::cout << "    help                        Bu yardımı göster\n";
    std::cout << "    quit/exit                   Çıkış\n";
    std::cout << "\n";
    
    std::cout << Color::YELLOW << "Not: " << Color::RESET 
              << "Checkpoint işlemleri için root yetkisi gerekebilir.\n\n";
}

// ============================================================================
// Command: ps - List processes
// ============================================================================
void cmdPs() {
    auto pids = g_state.procReader.listAllPids();
    
    std::cout << Color::BOLD;
    std::cout << std::setw(8) << "PID" 
              << std::setw(8) << "PPID"
              << std::setw(12) << "STATE"
              << std::setw(12) << "MEMORY"
              << std::setw(8) << "THREADS"
              << "  NAME\n";
    std::cout << Color::RESET;
    std::cout << std::string(70, '-') << "\n";
    
    int count = 0;
    for (pid_t pid : pids) {
        auto info = g_state.procReader.getProcessInfo(pid);
        if (!info) continue;
        
        // Kernel thread'leri atla
        if (info->cmdline.empty() && info->name.empty()) continue;
        
        std::cout << std::setw(8) << info->pid
                  << std::setw(8) << info->ppid
                  << std::setw(12) << linuxStateToString(info->state)
                  << std::setw(12) << formatSize(info->vmRss)
                  << std::setw(8) << info->numThreads
                  << "  " << (info->name.empty() ? "[unknown]" : info->name) << "\n";
        
        count++;
        if (count >= 50) {
            std::cout << Color::DIM << "... ve " << (pids.size() - 50) 
                      << " process daha (toplam: " << pids.size() << ")\n" << Color::RESET;
            break;
        }
    }
    std::cout << "\nToplam: " << pids.size() << " process\n\n";
}

// ============================================================================
// Command: info - Process info
// ============================================================================
void cmdInfo(pid_t pid) {
    auto info = g_state.procReader.getProcessInfo(pid);
    if (!info) {
        printError("Process bulunamadı: " + std::to_string(pid));
        return;
    }
    
    std::cout << "\n" << Color::BOLD << "Process Bilgisi - PID " << pid << Color::RESET << "\n";
    std::cout << std::string(60, '=') << "\n\n";
    
    std::cout << Color::CYAN << "Temel Bilgiler:\n" << Color::RESET;
    std::cout << "  PID:        " << info->pid << "\n";
    std::cout << "  PPID:       " << info->ppid << "\n";
    std::cout << "  Name:       " << info->name << "\n";
    std::cout << "  Cmdline:    " << info->cmdline << "\n";
    std::cout << "  Exe:        " << info->exe << "\n";
    std::cout << "  CWD:        " << info->cwd << "\n";
    std::cout << "  State:      " << linuxStateToString(info->state) << "\n";
    
    std::cout << "\n" << Color::CYAN << "Bellek Kullanımı:\n" << Color::RESET;
    std::cout << "  Virtual:    " << formatSize(info->vmSize) << "\n";
    std::cout << "  Resident:   " << formatSize(info->vmRss) << "\n";
    std::cout << "  Peak:       " << formatSize(info->vmPeak) << "\n";
    std::cout << "  Data:       " << formatSize(info->vmData) << "\n";
    std::cout << "  Stack:      " << formatSize(info->vmStack) << "\n";
    
    std::cout << "\n" << Color::CYAN << "CPU:\n" << Color::RESET;
    std::cout << "  Threads:    " << info->numThreads << "\n";
    std::cout << "  Priority:   " << info->priority << "\n";
    std::cout << "  Nice:       " << info->nice << "\n";
    std::cout << "  User Time:  " << info->utime << " ticks\n";
    std::cout << "  Sys Time:   " << info->stime << " ticks\n";
    
    std::cout << "\n" << Color::CYAN << "Kullanıcı:\n" << Color::RESET;
    std::cout << "  UID:        " << info->uid << " (euid: " << info->euid << ")\n";
    std::cout << "  GID:        " << info->gid << " (egid: " << info->egid << ")\n";
    
    std::cout << "\n";
}

// ============================================================================
// Command: regs - Show registers
// ============================================================================
void cmdRegs(pid_t pid) {
    PtraceController ptrace;
    
    auto err = ptrace.attach(pid);
    if (err != PtraceError::SUCCESS) {
        printError("Process'e attach olunamadı: " + ptraceErrorToString(err));
        return;
    }
    
    LinuxRegisters regs;
    err = ptrace.getRegisters(regs);
    if (err != PtraceError::SUCCESS) {
        printError("Register'lar okunamadı: " + ptraceErrorToString(err));
        ptrace.detach();
        return;
    }
    
    std::cout << "\n" << Color::BOLD << "CPU Registers - PID " << pid << Color::RESET << "\n";
    std::cout << std::string(60, '=') << "\n\n";
    
    std::cout << Color::CYAN << "Instruction & Stack Pointers:\n" << Color::RESET;
    std::cout << "  RIP: " << std::hex << "0x" << std::setw(16) << std::setfill('0') << regs.rip 
              << std::setfill(' ') << std::dec << "\n";
    std::cout << "  RSP: " << std::hex << "0x" << std::setw(16) << std::setfill('0') << regs.rsp 
              << std::setfill(' ') << std::dec << "\n";
    std::cout << "  RBP: " << std::hex << "0x" << std::setw(16) << std::setfill('0') << regs.rbp 
              << std::setfill(' ') << std::dec << "\n";
    
    std::cout << "\n" << Color::CYAN << "General Purpose:\n" << Color::RESET;
    std::cout << "  RAX: " << std::hex << "0x" << std::setw(16) << std::setfill('0') << regs.rax 
              << "   RBX: 0x" << std::setw(16) << regs.rbx << std::setfill(' ') << std::dec << "\n";
    std::cout << "  RCX: " << std::hex << "0x" << std::setw(16) << std::setfill('0') << regs.rcx 
              << "   RDX: 0x" << std::setw(16) << regs.rdx << std::setfill(' ') << std::dec << "\n";
    std::cout << "  RSI: " << std::hex << "0x" << std::setw(16) << std::setfill('0') << regs.rsi 
              << "   RDI: 0x" << std::setw(16) << regs.rdi << std::setfill(' ') << std::dec << "\n";
    
    std::cout << "\n" << Color::CYAN << "Extended:\n" << Color::RESET;
    std::cout << "  R8:  " << std::hex << "0x" << std::setw(16) << std::setfill('0') << regs.r8 
              << "   R9:  0x" << std::setw(16) << regs.r9 << std::setfill(' ') << std::dec << "\n";
    std::cout << "  R10: " << std::hex << "0x" << std::setw(16) << std::setfill('0') << regs.r10 
              << "   R11: 0x" << std::setw(16) << regs.r11 << std::setfill(' ') << std::dec << "\n";
    std::cout << "  R12: " << std::hex << "0x" << std::setw(16) << std::setfill('0') << regs.r12 
              << "   R13: 0x" << std::setw(16) << regs.r13 << std::setfill(' ') << std::dec << "\n";
    std::cout << "  R14: " << std::hex << "0x" << std::setw(16) << std::setfill('0') << regs.r14 
              << "   R15: 0x" << std::setw(16) << regs.r15 << std::setfill(' ') << std::dec << "\n";
    
    std::cout << "\n" << Color::CYAN << "Flags:\n" << Color::RESET;
    std::cout << "  EFLAGS: " << std::hex << "0x" << regs.eflags << std::dec << "\n";
    
    ptrace.detach();
    std::cout << "\n";
}

// ============================================================================
// Command: maps - Memory map
// ============================================================================
void cmdMaps(pid_t pid) {
    auto maps = g_state.procReader.getMemoryMaps(pid);
    if (maps.empty()) {
        printError("Memory map okunamadı veya process bulunamadı.");
        return;
    }
    
    std::cout << "\n" << Color::BOLD << "Memory Map - PID " << pid << Color::RESET << "\n";
    std::cout << std::string(90, '=') << "\n\n";
    
    std::cout << Color::BOLD;
    std::cout << std::setw(18) << "START" << " - " << std::setw(18) << "END"
              << "  " << std::setw(5) << "PERM"
              << "  " << std::setw(10) << "SIZE"
              << "  PATH\n";
    std::cout << Color::RESET;
    std::cout << std::string(90, '-') << "\n";
    
    uint64_t totalSize = 0;
    uint64_t writableSize = 0;
    
    for (const auto& region : maps) {
        std::string perms;
        perms += region.readable ? 'r' : '-';
        perms += region.writable ? 'w' : '-';
        perms += region.executable ? 'x' : '-';
        perms += region.isPrivate ? 'p' : 's';
        
        std::string path = region.pathname;
        if (path.length() > 40) {
            path = "..." + path.substr(path.length() - 37);
        }
        
        std::cout << std::hex 
                  << "0x" << std::setw(16) << std::setfill('0') << region.startAddr
                  << " - 0x" << std::setw(16) << region.endAddr
                  << std::setfill(' ') << std::dec
                  << "  " << perms
                  << "  " << std::setw(10) << formatSize(region.size())
                  << "  " << path << "\n";
        
        totalSize += region.size();
        if (region.writable) writableSize += region.size();
    }
    
    std::cout << std::string(90, '-') << "\n";
    std::cout << "Toplam: " << maps.size() << " region, "
              << formatSize(totalSize) << " (writable: " << formatSize(writableSize) << ")\n\n";
}

// ============================================================================
// Command: snapshot - Create checkpoint
// ============================================================================
void cmdSnapshot(pid_t pid, const std::string& name) {
    printInfo("Checkpoint alınıyor: PID " + std::to_string(pid));
    
    // Progress callback
    g_state.checkpointer.setProgressCallback([](const std::string& stage, double progress) {
        std::cout << "\r" << Color::CYAN << "[" << std::setw(3) << int(progress * 100) << "%] " 
                  << Color::RESET << stage << std::string(30, ' ') << std::flush;
    });
    
    CheckpointOptions options;
    options.saveMemory = true;
    options.saveRegisters = true;
    options.saveFileDescriptors = true;
    options.dumpHeap = true;
    options.dumpStack = true;
    options.dumpAnonymous = true;
    
    auto checkpoint = g_state.checkpointer.createCheckpoint(pid, name, options);
    
    std::cout << "\r" << std::string(60, ' ') << "\r"; // Progress satırını temizle
    
    if (!checkpoint) {
        printError("Checkpoint alınamadı: " + g_state.checkpointer.getLastError());
        return;
    }
    
    // ID ata ve kaydet
    checkpoint->checkpointId = g_state.nextCheckpointId++;
    g_state.checkpoints[checkpoint->checkpointId] = *checkpoint;
    
    // Dosyaya da kaydet
    fs::create_directories(g_state.checkpointDir);
    std::string filepath = g_state.checkpointDir + "/checkpoint_" + 
                          std::to_string(checkpoint->checkpointId) + ".chkpt";
    g_state.checkpointer.saveCheckpoint(*checkpoint, filepath);
    
    std::cout << "\n";
    printSuccess("Checkpoint alındı!");
    std::cout << "  ID:          " << checkpoint->checkpointId << "\n";
    std::cout << "  Process:     " << checkpoint->info.name << " (PID: " << pid << ")\n";
    std::cout << "  Memory:      " << formatSize(checkpoint->dumpedMemorySize()) << " dumped\n";
    std::cout << "  Regions:     " << checkpoint->memoryDumps.size() << "\n";
    std::cout << "  File:        " << filepath << "\n\n";
}

// ============================================================================
// Command: restore - Restore checkpoint
// ============================================================================
void cmdRestore(uint64_t checkpointId, pid_t targetPid) {
    auto it = g_state.checkpoints.find(checkpointId);
    if (it == g_state.checkpoints.end()) {
        printError("Checkpoint bulunamadı: " + std::to_string(checkpointId));
        return;
    }
    
    const auto& checkpoint = it->second;
    
    if (targetPid == 0) {
        targetPid = checkpoint.info.pid;
    }
    
    printWarning("DİKKAT: Bu işlem tehlikelidir ve process'i bozabilir!");
    std::cout << "  Target PID:   " << targetPid << "\n";
    std::cout << "  Checkpoint:   " << checkpointId << " (" << checkpoint.name << ")\n";
    std::cout << "  Memory:       " << formatSize(checkpoint.dumpedMemorySize()) << "\n";
    std::cout << "\nDevam etmek istiyor musunuz? [y/N]: ";
    
    std::string confirm;
    std::getline(std::cin, confirm);
    
    if (confirm != "y" && confirm != "Y") {
        printInfo("İptal edildi.");
        return;
    }
    
    printInfo("Restore ediliyor...");
    
    RestoreOptions options;
    options.restoreRegisters = true;
    options.restoreMemory = true;
    options.continueAfterRestore = true;
    
    auto err = g_state.checkpointer.restoreCheckpoint(targetPid, checkpoint, options);
    
    if (err != PtraceError::SUCCESS) {
        printError("Restore başarısız: " + ptraceErrorToString(err));
        return;
    }
    
    printSuccess("Checkpoint restore edildi!");
    std::cout << "\n";
}

// ============================================================================
// Command: list - List checkpoints
// ============================================================================
void cmdList() {
    if (g_state.checkpoints.empty()) {
        printInfo("Henüz checkpoint alınmamış.");
        std::cout << "  'snapshot <pid>' komutu ile checkpoint alabilirsiniz.\n\n";
        return;
    }
    
    std::cout << "\n" << Color::BOLD << "Kayitli Checkpoint'ler\n" << Color::RESET;
    std::cout << std::string(80, '=') << "\n\n";
    
    std::cout << Color::BOLD;
    std::cout << std::setw(6) << "ID"
              << std::setw(20) << "NAME"
              << std::setw(8) << "PID"
              << std::setw(15) << "PROCESS"
              << std::setw(12) << "MEMORY"
              << std::setw(8) << "REGIONS"
              << "  TIME\n";
    std::cout << Color::RESET;
    std::cout << std::string(80, '-') << "\n";
    
    for (const auto& [id, cp] : g_state.checkpoints) {
        std::string name = cp.name.empty() ? "-" : cp.name;
        if (name.length() > 18) name = name.substr(0, 15) + "...";
        
        std::string procName = cp.info.name;
        if (procName.length() > 13) procName = procName.substr(0, 10) + "...";
        
        std::cout << std::setw(6) << id
                  << std::setw(20) << name
                  << std::setw(8) << cp.info.pid
                  << std::setw(15) << procName
                  << std::setw(12) << formatSize(cp.dumpedMemorySize())
                  << std::setw(8) << cp.memoryDumps.size()
                  << "  " << formatTimestamp(cp.timestamp) << "\n";
    }
    
    std::cout << "\nToplam: " << g_state.checkpoints.size() << " checkpoint\n\n";
}

// ============================================================================
// Command: show - Show checkpoint details
// ============================================================================
void cmdShow(uint64_t checkpointId) {
    auto it = g_state.checkpoints.find(checkpointId);
    if (it == g_state.checkpoints.end()) {
        printError("Checkpoint bulunamadı: " + std::to_string(checkpointId));
        return;
    }
    
    const auto& cp = it->second;
    
    std::cout << "\n" << Color::BOLD << "Checkpoint Detaylari - ID " << checkpointId << Color::RESET << "\n";
    std::cout << std::string(60, '=') << "\n\n";
    
    std::cout << Color::CYAN << "Metadata:\n" << Color::RESET;
    std::cout << "  ID:         " << cp.checkpointId << "\n";
    std::cout << "  Name:       " << (cp.name.empty() ? "-" : cp.name) << "\n";
    std::cout << "  Time:       " << formatTimestamp(cp.timestamp) << "\n";
    
    std::cout << "\n" << Color::CYAN << "Process:\n" << Color::RESET;
    std::cout << "  PID:        " << cp.info.pid << "\n";
    std::cout << "  Name:       " << cp.info.name << "\n";
    std::cout << "  Cmdline:    " << cp.info.cmdline << "\n";
    std::cout << "  State:      " << linuxStateToString(cp.info.state) << "\n";
    
    std::cout << "\n" << Color::CYAN << "Memory:\n" << Color::RESET;
    std::cout << "  Total Map:  " << cp.memoryMap.size() << " regions\n";
    std::cout << "  Dumped:     " << cp.memoryDumps.size() << " regions\n";
    std::cout << "  Size:       " << formatSize(cp.dumpedMemorySize()) << "\n";
    
    std::cout << "\n" << Color::CYAN << "Registers:\n" << Color::RESET;
    std::cout << "  RIP:        " << std::hex << "0x" << cp.registers.rip << std::dec << "\n";
    std::cout << "  RSP:        " << std::hex << "0x" << cp.registers.rsp << std::dec << "\n";
    std::cout << "  RBP:        " << std::hex << "0x" << cp.registers.rbp << std::dec << "\n";
    
    std::cout << "\n" << Color::CYAN << "Diğer:\n" << Color::RESET;
    std::cout << "  FDs:        " << cp.fileDescriptors.size() << " open files\n";
    std::cout << "  Env Vars:   " << cp.environ.size() << "\n";
    
    std::cout << "\n";
}

// ============================================================================
// Command: delete - Delete checkpoint
// ============================================================================
void cmdDelete(uint64_t checkpointId) {
    auto it = g_state.checkpoints.find(checkpointId);
    if (it == g_state.checkpoints.end()) {
        printError("Checkpoint bulunamadı: " + std::to_string(checkpointId));
        return;
    }
    
    g_state.checkpoints.erase(it);
    
    // Dosyayı da sil
    std::string filepath = g_state.checkpointDir + "/checkpoint_" + 
                          std::to_string(checkpointId) + ".chkpt";
    fs::remove(filepath);
    
    printSuccess("Checkpoint silindi: " + std::to_string(checkpointId));
}

// ============================================================================
// Command: export - Export checkpoint to file
// ============================================================================
void cmdExport(uint64_t checkpointId, const std::string& filepath) {
    auto it = g_state.checkpoints.find(checkpointId);
    if (it == g_state.checkpoints.end()) {
        printError("Checkpoint bulunamadı: " + std::to_string(checkpointId));
        return;
    }
    
    if (g_state.checkpointer.saveCheckpoint(it->second, filepath)) {
        printSuccess("Checkpoint dosyaya kaydedildi: " + filepath);
    } else {
        printError("Dosyaya yazılamadı: " + g_state.checkpointer.getLastError());
    }
}

// ============================================================================
// Command: import - Import checkpoint from file
// ============================================================================
void cmdImport(const std::string& filepath) {
    auto checkpoint = g_state.checkpointer.loadCheckpoint(filepath);
    if (!checkpoint) {
        printError("Dosya okunamadı: " + g_state.checkpointer.getLastError());
        return;
    }
    
    checkpoint->checkpointId = g_state.nextCheckpointId++;
    g_state.checkpoints[checkpoint->checkpointId] = *checkpoint;
    
    printSuccess("Checkpoint yüklendi!");
    std::cout << "  ID:       " << checkpoint->checkpointId << "\n";
    std::cout << "  Process:  " << checkpoint->info.name << " (PID: " << checkpoint->info.pid << ")\n";
    std::cout << "  Memory:   " << formatSize(checkpoint->dumpedMemorySize()) << "\n\n";
}

// ============================================================================
// Command: diff - Compare checkpoints
// ============================================================================
void cmdDiff(uint64_t id1, uint64_t id2) {
    auto it1 = g_state.checkpoints.find(id1);
    auto it2 = g_state.checkpoints.find(id2);
    
    if (it1 == g_state.checkpoints.end()) {
        printError("Checkpoint bulunamadı: " + std::to_string(id1));
        return;
    }
    if (it2 == g_state.checkpoints.end()) {
        printError("Checkpoint bulunamadı: " + std::to_string(id2));
        return;
    }
    
    auto diff = g_state.checkpointer.compareCheckpoints(it1->second, it2->second);
    
    std::cout << "\n" << Color::BOLD << "Checkpoint Karsilastirmasi: " 
              << id1 << " vs " << id2 << Color::RESET << "\n";
    std::cout << std::string(60, '=') << "\n\n";
    
    std::cout << Color::CYAN << "Register Değişiklikleri:\n" << Color::RESET;
    if (diff.registersChanged) {
        std::cout << "  Değişen register'lar: ";
        for (const auto& reg : diff.changedRegisters) {
            std::cout << reg << " ";
        }
        std::cout << "\n";
    } else {
        std::cout << "  Değişiklik yok\n";
    }
    
    std::cout << "\n" << Color::CYAN << "Memory Değişiklikleri:\n" << Color::RESET;
    if (diff.memoryChanged) {
        std::cout << "  Eklenen region:   " << diff.addedRegions.size() << "\n";
        std::cout << "  Silinen region:   " << diff.removedRegions.size() << "\n";
        std::cout << "  Değişen region:   " << diff.modifiedRegions.size() << "\n";
        std::cout << "  Toplam değişiklik: " << formatSize(diff.totalBytesChanged) << "\n";
    } else {
        std::cout << "  Değişiklik yok\n";
    }
    
    std::cout << "\n" << Color::CYAN << "File Descriptor Değişiklikleri:\n" << Color::RESET;
    if (diff.filesChanged) {
        std::cout << "  Açılan FD: " << diff.addedFds.size() << "\n";
        std::cout << "  Kapanan FD: " << diff.removedFds.size() << "\n";
    } else {
        std::cout << "  Değişiklik yok\n";
    }
    
    std::cout << "\n";
}

// ============================================================================
// Process command
// ============================================================================
void processCommand(const std::string& input) {
    auto tokens = splitCommand(input);
    if (tokens.empty()) return;
    
    const std::string& cmd = tokens[0];
    
    try {
        if (cmd == "help" || cmd == "h" || cmd == "?") {
            cmdHelp();
        }
        else if (cmd == "quit" || cmd == "exit" || cmd == "q") {
            g_state.running = false;
        }
        else if (cmd == "clear" || cmd == "cls") {
            std::cout << "\033[2J\033[H";
        }
        else if (cmd == "ps") {
            cmdPs();
        }
        else if (cmd == "info") {
            if (tokens.size() < 2) {
                printError("Kullanım: info <pid>");
                return;
            }
            cmdInfo(std::stoi(tokens[1]));
        }
        else if (cmd == "regs") {
            if (tokens.size() < 2) {
                printError("Kullanım: regs <pid>");
                return;
            }
            cmdRegs(std::stoi(tokens[1]));
        }
        else if (cmd == "maps") {
            if (tokens.size() < 2) {
                printError("Kullanım: maps <pid>");
                return;
            }
            cmdMaps(std::stoi(tokens[1]));
        }
        else if (cmd == "snapshot" || cmd == "snap" || cmd == "s") {
            if (tokens.size() < 2) {
                printError("Kullanım: snapshot <pid> [name]");
                return;
            }
            std::string name = tokens.size() > 2 ? tokens[2] : "";
            cmdSnapshot(std::stoi(tokens[1]), name);
        }
        else if (cmd == "restore" || cmd == "r") {
            if (tokens.size() < 2) {
                printError("Kullanım: restore <checkpoint_id> [target_pid]");
                return;
            }
            pid_t targetPid = tokens.size() > 2 ? std::stoi(tokens[2]) : 0;
            cmdRestore(std::stoull(tokens[1]), targetPid);
        }
        else if (cmd == "list" || cmd == "ls" || cmd == "l") {
            cmdList();
        }
        else if (cmd == "show") {
            if (tokens.size() < 2) {
                printError("Kullanım: show <checkpoint_id>");
                return;
            }
            cmdShow(std::stoull(tokens[1]));
        }
        else if (cmd == "delete" || cmd == "del" || cmd == "rm") {
            if (tokens.size() < 2) {
                printError("Kullanım: delete <checkpoint_id>");
                return;
            }
            cmdDelete(std::stoull(tokens[1]));
        }
        else if (cmd == "export") {
            if (tokens.size() < 3) {
                printError("Kullanım: export <checkpoint_id> <filepath>");
                return;
            }
            cmdExport(std::stoull(tokens[1]), tokens[2]);
        }
        else if (cmd == "import") {
            if (tokens.size() < 2) {
                printError("Kullanım: import <filepath>");
                return;
            }
            cmdImport(tokens[1]);
        }
        else if (cmd == "diff") {
            if (tokens.size() < 3) {
                printError("Kullanım: diff <checkpoint_id1> <checkpoint_id2>");
                return;
            }
            cmdDiff(std::stoull(tokens[1]), std::stoull(tokens[2]));
        }
        else if (cmd == "verbose") {
            if (tokens.size() > 1) {
                g_state.verbose = (tokens[1] == "on" || tokens[1] == "1");
            }
            std::cout << "Verbose mode: " << (g_state.verbose ? "ON" : "OFF") << "\n";
        }
        else {
            printError("Bilinmeyen komut: " + cmd);
            std::cout << "  'help' yazarak kullanılabilir komutları görebilirsiniz.\n";
        }
    }
    catch (const std::exception& e) {
        printError(std::string("Hata: ") + e.what());
    }
}

// ============================================================================
// Main
// ============================================================================
int main(int argc, char* argv[]) {
    // Argument parsing
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << "Kullanım: " << argv[0] << " [options]\n\n";
            std::cout << "Options:\n";
            std::cout << "  --help, -h           Yardım göster\n";
            std::cout << "  --pid <pid>          Belirli bir PID ile başla\n";
            std::cout << "  --dir <directory>    Checkpoint dizini\n";
            std::cout << "\n";
            return 0;
        }
        else if ((arg == "--pid" || arg == "-p") && i + 1 < argc) {
            // Başlangıçta bu PID'nin bilgisini göster
            pid_t pid = std::stoi(argv[++i]);
            printHeader();
            cmdInfo(pid);
        }
        else if ((arg == "--dir" || arg == "-d") && i + 1 < argc) {
            g_state.checkpointDir = argv[++i];
        }
    }
    
    // Create checkpoint directory
    fs::create_directories(g_state.checkpointDir);
    
    // Load existing checkpoints
    for (const auto& entry : fs::directory_iterator(g_state.checkpointDir)) {
        if (entry.path().extension() == ".chkpt") {
            auto cp = g_state.checkpointer.loadCheckpoint(entry.path().string());
            if (cp && cp->checkpointId > 0) {
                g_state.checkpoints[cp->checkpointId] = *cp;
                if (cp->checkpointId >= g_state.nextCheckpointId) {
                    g_state.nextCheckpointId = cp->checkpointId + 1;
                }
            }
        }
    }
    
    printHeader();
    std::cout << Color::DIM << "  'help' yazarak komutları görebilirsiniz.\n" << Color::RESET;
    std::cout << Color::DIM << "  Checkpoint işlemleri için root yetkisi gerekebilir.\n\n" << Color::RESET;
    
    // Main loop
    std::string input;
    while (g_state.running) {
        printPrompt();
        if (!std::getline(std::cin, input)) {
            break;
        }
        
        // Trim whitespace
        size_t start = input.find_first_not_of(" \t");
        size_t end = input.find_last_not_of(" \t");
        if (start == std::string::npos) continue;
        input = input.substr(start, end - start + 1);
        
        processCommand(input);
    }
    
    std::cout << "\nGüle güle!\n";
    return 0;
}
