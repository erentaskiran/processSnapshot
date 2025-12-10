// Real Process Checkpoint/Restore Test v3
// More detailed test with memory region debugging

#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <cstring>
#include <thread>
#include <chrono>
#include <fstream>
#include <iomanip>

#include "real_process/ptrace_controller.hpp"
#include "real_process/aslr_handler.hpp"
#include "real_process/memory_manager.hpp"

using namespace checkpoint::real_process;

// Even simpler target program - just increment a counter and print
const char* TARGET_PROGRAM = R"(
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

// Global counter in data section
int counter = 100;
volatile int running = 1;

void handler(int sig) {
    (void)sig;
    running = 0;
}

int main() {
    signal(SIGTERM, handler);
    
    printf("PID: %d, counter addr: %p\n", getpid(), &counter);
    fflush(stdout);
    
    while(running) {
        printf("COUNTER=%d\n", counter);
        fflush(stdout);
        
        counter++;
        
        // Busy wait ~2 seconds
        for(volatile long i = 0; i < 1000000000L && running; i++);
    }
    
    return 0;
}
)";

void printSeparator(const std::string& title) {
    std::cout << "\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    std::cout << "  " << title << "\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
}

void printMemoryRegions(const std::vector<MemoryDump>& dumps) {
    std::cout << "\nMemory Dumps:\n";
    std::cout << std::setw(18) << "Start" << " - " 
              << std::setw(18) << "End" << "  "
              << std::setw(8) << "Size" << "  "
              << std::setw(5) << "Perms" << "  "
              << "Path\n";
    std::cout << std::string(80, '-') << "\n";
    
    int idx = 0;
    for (const auto& dump : dumps) {
        std::string perms;
        perms += dump.region.readable ? 'r' : '-';
        perms += dump.region.writable ? 'w' : '-';
        perms += dump.region.executable ? 'x' : '-';
        perms += dump.region.isPrivate ? 'p' : 's';
        
        std::cout << "[" << std::setw(2) << idx++ << "] "
                  << "0x" << std::hex << std::setw(12) << std::setfill('0') << dump.region.startAddr
                  << " - 0x" << std::setw(12) << dump.region.endAddr << std::dec << std::setfill(' ')
                  << "  " << std::setw(8) << dump.region.size()
                  << "  " << perms
                  << "  " << dump.region.pathname
                  << (dump.isValid ? " [VALID]" : " [INVALID]")
                  << "\n";
    }
}

int main() {
    printSeparator("Real Process Checkpoint/Restore Test v3");
    
    // Check ASLR status
    std::cout << getASLRStatusReport() << std::endl;
    
    if (ASLRHandler::isASLREnabled()) {
        std::cout << "\n⚠️  WARNING: ASLR is enabled!\n";
        std::cout << "Please disable ASLR first:\n";
        std::cout << "  echo 0 | sudo tee /proc/sys/kernel/randomize_va_space\n";
        return 1;
    }
    
    // Create target program source file
    printSeparator("Step 1: Creating Target Program");
    
    FILE* f = fopen("/tmp/target_v3.c", "w");
    if (!f) {
        std::cerr << "Failed to create target source file\n";
        return 1;
    }
    fprintf(f, "%s", TARGET_PROGRAM);
    fclose(f);
    
    // Compile with -no-pie and -O0
    std::cout << "Compiling target program...\n";
    int ret = system("gcc -no-pie -O0 -g -o /tmp/target_v3 /tmp/target_v3.c 2>&1");
    if (ret != 0) {
        std::cerr << "Failed to compile target program\n";
        return 1;
    }
    
    // Get info about the binary
    std::cout << "\nBinary info:\n";
    system("file /tmp/target_v3");
    system("readelf -h /tmp/target_v3 2>/dev/null | grep -E 'Type:|Entry'");
    
    // Start target process
    printSeparator("Step 2: Starting Target Process");
    
    int pipefd[2];
    pipe(pipefd);
    
    pid_t targetPid = fork();
    if (targetPid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        execl("/tmp/target_v3", "target_v3", nullptr);
        _exit(1);
    }
    
    close(pipefd[1]);
    
    if (targetPid < 0) {
        std::cerr << "Failed to start target process\n";
        return 1;
    }
    
    std::cout << "✓ Target process started with PID: " << targetPid << "\n";
    
    // Read initial output
    char buf[256];
    FILE* pipeIn = fdopen(pipefd[0], "r");
    
    std::cout << "\nWaiting for process to output some values...\n\n";
    
    // Read first few lines
    for (int i = 0; i < 3 && fgets(buf, sizeof(buf), pipeIn); i++) {
        std::cout << "  Output: " << buf;
    }
    
    // Create checkpoint
    printSeparator("Step 3: Creating Checkpoint");
    
    RealProcessCheckpointer checkpointer;
    checkpointer.setProgressCallback([](const std::string& stage, double progress) {
        if (int(progress * 100) % 10 == 0) {
            std::cout << "  [" << int(progress * 100) << "%] " << stage << "\n";
        }
    });
    
    CheckpointOptions cpOptions = CheckpointOptions::full();
    
    auto checkpoint = checkpointer.createCheckpoint(targetPid, "test_checkpoint", cpOptions);
    
    if (!checkpoint) {
        std::cerr << "Failed to create checkpoint: " << checkpointer.getLastError() << "\n";
        kill(targetPid, SIGTERM);
        waitpid(targetPid, nullptr, 0);
        return 1;
    }
    
    std::cout << "\n✓ Checkpoint created!\n";
    std::cout << "  - Checkpoint ID: " << checkpoint->checkpointId << "\n";
    std::cout << "  - Memory dumps: " << checkpoint->memoryDumps.size() << "\n";
    std::cout << "  - Dumped memory: " << checkpoint->dumpedMemorySize() << " bytes\n";
    std::cout << "  - RIP: 0x" << std::hex << checkpoint->registers.rip << std::dec << "\n";
    
    // Print memory regions
    printMemoryRegions(checkpoint->memoryDumps);
    
    // Show which regions are writable
    int writableCount = 0;
    size_t writableSize = 0;
    for (const auto& dump : checkpoint->memoryDumps) {
        if (dump.region.writable && dump.isValid) {
            writableCount++;
            writableSize += dump.region.size();
        }
    }
    std::cout << "\nWritable regions with data: " << writableCount 
              << " (" << writableSize << " bytes)\n";
    
    // Read the next counter value after checkpoint
    std::cout << "\n--- Values after checkpoint ---\n";
    for (int i = 0; i < 2 && fgets(buf, sizeof(buf), pipeIn); i++) {
        std::cout << "  Output: " << buf;
    }
    
    // Now restore
    printSeparator("Step 4: Restoring Checkpoint");
    
    std::cout << "Restoring process to checkpoint state...\n\n";
    
    RestoreOptions restoreOptions = RestoreOptions::safe();
    restoreOptions.ignoreMemoryErrors = false;  // Want to see errors
    restoreOptions.continueAfterRestore = true;
    restoreOptions.restoreMemory = true;
    restoreOptions.restoreRegisters = true;
    
    auto result = checkpointer.restoreCheckpointEx(targetPid, *checkpoint, restoreOptions);
    
    std::cout << "\nRestore Result:\n";
    std::cout << "  - Success: " << (result.success ? "Yes" : "No") << "\n";
    std::cout << "  - Registers restored: " << result.registersRestored << "\n";
    std::cout << "  - Memory regions restored: " << result.memoryRegionsRestored << "\n";
    std::cout << "  - Memory regions failed: " << result.memoryRegionsFailed << "\n";
    
    if (!result.warnings.empty()) {
        std::cout << "\n  Warnings:\n";
        for (const auto& w : result.warnings) {
            std::cout << "    - " << w << "\n";
        }
    }
    
    if (!result.errorMessage.empty()) {
        std::cout << "\n  Error: " << result.errorMessage << "\n";
    }
    
    // Let restored process run
    printSeparator("Step 5: Observing Restored Process");
    std::cout << "Reading output after restore...\n\n";
    
    for (int i = 0; i < 3 && fgets(buf, sizeof(buf), pipeIn); i++) {
        std::cout << "  Output: " << buf;
    }
    
    // Cleanup
    printSeparator("Cleanup");
    std::cout << "Stopping target process...\n";
    kill(targetPid, SIGTERM);
    waitpid(targetPid, nullptr, 0);
    fclose(pipeIn);
    std::cout << "✓ Done!\n";
    
    // Re-enable ASLR reminder
    std::cout << "\n💡 Remember to re-enable ASLR when done:\n";
    std::cout << "   echo 2 | sudo tee /proc/sys/kernel/randomize_va_space\n";
    
    return 0;
}
