# Getting Started with State Checkpoint System (Real Processes)

This project now focuses solely on checkpointing and restoring real Linux processes via `ptrace` and custom serialization. The former simulation/instruction-set path has been removed.

## Quick Start

```bash
# From the repository root
test -d build || mkdir build
cd build
cmake ..
make -j"$(nproc)"
ctest --output-on-failure
```

Run a few executables from `build/bin`:
- `simple_example` – minimal checkpoint flow
- `auto_save_example` – periodic checkpoints
- `partial_rollback_example` – selective rollback
- `real_process_demo` – capture/restore a real process
- `real_restore_test_v5` – validated end-to-end restore (run with sudo; disable ASLR as noted below)

ASLR must be disabled for the real restore tests:
```bash
echo 0 | sudo tee /proc/sys/kernel/randomize_va_space
sudo ./bin/real_restore_test_v5
echo 2 | sudo tee /proc/sys/kernel/randomize_va_space
```

## Architecture (high level)
- Core: shared types and serialization helpers.
- State: checkpoint metadata and storage backends.
- Rollback: restoration orchestration and history.
- Logger: structured operation logging.
- Real process: `ptrace` control, memory capture, ASLR/file-descriptor handling.

## Learning Path
1) Basics
- Read `README.md`.
- Skim `examples/simple_example.cpp` and `examples/auto_save_example.cpp`.
- Study `include/state/state_manager.hpp` and `include/core/types.hpp`.

2) Real process flow
- Review `include/real_process/ptrace_controller.hpp` and `src/real_process/*.cpp`.
- Run `real_process_demo` then `real_restore_test_v5` to see capture/restore in practice.

3) Rollback internals
- Read `src/rollback/rollback_engine.cpp` and `src/state/storage.cpp` for persistence and restore logic.

## Important Files
- `include/state/state_manager.hpp` – main API for checkpoints
- `include/core/types.hpp` – `StateData`, `Result<T>`, common aliases
- `include/rollback/rollback_engine.hpp` – rollback coordination
- `include/real_process/ptrace_controller.hpp` – ptrace-based capture/restore
- `examples/simple_example.cpp`, `examples/auto_save_example.cpp`, `examples/partial_rollback_example.cpp` – quick reference usages
- `examples/real_process_demo.cpp`, `examples/real_restore_test_v5.cpp` – real-process demonstrations

## Build and Test
```bash
cd build
cmake -DBUILD_TESTS=ON ..
make -j"$(nproc)"
ctest --output-on-failure
```

## Notes
- Simulation/instruction-set artifacts (ProcessSimulator, instruction_set, process_demo) are removed. If you see stale references, update them to the real-process APIs listed above.
