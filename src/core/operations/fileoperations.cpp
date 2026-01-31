#include "fileoperations.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <chrono>

namespace Kitaplik::Core {

// Static member definitions
std::unique_ptr<FileOperations::OperationManager> FileOperations::manager_;
std::mutex FileOperations::managerMutex_;

FileOperations::OperationManager* FileOperations::getManager() {
    std::lock_guard<std::mutex> lock(managerMutex_);
    if (!manager_) {
        manager_ = std::make_unique<OperationManager>();
    }
    return manager_.get();
}

std::future<OperationResult> FileOperations::copyFilesAsync(
    const std::vector<std::string>& sources,
    const std::string& destination,
    ProgressCallback callback,
    bool followSymlinks,
    bool overwrite) {
    
    auto promise = std::make_shared<std::promise<OperationResult>>();
    auto future = promise->get_future();
    
    auto manager = getManager();
    uint64_t operationId = manager->startOperation([=](std::atomic<bool>& cancelled, std::atomic<bool>& completed) {
        OperationResult result;
        
        try {
            // Validate destination
            auto destValidation = PathValidator::validatePath(destination);
            if (!destValidation.isSuccess()) {
                promise->set_value(OperationResult(FileError::InvalidPath, "Invalid destination", destValidation.context()));
                return;
            }
            
            std::filesystem::path destPath(destValidation.value());
            if (!std::filesystem::exists(destPath)) {
                std::filesystem::create_directories(destPath);
            } else if (!std::filesystem::is_directory(destPath)) {
                promise->set_value(OperationResult(FileError::DestinationExists, "Destination is not a directory"));
                return;
            }
            
            // Calculate total size for progress
            uint64_t totalSize = 0;
            for (const auto& source : sources) {
                auto validation = PathValidator::validatePath(source);
                if (!validation.isSuccess()) {
                    promise->set_value(OperationResult(FileError::InvalidPath, "Invalid source", validation.context()));
                    return;
                }
                
                std::filesystem::path srcPath(validation.value());
                if (!std::filesystem::exists(srcPath)) {
                    promise->set_value(OperationResult(FileError::PathNotFound, "Source not found", source));
                    return;
                }
                
                // Calculate size (simplified - in real implementation would recurse)
                if (std::filesystem::is_regular_file(srcPath)) {
                    totalSize += std::filesystem::file_size(srcPath);
                }
            }
            
            uint64_t doneSize = 0;
            
            // Copy each file
            for (const auto& source : sources) {
                if (cancelled.load()) {
                    promise->set_value(OperationResult(FileError::OperationFailed, "Operation cancelled"));
                    return;
                }
                
                auto validation = PathValidator::validatePath(source);
                std::filesystem::path srcPath(validation.value());
                std::filesystem::path finalDest = destPath / srcPath.filename();
                
                try {
                    if (std::filesystem::is_directory(srcPath)) {
                        std::filesystem::create_directories(finalDest);
                        // In real implementation, would copy directory contents recursively
                    } else {
                        if (std::filesystem::exists(finalDest) && !overwrite) {
                            promise->set_value(OperationResult(FileError::DestinationExists, "File already exists", finalDest.string()));
                            return;
                        }
                        
                        std::filesystem::copy_file(srcPath, finalDest, std::filesystem::copy_options::overwrite_existing);
                        doneSize += std::filesystem::file_size(srcPath);
                    }
                    
                    if (callback) {
                        callback(doneSize, totalSize);
                    }
                } catch (const std::exception& e) {
                    promise->set_value(OperationResult(FileError::OperationFailed, "Copy failed", e.what()));
                    return;
                }
            }
            
            promise->set_value(OperationResult::successResult());
            
        } catch (const std::exception& e) {
            promise->set_value(OperationResult(FileError::UnknownError, "Unexpected error", e.what()));
        }
    });
    
    return future;
}

std::future<OperationResult> FileOperations::moveFilesAsync(
    const std::vector<std::string>& sources,
    const std::string& destination,
    ProgressCallback callback,
    bool followSymlinks,
    bool overwrite) {
    
    auto promise = std::make_shared<std::promise<OperationResult>>();
    auto future = promise->get_future();
    
    auto manager = getManager();
    uint64_t operationId = manager->startOperation([=](std::atomic<bool>& cancelled, std::atomic<bool>& completed) {
        // For move operations, we can try rename first, then fallback to copy+delete
        auto copyFuture = copyFilesAsync(sources, destination, callback, followSymlinks, overwrite);
        auto copyResult = copyFuture.get();
        
        if (!copyResult.success) {
            promise->set_value(copyResult);
            return;
        }
        
        // Delete original files if copy was successful
        for (const auto& source : sources) {
            if (cancelled.load()) {
                promise->set_value(OperationResult(FileError::OperationFailed, "Operation cancelled"));
                return;
            }
            
            try {
                auto validation = PathValidator::validatePath(source);
                if (validation.isSuccess()) {
                    std::filesystem::remove_all(validation.value());
                }
            } catch (const std::exception& e) {
                // Log error but continue - cleanup can be handled separately
            }
        }
        
        promise->set_value(OperationResult::successResult());
    });
    
    return future;
}

std::future<OperationResult> FileOperations::deleteFilesAsync(
    const std::vector<std::string>& paths,
    ProgressCallback callback) {
    
    auto promise = std::make_shared<std::promise<OperationResult>>();
    auto future = promise->get_future();
    
    auto manager = getManager();
    uint64_t operationId = manager->startOperation([=](std::atomic<bool>& cancelled, std::atomic<bool>& completed) {
        try {
            uint64_t totalPaths = paths.size();
            uint64_t donePaths = 0;
            
            for (const auto& path : paths) {
                if (cancelled.load()) {
                    promise->set_value(OperationResult(FileError::OperationFailed, "Operation cancelled"));
                    return;
                }
                
                auto validation = PathValidator::validatePath(path);
                if (!validation.isSuccess()) {
                    promise->set_value(OperationResult(FileError::InvalidPath, "Invalid path", validation.context()));
                    return;
                }
                
                try {
                    std::filesystem::remove_all(validation.value());
                    donePaths++;
                    
                    if (callback) {
                        callback(donePaths, totalPaths);
                    }
                } catch (const std::exception& e) {
                    promise->set_value(OperationResult(FileError::OperationFailed, "Delete failed", e.what()));
                    return;
                }
            }
            
            promise->set_value(OperationResult::successResult());
            
        } catch (const std::exception& e) {
            promise->set_value(OperationResult(FileError::UnknownError, "Unexpected error", e.what()));
        }
    });
    
    return future;
}

std::future<OperationResult> FileOperations::createDirectoryAsync(
    const std::string& path,
    bool createParents) {
    
    auto promise = std::make_shared<std::promise<OperationResult>>();
    auto future = promise->get_future();
    
    auto manager = getManager();
    uint64_t operationId = manager->startOperation([=](std::atomic<bool>& cancelled, std::atomic<bool>& completed) {
        try {
            auto validation = PathValidator::validatePath(path);
            if (!validation.isSuccess()) {
                promise->set_value(OperationResult(FileError::InvalidPath, "Invalid path", validation.context()));
                return;
            }
            
            std::filesystem::path dirPath(validation.value());
            
            if (std::filesystem::exists(dirPath)) {
                if (std::filesystem::is_directory(dirPath)) {
                    promise->set_value(OperationResult::successResult());
                } else {
                    promise->set_value(OperationResult(FileError::DestinationExists, "Path exists and is not a directory"));
                }
                return;
            }
            
            if (createParents) {
                std::filesystem::create_directories(dirPath);
            } else {
                std::filesystem::create_directory(dirPath);
            }
            
            promise->set_value(OperationResult::successResult());
            
        } catch (const std::exception& e) {
            promise->set_value(OperationResult(FileError::OperationFailed, "Create directory failed", e.what()));
        }
    });
    
    return future;
}

std::future<Result<uint64_t>> FileOperations::calculateSizeAsync(
    const std::vector<std::string>& paths,
    ProgressCallback callback) {
    
    auto promise = std::make_shared<std::promise<Result<uint64_t>>>();
    auto future = promise->get_future();
    
    auto manager = getManager();
    uint64_t operationId = manager->startOperation([=](std::atomic<bool>& cancelled, std::atomic<bool>& completed) {
        try {
            uint64_t totalSize = 0;
            uint64_t totalPaths = paths.size();
            uint64_t processedPaths = 0;
            
            for (const auto& path : paths) {
                if (cancelled.load()) {
                    promise->set_value(Result<uint64_t>(FileError::OperationFailed, "Operation cancelled"));
                    return;
                }
                
                auto validation = PathValidator::validatePath(path);
                if (!validation.isSuccess()) {
                    promise->set_value(Result<uint64_t>(FileError::InvalidPath, "Invalid path", validation.context()));
                    return;
                }
                
                try {
                    std::filesystem::path fsPath(validation.value());
                    if (std::filesystem::is_regular_file(fsPath)) {
                        totalSize += std::filesystem::file_size(fsPath);
                    } else if (std::filesystem::is_directory(fsPath)) {
                        // Simplified directory size calculation
                        for (const auto& entry : std::filesystem::recursive_directory_iterator(fsPath)) {
                            if (cancelled.load()) {
                                promise->set_value(Result<uint64_t>(FileError::OperationFailed, "Operation cancelled"));
                                return;
                            }
                            
                            if (std::filesystem::is_regular_file(entry.path())) {
                                totalSize += std::filesystem::file_size(entry.path());
                            }
                        }
                    }
                    
                    processedPaths++;
                    if (callback) {
                        callback(processedPaths, totalPaths);
                    }
                    
                } catch (const std::exception& e) {
                    promise->set_value(Result<uint64_t>(FileError::OperationFailed, "Size calculation failed", e.what()));
                    return;
                }
            }
            
            promise->set_value(Result<uint64_t>(totalSize));
            
        } catch (const std::exception& e) {
            promise->set_value(Result<uint64_t>(FileError::UnknownError, "Unexpected error", e.what()));
        }
    });
    
    return future;
}

bool FileOperations::cancelOperation(uint64_t operationId) {
    auto manager = getManager();
    return manager->cancelOperation(operationId);
}

bool FileOperations::isOperationRunning(uint64_t operationId) {
    auto manager = getManager();
    return manager->isOperationRunning(operationId);
}

size_t FileOperations::getRunningOperationCount() {
    auto manager = getManager();
    return manager->getRunningOperationCount();
}

// OperationManager implementation
uint64_t FileOperations::OperationManager::startOperation(std::function<void(std::atomic<bool>&, std::atomic<bool>&)> task) {
    std::lock_guard<std::mutex> lock(operationsMutex_);
    
    uint64_t id = nextId_++;
    auto operation = std::make_unique<Operation>(id);
    
    operation->thread = std::thread([this, id, task = std::move(task)]() {
        auto& op = operations_[id];
        task(op->cancelled, op->completed);
        op->completed.store(true);
    });
    
    operations_[id] = std::move(operation);
    return id;
}

bool FileOperations::OperationManager::cancelOperation(uint64_t id) {
    std::lock_guard<std::mutex> lock(operationsMutex_);
    
    auto it = operations_.find(id);
    if (it != operations_.end()) {
        it->second->cancelled.store(true);
        return true;
    }
    return false;
}

bool FileOperations::OperationManager::isOperationRunning(uint64_t id) {
    std::lock_guard<std::mutex> lock(operationsMutex_);
    
    auto it = operations_.find(id);
    if (it != operations_.end()) {
        return !it->second->completed.load();
    }
    return false;
}

size_t FileOperations::OperationManager::getRunningOperationCount() {
    std::lock_guard<std::mutex> lock(operationsMutex_);
    
    size_t count = 0;
    for (const auto& [id, op] : operations_) {
        if (!op->completed.load()) {
            count++;
        }
    }
    return count;
}

void FileOperations::OperationManager::waitForCompletion(uint64_t id) {
    std::unique_lock<std::mutex> lock(operationsMutex_);
    
    auto it = operations_.find(id);
    if (it != operations_.end()) {
        auto& op = it->second;
        lock.unlock();
        
        if (op->thread.joinable()) {
            op->thread.join();
        }
        
        lock.lock();
        operations_.erase(it);
    }
}

void FileOperations::OperationManager::cleanupCompletedOperations() {
    std::lock_guard<std::mutex> lock(operationsMutex_);
    
    auto it = operations_.begin();
    while (it != operations_.end()) {
        if (it->second->completed.load()) {
            if (it->second->thread.joinable()) {
                it->second->thread.join();
            }
            it = operations_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace Kitaplik::Core