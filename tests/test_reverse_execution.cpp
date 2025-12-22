#include <gtest/gtest.h>
#include "real_process/file_operation.hpp"
#include "real_process/reverse_executor.hpp"
#include <fstream>
#include <filesystem>

using namespace checkpoint::real_process;

class FileOperationTest : public ::testing::Test {
protected:
    std::string testDir;
    
    void SetUp() override {
        testDir = "/tmp/checkpoint_test_" + std::to_string(getpid());
        std::filesystem::create_directories(testDir);
    }
    
    void TearDown() override {
        std::filesystem::remove_all(testDir);
    }
    
    std::string createTestFile(const std::string& name, const std::string& content) {
        std::string path = testDir + "/" + name;
        std::ofstream file(path);
        file << content;
        file.close();
        return path;
    }
    
    std::string readFile(const std::string& path) {
        std::ifstream file(path);
        std::string content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
        return content;
    }
};

// ============================================================================
// FileOperation Tests
// ============================================================================

TEST_F(FileOperationTest, SerializeDeserialize) {
    FileOperation op;
    op.operationId = 12345;
    op.type = FileOperationType::WRITE;
    op.path = "/tmp/test.txt";
    op.originalPath = "/tmp/old.txt";
    op.offset = 100;
    op.originalSize = 500;
    op.newSize = 600;
    op.originalContent = std::vector<uint8_t>{1, 2, 3, 4, 5};
    op.isReversible = true;
    op.description = "Test operation";
    
    FileContentDiff diff;
    diff.offset = 50;
    diff.oldData = {10, 20, 30};
    diff.newData = {40, 50, 60, 70};
    op.diffs.push_back(diff);
    
    auto serialized = op.serialize();
    auto deserialized = FileOperation::deserialize(serialized);
    
    EXPECT_EQ(deserialized.operationId, 12345);
    EXPECT_EQ(deserialized.type, FileOperationType::WRITE);
    EXPECT_EQ(deserialized.path, "/tmp/test.txt");
    EXPECT_EQ(deserialized.originalPath, "/tmp/old.txt");
    EXPECT_EQ(deserialized.offset, 100);
    EXPECT_EQ(deserialized.originalSize, 500);
    EXPECT_EQ(deserialized.newSize, 600);
    EXPECT_TRUE(deserialized.originalContent.has_value());
    EXPECT_EQ(deserialized.originalContent->size(), 5);
    EXPECT_TRUE(deserialized.isReversible);
    EXPECT_EQ(deserialized.diffs.size(), 1);
    EXPECT_EQ(deserialized.diffs[0].offset, 50);
}

TEST_F(FileOperationTest, FileOpTypeToString) {
    EXPECT_EQ(fileOpTypeToString(FileOperationType::CREATE), "CREATE");
    EXPECT_EQ(fileOpTypeToString(FileOperationType::WRITE), "WRITE");
    EXPECT_EQ(fileOpTypeToString(FileOperationType::DELETE), "DELETE");
    EXPECT_EQ(fileOpTypeToString(FileOperationType::RENAME), "RENAME");
    EXPECT_EQ(fileOpTypeToString(FileOperationType::TRUNCATE), "TRUNCATE");
}

TEST_F(FileOperationTest, StringToFileOpType) {
    EXPECT_EQ(stringToFileOpType("CREATE"), FileOperationType::CREATE);
    EXPECT_EQ(stringToFileOpType("WRITE"), FileOperationType::WRITE);
    EXPECT_EQ(stringToFileOpType("DELETE"), FileOperationType::DELETE);
    EXPECT_EQ(stringToFileOpType("RENAME"), FileOperationType::RENAME);
}

// ============================================================================
// FileOperationLog Tests
// ============================================================================

TEST_F(FileOperationTest, LogRecordAndRetrieve) {
    FileOperationLog log;
    
    FileOperation op1;
    op1.type = FileOperationType::CREATE;
    op1.path = "/tmp/file1.txt";
    
    FileOperation op2;
    op2.type = FileOperationType::WRITE;
    op2.path = "/tmp/file2.txt";
    
    log.recordOperation(op1);
    log.recordOperation(op2);
    
    EXPECT_EQ(log.getOperationCount(), 2);
    
    auto allOps = log.getAllOperations();
    EXPECT_EQ(allOps.size(), 2);
}

TEST_F(FileOperationTest, LogGetOperationsForFile) {
    FileOperationLog log;
    
    FileOperation op1;
    op1.type = FileOperationType::CREATE;
    op1.path = "/tmp/target.txt";
    
    FileOperation op2;
    op2.type = FileOperationType::WRITE;
    op2.path = "/tmp/target.txt";
    
    FileOperation op3;
    op3.type = FileOperationType::CREATE;
    op3.path = "/tmp/other.txt";
    
    log.recordOperation(op1);
    log.recordOperation(op2);
    log.recordOperation(op3);
    
    auto targetOps = log.getOperationsForFile("/tmp/target.txt");
    EXPECT_EQ(targetOps.size(), 2);
}

TEST_F(FileOperationTest, LogCheckpointMarker) {
    FileOperationLog log;
    
    FileOperation op1;
    op1.type = FileOperationType::CREATE;
    log.recordOperation(op1);
    
    log.markCheckpoint(100);
    
    FileOperation op2;
    op2.type = FileOperationType::WRITE;
    log.recordOperation(op2);
    
    FileOperation op3;
    op3.type = FileOperationType::DELETE;
    log.recordOperation(op3);
    
    auto opsSinceCheckpoint = log.getOperationsSince(100);
    EXPECT_EQ(opsSinceCheckpoint.size(), 2);
}

TEST_F(FileOperationTest, LogSerializeDeserialize) {
    FileOperationLog log;
    
    FileOperation op;
    op.type = FileOperationType::WRITE;
    op.path = "/tmp/test.txt";
    op.originalContent = std::vector<uint8_t>{1, 2, 3};
    log.recordOperation(op);
    
    log.markCheckpoint(1);
    
    auto serialized = log.serialize();
    auto deserialized = FileOperationLog::deserialize(serialized);
    
    EXPECT_EQ(deserialized.getOperationCount(), 1);
}

// ============================================================================
// FileOperationTracker Tests
// ============================================================================

TEST_F(FileOperationTest, TrackerBasicTracking) {
    FileOperationTracker tracker;
    tracker.startTracking(getpid());
    
    EXPECT_TRUE(tracker.isTracking());
    
    tracker.stopTracking();
    EXPECT_FALSE(tracker.isTracking());
}

TEST_F(FileOperationTest, TrackerCaptureFileContent) {
    FileOperationTracker tracker;
    
    std::string testPath = createTestFile("capture_test.txt", "Hello World!");
    
    auto content = tracker.captureFileContent(testPath);
    EXPECT_EQ(content.size(), 12);
    
    std::string captured(content.begin(), content.end());
    EXPECT_EQ(captured, "Hello World!");
}

TEST_F(FileOperationTest, TrackerOptions) {
    FileTrackingOptions options;
    options.includePaths.push_back(testDir);
    options.maxFileSize = 1024;
    
    FileOperationTracker tracker(options);
    
    EXPECT_EQ(tracker.getOptions().maxFileSize, 1024);
}

// ============================================================================
// ReverseExecutor Tests
// ============================================================================

TEST_F(FileOperationTest, ReverseCreate) {
    // Create a file
    std::string testPath = createTestFile("reverse_create.txt", "content");
    EXPECT_TRUE(std::filesystem::exists(testPath));
    
    // Record the create operation
    FileOperation op;
    op.operationId = 1;
    op.type = FileOperationType::CREATE;
    op.path = testPath;
    op.isReversible = true;
    
    // Reverse it
    ReverseExecutor executor;
    auto result = executor.reverseOperation(op);
    
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(std::filesystem::exists(testPath));
}

TEST_F(FileOperationTest, ReverseDelete) {
    std::string testPath = testDir + "/reverse_delete.txt";
    std::string originalContent = "Original content to restore";
    
    // Record delete operation with backup
    FileOperation op;
    op.operationId = 1;
    op.type = FileOperationType::DELETE;
    op.path = testPath;
    op.originalSize = originalContent.size();
    op.originalContent = std::vector<uint8_t>(originalContent.begin(), originalContent.end());
    op.isReversible = true;
    
    // File doesn't exist (was deleted)
    EXPECT_FALSE(std::filesystem::exists(testPath));
    
    // Reverse the delete (recreate)
    ReverseOptions opts;
    opts.createBackups = false;
    ReverseExecutor executor(opts);
    auto result = executor.reverseOperation(op);
    
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(std::filesystem::exists(testPath));
    EXPECT_EQ(readFile(testPath), originalContent);
}

TEST_F(FileOperationTest, ReverseRename) {
    std::string oldPath = testDir + "/old_name.txt";
    std::string newPath = testDir + "/new_name.txt";
    
    // Create file at "new" location (after rename)
    createTestFile("new_name.txt", "content");
    EXPECT_TRUE(std::filesystem::exists(newPath));
    EXPECT_FALSE(std::filesystem::exists(oldPath));
    
    // Record rename operation
    FileOperation op;
    op.operationId = 1;
    op.type = FileOperationType::RENAME;
    op.path = newPath;          // Current location
    op.originalPath = oldPath;  // Original location
    op.isReversible = true;
    
    // Reverse the rename
    ReverseOptions opts;
    opts.createBackups = false;
    ReverseExecutor executor(opts);
    auto result = executor.reverseOperation(op);
    
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(std::filesystem::exists(oldPath));
    EXPECT_FALSE(std::filesystem::exists(newPath));
}

TEST_F(FileOperationTest, ReverseWrite) {
    std::string testPath = createTestFile("reverse_write.txt", "New content");
    std::string originalContent = "Old content";
    
    // Record write operation with full backup
    FileOperation op;
    op.operationId = 1;
    op.type = FileOperationType::WRITE;
    op.path = testPath;
    op.originalContent = std::vector<uint8_t>(originalContent.begin(), originalContent.end());
    op.isReversible = true;
    
    // Reverse the write
    ReverseOptions opts;
    opts.createBackups = false;
    ReverseExecutor executor(opts);
    auto result = executor.reverseOperation(op);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(readFile(testPath), originalContent);
}

TEST_F(FileOperationTest, ReverseTruncate) {
    std::string originalContent = "This is a longer original content";
    std::string testPath = createTestFile("reverse_truncate.txt", "Short");
    
    // Record truncate operation with full backup
    FileOperation op;
    op.operationId = 1;
    op.type = FileOperationType::TRUNCATE;
    op.path = testPath;
    op.originalSize = originalContent.size();
    op.newSize = 5;
    op.originalContent = std::vector<uint8_t>(originalContent.begin(), originalContent.end());
    op.isReversible = true;
    
    // Reverse the truncate
    ReverseOptions opts;
    opts.createBackups = false;
    ReverseExecutor executor(opts);
    auto result = executor.reverseOperation(op);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(readFile(testPath), originalContent);
}

TEST_F(FileOperationTest, BatchReverse) {
    // Create test files
    std::string file1 = createTestFile("batch1.txt", "Modified 1");
    std::string file2 = createTestFile("batch2.txt", "Modified 2");
    std::string file3 = testDir + "/batch3.txt";  // Will be created by reverse
    
    std::string orig1 = "Original 1";
    std::string orig2 = "Original 2";
    std::string orig3 = "Original 3";
    
    // Create operations
    std::vector<FileOperation> ops;
    
    FileOperation op1;
    op1.operationId = 1;
    op1.type = FileOperationType::WRITE;
    op1.path = file1;
    op1.originalContent = std::vector<uint8_t>(orig1.begin(), orig1.end());
    op1.isReversible = true;
    op1.timestamp = std::chrono::system_clock::now() - std::chrono::seconds(3);
    ops.push_back(op1);
    
    FileOperation op2;
    op2.operationId = 2;
    op2.type = FileOperationType::WRITE;
    op2.path = file2;
    op2.originalContent = std::vector<uint8_t>(orig2.begin(), orig2.end());
    op2.isReversible = true;
    op2.timestamp = std::chrono::system_clock::now() - std::chrono::seconds(2);
    ops.push_back(op2);
    
    FileOperation op3;
    op3.operationId = 3;
    op3.type = FileOperationType::DELETE;
    op3.path = file3;
    op3.originalContent = std::vector<uint8_t>(orig3.begin(), orig3.end());
    op3.isReversible = true;
    op3.timestamp = std::chrono::system_clock::now() - std::chrono::seconds(1);
    ops.push_back(op3);
    
    // Execute batch reverse
    ReverseOptions opts;
    opts.createBackups = false;
    opts.reverseNewestFirst = true;
    ReverseExecutor executor(opts);
    
    auto result = executor.reverseOperations(ops);
    
    EXPECT_TRUE(result.allSucceeded);
    EXPECT_EQ(result.successCount, 3);
    EXPECT_EQ(result.failCount, 0);
    
    // Verify results
    EXPECT_EQ(readFile(file1), orig1);
    EXPECT_EQ(readFile(file2), orig2);
    EXPECT_TRUE(std::filesystem::exists(file3));
    EXPECT_EQ(readFile(file3), orig3);
}

TEST_F(FileOperationTest, PreviewReverse) {
    FileOperation op1;
    op1.type = FileOperationType::CREATE;
    op1.path = "/tmp/test1.txt";
    op1.isReversible = true;
    
    FileOperation op2;
    op2.type = FileOperationType::RENAME;
    op2.path = "/tmp/new.txt";
    op2.originalPath = "/tmp/old.txt";
    op2.isReversible = true;
    
    std::vector<FileOperation> ops = {op1, op2};
    
    ReverseExecutor executor;
    auto preview = executor.previewReverse(ops);
    
    EXPECT_EQ(preview.size(), 2);
    EXPECT_TRUE(preview[0].find("DELETE") != std::string::npos || 
                preview[1].find("DELETE") != std::string::npos);
    EXPECT_TRUE(preview[0].find("RENAME") != std::string::npos || 
                preview[1].find("RENAME") != std::string::npos);
}

TEST_F(FileOperationTest, DryRun) {
    std::string testPath = createTestFile("dryrun.txt", "Content");
    
    FileOperation op;
    op.operationId = 1;
    op.type = FileOperationType::CREATE;
    op.path = testPath;
    op.isReversible = true;
    
    // Use dry run option
    ReverseOptions opts = ReverseOptions::preview();
    ReverseExecutor executor(opts);
    
    auto result = executor.reverseOperation(op);
    
    EXPECT_TRUE(result.success);
    // File should still exist (dry run doesn't actually delete)
    EXPECT_TRUE(std::filesystem::exists(testPath));
}

TEST_F(FileOperationTest, CannotReverseNonReversible) {
    FileOperation op;
    op.operationId = 1;
    op.type = FileOperationType::WRITE;
    op.path = "/tmp/test.txt";
    op.isReversible = false;  // Marked as not reversible
    
    ReverseExecutor executor;
    EXPECT_FALSE(executor.canReverse(op));
}

TEST_F(FileOperationTest, CannotReverseAlreadyReversed) {
    FileOperation op;
    op.operationId = 1;
    op.type = FileOperationType::WRITE;
    op.path = "/tmp/test.txt";
    op.isReversible = true;
    op.wasReversed = true;  // Already reversed
    
    ReverseExecutor executor;
    EXPECT_FALSE(executor.canReverse(op));
}

// ============================================================================
// Integration Test
// ============================================================================

TEST_F(FileOperationTest, FullWorkflow) {
    // Setup tracker
    FileTrackingOptions trackOpts;
    trackOpts.includePaths.push_back(testDir);
    FileOperationTracker tracker(trackOpts);
    tracker.startTracking(getpid());
    
    // Create a test file
    std::string testPath = testDir + "/workflow_test.txt";
    std::string originalContent = "Original workflow content";
    
    // Simulate operations by manually recording them
    // In real usage, these would be captured via syscall interception
    
    // 1. Record file creation
    tracker.beforeCreate(testPath, 0644);
    {
        std::ofstream file(testPath);
        file << originalContent;
    }
    tracker.afterCreate(testPath, 3, true);
    
    // 2. Create checkpoint
    tracker.onCheckpointCreated(1000);
    
    // 3. Modify the file
    std::string newContent = "Modified content";
    tracker.beforeWrite(3, testPath, 0, newContent.size());
    {
        std::ofstream file(testPath, std::ios::trunc);
        file << newContent;
    }
    tracker.afterWrite(3, testPath, 0, newContent.size(), true);
    
    // Verify file was modified
    EXPECT_EQ(readFile(testPath), newContent);
    
    // 4. Get operations since checkpoint
    auto& log = tracker.getLog();
    auto opsSinceCheckpoint = log.getOperationsSince(1000);
    
    EXPECT_GE(opsSinceCheckpoint.size(), 1);
    
    tracker.stopTracking();
}
