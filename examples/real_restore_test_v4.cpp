// Real Process Checkpoint/Restore Test v4
// Verify memory content before and after restore

#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/select.h>
#include <signal.h>
#include <cstring>
#include <thread>
#include <chrono>
#include <fstream>
#include <iomanip>

#include "real_process/ptrace_controller.hpp"
#include "real_process/aslr_handler.hpp"

using namespace checkpoint::real_process;

// Target program with known counter address
const char* TARGET_PROGRAM = R"(
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

// Counter at fixed location in data section
int counter = 100;
volatile int running = 1;

void handler(int sig) {
    running = 0;
}

int main() {
    signal(SIGTERM, handler);
    
    // Print counter address for verification
    printf("ADDR:%p\n", (void*)&counter);
    fflush(stdout);
    
    while(running) {
        printf("VAL:%d\n", counter);
        fflush(stdout);
        
        counter++;
        
        // Busy wait ~2 seconds
        for(volatile long i = 0; i < 1000000000L && running; i++);
    }
    
    return 0;
}
)";

void printSep(const std::string& t) {
    std::cout << "\n=== " << t << " ===\n";
}

int readMemoryWord(pid_t pid, uint64_t addr) {
    errno = 0;
    long val = ptrace(PTRACE_PEEKDATA, pid, addr, nullptr);
    if (errno != 0) {
        perror("ptrace peek");
        return -1;
    }
    return (int)val;
}

int main() {
    printSep("Real Process Test v4 - Memory Verification");
    
    if (ASLRHandler::isASLREnabled()) {
        std::cout << "ERROR: ASLR enabled. Run: echo 0 | sudo tee /proc/sys/kernel/randomize_va_space\n";
        return 1;
    }
    
    // Create and compile target
    FILE* f = fopen("/tmp/target_v4.c", "w");
    fprintf(f, "%s", TARGET_PROGRAM);
    fclose(f);
    
    system("gcc -no-pie -O0 -o /tmp/target_v4 /tmp/target_v4.c 2>/dev/null");
    
    // Start target
    int pipefd[2];
    pipe(pipefd);
    
    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        execl("/tmp/target_v4", "target_v4", nullptr);
        _exit(1);
    }
    
    close(pipefd[1]);
    FILE* pipeIn = fdopen(pipefd[0], "r");
    
    std::cout << "Started target PID: " << pid << "\n";
    
    // Read address line
    char buf[256];
    fgets(buf, sizeof(buf), pipeIn);
    
    uint64_t counterAddr = 0;
    if (sscanf(buf, "ADDR:%lx", &counterAddr) != 1) {
        std::cerr << "Failed to read counter address\n";
        kill(pid, SIGTERM);
        return 1;
    }
    std::cout << "Counter address: 0x" << std::hex << counterAddr << std::dec << "\n";
    
    // Read some values
    for (int i = 0; i < 2 && fgets(buf, sizeof(buf), pipeIn); i++) {
        std::cout << "  " << buf;
    }
    
    // Create checkpoint
    printSep("Creating Checkpoint");
    
    RealProcessCheckpointer checkpointer;
    CheckpointOptions opts = CheckpointOptions::full();
    
    auto checkpoint = checkpointer.createCheckpoint(pid, "test", opts);
    if (!checkpoint) {
        std::cerr << "Checkpoint failed: " << checkpointer.getLastError() << "\n";
        kill(pid, SIGTERM);
        return 1;
    }
    
    std::cout << "Checkpoint created. RIP: 0x" << std::hex << checkpoint->registers.rip << std::dec << "\n";
    
    // Find and display the counter value in checkpoint memory
    int checkpointCounterValue = -1;
    for (const auto& dump : checkpoint->memoryDumps) {
        if (counterAddr >= dump.region.startAddr && counterAddr < dump.region.endAddr && dump.isValid) {
            size_t offset = counterAddr - dump.region.startAddr;
            if (offset + sizeof(int) <= dump.data.size()) {
                memcpy(&checkpointCounterValue, dump.data.data() + offset, sizeof(int));
            }
            break;
        }
    }
    std::cout << "Counter value in checkpoint memory: " << checkpointCounterValue << "\n";
    
    // Let process continue
    printSep("Letting Process Run");
    
    for (int i = 0; i < 3 && fgets(buf, sizeof(buf), pipeIn); i++) {
        std::cout << "  " << buf;
    }
    
    // Attach and read current counter value
    ptrace(PTRACE_ATTACH, pid, nullptr, nullptr);
    waitpid(pid, nullptr, 0);
    int preRestoreValue = readMemoryWord(pid, counterAddr);
    std::cout << "Counter in process memory before restore: " << preRestoreValue << "\n";
    ptrace(PTRACE_DETACH, pid, nullptr, nullptr);
    
    // Restore
    printSep("Restoring Checkpoint");
    
    RestoreOptions restoreOpts = RestoreOptions::safe();
    restoreOpts.continueAfterRestore = true;
    
    auto result = checkpointer.restoreCheckpointEx(pid, *checkpoint, restoreOpts);
    
    std::cout << "Restore success: " << (result.success ? "Yes" : "No") << "\n";
    std::cout << "Memory regions restored: " << result.memoryRegionsRestored << "\n";
    
    // Can't re-attach immediately without special handling
    // Instead, check the next output from the process
    printSep("Checking Process Output After Restore");
    
    // Set read timeout with select
    fd_set readfds;
    struct timeval tv;
    
    for (int i = 0; i < 5; i++) {
        FD_ZERO(&readfds);
        FD_SET(pipefd[0], &readfds);
        tv.tv_sec = 3;
        tv.tv_usec = 0;
        
        int ret = select(pipefd[0] + 1, &readfds, nullptr, nullptr, &tv);
        if (ret > 0) {
            if (fgets(buf, sizeof(buf), pipeIn)) {
                int val = 0;
                if (sscanf(buf, "VAL:%d", &val) == 1) {
                    std::cout << "  Output: VAL:" << val << "\n";
                    
                    if (i == 0) {
                        // First value after restore
                        printSep("Summary");
                        std::cout << "Counter at checkpoint:      " << checkpointCounterValue << "\n";
                        std::cout << "Counter before restore:     " << preRestoreValue << "\n";
                        std::cout << "First output after restore: " << val << "\n";
                        
                        if (val == checkpointCounterValue || val == checkpointCounterValue - 1) {
                            std::cout << "\n✅ SUCCESS! Counter was restored to checkpoint value!\n";
                        } else if (val < preRestoreValue) {
                            std::cout << "\n⚠️  PARTIAL: Counter decreased from " << preRestoreValue 
                                      << " to " << val 
                                      << " (expected ~" << checkpointCounterValue << ")\n";
                        } else {
                            std::cout << "\n❌ FAILED: Counter was not restored\n";
                        }
                        break;
                    }
                } else {
                    std::cout << "  Output: " << buf;
                }
            }
        } else if (ret == 0) {
            std::cout << "  Timeout waiting for output\n";
            break;
        } else {
            perror("select");
            break;
        }
    }
    
    // Cleanup
    kill(pid, SIGTERM);
    waitpid(pid, nullptr, 0);
    fclose(pipeIn);
    
    return 0;
}
