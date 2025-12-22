#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <optional>
#include <functional>
#include <filesystem>

namespace checkpoint {
namespace real_process {

// ============================================================================
// File Operation Types - Dosya işlem türleri
// ============================================================================
enum class FileOperationType {
    CREATE,         // Yeni dosya oluşturma
    WRITE,          // Dosyaya yazma
    TRUNCATE,       // Dosyayı kesme
    DELETE,         // Dosya silme (unlink)
    RENAME,         // Dosya taşıma/yeniden adlandırma
    CHMOD,          // İzin değişikliği
    CHOWN,          // Sahiplik değişikliği
    MKDIR,          // Dizin oluşturma
    RMDIR,          // Dizin silme
    SYMLINK,        // Sembolik link oluşturma
    APPEND          // Dosya sonuna ekleme
};

std::string fileOpTypeToString(FileOperationType type);
FileOperationType stringToFileOpType(const std::string& str);

// ============================================================================
// File Content Diff - Dosya içerik farkı (delta encoding için)
// ============================================================================
struct FileContentDiff {
    off_t offset;                       // Değişikliğin başlangıç pozisyonu
    std::vector<uint8_t> oldData;       // Önceki veri
    std::vector<uint8_t> newData;       // Yeni veri
    
    bool isEmpty() const { return oldData.empty() && newData.empty(); }
    size_t oldSize() const { return oldData.size(); }
    size_t newSize() const { return newData.size(); }
};

// ============================================================================
// File Operation Record - Tek bir dosya işlemi kaydı
// ============================================================================
struct FileOperation {
    // Unique identifier
    uint64_t operationId;
    
    // Operation info
    FileOperationType type;
    std::chrono::system_clock::time_point timestamp;
    
    // File info
    std::string path;                   // Dosya yolu
    std::string originalPath;           // RENAME için: eski yol
    int fd;                             // İlgili file descriptor (varsa)
    
    // Content tracking
    off_t offset;                       // Yazma pozisyonu (WRITE/TRUNCATE için)
    size_t originalSize;                // İşlem öncesi dosya boyutu
    size_t newSize;                     // İşlem sonrası dosya boyutu
    
    // Full content backup (küçük dosyalar için)
    std::optional<std::vector<uint8_t>> originalContent;
    
    // Delta encoding (büyük dosyalar için)
    std::vector<FileContentDiff> diffs;
    
    // Metadata
    mode_t originalMode;                // Önceki izinler
    mode_t newMode;                     // Yeni izinler (CHMOD için)
    uid_t originalUid;
    gid_t originalGid;
    uid_t newUid;
    gid_t newGid;
    
    // Status
    bool isReversible;                  // Geri alınabilir mi?
    bool wasReversed;                   // Geri alındı mı?
    std::string description;            // İsteğe bağlı açıklama
    
    // Process info
    pid_t pid;                          // İşlemi yapan process
    
    // Constructor
    FileOperation() 
        : operationId(0), type(FileOperationType::WRITE),
          fd(-1), offset(0), originalSize(0), newSize(0),
          originalMode(0), newMode(0),
          originalUid(0), originalGid(0),
          newUid(0), newGid(0),
          isReversible(true), wasReversed(false), pid(0) {}
    
    // Serialization
    std::vector<uint8_t> serialize() const;
    static FileOperation deserialize(const std::vector<uint8_t>& data);
    
    // Utility
    bool hasFullBackup() const { return originalContent.has_value(); }
    bool hasDiffs() const { return !diffs.empty(); }
    size_t estimatedMemoryUsage() const;
};

// ============================================================================
// File Operation Log - Dosya işlemleri günlüğü
// ============================================================================
class FileOperationLog {
public:
    FileOperationLog();
    ~FileOperationLog();
    
    // Move semantics
    FileOperationLog(FileOperationLog&&) noexcept;
    FileOperationLog& operator=(FileOperationLog&&) noexcept;
    
    // Disable copy
    FileOperationLog(const FileOperationLog&) = delete;
    FileOperationLog& operator=(const FileOperationLog&) = delete;
    
    // Operation tracking
    uint64_t recordOperation(const FileOperation& op);
    void recordOperations(const std::vector<FileOperation>& ops);
    
    // Querying
    std::vector<FileOperation> getOperationsSince(uint64_t checkpointId) const;
    std::vector<FileOperation> getOperationsForFile(const std::string& path) const;
    std::vector<FileOperation> getOperationsByType(FileOperationType type) const;
    std::vector<FileOperation> getAllOperations() const;
    std::optional<FileOperation> getOperation(uint64_t operationId) const;
    
    // Filtering
    std::vector<FileOperation> filterOperations(
        std::function<bool(const FileOperation&)> predicate) const;
    
    // Checkpoint integration
    void markCheckpoint(uint64_t checkpointId);
    void clearOperationsBeforeCheckpoint(uint64_t checkpointId);
    
    // State management
    size_t getOperationCount() const;
    void clear();
    
    // Serialization
    std::vector<uint8_t> serialize() const;
    static FileOperationLog deserialize(const std::vector<uint8_t>& data);
    
    // Persistence
    bool saveToFile(const std::filesystem::path& path) const;
    bool loadFromFile(const std::filesystem::path& path);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// ============================================================================
// File Operation Tracker - Dosya işlemlerini izleyen sınıf
// ============================================================================
struct FileTrackingOptions {
    std::vector<std::string> includePaths;      // İzlenecek dizinler
    std::vector<std::string> excludePaths;      // Hariç tutulacak dizinler
    std::vector<std::string> includeExtensions; // İzlenecek uzantılar
    std::vector<std::string> excludeExtensions; // Hariç tutulacak uzantılar
    
    size_t maxFileSize;                         // Tam yedekleme için max boyut
    bool useDeltas;                             // Delta encoding kullan
    bool trackMetadata;                         // İzin/sahiplik değişikliklerini izle
    
    FileTrackingOptions()
        : maxFileSize(10 * 1024 * 1024),  // 10 MB default
          useDeltas(true),
          trackMetadata(true) {}
};

class FileOperationTracker {
public:
    FileOperationTracker();
    explicit FileOperationTracker(const FileTrackingOptions& options);
    ~FileOperationTracker();
    
    // Tracking control
    void startTracking(pid_t pid);
    void stopTracking();
    bool isTracking() const;
    
    // Manual operation recording (interceptor'dan çağrılır)
    void beforeWrite(int fd, const std::string& path, off_t offset, size_t size);
    void afterWrite(int fd, const std::string& path, off_t offset, size_t size, bool success);
    
    void beforeTruncate(const std::string& path, off_t length);
    void afterTruncate(const std::string& path, off_t length, bool success);
    
    void beforeUnlink(const std::string& path);
    void afterUnlink(const std::string& path, bool success);
    
    void beforeRename(const std::string& oldPath, const std::string& newPath);
    void afterRename(const std::string& oldPath, const std::string& newPath, bool success);
    
    void beforeCreate(const std::string& path, mode_t mode);
    void afterCreate(const std::string& path, int fd, bool success);
    
    // File content capture
    std::vector<uint8_t> captureFileContent(const std::string& path);
    std::vector<uint8_t> captureFileContent(const std::string& path, off_t offset, size_t size);
    
    // Get recorded operations
    FileOperationLog& getLog();
    const FileOperationLog& getLog() const;
    
    // Configuration
    void setOptions(const FileTrackingOptions& options);
    const FileTrackingOptions& getOptions() const;
    
    // Checkpoint integration
    void onCheckpointCreated(uint64_t checkpointId);
    
    // Statistics
    size_t getTrackedOperationCount() const;
    size_t getTotalBackupSize() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    
    bool shouldTrackFile(const std::string& path) const;
    uint64_t generateOperationId();
};

} // namespace real_process
} // namespace checkpoint
