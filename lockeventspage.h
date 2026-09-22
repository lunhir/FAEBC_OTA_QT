#pragma once
#include <QWidget>
#include <QString>
#include <QByteArray>
#include <functional>
class QLabel;
class QPushButton;
class QTableWidget;
class QTimer;

class LockEventsPage : public QWidget
{
public:
    explicit LockEventsPage(std::function<void()> request, QWidget *parent = nullptr);
    void setDevice(const QString &uid);
    quint32 beginRequest(const QString &uid);
    bool showResponse(const QString &uid, const QByteArray &payload);
private:
    void clearData();
    void fail(const QString &message);
    QString m_uid;
    quint32 m_requestId = 0;
    bool m_pending = false;
    QLabel *m_status;
    QLabel *m_state;
    QLabel *m_details;
    QPushButton *m_read;
    QTableWidget *m_counts;
    QTableWidget *m_events;
    QTimer *m_timeout;
};
