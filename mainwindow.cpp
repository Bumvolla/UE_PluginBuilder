#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QProcess>
#include <QDir>
#include <QStandardPaths>
#include <QCheckBox>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);

    // Apply stylesheet to the console output
    ui->consoleOutput->setStyleSheet(R"(
        QScrollBar:vertical {
            border: none;
            background: #2D2D2D;
            width: 12px;
            margin: 0px 0px 0px 0px;
        }
        QScrollBar::handle:vertical {
            background: #555555;
            min-height: 20px;
            border-radius: 6px;
        }
        QScrollBar::add-line:vertical {
            border: none;
            background: none;
            height: 0px;
            subcontrol-position: bottom;
            subcontrol-origin: margin;
        }
        QScrollBar::sub-line:vertical {
            border: none;
            background: none;
            height: 0px;
            subcontrol-position: top;
            subcontrol-origin: margin;
        }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
            background: none;
        }
        QScrollBar:horizontal {
            border: none;
            background: #2D2D2D;
            height: 12px;
            margin: 0px 0px 0px 0px;
        }
        QScrollBar::handle:horizontal {
            background: #555555;
            min-width: 20px;
            border-radius: 6px;
        }
        QScrollBar::add-line:horizontal {
            border: none;
            background: none;
            width: 0px;
            subcontrol-position: right;
            subcontrol-origin: margin;
        }
        QScrollBar::sub-line:horizontal {
            border: none;
            background: none;
            width: 0px;
            subcontrol-position: left;
            subcontrol-origin: margin;
        }
        QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
            background: none;
        }
    )");

    // Connect buttons to slots
    connect(ui->btnSelectUEPath, &QPushButton::clicked, this, &MainWindow::onSelectUEPath);
    connect(ui->btnSelectPluginFile, &QPushButton::clicked, this, &MainWindow::onSelectPluginFile);
    connect(ui->btnSelectPackageFolder, &QPushButton::clicked, this, &MainWindow::onSelectPackageFolder);
    connect(ui->btnBuild, &QPushButton::clicked, this, &MainWindow::onBuildPlugin);
}

MainWindow::~MainWindow() {
    delete ui;
}

void MainWindow::onSelectUEPath() {
    QString path = QFileDialog::getExistingDirectory(this, "Select Unreal Engine Installation Path", "C:\\Program Files\\Epic Games");
    if (!path.isEmpty()) {
        ui->editUEPath->setText(path);

        // Detect versions and add checkboxes
        QStringList versions = detectUnrealEngineVersions(path);
        addVersionCheckboxes(versions);
    }
}

void MainWindow::onSelectPluginFile() {
    QString file = QFileDialog::getOpenFileName(this, "Select .uplugin File", QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), "Plugin Files (*.uplugin)");
    if (!file.isEmpty()) {
        ui->editPluginFile->setText(file);
    }
}

void MainWindow::onSelectPackageFolder() {
    QString path = QFileDialog::getExistingDirectory(this, "Select Package Output Folder", QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    if (!path.isEmpty()) {
        ui->editPackageFolder->setText(path);
    }
}

void MainWindow::onBuildPlugin() {
    QString uePath = ui->editUEPath->text();
    QString pluginFile = ui->editPluginFile->text();
    QString packageFolder = ui->editPackageFolder->text();

    if (uePath.isEmpty() || pluginFile.isEmpty() || packageFolder.isEmpty()) {
        QMessageBox::critical(this, "Error", "Please select UE path, plugin file, and package folder.\n");
        return;
    }

    // Disable the build button to prevent multiple clicks
    ui->btnBuild->setEnabled(false);

    // Get selected versions
    QList<QCheckBox*> checkboxes = ui->versionGroupBox->findChildren<QCheckBox*>(); // Search within versionGroupBox
    bool bIsAnyTrue = false;

    for (QCheckBox* checkbox : checkboxes) {
        if (checkbox->isChecked()) {
            bIsAnyTrue = true;
            QString version = checkbox->text();
            QString versionedPackageFolder = packageFolder + "/" + QFileInfo(pluginFile).baseName() + "_" + version;

            // Create version-specific output folder
            QDir().mkpath(versionedPackageFolder);

            // Build command
            QString command = uePath + "/" + version + "/Engine/Build/BatchFiles/RunUAT.bat BuildPlugin -plugin=\"" + pluginFile + "\" -package=\"" + versionedPackageFolder + "\"";

            // Create a QProcess for this build
            QProcess* process = new QProcess(this);
            QString logFilePath = versionedPackageFolder + "/build_log.txt";
            QFile* logFile = new QFile(logFilePath, this);

            if (!logFile->open(QIODevice::WriteOnly | QIODevice::Text)) {
                appendToConsole("Failed to create log file for version: " + version + "\n");
                delete logFile;
                continue;
            }

            // Connect process signals
            connect(process, &QProcess::readyReadStandardOutput, this, [this, process, logFile]() {
                QString output = process->readAllStandardOutput();
                appendToConsole(output);
                logFile->write(output.toUtf8());
            });

            connect(process, &QProcess::readyReadStandardError, this, [this, process, logFile]() {
                QString error = process->readAllStandardError();
                appendToConsole(error);
                logFile->write(error.toUtf8());
            });

            connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, process, version, logFile](int exitCode, QProcess::ExitStatus exitStatus) {
                logFile->close();
                delete logFile;

                if (exitStatus == QProcess::NormalExit && exitCode == 0) {
                    appendToConsole("Build process completed for version: " + version + "\n");
                } else {
                    appendToConsole("Build process failed for version: " + version + "\n");
                }
                process->deleteLater(); // Clean up the process
            });

            // Start the build process
            process->start(command);
            if (!process->waitForStarted()) {
                QMessageBox::critical(this, "Error", "Failed to start the build process for version: " + version + "\n");
                process->deleteLater(); // Clean up the process
                logFile->close();
                delete logFile;
            }
        }
    }

    // Re-enable the build button after all processes are started
    ui->btnBuild->setEnabled(true);

    if (bIsAnyTrue) {
        appendToConsole("Build process started for all selected versions.\n");
    } else {
        appendToConsole("No version was selected.\n");
    }
}

QStringList MainWindow::detectUnrealEngineVersions(const QString& uePath) {
    QDir ueDir(uePath);
    QStringList versions;

    // Look for directories starting with "UE_"
    QStringList filters;
    filters << "UE_*";
    ueDir.setNameFilters(filters);
    ueDir.setFilter(QDir::Dirs);

    for (const auto& dir : ueDir.entryList()) {
        versions.append(dir);
    }

    return versions;
}

void MainWindow::addVersionCheckboxes(const QStringList& versions) {
    // Clear existing checkboxes
    QLayout* layout = ui->versionGroupBox->layout();
    if (layout) {
        QLayoutItem* item;
        while ((item = layout->takeAt(0))) {
            delete item->widget();
            delete item;
        }
    }

    // Add new checkboxes in a grid layout
    QGridLayout* gridLayout = qobject_cast<QGridLayout*>(ui->versionGroupBox->layout());
    if (!gridLayout) {
        gridLayout = new QGridLayout(ui->versionGroupBox);
        ui->versionGroupBox->setLayout(gridLayout);
    }

    int row = 0;
    int col = 0; // Adjust the number of columns as needed

    for (const auto& version : versions) {
        QCheckBox* checkbox = new QCheckBox(version, ui->versionGroupBox); // Ensure parent is versionGroupBox
        gridLayout->addWidget(checkbox, row, col, Qt::AlignCenter);

        col++;
        if (col >= MAX_GRID_COLUMNS) {
            col = 0;
            row++;
        }
    }
}

void MainWindow::appendToConsole(const QString& text) {
    QTextCharFormat format;
    format.setForeground(Qt::white);

    if (text.contains("error", Qt::CaseSensitive) | text.contains("failed", Qt::CaseInsensitive)) {
        format.setForeground(Qt::red);
    } else if (text.contains("warning", Qt::CaseSensitive)) {
        format.setForeground(Qt::darkYellow);
    }else if (text.contains("SUCCESSFUL", Qt::CaseSensitive) | text.contains("completed", Qt::CaseSensitive) ) {
        format.setForeground(Qt::green);
    }

    // Apply the format to the text
    QTextCursor cursor = ui->consoleOutput->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text, format);
    ui->consoleOutput->setTextCursor(cursor);
    ui->consoleOutput->ensureCursorVisible();
}


