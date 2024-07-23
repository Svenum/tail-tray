#include <QDir>
#include <QFile>
#include <QNetworkInformation>

#include "MainWindow.h"

#include <QFileDialog>
#include <QMessageBox>

#include "ManageDriveWindow.h"

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui(std::make_unique<Ui::MainWindow>())
    , accountsTabUi(nullptr)
    , pTrayManager(nullptr)
    , eCurrentState(TailState::NoAccount)
    , pCurrentExecution(nullptr)
    , pTailStatus(nullptr)
{
    ui->setupUi(this);

    setupTailDriveListView();

    // Remove the tail drive tab by default
    ui->tabWidget->removeTab(2);

    // Make sure to adjust tail drive based on the check state
    if (settings.tailDriveEnabled()) {
        ui->tabWidget->insertTab(2, ui->tabTailDrive, "Tail Drive");
    }

    connect(ui->btnAddTailDrive, &QPushButton::clicked,
        this, &MainWindow::addTailDriveButtonClicked);

    connect(ui->btnRemoveSelectedTailDrive, &QPushButton::clicked,
        this, &MainWindow::removeTailDriveButtonClicked);

    connect(ui->chkUseTailDrive, &QCheckBox::clicked,
        this, [this]() {
            auto checked = ui->chkUseTailDrive->isChecked();
            settings.tailDriveEnabled(checked);

            if (checked) {
                ui->tabWidget->insertTab(2, ui->tabTailDrive, "Tail Drive");
            }
            else {
                ui->tabWidget->removeTab(2);
            }
    });

    connect(ui->btnSelectTailDriveMountPath, &QPushButton::clicked,
        this, &MainWindow::selectTailDriveMountPath);

    pCurrentExecution = std::make_unique<TailRunner>(settings, this);
    connect(pCurrentExecution.get(), &TailRunner::statusUpdated, this, &MainWindow::onTailStatusChanged);
    connect(pCurrentExecution.get(), &TailRunner::loginFlowCompleted, this, &MainWindow::loginFlowCompleted);
    connect(pCurrentExecution.get(), &TailRunner::accountsListed, this, &MainWindow::onAccountsListed);
    connect(pCurrentExecution.get(), &TailRunner::driveListed, this, &MainWindow::drivesListed);

    accountsTabUi = std::make_unique<AccountsTabUiManager>(ui.get(), pCurrentExecution.get(), this);
    pTrayManager = std::make_unique<TrayMenuManager>(settings, pCurrentExecution.get(), this);

    changeToState(TailState::NotConnected);
    pCurrentExecution->getAccounts();

    connect(ui->btnSettingsClose, &QPushButton::clicked,
this, &MainWindow::settingsClosed);

    syncSettingsToUi();

    // Make sure the settings tab is selected by default
    ui->tabWidget->setCurrentIndex(1);

    setupNetworkCallbacks();
}

void MainWindow::showSettingsTab() {
    ui->tabWidget->setCurrentIndex(1);
    show();
}

void MainWindow::showAccountsTab() {
    ui->tabWidget->setCurrentIndex(0);
    show();
}

void MainWindow::showAboutTab() {
    ui->tabWidget->setCurrentIndex(2);
    show();
}

void MainWindow::onAccountsListed(const QList<TailAccountInfo>& foundAccounts) {
    accounts = foundAccounts;
    pTrayManager->onAccountsListed(foundAccounts);
    pTrayManager->stateChangedTo(eCurrentState, pTailStatus.get());

    accountsTabUi->onAccountsListed(foundAccounts);
    accountsTabUi->onTailStatusChanged(pTailStatus.get());
}

void MainWindow::settingsClosed() {
    syncSettingsFromUi();
    pCurrentExecution->start();
    hide();
}

void MainWindow::loginFlowCompleted() const {
    pCurrentExecution->start();
}

void MainWindow::onNetworkRechabilityChanged(QNetworkInformation::Reachability newReachability) {
    qDebug() << "onNetworkRechabilityChanged -> " << newReachability;

    if (newReachability == QNetworkInformation::Reachability::Online) {
        // Fetch accounts and then get the status info after that
        pCurrentExecution->getAccounts();
        QTimer::singleShot(500, this, [this]() {
            pCurrentExecution->checkStatus();
        });
        return;
    }

    if (eCurrentState != TailState::NotConnected)
        changeToState(TailState::NotConnected);
}

void MainWindow::drivesListed(const QList<TailDriveInfo>& drives, bool error, const QString& errorMsg) const {
    if (error) {
        pTailStatus->drivesConfigured = false;
        qDebug() << errorMsg;
        qDebug() << "To read more about configuring taill drives, see https://tailscale.com/kb/1369/taildrive";
        return; // Nothing to do here
    }

    pTailStatus->drivesConfigured = true;

    // Store available drives
    pTailStatus->drives = drives;

    // Refresh the tray icon menus
    pTrayManager->stateChangedTo(eCurrentState, pTailStatus.get());

    ui->twSharedDrives->clearContents();
    ui->twSharedDrives->setRowCount(drives.count());
    for (int i = 0; i < drives.count(); i++) {
        const auto& drive = drives[i];
        qDebug() << "Drive: " << drive.name << " (" << drive.path << ")";
        auto nameItem = new QTableWidgetItem(drive.name);
        auto pathItem = new QTableWidgetItem(drive.path);
        ui->twSharedDrives->setItem(i, 0, nameItem);
        ui->twSharedDrives->setItem(i, 1, pathItem);
    }
}

void MainWindow::setupTailDriveListView() {
    ui->twSharedDrives->setHorizontalHeaderLabels(QStringList() << "Name" << "Path");
    ui->twSharedDrives->setRowCount(0);
}

void MainWindow::addTailDriveButtonClicked() const {
    ManageDriveWindow dlg(TailDriveInfo{}, nullptr);
    auto result = dlg.exec();
    if (result == QDialog::Accepted) {
        auto newDrive = dlg.driveInfo();
        pCurrentExecution->addDrive(newDrive);

        pTailStatus->drives.emplace_back(newDrive);
        drivesListed(pTailStatus->drives, false, QString());
    }
}

void MainWindow::removeTailDriveButtonClicked() const {
    auto selectedItems = ui->twSharedDrives->selectedItems();
    if (selectedItems.count() < 1) {
        return;
    }

    auto row = ui->twSharedDrives->row(selectedItems.first());
    const auto& drive = pTailStatus->drives[row];

    auto answer = QMessageBox::question(nullptr, "Are you sure?", "Do you really want to remove the share " + drive.path + "?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

    if (answer != QMessageBox::Yes) {
        return;
    }

    pCurrentExecution->removeDrive(drive);
    ui->twSharedDrives->removeRow(row);
    if (row > 0)
        ui->twSharedDrives->selectRow(row - 1);
}

void MainWindow::selectTailDriveMountPath() const {
    QFileDialog dlg(nullptr, "Select mount path", ui->txtTailDriveDefaultMountPath->text());
    dlg.setOption(QFileDialog::Option::ShowDirsOnly, true);
    dlg.setFileMode(QFileDialog::FileMode::Directory);

    auto result = dlg.exec();
    if (result == QDialog::Accepted) {
        auto files = dlg.selectedFiles();
        if (files.count() > 0) {
            ui->txtTailDriveDefaultMountPath->setText(files.first());
        }
    }
}

TailState MainWindow::changeToState(TailState newState)
{
    auto retVal = eCurrentState;
    eCurrentState = newState;

    if (eCurrentState == TailState::NotLoggedIn)
    {
        // Clear the status
        pTailStatus = std::make_unique<TailStatus>();
        pTailStatus->self = std::make_unique<TailDeviceInfo>();
        pTailStatus->user = std::make_unique<TailUser>();
    }

    pTrayManager->stateChangedTo(newState, pTailStatus.get());
    accountsTabUi->onTailStatusChanged(pTailStatus.get());

    if (eCurrentState == TailState::Connected) {
        pCurrentExecution->listDrives();
    }

    return retVal;
}

void MainWindow::onTailStatusChanged(TailStatus* pNewStatus)
{
    pTailStatus.reset(pNewStatus);

    if (pTailStatus->user->id > 0)
    {
        // Logged in
        if (pTailStatus->health.count() < 1)
            changeToState(TailState::Connected);
        else
            changeToState(TailState::NotConnected);

        auto formattedVersion = pTailStatus->version.mid(0, pTailStatus->version.indexOf("-"));
        ui->lblVersionNumber->setText("Version " + formattedVersion);
    }

    accountsTabUi->onTailStatusChanged(pTailStatus.get());
}

bool MainWindow::shallowCheckForNetworkAvailable() {
    auto* inst = QNetworkInformation::instance();
    if (inst->reachability() == QNetworkInformation::Reachability::Online)
        return true;

    return false;
}

void MainWindow::setupNetworkCallbacks() const {
    auto* inst = QNetworkInformation::instance();
    if (inst == nullptr) {
        if (!QNetworkInformation::loadDefaultBackend()) {
            qDebug() << "Unable to load Network information stack";
            return;
        }

        inst = QNetworkInformation::instance();
        if (inst == nullptr) {
            qDebug() << "No noetwork information stack available";
            return;
        }
    }

    connect(inst, &QNetworkInformation::reachabilityChanged,
        this, &MainWindow::onNetworkRechabilityChanged);
}

void MainWindow::syncSettingsToUi() const {
    ui->chkAllowIncomingCnx->setChecked(settings.allowIncomingConnections());
    ui->chkUseTailscaleDns->setChecked(settings.useTailscaleDns());
    ui->chkUseTailscaleSubnets->setChecked(settings.useSubnets());
    ui->chkRunAsExitNode->setChecked(settings.advertiseAsExitNode());
    ui->chkExitNodeAllowNetworkAccess->setEnabled(settings.advertiseAsExitNode());
    ui->chkExitNodeAllowNetworkAccess->setChecked(settings.exitNodeAllowLanAccess());
    ui->chkStartOnLogin->setChecked(settings.startOnLogin());
    ui->chkStartOnLogin->setChecked(false);
    ui->chkUseTailDrive->setChecked(settings.tailDriveEnabled());
    ui->txtTailDriveDefaultMountPath->setText(settings.tailDriveMountPath());

    // Do we have a startup entry?
    auto configDir = QDir::home();
    if (!configDir.cd(".config"))
        return; // That's odd, no .config folder...

    if (!configDir.exists("autostart")) {
        if (!configDir.mkdir("autostart"))
            return;
    }

    configDir.cd("autostart");
    ui->chkStartOnLogin->setChecked(QFile::exists(configDir.absolutePath() + "/tail-tray.desktop"));
}

void MainWindow::syncSettingsFromUi() {
    settings.allowIncomingConnections(ui->chkAllowIncomingCnx->isChecked());
    settings.useTailscaleDns(ui->chkUseTailscaleDns->isChecked());
    settings.useSubnets(ui->chkUseTailscaleSubnets->isChecked());
    settings.advertiseAsExitNode(ui->chkRunAsExitNode->isChecked());
    settings.exitNodeAllowLanAccess(ui->chkExitNodeAllowNetworkAccess->isChecked());
    settings.startOnLogin(ui->chkStartOnLogin->isChecked());
    settings.tailDriveEnabled(ui->chkUseTailDrive->isChecked());
    settings.tailDriveMountPath(ui->txtTailDriveDefaultMountPath->text().trimmed());

    auto homeDir = QDir::home();
    auto targetFile = homeDir.absolutePath() + "/.config/autostart/tail-tray.desktop";
    if (settings.startOnLogin()) {
        if (!QFile::exists(targetFile)) {
            (void)homeDir.mkpath(".config/autostart");
            QFile::copy("/usr/local/share/applications/tail-tray.desktop", targetFile);
        }
    }
    else {
        QFile::remove(targetFile);
    }

    settings.save();
}
