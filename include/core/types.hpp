#pragma once

#include <cstdint>
#include <string>
#include <chrono>
#include <vector>
#include <memory>
#include <optional>
#include <variant>
#include <functional>

namespace checkpoint {

// Zaman türleri
using Timestamp = std::chrono::system_clock::time_point;
using Duration = std::chrono::milliseconds;

// Benzersiz tanımlayıcılar
using CheckpointId = uint64_t;
using OperationId = uint64_t;
using SessionId = uint64_t;

// Durum verileri için genel tür
using StateData = std::vector<uint8_t>;

// Durum enum'ları
enum class CheckpointStatus {
    Pending,
    Active,
    Committed,
    RolledBack,
    Corrupted,
    Deleted
};

enum class OperationType {
    Create,
    Update,
    Delete,
    Checkpoint,
    Rollback,
    Custom
};

enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Critical
};

enum class RollbackStrategy {
    Full,           // Tam geri yükleme
    Partial,        // Kısmi geri yükleme
    Incremental,    // Artımlı geri yükleme
    Selective       // Seçici geri yükleme
};

// Hata kodları
enum class ErrorCode {
    Success = 0,
    CheckpointNotFound,
    CheckpointCorrupted,
    RollbackFailed,
    InvalidState,
    SerializationError,
    DeserializationError,
    IOError,
    PermissionDenied,
    OutOfMemory,
    Timeout,
    Unknown
};

// Result tipi - hata yönetimi için
template<typename T>
struct Result {
    std::optional<T> value;
    ErrorCode error;
    std::string message;

    bool isSuccess() const { return error == ErrorCode::Success; }
    bool isError() const { return error != ErrorCode::Success; }

    static Result<T> success(T val) {
        return {std::move(val), ErrorCode::Success, ""};
    }

    static Result<T> failure(ErrorCode err, const std::string& msg = "") {
        return {std::nullopt, err, msg};
    }
};

// Void için özelleştirilmiş Result
template<>
struct Result<void> {
    ErrorCode error;
    std::string message;

    bool isSuccess() const { return error == ErrorCode::Success; }
    bool isError() const { return error != ErrorCode::Success; }

    static Result<void> success() {
        return {ErrorCode::Success, ""};
    }

    static Result<void> failure(ErrorCode err, const std::string& msg = "") {
        return {err, msg};
    }
};

// Callback türleri
using ProgressCallback = std::function<void(double progress, const std::string& status)>;
using ErrorCallback = std::function<void(ErrorCode error, const std::string& message)>;

} // namespace checkpoint
