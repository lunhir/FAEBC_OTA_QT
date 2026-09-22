#pragma once
#include <QByteArray>
#include <QString>
#include <QList>
#include <QPair>
#include <cstdint>

namespace Protocol {

constexpr uint8_t  HEADER      = 0xFA;
constexpr int      PKT_DATA_SZ = 2048;

// MQTT broker
constexpr const char *BROKER_HOST = "111.231.140.54";
constexpr quint16     BROKER_PORT = 1883;

// Subscription wildcards (server subscribes these)
constexpr const char *TOPIC_UP_SUB     = "tbox/+/up";
constexpr const char *TOPIC_STATUS_SUB = "tbox/+/status";

// Per-device topic format strings — use QString(TOPIC_DOWN_FMT).arg(device_id)
constexpr const char *TOPIC_DOWN_FMT   = "tbox/%1/down";

enum MsgType : uint8_t {
    OTA_ENTER      = 0x01,
    OTA_ENTER_ACK  = 0x02,
    OTA_BEGIN      = 0x03,
    OTA_BEGIN_ACK  = 0x04,
    OTA_DATA       = 0x05,
    OTA_DATA_ACK   = 0x06,
    OTA_FINISH     = 0x07,
    OTA_FINISH_ACK = 0x08,
    HEARTBEAT      = 0xCC,

    SYSTEM_DATA_REQ = 0x17, // Qt -> MCU: uint32 LE request ID
    SYSTEM_DATA_RSP = 0x18, // MCU -> Qt: binary snapshot v1, 293 bytes
    LOCK_EVENTS_REQ = 0x15, // Qt -> MCU: uint32 LE request ID
    LOCK_EVENTS_RSP = 0x16, // MCU -> Qt: lock events JSON v1
    DEBUG_READ_REQ = 0x10,   // Qt  → MCU : 请求调试数据
    DEBUG_READ_RSP = 0x11,   // MCU → Qt  : JSON 键值对
    CHARGE_MONITOR_REQ = 0x12,    // Qt  → MCU : 1B 系统事件监测命令（名称保留兼容）
    CHARGE_MONITOR_ACK = 0x13,    // MCU → Qt  : 启动/停止确认 JSON
    CHARGE_MONITOR_REPORT = 0x14, // MCU → Qt  : 持续系统监测期间的重要事件 JSON

    DIB_WRITE_REQ  = 0x20,   // Qt  → MCU : 写入 DIB（payload = dib_t 原始字节，定长 472B）
    DIB_WRITE_RSP  = 0x21,   // MCU → Qt  : 写入结果（1B：0x00=OK 0x01=长度错 0x02=写入失败）

    RESET_REQ      = 0x30,   // Qt  → MCU : 请求软件复位（无回复）

    CHARGE_LIMIT_REQ = 0x31, // Qt → MCU : 充电限制下发（无回复）
                             //   payload 1B：0=允许充电  1=限制充电
                             //   MCU 收到后调用 EVCC_limit_charge(state) 发 CAN2

    FLASH_ERASE_REQ  = 0x32, // Qt → MCU : 擦除 UOTTA flashData 区（无 payload，无回复）
                             //   MCU 清零 flashData 并写回，触发扇区 3 擦除
};

enum class ChargeMonitorCommand : uint8_t {
    Disable = 0,
    Arm = 1,
};

enum class ChargeMonitorState : uint8_t {
    Disabled = 0,
    Armed = 1,
    Charging = 2,
};

/* ── DIB 结构 —— 与 FAEBC_APP/User/Inc/u_dib.h 中 dib_t 严格对应 ────
 * 字段顺序、长度、对齐必须完全一致。任何变更须同步修改两端。 */
#pragma pack(push, 1)
struct DibLayout {
    uint32_t magic;            // = DIB_MAGIC (Qt 可填 0，MCU 会强制覆盖)
    uint32_t version;          // = 1        (同上)
    char     username[80];
    char     password[96];
    char     clientId[96];
    char     hostname[128];
    uint16_t port;
    uint16_t wifi_device_addr;
    uint8_t  reserved[56];
    uint32_t crc32;            // Qt 可填 0，MCU 会重算
};
#pragma pack(pop)

constexpr uint32_t DIB_MAGIC   = 0x44494230;   // 'DIB0'
constexpr uint32_t DIB_VERSION = 1;
constexpr int      DIB_SIZE    = 472;           // sizeof(DibLayout)

// 组装 DIB 字节流（字段会被截断至定长并补 0）；返回 DIB_SIZE 字节的 QByteArray
QByteArray buildDibPayload(const QString &username,
                           const QString &password,
                           const QString &clientId,
                           const QString &hostname,
                           uint16_t port,
                           uint16_t wifi_device_addr);

// Heartbeat fields decoded from JSON
struct HeartbeatData {
    QString  batch;          // batch string, e.g. "2026-4"
    QString  vin;            // vehicle VIN code (up to 17 chars)
    QString  version;        // firmware version string, e.g. "1.0"
    uint8_t  comm_state;     // 0=disconnected  1=connected
    uint8_t  vehicle_state;  // 0=off  1=power  2=running
    uint8_t  hv_state;       // 0=open  1=closed
    uint8_t  brake_state;    // 0=released  1=engaged
    uint8_t  speed;          // km/h
    uint32_t total_mileage;  // km
    uint8_t  charge_state;   // 1=charging  2=discharging  3=other
    int32_t  lng;            // *1e-6 degrees
    int32_t  lat;            // *1e-6 degrees
    int64_t  ts;             // Unix timestamp (seconds)
};

// CRC16: poly=0x8005 init=0xFFFF no reflect
uint16_t crc16(const uint8_t *data, int len);

// CRC32/MPEG-2: poly=0x04C11DB7 init=0xFFFFFFFF no reflect no xorout
uint32_t crc32mpeg2(const uint8_t *data, int len);

// Build V2.0 packet: [0xFA][Type 1B][Len 2B LE][Data NB][CRC16 2B LE]
QByteArray buildPacket(MsgType type, const QByteArray &data = {});

// Parse V2.0 packet; returns false if header wrong, too short, or CRC fails
bool parsePacket(const QByteArray &raw, MsgType &type, QByteArray &data);

// Parse heartbeat JSON payload into HeartbeatData
bool parseHeartbeat(const QByteArray &json, HeartbeatData &hb);

// Parse debug response JSON payload (MCU's DEBUG_READ_RSP data field)
// Returns list of (key, value) pairs in the order the JSON object was built.
// On failure, returns empty list and (if errMsg != nullptr) writes a human-readable
// reason — including which key broke parsing when it can be localized.
QList<QPair<QString,QString>> parseDebugData(const QByteArray &json,
                                             QString *errMsg = nullptr);

} // namespace Protocol
