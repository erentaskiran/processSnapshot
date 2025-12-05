#pragma once

#include "core/types.hpp"
#include "state/state_manager.hpp"
#include <filesystem>
#include <fstream>
#include <map>

namespace checkpoint {

// Depolama arayüzü
class IStorage {
public:
    virtual ~IStorage() = default;
    
    virtual Result<void> save(CheckpointId id, const StateData& data) = 0;
    virtual Result<StateData> load(CheckpointId id) = 0;
    virtual Result<void> remove(CheckpointId id) = 0;
    virtual bool exists(CheckpointId id) = 0;
    virtual std::vector<CheckpointId> listAll() = 0;
    virtual size_t getSize(CheckpointId id) = 0;
    virtual size_t getTotalSize() = 0;
};

// Dosya tabanlı depolama
class FileStorage : public IStorage {
private:
    std::filesystem::path m_basePath;
    std::string m_extension;
    
    std::filesystem::path getFilePath(CheckpointId id) const;
    
public:
    explicit FileStorage(const std::filesystem::path& basePath, 
                        const std::string& extension = ".chkpt");
    
    Result<void> save(CheckpointId id, const StateData& data) override;
    Result<StateData> load(CheckpointId id) override;
    Result<void> remove(CheckpointId id) override;
    bool exists(CheckpointId id) override;
    std::vector<CheckpointId> listAll() override;
    size_t getSize(CheckpointId id) override;
    size_t getTotalSize() override;
    
    // Dosya spesifik
    std::filesystem::path getBasePath() const { return m_basePath; }
    void setBasePath(const std::filesystem::path& path);
    Result<void> cleanup(Duration olderThan);
};

// Bellek içi depolama (test ve geçici kullanım için)
class MemoryStorage : public IStorage {
private:
    std::map<CheckpointId, StateData> m_storage;
    size_t m_maxSize;
    
public:
    explicit MemoryStorage(size_t maxSize = 100 * 1024 * 1024); // Default 100MB
    
    Result<void> save(CheckpointId id, const StateData& data) override;
    Result<StateData> load(CheckpointId id) override;
    Result<void> remove(CheckpointId id) override;
    bool exists(CheckpointId id) override;
    std::vector<CheckpointId> listAll() override;
    size_t getSize(CheckpointId id) override;
    size_t getTotalSize() override;
    
    void clear();
    void setMaxSize(size_t size) { m_maxSize = size; }
};

// Hibrit depolama (önce bellek, sonra dosya)
class HybridStorage : public IStorage {
private:
    std::unique_ptr<MemoryStorage> m_memoryStorage;
    std::unique_ptr<FileStorage> m_fileStorage;
    size_t m_memoryThreshold;
    
public:
    HybridStorage(const std::filesystem::path& basePath, 
                  size_t memoryThreshold = 10 * 1024 * 1024);
    
    Result<void> save(CheckpointId id, const StateData& data) override;
    Result<StateData> load(CheckpointId id) override;
    Result<void> remove(CheckpointId id) override;
    bool exists(CheckpointId id) override;
    std::vector<CheckpointId> listAll() override;
    size_t getSize(CheckpointId id) override;
    size_t getTotalSize() override;
    
    void flushToFile();
    void loadToMemory(CheckpointId id);
};

} // namespace checkpoint
