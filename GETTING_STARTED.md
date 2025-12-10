# Getting Started with State Checkpoint System

## 📖 Introduction

Welcome to the **State Checkpoint System** project! This document will guide you through understanding the project architecture, codebase structure, and how to get started as a new contributor or user.

### What is This Project?

This is an Operating Systems course project that implements a **process state checkpoint and rollback system**. The system can:
- Simulate process execution with a custom instruction set
- Save process states at any point (checkpoint)
- Restore process states to previous checkpoints (rollback)
- Track and log all state changes
- Work with both simulated and real processes

## 🎯 Quick Start Guide

### 1. Environment Setup (Arch Linux VM)

This project requires Arch Linux environment. Follow these steps to set up the development environment:

#### Step 1: Download Arch Linux VM

Download the pre-configured Arch Linux environment from:
**[Download arch.zip](https://drive.google.com/file/d/1hLlEgRtGzY1hLI_UVXxlPyi54dCjAiei/view?usp=share_link)** 

#### Step 2: Extract the Archive

```bash
# Extract the downloaded file
unzip arch.zip
cd arch
```

#### Step 3: Start the Arch Linux VM

Choose the appropriate script based on your operating system:

**For Windows:**
```powershell
# Run PowerShell script
.\start_arch.ps1
```

**For Linux/macOS:**
```bash
# Make script executable (first time only)
chmod +x start_arch.sh

# Run the script
./start_arch.sh
```

#### Step 4: Clone the Project

Once inside the Arch Linux VM:

```bash
# Clone the repository
git clone https://github.com/erentaskiran/processSnapshot.git

# Navigate to project directory
cd processSnapshot
```

### 2. First Things to Read

Start by reading these documents in order:

1. **README.md** - Project overview, features, and basic usage
2. **GETTING_STARTED.md** (this file) - Detailed onboarding guide
3. **GOREV_DAGILIMI.md** - Task distribution and project structure (if available)

### 3. Build the Project

```bash
# You should already be in the project directory (processSnapshot)
# If not: cd processSnapshot

# Create build directory
mkdir -p build && cd build

# Configure with CMake
cmake ..

# Build the project
make -j$(nproc)

# Run tests to verify everything works
./bin/checkpoint_tests
```

### 4. Run Your First Example

```bash
# Simple checkpoint example
./bin/simple_example

# Process simulation with checkpoints
./bin/process_demo

# Auto-save checkpoint example
./bin/auto_save_example
```

## 🏗️ Project Architecture

### High-Level Overview

```
┌─────────────────────────────────────────────────────────────┐
│                     Application Layer                       │
│           (Examples, CLI Tools, Process Demos)              │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌────────────────────────────────────────────────────────────┐
│                    Core Components                         │
├──────────────────┬──────────────────┬──────────────────────┤
│  State Manager   │  Rollback Engine │  Operation Logger    │
│  (Checkpointing) │  (Restoration)   │  (Tracking)          │
└──────────────────┴──────────────────┴──────────────────────┘
                            ↓
┌────────────────────────────────────────────────────────────┐
│                   Low-Level Systems                        │
├──────────────────┬──────────────────┬──────────────────────┤
│  Serializer      │  Storage         │  Process Simulator   │
│  (Data Format)   │  (File I/O)      │  (Execution)         │
└──────────────────┴──────────────────┴──────────────────────┘
```

### Key Components

#### 1. **Core Module** (`include/core/`, `src/core/`)

The foundation of the system:

- **types.hpp**: Basic types and data structures
  - `StateData`: Binary state data representation
  - `CheckpointID`: Unique identifier for checkpoints
  - `Result<T>`: Error handling wrapper
  
- **serializer.hpp**: Serialization/deserialization of state data
  - Binary format conversion
  - Metadata handling
  
- **exceptions.hpp**: Custom exception types
  - `CheckpointException`
  - `SerializationException`
  - `StorageException`

**Start here if you're new!** These define the fundamental building blocks.

#### 2. **State Module** (`include/state/`, `src/state/`)

Manages checkpoint creation and retrieval:

- **state_manager.hpp**: Main interface for checkpoint operations
  - `createCheckpoint()`: Save current state
  - `getCheckpoint()`: Retrieve saved state
  - `listCheckpoints()`: View all checkpoints
  - `deleteCheckpoint()`: Remove checkpoint
  
- **storage.hpp**: File system storage backend
  - Checkpoint file format
  - Metadata persistence

**Key class to understand:** `StateManager`

#### 3. **Process Module** (`include/process/`, `src/process/`)

Simulates process execution:

- **process_types.hpp**: Process data structures
  - `PCB` (Process Control Block): PID, state, priority, registers, CPU time
  - `RegisterSet`: 16 general-purpose registers + PC, SP, BP, FLAGS
  - `Memory`: Code, Data, Heap, Stack segments
  - `PageTable`: Virtual memory simulation
  
- **instruction_set.hpp**: Custom assembly language
  - 40+ instructions (LOAD, STORE, ADD, SUB, JMP, CALL, etc.)
  - Assembler for instruction creation
  
- **process_simulator.hpp**: Process execution engine
  - Execute instructions
  - Manage memory
  - Handle system calls

**This is the most complex module.** Study the examples first!

#### 4. **Rollback Module** (`include/rollback/`, `src/rollback/`)

Handles state restoration:

- **rollback_engine.hpp**: Rollback operations
  - Full state restoration
  - Partial rollback support
  - Rollback history tracking

#### 5. **Logger Module** (`include/logger/`, `src/logger/`)

Tracks all operations:

- **operation_logger.hpp**: Operation logging
  - Log checkpoint creation
  - Log rollback operations
  - Query operation history

#### 6. **Real Process Module** (`include/real_process/`, `src/real_process/`)

Works with actual Linux processes (fully functional!):

- **ptrace_controller.hpp**: Process control via ptrace
  - `createCheckpoint()`: Save process state (registers + memory)
  - `restoreCheckpointEx()`: Restore process to checkpoint state
- **proc_reader.hpp**: Read process information from `/proc`
- **real_process_types.hpp**: Real process data structures
- **memory_manager.hpp**: Memory region management
- **aslr_handler.hpp**: ASLR detection and handling
- **fd_restorer.hpp**: File descriptor restoration

**Prerequisites for Real Process Restore:**
```bash
# Disable ASLR (required)
echo 0 | sudo tee /proc/sys/kernel/randomize_va_space

# Compile target with -no-pie -O0 for consistent addresses
gcc -no-pie -O0 -o target target.c

# Run restore test (requires root)
sudo ./bin/real_restore_test_v5
```

## 📚 Understanding the Codebase

### Step-by-Step Learning Path

#### Level 1: Basics (Start Here!)

1. **Read the simple example** (`examples/simple_example.cpp`)
   - Shows basic checkpoint creation and retrieval
   - ~50 lines of code
   - Good introduction to `StateManager` API

2. **Study core types** (`include/core/types.hpp`)
   - Understand `StateData`, `CheckpointID`, `Result<T>`
   - These are used everywhere

3. **Run and modify simple_example**
   - Try creating multiple checkpoints
   - Experiment with checkpoint names
   - Print checkpoint metadata

#### Level 2: Process Simulation

1. **Study process types** (`include/process/process_types.hpp`)
   - Understand `PCB` structure
   - Learn about register layout
   - See how memory is organized

2. **Read instruction set** (`include/process/instruction_set.hpp`)
   - Learn available instructions
   - Understand instruction format
   - Study the `Opcode` enum

3. **Run process_demo** (`examples/process_demo.cpp`)
   - Watch process execution
   - See checkpoints in action
   - Observe rollback behavior

4. **Create your own program**
   - Write a simple program using the instruction set
   - Add checkpoint calls
   - Test rollback functionality

#### Level 3: Advanced Features

1. **Study auto-save** (`examples/auto_save_example.cpp`)
   - Periodic automatic checkpointing
   - Time-based snapshots

2. **Explore partial rollback** (`examples/partial_rollback_example.cpp`)
   - Selective state restoration
   - Rollback history management

3. **Real process checkpointing** (`examples/real_process_demo.cpp`)
   - Checkpoint actual Linux processes
   - Use ptrace for process control

4. **Real process restore test** (`examples/real_restore_test_v5.cpp`)
   - Full checkpoint/restore of a running process
   - Demonstrates memory and register restoration
   - **Working test with verified results!**

#### Level 4: Internals

1. **Serialization** (`src/core/serializer.cpp`)
   - Binary format details
   - Metadata encoding

2. **Storage** (`src/state/storage.cpp`)
   - File system layout
   - Checkpoint file format

3. **Rollback engine** (`src/rollback/rollback_engine.cpp`)
   - State restoration algorithm
   - Consistency checks

## 🔍 Important Files and Their Purpose

### Must-Read Files

| File | Purpose | Complexity | Priority |
|------|---------|------------|----------|
| `README.md` | Project overview | Low | ⭐⭐⭐⭐⭐ |
| `examples/simple_example.cpp` | Basic usage | Low | ⭐⭐⭐⭐⭐ |
| `include/core/types.hpp` | Core types | Low | ⭐⭐⭐⭐⭐ |
| `include/state/state_manager.hpp` | Main API | Medium | ⭐⭐⭐⭐ |
| `examples/process_demo.cpp` | Process simulation | Medium | ⭐⭐⭐⭐ |
| `include/process/process_types.hpp` | Process structures | Medium | ⭐⭐⭐⭐ |
| `include/process/instruction_set.hpp` | Instructions | High | ⭐⭐⭐ |
| `src/process/process_simulator.cpp` | Execution engine | High | ⭐⭐⭐ |
| `examples/real_restore_test_v5.cpp` | Real process restore | Medium | ⭐⭐⭐⭐ |
| `include/real_process/ptrace_controller.hpp` | Ptrace API | High | ⭐⭐⭐ |

### Configuration Files

- **CMakeLists.txt**: Build configuration
  - Compiler settings (C++20)
  - Dependencies (Google Test)
  - Build options

- **build.sh**: Build script (if exists)
  - Automated build process

## 🎓 Key Concepts to Understand

### 1. Checkpoint

A **checkpoint** is a snapshot of a process state at a specific point in time. It includes:
- Process Control Block (PCB)
- Register values
- Memory contents (Code, Data, Heap, Stack)
- Page table
- Metadata (timestamp, name)

### 2. Rollback

**Rollback** is the process of restoring a previous checkpoint, effectively "undoing" changes made since that checkpoint was created.

### 3. StateManager

The main interface for checkpoint operations. Usage pattern:

```cpp
StateManager manager("checkpoint_dir/");

// Create checkpoint
StateData data = getCurrentState();
auto result = manager.createCheckpoint("my_checkpoint", data);

// Restore checkpoint
auto checkpoint = manager.getCheckpoint(checkpoint_id);
restoreState(checkpoint->getData());
```

### 4. Process Control Block (PCB)

The PCB contains all information about a process:
- **PID**: Process ID
- **State**: NEW, READY, RUNNING, BLOCKED, TERMINATED
- **Registers**: CPU context
- **Memory**: Code/Data/Heap/Stack
- **Statistics**: CPU time, instruction count, etc.

### 5. Instruction Set Architecture (ISA)

Custom 16-bit instruction set with:
- 16 general-purpose registers (R0-R15)
- Special registers: PC, SP, BP, FLAGS
- 40+ instructions organized into categories:
  - Data movement (LOAD, STORE, MOV)
  - Arithmetic (ADD, SUB, MUL, DIV)
  - Bitwise (AND, OR, XOR, SHL, SHR)
  - Control flow (JMP, CALL, RET)
  - System (SYSCALL, HALT)

## 🛠️ Development Workflow

### Adding a New Feature

1. **Plan**: Identify which module(s) need changes
2. **Interface**: Update header files in `include/`
3. **Implementation**: Write code in `src/`
4. **Testing**: Add tests in `tests/`
5. **Example**: Create example in `examples/`
6. **Documentation**: Update README.md

### Testing Your Changes

```bash
# Build with tests
cd build
cmake -DBUILD_TESTS=ON ..
make

# Run all tests
./bin/checkpoint_tests

# Run specific test
./bin/checkpoint_tests --gtest_filter=StateManagerTest.*

# Run with verbose output
./bin/checkpoint_tests --gtest_print_time=1
```

### Debugging

```bash
# Build with debug symbols
cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make

# Run with GDB
gdb ./bin/process_demo

# Or use Valgrind for memory issues
valgrind --leak-check=full ./bin/process_demo
```

## 📝 Common Tasks

### How to Create a Simple Checkpoint

```cpp
#include "state/state_manager.hpp"

StateManager manager("checkpoints/");
std::string data = "Hello, checkpoint!";
StateData state(data.begin(), data.end());
auto result = manager.createCheckpoint("test", state);
```

### How to Create a Process Program

```cpp
#include "process/instruction_set.hpp"

std::vector<Instruction> program;

// R0 = 5
program.push_back(Instruction(Opcode::LOAD_IMM, 0, 5));

// R1 = 10
program.push_back(Instruction(Opcode::LOAD_IMM, 1, 10));

// R2 = R0 + R1
program.push_back(Instruction(Opcode::ADD, 2, 0, 1));

// Print R2
program.push_back(Instruction(Opcode::PRINT, 2));

// Halt
program.push_back(Instruction(Opcode::HALT));
```

### How to Checkpoint a Running Process

```cpp
#include "process/process_simulator.hpp"
#include "state/state_manager.hpp"

ProcessSimulator sim(1001, "MyProcess");
sim.loadProgram(program);

// Execute some instructions
sim.step();  // Execute one instruction
sim.step();

// Create checkpoint
auto checkpoint = sim.createCheckpoint("after_step_2");

// Continue execution
sim.run();

// Rollback if needed
sim.restoreCheckpoint(checkpoint);
```

## 🤝 Contributing

### Code Style

- Use C++20 features
- Follow existing naming conventions:
  - Classes: `PascalCase`
  - Functions: `camelCase`
  - Variables: `camelCase`
  - Constants: `UPPER_CASE`
- Use header guards: `#pragma once`
- Document public APIs with comments

### Before Submitting

1. Ensure code compiles without warnings
2. Run all tests successfully
3. Format code consistently
4. Update documentation
5. Add example if introducing new feature

## 📞 Getting Help

### Troubleshooting

**Build fails with C++20 errors:**
- Ensure GCC 12+ or Clang 14+
- Check CMake version (3.20+)

**Tests fail:**
- Check if checkpoint directories exist
- Verify file permissions
- Look at test output for specific errors

**Segmentation fault:**
- Run with Valgrind to find memory issues
- Check array bounds in process simulator
- Verify register indices (0-15)

### Resources

- **Header files**: Start with `include/` directory
- **Examples**: Look at `examples/` for usage patterns
- **Tests**: Check `tests/` for API usage examples
- **README**: Project overview and quick reference

## 🎯 Project Goals

Understanding the project goals will help you make better decisions:

1. **Educational**: Demonstrate OS concepts (PCB, memory management, context switching)
2. **Practical**: Implement working checkpoint/rollback system
3. **Extensible**: Easy to add new features and instruction types
4. **Testable**: Comprehensive test coverage
5. **Real-world**: Support both simulated and real processes

## 🚀 Next Steps

Now that you've read this guide:

1. ✅ Build the project
2. ✅ Run `simple_example`
3. ✅ Run `process_demo`
4. ✅ Read `include/core/types.hpp`
5. ✅ Read `include/state/state_manager.hpp`
6. ✅ Write your own example program
7. ✅ Run the test suite
8. ✅ Explore advanced features

## 🎉 Real Process Restore - Working!

The project now supports **real process checkpoint/restore**! This is a major feature that allows you to:

1. **Checkpoint a running Linux process** - Save its complete state
2. **Continue execution** - Let the process run and modify its state
3. **Restore to checkpoint** - Rewind the process back in time!

### Quick Test

```bash
# Build the project
cd build && cmake .. && make

# Disable ASLR
echo 0 | sudo tee /proc/sys/kernel/randomize_va_space

# Run the working test
sudo ./bin/real_restore_test_v5

# Re-enable ASLR when done
echo 2 | sudo tee /proc/sys/kernel/randomize_va_space
```

### Expected Output
```
Checkpoint counter: 102
Pre-restore counter: 104  (process ran 2 more iterations)
Post-restore counter: 102  (correctly restored!)

✅ Counter correctly restored to checkpoint value!
```

**Welcome to the project! Happy coding! 🎉**
