#pragma once

#include "core/types.hpp"
#include <stdexcept>
#include <string>

namespace checkpoint {

// Base exception class
class CheckpointException : public std::runtime_error {
protected:
    ErrorCode m_errorCode;

public:
    explicit CheckpointException(const std::string& message, ErrorCode code = ErrorCode::Unknown)
        : std::runtime_error(message), m_errorCode(code) {}

    ErrorCode errorCode() const noexcept { return m_errorCode; }
};

// Checkpoint bulunamadı hatası
class CheckpointNotFoundException : public CheckpointException {
public:
    explicit CheckpointNotFoundException(CheckpointId id)
        : CheckpointException("Checkpoint not found: " + std::to_string(id), 
                             ErrorCode::CheckpointNotFound) {}
};

// Checkpoint bozuk hatası
class CheckpointCorruptedException : public CheckpointException {
public:
    explicit CheckpointCorruptedException(CheckpointId id)
        : CheckpointException("Checkpoint corrupted: " + std::to_string(id), 
                             ErrorCode::CheckpointCorrupted) {}
};

// Geri alma hatası
class RollbackException : public CheckpointException {
public:
    explicit RollbackException(const std::string& reason)
        : CheckpointException("Rollback failed: " + reason, 
                             ErrorCode::RollbackFailed) {}
};

// Geçersiz durum hatası
class InvalidStateException : public CheckpointException {
public:
    explicit InvalidStateException(const std::string& details)
        : CheckpointException("Invalid state: " + details, 
                             ErrorCode::InvalidState) {}
};

// Serileştirme hatası
class SerializationException : public CheckpointException {
public:
    explicit SerializationException(const std::string& details)
        : CheckpointException("Serialization error: " + details, 
                             ErrorCode::SerializationError) {}
};

// I/O hatası
class IOErrorException : public CheckpointException {
public:
    explicit IOErrorException(const std::string& details)
        : CheckpointException("I/O error: " + details, 
                             ErrorCode::IOError) {}
};

} // namespace checkpoint
