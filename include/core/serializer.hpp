#pragma once

#include "core/types.hpp"
#include <string>
#include <vector>
#include <concepts>
#include <cstring>

namespace checkpoint {

// Serileştirilebilir kavramı
template<typename T>
concept Serializable = requires(T obj, StateData& data) {
    { obj.serialize() } -> std::convertible_to<StateData>;
    { T::deserialize(data) } -> std::convertible_to<T>;
};

// Temel serileştirici arayüzü
class ISerializer {
public:
    virtual ~ISerializer() = default;
    
    virtual StateData serialize(const void* data, size_t size) = 0;
    virtual bool deserialize(const StateData& data, void* output, size_t size) = 0;
    
    // Sıkıştırma desteği
    virtual StateData compress(const StateData& data) = 0;
    virtual StateData decompress(const StateData& data) = 0;
    
    // Checksum hesaplama
    virtual uint32_t calculateChecksum(const StateData& data) = 0;
    virtual bool verifyChecksum(const StateData& data, uint32_t checksum) = 0;
};

// Binary serileştirici implementasyonu
class BinarySerializer : public ISerializer {
public:
    StateData serialize(const void* data, size_t size) override;
    bool deserialize(const StateData& data, void* output, size_t size) override;
    
    StateData compress(const StateData& data) override;
    StateData decompress(const StateData& data) override;
    
    uint32_t calculateChecksum(const StateData& data) override;
    bool verifyChecksum(const StateData& data, uint32_t checksum) override;
    
    // Yardımcı template fonksiyonlar
    template<typename T>
    StateData serializeObject(const T& obj) {
        StateData data(sizeof(T));
        std::memcpy(data.data(), &obj, sizeof(T));
        return data;
    }
    
    template<typename T>
    T deserializeObject(const StateData& data) {
        T obj;
        std::memcpy(&obj, data.data(), sizeof(T));
        return obj;
    }
};

// JSON serileştirici (opsiyonel, daha okunaklı çıktı için)
class JsonSerializer : public ISerializer {
public:
    StateData serialize(const void* data, size_t size) override;
    bool deserialize(const StateData& data, void* output, size_t size) override;
    
    StateData compress(const StateData& data) override;
    StateData decompress(const StateData& data) override;
    
    uint32_t calculateChecksum(const StateData& data) override;
    bool verifyChecksum(const StateData& data, uint32_t checksum) override;
    
    // JSON spesifik metodlar
    std::string toJsonString(const StateData& data);
    StateData fromJsonString(const std::string& json);
};

} // namespace checkpoint
