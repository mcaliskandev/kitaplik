#ifndef KITAPLIK_HPP
#define KITAPLIK_HPP

#include <QFileSystemModel>
#include <QPoint>
#include <QStandardItemModel>
#include <QStringListModel>
#include <QWidget>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

enum class FileSortField
{
    Name,
    Size,
    Type,
    Modified,
    Created,
};

class FileSortProxyModel;

namespace Ui {
class Kitaplik;
}

class Kitaplik : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentPathChanged)

public:
    explicit Kitaplik(QWidget *parent = nullptr);
    ~Kitaplik() override;

    QString currentPath() const;

public slots:
    void setRootPath(const QString& path);
    void goHome();
    void goUp();
    void goBack();
    void goForward();

signals:
    void currentPathChanged(const QString& path);

private:
    void showFileMenu(const QPoint& viewPos);
    void onMenuOpen(const QString& targetPath);
    void onMenuRename(const QString& targetPath);
    void onMenuCopy(const QString& targetPath);
    void onMenuCut(const QString& targetPath);
    void onMenuDelete(const QString& targetPath);
    void onMenuNewFolder(const QString& parentDir);
    void onMenuPaste(const QString& destDir);

    void setCopyPasteProgressVisible(bool visible, const QString& text = QString());
    void updateCopyPasteProgress(std::uint64_t doneBytes, std::uint64_t totalBytes);
    void finishPasteOperation(const QString& errorText, bool clearClipboard);

    void updateGoToPathButton();
    void goToPathFromPathLabel();

    void navigateTo(const QString& path, bool recordHistory);
    void updateWindowTitle(const QString& path);
    void updateNavButtons();
    void applySort(FileSortField field, Qt::SortOrder order);
    void refreshHistoryView();
    void updateFileInfoView(const QModelIndex& index);
    void addPinnedFolder(const QString& label, const QString& path);
    QModelIndex mapToSourceIndex(const QModelIndex& proxyIndex) const;

    QFileSystemModel model;
    QStandardItemModel pinnedFoldersModel;
    std::vector<QString> history;
    int historyIndex = -1;
    std::unique_ptr<Ui::Kitaplik> ui;

    QStringListModel historyListModel;
    QStandardItemModel fileInfoModel;
    FileSortProxyModel* sortProxy = nullptr;
    FileSortField currentSortField = FileSortField::Name;
    Qt::SortOrder currentSortOrder = Qt::AscendingOrder;

    std::jthread fileOpThread;
    std::atomic_bool pasteInProgress = false;
    QString pasteOpLabel;
};

#endif // KITAPLIK_HPP
