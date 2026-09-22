#include "lockeventspage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QTabWidget>
#include <cmath>

namespace {
QString sourceName(int n)
{
    static const QString names[] = {QStringLiteral("站控（Wi-Fi）"), QStringLiteral("云平台"), QStringLiteral("控制台")};
    return names[n];
}
bool integer(const QJsonValue &v, double maximum = 4294967295.0)
{
    const double n = v.toDouble(-1);
    return v.isDouble() && std::isfinite(n) && n >= 0 && n <= maximum && std::floor(n) == n;
}
QString number(const QJsonValue &v) { return QString::number(static_cast<quint64>(v.toDouble())); }
QString reason(int n)
{
    static const QString names[] = {QStringLiteral("—"), QStringLiteral("车速不为零"), QStringLiteral("手刹未拉起"),
                                   QStringLiteral("高压已接通"), QStringLiteral("挡位不是 P 挡")};
    return names[n];
}
QTableWidget *table(const QStringList &headers)
{
    auto *t = new QTableWidget(0, headers.size());
    t->setHorizontalHeaderLabels(headers);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setAlternatingRowColors(true);
    t->verticalHeader()->hide();
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    t->horizontalHeader()->setStretchLastSection(true);
    t->setStyleSheet("QTableWidget{background:#020510;color:#E3F5FA;gridline-color:#193340;}"
                     "QTableWidget::item:alternate{background:#0B1825;}"
                     "QTableWidget::item:selected{background:#245466;}"
                     "QHeaderView{background:#142838;}"
                     "QHeaderView::section{background:#142838;color:#D7F0F7;padding:4px 2px;border:0;}");
    return t;
}
void row(QTableWidget *t, const QStringList &values)
{
    int r = t->rowCount(); t->insertRow(r);
    for (int c = 0; c < values.size(); ++c) {
        auto *item = new QTableWidgetItem(values[c]);
        item->setToolTip(values[c]);
        t->setItem(r, c, item);
    }
}
}

LockEventsPage::LockEventsPage(std::function<void()> request, QWidget *parent) : QWidget(parent)
{
    setObjectName("lockEventsPage");
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet("QWidget#lockEventsPage{background:#06101B;} QLabel{color:#D7EDF4;background:transparent;}");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6,10,6,10); layout->setSpacing(8);
    auto *top = new QHBoxLayout;
    m_read = new QPushButton(QStringLiteral("读取锁止事件"));
    m_read->setObjectName("lockReadButton");
    m_read->setStyleSheet("QPushButton{background:#087E8B;color:white;padding:8px 18px;border:0;border-radius:4px;}"
                         "QPushButton:disabled{background:#394A55;color:#A3AFB5;}");
    m_status = new QLabel(QStringLiteral("请选择设备，然后点击读取"));
    m_status->setObjectName("lockStatus"); m_status->setWordWrap(true);
    top->addWidget(m_read); top->addWidget(m_status,1); layout->addLayout(top);
    m_state = new QLabel(QStringLiteral("当前到位状态：—"));
    m_state->setObjectName("lockState"); m_state->setStyleSheet("color:#64E0C5;font-size:18px;font-weight:600;");
    m_details = new QLabel(QStringLiteral("动作状态：—"));
    m_details->setObjectName("lockDetails"); m_details->setWordWrap(true);
    m_state->setWordWrap(true);
    auto *tabs = new QTabWidget; tabs->setObjectName("lockEventsTabs");
    tabs->setStyleSheet("QTabWidget::pane{border:1px solid #193340;background:#06101B;}"
        "QTabBar::tab{background:#142838;color:#B9D3DE;padding:10px 22px;margin-right:4px;}"
        "QTabBar::tab:selected{background:#087E8B;color:white;}"
        "QTabBar::tab:hover{background:#245466;}");
    auto *statsBox = new QWidget;
    auto *statsLayout = new QVBoxLayout(statsBox); statsLayout->setContentsMargins(6,10,6,10); statsLayout->setSpacing(12);
    statsLayout->addWidget(m_state); statsLayout->addWidget(m_details);
    statsLayout->addWidget(new QLabel(QStringLiteral("累计操作次数（按来源、动作分别统计）")));
    m_counts = table({"来源","动作","总请求","成功","条件\n拒绝","忙","超时","任务\n不可用","同时到位\n故障","强制\n请求","结果\n未确认"});
    m_counts->setObjectName("lockCounters");
    m_counts->horizontalHeader()->setStretchLastSection(false);
    m_counts->horizontalHeader()->setMinimumSectionSize(42);
    m_counts->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_counts->verticalHeader()->setMinimumSectionSize(36);
    m_counts->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    connect(m_counts->horizontalHeader(), &QHeaderView::sectionResized, m_counts, &QTableWidget::resizeRowsToContents);
    statsLayout->addWidget(m_counts);
    auto *eventsBox = new QWidget;
    auto *eventsLayout = new QVBoxLayout(eventsBox); eventsLayout->setContentsMargins(6,10,6,10); eventsLayout->setSpacing(12);
    eventsLayout->addWidget(new QLabel(QStringLiteral("最近 20 条操作记录（最新请求在前）")));
    m_events = table({"操作时间","来源","动作","方式","结果","原因 / 说明","耗时\n（ms）","序号"});
    m_events->setObjectName("lockEvents");
    m_events->horizontalHeader()->setStretchLastSection(false);
    m_events->horizontalHeader()->setMinimumSectionSize(38);
    m_events->horizontalHeader()->setSectionResizeMode(4,QHeaderView::Stretch);
    m_events->horizontalHeader()->setSectionResizeMode(5,QHeaderView::Stretch);
    m_events->verticalHeader()->setMinimumSectionSize(36);
    m_events->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    connect(m_events->horizontalHeader(), &QHeaderView::sectionResized, m_events, &QTableWidget::resizeRowsToContents);
    eventsLayout->addWidget(m_events);
    tabs->addTab(statsBox,"状态与统计"); tabs->addTab(eventsBox,"操作日志（最近20条）");
    layout->addWidget(tabs,1);
    auto *hint = new QLabel(QStringLiteral("总请求包含忙、拒绝及未确认结果；强制解锁计入解锁总数。页面仅查询，不控制锁。"));
    hint->setWordWrap(true); layout->addWidget(hint);
    m_timeout = new QTimer(this); m_timeout->setSingleShot(true);
    connect(m_timeout,&QTimer::timeout,this,[this]{ fail(QStringLiteral("读取超时：请检查设备连接及固件是否支持锁止事件查询")); });
    connect(m_read,&QPushButton::clicked,this,[request]{ request(); });
}
void LockEventsPage::clearData()
{
    m_counts->setRowCount(0); m_events->setRowCount(0);
    m_state->setText(QStringLiteral("当前到位状态：—")); m_details->setText(QStringLiteral("动作状态：—"));
}
void LockEventsPage::setDevice(const QString &uid)
{
    if (uid == m_uid) return;
    m_uid = uid; m_pending = false; m_timeout->stop(); m_read->setEnabled(true); clearData();
    m_status->setText(uid.isEmpty() ? QStringLiteral("请选择设备，然后点击读取") : QStringLiteral("设备：%1 · 尚未读取").arg(uid));
}
quint32 LockEventsPage::beginRequest(const QString &uid)
{
    setDevice(uid); clearData(); ++m_requestId;
    m_pending = true; m_read->setEnabled(false); m_timeout->start(15000);
    m_status->setText(QStringLiteral("设备：%1 · 正在读取…").arg(uid));
    return m_requestId;
}
void LockEventsPage::fail(const QString &message)
{
    m_pending = false; m_timeout->stop(); m_read->setEnabled(true); clearData();
    m_status->setText(message);
}
bool LockEventsPage::showResponse(const QString &uid, const QByteArray &payload)
{
    if (!m_pending || uid != m_uid) return false;
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(payload,&error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) { fail(QStringLiteral("锁止事件响应格式错误")); return false; }
    const auto o = doc.object();
    if (!integer(o["rid"]) || o["rid"].toDouble() != m_requestId) return false;
    const auto counts = o["counts"].toArray(), events = o["events"].toArray(), forced = o["forced"].toArray();
    const int version=o["v"].toInt();
    const int counterFields=version==2 ? 7 : 6;
    bool valid = integer(o["v"],2) && version>=1 && integer(o["inputs"],31) &&
        integer(o["output"],1) && integer(o["busy"],1) && counts.size()==6 && forced.size()==3 &&
        o["events"].isArray() && events.size()<=(version==2 ? 20 : 10);
    for (const auto &v : forced) valid = valid && integer(v);
    for (const auto &v : counts) {
        auto a=v.toArray(); valid = valid && a.size()==counterFields;
        for (const auto &x : a) valid = valid && integer(x);
    }
    for (const auto &v : events) {
        auto a=v.toArray();
        if (a.size()!=9) { valid=false; break; }
        valid = valid && integer(a[0]) && a[1].isString() && integer(a[2],2) && integer(a[3],1) &&
            integer(a[4],1) && integer(a[5],version==2 ? 6 : 5) && integer(a[6],4) && integer(a[7],31) && integer(a[8]);
    }
    if (!valid) { fail(QStringLiteral("锁止事件字段缺失或版本不兼容，请更新固件和上位机")); return false; }
    m_timeout->stop(); m_pending=false; m_read->setEnabled(true);
    const int inputs=o["inputs"].toInt();
    const bool locked=(inputs & 15)==0, unlocked=(inputs & 16)==0;
    QString state;
    if ((inputs & 15)!=15 && unlocked) state=QStringLiteral("锁止伸出和收回同时到位故障");
    else if (locked) state=QStringLiteral("已上锁");
    else if (unlocked) state=QStringLiteral("已解锁");
    else if ((inputs & 15)!=15) state=QStringLiteral("部分锁止到位");
    else state=QStringLiteral("未到位");
    m_state->setText(QStringLiteral("当前到位状态：%1").arg(state));
    QStringList pins;
    for (int i=0;i<4;++i) pins << QStringLiteral("锁%1：%2").arg(i+1).arg((inputs&(1<<i)) ? "未到位":"到位");
    m_details->setText(QStringLiteral("动作：%1　输出：%2\n%3　解锁反馈：%4")
        .arg(o["busy"].toInt() ? "执行中":"空闲")
        .arg(o["output"].toInt() ? "解锁":"上锁")
        .arg(pins.join("　")).arg(unlocked ? "到位":"未到位"));
    for (int i=0;i<6;++i) {
        const auto a=counts[i].toArray(); quint64 finalized=0;
        for (int j=1;j<counterFields;++j) finalized+=static_cast<quint64>(a[j].toDouble());
        quint64 total=static_cast<quint64>(a[0].toDouble());
        row(m_counts,{sourceName(i/2),i%2 ? "解锁":"上锁",number(a[0]),number(a[1]),number(a[2]),number(a[3]),number(a[4]),number(a[5]),
            version==2 ? number(a[6]):"0",i%2 ? number(forced[i/2]):"0",QString::number(total>finalized ? total-finalized:0)});
    }
    static const QString results[]={"成功","条件拒绝","忙","超时","任务不可用","结果未确认","同时到位故障"};
    for (const auto &v : events) {
        const auto a=v.toArray(); const int status=a[5].toInt();
        QString detail=reason(a[6].toInt());
        if (status==2) detail="已有动作或结果等待处理";
        else if (status==3) detail="5 秒内未确认到位";
        else if (status==4) detail="锁任务未就绪";
        else if (status==5) detail="动作尚未完成，或断电前未保存结果";
        else if (status==6) detail="锁止伸出和收回同时到位故障";
        const QString time=a[1].toString();
        row(m_events,{QDateTime::fromString(time,"yyyy-MM-dd HH:mm:ss").isValid() ? time:"时间未校准",
            sourceName(a[2].toInt()),a[3].toInt() ? "解锁":"上锁",a[4].toInt() ? "强制":"普通",results[status],detail,number(a[8]),number(a[0])});
    }
    m_status->setText(QStringLiteral("设备：%1 · 更新于 %2 · 最近 %3 条%4").arg(uid)
        .arg(QDateTime::currentDateTime().toString("HH:mm:ss")).arg(events.size())
        .arg(events.isEmpty() ? "（暂无操作记录）":""));
    return true;
}
