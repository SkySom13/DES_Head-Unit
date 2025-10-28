// dbus_manager.h
#ifndef DBUS_MANAGER_H
#define DBUS_MANAGER_H

#include <QObject>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDebug>

class DBusManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool afmConnected READ isAFMConnected NOTIFY afmConnectionChanged)

public:
    explicit DBusManager(QObject *parent = nullptr);
    ~DBusManager();

    bool isAFMConnected() const { return m_afmConnected; }

    // Methods callable from QML
    Q_INVOKABLE void launchApp(int iviId);
    Q_INVOKABLE void activateApp(int iviId);
    Q_INVOKABLE void terminateApp(int iviId);
    Q_INVOKABLE void notifyAppConnected(int iviId);
    Q_INVOKABLE void notifyAppDisconnected(int iviId);
    Q_INVOKABLE QString getAppState(int iviId);

signals:
    // Signals from AFM that QML can connect to
    void appLaunched(int iviId, int runId);
    void appTerminated(int iviId);
    void appStateChanged(int iviId, const QString &state);
    void afmConnectionChanged();

private slots:
    void onAFMStateChanged(int iviId, const QString &state);
    void onAFMAppLaunched(int iviId, int runId);
    void onAFMAppTerminated(int iviId);

private:
    void setupAFMConnection();
    void setupWindowManagerConnection();

    QDBusConnection m_sessionBus;
    QDBusInterface *m_afmInterface;
    QDBusInterface *m_wmInterface;
    bool m_afmConnected;
};

#endif // DBUS_MANAGER_H
