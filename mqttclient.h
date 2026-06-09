#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QByteArray>
#include <QString>

/**
 * Minimal MQTT 3.1.1 client over QTcpSocket.
 * Supports: CONNECT, SUBSCRIBE, PUBLISH (QoS 0/1), PINGREQ/PINGRESP.
 */
class MqttClient : public QObject
{
    Q_OBJECT
public:
    enum State { Disconnected, Connecting, Connected };
    Q_ENUM(State)

    explicit MqttClient(QObject *parent = nullptr);

    void setHost(const QString &host, quint16 port);
    void setClientId(const QString &id);
    void setKeepAlive(uint16_t seconds);
    void setCredentials(const QString &username, const QString &password);

    void connectToHost();
    void disconnectFromHost();

    /** Publish with QoS 0 (fire & forget) */
    void publish(const QString &topic, const QByteArray &payload, uint8_t qos = 0);

    /** Subscribe to topic with given QoS */
    void subscribe(const QString &topicFilter, uint8_t qos = 1);

    State state() const { return m_state; }

signals:
    void connected();
    void disconnected();
    void stateChanged(State s);
    void messageReceived(const QByteArray &payload, const QString &topic);
    void error(const QString &msg);

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketReadyRead();
    void onSocketError(QAbstractSocket::SocketError err);
    void onPingTimer();

private:
    // Packet builders
    QByteArray buildConnect();
    QByteArray buildSubscribe(const QString &filter, uint8_t qos);
    QByteArray buildPublish(const QString &topic, const QByteArray &payload, uint8_t qos);
    QByteArray buildPuback(uint16_t packetId);
    QByteArray buildPingreq();

    // Remaining-length helpers
    static QByteArray encodeLength(int length);
    static int decodeLength(const QByteArray &buf, int &bytesUsed);

    void setState(State s);
    void sendRaw(const QByteArray &data);
    void processIncoming();
    void handlePacket(uint8_t type, const QByteArray &payload);

    QTcpSocket *m_socket;
    QTimer     *m_pingTimer;
    QByteArray  m_rxBuf;

    QString   m_host;
    quint16   m_port        = 1883;
    QString   m_clientId;
    QString   m_username;
    QString   m_password;
    uint16_t  m_keepAlive   = 60;
    State     m_state       = Disconnected;
    uint16_t  m_packetIdSeq = 1;

    uint16_t nextPacketId() { return m_packetIdSeq++; }
};
