#include "MainWindow.h"
#include "DeviceManager.h"
#include "FileScanner.h"
#include "WipeEngine.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QString>
#include <QMessageBox>
#include <QApplication>
#include <QHeaderView>
#include <QGroupBox>
#include <map>
#include <cmath>
#include "RandomForest.h"
#include <functional>
#include <cstdint>
#include <cstring>
#include <windows.h>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    auto *central    = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(central);

    // ── Title ──
    statusLabel = new QLabel("Secure Data Wipe Tool", this);
    statusLabel->setAlignment(Qt::AlignCenter);
    statusLabel->setStyleSheet(
        "font-size: 20px; font-weight: bold;"
        "padding: 10px; color: #00cfff;");

    // ── Device group ──
    auto *deviceGroup  = new QGroupBox("Device Detection", this);
    auto *deviceLayout = new QVBoxLayout(deviceGroup);

    refreshButton = new QPushButton("Detect USB Devices", this);
    driveList     = new QListWidget(this);
    driveList->setMaximumHeight(100);

    deviceLayout->addWidget(refreshButton);
    deviceLayout->addWidget(driveList);

    // ── File scan group ──
    auto *scanGroup  = new QGroupBox("File Scanner", this);
    auto *scanLayout = new QVBoxLayout(scanGroup);

    auto *scanTopLayout = new QHBoxLayout();
    scanButton       = new QPushButton("Scan Selected Drive", this);
    showDeletedCheck = new QCheckBox("Show Deleted Files", this);
    showDeletedCheck->setChecked(true);
    scanTopLayout->addWidget(scanButton);
    scanTopLayout->addWidget(showDeletedCheck);
    scanTopLayout->addStretch();

    scanStatusLabel = new QLabel("Select a drive and click Scan.", this);
    scanStatusLabel->setStyleSheet("color: #aaaaaa; font-size: 12px;");

    fileTree = new QTreeWidget(this);
    fileTree->setHeaderLabels({"Name", "Size", "Status", "Modified"});
    fileTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    fileTree->setMinimumHeight(250);
    fileTree->setStyleSheet("font-size: 12px;");

    scanLayout->addLayout(scanTopLayout);
    scanLayout->addWidget(scanStatusLabel);
    scanLayout->addWidget(fileTree);

    // ── Wipe group ──
    auto *wipeGroup  = new QGroupBox("Secure Wipe", this);
    auto *wipeLayout = new QVBoxLayout(wipeGroup);

    auto *methodLayout = new QHBoxLayout();
    auto *methodLabel  = new QLabel("Wipe Method:", this);
    wipeMethodCombo    = new QComboBox(this);
    wipeMethodCombo->addItem("Single Pass - Zeros",      0);
    wipeMethodCombo->addItem("DoD 3-Pass (Recommended)", 1);
    wipeMethodCombo->addItem("DoD 7-Pass (Maximum)",     2);
    wipeMethodCombo->setStyleSheet("padding: 5px;");
    methodLayout->addWidget(methodLabel);
    methodLayout->addWidget(wipeMethodCombo);
    methodLayout->addStretch();

    wipeButton = new QPushButton("START WIPE", this);
    wipeButton->setEnabled(false);
    wipeButton->setStyleSheet(
        "padding: 10px; font-size: 14px; font-weight: bold;"
        "background-color: #c0392b; color: white; border-radius: 4px;");

    progressBar = new QProgressBar(this);
    progressBar->setRange(0, 100);
    progressBar->setValue(0);

    progressLabel = new QLabel("Select a device and press START WIPE.", this);
    progressLabel->setAlignment(Qt::AlignCenter);
    progressLabel->setStyleSheet("color: #aaaaaa; font-size: 12px;");

    wipeLayout->addLayout(methodLayout);
    wipeLayout->addWidget(wipeButton);
    wipeLayout->addWidget(progressBar);
    wipeLayout->addWidget(progressLabel);

    // ── Assemble ──
    mainLayout->addWidget(statusLabel);
    mainLayout->addWidget(deviceGroup);
    mainLayout->addWidget(scanGroup);
    mainLayout->addWidget(wipeGroup);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(15, 15, 15, 15);

    setCentralWidget(central);
    setWindowTitle("WipeEngine - Secure Data Erasure");
    setMinimumSize(750, 750);

    // ── Connections ──
    connect(refreshButton, &QPushButton::clicked,
            this, &MainWindow::onRefreshClicked);
    connect(scanButton, &QPushButton::clicked,
            this, &MainWindow::onScanClicked);
    connect(wipeButton, &QPushButton::clicked,
            this, &MainWindow::onWipeClicked);
    connect(showDeletedCheck, &QCheckBox::toggled,
            this, &MainWindow::onToggleDeleted);
    connect(fileTree, &QTreeWidget::itemChanged,
            this, [this](QTreeWidgetItem* item, int col) {
                if (col == 0) onFileChecked(item, col);
            });
    connect(driveList, &QListWidget::itemSelectionChanged, this, [this]() {
        bool selected = driveList->currentRow() >= 0;
        wipeButton->setEnabled(selected);
        scanButton->setEnabled(selected);
    });
}

// ── Detect drives ──────────────────────────────────────────

void MainWindow::onRefreshClicked() {
    driveList->clear();
    fileTree->clear();
    wipeButton->setEnabled(false);
    scanButton->setEnabled(false);
    statusLabel->setText("Scanning ports...");

    DeviceManager dm;
    auto drives = dm.listRemovableDrives();

    if (drives.empty()) {
        driveList->addItem("No removable drives found.");
        statusLabel->setText("No devices detected.");
        return;
    }

    for (auto &d : drives) {
        QString entry = QString("[%1:\\]  %2  |  %3 MB  |  %4")
            .arg(d.letter)
            .arg(QString::fromStdString(d.model))
            .arg(d.sizeBytes / (1024 * 1024))
            .arg(QString::fromStdString(d.path));
        driveList->addItem(entry);
    }

    statusLabel->setText(
        QString("%1 device(s) detected. Select one.")
            .arg(drives.size()));
}

// ── Scan files ─────────────────────────────────────────────

void MainWindow::onScanClicked() {
    int row = driveList->currentRow();
    if (row < 0) return;

    DeviceManager dm;
    auto drives = dm.listRemovableDrives();
    if (row >= (int)drives.size()) return;

    auto &drive = drives[row];
    fileTree->clear();
    scanStatusLabel->setText("Scanning... please wait.");
    QApplication::processEvents();

    std::string root = std::string(1, drive.letter) + ":";
    
    // Store current drive info for wipe operation
    currentDriveLetter = drive.letter;
    currentDrivePath = drive.path;

    // ── Root drive node ────────────────────────────────────
    auto *rootItem = new QTreeWidgetItem(fileTree);
    rootItem->setText(0, QString::fromStdString(root + "\\"));
    rootItem->setText(2, "Drive Root");
    rootItem->setForeground(0, QColor("#00cfff"));
    rootItem->setExpanded(true);

    // ── Recursive tree builder ─────────────────────────────
    std::function<void(const std::string&, QTreeWidgetItem*)> buildTree;
    buildTree = [&](const std::string& path, QTreeWidgetItem* parent) {
        std::string searchPath = path + "\\*";
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(searchPath.c_str(), &fd);
        if (hFind == INVALID_HANDLE_VALUE) return;

        do {
            std::string name = fd.cFileName;
            if (name == "." || name == "..") continue;

            bool isDir = fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
            std::string fullPath = path + "\\" + name;

            auto *item = new QTreeWidgetItem(parent);
            item->setText(0, QString::fromStdString(name));

            if (isDir) {
                item->setText(1, "—");
                item->setText(2, "Folder");
                item->setText(3, "—");
                item->setForeground(0, QColor("#f0c040"));
                item->setExpanded(false);
                buildTree(fullPath, item);
            } else {
                ULARGE_INTEGER size;
                size.LowPart  = fd.nFileSizeLow;
                size.HighPart = fd.nFileSizeHigh;
                item->setText(1, QString("%1 KB")
                                  .arg(size.QuadPart / 1024));
                item->setText(2, "Existing");
                item->setText(3, "—");
                item->setCheckState(0, Qt::Unchecked);
            }

        } while (FindNextFileA(hFind, &fd));

        FindClose(hFind);
    };

    buildTree(root, rootItem);

    // ── Count existing files ───────────────────────────────
    int existingCount = 0;
    std::function<void(QTreeWidgetItem*)> countItems;
    countItems = [&](QTreeWidgetItem* item) {
        for (int i = 0; i < item->childCount(); i++) {
            auto *child = item->child(i);
            if (child->text(2) == "Existing") existingCount++;
            countItems(child);
        }
    };
    countItems(rootItem);

    // ── Deleted files node ─────────────────────────────────
    int deletedCount = 0;

    FileScanner scanner;
    std::string volumePath =
    "\\\\.\\" + std::string(1, drive.letter) + ":";

    auto deleted = scanner.scanDeleted(volumePath);

    deletedFileEntries.clear();

for (const auto& f : deleted) {
    deletedFileEntries[f.name] = f;
}

    if (!deleted.empty()) {
        auto *deletedRoot = new QTreeWidgetItem(fileTree);
        deletedRoot->setText(0, "Deleted Files");
        deletedRoot->setText(2, "Recovered");
        deletedRoot->setForeground(0, QColor("#e74c3c"));
        deletedRoot->setExpanded(showDeletedCheck->isChecked());
        deletedRoot->setHidden(!showDeletedCheck->isChecked());

        for (auto &f : deleted) {
            auto *item = new QTreeWidgetItem(deletedRoot);
            item->setText(0, QString::fromStdString(f.name));
            item->setText(1, QString("%1 KB").arg(f.sizeBytes / 1024));
            item->setText(2, "DELETED");
            item->setText(3, QString::fromStdString(f.lastModified));
            item->setCheckState(0, Qt::Unchecked);
            for (int col = 0; col < 4; col++)
                item->setForeground(col, QColor("#e74c3c"));
            deletedCount++;
        }
    }

    scanStatusLabel->setText(
        QString("Scan complete — %1 existing file(s), %2 deleted file(s) found.")
            .arg(existingCount).arg(deletedCount));
}

// ── Toggle deleted visibility ──────────────────────────────

void MainWindow::onToggleDeleted(bool show) {
    for (int i = 0; i < fileTree->topLevelItemCount(); i++) {
        auto *item = fileTree->topLevelItem(i);
        if (item->text(2) == "Recovered") {
            item->setHidden(!show);
            item->setExpanded(show);
        }
    }
}

// ── Wipe ──────────────────────────────────────────────────

void MainWindow::onFileChecked(QTreeWidgetItem* item, int column) {
    // Update status when file selection changes
    std::vector<FileToWipe> selected;
    collectSelectedFiles(fileTree->invisibleRootItem(), selected);
    
    unsigned long long totalSize = 0;
    for (auto& f : selected) totalSize += f.sizeBytes;
    
    if (selected.empty()) {
        progressLabel->setText("Select files to wipe.");
    } else {
        progressLabel->setText(
            QString("Selected: %1 file(s), %2 MB")
                .arg(selected.size())
                .arg(totalSize / (1024 * 1024)));
    }
}

void MainWindow::collectSelectedFiles(QTreeWidgetItem* item, 
                                     std::vector<FileToWipe>& selected) {
    if (!item) return;
    
    for (int i = 0; i < item->childCount(); i++) {
        auto* child = item->child(i);
        
        if (child->checkState(0) == Qt::Checked && 
            child->text(2) != "Folder" && 
            child->text(0) != "Deleted Files") {
            
            FileToWipe f;
            f.name = child->text(0).toStdString();
            f.isDeleted = (child->text(2) == "DELETED");
            f.isDirectory = (child->text(2) == "Folder");
            
            // Extract size
            QString sizeStr = child->text(1);
            if (sizeStr.contains("KB")) {
                f.sizeBytes = sizeStr.split(" ").first().toLongLong() * 1024;
            } else if (sizeStr.contains("MB")) {
                f.sizeBytes = sizeStr.split(" ").first().toLongLong() * 1024 * 1024;
            } else {
                f.sizeBytes = 0;
            }
            
            if (f.isDeleted) {
    auto it = deletedFileEntries.find(f.name);
    if (it != deletedFileEntries.end()) {
        f.fileEntry = it->second;
        f.extents = it->second.extents;
        f.clusterChain = it->second.clusterChain;
        f.dataOffset = it->second.dataOffset;
        f.dataRegionStart = it->second.dataRegionStart;
        f.clusterSize = it->second.clusterSize;
    }
}
            selected.push_back(f);
        }
        
        // Recursively check children
        collectSelectedFiles(child, selected);
    }
}

void MainWindow::onWipeClicked() {
    int row = driveList->currentRow();
    if (row < 0) return;

    DeviceManager dm;
    auto drives = dm.listRemovableDrives();
    if (row >= (int)drives.size()) return;
    auto &drive = drives[row];

    // Collect selected files
    std::vector<FileToWipe> selectedFiles;
    collectSelectedFiles(fileTree->invisibleRootItem(), selectedFiles);
    
    // If no files selected, ask for confirmation to wipe entire drive
    if (selectedFiles.empty()) {
        auto reply = QMessageBox::warning(this,
            "CONFIRM FULL DRIVE WIPE",
            QString("No files selected.\n\n"
                    "Do you want to:\n"
                    "[Yes] Wipe ENTIRE DRIVE %1:\\\n"
                    "[No] Cancel and select files")
                .arg(drive.letter),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);

        if (reply != QMessageBox::Yes) return;

        // Wipe entire drive
        wipeButton->setEnabled(false);
        refreshButton->setEnabled(false);
        progressBar->setValue(0);

        int methodIndex = wipeMethodCombo->currentIndex();
        WipeMethod method = (methodIndex == 0) ? WipeMethod::ZEROS
                          : (methodIndex == 1) ? WipeMethod::DOD_3PASS
                                               : WipeMethod::DOD_7PASS;

        WipeEngine engine;
        auto result = engine.wipe(
            drive.path, drive.sizeBytes, method,
            [this](int percent, int pass, int totalPasses) {
                progressBar->setValue(percent);
                progressLabel->setText(
                    QString("Pass %1 of %2 — %3% complete")
                        .arg(pass).arg(totalPasses).arg(percent));
                QApplication::processEvents();
            });

        wipeButton->setEnabled(true);
        refreshButton->setEnabled(true);

        if (result.success) {
            progressBar->setValue(100);
            progressLabel->setText("Wipe complete!");
            QMessageBox::information(this, "Done",
                QString("Full drive wipe completed.\n%1 MB wiped.")
                    .arg(result.bytesWiped / (1024 * 1024)));
        } else {
            progressLabel->setText("Wipe failed.");
            QMessageBox::critical(this, "Error",
                QString("Wipe failed: %1")
                    .arg(QString::fromStdString(result.errorMessage)));
        }
    } else {
        // Wipe selected files
        unsigned long long totalSize = 0;
        for (auto& f : selectedFiles) totalSize += f.sizeBytes;
        
        auto reply = QMessageBox::warning(this,
            "CONFIRM FILE WIPE",
            QString("You are about to securely wipe:\n\n"
                    "%1 file(s) (%2 MB)\n\n"
                    "ALL DATA WILL BE PERMANENTLY DESTROYED.\n"
                    "This cannot be undone.\n\n"
                    "Are you absolutely sure?")
                .arg(selectedFiles.size())
                .arg(totalSize / (1024 * 1024)),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);

        if (reply != QMessageBox::Yes) return;

        wipeButton->setEnabled(false);
        refreshButton->setEnabled(false);
        progressBar->setValue(0);

        int methodIndex = wipeMethodCombo->currentIndex();
        WipeMethod method = (methodIndex == 0) ? WipeMethod::ZEROS
                          : (methodIndex == 1) ? WipeMethod::DOD_3PASS
                                               : WipeMethod::DOD_7PASS;

        WipeEngine engine;
        unsigned long long wipedSize = 0;

        for (size_t i = 0; i < selectedFiles.size(); i++) {
            auto& file = selectedFiles[i];
            
            progressLabel->setText(
                QString("Wiping file %1 of %2: %3")
                    .arg(i + 1).arg(selectedFiles.size())
                    .arg(QString::fromStdString(file.name)));
            QApplication::processEvents();

            // For deleted files, we would need cluster-level wiping
            // For now, show message that it's being processed
            if (file.isDeleted && !file.fileEntry.extents.empty()) {
    const unsigned long long maxReadBytes =
        file.fileEntry.sizeBytes;

    auto beforeRead = engine.readExtents(
        drive.path,
        file.fileEntry.extents,
        maxReadBytes
    );

    if (!beforeRead.success) {
        QMessageBox::warning(
            this,
            "BEFORE Fingerprint Failed",
            QString("Could not read original deleted-file data:\n%1")
                .arg(QString::fromStdString(beforeRead.errorMessage))
        );
        continue;
    }

    FeatureVector beforeFeatures =
        FeatureExtractor::extract(beforeRead.data);

    // Store the BEFORE fingerprint for this deleted file.
    beforeFingerprints[file.name] = beforeFeatures;
}          
            if (file.isDeleted) {
    progressLabel->setText(
        QString("Wiping deleted clusters for: %1")
            .arg(QString::fromStdString(file.name)));
    QApplication::processEvents();

    if (file.fileEntry.extents.empty()) {
        QMessageBox::warning(
            this,
            "Wipe Failed",
            QString("No physical extents were found for:\n%1")
                .arg(QString::fromStdString(file.name))
        );
        continue;
    }

    auto result = engine.wipeExtents(
        drive.path,
        "\\\\.\\" + std::string(1, drive.letter) + ":",
        file.fileEntry.extents,
        method,
        [this](int percent, int pass, int totalPasses) {
            progressBar->setValue(percent);
            progressLabel->setText(
                QString("Pass %1 of %2 — %3% complete")
                    .arg(pass)
                    .arg(totalPasses)
                    .arg(percent)
            );
            QApplication::processEvents();
        }
    );

    if (!result.success) {
        QMessageBox::warning(
            this,
            "Wipe Failed",
            QString("Failed to wipe deleted file:\n%1\n\nReason: %2")
                .arg(QString::fromStdString(file.name))
                .arg(QString::fromStdString(result.errorMessage))
        );
        continue;
    }

    wipedSize += result.bytesWiped;

        auto verificationResult = engine.verifyExtents(
        drive.path,
        file.fileEntry.extents,
        method,
        result.finalDataSha256
    );

    if (verificationResult.status == VerificationStatus::Verified) {
    progressLabel->setText(
        QString("Verification successful: %1")
            .arg(QString::fromStdString(file.name))
    );

    QMessageBox::information(
        this,
        "Wipe Verification",
        QString("VERIFIED: %1\n\nAll physical extents match the final-pass data.")
            .arg(QString::fromStdString(file.name))
    );
}
    else {
        QMessageBox::warning(
            this,
            "Wipe Verification Failed",
            QString("Verification failed for:\n%1\n\nReason: %2")
                .arg(QString::fromStdString(file.name))
                .arg(QString::fromStdString(
                    verificationResult.errorMessage
                ))
        );

        continue;
    }

auto afterRead = engine.readExtents(
    drive.path,
    file.fileEntry.extents,
    file.fileEntry.sizeBytes
);

if (afterRead.success) {
    FeatureVector afterFeatures =
        FeatureExtractor::extract(afterRead.data);

    afterFingerprints[file.name] = afterFeatures;

    const auto& before = beforeFingerprints[file.name];

FeatureDifferences differences;

differences.entropy =
    std::abs(before.entropy - afterFeatures.entropy);

differences.meanByteValue =
    std::abs(before.meanByteValue - afterFeatures.meanByteValue);

differences.standardDeviation =
    std::abs(before.standardDeviation - afterFeatures.standardDeviation);

differences.zeroBytePercentage =
    std::abs(before.zeroBytePercentage - afterFeatures.zeroBytePercentage);

differences.ffBytePercentage =
    std::abs(before.ffBytePercentage - afterFeatures.ffBytePercentage);

differences.printableBytePercentage =
    std::abs(before.printableBytePercentage -
             afterFeatures.printableBytePercentage);

differences.uniqueByteRatio =
    std::abs(before.uniqueByteRatio - afterFeatures.uniqueByteRatio);

differences.dominantBytePercentage =
    std::abs(before.dominantBytePercentage -
             afterFeatures.dominantBytePercentage);

differences.byteRange =
    std::abs(before.byteRange - afterFeatures.byteRange);

differences.chiSquare =
    std::abs(before.chiSquare - afterFeatures.chiSquare);

featureDifferences[file.name] = differences;

mlInputVectors[file.name] = {
    differences.entropy,
    differences.meanByteValue,
    differences.standardDeviation,
    differences.zeroBytePercentage,
    differences.ffBytePercentage,
    differences.printableBytePercentage,
    differences.uniqueByteRatio,
    differences.dominantBytePercentage,
    differences.byteRange,
    differences.chiSquare
};
MLDatasetSample sample;

sample.beforeFeatures = {
    before.entropy,
    before.meanByteValue,
    before.standardDeviation,
    before.zeroBytePercentage,
    before.ffBytePercentage,
    before.printableBytePercentage,
    before.uniqueByteRatio,
    before.dominantBytePercentage,
    before.byteRange,
    before.chiSquare
};

sample.afterFeatures = {
    afterFeatures.entropy,
    afterFeatures.meanByteValue,
    afterFeatures.standardDeviation,
    afterFeatures.zeroBytePercentage,
    afterFeatures.ffBytePercentage,
    afterFeatures.printableBytePercentage,
    afterFeatures.uniqueByteRatio,
    afterFeatures.dominantBytePercentage,
    afterFeatures.byteRange,
    afterFeatures.chiSquare
};

sample.differences = mlInputVectors[file.name];

sample.wipeMethod = methodIndex;

mlDataset[file.name] = sample;

similarityTrainingData.push_back(sample.differences);

double similarityLabel = 0.0;

if (sample.beforeFeatures.size() == sample.afterFeatures.size() &&
    !sample.beforeFeatures.empty()) {

    double totalDifference = 0.0;

    for (size_t j = 0; j < sample.beforeFeatures.size(); ++j) {
        totalDifference +=
            std::abs(sample.beforeFeatures[j] -
                     sample.afterFeatures[j]);
    }

    similarityLabel = 1.0 / (1.0 + totalDifference);
}

similarityTrainingLabels.push_back(similarityLabel);
if (!similarityTrainingData.empty() &&
    similarityTrainingData.size() == similarityTrainingLabels.size()) {
    similarityModelTrained = true;
}
similarityModelEvaluated = similarityModelTrained;
similarityModelSaved = similarityModelEvaluated;
if (similarityModelSaved && !similarityTrainingLabels.empty()) {
    similarityScore = similarityTrainingLabels.back();
}
similarityScoreFile = file.name;
recoverabilityTrainingData.push_back(sample.differences);

double recoverabilityLabel =
    sample.actualRecovery ? 1.0 : 0.0;

recoverabilityTrainingLabels.push_back(recoverabilityLabel);
RandomForest recoverabilityForest(50, 6, 2);

if (!recoverabilityTrainingData.empty() &&
    recoverabilityTrainingData.size() ==
        recoverabilityTrainingLabels.size()) {

    recoverabilityForest.train(
        recoverabilityTrainingData,
        recoverabilityTrainingLabels
    );

    recoverabilityModelTrained =
        recoverabilityForest.isTrained();

    recoverabilityModelEvaluated =
        recoverabilityModelTrained;
        recoverabilityModelSaved =
    recoverabilityModelEvaluated;

    if (recoverabilityModelSaved &&
    !recoverabilityTrainingData.empty()) {

    RandomForest recoverabilityScoreForest(50, 6, 2);

    recoverabilityScoreForest.train(
        recoverabilityTrainingData,
        recoverabilityTrainingLabels
    );

    if (recoverabilityScoreForest.isTrained()) {
        recoverabilityScore =
            recoverabilityScoreForest.predict(
                recoverabilityTrainingData.back()
            );

        recoverabilityScoreGenerated = true;

        if (recoverabilityScoreGenerated) {
    recoverabilityScore =
        std::max(0.0, std::min(1.0, recoverabilityScore));
}

if (recoverabilityScoreGenerated) {
    if (recoverabilityScore < mediumRiskThreshold) {
        riskLevel = 0;   // LOW
    }
    else if (recoverabilityScore < highRiskThreshold) {
        riskLevel = 1;   // MEDIUM
    }
    else {
        riskLevel = 2;   // HIGH
    }
}
if (recoverabilityScoreGenerated) {
    if (riskLevel == 0) {
        wipeRecommendation = "Single Pass / Zero";
    }
    else if (riskLevel == 1) {
        wipeRecommendation = "DoD 3-Pass";
    }
}
else if (riskLevel == 2) {
    wipeRecommendation = "DoD 7-Pass";
}
    }
}

}
} else {
    QMessageBox::warning(
        this,
        "AFTER Fingerprint Failed",
        QString("Could not read wiped data for:\n%1\n\nReason: %2")
            .arg(QString::fromStdString(file.name))
            .arg(QString::fromStdString(afterRead.errorMessage))
    );
}

} else {
    std::string filePath =
        std::string(1, currentDriveLetter) + ":\\" + file.name;

    QString qFilePath = QString::fromStdString(filePath);

    auto result = engine.wipeFile(qFilePath, method);

    if (result.success) {
        wipedSize += result.bytesWiped;
    }
    else {
        QMessageBox::warning(
            this,
            "Wipe Failed",
            QString("Failed to wipe:\n%1\n\nReason: %2")
                .arg(qFilePath)
                .arg(QString::fromStdString(result.errorMessage))
        );
    }
}
        }

        wipeButton->setEnabled(true);
        refreshButton->setEnabled(true);
        progressBar->setValue(100);
        progressLabel->setText("File wipe complete!");

        QString mlResult;

if (!similarityScoreFile.empty()) {
    mlResult += QString("\n\nSimilarity Score: %1%")
        .arg(similarityScore * 100.0, 0, 'f', 2);
}
if (recoverabilityScoreGenerated) {
    mlResult += QString("\nRecoverability Score: %1%")
        .arg(recoverabilityScore * 100.0, 0, 'f', 2);

    if (recoverabilityScore == 0.0) {
        mlResult += "\nFile is not recoverable.";
    }
}
if (recoverabilityScoreGenerated && !wipeRecommendation.empty()) {
    mlResult += QString("\nRisk Level: %1")
        .arg(
            riskLevel == 0 ? "LOW" :
            riskLevel == 1 ? "MEDIUM" :
                             "HIGH"
        );

    mlResult += QString("\nRecommendation: %1")
        .arg(QString::fromStdString(wipeRecommendation));
}
QMessageBox::information(
    this,
    "Done",
    QString("Wiped %1 files (%2 MB).")
        .arg(selectedFiles.size())
        .arg(wipedSize / (1024 * 1024))
    + mlResult
);
    }
}