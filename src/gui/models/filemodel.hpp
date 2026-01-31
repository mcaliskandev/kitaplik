#ifndef FILEMODEL_HPP
#define FILEMODEL_HPP

#include <QFileSystemModel>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTimer>
#include <QFuture>
#include <QFutureWatcher>
#include <QtConcurrent>

#include "../core/operations/fileoperations.hpp"
#include "../core/pathvalidator.hpp"

namespace Kitaplik::GUI::Models {

/**
 * @brief Enhanced file system model with async operations and security
 */
class FileModel : public QFileSystemModel {
    Q_OBJECT

public:
    explicit FileModel(QObject* parent = nullptr);
    ~FileModel() override;

    // Enhanced navigation with validation
    bool setRootPathSafe(const QString& path);
    QString currentPath() const { return rootPath(); }

    // Async operations
    QFuture<Kitaplik::Core::OperationResult> copyFilesAsync(
        const QStringList& sources,
        const QString& destination,
        bool overwrite = false);

    QFuture<Kitaplik::Core::OperationResult> moveFilesAsync(
        const QStringList& sources,
        const QString& destination,
        bool overwrite = false);

    QFuture<Kitaplik::Core::OperationResult> deleteFilesAsync(
        const QStringList& paths);

    QFuture<Kitaplik::Core::OperationResult> createDirectoryAsync(
        const QString& path,
        bool createParents = true);

    // File information
    QFuture<Kitaplik::Core::Result<uint64_t>> calculateSizeAsync(
        const QStringList& paths);

    // Security validation
    bool isPathSafe(const QString& path) const;
    QString sanitizePath(const QString& path) const;

    // Enhanced filtering
    void setFilterHiddenFiles(bool hide);
    void setFilterSystemFiles(bool hide);
    void setPatternFilter(const QString& pattern);

signals:
    void operationProgress(uint64_t done, uint64_t total);
    void operationCompleted(const QString& operation, bool success);
    void validationError(const QString& path, const QString& error);

private slots:
    void handleOperationProgress(uint64_t done, uint64_t total);
    void handleOperationCompleted();

private:
    void setupFilters();
    bool validatePath(const QString& path) const;

    bool hideHiddenFiles_ = true;
    bool hideSystemFiles_ = true;
    QString patternFilter_;
    
    // Operation tracking
    QMap<QString, QFutureWatcher<Kitaplik::Core::OperationResult>*> operationWatchers_;
};

/**
 * @brief Sort proxy model with enhanced sorting capabilities
 */
class FileSortProxyModel : public QSortFilterProxyModel {
    Q_OBJECT

public:
    enum class SortField {
        Name,
        Size,
        Type,
        Modified,
        Created,
        Permissions
    };
    Q_ENUM(SortField)

    explicit FileSortProxyModel(QObject* parent = nullptr);

    void setSortField(SortField field);
    SortField sortField() const { return sortField_; }

    void setFoldersFirst(bool foldersFirst);
    bool foldersFirst() const { return foldersFirst_; }

    void setCaseSensitive(bool caseSensitive);
    bool caseSensitive() const { return caseSensitive_; }

    void setNaturalSort(bool naturalSort);
    bool naturalSort() const { return naturalSort_; }

protected:
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    SortField sortField_ = SortField::Name;
    bool foldersFirst_ = true;
    bool caseSensitive_ = false;
    bool naturalSort_ = true;
    QString filterPattern_;

    bool isFolder(const QModelIndex& index) const;
    QString getFileExtension(const QString& filename) const;
    bool naturalCompare(const QString& s1, const QString& s2) const;
};

/**
 * @brief Model for pinned folders/bookmarks
 */
class PinnedFoldersModel : public QStandardItemModel {
    Q_OBJECT

public:
    explicit PinnedFoldersModel(QObject* parent = nullptr);

    // Pinned folder management
    bool addPinnedFolder(const QString& label, const QString& path);
    bool removePinnedFolder(const QString& path);
    bool updatePinnedFolder(const QString& oldPath, const QString& newLabel, const QString& newPath);

    QStringList getPinnedPaths() const;
    QString getPathForLabel(const QString& label) const;
    QString getLabelForPath(const QString& path) const;

    // Validation
    bool validatePinnedFolder(const QString& path) const;
    void refreshPinnedFolders();  // Check if folders still exist

signals:
    void pinnedFolderAdded(const QString& label, const QString& path);
    void pinnedFolderRemoved(const QString& path);
    void pinnedFolderUpdated(const QString& oldPath, const QString& newLabel, const QString& newPath);

private:
    static constexpr int LabelRole = Qt::UserRole + 1;
    static constexpr int PathRole = Qt::UserRole + 2;
    static constexpr int ValidRole = Qt::UserRole + 3;

    void setupDefaultPinnedFolders();
    bool isFolderValid(const QString& path) const;
};

/**
 * @brief Model for navigation history
 */
class NavigationHistoryModel : public QStandardItemModel {
    Q_OBJECT

public:
    explicit NavigationHistoryModel(QObject* parent = nullptr);

    // History management
    void addToHistory(const QString& path);
    QString goBack();
    QString goForward();
    void clearHistory();

    bool canGoBack() const;
    bool canGoForward() const;

    QString currentPath() const;
    QStringList getHistory() const;

    void setMaxHistorySize(int maxSize);
    int maxHistorySize() const { return maxHistorySize_; }

signals:
    void historyChanged();
    void currentPathChanged(const QString& path);

private:
    static constexpr int PathRole = Qt::UserRole + 1;
    static constexpr int TimestampRole = Qt::UserRole + 2;

    int maxHistorySize_ = 50;
    int currentIndex_ = -1;

    void truncateHistory();
    void updateCurrentIndex();
};

/**
 * @brief Model for file information display
 */
class FileInfoModel : public QStandardItemModel {
    Q_OBJECT

public:
    explicit FileInfoModel(QObject* parent = nullptr);

    void setFileInfo(const QFileInfo& fileInfo);
    void clearFileInfo();

    // Formatted information
    QString getFormattedSize() const;
    QString getFormattedDate(const QDateTime& dateTime) const;
    QString getFileTypeDescription() const;
    QString getPermissionsString() const;

signals:
    void fileInfoUpdated();

private:
    QFileInfo currentFileInfo_;
    void updateFileInfo();
};

} // namespace Kitaplik::GUI::Models

#endif // FILEMODEL_HPP