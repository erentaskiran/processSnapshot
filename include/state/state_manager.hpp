#pragma once

#include "core/types.hpp"
#include <string>
#include <vector>
#include <map>
#include <any>
#include <filesystem>

namespace checkpoint {

// Checkpoint metadata yapısı
struct CheckpointMetadata {
    CheckpointId id;
    std::string name;
    std::string description;
    Timestamp createdAt;
    Timestamp modifiedAt;
    CheckpointStatus status;
    size_t dataSize;
    uint32_t checksum;
    std::map<std::string, std::string> tags;
    
    // Serileştirme
    StateData serialize() const;
    static CheckpointMetadata deserialize(const StateData& data);
};

// Tek bir checkpoint kaydı
class Checkpoint {
private:
    CheckpointMetadata m_metadata;
    StateData m_data;
    std::vector<OperationId> m_relatedOperations;
    
public:
    Checkpoint() = default;
    Checkpoint(CheckpointId id, const std::string& name);
    Checkpoint(const Checkpoint&) = default;
    Checkpoint(Checkpoint&&) noexcept = default;
    Checkpoint& operator=(const Checkpoint&) = default;
    Checkpoint& operator=(Checkpoint&&) noexcept = default;
    
    // Getter'lar
    CheckpointId getId() const { return m_metadata.id; }
    const std::string& getName() const { return m_metadata.name; }
    const std::string& getDescription() const { return m_metadata.description; }
    Timestamp getCreatedAt() const { return m_metadata.createdAt; }
    CheckpointStatus getStatus() const { return m_metadata.status; }
    const StateData& getData() const { return m_data; }
    const CheckpointMetadata& getMetadata() const { return m_metadata; }
    const std::vector<OperationId>& getRelatedOperations() const { return m_relatedOperations; }
    
    // Setter'lar
    void setName(const std::string& name);
    void setDescription(const std::string& description);
    void setStatus(CheckpointStatus status);
    void setData(const StateData& data);
    void setData(StateData&& data);
    void addTag(const std::string& key, const std::string& value);
    void addRelatedOperation(OperationId opId);
    
    // Doğrulama
    bool isValid() const;
    bool verifyIntegrity() const;
    
    // Serileştirme
    StateData serialize() const;
    static Checkpoint deserialize(const StateData& data);
};

// İşlem kaydı - log entry
struct OperationRecord {
    OperationId id;
    OperationType type;
    Timestamp timestamp;
    CheckpointId relatedCheckpoint;
    std::string description;
    StateData previousState;  // Geri alma için önceki durum
    StateData newState;       // Yeni durum
    bool canUndo;
    
    StateData serialize() const;
    static OperationRecord deserialize(const StateData& data);
};

// Durum yöneticisi arayüzü
class IStateManager {
public:
    virtual ~IStateManager() = default;
    
    // Checkpoint işlemleri
    virtual Result<CheckpointId> createCheckpoint(const std::string& name, const StateData& state) = 0;
    virtual Result<Checkpoint> getCheckpoint(CheckpointId id) = 0;
    virtual Result<void> updateCheckpoint(CheckpointId id, const StateData& state) = 0;
    virtual Result<void> deleteCheckpoint(CheckpointId id) = 0;
    virtual std::vector<CheckpointMetadata> listCheckpoints() = 0;
    
    // Otomatik kaydetme
    virtual void setAutoSaveInterval(Duration interval) = 0;
    virtual void enableAutoSave(bool enable) = 0;
    
    // Durum sorgulama
    virtual Result<StateData> getCurrentState() = 0;
    virtual Result<CheckpointId> getLatestCheckpointId() = 0;
};

// Durum yöneticisi implementasyonu
class StateManager : public IStateManager {
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    
public:
    StateManager();
    explicit StateManager(const std::filesystem::path& storagePath);
    ~StateManager();
    
    // Move semantics
    StateManager(StateManager&&) noexcept;
    StateManager& operator=(StateManager&&) noexcept;
    
    // IStateManager implementasyonu
    Result<CheckpointId> createCheckpoint(const std::string& name, const StateData& state) override;
    Result<Checkpoint> getCheckpoint(CheckpointId id) override;
    Result<void> updateCheckpoint(CheckpointId id, const StateData& state) override;
    Result<void> deleteCheckpoint(CheckpointId id) override;
    std::vector<CheckpointMetadata> listCheckpoints() override;
    
    void setAutoSaveInterval(Duration interval) override;
    void enableAutoSave(bool enable) override;
    
    Result<StateData> getCurrentState() override;
    Result<CheckpointId> getLatestCheckpointId() override;
    
    // Ek özellikler
    Result<void> saveToFile(CheckpointId id, const std::filesystem::path& path);
    Result<Checkpoint> loadFromFile(const std::filesystem::path& path);
    
    // İstatistikler
    size_t getCheckpointCount() const;
    size_t getTotalStorageSize() const;
};

} // namespace checkpoint
