#include <gtest/gtest.h>
#include "state/state_manager.hpp"
#include "state/storage.hpp"
#include <filesystem>
#include <thread>

using namespace checkpoint;

class StateManagerTest : public ::testing::Test {
protected:
    std::filesystem::path testDir = "test_checkpoints";
    
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

// Checkpoint Creation Tests
TEST_F(StateManagerTest, CreateCheckpoint) {
    StateManager manager(testDir);
    auto data = createTestData("Test checkpoint data");
    
    auto result = manager.createCheckpoint("Test Checkpoint", data);
    
    ASSERT_TRUE(result.isSuccess());
    EXPECT_GT(*result.value, 0);
}

TEST_F(StateManagerTest, GetCheckpoint) {
    StateManager manager(testDir);
    auto data = createTestData("Test data for retrieval");
    
    auto createResult = manager.createCheckpoint("Retrieval Test", data);
    ASSERT_TRUE(createResult.isSuccess());
    
    auto getResult = manager.getCheckpoint(*createResult.value);
    ASSERT_TRUE(getResult.isSuccess());
    
    EXPECT_EQ(getResult.value->getName(), "Retrieval Test");
    EXPECT_EQ(getResult.value->getData(), data);
}

TEST_F(StateManagerTest, GetNonExistentCheckpoint) {
    StateManager manager(testDir);
    
    auto result = manager.getCheckpoint(99999);
    
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.error, ErrorCode::CheckpointNotFound);
}

TEST_F(StateManagerTest, UpdateCheckpoint) {
    StateManager manager(testDir);
    auto initialData = createTestData("Initial data");
    auto updatedData = createTestData("Updated data");
    
    auto createResult = manager.createCheckpoint("Update Test", initialData);
    ASSERT_TRUE(createResult.isSuccess());
    
    auto updateResult = manager.updateCheckpoint(*createResult.value, updatedData);
    EXPECT_TRUE(updateResult.isSuccess());
    
    auto getResult = manager.getCheckpoint(*createResult.value);
    ASSERT_TRUE(getResult.isSuccess());
    EXPECT_EQ(getResult.value->getData(), updatedData);
}

TEST_F(StateManagerTest, DeleteCheckpoint) {
    StateManager manager(testDir);
    auto data = createTestData("Data to delete");
    
    auto createResult = manager.createCheckpoint("Delete Test", data);
    ASSERT_TRUE(createResult.isSuccess());
    
    auto deleteResult = manager.deleteCheckpoint(*createResult.value);
    EXPECT_TRUE(deleteResult.isSuccess());
    
    auto getResult = manager.getCheckpoint(*createResult.value);
    EXPECT_TRUE(getResult.isError());
}

TEST_F(StateManagerTest, ListCheckpoints) {
    StateManager manager(testDir);
    
    manager.createCheckpoint("First", createTestData("1"));
    manager.createCheckpoint("Second", createTestData("2"));
    manager.createCheckpoint("Third", createTestData("3"));
    
    auto checkpoints = manager.listCheckpoints();
    
    EXPECT_EQ(checkpoints.size(), 3);
}

TEST_F(StateManagerTest, GetLatestCheckpoint) {
    StateManager manager(testDir);
    
    manager.createCheckpoint("First", createTestData("1"));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto secondResult = manager.createCheckpoint("Second", createTestData("2"));
    
    auto latestResult = manager.getLatestCheckpointId();
    
    ASSERT_TRUE(latestResult.isSuccess());
    EXPECT_EQ(*latestResult.value, *secondResult.value);
}

TEST_F(StateManagerTest, GetCurrentState) {
    StateManager manager(testDir);
    auto data = createTestData("Current state data");
    
    manager.createCheckpoint("State Test", data);
    
    auto stateResult = manager.getCurrentState();
    ASSERT_TRUE(stateResult.isSuccess());
    EXPECT_EQ(*stateResult.value, data);
}

// Checkpoint Integrity Tests
TEST_F(StateManagerTest, CheckpointIntegrity) {
    StateManager manager(testDir);
    auto data = createTestData("Integrity test data");
    
    auto createResult = manager.createCheckpoint("Integrity Test", data);
    ASSERT_TRUE(createResult.isSuccess());
    
    auto getResult = manager.getCheckpoint(*createResult.value);
    ASSERT_TRUE(getResult.isSuccess());
    
    EXPECT_TRUE(getResult.value->verifyIntegrity());
}

TEST_F(StateManagerTest, CheckpointValidity) {
    StateManager manager(testDir);
    auto data = createTestData("Validity test");
    
    auto createResult = manager.createCheckpoint("Validity Test", data);
    ASSERT_TRUE(createResult.isSuccess());
    
    auto getResult = manager.getCheckpoint(*createResult.value);
    ASSERT_TRUE(getResult.isSuccess());
    
    EXPECT_TRUE(getResult.value->isValid());
}

// Storage Tests
TEST_F(StateManagerTest, PersistenceAcrossSessions) {
    CheckpointId savedId;
    
    {
        StateManager manager(testDir);
        auto data = createTestData("Persistent data");
        auto result = manager.createCheckpoint("Persistence Test", data);
        ASSERT_TRUE(result.isSuccess());
        savedId = *result.value;
    }
    
    // Yeni session
    {
        StateManager manager(testDir);
        auto result = manager.getCheckpoint(savedId);
        ASSERT_TRUE(result.isSuccess());
        EXPECT_EQ(result.value->getName(), "Persistence Test");
    }
}

TEST_F(StateManagerTest, SaveAndLoadFromFile) {
    StateManager manager(testDir);
    auto data = createTestData("File save test data");
    
    auto createResult = manager.createCheckpoint("File Test", data);
    ASSERT_TRUE(createResult.isSuccess());
    
    auto filePath = testDir / "exported.chkpt";
    auto saveResult = manager.saveToFile(*createResult.value, filePath);
    EXPECT_TRUE(saveResult.isSuccess());
    EXPECT_TRUE(std::filesystem::exists(filePath));
    
    auto loadResult = manager.loadFromFile(filePath);
    ASSERT_TRUE(loadResult.isSuccess());
    EXPECT_EQ(loadResult.value->getName(), "File Test");
}

// Statistics Tests
TEST_F(StateManagerTest, CheckpointCount) {
    StateManager manager(testDir);
    
    EXPECT_EQ(manager.getCheckpointCount(), 0);
    
    manager.createCheckpoint("1", createTestData("a"));
    manager.createCheckpoint("2", createTestData("b"));
    
    EXPECT_EQ(manager.getCheckpointCount(), 2);
}

TEST_F(StateManagerTest, TotalStorageSize) {
    StateManager manager(testDir);
    
    manager.createCheckpoint("Large", createTestData(std::string(1000, 'X')));
    
    EXPECT_GT(manager.getTotalStorageSize(), 0);
}

// Memory Storage Tests
TEST_F(StateManagerTest, MemoryStorageBasic) {
    StateManager manager;  // Varsayılan memory storage
    auto data = createTestData("Memory test");
    
    auto result = manager.createCheckpoint("Memory Test", data);
    ASSERT_TRUE(result.isSuccess());
    
    auto getResult = manager.getCheckpoint(*result.value);
    ASSERT_TRUE(getResult.isSuccess());
    EXPECT_EQ(getResult.value->getData(), data);
}

// Edge Cases
TEST_F(StateManagerTest, EmptyCheckpointName) {
    StateManager manager(testDir);
    auto data = createTestData("test");
    
    auto result = manager.createCheckpoint("", data);
    ASSERT_TRUE(result.isSuccess());  // Boş isim kabul edilebilir
}

TEST_F(StateManagerTest, EmptyCheckpointData) {
    StateManager manager(testDir);
    StateData emptyData;
    
    auto result = manager.createCheckpoint("Empty Data", emptyData);
    ASSERT_TRUE(result.isSuccess());
    
    auto getResult = manager.getCheckpoint(*result.value);
    ASSERT_TRUE(getResult.isSuccess());
    EXPECT_TRUE(getResult.value->getData().empty());
}

TEST_F(StateManagerTest, LargeCheckpointData) {
    StateManager manager(testDir);
    
    // 1MB veri
    std::string largeStr(1024 * 1024, 'X');
    auto data = createTestData(largeStr);
    
    auto result = manager.createCheckpoint("Large Data", data);
    ASSERT_TRUE(result.isSuccess());
    
    auto getResult = manager.getCheckpoint(*result.value);
    ASSERT_TRUE(getResult.isSuccess());
    EXPECT_EQ(getResult.value->getData().size(), data.size());
}
