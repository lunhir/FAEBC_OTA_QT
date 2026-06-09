#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QTimer>
#include <QDateTime>
#include <QFrame>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTextEdit>
#include <QLineEdit>
#include <QComboBox>
#include <QMap>
#include <QSqlDatabase>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTableWidget>
#include <QStackedWidget>
#include <QSerialPort>
#include <QSerialPortInfo>
#include "mqttclient.h"
#include "protocol.h"

QT_BEGIN_NAMESPACE
namespace Ui { class Widget; }
QT_END_NAMESPACE

// ─── Broker connection profile ─────────────────────────────────────────────
struct BrokerProfile {
    QString host;
    quint16 port     = 1883;
    QString username;
    QString password;   // stored as plaintext in local SQLite
};

// ─── Device record ─────────────────────────────────────────────────────────
struct DeviceRecord {
    QString    uid;           // 24-char lowercase hex string (full 12-byte UID, from MQTT topic)
    QDateTime  lastHeartbeat;
    Protocol::HeartbeatData hb{};
    bool hasHeartbeat = false;
    QString groupName = "未知";

    bool isActive() const {
        return hasHeartbeat &&
               lastHeartbeat.secsTo(QDateTime::currentDateTime()) < 61;
    }
};

// ─── OTA state machine ──────────────────────────────────────────────────────
enum class OtaState {
    Idle,
    WaitBootAck,
    WaitStartAck,
    SendingData,
    WaitEndAck,
};

// ─── OTA channel ────────────────────────────────────────────────────────────
enum class OtaChannel { Mqtt, Serial };

// ─── Main widget ────────────────────────────────────────────────────────────
class Widget : public QWidget
{
    Q_OBJECT

public:
    explicit Widget(QWidget *parent = nullptr);
    ~Widget();

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private slots:
    void onMqttConnected();
    void onMqttDisconnected();
    void onMqttStateChanged(MqttClient::State state);
    void onMessageReceived(const QByteArray &message, const QString &topic);
    void onRefreshTimer();
    void onOtaTimeout();
    void onConnectClicked();
    void onSelectFirmwareClicked();
    void onStartOtaClicked();
    void onTreeSelectionChanged();
    void onTreeContextMenu(const QPoint &pos);
    void onSerialOpenClicked();
    void onSerialReadyRead();
    void onRefreshSerialPorts();
    void onReadDebugClicked();
    void onWriteDibClicked();
    void onLoadDibFromFileClicked();
    void onResetDeviceClicked();
    void onViewDibLogClicked();
    void onRemoteUnlockClicked();
    void onRemoteLockClicked();
    void onChargeAllowClicked();
    void onChargeLimitClicked();
    void onEraseFlashClicked();

private:
    // ── UI ─────────────────────────────────────────────────────────────────
    void buildUI();
    void updateDeviceInfo();
    void addLog(const QString &msg);

    // ── Tree / groups ───────────────────────────────────────────────────────
    QTreeWidgetItem *getOrCreateGroup(const QString &groupName,
                                      const QString &displayName = "");
    void addDeviceToTree(const QString &uid, const QString &groupName = "未知");
    void updateTreeItem(const QString &uid);
    void refreshGroupCount(QTreeWidgetItem *groupItem);

    // ── MQTT ────────────────────────────────────────────────────────────────
    void publishPacket(const QString &deviceId, Protocol::MsgType type,
                       const QByteArray &data = {});

    // ── Serial ──────────────────────────────────────────────────────────────
    void serialSendPacket(Protocol::MsgType type, const QByteArray &data = {});

    // ── Database ────────────────────────────────────────────────────────────
    void setupDatabase();
    void saveDevice(const QString &uid, const QString &groupName = "未知");
    void loadHistoryDevices();
    void saveBrokerProfile(const BrokerProfile &p);
    BrokerProfile loadBrokerProfile();
    void saveAllHeartbeats();
    void loadHeartbeats();

    // ── Protocol handlers ───────────────────────────────────────────────────
    void handleHeartbeat(const QString &deviceId, const QByteArray &payload);
    void handleOtaUplink(const QString &deviceId, Protocol::MsgType type,
                         const QByteArray &data);
    void handleDebugResponse(const QString &deviceId, const QByteArray &data);

    // 下发 DIB 配置到指定设备（MCU 写入 Sector 10）
    void sendDibConfig(const QString &deviceId,
                       const QString &username,
                       const QString &password,
                       const QString &clientId,
                       const QString &hostname,
                       uint16_t port,
                       uint16_t wifiAddr);

    // ── OTA state machine ───────────────────────────────────────────────────
    void startOta();
    void sendOtaData();
    void otaNextStep(Protocol::MsgType type, const QByteArray &data);
    void otaFail(const QString &reason);
    void otaSuccess();

    // ── Helpers ─────────────────────────────────────────────────────────────
    void refreshSerialPorts();       // 刷新端口列表（智能，不重置已选端口）

    // ── Members ─────────────────────────────────────────────────────────────
    Ui::Widget *ui;
    MqttClient *m_mqtt = nullptr;

    // Data
    QMap<QString, DeviceRecord>      m_devices;
    QMap<QString, QTreeWidgetItem *> m_deviceItems;
    QMap<QString, QTreeWidgetItem *> m_groupItems;

    // Left tree
    QTreeWidget *m_tree = nullptr;

    // Top-bar
    QLineEdit   *m_editHost   = nullptr;
    QLineEdit   *m_editPort   = nullptr;
    QLineEdit   *m_editUser   = nullptr;
    QLineEdit   *m_editPass   = nullptr;
    QPushButton *m_btnConnect = nullptr;
    QLabel      *m_lblStatus  = nullptr;

    // Info-tab labels
    QLabel *m_lblUid     = nullptr;
    QLabel *m_lblVersion = nullptr;
    QLabel *m_lblBatch   = nullptr;
    QLabel *m_lblVin     = nullptr;
    QLabel *m_lblLastHb  = nullptr;
    QLabel *m_lblComm    = nullptr;
    QLabel *m_lblVehicle = nullptr;
    QLabel *m_lblHv      = nullptr;
    QLabel *m_lblBrake   = nullptr;
    QLabel *m_lblSpeed   = nullptr;
    QLabel *m_lblMileage = nullptr;
    QLabel *m_lblCharge  = nullptr;
    QLabel *m_lblPos     = nullptr;

    // OTA-tab
    QLabel       *m_lblFirmPath  = nullptr;
    QLabel       *m_lblFirmSize  = nullptr;
    QComboBox    *m_cmbPktSize   = nullptr;   // 包大小选择：2048 / 1024 / 512
    QComboBox    *m_cmbChannel   = nullptr;   // 通道切换：网络 / 串口
    QWidget      *m_serialRow    = nullptr;   // 串口配置行（随通道显隐）
    QProgressBar *m_otaProgress  = nullptr;
    QLabel       *m_lblOtaStatus = nullptr;
    QPushButton  *m_btnStart     = nullptr;

    // Debug-tab
    QPushButton  *m_btnReadDebug  = nullptr;

    // ── DIB 设备参数配置 ───────────────────────────────────────────────
    QLineEdit    *m_dibUsername   = nullptr;
    QLineEdit    *m_dibPassword   = nullptr;
    QLineEdit    *m_dibClientId   = nullptr;
    QLineEdit    *m_dibHostname   = nullptr;
    QLineEdit    *m_dibPort       = nullptr;
    QLineEdit    *m_dibWifiAddr   = nullptr;
    QPushButton  *m_btnWriteDib   = nullptr;
    QPushButton  *m_btnLoadDib    = nullptr;
    QPushButton  *m_btnReset      = nullptr;
    QPushButton  *m_btnViewDibLog = nullptr;
    QTableWidget *m_debugTable    = nullptr;
    QLabel       *m_lblDebugFrom  = nullptr;

    // ── 远程控制 ───────────────────────────────────────────────────────
    QStackedWidget *m_remoteStack    = nullptr;
    QLineEdit      *m_remotePassEdit = nullptr;
    QPushButton    *m_btnRemoteUnlock= nullptr;
    QPushButton    *m_btnRemoteLock  = nullptr;
    QLabel         *m_lblRemoteHint  = nullptr;
    bool            m_remoteUnlocked = false;
    QPushButton    *m_btnChargeAllow = nullptr;
    QPushButton    *m_btnChargeLimit = nullptr;
    QPushButton    *m_btnEraseFlash  = nullptr;

    // Log-tab
    QTextEdit *m_logEdit = nullptr;

    // Timers / DB
    QTimer       *m_refreshTimer = nullptr;
    QTimer       *m_otaTimeout   = nullptr;
    QSqlDatabase  m_db;

    // Selection & OTA
    QString    m_selectedUid;
    OtaState   m_otaState   = OtaState::Idle;
    OtaChannel m_otaChannel = OtaChannel::Mqtt;   // 当前 OTA 使用的通道
    QByteArray m_firmware;
    QString    m_otaUid;       // device_id of device currently being upgraded
    int        m_otaPkt      = 0;
    int        m_otaPktTotal = 0;
    int        m_otaRetry    = 0;   // 当前包已重传次数（最多 3 次）

    // Custom title bar
    QWidget *m_titleBar = nullptr;
    QPoint   m_dragOffset;
    bool     m_dragging = false;

    // Serial port (RS485)
    QSerialPort *m_serial             = nullptr;
    QByteArray   m_serialRxBuf;                // 接收粘包缓冲
    QComboBox   *m_cmbSerialPort      = nullptr;
    QComboBox   *m_cmbBaudRate        = nullptr;
    QPushButton *m_btnSerialOpen      = nullptr;
    QPushButton *m_btnRefreshPorts    = nullptr;
    QLabel      *m_lblSerialStatus    = nullptr;
    QTimer      *m_portScanTimer      = nullptr;
    QStringList  m_lastPortList;               // 上次扫描到的端口列表（用于变化检测）
};

#endif // WIDGET_H
