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
#include <QList>
#include <QQueue>
#include <QSerialPort>
#include <QSerialPortInfo>
#include "mqttclient.h"
#include "mqttotatask.h"
#include "protocol.h"
#include "trace_receiver.h"

QT_BEGIN_NAMESPACE
namespace Ui { class Widget; }
QT_END_NAMESPACE

class QGroupBox;
class LockEventsPage;
class SystemDataPage;

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
    void leaveEvent(QEvent *e) override;
    void showEvent(QShowEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void changeEvent(QEvent *e) override;

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
    void onReadLockEventsClicked();
    void onReadSystemDataClicked();
    void onWriteDibClicked();
    void onLoadDibFromFileClicked();
    void onResetDeviceClicked();
    void onViewDibLogClicked();
    void onProtectedPagesLockClicked();
    void onChargeAllowClicked();
    void onChargeLimitClicked();
    void onEraseFlashClicked();

private:
    // ── UI ─────────────────────────────────────────────────────────────────
    void buildUI();
    void toggleFullScreen();
    void fitWindowToScreen();
    Qt::Edges resizeEdges(const QPoint &point) const;
    QWidget *createProtectedPage(QWidget *content, const QString &pageName);
    void tryUnlockProtectedPages(QLineEdit *passwordEdit, QLabel *hintLabel);
    void setProtectedPagesUnlocked(bool unlocked);
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
    bool serialSendPacket(Protocol::MsgType type, const QByteArray &data = {});

    // ── Database ────────────────────────────────────────────────────────────
    void setupDatabase();
    void seedDatabaseFrom(const QString &dbPath);
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
    void handleChargeMonitorMessage(const QString &deviceId,
                                    Protocol::MsgType type,
                                    const QByteArray &data);
    void sendChargeMonitorCommand(Protocol::ChargeMonitorCommand command);

    // 下发 DIB 配置到指定设备（MCU 写入 Sector 10）
    void sendDibConfig(const QString &deviceId,
                       const QString &username,
                       const QString &password,
                       const QString &clientId,
                       const QString &hostname,
                       uint16_t port,
                       uint16_t wifiAddr);

    // ── Serial OTA state machine ────────────────────────────────────────────
    void sendOtaData();
    void otaNextStep(Protocol::MsgType type, const QByteArray &data);
    void otaFail(const QString &reason);
    void otaSuccess();

    // ── Concurrent MQTT OTA task manager ────────────────────────────────────
    QStringList selectedDeviceUids() const;
    void createMqttOtaTasks();
    void startQueuedMqttOtaTasks();
    void updateMqttOtaQueuePositions();
    void updateMqttOtaSummary();
    void updateOtaControls();
    int runningMqttOtaTaskCount() const;
    MqttOtaTask *mqttOtaTask(quint64 taskId) const;
    int mqttOtaTaskRow(quint64 taskId) const;
    void addMqttOtaTaskRow(MqttOtaTask *task);
    void updateMqttOtaTaskRow(quint64 taskId);
    void clearFinishedMqttOtaTasks();
    void onMqttOtaPacketReady(quint64 taskId,
                              const QString &deviceId,
                              Protocol::MsgType type,
                              const QByteArray &data,
                              int timeoutMs);
    void onMqttOtaTaskFinished(quint64 taskId, bool success);

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
    QGroupBox    *m_serialOtaGroup = nullptr;
    QGroupBox    *m_mqttOtaGroup = nullptr;
    QTableWidget *m_mqttOtaTable = nullptr;
    QLabel       *m_lblMqttOtaSummary = nullptr;
    QPushButton  *m_btnClearFinishedOta = nullptr;

    // Debug-tab
    QPushButton  *m_btnReadDebug  = nullptr;
    LockEventsPage *m_lockEventsPage = nullptr;
    SystemDataPage *m_systemDataPage = nullptr;

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

    TraceReceiver m_traceReceiver;
    QMap<QString,QString> m_traceSummaries;
    // System event monitor tab (protocol names remain ChargeMonitor for compatibility)
    QLabel      *m_lblChargeMonitorState = nullptr;
    QTextEdit   *m_chargeMonitorLog = nullptr;
    QPushButton *m_btnChargeMonitorArm = nullptr;
    QPushButton *m_btnChargeMonitorStop = nullptr;

    // ── 全局维护权限：任一受保护页面解锁后，所有页面同步解锁 ─────────────
    QList<QStackedWidget *> m_protectedPageStacks;
    QList<QLineEdit *>      m_protectedPasswordEdits;
    QList<QLabel *>         m_protectedHintLabels;
    bool                    m_protectedPagesUnlocked = false;
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
    QByteArray m_firmware;
    QString    m_firmwareName;
    QString    m_otaUid;       // device_id of device currently being upgraded
    int        m_otaPkt      = 0;
    int        m_otaPktTotal = 0;
    int        m_otaRetry    = 0;   // 当前包已重传次数（最多 3 次）

    static constexpr int kMaxConcurrentMqttOtaTasks = 5;
    quint64 m_nextMqttOtaTaskId = 1;
    QMap<quint64, MqttOtaTask *> m_mqttOtaTasks;
    QMap<QString, quint64> m_mqttOtaByUid;
    QMap<QString, quint64> m_lastSuccessfulMqttOtaByUid;
    QQueue<quint64> m_mqttOtaQueue;

    // Custom title bar
    QWidget *m_titleBar = nullptr;
    QSize m_normalWindowSize = QSize(1180,720);
    bool m_screenChangePending = false;
    bool m_adjustingWindow = false;
    bool m_windowSignalsConnected = false;
    bool m_wasMaximized = false;

    // Serial port (RS485)
    QSerialPort *m_serial             = nullptr;
    QByteArray   m_serialRxBuf;                // 接收粘包缓冲
    qint64       m_bootRxBytes = 0;            // 本次 BOOT 握手期间收到的原始字节数
    QComboBox   *m_cmbSerialPort      = nullptr;
    QComboBox   *m_cmbBaudRate        = nullptr;
    QPushButton *m_btnSerialOpen      = nullptr;
    QPushButton *m_btnRefreshPorts    = nullptr;
    QLabel      *m_lblSerialStatus    = nullptr;
    QTimer      *m_portScanTimer      = nullptr;
    QStringList  m_lastPortList;               // 上次扫描到的端口列表（用于变化检测）
};

#endif // WIDGET_H
