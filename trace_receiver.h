#ifndef TRACE_RECEIVER_H
#define TRACE_RECEIVER_H
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QDir>
#include <QFile>
#include <QMap>
#include <QRegularExpression>
#include <QDateTime>
#include "protocol.h"

// Header-only to keep the existing host project/build system unchanged.
class TraceReceiver {
    struct Assembly { quint32 id=0,crc=0,build=0; int total=0,size=0; QMap<int,QByteArray> parts; };
    QMap<QString,Assembly> pending;
    QMap<QString,QString> displayed;
    static quint32 word(const QByteArray &b,int offset) {
        const auto *p=reinterpret_cast<const unsigned char *>(b.constData()+offset);
        return quint32(p[0])|(quint32(p[1])<<8)|(quint32(p[2])<<16)|(quint32(p[3])<<24);
    }
    static quint32 crc(const QByteArray &b) {
        return Protocol::crc32mpeg2(reinterpret_cast<const uint8_t *>(b.constData()),b.size());
    }
    static QString hex(quint32 x) { return QString("0x%1").arg(x,8,16,QChar('0')); }
    static QString sleepName(quint32 stage) {
        switch(stage) {
        case 0:return "休眠倒计时"; case 1:return "关闭输出"; case 2:return "等待蜂窝发送完成";
        case 3:return "关闭蜂窝模块"; case 4:return "准备进入 STOP"; case 10:return "暂停系统节拍";
        case 11:return "配置 RTC 唤醒"; case 12:return "进入 STOP/WFI";
        case 13:return "STOP 已返回，恢复时钟"; case 14:return "检查唤醒来源";
        case 15:return "RTC 唤醒已喂狗"; case 16:return "外部唤醒退出";
        case 17:return "释放蜂窝信号量"; case 90:return "调用软件复位";
        default:return "未知阶段";
        }
    }
public:
    struct Result { bool saved=false,fresh=false; QByteArray ack; QString summary,error,path; };
    Result accept(const QString &uid,const QJsonObject &o,const QString &directory) {
        Result r;
        static const QRegularExpression validUid("^[a-fA-F0-9]{24}$"), validHex("^[a-fA-F0-9]+$");
        if (!validUid.match(uid).hasMatch()) return r;
        const int size=o.value("size").toInt(), total=o.value("total").toInt(), part=o.value("part").toInt(-1);
        const QString data=o.value("data").toString();
        if ((size!=2092 && size!=2028 && size!=1964 && size!=1952) || total!=(size+127)/128 || part<0 || part>=total ||
            data.size()!=2*qMin(128,size-part*128) || !validHex.match(data).hasMatch()) return r;
        const quint32 id=o.value("record_id").toVariant().toUInt(), c=o.value("record_crc").toVariant().toUInt();
        const quint32 build=o.value("build_id").toVariant().toUInt();
        if (!pending.contains(uid) && pending.size()>=64) pending.erase(pending.begin());
        auto &a=pending[uid];
        if (a.id!=id || a.crc!=c || a.build!=build) { a=Assembly{}; a.id=id;a.crc=c;a.build=build;a.total=total;a.size=size; }
        a.parts[part]=QByteArray::fromHex(data.toLatin1());
        if (a.parts.size()!=total) return r;
        QByteArray raw; for(int i=0;i<total;i++) raw+=a.parts.value(i);
        const bool v6=size==2092, v5=size==2028;
        const int recordCrc=v6?1880:(v5?1848:1784), f=recordCrc+4,
                  faultSize=v6?196:(v5?164:164), sleepOffset=(v6||v5)?1416:1736;
        if (raw.size()!=size || crc(raw)!=c || word(raw,0)!=((v6||v5)?0x54524335U:0x54524334U) ||
            word(raw,4)!=(v6?6U:(v5?5U:4U)) ||
            word(raw,8)!=id || word(raw,24)!=build || crc(raw.left(recordCrc))!=word(raw,recordCrc) ||
            (word(raw,f)!=0U && crc(raw.mid(f,faultSize-4))!=word(raw,f+faultSize-4))) {
            pending.remove(uid);r.error="诊断记录校验失败，等待重传";return r;
        }
        const quint32 trigger=word(raw,12);
        const QStringList triggers={"未知","复位前现场","CAN 停发","喂狗延迟","CPU 异常","系统断言","Error_Handler","串口越界","中断优先级不允许调用RTOS","字符串缺少结束符/空指针","任务栈边界损坏","内存分配失败","报文长度越界"};
        QStringList lines;
        lines << QString("【主动追踪已保存】设备 %1；记录 %2；固件 %3").arg(uid).arg(id).arg(hex(build));
        lines << QString("首次触发：%1；运行时间 %2 ms；复位标志 %3")
            .arg(triggers.value(int(trigger),"未知")).arg(word(raw,20)).arg(hex(word(raw,16)));
        QJsonObject meta=o;meta.remove("data");meta.remove("part");
        meta["saved_at"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        meta["trigger"]=int(trigger);meta["reset_flags"]=hex(word(raw,16));
        QJsonArray lanes;
        const QStringList names={"系统任务","CAN1发送","CAN2发送","BMS接收","生命信号定时器","Flash","蜂窝发送锁","站端状态机"};
        for(int i=0;i<8;i++) {
            const int p=56+i*80;
            QJsonObject lane;lane["name"]=names[i];lane["stage"]=int(word(raw,p));lane["arg"]=hex(word(raw,p+4));
            lane["pc"]=hex(word(raw,p+8));lane["tick"]=double(word(raw,p+12));lane["task"]=hex(word(raw,p+16));
            lanes.append(lane);
            lines << QString("%1：阶段 %2，参数 %3，位置 %4，更新于 %5 ms")
                .arg(names[i]).arg(word(raw,p)).arg(hex(word(raw,p+4)),hex(word(raw,p+8))).arg(word(raw,p+12));
        }
        meta["lanes"]=lanes;
        const quint32 sleep=word(raw,sleepOffset),inputs=word(raw,sleepOffset+4);
        lines << QString("休眠：%1（%2）；计数 %3 × 100ms；WAKE_A=%4，KeyOn=%5，WiFi=%6；STOP %7 次，RTC喂狗 %8 次")
            .arg(sleepName(sleep)).arg(sleep).arg(inputs&65535U).arg((inputs>>16)&1U)
            .arg((inputs>>17)&1U).arg((inputs>>18)&1U).arg(word(raw,sleepOffset+32)).arg(word(raw,sleepOffset+36));
        if (v5 || v6) {
            const int d=1464;
            const int ports[]={2,3,5,6};
            for(int i=0;i<4;i++) {
                int p=d+i*80;quint32 priorities=word(raw,p+60);
                lines << QString("UART%1：阶段 %2，接收 %3/%4，错误 %5，DMA启动结果 %6；UART/DMA中断优先级 %7/%8，DMA仲裁优先级 %9；NDTR %10，越界 %11")
                    .arg(ports[i]).arg(word(raw,p)).arg(word(raw,p+4)).arg(word(raw,p+8))
                    .arg(hex(word(raw,p+20))).arg(word(raw,p+24)).arg((priorities&255U)>>4).arg(((priorities>>8)&255U)>>4)
                    .arg((word(raw,p+40)>>16)&3U).arg(word(raw,p+44)).arg(word(raw,p+72));
            }
            if (v6) {
                const int e=d+96;
                const quint32 event=word(raw,e);
                lines << QString("UART6追踪：事件 %1，标志 %2，DMA CR %3，NDTR %4，状态 %5，值 %6，时间 %7 ms，IPSR/优先级 %8")
                    .arg(event&255U).arg(hex(word(raw,e+4))).arg(hex(word(raw,e+8)))
                    .arg(word(raw,e+12)).arg(hex(word(raw,e+16))).arg(hex(word(raw,e+20)))
                    .arg(word(raw,e+24)).arg(hex(word(raw,e+28)));
            }
            lines << QString("首次违规 %1，参数 %2 / %3，位置 %4，中断号 %5，原始优先级 %6")
                .arg(word(raw,d+320)).arg(hex(word(raw,d+324)),hex(word(raw,d+328)),hex(word(raw,d+332)))
                .arg(word(raw,d+336)).arg(word(raw,d+340));
            lines << QString("堆剩余 %1，历史最少 %2，中断队列发送失败 %3")
                .arg(word(raw,d+352)).arg(word(raw,d+356)).arg(word(raw,d+368));
        }
        if(word(raw,f)==7U) {
            lines << QString("系统断言：文件地址 %1，行 %2，调用位置 %3，IPSR %4，原始中断优先级 %5")
                .arg(hex(word(raw,f+68))).arg(word(raw,f+64)).arg(hex(word(raw,f+88)))
                .arg(word(raw,f+72)).arg(word(raw,f+76));
        } else if(word(raw,f)) {
            lines << QString("终止异常 %1；异常帧有效=%2；PC %3，LR %4，CFSR %5，HFSR %6")
                .arg(word(raw,f)).arg(word(raw,f+60)).arg(hex(word(raw,f+88)),hex(word(raw,f+84)),hex(word(raw,f+32)),hex(word(raw,f+36)));
        }
        if (size>=f+faultSize+12 && word(raw,f+faultSize+8)==(((v6||v5)?0x54524335U:0x54524334U)^word(raw,f+faultSize)^word(raw,f+faultSize+4)))
            lines << QString("捕获后的首次复位：标志 %1，启动计数 %2").arg(hex(word(raw,f+164))).arg(word(raw,f+168));
        lines << "用相同固件标识的 ELF 和 decode_trace.py 解析源码行；任务栈采样不等于完整调用栈。";
        if (!QDir().mkpath(directory)) { r.error="不能创建诊断保存目录，未确认，设备将重传";return r; }
        const QString stem=uid+QString("_%1_%2_%3").arg(hex(build)).arg(id).arg(hex(c));
        r.path=QDir(directory).filePath(stem+".trace.bin");
        lines << "文件："+r.path;
        r.summary=lines.join('\n');meta["summary"]=r.summary;
        auto save=[](const QString &path,const QByteArray &bytes) {
            QSaveFile f(path);return f.open(QIODevice::WriteOnly) && f.write(bytes)==bytes.size() && f.commit();
        };
        // Duplicate rounds still ACK, but never ACK a failed durable write.
        QFile old(r.path);const bool same=old.open(QIODevice::ReadOnly)&&old.readAll()==raw;old.close();
        if ((!same && !save(r.path,raw)) || !save(QDir(directory).filePath(stem+".json"),QJsonDocument(meta).toJson())) {
            r.error="诊断文件保存失败，未确认，设备将重传";return r;
        }
        r.saved=true;r.fresh=displayed.value(uid)!=stem;
        if(displayed.size()>=256 && !displayed.contains(uid)) displayed.erase(displayed.begin());
        displayed[uid]=stem;
        for(int i=0;i<4;i++) r.ack.append(char(id>>(8*i)));
        for(int i=0;i<4;i++) r.ack.append(char(c>>(8*i)));
        pending.remove(uid);return r;
    }
};
#endif
