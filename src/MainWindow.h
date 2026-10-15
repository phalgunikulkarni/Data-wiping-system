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
#include <QByteArray>
#include <utility>
#include <map>
#include <vector>
#include <string>
#include "FeatureExtractor.h"
#include "FileScanner.h"

// Structure to track files to be wiped
struct FileToWipe {
    std::string name;
    std::string fullPath;
    unsigned long long sizeBytes;
    bool isDeleted;
    bool isDirectory;
    bool isValid = true;

    uint32_t startCluster = 0;
    uint64_t dataOffset = 0;
    uint64_t dataRegionStart = 0;
    uint64_t clusterSize = 0;
    std::vector<uint32_t> clusterChain;
    std::vector<uint64_t> dataOffsets;
    std::vector<std::pair<uint64_t, uint64_t>> extents;

    FileEntry fileEntry;
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
std::map<std::string, FileEntry> deletedFileEntries;
std::map<std::string, FeatureVector> beforeFingerprints;
std::map<std::string, FeatureVector> afterFingerprints;

struct FeatureDifferences {
    double entropy = 0.0;
    double meanByteValue = 0.0;
    double standardDeviation = 0.0;
    double zeroBytePercentage = 0.0;
    double ffBytePercentage = 0.0;
    double printableBytePercentage = 0.0;
    double uniqueByteRatio = 0.0;
    double dominantBytePercentage = 0.0;
    double byteRange = 0.0;
    double chiSquare = 0.0;
};

std::map<std::string, FeatureDifferences> featureDifferences;
std::map<std::string, std::vector<double>> mlInputVectors;
struct MLDatasetSample {
    std::vector<double> beforeFeatures;
    std::vector<double> afterFeatures;
    std::vector<double> differences;
    int wipeMethod = 0;
    bool actualRecovery = false;
};

std::map<std::string, MLDatasetSample> mlDataset;
std::vector<std::vector<double>> similarityTrainingData;
std::vector<double> similarityTrainingLabels;
bool similarityModelTrained = false;
bool similarityModelEvaluated = false;
bool similarityModelSaved = false;
double similarityScore = 0.0;
std::string similarityScoreFile;
std::vector<std::vector<double>> recoverabilityTrainingData;
std::vector<double> recoverabilityTrainingLabels;
bool recoverabilityModelTrained = false;
bool recoverabilityModelEvaluated = false;
bool recoverabilityModelSaved = false;
double recoverabilityScore = 0.0;
bool recoverabilityScoreGenerated = false;
int riskLevel = 0;
double mediumRiskThreshold = 0.30;
double highRiskThreshold = 0.60;
std::string wipeRecommendation;
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
    QPushButton   *createControlledTestButton;
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

    QString       controlledTestId;
    QString       controlledTestFileName;
    QString       controlledTestDeletedName;
    QString       controlledTestPhysicalPath;
    char          controlledTestDriveLetter = 0;
    QByteArray    controlledTestContent;
};