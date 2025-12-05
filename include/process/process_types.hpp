#pragma once

#include <cstdint>
#include <string>
#include <array>
#include <vector>
#include <chrono>
#include <map>
#include "core/types.hpp"

namespace checkpoint {
namespace process {

// ============================================================================
// Process State Enum - İşlem Durumları
// ============================================================================
enum class ProcessState {
    NEW,        // Yeni oluşturulmuş, henüz çalışmadı
    READY,      // Çalışmaya hazır, CPU bekliyor
    RUNNING,    // Şu anda CPU'da çalışıyor
    WAITING,    // I/O veya event bekliyor
    TERMINATED  // Sonlandırılmış
};

inline std::string processStateToString(ProcessState state) {
    switch (state) {
        case ProcessState::NEW: return "NEW";
        case ProcessState::READY: return "READY";
        case ProcessState::RUNNING: return "RUNNING";
        case ProcessState::WAITING: return "WAITING";
        case ProcessState::TERMINATED: return "TERMINATED";
        default: return "UNKNOWN";
    }
}

// ============================================================================
// Register Tanımları
// ============================================================================
constexpr size_t NUM_GENERAL_REGISTERS = 16;  // R0-R15
constexpr size_t STACK_SIZE = 4096;           // 4KB stack
constexpr size_t HEAP_SIZE = 8192;            // 8KB heap
constexpr size_t DATA_SIZE = 2048;            // 2KB data segment
constexpr size_t CODE_SIZE = 4096;            // 4KB code segment
constexpr size_t PAGE_SIZE = 256;             // 256 byte page size

// Register isimleri
enum class Register : uint8_t {
    R0 = 0, R1, R2, R3, R4, R5, R6, R7,
    R8, R9, R10, R11, R12, R13, R14, R15,
    // Özel registerlar (RegisterSet içinde ayrı tutulur)
    PC,  // Program Counter
    SP,  // Stack Pointer
    BP,  // Base Pointer
    FLAGS  // Status Flags
};

// ============================================================================
// CPU Flags
// ============================================================================
struct CPUFlags {
    bool zero;      // Zero flag - son işlem 0 sonuç verdi
    bool carry;     // Carry flag - taşma oldu
    bool negative;  // Negative flag - sonuç negatif
    bool overflow;  // Overflow flag - işaretli taşma
    
    CPUFlags() : zero(false), carry(false), negative(false), overflow(false) {}
    
    void reset() {
        zero = carry = negative = overflow = false;
    }
    
    uint8_t toByte() const {
        return (zero ? 1 : 0) | 
               (carry ? 2 : 0) | 
               (negative ? 4 : 0) | 
               (overflow ? 8 : 0);
    }
    
    void fromByte(uint8_t byte) {
        zero = byte & 1;
        carry = byte & 2;
        negative = byte & 4;
        overflow = byte & 8;
    }
};

// ============================================================================
// Register Set - Tüm CPU Registerları
// ============================================================================
struct RegisterSet {
    std::array<int32_t, NUM_GENERAL_REGISTERS> general;  // R0-R15
    uint32_t pc;    // Program Counter
    uint32_t sp;    // Stack Pointer
    uint32_t bp;    // Base Pointer
    CPUFlags flags; // CPU Flags
    
    RegisterSet() : pc(0), sp(STACK_SIZE - 1), bp(STACK_SIZE - 1) {
        general.fill(0);
    }
    
    int32_t& operator[](Register reg) {
        if (static_cast<uint8_t>(reg) < NUM_GENERAL_REGISTERS) {
            return general[static_cast<uint8_t>(reg)];
        }
        throw std::out_of_range("Invalid register access");
    }
    
    int32_t operator[](Register reg) const {
        if (static_cast<uint8_t>(reg) < NUM_GENERAL_REGISTERS) {
            return general[static_cast<uint8_t>(reg)];
        }
        throw std::out_of_range("Invalid register access");
    }
    
    // Serileştirme
    StateData serialize() const;
    static RegisterSet deserialize(const StateData& data, size_t& offset);
};

// ============================================================================
// Page Table Entry - Sayfa Tablosu Girişi
// ============================================================================
struct PageTableEntry {
    uint32_t frameNumber;   // Fiziksel frame numarası
    bool valid;             // Geçerli mi?
    bool dirty;             // Değiştirilmiş mi?
    bool accessed;          // Erişilmiş mi?
    bool readOnly;          // Salt okunur mu?
    
    PageTableEntry() 
        : frameNumber(0), valid(false), dirty(false), 
          accessed(false), readOnly(false) {}
};

// ============================================================================
// Memory Segment Types
// ============================================================================
enum class SegmentType {
    CODE,   // Instruction'lar
    DATA,   // Global/static değişkenler
    HEAP,   // Dinamik bellek
    STACK   // Stack
};

// ============================================================================
// Process Control Block (PCB) - İşlem Kontrol Bloğu
// ============================================================================
struct PCB {
    // Temel bilgiler
    uint32_t pid;           // Process ID
    uint32_t parentPid;     // Parent Process ID
    std::string name;       // Process adı
    
    // Durum bilgisi
    ProcessState state;     // Process durumu
    uint8_t priority;       // Öncelik (0-255, düşük = yüksek öncelik)
    
    // CPU Context
    RegisterSet registers;  // Tüm registerlar
    
    // Zamanlama bilgileri
    uint64_t cpuTimeUsed;       // Kullanılan CPU zamanı (cycle)
    uint64_t creationTime;      // Oluşturulma zamanı
    uint64_t lastScheduledTime; // Son schedule edilme zamanı
    
    // Sayaçlar
    uint32_t contextSwitchCount;  // Context switch sayısı
    uint32_t instructionCount;    // Çalıştırılan instruction sayısı
    
    PCB() : pid(0), parentPid(0), state(ProcessState::NEW), 
            priority(128), cpuTimeUsed(0), creationTime(0),
            lastScheduledTime(0), contextSwitchCount(0), 
            instructionCount(0) {}
    
    // Serileştirme
    StateData serialize() const;
    static PCB deserialize(const StateData& data, size_t& offset);
};

// ============================================================================
// Process Memory - İşlem Belleği
// ============================================================================
struct ProcessMemory {
    std::vector<uint8_t> codeSegment;   // Kod segmenti (instruction'lar)
    std::vector<uint8_t> dataSegment;   // Data segmenti (değişkenler)
    std::vector<uint8_t> heapSegment;   // Heap segmenti
    std::vector<uint8_t> stackSegment;  // Stack segmenti
    
    // Page Table
    std::vector<PageTableEntry> pageTable;
    
    ProcessMemory() {
        codeSegment.resize(CODE_SIZE, 0);
        dataSegment.resize(DATA_SIZE, 0);
        heapSegment.resize(HEAP_SIZE, 0);
        stackSegment.resize(STACK_SIZE, 0);
        
        // Page table'ı başlat
        size_t totalPages = (CODE_SIZE + DATA_SIZE + HEAP_SIZE + STACK_SIZE) / PAGE_SIZE;
        pageTable.resize(totalPages);
        for (size_t i = 0; i < totalPages; ++i) {
            pageTable[i].frameNumber = static_cast<uint32_t>(i);
            pageTable[i].valid = true;
        }
    }
    
    // Memory access metodları
    uint8_t readByte(SegmentType segment, uint32_t offset) const;
    void writeByte(SegmentType segment, uint32_t offset, uint8_t value);
    int32_t readWord(SegmentType segment, uint32_t offset) const;
    void writeWord(SegmentType segment, uint32_t offset, int32_t value);
    
    // Stack operasyonları
    void push(int32_t value, uint32_t& sp);
    int32_t pop(uint32_t& sp);
    
    // Serileştirme
    StateData serialize() const;
    static ProcessMemory deserialize(const StateData& data, size_t& offset);
};

// ============================================================================
// Process Snapshot - Tam Process Durumu
// ============================================================================
struct ProcessSnapshot {
    PCB pcb;
    ProcessMemory memory;
    std::string checkpointName;
    uint64_t timestamp;
    
    ProcessSnapshot() : timestamp(0) {}
    
    // Serileştirme
    StateData serialize() const;
    static ProcessSnapshot deserialize(const StateData& data);
};

} // namespace process
} // namespace checkpoint
