#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include "process/process_types.hpp"

namespace checkpoint {
namespace process {

// ============================================================================
// Instruction Opcodes - İşlem Kodları
// ============================================================================
enum class Opcode : uint8_t {
    // No Operation
    NOP = 0x00,
    
    // Data Movement
    LOAD_IMM = 0x01,    // LOAD Rd, imm    - Immediate değeri Rd'ye yükle
    LOAD_MEM = 0x02,    // LOAD Rd, [addr] - Memory'den Rd'ye yükle
    STORE = 0x03,       // STORE Rs, [addr] - Rs'yi memory'ye kaydet
    MOV = 0x04,         // MOV Rd, Rs      - Rs'yi Rd'ye kopyala
    
    // Stack Operations
    PUSH = 0x10,        // PUSH Rs         - Rs'yi stack'e it
    POP = 0x11,         // POP Rd          - Stack'ten Rd'ye çek
    
    // Arithmetic Operations
    ADD = 0x20,         // ADD Rd, Rs1, Rs2 - Rd = Rs1 + Rs2
    SUB = 0x21,         // SUB Rd, Rs1, Rs2 - Rd = Rs1 - Rs2
    MUL = 0x22,         // MUL Rd, Rs1, Rs2 - Rd = Rs1 * Rs2
    DIV = 0x23,         // DIV Rd, Rs1, Rs2 - Rd = Rs1 / Rs2
    MOD = 0x24,         // MOD Rd, Rs1, Rs2 - Rd = Rs1 % Rs2
    INC = 0x25,         // INC Rd          - Rd++
    DEC = 0x26,         // DEC Rd          - Rd--
    NEG = 0x27,         // NEG Rd          - Rd = -Rd
    
    // Bitwise Operations
    AND = 0x30,         // AND Rd, Rs1, Rs2 - Rd = Rs1 & Rs2
    OR = 0x31,          // OR Rd, Rs1, Rs2  - Rd = Rs1 | Rs2
    XOR = 0x32,         // XOR Rd, Rs1, Rs2 - Rd = Rs1 ^ Rs2
    NOT = 0x33,         // NOT Rd          - Rd = ~Rd
    SHL = 0x34,         // SHL Rd, Rs, n   - Rd = Rs << n
    SHR = 0x35,         // SHR Rd, Rs, n   - Rd = Rs >> n
    
    // Comparison
    CMP = 0x40,         // CMP Rs1, Rs2    - Karşılaştır, flags ayarla
    TEST = 0x41,        // TEST Rs1, Rs2   - AND işlemi, sadece flags
    
    // Control Flow
    JMP = 0x50,         // JMP addr        - Koşulsuz dallan
    JZ = 0x51,          // JZ addr         - Zero ise dallan
    JNZ = 0x52,         // JNZ addr        - Zero değilse dallan
    JG = 0x53,          // JG addr         - Greater ise dallan
    JL = 0x54,          // JL addr         - Less ise dallan
    JGE = 0x55,         // JGE addr        - Greater or equal ise dallan
    JLE = 0x56,         // JLE addr        - Less or equal ise dallan
    
    // Subroutine
    CALL = 0x60,        // CALL addr       - Alt programı çağır
    RET = 0x61,         // RET             - Alt programdan dön
    
    // System
    SYSCALL = 0x70,     // SYSCALL n       - Sistem çağrısı
    HALT = 0x71,        // HALT            - Programı durdur
    
    // I/O Simulation
    IN = 0x80,          // IN Rd, port     - Port'tan oku (simüle)
    OUT = 0x81,         // OUT port, Rs    - Port'a yaz (simüle)
    
    // Debug
    BREAK = 0xF0,       // BREAK           - Breakpoint
    PRINT = 0xF1        // PRINT Rs        - Register'ı yazdır (debug)
};

// ============================================================================
// Instruction Format
// ============================================================================
struct Instruction {
    Opcode opcode;
    uint8_t dest;       // Destination register (0-15) veya port
    uint8_t src1;       // Source register 1 veya immediate high byte
    uint8_t src2;       // Source register 2 veya immediate low byte
    int32_t immediate;  // Immediate değer veya adres
    
    Instruction() : opcode(Opcode::NOP), dest(0), src1(0), src2(0), immediate(0) {}
    
    Instruction(Opcode op) : opcode(op), dest(0), src1(0), src2(0), immediate(0) {}
    
    Instruction(Opcode op, uint8_t d) 
        : opcode(op), dest(d), src1(0), src2(0), immediate(0) {}
    
    Instruction(Opcode op, uint8_t d, int32_t imm) 
        : opcode(op), dest(d), src1(0), src2(0), immediate(imm) {}
    
    Instruction(Opcode op, uint8_t d, uint8_t s1) 
        : opcode(op), dest(d), src1(s1), src2(0), immediate(0) {}
    
    Instruction(Opcode op, uint8_t d, uint8_t s1, uint8_t s2) 
        : opcode(op), dest(d), src1(s1), src2(s2), immediate(0) {}
    
    // Serileştirme (8 byte sabit boyut)
    std::vector<uint8_t> encode() const {
        std::vector<uint8_t> bytes(8);
        bytes[0] = static_cast<uint8_t>(opcode);
        bytes[1] = dest;
        bytes[2] = src1;
        bytes[3] = src2;
        bytes[4] = (immediate >> 24) & 0xFF;
        bytes[5] = (immediate >> 16) & 0xFF;
        bytes[6] = (immediate >> 8) & 0xFF;
        bytes[7] = immediate & 0xFF;
        return bytes;
    }
    
    static Instruction decode(const uint8_t* bytes) {
        Instruction inst;
        inst.opcode = static_cast<Opcode>(bytes[0]);
        inst.dest = bytes[1];
        inst.src1 = bytes[2];
        inst.src2 = bytes[3];
        inst.immediate = (bytes[4] << 24) | (bytes[5] << 16) | 
                        (bytes[6] << 8) | bytes[7];
        return inst;
    }
    
    std::string toString() const;
};

// ============================================================================
// Instruction Size
// ============================================================================
constexpr size_t INSTRUCTION_SIZE = 8;  // Her instruction 8 byte

// ============================================================================
// Opcode bilgi tablosu
// ============================================================================
struct OpcodeInfo {
    std::string mnemonic;
    std::string description;
    uint8_t operandCount;
    bool modifiesFlags;
};

// Opcode bilgi fonksiyonu
OpcodeInfo getOpcodeInfo(Opcode opcode);

// String'den Opcode'a dönüştürme
Opcode stringToOpcode(const std::string& mnemonic);

// ============================================================================
// Simple Assembler - Basit bir assembler
// ============================================================================
class SimpleAssembler {
public:
    // Assembly kodunu instruction listesine çevir
    static std::vector<Instruction> assemble(const std::string& code);
    
    // Instruction listesini binary'ye çevir
    static std::vector<uint8_t> toBinary(const std::vector<Instruction>& instructions);
    
    // Binary'den instruction listesine çevir
    static std::vector<Instruction> fromBinary(const std::vector<uint8_t>& binary);
    
    // Label'lı assembly desteği
    struct AssemblyResult {
        std::vector<Instruction> instructions;
        std::unordered_map<std::string, uint32_t> labels;
        std::vector<std::string> errors;
        bool success;
    };
    
    static AssemblyResult assembleWithLabels(const std::string& code);
};

// ============================================================================
// Predefined Programs - Önceden tanımlı test programları
// ============================================================================
namespace Programs {

// Basit sayaç programı (R0'ı 10'a kadar say)
std::vector<Instruction> counter();

// Fibonacci hesaplayan program
std::vector<Instruction> fibonacci(int n);

// Faktöriyel hesaplayan program
std::vector<Instruction> factorial(int n);

// Memory copy programı
std::vector<Instruction> memcpy_prog(uint32_t src, uint32_t dst, uint32_t size);

// Bubble sort programı
std::vector<Instruction> bubbleSort();

} // namespace Programs

} // namespace process
} // namespace checkpoint
