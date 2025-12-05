# 👥 Görev Dağılımı - 4 Kişilik Ekip

## 📋 Process State Checkpoint & Rollback Projesi

Bu proje, işletim sistemleri dersinde process yönetimi, checkpoint ve rollback kavramlarını simüle eder.

---

## 🏗️ Proje Yapısı

```
state-checkpoint-system/
├── include/
│   ├── core/               # Temel tipler ve serileştirme
│   │   ├── types.hpp
│   │   ├── serializer.hpp
│   │   └── exceptions.hpp
│   ├── state/              # State yönetimi
│   │   ├── state_manager.hpp
│   │   └── storage.hpp
│   ├── logger/             # Loglama sistemi
│   │   └── operation_logger.hpp
│   ├── rollback/           # Rollback motoru
│   │   └── rollback_engine.hpp
│   └── process/            # 🆕 Process simülasyonu
│       ├── process_types.hpp      # PCB, Memory, Register yapıları
│       ├── instruction_set.hpp    # Instruction set ve assembler
│       └── process_simulator.hpp  # Ana simülatör
├── src/
│   ├── core/
│   ├── state/
│   ├── logger/
│   ├── rollback/
│   └── process/            # 🆕 Process implementasyonları
│       ├── process_types.cpp
│       ├── instruction_set.cpp
│       └── process_simulator.cpp
├── tests/
│   ├── test_process.cpp    # 🆕 Process testleri
│   └── ...
├── examples/
│   ├── process_demo.cpp    # 🆕 Ana demo uygulaması
│   └── ...
└── CMakeLists.txt
```

---

## 🟦 Kişi 1: PCB & Process State Modülü

### Sorumluluk Alanları
- Process Control Block (PCB) yapısı
- Register yönetimi (R0-R15, PC, SP, BP, Flags)
- Process state transitions
- PCB serileştirme/deserileştirme

### Dosyalar
```
include/process/
└── process_types.hpp       # PCB, RegisterSet, CPUFlags
    ├── ProcessState enum
    ├── RegisterSet struct
    ├── CPUFlags struct
    └── PCB struct

src/process/
└── process_types.cpp
    ├── RegisterSet::serialize/deserialize
    └── PCB::serialize/deserialize

tests/
└── test_process.cpp (PCB testleri)
```

### Ana Yapılar

#### ProcessState Enum
```cpp
enum class ProcessState {
    NEW,        // Yeni oluşturulmuş
    READY,      // CPU bekliyor
    RUNNING,    // Çalışıyor
    WAITING,    // I/O bekliyor
    TERMINATED  // Sonlandırılmış
};
```

#### PCB Struct
```cpp
struct PCB {
    uint32_t pid;           // Process ID
    uint32_t parentPid;     // Parent PID
    std::string name;       // Process adı
    ProcessState state;     // Durum
    uint8_t priority;       // Öncelik (0-255)
    RegisterSet registers;  // CPU context
    uint64_t cpuTimeUsed;   // CPU zamanı
    uint32_t instructionCount;
    // ...
};
```

### Yapılacaklar
1. [x] PCB yapısını tanımla
2. [x] RegisterSet'i implement et
3. [x] CPUFlags'ı implement et
4. [x] Serileştirme metodlarını yaz
5. [ ] PCB state transition validasyonu ekle
6. [ ] Process scheduling bilgileri ekle
7. [ ] Unit testleri genişlet
8. [ ] Dokümantasyon yaz

### Tahmini Süre: 2-3 gün

---

## 🟩 Kişi 2: Process Memory & Page Table Modülü

### Sorumluluk Alanları
- Memory segment yönetimi (Code, Data, Heap, Stack)
- Page Table simülasyonu
- Memory operasyonları (read/write)
- Stack operasyonları (push/pop)

### Dosyalar
```
include/process/
└── process_types.hpp
    ├── SegmentType enum
    ├── PageTableEntry struct
    └── ProcessMemory struct

src/process/
└── process_types.cpp
    ├── ProcessMemory read/write metodları
    └── ProcessMemory serialize/deserialize
```

### Ana Yapılar

#### Memory Segmentleri
```cpp
struct ProcessMemory {
    std::vector<uint8_t> codeSegment;   // 4KB - Instructions
    std::vector<uint8_t> dataSegment;   // 2KB - Global veriler
    std::vector<uint8_t> heapSegment;   // 8KB - Dinamik bellek
    std::vector<uint8_t> stackSegment;  // 4KB - Stack
    std::vector<PageTableEntry> pageTable;  // Sayfa tablosu
};
```

#### Page Table Entry
```cpp
struct PageTableEntry {
    uint32_t frameNumber;   // Fiziksel frame
    bool valid;             // Geçerli mi?
    bool dirty;             // Değiştirilmiş mi?
    bool accessed;          // Erişilmiş mi?
    bool readOnly;          // Salt okunur?
};
```

### Yapılacaklar
1. [x] Memory segment'leri tanımla
2. [x] Page Table yapısını implement et
3. [x] read/write metodlarını yaz
4. [x] Stack push/pop implement et
5. [ ] Virtual memory simulation ekle
6. [ ] Memory protection kontrolü
7. [ ] Heap allocation simülasyonu
8. [ ] Unit testleri genişlet

### Tahmini Süre: 2-3 gün

---

## 🟨 Kişi 3: Instruction Set & Assembler Modülü

### Sorumluluk Alanları
- Opcode tanımlamaları
- Instruction encoding/decoding
- Simple Assembler
- Önceden tanımlı programlar

### Dosyalar
```
include/process/
└── instruction_set.hpp
    ├── Opcode enum
    ├── Instruction struct
    ├── OpcodeInfo struct
    └── SimpleAssembler class

src/process/
└── instruction_set.cpp
    ├── Instruction encode/decode
    ├── Assembler implementation
    └── Predefined programs
```

### Instruction Set

#### Opcode Kategorileri
```cpp
// Data Movement
LOAD_IMM, LOAD_MEM, STORE, MOV

// Stack Operations
PUSH, POP

// Arithmetic
ADD, SUB, MUL, DIV, MOD, INC, DEC, NEG

// Bitwise
AND, OR, XOR, NOT, SHL, SHR

// Comparison
CMP, TEST

// Control Flow
JMP, JZ, JNZ, JG, JL, JGE, JLE

// Subroutine
CALL, RET

// System
SYSCALL, HALT

// I/O
IN, OUT

// Debug
BREAK, PRINT
```

### Yapılacaklar
1. [x] Opcode'ları tanımla
2. [x] Instruction encoding implement et
3. [x] Assembler yaz
4. [x] Örnek programlar oluştur
5. [ ] Daha fazla instruction ekle (ör: floating point)
6. [ ] Assembler hata mesajlarını iyileştir
7. [ ] Disassembler'ı geliştir
8. [ ] Unit testleri genişlet

### Tahmini Süre: 3-4 gün

---

## 🟥 Kişi 4: Process Simulator & Demo

### Sorumluluk Alanları
- ProcessSimulator sınıfı
- Instruction execution
- Checkpoint & Rollback entegrasyonu
- Demo uygulaması

### Dosyalar
```
include/process/
└── process_simulator.hpp
    ├── SimulatorConfig struct
    ├── ExecutionResult struct
    └── ProcessSimulator class

src/process/
└── process_simulator.cpp
    ├── Process management
    ├── Execution engine
    └── Checkpoint/Rollback

examples/
└── process_demo.cpp      # Ana demo
```

### ProcessSimulator API

```cpp
class ProcessSimulator {
public:
    // Process yönetimi
    uint32_t createProcess(const std::string& name, uint8_t priority);
    bool loadProgram(uint32_t pid, const std::vector<Instruction>& program);
    bool terminateProcess(uint32_t pid);
    
    // Execution
    ExecutionResult step(uint32_t pid);
    ExecutionResult runUntilHalt(uint32_t pid);
    
    // Checkpoint & Rollback
    ProcessSnapshot takeSnapshot(uint32_t pid, const std::string& name);
    bool restoreFromSnapshot(uint32_t pid, const ProcessSnapshot& snapshot);
    
    // StateManager entegrasyonu
    CheckpointId createCheckpoint(uint32_t pid, const std::string& name,
                                  StateManager& stateManager);
    bool rollbackToCheckpoint(uint32_t pid, CheckpointId id,
                             StateManager& stateManager);
    
    // Debug
    std::string dumpRegisters(uint32_t pid) const;
    std::string disassemble(uint32_t pid, uint32_t start, uint32_t count);
};
```

### Yapılacaklar
1. [x] ProcessSimulator sınıfını implement et
2. [x] Tüm instruction'ları execute et
3. [x] Checkpoint/Rollback entegrasyonu
4. [x] Demo uygulamasını yaz
5. [ ] Multi-process desteği ekle
6. [ ] Scheduler simülasyonu
7. [ ] Performans optimizasyonları
8. [ ] Daha kapsamlı demo senaryoları

### Tahmini Süre: 4-5 gün

---

## 📅 Proje Takvimi

### Hafta 1: Temel Implementasyon
| Gün | Kişi 1 (PCB) | Kişi 2 (Memory) | Kişi 3 (ISA) | Kişi 4 (Simulator) |
|-----|--------------|-----------------|--------------|---------------------|
| 1   | PCB struct   | Memory segments | Opcode enum  | SimulatorConfig     |
| 2   | RegisterSet  | Page Table      | Instruction  | Create/Load process |
| 3   | CPUFlags     | Read/Write      | Assembler    | Step execution      |
| 4   | Serialize    | Stack ops       | Programs     | All instructions    |
| 5   | Unit test    | Unit test       | Unit test    | Unit test           |

### Hafta 2: Entegrasyon ve Polish
| Gün | Kişi 1 | Kişi 2 | Kişi 3 | Kişi 4 |
|-----|--------|--------|--------|--------|
| 1   | Validation | Virtual mem | More ISA | Checkpoint |
| 2   | Scheduling | Protection | Disasm | Rollback |
| 3   | Code review | Code review | Code review | Demo |
| 4   | Bug fix | Bug fix | Bug fix | Integration |
| 5   | Docs | Docs | Docs | Final test |

---

## 🔄 İş Akışı

### Git Branch Stratejisi
```
main
├── develop
│   ├── feature/pcb-types           (Kişi 1)
│   ├── feature/process-memory      (Kişi 2)
│   ├── feature/instruction-set     (Kişi 3)
│   └── feature/process-simulator   (Kişi 4)
```

### Code Review
- Her PR en az 1 kişi tarafından review edilmeli
- Tüm testler geçmeli
- CI/CD başarılı olmalı

### İletişim
- Günlük stand-up (15 dk)
- Blocker'lar için anında iletişim
- Haftalık demo

---

## 🧪 Test Stratejisi

### Unit Testler
Her modül için ayrı test dosyası:
- `test_process.cpp` - PCB, Memory, ISA testleri

### Test Kategorileri
```cpp
// ProcessTypesTest
TEST_F(ProcessTypesTest, RegisterSetInit)
TEST_F(ProcessTypesTest, RegisterSetSerialize)
TEST_F(ProcessTypesTest, PCBSerialize)
TEST_F(ProcessTypesTest, ProcessMemoryReadWrite)
TEST_F(ProcessTypesTest, ProcessMemoryStack)

// InstructionSetTest
TEST_F(InstructionSetTest, InstructionEncodeDecode)
TEST_F(InstructionSetTest, InstructionToString)
TEST_F(InstructionSetTest, AssemblerToBinary)

// ProcessSimulatorTest
TEST_F(ProcessSimulatorTest, CreateProcess)
TEST_F(ProcessSimulatorTest, ArithmeticOperations)
TEST_F(ProcessSimulatorTest, JumpInstructions)
TEST_F(ProcessSimulatorTest, StackOperations)

// CheckpointRollbackTest
TEST_F(CheckpointRollbackTest, TakeSnapshot)
TEST_F(CheckpointRollbackTest, RestoreFromSnapshot)
TEST_F(CheckpointRollbackTest, StateManagerIntegration)
```

---

## 📝 Demo Senaryosu

Demo uygulaması (`process_demo.cpp`) şu adımları gösterir:

1. **Process Oluşturma**
   - Yeni process oluştur
   - PCB yapısını göster

2. **Program Yükleme**
   - Faktöriyel programını yükle
   - Instruction'ları listele

3. **Step-by-Step Execution**
   - Her instruction'ı tek tek çalıştır
   - Register değişimlerini göster

4. **Checkpoint Alma**
   - Process'in snapshot'ını al
   - Checkpoint bilgilerini göster

5. **Program Tamamlama**
   - Programı sonuna kadar çalıştır
   - Sonucu göster

6. **Rollback**
   - Checkpoint'e geri dön
   - Eski durumun restore edildiğini göster

7. **StateManager Entegrasyonu**
   - StateManager ile checkpoint kaydet
   - Kalıcı checkpoint göster

---

## ✅ Teslim Kontrol Listesi

### Kod
- [ ] Tüm modüller implement edildi
- [ ] Kod derlenebilir durumda
- [ ] Tüm testler geçiyor
- [ ] Memory leak yok
- [ ] Thread-safe (gerekli yerlerde)

### Dokümantasyon
- [ ] README güncel
- [ ] Kod yorumları yeterli
- [ ] API reference mevcut
- [ ] Kullanım örnekleri var

### Demo
- [ ] `process_demo` çalışıyor
- [ ] Tüm özellikler gösteriliyor
- [ ] Checkpoint/Rollback çalışıyor

---

## 🛠️ Build & Run

### Build
```bash
cd state-checkpoint-system
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

### Testleri Çalıştır
```bash
cd build
./bin/checkpoint_tests
```

### Demo'yu Çalıştır
```bash
cd build
./bin/process_demo
```

---

## 📚 Kaynaklar

### İşletim Sistemleri
- Operating System Concepts (Silberschatz)
- Modern Operating Systems (Tanenbaum)

### C++20
- [cppreference.com](https://en.cppreference.com/)
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/)

### Arch Linux
- [Arch Wiki](https://wiki.archlinux.org/)

---

**Son Güncelleme:** 5 Aralık 2024
