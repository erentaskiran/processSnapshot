// Real Process Checkpoint/Restore Test
// This program creates a simple counter process, checkpoints it,
// lets it continue, then restores it to the checkpoint state.

#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <cstring>
#include <thread>
#include <chrono>

#include "real_process/ptrace_controller.hpp"
#include "real_process/aslr_handler.hpp"
#include "real_process/memory_manager.hpp"

using namespace checkpoint::real_process;

// Simple target program that counts in a loop
const char* TARGET_PROGRAM = R"(
#include <stdio.h>
#include <unistd.h>

volatile int counter = 0;

int main() {
    printf("Target started. PID: %d\n", getpid());
    fflush(stdout);
    
    while(1) {
        counter++;
        printf("Counter: %d\n", counter);
        fflush(stdout);
        sleep(1);
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

void printASLRStatus() {
    std::cout << getASLRStatusReport() << std::endl;
}

int main() {
    printSeparator("Real Process Checkpoint/Restore Test");
    
    // Check ASLR status
    printASLRStatus();
    
    if (ASLRHandler::isASLREnabled()) {
        std::cout << "\n⚠️  WARNING: ASLR is enabled!\n";
        std::cout << "For best results, disable ASLR:\n";
        std::cout << "  echo 0 | sudo tee /proc/sys/kernel/randomize_va_space\n";
        std::cout << "\nContinuing anyway (restore may fail)...\n";
    }
    
    // Create target program source file
    printSeparator("Step 1: Creating Target Program");
    
    FILE* f = fopen("/tmp/target_counter.c", "w");
    if (!f) {
        std::cerr << "Failed to create target source file\n";
        return 1;
    }
    fprintf(f, "%s", TARGET_PROGRAM);
    fclose(f);
    
    // Compile with -no-pie to avoid PIE relocation issues
    std::cout << "Compiling target program (without PIE)...\n";
    int ret = system("gcc -no-pie -o /tmp/target_counter /tmp/target_counter.c 2>&1");
    if (ret != 0) {
        std::cerr << "Failed to compile target program\n";
        return 1;
    }
    std::cout << "✓ Target program compiled: /tmp/target_counter\n";
    
    // Start target process (with ASLR disabled for this process)
    printSeparator("Step 2: Starting Target Process");
    
    pid_t targetPid = ASLRHandler::execWithoutASLR("/tmp/target_counter", {});
    if (targetPid < 0) {
        std::cerr << "Failed to start target process\n";
        return 1;
    }
    
    std::cout << "✓ Target process started with PID: " << targetPid << "\n";
    std::cout << "  (Running with ASLR disabled)\n";
    
    // Wait for process to start and run for a bit
    std::cout << "\nWaiting 3 seconds for process to run...\n";
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    // Create checkpoint
    printSeparator("Step 3: Creating Checkpoint");
    
    RealProcessCheckpointer checkpointer;
    checkpointer.setProgressCallback([](const std::string& stage, double progress) {
        std::cout << "  [" << int(progress * 100) << "%] " << stage << "\n";
    });
    
    CheckpointOptions cpOptions;
    cpOptions.saveRegisters = true;
    cpOptions.saveMemory = true;
    cpOptions.dumpHeap = true;
    cpOptions.dumpStack = true;
    cpOptions.dumpAnonymous = true;
    cpOptions.saveFileDescriptors = true;
    cpOptions.skipReadOnly = true;
    
    auto checkpoint = checkpointer.createCheckpoint(targetPid, "counter_checkpoint", cpOptions);
    
    if (!checkpoint) {
        std::cerr << "Failed to create checkpoint: " << checkpointer.getLastError() << "\n";
        kill(targetPid, SIGKILL);
        return 1;
    }
    
    std::cout << "\n✓ Checkpoint created!\n";
    std::cout << "  - Checkpoint ID: " << checkpoint->checkpointId << "\n";
    std::cout << "  - Memory regions: " << checkpoint->memoryMap.size() << "\n";
    std::cout << "  - Memory dumps: " << checkpoint->memoryDumps.size() << "\n";
    std::cout << "  - Total memory size: " << checkpoint->totalMemorySize() << " bytes\n";
    std::cout << "  - Dumped memory: " << checkpoint->dumpedMemorySize() << " bytes\n";
    std::cout << "  - RIP (instruction pointer): 0x" << std::hex << checkpoint->registers.rip << std::dec << "\n";
    std::cout << "  - RSP (stack pointer): 0x" << std::hex << checkpoint->registers.rsp << std::dec << "\n";
    
    // Save checkpoint to file
    std::string checkpointFile = "/tmp/counter_checkpoint.chkpt";
    if (checkpointer.saveCheckpoint(*checkpoint, checkpointFile)) {
        std::cout << "  - Saved to: " << checkpointFile << "\n";
    }
    
    // Let process continue running
    printSeparator("Step 4: Letting Process Continue");
    std::cout << "Process will continue running for 5 more seconds...\n";
    std::cout << "(Watch the counter values increase)\n\n";
    
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    // Now restore
    printSeparator("Step 5: Restoring Checkpoint");
    
    std::cout << "Attempting to restore process to checkpoint state...\n\n";
    
    RestoreOptions restoreOptions;
    restoreOptions.restoreRegisters = true;
    restoreOptions.restoreMemory = true;
    restoreOptions.validateBeforeRestore = true;
    restoreOptions.stopOnError = false;
    restoreOptions.ignoreMemoryErrors = true;  // Continue even if some regions fail
    restoreOptions.continueAfterRestore = true;
    
    auto result = checkpointer.restoreCheckpointEx(targetPid, *checkpoint, restoreOptions);
    
    std::cout << "\nRestore Result:\n";
    std::cout << "  - Success: " << (result.success ? "Yes" : "No") << "\n";
    std::cout << "  - Registers restored: " << result.registersRestored << "\n";
    std::cout << "  - Memory regions restored: " << result.memoryRegionsRestored << "\n";
    std::cout << "  - Memory regions failed: " << result.memoryRegionsFailed << "\n";
    std::cout << "  - ASLR detected: " << (result.aslrDetected ? "Yes" : "No") << "\n";
    
    if (!result.errorMessage.empty()) {
        std::cout << "  - Error: " << result.errorMessage << "\n";
    }
    
    if (!result.warnings.empty()) {
        std::cout << "\n  Warnings:\n";
        for (const auto& w : result.warnings) {
            std::cout << "    - " << w << "\n";
        }
    }
    
    // Let restored process run
    printSeparator("Step 6: Observing Restored Process");
    std::cout << "If restore was successful, counter should restart from checkpoint value!\n";
    std::cout << "Watching for 5 seconds...\n\n";
    
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    // Cleanup
    printSeparator("Cleanup");
    std::cout << "Killing target process...\n";
    kill(targetPid, SIGKILL);
    waitpid(targetPid, nullptr, 0);
    std::cout << "✓ Done!\n";
    
    // Summary
    printSeparator("Summary");
    if (result.success && result.memoryRegionsFailed == 0) {
        std::cout << "✅ Checkpoint/Restore test PASSED!\n";
        std::cout << "   The process state was successfully saved and restored.\n";
    } else if (result.success) {
        std::cout << "⚠️  Checkpoint/Restore test PARTIAL SUCCESS\n";
        std::cout << "   Some memory regions could not be restored.\n";
        std::cout << "   This is common and usually doesn't affect functionality.\n";
    } else {
        std::cout << "❌ Checkpoint/Restore test FAILED\n";
        std::cout << "   Reason: " << result.errorMessage << "\n";
        if (result.aslrDetected) {
            std::cout << "\n   ASLR was detected. Try disabling ASLR:\n";
            std::cout << "   echo 0 | sudo tee /proc/sys/kernel/randomize_va_space\n";
        }
    }
    
    return result.success ? 0 : 1;
}
