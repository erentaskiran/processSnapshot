#include "process/instruction_set.hpp"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <map>

namespace checkpoint {
namespace process {

// ============================================================================
// Instruction::toString Implementasyonu
// ============================================================================

std::string Instruction::toString() const {
    std::stringstream ss;
    OpcodeInfo info = getOpcodeInfo(opcode);
    ss << info.mnemonic;
    
    switch (opcode) {
        case Opcode::NOP:
        case Opcode::RET:
        case Opcode::HALT:
        case Opcode::BREAK:
            break;
            
        case Opcode::LOAD_IMM:
            ss << " R" << static_cast<int>(dest) << ", " << immediate;
            break;
            
        case Opcode::LOAD_MEM:
            ss << " R" << static_cast<int>(dest) << ", [" << immediate << "]";
            break;
            
        case Opcode::STORE:
            ss << " R" << static_cast<int>(dest) << ", [" << immediate << "]";
            break;
            
        case Opcode::MOV:
            ss << " R" << static_cast<int>(dest) << ", R" << static_cast<int>(src1);
            break;
            
        case Opcode::PUSH:
        case Opcode::POP:
        case Opcode::INC:
        case Opcode::DEC:
        case Opcode::NEG:
        case Opcode::NOT:
        case Opcode::PRINT:
            ss << " R" << static_cast<int>(dest);
            break;
            
        case Opcode::ADD:
        case Opcode::SUB:
        case Opcode::MUL:
        case Opcode::DIV:
        case Opcode::MOD:
        case Opcode::AND:
        case Opcode::OR:
        case Opcode::XOR:
            ss << " R" << static_cast<int>(dest) 
               << ", R" << static_cast<int>(src1)
               << ", R" << static_cast<int>(src2);
            break;
            
        case Opcode::SHL:
        case Opcode::SHR:
            ss << " R" << static_cast<int>(dest)
               << ", R" << static_cast<int>(src1)
               << ", " << static_cast<int>(src2);
            break;
            
        case Opcode::CMP:
        case Opcode::TEST:
            ss << " R" << static_cast<int>(dest) 
               << ", R" << static_cast<int>(src1);
            break;
            
        case Opcode::JMP:
        case Opcode::JZ:
        case Opcode::JNZ:
        case Opcode::JG:
        case Opcode::JL:
        case Opcode::JGE:
        case Opcode::JLE:
        case Opcode::CALL:
            ss << " " << immediate;
            break;
            
        case Opcode::SYSCALL:
            ss << " " << static_cast<int>(dest);
            break;
            
        case Opcode::IN:
            ss << " R" << static_cast<int>(dest) << ", " << static_cast<int>(src1);
            break;
            
        case Opcode::OUT:
            ss << " " << static_cast<int>(dest) << ", R" << static_cast<int>(src1);
            break;
            
        default:
            break;
    }
    
    return ss.str();
}

// ============================================================================
// Opcode Info Tablosu
// ============================================================================

OpcodeInfo getOpcodeInfo(Opcode opcode) {
    static const std::map<Opcode, OpcodeInfo> opcodeTable = {
        {Opcode::NOP,      {"NOP",      "No operation",                    0, false}},
        {Opcode::LOAD_IMM, {"LOAD",     "Load immediate to register",      2, false}},
        {Opcode::LOAD_MEM, {"LOAD",     "Load from memory to register",    2, false}},
        {Opcode::STORE,    {"STORE",    "Store register to memory",        2, false}},
        {Opcode::MOV,      {"MOV",      "Move register to register",       2, false}},
        {Opcode::PUSH,     {"PUSH",     "Push register to stack",          1, false}},
        {Opcode::POP,      {"POP",      "Pop from stack to register",      1, false}},
        {Opcode::ADD,      {"ADD",      "Add two registers",               3, true}},
        {Opcode::SUB,      {"SUB",      "Subtract two registers",          3, true}},
        {Opcode::MUL,      {"MUL",      "Multiply two registers",          3, true}},
        {Opcode::DIV,      {"DIV",      "Divide two registers",            3, true}},
        {Opcode::MOD,      {"MOD",      "Modulo two registers",            3, true}},
        {Opcode::INC,      {"INC",      "Increment register",              1, true}},
        {Opcode::DEC,      {"DEC",      "Decrement register",              1, true}},
        {Opcode::NEG,      {"NEG",      "Negate register",                 1, true}},
        {Opcode::AND,      {"AND",      "Bitwise AND",                     3, true}},
        {Opcode::OR,       {"OR",       "Bitwise OR",                      3, true}},
        {Opcode::XOR,      {"XOR",      "Bitwise XOR",                     3, true}},
        {Opcode::NOT,      {"NOT",      "Bitwise NOT",                     1, true}},
        {Opcode::SHL,      {"SHL",      "Shift left",                      3, true}},
        {Opcode::SHR,      {"SHR",      "Shift right",                     3, true}},
        {Opcode::CMP,      {"CMP",      "Compare two registers",           2, true}},
        {Opcode::TEST,     {"TEST",     "Test (AND) two registers",        2, true}},
        {Opcode::JMP,      {"JMP",      "Unconditional jump",              1, false}},
        {Opcode::JZ,       {"JZ",       "Jump if zero",                    1, false}},
        {Opcode::JNZ,      {"JNZ",      "Jump if not zero",                1, false}},
        {Opcode::JG,       {"JG",       "Jump if greater",                 1, false}},
        {Opcode::JL,       {"JL",       "Jump if less",                    1, false}},
        {Opcode::JGE,      {"JGE",      "Jump if greater or equal",        1, false}},
        {Opcode::JLE,      {"JLE",      "Jump if less or equal",           1, false}},
        {Opcode::CALL,     {"CALL",     "Call subroutine",                 1, false}},
        {Opcode::RET,      {"RET",      "Return from subroutine",          0, false}},
        {Opcode::SYSCALL,  {"SYSCALL",  "System call",                     1, false}},
        {Opcode::HALT,     {"HALT",     "Halt execution",                  0, false}},
        {Opcode::IN,       {"IN",       "Input from port",                 2, false}},
        {Opcode::OUT,      {"OUT",      "Output to port",                  2, false}},
        {Opcode::BREAK,    {"BREAK",    "Breakpoint",                      0, false}},
        {Opcode::PRINT,    {"PRINT",    "Print register (debug)",          1, false}},
    };
    
    auto it = opcodeTable.find(opcode);
    if (it != opcodeTable.end()) {
        return it->second;
    }
    return {"???", "Unknown opcode", 0, false};
}

// ============================================================================
// String'den Opcode'a dönüştürme
// ============================================================================

Opcode stringToOpcode(const std::string& mnemonic) {
    static const std::map<std::string, Opcode> mnemonicTable = {
        {"NOP", Opcode::NOP},
        {"LOAD", Opcode::LOAD_IMM},
        {"STORE", Opcode::STORE},
        {"MOV", Opcode::MOV},
        {"PUSH", Opcode::PUSH},
        {"POP", Opcode::POP},
        {"ADD", Opcode::ADD},
        {"SUB", Opcode::SUB},
        {"MUL", Opcode::MUL},
        {"DIV", Opcode::DIV},
        {"MOD", Opcode::MOD},
        {"INC", Opcode::INC},
        {"DEC", Opcode::DEC},
        {"NEG", Opcode::NEG},
        {"AND", Opcode::AND},
        {"OR", Opcode::OR},
        {"XOR", Opcode::XOR},
        {"NOT", Opcode::NOT},
        {"SHL", Opcode::SHL},
        {"SHR", Opcode::SHR},
        {"CMP", Opcode::CMP},
        {"TEST", Opcode::TEST},
        {"JMP", Opcode::JMP},
        {"JZ", Opcode::JZ},
        {"JNZ", Opcode::JNZ},
        {"JG", Opcode::JG},
        {"JL", Opcode::JL},
        {"JGE", Opcode::JGE},
        {"JLE", Opcode::JLE},
        {"CALL", Opcode::CALL},
        {"RET", Opcode::RET},
        {"SYSCALL", Opcode::SYSCALL},
        {"HALT", Opcode::HALT},
        {"IN", Opcode::IN},
        {"OUT", Opcode::OUT},
        {"BREAK", Opcode::BREAK},
        {"PRINT", Opcode::PRINT},
    };
    
    std::string upper = mnemonic;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    
    auto it = mnemonicTable.find(upper);
    if (it != mnemonicTable.end()) {
        return it->second;
    }
    return Opcode::NOP;
}

// ============================================================================
// SimpleAssembler Implementasyonu
// ============================================================================

std::vector<Instruction> SimpleAssembler::assemble(const std::string& code) {
    auto result = assembleWithLabels(code);
    return result.instructions;
}

std::vector<uint8_t> SimpleAssembler::toBinary(const std::vector<Instruction>& instructions) {
    std::vector<uint8_t> binary;
    binary.reserve(instructions.size() * INSTRUCTION_SIZE);
    
    for (const auto& inst : instructions) {
        auto bytes = inst.encode();
        binary.insert(binary.end(), bytes.begin(), bytes.end());
    }
    
    return binary;
}

std::vector<Instruction> SimpleAssembler::fromBinary(const std::vector<uint8_t>& binary) {
    std::vector<Instruction> instructions;
    
    for (size_t i = 0; i + INSTRUCTION_SIZE <= binary.size(); i += INSTRUCTION_SIZE) {
        instructions.push_back(Instruction::decode(binary.data() + i));
    }
    
    return instructions;
}

SimpleAssembler::AssemblyResult SimpleAssembler::assembleWithLabels(const std::string& code) {
    AssemblyResult result;
    result.success = true;
    
    std::istringstream stream(code);
    std::string line;
    uint32_t address = 0;
    
    // İlk geçiş: Label'ları topla
    while (std::getline(stream, line)) {
        // Yorum ve boşlukları temizle
        size_t commentPos = line.find(';');
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
        }
        
        // Trim
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);
        
        if (line.empty()) continue;
        
        // Label kontrolü
        size_t colonPos = line.find(':');
        if (colonPos != std::string::npos) {
            std::string label = line.substr(0, colonPos);
            result.labels[label] = address;
            line = line.substr(colonPos + 1);
            
            // Trim again
            start = line.find_first_not_of(" \t");
            if (start == std::string::npos) continue;
            end = line.find_last_not_of(" \t\r\n");
            line = line.substr(start, end - start + 1);
        }
        
        if (!line.empty()) {
            address += INSTRUCTION_SIZE;
        }
    }
    
    // İkinci geçiş: Instruction'ları parse et
    stream.clear();
    stream.seekg(0);
    
    while (std::getline(stream, line)) {
        // Yorum ve boşlukları temizle
        size_t commentPos = line.find(';');
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
        }
        
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);
        
        if (line.empty()) continue;
        
        // Label'ı atla
        size_t colonPos = line.find(':');
        if (colonPos != std::string::npos) {
            line = line.substr(colonPos + 1);
            start = line.find_first_not_of(" \t");
            if (start == std::string::npos) continue;
            end = line.find_last_not_of(" \t\r\n");
            line = line.substr(start, end - start + 1);
        }
        
        if (line.empty()) continue;
        
        // Parse instruction
        std::istringstream lineStream(line);
        std::string mnemonic;
        lineStream >> mnemonic;
        
        Opcode opcode = stringToOpcode(mnemonic);
        Instruction inst(opcode);
        
        // Operand'ları parse et
        std::string operands;
        std::getline(lineStream, operands);
        
        // Operand'ları virgülle ayır
        std::vector<std::string> ops;
        std::istringstream opStream(operands);
        std::string op;
        while (std::getline(opStream, op, ',')) {
            size_t s = op.find_first_not_of(" \t");
            if (s != std::string::npos) {
                size_t e = op.find_last_not_of(" \t");
                ops.push_back(op.substr(s, e - s + 1));
            }
        }
        
        // Operand'ları instruction'a ata
        auto parseRegister = [](const std::string& s) -> uint8_t {
            if (s.empty()) return 0;
            if (s[0] == 'R' || s[0] == 'r') {
                return static_cast<uint8_t>(std::stoi(s.substr(1)));
            }
            return 0;
        };
        
        auto parseImmediate = [&result](const std::string& s) -> int32_t {
            if (s.empty()) return 0;
            
            // Label mi?
            if (result.labels.find(s) != result.labels.end()) {
                return static_cast<int32_t>(result.labels[s]);
            }
            
            // Memory reference [addr]?
            if (s[0] == '[' && s.back() == ']') {
                std::string inner = s.substr(1, s.size() - 2);
                if (result.labels.find(inner) != result.labels.end()) {
                    return static_cast<int32_t>(result.labels[inner]);
                }
                return std::stoi(inner);
            }
            
            // Hex?
            if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
                return static_cast<int32_t>(std::stoul(s, nullptr, 16));
            }
            
            return std::stoi(s);
        };
        
        switch (opcode) {
            case Opcode::LOAD_IMM:
                if (ops.size() >= 2) {
                    inst.dest = parseRegister(ops[0]);
                    // Memory reference mi immediate mi?
                    if (ops[1][0] == '[') {
                        inst.opcode = Opcode::LOAD_MEM;
                    }
                    inst.immediate = parseImmediate(ops[1]);
                }
                break;
                
            case Opcode::STORE:
                if (ops.size() >= 2) {
                    inst.dest = parseRegister(ops[0]);
                    inst.immediate = parseImmediate(ops[1]);
                }
                break;
                
            case Opcode::MOV:
                if (ops.size() >= 2) {
                    inst.dest = parseRegister(ops[0]);
                    inst.src1 = parseRegister(ops[1]);
                }
                break;
                
            case Opcode::PUSH:
            case Opcode::POP:
            case Opcode::INC:
            case Opcode::DEC:
            case Opcode::NEG:
            case Opcode::NOT:
            case Opcode::PRINT:
                if (ops.size() >= 1) {
                    inst.dest = parseRegister(ops[0]);
                }
                break;
                
            case Opcode::ADD:
            case Opcode::SUB:
            case Opcode::MUL:
            case Opcode::DIV:
            case Opcode::MOD:
            case Opcode::AND:
            case Opcode::OR:
            case Opcode::XOR:
                if (ops.size() >= 3) {
                    inst.dest = parseRegister(ops[0]);
                    inst.src1 = parseRegister(ops[1]);
                    inst.src2 = parseRegister(ops[2]);
                }
                break;
                
            case Opcode::SHL:
            case Opcode::SHR:
                if (ops.size() >= 3) {
                    inst.dest = parseRegister(ops[0]);
                    inst.src1 = parseRegister(ops[1]);
                    inst.src2 = static_cast<uint8_t>(parseImmediate(ops[2]));
                }
                break;
                
            case Opcode::CMP:
            case Opcode::TEST:
                if (ops.size() >= 2) {
                    inst.dest = parseRegister(ops[0]);
                    inst.src1 = parseRegister(ops[1]);
                }
                break;
                
            case Opcode::JMP:
            case Opcode::JZ:
            case Opcode::JNZ:
            case Opcode::JG:
            case Opcode::JL:
            case Opcode::JGE:
            case Opcode::JLE:
            case Opcode::CALL:
                if (ops.size() >= 1) {
                    inst.immediate = parseImmediate(ops[0]);
                }
                break;
                
            case Opcode::SYSCALL:
                if (ops.size() >= 1) {
                    inst.dest = static_cast<uint8_t>(parseImmediate(ops[0]));
                }
                break;
                
            case Opcode::IN:
                if (ops.size() >= 2) {
                    inst.dest = parseRegister(ops[0]);
                    inst.src1 = static_cast<uint8_t>(parseImmediate(ops[1]));
                }
                break;
                
            case Opcode::OUT:
                if (ops.size() >= 2) {
                    inst.dest = static_cast<uint8_t>(parseImmediate(ops[0]));
                    inst.src1 = parseRegister(ops[1]);
                }
                break;
                
            default:
                break;
        }
        
        result.instructions.push_back(inst);
    }
    
    return result;
}

// ============================================================================
// Predefined Programs
// ============================================================================

namespace Programs {

// Basit sayaç programı (R0'ı 10'a kadar say)
std::vector<Instruction> counter() {
    return {
        Instruction(Opcode::LOAD_IMM, 0, 0),      // R0 = 0
        Instruction(Opcode::LOAD_IMM, 1, 10),     // R1 = 10 (limit)
        // loop:
        Instruction(Opcode::INC, 0),              // R0++
        Instruction(Opcode::PRINT, 0),            // Print R0
        Instruction(Opcode::CMP, 0, 1),           // Compare R0, R1
        Instruction(Opcode::JL, 0, 0, 0),         // JL loop (addr = 16)
        Instruction(Opcode::HALT)                 // Halt
    };
    // Fix jump address
}

// Fibonacci hesaplayan program
std::vector<Instruction> fibonacci(int n) {
    std::vector<Instruction> program;
    
    // R0 = fib(n-2), R1 = fib(n-1), R2 = current, R3 = counter, R4 = n
    program.push_back(Instruction(Opcode::LOAD_IMM, 0, 0));      // R0 = 0 (fib(0))
    program.push_back(Instruction(Opcode::LOAD_IMM, 1, 1));      // R1 = 1 (fib(1))
    program.push_back(Instruction(Opcode::LOAD_IMM, 3, 2));      // R3 = 2 (counter)
    program.push_back(Instruction(Opcode::LOAD_IMM, 4, n));      // R4 = n
    
    // loop:
    program.push_back(Instruction(Opcode::CMP, 3, 4));           // Compare counter, n
    program.push_back(Instruction(Opcode::JG, 0, 0, 0));         // JG end (will fix)
    
    program.push_back(Instruction(Opcode::ADD, 2, 0, 1));        // R2 = R0 + R1
    program.push_back(Instruction(Opcode::MOV, 0, 1));           // R0 = R1
    program.push_back(Instruction(Opcode::MOV, 1, 2));           // R1 = R2
    program.push_back(Instruction(Opcode::INC, 3));              // counter++
    program.push_back(Instruction(Opcode::PRINT, 2));            // Print current fib
    
    // JMP loop
    auto jmpInst = Instruction(Opcode::JMP);
    jmpInst.immediate = 4 * INSTRUCTION_SIZE;  // Address of CMP instruction
    program.push_back(jmpInst);
    
    // end:
    program.push_back(Instruction(Opcode::MOV, 0, 1));           // R0 = result
    program.push_back(Instruction(Opcode::HALT));
    
    // Fix JG address
    program[5].immediate = 12 * INSTRUCTION_SIZE;
    
    return program;
}

// Faktöriyel hesaplayan program
std::vector<Instruction> factorial(int n) {
    std::vector<Instruction> program;
    
    // R0 = result, R1 = counter, R2 = n
    program.push_back(Instruction(Opcode::LOAD_IMM, 0, 1));      // R0 = 1 (result)
    program.push_back(Instruction(Opcode::LOAD_IMM, 1, 1));      // R1 = 1 (counter)
    program.push_back(Instruction(Opcode::LOAD_IMM, 2, n));      // R2 = n
    
    // loop:
    program.push_back(Instruction(Opcode::CMP, 1, 2));           // Compare counter, n
    program.push_back(Instruction(Opcode::JG, 0, 0, 0));         // JG end
    
    program.push_back(Instruction(Opcode::MUL, 0, 0, 1));        // R0 = R0 * R1
    program.push_back(Instruction(Opcode::INC, 1));              // counter++
    program.push_back(Instruction(Opcode::PRINT, 0));            // Print intermediate
    
    // JMP loop
    auto jmpInst = Instruction(Opcode::JMP);
    jmpInst.immediate = 3 * INSTRUCTION_SIZE;
    program.push_back(jmpInst);
    
    // end:
    program.push_back(Instruction(Opcode::HALT));
    
    // Fix JG address
    program[4].immediate = 9 * INSTRUCTION_SIZE;
    
    return program;
}

// Memory copy programı
std::vector<Instruction> memcpy_prog(uint32_t src, uint32_t dst, uint32_t size) {
    std::vector<Instruction> program;
    
    // R0 = src, R1 = dst, R2 = size, R3 = counter, R4 = temp
    program.push_back(Instruction(Opcode::LOAD_IMM, 0, static_cast<int32_t>(src)));
    program.push_back(Instruction(Opcode::LOAD_IMM, 1, static_cast<int32_t>(dst)));
    program.push_back(Instruction(Opcode::LOAD_IMM, 2, static_cast<int32_t>(size)));
    program.push_back(Instruction(Opcode::LOAD_IMM, 3, 0));      // counter = 0
    
    // loop:
    program.push_back(Instruction(Opcode::CMP, 3, 2));           // Compare counter, size
    program.push_back(Instruction(Opcode::JGE, 0, 0, 0));        // JGE end
    
    // Load from src + counter
    program.push_back(Instruction(Opcode::ADD, 5, 0, 3));        // R5 = src + counter
    auto loadInst = Instruction(Opcode::LOAD_MEM, 4);
    program.push_back(loadInst);                                  // R4 = [R5] (simplified)
    
    // Store to dst + counter
    program.push_back(Instruction(Opcode::ADD, 6, 1, 3));        // R6 = dst + counter
    auto storeInst = Instruction(Opcode::STORE, 4);
    program.push_back(storeInst);                                 // [R6] = R4 (simplified)
    
    program.push_back(Instruction(Opcode::INC, 3));              // counter++
    
    auto jmpInst = Instruction(Opcode::JMP);
    jmpInst.immediate = 4 * INSTRUCTION_SIZE;
    program.push_back(jmpInst);
    
    // end:
    program.push_back(Instruction(Opcode::HALT));
    
    // Fix JGE address
    program[5].immediate = 12 * INSTRUCTION_SIZE;
    
    return program;
}

// Bubble sort programı (basitleştirilmiş, küçük array için)
std::vector<Instruction> bubbleSort() {
    // Bu basit bir örnek - gerçek bubble sort için daha kompleks code gerekir
    std::vector<Instruction> program;
    
    // Örnek: 4 elemanlı array sırala
    // Data segment'te: [5, 3, 8, 1]
    
    program.push_back(Instruction(Opcode::LOAD_IMM, 0, 4));      // R0 = array size
    program.push_back(Instruction(Opcode::LOAD_IMM, 1, 0));      // R1 = outer index
    
    // outer_loop:
    program.push_back(Instruction(Opcode::LOAD_IMM, 2, 0));      // R2 = inner index
    
    // inner_loop:
    // ... (karşılaştırma ve swap logic)
    
    program.push_back(Instruction(Opcode::HALT));
    
    return program;
}

} // namespace Programs

} // namespace process
} // namespace checkpoint
