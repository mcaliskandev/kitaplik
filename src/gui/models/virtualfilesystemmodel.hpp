#ifndef VIRTUALFILESYSTEMMODEL_HPP
#define VIRTUALFILESYSTEMMODEL_HPP

#include <QAbstractItemModel>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QCache>
#include <QIcon>
#include <QThread>
#include <QThreadPool>
#include <QFuture>
#include <QFutureWatcher>
#include <QtConcurrent>

#include <memory>
#include <unordered_map>
#include <atomic>
#include <optional>

namespace Kitaplik::GUI::Models {

/**
 * @brief Virtual file system item for lazy loading
 */
struct VirtualFileSystemItem {
    QString name;
    QString fullPath;
    bool isDirectory;
    bool isHidden;
    bool isSymlink;
    qint64 size;
    QDateTime lastModified;
    QDateTime created;
    QString permissions;
    QString mimeType;
    QIcon icon;
    
    // Lazy loading state
    enum class LoadState {
        NotLoaded,
        Loading,
        Loaded,
        Error
    } loadState = LoadState::NotLoaded;
    
    // Parent-child relationships
    VirtualFileSystemItem* parent = nullptr;
    std::vector<std::unique_ptr<VirtualFileSystemItem>> children;
    
    // Cached data for performance
    mutable bool sizeCalculated = false;
    mutable qint64 cachedSize = -1;
    
    explicit VirtualFileSystemItem(const QString& path, VirtualFileSystemItem* parent = nullptr);
    
    // Performance optimization methods
    qint64 calculateSize() const;
    void invalidateCache();
    void loadChildren(bool async = true);
    bool hasLoadedChildren() const;
};

/**
 * @brief High-performance virtual file system model with lazy loading
 * 
 * This model provides significant performance improvements over QFileSystemModel:
 * - Lazy loading of directory contents
 * - Async size calculation for large directories  
 * - Intelligent caching
 * - Background monitoring
 * - Pagination for very large directories
 */
class VirtualFileSystemModel : public QAbstractItemModel {
    Q_OBJECT

public:
    explicit VirtualFileSystemModel(QObject* parent = nullptr);
    ~VirtualFileSystemModel() override;

    // QAbstractItemModel interface
    QVariant data(const QModelIndex& index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    QModelIndex index(int row, int column, const QModelIndex& parent) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent) const override;
    int columnCount(const QModelIndex& parent) const override;
    
    // Enhanced navigation
    QModelIndex setRootPath(const QString& path);
    QString rootPath() const { return rootPath_; }
    QModelIndex rootIndex() const { return rootIndex_; }
    
    // Performance features
    void setLazyLoading(bool enabled) { lazyLoading_ = enabled; }
    bool lazyLoading() const { return lazyLoading_; }
    
    void setAsyncSizeCalculation(bool enabled) { asyncSizeCalculation_ = enabled; }
    bool asyncSizeCalculation() const { return asyncSizeCalculation_; }
    
    void setCacheSize(int size);
    int cacheSize() const { return cacheSize_; }
    
    void setPageSize(int size) { pageSize_ = size; }
    int pageSize() const { return pageSize_; }
    
    // Filtering and searching
    void setFilterPattern(const QString& pattern);
    QString filterPattern() const { return filterPattern_; }
    
    void setShowHidden(bool show);
    bool showHidden() const { return showHidden_; }
    
    // Advanced features
    void enableFileMonitoring(bool enabled);
    bool fileMonitoringEnabled() const { return fileMonitoring_; }
    
    void setThumbnailGeneration(bool enabled);
    bool thumbnailGeneration() const { return thumbnailGeneration_; }
    
    // Batch operations
    void prefetchDirectory(const QString& path);
    void clearCache();
    void clearThumbnailCache();

signals:
    void directoryLoaded(const QString& path);
    void sizeCalculated(const QString& path, qint64 size);
    void errorOccurred(const QString& path, const QString& error);
    void loadingStarted(const QString& path);
    void loadingFinished(const QString& path);

public slots:
    void refresh(const QModelIndex& index = QModelIndex());
    void refreshPath(const QString& path);
    void forceRefresh();

private slots:
    void handleDirectoryChanged(const QString& path);
    void handleFileChanged(const QString& path);
    void onSizeCalculationFinished();
    void onThumbnailGenerationFinished();

private:
    // Core data structure
    std::unique_ptr<VirtualFileSystemItem> rootItem_;
    QString rootPath_;
    QModelIndex rootIndex_;
    
    // Performance settings
    bool lazyLoading_ = true;
    bool asyncSizeCalculation_ = true;
    bool fileMonitoring_ = true;
    bool thumbnailGeneration_ = false;
    int cacheSize_ = 1000;
    int pageSize_ = 100;
    
    // Filtering
    QString filterPattern_;
    bool showHidden_ = false;
    
    // Caching
    QCache<QString, QIcon> iconCache_;
    QCache<QString, QPixmap> thumbnailCache_;
    std::unordered_map<QString, std::shared_ptr<VirtualFileSystemItem>> itemCache_;
    
    // Async operations
    QThreadPool* threadPool_;
    QFileSystemWatcher* fileWatcher_;
    QTimer* refreshTimer_;
    
    // Background operations tracking
    std::unordered_map<QString, QFutureWatcher<qint64>*> sizeCalculations_;
    std::unordered_map<QString, QFutureWatcher<QPixmap>*> thumbnailGenerations_;
    
    // Internal methods
    VirtualFileSystemItem* getItem(const QModelIndex& index) const;
    QModelIndex createIndex(VirtualFileSystemItem* item) const;
    
    void loadDirectoryContents(VirtualFileSystemItem* item);
    void calculateDirectorySize(VirtualFileSystemItem* item);
    void generateThumbnail(VirtualFileSystemItem* item);
    
    bool matchesFilter(const VirtualFileSystemItem* item) const;
    QIcon getFileIcon(const VirtualFileSystemItem* item) const;
    QString getDisplaySize(qint64 bytes) const;
    
    // Cache management
    void addToCache(const QString& path, std::shared_ptr<VirtualFileSystemItem> item);
    std::shared_ptr<VirtualFileSystemItem> getFromCache(const QString& path) const;
    void pruneCache();
    
    // Background operations
    void startSizeCalculation(const QString& path, VirtualFileSystemItem* item);
    void startThumbnailGeneration(const QString& path, VirtualFileSystemItem* item);
    
    // Monitoring
    void setupFileWatcher();
    void updateWatcherPaths();
    void handleFilesystemChange(const QString& path);
    
    // Utility methods
    QString getMimeType(const QString& filePath) const;
    bool isTextFile(const QString& mimeType) const;
    bool isImageFile(const QString& mimeType) const;
};

/**
 * @brief Background worker for directory scanning
 */
class DirectoryScanner : public QObject {
    Q_OBJECT

public:
    struct ScanResult {
        QString path;
        std::vector<std::unique_ptr<VirtualFileSystemItem>> items;
        QString error;
        qint64 totalSize;
    };

    explicit DirectoryScanner(QObject* parent = nullptr);

    QFuture<ScanResult> scanDirectory(const QString& path, bool includeHidden, const QString& filter = QString());
    QFuture<qint64> calculateSize(const QString& path);
    QFuture<QPixmap> generateThumbnail(const QString& path, const QSize& size = QSize(128, 128));

signals:
    void scanProgress(const QString& path, int processed, int total);
    void sizeProgress(const QString& path, qint64 calculated, qint64 total);

private:
    QThreadPool* threadPool_;
    
    std::vector<std::unique_ptr<VirtualFileSystemItem>> scanDirectorySync(
        const QString& path, 
        bool includeHidden, 
        const QString& filter
    );
    
    qint64 calculateSizeSync(const QString& path);
    QPixmap generateThumbnailSync(const QString& path, const QSize& size);
    
    bool shouldIncludeFile(const QFileInfo& fileInfo, bool includeHidden, const QString& filter) const;
    std::unique_ptr<VirtualFileSystemItem> createItemFromFileInfo(const QFileInfo& fileInfo) const;
};

/**
 * @brief Performance monitoring and statistics
 */
class PerformanceMonitor : public QObject {
    Q_OBJECT

public:
    struct Statistics {
        qint64 directoriesScanned = 0;
        qint64 filesScanned = 0;
        qint64 totalBytesProcessed = 0;
        double averageScanTime = 0.0;
        qint64 cacheHits = 0;
        qint64 cacheMisses = 0;
        qint64 activeOperations = 0;
    };

    static PerformanceMonitor* instance();

    void recordDirectoryScan(const QString& path, double timeMs, int fileCount);
    void recordCacheHit();
    void recordCacheMiss();
    void recordOperationStart();
    void recordOperationEnd();
    
    Statistics getStatistics() const;
    void resetStatistics();

signals:
    void statisticsUpdated(const Statistics& stats);

private:
    explicit PerformanceMonitor(QObject* parent = nullptr);
    static PerformanceMonitor* instance_;
    
    Statistics stats_;
    QMutex statsMutex_;
    QElapsedTimer scanTimer_;
};

} // namespace Kitaplik::GUI::Models

#endif // VIRTUALFILESYSTEMMODEL_HPP