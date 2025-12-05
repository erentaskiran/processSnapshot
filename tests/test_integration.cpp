#include <gtest/gtest.h>
#include "core/types.hpp"
#include "state/state_manager.hpp"
#include "state/storage.hpp"
#include "logger/operation_logger.hpp"
#include "rollback/rollback_engine.hpp"
#include "utils/helpers.hpp"
#include <filesystem>
#include <thread>
#include <random>

using namespace checkpoint;

class IntegrationTest : public ::testing::Test {
protected:
    std::filesystem::path testDir = "test_integration";
    
    void SetUp() override {
        std::filesystem::create_directories(testDir);
    }
    
    void TearDown() override {
        std::filesystem::remove_all(testDir);
    }
    
    StateData createTestData(const std::string& content) {
        return StateData(content.begin(), content.end());
    }
};

// End-to-End Tests
TEST_F(IntegrationTest, FullWorkflow) {
    // 1. Bileşenleri oluştur
    auto stateManager = std::make_shared<StateManager>(testDir / "checkpoints");
    auto logger = std::make_shared<OperationLogger>();
    auto rollbackEngine = std::make_shared<RollbackEngine>(stateManager, logger);
    
    // 2. İlk durumu kaydet
    auto state1 = createTestData("Initial application state");
    auto cp1 = stateManager->createCheckpoint("Initial State", state1);
    ASSERT_TRUE(cp1.isSuccess());
    logger->logOperation(OperationType::Checkpoint, "Created initial checkpoint", *cp1.value);
    
    // 3. Durumu değiştir
    auto state2 = createTestData("Modified application state");
    stateManager->updateCheckpoint(*cp1.value, state2);
    logger->logOperation(OperationType::Update, "Modified state", *cp1.value);
    
    // 4. Yeni checkpoint oluştur
    auto cp2 = stateManager->createCheckpoint("After Modification", state2);
    ASSERT_TRUE(cp2.isSuccess());
    logger->logOperation(OperationType::Checkpoint, "Created second checkpoint", *cp2.value);
    
    // 5. Daha fazla değişiklik
    auto state3 = createTestData("Further modifications");
    auto cp3 = stateManager->createCheckpoint("Final State", state3);
    ASSERT_TRUE(cp3.isSuccess());
    
    // 6. Checkpoint'leri listele
    auto checkpoints = stateManager->listCheckpoints();
    EXPECT_EQ(checkpoints.size(), 3);
    
    // 7. Geri alma planı oluştur
    auto planResult = rollbackEngine->createRollbackPlan(*cp1.value);
    ASSERT_TRUE(planResult.isSuccess());
    
    // 8. Geri alma işlemi
    auto rollbackResult = rollbackEngine->executeRollback(*planResult.value);
    ASSERT_TRUE(rollbackResult.isSuccess());
    EXPECT_TRUE(rollbackResult.value->success);
    
    // 9. Log kayıtlarını kontrol et
    auto entries = logger->getEntries(LogLevel::Info, 100);
    EXPECT_GE(entries.size(), 3);
    
    // 10. İstatistikleri kontrol et
    EXPECT_EQ(stateManager->getCheckpointCount(), 3);
    EXPECT_EQ(rollbackEngine->getRollbackCount(), 1);
}

TEST_F(IntegrationTest, ErrorRecoveryScenario) {
    auto stateManager = std::make_shared<StateManager>(testDir / "recovery");
    auto logger = std::make_shared<OperationLogger>();
    auto rollbackEngine = std::make_shared<RollbackEngine>(stateManager, logger);
    
    // Stabil durum
    auto stableState = createTestData("Stable working state");
    auto stableCp = stateManager->createCheckpoint("Stable", stableState);
    ASSERT_TRUE(stableCp.isSuccess());
    
    // Hatalı işlem simülasyonu
    auto badState = createTestData("Corrupted or bad state");
    stateManager->createCheckpoint("Bad State", badState);
    logger->logOperation(OperationType::Update, "Bad operation performed");
    
    // Hata tespit edildi - geri al
    auto rollbackResult = rollbackEngine->rollbackToCheckpoint(*stableCp.value);
    ASSERT_TRUE(rollbackResult.isSuccess());
    EXPECT_TRUE(rollbackResult.value->success);
    
    // Stabil duruma döndük
    auto currentState = stateManager->getCurrentState();
    // Durum kontrol edilebilir
}

TEST_F(IntegrationTest, IncrementalCheckpoints) {
    auto stateManager = std::make_shared<StateManager>(testDir / "incremental");
    
    // Artımlı checkpoint'ler oluştur
    std::vector<CheckpointId> checkpointIds;
    
    for (int i = 1; i <= 10; ++i) {
        std::string data(i * 100, 'X');  // Artan boyut
        auto cpResult = stateManager->createCheckpoint(
            "Checkpoint " + std::to_string(i),
            createTestData(data)
        );
        ASSERT_TRUE(cpResult.isSuccess());
        checkpointIds.push_back(*cpResult.value);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Tüm checkpoint'ler mevcut
    EXPECT_EQ(stateManager->getCheckpointCount(), 10);
    
    // Her checkpoint'e erişilebilir
    for (auto id : checkpointIds) {
        auto result = stateManager->getCheckpoint(id);
        EXPECT_TRUE(result.isSuccess());
    }
    
    // En son checkpoint doğru
    auto latestResult = stateManager->getLatestCheckpointId();
    ASSERT_TRUE(latestResult.isSuccess());
    EXPECT_EQ(*latestResult.value, checkpointIds.back());
}

TEST_F(IntegrationTest, ConcurrentAccess) {
    auto stateManager = std::make_shared<StateManager>(testDir / "concurrent");
    
    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};
    const int numThreads = 4;
    const int opsPerThread = 25;
    
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&stateManager, &successCount, t, opsPerThread]() {
            for (int i = 0; i < opsPerThread; ++i) {
                std::string name = "Thread" + std::to_string(t) + "_CP" + std::to_string(i);
                std::string data = "Data from thread " + std::to_string(t);
                
                auto result = stateManager->createCheckpoint(
                    name,
                    StateData(data.begin(), data.end())
                );
                
                if (result.isSuccess()) {
                    successCount++;
                }
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    EXPECT_EQ(successCount, numThreads * opsPerThread);
    EXPECT_EQ(stateManager->getCheckpointCount(), numThreads * opsPerThread);
}

TEST_F(IntegrationTest, PersistenceAndRecovery) {
    std::vector<CheckpointId> savedIds;
    
    // İlk session - checkpoint'ler oluştur
    {
        auto stateManager = std::make_shared<StateManager>(testDir / "persistence");
        
        for (int i = 0; i < 5; ++i) {
            auto result = stateManager->createCheckpoint(
                "Persistent CP " + std::to_string(i),
                createTestData("Persistent data " + std::to_string(i))
            );
            ASSERT_TRUE(result.isSuccess());
            savedIds.push_back(*result.value);
        }
    }
    
    // İkinci session - checkpoint'leri geri yükle
    {
        auto stateManager = std::make_shared<StateManager>(testDir / "persistence");
        
        // Tüm checkpoint'ler yüklenmeli
        EXPECT_EQ(stateManager->getCheckpointCount(), 5);
        
        // Her biri erişilebilir olmalı
        for (auto id : savedIds) {
            auto result = stateManager->getCheckpoint(id);
            ASSERT_TRUE(result.isSuccess());
        }
    }
}

TEST_F(IntegrationTest, LargeDataHandling) {
    auto stateManager = std::make_shared<StateManager>(testDir / "large");
    
    // 5MB veri
    std::string largeData(5 * 1024 * 1024, 'X');
    
    auto startTime = std::chrono::steady_clock::now();
    
    auto result = stateManager->createCheckpoint("Large Data", 
                                                  createTestData(largeData));
    
    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    ASSERT_TRUE(result.isSuccess());
    
    // Geri okuma
    auto getResult = stateManager->getCheckpoint(*result.value);
    ASSERT_TRUE(getResult.isSuccess());
    EXPECT_EQ(getResult.value->getData().size(), largeData.size());
    
    // Integrity kontrolü
    EXPECT_TRUE(getResult.value->verifyIntegrity());
    
    std::cout << "Large data checkpoint time: " << duration.count() << "ms\n";
}

TEST_F(IntegrationTest, PartialRollbackScenario) {
    auto stateManager = std::make_shared<StateManager>(testDir / "partial");
    auto logger = std::make_shared<OperationLogger>();
    auto rollbackEngine = std::make_shared<RollbackEngine>(stateManager, logger);
    
    // Base checkpoint
    auto baseCp = stateManager->createCheckpoint("Base", createTestData("base"));
    ASSERT_TRUE(baseCp.isSuccess());
    
    // Bir dizi işlem
    std::vector<OperationId> opIds;
    opIds.push_back(logger->logOperation(OperationType::Create, "Created item A", *baseCp.value));
    opIds.push_back(logger->logOperation(OperationType::Update, "Updated item B", *baseCp.value));
    opIds.push_back(logger->logOperation(OperationType::Delete, "Deleted item C", *baseCp.value));
    opIds.push_back(logger->logOperation(OperationType::Update, "Updated item A", *baseCp.value));
    
    // Sadece belirli işlemleri geri al
    PartialRollbackOptions options;
    options.operationsToUndo = {opIds[1], opIds[3]};  // Sadece Update işlemlerini geri al
    options.preserveNewerChanges = true;
    
    auto result = rollbackEngine->partialRollback(options);
    ASSERT_TRUE(result.isSuccess());
    EXPECT_TRUE(result.value->success);
    EXPECT_EQ(result.value->operationsUndone, 2);
}

TEST_F(IntegrationTest, RollbackChainScenario) {
    auto stateManager = std::make_shared<StateManager>(testDir / "chain");
    auto logger = std::make_shared<OperationLogger>();
    auto rollbackEngine = std::make_shared<RollbackEngine>(stateManager, logger);
    
    // Checkpoint zinciri oluştur
    auto cp1 = stateManager->createCheckpoint("State 1", createTestData("1"));
    ASSERT_TRUE(cp1.isSuccess());
    
    auto cp2 = stateManager->createCheckpoint("State 2", createTestData("2"));
    ASSERT_TRUE(cp2.isSuccess());
    
    auto cp3 = stateManager->createCheckpoint("State 3", createTestData("3"));
    ASSERT_TRUE(cp3.isSuccess());
    
    // En başa geri al
    auto rb1 = rollbackEngine->rollbackToCheckpoint(*cp1.value);
    ASSERT_TRUE(rb1.isSuccess());
    EXPECT_TRUE(rb1.value->success);
    
    // Geri almayı geri al
    EXPECT_TRUE(rollbackEngine->canUndoRollback());
    auto undoResult = rollbackEngine->undoRollback();
    EXPECT_TRUE(undoResult.isSuccess());
    
    // Ortadaki checkpoint'e git
    auto rb2 = rollbackEngine->rollbackToCheckpoint(*cp2.value);
    ASSERT_TRUE(rb2.isSuccess());
    EXPECT_TRUE(rb2.value->success);
    
    // İstatistikleri kontrol et
    EXPECT_EQ(rollbackEngine->getRollbackCount(), 2);
}

TEST_F(IntegrationTest, StorageTypeComparison) {
    // Memory storage
    auto memStart = std::chrono::steady_clock::now();
    {
        StateManager memManager;  // Default memory storage
        for (int i = 0; i < 100; ++i) {
            memManager.createCheckpoint("Mem" + std::to_string(i), 
                                        createTestData(std::string(1000, 'X')));
        }
    }
    auto memEnd = std::chrono::steady_clock::now();
    auto memDuration = std::chrono::duration_cast<std::chrono::milliseconds>(memEnd - memStart);
    
    // File storage
    auto fileStart = std::chrono::steady_clock::now();
    {
        StateManager fileManager(testDir / "storage_comparison");
        for (int i = 0; i < 100; ++i) {
            fileManager.createCheckpoint("File" + std::to_string(i), 
                                         createTestData(std::string(1000, 'X')));
        }
    }
    auto fileEnd = std::chrono::steady_clock::now();
    auto fileDuration = std::chrono::duration_cast<std::chrono::milliseconds>(fileEnd - fileStart);
    
    std::cout << "Memory storage: " << memDuration.count() << "ms\n";
    std::cout << "File storage: " << fileDuration.count() << "ms\n";
    
    // Memory genellikle daha hızlı olmalı
    // (Disk I/O'ya bağlı olarak her zaman doğru olmayabilir)
}

TEST_F(IntegrationTest, CompleteSystemTest) {
    // Tam sistem testi
    auto stateManager = std::make_shared<StateManager>(testDir / "complete");
    auto logger = std::make_shared<OperationLogger>();
    logger->setMinLevel(LogLevel::Debug);
    logger->addOutput(std::make_shared<FileLogOutput>(testDir / "complete" / "system.log"));
    
    auto rollbackEngine = std::make_shared<RollbackEngine>(stateManager, logger);
    
    LOG_INFO("Test", "Starting complete system test");
    
    // Simüle edilmiş uygulama yaşam döngüsü
    struct AppState {
        int version = 0;
        std::string data;
    } state;
    
    auto serializeState = [](const AppState& s) {
        std::string str = std::to_string(s.version) + ":" + s.data;
        return StateData(str.begin(), str.end());
    };
    
    // V1
    state.version = 1;
    state.data = "Initial data";
    auto v1Cp = stateManager->createCheckpoint("V1", serializeState(state));
    ASSERT_TRUE(v1Cp.isSuccess());
    logger->logOperation(OperationType::Checkpoint, "Created V1", *v1Cp.value);
    
    // V2
    state.version = 2;
    state.data = "Updated data with new features";
    auto v2Cp = stateManager->createCheckpoint("V2", serializeState(state));
    ASSERT_TRUE(v2Cp.isSuccess());
    logger->logOperation(OperationType::Checkpoint, "Created V2", *v2Cp.value);
    
    // V3 - Hatalı versiyon
    state.version = 3;
    state.data = "Buggy data that causes issues";
    auto v3Cp = stateManager->createCheckpoint("V3-Buggy", serializeState(state));
    ASSERT_TRUE(v3Cp.isSuccess());
    logger->logOperation(OperationType::Checkpoint, "Created V3 (buggy)", *v3Cp.value);
    LOG_ERROR("Test", "Bug detected in V3!");
    
    // V2'ye geri dön
    LOG_INFO("Test", "Rolling back to V2");
    auto rollbackResult = rollbackEngine->rollbackToCheckpoint(*v2Cp.value);
    ASSERT_TRUE(rollbackResult.isSuccess());
    EXPECT_TRUE(rollbackResult.value->success);
    
    // V4 - Düzeltilmiş versiyon
    state.version = 4;
    state.data = "Fixed data after rollback";
    auto v4Cp = stateManager->createCheckpoint("V4-Fixed", serializeState(state));
    ASSERT_TRUE(v4Cp.isSuccess());
    logger->logOperation(OperationType::Checkpoint, "Created V4 (fixed)", *v4Cp.value);
    
    LOG_INFO("Test", "Complete system test finished");
    logger->flush();
    
    // Final assertions
    EXPECT_EQ(stateManager->getCheckpointCount(), 4);
    EXPECT_EQ(rollbackEngine->getRollbackCount(), 1);
    
    // Log dosyası oluşturulmuş olmalı
    EXPECT_TRUE(std::filesystem::exists(testDir / "complete" / "system.log"));
}
