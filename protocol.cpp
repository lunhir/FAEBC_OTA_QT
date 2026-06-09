#include "protocol.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <cstring>

namespace Protocol {

/* ── DIB 组装：字段截断至定长并以 0 补齐 ─────────────────────────── */
static void copyFixedCString(char *dst, int dstSize, const QString &src)
{
    QByteArray utf8 = src.toUtf8();
    std::memset(dst, 0, dstSize);
    int n = qMin(utf8.size(), dstSize - 1);   // 保留 1 字节做 '\0'
    std::memcpy(dst, utf8.constData(), n);
}

QByteArray buildDibPayload(const QString &username,
                           const QString &password,
                           const QString &clientId,
                           const QString &hostname,
                           uint16_t port,
                           uint16_t wifi_device_addr)
{
    static_assert(sizeof(DibLayout) == DIB_SIZE,
                  "DibLayout size mismatch, must match firmware dib_t");

    DibLayout d;
    std::memset(&d, 0, sizeof(d));

    d.magic            = DIB_MAGIC;      // MCU 也会强制覆盖
    d.version          = DIB_VERSION;
    copyFixedCString(d.username, sizeof(d.username), username);
    copyFixedCString(d.password, sizeof(d.password), password);
    copyFixedCString(d.clientId, sizeof(d.clientId), clientId);
    copyFixedCString(d.hostname, sizeof(d.hostname), hostname);
    d.port             = port;
    d.wifi_device_addr = wifi_device_addr;
    d.crc32            = 0;              // MCU 端会重算

    return QByteArray(reinterpret_cast<const char *>(&d), DIB_SIZE);
}

uint16_t crc16(const uint8_t *data, int len)
{
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; ++j)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x8005 : crc << 1;
    }
    return crc;
}

uint32_t crc32mpeg2(const uint8_t *data, int len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (int i = 0; i < len; ++i) {
        crc ^= (uint32_t)data[i] << 24;
        for (int j = 0; j < 8; ++j)
            crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04C11DB7u : crc << 1;
    }
    return crc;
}

// V2.0 frame: [0xFA][Type 1B][Len 2B LE][Data NB][CRC16 2B LE]  total = N+6
QByteArray buildPacket(MsgType type, const QByteArray &data)
{
    QByteArray pkt;
    pkt.reserve(6 + data.size());

    pkt.append((char)HEADER);
    pkt.append((char)type);

    uint16_t len = (uint16_t)data.size();
    pkt.append((char)(len & 0xFF));
    pkt.append((char)(len >> 8));
    pkt.append(data);

    uint16_t crc = crc16((const uint8_t *)pkt.constData(), pkt.size());
    pkt.append((char)(crc & 0xFF));
    pkt.append((char)(crc >> 8));

    return pkt;
}

bool parsePacket(const QByteArray &raw, MsgType &type, QByteArray &data)
{
    if (raw.size() < 6) return false;
    if ((uint8_t)raw[0] != HEADER) return false;

    type = (MsgType)(uint8_t)raw[1];

    uint16_t len = (uint8_t)raw[2] | ((uint8_t)raw[3] << 8);
    if (raw.size() < 4 + (int)len + 2) return false;

    data = raw.mid(4, len);

    uint16_t rxCrc   = (uint8_t)raw[4 + len] | ((uint8_t)raw[4 + len + 1] << 8);
    uint16_t calcCrc = crc16((const uint8_t *)raw.constData(), 4 + len);

    return rxCrc == calcCrc;
}

bool parseHeartbeat(const QByteArray &json, HeartbeatData &hb)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    QJsonObject obj = doc.object();

    hb.version       =           obj.value("version").toString();
    hb.batch         =           obj.value("batch")  .toString();
    hb.vin           =           obj.value("vin")    .toString();
    hb.comm_state    = (uint8_t) obj.value("comm")   .toInt(0);
    hb.vehicle_state = (uint8_t) obj.value("vehicle").toInt(0);
    hb.hv_state      = (uint8_t) obj.value("hv")     .toInt(0);
    hb.brake_state   = (uint8_t) obj.value("brake")  .toInt(0);
    hb.speed         = (uint8_t) obj.value("speed")  .toInt(0);
    hb.total_mileage = (uint32_t)obj.value("mileage").toInt(0);
    hb.charge_state  = (uint8_t) obj.value("charge") .toInt(0);
    hb.lng           = (int32_t) obj.value("lng")    .toInt(0);
    hb.lat           = (int32_t) obj.value("lat")    .toInt(0);
    hb.ts            = (int64_t) obj.value("ts")     .toDouble(0);

    return true;
}

// Best-effort: given a JSON byte stream and a parser error offset, find the
// most recent "key": that appeared before the offset. This is the field whose
// value (or whose own quoting) most likely broke parsing.
static QString findKeyNearOffset(const QByteArray &json, int offset)
{
    if (offset <= 0 || offset > json.size()) offset = json.size();
    // Walk backwards from offset looking for the last `"..."<ws>:` pattern.
    for (int i = offset - 1; i >= 1; --i) {
        if (json[i] != ':') continue;
        int j = i - 1;
        while (j >= 0 && (json[j] == ' ' || json[j] == '\t')) --j;
        if (j < 1 || json[j] != '"') continue;
        int closeQuote = j;
        int openQuote = -1;
        for (int k = j - 1; k >= 0; --k) {
            if (json[k] == '"' && (k == 0 || json[k-1] != '\\')) {
                openQuote = k;
                break;
            }
        }
        if (openQuote < 0) continue;
        return QString::fromUtf8(json.mid(openQuote + 1,
                                          closeQuote - openQuote - 1));
    }
    return QString();
}

QList<QPair<QString,QString>> parseDebugData(const QByteArray &json,
                                             QString *errMsg)
{
    QList<QPair<QString,QString>> rows;
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errMsg) {
            if (err.error != QJsonParseError::NoError) {
                QString badKey = findKeyNearOffset(json, err.offset);
                *errMsg = QString("JSON 解析失败: %1 (offset=%2)")
                          .arg(err.errorString()).arg(err.offset);
                if (!badKey.isEmpty())
                    *errMsg += QString("，疑似出错字段: \"%1\"").arg(badKey);
            } else {
                *errMsg = "JSON 顶层不是对象";
            }
        }
        return rows;
    }

    QJsonObject obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        QString val;
        const QJsonValue &v = it.value();
        if      (v.isString()) val = v.toString();
        else if (v.isDouble()) val = QString::number(v.toDouble(), 'g', 10);
        else if (v.isBool())   val = v.toBool() ? "true" : "false";
        else                   val = QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact);
        rows.append({it.key(), val});
    }
    return rows;
}

} // namespace Protocol
