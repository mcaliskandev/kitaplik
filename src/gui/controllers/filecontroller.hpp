#ifndef FILECONTROLLER_HPP
#define FILECONTROLLER_HPP

#include <QObject>
#include <QString>
#include <QStringList>
#include <QFuture>
#include <QTimer>
#include <QClipboard>

#include "../models/filemodel.hpp"
#include "../../core/operations/fileoperations.hpp"
#include "../../core/commands/filecommand.hpp"

namespace Kitaplik::GUI::Controllers {

/**
 * @brief Controller for file operations - coordinates between models and views
 */
class FileController : public QObject {
    Q_OBJECT

public:
    explicit FileController(QObject* parent = nullptr);
    ~FileController() override;

    // Model access
    void setFileModel(Models::FileModel* model);
    void setSortProxyModel(Models::FileSortProxyModel* sortModel);
    void setPinnedFoldersModel(Models::PinnedFoldersModel* pinnedModel);
    void setHistoryModel(Models::NavigationHistoryModel* historyModel);
    void setFileInfoModel(Models::FileInfoModel* infoModel);

    // Navigation
    Q_INVOKABLE bool navigateToPath(const QString& path);
    Q_INVOKABLE void goHome();
    Q_INVOKABLE void goUp();
    Q_INVOKABLE void goBack();
    Q_INVOKABLE void goForward();
    Q_INVOKABLE void refresh();

    // File operations
    Q_INVOKABLE void copyFiles(const QStringList& paths);
    Q_INVOKABLE void cutFiles(const QStringList& paths);
    Q_INVOKABLE void pasteFiles(const QString& destinationPath = QString());
    Q_INVOKABLE void deleteFiles(const QStringList& paths);
    Q_INVOKABLE void renameFile(const QString& oldPath, const QString& newName);
    Q_INVOKABLE void createFolder(const QString& parentPath, const QString& folderName);

    // Selection and context
    Q_INVOKABLE void setSelection(const QStringList& paths);
    Q_INVOKABLE QStringList getSelection() const;
    Q_INVOKABLE void clearSelection();

    // Pinned folders
    Q_INVOKABLE bool addPinnedFolder(const QString& label, const QString& path);
    Q_INVOKABLE bool removePinnedFolder(const QString& path);
    Q_INVOKABLE QStringList getPinnedFolders() const;

    // Search and filtering
    Q_INVOKABLE void setFilterPattern(const QString& pattern);
    Q_INVOKABLE void setSortField(int field);
    Q_INVOKABLE void setSortOrder(bool ascending);

    // Clipboard operations
    Q_INVOKABLE void copyToClipboard(const QStringList& paths);
    Q_INVOKABLE void cutToClipboard(const QStringList& paths);
    Q_INVOKABLE QStringList getClipboardContents() const;
    Q_INVOKABLE bool clipboardHasCutOperation() const;

    // Command pattern support
    Q_INVOKABLE void executeCommand(std::unique_ptr<Kitaplik::Core::FileCommand> command);
    Q_INVOKABLE bool canUndo() const;
    Q_INVOKABLE bool canRedo() const;
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void clearCommandHistory();

    // Properties
    QString currentPath() const;
    bool isOperationRunning() const;
    int operationProgress() const;

signals:
    void navigationStarted(const QString& path);
    void navigationCompleted(const QString& path, bool success);
    void navigationFailed(const QString& path, const QString& error);

    void operationStarted(const QString& operation);
    void operationProgressChanged(int percent);
    void operationCompleted(const QString& operation, bool success);
    void operationFailed(const QString& operation, const QString& error);

    void selectionChanged(const QStringList& selectedPaths);
    void fileInfoChanged(const QString& path);

    void clipboardChanged();
    void pinnedFoldersChanged();

    void undoAvailabilityChanged(bool canUndo);
    void redoAvailabilityChanged(bool canRedo);

    void errorOccurred(const QString& error, const QString& context);

public slots:
    void handleSelectionChange(const QStringList& selectedPaths);
    void handlePathChange(const QString& newPath);
    void handleOperationProgress(uint64_t done, uint64_t total);
    void handleValidationError(const QString& path, const QString& error);

private slots:
    void processOperationQueue();
    void updateOperationProgress();

private:
    // Models
    Models::FileModel* fileModel_ = nullptr;
    Models::FileSortProxyModel* sortProxyModel_ = nullptr;
    Models::PinnedFoldersModel* pinnedFoldersModel_ = nullptr;
    Models::NavigationHistoryModel* historyModel_ = nullptr;
    Models::FileInfoModel* fileInfoModel_ = nullptr;

    // State
    QString currentPath_;
    QStringList selectedPaths_;
    QStringList clipboardPaths_;
    bool clipboardIsCut_ = false;

    // Operation management
    struct PendingOperation {
        QString type;
        QStringList parameters;
        QString destination;
        std::function<void(Kitaplik::Core::OperationResult)> callback;
    };
    QList<PendingOperation> operationQueue_;
    QTimer* operationTimer_;
    bool operationInProgress_ = false;
    int currentOperationProgress_ = 0;

    // Command history
    std::vector<std::unique_ptr<Kitaplik::Core::FileCommand>> commandHistory_;
    std::vector<std::unique_ptr<Kitaplik::Core::FileCommand>> redoStack_;
    size_t maxCommandHistory_ = 50;

    // Helper methods
    bool validatePath(const QString& path) const;
    QString sanitizePath(const QString& path) const;
    void updateModels();
    void emitError(const QString& error, const QString& context = QString());
    
    // Operation helpers
    void enqueueOperation(const QString& type, const QStringList& params, 
                         const QString& dest = QString(),
                         std::function<void(Kitaplik::Core::OperationResult)> callback = nullptr);
    void startNextOperation();
    void finishCurrentOperation(bool success, const QString& error = QString());
    
    // Command helpers
    void addToCommandHistory(std::unique_ptr<Kitaplik::Core::FileCommand> command);
    void trimCommandHistory();
};

/**
 * @brief Controller for application settings and configuration
 */
class SettingsController : public QObject {
    Q_OBJECT

public:
    explicit SettingsController(QObject* parent = nullptr);

    // View settings
    void setShowHiddenFiles(bool show);
    bool showHiddenFiles() const;

    void setShowSystemFiles(bool show);
    bool showSystemFiles() const;

    void setFolderSizeDisplay(bool show);
    bool folderSizeDisplay() const;

    void setTheme(const QString& theme);
    QString theme() const;

    // Behavior settings
    void setDoubleClickToExecute(bool enabled);
    bool doubleClickToExecute() const;

    void setConfirmDelete(bool confirm);
    bool confirmDelete() const;

    void setDefaultViewMode(const QString& mode);
    QString defaultViewMode() const;

    // Security settings
    void setFollowSymlinks(bool follow);
    bool followSymlinks() const;

    void setAllowPathTraversal(bool allow);
    bool allowPathTraversal() const;

    // Performance settings
    void setAsyncOperations(bool async);
    bool asyncOperations() const;

    void setMaxConcurrentOperations(int max);
    int maxConcurrentOperations() const;

signals:
    void settingChanged(const QString& key, const QVariant& value);
    void settingsLoaded();

public slots:
    void loadSettings();
    void saveSettings();
    void resetToDefaults();

private:
    QMap<QString, QVariant> settings_;
    QString settingsFile_;

    QVariant getSetting(const QString& key, const QVariant& defaultValue = QVariant()) const;
    void setSetting(const QString& key, const QVariant& value);
};

} // namespace Kitaplik::GUI::Controllers

#endif // FILECONTROLLER_HPP