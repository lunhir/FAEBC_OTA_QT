#include "mqttotatask.h"

#include <QtGlobal>
#include <cstring>

namespace {

constexpr int kMaxRetries = 3;
constexpr int kControlTimeoutMs = 10000;
constexpr int kDataTimeoutMs = 5000;

} // namespace

MqttOtaTask::MqttOtaTask(quint64 id,
                         const QString &deviceId,
                         const QString &firmwareName,
                         const QByteArray &firmware,
                         int packetSize,
                         QObject *parent)
    : QObject(parent)
    , m_id(id)
    , m_deviceId(deviceId)
    , m_firmwareName(firmwareName)
    , m_firmware(firmware)
    , m_packetSize(packetSize)
{
    m_packetTotal = (m_firmware.size() + m_packetSize - 1) / m_packetSize;
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, &MqttOtaTask::onTimeout);
}

bool MqttOtaTask::isRunning() const
{
    return m_state == State::WaitBootAck ||
           m_state == State::WaitStartAck ||
           m_state == State::SendingData ||
           m_state == State::WaitEndAck;
}

bool MqttOtaTask::isTerminal() const
{
    return m_state == State::Succeeded ||
           m_state == State::Failed ||
           m_state == State::Cancelled;
}

void MqttOtaTask::setQueuePosition(int position)
{
    if (!isQueued())
        return;
    m_statusText = QString("排队中（第 %1 位）").arg(position);
    emit changed(m_id);
}

void MqttOtaTask::start()
{
    if (!isQueued())
        return;

    m_packetIndex = 0;
    m_retryCount = 0;
    setState(State::WaitBootAck, "通知设备跳转至 Bootloader", 0);
    emit logMessage(m_id, "任务开始，发送 OTA_ENTER");
    sendEnter();
}

void MqttOtaTask::handlePacket(Protocol::MsgType type,
                               const QByteArray &data)
{
    if (!isRunning())
        return;

    switch (m_state) {
    case State::WaitBootAck:
        if (type != Protocol::OTA_ENTER_ACK) {
            emit logMessage(m_id,
                            QString("等待 ENTER_ACK，忽略包 0x%1")
                                .arg(static_cast<uint8_t>(type), 2, 16, QChar('0')));
            return;
        }
        m_timeout.stop();
        m_retryCount = 0;
        setState(State::WaitStartAck, "设备已进入 Bootloader，等待升级确认", 5);
        emit logMessage(m_id, "收到 OTA_ENTER_ACK，发送 OTA_BEGIN");
        sendBegin();
        break;

    case State::WaitStartAck:
        if (type != Protocol::OTA_BEGIN_ACK) {
            emit logMessage(m_id,
                            QString("等待 BEGIN_ACK，忽略包 0x%1")
                                .arg(static_cast<uint8_t>(type), 2, 16, QChar('0')));
            return;
        }
        m_timeout.stop();
        if (!data.isEmpty() && static_cast<uint8_t>(data[0]) != 0x00U) {
            const uint8_t code = static_cast<uint8_t>(data[0]);
            const QString reason =
                code == 0x01U ? "版本相同，设备拒绝升级" :
                code == 0x02U ? "Flash 空间不足" :
                code == 0x03U ? "当前状态不允许升级" :
                QString("设备拒绝升级（0x%1）").arg(code, 2, 16, QChar('0'));
            fail(reason);
            return;
        }
        m_retryCount = 0;
        m_packetIndex = 0;
        setState(State::SendingData, "开始传输固件", 10);
        emit logMessage(m_id, "收到 OTA_BEGIN_ACK，开始发送数据");
        sendData();
        break;

    case State::SendingData: {
        if (type != Protocol::OTA_DATA_ACK) {
            emit logMessage(m_id,
                            QString("等待 DATA_ACK，忽略包 0x%1")
                                .arg(static_cast<uint8_t>(type), 2, 16, QChar('0')));
            return;
        }
        if (data.size() < 3) {
            emit logMessage(m_id, "收到长度不足的 DATA_ACK，继续等待");
            return;
        }

        const int ackIndex = static_cast<uint8_t>(data[0]) |
                             (static_cast<uint8_t>(data[1]) << 8);
        if (ackIndex != m_packetIndex) {
            emit logMessage(
                m_id,
                QString("忽略非当前包 ACK：收到 %1，当前 %2")
                    .arg(ackIndex + 1)
                    .arg(m_packetIndex + 1));
            return;
        }

        m_timeout.stop();
        const uint8_t result = static_cast<uint8_t>(data[2]);
        if (result != 0x00U) {
            retryOrFail(QString("数据包 %1 校验失败（0x%2）")
                            .arg(m_packetIndex + 1)
                            .arg(result, 2, 16, QChar('0')));
            return;
        }

        m_retryCount = 0;
        ++m_packetIndex;
        sendData();
        break;
    }

    case State::WaitEndAck:
        if (type != Protocol::OTA_FINISH_ACK) {
            emit logMessage(m_id,
                            QString("等待 FINISH_ACK，忽略包 0x%1")
                                .arg(static_cast<uint8_t>(type), 2, 16, QChar('0')));
            return;
        }
        m_timeout.stop();
        if (!data.isEmpty() && static_cast<uint8_t>(data[0]) != 0x00U) {
            const uint8_t code = static_cast<uint8_t>(data[0]);
            const QString reason =
                code == 0x01U ? "CRC32 校验失败" :
                code == 0x02U ? "Flash 烧录失败" :
                code == 0xFFU ? "设备报告其他错误" :
                QString("设备报告未知错误（0x%1）").arg(code, 2, 16, QChar('0'));
            fail(reason);
            return;
        }
        succeed();
        break;

    default:
        break;
    }
}

void MqttOtaTask::notifyPacketPublished(int timeoutMs)
{
    if (isRunning())
        m_timeout.start(timeoutMs);
}

void MqttOtaTask::abort(const QString &reason)
{
    if (isTerminal())
        return;
    if (isQueued()) {
        m_timeout.stop();
        setState(State::Cancelled, "已取消：" + reason, m_progress);
        emit finished(m_id, false);
        return;
    }
    fail(reason);
}

void MqttOtaTask::confirmHeartbeat(const QString &version)
{
    if (m_state != State::Succeeded)
        return;

    m_statusText = version.isEmpty()
                       ? "升级成功，设备已重启上线"
                       : QString("升级成功，设备已上线（版本 %1）").arg(version);
    emit changed(m_id);
}

void MqttOtaTask::onTimeout()
{
    switch (m_state) {
    case State::WaitBootAck:
        retryOrFail("等待 ENTER_ACK 超时");
        break;
    case State::WaitStartAck:
        retryOrFail("等待 BEGIN_ACK 超时");
        break;
    case State::SendingData:
        retryOrFail(QString("数据包 %1 等待 ACK 超时")
                        .arg(m_packetIndex + 1));
        break;
    case State::WaitEndAck:
        retryOrFail("等待 FINISH_ACK 超时");
        break;
    default:
        break;
    }
}

void MqttOtaTask::setState(State state,
                           const QString &status,
                           int progress)
{
    m_state = state;
    m_statusText = status;
    m_progress = qBound(0, progress, 100);
    emit changed(m_id);
}

void MqttOtaTask::sendEnter()
{
    emit packetReady(m_id, m_deviceId, Protocol::OTA_ENTER, {},
                     kControlTimeoutMs);
}

void MqttOtaTask::sendBegin()
{
    const uint32_t firmwareSize = static_cast<uint32_t>(m_firmware.size());
    const uint16_t packetSize = static_cast<uint16_t>(m_packetSize);
    const uint16_t packetTotal = static_cast<uint16_t>(m_packetTotal);

    QByteArray data(8, 0);
    data[0] = static_cast<char>(firmwareSize & 0xFFU);
    data[1] = static_cast<char>((firmwareSize >> 8) & 0xFFU);
    data[2] = static_cast<char>((firmwareSize >> 16) & 0xFFU);
    data[3] = static_cast<char>((firmwareSize >> 24) & 0xFFU);
    data[4] = static_cast<char>(packetSize & 0xFFU);
    data[5] = static_cast<char>(packetSize >> 8);
    data[6] = static_cast<char>(packetTotal & 0xFFU);
    data[7] = static_cast<char>(packetTotal >> 8);

    emit packetReady(m_id, m_deviceId, Protocol::OTA_BEGIN, data,
                     kControlTimeoutMs);
}

void MqttOtaTask::sendData()
{
    if (m_packetIndex >= m_packetTotal) {
        m_retryCount = 0;
        setState(State::WaitEndAck, "数据发送完成，等待固件校验", 90);
        sendFinish();
        return;
    }

    const int offset = m_packetIndex * m_packetSize;
    const int size = qMin(m_firmware.size() - offset, m_packetSize);
    QByteArray data(4 + size, 0);
    data[0] = static_cast<char>(m_packetIndex & 0xFF);
    data[1] = static_cast<char>(m_packetIndex >> 8);
    data[2] = static_cast<char>(m_packetTotal & 0xFF);
    data[3] = static_cast<char>(m_packetTotal >> 8);
    std::memcpy(data.data() + 4, m_firmware.constData() + offset, size);

    const int progress = 10 + (m_packetIndex * 80) / m_packetTotal;
    setState(State::SendingData,
             QString("传输数据包 %1 / %2")
                 .arg(m_packetIndex + 1)
                 .arg(m_packetTotal),
             progress);
    emit packetReady(m_id, m_deviceId, Protocol::OTA_DATA, data,
                     kDataTimeoutMs);
}

void MqttOtaTask::sendFinish()
{
    const uint32_t crc32 = Protocol::crc32mpeg2(
        reinterpret_cast<const uint8_t *>(m_firmware.constData()),
        m_firmware.size());

    QByteArray data(5, 0);
    data[0] = static_cast<char>(crc32 & 0xFFU);
    data[1] = static_cast<char>((crc32 >> 8) & 0xFFU);
    data[2] = static_cast<char>((crc32 >> 16) & 0xFFU);
    data[3] = static_cast<char>((crc32 >> 24) & 0xFFU);
    data[4] = static_cast<char>(0x01);

    emit packetReady(m_id, m_deviceId, Protocol::OTA_FINISH, data,
                     kControlTimeoutMs);
}

void MqttOtaTask::retryOrFail(const QString &description)
{
    ++m_retryCount;
    if (m_retryCount > kMaxRetries) {
        fail(description + "，重试 3 次仍无响应");
        return;
    }

    emit logMessage(m_id,
                    QString("%1，第 %2 次重试")
                        .arg(description)
                        .arg(m_retryCount));

    switch (m_state) {
    case State::WaitBootAck:
        sendEnter();
        break;
    case State::WaitStartAck:
        sendBegin();
        break;
    case State::SendingData:
        sendData();
        break;
    case State::WaitEndAck:
        sendFinish();
        break;
    default:
        break;
    }
}

void MqttOtaTask::fail(const QString &reason)
{
    m_timeout.stop();
    setState(State::Failed, "升级失败：" + reason, m_progress);
    emit logMessage(m_id, "任务失败：" + reason);
    emit finished(m_id, false);
}

void MqttOtaTask::succeed()
{
    m_timeout.stop();
    setState(State::Succeeded, "升级成功，等待设备重启心跳", 100);
    emit logMessage(m_id, "收到 OTA_FINISH_ACK，升级成功");
    emit finished(m_id, true);
}

