#include "mainwindow.h"

#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QProgressDialog>
#include <QHeaderView>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QApplication>
#include <QDesktopServices>
#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <QDialog>

#include <filesystem>
#include <fstream>
#include <algorithm>

#include "xstd_errors.h"
#include "metadata.h"
#include "types.h"

using namespace xstd;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("XSTD Archive Manager");
    setAcceptDrops(true);

    setupCentralWidget();
    setupMenuBar();
    setupToolBar();
    setupStatusBar();

    // Load recent files from QSettings
    QSettings settings;
    recentFiles_ = settings.value("recentArchives").toStringList();
    updateRecentMenu();
    updateActions(false);
}

MainWindow::~MainWindow()
{
    closeArchive();
}

// ---------------------------------------------------------------------------
// Central widget: QStackedWidget with welcome page and tree view
// ---------------------------------------------------------------------------

void MainWindow::setupCentralWidget()
{
    stack_ = new QStackedWidget(this);

    // Page 0: Welcome
    welcome_ = new WelcomeWidget(this);
    connect(welcome_, &WelcomeWidget::openArchiveRequested, this, &MainWindow::onOpenArchive);
    connect(welcome_, &WelcomeWidget::createArchiveRequested, this, &MainWindow::onCreateArchive);
    stack_->addWidget(welcome_);

    // Page 1: Archive file list
    treeView_ = new QTreeView(this);
    model_ = new ArchiveModel(this);
    proxyModel_ = new QSortFilterProxyModel(this);
    proxyModel_->setSourceModel(model_);
    proxyModel_->setSortCaseSensitivity(Qt::CaseInsensitive);
    proxyModel_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxyModel_->setFilterKeyColumn(0); // filter on Name column

    treeView_->setModel(proxyModel_);
    treeView_->setSortingEnabled(true);
    treeView_->setAlternatingRowColors(true);
    treeView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    treeView_->setRootIsDecorated(false);
    treeView_->setContextMenuPolicy(Qt::CustomContextMenu);
    treeView_->header()->setStretchLastSection(true);
    stack_->addWidget(treeView_);

    setCentralWidget(stack_);
    stack_->setCurrentIndex(0);
}

// ---------------------------------------------------------------------------
// Menu bar
// ---------------------------------------------------------------------------

void MainWindow::setupMenuBar()
{
    auto* menuBar = this->menuBar();

    // ---- File ----
    auto* fileMenu = menuBar->addMenu(tr("&File"));

    auto* openAct = fileMenu->addAction(tr("&Open Archive..."), QKeySequence::Open, this, &MainWindow::onOpenArchive);
    Q_UNUSED(openAct);

    auto* createAct = fileMenu->addAction(tr("&Create Archive..."), QKeySequence(Qt::CTRL | Qt::Key_N), this, &MainWindow::onCreateArchive);
    Q_UNUSED(createAct);

    fileMenu->addSeparator();

    auto* changeDriveAct = fileMenu->addAction(tr("Change &Drive..."), this, &MainWindow::onChangeDrive);
    Q_UNUSED(changeDriveAct);

    fileMenu->addSeparator();

    // Recent archives submenu
    recentMenu_ = fileMenu->addMenu(tr("&Recent Archives"));
    updateRecentMenu();

    fileMenu->addSeparator();

    fileMenu->addAction(tr("E&xit"), QKeySequence(Qt::CTRL | Qt::Key_Q), this, &QWidget::close);

    // ---- Commands ----
    auto* cmdMenu = menuBar->addMenu(tr("&Commands"));

    auto* viewAct = cmdMenu->addAction(tr("&View File"), this, &MainWindow::onViewFile);
    archiveActions_.append(viewAct);

    auto* extractAct = cmdMenu->addAction(tr("&Extract File(s)..."), QKeySequence(Qt::CTRL | Qt::Key_E), this, &MainWindow::onExtractFiles);
    archiveActions_.append(extractAct);

    auto* addAct = cmdMenu->addAction(tr("&Add File(s)..."), QKeySequence(Qt::CTRL | Qt::Key_A), this, &MainWindow::onAddFiles);
    archiveActions_.append(addAct);

    cmdMenu->addSeparator();

    auto* deleteAct = cmdMenu->addAction(tr("&Delete File"), QKeySequence::Delete, this, &MainWindow::onDeleteFile);
    archiveActions_.append(deleteAct);

    auto* renameAct = cmdMenu->addAction(tr("&Rename File..."), QKeySequence(Qt::Key_F2), this, &MainWindow::onRenameFile);
    archiveActions_.append(renameAct);

    // ---- Tools ----
    auto* toolsMenu = menuBar->addMenu(tr("&Tools"));

    auto* searchAct = toolsMenu->addAction(tr("&Search..."), QKeySequence(Qt::CTRL | Qt::Key_F), this, &MainWindow::onSearch);
    archiveActions_.append(searchAct);

    toolsMenu->addSeparator();

    auto* infoAct = toolsMenu->addAction(tr("Archive &Info..."), QKeySequence(Qt::CTRL | Qt::Key_I), this, &MainWindow::onArchiveInfo);
    archiveActions_.append(infoAct);

    auto* recoverAct = toolsMenu->addAction(tr("Reco&ver Deleted File..."), this, &MainWindow::onRecoverFile);
    archiveActions_.append(recoverAct);

    // ---- Options ----
    auto* optionsMenu = menuBar->addMenu(tr("&Options"));
    optionsMenu->addAction(tr("&Settings..."))->setEnabled(false); // placeholder

    // ---- Help ----
    auto* helpMenu = menuBar->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About XSTD..."), this, &MainWindow::onAbout);
    helpMenu->addAction(tr("About &Qt"), qApp, &QApplication::aboutQt);
}

// ---------------------------------------------------------------------------
// Toolbar
// ---------------------------------------------------------------------------

void MainWindow::setupToolBar()
{
    auto* toolbar = addToolBar(tr("Main"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    toolbar->addAction(tr("Open"), this, &MainWindow::onOpenArchive);

    auto* extractBtn = toolbar->addAction(tr("Extract"), this, &MainWindow::onExtractFiles);
    archiveActions_.append(extractBtn);

    auto* addBtn = toolbar->addAction(tr("Add"), this, &MainWindow::onAddFiles);
    archiveActions_.append(addBtn);

    auto* deleteBtn = toolbar->addAction(tr("Delete"), this, &MainWindow::onDeleteFile);
    archiveActions_.append(deleteBtn);

    auto* infoBtn = toolbar->addAction(tr("Info"), this, &MainWindow::onArchiveInfo);
    archiveActions_.append(infoBtn);
}

// ---------------------------------------------------------------------------
// Status bar
// ---------------------------------------------------------------------------

void MainWindow::setupStatusBar()
{
    statusLabel_ = new QLabel(tr("Ready"), this);
    statusBar()->addPermanentWidget(statusLabel_);
}

// ---------------------------------------------------------------------------
// Update helpers
// ---------------------------------------------------------------------------

void MainWindow::updateActions(bool archiveOpen)
{
    for (auto* act : archiveActions_)
        act->setEnabled(archiveOpen);
}

void MainWindow::updateWindowTitle()
{
    if (currentArchivePath_.isEmpty())
        setWindowTitle("XSTD Archive Manager");
    else
        setWindowTitle(QString("XSTD Archive Manager — %1").arg(QFileInfo(currentArchivePath_).fileName()));
}

void MainWindow::addRecentArchive(const QString& path)
{
    recentFiles_.removeAll(path);
    recentFiles_.prepend(path);
    while (recentFiles_.size() > kMaxRecent)
        recentFiles_.removeLast();

    QSettings settings;
    settings.setValue("recentArchives", recentFiles_);
    updateRecentMenu();
}

void MainWindow::updateRecentMenu()
{
    if (!recentMenu_) return;
    recentMenu_->clear();

    if (recentFiles_.isEmpty()) {
        recentMenu_->addAction(tr("(No recent archives)"))->setEnabled(false);
        return;
    }

    for (const auto& path : recentFiles_) {
        auto* act = recentMenu_->addAction(path, this, &MainWindow::onOpenRecent);
        act->setData(path);
    }
}

// ---------------------------------------------------------------------------
// Encryption key prompt
// ---------------------------------------------------------------------------

std::vector<uint8_t> MainWindow::promptForKey()
{
    bool ok = false;
    QString hex = QInputDialog::getText(this, tr("Encryption Key"),
        tr("Enter the decryption key (hex string):"), QLineEdit::Normal, {}, &ok);

    if (!ok || hex.isEmpty())
        return {};

    // Parse hex string to bytes
    hex = hex.remove(' ');
    if (hex.size() % 2 != 0) {
        QMessageBox::warning(this, tr("Invalid Key"), tr("Hex key must have even number of characters."));
        return {};
    }

    std::vector<uint8_t> key;
    key.reserve(hex.size() / 2);
    for (int i = 0; i < hex.size(); i += 2) {
        bool convOk = false;
        uint8_t byte = static_cast<uint8_t>(hex.mid(i, 2).toUInt(&convOk, 16));
        if (!convOk) {
            QMessageBox::warning(this, tr("Invalid Key"), tr("Invalid hex character in key."));
            return {};
        }
        key.push_back(byte);
    }
    return key;
}

// ---------------------------------------------------------------------------
// Open / Close archive
// ---------------------------------------------------------------------------

void MainWindow::openArchive(const QString& path)
{
    closeArchive();

    auto fsPath = std::filesystem::path(path.toStdWString());

    // First, try opening without key to check if encrypted
    ArchiveOptions opts;
    opts.read_write = true;
    auto tempArchive = std::make_unique<Archive>(fsPath, opts);

    auto res = tempArchive->Open();

    // If decryption failed, ask for key
    if (res == kDecryptionFailed || res == kMissingEncryptionKey || res == kInvalidArchive) {
        auto key = promptForKey();
        if (key.empty()) return; // cancelled

        opts.key = key;
        tempArchive = std::make_unique<Archive>(fsPath, opts);
        res = tempArchive->Open();
        if (XSTD_isError(res)) {
            QMessageBox::critical(this, tr("Error"),
                tr("Failed to open archive: %1").arg(XSTD_ErrorCode_toString(res)));
            return;
        }
        currentKey_ = key;
    } else if (XSTD_isError(res)) {
        QMessageBox::critical(this, tr("Error"),
            tr("Failed to open archive: %1").arg(XSTD_ErrorCode_toString(res)));
        return;
    }

    archive_ = std::move(tempArchive);
    currentArchivePath_ = path;

    // Populate model
    model_->loadFromArchive(*archive_);

    // Resize columns to content
    for (int i = 0; i < model_->columnCount(); ++i)
        treeView_->resizeColumnToContents(i);

    // Switch to tree view
    stack_->setCurrentIndex(1);
    updateWindowTitle();
    updateActions(true);
    addRecentArchive(path);

    statusLabel_->setText(tr("%1 file(s) in archive").arg(archive_->FileCount()));
}

void MainWindow::closeArchive()
{
    if (archive_) {
        archive_->Close();
        archive_.reset();
    }
    currentArchivePath_.clear();
    currentKey_.clear();
    model_->clear();
    updateWindowTitle();
    updateActions(false);
    statusLabel_->setText(tr("Ready"));
}

void MainWindow::reloadArchive()
{
    if (!archive_) return;

    // Re-populate model from the already-open archive (catalog is up-to-date)
    model_->loadFromArchive(*archive_);
    for (int i = 0; i < model_->columnCount(); ++i)
        treeView_->resizeColumnToContents(i);

    stack_->setCurrentIndex(1);
    updateWindowTitle();
    updateActions(true);
    statusLabel_->setText(tr("%1 file(s) in archive").arg(archive_->FileCount()));
}

// ---------------------------------------------------------------------------
// Drag & Drop
// ---------------------------------------------------------------------------

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    const auto urls = event->mimeData()->urls();
    if (urls.isEmpty()) return;

    // If first file is .xstd, open it as archive
    QString firstPath = urls.first().toLocalFile();
    if (firstPath.endsWith(".xstd", Qt::CaseInsensitive)) {
        openArchive(firstPath);
        return;
    }

    // Otherwise, if an archive is open, add dropped files
    if (!archive_) {
        QMessageBox::information(this, tr("Info"),
            tr("Open an archive first, or drop a .xstd file to open it."));
        return;
    }

    QStringList filePaths;
    for (const auto& url : urls) {
        if (url.isLocalFile())
            filePaths.append(url.toLocalFile());
    }

    if (filePaths.isEmpty()) return;

    int added = 0;
    for (const auto& fp : filePaths) {
        auto stdPath = std::filesystem::path(fp.toStdWString());
        std::string archName = stdPath.filename().string();
        auto r = archive_->AddFile(archName, stdPath);
        if (XSTD_isError(r)) {
            QMessageBox::warning(this, tr("Warning"),
                tr("Failed to add '%1': %2").arg(QString::fromStdString(archName),
                    XSTD_ErrorCode_toString(r)));
        } else {
            ++added;
        }
    }

    if (added > 0) {
        reloadArchive();
        statusBar()->showMessage(tr("Added %1 file(s)").arg(added), 3000);
    }
}

// ---------------------------------------------------------------------------
// File menu slots
// ---------------------------------------------------------------------------

void MainWindow::onOpenArchive()
{
    QString path = QFileDialog::getOpenFileName(this, tr("Open XSTD Archive"),
        {}, tr("XSTD Archives (*.xstd);;All Files (*)"));
    if (!path.isEmpty())
        openArchive(path);
}

void MainWindow::onCreateArchive()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Create New Archive"),
        {}, tr("XSTD Archives (*.xstd)"));
    if (path.isEmpty()) return;

    ArchiveOptions opts;
    Archive arch(std::filesystem::path(path.toStdWString()), opts);

    auto res = arch.Create();
    if (XSTD_isError(res)) {
        QMessageBox::critical(this, tr("Error"),
            tr("Failed to create archive: %1").arg(XSTD_ErrorCode_toString(res)));
        return;
    }

    res = arch.Close();
    if (XSTD_isError(res)) {
        QMessageBox::critical(this, tr("Error"),
            tr("Failed to finalise archive: %1").arg(XSTD_ErrorCode_toString(res)));
        return;
    }

    openArchive(path);
}

void MainWindow::onChangeDrive()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Directory"));
    if (dir.isEmpty()) return;

    QString path = QFileDialog::getOpenFileName(this, tr("Open XSTD Archive"), dir,
        tr("XSTD Archives (*.xstd);;All Files (*)"));
    if (!path.isEmpty())
        openArchive(path);
}

void MainWindow::onOpenRecent()
{
    auto* act = qobject_cast<QAction*>(sender());
    if (!act) return;
    QString path = act->data().toString();
    if (QFile::exists(path))
        openArchive(path);
    else {
        QMessageBox::warning(this, tr("Not Found"), tr("Archive not found: %1").arg(path));
        recentFiles_.removeAll(path);
        QSettings().setValue("recentArchives", recentFiles_);
        updateRecentMenu();
    }
}

// ---------------------------------------------------------------------------
// Commands menu slots
// ---------------------------------------------------------------------------

void MainWindow::onViewFile()
{
    if (!archive_) return;

    auto indexes = treeView_->selectionModel()->selectedRows();
    if (indexes.isEmpty()) {
        QMessageBox::information(this, tr("View"), tr("Select a file to view."));
        return;
    }

    // Get the archive path from column 0
    auto srcIdx = proxyModel_->mapToSource(indexes.first());
    QString archPath = model_->data(model_->index(srcIdx.row(), 0)).toString();

    std::vector<uint8_t> data;
    auto res = archive_->ExtractFile(archPath.toStdString(), data);
    if (XSTD_isError(res)) {
        QMessageBox::critical(this, tr("Error"),
            tr("Failed to extract '%1': %2").arg(archPath, XSTD_ErrorCode_toString(res)));
        return;
    }

    // Show in a simple text dialog (works for text files)
    QString content = QString::fromUtf8(reinterpret_cast<const char*>(data.data()),
                                         static_cast<int>(data.size()));

    auto* dlg = new QDialog(this);
    dlg->setWindowTitle(tr("View — %1").arg(archPath));
    dlg->resize(700, 500);
    auto* layout = new QVBoxLayout(dlg);
    auto* textEdit = new QPlainTextEdit(dlg);
    textEdit->setReadOnly(true);
    textEdit->setPlainText(content);
    textEdit->setFont(QFont("Consolas", 10));
    layout->addWidget(textEdit);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::onExtractFiles()
{
    if (!archive_) return;

    auto indexes = treeView_->selectionModel()->selectedRows();

    // If nothing selected, extract all
    QStringList paths;
    if (indexes.isEmpty()) {
        auto files = archive_->ListFiles();
        for (const auto& f : files)
            paths.append(QString::fromStdString(f));
    } else {
        for (const auto& idx : indexes) {
            auto srcIdx = proxyModel_->mapToSource(idx);
            paths.append(model_->data(model_->index(srcIdx.row(), 0)).toString());
        }
    }

    if (paths.isEmpty()) return;

    QString destDir = QFileDialog::getExistingDirectory(this, tr("Extract to Directory"));
    if (destDir.isEmpty()) return;

    QProgressDialog progress(tr("Extracting files..."), tr("Cancel"), 0, paths.size(), this);
    progress.setWindowModality(Qt::WindowModal);

    int extracted = 0;
    for (int i = 0; i < paths.size(); ++i) {
        if (progress.wasCanceled()) break;
        progress.setValue(i);

        auto dest = std::filesystem::path(destDir.toStdWString()) / paths[i].toStdString();

        // Create parent directories
        std::filesystem::create_directories(dest.parent_path());

        auto res = archive_->ExtractFile(paths[i].toStdString(), dest);
        if (XSTD_isError(res)) {
            QMessageBox::warning(this, tr("Warning"),
                tr("Failed to extract '%1': %2").arg(paths[i], XSTD_ErrorCode_toString(res)));
        } else {
            ++extracted;
        }
    }

    progress.setValue(paths.size());
    statusBar()->showMessage(tr("Extracted %1 of %2 file(s)").arg(extracted).arg(paths.size()), 3000);
}

void MainWindow::onAddFiles()
{
    if (!archive_) return;

    QStringList filePaths = QFileDialog::getOpenFileNames(this, tr("Add Files to Archive"));
    if (filePaths.isEmpty()) return;

    int added = 0;
    for (const auto& fp : filePaths) {
        auto stdPath = std::filesystem::path(fp.toStdWString());
        std::string archName = stdPath.filename().string();
        auto r = archive_->AddFile(archName, stdPath);
        if (XSTD_isError(r)) {
            QMessageBox::warning(this, tr("Warning"),
                tr("Failed to add '%1': %2").arg(QString::fromStdString(archName),
                    XSTD_ErrorCode_toString(r)));
        } else {
            ++added;
        }
    }

    if (added > 0) {
        reloadArchive();
        statusBar()->showMessage(tr("Added %1 file(s)").arg(added), 3000);
    }
}

void MainWindow::onDeleteFile()
{
    if (!archive_) return;

    auto indexes = treeView_->selectionModel()->selectedRows();
    if (indexes.isEmpty()) {
        QMessageBox::information(this, tr("Delete"), tr("Select a file to delete."));
        return;
    }

    auto srcIdx = proxyModel_->mapToSource(indexes.first());
    QString archPath = model_->data(model_->index(srcIdx.row(), 0)).toString();

    auto answer = QMessageBox::question(this, tr("Delete"),
        tr("Delete '%1' from archive?").arg(archPath));
    if (answer != QMessageBox::Yes) return;

    auto res = archive_->DeleteFile(archPath.toStdString());
    if (XSTD_isError(res)) {
        QMessageBox::critical(this, tr("Error"),
            tr("Failed to delete '%1': %2").arg(archPath, XSTD_ErrorCode_toString(res)));
        return;
    }

    reloadArchive();
    statusBar()->showMessage(tr("Deleted '%1'").arg(archPath), 3000);
}

void MainWindow::onRenameFile()
{
    if (!archive_) return;

    auto indexes = treeView_->selectionModel()->selectedRows();
    if (indexes.isEmpty()) {
        QMessageBox::information(this, tr("Rename"), tr("Select a file to rename."));
        return;
    }

    auto srcIdx = proxyModel_->mapToSource(indexes.first());
    QString oldName = model_->data(model_->index(srcIdx.row(), 0)).toString();

    bool ok = false;
    QString newName = QInputDialog::getText(this, tr("Rename File"),
        tr("New name for '%1':").arg(oldName), QLineEdit::Normal, oldName, &ok);
    if (!ok || newName.isEmpty() || newName == oldName) return;

    auto res = archive_->RenameFile(oldName.toStdString(), newName.toStdString());
    if (XSTD_isError(res)) {
        QMessageBox::critical(this, tr("Error"),
            tr("Failed to rename '%1': %2").arg(oldName, XSTD_ErrorCode_toString(res)));
        return;
    }

    reloadArchive();
    statusBar()->showMessage(tr("Renamed '%1' → '%2'").arg(oldName, newName), 3000);
}

// ---------------------------------------------------------------------------
// Tools menu slots
// ---------------------------------------------------------------------------

void MainWindow::onSearch()
{
    if (!archive_) return;

    bool ok = false;
    QString query = QInputDialog::getText(this, tr("Search"),
        tr("Search file names:"), QLineEdit::Normal, {}, &ok);
    if (!ok) return;

    proxyModel_->setFilterFixedString(query);
    statusBar()->showMessage(query.isEmpty()
        ? tr("Filter cleared")
        : tr("Showing files matching '%1'").arg(query), 3000);
}

void MainWindow::onArchiveInfo()
{
    if (!archive_) return;

    const auto& hdr = archive_->Header();
    auto fileCount = archive_->FileCount();
    auto deletedCount = archive_->ListDeletedFiles().size();

    // Compression type string
    auto codec = CompressionCodec();
    codec.raw = hdr.flags.IsCompressed() ? static_cast<uint8_t>(CompressionType::ZSTD) : 0;

    QString encStr;
    if (hdr.IsEncrypted()) {
        auto alg = hdr.encryption.GetAlgorithm();
        auto ks = hdr.encryption.GetKeySize();
        QString algName = (alg == EncryptionAlgorithm::AES_GCM_V1) ? "AES-GCM" : "AES-CTR";
        encStr = QString("%1 (%2-bit)").arg(algName).arg(static_cast<int>(ks) * 8);
    } else {
        encStr = "None";
    }

    QString info = tr(
        "<h3>Archive Information</h3>"
        "<table>"
        "<tr><td><b>Path:</b></td><td>%1</td></tr>"
        "<tr><td><b>Format Version:</b></td><td>%2</td></tr>"
        "<tr><td><b>Page Size:</b></td><td>%3 KB</td></tr>"
        "<tr><td><b>Total Pages:</b></td><td>%4</td></tr>"
        "<tr><td><b>Files:</b></td><td>%5</td></tr>"
        "<tr><td><b>Deleted Files:</b></td><td>%6</td></tr>"
        "<tr><td><b>Encryption:</b></td><td>%7</td></tr>"
        "<tr><td><b>Compressed:</b></td><td>%8</td></tr>"
        "</table>"
    ).arg(currentArchivePath_)
     .arg(hdr.version)
     .arg(PageSizeBytes(static_cast<PageSize>(hdr.page_size)) / 1024)
     .arg(hdr.num_pages)
     .arg(fileCount)
     .arg(deletedCount)
     .arg(encStr)
     .arg(hdr.IsCompressed() ? tr("Yes") : tr("No"));

    QMessageBox::information(this, tr("Archive Info"), info);
}

void MainWindow::onRecoverFile()
{
    if (!archive_) return;

    auto deleted = archive_->ListDeletedFiles();
    if (deleted.empty()) {
        QMessageBox::information(this, tr("Recover"), tr("No deleted files to recover."));
        return;
    }

    QStringList items;
    for (const auto& f : deleted)
        items.append(QString::fromStdString(f));

    bool ok = false;
    QString chosen = QInputDialog::getItem(this, tr("Recover Deleted File"),
        tr("Select file to recover:"), items, 0, false, &ok);
    if (!ok) return;

    auto data = archive_->RecoverFile(chosen.toStdString());
    if (!data.has_value()) {
        QMessageBox::critical(this, tr("Error"), tr("Failed to recover '%1'.").arg(chosen));
        return;
    }

    QString destDir = QFileDialog::getExistingDirectory(this, tr("Save Recovered File to"));
    if (destDir.isEmpty()) return;

    auto dest = std::filesystem::path(destDir.toStdWString()) / chosen.toStdString();
    std::filesystem::create_directories(dest.parent_path());

    std::ofstream out(dest, std::ios::binary);
    if (!out) {
        QMessageBox::critical(this, tr("Error"), tr("Cannot write to '%1'.").arg(QString::fromStdWString(dest.wstring())));
        return;
    }
    out.write(reinterpret_cast<const char*>(data->data()), static_cast<std::streamsize>(data->size()));
    out.close();

    statusBar()->showMessage(tr("Recovered '%1'").arg(chosen), 3000);
}

// ---------------------------------------------------------------------------
// Help
// ---------------------------------------------------------------------------

void MainWindow::onAbout()
{
    QMessageBox::about(this, tr("About XSTD Archive Manager"),
        tr("<h3>XSTD Archive Manager</h3>"
           "<p>Version 1.0.0</p>"
           "<p>A modern archive format supporting:</p>"
           "<ul>"
           "<li>ZSTD compression (levels 1-9)</li>"
           "<li>AES-GCM / AES-CTR encryption</li>"
           "<li>SHA-256 integrity verification</li>"
           "<li>Delta encoding</li>"
           "<li>B+ Tree file catalog</li>"
           "</ul>"
           "<p>Built with Qt %1</p>").arg(QT_VERSION_STR));
}
