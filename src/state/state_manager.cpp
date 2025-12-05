#include "state/state_manager.hpp"
#include "state/storage.hpp"
#include "core/serializer.hpp"
#include "utils/helpers.hpp"
#include <mutex>
#include <map>
#include <thread>
#include <atomic>
#include <condition_variable>

namespace checkpoint {

// ==================== CheckpointMetadata ====================

StateData CheckpointMetadata::serialize() const {
    BinarySerializer serializer;
    StateData data;
    
    // ID
    auto idData = serializer.serializeObject(id);
    data.insert(data.end(), idData.begin(), idData.end());
    
    // Name length + name
    uint32_t nameLen = static_cast<uint32_t>(name.size());
    auto nameLenData = serializer.serializeObject(nameLen);
    data.insert(data.end(), nameLenData.begin(), nameLenData.end());
    data.insert(data.end(), name.begin(), name.end());
    
    // Description length + description
    uint32_t descLen = static_cast<uint32_t>(description.size());
    auto descLenData = serializer.serializeObject(descLen);
    data.insert(data.end(), descLenData.begin(), descLenData.end());
    data.insert(data.end(), description.begin(), description.end());
    
    // Timestamps
    int64_t created = utils::TimeUtils::toUnixTimestamp(createdAt);
    int64_t modified = utils::TimeUtils::toUnixTimestamp(modifiedAt);
    auto createdData = serializer.serializeObject(created);
    auto modifiedData = serializer.serializeObject(modified);
    data.insert(data.end(), createdData.begin(), createdData.end());
    data.insert(data.end(), modifiedData.begin(), modifiedData.end());
    
    // Status
    auto statusData = serializer.serializeObject(static_cast<int>(status));
    data.insert(data.end(), statusData.begin(), statusData.end());
    
    // Data size and checksum
    auto sizeData = serializer.serializeObject(dataSize);
    auto checksumData = serializer.serializeObject(checksum);
    data.insert(data.end(), sizeData.begin(), sizeData.end());
    data.insert(data.end(), checksumData.begin(), checksumData.end());
    
    return data;
}

CheckpointMetadata CheckpointMetadata::deserialize(const StateData& data) {
    CheckpointMetadata meta;
    BinarySerializer serializer;
    size_t offset = 0;
    
    // ID
    meta.id = serializer.deserializeObject<CheckpointId>(
        StateData(data.begin() + offset, data.begin() + offset + sizeof(CheckpointId)));
    offset += sizeof(CheckpointId);
    
    // Name
    uint32_t nameLen = serializer.deserializeObject<uint32_t>(
        StateData(data.begin() + offset, data.begin() + offset + sizeof(uint32_t)));
    offset += sizeof(uint32_t);
    meta.name = std::string(data.begin() + offset, data.begin() + offset + nameLen);
    offset += nameLen;
    
    // Description
    uint32_t descLen = serializer.deserializeObject<uint32_t>(
        StateData(data.begin() + offset, data.begin() + offset + sizeof(uint32_t)));
    offset += sizeof(uint32_t);
    meta.description = std::string(data.begin() + offset, data.begin() + offset + descLen);
    offset += descLen;
    
    // Timestamps
    int64_t created = serializer.deserializeObject<int64_t>(
        StateData(data.begin() + offset, data.begin() + offset + sizeof(int64_t)));
    offset += sizeof(int64_t);
    int64_t modified = serializer.deserializeObject<int64_t>(
        StateData(data.begin() + offset, data.begin() + offset + sizeof(int64_t)));
    offset += sizeof(int64_t);
    
    meta.createdAt = utils::TimeUtils::fromUnixTimestamp(created);
    meta.modifiedAt = utils::TimeUtils::fromUnixTimestamp(modified);
    
    // Status
    int statusInt = serializer.deserializeObject<int>(
        StateData(data.begin() + offset, data.begin() + offset + sizeof(int)));
    offset += sizeof(int);
    meta.status = static_cast<CheckpointStatus>(statusInt);
    
    // Data size and checksum
    meta.dataSize = serializer.deserializeObject<size_t>(
        StateData(data.begin() + offset, data.begin() + offset + sizeof(size_t)));
    offset += sizeof(size_t);
    meta.checksum = serializer.deserializeObject<uint32_t>(
        StateData(data.begin() + offset, data.begin() + offset + sizeof(uint32_t)));
    
    return meta;
}

// ==================== Checkpoint ====================

Checkpoint::Checkpoint(CheckpointId id, const std::string& name) {
    m_metadata.id = id;
    m_metadata.name = name;
    m_metadata.createdAt = utils::TimeUtils::now();
    m_metadata.modifiedAt = m_metadata.createdAt;
    m_metadata.status = CheckpointStatus::Pending;
    m_metadata.dataSize = 0;
    m_metadata.checksum = 0;
}

void Checkpoint::setName(const std::string& name) {
    m_metadata.name = name;
    m_metadata.modifiedAt = utils::TimeUtils::now();
}

void Checkpoint::setDescription(const std::string& description) {
    m_metadata.description = description;
    m_metadata.modifiedAt = utils::TimeUtils::now();
}

void Checkpoint::setStatus(CheckpointStatus status) {
    m_metadata.status = status;
    m_metadata.modifiedAt = utils::TimeUtils::now();
}

void Checkpoint::setData(const StateData& data) {
    m_data = data;
    m_metadata.dataSize = data.size();
    
    BinarySerializer serializer;
    m_metadata.checksum = serializer.calculateChecksum(data);
    m_metadata.modifiedAt = utils::TimeUtils::now();
}

void Checkpoint::setData(StateData&& data) {
    m_data = std::move(data);
    m_metadata.dataSize = m_data.size();
    
    BinarySerializer serializer;
    m_metadata.checksum = serializer.calculateChecksum(m_data);
    m_metadata.modifiedAt = utils::TimeUtils::now();
}

void Checkpoint::addTag(const std::string& key, const std::string& value) {
    m_metadata.tags[key] = value;
    m_metadata.modifiedAt = utils::TimeUtils::now();
}

void Checkpoint::addRelatedOperation(OperationId opId) {
    m_relatedOperations.push_back(opId);
}

bool Checkpoint::isValid() const {
    return m_metadata.id != 0 && 
           !m_metadata.name.empty() &&
           m_metadata.status != CheckpointStatus::Corrupted;
}

bool Checkpoint::verifyIntegrity() const {
    if (m_data.empty()) return true;
    
    BinarySerializer serializer;
    return serializer.verifyChecksum(m_data, m_metadata.checksum);
}

StateData Checkpoint::serialize() const {
    StateData result;
    
    // Metadata
    auto metaData = m_metadata.serialize();
    uint32_t metaSize = static_cast<uint32_t>(metaData.size());
    
    BinarySerializer serializer;
    auto metaSizeData = serializer.serializeObject(metaSize);
    result.insert(result.end(), metaSizeData.begin(), metaSizeData.end());
    result.insert(result.end(), metaData.begin(), metaData.end());
    
    // Data
    uint32_t dataSize = static_cast<uint32_t>(m_data.size());
    auto dataSizeData = serializer.serializeObject(dataSize);
    result.insert(result.end(), dataSizeData.begin(), dataSizeData.end());
    result.insert(result.end(), m_data.begin(), m_data.end());
    
    // Related operations
    uint32_t opCount = static_cast<uint32_t>(m_relatedOperations.size());
    auto opCountData = serializer.serializeObject(opCount);
    result.insert(result.end(), opCountData.begin(), opCountData.end());
    
    for (OperationId opId : m_relatedOperations) {
        auto opData = serializer.serializeObject(opId);
        result.insert(result.end(), opData.begin(), opData.end());
    }
    
    return result;
}

Checkpoint Checkpoint::deserialize(const StateData& data) {
    Checkpoint checkpoint;
    BinarySerializer serializer;
    size_t offset = 0;
    
    // Metadata size
    uint32_t metaSize = serializer.deserializeObject<uint32_t>(
        StateData(data.begin() + offset, data.begin() + offset + sizeof(uint32_t)));
    offset += sizeof(uint32_t);
    
    // Metadata
    checkpoint.m_metadata = CheckpointMetadata::deserialize(
        StateData(data.begin() + offset, data.begin() + offset + metaSize));
    offset += metaSize;
    
    // Data size
    uint32_t dataSize = serializer.deserializeObject<uint32_t>(
        StateData(data.begin() + offset, data.begin() + offset + sizeof(uint32_t)));
    offset += sizeof(uint32_t);
    
    // Data
    checkpoint.m_data = StateData(data.begin() + offset, data.begin() + offset + dataSize);
    offset += dataSize;
    
    // Related operations
    uint32_t opCount = serializer.deserializeObject<uint32_t>(
        StateData(data.begin() + offset, data.begin() + offset + sizeof(uint32_t)));
    offset += sizeof(uint32_t);
    
    for (uint32_t i = 0; i < opCount; i++) {
        OperationId opId = serializer.deserializeObject<OperationId>(
            StateData(data.begin() + offset, data.begin() + offset + sizeof(OperationId)));
        offset += sizeof(OperationId);
        checkpoint.m_relatedOperations.push_back(opId);
    }
    
    return checkpoint;
}

// ==================== OperationRecord ====================

StateData OperationRecord::serialize() const {
    BinarySerializer serializer;
    StateData result;
    
    auto idData = serializer.serializeObject(id);
    result.insert(result.end(), idData.begin(), idData.end());
    
    auto typeData = serializer.serializeObject(static_cast<int>(type));
    result.insert(result.end(), typeData.begin(), typeData.end());
    
    int64_t ts = utils::TimeUtils::toUnixTimestamp(timestamp);
    auto tsData = serializer.serializeObject(ts);
    result.insert(result.end(), tsData.begin(), tsData.end());
    
    auto cpData = serializer.serializeObject(relatedCheckpoint);
    result.insert(result.end(), cpData.begin(), cpData.end());
    
    uint32_t descLen = static_cast<uint32_t>(description.size());
    auto descLenData = serializer.serializeObject(descLen);
    result.insert(result.end(), descLenData.begin(), descLenData.end());
    result.insert(result.end(), description.begin(), description.end());
    
    auto canUndoData = serializer.serializeObject(canUndo);
    result.insert(result.end(), canUndoData.begin(), canUndoData.end());
    
    return result;
}

OperationRecord OperationRecord::deserialize(const StateData& data) {
    OperationRecord record;
    BinarySerializer serializer;
    size_t offset = 0;
    
    record.id = serializer.deserializeObject<OperationId>(
        StateData(data.begin() + offset, data.begin() + offset + sizeof(OperationId)));
    offset += sizeof(OperationId);
    
    int typeInt = serializer.deserializeObject<int>(
        StateData(data.begin() + offset, data.begin() + offset + sizeof(int)));
    offset += sizeof(int);
    record.type = static_cast<OperationType>(typeInt);
    
    int64_t ts = serializer.deserializeObject<int64_t>(
        StateData(data.begin() + offset, data.begin() + offset + sizeof(int64_t)));
    offset += sizeof(int64_t);
    record.timestamp = utils::TimeUtils::fromUnixTimestamp(ts);
    
    record.relatedCheckpoint = serializer.deserializeObject<CheckpointId>(
        StateData(data.begin() + offset, data.begin() + offset + sizeof(CheckpointId)));
    offset += sizeof(CheckpointId);
    
    uint32_t descLen = serializer.deserializeObject<uint32_t>(
        StateData(data.begin() + offset, data.begin() + offset + sizeof(uint32_t)));
    offset += sizeof(uint32_t);
    record.description = std::string(data.begin() + offset, data.begin() + offset + descLen);
    offset += descLen;
    
    record.canUndo = serializer.deserializeObject<bool>(
        StateData(data.begin() + offset, data.begin() + offset + sizeof(bool)));
    
    return record;
}

// ==================== StateManager Implementation ====================

struct StateManager::Impl {
    std::unique_ptr<IStorage> storage;
    std::map<CheckpointId, Checkpoint> checkpoints;
    StateData currentState;
    CheckpointId latestCheckpointId = 0;
    
    std::mutex mutex;
    
    // Auto-save
    bool autoSaveEnabled = false;
    Duration autoSaveInterval = Duration(60000);  // 1 dakika
    std::thread autoSaveThread;
    std::atomic<bool> running{false};
    std::condition_variable cv;
    
    void autoSaveLoop() {
        while (running) {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait_for(lock, autoSaveInterval, [this]{ return !running.load(); });
            
            if (running && autoSaveEnabled && !currentState.empty()) {
                // Auto-save checkpoint oluştur
                auto id = utils::IdGenerator::generateCheckpointId();
                Checkpoint cp(id, "AutoSave_" + utils::TimeUtils::formatTimestamp(utils::TimeUtils::now()));
                cp.setData(currentState);
                cp.setStatus(CheckpointStatus::Committed);
                checkpoints[id] = cp;
                latestCheckpointId = id;
                
                if (storage) {
                    storage->save(id, cp.serialize());
                }
            }
        }
    }
};

StateManager::StateManager() : m_impl(std::make_unique<Impl>()) {
    m_impl->storage = std::make_unique<MemoryStorage>();
}

StateManager::StateManager(const std::filesystem::path& storagePath) 
    : m_impl(std::make_unique<Impl>()) {
    m_impl->storage = std::make_unique<FileStorage>(storagePath);
    
    // Mevcut checkpoint'leri yükle
    auto ids = m_impl->storage->listAll();
    for (auto id : ids) {
        auto result = m_impl->storage->load(id);
        if (result.isSuccess()) {
            auto checkpoint = Checkpoint::deserialize(*result.value);
            m_impl->checkpoints[id] = checkpoint;
            if (id > m_impl->latestCheckpointId) {
                m_impl->latestCheckpointId = id;
            }
        }
    }
}

StateManager::~StateManager() {
    if (m_impl->running) {
        m_impl->running = false;
        m_impl->cv.notify_all();
        if (m_impl->autoSaveThread.joinable()) {
            m_impl->autoSaveThread.join();
        }
    }
}

StateManager::StateManager(StateManager&&) noexcept = default;
StateManager& StateManager::operator=(StateManager&&) noexcept = default;

Result<CheckpointId> StateManager::createCheckpoint(const std::string& name, const StateData& state) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    auto id = utils::IdGenerator::generateCheckpointId();
    Checkpoint checkpoint(id, name);
    checkpoint.setData(state);
    checkpoint.setStatus(CheckpointStatus::Committed);
    
    m_impl->checkpoints[id] = checkpoint;
    m_impl->currentState = state;
    m_impl->latestCheckpointId = id;
    
    auto saveResult = m_impl->storage->save(id, checkpoint.serialize());
    if (saveResult.isError()) {
        return Result<CheckpointId>::failure(saveResult.error, saveResult.message);
    }
    
    return Result<CheckpointId>::success(id);
}

Result<Checkpoint> StateManager::getCheckpoint(CheckpointId id) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    auto it = m_impl->checkpoints.find(id);
    if (it == m_impl->checkpoints.end()) {
        return Result<Checkpoint>::failure(ErrorCode::CheckpointNotFound, 
                                          "Checkpoint not found: " + std::to_string(id));
    }
    
    return Result<Checkpoint>::success(it->second);
}

Result<void> StateManager::updateCheckpoint(CheckpointId id, const StateData& state) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    auto it = m_impl->checkpoints.find(id);
    if (it == m_impl->checkpoints.end()) {
        return Result<void>::failure(ErrorCode::CheckpointNotFound);
    }
    
    it->second.setData(state);
    m_impl->storage->save(id, it->second.serialize());
    
    return Result<void>::success();
}

Result<void> StateManager::deleteCheckpoint(CheckpointId id) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    auto it = m_impl->checkpoints.find(id);
    if (it == m_impl->checkpoints.end()) {
        return Result<void>::failure(ErrorCode::CheckpointNotFound);
    }
    
    m_impl->checkpoints.erase(it);
    m_impl->storage->remove(id);
    
    return Result<void>::success();
}

std::vector<CheckpointMetadata> StateManager::listCheckpoints() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    std::vector<CheckpointMetadata> result;
    for (const auto& [id, checkpoint] : m_impl->checkpoints) {
        result.push_back(checkpoint.getMetadata());
    }
    
    return result;
}

void StateManager::setAutoSaveInterval(Duration interval) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->autoSaveInterval = interval;
}

void StateManager::enableAutoSave(bool enable) {
    if (enable && !m_impl->running) {
        m_impl->running = true;
        m_impl->autoSaveEnabled = true;
        m_impl->autoSaveThread = std::thread(&Impl::autoSaveLoop, m_impl.get());
    } else if (!enable && m_impl->running) {
        m_impl->running = false;
        m_impl->cv.notify_all();
        if (m_impl->autoSaveThread.joinable()) {
            m_impl->autoSaveThread.join();
        }
    }
    m_impl->autoSaveEnabled = enable;
}

Result<StateData> StateManager::getCurrentState() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return Result<StateData>::success(m_impl->currentState);
}

Result<CheckpointId> StateManager::getLatestCheckpointId() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (m_impl->latestCheckpointId == 0) {
        return Result<CheckpointId>::failure(ErrorCode::CheckpointNotFound, "No checkpoints exist");
    }
    return Result<CheckpointId>::success(m_impl->latestCheckpointId);
}

Result<void> StateManager::saveToFile(CheckpointId id, const std::filesystem::path& path) {
    auto result = getCheckpoint(id);
    if (result.isError()) {
        return Result<void>::failure(result.error, result.message);
    }
    
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return Result<void>::failure(ErrorCode::IOError, "Cannot open file for writing");
    }
    
    auto data = result.value->serialize();
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    
    return Result<void>::success();
}

Result<Checkpoint> StateManager::loadFromFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return Result<Checkpoint>::failure(ErrorCode::IOError, "Cannot open file for reading");
    }
    
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    StateData data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);
    
    return Result<Checkpoint>::success(Checkpoint::deserialize(data));
}

size_t StateManager::getCheckpointCount() const {
    return m_impl->checkpoints.size();
}

size_t StateManager::getTotalStorageSize() const {
    return m_impl->storage->getTotalSize();
}

} // namespace checkpoint
