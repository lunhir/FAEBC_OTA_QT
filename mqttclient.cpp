#include "mqttclient.h"
#include <QDebug>

// ─── helpers ──────────────────────────────────────────────────────────────────
static void appendU16(QByteArray &b, uint16_t v)
{
    b.append((char)(v >> 8));
    b.append((char)(v & 0xFF));
}
static void appendStr(QByteArray &b, const QString &s)
{
    QByteArray u = s.toUtf8();
    appendU16(b, (uint16_t)u.size());
    b.append(u);
}
static uint16_t readU16(const QByteArray &b, int off)
{
    return ((uint8_t)b[off] << 8) | (uint8_t)b[off + 1];
}

// ─────────────────────────────────────────────────────────────────────────────
MqttClient::MqttClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_pingTimer(new QTimer(this))
{
    connect(m_socket, &QTcpSocket::connected,
            this, &MqttClient::onSocketConnected);
    connect(m_socket, &QTcpSocket::disconnected,
            this, &MqttClient::onSocketDisconnected);
    connect(m_socket, &QTcpSocket::readyRead,
            this, &MqttClient::onSocketReadyRead);
    connect(m_socket,
            QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
            this, &MqttClient::onSocketError);
    connect(m_pingTimer, &QTimer::timeout, this, &MqttClient::onPingTimer);
}

void MqttClient::setHost(const QString &host, quint16 port)           { m_host = host; m_port = port; }
void MqttClient::setClientId(const QString &id)                        { m_clientId = id; }
void MqttClient::setKeepAlive(uint16_t seconds)                        { m_keepAlive = seconds; }
void MqttClient::setCredentials(const QString &u, const QString &p)   { m_username = u; m_password = p; }

// ─── connection ───────────────────────────────────────────────────────────────
void MqttClient::connectToHost()
{
    if (m_state != Disconnected) return;
    setState(Connecting);
    m_rxBuf.clear();
    m_socket->connectToHost(m_host, m_port);
}

void MqttClient::disconnectFromHost()
{
    m_pingTimer->stop();
    // Send DISCONNECT (0xE0 0x00) politely
    QByteArray disc;
    disc.append((char)0xE0);
    disc.append((char)0x00);
    m_socket->write(disc);
    m_socket->disconnectFromHost();
}

void MqttClient::onSocketConnected()
{
    sendRaw(buildConnect());
    // CONNACK handled in processIncoming
}

void MqttClient::onSocketDisconnected()
{
    m_pingTimer->stop();
    setState(Disconnected);
    emit disconnected();
}

void MqttClient::onSocketError(QAbstractSocket::SocketError)
{
    emit error(m_socket->errorString());
}

// ─── keep-alive ping ──────────────────────────────────────────────────────────
void MqttClient::onPingTimer()
{
    sendRaw(buildPingreq());
}

// ─── incoming data ────────────────────────────────────────────────────────────
void MqttClient::onSocketReadyRead()
{
    m_rxBuf.append(m_socket->readAll());
    processIncoming();
}

void MqttClient::processIncoming()
{
    while (m_rxBuf.size() >= 2) {
        // Parse remaining length (variable, 1-4 bytes starting at offset 1)
        int bytesUsed = 0;
        int remaining = decodeLength(m_rxBuf, bytesUsed);
        if (remaining < 0) break;          // not enough bytes yet to decode length
        int totalLen = 1 + bytesUsed + remaining;
        if (m_rxBuf.size() < totalLen) break;  // incomplete packet

        uint8_t   packetType = (uint8_t)m_rxBuf[0];
        QByteArray payload   = m_rxBuf.mid(1 + bytesUsed, remaining);
        m_rxBuf.remove(0, totalLen);

        handlePacket(packetType, payload);
    }
}

void MqttClient::handlePacket(uint8_t type, const QByteArray &payload)
{
    uint8_t ptype = type & 0xF0;

    switch (ptype) {
    case 0x20: {  // CONNACK
        if (payload.size() >= 2 && (uint8_t)payload[1] == 0x00) {
            setState(Connected);
            m_pingTimer->start(m_keepAlive * 900);  // 90% of keepalive
            emit connected();
        } else {
            uint8_t code = payload.size() >= 2 ? (uint8_t)payload[1] : 0xFF;
            emit error(QString("CONNACK拒绝，代码: 0x%1").arg(code, 2, 16, QChar('0')));
            m_socket->disconnectFromHost();
        }
        break;
    }
    case 0x30: {  // PUBLISH (received from broker)
        if (payload.size() < 2) break;
        uint16_t topicLen = readU16(payload, 0);
        if ((int)payload.size() < 2 + topicLen) break;
        QString topic = QString::fromUtf8(payload.mid(2, topicLen));

        int dataOff = 2 + topicLen;
        uint8_t qos = (type >> 1) & 0x03;
        if (qos > 0) {
            // QoS 1/2: packet ID follows topic
            if (payload.size() < dataOff + 2) break;
            uint16_t pid = readU16(payload, dataOff);
            dataOff += 2;
            if (qos == 1) sendRaw(buildPuback(pid));
        }
        QByteArray msg = payload.mid(dataOff);
        emit messageReceived(msg, topic);
        break;
    }
    case 0x40:  // PUBACK – QoS 1 publish confirmed; nothing to do
        break;
    case 0x90:  // SUBACK – subscription confirmed
        break;
    case 0xD0:  // PINGRESP
        break;
    default:
        break;
    }
}

// ─── publish ─────────────────────────────────────────────────────────────────
void MqttClient::publish(const QString &topic, const QByteArray &payload, uint8_t qos)
{
    if (m_state != Connected) return;
    sendRaw(buildPublish(topic, payload, qos));
}

// ─── subscribe ───────────────────────────────────────────────────────────────
void MqttClient::subscribe(const QString &topicFilter, uint8_t qos)
{
    if (m_state != Connected) return;
    sendRaw(buildSubscribe(topicFilter, qos));
}

// ─── packet builders ──────────────────────────────────────────────────────────
QByteArray MqttClient::buildConnect()
{
    bool hasUser = !m_username.isEmpty();
    bool hasPass = !m_password.isEmpty();

    uint8_t connectFlags = 0x02;  // CleanSession
    if (hasUser) connectFlags |= 0x80;
    if (hasPass) connectFlags |= 0x40;

    QByteArray vh;
    appendStr(vh, "MQTT");
    vh.append((char)0x04);              // Protocol Level 3.1.1
    vh.append((char)connectFlags);
    appendU16(vh, m_keepAlive);

    QByteArray pl;
    appendStr(pl, m_clientId.isEmpty() ? "MqttClient" : m_clientId);
    if (hasUser) appendStr(pl, m_username);
    if (hasPass) appendStr(pl, m_password);

    QByteArray pkt;
    pkt.append((char)0x10);             // CONNECT
    pkt.append(encodeLength(vh.size() + pl.size()));
    pkt.append(vh);
    pkt.append(pl);
    return pkt;
}

QByteArray MqttClient::buildSubscribe(const QString &filter, uint8_t qos)
{
    uint16_t pid = nextPacketId();
    QByteArray vh;
    appendU16(vh, pid);
    QByteArray pl;
    appendStr(pl, filter);
    pl.append((char)(qos & 0x03));

    QByteArray pkt;
    pkt.append((char)0x82);          // SUBSCRIBE
    pkt.append(encodeLength(vh.size() + pl.size()));
    pkt.append(vh);
    pkt.append(pl);
    return pkt;
}

QByteArray MqttClient::buildPublish(const QString &topic, const QByteArray &payload, uint8_t qos)
{
    QByteArray vh;
    appendStr(vh, topic);
    if (qos > 0) appendU16(vh, nextPacketId());

    uint8_t fixedHeader = 0x30 | ((qos & 0x03) << 1);
    QByteArray pkt;
    pkt.append((char)fixedHeader);
    pkt.append(encodeLength(vh.size() + payload.size()));
    pkt.append(vh);
    pkt.append(payload);
    return pkt;
}

QByteArray MqttClient::buildPuback(uint16_t packetId)
{
    QByteArray pkt;
    pkt.append((char)0x40);
    pkt.append((char)0x02);
    appendU16(pkt, packetId);
    return pkt;
}

QByteArray MqttClient::buildPingreq()
{
    QByteArray pkt;
    pkt.append((char)0xC0);
    pkt.append((char)0x00);
    return pkt;
}

// ─── variable-length encoding ─────────────────────────────────────────────────
QByteArray MqttClient::encodeLength(int length)
{
    QByteArray result;
    do {
        uint8_t byte = length & 0x7F;
        length >>= 7;
        if (length > 0) byte |= 0x80;
        result.append((char)byte);
    } while (length > 0);
    return result;
}

// Returns remaining length decoded from buf starting at offset 1.
// Sets bytesUsed to number of bytes consumed for the length field.
// Returns -1 if not enough data.
int MqttClient::decodeLength(const QByteArray &buf, int &bytesUsed)
{
    int value = 0;
    int mul   = 1;
    bytesUsed = 0;
    for (int i = 1; i <= 4; ++i) {
        if (i >= buf.size()) return -1;   // not enough bytes
        uint8_t b = (uint8_t)buf[i];
        value += (b & 0x7F) * mul;
        mul   <<= 7;
        bytesUsed++;
        if ((b & 0x80) == 0) return value;
    }
    return -1;  // malformed
}

// ─── state helper ─────────────────────────────────────────────────────────────
void MqttClient::setState(State s)
{
    if (m_state == s) return;
    m_state = s;
    emit stateChanged(s);
}

void MqttClient::sendRaw(const QByteArray &data)
{
    if (m_socket->state() == QAbstractSocket::ConnectedState)
        m_socket->write(data);
}
