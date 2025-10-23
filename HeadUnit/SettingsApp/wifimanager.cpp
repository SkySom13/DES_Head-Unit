#include "wifimanager.h"
#include <QDBusReply>
#include <QDBusMetaType>
#include <QDebug>
#include <QTimer>

WiFiManager::WiFiManager(QObject *parent)
    : QObject(parent)
    , m_systemBus(QDBusConnection::systemBus())
    , m_isConnected(false)
{
    initializeNetworkManager();
    refreshStatus();
}

WiFiManager::~WiFiManager()
{
    delete m_nmInterface;
}

void WiFiManager::initializeNetworkManager()
{
    // Initialize NetworkManager DBus interface
    m_nmInterface = new QDBusInterface(
        "org.freedesktop.NetworkManager",
        "/org/freedesktop/NetworkManager",
        "org.freedesktop.NetworkManager",
        m_systemBus,
        this
        );

    if (!m_nmInterface->isValid()) {
        qWarning() << "Failed to connect to NetworkManager:"
                   << m_nmInterface->lastError().message();
    } else {
        qDebug() << "Connected to NetworkManager via DBus";

        // Connect to NetworkManager signals
        m_systemBus.connect(
            "org.freedesktop.NetworkManager",
            "/org/freedesktop/NetworkManager",
            "org.freedesktop.NetworkManager",
            "StateChanged",
            this,
            SLOT(handleConnectionStateChanged())
            );
    }
}

void WiFiManager::scanNetworks()
{
    qDebug() << "Scanning for WiFi networks...";

    if (!m_nmInterface || !m_nmInterface->isValid()) {
        qWarning() << "NetworkManager interface not available";
        emit connectionFailed("NetworkManager not available");
        return;
    }

    // Get WiFi device
    QDBusReply<QList<QDBusObjectPath>> devicesReply =
        m_nmInterface->call("GetDevices");

    if (!devicesReply.isValid()) {
        qWarning() << "Failed to get devices:" << devicesReply.error().message();
        return;
    }

    // Find WiFi device and request scan
    for (const QDBusObjectPath &devicePath : devicesReply.value()) {
        QDBusInterface deviceInterface(
            "org.freedesktop.NetworkManager",
            devicePath.path(),
            "org.freedesktop.NetworkManager.Device",
            m_systemBus
            );

        QVariant deviceType = deviceInterface.property("DeviceType");
        if (deviceType.toUInt() == 2) { // 2 = WiFi device
            QDBusInterface wirelessInterface(
                "org.freedesktop.NetworkManager",
                devicePath.path(),
                "org.freedesktop.NetworkManager.Device.Wireless",
                m_systemBus
                );

            // Request scan
            wirelessInterface.call("RequestScan", QVariantMap());

            // Wait a bit then get results
            QTimer::singleShot(2000, this, &WiFiManager::handleScanResults);
            return;
        }
    }

    qWarning() << "No WiFi device found";
}

void WiFiManager::handleScanResults()
{
    m_availableNetworks = parseAccessPoints();
    emit availableNetworksChanged();
    emit scanCompleted();
    qDebug() << "Found" << m_availableNetworks.size() << "networks";
}

QVariantList WiFiManager::parseAccessPoints()
{
    QVariantList networks;

    if (!m_nmInterface || !m_nmInterface->isValid()) {
        return networks;
    }

    // Get WiFi device
    QDBusReply<QList<QDBusObjectPath>> devicesReply =
        m_nmInterface->call("GetDevices");

    if (!devicesReply.isValid()) {
        return networks;
    }

    for (const QDBusObjectPath &devicePath : devicesReply.value()) {
        QDBusInterface deviceInterface(
            "org.freedesktop.NetworkManager",
            devicePath.path(),
            "org.freedesktop.NetworkManager.Device",
            m_systemBus
            );

        QVariant deviceType = deviceInterface.property("DeviceType");
        if (deviceType.toUInt() == 2) { // WiFi device
            QDBusInterface wirelessInterface(
                "org.freedesktop.NetworkManager",
                devicePath.path(),
                "org.freedesktop.NetworkManager.Device.Wireless",
                m_systemBus
                );

            // Get access points
            QDBusReply<QList<QDBusObjectPath>> apsReply =
                wirelessInterface.call("GetAccessPoints");

            if (apsReply.isValid()) {
                for (const QDBusObjectPath &apPath : apsReply.value()) {
                    QDBusInterface apInterface(
                        "org.freedesktop.NetworkManager",
                        apPath.path(),
                        "org.freedesktop.NetworkManager.AccessPoint",
                        m_systemBus
                        );

                    QVariantMap network;
                    QByteArray ssidBytes = apInterface.property("Ssid").toByteArray();
                    QString ssid = QString::fromUtf8(ssidBytes);

                    if (!ssid.isEmpty()) {
                        network["ssid"] = ssid;
                        network["strength"] = apInterface.property("Strength").toUInt();
                        network["secured"] = (apInterface.property("Flags").toUInt() > 0);
                        networks.append(network);
                    }
                }
            }
            break;
        }
    }

    return networks;
}

void WiFiManager::connectToNetwork(const QString &ssid, const QString &password)
{
    qDebug() << "Connecting to network:" << ssid;

    if (!m_nmInterface || !m_nmInterface->isValid()) {
        qWarning() << "NetworkManager interface not available";
        emit connectionFailed("NetworkManager not available");
        return;
    }

    // Get WiFi device path
    QDBusReply<QList<QDBusObjectPath>> devicesReply =
        m_nmInterface->call("GetDevices");

    if (!devicesReply.isValid()) {
        emit connectionFailed("Failed to get devices");
        return;
    }

    QDBusObjectPath wifiDevicePath;
    for (const QDBusObjectPath &devicePath : devicesReply.value()) {
        QDBusInterface deviceInterface(
            "org.freedesktop.NetworkManager",
            devicePath.path(),
            "org.freedesktop.NetworkManager.Device",
            m_systemBus
            );

        QVariant deviceType = deviceInterface.property("DeviceType");
        if (deviceType.toUInt() == 2) { // WiFi device
            wifiDevicePath = devicePath;
            break;
        }
    }

    if (wifiDevicePath.path().isEmpty()) {
        emit connectionFailed("No WiFi device found");
        return;
    }

    // Find the access point with matching SSID
    QDBusInterface wirelessInterface(
        "org.freedesktop.NetworkManager",
        wifiDevicePath.path(),
        "org.freedesktop.NetworkManager.Device.Wireless",
        m_systemBus
        );

    QDBusReply<QList<QDBusObjectPath>> apsReply =
        wirelessInterface.call("GetAccessPoints");

    if (!apsReply.isValid()) {
        emit connectionFailed("Failed to get access points");
        return;
    }

    QDBusObjectPath targetApPath;
    for (const QDBusObjectPath &apPath : apsReply.value()) {
        QDBusInterface apInterface(
            "org.freedesktop.NetworkManager",
            apPath.path(),
            "org.freedesktop.NetworkManager.AccessPoint",
            m_systemBus
            );

        QByteArray ssidBytes = apInterface.property("Ssid").toByteArray();
        QString apSsid = QString::fromUtf8(ssidBytes);

        if (apSsid == ssid) {
            targetApPath = apPath;
            break;
        }
    }

    if (targetApPath.path().isEmpty()) {
        emit connectionFailed("Network not found");
        return;
    }

    // Create connection settings
    QVariantMap connection;
    connection["id"] = ssid;
    connection["type"] = "802-11-wireless";
    connection["autoconnect"] = true;

    QVariantMap wireless;
    wireless["ssid"] = ssid.toUtf8();
    wireless["mode"] = "infrastructure";

    QVariantMap connectionSettings;
    connectionSettings["connection"] = connection;
    connectionSettings["802-11-wireless"] = wireless;

    // Add security settings if password provided
    if (!password.isEmpty()) {
        QVariantMap wirelessSecurity;
        wirelessSecurity["key-mgmt"] = "wpa-psk";
        wirelessSecurity["psk"] = password;
        connectionSettings["802-11-wireless-security"] = wirelessSecurity;
    }

    // Activate connection
    QDBusInterface settingsInterface(
        "org.freedesktop.NetworkManager",
        "/org/freedesktop/NetworkManager/Settings",
        "org.freedesktop.NetworkManager.Settings",
        m_systemBus
        );

    QDBusReply<QDBusObjectPath> addReply = settingsInterface.call(
        "AddConnection",
        QVariant::fromValue(connectionSettings)
        );

    if (addReply.isValid()) {
        // Activate the new connection
        QDBusReply<QDBusObjectPath> activateReply = m_nmInterface->call(
            "ActivateConnection",
            QVariant::fromValue(addReply.value()),
            QVariant::fromValue(wifiDevicePath),
            QVariant::fromValue(targetApPath)
            );

        if (activateReply.isValid()) {
            m_currentNetwork = ssid;
            m_isConnected = true;
            emit currentNetworkChanged();
            emit isConnectedChanged();
            emit connectionSuccess(ssid);
            qDebug() << "Successfully connected to:" << ssid;
        } else {
            emit connectionFailed("Failed to activate connection");
        }
    } else {
        emit connectionFailed("Failed to create connection");
    }
}


void WiFiManager::disconnectNetwork()
{
    qDebug() << "Disconnecting from network";

    if (m_nmInterface && m_nmInterface->isValid()) {
        // Deactivate active connections
        QDBusReply<QList<QDBusObjectPath>> connectionsReply =
            m_nmInterface->call("ActiveConnections");

        if (connectionsReply.isValid()) {
            for (const QDBusObjectPath &connPath : connectionsReply.value()) {
                m_nmInterface->call("DeactivateConnection",
                                    QVariant::fromValue(connPath));
            }
        }
    }

    m_isConnected = false;
    m_currentNetwork = "";
    emit isConnectedChanged();
    emit currentNetworkChanged();
}

void WiFiManager::refreshStatus()
{
    updateConnectionState();
}

void WiFiManager::updateConnectionState()
{
    // Get current connection status from NetworkManager
    if (m_nmInterface && m_nmInterface->isValid()) {
        QVariant state = m_nmInterface->property("State");
        m_isConnected = (state.toUInt() == 70); // 70 = NM_STATE_CONNECTED_GLOBAL

        if (m_isConnected) {
            // Get active connection SSID
            // Simplified - full implementation would query active connection details
            m_currentNetwork = "Connected Network";
        } else {
            m_currentNetwork = "Not Connected";
        }

        emit isConnectedChanged();
        emit currentNetworkChanged();
    }
}

void WiFiManager::handleConnectionStateChanged()
{
    updateConnectionState();
}
