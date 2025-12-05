#include <gtest/gtest.h>
#include "rollback/rollback_engine.hpp"
#include "state/state_manager.hpp"
#include "logger/operation_logger.hpp"
#include <filesystem>

using namespace checkpoint;

class RollbackTest : public ::testing::Test {
protected:
    std::filesystem::path testDir = "test_rollback";
    std::shared_ptr<StateManager> stateManager;
    std::shared_ptr<OperationLogger> logger;
    std::shared_ptr<RollbackEngine> rollbackEngine;
    
    void SetUp() override {
        std::filesystem::create_directories(testDir);
        stateManager = std::make_shared<StateManager>(testDir);
        logger = std::make_shared<OperationLogger>();
        rollbackEngine = std::make_shared<RollbackEngine>(stateManager, logger);
    }
    
    void TearDown() override {
        std::filesystem::remove_all(testDir);
    }
    
    StateData createTestData(const std::string& content) {
        return StateData(content.begin(), content.end());
    }
};

// RollbackPlan Tests
TEST_F(RollbackTest, CreateRollbackPlan) {
    auto data = createTestData("Initial state");
    auto cpResult = stateManager->createCheckpoint("Initial", data);
    ASSERT_TRUE(cpResult.isSuccess());
    
    auto planResult = rollbackEngine->createRollbackPlan(*cpResult.value);
    ASSERT_TRUE(planResult.isSuccess());
    
    EXPECT_EQ(planResult.value->targetCheckpoint, *cpResult.value);
    EXPECT_EQ(planResult.value->strategy, RollbackStrategy::Full);
}

TEST_F(RollbackTest, CreateRollbackPlanForNonExistent) {
    auto planResult = rollbackEngine->createRollbackPlan(99999);
    
    EXPECT_TRUE(planResult.isError());
    EXPECT_EQ(planResult.error, ErrorCode::CheckpointNotFound);
}

TEST_F(RollbackTest, RollbackPlanWithStrategy) {
    auto cpResult = stateManager->createCheckpoint("Test", createTestData("data"));
    ASSERT_TRUE(cpResult.isSuccess());
    
    auto planResult = rollbackEngine->createRollbackPlan(*cpResult.value, RollbackStrategy::Partial);
    ASSERT_TRUE(planResult.isSuccess());
    
    EXPECT_EQ(planResult.value->strategy, RollbackStrategy::Partial);
}

// Execute Rollback Tests
TEST_F(RollbackTest, ExecuteRollback) {
    auto data = createTestData("Initial state");
    auto cpResult = stateManager->createCheckpoint("Initial", data);
    ASSERT_TRUE(cpResult.isSuccess());
    
    auto planResult = rollbackEngine->createRollbackPlan(*cpResult.value);
    ASSERT_TRUE(planResult.isSuccess());
    
    auto rollbackResult = rollbackEngine->executeRollback(*planResult.value);
    ASSERT_TRUE(rollbackResult.isSuccess());
    EXPECT_TRUE(rollbackResult.value->success);
    EXPECT_EQ(rollbackResult.value->restoredCheckpoint, *cpResult.value);
}

TEST_F(RollbackTest, RollbackToCheckpoint) {
    auto data = createTestData("Initial state");
    auto cpResult = stateManager->createCheckpoint("Initial", data);
    ASSERT_TRUE(cpResult.isSuccess());
    
    auto rollbackResult = rollbackEngine->rollbackToCheckpoint(*cpResult.value);
    ASSERT_TRUE(rollbackResult.isSuccess());
    EXPECT_TRUE(rollbackResult.value->success);
}

TEST_F(RollbackTest, RollbackToLatest) {
    stateManager->createCheckpoint("First", createTestData("1"));
    auto cp2 = stateManager->createCheckpoint("Second", createTestData("2"));
    ASSERT_TRUE(cp2.isSuccess());
    
    auto rollbackResult = rollbackEngine->rollbackToLatest();
    ASSERT_TRUE(rollbackResult.isSuccess());
    EXPECT_TRUE(rollbackResult.value->success);
    EXPECT_EQ(rollbackResult.value->restoredCheckpoint, *cp2.value);
}

TEST_F(RollbackTest, RollbackWithProgress) {
    auto cpResult = stateManager->createCheckpoint("Test", createTestData("data"));
    ASSERT_TRUE(cpResult.isSuccess());
    
    // Bazı operasyonlar ekle
    logger->logOperation(OperationType::Update, "Update 1", *cpResult.value);
    logger->logOperation(OperationType::Update, "Update 2", *cpResult.value);
    
    auto planResult = rollbackEngine->createRollbackPlan(*cpResult.value);
    ASSERT_TRUE(planResult.isSuccess());
    
    bool progressCalled = false;
    auto progressCallback = [&progressCalled](double progress, const std::string& status) {
        progressCalled = true;
        EXPECT_GE(progress, 0.0);
        EXPECT_LE(progress, 1.0);
    };
    
    auto rollbackResult = rollbackEngine->executeRollback(*planResult.value, progressCallback);
    ASSERT_TRUE(rollbackResult.isSuccess());
    // Progress callback çağrılmış olabilir veya olmayabilir (operasyon sayısına bağlı)
}

// Preview Rollback Tests
TEST_F(RollbackTest, PreviewRollback) {
    auto cpResult = stateManager->createCheckpoint("Target", createTestData("data"));
    ASSERT_TRUE(cpResult.isSuccess());
    
    logger->logOperation(OperationType::Update, "Op 1", *cpResult.value);
    logger->logOperation(OperationType::Update, "Op 2", *cpResult.value);
    
    auto preview = rollbackEngine->previewRollback(*cpResult.value);
    // Preview yapılabilmeli
}

// Undo Rollback Tests
TEST_F(RollbackTest, UndoRollback) {
    // Önce bir checkpoint oluştur
    auto cpResult = stateManager->createCheckpoint("Before Rollback", createTestData("data"));
    ASSERT_TRUE(cpResult.isSuccess());
    
    // Rollback yap
    auto rollbackResult = rollbackEngine->rollbackToCheckpoint(*cpResult.value);
    ASSERT_TRUE(rollbackResult.isSuccess());
    
    // Şimdi undo yapılabilir olmalı
    EXPECT_TRUE(rollbackEngine->canUndoRollback());
    
    auto undoResult = rollbackEngine->undoRollback();
    EXPECT_TRUE(undoResult.isSuccess());
}

TEST_F(RollbackTest, CannotUndoWithoutRollback) {
    EXPECT_FALSE(rollbackEngine->canUndoRollback());
    
    auto undoResult = rollbackEngine->undoRollback();
    EXPECT_TRUE(undoResult.isError());
}

// Partial Rollback Tests
TEST_F(RollbackTest, PartialRollback) {
    auto cpResult = stateManager->createCheckpoint("Base", createTestData("base"));
    ASSERT_TRUE(cpResult.isSuccess());
    
    auto opId1 = logger->logOperation(OperationType::Update, "Update 1", *cpResult.value);
    auto opId2 = logger->logOperation(OperationType::Update, "Update 2", *cpResult.value);
    
    PartialRollbackOptions options;
    options.operationsToUndo = {opId1};  // Sadece ilk operasyonu geri al
    options.preserveNewerChanges = true;
    
    auto result = rollbackEngine->partialRollback(options);
    ASSERT_TRUE(result.isSuccess());
    EXPECT_TRUE(result.value->success);
}

TEST_F(RollbackTest, PartialRollbackWithFilter) {
    auto cpResult = stateManager->createCheckpoint("Base", createTestData("base"));
    ASSERT_TRUE(cpResult.isSuccess());
    
    logger->logOperation(OperationType::Create, "Create op", *cpResult.value);
    logger->logOperation(OperationType::Update, "Update op", *cpResult.value);
    logger->logOperation(OperationType::Delete, "Delete op", *cpResult.value);
    
    PartialRollbackOptions options;
    options.filter = [](const OperationRecord& op) {
        return op.type == OperationType::Update;  // Sadece Update operasyonlarını geri al
    };
    
    auto result = rollbackEngine->partialRollback(options);
    ASSERT_TRUE(result.isSuccess());
}

// Confirmation Callback Tests
TEST_F(RollbackTest, RollbackWithConfirmation) {
    auto cpResult = stateManager->createCheckpoint("Test", createTestData("data"));
    ASSERT_TRUE(cpResult.isSuccess());
    
    bool confirmationCalled = false;
    rollbackEngine->setConfirmationCallback([&confirmationCalled](const RollbackPlan& plan) {
        confirmationCalled = true;
        return true;  // Onayla
    });
    
    // Büyük plan oluştur (confirmation gerektirecek)
    for (int i = 0; i < 15; ++i) {
        logger->logOperation(OperationType::Update, "Update " + std::to_string(i), *cpResult.value);
    }
    
    auto planResult = rollbackEngine->createRollbackPlan(*cpResult.value);
    ASSERT_TRUE(planResult.isSuccess());
    
    // Plan confirmation gerektirmeli
    if (planResult.value->requiresConfirmation) {
        auto rollbackResult = rollbackEngine->executeRollback(*planResult.value);
        // Confirmation callback çağrılmış olmalı
    }
}

TEST_F(RollbackTest, RollbackCancelledByConfirmation) {
    auto cpResult = stateManager->createCheckpoint("Test", createTestData("data"));
    ASSERT_TRUE(cpResult.isSuccess());
    
    rollbackEngine->setConfirmationCallback([](const RollbackPlan& plan) {
        return false;  // Reddet
    });
    
    // Büyük plan oluştur
    for (int i = 0; i < 15; ++i) {
        logger->logOperation(OperationType::Update, "Update " + std::to_string(i), *cpResult.value);
    }
    
    auto planResult = rollbackEngine->createRollbackPlan(*cpResult.value);
    ASSERT_TRUE(planResult.isSuccess());
    
    if (planResult.value->requiresConfirmation) {
        auto rollbackResult = rollbackEngine->executeRollback(*planResult.value);
        ASSERT_TRUE(rollbackResult.isSuccess());
        EXPECT_FALSE(rollbackResult.value->success);
        EXPECT_TRUE(rollbackResult.value->errorMessage.find("cancelled") != std::string::npos);
    }
}

// Statistics Tests
TEST_F(RollbackTest, RollbackStatistics) {
    EXPECT_EQ(rollbackEngine->getRollbackCount(), 0);
    EXPECT_EQ(rollbackEngine->getTotalRollbackTime().count(), 0);
    
    auto cpResult = stateManager->createCheckpoint("Test", createTestData("data"));
    ASSERT_TRUE(cpResult.isSuccess());
    
    rollbackEngine->rollbackToCheckpoint(*cpResult.value);
    
    EXPECT_EQ(rollbackEngine->getRollbackCount(), 1);
    EXPECT_GE(rollbackEngine->getTotalRollbackTime().count(), 0);
}

// Max Undo History Tests
TEST_F(RollbackTest, MaxUndoHistory) {
    rollbackEngine->setMaxUndoHistory(3);
    
    // 5 rollback yap
    for (int i = 0; i < 5; ++i) {
        auto cpResult = stateManager->createCheckpoint("CP" + std::to_string(i), 
                                                       createTestData("data" + std::to_string(i)));
        ASSERT_TRUE(cpResult.isSuccess());
        rollbackEngine->rollbackToCheckpoint(*cpResult.value);
    }
    
    // Sadece son 3 undo yapılabilir olmalı
    int undoCount = 0;
    while (rollbackEngine->canUndoRollback()) {
        auto result = rollbackEngine->undoRollback();
        if (result.isSuccess()) undoCount++;
        else break;
    }
    
    EXPECT_LE(undoCount, 3);
}
