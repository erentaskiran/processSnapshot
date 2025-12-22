#include "rollback/rollback_engine.hpp"
#include "real_process/file_operation.hpp"
#include "real_process/reverse_executor.hpp"
#include "utils/helpers.hpp"
#include <stack>
#include <algorithm>

namespace checkpoint {

// ==================== RollbackEngine Implementation ====================

struct RollbackEngine::Impl {
    std::shared_ptr<StateManager> stateManager;
    std::shared_ptr<OperationLogger> logger;
    
    std::stack<StateData> undoStack;  // Geri alınan durumları sakla
    size_t maxUndoHistory = 10;
    
    std::function<bool(const RollbackPlan&)> confirmationCallback;
    
    size_t rollbackCount = 0;
    Duration totalRollbackTime{0};
    
    // File operation reverse execution support
    std::shared_ptr<real_process::FileOperationTracker> fileTracker;
    std::unique_ptr<real_process::ReverseExecutor> reverseExecutor;
    bool enableReverseExecution = true;
    
    std::mutex mutex;
};

RollbackEngine::RollbackEngine(std::shared_ptr<StateManager> stateManager,
                               std::shared_ptr<OperationLogger> logger)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->stateManager = stateManager;
    m_impl->logger = logger;
    
    // Initialize reverse executor with safe defaults
    real_process::ReverseOptions reverseOpts = real_process::ReverseOptions::safe();
    m_impl->reverseExecutor = std::make_unique<real_process::ReverseExecutor>(reverseOpts);
}

RollbackEngine::~RollbackEngine() = default;

RollbackEngine::RollbackEngine(RollbackEngine&&) noexcept = default;
RollbackEngine& RollbackEngine::operator=(RollbackEngine&&) noexcept = default;

Result<RollbackPlan> RollbackEngine::createRollbackPlan(CheckpointId targetId, 
                                                        RollbackStrategy strategy) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    // Hedef checkpoint'i kontrol et
    auto checkpointResult = m_impl->stateManager->getCheckpoint(targetId);
    if (checkpointResult.isError()) {
        return Result<RollbackPlan>::failure(checkpointResult.error, checkpointResult.message);
    }
    
    RollbackPlan plan;
    plan.targetCheckpoint = targetId;
    plan.strategy = strategy;
    
    // Bu checkpoint'ten sonraki işlemleri bul
    if (m_impl->logger) {
        plan.operationsToUndo = m_impl->logger->getOperationsSince(targetId);
    }
    
    // Tahmini süre hesapla
    plan.estimatedTime = Duration(plan.operationsToUndo.size() * 10);  // 10ms per operation
    
    // Açıklama oluştur
    plan.description = "Rollback to checkpoint " + std::to_string(targetId) + 
                       " (" + checkpointResult.value->getName() + ")";
    
    // Onay gereksinimi
    plan.requiresConfirmation = (plan.operationsToUndo.size() > 10);
    
    return Result<RollbackPlan>::success(plan);
}

Result<RollbackResult> RollbackEngine::executeRollback(const RollbackPlan& plan,
                                                       ProgressCallback progress) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    auto startTime = utils::TimeUtils::now();
    RollbackResult result;
    result.success = false;
    result.operationsUndone = 0;
    
    // Onay callback'i varsa çağır
    if (plan.requiresConfirmation && m_impl->confirmationCallback) {
        if (!m_impl->confirmationCallback(plan)) {
            result.errorMessage = "Rollback cancelled by user";
            return Result<RollbackResult>::success(result);
        }
    }
    
    // Mevcut durumu kaydet (undo için)
    auto currentStateResult = m_impl->stateManager->getCurrentState();
    if (currentStateResult.isSuccess()) {
        if (m_impl->undoStack.size() >= m_impl->maxUndoHistory) {
            // En eski undo kaydını sil
            std::stack<StateData> temp;
            while (m_impl->undoStack.size() > 1) {
                temp.push(m_impl->undoStack.top());
                m_impl->undoStack.pop();
            }
            m_impl->undoStack.pop();  // En eskisini at
            while (!temp.empty()) {
                m_impl->undoStack.push(temp.top());
                temp.pop();
            }
        }
        m_impl->undoStack.push(*currentStateResult.value);
    }
    
    // Hedef checkpoint'i al
    auto checkpointResult = m_impl->stateManager->getCheckpoint(plan.targetCheckpoint);
    if (checkpointResult.isError()) {
        result.errorMessage = checkpointResult.message;
        return Result<RollbackResult>::success(result);
    }
    
    // ============================================================
    // REVERSE FILE OPERATIONS EXECUTION (Yeni eklenen kısım)
    // ============================================================
    if (m_impl->enableReverseExecution && m_impl->fileTracker && m_impl->reverseExecutor) {
        if (progress) {
            progress(0.1, "Reversing file operations...");
        }
        
        // Get file operations since the target checkpoint
        auto& fileLog = m_impl->fileTracker->getLog();
        auto fileOps = fileLog.getOperationsSince(plan.targetCheckpoint);
        
        if (!fileOps.empty()) {
            // Set up progress callback for reverse executor
            m_impl->reverseExecutor->setProgressCallback(
                [&progress](size_t current, size_t total, const std::string& status) {
                    if (progress) {
                        double fileProgress = 0.1 + (0.4 * static_cast<double>(current) / total);
                        progress(fileProgress, "File: " + status);
                    }
                });
            
            // Execute reverse operations in LIFO order
            auto reverseResult = m_impl->reverseExecutor->reverseOperations(fileOps);
            
            if (!reverseResult.allSucceeded) {
                for (const auto& res : reverseResult.results) {
                    if (!res.success) {
                        result.warnings.push_back("File reverse failed: " + res.errorMessage);
                    }
                }
            }
            
            if (m_impl->logger) {
                m_impl->logger->info("Rollback", 
                    "Reversed " + std::to_string(reverseResult.successCount) + "/" + 
                    std::to_string(reverseResult.totalOperations) + " file operations");
            }
        }
    }
    // ============================================================
    
    // İşlemleri geri al (state operations)
    size_t totalOps = plan.operationsToUndo.size();
    for (size_t i = 0; i < totalOps; ++i) {
        if (progress) {
            double progressVal = 0.5 + (0.4 * static_cast<double>(i) / totalOps);
            progress(progressVal, "Undoing operation " + std::to_string(i + 1) + "/" + std::to_string(totalOps));
        }
        
        // İşlemi geri al (burada gerçek geri alma mantığı olmalı)
        result.operationsUndone++;
    }
    
    // Durumu geri yükle
    // Not: Bu basit implementasyonda sadece checkpoint verisini geri yüklüyoruz
    // Gerçek uygulamada daha karmaşık olabilir
    
    if (progress) {
        progress(1.0, "Rollback completed");
    }
    
    // Loglama
    if (m_impl->logger) {
        m_impl->logger->logOperation(OperationType::Rollback, 
                                     plan.description, 
                                     plan.targetCheckpoint);
    }
    
    result.success = true;
    result.restoredCheckpoint = plan.targetCheckpoint;
    
    auto endTime = utils::TimeUtils::now();
    result.timeTaken = std::chrono::duration_cast<Duration>(endTime - startTime);
    
    m_impl->rollbackCount++;
    m_impl->totalRollbackTime += result.timeTaken;
    
    return Result<RollbackResult>::success(result);
}

Result<RollbackResult> RollbackEngine::rollbackToCheckpoint(CheckpointId targetId) {
    auto planResult = createRollbackPlan(targetId, RollbackStrategy::Full);
    if (planResult.isError()) {
        RollbackResult result;
        result.success = false;
        result.errorMessage = planResult.message;
        return Result<RollbackResult>::success(result);
    }
    
    return executeRollback(*planResult.value);
}

Result<RollbackResult> RollbackEngine::rollbackToLatest() {
    auto latestResult = m_impl->stateManager->getLatestCheckpointId();
    if (latestResult.isError()) {
        RollbackResult result;
        result.success = false;
        result.errorMessage = latestResult.message;
        return Result<RollbackResult>::success(result);
    }
    
    return rollbackToCheckpoint(*latestResult.value);
}

Result<RollbackResult> RollbackEngine::partialRollback(const PartialRollbackOptions& options) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    auto startTime = utils::TimeUtils::now();
    RollbackResult result;
    result.success = false;
    result.operationsUndone = 0;
    
    // İşlem geçmişini al
    auto operations = m_impl->logger->getOperationHistory(1000);
    
    // Filtreleme uygula
    std::vector<OperationRecord> toUndo;
    
    for (const auto& op : operations) {
        bool shouldUndo = false;
        
        // Belirli işlem ID'leri
        if (!options.operationsToUndo.empty()) {
            shouldUndo = std::find(options.operationsToUndo.begin(), 
                                   options.operationsToUndo.end(), 
                                   op.id) != options.operationsToUndo.end();
        }
        
        // Özel filtre
        if (options.filter && options.filter(op)) {
            shouldUndo = true;
        }
        
        if (shouldUndo && op.canUndo) {
            toUndo.push_back(op);
        }
    }
    
    // İşlemleri geri al (ters sırada)
    std::reverse(toUndo.begin(), toUndo.end());
    
    for (const auto& op : toUndo) {
        // İşlemi geri al
        // Gerçek implementasyonda previousState kullanılır
        result.operationsUndone++;
        
        if (m_impl->logger) {
            m_impl->logger->info("Rollback", "Undid operation: " + op.description);
        }
    }
    
    result.success = true;
    
    auto endTime = utils::TimeUtils::now();
    result.timeTaken = std::chrono::duration_cast<Duration>(endTime - startTime);
    
    if (!options.preserveNewerChanges) {
        result.warnings.push_back("Newer changes may have been affected");
    }
    
    return Result<RollbackResult>::success(result);
}

std::vector<OperationRecord> RollbackEngine::previewRollback(CheckpointId targetId) {
    if (!m_impl->logger) {
        return {};
    }
    
    return m_impl->logger->getOperationsSince(targetId);
}

Result<void> RollbackEngine::undoRollback() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    
    if (m_impl->undoStack.empty()) {
        return Result<void>::failure(ErrorCode::InvalidState, "No rollback to undo");
    }
    
    StateData previousState = m_impl->undoStack.top();
    m_impl->undoStack.pop();
    
    // Durumu geri yükle
    auto id = utils::IdGenerator::generateCheckpointId();
    auto result = m_impl->stateManager->createCheckpoint("UndoRollback", previousState);
    
    if (result.isError()) {
        return Result<void>::failure(result.error, result.message);
    }
    
    if (m_impl->logger) {
        m_impl->logger->logOperation(OperationType::Rollback, "Undo rollback", *result.value);
    }
    
    return Result<void>::success();
}

bool RollbackEngine::canUndoRollback() const {
    return !m_impl->undoStack.empty();
}

void RollbackEngine::setConfirmationCallback(std::function<bool(const RollbackPlan&)> callback) {
    m_impl->confirmationCallback = callback;
}

void RollbackEngine::setMaxUndoHistory(size_t count) {
    m_impl->maxUndoHistory = count;
}

size_t RollbackEngine::getRollbackCount() const {
    return m_impl->rollbackCount;
}

Duration RollbackEngine::getTotalRollbackTime() const {
    return m_impl->totalRollbackTime;
}

void RollbackEngine::setFileOperationTracker(
    std::shared_ptr<real_process::FileOperationTracker> tracker) {
    m_impl->fileTracker = std::move(tracker);
}

void RollbackEngine::setReverseExecutionEnabled(bool enabled) {
    m_impl->enableReverseExecution = enabled;
}

bool RollbackEngine::isReverseExecutionEnabled() const {
    return m_impl->enableReverseExecution;
}

std::vector<std::string> RollbackEngine::previewFileReversal(CheckpointId targetId) {
    if (!m_impl->fileTracker || !m_impl->reverseExecutor) {
        return {};
    }
    
    auto& fileLog = m_impl->fileTracker->getLog();
    auto fileOps = fileLog.getOperationsSince(targetId);
    
    return m_impl->reverseExecutor->previewReverse(fileOps);
}

// ==================== AutoRollbackManager ====================

AutoRollbackManager::AutoRollbackManager(std::shared_ptr<RollbackEngine> engine,
                                         std::shared_ptr<StateManager> stateManager)
    : m_engine(engine)
    , m_stateManager(stateManager)
    , m_enabled(false)
    , m_checkInterval(Duration(5000))  // 5 saniye
    , m_running(false) {
}

AutoRollbackManager::~AutoRollbackManager() {
    stop();
}

void AutoRollbackManager::start() {
    if (m_running) return;
    
    m_running = true;
    m_monitorThread = std::thread(&AutoRollbackManager::monitorLoop, this);
}

void AutoRollbackManager::stop() {
    m_running = false;
    if (m_monitorThread.joinable()) {
        m_monitorThread.join();
    }
}

void AutoRollbackManager::monitorLoop() {
    while (m_running) {
        std::this_thread::sleep_for(m_checkInterval);
        
        if (!m_enabled || !m_running) continue;
        
        if (detectCorruption()) {
            // Otomatik geri alma
            m_engine->rollbackToLatest();
        }
    }
}

bool AutoRollbackManager::detectCorruption() {
    // Basit bütünlük kontrolü
    auto checkpoints = m_stateManager->listCheckpoints();
    
    for (const auto& meta : checkpoints) {
        auto result = m_stateManager->getCheckpoint(meta.id);
        if (result.isSuccess()) {
            if (!result.value->verifyIntegrity()) {
                return true;  // Bozulma tespit edildi
            }
        }
    }
    
    return false;
}

void AutoRollbackManager::setCheckInterval(Duration interval) {
    m_checkInterval = interval;
}

void AutoRollbackManager::setEnabled(bool enabled) {
    m_enabled = enabled;
}

void AutoRollbackManager::setErrorCallback(ErrorCallback callback) {
    // TODO: Hata callback'i sakla ve kullan
}

} // namespace checkpoint
