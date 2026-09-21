#pragma once

#include "DeviceData.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QQueue>
#include <QSerialPort>

class QTimer;

class SerialWorker final : public QObject
{
    Q_OBJECT

public:
    explicit SerialWorker(QObject *parent = nullptr);

public slots:
    void openPort(const QString &portName);
    void closePort();
    void requestImmediatePoll();

    void setVoltage(double volts);
    void setCurrent(double amps);
    void setOutputEnabled(bool enabled);
    void setCurrentRange(int range);
    void setPollInterval(int milliseconds);

signals:
    void connectionChanged(bool connected, const QString &message);
    void deviceInfoReceived(const DeviceInfo &info);
    void snapshotReceived(const DeviceSnapshot &snapshot);
    void protocolError(const QString &message);
    void commandAcknowledged(const QString &message);

private slots:
    void onReadyRead();
    void onSerialError(QSerialPort::SerialPortError error);
    void onRequestTimeout();
    void onPollTimer();

private:
    enum class RequestType {
        DeviceInfo,
        Brightness,
        Temperature,
        SetpointPoll,
        StatusPoll,
        FastMeter,
        WriteVoltage,
        WriteCurrent,
        WriteOutput,
        WriteRange
    };

    struct Request {
        RequestType type = RequestType::FastMeter;
        QByteArray frame;
        quint8 function = 0;
        quint16 startRegister = 0;
        quint16 count = 0;
        QString description;
        int retryCount = 0;
    };

    void enqueueRead(quint16 start, quint16 count, RequestType type,
                     const QString &description, bool priority = false);
    void enqueueWrite(quint16 reg, quint16 value, RequestType type,
                      const QString &description);
    void runNextRequest();
    void transmitCurrentRequest();
    void completeCurrentRequest(const QByteArray &frame);
    void failCurrentRequest(const QString &reason);
    void scheduleNextPoll();
    int expectedFrameLength() const;
    void tryExtractFrame();
    bool fastMeterAlreadyPending() const;
    void enqueueOneDueBackgroundRead();

    QSerialPort *m_serial = nullptr;
    QTimer *m_timeoutTimer = nullptr;
    QTimer *m_pollTimer = nullptr;

    QByteArray m_rxBuffer;
    QQueue<Request> m_priorityQueue;
    QQueue<Request> m_normalQueue;

    Request m_currentRequest;
    bool m_hasCurrentRequest = false;
    bool m_connected = false;

    int m_pollIntervalMs = 0;
    int m_currentRange = 0;
    int m_lastBacklight = -1;

    DeviceSnapshot m_cachedSnapshot;

    QElapsedTimer m_statusClock;
    QElapsedTimer m_setpointClock;
    QElapsedTimer m_temperatureClock;
    QElapsedTimer m_rateClock;
    QElapsedTimer m_requestClock;

    int m_rateSampleCount = 0;
    double m_liveRateHz = 0.0;

    static constexpr int kRequestTimeoutMs = 350;
    static constexpr int kStatusIntervalMs = 250;
    static constexpr int kSetpointIntervalMs = 500;
    static constexpr int kTemperatureIntervalMs = 1000;
};
