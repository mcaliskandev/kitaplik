#ifndef FILEOPERATIONS_HPP
#define FILEOPERATIONS_HPP

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <future>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "../errors/fileerror.hpp"
#include "../pathvalidator.hpp"

namespace Kitaplik::Core {

/**
 * @brief Progress callback type for file operations
 */
using ProgressCallback = std::function<void(uint64_t done, uint64_t total)>;

/**
 * @brief Result structure for file operations
 */
struct OperationResult {
    bool success;
    FileError error;
    std::string errorMessage;
    std::string details;
    
    OperationResult() : success(true), error(FileError::NoError) {}
    OperationResult(FileError err, const std::string& message = std::string(), const std::string& det = std::string())
        : success(false), error(err), errorMessage(message), details(det) {}
    
    static OperationResult successResult() { return OperationResult(); }
};

/**
 * @brief Information about a file operation
 */
struct OperationInfo {
    std::vector<std::string> sourcePaths;
    std::string destinationPath;
    bool isMove;
    bool followSymlinks;
    bool overwriteExisting;
    
    OperationInfo() : isMove(false), followSymlinks(false), overwriteExisting(false) {}
};

/**
 * @brief Handles asynchronous file operations with progress reporting
 */
class FileOperations {
public:
    /**
     * @brief Copy files asynchronously
     * @param sources Source file paths
     * @param destination Destination directory
     * @param callback Progress callback (optional)
     * @param followSymlinks Whether to follow symbolic links
     * @param overwrite Whether to overwrite existing files
     * @return Future containing operation result
     */
    static std::future<OperationResult> copyFilesAsync(
        const std::vector<std::string>& sources,
        const std::string& destination,
        ProgressCallback callback = nullptr,
        bool followSymlinks = false,
        bool overwrite = false
    );

    /**
     * @brief Move files asynchronously
     * @param sources Source file paths
     * @param destination Destination directory
     * @param callback Progress callback (optional)
     * @param followSymlinks Whether to follow symbolic links
     * @param overwrite Whether to overwrite existing files
     * @return Future containing operation result
     */
    static std::future<OperationResult> moveFilesAsync(
        const std::vector<std::string>& sources,
        const std::string& destination,
        ProgressCallback callback = nullptr,
        bool followSymlinks = false,
        bool overwrite = false
    );

    /**
     * @brief Delete files asynchronously
     * @param paths File paths to delete
     * @param callback Progress callback (optional)
     * @return Future containing operation result
     */
    static std::future<OperationResult> deleteFilesAsync(
        const std::vector<std::string>& paths,
        ProgressCallback callback = nullptr
    );

    /**
     * @brief Create directory asynchronously
     * @param path Directory path to create
     * @param createParents Whether to create parent directories
     * @return Future containing operation result
     */
    static std::future<OperationResult> createDirectoryAsync(
        const std::string& path,
        bool createParents = true
    );

    /**
     * @brief Calculate total size of files/directories
     * @param paths Paths to calculate size for
     * @param callback Progress callback (optional)
     * @return Future containing size in bytes
     */
    static std::future<Result<uint64_t>> calculateSizeAsync(
        const std::vector<std::string>& paths,
        ProgressCallback callback = nullptr
    );

    /**
     * @brief Cancel an ongoing operation
     * @param operationId ID of the operation to cancel
     * @return true if operation was cancelled, false if not found or already completed
     */
    static bool cancelOperation(uint64_t operationId);

    /**
     * @brief Check if an operation is running
     * @param operationId ID of the operation
     * @return true if operation is running
     */
    static bool isOperationRunning(uint64_t operationId);

    /**
     * @brief Get the number of currently running operations
     * @return Number of running operations
     */
    static size_t getRunningOperationCount();

private:
    class OperationManager;
    static std::unique_ptr<OperationManager> manager_;
    static std::mutex managerMutex_;
    
    static OperationManager* getManager();
};

/**
 * @brief Internal operation manager for tracking and canceling operations
 */
class FileOperations::OperationManager {
private:
    struct Operation {
        uint64_t id;
        std::atomic<bool> cancelled;
        std::atomic<bool> completed;
        std::thread thread;
        std::mutex mutex;
        std::condition_variable condition;
        
        Operation(uint64_t id) : id(id), cancelled(false), completed(false) {}
    };
    
    std::unordered_map<uint64_t, std::unique_ptr<Operation>> operations_;
    std::mutex operationsMutex_;
    std::atomic<uint64_t> nextId_{1};
    
public:
    uint64_t startOperation(std::function<void(std::atomic<bool>&, std::atomic<bool>&)> task);
    bool cancelOperation(uint64_t id);
    bool isOperationRunning(uint64_t id);
    size_t getRunningOperationCount();
    void waitForCompletion(uint64_t id);
    
private:
    void cleanupCompletedOperations();
};

} // namespace Kitaplik::Core

#endif // FILEOPERATIONS_HPP