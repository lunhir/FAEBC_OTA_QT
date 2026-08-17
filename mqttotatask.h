#ifndef MQTTOTATASK_H
#define MQTTOTATASK_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>

#include "protocol.h"

class MqttOtaTask : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Queued,
        WaitBootAck,
        WaitStartAck,
        SendingData,
        WaitEndAck,
        Succeeded,
        Failed,
        Cancelled,
    };
    Q_ENUM(State)

    MqttOtaTask(quint64 id,
                const QString &deviceId,
                const QString &firmwareName,
                const QByteArray &firmware,
                int packetSize,
                QObject *parent = nullptr);

    quint64 id() const { return m_id; }
    QString deviceId() const { return m_deviceId; }
    QString firmwareName() const { return m_firmwareName; }
    int firmwareSize() const { return m_firmware.size(); }
    int packetSize() const { return m_packetSize; }
    int packetIndex() const { return m_packetIndex; }
    int packetTotal() const { return m_packetTotal; }
    int progress() const { return m_progress; }
    QString statusText() const { return m_statusText; }
    State state() const { return m_state; }

    bool isQueued() const { return m_state == State::Queued; }
    bool isRunning() const;
    bool isTerminal() const;

    void setQueuePosition(int position);
    void start();
    void handlePacket(Protocol::MsgType type, const QByteArray &data);
    void notifyPacketPublished(int timeoutMs);
    void abort(const QString &reason);
    void confirmHeartbeat(const QString &version);

signals:
    void packetReady(quint64 taskId,
                     const QString &deviceId,
                     Protocol::MsgType type,
                     const QByteArray &data,
                     int timeoutMs);
    void changed(quint64 taskId);
    void logMessage(quint64 taskId, const QString &message);
    void finished(quint64 taskId, bool success);

private slots:
    void onTimeout();

private:
    void setState(State state, const QString &status, int progress);
    void sendEnter();
    void sendBegin();
    void sendData();
    void sendFinish();
    void retryOrFail(const QString &description);
    void fail(const QString &reason);
    void succeed();

    quint64 m_id;
    QString m_deviceId;
    QString m_firmwareName;
    QByteArray m_firmware;
    int m_packetSize;
    int m_packetIndex = 0;
    int m_packetTotal = 0;
    int m_retryCount = 0;
    int m_progress = 0;
    State m_state = State::Queued;
    QString m_statusText = "排队中";
    QTimer m_timeout;
};

#endif // MQTTOTATASK_H
