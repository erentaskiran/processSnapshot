#include "core/serializer.hpp"
#include <cstring>
#include <numeric>
#include <algorithm>

namespace checkpoint {

// ==================== BinarySerializer ====================

StateData BinarySerializer::serialize(const void* data, size_t size) {
    StateData result(size);
    std::memcpy(result.data(), data, size);
    return result;
}

bool BinarySerializer::deserialize(const StateData& data, void* output, size_t size) {
    if (data.size() < size) {
        return false;
    }
    std::memcpy(output, data.data(), size);
    return true;
}

StateData BinarySerializer::compress(const StateData& data) {
    // Basit RLE (Run-Length Encoding) sıkıştırma
    // TODO: Gerçek bir sıkıştırma kütüphanesi (zlib, lz4) kullanılabilir
    StateData compressed;
    compressed.reserve(data.size());
    
    size_t i = 0;
    while (i < data.size()) {
        uint8_t current = data[i];
        size_t count = 1;
        
        while (i + count < data.size() && 
               data[i + count] == current && 
               count < 255) {
            count++;
        }
        
        if (count >= 4 || current == 0xFF) {
            compressed.push_back(0xFF);  // Escape character
            compressed.push_back(static_cast<uint8_t>(count));
            compressed.push_back(current);
        } else {
            for (size_t j = 0; j < count; j++) {
                compressed.push_back(current);
            }
        }
        
        i += count;
    }
    
    return compressed;
}

StateData BinarySerializer::decompress(const StateData& data) {
    StateData decompressed;
    decompressed.reserve(data.size() * 2);
    
    size_t i = 0;
    while (i < data.size()) {
        if (data[i] == 0xFF && i + 2 < data.size()) {
            size_t count = data[i + 1];
            uint8_t value = data[i + 2];
            for (size_t j = 0; j < count; j++) {
                decompressed.push_back(value);
            }
            i += 3;
        } else {
            decompressed.push_back(data[i]);
            i++;
        }
    }
    
    return decompressed;
}

uint32_t BinarySerializer::calculateChecksum(const StateData& data) {
    // CRC32-like checksum
    uint32_t checksum = 0xFFFFFFFF;
    
    for (uint8_t byte : data) {
        checksum ^= byte;
        for (int i = 0; i < 8; i++) {
            if (checksum & 1) {
                checksum = (checksum >> 1) ^ 0xEDB88320;
            } else {
                checksum >>= 1;
            }
        }
    }
    
    return ~checksum;
}

bool BinarySerializer::verifyChecksum(const StateData& data, uint32_t checksum) {
    return calculateChecksum(data) == checksum;
}

// ==================== JsonSerializer ====================

StateData JsonSerializer::serialize(const void* data, size_t size) {
    // JSON formatında hex string olarak kaydet
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    std::string json = "{\"data\":\"";
    
    static const char hex_chars[] = "0123456789ABCDEF";
    for (size_t i = 0; i < size; i++) {
        json += hex_chars[(bytes[i] >> 4) & 0x0F];
        json += hex_chars[bytes[i] & 0x0F];
    }
    
    json += "\",\"size\":" + std::to_string(size) + "}";
    
    return StateData(json.begin(), json.end());
}

bool JsonSerializer::deserialize(const StateData& data, void* output, size_t size) {
    std::string json(data.begin(), data.end());
    
    // Basit JSON parsing
    auto dataStart = json.find("\"data\":\"");
    if (dataStart == std::string::npos) return false;
    
    dataStart += 8;  // "data":" uzunluğu
    auto dataEnd = json.find("\"", dataStart);
    if (dataEnd == std::string::npos) return false;
    
    std::string hexStr = json.substr(dataStart, dataEnd - dataStart);
    
    if (hexStr.size() / 2 < size) return false;
    
    uint8_t* bytes = static_cast<uint8_t*>(output);
    for (size_t i = 0; i < size; i++) {
        char high = hexStr[i * 2];
        char low = hexStr[i * 2 + 1];
        
        auto hexValue = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return 0;
        };
        
        bytes[i] = (hexValue(high) << 4) | hexValue(low);
    }
    
    return true;
}

StateData JsonSerializer::compress(const StateData& data) {
    // JSON için sıkıştırma yapmıyoruz (okunabilirlik için)
    return data;
}

StateData JsonSerializer::decompress(const StateData& data) {
    return data;
}

uint32_t JsonSerializer::calculateChecksum(const StateData& data) {
    // Binary serializer ile aynı
    BinarySerializer binary;
    return binary.calculateChecksum(data);
}

bool JsonSerializer::verifyChecksum(const StateData& data, uint32_t checksum) {
    return calculateChecksum(data) == checksum;
}

std::string JsonSerializer::toJsonString(const StateData& data) {
    return std::string(data.begin(), data.end());
}

StateData JsonSerializer::fromJsonString(const std::string& json) {
    return StateData(json.begin(), json.end());
}

} // namespace checkpoint
