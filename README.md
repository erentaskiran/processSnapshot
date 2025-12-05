# Process State Checkpoint & Rollback System

İşletim Sistemleri Dersi Projesi - Process simülasyonu ile checkpoint ve rollback sistemi.

## 🎯 Proje Özellikleri

### Process Simülasyonu
- **Process Control Block (PCB)**: PID, state, priority, registers, CPU time
- **Memory Segmentleri**: Code, Data, Heap, Stack
- **Page Table**: Basit sayfa tablosu simülasyonu
- **16 General Purpose Register**: R0-R15
- **Özel Registerlar**: PC (Program Counter), SP (Stack Pointer), BP (Base Pointer), FLAGS

### Instruction Set
- **Data Movement**: LOAD_IMM, LOAD_MEM, STORE, MOV
- **Stack**: PUSH, POP
- **Arithmetic**: ADD, SUB, MUL, DIV, MOD, INC, DEC, NEG
- **Bitwise**: AND, OR, XOR, NOT, SHL, SHR
- **Comparison**: CMP, TEST
- **Control Flow**: JMP, JZ, JNZ, JG, JL, JGE, JLE
- **Subroutine**: CALL, RET
- **System**: SYSCALL, HALT
- **I/O**: IN, OUT
- **Debug**: BREAK, PRINT

### Checkpoint & Rollback
- Herhangi bir anda process durumunu kaydet
- Birden fazla checkpoint tutabilme
- Checkpoint'e timestamp ve isim verme
- Tam geri yükleme (PCB + Memory)
- StateManager entegrasyonu

## 📁 Proje Yapısı

```
state-checkpoint-system/
├── include/
│   ├── core/                  # Temel tipler ve serileştirme
│   │   ├── types.hpp
│   │   ├── serializer.hpp
│   │   └── exceptions.hpp
│   ├── state/                 # State yönetimi
│   │   ├── state_manager.hpp
│   │   └── storage.hpp
│   ├── logger/                # Loglama sistemi
│   │   └── operation_logger.hpp
│   ├── rollback/              # Rollback motoru
│   │   └── rollback_engine.hpp
│   └── process/               # Process simülasyonu
│       ├── process_types.hpp      # PCB, Memory, Register yapıları
│       ├── instruction_set.hpp    # Instruction set ve assembler
│       └── process_simulator.hpp  # Ana simülatör
├── src/
│   ├── core/
│   ├── state/
│   ├── logger/
│   ├── rollback/
│   └── process/               # Process implementasyonları
│       ├── process_types.cpp
│       ├── instruction_set.cpp
│       └── process_simulator.cpp
├── tests/
│   ├── test_process.cpp       # Process testleri
│   └── ...
├── examples/
│   ├── process_demo.cpp       # Ana demo uygulaması
│   └── ...
└── CMakeLists.txt
```

## 🛠️ Gereksinimler

- Arch Linux (veya herhangi bir Linux dağıtımı)
- GCC 12+ veya Clang 14+ (C++20 desteği için)
- CMake 3.20+
- Google Test (otomatik indirilir)

## 🚀 Kurulum ve Build

```bash
# Repo'yu klonla
cd state-checkpoint-system

# Build dizini oluştur
mkdir -p build && cd build

# CMake ile konfigüre et
cmake ..

# Derle
make -j$(nproc)
```

## 📦 Çalıştırılabilir Dosyalar

Build sonrası `build/bin/` dizininde:

- `process_demo` - Ana demo uygulaması (checkpoint & rollback)
- `checkpoint_demo` - Genel checkpoint demo
- `checkpoint_tests` - Unit testler
- `simple_example` - Basit örnek
- `auto_save_example` - Otomatik kaydetme örneği

## 🧪 Testleri Çalıştırma

```bash
cd build

# Tüm testler
./bin/checkpoint_tests

# Sadece process testleri
./bin/checkpoint_tests --gtest_filter="Process*"

# Sadece checkpoint testleri
./bin/checkpoint_tests --gtest_filter="Checkpoint*"
```

## 🎮 Demo Çalıştırma

```bash
cd build
./bin/process_demo
```

Demo şunları gösterir:
1. Process oluşturma ve PCB yapısı
2. Program yükleme (faktöriyel hesaplama)
3. Step-by-step execution
4. Checkpoint alma
5. Program tamamlama
6. Rollback (checkpoint'e geri dönme)
7. StateManager entegrasyonu

## 📝 Örnek Kullanım

```cpp
#include "process/process_simulator.hpp"
#include "process/instruction_set.hpp"

using namespace checkpoint::process;

int main() {
    // Simülatör oluştur
    ProcessSimulator simulator;
    
    // Process oluştur
    uint32_t pid = simulator.createProcess("MyProcess", 100);
    
    // Program yükle
    std::vector<Instruction> program = {
        Instruction(Opcode::LOAD_IMM, 0, 42),   // R0 = 42
        Instruction(Opcode::INC, 0),            // R0++
        Instruction(Opcode::PRINT, 0),          // Print R0
        Instruction(Opcode::HALT)               // Stop
    };
    simulator.loadProgram(pid, program);
    
    // Checkpoint al
    ProcessSnapshot checkpoint = simulator.takeSnapshot(pid, "Before");
    
    // Programı çalıştır
    simulator.runUntilHalt(pid);
    
    // Rollback
    simulator.restoreFromSnapshot(pid, checkpoint);
    
    return 0;
}
```

## 🏗️ Mimari

### ProcessSimulator
Ana simülatör sınıfı. Process oluşturma, program yükleme, execution ve checkpoint/rollback işlemlerini yönetir.

### PCB (Process Control Block)
```cpp
struct PCB {
    uint32_t pid;           // Process ID
    uint32_t parentPid;     // Parent PID
    std::string name;       // Process adı
    ProcessState state;     // NEW, READY, RUNNING, WAITING, TERMINATED
    uint8_t priority;       // 0-255
    RegisterSet registers;  // R0-R15, PC, SP, BP, FLAGS
    uint64_t cpuTimeUsed;   // CPU cycles
    uint32_t instructionCount;
};
```

### ProcessMemory
```cpp
struct ProcessMemory {
    std::vector<uint8_t> codeSegment;   // 4KB - Instructions
    std::vector<uint8_t> dataSegment;   // 2KB - Global data
    std::vector<uint8_t> heapSegment;   // 8KB - Dynamic memory
    std::vector<uint8_t> stackSegment;  // 4KB - Stack
    std::vector<PageTableEntry> pageTable;
};
```

### Instruction
```cpp
struct Instruction {
    Opcode opcode;      // İşlem kodu
    uint8_t dest;       // Hedef register
    uint8_t src1;       // Kaynak register 1
    uint8_t src2;       // Kaynak register 2
    int32_t immediate;  // Immediate değer veya adres
};
```

## 👥 Ekip ve Görev Dağılımı

Detaylı görev dağılımı için: [GOREV_DAGILIMI.md](GOREV_DAGILIMI.md)

| Kişi | Sorumluluk |
|------|------------|
| Kişi 1 | PCB & Process State Modülü |
| Kişi 2 | Process Memory & Page Table |
| Kişi 3 | Instruction Set & Assembler |
| Kişi 4 | Process Simulator & Demo |

## 📊 Test Coverage

- ProcessTypesTest: PCB, RegisterSet, Memory serileştirme
- InstructionSetTest: Encoding/decoding, assembler
- ProcessSimulatorTest: Execution, arithmetic, jumps, stack
- CheckpointRollbackTest: Snapshot, restore, StateManager

## 📚 Kaynaklar

### İşletim Sistemleri
- Operating System Concepts (Silberschatz)
- Modern Operating Systems (Tanenbaum)

### C++20
- [cppreference.com](https://en.cppreference.com/)
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/)

## 📄 Lisans

Bu proje eğitim amaçlı hazırlanmıştır.

---

**Son Güncelleme:** 5 Aralık 2024
