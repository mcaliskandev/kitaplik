#include "kitaplik.hpp"

#include <QAction>
#include <QActionGroup>
#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QStringList>
#include <QLocale>
#include <QMenu>
#include <QListView>
#include <QToolButton>
#include <QMessageBox>
#include <QMimeData>
#include <QMetaObject>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QResource>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QTimer>
#include <QUrl>

#include "ui_kitaplik.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <optional>

namespace {

class FileItemDelegate final : public QStyledItemDelegate
{
public:
    explicit FileItemDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);

        const auto* fsModel = qobject_cast<const QFileSystemModel*>(index.model());
        const bool isDir = fsModel ? fsModel->isDir(index) : false;
        if (isDir)
            opt.palette.setColor(QPalette::Text, QColor("#4fc3f7"));

        QStyledItemDelegate::paint(painter, opt, index);
    }
};


QString cleanPath(const QString& path)
{
    if (path.trimmed().isEmpty())
        return QDir::homePath();

    const QString trimmed = path.trimmed();
    if (trimmed == "~")
        return QDir::homePath();
    if (trimmed.startsWith("~/"))
        return QDir::homePath() + trimmed.sliced(1);

    return QDir(path).absolutePath();
}

constexpr int PinnedPathRole = Qt::UserRole + 1;
constexpr const char* ClipboardCutMimeType = "application/x-kitaplik-cut";

bool removeRecursively(const QString& path, QString* error)
{
    const QFileInfo info(path);
    if (!info.exists())
        return true;

    if (info.isDir()) {
        QDir dir(path);
        if (!dir.removeRecursively()) {
            if (error)
                *error = QString("Failed to delete directory: %1").arg(path);
            return false;
        }
        return true;
    }

    if (!QFile::remove(path)) {
        if (error)
            *error = QString("Failed to delete file: %1").arg(path);
        return false;
    }
    return true;
}

enum class ConflictChoice
{
    Replace,
    Skip,
    KeepBoth,
    Cancel,
};

using ConflictResolver = std::function<ConflictChoice(const QString& sourcePath, const QString& destinationPath, bool isDirectory)>;

QString makeUniqueKeepBothPath(const QString& destinationPath)
{
    const QFileInfo destinationInfo(destinationPath);
    const QDir parentDir = destinationInfo.dir();

    QString baseName;
    QString suffix;
    if (destinationInfo.isDir()) {
        baseName = destinationInfo.fileName();
    } else {
        baseName = destinationInfo.completeBaseName();
        suffix = destinationInfo.completeSuffix();
        if (!suffix.trimmed().isEmpty())
            suffix.prepend('.');
    }

    if (baseName.trimmed().isEmpty())
        baseName = destinationInfo.fileName();

    for (int i = 1; i <= 10000; ++i) {
        const QString candidateName = i == 1
            ? QString("%1 (copy)%2").arg(baseName, suffix)
            : QString("%1 (copy %2)%3").arg(baseName, QString::number(i), suffix);
        const QString candidatePath = parentDir.filePath(candidateName);
        if (!QFileInfo::exists(candidatePath))
            return candidatePath;
    }

    return parentDir.filePath(QString("%1 (%2)%3").arg(baseName, QString::number(QDateTime::currentMSecsSinceEpoch()), suffix));
}

std::optional<std::uint64_t> totalBytesForPath(const QString& path, QString* error)
{
    const QFileInfo info(path);
    if (!info.exists())
        return 0;

    if (info.isFile() || info.isSymLink())
        return static_cast<std::uint64_t>(info.size());

    if (!info.isDir()) {
        if (error)
            *error = QString("Unsupported file type: %1").arg(path);
        return std::nullopt;
    }

    std::uint64_t total = 0;
    QDir dir(path);
    const QFileInfoList entries = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
    for (const QFileInfo& entry : entries) {
        QString childError;
        const auto child = totalBytesForPath(entry.absoluteFilePath(), &childError);
        if (!child.has_value()) {
            if (error)
                *error = childError;
            return std::nullopt;
        }
        total += *child;
    }
    return total;
}

bool advanceProgressByPathSize(const QString& path,
                               std::uint64_t* doneBytes,
                               std::uint64_t totalBytes,
                               const std::function<void(std::uint64_t, std::uint64_t)>& onProgress,
                               QString* error)
{
    QString sizeError;
    const auto size = totalBytesForPath(path, &sizeError);
    if (!size.has_value()) {
        if (error)
            *error = sizeError;
        return false;
    }

    if (doneBytes)
        *doneBytes += *size;

    if (totalBytes > 0)
        onProgress(*doneBytes, totalBytes);

    return true;
}

bool copyFileWithProgress(const QString& srcPath,
                          QString destPath,
                          std::uint64_t* doneBytes,
                          std::uint64_t totalBytes,
                          const std::function<void(std::uint64_t, std::uint64_t)>& onProgress,
                          const ConflictResolver& resolveConflict,
                          bool* cancelledByUser,
                          QString* error)
{
    if (cancelledByUser)
        *cancelledByUser = false;

    if (QFileInfo::exists(destPath)) {
        const ConflictChoice choice = resolveConflict(srcPath, destPath, false);
        if (choice == ConflictChoice::Cancel) {
            if (cancelledByUser)
                *cancelledByUser = true;
            if (error)
                *error = QStringLiteral("Operation cancelled.");
            return false;
        }
        if (choice == ConflictChoice::Skip)
            return advanceProgressByPathSize(srcPath, doneBytes, totalBytes, onProgress, error);
        if (choice == ConflictChoice::KeepBoth)
            destPath = makeUniqueKeepBothPath(destPath);
        if (choice == ConflictChoice::Replace) {
            QString rmError;
            if (!removeRecursively(destPath, &rmError)) {
                if (error)
                    *error = rmError.isEmpty() ? QString("Failed to replace destination: %1").arg(destPath) : rmError;
                return false;
            }
        }
    }

    QFile src(srcPath);
    if (!src.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QString("Failed to open source: %1").arg(srcPath);
        return false;
    }

    QFile dst(destPath);
    if (!dst.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        if (error)
            *error = QString("Failed to create destination: %1").arg(destPath);
        return false;
    }

    QByteArray buffer;
    buffer.resize(1024 * 1024);
    while (!src.atEnd()) {
        const qint64 n = src.read(buffer.data(), buffer.size());
        if (n < 0) {
            if (error)
                *error = QString("Read error: %1").arg(srcPath);
            return false;
        }
        if (n == 0)
            break;
        if (dst.write(buffer.constData(), n) != n) {
            if (error)
                *error = QString("Write error: %1").arg(destPath);
            return false;
        }
        if (doneBytes) {
            *doneBytes += static_cast<std::uint64_t>(n);
            onProgress(*doneBytes, totalBytes);
        }
    }
    dst.flush();
    return true;
}

bool copyRecursivelyWithProgress(const QString& sourcePath,
                                 QString destPath,
                                 std::uint64_t* doneBytes,
                                 std::uint64_t totalBytes,
                                 const std::function<void(std::uint64_t, std::uint64_t)>& onProgress,
                                 const ConflictResolver& resolveConflict,
                                 bool* cancelledByUser,
                                 QString* error)
{
    if (cancelledByUser)
        *cancelledByUser = false;

    const QFileInfo srcInfo(sourcePath);
    if (!srcInfo.exists()) {
        if (error)
            *error = QString("Missing source: %1").arg(sourcePath);
        return false;
    }

    if (srcInfo.isDir()) {
        if (QFileInfo::exists(destPath)) {
            const ConflictChoice choice = resolveConflict(sourcePath, destPath, true);
            if (choice == ConflictChoice::Cancel) {
                if (cancelledByUser)
                    *cancelledByUser = true;
                if (error)
                    *error = QStringLiteral("Operation cancelled.");
                return false;
            }
            if (choice == ConflictChoice::Skip)
                return advanceProgressByPathSize(sourcePath, doneBytes, totalBytes, onProgress, error);
            if (choice == ConflictChoice::KeepBoth)
                destPath = makeUniqueKeepBothPath(destPath);
            if (choice == ConflictChoice::Replace) {
                QString rmError;
                if (!removeRecursively(destPath, &rmError)) {
                    if (error)
                        *error = rmError.isEmpty() ? QString("Failed to replace destination: %1").arg(destPath) : rmError;
                    return false;
                }
            }
        }

        if (!QFileInfo::exists(destPath)) {
            QDir parent = QFileInfo(destPath).dir();
            const QString name = QFileInfo(destPath).fileName();
            if (!parent.mkdir(name)) {
                if (error)
                    *error = QString("Failed to create directory: %1").arg(destPath);
                return false;
            }
        } else if (!QFileInfo(destPath).isDir()) {
            if (error)
                *error = QString("Destination exists and isn't a directory: %1").arg(destPath);
            return false;
        }

        QDir srcDir(sourcePath);
        const QFileInfoList entries =
            srcDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries, QDir::Name | QDir::DirsFirst);
        for (const QFileInfo& entry : entries) {
            const QString srcChild = entry.absoluteFilePath();
            const QString destChild = QDir(destPath).filePath(entry.fileName());
            if (!copyRecursivelyWithProgress(srcChild,
                                             destChild,
                                             doneBytes,
                                             totalBytes,
                                             onProgress,
                                             resolveConflict,
                                             cancelledByUser,
                                             error))
                return false;
        }
        return true;
    }

    return copyFileWithProgress(sourcePath,
                                destPath,
                                doneBytes,
                                totalBytes,
                                onProgress,
                                resolveConflict,
                                cancelledByUser,
                                error);
}

} // namespace

class FileSortProxyModel : public QSortFilterProxyModel
{
public:
    explicit FileSortProxyModel(QObject* parent = nullptr)
        : QSortFilterProxyModel(parent)
    {
        setDynamicSortFilter(true);
    }

    void setSortField(FileSortField field)
    {
        sortField_ = field;
    }

protected:
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override
    {
        const auto* fsModel = qobject_cast<const QFileSystemModel*>(sourceModel());
        if (!fsModel)
            return QSortFilterProxyModel::lessThan(left, right);

        const QFileInfo leftInfo = fsModel->fileInfo(left);
        const QFileInfo rightInfo = fsModel->fileInfo(right);

        switch (sortField_) {
        case FileSortField::Name:
            return leftInfo.fileName().toLower() < rightInfo.fileName().toLower();
        case FileSortField::Size:
            return leftInfo.size() < rightInfo.size();
        case FileSortField::Type: {
            const QString leftType = fsModel->type(left);
            const QString rightType = fsModel->type(right);
            return leftType.toLower() < rightType.toLower();
        }
        case FileSortField::Modified:
            return leftInfo.lastModified() < rightInfo.lastModified();
        case FileSortField::Created:
            return leftInfo.birthTime() < rightInfo.birthTime();
        }

        return QSortFilterProxyModel::lessThan(left, right);
    }

private:
    FileSortField sortField_ = FileSortField::Name;
};

Kitaplik::Kitaplik(QWidget* parent)
    : QWidget(parent)
{
    ui = std::make_unique<Ui::Kitaplik>();
    ui->setupUi(this);
    setCopyPasteProgressVisible(false);
    ui->pathLabel->setReadOnly(false);
    ui->pathLabel->setFocusPolicy(Qt::ClickFocus);
    ui->btn_go_to_path->setEnabled(false);
    ui->btn_go_to_path->setIcon(QIcon(":/src/ui/icons/go_to_path.png"));
    ui->btn_go_to_path->setToolTip("Go to path");

    sortProxy = new FileSortProxyModel(this);
    sortProxy->setSourceModel(&model);
    ui->treeView->setModel(sortProxy);
    ui->treeView->setItemDelegate(new FileItemDelegate(ui->treeView));
    ui->treeView->setSortingEnabled(true);
    ui->treeView->header()->setSortIndicatorShown(true);
    ui->treeView->sortByColumn(0, Qt::AscendingOrder);
    for (int col = 1; col < model.columnCount(); ++col)
        ui->treeView->setColumnHidden(col, true);
    ui->treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->treeView->setRootIsDecorated(false);
    ui->treeView->setItemsExpandable(false);
    ui->treeView->setExpandsOnDoubleClick(false);

    QMenu* sortMenu = new QMenu(this);
    QActionGroup* fieldGroup = new QActionGroup(sortMenu);
    fieldGroup->setExclusive(true);
    struct SortOption {
        const char* label;
        FileSortField field;
    };
    constexpr SortOption sortOptions[] = {
        {"Name", FileSortField::Name},
        {"Size", FileSortField::Size},
        {"Type", FileSortField::Type},
        {"Modified Date", FileSortField::Modified},
        {"Created Date", FileSortField::Created},
    };
    for (const SortOption& option : sortOptions) {
        QAction* action = sortMenu->addAction(option.label);
        action->setCheckable(true);
        if (option.field == currentSortField)
            action->setChecked(true);
        fieldGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, option] {
            applySort(option.field, currentSortOrder);
        });
    }
    sortMenu->addSeparator();
    QActionGroup* orderGroup = new QActionGroup(sortMenu);
    orderGroup->setExclusive(true);
    QAction* ascOrder = sortMenu->addAction("Ascending");
    QAction* descOrder = sortMenu->addAction("Descending");
    ascOrder->setCheckable(true);
    descOrder->setCheckable(true);
    orderGroup->addAction(ascOrder);
    orderGroup->addAction(descOrder);
    ascOrder->setChecked(true);
    connect(ascOrder, &QAction::triggered, this, [this] { applySort(currentSortField, Qt::AscendingOrder); });
    connect(descOrder, &QAction::triggered, this, [this] { applySort(currentSortField, Qt::DescendingOrder); });
    ui->sortButton->setMenu(sortMenu);
    ui->sortButton->setPopupMode(QToolButton::InstantPopup);
    ui->sortButton->setIcon(QIcon::fromTheme("view-sort-ascending"));
    applySort(currentSortField, currentSortOrder);

    ui->lastHistoryView->setModel(&historyListModel);
    ui->lastHistoryView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->lastHistoryView->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(ui->lastHistoryView, &QListView::doubleClicked, this, [this](const QModelIndex& idx) {
        if (!idx.isValid())
            return;
        const QString path = idx.data(Qt::DisplayRole).toString();
        if (path.trimmed().isEmpty())
            return;
        setRootPath(path);
    });

    ui->fileInfoView->setModel(&fileInfoModel);
    fileInfoModel.setColumnCount(2);
    fileInfoModel.setHeaderData(0, Qt::Horizontal, "Property");
    fileInfoModel.setHeaderData(1, Qt::Horizontal, "Value");

    pinnedFoldersModel.setColumnCount(1);
    pinnedFoldersModel.setHeaderData(0, Qt::Horizontal, "Pinned");
    ui->listViewForPinnedFolders->setModel(&pinnedFoldersModel);

    model.setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
    model.setRootPath(QDir::homePath());

    addPinnedFolder("Home", QDir::homePath());
    setRootPath(QDir::homePath());
    updateNavButtons();

    connect(ui->treeView, &QListView::doubleClicked, this, [this](const QModelIndex& idx) {
        if (!idx.isValid())
            return;
        const QModelIndex sourceIndex = mapToSourceIndex(idx);
        if (!sourceIndex.isValid())
            return;
        if (!model.isDir(sourceIndex))
            return;
        setRootPath(model.filePath(sourceIndex));
    });
    connect(ui->treeView, &QWidget::customContextMenuRequested, this, &Kitaplik::showFileMenu);
    connect(ui->listViewForPinnedFolders, &QListView::doubleClicked, this, [this](const QModelIndex& idx) {
        if (!idx.isValid())
            return;
        const QString path = idx.data(PinnedPathRole).toString();
        if (path.trimmed().isEmpty())
            return;
        setRootPath(path);
    });
    connect(ui->actionHome, &QAction::triggered, this, &Kitaplik::goHome);
    connect(ui->actionUp, &QAction::triggered, this, &Kitaplik::goUp);
    connect(ui->btn_back, &QToolButton::clicked, this, &Kitaplik::goBack);
    connect(ui->btn_forward, &QToolButton::clicked, this, &Kitaplik::goForward);
    connect(ui->pathLabel, &QLineEdit::returnPressed, this, &Kitaplik::goToPathFromPathLabel);
    connect(ui->pathLabel, &QLineEdit::textEdited, this, [this](const QString&) { updateGoToPathButton(); });
    connect(ui->btn_go_to_path, &QToolButton::clicked, this, &Kitaplik::goToPathFromPathLabel);

    updateGoToPathButton();

    QTimer::singleShot(0, this, [this] { ui->treeView->setFocus(Qt::OtherFocusReason); });
}

Kitaplik::~Kitaplik() = default;

QString Kitaplik::currentPath() const
{
    return model.rootPath();
}

void Kitaplik::setCopyPasteProgressVisible(bool visible, const QString& text)
{
    ui->label->setVisible(visible);
    ui->copyPasteProgressBar->setVisible(visible);

    if (!text.trimmed().isEmpty())
        ui->label->setText(text);
    else
        ui->label->setText("Progress");

    if (visible) {
        ui->copyPasteProgressBar->setRange(0, 0); // start indeterminate until we know total
        ui->copyPasteProgressBar->setValue(0);
    } else {
        ui->copyPasteProgressBar->setRange(0, 100);
        ui->copyPasteProgressBar->setValue(0);
    }
}

void Kitaplik::updateCopyPasteProgress(std::uint64_t doneBytes, std::uint64_t totalBytes)
{
    if (totalBytes == 0) {
        ui->copyPasteProgressBar->setRange(0, 0);
        ui->copyPasteProgressBar->setValue(0);
        return;
    }

    const int percent = static_cast<int>((doneBytes * 100u) / totalBytes);
    ui->copyPasteProgressBar->setRange(0, 100);
    ui->copyPasteProgressBar->setValue(std::clamp(percent, 0, 100));
    if (!pasteOpLabel.trimmed().isEmpty())
        ui->label->setText(QString("%1 %2%").arg(pasteOpLabel, QString::number(std::clamp(percent, 0, 100))));
}

void Kitaplik::finishPasteOperation(const QString& errorText, bool clearClipboard)
{
    setCopyPasteProgressVisible(false);
    pasteInProgress.store(false);
    pasteOpLabel.clear();

    if (clearClipboard)
        QApplication::clipboard()->clear();

    if (!errorText.trimmed().isEmpty())
        QMessageBox::warning(this, "Paste", errorText);

    navigateTo(currentPath(), false);
}

void Kitaplik::showFileMenu(const QPoint& viewPos)
{
    const QModelIndex index = ui->treeView->indexAt(viewPos);
    if (!index.isValid()) {
        QMenu menu(ui->treeView);
        QAction* newFolderAct = menu.addAction("New Folder");
        QAction* pasteAct = menu.addAction("Paste");
        const auto* mime = QApplication::clipboard()->mimeData();
        pasteAct->setEnabled(mime && mime->hasUrls() && QFileInfo(currentPath()).isDir());
        QAction* chosen = menu.exec(ui->treeView->viewport()->mapToGlobal(viewPos));
        if (chosen == newFolderAct) {
            onMenuNewFolder(currentPath());
        } else if (chosen == pasteAct) {
            onMenuPaste(currentPath());
        }
        return;
    }

    const QModelIndex sourceIndex = mapToSourceIndex(index);
    if (!sourceIndex.isValid())
        return;
    const QString targetPath = model.filePath(sourceIndex);

    QMenu menu(ui->treeView);
    QAction* openAct = menu.addAction("Open");
    QAction* renameAct = menu.addAction("Rename");
    menu.addSeparator();
    QAction* copyAct = menu.addAction("Copy");
    QAction* cutAct = menu.addAction("Cut");
    menu.addSeparator();
    QAction* deleteAct = menu.addAction("Delete");

    QAction* chosen = menu.exec(ui->treeView->viewport()->mapToGlobal(viewPos));
    if (!chosen)
        return;

    if (chosen == openAct)
        onMenuOpen(targetPath);
    else if (chosen == renameAct)
        onMenuRename(targetPath);
    else if (chosen == copyAct)
        onMenuCopy(targetPath);
    else if (chosen == cutAct)
        onMenuCut(targetPath);
    else if (chosen == deleteAct)
        onMenuDelete(targetPath);
}

void Kitaplik::onMenuNewFolder(const QString& parentDir)
{
    const QFileInfo parentInfo(parentDir);
    if (!parentInfo.exists() || !parentInfo.isDir()) {
        QMessageBox::warning(this, "New Folder", QString("Invalid directory:\n%1").arg(parentDir));
        return;
    }

    QString name = QInputDialog::getText(this, "New Folder", "Folder name:", QLineEdit::Normal).trimmed();
    if (name.isEmpty())
        return;

    if (name.contains('/') || name.contains('\\')) {
        QMessageBox::warning(this, "New Folder", "Folder name can't contain path separators.");
        return;
    }

    QDir dir(parentDir);
    const QString newPath = dir.filePath(name);
    if (QFileInfo::exists(newPath)) {
        QMessageBox::warning(this, "New Folder", QString("Already exists: %1").arg(name));
        return;
    }

    if (!dir.mkdir(name)) {
        QMessageBox::warning(this, "New Folder", QString("Failed to create folder:\n%1").arg(newPath));
        return;
    }

    navigateTo(currentPath(), false);
}

void Kitaplik::onMenuPaste(const QString& destDir)
{
    const QFileInfo destInfo(destDir);
    if (!destInfo.exists() || !destInfo.isDir()) {
        QMessageBox::warning(this, "Paste", QString("Invalid directory:\n%1").arg(destDir));
        return;
    }

    if (pasteInProgress.load()) {
        QMessageBox::information(this, "Paste", "Another copy/move is already running.");
        return;
    }

    const auto* mime = QApplication::clipboard()->mimeData();
    if (!mime || !mime->hasUrls())
        return;

    const bool isCut = mime->data(ClipboardCutMimeType) == QByteArray("1");

    QStringList sourcePaths;
    sourcePaths.reserve(mime->urls().size());
    for (const QUrl& url : mime->urls()) {
        if (!url.isLocalFile())
            continue;
        const QString srcPath = url.toLocalFile();
        if (!QFileInfo::exists(srcPath))
            continue;
        sourcePaths.push_back(srcPath);
    }
    if (sourcePaths.isEmpty())
        return;

    pasteInProgress.store(true);
    pasteOpLabel = isCut ? "Moving..." : "Copying...";
    setCopyPasteProgressVisible(true, pasteOpLabel);

    QPointer<Kitaplik> self(this);
    fileOpThread = std::jthread([self, sourcePaths, destDir, isCut] {
        QStringList errors;

        const QString normalizedDestDir = QDir(destDir).absolutePath();
        std::vector<std::uint64_t> perSrcBytes;
        perSrcBytes.reserve(static_cast<size_t>(sourcePaths.size()));
        std::uint64_t totalBytes = 0;

        for (const QString& srcPath : sourcePaths) {
            QString sizeErr;
            const auto sizeOpt = totalBytesForPath(srcPath, &sizeErr);
            if (!sizeOpt.has_value()) {
                errors.push_back(sizeErr.isEmpty() ? QString("Failed to scan: %1").arg(srcPath) : sizeErr);
                perSrcBytes.push_back(0);
                continue;
            }
            perSrcBytes.push_back(*sizeOpt);
            totalBytes += *sizeOpt;
        }

        if (self) {
            QMetaObject::invokeMethod(
                self,
                [self, totalBytes] {
                    if (!self)
                        return;
                    self->updateCopyPasteProgress(0, totalBytes);
                },
                Qt::QueuedConnection);
        }

        std::uint64_t doneBytes = 0;
        auto lastTick = std::chrono::steady_clock::now();
        int lastPercent = -1;
        const auto progress = [&](std::uint64_t done, std::uint64_t total) {
            if (!self || total == 0)
                return;
            const int percent = static_cast<int>((done * 100u) / total);
            const auto now = std::chrono::steady_clock::now();
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTick).count();
            if (percent == lastPercent && elapsedMs < 100)
                return;
            lastPercent = percent;
            lastTick = now;

            QMetaObject::invokeMethod(
                self,
                [self, done, total] {
                    if (!self)
                        return;
                    self->updateCopyPasteProgress(done, total);
                },
                Qt::QueuedConnection);
        };

        const auto resolveConflict = [self](const QString& sourcePath, const QString& destinationPath, bool isDirectory) {
            if (!self)
                return ConflictChoice::Cancel;

            ConflictChoice choice = ConflictChoice::Skip;
            QMetaObject::invokeMethod(
                self,
                [self, sourcePath, destinationPath, isDirectory, &choice] {
                    if (!self) {
                        choice = ConflictChoice::Cancel;
                        return;
                    }

                    QMessageBox box(self);
                    box.setIcon(QMessageBox::Question);
                    box.setWindowTitle("Paste conflict");
                    box.setText(isDirectory
                        ? QString("A folder already exists at destination:\n%1").arg(destinationPath)
                        : QString("A file already exists at destination:\n%1").arg(destinationPath));
                    box.setInformativeText(QString("Source: %1").arg(sourcePath));

                    QPushButton* replaceButton = box.addButton("Replace", QMessageBox::AcceptRole);
                    QPushButton* skipButton = box.addButton("Skip", QMessageBox::DestructiveRole);
                    QPushButton* keepBothButton = box.addButton("Keep both", QMessageBox::ActionRole);
                    QPushButton* cancelButton = box.addButton(QMessageBox::Cancel);
                    box.setDefaultButton(skipButton);

                    box.exec();
                    if (box.clickedButton() == replaceButton)
                        choice = ConflictChoice::Replace;
                    else if (box.clickedButton() == keepBothButton)
                        choice = ConflictChoice::KeepBoth;
                    else if (box.clickedButton() == cancelButton)
                        choice = ConflictChoice::Cancel;
                    else
                        choice = ConflictChoice::Skip;
                },
                Qt::BlockingQueuedConnection);

            return choice;
        };

        bool userCancelled = false;
        for (int i = 0; i < sourcePaths.size(); i++) {
            const QString& srcPath = sourcePaths.at(i);
            const QFileInfo srcInfo(srcPath);
            if (!srcInfo.exists())
                continue;

            const QString destPath = QDir(normalizedDestDir).filePath(srcInfo.fileName());
            if (QDir::cleanPath(srcPath) == QDir::cleanPath(destPath))
                continue;

            QString error;
            bool ok = false;
            if (isCut) {
                if (!QFileInfo::exists(destPath)) {
                    if (srcInfo.isDir())
                        ok = QDir().rename(srcPath, destPath);
                    else
                        ok = QFile::rename(srcPath, destPath);
                }

                if (ok) {
                    const auto idx = static_cast<size_t>(i);
                    if (idx < perSrcBytes.size())
                        doneBytes += perSrcBytes[idx];
                    progress(doneBytes, totalBytes);
                } else {
                    // cross-device move fallback and destination-conflict handling
                    ok = copyRecursivelyWithProgress(srcPath,
                                                     destPath,
                                                     &doneBytes,
                                                     totalBytes,
                                                     progress,
                                                     resolveConflict,
                                                     &userCancelled,
                                                     &error);
                    if (ok) {
                        QString rmErr;
                        if (!removeRecursively(srcPath, &rmErr)) {
                            ok = false;
                            error = rmErr.isEmpty() ? QString("Failed to delete after move: %1").arg(srcPath) : rmErr;
                        }
                    }
                    if (!ok && error.trimmed().isEmpty())
                        error = QString("Failed to move:\n%1\n→ %2").arg(srcPath, destPath);
                }
            } else {
                ok = copyRecursivelyWithProgress(srcPath,
                                                 destPath,
                                                 &doneBytes,
                                                 totalBytes,
                                                 progress,
                                                 resolveConflict,
                                                 &userCancelled,
                                                 &error);
            }

            if (!ok) {
                if (userCancelled)
                    break;
                if (error.trimmed().isEmpty())
                    error = QString("Paste failed for: %1").arg(srcPath);
                errors.push_back(error);
            }
        }

        progress(doneBytes, totalBytes);

        QString errorText = errors.join("\n\n");
        if (userCancelled) {
            if (!errorText.trimmed().isEmpty())
                errorText += "\n\n";
            errorText += "Operation cancelled.";
        }

        const bool clearClipboard = errorText.trimmed().isEmpty() && isCut;

        if (!self)
            return;
        QMetaObject::invokeMethod(
            self,
            [self, errorText, clearClipboard] {
                if (!self)
                    return;
                self->finishPasteOperation(errorText, clearClipboard);
            },
            Qt::QueuedConnection);
    });
}

void Kitaplik::onMenuOpen(const QString& targetPath)
{
    const QFileInfo info(targetPath);
    if (info.isDir()) {
        setRootPath(targetPath);
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(targetPath));
}

void Kitaplik::onMenuRename(const QString& targetPath)
{
    const QFileInfo info(targetPath);
    const QString oldName = info.fileName();
    const QString newName = QInputDialog::getText(this, "Rename", "New name:", QLineEdit::Normal, oldName);
    if (newName.trimmed().isEmpty() || newName == oldName)
        return;

    QDir parentDir = info.dir();
    const QString newPath = parentDir.filePath(newName);
    if (QFileInfo::exists(newPath)) {
        QMessageBox::warning(this, "Rename", QString("Already exists: %1").arg(newName));
        return;
    }

    if (!parentDir.rename(oldName, newName)) {
        QMessageBox::warning(this, "Rename", QString("Failed to rename: %1").arg(oldName));
        return;
    }

    navigateTo(currentPath(), false);
}

void Kitaplik::onMenuCopy(const QString& targetPath)
{
    auto* mimeData = new QMimeData();
    mimeData->setUrls({ QUrl::fromLocalFile(targetPath) });
    mimeData->setData(ClipboardCutMimeType, QByteArray("0"));
    QApplication::clipboard()->setMimeData(mimeData);
}

void Kitaplik::onMenuCut(const QString& targetPath)
{
    auto* mimeData = new QMimeData();
    mimeData->setUrls({ QUrl::fromLocalFile(targetPath) });
    mimeData->setData(ClipboardCutMimeType, QByteArray("1"));
    QApplication::clipboard()->setMimeData(mimeData);
}

void Kitaplik::onMenuDelete(const QString& targetPath)
{
    const QFileInfo info(targetPath);
    const QString label = info.fileName().trimmed().isEmpty() ? targetPath : info.fileName();

    const auto choice =
        QMessageBox::question(this, "Delete", QString("Delete \"%1\"?").arg(label), QMessageBox::Yes | QMessageBox::No);
    if (choice != QMessageBox::Yes)
        return;

    QString error;
    if (!removeRecursively(targetPath, &error))
        QMessageBox::warning(this, "Delete", error.isEmpty() ? "Delete failed." : error);

    navigateTo(currentPath(), false);
}

void Kitaplik::updateGoToPathButton()
{
    const QString normalized = cleanPath(ui->pathLabel->text());
    const QFileInfo info(normalized);
    ui->btn_go_to_path->setEnabled(normalized != currentPath() && info.exists() && info.isDir());
}

void Kitaplik::goToPathFromPathLabel()
{
    const QString normalized = cleanPath(ui->pathLabel->text());
    const QFileInfo info(normalized);
    if (!info.exists() || !info.isDir()) {
        QMessageBox::warning(this, "Invalid path", QString("No such directory:\n%1").arg(normalized));
        ui->pathLabel->setText(currentPath());
        updateGoToPathButton();
        return;
    }
    setRootPath(normalized);
}

void Kitaplik::setRootPath(const QString& path)
{
    navigateTo(path, true);
}

void Kitaplik::goHome()
{
    setRootPath(QDir::homePath());
}

void Kitaplik::goUp()
{
    const QDir dir(model.rootPath());
    const QString parent = dir.absolutePath() == "/" ? "/" : dir.absoluteFilePath("..");
    setRootPath(parent);
}

void Kitaplik::goBack()
{
    if (historyIndex <= 0)
        return;
    historyIndex--;
    navigateTo(history.at(static_cast<size_t>(historyIndex)), false);
    updateNavButtons();
}

void Kitaplik::goForward()
{
    if (historyIndex < 0)
        return;
    if (static_cast<size_t>(historyIndex + 1) >= history.size())
        return;
    historyIndex++;
    navigateTo(history.at(static_cast<size_t>(historyIndex)), false);
    updateNavButtons();
}

void Kitaplik::navigateTo(const QString& path, bool recordHistory)
{
    const QString normalized = cleanPath(path);
    const QModelIndex rootIndex = model.setRootPath(normalized);
    if (!rootIndex.isValid())
        return;

    const QModelIndex proxyRootIndex = sortProxy ? sortProxy->mapFromSource(rootIndex) : rootIndex;
    ui->treeView->setRootIndex(proxyRootIndex);
    ui->pathLabel->setText(normalized);
    updateGoToPathButton();
    updateWindowTitle(normalized);

    if (recordHistory) {
        if (historyIndex >= 0 && static_cast<size_t>(historyIndex) < history.size()
            && history.at(static_cast<size_t>(historyIndex)) == normalized) {
        } else {
            if (historyIndex >= 0 && static_cast<size_t>(historyIndex + 1) < history.size())
                history.erase(history.begin() + historyIndex + 1, history.end());
            history.push_back(normalized);
            historyIndex = static_cast<int>(history.size()) - 1;
            refreshHistoryView();
            updateNavButtons();
        }
    }

    emit currentPathChanged(normalized);
}

void Kitaplik::updateWindowTitle(const QString& path)
{
    QFileInfo info(path);
    QString title = info.fileName().trimmed();
    if (title.isEmpty())
        title = path.trimmed();
    if (title.isEmpty())
        title = "Kitaplik";

    setWindowTitle(title);
}

void Kitaplik::updateNavButtons()
{
    const bool canGoBack = historyIndex > 0;
    const bool canGoForward = historyIndex >= 0 && static_cast<size_t>(historyIndex + 1) < history.size();
    ui->btn_back->setEnabled(canGoBack);
    ui->btn_forward->setEnabled(canGoForward);
}

void Kitaplik::applySort(FileSortField field, Qt::SortOrder order)
{
    if (!sortProxy)
        return;

    currentSortField = field;
    currentSortOrder = order;
    sortProxy->setSortField(field);
    sortProxy->sort(0, order);
}

void Kitaplik::refreshHistoryView()
{
    constexpr int MaxEntries = 4;
    QStringList entries;
    for (int i = static_cast<int>(history.size()) - 1; i >= 0 && entries.size() < MaxEntries; --i)
        entries << history.at(static_cast<size_t>(i));
    historyListModel.setStringList(entries);
}

void Kitaplik::updateFileInfoView(const QModelIndex& index)
{
    fileInfoModel.removeRows(0, fileInfoModel.rowCount());
    if (!index.isValid())
        return;

    if (!sortProxy)
        return;
    const QModelIndex sourceIndex = sortProxy->mapToSource(index);
    if (!sourceIndex.isValid())
        return;

    const QFileInfo info = model.fileInfo(sourceIndex);
    auto addRow = [this](const QString& label, const QString& value) {
        const int row = fileInfoModel.rowCount();
        fileInfoModel.insertRow(row);
        fileInfoModel.setData(fileInfoModel.index(row, 0), label);
        fileInfoModel.setData(fileInfoModel.index(row, 1), value);
    };

    const auto formatDate = [](const QDateTime& dt) {
        return dt.isValid()
            ? QLocale::system().toString(dt, QLocale::ShortFormat)
            : QStringLiteral("<unknown>");
    };

    const QString itemName = info.fileName().trimmed().isEmpty() ? info.absoluteFilePath() : info.fileName();
    addRow("Name", itemName);
    addRow("Path", info.absoluteFilePath());
    addRow("Type", info.isDir() ? "Folder" : "File");
    if (info.isFile())
        addRow("Size", QLocale::system().formattedDataSize(info.size()));
    addRow("Modified", formatDate(info.lastModified()));
    addRow("Created", formatDate(info.birthTime()));
    addRow("Permissions", info.permissions() == QFileDevice::Permissions() ? "None" : "Readable");
}

void Kitaplik::addPinnedFolder(const QString& label, const QString& path)
{
    const QString clean = QDir(path).absolutePath();
    for (int row = 0; row < pinnedFoldersModel.rowCount(); ++row) {
        const QModelIndex idx = pinnedFoldersModel.index(row, 0);
        if (idx.data(PinnedPathRole).toString() == clean)
            return;
    }

    QStandardItem* item = new QStandardItem(label);
    item->setData(clean, PinnedPathRole);
    item->setToolTip(clean);
    pinnedFoldersModel.appendRow(item);
}

QModelIndex Kitaplik::mapToSourceIndex(const QModelIndex& proxyIndex) const
{
    if (!proxyIndex.isValid())
        return {};
    if (!sortProxy)
        return proxyIndex;
    return sortProxy->mapToSource(proxyIndex);
}
