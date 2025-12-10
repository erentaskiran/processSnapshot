// Real Process Checkpoint/Restore Test v2
// This version uses a simpler busy-wait loop that better demonstrates restore

#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <cstring>
#include <thread>
#include <chrono>
#include <fstream>

#include "real_process/ptrace_controller.hpp"
#include "real_process/aslr_handler.hpp"
#include "real_process/memory_manager.hpp"

using namespace checkpoint::real_process;

// Simple target program - writes counter to a file every second
const char* TARGET_PROGRAM = R"(
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

volatile int counter = 100;
volatile int running = 1;

void handler(int sig) {
    (void)sig;
    running = 0;
}

int main() {
    signal(SIGTERM, handler);
    
    FILE* f = fopen("/tmp/counter_value.txt", "w");
    if (!f) return 1;
    
    printf("Target started. PID: %d, Initial counter: %d\n", getpid(), counter);
    fflush(stdout);
    
    while(running) {
        // Write counter to file
        rewind(f);
        fprintf(f, "%d", counter);
        fflush(f);
        
        printf("Counter: %d\n", counter);
        fflush(stdout);
        
        counter++;
        
        // Busy wait ~1 second (avoid syscall)
        for(volatile long i = 0; i < 500000000L && running; i++);
    }
    
    fclose(f);
    return 0;
}
)";

void printSeparator(const std::string& title) {
    std::cout << "\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    std::cout << "  " << title << "\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
}

std::string readCounterFile() {
    std::ifstream f("/tmp/counter_value.txt");
    if (!f.is_open()) return "N/A";
    std::string value;
    f >> value;
    return value;
}

int main() {
    printSeparator("Real Process Checkpoint/Restore Test v2");
    
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
    
    FILE* f = fopen("/tmp/target_counter2.c", "w");
    if (!f) {
        std::cerr << "Failed to create target source file\n";
        return 1;
    }
    fprintf(f, "%s", TARGET_PROGRAM);
    fclose(f);
    
    // Compile with -no-pie and -O0 to prevent optimization
    std::cout << "Compiling target program (no-pie, no optimization)...\n";
    int ret = system("gcc -no-pie -O0 -o /tmp/target_counter2 /tmp/target_counter2.c 2>&1");
    if (ret != 0) {
        std::cerr << "Failed to compile target program\n";
        return 1;
    }
    std::cout << "✓ Target program compiled: /tmp/target_counter2\n";
    
    // Remove old counter file
    unlink("/tmp/counter_value.txt");
    
    // Start target process
    printSeparator("Step 2: Starting Target Process");
    
    pid_t targetPid = fork();
    if (targetPid == 0) {
        // Child - exec the program
        execl("/tmp/target_counter2", "target_counter2", nullptr);
        _exit(1);
    }
    
    if (targetPid < 0) {
        std::cerr << "Failed to start target process\n";
        return 1;
    }
    
    std::cout << "✓ Target process started with PID: " << targetPid << "\n";
    
    // Wait for process to start
    std::cout << "\nWaiting 3 seconds for counter to increase...\n";
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    std::cout << "Counter file value: " << readCounterFile() << "\n";
    
    // Create checkpoint
    printSeparator("Step 3: Creating Checkpoint");
    
    RealProcessCheckpointer checkpointer;
    checkpointer.setProgressCallback([](const std::string& stage, double progress) {
        std::cout << "  [" << int(progress * 100) << "%] " << stage << "\n";
    });
    
    CheckpointOptions cpOptions = CheckpointOptions::full();
    
    auto checkpoint = checkpointer.createCheckpoint(targetPid, "counter_checkpoint", cpOptions);
    
    if (!checkpoint) {
        std::cerr << "Failed to create checkpoint: " << checkpointer.getLastError() << "\n";
        kill(targetPid, SIGTERM);
        waitpid(targetPid, nullptr, 0);
        return 1;
    }
    
    std::string checkpointCounterValue = readCounterFile();
    
    std::cout << "\n✓ Checkpoint created!\n";
    std::cout << "  - Counter value at checkpoint: " << checkpointCounterValue << "\n";
    std::cout << "  - Checkpoint ID: " << checkpoint->checkpointId << "\n";
    std::cout << "  - Memory regions: " << checkpoint->memoryMap.size() << "\n";
    std::cout << "  - Memory dumps: " << checkpoint->memoryDumps.size() << "\n";
    std::cout << "  - Dumped memory: " << checkpoint->dumpedMemorySize() << " bytes\n";
    std::cout << "  - RIP: 0x" << std::hex << checkpoint->registers.rip << std::dec << "\n";
    
    // Let process continue running
    printSeparator("Step 4: Letting Process Continue");
    std::cout << "Process will continue running for 5 more seconds...\n";
    std::cout << "(Counter should increase significantly)\n\n";
    
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    std::string preRestoreValue = readCounterFile();
    std::cout << "\nCounter value before restore: " << preRestoreValue << "\n";
    
    // Now restore
    printSeparator("Step 5: Restoring Checkpoint");
    
    std::cout << "Attempting to restore process to checkpoint state...\n";
    std::cout << "Expected: counter should go back to ~" << checkpointCounterValue << "\n\n";
    
    RestoreOptions restoreOptions = RestoreOptions::safe();
    restoreOptions.ignoreMemoryErrors = true;
    restoreOptions.continueAfterRestore = true;
    
    auto result = checkpointer.restoreCheckpointEx(targetPid, *checkpoint, restoreOptions);
    
    std::cout << "\nRestore Result:\n";
    std::cout << "  - Success: " << (result.success ? "Yes" : "No") << "\n";
    std::cout << "  - Registers restored: " << result.registersRestored << "\n";
    std::cout << "  - Memory regions restored: " << result.memoryRegionsRestored << "\n";
    std::cout << "  - Memory regions failed: " << result.memoryRegionsFailed << "\n";
    
    if (!result.warnings.empty()) {
        std::cout << "\n  Warnings:\n";
        for (size_t i = 0; i < std::min(result.warnings.size(), size_t(5)); i++) {
            std::cout << "    - " << result.warnings[i] << "\n";
        }
        if (result.warnings.size() > 5) {
            std::cout << "    ... and " << (result.warnings.size() - 5) << " more\n";
        }
    }
    
    // Let restored process run
    printSeparator("Step 6: Observing Restored Process");
    std::cout << "Watching for 3 seconds...\n\n";
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::string postRestoreValue = readCounterFile();
    std::cout << "Counter value immediately after restore: " << postRestoreValue << "\n";
    
    std::this_thread::sleep_for(std::chrono::seconds(3));
    std::string finalValue = readCounterFile();
    std::cout << "Counter value after 3 seconds: " << finalValue << "\n";
    
    // Cleanup
    printSeparator("Cleanup");
    std::cout << "Stopping target process...\n";
    kill(targetPid, SIGTERM);
    waitpid(targetPid, nullptr, 0);
    std::cout << "✓ Done!\n";
    
    // Summary
    printSeparator("Summary");
    std::cout << "Counter values:\n";
    std::cout << "  - At checkpoint:          " << checkpointCounterValue << "\n";
    std::cout << "  - Before restore:         " << preRestoreValue << "\n";
    std::cout << "  - Immediately after:      " << postRestoreValue << "\n";
    std::cout << "  - 3 seconds after:        " << finalValue << "\n\n";
    
    // Check if restore worked
    int cpVal = std::stoi(checkpointCounterValue);
    int postVal = std::stoi(postRestoreValue);
    int preVal = std::stoi(preRestoreValue);
    
    if (postVal <= cpVal + 2 && postVal < preVal) {
        std::cout << "✅ SUCCESS! Counter was restored to checkpoint value!\n";
        std::cout << "   The counter went from " << preVal << " back to ~" << postVal << "\n";
    } else if (postVal < preVal) {
        std::cout << "⚠️  PARTIAL SUCCESS\n";
        std::cout << "   Counter decreased but not to exact checkpoint value.\n";
    } else {
        std::cout << "❌ FAILED\n";
        std::cout << "   Counter did not reset to checkpoint value.\n";
    }
    
    // Re-enable ASLR reminder
    std::cout << "\n💡 Remember to re-enable ASLR when done:\n";
    std::cout << "   echo 2 | sudo tee /proc/sys/kernel/randomize_va_space\n";
    
    return 0;
}
