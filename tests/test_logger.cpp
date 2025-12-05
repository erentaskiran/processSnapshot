#include <gtest/gtest.h>
#include "logger/operation_logger.hpp"
#include "utils/helpers.hpp"
#include <filesystem>
#include <thread>
#include <sstream>

using namespace checkpoint;

class LoggerTest : public ::testing::Test {
protected:
    std::filesystem::path testLogDir = "test_logs";
    
    void SetUp() override {
        std::filesystem::create_directories(testLogDir);
    }
    
    void TearDown() override {
        std::filesystem::remove_all(testLogDir);
    }
};

// LogEntry Tests
TEST_F(LoggerTest, LogEntryToString) {
    LogEntry entry;
    entry.level = LogLevel::Info;
    entry.category = "Test";
    entry.message = "Test message";
    entry.timestamp = utils::TimeUtils::now();
    
    std::string str = entry.toString();
    
    EXPECT_TRUE(str.find("[INFO]") != std::string::npos);
    EXPECT_TRUE(str.find("[Test]") != std::string::npos);
    EXPECT_TRUE(str.find("Test message") != std::string::npos);
}

TEST_F(LoggerTest, LogEntryWithFileInfo) {
    LogEntry entry;
    entry.level = LogLevel::Error;
    entry.category = "Error";
    entry.message = "Something went wrong";
    entry.file = "test.cpp";
    entry.line = 42;
    entry.timestamp = utils::TimeUtils::now();
    
    std::string str = entry.toString();
    
    EXPECT_TRUE(str.find("test.cpp:42") != std::string::npos);
}

// Formatter Tests
TEST_F(LoggerTest, TextFormatter) {
    TextLogFormatter formatter;
    
    LogEntry entry;
    entry.level = LogLevel::Warning;
    entry.category = "Format";
    entry.message = "Formatter test";
    entry.timestamp = utils::TimeUtils::now();
    
    std::string formatted = formatter.format(entry);
    
    EXPECT_TRUE(formatted.find("[WARNING]") != std::string::npos);
    EXPECT_TRUE(formatted.ends_with("\n"));
}

TEST_F(LoggerTest, JsonFormatter) {
    JsonLogFormatter formatter;
    
    LogEntry entry;
    entry.id = 123;
    entry.level = LogLevel::Debug;
    entry.category = "JSON";
    entry.message = "JSON test";
    entry.timestamp = utils::TimeUtils::now();
    
    std::string formatted = formatter.format(entry);
    
    EXPECT_TRUE(formatted.find("\"id\":123") != std::string::npos);
    EXPECT_TRUE(formatted.find("\"category\":\"JSON\"") != std::string::npos);
    EXPECT_TRUE(formatted.find("\"message\":\"JSON test\"") != std::string::npos);
}

// Output Tests
TEST_F(LoggerTest, FileOutput) {
    auto logPath = testLogDir / "test.log";
    
    {
        FileLogOutput output(logPath);
        output.write("Test log line 1\n");
        output.write("Test log line 2\n");
        output.flush();
    }
    
    EXPECT_TRUE(std::filesystem::exists(logPath));
    EXPECT_GT(std::filesystem::file_size(logPath), 0);
}

// OperationLogger Tests
TEST_F(LoggerTest, BasicLogging) {
    OperationLogger logger;
    logger.setMinLevel(LogLevel::Debug);
    
    logger.debug("Test", "Debug message");
    logger.info("Test", "Info message");
    logger.warning("Test", "Warning message");
    logger.error("Test", "Error message");
    
    auto entries = logger.getEntries(LogLevel::Debug, 100);
    EXPECT_GE(entries.size(), 4);
}

TEST_F(LoggerTest, LogLevelFiltering) {
    OperationLogger logger;
    logger.setMinLevel(LogLevel::Warning);
    
    logger.debug("Test", "Should not appear");
    logger.info("Test", "Should not appear");
    logger.warning("Test", "Should appear");
    logger.error("Test", "Should appear");
    
    auto entries = logger.getEntries(LogLevel::Debug, 100);
    // Sadece Warning ve üstü görünmeli
    for (const auto& entry : entries) {
        EXPECT_GE(static_cast<int>(entry.level), static_cast<int>(LogLevel::Warning));
    }
}

TEST_F(LoggerTest, LogOperation) {
    OperationLogger logger;
    
    auto opId = logger.logOperation(OperationType::Create, "Test operation", 123);
    
    EXPECT_GT(opId, 0);
    
    auto history = logger.getOperationHistory(10);
    EXPECT_GE(history.size(), 1);
    
    bool found = false;
    for (const auto& op : history) {
        if (op.id == opId) {
            EXPECT_EQ(op.type, OperationType::Create);
            EXPECT_EQ(op.description, "Test operation");
            EXPECT_EQ(op.relatedCheckpoint, 123);
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(LoggerTest, LogOperationComplete) {
    OperationLogger logger;
    
    auto opId = logger.logOperation(OperationType::Update, "Operation to complete");
    logger.logOperationComplete(opId, true);
    
    // Completion log yazılmış olmalı
    auto entries = logger.getEntries(LogLevel::Info, 10);
    bool foundCompletion = false;
    for (const auto& entry : entries) {
        if (entry.message.find("completed successfully") != std::string::npos) {
            foundCompletion = true;
            break;
        }
    }
    EXPECT_TRUE(foundCompletion);
}

TEST_F(LoggerTest, GetEntriesBetweenTimestamps) {
    OperationLogger logger;
    
    auto start = utils::TimeUtils::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    logger.info("Test", "Entry 1");
    logger.info("Test", "Entry 2");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto end = utils::TimeUtils::now();
    
    auto entries = logger.getEntriesBetween(start, end);
    EXPECT_GE(entries.size(), 2);
}

TEST_F(LoggerTest, GetOperationsSince) {
    OperationLogger logger;
    
    CheckpointId cpId = 1000;
    logger.logOperation(OperationType::Checkpoint, "Checkpoint created", cpId);
    logger.logOperation(OperationType::Update, "Update 1", cpId);
    logger.logOperation(OperationType::Update, "Update 2", cpId);
    
    auto operations = logger.getOperationsSince(cpId);
    EXPECT_GE(operations.size(), 3);
}

TEST_F(LoggerTest, ClearLogs) {
    OperationLogger logger;
    
    logger.info("Test", "Entry to clear");
    logger.logOperation(OperationType::Create, "Operation to clear");
    
    logger.clear();
    
    auto entries = logger.getEntries(LogLevel::Trace, 100);
    auto operations = logger.getOperationHistory(100);
    
    EXPECT_TRUE(entries.empty());
    EXPECT_TRUE(operations.empty());
}

// Macro Tests
TEST_F(LoggerTest, LogMacros) {
    auto& logger = OperationLogger::getInstance();
    logger.setMinLevel(LogLevel::Trace);
    logger.clear();
    
    LOG_TRACE("Macro", "Trace via macro");
    LOG_DEBUG("Macro", "Debug via macro");
    LOG_INFO("Macro", "Info via macro");
    LOG_WARNING("Macro", "Warning via macro");
    LOG_ERROR("Macro", "Error via macro");
    LOG_CRITICAL("Macro", "Critical via macro");
    
    auto entries = logger.getEntries(LogLevel::Trace, 100);
    EXPECT_GE(entries.size(), 6);
}

// File Rotation Tests
TEST_F(LoggerTest, FileRotation) {
    auto logPath = testLogDir / "rotation_test.log";
    
    {
        // Küçük max size ile rotation test et
        FileLogOutput output(logPath, 100, 3);
        
        for (int i = 0; i < 20; ++i) {
            output.write("This is a test log line that should trigger rotation eventually " + std::to_string(i) + "\n");
        }
        output.flush();
    }
    
    // Ana log dosyası var olmalı
    EXPECT_TRUE(std::filesystem::exists(logPath));
}

// Thread Safety Tests
TEST_F(LoggerTest, ConcurrentLogging) {
    OperationLogger logger;
    logger.setMinLevel(LogLevel::Info);
    
    std::vector<std::thread> threads;
    const int numThreads = 4;
    const int logsPerThread = 100;
    
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&logger, t, logsPerThread]() {
            for (int i = 0; i < logsPerThread; ++i) {
                logger.info("Thread" + std::to_string(t), 
                           "Message " + std::to_string(i));
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto entries = logger.getEntries(LogLevel::Info, numThreads * logsPerThread);
    EXPECT_EQ(entries.size(), numThreads * logsPerThread);
}
