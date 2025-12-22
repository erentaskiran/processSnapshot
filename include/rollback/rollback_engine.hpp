#pragma once

#include "core/types.hpp"
#include "state/state_manager.hpp"
#include "logger/operation_logger.hpp"
#include <functional>
#include <stack>

// Forward declarations for file operation support
namespace checkpoint {
namespace real_process {
    class FileOperationTracker;
    class ReverseExecutor;
}
}

namespace checkpoint {

// Geri alma planı
struct RollbackPlan {
    CheckpointId targetCheckpoint;
    RollbackStrategy strategy;
    std::vector<OperationRecord> operationsToUndo;
    bool requiresConfirmation;
    Duration estimatedTime;
    std::string description;
    
    size_t getOperationCount() const { return operationsToUndo.size(); }
};

// Geri alma sonucu
struct RollbackResult {
    bool success;
    CheckpointId restoredCheckpoint;
    size_t operationsUndone;
    Duration timeTaken;
    std::vector<std::string> warnings;
    std::string errorMessage;
};

// Kısmi geri alma seçenekleri
struct PartialRollbackOptions {
    std::vector<OperationId> operationsToUndo;  // Belirli işlemleri geri al
    std::vector<std::string> categoriesToUndo;  // Belirli kategorileri geri al
    bool preserveNewerChanges;                   // Yeni değişiklikleri koru
    std::function<bool(const OperationRecord&)> filter;  // Özel filtre
};

// Geri alma motoru arayüzü
class IRollbackEngine {
public:
    virtual ~IRollbackEngine() = default;
    
    // Geri alma işlemleri
    virtual Result<RollbackPlan> createRollbackPlan(CheckpointId targetId, 
                                                    RollbackStrategy strategy = RollbackStrategy::Full) = 0;
    virtual Result<RollbackResult> executeRollback(const RollbackPlan& plan, 
                                                   ProgressCallback progress = nullptr) = 0;
    virtual Result<RollbackResult> rollbackToCheckpoint(CheckpointId targetId) = 0;
    virtual Result<RollbackResult> rollbackToLatest() = 0;
    
    // Kısmi geri alma
    virtual Result<RollbackResult> partialRollback(const PartialRollbackOptions& options) = 0;
    
    // Önizleme
    virtual std::vector<OperationRecord> previewRollback(CheckpointId targetId) = 0;
    
    // Geri alma işlemini geri al (redo)
    virtual Result<void> undoRollback() = 0;
    virtual bool canUndoRollback() const = 0;
};

// Geri alma motoru implementasyonu
class RollbackEngine : public IRollbackEngine {
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    
public:
    RollbackEngine(std::shared_ptr<StateManager> stateManager,
                   std::shared_ptr<OperationLogger> logger);
    ~RollbackEngine();
    
    // Move semantics
    RollbackEngine(RollbackEngine&&) noexcept;
    RollbackEngine& operator=(RollbackEngine&&) noexcept;
    
    // IRollbackEngine implementasyonu
    Result<RollbackPlan> createRollbackPlan(CheckpointId targetId, 
                                           RollbackStrategy strategy = RollbackStrategy::Full) override;
    Result<RollbackResult> executeRollback(const RollbackPlan& plan, 
                                          ProgressCallback progress = nullptr) override;
    Result<RollbackResult> rollbackToCheckpoint(CheckpointId targetId) override;
    Result<RollbackResult> rollbackToLatest() override;
    
    Result<RollbackResult> partialRollback(const PartialRollbackOptions& options) override;
    
    std::vector<OperationRecord> previewRollback(CheckpointId targetId) override;
    
    Result<void> undoRollback() override;
    bool canUndoRollback() const override;
    
    // Ek özellikler
    void setConfirmationCallback(std::function<bool(const RollbackPlan&)> callback);
    void setMaxUndoHistory(size_t count);
    
    // File operation reverse execution support
    void setFileOperationTracker(std::shared_ptr<real_process::FileOperationTracker> tracker);
    void setReverseExecutionEnabled(bool enabled);
    bool isReverseExecutionEnabled() const;
    
    // Preview file operations that will be reversed
    std::vector<std::string> previewFileReversal(CheckpointId targetId);
    
    // İstatistikler
    size_t getRollbackCount() const;
    Duration getTotalRollbackTime() const;
};

// Otomatik geri alma yöneticisi
class AutoRollbackManager {
private:
    std::shared_ptr<RollbackEngine> m_engine;
    std::shared_ptr<StateManager> m_stateManager;
    bool m_enabled;
    Duration m_checkInterval;
    std::thread m_monitorThread;
    std::atomic<bool> m_running;
    
    void monitorLoop();
    bool detectCorruption();
    
public:
    AutoRollbackManager(std::shared_ptr<RollbackEngine> engine,
                       std::shared_ptr<StateManager> stateManager);
    ~AutoRollbackManager();
    
    void start();
    void stop();
    
    void setCheckInterval(Duration interval);
    void setEnabled(bool enabled);
    
    // Hata callback'i
    void setErrorCallback(ErrorCallback callback);
};

} // namespace checkpoint
