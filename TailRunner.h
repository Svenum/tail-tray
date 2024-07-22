#ifndef TAILRUNNER_H
#define TAILRUNNER_H

#include <memory>

#include <QObject>
#include <QString>
#include <QList>
#include <QProcess>

#include "models.h"
#include "TailSettings.h"

class TailRunner : public QObject
{
    Q_OBJECT
public:
    explicit TailRunner(const TailSettings& s, QObject* parent = nullptr);

    void checkStatus();
    void getAccounts();
    void switchAccount(const QString& accountId);

    void login();
    void logout();

    void start(bool usePkExec = false);
    void stop();

    void listDrives();

private:
    const TailSettings& settings;
    std::unique_ptr<QProcess> pProcess;
    enum class Command {
        ListAccounts,
        SwitchAccount,
        Login,
        Logout,
        Connect,
        Disconnect,
        SettingsChange,
        Status,
        Drive
    };

    Command eCommand;

signals:
    void accountsListed(const QList<TailAccountInfo>& accounts);
    void statusUpdated(TailStatus* newStatus);
    void loginFlowCompleted();
    void driveListed(const QList<TailDriveInfo>& drives, bool error, const QString& errorMsg);

private:
    void runCommand(const QString& cmd, QStringList args, bool jsonResult = false, bool usePkExec = false);
    void onProcessCanReadStdOut();

    void parseStatusResponse(const QJsonObject& obj);
};

#endif // TAILRUNNER_H
