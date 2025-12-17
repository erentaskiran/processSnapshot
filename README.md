# State Checkpoint & Rollback System (Real Processes Only)

A lightweight toolkit for checkpointing and restoring real Linux processes using ptrace and custom serialization. The former instruction-set simulation has been removed to keep the codebase focused on production use.

## Highlights
- Ptrace-based capture and restore of process state (registers, memory, FDs, ASLR handling).
- Pluggable storage via `StateManager` and rollback orchestration via `RollbackEngine`.
- Structured logging with `OperationLogger`.
- Real-process demos and CLI utilities for capturing and restoring running programs.

## Layout
```
state-checkpoint-system/
├── include/
│   ├── core/            # Base types and serialization helpers
│   ├── state/           # State manager + storage abstractions
│   ├── logger/          # Logging interfaces
│   ├── rollback/        # Rollback engine
│   └── real_process/    # Ptrace-based real process support
├── src/
│   ├── core/
│   ├── state/
│   ├── logger/
│   ├── rollback/
│   └── real_process/
├── examples/
│   ├── auto_save_example.cpp
│   ├── partial_rollback_example.cpp
│   ├── real_process_demo.cpp
│   ├── real_restore_test.cpp
│   ├── real_restore_test_v2.cpp
│   ├── real_restore_test_v3.cpp
│   ├── real_restore_test_v4.cpp
│   ├── real_restore_test_v5.cpp
│   └── simple_example.cpp
├── src/cli/process_checkpoint_cli.cpp
├── tests/                # Unit tests (simulator tests removed)
└── CMakeLists.txt
```

## Build
```bash
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

## Executables (build/bin)
- `checkpoint_demo` – simple checkpoint walkthrough.
- `process_checkpoint_cli` – capture/restore real processes from the command line.
- `real_process_demo` and `real_restore_test_v2..v5` – ptrace-focused demos and verification tools.
- `simple_example`, `auto_save_example`, `partial_rollback_example` – minimal library usage samples.

## Tests
```bash
cd build
ctest --output-on-failure
# or run the binary directly
./bin/checkpoint_tests
```

## Notes
- Simulation mode (ProcessSimulator, instruction set, and related tests/examples) has been removed. The codebase now targets real-process checkpointing exclusively.
