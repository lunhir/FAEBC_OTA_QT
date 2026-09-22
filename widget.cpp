#include "widget.h"
#include "lockeventspage.h"
#include "systemdatapage.h"
#include "ui_widget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QSplitter>
#include <QScrollArea>
#include <QTabWidget>
#include <QGroupBox>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QProgressBar>
#include <QTextEdit>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QDirIterator>
#include <QDateTime>
#include <QFont>
#include <QMenu>
#include <QAction>
#include <QHeaderView>
#include <QSqlQuery>
#include <QSqlError>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QMouseEvent>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDialog>
#include <QAbstractItemView>
#include <QShortcut>
#include <QKeySequence>
#include <QGuiApplication>
#include <QClipboard>
#include <QLocale>

namespace {

QString chargeMonitorReasonText(const QString &reason)
{
    if (reason == "system_reset")          return "设备启动/复位诊断";
    if (reason == "can_tx_busy")           return "CAN1发送邮箱持续忙";
    if (reason == "can_rx_queue_drop")     return "CAN1接收队列丢帧";
    if (reason == "system_feed_delayed")   return "系统常规喂狗已延迟5秒";
    if (reason == "armed")                 return "检测已启动";
    if (reason == "disabled")              return "检测已停止";
    if (reason == "charge_started")         return "车辆开始充电";
    if (reason == "charge_completed")       return "充电正常完成";
    if (reason == "charge_stopped")         return "充电提前停止";
    if (reason == "bms_charge_fault")       return "BMS报告充电故障";
    if (reason == "can_bus_off")            return "CAN1进入Bus-Off";
    if (reason == "can_recover_failed")     return "CAN1恢复失败";
    if (reason == "can_auto_recovered")     return "CAN1控制器自动恢复监听";
    if (reason == "can_recovered")          return "CAN1控制器强制恢复监听";
    if (reason == "can_tx_error")           return "CAN1发送失败";
    if (reason == "can_no_ack")             return "CAN1无应答";
    if (reason == "can_tx_queue_drop")      return "CAN1发送队列丢帧";
    if (reason == "can_error")              return "CAN1发生通信错误";
    if (reason == "can_tx_silent")          return "CAN1连续3秒没有发送";
    if (reason == "can_tx_resumed")         return "CAN1发送已经恢复";
    if (reason == "system_sleep_warning")   return "设备将在约1分钟后休眠";
    if (reason == "system_sleep")           return "设备正在进入休眠";
    return "未知事件（" + reason + "）";
}

QString chargeMonitorReasonAdvice(const QString &reason)
{
    if (reason == "system_reset")
        return "请比较复位原因与累计次数；故障寄存器保留最近一次异常，不一定属于最近一次启动。";
    if (reason == "can_tx_busy")
        return "发送等待已返回忙，请结合邮箱数量、ESR和成功提交计数判断；设备没有执行新增恢复操作。";
    if (reason == "can_rx_queue_drop")
        return "接收队列入队失败，请结合任务运行情况判断。";
    if (reason == "system_feed_delayed")
        return "常规喂狗超过5秒未执行，请查看系统任务阶段；这条预警本身不代表已经复位。";
    if (reason == "can_tx_queue_drop")
        return "本次检测已新增至少3次发送队列丢帧，请检查任务调度、队列容量和发送任务运行情况。";
    if (reason == "can_bus_off")
        return "CAN控制器已Bus-Off，请检查终端电阻、波特率、线束和外部节点供电。";
    if (reason == "can_recover_failed")
        return "自动恢复未成功，需要继续检查CAN外设状态和物理总线。";
    if (reason == "can_recovered" || reason == "can_auto_recovered")
        return "CAN1控制器已恢复监听，但不代表外部节点已经应答；请同时检查无应答计数。";
    if (reason == "can_no_ack")
        return "连续发送未收到外部节点ACK，请检查外部节点供电、波特率、正常/只听模式、终端电阻和CAN线束。";
    if (reason == "can_tx_silent")
        return "系统检测期间没有新的CAN帧提交，优先检查周期定时器和BMS_CAN_task。";
    if (reason == "can_tx_resumed")
        return "CAN1成功提交计数重新增长，发送静默已经解除，系统继续监测。";
    if (reason == "system_sleep_warning")
        return "休眠条件已持续约1分钟；若唤醒输入和WiFi状态仍未恢复，设备约1分钟后进入休眠。";
    if (reason == "bms_charge_fault")
        return "BMS充电状态为故障，请结合BMS报文和故障码判断。";
    return {};
}

QString bmsChargeStateText(int state)
{
    switch (state) {
        case 0: return "空闲（0）";
        case 1: return "充电中（1）";
        case 2: return "充电完成（2）";
        case 3: return "充电故障（3）";
        default: return QString("未知（%1）").arg(state);
    }
}

QString chargeMonitorStateText(int state)
{
    switch (static_cast<Protocol::ChargeMonitorState>(state)) {
        case Protocol::ChargeMonitorState::Disabled: return "未布防";
        case Protocol::ChargeMonitorState::Armed:    return "系统事件监测中（当前未充电）";
        case Protocol::ChargeMonitorState::Charging: return "系统事件监测中（正在充电）";
    }
    return QString("未知（%1）").arg(state);
}

QString canHalStateText(int state)
{
    switch (state) {
        case 0: return "复位";
        case 1: return "就绪";
        case 2: return "正常监听";
        case 3: return "等待休眠";
        case 4: return "已休眠";
        case 5: return "错误";
        default: return QString("未知（%1）").arg(state);
    }
}

QString formattedCount(const QJsonObject &object, const char *key)
{
    static const QLocale numberLocale(QLocale::English);
    if (!object.contains(key)) return "未采集";
    return numberLocale.toString(object.value(key).toVariant().toLongLong());
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
//  Global stylesheet
// ─────────────────────────────────────────────────────────────────────────────
static const char *APP_QSS = R"(
/* ══ CYBERPUNK THEME ═══════════════════════════════════════════ */
QWidget {
    font-family: "Microsoft YaHei", "Consolas", "SimHei", sans-serif;
    font-size: 12px;
    background-color: #05060E;
    color: #B8F5FF;
}
/* ── Custom title bar ── */
QFrame#titleBar {
    background: qlineargradient(x1:0,y1:0,x2:1,y2:0,
        stop:0 #0A0E1C, stop:0.5 #12182A, stop:1 #0A0E1C);
    border: 1px solid #FF2E97;
    border-bottom: 2px solid #00E5FF;
    border-radius: 4px;
}
/* ── Cards ── */
QFrame#topCard {
    background: qlineargradient(x1:0,y1:0,x2:1,y2:0,
        stop:0 #0A0E1C, stop:0.5 #0D1424, stop:1 #0A0E1C);
    border: 1px solid #00E5FF;
    border-radius: 4px;
}
QFrame#leftCard {
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
        stop:0 #070A18, stop:1 #05060E);
    border: 1px solid #00B8D4;
    border-left: 2px solid #00E5FF;
    border-radius: 4px;
}
QFrame#rightCard {
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
        stop:0 #070A18, stop:1 #05060E);
    border: 1px solid #00B8D4;
    border-right: 2px solid #FF2E97;
    border-radius: 4px;
}
/* ── Splitter handle ── */
QSplitter::handle {
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
        stop:0 #00E5FF, stop:0.5 #FF2E97, stop:1 #00E5FF);
    width: 2px;
}
/* ── Buttons ── */
QPushButton {
    border: 1px solid #0097A7;
    border-radius: 2px;
    padding: 4px 12px;
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
        stop:0 #0A1A2A, stop:1 #050A14);
    color: #4DD0E1;
    font-weight: bold;
    letter-spacing: 1px;
}
QPushButton:hover {
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
        stop:0 #003A4A, stop:1 #001820);
    border: 1px solid #00E5FF;
    color: #00E5FF;
}
QPushButton:pressed {
    background: #00E5FF;
    color: #05060E;
    border: 1px solid #FF2E97;
}
QPushButton:disabled {
    background: #0A0D16;
    color: #1F3844;
    border: 1px solid #112028;
}
/* ── Line-edits ── */
QLineEdit {
    border: 1px solid #0097A7;
    border-radius: 2px;
    padding: 4px 8px;
    background: #030610;
    color: #00E5FF;
    selection-background-color: #FF2E97;
    selection-color: #05060E;
}
QLineEdit:focus {
    border: 1px solid #FF2E97;
    background: #08101C;
}
/* ── Labels ── */
QLabel { background: transparent; color: #7FDEEA; }
/* ── Tab widget ── */
QTabWidget::pane {
    border: 1px solid #00B8D4;
    border-top: 2px solid #00E5FF;
    border-radius: 0;
    background: #05080F;
    top: -1px;
}
QTabBar::tab {
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
        stop:0 #070B16, stop:1 #030610);
    border: 1px solid #0A2A35;
    border-bottom: none;
    padding: 6px 18px;
    margin-right: 2px;
    color: #3F7B87;
    font-size: 12px;
    font-weight: bold;
    letter-spacing: 2px;
}
QTabBar::tab:selected {
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
        stop:0 #00E5FF, stop:0.08 #0A1A2A, stop:1 #05080F);
    color: #00E5FF;
    border: 1px solid #00E5FF;
    border-bottom: none;
}
QTabBar::tab:hover:!selected {
    background: #0A1A2A;
    color: #7FDEEA;
}
/* ── Progress bar ── */
QProgressBar {
    border: 1px solid #00B8D4;
    border-radius: 2px;
    background: #020510;
    color: #E0FFFF;
    text-align: center;
    min-height: 18px;
    max-height: 18px;
    font-size: 11px;
    font-weight: bold;
    letter-spacing: 1px;
}
QProgressBar::chunk {
    background: qlineargradient(x1:0,y1:0,x2:1,y2:0,
        stop:0 #FF2E97, stop:0.5 #B14BFF, stop:1 #00E5FF);
}
/* ── Tree widget ── */
QTreeWidget {
    border: 1px solid #0A2A35;
    border-radius: 2px;
    background: #020510;
    alternate-background-color: #05090F;
    outline: none;
    show-decoration-selected: 1;
    font-family: "Consolas", "Microsoft YaHei", monospace;
    font-size: 12px;
}
QTreeWidget::item {
    height: 26px;
    padding: 0 4px;
    border-bottom: 1px solid #081620;
}
QTreeWidget::item:selected {
    background: qlineargradient(x1:0,y1:0,x2:1,y2:0,
        stop:0 #FF2E97, stop:0.02 #1A0820, stop:1 #001A28);
    color: #00E5FF;
    border-left: 2px solid #FF2E97;
}
QTreeWidget::item:hover:!selected {
    background: #07121C;
    color: #B8F5FF;
}
QTreeWidget::branch:has-children:!has-siblings:closed,
QTreeWidget::branch:closed:has-children:has-siblings {
    border-image: none;
    image: none;
}
QHeaderView::section {
    background: #05090F;
    border: none;
    border-bottom: 1px solid #00B8D4;
    color: #00E5FF;
    padding: 4px;
    font-weight: bold;
    letter-spacing: 1px;
}
/* ── GroupBox ── */
QGroupBox {
    font-weight: bold;
    border: 1px solid #00B8D4;
    border-radius: 2px;
    margin-top: 10px;
    background: #03070F;
    padding-top: 6px;
    color: #00E5FF;
    font-size: 12px;
    letter-spacing: 2px;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 10px;
    padding: 0 8px;
    color: #FF2E97;
    background: #05060E;
}
/* ── Scroll-area / bar ── */
QScrollArea { border: none; background: transparent; }
QScrollBar:vertical {
    background: #020510; width: 8px; border: none;
    border-left: 1px solid #0A2A35;
}
QScrollBar::handle:vertical {
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
        stop:0 #00E5FF, stop:1 #FF2E97);
    border-radius: 2px; min-height: 24px;
}
QScrollBar::handle:vertical:hover {
    background: #FF2E97;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar:horizontal {
    background: #020510; height: 8px; border: none;
    border-top: 1px solid #0A2A35;
}
QScrollBar::handle:horizontal {
    background: qlineargradient(x1:0,y1:0,x2:1,y2:0,
        stop:0 #00E5FF, stop:1 #FF2E97);
    border-radius: 2px; min-width: 24px;
}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
/* ── Context menu ── */
QMenu {
    background: #05090F;
    border: 1px solid #00E5FF;
    border-radius: 2px;
    padding: 4px 0;
}
QMenu::item {
    padding: 6px 24px 6px 14px;
    color: #7FDEEA;
    font-weight: bold;
}
QMenu::item:selected {
    background: qlineargradient(x1:0,y1:0,x2:1,y2:0,
        stop:0 #FF2E97, stop:1 #00E5FF);
    color: #05060E;
}
QMenu::separator { height: 1px; background: #00B8D4; margin: 4px 8px; }
/* ── Tooltip ── */
QToolTip {
    background: #03070F;
    color: #00E5FF;
    border: 1px solid #FF2E97;
    border-radius: 2px;
    padding: 4px 8px;
    font-size: 11px;
    font-family: "Consolas", monospace;
}
/* ── MessageBox / Dialog ── */
QMessageBox, QInputDialog {
    background: #05060E;
    color: #B8F5FF;
}
/* ── ComboBox ── */
QComboBox {
    border: 1px solid #0097A7;
    border-radius: 2px;
    padding: 4px 8px;
    background: #030610;
    color: #00E5FF;
    font-weight: bold;
}
QComboBox:hover { border: 1px solid #00E5FF; }
QComboBox::drop-down {
    border: none;
    background: transparent;
    width: 18px;
}
QComboBox::down-arrow {
    image: none;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 5px solid #00E5FF;
    margin-right: 6px;
}
QComboBox QAbstractItemView {
    background: #05090F;
    border: 1px solid #00E5FF;
    selection-background-color: #FF2E97;
    selection-color: #05060E;
    color: #7FDEEA;
    outline: none;
}
/* ── QTextEdit ── */
QTextEdit {
    background: #020510;
    color: #00E5FF;
    border: 1px solid #0A2A35;
    border-radius: 2px;
    selection-background-color: #FF2E97;
    selection-color: #05060E;
}
)";

// ─────────────────────────────────────────────────────────────────────────────
//  Frameless window dragging
// ─────────────────────────────────────────────────────────────────────────────
void Widget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && m_titleBar &&
        m_titleBar->geometry().contains(e->pos())) {
        m_dragging  = true;
        m_dragOffset = e->globalPosition().toPoint() - frameGeometry().topLeft();
        e->accept();
        return;
    }
    QWidget::mousePressEvent(e);
}

void Widget::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragging && (e->buttons() & Qt::LeftButton)) {
        move(e->globalPosition().toPoint() - m_dragOffset);
        e->accept();
        return;
    }
    QWidget::mouseMoveEvent(e);
}

void Widget::mouseReleaseEvent(QMouseEvent *e)
{
    m_dragging = false;
    QWidget::mouseReleaseEvent(e);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────
Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{
    ui->setupUi(this);

    m_refreshTimer = new QTimer(this);
    m_otaTimeout   = new QTimer(this);
    m_otaTimeout->setSingleShot(true);

    m_serial = new QSerialPort(this);
    connect(m_serial, &QSerialPort::readyRead, this, &Widget::onSerialReadyRead);
    connect(m_serial, &QSerialPort::errorOccurred, this,
            [this](QSerialPort::SerialPortError error) {
        if (error != QSerialPort::NoError)
            addLog(QString("串口错误 (%1): %2").arg(int(error)).arg(m_serial->errorString()));
    });

    buildUI();
    setupDatabase();

    m_mqtt = new MqttClient(this);
    m_mqtt->setKeepAlive(60);
    connect(m_mqtt, &MqttClient::connected,       this, &Widget::onMqttConnected);
    connect(m_mqtt, &MqttClient::disconnected,    this, &Widget::onMqttDisconnected);
    connect(m_mqtt, &MqttClient::stateChanged,    this, &Widget::onMqttStateChanged);
    connect(m_mqtt, &MqttClient::messageReceived, this, &Widget::onMessageReceived);
    connect(m_mqtt, &MqttClient::error, this,
            [this](const QString &msg){ addLog("MQTT 错误: " + msg); });

    connect(m_refreshTimer, &QTimer::timeout, this, &Widget::onRefreshTimer);
    connect(m_otaTimeout,   &QTimer::timeout, this, &Widget::onOtaTimeout);

    m_refreshTimer->start(1000);
    loadHistoryDevices();
}

Widget::~Widget()
{
    saveAllHeartbeats();
    delete ui;
}

QWidget *Widget::createProtectedPage(QWidget *content,
                                     const QString &pageName)
{
    auto *root = new QWidget;
    root->setStyleSheet("background:transparent;");
    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(12, 12, 12, 12);

    auto *stack = new QStackedWidget(root);

    // Page 0：密码门。每个受保护页面都有入口，但共享同一个解锁状态。
    auto *gate = new QWidget;
    auto *gateLayout = new QVBoxLayout(gate);
    gateLayout->setContentsMargins(0, 30, 0, 0);
    gateLayout->setSpacing(12);
    gateLayout->setAlignment(Qt::AlignTop);

    auto *title = new QLabel(pageName + " · 受密码保护");
    title->setStyleSheet("color:#FF2E97;font-weight:bold;letter-spacing:2px;"
                         "font-size:14px;background:transparent;");
    title->setAlignment(Qt::AlignCenter);
    gateLayout->addWidget(title);

    auto *form = new QHBoxLayout;
    form->setAlignment(Qt::AlignCenter);
    auto *passwordLabel = new QLabel("密码：");
    passwordLabel->setStyleSheet(
        "color:#FF2E97;font-weight:bold;background:transparent;");
    auto *passwordEdit = new QLineEdit;
    passwordEdit->setEchoMode(QLineEdit::Password);
    passwordEdit->setMaxLength(32);
    passwordEdit->setFixedWidth(220);
    passwordEdit->setPlaceholderText("请输入维护访问密码");
    passwordEdit->setStyleSheet(
        "QLineEdit{background:#0A0F1F;color:#00E5FF;"
        "border:1px solid #00B8D4;border-radius:2px;padding:4px 6px;}"
        "QLineEdit:focus{border:1px solid #FF2E97;}");
    auto *unlockButton = new QPushButton("解锁");
    unlockButton->setFixedWidth(80);
    form->addWidget(passwordLabel);
    form->addWidget(passwordEdit);
    form->addWidget(unlockButton);
    gateLayout->addLayout(form);

    auto *hint = new QLabel(" ");
    hint->setAlignment(Qt::AlignCenter);
    hint->setStyleSheet("color:#FFB000;background:transparent;");
    gateLayout->addWidget(hint);
    gateLayout->addStretch();

    // Page 1：统一的已解锁标题栏 + 原页面内容。
    auto *unlockedPage = new QWidget;
    auto *unlockedLayout = new QVBoxLayout(unlockedPage);
    unlockedLayout->setContentsMargins(0, 0, 0, 0);
    unlockedLayout->setSpacing(8);
    auto *bar = new QHBoxLayout;
    auto *unlockedLabel = new QLabel("● 维护权限已解锁");
    unlockedLabel->setStyleSheet(
        "color:#00FF88;font-weight:bold;background:transparent;");
    auto *lockButton = new QPushButton("重新锁定");
    lockButton->setFixedWidth(100);
    bar->addWidget(unlockedLabel);
    bar->addStretch();
    bar->addWidget(lockButton);
    unlockedLayout->addLayout(bar);
    auto *separator = new QFrame;
    separator->setFrameShape(QFrame::HLine);
    separator->setStyleSheet("background:#00B8D4;border:none;max-height:1px;");
    unlockedLayout->addWidget(separator);
    unlockedLayout->addWidget(content, 1);

    stack->addWidget(gate);
    stack->addWidget(unlockedPage);
    stack->setCurrentIndex(m_protectedPagesUnlocked ? 1 : 0);
    rootLayout->addWidget(stack);

    m_protectedPageStacks.append(stack);
    m_protectedPasswordEdits.append(passwordEdit);
    m_protectedHintLabels.append(hint);

    connect(unlockButton, &QPushButton::clicked, this,
            [this, passwordEdit, hint]() {
                tryUnlockProtectedPages(passwordEdit, hint);
            });
    connect(passwordEdit, &QLineEdit::returnPressed, this,
            [this, passwordEdit, hint]() {
                tryUnlockProtectedPages(passwordEdit, hint);
            });
    connect(lockButton, &QPushButton::clicked,
            this, &Widget::onProtectedPagesLockClicked);

    return root;
}

void Widget::tryUnlockProtectedPages(QLineEdit *passwordEdit, QLabel *hintLabel)
{
    static const QString kMaintenancePassword = "12342234";

    if (passwordEdit != nullptr && passwordEdit->text() == kMaintenancePassword) {
        setProtectedPagesUnlocked(true);
        addLog("[维护权限] 已解锁 OTA、调试数据、系统事件、远程控制和设备参数");
        return;
    }

    if (hintLabel != nullptr)
        hintLabel->setText("密码错误");
    if (passwordEdit != nullptr)
        passwordEdit->selectAll();
    addLog("[维护权限] 密码错误，拒绝解锁");
}

void Widget::setProtectedPagesUnlocked(bool unlocked)
{
    m_protectedPagesUnlocked = unlocked;
    for (auto *edit : m_protectedPasswordEdits)
        edit->clear();
    for (auto *hint : m_protectedHintLabels)
        hint->setText(" ");
    for (auto *stack : m_protectedPageStacks)
        stack->setCurrentIndex(unlocked ? 1 : 0);
}

// ─────────────────────────────────────────────────────────────────────────────
//  UI Construction
// ─────────────────────────────────────────────────────────────────────────────
void Widget::buildUI()
{
    setStyleSheet(APP_QSS);
    setWindowTitle("▍ OTA CONTROL // 远程升级管理终端");
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    resize(1180, 720);
    setMinimumSize(980, 560);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(4);

    // ── Custom cyberpunk title bar ────────────────────────────────────────
    m_titleBar = new QFrame;
    m_titleBar->setObjectName("titleBar");
    m_titleBar->setFixedHeight(32);
    auto *tbH = new QHBoxLayout(m_titleBar);
    tbH->setContentsMargins(14, 0, 6, 0);
    tbH->setSpacing(8);

    auto *lblBrand = new QLabel("◆");
    lblBrand->setStyleSheet("color:#FF2E97;font-size:16px;font-weight:bold;"
                            "background:transparent;border:none;");
    tbH->addWidget(lblBrand);

    auto *lblWinTitle = new QLabel("OTA  CONTROL  ::  远程升级管理终端");
    lblWinTitle->setStyleSheet("color:#00E5FF;font-size:12px;font-weight:bold;"
                               "letter-spacing:4px;"
                               "background:transparent;border:none;");
    tbH->addWidget(lblWinTitle);
    tbH->addStretch();

    auto *lblSess = new QLabel("SESSION # 0x1A2F");
    lblSess->setStyleSheet("color:#7FDEEA;font-size:10px;letter-spacing:2px;"
                           "font-family:Consolas,monospace;"
                           "background:transparent;border:none;");
    tbH->addWidget(lblSess);
    tbH->addSpacing(12);

    const char *winBtnQss =
        "QPushButton{background:transparent;color:%1;"
        "border:1px solid %1;border-radius:2px;"
        "font-size:11px;font-weight:bold;min-width:32px;min-height:20px;}"
        "QPushButton:hover{background:%1;color:#05060E;}"
        "QPushButton:pressed{background:%2;color:#05060E;border-color:%2;}";

    auto *btnMin = new QPushButton("─");
    btnMin->setCursor(Qt::PointingHandCursor);
    btnMin->setStyleSheet(QString(winBtnQss).arg("#00E5FF", "#0097A7"));
    btnMin->setFocusPolicy(Qt::NoFocus);
    tbH->addWidget(btnMin);

    auto *btnClose = new QPushButton("✕");
    btnClose->setCursor(Qt::PointingHandCursor);
    btnClose->setStyleSheet(QString(winBtnQss).arg("#FF2E97", "#B14BFF"));
    btnClose->setFocusPolicy(Qt::NoFocus);
    tbH->addWidget(btnClose);

    connect(btnMin,   &QPushButton::clicked, this, &Widget::showMinimized);
    connect(btnClose, &QPushButton::clicked, this, &Widget::close);

    root->addWidget(m_titleBar);

    // ── Top bar ───────────────────────────────────────────────────────────
    auto *topCard = new QFrame;
    topCard->setObjectName("topCard");
    auto *topH = new QHBoxLayout(topCard);
    topH->setContentsMargins(10, 4, 10, 4);
    topH->setSpacing(6);

    auto *lblTitle = new QLabel("▍ OTA CONTROL  //  远程升级管理终端");
    lblTitle->setStyleSheet("font-size:16px;font-weight:bold;color:#00E5FF;"
                            "letter-spacing:3px;"
                            "background:transparent;border:none;");
    topH->addWidget(lblTitle);
    topH->addStretch();

    auto *vline = new QFrame;
    vline->setFrameShape(QFrame::VLine);
    vline->setStyleSheet("background:#00E5FF;border:none;");
    vline->setFixedWidth(1);
    topH->addWidget(vline);
    topH->addSpacing(4);

    topH->addWidget(new QLabel("Broker 地址:"));
    m_editHost = new QLineEdit(Protocol::BROKER_HOST);
    m_editHost->setFixedWidth(200);
    m_editHost->setPlaceholderText("IP 或域名，如 mqtt.example.com");
    m_editHost->setToolTip("支持 IPv4 地址或域名（如 mqtt.broker.com）");
    topH->addWidget(m_editHost);

    topH->addWidget(new QLabel("端口:"));
    m_editPort = new QLineEdit(QString::number(Protocol::BROKER_PORT));
    m_editPort->setFixedWidth(60);
    m_editPort->setPlaceholderText("1883");
    topH->addWidget(m_editPort);

    topH->addWidget(new QLabel("用户名:"));
    m_editUser = new QLineEdit;
    m_editUser->setFixedWidth(100);
    m_editUser->setPlaceholderText("可选");
    topH->addWidget(m_editUser);

    topH->addWidget(new QLabel("密码:"));
    m_editPass = new QLineEdit;
    m_editPass->setFixedWidth(100);
    m_editPass->setPlaceholderText("可选");
    m_editPass->setEchoMode(QLineEdit::Password);
    topH->addWidget(m_editPass);

    m_btnConnect = new QPushButton("▶ CONNECT");
    m_btnConnect->setFixedWidth(100);
    m_btnConnect->setStyleSheet(
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 #00E5FF,stop:1 #0097A7);color:#05060E;border:1px solid #00E5FF;"
            "border-radius:2px;padding:5px 0;font-weight:bold;letter-spacing:2px;}"
        "QPushButton:hover{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 #FF2E97,stop:1 #B14BFF);color:#05060E;border:1px solid #FF2E97;}"
        "QPushButton:pressed{background:#FF2E97;color:#05060E;}");
    topH->addWidget(m_btnConnect);

    m_lblStatus = new QLabel("●  OFFLINE");
    m_lblStatus->setStyleSheet("color:#FF2E97;font-weight:bold;min-width:100px;"
                               "letter-spacing:2px;"
                               "background:transparent;border:none;");
    topH->addWidget(m_lblStatus);

    root->addWidget(topCard);

    // ── Splitter ──────────────────────────────────────────────────────────
    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setHandleWidth(6);
    root->addWidget(splitter, 1);

    // ─── Left: tree panel ─────────────────────────────────────────────────
    auto *leftCard   = new QFrame;
    leftCard->setObjectName("leftCard");
    leftCard->setMinimumWidth(340);
    auto *leftLayout = new QVBoxLayout(leftCard);
    leftLayout->setContentsMargins(8, 8, 8, 8);
    leftLayout->setSpacing(6);

    // Header row
    auto *leftHdr = new QHBoxLayout;
    auto *lblHdr  = new QLabel("▍ DEVICE  MATRIX");
    lblHdr->setStyleSheet("font-size:13px;font-weight:bold;color:#00E5FF;"
                          "letter-spacing:3px;"
                          "background:transparent;border:none;");
    leftHdr->addWidget(lblHdr);
    leftHdr->addStretch();

    auto *btnAddGrp = new QPushButton("+ NEW GROUP");
    btnAddGrp->setFixedHeight(26);
    btnAddGrp->setStyleSheet(
        "QPushButton{background:transparent;color:#FF2E97;border:1px solid #FF2E97;"
        "border-radius:2px;padding:2px 10px;font-size:11px;font-weight:bold;letter-spacing:1px;}"
        "QPushButton:hover{background:#1A0820;color:#FF2E97;border:1px solid #FF2E97;}");
    leftHdr->addWidget(btnAddGrp);
    leftLayout->addLayout(leftHdr);

    // Tree
    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(2);
    m_tree->setIndentation(16);
    m_tree->setAnimated(true);
    // OTA 页面支持一次选中多台设备；其他页面仍以 currentItem 作为当前设备。
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_tree->header()->resizeSection(1, 70);
    leftLayout->addWidget(m_tree, 1);

    splitter->addWidget(leftCard);

    // ─── Right: tabs ──────────────────────────────────────────────────────
    auto *rightCard   = new QFrame;
    rightCard->setObjectName("rightCard");
    auto *rightLayout = new QVBoxLayout(rightCard);
    rightLayout->setContentsMargins(6, 6, 6, 6);
    rightLayout->setSpacing(0);

    auto *tabs = new QTabWidget;

    // ── Tab 1: Device Info ────────────────────────────────────────────────
    auto *infoW    = new QWidget;
    infoW->setStyleSheet("background:transparent;");
    auto *infoForm = new QFormLayout(infoW);
    infoForm->setSpacing(6);
    infoForm->setContentsMargins(12, 10, 12, 10);
    infoForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    infoForm->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    infoForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    infoForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    infoForm->setHorizontalSpacing(12);

    auto makeVal = [&]() -> QLabel * {
        auto *l = new QLabel("-");
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        l->setWordWrap(true);
        l->setStyleSheet("background:transparent;border:none;color:#00E5FF;"
                         "font-family:Consolas,monospace;");
        return l;
    };
    auto addFormRow = [&](const QString &labelText, QLabel *&field) {
        auto *lbl = new QLabel(labelText);
        lbl->setStyleSheet("color:#FF2E97;font-weight:bold;letter-spacing:1px;"
                           "background:transparent;border:none;");
        field = makeVal();
        infoForm->addRow(lbl, field);
    };

    addFormRow("UID:",      m_lblUid);
    addFormRow("固件版本:", m_lblVersion);
    addFormRow("批次:",     m_lblBatch);
    addFormRow("VIN码:",    m_lblVin);
    addFormRow("最后心跳:", m_lblLastHb);

    auto *sep = new QFrame; sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("background:#00B8D4;border:none;max-height:1px;");
    infoForm->addRow(sep);

    addFormRow("通讯状态:", m_lblComm);
    addFormRow("车辆状态:", m_lblVehicle);
    addFormRow("高压状态:", m_lblHv);
    addFormRow("手刹状态:", m_lblBrake);
    addFormRow("车速:",     m_lblSpeed);
    addFormRow("总里程:",   m_lblMileage);
    addFormRow("充电状态:", m_lblCharge);
    addFormRow("位置:",     m_lblPos);

    tabs->addTab(infoW, "  设备信息  ");

    // ── Tab 2: OTA ────────────────────────────────────────────────────────
    auto *otaW  = new QWidget;
    otaW->setStyleSheet("background:transparent;");
    auto *otaVL = new QVBoxLayout(otaW);
    otaVL->setContentsMargins(12, 12, 12, 12);
    otaVL->setSpacing(10);

    // ── 固件 + 通道配置组 ─────────────────────────────────────────────
    auto *fwGrp = new QGroupBox("固件配置");
    auto *fwVL  = new QVBoxLayout(fwGrp);
    fwVL->setSpacing(8);

    // 固件选择行
    auto *fwRow = new QHBoxLayout;
    m_lblFirmPath = new QLabel("// NO FIRMWARE LOADED");
    m_lblFirmPath->setStyleSheet("color:#3F7B87;background:transparent;border:none;"
                                 "font-family:Consolas,monospace;");
    m_lblFirmPath->setWordWrap(true);
    auto *btnSel = new QPushButton("选择 .bin ...");
    btnSel->setFixedWidth(110);
    fwRow->addWidget(m_lblFirmPath, 1);
    fwRow->addWidget(btnSel);
    fwVL->addLayout(fwRow);

    m_lblFirmSize = new QLabel("SIZE: —");
    m_lblFirmSize->setStyleSheet("color:#7FDEEA;font-size:11px;"
                                 "font-family:Consolas,monospace;"
                                 "background:transparent;border:none;");
    fwVL->addWidget(m_lblFirmSize);

    // 包大小 + 通道切换 同一行
    auto *cfgRow = new QHBoxLayout;
    auto *lblPkt = new QLabel("包大小:");
    lblPkt->setStyleSheet("background:transparent;border:none;");
    m_cmbPktSize = new QComboBox;
    m_cmbPktSize->addItem("1024 字节", 1024);
    m_cmbPktSize->addItem("2048 字节", 2048);
    m_cmbPktSize->addItem(" 512 字节",  512);
    m_cmbPktSize->setFixedWidth(110);
    cfgRow->addWidget(lblPkt);
    cfgRow->addWidget(m_cmbPktSize);
    cfgRow->addSpacing(20);

    auto *lblCh = new QLabel("通道:");
    lblCh->setStyleSheet("background:transparent;border:none;");
    m_cmbChannel = new QComboBox;
    m_cmbChannel->addItem("🌐  网络 (MQTT)", (int)OtaChannel::Mqtt);
    m_cmbChannel->addItem("🔌  串口 (RS485)", (int)OtaChannel::Serial);
    m_cmbChannel->setFixedWidth(150);
    cfgRow->addWidget(lblCh);
    cfgRow->addWidget(m_cmbChannel);
    cfgRow->addStretch();
    fwVL->addLayout(cfgRow);

    // 串口配置行（仅串口通道显示）
    m_serialRow = new QWidget;
    m_serialRow->setStyleSheet("background:transparent;");
    auto *serHL = new QHBoxLayout(m_serialRow);
    serHL->setContentsMargins(0, 0, 0, 0);
    serHL->setSpacing(6);

    serHL->addWidget(new QLabel("端口:"));
    m_cmbSerialPort = new QComboBox;
    m_cmbSerialPort->setFixedWidth(100);
    for (auto &info : QSerialPortInfo::availablePorts())
        m_cmbSerialPort->addItem(info.portName());
    serHL->addWidget(m_cmbSerialPort);

    m_btnRefreshPorts = new QPushButton("↻");
    m_btnRefreshPorts->setFixedSize(26, 24);
    m_btnRefreshPorts->setToolTip("刷新串口列表");
    m_btnRefreshPorts->setStyleSheet(
        "QPushButton{border:1px solid #0097A7;border-radius:2px;"
        "background:#050A14;color:#4DD0E1;font-size:14px;padding:0px;}"
        "QPushButton:hover{border-color:#00E5FF;color:#00E5FF;background:#003A4A;}"
        "QPushButton:pressed{background:#00E5FF;color:#05060E;}");
    serHL->addWidget(m_btnRefreshPorts);

    serHL->addWidget(new QLabel("波特率:"));
    m_cmbBaudRate = new QComboBox;
    m_cmbBaudRate->setFixedWidth(90);
    m_cmbBaudRate->addItem("115200", 115200);
    m_cmbBaudRate->addItem("57600",  57600);
    m_cmbBaudRate->addItem("38400",  38400);
    serHL->addWidget(m_cmbBaudRate);

    m_btnSerialOpen = new QPushButton("OPEN");
    m_btnSerialOpen->setFixedWidth(80);
    serHL->addWidget(m_btnSerialOpen);

    m_lblSerialStatus = new QLabel("●  CLOSED");
    m_lblSerialStatus->setStyleSheet("color:#FF2E97;font-weight:bold;letter-spacing:1px;"
                                     "background:transparent;border:none;");
    serHL->addWidget(m_lblSerialStatus);
    serHL->addStretch();

    m_serialRow->setVisible(false);   // 默认隐藏
    fwVL->addWidget(m_serialRow);

    otaVL->addWidget(fwGrp);

    // ── 开始/新建任务按钮（按当前通道切换语义）──────────────────────
    m_btnStart = new QPushButton("＋   新建升级任务");
    m_btnStart->setEnabled(false);
    m_btnStart->setMinimumHeight(42);
    m_btnStart->setStyleSheet(
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
            "stop:0 #FF2E97,stop:0.5 #B14BFF,stop:1 #00E5FF);"
            "color:#05060E;font-size:14px;font-weight:bold;letter-spacing:4px;"
            "border:1px solid #00E5FF;border-radius:2px;}"
        "QPushButton:hover{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
            "stop:0 #00E5FF,stop:0.5 #B14BFF,stop:1 #FF2E97);"
            "border:1px solid #FF2E97;}"
        "QPushButton:pressed{background:#FF2E97;color:#FFFFFF;}"
        "QPushButton:disabled{background:#0A0D16;color:#1F3844;"
            "border:1px solid #112028;}");
    otaVL->addWidget(m_btnStart);

    // ── MQTT 并发升级任务 ─────────────────────────────────────────────
    m_mqttOtaGroup = new QGroupBox("MQTT 升级任务（最多同时 5 台，其余自动排队）");
    auto *mqttTaskVL = new QVBoxLayout(m_mqttOtaGroup);
    mqttTaskVL->setSpacing(8);

    auto *mqttSummaryRow = new QHBoxLayout;
    m_lblMqttOtaSummary = new QLabel("运行 0 / 5　排队 0　成功 0　失败 0");
    m_lblMqttOtaSummary->setStyleSheet(
        "color:#7FDEEA;font-family:Consolas,monospace;"
        "background:transparent;border:none;");
    m_btnClearFinishedOta = new QPushButton("清除已完成");
    m_btnClearFinishedOta->setFixedWidth(100);
    mqttSummaryRow->addWidget(m_lblMqttOtaSummary);
    mqttSummaryRow->addStretch();
    mqttSummaryRow->addWidget(m_btnClearFinishedOta);
    mqttTaskVL->addLayout(mqttSummaryRow);

    m_mqttOtaTable = new QTableWidget(0, 4);
    m_mqttOtaTable->setHorizontalHeaderLabels(
        {"设备 UID", "固件", "状态", "进度"});
    m_mqttOtaTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_mqttOtaTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_mqttOtaTable->setFocusPolicy(Qt::NoFocus);
    m_mqttOtaTable->verticalHeader()->setVisible(false);
    m_mqttOtaTable->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    m_mqttOtaTable->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    m_mqttOtaTable->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::Stretch);
    m_mqttOtaTable->horizontalHeader()->setSectionResizeMode(
        3, QHeaderView::ResizeToContents);
    m_mqttOtaTable->setMinimumHeight(170);
    mqttTaskVL->addWidget(m_mqttOtaTable, 1);
    otaVL->addWidget(m_mqttOtaGroup, 1);

    // ── 串口升级仍保留单任务进度 ─────────────────────────────────────
    m_serialOtaGroup = new QGroupBox("串口升级进度");
    auto *progVL  = new QVBoxLayout(m_serialOtaGroup);
    progVL->setSpacing(8);

    m_otaProgress = new QProgressBar;
    m_otaProgress->setRange(0, 100);
    m_otaProgress->setTextVisible(true);
    m_otaProgress->setFormat("%p%  %v / 100");
    progVL->addWidget(m_otaProgress);

    m_lblOtaStatus = new QLabel("STATUS: IDLE");
    m_lblOtaStatus->setStyleSheet("color:#7FDEEA;font-family:Consolas,monospace;"
                                  "letter-spacing:1px;"
                                  "background:transparent;border:none;");
    progVL->addWidget(m_lblOtaStatus);
    m_serialOtaGroup->setVisible(false);
    otaVL->addWidget(m_serialOtaGroup);
    otaVL->addStretch();

    // ── 通道切换联动 ──────────────────────────────────────────────────
    connect(m_cmbChannel, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        OtaChannel ch = (OtaChannel)m_cmbChannel->itemData(idx).toInt();
        bool isSerial = (ch == OtaChannel::Serial);
        m_serialRow->setVisible(isSerial);
        m_serialOtaGroup->setVisible(isSerial);
        m_mqttOtaGroup->setVisible(!isSerial);
        m_btnStart->setText(isSerial
                                ? "▶   开始串口升级"
                                : "＋   新建升级任务");
        updateOtaControls();
    });

    connect(m_btnSerialOpen,   &QPushButton::clicked, this, &Widget::onSerialOpenClicked);
    connect(m_btnRefreshPorts, &QPushButton::clicked, this, &Widget::onRefreshSerialPorts);
    connect(m_btnClearFinishedOta, &QPushButton::clicked,
            this, &Widget::clearFinishedMqttOtaTasks);

    // 后台自动检测串口插拔（每 2 秒扫描一次）
    m_portScanTimer = new QTimer(this);
    m_portScanTimer->setInterval(2000);
    connect(m_portScanTimer, &QTimer::timeout, this, &Widget::onRefreshSerialPorts);
    m_portScanTimer->start();

    // 记录初始端口列表
    for (int i = 0; i < m_cmbSerialPort->count(); ++i)
        m_lastPortList << m_cmbSerialPort->itemText(i);

    tabs->addTab(createProtectedPage(otaW, "OTA 升级"),
                 "  OTA 升级  ");

    // ── Tab 3: 调试数据 ───────────────────────────────────────────────────
    auto *dbgW  = new QWidget;
    dbgW->setStyleSheet("background:transparent;");
    auto *dbgVL = new QVBoxLayout(dbgW);
    dbgVL->setContentsMargins(12, 12, 12, 12);
    dbgVL->setSpacing(8);

    auto *dbgTopRow = new QHBoxLayout;
    m_btnReadDebug = new QPushButton("读取调试数据");
    m_btnReadDebug->setFixedWidth(120);
    auto *btnClearDebug = new QPushButton("清除");
    btnClearDebug->setFixedWidth(60);
    m_lblDebugFrom = new QLabel("来源: —");
    m_lblDebugFrom->setStyleSheet("color:#FF2E97;font-family:Consolas,monospace;");
    dbgTopRow->addWidget(m_btnReadDebug);
    dbgTopRow->addWidget(btnClearDebug);
    dbgTopRow->addWidget(m_lblDebugFrom);
    dbgTopRow->addStretch();
    dbgVL->addLayout(dbgTopRow);

    m_debugTable = new QTableWidget(0, 2);
    m_debugTable->setHorizontalHeaderLabels({"字段", "数值"});
    m_debugTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_debugTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_debugTable->verticalHeader()->setVisible(false);
    m_debugTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_debugTable->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_debugTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_debugTable->setTextElideMode(Qt::ElideNone);
    m_debugTable->setAlternatingRowColors(true);
    m_debugTable->setStyleSheet(
        "QTableWidget{background:#020510;color:#00E5FF;"
        "border:1px solid #00B8D4;border-radius:2px;gridline-color:#0A2A35;}"
        "QTableWidget::item:alternate{background:#030A18;}"
        "QTableWidget::item:selected{background:#1E5A6B;color:#A8FFF2;}");
    dbgVL->addWidget(m_debugTable, 1);

    // Ctrl+C: 复制选中单元格文本到剪贴板（多个单元格用 \t / \n 分隔）
    auto *copyShortcut = new QShortcut(QKeySequence::Copy, m_debugTable);
    copyShortcut->setContext(Qt::WidgetShortcut);
    connect(copyShortcut, &QShortcut::activated, this, [this]() {
        const auto ranges = m_debugTable->selectedRanges();
        if (ranges.isEmpty()) return;
        const auto &r = ranges.first();
        QString text;
        for (int row = r.topRow(); row <= r.bottomRow(); ++row) {
            QStringList cols;
            for (int col = r.leftColumn(); col <= r.rightColumn(); ++col) {
                auto *it = m_debugTable->item(row, col);
                cols << (it ? it->text() : QString());
            }
            text += cols.join('\t');
            if (row != r.bottomRow()) text += '\n';
        }
        QGuiApplication::clipboard()->setText(text);
    });

    connect(m_btnReadDebug, &QPushButton::clicked, this, &Widget::onReadDebugClicked);
    connect(btnClearDebug, &QPushButton::clicked, this, [this]() {
        m_debugTable->setRowCount(0);
        m_lblDebugFrom->setText("来源: —");
    });

    tabs->addTab(createProtectedPage(dbgW, "调试数据"),
                 "  调试数据  ");

    m_lockEventsPage = new LockEventsPage([this] { onReadLockEventsClicked(); });
    tabs->addTab(createProtectedPage(m_lockEventsPage, "锁止事件"), "  锁止事件  ");
    m_systemDataPage = new SystemDataPage([this] { onReadSystemDataClicked(); });
    tabs->addTab(createProtectedPage(m_systemDataPage, "系统数据"), "  系统数据  ");

    // System event monitor: normally silent; the device reports important state changes.
    auto *chargeMonW = new QWidget;
    chargeMonW->setStyleSheet("background:transparent;");
    auto *chargeMonVL = new QVBoxLayout(chargeMonW);
    chargeMonVL->setContentsMargins(12, 12, 12, 12);
    chargeMonVL->setSpacing(8);

    auto *chargeMonTop = new QHBoxLayout;
    m_btnChargeMonitorArm = new QPushButton("开始系统检测");
    m_btnChargeMonitorStop = new QPushButton("停止检测");
    m_lblChargeMonitorState = new QLabel("状态: 未布防");
    m_lblChargeMonitorState->setStyleSheet(
        "color:#FF2E97;font-family:Consolas,monospace;background:transparent;");
    chargeMonTop->addWidget(m_btnChargeMonitorArm);
    chargeMonTop->addWidget(m_btnChargeMonitorStop);
    chargeMonTop->addWidget(m_lblChargeMonitorState);
    chargeMonTop->addStretch();
    chargeMonVL->addLayout(chargeMonTop);

    m_chargeMonitorLog = new QTextEdit;
    m_chargeMonitorLog->setReadOnly(true);
    m_chargeMonitorLog->setFont(QFont("Consolas", 9));
    m_chargeMonitorLog->setStyleSheet(
        "QTextEdit{background:#020510;color:#A8FFF2;"
        "border-radius:2px;border:1px solid #00B8D4;"
        "selection-background-color:#FF2E97;selection-color:#05060E;}");
    chargeMonVL->addWidget(m_chargeMonitorLog, 1);

    auto *chargeMonBottom = new QHBoxLayout;
    auto *btnClearChargeMon = new QPushButton("清除日志");
    chargeMonBottom->addStretch();
    chargeMonBottom->addWidget(btnClearChargeMon);
    chargeMonVL->addLayout(chargeMonBottom);

    connect(m_btnChargeMonitorArm, &QPushButton::clicked, this, [this]() {
        sendChargeMonitorCommand(Protocol::ChargeMonitorCommand::Arm);
    });
    connect(m_btnChargeMonitorStop, &QPushButton::clicked, this, [this]() {
        sendChargeMonitorCommand(Protocol::ChargeMonitorCommand::Disable);
    });
    connect(btnClearChargeMon, &QPushButton::clicked,
            m_chargeMonitorLog, &QTextEdit::clear);
    tabs->addTab(createProtectedPage(chargeMonW, "系统事件"),
                 "  系统事件  ");

    // ── Tab 4: 设备参数 (DIB) ──────────────────────────────────────────
    auto *dibW  = new QWidget;
    dibW->setStyleSheet("background:transparent;");
    auto *dibVL = new QVBoxLayout(dibW);
    dibVL->setContentsMargins(12, 12, 12, 12);
    dibVL->setSpacing(8);

    auto *dibForm = new QGridLayout;
    dibForm->setHorizontalSpacing(10);
    dibForm->setVerticalSpacing(6);
    const char *labelCss =
        "color:#00E5FF;font-family:Consolas,monospace;background:transparent;";
    const char *editCss =
        "QLineEdit{background:#020510;color:#00E5FF;"
        "border:1px solid #00B8D4;border-radius:2px;padding:3px;}";

    auto mkLabel = [&](const QString &t) {
        auto *l = new QLabel(t);
        l->setStyleSheet(labelCss);
        return l;
    };
    auto mkEdit = [&](const QString &placeholder) {
        auto *e = new QLineEdit;
        e->setPlaceholderText(placeholder);
        e->setStyleSheet(editCss);
        return e;
    };

    m_dibUsername = mkEdit("MQTT username (最多 79 字符)");
    m_dibPassword = mkEdit("MQTT password (最多 95 字符)");
    m_dibClientId = mkEdit("MQTT clientId (最多 95 字符)");
    m_dibHostname = mkEdit("Broker hostname (最多 127 字符)");
    m_dibPort     = mkEdit("8883");
    m_dibWifiAddr = mkEdit("3");
    m_dibPort->setMaximumWidth(100);
    m_dibWifiAddr->setMaximumWidth(100);

    int row = 0;
    dibForm->addWidget(mkLabel("username"), row, 0);
    dibForm->addWidget(m_dibUsername,       row, 1); ++row;
    dibForm->addWidget(mkLabel("password"), row, 0);
    dibForm->addWidget(m_dibPassword,       row, 1); ++row;
    dibForm->addWidget(mkLabel("clientId"), row, 0);
    dibForm->addWidget(m_dibClientId,       row, 1); ++row;
    dibForm->addWidget(mkLabel("hostname"), row, 0);
    dibForm->addWidget(m_dibHostname,       row, 1); ++row;
    dibForm->addWidget(mkLabel("port"),     row, 0);
    dibForm->addWidget(m_dibPort,           row, 1); ++row;
    dibForm->addWidget(mkLabel("wifi_addr"),row, 0);
    dibForm->addWidget(m_dibWifiAddr,       row, 1); ++row;

    dibVL->addLayout(dibForm);

    auto *dibBtnRow = new QHBoxLayout;
    m_btnLoadDib    = new QPushButton("从文件加载…");
    m_btnWriteDib   = new QPushButton("写入 DIB 到选中设备");
    m_btnViewDibLog = new QPushButton("查看写入记录");
    m_btnLoadDib   ->setFixedWidth(120);
    m_btnWriteDib  ->setFixedWidth(160);
    m_btnViewDibLog->setFixedWidth(120);
    dibBtnRow->addWidget(m_btnLoadDib);
    dibBtnRow->addWidget(m_btnWriteDib);
    dibBtnRow->addWidget(m_btnViewDibLog);
    dibBtnRow->addStretch();
    dibVL->addLayout(dibBtnRow);

    auto *dibHint = new QLabel("注：写入成功后设备保留旧 MQTT 连接，重启后新凭证才生效");
    dibHint->setStyleSheet("color:#FF2E97;font-family:Consolas,monospace;"
                           "background:transparent;");
    dibHint->setWordWrap(true);
    dibVL->addWidget(dibHint);
    dibVL->addStretch();

    connect(m_btnWriteDib, &QPushButton::clicked,
            this, &Widget::onWriteDibClicked);
    connect(m_btnLoadDib, &QPushButton::clicked,
            this, &Widget::onLoadDibFromFileClicked);
    connect(m_btnViewDibLog, &QPushButton::clicked,
            this, &Widget::onViewDibLogClicked);

    // 设备参数 tab 延后到远程控制之后再 addTab（位置互换）

    // ── Tab 5: 远程控制 ───────────────────────────────────────────────────
    auto *ctrlW  = new QWidget;
    auto *ctrlVL = new QVBoxLayout(ctrlW);
    ctrlVL->setContentsMargins(0, 0, 0, 0);
    ctrlVL->setSpacing(10);

    auto *ctrlGrp = new QGroupBox("设备操控");
    auto *ctrlGrpVL = new QVBoxLayout(ctrlGrp);
    ctrlGrpVL->setSpacing(8);

    auto *resetRow = new QHBoxLayout;
    m_btnReset = new QPushButton("软件复位选中设备");
    m_btnReset->setFixedWidth(160);
    auto *resetHint = new QLabel("向选中设备下发软件复位命令，设备将立即重启");
    resetHint->setStyleSheet("color:#00E5FF;background:transparent;");
    resetRow->addWidget(m_btnReset);
    resetRow->addWidget(resetHint);
    resetRow->addStretch();
    ctrlGrpVL->addLayout(resetRow);

    // 允许 / 限制充电功能已弃用，按用户要求注释屏蔽
    // auto *chargeSep = new QFrame; chargeSep->setFrameShape(QFrame::HLine);
    // chargeSep->setStyleSheet("background:#00B8D4;border:none;max-height:1px;");
    // ctrlGrpVL->addWidget(chargeSep);
    //
    // auto *chargeRow = new QHBoxLayout;
    // m_btnChargeAllow = new QPushButton("允许充电");
    // m_btnChargeLimit = new QPushButton("限制充电");
    // m_btnChargeAllow->setFixedWidth(120);
    // m_btnChargeLimit->setFixedWidth(120);
    // m_btnChargeLimit->setStyleSheet(
    //     "QPushButton{background:#3A0010;color:#FF7080;"
    //     "border:1px solid #FF2E97;border-radius:2px;padding:4px 8px;}"
    //     "QPushButton:hover{background:#FF2E97;color:#05060E;}");
    // auto *chargeHint = new QLabel("控制 EVCC（CAN2）：限制 = 限流 0A，允许 = 限流 500");
    // chargeHint->setStyleSheet("color:#00E5FF;background:transparent;");
    // chargeRow->addWidget(m_btnChargeAllow);
    // chargeRow->addWidget(m_btnChargeLimit);
    // chargeRow->addWidget(chargeHint);
    // chargeRow->addStretch();
    // ctrlGrpVL->addLayout(chargeRow);

    auto *flashSep = new QFrame; flashSep->setFrameShape(QFrame::HLine);
    flashSep->setStyleSheet("background:#00B8D4;border:none;max-height:1px;");
    ctrlGrpVL->addWidget(flashSep);

    auto *flashRow = new QHBoxLayout;
    m_btnEraseFlash = new QPushButton("擦除 flashData");
    m_btnEraseFlash->setFixedWidth(160);
    m_btnEraseFlash->setStyleSheet(
        "QPushButton{background:#3A0010;color:#FF7080;"
        "border:1px solid #FF2E97;border-radius:2px;padding:4px 8px;}"
        "QPushButton:hover{background:#FF2E97;color:#05060E;}");
    auto *flashHint = new QLabel("清空 flashData（充电/换电累计、充电限制、网络状态等持久化数据）");
    flashHint->setStyleSheet("color:#FFB000;background:transparent;");
    flashRow->addWidget(m_btnEraseFlash);
    flashRow->addWidget(flashHint);
    flashRow->addStretch();
    ctrlGrpVL->addLayout(flashRow);

    ctrlVL->addWidget(ctrlGrp);
    ctrlVL->addStretch();
    connect(m_btnReset, &QPushButton::clicked,
            this, &Widget::onResetDeviceClicked);
    // connect(m_btnChargeAllow, &QPushButton::clicked,
    //         this, &Widget::onChargeAllowClicked);
    // connect(m_btnChargeLimit, &QPushButton::clicked,
    //         this, &Widget::onChargeLimitClicked);
    connect(m_btnEraseFlash, &QPushButton::clicked,
            this, &Widget::onEraseFlashClicked);

    tabs->addTab(createProtectedPage(ctrlW, "远程控制"),
                 "  远程控制  ");
    tabs->addTab(createProtectedPage(dibW, "设备参数"),
                 "  设备参数  ");

    // ── Tab 5: Log ────────────────────────────────────────────────────────
    auto *logW  = new QWidget;
    logW->setStyleSheet("background:transparent;");
    auto *logVL = new QVBoxLayout(logW);
    logVL->setContentsMargins(8, 8, 8, 8);
    logVL->setSpacing(5);

    m_logEdit = new QTextEdit;
    m_logEdit->setReadOnly(true);
    m_logEdit->setFont(QFont("Consolas", 9));
    m_logEdit->setStyleSheet(
        "QTextEdit{background:#020510;color:#00E5FF;"
        "border-radius:2px;border:1px solid #00B8D4;"
        "selection-background-color:#FF2E97;selection-color:#05060E;}");
    logVL->addWidget(m_logEdit, 1);

    auto *logBtnRow = new QHBoxLayout;
    logBtnRow->addStretch();
    auto *btnClear = new QPushButton("清除日志");
    btnClear->setFixedWidth(88);
    logBtnRow->addWidget(btnClear);
    logVL->addLayout(logBtnRow);

    tabs->addTab(logW, "  日志  ");

    rightLayout->addWidget(tabs);
    splitter->addWidget(rightCard);
    splitter->setSizes({380, 800});
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    // ── Connections ───────────────────────────────────────────────────────
    connect(m_btnConnect, &QPushButton::clicked, this, &Widget::onConnectClicked);
    connect(btnSel,       &QPushButton::clicked, this, &Widget::onSelectFirmwareClicked);
    connect(m_btnStart,   &QPushButton::clicked, this, &Widget::onStartOtaClicked);
    connect(btnClear,     &QPushButton::clicked, m_logEdit, &QTextEdit::clear);
    connect(btnAddGrp,    &QPushButton::clicked, this, [this]{
        bool ok;
        QString name = QInputDialog::getText(this, "新建设备组",
            "请输入组名称:", QLineEdit::Normal, "", &ok);
        if (!ok || name.trimmed().isEmpty()) return;
        name = name.trimmed();
        if (m_groupItems.contains(name)) {
            QMessageBox::information(this, "提示", "组 "" + name + "" 已存在");
            return;
        }
        getOrCreateGroup(name, name);
        if (m_db.isOpen()) {
            QSqlQuery q;
            q.prepare("INSERT OR IGNORE INTO groups(name,display_name,created)"
                      " VALUES(:n,:d,datetime('now'))");
            q.bindValue(":n", name);
            q.bindValue(":d", name);
            q.exec();
        }
        addLog("新建设备组: " + name);
    });

    connect(m_tree, &QTreeWidget::itemSelectionChanged,
            this, &Widget::onTreeSelectionChanged);
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &Widget::onTreeContextMenu);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Group / Tree helpers
// ─────────────────────────────────────────────────────────────────────────────
QTreeWidgetItem *Widget::getOrCreateGroup(const QString &groupName,
                                          const QString &displayName)
{
    if (m_groupItems.contains(groupName))
        return m_groupItems[groupName];

    QString label = displayName.isEmpty() ? groupName : displayName;

    auto *item = new QTreeWidgetItem(m_tree);
    item->setText(0, label);
    item->setData(0, Qt::UserRole,     groupName);
    item->setData(0, Qt::UserRole + 1, QString("group"));
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    item->setExpanded(true);

    QFont f = item->font(0);
    f.setBold(true);
    f.setPointSize(11);
    item->setFont(0, f);
    item->setForeground(0, QColor("#FF2E97"));

    m_groupItems[groupName] = item;
    return item;
}

void Widget::refreshGroupCount(QTreeWidgetItem *groupItem)
{
    if (!groupItem) return;
    int n = groupItem->childCount();
    QString raw = groupItem->text(0);
    int idx = raw.lastIndexOf("  [");
    if (idx >= 0) raw = raw.left(idx);
    groupItem->setText(0, raw + QString("  [%1台]").arg(n));
}

void Widget::addDeviceToTree(const QString &uid, const QString &groupName)
{
    if (m_deviceItems.contains(uid)) return;

    QTreeWidgetItem *grp = getOrCreateGroup(groupName);
    auto *item = new QTreeWidgetItem(grp);
    item->setText(0, "○  " + uid);
    item->setText(1, "OFFLINE");
    item->setTextAlignment(1, Qt::AlignCenter);
    item->setForeground(0, QColor("#3F7B87"));
    item->setForeground(1, QColor("#FF2E97"));
    item->setFont(0, QFont("Consolas", 10));
    item->setFont(1, QFont("Consolas", 9, QFont::Bold));
    item->setData(0, Qt::UserRole,     uid);
    item->setData(0, Qt::UserRole + 1, QString("device"));
    item->setToolTip(0, "UID: " + uid);

    grp->setExpanded(true);
    m_deviceItems[uid] = item;
    refreshGroupCount(grp);
}

void Widget::updateTreeItem(const QString &uid)
{
    if (!m_deviceItems.contains(uid) || !m_devices.contains(uid)) return;
    QTreeWidgetItem    *item = m_deviceItems[uid];
    const DeviceRecord &rec  = m_devices[uid];

    // 计算时间字符串
    QString timeStr;
    if (rec.hasHeartbeat) {
        qint64 secs = (qint64)rec.lastHeartbeat.secsTo(QDateTime::currentDateTime());
        QString rel;
        if (secs < 60) {
            rel = QString("%1秒前").arg(secs);
        } else if (secs < 3600) {
            rel = QString("%1分%2秒前").arg(secs / 60).arg(secs % 60);
        } else if (secs < 86400) {
            rel = QString("%1时%2分%3秒前")
                  .arg(secs / 3600).arg((secs % 3600) / 60).arg(secs % 60);
        } else if (secs < 86400LL * 365) {
            qint64 d = secs / 86400, r = secs % 86400;
            rel = QString("%1天%2时%3分%4秒前")
                  .arg(d).arg(r / 3600).arg((r % 3600) / 60).arg(r % 60);
        } else {
            qint64 y = secs / (86400LL * 365), r = secs % (86400LL * 365);
            qint64 d = r / 86400; r %= 86400;
            rel = QString("%1年%2天%3时%4分%5秒前")
                  .arg(y).arg(d).arg(r / 3600).arg((r % 3600) / 60).arg(r % 60);
        }
        timeStr = rec.lastHeartbeat.toString("yyyy年MM月dd日 hh:mm:ss") +
                  "  （" + rel + "）";
    }

    if (rec.isActive()) {
        item->setText(0, "●  " + uid);
        item->setText(1, "ONLINE");
        item->setForeground(0, QColor("#00E5FF"));
        item->setForeground(1, QColor("#39FF14"));
    } else {
        item->setText(0, "○  " + uid);
        item->setText(1, rec.hasHeartbeat ? "OFFLINE" : "VOID");
        item->setForeground(0, QColor("#3F7B87"));
        item->setForeground(1, QColor("#FF2E97"));
    }

    if (rec.hasHeartbeat) {
        item->setToolTip(0, QString("UID: %1\n版本: %2\n最后心跳: %3")
                         .arg(uid).arg(rec.hb.version).arg(timeStr));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Tree selection
// ─────────────────────────────────────────────────────────────────────────────
void Widget::onTreeSelectionChanged()
{
    const auto sel = m_tree->selectedItems();
    if (sel.isEmpty()) {
        updateOtaControls();
        return;
    }

    QTreeWidgetItem *item = m_tree->currentItem();
    if (item == nullptr ||
        item->data(0, Qt::UserRole + 1).toString() != "device") {
        item = nullptr;
        for (QTreeWidgetItem *candidate : sel) {
            if (candidate->data(0, Qt::UserRole + 1).toString() == "device") {
                item = candidate;
                break;
            }
        }
    }
    if (item == nullptr) {
        updateOtaControls();
        return;
    }

    QString uid = item->data(0, Qt::UserRole).toString();
    m_selectedUid = uid;
    if (m_traceSummaries.contains(uid))
        m_chargeMonitorLog->append(m_traceSummaries.value(uid).toHtmlEscaped().replace("\n","<br>"));
    updateDeviceInfo();
    updateOtaControls();
    addLog(QString("当前设备: %1（OTA 已选择 %2 台）")
               .arg(uid)
               .arg(selectedDeviceUids().size()));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Tree context menu
// ─────────────────────────────────────────────────────────────────────────────
void Widget::onTreeContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_tree->itemAt(pos);
    if (!item) return;
    QString kind = item->data(0, Qt::UserRole + 1).toString();
    QString uid  = item->data(0, Qt::UserRole).toString();

    if (kind == "device") {
        QMenu menu(this);
        QAction *actMove   = menu.addAction("移动到其他组...");
        menu.addSeparator();
        QAction *actDelete = menu.addAction("从历史记录删除");
        actDelete->setToolTip("将此设备从本地数据库中删除，不影响设备运行");

        QAction *chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
        if (!chosen) return;

        if (chosen == actDelete) {
            QString shortUid = uid.length() > 12 ? "..." + uid.right(12) : uid;
            if (QMessageBox::question(this, "删除设备",
                    QString("确认删除设备\n%1\n\n该设备将从本地历史记录中移除。\n"
                            "若设备仍在线，下次心跳将自动重新添加。")
                    .arg(shortUid),
                    QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
                return;

            // Remove from DB
            if (m_db.isOpen()) {
                QSqlQuery q;
                q.prepare("DELETE FROM devices WHERE uid=:uid");
                q.bindValue(":uid", uid);
                q.exec();
            }
            // Remove from tree
            QTreeWidgetItem *parent = item->parent();
            if (parent) { parent->removeChild(item); refreshGroupCount(parent); }
            delete item;
            m_deviceItems.remove(uid);
            m_devices.remove(uid);

            if (m_selectedUid == uid) {
                m_selectedUid.clear();
                updateDeviceInfo();
            }
            addLog("已删除历史设备: " + uid);
            return;
        }

        // actMove
        QStringList groups;
        for (auto it = m_groupItems.cbegin(); it != m_groupItems.cend(); ++it)
            groups << it.key();
        if (groups.size() < 2) {
            QMessageBox::information(this, "提示", "请先新建其他设备组");
            return;
        }
        bool ok;
        QString newGrp = QInputDialog::getItem(
            this, "移动设备", "选择目标组:", groups, 0, false, &ok);
        if (!ok || newGrp.isEmpty()) return;

        // Move tree item
        QTreeWidgetItem *oldParent = item->parent();
        if (oldParent) {
            oldParent->removeChild(item);
            refreshGroupCount(oldParent);
        }
        QTreeWidgetItem *newParent = m_groupItems[newGrp];
        newParent->addChild(item);
        newParent->setExpanded(true);
        refreshGroupCount(newParent);

        // Update record & DB
        if (m_devices.contains(uid)) {
            m_devices[uid].groupName = newGrp;
            saveDevice(uid, newGrp);
        }
        addLog(QString("设备 %1 已移动到组: %2").arg(uid).arg(newGrp));

    } else if (kind == "group") {
        QMenu menu(this);
        QAction *actRename      = menu.addAction("重命名组...");
        menu.addSeparator();
        QAction *actClearOffline = menu.addAction("清除所有离线设备");
        menu.addSeparator();
        QAction *actDelete      = menu.addAction("删除组...");

        QAction *chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
        if (!chosen) return;

        if (chosen == actRename) {
            bool ok;
            QString cur = item->text(0);
            int i = cur.lastIndexOf("  [");
            if (i >= 0) cur = cur.left(i);

            QString newName = QInputDialog::getText(
                this, "重命名组", "新显示名称:", QLineEdit::Normal, cur, &ok);
            if (!ok || newName.trimmed().isEmpty()) return;

            int ci = item->text(0).lastIndexOf("  [");
            QString suffix = (ci >= 0) ? item->text(0).mid(ci) : "";
            item->setText(0, newName.trimmed() + suffix);

            if (m_db.isOpen()) {
                QSqlQuery q;
                q.prepare("UPDATE groups SET display_name=:d WHERE name=:n");
                q.bindValue(":d", newName.trimmed());
                q.bindValue(":n", uid);
                q.exec();
            }

        } else if (chosen == actClearOffline) {
            // Collect offline children
            QList<QTreeWidgetItem *> toRemove;
            for (int i = 0; i < item->childCount(); ++i) {
                QTreeWidgetItem *child = item->child(i);
                QString devUid = child->data(0, Qt::UserRole).toString();
                if (!m_devices.contains(devUid) || !m_devices[devUid].isActive())
                    toRemove << child;
            }
            if (toRemove.isEmpty()) {
                QMessageBox::information(this, "提示", "该组内没有离线设备");
                return;
            }
            if (QMessageBox::question(this, "清除离线设备",
                    QString("将从组 \"%1\" 中删除 %2 台离线设备（含历史记录）。\n确认？")
                    .arg(uid).arg(toRemove.size()),
                    QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
                return;

            for (QTreeWidgetItem *child : toRemove) {
                QString devUid = child->data(0, Qt::UserRole).toString();
                if (m_db.isOpen()) {
                    QSqlQuery q;
                    q.prepare("DELETE FROM devices WHERE uid=:uid");
                    q.bindValue(":uid", devUid);
                    q.exec();
                }
                item->removeChild(child);
                m_deviceItems.remove(devUid);
                m_devices.remove(devUid);
                if (m_selectedUid == devUid) { m_selectedUid.clear(); updateDeviceInfo(); }
                delete child;
            }
            refreshGroupCount(item);
            addLog(QString("已清除组 \"%1\" 中 %2 台离线设备").arg(uid).arg(toRemove.size()));

        } else if (chosen == actDelete) {
            int childCount = item->childCount();
            QString msg = childCount > 0
                ? QString("组 \"%1\" 下还有 %2 台设备，删除后这些设备将移入「BOOT无APP」组。\n确认删除？")
                  .arg(uid).arg(childCount)
                : QString("确认删除组 \"%1\"？").arg(uid);

            if (QMessageBox::question(this, "删除组", msg,
                    QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
                return;

            // 将子设备移入「BOOT无APP」组
            if (item->childCount() > 0) {
                if (!m_groupItems.contains("BOOT无APP")) {
                    getOrCreateGroup("BOOT无APP", "BOOT无APP");
                    if (m_db.isOpen()) {
                        QSqlQuery qi;
                        qi.prepare("INSERT OR IGNORE INTO groups(name,display_name,created)"
                                   " VALUES('BOOT无APP','BOOT无APP',datetime('now'))");
                        qi.exec();
                    }
                }
                QTreeWidgetItem *unknownGrp = m_groupItems["BOOT无APP"];
                while (item->childCount() > 0) {
                    QTreeWidgetItem *child = item->takeChild(0);
                    QString devUid = child->data(0, Qt::UserRole).toString();
                    unknownGrp->addChild(child);
                    if (m_devices.contains(devUid)) {
                        m_devices[devUid].groupName = "BOOT无APP";
                        saveDevice(devUid, "BOOT无APP");
                    }
                }
                unknownGrp->setExpanded(true);
                refreshGroupCount(unknownGrp);
            }

            // 从 DB 删除
            if (m_db.isOpen()) {
                QSqlQuery q;
                q.prepare("DELETE FROM groups WHERE name=:n");
                q.bindValue(":n", uid);
                q.exec();
            }

            // 从 tree 和 map 中移除
            m_groupItems.remove(uid);
            delete item;

            addLog(QString("已删除设备组: %1，其下设备已移入「BOOT无APP」").arg(uid));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Database
// ─────────────────────────────────────────────────────────────────────────────
void Widget::setupDatabase()
{
    // 数据库固定存放在项目源码目录（APP_SOURCE_DIR 由 .pro 注入），
    // 避免 Debug/Release 各自的工作目录指向不同的 ota_devices.db
#ifndef APP_SOURCE_DIR
#define APP_SOURCE_DIR "."
#endif
    const QString dbPath = QDir(QString(APP_SOURCE_DIR)).absoluteFilePath("ota_devices.db");

    // 首次切换到新位置时，从 build 目录里最新的旧库迁移一次历史数据
    if (!QFile::exists(dbPath)) {
        seedDatabaseFrom(dbPath);
    }

    // 历史遗留的库文件可能被标记为只读（如 build 目录里的拷贝），
    // 只读会让后续 INSERT/UPDATE 静默失败——打开前先清掉只读属性
    if (QFile::exists(dbPath)) {
        QFile::setPermissions(dbPath,
            QFileDevice::ReadOwner  | QFileDevice::WriteOwner |
            QFileDevice::ReadUser   | QFileDevice::WriteUser);
    }

    m_db = QSqlDatabase::addDatabase("QSQLITE");
    m_db.setDatabaseName(dbPath);
    if (!m_db.open()) {
        addLog("数据库打开失败: " + m_db.lastError().text());
        return;
    }

    QSqlQuery q;
    // Groups table
    q.exec(
        "CREATE TABLE IF NOT EXISTS groups ("
        "  name         TEXT PRIMARY KEY,"
        "  display_name TEXT NOT NULL,"
        "  created      TEXT NOT NULL"
        ")");
    // Devices table with group_name
    q.exec(
        "CREATE TABLE IF NOT EXISTS devices ("
        "  uid              TEXT PRIMARY KEY,"
        "  first_seen       TEXT NOT NULL,"
        "  last_seen        TEXT NOT NULL,"
        "  firmware_version TEXT DEFAULT '',"
        "  group_name       TEXT DEFAULT 'BOOT无APP'"
        ")");
    // Migration: add group_name if the table existed without it
    q.exec("ALTER TABLE devices ADD COLUMN group_name       TEXT    DEFAULT 'BOOT无APP'");
    // Migration: heartbeat fields
    q.exec("ALTER TABLE devices ADD COLUMN hb_version       TEXT    DEFAULT ''");
    q.exec("ALTER TABLE devices ADD COLUMN hb_batch         TEXT    DEFAULT ''");
    q.exec("ALTER TABLE devices ADD COLUMN hb_vin           TEXT    DEFAULT ''");
    q.exec("ALTER TABLE devices ADD COLUMN hb_comm          INTEGER DEFAULT 0");
    q.exec("ALTER TABLE devices ADD COLUMN hb_vehicle       INTEGER DEFAULT 0");
    q.exec("ALTER TABLE devices ADD COLUMN hb_hv            INTEGER DEFAULT 0");
    q.exec("ALTER TABLE devices ADD COLUMN hb_brake         INTEGER DEFAULT 0");
    q.exec("ALTER TABLE devices ADD COLUMN hb_speed         INTEGER DEFAULT 0");
    q.exec("ALTER TABLE devices ADD COLUMN hb_mileage       INTEGER DEFAULT 0");
    q.exec("ALTER TABLE devices ADD COLUMN hb_charge        INTEGER DEFAULT 0");
    q.exec("ALTER TABLE devices ADD COLUMN hb_lng           INTEGER DEFAULT 0");
    q.exec("ALTER TABLE devices ADD COLUMN hb_lat           INTEGER DEFAULT 0");
    q.exec("ALTER TABLE devices ADD COLUMN hb_ts            INTEGER DEFAULT 0");
    q.exec("ALTER TABLE devices ADD COLUMN hb_last_time     TEXT    DEFAULT ''");

    // DIB 写入记录（每个 UID 仅保留最新一次；换了 username 就覆盖旧的）
    q.exec(
        "CREATE TABLE IF NOT EXISTS dib_log ("
        "  uid      TEXT PRIMARY KEY,"
        "  username TEXT NOT NULL,"
        "  written  TEXT NOT NULL"
        ")");

    // Broker profiles table (id=1 is the last-used profile)
    q.exec(
        "CREATE TABLE IF NOT EXISTS broker_profiles ("
        "  id       INTEGER PRIMARY KEY,"
        "  host     TEXT NOT NULL,"
        "  port     INTEGER NOT NULL DEFAULT 1883,"
        "  username TEXT NOT NULL DEFAULT '',"
        "  password TEXT NOT NULL DEFAULT ''"
        ")");

    addLog("数据库已就绪: " + dbPath);
}

// 切换到固定数据库位置后，首次启动时把 build 目录中最新一份历史库迁移过来，
// 避免“看起来数据全没了”。只在目标库尚不存在时执行一次。
void Widget::seedDatabaseFrom(const QString &dbPath)
{
    QDir root(QString(APP_SOURCE_DIR));
    QString newestPath;
    QDateTime newestTime;

    // 在源码目录树下递归找所有遗留的 ota_devices.db，挑修改时间最新的
    QDirIterator it(root.absolutePath(), QStringList() << "ota_devices.db",
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QFileInfo fi(it.next());
        if (fi.absoluteFilePath() == dbPath) {
            continue;  // 跳过目标自身
        }
        if (newestPath.isEmpty() || fi.lastModified() > newestTime) {
            newestPath = fi.absoluteFilePath();
            newestTime = fi.lastModified();
        }
    }

    if (newestPath.isEmpty()) {
        return;  // 没有可迁移的历史库，全新开始
    }

    if (QFile::copy(newestPath, dbPath)) {
        addLog("已迁移历史数据库: " + newestPath + " → " + dbPath);
    }
    else {
        addLog("历史数据库迁移失败，将新建空库: " + newestPath);
    }
}

void Widget::saveDevice(const QString &uid, const QString &groupName)
{
    if (!m_db.isOpen()) return;
    QString now = QDateTime::currentDateTime().toString(Qt::ISODate);

    QSqlQuery q;
    q.prepare("INSERT OR IGNORE INTO devices(uid,first_seen,last_seen,group_name)"
              " VALUES(:uid,:now,:now,:grp)");
    q.bindValue(":uid", uid);
    q.bindValue(":now", now);
    q.bindValue(":grp", groupName);
    q.exec();

    q.prepare("UPDATE devices SET last_seen=:now, group_name=:grp WHERE uid=:uid");
    q.bindValue(":uid", uid);
    q.bindValue(":now", now);
    q.bindValue(":grp", groupName);
    q.exec();
}

void Widget::loadHistoryDevices()
{
    if (!m_db.isOpen()) return;

    // Load custom groups first
    QSqlQuery gq("SELECT name, display_name FROM groups");
    while (gq.next()) {
        QString n = gq.value(0).toString();
        QString d = gq.value(1).toString();
        getOrCreateGroup(n, d);
    }

    // Load devices
    QSqlQuery q("SELECT uid, group_name FROM devices ORDER BY last_seen DESC");
    int count = 0;
    while (q.next()) {
        QString uid = q.value(0).toString();
        QString grp = q.value(1).toString();
        if (grp.isEmpty()) grp = "BOOT无APP";
        if (!m_devices.contains(uid)) {
            DeviceRecord rec;
            rec.uid       = uid;
            rec.groupName = grp;
            m_devices[uid] = rec;
            addDeviceToTree(uid, grp);
            count++;
        }
    }
    addLog(QString("从数据库加载 %1 个历史设备").arg(count));

    loadHeartbeats();

    // Auto-fill last broker profile
    BrokerProfile p = loadBrokerProfile();
    if (!p.host.isEmpty()) {
        m_editHost->setText(p.host);
        m_editPort->setText(QString::number(p.port));
        m_editUser->setText(p.username);
        m_editPass->setText(p.password);
        addLog(QString("已读取上次连接: %1:%2").arg(p.host).arg(p.port));
    }
}

void Widget::saveBrokerProfile(const BrokerProfile &p)
{
    if (!m_db.isOpen()) return;
    QSqlQuery q;
    q.prepare(
        "INSERT OR REPLACE INTO broker_profiles(id,host,port,username,password)"
        " VALUES(1,:h,:po,:u,:pw)");
    q.bindValue(":h",  p.host);
    q.bindValue(":po", p.port);
    q.bindValue(":u",  p.username);
    q.bindValue(":pw", p.password);
    q.exec();
}

BrokerProfile Widget::loadBrokerProfile()
{
    BrokerProfile p;
    if (!m_db.isOpen()) return p;
    QSqlQuery q("SELECT host,port,username,password FROM broker_profiles WHERE id=1");
    if (q.next()) {
        p.host     = q.value(0).toString();
        p.port     = (quint16)q.value(1).toUInt();
        p.username = q.value(2).toString();
        p.password = q.value(3).toString();
    }
    return p;
}

void Widget::saveAllHeartbeats()
{
    if (!m_db.isOpen()) return;
    for (const auto &rec : m_devices) {
        if (!rec.hasHeartbeat) continue;
        const Protocol::HeartbeatData &hb = rec.hb;
        QSqlQuery q;
        q.prepare(
            "UPDATE devices SET"
            "  hb_version=:ver, hb_batch=:bat, hb_vin=:vin,"
            "  hb_comm=:comm,   hb_vehicle=:veh, hb_hv=:hv,"
            "  hb_brake=:brk,   hb_speed=:spd,  hb_mileage=:mil,"
            "  hb_charge=:chg,  hb_lng=:lng,    hb_lat=:lat,"
            "  hb_ts=:ts,       hb_last_time=:lt"
            " WHERE uid=:uid");
        q.bindValue(":ver",  hb.version);
        q.bindValue(":bat",  hb.batch);
        q.bindValue(":vin",  hb.vin);
        q.bindValue(":comm", hb.comm_state);
        q.bindValue(":veh",  hb.vehicle_state);
        q.bindValue(":hv",   hb.hv_state);
        q.bindValue(":brk",  hb.brake_state);
        q.bindValue(":spd",  hb.speed);
        q.bindValue(":mil",  (int)hb.total_mileage);
        q.bindValue(":chg",  hb.charge_state);
        q.bindValue(":lng",  hb.lng);
        q.bindValue(":lat",  hb.lat);
        q.bindValue(":ts",   (qint64)hb.ts);
        q.bindValue(":lt",   rec.lastHeartbeat.toString(Qt::ISODate));
        q.bindValue(":uid",  rec.uid);
        q.exec();
    }
}

void Widget::loadHeartbeats()
{
    if (!m_db.isOpen()) return;
    QSqlQuery q(
        "SELECT uid, hb_version, hb_batch, hb_vin,"
        "       hb_comm, hb_vehicle, hb_hv, hb_brake,"
        "       hb_speed, hb_mileage, hb_charge, hb_lng, hb_lat,"
        "       hb_ts, hb_last_time"
        " FROM devices WHERE hb_last_time != ''");
    int count = 0;
    while (q.next()) {
        QString uid = q.value(0).toString();
        if (!m_devices.contains(uid)) continue;
        QString lastTimeStr = q.value(14).toString();
        if (lastTimeStr.isEmpty()) continue;

        DeviceRecord &rec = m_devices[uid];
        Protocol::HeartbeatData &hb = rec.hb;
        hb.version       = q.value(1).toString();
        hb.batch         = q.value(2).toString();
        hb.vin           = q.value(3).toString();
        hb.comm_state    = (uint8_t)q.value(4).toUInt();
        hb.vehicle_state = (uint8_t)q.value(5).toUInt();
        hb.hv_state      = (uint8_t)q.value(6).toUInt();
        hb.brake_state   = (uint8_t)q.value(7).toUInt();
        hb.speed         = (uint8_t)q.value(8).toUInt();
        hb.total_mileage = q.value(9).toUInt();
        hb.charge_state  = (uint8_t)q.value(10).toUInt();
        hb.lng           = q.value(11).toInt();
        hb.lat           = q.value(12).toInt();
        hb.ts            = q.value(13).toLongLong();
        rec.lastHeartbeat = QDateTime::fromString(lastTimeStr, Qt::ISODate);
        rec.hasHeartbeat  = true;
        updateTreeItem(uid);
        count++;
    }
    if (count > 0)
        addLog(QString("已恢复 %1 台设备的历史心跳数据").arg(count));
}

// ─────────────────────────────────────────────────────────────────────────────
//  MQTT connection
// ─────────────────────────────────────────────────────────────────────────────
void Widget::onConnectClicked()
{
    if (m_mqtt->state() != MqttClient::Disconnected) {
        m_mqtt->disconnectFromHost();
        m_btnConnect->setText("连接");
        return;
    }
    QString host = m_editHost->text().trimmed();
    quint16 port = (quint16)m_editPort->text().toUInt();
    if (host.isEmpty() || port == 0) {
        QMessageBox::warning(this, "配置错误", "请填写正确的 Broker 地址和端口");
        return;
    }
    QString user = m_editUser->text().trimmed();
    QString pass = m_editPass->text();

    BrokerProfile p;
    p.host     = host;
    p.port     = port;
    p.username = user;
    p.password = pass;
    saveBrokerProfile(p);

    m_mqtt->setHost(host, port);
    m_mqtt->setCredentials(user, pass);
    m_mqtt->setClientId("OTA_Server_" +
                        QString::number(QDateTime::currentMSecsSinceEpoch()));
    m_mqtt->connectToHost();
    addLog(QString("正在连接 %1:%2 %3...")
           .arg(host).arg(port)
           .arg(user.isEmpty() ? "" : "(用户: " + user + ")"));
    m_btnConnect->setText("断开");
}

void Widget::onMqttConnected()
{
    m_lblStatus->setText("●  ONLINE");
    m_lblStatus->setStyleSheet("color:#39FF14;font-weight:bold;letter-spacing:2px;"
                               "background:transparent;border:none;");
    m_btnConnect->setText("■ DISCONNECT");
    addLog("MQTT 已连接");
    m_mqtt->subscribe(Protocol::TOPIC_UP_SUB, 1);
    m_mqtt->subscribe(Protocol::TOPIC_STATUS_SUB, 1);
    addLog(QString("已订阅: %1  %2")
           .arg(Protocol::TOPIC_UP_SUB).arg(Protocol::TOPIC_STATUS_SUB));
    startQueuedMqttOtaTasks();
    updateOtaControls();
}

void Widget::onMqttDisconnected()
{
    m_lblStatus->setText("●  OFFLINE");
    m_lblStatus->setStyleSheet("color:#FF2E97;font-weight:bold;letter-spacing:2px;"
                               "background:transparent;border:none;");
    m_btnConnect->setText("▶ CONNECT");
    addLog("MQTT 已断开");

    // 每个运行中的任务独立结束；尚未开始的排队任务保留，重连后继续调度。
    const auto tasks = m_mqttOtaTasks.values();
    for (MqttOtaTask *task : tasks) {
        if (task != nullptr && task->isRunning())
            task->abort("MQTT 连接断开");
    }
    updateOtaControls();
}

void Widget::onMqttStateChanged(MqttClient::State state)
{
    if (state == MqttClient::Connecting) {
        m_lblStatus->setText("●  LINKING...");
        m_lblStatus->setStyleSheet("color:#FFD300;font-weight:bold;letter-spacing:2px;"
                                   "background:transparent;border:none;");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Incoming message dispatch
// ─────────────────────────────────────────────────────────────────────────────
void Widget::onMessageReceived(const QByteArray &message, const QString &topic)
{
    QStringList parts = topic.split('/');
    if (parts.size() < 3) return;
    QString deviceId = parts[1];

    // 任意来自设备的上行包均刷新「最近可见」时间戳，避免 OTA 期间
    // 设备因停止发送心跳而被判定离线
    if (m_devices.contains(deviceId))
        m_devices[deviceId].lastHeartbeat = QDateTime::currentDateTime();

    // Heartbeat arrives on tbox/{id}/status as raw JSON
    if (topic.endsWith("/status")) {
        handleHeartbeat(deviceId, message);
        return;
    }

    // OTA uplink arrives on tbox/{id}/up as binary frame
    Protocol::MsgType type;
    QByteArray data;
    if (!Protocol::parsePacket(message, type, data)) {
        addLog(QString("[%1] 收到无效数据包 (len=%2)").arg(deviceId).arg(message.size()));
        return;
    }
    if (type == Protocol::SYSTEM_DATA_RSP) {
        m_systemDataPage->showResponse(deviceId, data);
        return;
    }
    if (type == Protocol::LOCK_EVENTS_RSP) {
        m_lockEventsPage->showResponse(deviceId, data);
        return;
    }
    if (type == Protocol::DEBUG_READ_RSP) {
        handleDebugResponse(deviceId, data);
        return;
    }
    if (type == Protocol::CHARGE_MONITOR_ACK ||
        type == Protocol::CHARGE_MONITOR_REPORT) {
        handleChargeMonitorMessage(deviceId, type, data);
        return;
    }
    if (type == Protocol::DIB_WRITE_RSP) {
        uint8_t code = data.isEmpty() ? 0xFF : (uint8_t)data[0];
        QString msg;
        switch (code) {
            case 0x00: msg = "DIB 写入成功"; break;
            case 0x01: msg = "DIB 长度不匹配"; break;
            case 0x02: msg = "DIB 写入失败（Flash 错误）"; break;
            default:   msg = QString("DIB 未知返回码 0x%1").arg(code, 2, 16, QChar('0'));
        }
        addLog(QString("[%1] %2").arg(deviceId).arg(msg));
        return;
    }

    handleOtaUplink(deviceId, type, data);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DIB 配置按钮槽
// ─────────────────────────────────────────────────────────────────────────────
void Widget::onWriteDibClicked()
{
    if (m_selectedUid.isEmpty()) {
        QMessageBox::warning(this, "提示", "请先在左侧设备树选中一台设备");
        return;
    }

    const QString username = m_dibUsername->text().trimmed();
    const QString password = m_dibPassword->text().trimmed();
    const QString clientId = m_dibClientId->text().trimmed();
    const QString hostname = m_dibHostname->text().trimmed();

    struct FieldCheck {
        const char *name;
        QString     value;
        int         maxBytes;   // 不含末尾 '\0'
    } checks[] = {
        { "username", username, 79  },
        { "password", password, 95  },
        { "clientId", clientId, 95  },
        { "hostname", hostname, 127 },
    };

    for (auto &f : checks) {
        if (f.value.isEmpty()) {
            QMessageBox::warning(this, "提示",
                QString("%1 不能为空").arg(f.name));
            return;
        }
        int actual = f.value.toUtf8().size();
        if (actual > f.maxBytes) {
            QMessageBox::warning(this, "提示",
                QString("%1 超过最大长度：当前 %2 字节，上限 %3 字节")
                    .arg(f.name).arg(actual).arg(f.maxBytes));
            return;
        }
    }

    bool okPort = false, okWifi = false;
    int  portVal = m_dibPort->text().trimmed().toInt(&okPort);
    int  wifiVal = m_dibWifiAddr->text().trimmed().toInt(&okWifi);
    if (!okPort || portVal <= 0 || portVal > 65535) {
        QMessageBox::warning(this, "提示", "port 必须是 1~65535 的整数");
        return;
    }
    if (!okWifi || wifiVal < 0 || wifiVal > 65535) {
        QMessageBox::warning(this, "提示", "wifi_addr 必须是 0~65535 的整数");
        return;
    }

    auto ret = QMessageBox::question(this, "确认",
        QString("将 DIB 写入设备 %1？\n\n写入后原凭证会被覆盖，\n"
                "请确保输入正确（无法通过再次下发回滚到旧值除非你记得它）。")
            .arg(m_selectedUid),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes)
        return;

    sendDibConfig(m_selectedUid, username, password, clientId, hostname,
                  (uint16_t)portVal, (uint16_t)wifiVal);
}

/* ── 发送 DIB 配置到指定设备 ───────────────────────────────────────
 * 调用方负责从 UI 收集字段后调用本函数。返回后设备会在收到包时
 * 异步回复 DIB_WRITE_RSP，由 onMqttMessageReceived 打印结果日志。 */
void Widget::sendDibConfig(const QString &deviceId,
                           const QString &username,
                           const QString &password,
                           const QString &clientId,
                           const QString &hostname,
                           uint16_t port,
                           uint16_t wifiAddr)
{
    QByteArray payload = Protocol::buildDibPayload(
        username, password, clientId, hostname, port, wifiAddr);

    if (payload.size() != Protocol::DIB_SIZE) {
        addLog(QString("[%1] DIB 组装失败，长度异常 %2")
               .arg(deviceId).arg(payload.size()));
        return;
    }

    publishPacket(deviceId, Protocol::DIB_WRITE_REQ, payload);
    addLog(QString("[%1] 已下发 DIB 配置（%2 字节，无回复）")
           .arg(deviceId).arg(payload.size()));

    if (m_db.isOpen()) {
        QSqlQuery q;
        q.prepare("INSERT INTO dib_log(uid, username, written) VALUES(?, ?, ?) "
                  "ON CONFLICT(uid) DO UPDATE SET "
                  "username=excluded.username, written=excluded.written");
        q.addBindValue(deviceId);
        q.addBindValue(username);
        q.addBindValue(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));
        if (!q.exec())
            addLog("DIB 记录写入数据库失败: " + q.lastError().text());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  查看 DIB 写入记录
// ─────────────────────────────────────────────────────────────────────────────
void Widget::onViewDibLogClicked()
{
    QDialog dlg(this);
    dlg.setWindowTitle("DIB 写入记录");
    dlg.resize(720, 420);

    auto *vl = new QVBoxLayout(&dlg);
    auto *table = new QTableWidget(&dlg);
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels(QStringList() << "最后写入时间" << "UID" << "username");
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    vl->addWidget(table);

    if (m_db.isOpen()) {
        QSqlQuery q;
        q.exec("SELECT uid, username, written FROM dib_log ORDER BY written DESC");
        int row = 0;
        while (q.next()) {
            table->insertRow(row);
            table->setItem(row, 0, new QTableWidgetItem(q.value(2).toString()));
            table->setItem(row, 1, new QTableWidgetItem(q.value(0).toString()));
            table->setItem(row, 2, new QTableWidgetItem(q.value(1).toString()));
            ++row;
        }
    }

    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    auto *btnClose = new QPushButton("关闭", &dlg);
    btnRow->addWidget(btnClose);
    vl->addLayout(btnRow);
    QObject::connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);

    dlg.exec();
}

// ─────────────────────────────────────────────────────────────────────────────
//  从 JSON 文件自动填入 DIB 字段
// ─────────────────────────────────────────────────────────────────────────────
void Widget::onLoadDibFromFileClicked()
{
    QSettings cfg("OTA_Test", "OTA");
    QString lastDir = cfg.value("lastDibDir", "").toString();

    QString path = QFileDialog::getOpenFileName(
        this, "选择设备凭证文件", lastDir,
        "Device Key (*.txt *.json);;All Files (*)");
    if (path.isEmpty()) return;
    cfg.setValue("lastDibDir", QFileInfo(path).absolutePath());

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "错误", "无法打开文件:\n" + path);
        return;
    }
    QByteArray raw = f.readAll();
    f.close();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::warning(this, "错误",
            QString("JSON 解析失败：%1").arg(err.errorString()));
        return;
    }
    QJsonObject obj = doc.object();

    if (obj.contains("username")) m_dibUsername->setText(obj.value("username").toString());
    if (obj.contains("password")) m_dibPassword->setText(obj.value("password").toString());
    if (obj.contains("clientId")) m_dibClientId->setText(obj.value("clientId").toString());
    if (obj.contains("hostname")) m_dibHostname->setText(obj.value("hostname").toString());
    if (obj.contains("port"))     m_dibPort    ->setText(QString::number(obj.value("port").toInt()));

    addLog(QString("已从 %1 加载凭证：%2")
           .arg(QFileInfo(path).fileName())
           .arg(obj.value("username").toString()));
}

// ─────────────────────────────────────────────────────────────────────────────
//  软件复位选中设备
// ─────────────────────────────────────────────────────────────────────────────
void Widget::onResetDeviceClicked()
{
    if (!m_protectedPagesUnlocked) {
        QMessageBox::warning(this, "未授权", "请先在任一受保护页面输入密码解锁");
        return;
    }
    if (m_selectedUid.isEmpty()) {
        QMessageBox::warning(this, "提示", "请先在左侧设备树选中一台设备");
        return;
    }
    auto ret = QMessageBox::question(this, "确认",
        QString("对设备 %1 执行软件复位？").arg(m_selectedUid),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes)
        return;

    publishPacket(m_selectedUid, Protocol::RESET_REQ);
    addLog(QString("[%1] 已下发软件复位命令").arg(m_selectedUid));
}

// ─────────────────────────────────────────────────────────────────────────────
//  全局维护权限 — 重新锁定
// ─────────────────────────────────────────────────────────────────────────────
void Widget::onProtectedPagesLockClicked()
{
    setProtectedPagesUnlocked(false);
    addLog("[维护权限] 所有受保护页面已重新锁定");
}

// ─────────────────────────────────────────────────────────────────────────────
//  充电限制下发：state = 0 允许充电 / 1 限制充电
// ─────────────────────────────────────────────────────────────────────────────
void Widget::onChargeAllowClicked()
{
    if (!m_protectedPagesUnlocked) {
        QMessageBox::warning(this, "未授权", "请先在任一受保护页面输入密码解锁");
        return;
    }
    if (m_selectedUid.isEmpty()) {
        QMessageBox::warning(this, "提示", "请先在左侧设备树选中一台设备");
        return;
    }
    auto ret = QMessageBox::question(this, "确认",
        QString("对设备 %1 下发「允许充电」？").arg(m_selectedUid),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes)
        return;

    QByteArray payload;
    payload.append(char(0x00));  // 0 = 允许
    publishPacket(m_selectedUid, Protocol::CHARGE_LIMIT_REQ, payload);
    addLog(QString("[%1] 已下发『允许充电』(state=0)").arg(m_selectedUid));
}

void Widget::onChargeLimitClicked()
{
    if (!m_protectedPagesUnlocked) {
        QMessageBox::warning(this, "未授权", "请先在任一受保护页面输入密码解锁");
        return;
    }
    if (m_selectedUid.isEmpty()) {
        QMessageBox::warning(this, "提示", "请先在左侧设备树选中一台设备");
        return;
    }
    auto ret = QMessageBox::question(this, "确认",
        QString("对设备 %1 下发「限制充电」？\nEVCC 将立即限流为 0A。").arg(m_selectedUid),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes)
        return;

    QByteArray payload;
    payload.append(char(0x01));  // 1 = 限制
    publishPacket(m_selectedUid, Protocol::CHARGE_LIMIT_REQ, payload);
    addLog(QString("[%1] 已下发『限制充电』(state=1)").arg(m_selectedUid));
}

// ─────────────────────────────────────────────────────────────────────────────
//  擦除 flashData 数据区（破坏性，需双重确认）
// ─────────────────────────────────────────────────────────────────────────────
void Widget::onEraseFlashClicked()
{
    if (!m_protectedPagesUnlocked) {
        QMessageBox::warning(this, "未授权", "请先在任一受保护页面输入密码解锁");
        return;
    }
    if (m_selectedUid.isEmpty()) {
        QMessageBox::warning(this, "提示", "请先在左侧设备树选中一台设备");
        return;
    }

    auto ret = QMessageBox::warning(this, "危险操作",
        QString("将擦除设备 %1 的 flashData 数据区！\n\n"
                "下列持久化数据全部被清零：\n"
                "  · 充电/换电累计能量、次数\n"
                "  · 充电限制状态、电量额度、网络状态\n"
                "  · Flash 写入计数、CRC\n\n"
                "此操作不可恢复，确定继续？").arg(m_selectedUid),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes)
        return;

    bool ok = false;
    QString verify = QInputDialog::getText(this, "二次确认",
        "为防误操作，请输入 ERASE 后回车：",
        QLineEdit::Normal, "", &ok);
    if (!ok || verify.trimmed().toUpper() != "ERASE")
    {
        addLog("[远程控制] 擦除 Flash 已取消（二次确认未通过）");
        return;
    }

    publishPacket(m_selectedUid, Protocol::FLASH_ERASE_REQ);
    addLog(QString("[%1] 已下发『擦除 flashData』命令").arg(m_selectedUid));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Heartbeat
// ─────────────────────────────────────────────────────────────────────────────
void Widget::handleHeartbeat(const QString &deviceId, const QByteArray &payload)
{
    Protocol::HeartbeatData hb;
    if (!Protocol::parseHeartbeat(payload, hb)) {
        addLog(QString("[%1] 心跳 JSON 解析失败，忽略").arg(deviceId));
        return;
    }

    // 以批次字符串作为分组名；批次为空则归入「BOOT无APP」
    QString targetGroup = hb.batch.trimmed().isEmpty() ? "BOOT无APP" : hb.batch.trimmed();

    bool isNew = !m_devices.contains(deviceId);
    DeviceRecord &rec = m_devices[deviceId];
    rec.uid           = deviceId;
    rec.lastHeartbeat = QDateTime::currentDateTime();
    rec.hb            = hb;
    rec.hasHeartbeat  = true;

    addLog(QString("[%1] 心跳 [版本=%2  批次=%3  速度=%4 km/h  里程=%5 km  纬度=%6°  经度=%7°]")
           .arg(deviceId).arg(hb.version).arg(hb.batch).arg(hb.speed).arg(hb.total_mileage)
           .arg(hb.lat / 1000000.0, 0, 'f', 5).arg(hb.lng / 1000000.0, 0, 'f', 5));

    // 自动建组（持久化到 DB）
    auto ensureGroup = [&](const QString &grpName) {
        if (!m_groupItems.contains(grpName)) {
            getOrCreateGroup(grpName, grpName);
            if (m_db.isOpen()) {
                QSqlQuery q;
                q.prepare("INSERT OR IGNORE INTO groups(name,display_name,created)"
                          " VALUES(:n,:d,datetime('now'))");
                q.bindValue(":n", grpName);
                q.bindValue(":d", grpName);
                q.exec();
            }
            addLog("自动创建设备组: " + grpName);
        }
    };

    if (isNew) {
        rec.groupName = targetGroup;
        ensureGroup(targetGroup);
        addDeviceToTree(deviceId, targetGroup);
    } else if (rec.groupName != targetGroup) {
        // 批次变更：将设备移入新分组
        QString oldGroup = rec.groupName;
        rec.groupName = targetGroup;
        ensureGroup(targetGroup);

        if (m_deviceItems.contains(deviceId)) {
            QTreeWidgetItem *devItem   = m_deviceItems[deviceId];
            QTreeWidgetItem *oldParent = devItem->parent();
            if (oldParent) { oldParent->removeChild(devItem); refreshGroupCount(oldParent); }
            QTreeWidgetItem *newParent = m_groupItems[targetGroup];
            newParent->addChild(devItem);
            newParent->setExpanded(true);
            refreshGroupCount(newParent);
        }
        addLog(QString("[%1] 分组变更: %2 → %3").arg(deviceId).arg(oldGroup).arg(targetGroup));
    }

    saveDevice(deviceId, rec.groupName);
    updateTreeItem(deviceId);

    if (m_selectedUid == deviceId)
        updateDeviceInfo();

    // FINISH_ACK 后保留最近成功任务，等 App 心跳到达时补充最终上线确认。
    if (!hb.version.trimmed().isEmpty() &&
        m_lastSuccessfulMqttOtaByUid.contains(deviceId)) {
        const quint64 taskId = m_lastSuccessfulMqttOtaByUid.take(deviceId);
        if (MqttOtaTask *task = mqttOtaTask(taskId))
            task->confirmHeartbeat(hb.version);
    }

    if (m_otaState == OtaState::Idle && m_otaUid == deviceId)
        m_otaUid.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
//  OTA uplink handler
// ─────────────────────────────────────────────────────────────────────────────
void Widget::handleOtaUplink(const QString &deviceId, Protocol::MsgType type,
                              const QByteArray &data)
{
    const quint64 taskId = m_mqttOtaByUid.value(deviceId, 0);
    MqttOtaTask *task = mqttOtaTask(taskId);
    if (task == nullptr || !task->isRunning())
        return;

    addLog(QString("[OTA#%1][%2] 收到响应 type=0x%3")
               .arg(taskId)
               .arg(deviceId)
               .arg(static_cast<uint8_t>(type), 2, 16, QChar('0')));
    task->handlePacket(type, data);
}

void Widget::onReadLockEventsClicked()
{
    if (m_selectedUid.isEmpty()) {
        QMessageBox::information(this, "锁止事件", "请先在左侧选择一台设备");
        return;
    }
    if (m_mqtt->state() != MqttClient::Connected) {
        QMessageBox::information(this, "锁止事件", "请先连接服务器");
        return;
    }
    quint32 requestId = m_lockEventsPage->beginRequest(m_selectedUid);
    QByteArray payload;
    for (unsigned i=0;i<4;++i) payload.append(static_cast<char>(requestId >> (i*8)));
    publishPacket(m_selectedUid, Protocol::LOCK_EVENTS_REQ, payload);
    addLog(QString("[%1] 已发送锁止事件读取请求").arg(m_selectedUid));
}

void Widget::onReadSystemDataClicked()
{
    if (m_selectedUid.isEmpty()) {
        QMessageBox::information(this, "系统数据", "请先在左侧选择一台设备");
        return;
    }
    if (m_mqtt->state() != MqttClient::Connected) {
        QMessageBox::information(this, "系统数据", "请先连接服务器");
        return;
    }
    quint32 requestId = m_systemDataPage->beginRequest(m_selectedUid);
    QByteArray payload;
    for (unsigned i=0;i<4;++i) payload.append(static_cast<char>(requestId >> (i*8)));
    publishPacket(m_selectedUid, Protocol::SYSTEM_DATA_REQ, payload);
    addLog(QString("[%1] 已发送系统数据读取请求").arg(m_selectedUid));
}

void Widget::onReadDebugClicked()
{
    if (m_selectedUid.isEmpty()) {
        addLog("请先在左侧选择一台设备");
        return;
    }
    publishPacket(m_selectedUid, Protocol::DEBUG_READ_REQ);
    addLog(QString("[%1] 已发送调试数据读取请求").arg(m_selectedUid));
}

void Widget::handleDebugResponse(const QString &deviceId, const QByteArray &data)
{
    QString parseErr;
    auto rows = Protocol::parseDebugData(data, &parseErr);
    if (rows.isEmpty()) {
        addLog(QString("[%1] 调试数据解析失败或为空 (len=%2)%3")
               .arg(deviceId)
               .arg(data.size())
               .arg(parseErr.isEmpty() ? QString() : QString(" — %1").arg(parseErr)));
        addLog(QString("[%1] 原始 payload:\n%2")
               .arg(deviceId)
               .arg(QString::fromUtf8(data)));
        return;
    }

    m_debugTable->setRowCount(0);
    for (const auto &kv : rows) {
        int r = m_debugTable->rowCount();
        m_debugTable->insertRow(r);
        m_debugTable->setItem(r, 0, new QTableWidgetItem(kv.first));
        m_debugTable->setItem(r, 1, new QTableWidgetItem(kv.second));
    }
    m_lblDebugFrom->setText(QString("来源: %1  (%2 条)")
                            .arg(deviceId).arg(rows.size()));
    addLog(QString("[%1] 收到调试数据，共 %2 个字段").arg(deviceId).arg(rows.size()));
}

void Widget::sendChargeMonitorCommand(Protocol::ChargeMonitorCommand command)
{
    if (m_selectedUid.isEmpty()) {
        addLog("请先在左侧选择一台设备");
        return;
    }

    QByteArray payload(1, static_cast<char>(command));
    publishPacket(m_selectedUid, Protocol::CHARGE_MONITOR_REQ, payload);
    const bool armed = command == Protocol::ChargeMonitorCommand::Arm;
    m_lblChargeMonitorState->setText(armed ? "状态: 等待设备确认"
                                                  : "状态: 等待停止确认");
    const QString line = QString("[%1] [%2] 已下发%3命令")
        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz"))
        .arg(m_selectedUid)
        .arg(armed ? "开始检测" : "停止检测");
    m_chargeMonitorLog->append(line.toHtmlEscaped());
    addLog(QString("[%1] %2").arg(m_selectedUid)
               .arg(armed ? "已下发系统事件监测命令"
                           : "已下发停止系统事件监测命令"));
}

void Widget::handleChargeMonitorMessage(const QString &deviceId,
                                        Protocol::MsgType type,
                                        const QByteArray &data)
{
    // Always collect trace fragments, even without a selected device or monitor command.
    const QJsonObject traceObject=QJsonDocument::fromJson(data).object();
    if (traceObject.value("reason").toString()=="system_trace") {
        const auto result=m_traceReceiver.accept(deviceId,traceObject,QDir(QStringLiteral(APP_SOURCE_DIR)).filePath("diagnostics"));
        if (!result.error.isEmpty()) addLog(QString("[%1] %2").arg(deviceId,result.error));
        if (result.saved) {
            publishPacket(deviceId,static_cast<Protocol::MsgType>(0x15),result.ack);
            if (m_traceSummaries.size()>=256 && !m_traceSummaries.contains(deviceId)) m_traceSummaries.erase(m_traceSummaries.begin());
            m_traceSummaries[deviceId]=result.summary;
            if (result.fresh) {
                addLog(QString("[%1] 主动追踪已保存：%2").arg(deviceId,result.path));
                if (deviceId==m_selectedUid) {
                    m_chargeMonitorLog->append(result.summary.toHtmlEscaped().replace("\n","<br>"));
                    m_lblChargeMonitorState->setText("状态: 追踪现场已自动保存");
                }
            }
        }
        return;
    }
    // MQTT订阅仍用于维护全部设备；系统事件页面只展示左侧当前选中的设备。
    if (m_selectedUid.isEmpty() || deviceId != m_selectedUid)
        return;

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(data, &error);
    const QString localTime = QDateTime::currentDateTime()
                                  .toString("yyyy-MM-dd HH:mm:ss.zzz");
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        m_chargeMonitorLog->append(
            QString("[%1] 设备 %2 的系统事件数据解析失败：%3")
                .arg(localTime)
                .arg(deviceId)
                .arg(error.errorString()).toHtmlEscaped());
        return;
    }

    const QJsonObject object = document.object();
    const QString reason = object.value("reason").toString("unknown");
    const int state = object.value("monitor_state").toInt();
    const qint64 timestamp = object.value("timestamp").toVariant().toLongLong();
    const QString deviceTime = timestamp > 0
        ? QDateTime::fromSecsSinceEpoch(timestamp).toLocalTime()
              .toString("yyyy-MM-dd HH:mm:ss")
        : "时间无效";
    const QJsonObject can1 = object.value("can1").toObject();
    const bool isAck = type == Protocol::CHARGE_MONITOR_ACK;
    const QString title = isAck ? "命令确认" : "主动事件";
    const QString advice = chargeMonitorReasonAdvice(reason);
    const quint32 lastError = can1.value("last_error").toVariant().toUInt();

    QStringList lines;
    lines << QString("[%1] 【%2】%3")
                 .arg(localTime, title, chargeMonitorReasonText(reason));
    lines << QString("设备：%1").arg(deviceId);
    lines << QString("设备时间：%1").arg(deviceTime);
    lines << QString("检测状态：%1").arg(chargeMonitorStateText(state));
    lines << QString("充电信息：%1，SOC：%2%，车辆%3")
                 .arg(bmsChargeStateText(object.value("bms_charge_state").toInt()))
                 .arg(object.value("soc").toInt())
                 .arg(object.value("vehicle_online").toInt() ? "在线" : "离线");
    if (object.contains("sleep_idle_seconds"))
    lines << QString("休眠计数：已空闲约 %1 秒，距休眠约 %2 秒（阈值 %3 秒）")
                 .arg(object.value("sleep_idle_seconds").toInt())
                 .arg(object.value("sleep_remaining_seconds").toInt())
                 .arg(object.value("sleep_timeout_seconds").toInt());
    lines << QString("CAN1状态：%1，发送邮箱空闲 %2 个，ESR %3")
                 .arg(canHalStateText(can1.value("hal_state").toInt()))
                 .arg(can1.value("mailbox_free").toInt())
                 .arg(can1.value("esr").toString());
    lines << QString("CAN1累计：错误 %1，Bus-Off %2，发送成功放入邮箱 %3，无应答 %4，忙 %5，发送失败 %6")
                 .arg(formattedCount(can1, "error_count"))
                 .arg(formattedCount(can1, "busoff_count"))
                 .arg(formattedCount(can1, "tx_submit_count"))
                 .arg(formattedCount(can1, "ack_error_count"))
                 .arg(formattedCount(can1, "tx_busy_count"))
                 .arg(formattedCount(can1, "tx_error_count"));
    lines << QString("CAN1控制器处理：Bus-Off自动退出 %1，强制重启 %2，重启失败 %3；队列丢帧 %4；最近HAL错误 0x%5")
                 .arg(formattedCount(can1, "auto_recover_count"))
                 .arg(formattedCount(can1, "recover_count"))
                 .arg(formattedCount(can1, "recover_fail_count"))
                 .arg(formattedCount(can1, "tx_queue_drop_count"))
                 .arg(lastError, 8, 16, QChar('0'));
    lines << QString("当前监测阶段新增：CAN错误 %1，Bus-Off %2，无应答 %3，发送失败 %4，队列丢帧 %5，Bus-Off自动退出 %6，重启失败 %7")
                 .arg(formattedCount(can1, "error_delta"))
                 .arg(formattedCount(can1, "busoff_delta"))
                 .arg(formattedCount(can1, "ack_error_delta"))
                 .arg(formattedCount(can1, "tx_error_delta"))
                 .arg(formattedCount(can1, "tx_queue_drop_delta"))
                 .arg(formattedCount(can1, "auto_recover_delta"))
                 .arg(formattedCount(can1, "recover_fail_delta"));
    if (object.contains("SYS_last_reset_reason")) {
        lines << QString("复位原因：%1；启动累计 %2；IWDG %3；WWDG %4；HardFault %5；Error_Handler %6")
                     .arg(object.value("SYS_last_reset_reason").toString())
                     .arg(formattedCount(object, "SYS_boot_count"))
                     .arg(formattedCount(object, "SYS_iwdg_reset_count"))
                     .arg(formattedCount(object, "SYS_wwdg_reset_count"))
                     .arg(formattedCount(object, "SYS_hardfault_reset_count"))
                     .arg(formattedCount(object, "SYS_error_handler_count"));
        lines << QString("最近异常：CFSR 0x%1，HFSR 0x%2，故障地址 0x%3")
                     .arg(object.value("SYS_last_cfsr").toVariant().toUInt(), 8, 16, QChar('0'))
                     .arg(object.value("SYS_last_hfsr").toVariant().toUInt(), 8, 16, QChar('0'))
                     .arg(object.value("SYS_last_fault_address").toVariant().toUInt(), 8, 16, QChar('0'));
        lines << QString("事件序号 %1，启动后 %2 ms；系统阶段 %3（1=NTC，2=休眠，3=喂狗，4=Flash，5=延时）；常规喂狗 %4 次，距上次 %5 ms；事件队列丢弃 %6")
                     .arg(formattedCount(object, "event_seq"))
                     .arg(formattedCount(object, "event_uptime_ms"))
                     .arg(formattedCount(object, "SYS_task_stage"))
                     .arg(formattedCount(object, "SYS_feed_count"))
                     .arg(formattedCount(object, "SYS_feed_age_ms"))
                     .arg(formattedCount(object, "event_queue_dropped"));
        lines << QString("CAN接收队列丢帧 %1；最大提交间隔 %2 ms（提交不等于已获得总线ACK）")
                     .arg(formattedCount(can1, "rx_queue_drop_count"))
                     .arg(formattedCount(can1, "max_submit_gap_ms"));
    }
    if (!advice.isEmpty())
        lines << "判断建议：" + advice;

    const QString readableLog = lines.join('\n').toHtmlEscaped()
                                    .replace("\n", "<br>");
    m_chargeMonitorLog->append(readableLog);
    m_chargeMonitorLog->append("<hr>");

    if (isAck) {
        const auto monitorState = static_cast<Protocol::ChargeMonitorState>(state);
        m_lblChargeMonitorState->setText(
            monitorState == Protocol::ChargeMonitorState::Charging
                ? "状态: 检测中（正在充电）"
                : monitorState == Protocol::ChargeMonitorState::Armed
                    ? "状态: 系统事件监测中（当前未充电）"
                    : "状态: 未布防");
    } else {
        if (reason == "system_sleep") {
            m_lblChargeMonitorState->setText("状态: 设备休眠，监测已结束");
        } else {
            m_lblChargeMonitorState->setText(
                state == static_cast<int>(Protocol::ChargeMonitorState::Charging)
                    ? "状态: 持续检测中（正在充电）"
                    : state == static_cast<int>(Protocol::ChargeMonitorState::Armed)
                        ? "状态: 持续检测中（当前未充电）"
                        : "状态: 监测已结束");
        }
    }
    addLog(QString("[%1] 系统事件监测%2：%3")
               .arg(deviceId)
               .arg(title)
               .arg(chargeMonitorReasonText(reason)));
}

void Widget::publishPacket(const QString &deviceId, Protocol::MsgType type,
                           const QByteArray &data)
{
    QByteArray pkt = Protocol::buildPacket(type, data);
    QString topic = QString(Protocol::TOPIC_DOWN_FMT).arg(deviceId);
    QTimer::singleShot(200, this, [this, topic, pkt]() {
        if (m_mqtt->state() == MqttClient::Connected)
            m_mqtt->publish(topic, pkt, 1);
    });
}

// ─── RS485 串口发送（不依赖 deviceId/topic）─────────────────────────────────
bool Widget::serialSendPacket(Protocol::MsgType type, const QByteArray &data)
{
    if (!m_serial->isOpen()) {
        otaFail("串口未打开，命令未发送");
        return false;
    }
    const QByteArray packet = Protocol::buildPacket(type, data);
    if (m_serial->write(packet) != packet.size()) {
        otaFail("串口写入失败: " + m_serial->errorString());
        return false;
    }
    if (type == Protocol::OTA_ENTER)
        addLog(QString("串口 TX 已入队（%1，%2 bps）: %3")
               .arg(m_serial->portName()).arg(m_serial->baudRate())
               .arg(QString::fromLatin1(packet.toHex(' '))));
    return true;
}

// ─── 串口列表刷新 ─────────────────────────────────────────────────────────────
void Widget::refreshSerialPorts()
{
    // 获取当前可用端口列表
    QStringList current;
    for (const auto &info : QSerialPortInfo::availablePorts())
        current << info.portName();

    if (current == m_lastPortList)
        return;  // 没有变化，直接返回

    // 检测新增 / 移除
    QStringList added, removed;
    for (const auto &p : current)
        if (!m_lastPortList.contains(p)) added << p;
    for (const auto &p : m_lastPortList)
        if (!current.contains(p)) removed << p;

    // 记住当前选中项
    QString selected = m_cmbSerialPort->currentText();

    // 如果已打开的端口被拔出，自动关闭串口
    if (m_serial->isOpen() && removed.contains(m_serial->portName())) {
        m_serial->close();
        m_serialRxBuf.clear();
        if (m_otaState != OtaState::Idle)
            otaFail("串口已断开，升级终止");
        m_btnSerialOpen->setText("OPEN");
        m_lblSerialStatus->setText("●  CLOSED");
        m_lblSerialStatus->setStyleSheet("color:#FF2E97;font-weight:bold;letter-spacing:1px;"
                                         "background:transparent;border:none;");
        updateOtaControls();
        addLog(QString("⚠ 串口 %1 已断开").arg(m_serial->portName()));
    }

    // 更新下拉列表
    m_cmbSerialPort->blockSignals(true);
    m_cmbSerialPort->clear();
    m_cmbSerialPort->addItems(current);
    // 恢复之前的选中项（如果还存在）
    int idx = m_cmbSerialPort->findText(selected);
    if (idx >= 0)
        m_cmbSerialPort->setCurrentIndex(idx);
    m_cmbSerialPort->blockSignals(false);

    m_lastPortList = current;

    // 记录日志
    for (const auto &p : added)
        addLog(QString("✚ 检测到新串口: %1").arg(p));
    for (const auto &p : removed)
        addLog(QString("✖ 串口已移除: %1").arg(p));
}

void Widget::onRefreshSerialPorts()
{
    refreshSerialPorts();
}

// ─── 串口开关 ─────────────────────────────────────────────────────────────────
void Widget::onSerialOpenClicked()
{
    if (m_serial->isOpen()) {
        m_serial->close();
        m_serialRxBuf.clear();
        if (m_otaState != OtaState::Idle)
            otaFail("串口已关闭，升级终止");
        m_btnSerialOpen->setText("OPEN");
        m_lblSerialStatus->setText("●  CLOSED");
        m_lblSerialStatus->setStyleSheet("color:#FF2E97;font-weight:bold;letter-spacing:1px;"
                                         "background:transparent;border:none;");
        updateOtaControls();
        addLog("串口已关闭");
        return;
    }

    m_serialRxBuf.clear();
    m_serial->setPortName(m_cmbSerialPort->currentText());
    m_serial->setBaudRate(m_cmbBaudRate->currentData().toInt());
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_serial->open(QIODevice::ReadWrite)) {
        addLog("串口打开失败: " + m_serial->errorString());
        return;
    }

    m_btnSerialOpen->setText("CLOSE");
    m_lblSerialStatus->setText("●  LIVE");
    m_lblSerialStatus->setStyleSheet("color:#39FF14;font-weight:bold;letter-spacing:1px;"
                                     "background:transparent;border:none;");
    updateOtaControls();
    addLog(QString("串口已打开: %1  %2 bps")
           .arg(m_cmbSerialPort->currentText())
           .arg(m_cmbBaudRate->currentData().toInt()));
}

// ─── 串口接收：粘包处理，提取完整 V2.1 帧 ──────────────────────────────────
void Widget::onSerialReadyRead()
{
    const QByteArray received = m_serial->readAll();
    if (m_otaState == OtaState::WaitBootAck && !received.isEmpty()) {
        m_bootRxBytes += received.size();
        addLog(QString("BOOT 握手 RX %1 字节: %2%3")
               .arg(received.size())
               .arg(QString::fromLatin1(received.left(128).toHex(' ')))
               .arg(received.size() > 128 ? " ..." : ""));
    }
    m_serialRxBuf.append(received);

    // 帧格式：[0xFA][Type 1B][Len 2B LE][Data NB][CRC16 2B LE]  最短 6 字节
    while (m_serialRxBuf.size() >= 6) {
        // 找帧头
        int start = m_serialRxBuf.indexOf((char)0xFA);
        if (start < 0) { m_serialRxBuf.clear(); break; }
        if (start > 0)  { m_serialRxBuf.remove(0, start); }

        if (m_serialRxBuf.size() < 4) break;  // 还没收到 Len 字段

        uint16_t dataLen = (uint8_t)m_serialRxBuf[2]
                         | ((uint8_t)m_serialRxBuf[3] << 8);
        int totalLen = 6 + dataLen;            // header(1)+type(1)+len(2)+data(N)+crc(2)
        if (m_serialRxBuf.size() < totalLen) break;  // 数据还没到齐

        QByteArray frame = m_serialRxBuf.left(totalLen);

        Protocol::MsgType type;
        QByteArray payload;
        if (!Protocol::parsePacket(frame, type, payload)) {
            // CRC 失败时只跳过当前帧头，避免吞掉后面的有效 ACK。
            m_serialRxBuf.remove(0, 1);
            addLog("串口: CRC 校验失败，重新寻找帧头");
            continue;
        }
        m_serialRxBuf.remove(0, totalLen);

        if (m_otaState == OtaState::Idle) {
            addLog("串口收到 OTA 响应，但当前没有串口升级任务，已忽略");
            continue;
        }

        addLog(QString("串口 OTA 响应  type=0x%1").arg((uint8_t)type, 2, 16, QChar('0')));
        m_otaTimeout->start(10000);
        otaNextStep(type, payload);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Periodic refresh
// ─────────────────────────────────────────────────────────────────────────────
void Widget::onRefreshTimer()
{
    for (const auto &rec : m_devices)
        updateTreeItem(rec.uid);

    if (!m_selectedUid.isEmpty())
        updateDeviceInfo();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Device info panel
// ─────────────────────────────────────────────────────────────────────────────
void Widget::updateDeviceInfo()
{
    if (m_lockEventsPage) m_lockEventsPage->setDevice(m_selectedUid);
    if (m_systemDataPage) m_systemDataPage->setDevice(m_selectedUid);
    if (m_selectedUid.isEmpty() || !m_devices.contains(m_selectedUid)) {
        m_lblUid->setText("-");
        return;
    }
    const DeviceRecord         &rec = m_devices[m_selectedUid];
    const Protocol::HeartbeatData &hb = rec.hb;

    m_lblUid->setText(rec.uid);

    if (!rec.hasHeartbeat) {
        m_lblVersion->setText("-");
        m_lblBatch  ->setText("-");
        m_lblVin    ->setText("-");
        m_lblLastHb ->setText("从未（历史记录）");
        m_lblComm   ->setText("-");
        m_lblVehicle->setText("-");
        m_lblHv     ->setText("-");
        m_lblBrake  ->setText("-");
        m_lblSpeed  ->setText("-");
        m_lblMileage->setText("-");
        m_lblCharge ->setText("-");
        m_lblPos    ->setText("-");
        return;
    }

    m_lblVersion->setText(hb.version.isEmpty() ? "-" : hb.version);
    m_lblBatch->setText(hb.batch.isEmpty() ? "-" : hb.batch);
    m_lblVin  ->setText(hb.vin.isEmpty()   ? "-" : hb.vin);

    {
        qint64 secs = (qint64)rec.lastHeartbeat.secsTo(QDateTime::currentDateTime());
        QString rel;
        if (secs < 60) {
            rel = QString("%1秒前").arg(secs);
        } else if (secs < 3600) {
            rel = QString("%1分%2秒前").arg(secs / 60).arg(secs % 60);
        } else if (secs < 86400) {
            rel = QString("%1时%2分%3秒前")
                  .arg(secs / 3600).arg((secs % 3600) / 60).arg(secs % 60);
        } else if (secs < 86400LL * 365) {
            qint64 d = secs / 86400, r = secs % 86400;
            rel = QString("%1天%2时%3分%4秒前")
                  .arg(d).arg(r / 3600).arg((r % 3600) / 60).arg(r % 60);
        } else {
            qint64 y = secs / (86400LL * 365), r = secs % (86400LL * 365);
            qint64 d = r / 86400; r %= 86400;
            rel = QString("%1年%2天%3时%4分%5秒前")
                  .arg(y).arg(d).arg(r / 3600).arg((r % 3600) / 60).arg(r % 60);
        }
        m_lblLastHb->setText(
            rec.lastHeartbeat.toString("yyyy年MM月dd日 hh:mm:ss") +
            "  （" + rel + "）");
    }

    struct ColoredText { QString text; QString color; };
    auto setColored = [](QLabel *lbl, const QString &text, const QString &color) {
        lbl->setText(text);
        lbl->setStyleSheet(QString("color:%1;background:transparent;border:none;font-weight:bold;")
                           .arg(color));
    };
    auto clearColor = [](QLabel *lbl, const QString &text) {
        lbl->setText(text);
        lbl->setStyleSheet("background:transparent;border:none;");
    };
    Q_UNUSED(clearColor);

    // 通讯状态
    if (hb.comm_state == 1)
        setColored(m_lblComm, "已连接", "#66BB6A");
    else
        setColored(m_lblComm, "断开", "#EF5350");

    // 车辆状态
    static const QStringList vehStates = {"熄火", "上电", "行驶中"};
    static const QStringList vehColors = {"#888888", "#FFA726", "#66BB6A"};
    int vs = hb.vehicle_state;
    setColored(m_lblVehicle,
               (vs >= 0 && vs < vehStates.size()) ? vehStates[vs] : "未知",
               (vs >= 0 && vs < vehColors.size()) ? vehColors[vs] : "#888888");

    // 高压状态：与固件心跳一致，0=断开，1=闭合
    if (hb.hv_state == 1)
        setColored(m_lblHv, "闭合", "#66BB6A");
    else if (hb.hv_state == 0)
        setColored(m_lblHv, "断开", "#EF5350");
    else
        setColored(m_lblHv, "未知", "#888888");

    // 手刹状态
    if (hb.brake_state == 0)
        setColored(m_lblBrake, "放下", "#888888");
    else
        setColored(m_lblBrake, "拉起", "#FFA726");

    // 车速
    m_lblSpeed->setStyleSheet("background:transparent;border:none;");
    m_lblSpeed->setText(QString("%1 km/h").arg(hb.speed));

    // 总里程
    m_lblMileage->setStyleSheet("background:transparent;border:none;");
    m_lblMileage->setText(QString("%1 km").arg(hb.total_mileage));

    // 充电状态
    static const QStringList chgStates  = {"—", "充电中", "放电中", "其他"};
    static const QStringList chgColors  = {"#888888", "#4FC3F7", "#66BB6A", "#888888"};
    int cs = hb.charge_state;
    setColored(m_lblCharge,
               (cs >= 0 && cs < chgStates.size()) ? chgStates[cs] : "未知",
               (cs >= 0 && cs < chgColors.size()) ? chgColors[cs] : "#888888");

    // 位置：显示坐标，点击在系统浏览器打开 OpenStreetMap
    double lngD = hb.lng / 1000000.0;
    double latD = hb.lat / 1000000.0;
    if (hb.lng == 0 && hb.lat == 0) {
        m_lblPos->setStyleSheet("background:transparent;border:none;color:#888888;");
        m_lblPos->setText("暂无定位数据");
        m_lblPos->setOpenExternalLinks(false);
    } else {
        // 高德网页地图（国内打得开），点击在系统浏览器打开
        QString url = QString("https://uri.amap.com/marker?position=%1,%2&name=设备位置")
                      .arg(lngD, 0, 'f', 6).arg(latD, 0, 'f', 6);
        m_lblPos->setTextFormat(Qt::RichText);
        m_lblPos->setOpenExternalLinks(true);
        m_lblPos->setTextInteractionFlags(Qt::TextBrowserInteraction);
        m_lblPos->setCursor(Qt::PointingHandCursor);
        m_lblPos->setStyleSheet("background:transparent;border:none;");
        m_lblPos->setText(QString("<a href=\"%1\" style=\"color:#00E5FF;\">"
                                  "经度 %2°&nbsp;&nbsp;|&nbsp;&nbsp;纬度 %3°&nbsp;&nbsp;"
                                  "在地图中查看</a>")
                          .arg(url)
                          .arg(lngD, 0, 'f', 5)
                          .arg(latD, 0, 'f', 5));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Concurrent MQTT OTA task manager
// ─────────────────────────────────────────────────────────────────────────────
QStringList Widget::selectedDeviceUids() const
{
    QStringList result;
    if (m_tree != nullptr) {
        const auto selectedItems = m_tree->selectedItems();
        for (QTreeWidgetItem *item : selectedItems) {
            if (item == nullptr ||
                item->data(0, Qt::UserRole + 1).toString() != "device") {
                continue;
            }
            const QString uid = item->data(0, Qt::UserRole).toString();
            if (!uid.isEmpty() && !result.contains(uid))
                result.append(uid);
        }
    }

    if (result.isEmpty() && !m_selectedUid.isEmpty())
        result.append(m_selectedUid);
    return result;
}

int Widget::runningMqttOtaTaskCount() const
{
    int count = 0;
    for (MqttOtaTask *task : m_mqttOtaTasks) {
        if (task != nullptr && task->isRunning())
            ++count;
    }
    return count;
}

MqttOtaTask *Widget::mqttOtaTask(quint64 taskId) const
{
    return m_mqttOtaTasks.value(taskId, nullptr);
}

int Widget::mqttOtaTaskRow(quint64 taskId) const
{
    if (m_mqttOtaTable == nullptr)
        return -1;

    for (int row = 0; row < m_mqttOtaTable->rowCount(); ++row) {
        QTableWidgetItem *item = m_mqttOtaTable->item(row, 0);
        if (item != nullptr &&
            item->data(Qt::UserRole).toULongLong() == taskId) {
            return row;
        }
    }
    return -1;
}

void Widget::addMqttOtaTaskRow(MqttOtaTask *task)
{
    if (task == nullptr || m_mqttOtaTable == nullptr)
        return;

    const int row = m_mqttOtaTable->rowCount();
    m_mqttOtaTable->insertRow(row);

    auto *uidItem = new QTableWidgetItem(task->deviceId());
    uidItem->setData(Qt::UserRole,
                     QVariant::fromValue<qulonglong>(task->id()));
    uidItem->setToolTip(task->deviceId());
    m_mqttOtaTable->setItem(row, 0, uidItem);
    m_mqttOtaTable->setItem(row, 1,
                            new QTableWidgetItem(task->firmwareName()));
    m_mqttOtaTable->setItem(row, 2,
                            new QTableWidgetItem(task->statusText()));

    auto *progress = new QProgressBar;
    progress->setRange(0, 100);
    progress->setValue(task->progress());
    progress->setFormat("%p%");
    m_mqttOtaTable->setCellWidget(row, 3, progress);
    m_mqttOtaTable->setRowHeight(row, 28);
    updateMqttOtaTaskRow(task->id());
}

void Widget::updateMqttOtaTaskRow(quint64 taskId)
{
    MqttOtaTask *task = mqttOtaTask(taskId);
    const int row = mqttOtaTaskRow(taskId);
    if (task == nullptr || row < 0)
        return;

    QTableWidgetItem *statusItem = m_mqttOtaTable->item(row, 2);
    if (statusItem != nullptr) {
        statusItem->setText(task->statusText());
        QColor color("#7FDEEA");
        if (task->state() == MqttOtaTask::State::Succeeded)
            color = QColor("#66BB6A");
        else if (task->state() == MqttOtaTask::State::Failed ||
                 task->state() == MqttOtaTask::State::Cancelled)
            color = QColor("#EF5350");
        else if (task->isQueued())
            color = QColor("#FFD300");
        statusItem->setForeground(color);
    }

    if (auto *progress =
            qobject_cast<QProgressBar *>(
                m_mqttOtaTable->cellWidget(row, 3))) {
        progress->setValue(task->progress());
    }
    updateMqttOtaSummary();
}

void Widget::updateMqttOtaSummary()
{
    if (m_lblMqttOtaSummary == nullptr)
        return;

    int queued = 0;
    int succeeded = 0;
    int failed = 0;
    for (MqttOtaTask *task : m_mqttOtaTasks) {
        if (task == nullptr)
            continue;
        if (task->isQueued())
            ++queued;
        else if (task->state() == MqttOtaTask::State::Succeeded)
            ++succeeded;
        else if (task->state() == MqttOtaTask::State::Failed ||
                 task->state() == MqttOtaTask::State::Cancelled)
            ++failed;
    }

    m_lblMqttOtaSummary->setText(
        QString("运行 %1 / %2　排队 %3　成功 %4　失败 %5")
            .arg(runningMqttOtaTaskCount())
            .arg(kMaxConcurrentMqttOtaTasks)
            .arg(queued)
            .arg(succeeded)
            .arg(failed));
}

void Widget::updateOtaControls()
{
    if (m_btnStart == nullptr || m_cmbChannel == nullptr)
        return;

    const OtaChannel channel =
        static_cast<OtaChannel>(m_cmbChannel->currentData().toInt());
    if (channel == OtaChannel::Serial) {
        m_btnStart->setEnabled(!m_firmware.isEmpty() &&
                               m_serial != nullptr &&
                               m_serial->isOpen() &&
                               m_otaState == OtaState::Idle);
        return;
    }

    bool hasEligibleDevice = false;
    for (const QString &uid : selectedDeviceUids()) {
        if (m_devices.contains(uid) &&
            m_devices[uid].isActive() &&
            !m_mqttOtaByUid.contains(uid)) {
            hasEligibleDevice = true;
            break;
        }
    }

    m_btnStart->setEnabled(
        !m_firmware.isEmpty() &&
        m_mqtt != nullptr &&
        m_mqtt->state() == MqttClient::Connected &&
        hasEligibleDevice);
}

void Widget::createMqttOtaTasks()
{
    const QStringList selected = selectedDeviceUids();
    if (selected.isEmpty()) {
        QMessageBox::warning(this, "错误", "请先在左侧设备树选择设备");
        return;
    }

    QStringList offlineDevices;
    QStringList duplicateDevices;
    int createdCount = 0;
    const int packetSize = m_cmbPktSize->currentData().toInt();
    const QString firmwareName =
        m_firmwareName.isEmpty() ? QString("firmware.bin") : m_firmwareName;

    for (const QString &uid : selected) {
        if (!m_devices.contains(uid) || !m_devices[uid].isActive()) {
            offlineDevices.append(uid);
            continue;
        }
        if (m_mqttOtaByUid.contains(uid)) {
            duplicateDevices.append(uid);
            continue;
        }

        const quint64 taskId = m_nextMqttOtaTaskId++;
        auto *task = new MqttOtaTask(taskId, uid, firmwareName,
                                     m_firmware, packetSize, this);
        m_mqttOtaTasks.insert(taskId, task);
        m_mqttOtaByUid.insert(uid, taskId);
        m_mqttOtaQueue.enqueue(taskId);

        connect(task, &MqttOtaTask::packetReady,
                this, &Widget::onMqttOtaPacketReady);
        connect(task, &MqttOtaTask::changed,
                this, &Widget::updateMqttOtaTaskRow);
        connect(task, &MqttOtaTask::logMessage, this,
                [this, task](quint64 id, const QString &message) {
                    addLog(QString("[OTA#%1][%2] %3")
                               .arg(id)
                               .arg(task->deviceId())
                               .arg(message));
                });
        connect(task, &MqttOtaTask::finished,
                this, &Widget::onMqttOtaTaskFinished);

        addMqttOtaTaskRow(task);
        ++createdCount;
    }

    if (createdCount > 0) {
        addLog(QString("已新建 %1 个 MQTT OTA 任务，固件 %2，包大小 %3 字节")
                   .arg(createdCount)
                   .arg(firmwareName)
                   .arg(packetSize));
        startQueuedMqttOtaTasks();
    }

    if (!offlineDevices.isEmpty()) {
        addLog(QString("以下设备离线，未创建任务：%1")
                   .arg(offlineDevices.join(", ")));
    }
    if (!duplicateDevices.isEmpty()) {
        addLog(QString("以下设备已有升级任务，已跳过：%1")
                   .arg(duplicateDevices.join(", ")));
    }
    if (createdCount == 0) {
        QMessageBox::information(
            this, "未创建任务",
            "选中的设备均已离线，或已经存在升级/排队任务。");
    }

    updateMqttOtaQueuePositions();
    updateMqttOtaSummary();
    updateOtaControls();
}

void Widget::startQueuedMqttOtaTasks()
{
    if (m_mqtt == nullptr ||
        m_mqtt->state() != MqttClient::Connected) {
        updateMqttOtaQueuePositions();
        return;
    }

    while (runningMqttOtaTaskCount() < kMaxConcurrentMqttOtaTasks &&
           !m_mqttOtaQueue.isEmpty()) {
        const quint64 taskId = m_mqttOtaQueue.dequeue();
        MqttOtaTask *task = mqttOtaTask(taskId);
        if (task == nullptr || !task->isQueued())
            continue;

        addLog(QString("[OTA#%1][%2] 从队列启动（当前并发 %3/%4）")
                   .arg(taskId)
                   .arg(task->deviceId())
                   .arg(runningMqttOtaTaskCount() + 1)
                   .arg(kMaxConcurrentMqttOtaTasks));
        task->start();
    }

    updateMqttOtaQueuePositions();
    updateMqttOtaSummary();
    updateOtaControls();
}

void Widget::updateMqttOtaQueuePositions()
{
    int position = 1;
    for (quint64 taskId : m_mqttOtaQueue) {
        MqttOtaTask *task = mqttOtaTask(taskId);
        if (task != nullptr && task->isQueued())
            task->setQueuePosition(position++);
    }
}

void Widget::onMqttOtaPacketReady(quint64 taskId,
                                  const QString &deviceId,
                                  Protocol::MsgType type,
                                  const QByteArray &data,
                                  int timeoutMs)
{
    const QByteArray packet = Protocol::buildPacket(type, data);
    const QString topic = QString(Protocol::TOPIC_DOWN_FMT).arg(deviceId);

    // 保留原 OTA 下发前 200ms 间隔；每个任务的超时从真正 publish 后开始计算。
    QTimer::singleShot(200, this,
                       [this, taskId, topic, packet, timeoutMs]() {
        MqttOtaTask *task = mqttOtaTask(taskId);
        if (task == nullptr || !task->isRunning())
            return;
        if (m_mqtt == nullptr ||
            m_mqtt->state() != MqttClient::Connected) {
            task->abort("MQTT 连接不可用");
            return;
        }

        m_mqtt->publish(topic, packet, 1);
        task->notifyPacketPublished(timeoutMs);
    });
}

void Widget::onMqttOtaTaskFinished(quint64 taskId, bool success)
{
    MqttOtaTask *task = mqttOtaTask(taskId);
    if (task == nullptr)
        return;

    if (m_mqttOtaByUid.value(task->deviceId(), 0) == taskId)
        m_mqttOtaByUid.remove(task->deviceId());
    if (success)
        m_lastSuccessfulMqttOtaByUid[task->deviceId()] = taskId;

    updateMqttOtaTaskRow(taskId);
    addLog(QString("[OTA#%1][%2] %3，释放一个并发槽位")
               .arg(taskId)
               .arg(task->deviceId())
               .arg(success ? "升级成功" : "升级结束"));
    startQueuedMqttOtaTasks();
}

void Widget::clearFinishedMqttOtaTasks()
{
    for (int row = m_mqttOtaTable->rowCount() - 1; row >= 0; --row) {
        QTableWidgetItem *item = m_mqttOtaTable->item(row, 0);
        if (item == nullptr)
            continue;
        const quint64 taskId = item->data(Qt::UserRole).toULongLong();
        MqttOtaTask *task = mqttOtaTask(taskId);
        if (task == nullptr || !task->isTerminal())
            continue;

        if (m_lastSuccessfulMqttOtaByUid.value(task->deviceId(), 0) == taskId)
            m_lastSuccessfulMqttOtaByUid.remove(task->deviceId());
        m_mqttOtaTasks.remove(taskId);
        m_mqttOtaTable->removeRow(row);
        task->deleteLater();
    }

    updateMqttOtaSummary();
    updateOtaControls();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Firmware selection
// ─────────────────────────────────────────────────────────────────────────────
void Widget::onSelectFirmwareClicked()
{
    QSettings cfg("OTA_Test", "OTA");
    QString lastDir = cfg.value("lastFirmwareDir", "").toString();

    QString path = QFileDialog::getOpenFileName(
        this, "选择固件文件", lastDir, "Binary Files (*.bin);;All Files (*)");
    if (path.isEmpty()) return;
    cfg.setValue("lastFirmwareDir", QFileInfo(path).absolutePath());

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "错误", "无法打开文件:\n" + path);
        return;
    }
    m_firmware = f.readAll();
    f.close();
    m_firmwareName = QFileInfo(path).fileName();

    m_lblFirmPath->setText("▶ " + m_firmwareName);
    m_lblFirmPath->setStyleSheet("color:#00E5FF;background:transparent;border:none;"
                                 "font-family:Consolas,monospace;font-weight:bold;");
    double kb = m_firmware.size() / 1024.0;
    m_lblFirmSize->setText(
        QString("文件大小: %1 字节  （%2 KB）")
        .arg(m_firmware.size()).arg(kb, 0, 'f', 1));

    addLog(QString("固件已加载: %1  (%2 字节)")
           .arg(m_firmwareName).arg(m_firmware.size()));
    updateOtaControls();
}

// ─────────────────────────────────────────────────────────────────────────────
//  OTA – start
// ─────────────────────────────────────────────────────────────────────────────
void Widget::onStartOtaClicked()
{
    if (m_firmware.isEmpty()) {
        QMessageBox::warning(this, "错误", "请先选择固件 .bin 文件");
        return;
    }

    OtaChannel ch = (OtaChannel)m_cmbChannel->currentData().toInt();

    if (ch == OtaChannel::Serial) {
        if (m_otaState != OtaState::Idle) {
            QMessageBox::information(this, "提示", "当前串口升级任务尚未结束");
            return;
        }
        if (!m_serial->isOpen()) {
            QMessageBox::warning(this, "错误", "请先打开串口");
            return;
        }
        // 清理上一次会话的残帧；必须在发送 ENTER 前执行。
        m_serialRxBuf.clear();
        m_serial->readAll();
        m_bootRxBytes = 0;
        // 串口模式不依赖设备列表，otaUid 置空（Boot 侧不需要 topic 路由）
        m_otaUid     = m_selectedUid;   // 有就用，没有也无妨
        m_otaPkt     = 0;
        const int pktSz = m_cmbPktSize->currentData().toInt();
        m_otaPktTotal = (m_firmware.size() + pktSz - 1) / pktSz;
        m_otaState    = OtaState::WaitBootAck;
        m_otaRetry    = 0;
        m_otaProgress->setValue(0);
        updateOtaControls();
        m_lblOtaStatus->setText("状态: 通知设备跳转至 Bootloader (RS485)...");
        m_lblOtaStatus->setStyleSheet("color:#FB8C00;background:transparent;border:none;");
        addLog("=== 串口 OTA 升级开始 ===");
        addLog(QString("固件: %1 字节   共 %2 包")
               .arg(m_firmware.size()).arg(m_otaPktTotal));
        if (!serialSendPacket(Protocol::OTA_ENTER)) return;
        m_otaTimeout->start(10000);
        addLog("已发送 OTA_ENTER (RS485)");
    } else {
        if (m_mqtt->state() != MqttClient::Connected) {
            QMessageBox::warning(this, "错误", "MQTT 未连接");
            return;
        }
        createMqttOtaTasks();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  OTA – send one data packet
// ─────────────────────────────────────────────────────────────────────────────
void Widget::sendOtaData()
{
    if (m_otaPkt >= m_otaPktTotal) {
        // All data sent → OTA_FINISH with CRC32 + auto_reboot=1
        m_otaState = OtaState::WaitEndAck;

        uint32_t crc32 = Protocol::crc32mpeg2(
            (const uint8_t *)m_firmware.constData(), m_firmware.size());

        // OTA_FINISH data: [firmware_crc32 4B LE][auto_reboot 1B]
        QByteArray endData(5, 0);
        endData[0] = (char)( crc32        & 0xFF);
        endData[1] = (char)((crc32 >>  8) & 0xFF);
        endData[2] = (char)((crc32 >> 16) & 0xFF);
        endData[3] = (char)((crc32 >> 24) & 0xFF);
        endData[4] = (char)0x01;  // auto_reboot

        if (!serialSendPacket(Protocol::OTA_FINISH, endData)) return;
        m_otaTimeout->start(10000);
        m_otaProgress->setValue(90);
        m_lblOtaStatus->setText(
            QString("状态: 等待固件校验  CRC32=0x%1")
            .arg(crc32, 8, 16, QChar('0')));
        addLog(QString("已发送 OTA_FINISH   CRC32=0x%1  auto_reboot=1")
               .arg(crc32, 8, 16, QChar('0')));
        return;
    }

    const int pktSz = m_cmbPktSize->currentData().toInt();
    int offset = m_otaPkt * pktSz;
    int size   = qMin((int)(m_firmware.size() - offset), pktSz);

    // Data field: [pkt_index 2B LE][pkt_total 2B LE][firmware chunk]
    QByteArray payload(4 + size, 0);
    payload[0] = (char)( m_otaPkt        & 0xFF);
    payload[1] = (char)( m_otaPkt        >> 8);
    payload[2] = (char)( m_otaPktTotal   & 0xFF);
    payload[3] = (char)( m_otaPktTotal   >> 8);
    memcpy(payload.data() + 4, m_firmware.constData() + offset, size);

    if (!serialSendPacket(Protocol::OTA_DATA, payload)) return;
    m_otaTimeout->start(5000);

    // Progress: DATA phase maps to 10% – 90%
    int progress = 10 + (m_otaPkt * 80) / m_otaPktTotal;
    m_otaProgress->setValue(progress);
    m_lblOtaStatus->setText(
        QString("状态: 传输数据包 %1 / %2   (%3 字节)")
        .arg(m_otaPkt + 1).arg(m_otaPktTotal).arg(size));
    addLog(QString("发送 OTA_DATA  包 %1/%2   %3 字节")
           .arg(m_otaPkt + 1).arg(m_otaPktTotal).arg(size));
}

// ─────────────────────────────────────────────────────────────────────────────
//  OTA – state machine
// ─────────────────────────────────────────────────────────────────────────────
void Widget::otaNextStep(Protocol::MsgType type, const QByteArray &data)
{
    // 注：超时计时器已由 handleOtaUplink 在进入此函数前重启
    // 任意 OTA 上行包均视为设备存活；仅在状态正确推进时才 stop

    switch (m_otaState) {

    /* ── 等待 OTA_ENTER_ACK ────────────────────────────────────────────── */
    case OtaState::WaitBootAck:
        if (type != Protocol::OTA_ENTER_ACK) {
            addLog(QString("等待 ENTER_ACK，忽略非预期包 0x%1，继续等待...")
                   .arg((uint8_t)type, 2, 16, QChar('0')));
            break;
        }
        m_otaTimeout->stop();
        m_otaRetry = 0;   // 重置重传计数，避免影响后续阶段
        addLog("收到 OTA_ENTER_ACK  — 设备已跳转至 Bootloader");
        m_otaProgress->setValue(5);
        {
            m_otaState = OtaState::WaitStartAck;
            uint32_t sz = (uint32_t)m_firmware.size();
            uint16_t ps = (uint16_t)m_cmbPktSize->currentData().toInt();
            uint16_t pt = (uint16_t)m_otaPktTotal;

            // OTA_BEGIN V2.1: [firmware_size 4B LE][packet_size 2B LE][packet_total 2B LE]
            QByteArray d(8, 0);
            d[0]=(char)( sz        & 0xFF); d[1]=(char)((sz >>  8) & 0xFF);
            d[2]=(char)((sz >> 16) & 0xFF); d[3]=(char)((sz >> 24) & 0xFF);
            d[4]=(char)( ps        & 0xFF); d[5]=(char)( ps >> 8);
            d[6]=(char)( pt        & 0xFF); d[7]=(char)( pt >> 8);
            if (!serialSendPacket(Protocol::OTA_BEGIN, d)) return;
            m_otaTimeout->start(10000);
            m_lblOtaStatus->setText("状态: 等待设备确认升级...");
            addLog(QString("已发送 OTA_BEGIN   固件 %1 字节  共 %2 包").arg(sz).arg(pt));
        }
        break;

    /* ── 等待 OTA_BEGIN_ACK ─────────────────────────────────────────────── */
    case OtaState::WaitStartAck:
        if (type != Protocol::OTA_BEGIN_ACK) {
            addLog(QString("等待 BEGIN_ACK，忽略非预期包 0x%1，继续等待...")
                   .arg((uint8_t)type, 2, 16, QChar('0')));
            break;
        }
        if (!data.isEmpty() && (uint8_t)data[0] != 0x00) {
            QString reason =
                (data[0] == 0x01) ? "版本相同，设备拒绝升级" :
                (data[0] == 0x02) ? "Flash 空间不足" :
                (data[0] == 0x03) ? "当前状态不允许升级" :
                QString("设备拒绝(0x%1)").arg((uint8_t)data[0], 2, 16, QChar('0'));
            m_otaTimeout->stop();
            otaFail(reason);
            break;
        }
        m_otaTimeout->stop();
        addLog("收到 OTA_BEGIN_ACK  — 开始传输固件数据");
        m_otaProgress->setValue(10);
        m_otaState = OtaState::SendingData;
        m_otaPkt   = 0;
        sendOtaData();
        break;

    /* ── 传输数据中 ─────────────────────────────────────────────────────── */
    case OtaState::SendingData:
        if (type != Protocol::OTA_DATA_ACK) {
            addLog(QString("等待 DATA_ACK，忽略非预期包 0x%1，继续等待...")
                   .arg((uint8_t)type, 2, 16, QChar('0')));
            break;
        }
        m_otaTimeout->stop();
        if (data.size() >= 3 && (uint8_t)data[2] != 0x00) {
            addLog(QString("包 %1 校验失败 (0x%2)，重传...")
                   .arg(m_otaPkt + 1)
                   .arg((uint8_t)data[2], 2, 16, QChar('0')));
            sendOtaData();   // resend same packet
        } else {
            m_otaRetry = 0;  // 成功收到 ACK，重置重传计数
            m_otaPkt++;
            sendOtaData();
        }
        break;

    /* ── 等待 OTA_FINISH_ACK ───────────────────────────────────────────── */
    case OtaState::WaitEndAck:
        if (type != Protocol::OTA_FINISH_ACK) {
            addLog(QString("等待 FINISH_ACK，忽略非预期包 0x%1，继续等待...")
                   .arg((uint8_t)type, 2, 16, QChar('0')));
            break;
        }
        if (!data.isEmpty() && (uint8_t)data[0] != 0x00) {
            QString reason =
                (data[0] == 0x01) ? "CRC32 校验失败" :
                (data[0] == 0x02) ? "Flash 烧录失败" :
                (data[0] == 0xFF) ? "其他错误" :
                QString("未知错误(0x%1)").arg((uint8_t)data[0], 2, 16, QChar('0'));
            m_otaTimeout->stop();
            otaFail(reason);
            break;
        }
        m_otaTimeout->stop();
        addLog("收到 OTA_FINISH_ACK — 校验通过，设备自动重启跳入 App");
        m_otaProgress->setValue(100);
        otaSuccess();
        break;

    default:
        break;
    }
}

void Widget::otaFail(const QString &reason)
{
    m_otaTimeout->stop();
    m_otaState = OtaState::Idle;
    m_otaUid.clear();
    m_lblOtaStatus->setText("状态: 升级失败 — " + reason);
    m_lblOtaStatus->setStyleSheet("color:#EF5350;background:transparent;border:none;");
    updateOtaControls();
    addLog("=== OTA 失败: " + reason + " ===");
}

void Widget::otaSuccess()
{
    m_otaTimeout->stop();
    m_otaProgress->setValue(100);
    m_lblOtaStatus->setText("状态: 升级成功！等待设备重启后通过心跳确认新版本...");
    m_lblOtaStatus->setStyleSheet(
        "color:#66BB6A;font-weight:bold;background:transparent;border:none;");
    addLog("=== OTA 升级成功！设备将自动重启（auto_reboot=1），通过 HEARTBEAT 确认新版本 ===");

    // 在设备树 tooltip 上标注"等待心跳确认"，直到新心跳到来自动更新
    if (!m_otaUid.isEmpty() && m_deviceItems.contains(m_otaUid)) {
        m_deviceItems[m_otaUid]->setToolTip(
            0, QString("UID: %1\n状态: OTA 成功，等待设备重启确认...").arg(m_otaUid));
    }

    m_otaState = OtaState::Idle;
    // Keep m_otaUid for 5 s to match the first heartbeat after reboot
    QTimer::singleShot(5000, this, [this]{
        m_otaUid.clear();
        updateOtaControls();
        m_lblOtaStatus->setStyleSheet(
            "color:#616161;background:transparent;border:none;");
    });
}

void Widget::onOtaTimeout()
{
    if (m_otaState == OtaState::Idle) return;
    if (!m_serial->isOpen()) {
        otaFail("串口未打开，升级终止");
        return;
    }
    if (m_otaState == OtaState::WaitBootAck) {
        addLog(QString("BOOT 握手累计 RX %1 字节，未解析缓冲 %2 字节: %3")
               .arg(m_bootRxBytes).arg(m_serialRxBuf.size())
               .arg(QString::fromLatin1(m_serialRxBuf.left(128).toHex(' '))));
        // 10 秒仍不完整的帧已过期，避免错误长度阻塞下一次 ACK。
        m_serialRxBuf.clear();
        // Boot 跳转期间 ENTER_ACK 可能因 UART 重初始化丢失，自动重发
        m_otaRetry++;
        if (m_otaRetry <= 3) {
            addLog(QString("等待 ENTER_ACK 超时，第 %1 次重发 OTA_ENTER...").arg(m_otaRetry));
            if (!serialSendPacket(Protocol::OTA_ENTER)) return;
            m_otaTimeout->start(10000);
        } else {
            otaFail("等待 ENTER_ACK 超时，重发 3 次仍无响应");
        }
    } else if (m_otaState == OtaState::SendingData) {
        m_otaRetry++;
        if (m_otaRetry <= 3) {
            addLog(QString("数据包 %1 超时，第 %2 次重传...")
                   .arg(m_otaPkt + 1).arg(m_otaRetry));
            sendOtaData();
        } else {
            otaFail(QString("数据包 %1 超时，重传 3 次仍无响应").arg(m_otaPkt + 1));
        }
    } else {
        otaFail(QString("等待超时（状态 %1）").arg((int)m_otaState));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Log
// ─────────────────────────────────────────────────────────────────────────────
void Widget::addLog(const QString &msg)
{
    QString ts = QDateTime::currentDateTime().toString("hh:mm:ss");
    m_logEdit->append(
        "<span style='color:#89B4FA;'>[" + ts + "]</span>&nbsp;"
        "<span style='color:#CDD6F4;'>" + msg.toHtmlEscaped() + "</span>");
}
