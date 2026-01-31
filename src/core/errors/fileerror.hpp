#ifndef FILEERROR_HPP
#define FILEERROR_HPP

#include <string>
#include <variant>
#include <stdexcept>
#include <vector>

namespace Kitaplik::Core {

/**
 * @brief Enumeration of possible file operation errors
 */
enum class FileError {
    NoError,
    PermissionDenied,
    PathNotFound,
    DestinationExists,
    InvalidPath,
    OperationFailed,
    CrossDeviceMove,
    DiskFull,
    ReadOnlyFileSystem,
    SymlinkNotAllowed,
    UnknownError
};

/**
 * @brief Exception class for file operations
 */
class FileException : public std::runtime_error {
private:
    FileError error_;
    std::string context_;
    std::string detailedMessage_;

public:
    explicit FileException(FileError error, const std::string& context = std::string(), const std::string& detailedMessage = std::string())
        : std::runtime_error(formatError(error, context, detailedMessage))
        , error_(error)
        , context_(context)
        , detailedMessage_(detailedMessage) {}

    FileError error() const { return error_; }
    const std::string& context() const { return context_; }
    const std::string& detailedMessage() const { return detailedMessage_; }

private:
    static std::string formatError(FileError error, const std::string& context, const std::string& detailedMessage) {
        std::string baseError;
        switch (error) {
        case FileError::NoError:
            baseError = "No error";
            break;
        case FileError::PermissionDenied:
            baseError = "Permission denied";
            break;
        case FileError::PathNotFound:
            baseError = "Path not found";
            break;
        case FileError::DestinationExists:
            baseError = "Destination already exists";
            break;
        case FileError::InvalidPath:
            baseError = "Invalid path";
            break;
        case FileError::OperationFailed:
            baseError = "Operation failed";
            break;
        case FileError::CrossDeviceMove:
            baseError = "Cannot move across devices";
            break;
        case FileError::DiskFull:
            baseError = "Disk full";
            break;
        case FileError::ReadOnlyFileSystem:
            baseError = "Read-only file system";
            break;
        case FileError::SymlinkNotAllowed:
            baseError = "Symbolic links not allowed";
            break;
        case FileError::UnknownError:
            baseError = "Unknown error";
            break;
        }

        std::string result = baseError;
        if (!context.empty()) {
            result += ": " + context;
        }
        if (!detailedMessage.empty()) {
            result += " (" + detailedMessage + ")";
        }
        return result;
    }
};

/**
 * @brief Result type for operations that can fail
 */
template<typename T>
class Result {
private:
    std::variant<T, FileError> value_;
    std::string context_;
    std::string detailedMessage_;

public:
    Result(T value) : value_(std::move(value)) {}
    Result(FileError error, const std::string& context = std::string(), const std::string& detailedMessage = std::string())
        : value_(error), context_(context), detailedMessage_(detailedMessage) {}

    bool isSuccess() const { return std::holds_alternative<T>(value_); }
    bool isError() const { return std::holds_alternative<FileError>(value_); }

    const T& value() const {
        if (isError()) {
            throw FileException(error(), context_, detailedMessage_);
        }
        return std::get<T>(value_);
    }

    T& value() {
        if (isError()) {
            throw FileException(error(), context_, detailedMessage_);
        }
        return std::get<T>(value_);
    }

    FileError error() const {
        if (isSuccess()) {
            return FileError::NoError;
        }
        return std::get<FileError>(value_);
    }

    const std::string& context() const { return context_; }
    const std::string& detailedMessage() const { return detailedMessage_; }

    // Convenience methods
    explicit operator bool() const { return isSuccess(); }
    T operator*() const { return value(); }
    T* operator->() { return &value(); }
};

} // namespace Kitaplik::Core

#endif // FILEERROR_HPP