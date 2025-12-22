#include <gtest/gtest.h>
#include "real_process/file_operation.hpp"
#include "real_process/reverse_executor.hpp"
#include <fstream>
#include <filesystem>
#include <thread>
#include <chrono>

using namespace checkpoint::real_process;

// ============================================================================
// Advanced FileOperation Tests
// ============================================================================

class AdvancedFileOperationTest : public ::testing::Test {
protected:
    std::string testDir;
    
    void SetUp() override {
        testDir = "/tmp/checkpoint_adv_test_" + std::to_string(getpid());
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
    
    void createBinaryFile(const std::string& path, const std::vector<uint8_t>& data) {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
        file.close();
    }
    
    std::vector<uint8_t> readBinaryFile(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> data(size);
        file.read(reinterpret_cast<char*>(data.data()), size);
        return data;
    }
};

// ============================================================================
// FileOperation Serialization Edge Cases
// ============================================================================

TEST_F(AdvancedFileOperationTest, SerializeEmptyOperation) {
    FileOperation op;
    auto serialized = op.serialize();
    auto deserialized = FileOperation::deserialize(serialized);
    
    EXPECT_EQ(deserialized.operationId, 0);
    EXPECT_EQ(deserialized.type, FileOperationType::WRITE);
    EXPECT_TRUE(deserialized.path.empty());
    EXPECT_FALSE(deserialized.originalContent.has_value());
    EXPECT_TRUE(deserialized.diffs.empty());
}

TEST_F(AdvancedFileOperationTest, SerializeLargeContent) {
    FileOperation op;
    op.operationId = 1;
    op.type = FileOperationType::WRITE;
    op.path = "/tmp/large.bin";
    
    // Create 1MB of data
    std::vector<uint8_t> largeData(1024 * 1024);
    for (size_t i = 0; i < largeData.size(); ++i) {
        largeData[i] = static_cast<uint8_t>(i % 256);
    }
    op.originalContent = largeData;
    
    auto serialized = op.serialize();
    auto deserialized = FileOperation::deserialize(serialized);
    
    ASSERT_TRUE(deserialized.originalContent.has_value());
    EXPECT_EQ(deserialized.originalContent->size(), 1024 * 1024);
    EXPECT_EQ(*deserialized.originalContent, largeData);
}

TEST_F(AdvancedFileOperationTest, SerializeMultipleDiffs) {
    FileOperation op;
    op.operationId = 1;
    op.type = FileOperationType::WRITE;
    op.path = "/tmp/test.txt";
    
    // Add multiple diffs
    for (int i = 0; i < 10; ++i) {
        FileContentDiff diff;
        diff.offset = i * 100;
        diff.oldData = std::vector<uint8_t>(50, static_cast<uint8_t>(i));
        diff.newData = std::vector<uint8_t>(60, static_cast<uint8_t>(i + 100));
        op.diffs.push_back(diff);
    }
    
    auto serialized = op.serialize();
    auto deserialized = FileOperation::deserialize(serialized);
    
    EXPECT_EQ(deserialized.diffs.size(), 10);
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(deserialized.diffs[i].offset, i * 100);
        EXPECT_EQ(deserialized.diffs[i].oldData.size(), 50);
        EXPECT_EQ(deserialized.diffs[i].newData.size(), 60);
    }
}

TEST_F(AdvancedFileOperationTest, SerializeSpecialCharactersInPath) {
    FileOperation op;
    op.path = "/tmp/test file with spaces & special!@#$%chars.txt";
    op.originalPath = "/tmp/original-path_with_underscores.txt";
    op.description = "Description with émojis 🎉 and unicode: こんにちは";
    
    auto serialized = op.serialize();
    auto deserialized = FileOperation::deserialize(serialized);
    
    EXPECT_EQ(deserialized.path, op.path);
    EXPECT_EQ(deserialized.originalPath, op.originalPath);
    EXPECT_EQ(deserialized.description, op.description);
}

TEST_F(AdvancedFileOperationTest, OperationMemoryUsage) {
    FileOperation op;
    op.path = "/tmp/test.txt";
    op.description = "test";
    op.originalContent = std::vector<uint8_t>(1000);
    
    FileContentDiff diff;
    diff.oldData = std::vector<uint8_t>(500);
    diff.newData = std::vector<uint8_t>(500);
    op.diffs.push_back(diff);
    
    size_t usage = op.estimatedMemoryUsage();
    
    // Should be at least the size of content + diffs
    EXPECT_GE(usage, 2000);
}

// ============================================================================
// FileOperationLog Advanced Tests
// ============================================================================

TEST_F(AdvancedFileOperationTest, LogFilterOperations) {
    FileOperationLog log;
    
    // Add various operations
    for (int i = 0; i < 20; ++i) {
        FileOperation op;
        op.type = (i % 2 == 0) ? FileOperationType::WRITE : FileOperationType::CREATE;
        op.path = "/tmp/file" + std::to_string(i) + ".txt";
        op.originalSize = i * 100;
        log.recordOperation(op);
    }
    
    // Filter only WRITE operations
    auto writeOps = log.filterOperations([](const FileOperation& op) {
        return op.type == FileOperationType::WRITE;
    });
    
    EXPECT_EQ(writeOps.size(), 10);
    for (const auto& op : writeOps) {
        EXPECT_EQ(op.type, FileOperationType::WRITE);
    }
}

TEST_F(AdvancedFileOperationTest, LogFilterBySize) {
    FileOperationLog log;
    
    for (int i = 0; i < 10; ++i) {
        FileOperation op;
        op.path = "/tmp/file" + std::to_string(i) + ".txt";
        op.originalSize = i * 100;
        log.recordOperation(op);
    }
    
    // Filter operations with size >= 500
    auto largeOps = log.filterOperations([](const FileOperation& op) {
        return op.originalSize >= 500;
    });
    
    EXPECT_EQ(largeOps.size(), 5);
}

TEST_F(AdvancedFileOperationTest, LogMultipleCheckpoints) {
    FileOperationLog log;
    
    // Operations before checkpoint 1
    FileOperation op1;
    op1.path = "/tmp/before1.txt";
    log.recordOperation(op1);
    
    log.markCheckpoint(100);
    
    // Operations between checkpoint 1 and 2
    FileOperation op2;
    op2.path = "/tmp/between.txt";
    log.recordOperation(op2);
    
    log.markCheckpoint(200);
    
    // Operations after checkpoint 2
    FileOperation op3;
    op3.path = "/tmp/after.txt";
    log.recordOperation(op3);
    
    auto sinceCP1 = log.getOperationsSince(100);
    auto sinceCP2 = log.getOperationsSince(200);
    
    EXPECT_EQ(sinceCP1.size(), 2);  // op2 and op3
    EXPECT_EQ(sinceCP2.size(), 1);  // only op3
}

TEST_F(AdvancedFileOperationTest, LogClearBeforeCheckpoint) {
    FileOperationLog log;
    
    // Add 5 operations
    for (int i = 0; i < 5; ++i) {
        FileOperation op;
        op.path = "/tmp/file" + std::to_string(i) + ".txt";
        log.recordOperation(op);
    }
    
    log.markCheckpoint(100);
    
    // Add 3 more
    for (int i = 5; i < 8; ++i) {
        FileOperation op;
        op.path = "/tmp/file" + std::to_string(i) + ".txt";
        log.recordOperation(op);
    }
    
    EXPECT_EQ(log.getOperationCount(), 8);
    
    log.clearOperationsBeforeCheckpoint(100);
    
    EXPECT_EQ(log.getOperationCount(), 3);
}

TEST_F(AdvancedFileOperationTest, LogPersistence) {
    std::string logPath = testDir + "/test_log.bin";
    
    // Create and populate log
    {
        FileOperationLog log;
        
        FileOperation op;
        op.type = FileOperationType::WRITE;
        op.path = "/tmp/test.txt";
        op.originalContent = std::vector<uint8_t>{1, 2, 3, 4, 5};
        log.recordOperation(op);
        
        log.markCheckpoint(999);
        
        EXPECT_TRUE(log.saveToFile(logPath));
    }
    
    // Load and verify
    {
        FileOperationLog log;
        EXPECT_TRUE(log.loadFromFile(logPath));
        
        EXPECT_EQ(log.getOperationCount(), 1);
        auto ops = log.getAllOperations();
        EXPECT_EQ(ops[0].type, FileOperationType::WRITE);
        EXPECT_TRUE(ops[0].originalContent.has_value());
    }
}

// ============================================================================
// FileOperationTracker Advanced Tests
// ============================================================================

TEST_F(AdvancedFileOperationTest, TrackerPathFiltering) {
    FileTrackingOptions options;
    options.includePaths.push_back(testDir);
    options.excludePaths.push_back(testDir + "/excluded");
    
    FileOperationTracker tracker(options);
    tracker.startTracking(getpid());
    
    // Create excluded directory
    std::filesystem::create_directories(testDir + "/excluded");
    
    // Track included path
    std::string includedPath = testDir + "/included.txt";
    tracker.beforeCreate(includedPath, 0644);
    tracker.afterCreate(includedPath, 3, true);
    
    // Track excluded path - should be ignored
    std::string excludedPath = testDir + "/excluded/file.txt";
    tracker.beforeCreate(excludedPath, 0644);
    tracker.afterCreate(excludedPath, 4, true);
    
    EXPECT_EQ(tracker.getTrackedOperationCount(), 1);
}

TEST_F(AdvancedFileOperationTest, TrackerExtensionFiltering) {
    FileTrackingOptions options;
    options.includePaths.push_back(testDir);
    options.includeExtensions.push_back("txt");
    options.includeExtensions.push_back("log");
    
    FileOperationTracker tracker(options);
    tracker.startTracking(getpid());
    
    tracker.beforeCreate(testDir + "/file.txt", 0644);
    tracker.afterCreate(testDir + "/file.txt", 3, true);
    
    tracker.beforeCreate(testDir + "/file.log", 0644);
    tracker.afterCreate(testDir + "/file.log", 4, true);
    
    tracker.beforeCreate(testDir + "/file.bin", 0644);
    tracker.afterCreate(testDir + "/file.bin", 5, true);
    
    // Only .txt and .log should be tracked
    EXPECT_EQ(tracker.getTrackedOperationCount(), 2);
}

TEST_F(AdvancedFileOperationTest, TrackerCapturePartialContent) {
    std::string testPath = createTestFile("partial.txt", "0123456789ABCDEFGHIJ");
    
    FileOperationTracker tracker;
    
    // Capture partial content (offset 5, size 10)
    auto content = tracker.captureFileContent(testPath, 5, 10);
    
    EXPECT_EQ(content.size(), 10);
    std::string captured(content.begin(), content.end());
    EXPECT_EQ(captured, "56789ABCDE");
}

TEST_F(AdvancedFileOperationTest, TrackerMaxFileSizeLimit) {
    FileTrackingOptions options;
    options.maxFileSize = 100;  // Only backup files <= 100 bytes
    
    FileOperationTracker tracker(options);
    
    // Create small file
    std::string smallPath = createTestFile("small.txt", "small content");
    auto smallContent = tracker.captureFileContent(smallPath);
    EXPECT_FALSE(smallContent.empty());
    
    // Create large file
    std::string largePath = testDir + "/large.txt";
    std::string largeContent(200, 'X');
    createTestFile("large.txt", largeContent);
    
    auto captured = tracker.captureFileContent(largePath);
    EXPECT_TRUE(captured.empty());  // Should not capture due to size limit
}

// ============================================================================
// ReverseExecutor Advanced Tests
// ============================================================================

TEST_F(AdvancedFileOperationTest, ReverseWithDiffs) {
    // Create original file
    std::string originalContent = "AAAA____BBBB____CCCC";
    std::string testPath = createTestFile("diffs.txt", "XXXX____YYYY____ZZZZ");
    
    // Create operation with diffs (not full backup)
    FileOperation op;
    op.operationId = 1;
    op.type = FileOperationType::WRITE;
    op.path = testPath;
    op.originalSize = 20;
    op.newSize = 20;
    op.isReversible = true;
    
    // Diff 1: XXXX -> AAAA at offset 0
    FileContentDiff diff1;
    diff1.offset = 0;
    diff1.oldData = {'A', 'A', 'A', 'A'};
    diff1.newData = {'X', 'X', 'X', 'X'};
    op.diffs.push_back(diff1);
    
    // Diff 2: YYYY -> BBBB at offset 8
    FileContentDiff diff2;
    diff2.offset = 8;
    diff2.oldData = {'B', 'B', 'B', 'B'};
    diff2.newData = {'Y', 'Y', 'Y', 'Y'};
    op.diffs.push_back(diff2);
    
    // Diff 3: ZZZZ -> CCCC at offset 16
    FileContentDiff diff3;
    diff3.offset = 16;
    diff3.oldData = {'C', 'C', 'C', 'C'};
    diff3.newData = {'Z', 'Z', 'Z', 'Z'};
    op.diffs.push_back(diff3);
    
    ReverseOptions opts;
    opts.createBackups = false;
    ReverseExecutor executor(opts);
    
    auto result = executor.reverseOperation(op);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(readFile(testPath), originalContent);
}

TEST_F(AdvancedFileOperationTest, ReverseAppend) {
    // Create file with appended content
    std::string testPath = createTestFile("append.txt", "Original contentAppended data");
    
    FileOperation op;
    op.operationId = 1;
    op.type = FileOperationType::APPEND;
    op.path = testPath;
    op.originalSize = 16;  // "Original content" length
    op.newSize = 29;       // Full length after append
    op.isReversible = true;
    
    ReverseOptions opts;
    opts.createBackups = false;
    ReverseExecutor executor(opts);
    
    auto result = executor.reverseOperation(op);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(readFile(testPath), "Original content");
}

TEST_F(AdvancedFileOperationTest, ReverseChmod) {
    std::string testPath = createTestFile("chmod.txt", "content");
    
    // Change permissions
    chmod(testPath.c_str(), 0644);
    
    FileOperation op;
    op.operationId = 1;
    op.type = FileOperationType::CHMOD;
    op.path = testPath;
    op.originalMode = 0755;
    op.newMode = 0644;
    op.isReversible = true;
    
    ReverseOptions opts;
    opts.createBackups = false;
    ReverseExecutor executor(opts);
    
    auto result = executor.reverseOperation(op);
    
    EXPECT_TRUE(result.success);
    
    struct stat st;
    stat(testPath.c_str(), &st);
    EXPECT_EQ(st.st_mode & 0777, 0755);
}

TEST_F(AdvancedFileOperationTest, ReverseMkdir) {
    std::string dirPath = testDir + "/newdir";
    std::filesystem::create_directory(dirPath);
    
    FileOperation op;
    op.operationId = 1;
    op.type = FileOperationType::MKDIR;
    op.path = dirPath;
    op.isReversible = true;
    
    ReverseOptions opts;
    opts.createBackups = false;
    ReverseExecutor executor(opts);
    
    auto result = executor.reverseOperation(op);
    
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(std::filesystem::exists(dirPath));
}

TEST_F(AdvancedFileOperationTest, ReverseRmdir) {
    std::string dirPath = testDir + "/removeddir";
    // Directory was removed, reverse should recreate it
    
    FileOperation op;
    op.operationId = 1;
    op.type = FileOperationType::RMDIR;
    op.path = dirPath;
    op.originalMode = 0755;
    op.isReversible = true;
    
    ReverseOptions opts;
    opts.createBackups = false;
    ReverseExecutor executor(opts);
    
    auto result = executor.reverseOperation(op);
    
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(std::filesystem::exists(dirPath));
    EXPECT_TRUE(std::filesystem::is_directory(dirPath));
}

TEST_F(AdvancedFileOperationTest, ReverseSymlink) {
    std::string linkPath = testDir + "/symlink";
    std::string targetPath = testDir + "/target.txt";
    
    createTestFile("target.txt", "target content");
    std::filesystem::create_symlink(targetPath, linkPath);
    
    EXPECT_TRUE(std::filesystem::is_symlink(linkPath));
    
    FileOperation op;
    op.operationId = 1;
    op.type = FileOperationType::SYMLINK;
    op.path = linkPath;
    op.isReversible = true;
    
    ReverseOptions opts;
    opts.createBackups = false;
    ReverseExecutor executor(opts);
    
    auto result = executor.reverseOperation(op);
    
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(std::filesystem::exists(linkPath));
}

TEST_F(AdvancedFileOperationTest, ReverseWithTypeFilter) {
    // Create test files
    std::string file1 = createTestFile("filter1.txt", "Modified 1");
    std::string file2 = createTestFile("filter2.txt", "Modified 2");
    
    std::vector<FileOperation> ops;
    
    // WRITE operation
    FileOperation op1;
    op1.operationId = 1;
    op1.type = FileOperationType::WRITE;
    op1.path = file1;
    op1.originalContent = std::vector<uint8_t>{'O', 'r', 'i', 'g', '1'};
    op1.isReversible = true;
    ops.push_back(op1);
    
    // CREATE operation (should be filtered out)
    FileOperation op2;
    op2.operationId = 2;
    op2.type = FileOperationType::CREATE;
    op2.path = file2;
    op2.isReversible = true;
    ops.push_back(op2);
    
    ReverseOptions opts;
    opts.createBackups = false;
    opts.includeTypes.push_back(FileOperationType::WRITE);  // Only reverse WRITE
    ReverseExecutor executor(opts);
    
    auto result = executor.reverseOperations(ops);
    
    EXPECT_EQ(result.successCount, 1);
    EXPECT_EQ(result.skippedCount, 1);
    
    // file1 should be reversed
    EXPECT_EQ(readFile(file1), "Orig1");
    // file2 should still exist (CREATE was not reversed)
    EXPECT_TRUE(std::filesystem::exists(file2));
}

TEST_F(AdvancedFileOperationTest, ReverseWithExcludeFilter) {
    std::string file1 = createTestFile("excl1.txt", "New content");
    
    std::vector<FileOperation> ops;
    
    FileOperation op1;
    op1.operationId = 1;
    op1.type = FileOperationType::WRITE;
    op1.path = file1;
    op1.originalContent = std::vector<uint8_t>{'O', 'l', 'd'};
    op1.isReversible = true;
    ops.push_back(op1);
    
    FileOperation op2;
    op2.operationId = 2;
    op2.type = FileOperationType::DELETE;
    op2.path = testDir + "/deleted.txt";
    op2.originalContent = std::vector<uint8_t>{'D', 'e', 'l'};
    op2.isReversible = true;
    ops.push_back(op2);
    
    ReverseOptions opts;
    opts.createBackups = false;
    opts.excludeTypes.push_back(FileOperationType::DELETE);  // Don't reverse DELETE
    ReverseExecutor executor(opts);
    
    auto result = executor.reverseOperations(ops);
    
    EXPECT_EQ(result.successCount, 1);
    EXPECT_EQ(result.skippedCount, 1);
}

TEST_F(AdvancedFileOperationTest, ReverseWithCustomFilter) {
    std::vector<FileOperation> ops;
    
    for (int i = 0; i < 10; ++i) {
        std::string path = createTestFile("custom" + std::to_string(i) + ".txt", "New");
        
        FileOperation op;
        op.operationId = i + 1;
        op.type = FileOperationType::WRITE;
        op.path = path;
        op.originalContent = std::vector<uint8_t>{'O', 'l', 'd'};
        op.originalSize = i * 100;  // Varying sizes
        op.isReversible = true;
        ops.push_back(op);
    }
    
    ReverseOptions opts;
    opts.createBackups = false;
    // Only reverse operations with originalSize >= 500
    opts.filter = [](const FileOperation& op) {
        return op.originalSize >= 500;
    };
    ReverseExecutor executor(opts);
    
    auto result = executor.reverseOperations(ops);
    
    EXPECT_EQ(result.successCount, 5);  // ops 5-9 have size >= 500
    EXPECT_EQ(result.skippedCount, 5);  // ops 0-4 have size < 500
}

TEST_F(AdvancedFileOperationTest, ReverseStopOnError) {
    std::string file1 = createTestFile("stop1.txt", "content");
    std::string file2 = "/nonexistent/path/file.txt";  // Will fail
    std::string file3 = createTestFile("stop3.txt", "content");
    
    std::vector<FileOperation> ops;
    
    FileOperation op1;
    op1.operationId = 1;
    op1.type = FileOperationType::WRITE;
    op1.path = file1;
    op1.originalContent = std::vector<uint8_t>{'A'};
    op1.isReversible = true;
    op1.timestamp = std::chrono::system_clock::now() - std::chrono::seconds(3);
    ops.push_back(op1);
    
    FileOperation op2;
    op2.operationId = 2;
    op2.type = FileOperationType::WRITE;
    op2.path = file2;  // Invalid path
    op2.originalContent = std::vector<uint8_t>{'B'};
    op2.isReversible = true;
    op2.timestamp = std::chrono::system_clock::now() - std::chrono::seconds(2);
    ops.push_back(op2);
    
    FileOperation op3;
    op3.operationId = 3;
    op3.type = FileOperationType::WRITE;
    op3.path = file3;
    op3.originalContent = std::vector<uint8_t>{'C'};
    op3.isReversible = true;
    op3.timestamp = std::chrono::system_clock::now() - std::chrono::seconds(1);
    ops.push_back(op3);
    
    ReverseOptions opts;
    opts.createBackups = false;
    opts.stopOnError = true;
    opts.reverseNewestFirst = true;
    ReverseExecutor executor(opts);
    
    auto result = executor.reverseOperations(ops);
    
    EXPECT_FALSE(result.allSucceeded);
    // Should stop after first error (op2), but op3 was processed first (newest)
    EXPECT_GE(result.failCount, 1);
}

TEST_F(AdvancedFileOperationTest, ReverseContinueOnError) {
    std::string file1 = createTestFile("cont1.txt", "content");
    std::string file2 = "/nonexistent/path/file.txt";
    std::string file3 = createTestFile("cont3.txt", "content");
    
    std::vector<FileOperation> ops;
    
    FileOperation op1;
    op1.operationId = 1;
    op1.type = FileOperationType::WRITE;
    op1.path = file1;
    op1.originalContent = std::vector<uint8_t>{'A'};
    op1.isReversible = true;
    op1.timestamp = std::chrono::system_clock::now() - std::chrono::seconds(3);
    ops.push_back(op1);
    
    FileOperation op2;
    op2.operationId = 2;
    op2.type = FileOperationType::WRITE;
    op2.path = file2;
    op2.originalContent = std::vector<uint8_t>{'B'};
    op2.isReversible = true;
    op2.timestamp = std::chrono::system_clock::now() - std::chrono::seconds(2);
    ops.push_back(op2);
    
    FileOperation op3;
    op3.operationId = 3;
    op3.type = FileOperationType::WRITE;
    op3.path = file3;
    op3.originalContent = std::vector<uint8_t>{'C'};
    op3.isReversible = true;
    op3.timestamp = std::chrono::system_clock::now() - std::chrono::seconds(1);
    ops.push_back(op3);
    
    ReverseOptions opts;
    opts.createBackups = false;
    opts.stopOnError = false;  // Continue on error
    ReverseExecutor executor(opts);
    
    auto result = executor.reverseOperations(ops);
    
    EXPECT_FALSE(result.allSucceeded);
    EXPECT_EQ(result.successCount, 2);
    EXPECT_EQ(result.failCount, 1);
}

TEST_F(AdvancedFileOperationTest, ReverseValidation) {
    FileOperation op;
    op.operationId = 1;
    op.type = FileOperationType::DELETE;
    op.path = testDir + "/missing.txt";  // File doesn't exist to restore to
    op.isReversible = true;
    // No backup data
    
    ReverseExecutor executor;
    auto issues = executor.validateReverse(op);
    
    EXPECT_FALSE(issues.empty());
    // Should complain about no backup data
    bool hasBackupIssue = false;
    for (const auto& issue : issues) {
        if (issue.find("backup") != std::string::npos) {
            hasBackupIssue = true;
            break;
        }
    }
    EXPECT_TRUE(hasBackupIssue);
}

TEST_F(AdvancedFileOperationTest, ReverseStatistics) {
    std::string file1 = createTestFile("stat1.txt", "new");
    std::string file2 = createTestFile("stat2.txt", "new");
    
    std::vector<FileOperation> ops;
    
    FileOperation op1;
    op1.operationId = 1;
    op1.type = FileOperationType::WRITE;
    op1.path = file1;
    op1.originalContent = std::vector<uint8_t>(100, 'A');
    op1.isReversible = true;
    ops.push_back(op1);
    
    FileOperation op2;
    op2.operationId = 2;
    op2.type = FileOperationType::WRITE;
    op2.path = file2;
    op2.originalContent = std::vector<uint8_t>(200, 'B');
    op2.isReversible = true;
    ops.push_back(op2);
    
    ReverseOptions opts;
    opts.createBackups = false;
    ReverseExecutor executor(opts);
    
    executor.reverseOperations(ops);
    
    EXPECT_EQ(executor.getTotalReversedCount(), 2);
    EXPECT_EQ(executor.getTotalBytesRestored(), 300);
}

TEST_F(AdvancedFileOperationTest, ReverseProgressCallback) {
    std::string file1 = createTestFile("prog1.txt", "new");
    std::string file2 = createTestFile("prog2.txt", "new");
    std::string file3 = createTestFile("prog3.txt", "new");
    
    std::vector<FileOperation> ops;
    for (int i = 1; i <= 3; ++i) {
        FileOperation op;
        op.operationId = i;
        op.type = FileOperationType::WRITE;
        op.path = testDir + "/prog" + std::to_string(i) + ".txt";
        op.originalContent = std::vector<uint8_t>{'O', 'l', 'd'};
        op.isReversible = true;
        ops.push_back(op);
    }
    
    ReverseOptions opts;
    opts.createBackups = false;
    ReverseExecutor executor(opts);
    
    std::vector<std::pair<size_t, size_t>> progressCalls;
    executor.setProgressCallback([&](size_t current, size_t total, const std::string&) {
        progressCalls.push_back({current, total});
    });
    
    executor.reverseOperations(ops);
    
    EXPECT_EQ(progressCalls.size(), 3);
    EXPECT_EQ(progressCalls[0].second, 3);  // Total is 3
    EXPECT_EQ(progressCalls[2].first, 3);   // Last call current is 3
}

// ============================================================================
// Binary File Tests
// ============================================================================

TEST_F(AdvancedFileOperationTest, ReverseBinaryFile) {
    std::string testPath = testDir + "/binary.bin";
    
    // Original binary content
    std::vector<uint8_t> originalData = {0x00, 0xFF, 0x7F, 0x80, 0x01, 0xFE};
    
    // Current (modified) content
    std::vector<uint8_t> modifiedData = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    createBinaryFile(testPath, modifiedData);
    
    FileOperation op;
    op.operationId = 1;
    op.type = FileOperationType::WRITE;
    op.path = testPath;
    op.originalContent = originalData;
    op.isReversible = true;
    
    ReverseOptions opts;
    opts.createBackups = false;
    ReverseExecutor executor(opts);
    
    auto result = executor.reverseOperation(op);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(readBinaryFile(testPath), originalData);
}

// ============================================================================
// Concurrent Access Tests
// ============================================================================

TEST_F(AdvancedFileOperationTest, ConcurrentLogAccess) {
    FileOperationLog log;
    
    // Simulate concurrent writes from multiple threads
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&log, t]() {
            for (int i = 0; i < 25; ++i) {
                FileOperation op;
                op.path = "/tmp/thread" + std::to_string(t) + "_file" + std::to_string(i);
                log.recordOperation(op);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(log.getOperationCount(), 100);
}
