#pragma once
#include <QMainWindow>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QComboBox>
#include <QTreeWidget>
#include <QCheckBox>
#include <QSplitter>
#include <vector>
#include <string>

// Structure to track files to be wiped
struct FileToWipe {
    std::string name;
    std::string fullPath;
    unsigned long long sizeBytes;
    bool isDeleted;
    bool isDirectory;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);

private slots:
    void onRefreshClicked();
    void onScanClicked();
    void onWipeClicked();
    void onToggleDeleted(bool show);
    void onFileChecked(QTreeWidgetItem* item, int column);

private:
    // Helper to collect checked files
    void collectSelectedFiles(QTreeWidgetItem* item, 
                             std::vector<FileToWipe>& selected);

    // Device section
    QListWidget   *driveList;
    QPushButton   *refreshButton;
    QLabel        *statusLabel;

    // File tree section
    QTreeWidget   *fileTree;
    QPushButton   *scanButton;
    QCheckBox     *showDeletedCheck;
    QLabel        *scanStatusLabel;

    // Wipe section
    QComboBox     *wipeMethodCombo;
    QPushButton   *wipeButton;
    QProgressBar  *progressBar;
    QLabel        *progressLabel;
    
    // Track current drive for file operations
    std::string   currentDrivePath;
    char          currentDriveLetter;
};