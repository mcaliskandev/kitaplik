#ifndef FILECOMMAND_HPP
#define FILECOMMAND_HPP

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "../errors/fileerror.hpp"
#include "../operations/fileoperations.hpp"

namespace Kitaplik::Core {

/**
 * @brief Base class for file operations commands
 */
class FileCommand {
public:
    virtual ~FileCommand() = default;
    
    /**
     * @brief Execute the command
     * @return Result of the operation
     */
    virtual OperationResult execute() = 0;
    
    /**
     * @brief Undo the command
     * @return Result of the undo operation
     */
    virtual OperationResult undo() = 0;
    
    /**
     * @brief Get a description of the command
     * @return Command description
     */
    virtual std::string description() const = 0;
    
    /**
     * @brief Check if the command can be undone
     * @return true if undoable, false otherwise
     */
    virtual bool isUndoable() const = 0;
    
    /**
     * @brief Get the command ID
     * @return Unique command identifier
     */
    virtual uint64_t id() const = 0;
    
    /**
     * @brief Set progress callback for the command
     * @param callback Progress callback function
     */
    virtual void setProgressCallback(std::function<void(uint64_t, uint64_t)> callback) = 0;
};

/**
 * @brief Command for copying files
 */
class CopyCommand : public FileCommand {
private:
    std::vector<std::string> sourcePaths_;
    std::string destinationPath_;
    std::vector<std::string> copiedPaths_;  // Tracks what was actually copied for undo
    std::function<void(uint64_t, uint64_t)> progressCallback_;
    uint64_t id_;
    bool followSymlinks_;
    bool overwriteExisting_;
    
public:
    CopyCommand(const std::vector<std::string>& sources, 
                const std::string& destination,
                bool followSymlinks = false,
                bool overwrite = false);
    
    OperationResult execute() override;
    OperationResult undo() override;
    std::string description() const override;
    bool isUndoable() const override { return true; }
    uint64_t id() const override { return id_; }
    void setProgressCallback(std::function<void(uint64_t, uint64_t)> callback) override;
    
private:
    static std::atomic<uint64_t> nextId_;
};

/**
 * @brief Command for moving files
 */
class MoveCommand : public FileCommand {
private:
    std::vector<std::string> sourcePaths_;
    std::string destinationPath_;
    std::vector<std::pair<std::string, std::string>> movedPaths_;  // original -> moved
    std::function<void(uint64_t, uint64_t)> progressCallback_;
    uint64_t id_;
    bool followSymlinks_;
    bool overwriteExisting_;
    
public:
    MoveCommand(const std::vector<std::string>& sources,
                const std::string& destination,
                bool followSymlinks = false,
                bool overwrite = false);
    
    OperationResult execute() override;
    OperationResult undo() override;
    std::string description() const override;
    bool isUndoable() const override { return true; }
    uint64_t id() const override { return id_; }
    void setProgressCallback(std::function<void(uint64_t, uint64_t)> callback) override;
    
private:
    static std::atomic<uint64_t> nextId_;
};

/**
 * @brief Command for deleting files
 */
class DeleteCommand : public FileCommand {
private:
    std::vector<std::string> paths_;
    std::string backupDirectory_;  // Where files are moved before deletion
    std::vector<std::string> backedUpPaths_;  // Original paths for restore
    std::function<void(uint64_t, uint64_t)> progressCallback_;
    uint64_t id_;
    
public:
    explicit DeleteCommand(const std::vector<std::string>& paths);
    
    OperationResult execute() override;
    OperationResult undo() override;
    std::string description() const override;
    bool isUndoable() const override { return true; }
    uint64_t id() const override { return id_; }
    void setProgressCallback(std::function<void(uint64_t, uint64_t)> callback) override;
    
private:
    static std::atomic<uint64_t> nextId_;
    std::string createBackupDirectory();
    OperationResult moveToBackup(const std::string& path);
    OperationResult restoreFromBackup(const std::string& path);
};

/**
 * @brief Command for creating directories
 */
class CreateDirectoryCommand : public FileCommand {
private:
    std::string path_;
    bool createParents_;
    std::function<void(uint64_t, uint64_t)> progressCallback_;
    uint64_t id_;
    bool wasCreated_;
    
public:
    CreateDirectoryCommand(const std::string& path, bool createParents = true);
    
    OperationResult execute() override;
    OperationResult undo() override;
    std::string description() const override;
    bool isUndoable() const override { return true; }
    uint64_t id() const override { return id_; }
    void setProgressCallback(std::function<void(uint64_t, uint64_t)> callback) override;
    
private:
    static std::atomic<uint64_t> nextId_;
};

/**
 * @brief Command for renaming files/directories
 */
class RenameCommand : public FileCommand {
private:
    std::string oldPath_;
    std::string newPath_;
    std::function<void(uint64_t, uint64_t)> progressCallback_;
    uint64_t id_;
    bool wasRenamed_;
    
public:
    RenameCommand(const std::string& oldPath, const std::string& newPath);
    
    OperationResult execute() override;
    OperationResult undo() override;
    std::string description() const override;
    bool isUndoable() const override { return true; }
    uint64_t id() const override { return id_; }
    void setProgressCallback(std::function<void(uint64_t, uint64_t)> callback) override;
    
private:
    static std::atomic<uint64_t> nextId_;
};

/**
 * @brief Macro command for executing multiple commands as one unit
 */
class MacroCommand : public FileCommand {
private:
    std::vector<std::unique_ptr<FileCommand>> commands_;
    std::function<void(uint64_t, uint64_t)> progressCallback_;
    uint64_t id_;
    std::string description_;
    
public:
    explicit MacroCommand(const std::string& description = "Macro Command");
    
    void addCommand(std::unique_ptr<FileCommand> command);
    
    OperationResult execute() override;
    OperationResult undo() override;
    std::string description() const override;
    bool isUndoable() const override;
    uint64_t id() const override { return id_; }
    void setProgressCallback(std::function<void(uint64_t, uint64_t)> callback) override;
    
    size_t commandCount() const { return commands_.size(); }
    
private:
    static std::atomic<uint64_t> nextId_;
};

/**
 * @brief Factory for creating file commands
 */
class FileCommandFactory {
public:
    static std::unique_ptr<FileCommand> createCopyCommand(
        const std::vector<std::string>& sources,
        const std::string& destination,
        bool followSymlinks = false,
        bool overwrite = false);
    
    static std::unique_ptr<FileCommand> createMoveCommand(
        const std::vector<std::string>& sources,
        const std::string& destination,
        bool followSymlinks = false,
        bool overwrite = false);
    
    static std::unique_ptr<FileCommand> createDeleteCommand(
        const std::vector<std::string>& paths);
    
    static std::unique_ptr<FileCommand> createCreateDirectoryCommand(
        const std::string& path,
        bool createParents = true);
    
    static std::unique_ptr<FileCommand> createRenameCommand(
        const std::string& oldPath,
        const std::string& newPath);
    
    static std::unique_ptr<MacroCommand> createMacroCommand(
        const std::string& description = "Macro Command");
};

} // namespace Kitaplik::Core

#endif // FILECOMMAND_HPP