#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QTreeView>
#include <QSortFilterProxyModel>
#include <QLabel>
#include <QStringList>
#include <QSettings>
#include <memory>

#include "archivemodel.h"
#include "welcomewidget.h"
#include "archive.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    // File menu
    void onOpenArchive();
    void onCreateArchive();
    void onChangeDrive();
    void onOpenRecent();

    // Commands menu
    void onViewFile();
    void onExtractFiles();
    void onAddFiles();
    void onDeleteFile();
    void onRenameFile();

    // Tools menu
    void onSearch();
    void onArchiveInfo();
    void onRecoverFile();

    // Help menu
    void onAbout();

private:
    void setupMenuBar();
    void setupToolBar();
    void setupStatusBar();
    void setupCentralWidget();

    void openArchive(const QString& path);
    void closeArchive();
    void reloadArchive();
    void addRecentArchive(const QString& path);
    void updateRecentMenu();
    void updateWindowTitle();
    void updateActions(bool archiveOpen);

    // Ask user for encryption key if archive is encrypted. Returns empty if cancelled.
    std::vector<uint8_t> promptForKey();

    // Widgets
    QStackedWidget*        stack_       = nullptr;
    WelcomeWidget*         welcome_     = nullptr;
    QTreeView*             treeView_    = nullptr;
    ArchiveModel*          model_       = nullptr;
    QSortFilterProxyModel* proxyModel_  = nullptr;
    QLabel*                statusLabel_ = nullptr;

    // Archive state
    QString                            currentArchivePath_;
    std::unique_ptr<xstd::Archive>     archive_;
    std::vector<uint8_t>               currentKey_;

    // Menus
    QMenu*        recentMenu_  = nullptr;
    QStringList   recentFiles_;
    static constexpr int kMaxRecent = 5;

    // Actions that require an open archive
    QList<QAction*> archiveActions_;
};
