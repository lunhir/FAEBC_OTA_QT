#include "systemdatapage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QTimer>
#include <QDateTime>
#include <QTabWidget>
#include <QtEndian>
#include <cmath>
#include <cstring>
#include <limits>

namespace {
constexpr int PayloadSize = 293;
QTableWidget *makeTable(const QStringList &headers, const char *name)
{
    auto *t = new QTableWidget(0, headers.size());
    t->setObjectName(name); t->setHorizontalHeaderLabels(headers);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setAlternatingRowColors(true); t->verticalHeader()->hide();
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    t->horizontalHeader()->setStretchLastSection(true);
    t->setStyleSheet("QTableWidget{background:#020510;color:#E3F5FA;gridline-color:#193340;}"
        "QTableWidget::item:alternate{background:#0B1825;}"
        "QTableWidget::item:selected{background:#245466;}"
        "QHeaderView::section{background:#142838;color:#D7F0F7;padding:6px;border:0;}");
    return t;
}
void addRow(QTableWidget *t, const QStringList &values)
{
    int r=t->rowCount(); t->insertRow(r);
    for (int c=0;c<values.size();++c) {
        auto *item=new QTableWidgetItem(values[c]); item->setToolTip(values[c]); t->setItem(r,c,item);
    }
}
}

SystemDataPage::SystemDataPage(std::function<void()> request, QWidget *parent) : QWidget(parent)
{
    setObjectName("systemDataPage"); setAttribute(Qt::WA_StyledBackground,true);
    setStyleSheet("QWidget#systemDataPage{background:#06101B;} QLabel{color:#D7EDF4;background:transparent;}");
    auto *layout=new QVBoxLayout(this);
    layout->setContentsMargins(6,10,6,10);
    auto *top=new QHBoxLayout;
    m_read=new QPushButton("读取系统数据"); m_read->setObjectName("systemReadButton");
    m_read->setStyleSheet("QPushButton{background:#087E8B;color:white;padding:8px 18px;border:0;border-radius:4px;}"
                         "QPushButton:disabled{background:#394A55;color:#A3AFB5;}");
    m_status=new QLabel("请选择设备，然后点击读取"); m_status->setObjectName("systemStatus"); m_status->setWordWrap(true);
    top->addWidget(m_read); top->addWidget(m_status,1); layout->addLayout(top);
    m_can=makeTable({"索引","CAN ID","D0","D1","D2","D3","D4","D5","D6","D7"},"systemCan");
    m_analog=makeTable({"通道","ADC 原始值","NTC 温度（℃）","NTC 状态"},"systemAnalog");
    m_io=makeTable({"类型","编号","信号名称","原始电平"},"systemIo");
    auto *tabs=new QTabWidget; tabs->setObjectName("systemDataTabs");
    tabs->setStyleSheet("QTabWidget::pane{border:1px solid #193340;background:#06101B;}"
        "QTabBar::tab{background:#142838;color:#B9D3DE;padding:10px 22px;margin-right:4px;}"
        "QTabBar::tab:selected{background:#087E8B;color:white;}"
        "QTabBar::tab:hover{background:#245466;}");
    auto addPage=[tabs](const QString &title,const QString &hint,QTableWidget *table) {
        auto *page=new QWidget;
        auto *box=new QVBoxLayout(page); box->setContentsMargins(6,10,6,10); box->setSpacing(12);
        auto *label=new QLabel(hint); label->setWordWrap(true);
        box->addWidget(label); box->addWidget(table,1); tabs->addTab(page,title);
    };
    addPage("CAN 数据","30 组缓存数据 · 字节以十六进制显示。无逐帧接收时间，零值不代表已收到报文。",m_can);
    addPage("ADC / NTC","4 路采样 · ADC 与温度来自同一快照；连续确认开路或短路后温度为 -273.16；旧固件可能仍上传保留值。",m_analog);
    addPage("DI / DO","12 路输入 / 8 路输出 · 显示原始高低电平，输出电平不代表机构实际到位。",m_io);
    layout->addWidget(tabs,1);
    layout->addWidget(new QLabel("点击顶部按钮读取全部数据，切换子页无需重复读取。"));
    for (auto *t : {m_can,m_analog,m_io}) {
        t->verticalHeader()->setDefaultSectionSize(34);
        t->horizontalHeader()->setStretchLastSection(false);
        t->horizontalHeader()->setMinimumSectionSize(64);
        t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    }
    m_can->horizontalHeader()->setMinimumSectionSize(32);
    m_can->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
    m_can->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
    m_timeout=new QTimer(this); m_timeout->setSingleShot(true);
    connect(m_timeout,&QTimer::timeout,this,[this]{ fail("读取超时：请检查设备连接，或更新支持系统数据查询的固件"); });
    connect(m_read,&QPushButton::clicked,this,[request]{ request(); });
}
void SystemDataPage::clearData()
{
    m_can->setRowCount(0); m_analog->setRowCount(0); m_io->setRowCount(0);
}
void SystemDataPage::fail(const QString &message)
{
    m_pending=false; m_timeout->stop(); m_read->setEnabled(true); clearData(); m_status->setText(message);
}
void SystemDataPage::setDevice(const QString &uid)
{
    if (uid==m_uid) return;
    m_uid=uid; fail(uid.isEmpty() ? "请选择设备，然后点击读取" : QString("设备：%1 · 尚未读取").arg(uid));
}
quint32 SystemDataPage::beginRequest(const QString &uid)
{
    setDevice(uid); clearData(); ++m_requestId; m_pending=true;
    m_read->setEnabled(false); m_timeout->start(15000);
    m_status->setText(QString("设备：%1 · 正在读取…").arg(uid)); return m_requestId;
}
bool SystemDataPage::showResponse(const QString &uid, const QByteArray &payload)
{
    if (!m_pending || uid!=m_uid) return false;
    // Validate the complete layout before dereferencing any payload field.
    if (payload.size()!=PayloadSize) { fail("系统数据长度错误，响应未显示"); return false; }
    const auto *p=reinterpret_cast<const uchar *>(payload.constData());
    if (qFromLittleEndian<quint32>(p+1)!=m_requestId) return false;
    if (p[0]!=1) { fail("系统数据版本不兼容，请更新上位机与固件"); return false; }
    for (int i=0;i<4;++i) {
        if (qFromLittleEndian<quint16>(p+245+2*i)>4095 || p[269+i]>4) {
            fail("ADC 或 NTC 状态值异常，响应未显示"); return false;
        }
    }
    for (int i=273;i<PayloadSize;++i) {
        if (p[i]>1) { fail("DI/DO 电平值异常，响应未显示"); return false; }
    }
    // Mapping follows BMS.h; remaining array rows intentionally have no ID.
    static const char *ids[]={"1802F3D0","1801D0F3","18E1F3D0","18E1D0F3","18E2D0F3","18E3D0F3",
        "18E4D0F3","18E5D0F3","18E6D0F3","18C2D0F3","18C1D0F3","1880D0F3","1881D0F3","1882D0F3",
        "1883D0F3","1884D0F3","1885D0F3","1886D0F3","1887D0F3","1888D0F3","18F1D0F3","18F2D0F3",
        "18F3D0F3","18FFA7EF"};
    for (int i=0;i<30;++i) {
        QStringList values{QString::number(i),i<24 ? QString::fromLatin1(ids[i]):QString("未映射")};
        for (int j=0;j<8;++j) values << QString::number(p[5+i*8+j],16).rightJustified(2,'0').toUpper();
        addRow(m_can,values);
    }
    static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559,"float32 required");
    static const char *states[]={"正常","开路","二级故障","三级故障","短路"};
    for (int i=0;i<4;++i) {
        quint32 bits=qFromLittleEndian<quint32>(p+253+4*i); float temperature;
        std::memcpy(&temperature,&bits,sizeof(temperature));
        QString value=std::isfinite(temperature) ? QString::number(temperature,'f',2):QString("无效值");
        if (p[269+i]==1 || p[269+i]==4) {
            value += std::isfinite(temperature) && std::fabs(temperature + 273.16f)<0.01f ?
                "（故障值）":"（保留值）";
        }
        addRow(m_analog,{QString("ADC%1 / NTC%2").arg(i).arg(i+1),
            QString::number(qFromLittleEndian<quint16>(p+245+2*i)),value,QString::fromUtf8(states[p[269+i]])});
    }
    static const char *diNames[]={"锁止1","锁止2","锁止3","锁止4","锁止5","锁止6","收回到位",
        "连接器","紧急信号","压力信号","唤醒 A","KeyOn 唤醒"};
    static const char *doNames[]={"锁止控制","连接器控制","紧急控制","压力控制"};
    for (int i=0;i<20;++i) {
        bool input=i<12; int n=input ? i:i-12;
        QString name=input ? QString::fromUtf8(diNames[n]):n<4 ? QString::fromUtf8(doNames[n]):QString("未命名");
        addRow(m_io,{input ? "DI 输入":"DO 输出",QString::number(n),name,p[273+i] ? "1（高）":"0（低）"});
    }
    m_pending=false; m_timeout->stop(); m_read->setEnabled(true);
    m_status->setText(QString("设备：%1 · 读取完成 %2").arg(uid).arg(QDateTime::currentDateTime().toString("HH:mm:ss")));
    return true;
}
