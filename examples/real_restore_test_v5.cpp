// Real Process Checkpoint/Restore Test v5
// Check process state after restore

#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/select.h>
#include <sys/user.h>
#include <signal.h>
#include <cstring>
#include <thread>
#include <chrono>
#include <fstream>
#include <iomanip>

#include "real_process/ptrace_controller.hpp"
#include "real_process/aslr_handler.hpp"

using namespace checkpoint::real_process;

// Simpler target - no busy wait, use sleep
const char* TARGET_PROGRAM = R"(
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

int counter = 100;
volatile int running = 1;

void handler(int sig) { running = 0; }

int main() {
    signal(SIGTERM, handler);
    printf("ADDR:%p\n", (void*)&counter);
    fflush(stdout);
    
    while(running) {
        printf("VAL:%d\n", counter);
        fflush(stdout);
        counter++;
        sleep(1);  // Use sleep instead of busy-wait
    }
    return 0;
}
)";

int main() {
    std::cout << "=== Real Process Test v5 ===\n\n";
    
    if (ASLRHandler::isASLREnabled()) {
        std::cout << "ERROR: ASLR enabled\n";
        return 1;
    }
    
    // Create and compile
    FILE* f = fopen("/tmp/target_v5.c", "w");
    fprintf(f, "%s", TARGET_PROGRAM);
    fclose(f);
    system("gcc -no-pie -O0 -o /tmp/target_v5 /tmp/target_v5.c 2>/dev/null");
    
    // Create pipes for stdout and stderr
    int pipefd[2];
    pipe(pipefd);
    
    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        execl("/tmp/target_v5", "target_v5", nullptr);
        _exit(1);
    }
    close(pipefd[1]);
    
    std::cout << "Started PID: " << pid << "\n";
    
    // Read address
    char buf[256];
    FILE* pipeIn = fdopen(pipefd[0], "r");
    fgets(buf, sizeof(buf), pipeIn);
    
    uint64_t counterAddr = 0;
    sscanf(buf, "ADDR:%lx", &counterAddr);
    std::cout << "Counter at: 0x" << std::hex << counterAddr << std::dec << "\n";
    
    // Wait for some output
    for (int i = 0; i < 2 && fgets(buf, sizeof(buf), pipeIn); i++) {
        std::cout << "  " << buf;
    }
    
    // Create checkpoint
    std::cout << "\n--- Creating Checkpoint ---\n";
    RealProcessCheckpointer checkpointer;
    auto checkpoint = checkpointer.createCheckpoint(pid, "test", CheckpointOptions::full());
    
    if (!checkpoint) {
        std::cerr << "Checkpoint failed: " << checkpointer.getLastError() << "\n";
        kill(pid, SIGKILL);
        return 1;
    }
    
    // Find checkpoint counter value
    int cpCounterValue = -1;
    for (const auto& dump : checkpoint->memoryDumps) {
        if (counterAddr >= dump.region.startAddr && counterAddr < dump.region.endAddr && dump.isValid) {
            size_t offset = counterAddr - dump.region.startAddr;
            memcpy(&cpCounterValue, dump.data.data() + offset, sizeof(int));
            break;
        }
    }
    
    std::cout << "Checkpoint RIP: 0x" << std::hex << checkpoint->registers.rip << std::dec << "\n";
    std::cout << "Checkpoint counter: " << cpCounterValue << "\n";
    
    // Let process run more
    std::cout << "\n--- Process continues ---\n";
    for (int i = 0; i < 3 && fgets(buf, sizeof(buf), pipeIn); i++) {
        std::cout << "  " << buf;
    }
    
    // Attach and read before restore
    ptrace(PTRACE_ATTACH, pid, nullptr, nullptr);
    waitpid(pid, nullptr, 0);
    
    // Read current counter
    errno = 0;
    long val = ptrace(PTRACE_PEEKDATA, pid, counterAddr, nullptr);
    int preRestoreVal = (int)val;
    std::cout << "Before restore counter: " << preRestoreVal << "\n";
    
    // Now restore while still attached
    std::cout << "\n--- Restoring ---\n";
    
    // Set registers directly via ptrace
    struct user_regs_struct regs;
    regs.r15 = checkpoint->registers.r15;
    regs.r14 = checkpoint->registers.r14;
    regs.r13 = checkpoint->registers.r13;
    regs.r12 = checkpoint->registers.r12;
    regs.rbp = checkpoint->registers.rbp;
    regs.rbx = checkpoint->registers.rbx;
    regs.r11 = checkpoint->registers.r11;
    regs.r10 = checkpoint->registers.r10;
    regs.r9 = checkpoint->registers.r9;
    regs.r8 = checkpoint->registers.r8;
    regs.rax = checkpoint->registers.rax;
    regs.rcx = checkpoint->registers.rcx;
    regs.rdx = checkpoint->registers.rdx;
    regs.rsi = checkpoint->registers.rsi;
    regs.rdi = checkpoint->registers.rdi;
    regs.orig_rax = checkpoint->registers.orig_rax;
    regs.rip = checkpoint->registers.rip;
    regs.cs = checkpoint->registers.cs;
    regs.eflags = checkpoint->registers.eflags;
    regs.rsp = checkpoint->registers.rsp;
    regs.ss = checkpoint->registers.ss;
    regs.fs_base = checkpoint->registers.fs_base;
    regs.gs_base = checkpoint->registers.gs_base;
    regs.ds = checkpoint->registers.ds;
    regs.es = checkpoint->registers.es;
    regs.fs = checkpoint->registers.fs;
    regs.gs = checkpoint->registers.gs;
    
    if (ptrace(PTRACE_SETREGS, pid, nullptr, &regs) == -1) {
        perror("PTRACE_SETREGS");
    } else {
        std::cout << "Set registers: OK\n";
    }
    
    // Restore writable memory regions
    int restored = 0, failed = 0;
    for (const auto& dump : checkpoint->memoryDumps) {
        if (!dump.region.writable || !dump.isValid) continue;
        
        // Write memory word by word
        const uint8_t* data = dump.data.data();
        uint64_t addr = dump.region.startAddr;
        size_t size = dump.data.size();
        
        bool regionFailed = false;
        for (size_t i = 0; i < size; i += sizeof(uint64_t)) {
            uint64_t word = 0;
            size_t copySize = std::min(sizeof(uint64_t), size - i);
            memcpy(&word, data + i, copySize);
            
            if (ptrace(PTRACE_POKEDATA, pid, addr + i, word) == -1) {
                regionFailed = true;
                break;
            }
        }
        
        if (regionFailed) {
            failed++;
        } else {
            restored++;
        }
    }
    std::cout << "Restored " << restored << " regions, failed " << failed << "\n";
    
    // Read counter after restore (before continuing)
    val = ptrace(PTRACE_PEEKDATA, pid, counterAddr, nullptr);
    int postRestoreVal = (int)val;
    std::cout << "After restore counter: " << postRestoreVal << "\n";
    
    // Continue process
    std::cout << "\n--- Continuing process ---\n";
    if (ptrace(PTRACE_CONT, pid, nullptr, 0) == -1) {
        perror("PTRACE_CONT");
    }
    
    // Check if process is still alive
    int status;
    pid_t result = waitpid(pid, &status, WNOHANG);
    if (result == 0) {
        std::cout << "Process still running\n";
    } else if (result > 0) {
        if (WIFEXITED(status)) {
            std::cout << "Process exited with: " << WEXITSTATUS(status) << "\n";
        } else if (WIFSIGNALED(status)) {
            std::cout << "Process killed by signal: " << WTERMSIG(status) << "\n";
        } else if (WIFSTOPPED(status)) {
            std::cout << "Process stopped by signal: " << WSTOPSIG(status) << "\n";
        }
    }
    
    // Try to read more output
    std::cout << "\n--- Output after restore ---\n";
    
    fd_set fds;
    struct timeval tv;
    
    for (int i = 0; i < 5; i++) {
        FD_ZERO(&fds);
        FD_SET(pipefd[0], &fds);
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        
        int ret = select(pipefd[0] + 1, &fds, nullptr, nullptr, &tv);
        if (ret > 0) {
            if (fgets(buf, sizeof(buf), pipeIn)) {
                std::cout << "  " << buf;
            }
        } else if (ret == 0) {
            std::cout << "  [timeout]\n";
            
            // Check process state
            result = waitpid(pid, &status, WNOHANG);
            if (result > 0) {
                if (WIFSIGNALED(status)) {
                    std::cout << "Process crashed with signal: " << WTERMSIG(status) << "\n";
                }
            }
            break;
        }
    }
    
    // Summary
    std::cout << "\n=== Summary ===\n";
    std::cout << "Checkpoint counter: " << cpCounterValue << "\n";
    std::cout << "Pre-restore counter: " << preRestoreVal << "\n";
    std::cout << "Post-restore counter: " << postRestoreVal << "\n";
    
    if (postRestoreVal == cpCounterValue) {
        std::cout << "\n✅ Counter correctly restored to checkpoint value!\n";
    } else {
        std::cout << "\n❌ Counter mismatch\n";
    }
    
    // Cleanup
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
    fclose(pipeIn);
    
    return 0;
}
