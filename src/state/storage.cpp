#include "state/storage.hpp"
#include "core/exceptions.hpp"
#include <fstream>
#include <algorithm>

namespace checkpoint {

// ==================== FileStorage ====================

FileStorage::FileStorage(const std::filesystem::path& basePath, const std::string& extension)
    : m_basePath(basePath), m_extension(extension) {
    std::filesystem::create_directories(basePath);
}

std::filesystem::path FileStorage::getFilePath(CheckpointId id) const {
    return m_basePath / (std::to_string(id) + m_extension);
}

Result<void> FileStorage::save(CheckpointId id, const StateData& data) {
    try {
        std::ofstream file(getFilePath(id), std::ios::binary);
        if (!file) {
            return Result<void>::failure(ErrorCode::IOError, "Cannot open file for writing");
        }
        
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
        
        if (!file.good()) {
            return Result<void>::failure(ErrorCode::IOError, "Write failed");
        }
        
        return Result<void>::success();
    } catch (const std::exception& e) {
        return Result<void>::failure(ErrorCode::IOError, e.what());
    }
}

Result<StateData> FileStorage::load(CheckpointId id) {
    try {
        auto path = getFilePath(id);
        if (!std::filesystem::exists(path)) {
            return Result<StateData>::failure(ErrorCode::CheckpointNotFound);
        }
        
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return Result<StateData>::failure(ErrorCode::IOError, "Cannot open file for reading");
        }
        
        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        StateData data(size);
        file.read(reinterpret_cast<char*>(data.data()), size);
        
        return Result<StateData>::success(std::move(data));
    } catch (const std::exception& e) {
        return Result<StateData>::failure(ErrorCode::IOError, e.what());
    }
}

Result<void> FileStorage::remove(CheckpointId id) {
    try {
        auto path = getFilePath(id);
        if (std::filesystem::exists(path)) {
            std::filesystem::remove(path);
        }
        return Result<void>::success();
    } catch (const std::exception& e) {
        return Result<void>::failure(ErrorCode::IOError, e.what());
    }
}

bool FileStorage::exists(CheckpointId id) {
    return std::filesystem::exists(getFilePath(id));
}

std::vector<CheckpointId> FileStorage::listAll() {
    std::vector<CheckpointId> ids;
    
    for (const auto& entry : std::filesystem::directory_iterator(m_basePath)) {
        if (entry.is_regular_file() && entry.path().extension() == m_extension) {
            try {
                CheckpointId id = std::stoull(entry.path().stem().string());
                ids.push_back(id);
            } catch (...) {
                // Geçersiz dosya adı, atla
            }
        }
    }
    
    std::sort(ids.begin(), ids.end());
    return ids;
}

size_t FileStorage::getSize(CheckpointId id) {
    auto path = getFilePath(id);
    if (std::filesystem::exists(path)) {
        return std::filesystem::file_size(path);
    }
    return 0;
}

size_t FileStorage::getTotalSize() {
    size_t total = 0;
    for (const auto& entry : std::filesystem::directory_iterator(m_basePath)) {
        if (entry.is_regular_file()) {
            total += entry.file_size();
        }
    }
    return total;
}

void FileStorage::setBasePath(const std::filesystem::path& path) {
    m_basePath = path;
    std::filesystem::create_directories(path);
}

Result<void> FileStorage::cleanup(Duration olderThan) {
    auto now = std::chrono::system_clock::now();
    
    for (const auto& entry : std::filesystem::directory_iterator(m_basePath)) {
        if (entry.is_regular_file()) {
            auto lastWrite = std::filesystem::last_write_time(entry);
            auto fileTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                lastWrite - std::filesystem::file_time_type::clock::now() + now);
            
            if (now - fileTime > olderThan) {
                std::filesystem::remove(entry);
            }
        }
    }
    
    return Result<void>::success();
}

// ==================== MemoryStorage ====================

MemoryStorage::MemoryStorage(size_t maxSize) : m_maxSize(maxSize) {}

Result<void> MemoryStorage::save(CheckpointId id, const StateData& data) {
    size_t currentTotal = getTotalSize();
    
    if (currentTotal + data.size() > m_maxSize) {
        return Result<void>::failure(ErrorCode::OutOfMemory, "Memory storage limit exceeded");
    }
    
    m_storage[id] = data;
    return Result<void>::success();
}

Result<StateData> MemoryStorage::load(CheckpointId id) {
    auto it = m_storage.find(id);
    if (it == m_storage.end()) {
        return Result<StateData>::failure(ErrorCode::CheckpointNotFound);
    }
    return Result<StateData>::success(it->second);
}

Result<void> MemoryStorage::remove(CheckpointId id) {
    m_storage.erase(id);
    return Result<void>::success();
}

bool MemoryStorage::exists(CheckpointId id) {
    return m_storage.find(id) != m_storage.end();
}

std::vector<CheckpointId> MemoryStorage::listAll() {
    std::vector<CheckpointId> ids;
    for (const auto& [id, _] : m_storage) {
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

size_t MemoryStorage::getSize(CheckpointId id) {
    auto it = m_storage.find(id);
    if (it != m_storage.end()) {
        return it->second.size();
    }
    return 0;
}

size_t MemoryStorage::getTotalSize() {
    size_t total = 0;
    for (const auto& [_, data] : m_storage) {
        total += data.size();
    }
    return total;
}

void MemoryStorage::clear() {
    m_storage.clear();
}

// ==================== HybridStorage ====================

HybridStorage::HybridStorage(const std::filesystem::path& basePath, size_t memoryThreshold)
    : m_memoryStorage(std::make_unique<MemoryStorage>(memoryThreshold))
    , m_fileStorage(std::make_unique<FileStorage>(basePath))
    , m_memoryThreshold(memoryThreshold) {
}

Result<void> HybridStorage::save(CheckpointId id, const StateData& data) {
    // Küçük veriler bellekte, büyük veriler dosyada
    if (data.size() < m_memoryThreshold / 10) {
        return m_memoryStorage->save(id, data);
    } else {
        return m_fileStorage->save(id, data);
    }
}

Result<StateData> HybridStorage::load(CheckpointId id) {
    // Önce bellekte ara
    if (m_memoryStorage->exists(id)) {
        return m_memoryStorage->load(id);
    }
    // Sonra dosyada ara
    return m_fileStorage->load(id);
}

Result<void> HybridStorage::remove(CheckpointId id) {
    if (m_memoryStorage->exists(id)) {
        m_memoryStorage->remove(id);
    }
    if (m_fileStorage->exists(id)) {
        m_fileStorage->remove(id);
    }
    return Result<void>::success();
}

bool HybridStorage::exists(CheckpointId id) {
    return m_memoryStorage->exists(id) || m_fileStorage->exists(id);
}

std::vector<CheckpointId> HybridStorage::listAll() {
    auto memIds = m_memoryStorage->listAll();
    auto fileIds = m_fileStorage->listAll();
    
    std::vector<CheckpointId> result;
    std::set_union(memIds.begin(), memIds.end(),
                   fileIds.begin(), fileIds.end(),
                   std::back_inserter(result));
    
    return result;
}

size_t HybridStorage::getSize(CheckpointId id) {
    if (m_memoryStorage->exists(id)) {
        return m_memoryStorage->getSize(id);
    }
    return m_fileStorage->getSize(id);
}

size_t HybridStorage::getTotalSize() {
    return m_memoryStorage->getTotalSize() + m_fileStorage->getTotalSize();
}

void HybridStorage::flushToFile() {
    auto ids = m_memoryStorage->listAll();
    for (auto id : ids) {
        auto data = m_memoryStorage->load(id);
        if (data.isSuccess()) {
            m_fileStorage->save(id, *data.value);
            m_memoryStorage->remove(id);
        }
    }
}

void HybridStorage::loadToMemory(CheckpointId id) {
    if (m_fileStorage->exists(id) && !m_memoryStorage->exists(id)) {
        auto data = m_fileStorage->load(id);
        if (data.isSuccess()) {
            m_memoryStorage->save(id, *data.value);
        }
    }
}

} // namespace checkpoint
