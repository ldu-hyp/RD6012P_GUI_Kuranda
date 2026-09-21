#include "SerialWorker.h"

#include "RidenProtocol.h"

#include <QSerialPortInfo>
#include <QtMath>

SerialWorker::SerialWorker(QObject *parent)
    : QObject(parent)
{
    m_timeoutTimer.setSingleShot(true);
    m_pollTimer.setSingleShot(true);

    connect(&m_serial, &QSerialPort::readyRead,
            this, &SerialWorker::onReadyRead);
    connect(&m_serial, &QSerialPort::errorOccurred,
            this, &SerialWorker::onSerialError);
    connect(&m_timeoutTimer, &QTimer::timeout,
            this, &SerialWorker::onRequestTimeout);
    connect(&m_pollTimer, &QTimer::timeout,
            this, &SerialWorker::onPollTimer);
}

void SerialWorker::openPort(const QString &portName)
{
    if (m_serial.isOpen()) {
        closePort();
    }

    m_serial.setPortName(portName);
    m_serial.setBaudRate(RidenProtocol::kBaudRate);
    m_serial.setDataBits(QSerialPort::Data8);
    m_serial.setParity(QSerialPort::NoParity);
    m_serial.setStopBits(QSerialPort::OneStop);
    m_serial.setFlowControl(QSerialPort::NoFlowControl);

    if (!m_serial.open(QIODevice::ReadWrite)) {
        emit connectionChanged(false,
                               tr("Open %1 failed: %2")
                                   .arg(portName, m_serial.errorString()));
        return;
    }

    m_serial.clear();
    m_rxBuffer.clear();
    m_priorityQueue.clear();
    m_normalQueue.clear();
    m_hasCurrentRequest = false;
    m_connected = true;

    // RIDEN PC software sends this ASCII probe before Modbus traffic.
    m_serial.write(QByteArrayLiteral("queryd\r\n"));
    m_serial.flush();

    emit connectionChanged(true,
                           tr("%1 @ 115200 8N1").arg(portName));

    QTimer::singleShot(45, this, [this]() {
        if (!m_connected) {
            return;
        }
        enqueueRead(0x0000, 0x0004, RequestType::DeviceInfo,
                    tr("Read device information"), true);
        enqueueRead(0x0048, 0x0001, RequestType::Brightness,
                    tr("Read backlight"), false);
        runNextRequest();
    });
}

void SerialWorker::closePort()
{
    m_pollTimer.stop();
    m_timeoutTimer.stop();
    m_priorityQueue.clear();
    m_normalQueue.clear();
    m_rxBuffer.clear();
    m_hasCurrentRequest = false;

    if (m_serial.isOpen()) {
        m_serial.close();
    }

    if (m_connected) {
        m_connected = false;
        emit connectionChanged(false, tr("Disconnected"));
    }
}

void SerialWorker::requestImmediatePoll()
{
    if (!m_connected) {
        return;
    }
    m_pollTimer.stop();

    const auto alreadyQueued = [this]() {
        if (m_hasCurrentRequest && m_currentRequest.type == RequestType::PollState) {
            return true;
        }
        for (const Request &r : m_normalQueue) {
            if (r.type == RequestType::PollState) {
                return true;
            }
        }
        return false;
    };

    if (!alreadyQueued()) {
        enqueueRead(0x0004, 0x0026, RequestType::PollState,
                    tr("Read live state"), false);
    }
    runNextRequest();
}

void SerialWorker::setVoltage(double volts)
{
    const auto raw = static_cast<quint16>(
        qBound(0, qRound(volts * 1000.0), 60000));
    enqueueWrite(0x0008, raw, RequestType::WriteVoltage,
                 tr("Voltage set to %1 V").arg(volts, 0, 'f', 3));
}

void SerialWorker::setCurrent(double amps)
{
    const double scale = (m_currentRange == 0) ? 10000.0 : 1000.0;
    const int maxRaw = (m_currentRange == 0) ? 60000 : 12000;
    const auto raw = static_cast<quint16>(
        qBound(0, qRound(amps * scale), maxRaw));
    enqueueWrite(0x0009, raw, RequestType::WriteCurrent,
                 tr("Current set to %1 A")
                     .arg(amps, 0, 'f', m_currentRange == 0 ? 4 : 3));
}

void SerialWorker::setOutputEnabled(bool enabled)
{
    enqueueWrite(0x0012, enabled ? 1 : 0, RequestType::WriteOutput,
                 enabled ? tr("Output ON") : tr("Output OFF"));
}

void SerialWorker::setCurrentRange(int range)
{
    const int normalized = (range == 0) ? 0 : 1;
    enqueueWrite(0x0014, static_cast<quint16>(normalized),
                 RequestType::WriteRange,
                 normalized == 0 ? tr("6 A current range")
                                 : tr("12 A current range"));
}

void SerialWorker::setPollInterval(int milliseconds)
{
    m_pollIntervalMs = qBound(20, milliseconds, 2000);
    if (m_connected && !m_hasCurrentRequest) {
        scheduleNextPoll();
    }
}

void SerialWorker::enqueueRead(quint16 start, quint16 count,
                               RequestType type,
                               const QString &description,
                               bool priority)
{
    Request request;
    request.type = type;
    request.frame = RidenProtocol::readHoldingRegisters(start, count);
    request.function = 0x03;
    request.startRegister = start;
    request.count = count;
    request.description = description;

    if (priority) {
        m_priorityQueue.enqueue(request);
    } else {
        m_normalQueue.enqueue(request);
    }
}

void SerialWorker::enqueueWrite(quint16 reg, quint16 value,
                                RequestType type,
                                const QString &description)
{
    if (!m_connected) {
        emit protocolError(tr("Device is not connected."));
        return;
    }

    Request request;
    request.type = type;
    request.frame = RidenProtocol::writeSingleRegister(reg, value);
    request.function = 0x06;
    request.startRegister = reg;
    request.count = 1;
    request.description = description;

    // User commands always jump ahead of background polling.
    m_priorityQueue.enqueue(request);
    m_pollTimer.stop();
    runNextRequest();
}

void SerialWorker::runNextRequest()
{
    if (!m_connected || m_hasCurrentRequest) {
        return;
    }

    if (!m_priorityQueue.isEmpty()) {
        m_currentRequest = m_priorityQueue.dequeue();
    } else if (!m_normalQueue.isEmpty()) {
        m_currentRequest = m_normalQueue.dequeue();
    } else {
        scheduleNextPoll();
        return;
    }

    m_hasCurrentRequest = true;
    transmitCurrentRequest();
}

void SerialWorker::transmitCurrentRequest()
{
    m_rxBuffer.clear();
    const qint64 queued = m_serial.write(m_currentRequest.frame);
    if (queued != m_currentRequest.frame.size()) {
        failCurrentRequest(tr("Unable to queue complete serial frame."));
        return;
    }

    m_serial.flush();
    m_timeoutTimer.start(kRequestTimeoutMs);
}

void SerialWorker::onReadyRead()
{
    m_rxBuffer.append(m_serial.readAll());
    tryExtractFrame();
}

int SerialWorker::expectedFrameLength() const
{
    if (!m_hasCurrentRequest || m_rxBuffer.size() < 2) {
        return -1;
    }

    const quint8 function = static_cast<quint8>(m_rxBuffer.at(1));
    if (function & 0x80) {
        return 5;
    }

    if (function == 0x03) {
        if (m_rxBuffer.size() < 3) {
            return -1;
        }
        return static_cast<quint8>(m_rxBuffer.at(2)) + 5;
    }

    if (function == 0x06) {
        return 8;
    }

    return -1;
}

void SerialWorker::tryExtractFrame()
{
    while (m_hasCurrentRequest && !m_rxBuffer.isEmpty()) {
        // Resynchronize to slave address if stray bytes are present.
        const int slavePos = m_rxBuffer.indexOf(
            static_cast<char>(RidenProtocol::kSlaveAddress));
        if (slavePos < 0) {
            m_rxBuffer.clear();
            return;
        }
        if (slavePos > 0) {
            m_rxBuffer.remove(0, slavePos);
        }

        const int length = expectedFrameLength();
        if (length < 0 || m_rxBuffer.size() < length) {
            return;
        }

        const QByteArray frame = m_rxBuffer.left(length);
        m_rxBuffer.remove(0, length);

        if (!RidenProtocol::validateFrame(frame)) {
            emit protocolError(tr("CRC error; frame discarded."));
            continue;
        }

        completeCurrentRequest(frame);
    }
}

void SerialWorker::completeCurrentRequest(const QByteArray &frame)
{
    m_timeoutTimer.stop();

    const quint8 function = static_cast<quint8>(frame.at(1));
    if (function & 0x80) {
        const quint8 exceptionCode = static_cast<quint8>(frame.at(2));
        failCurrentRequest(
            tr("Modbus exception 0x%1 while %2")
                .arg(exceptionCode, 2, 16, QLatin1Char('0'))
                .arg(m_currentRequest.description));
        return;
    }

    if (function != m_currentRequest.function) {
        failCurrentRequest(tr("Unexpected Modbus function code."));
        return;
    }

    if (function == 0x03) {
        bool ok = false;
        const QVector<quint16> regs =
            RidenProtocol::decodeReadRegisters(frame, &ok);
        if (!ok) {
            failCurrentRequest(tr("Malformed read response."));
            return;
        }

        switch (m_currentRequest.type) {
        case RequestType::DeviceInfo:
            emit deviceInfoReceived(RidenProtocol::decodeDeviceInfo(regs));
            break;
        case RequestType::Brightness:
            if (!regs.isEmpty()) {
                m_lastBacklight = regs.first();
            }
            break;
        case RequestType::PollState: {
            const DeviceSnapshot snapshot =
                RidenProtocol::decodeSnapshot(regs);
            m_currentRange = snapshot.currentRange;
            emit snapshotReceived(snapshot);
            break;
        }
        default:
            break;
        }
    } else if (function == 0x06) {
        if (frame != m_currentRequest.frame) {
            failCurrentRequest(tr("Write acknowledgement does not match request."));
            return;
        }

        emit commandAcknowledged(m_currentRequest.description);
        // Reflect user commands quickly instead of waiting for the normal cadence.
        if (m_currentRequest.type == RequestType::WriteRange) {
            m_currentRange =
                (static_cast<quint8>(frame.at(4)) << 8)
                | static_cast<quint8>(frame.at(5));
        }
    }

    m_hasCurrentRequest = false;

    // After any write, verify the resulting state immediately.
    if (function == 0x06) {
        requestImmediatePoll();
    } else {
        runNextRequest();
    }
}

void SerialWorker::failCurrentRequest(const QString &reason)
{
    m_timeoutTimer.stop();

    if (m_hasCurrentRequest && m_currentRequest.retryCount < 1) {
        ++m_currentRequest.retryCount;
        emit protocolError(
            tr("%1 Retrying once...").arg(reason));
        transmitCurrentRequest();
        return;
    }

    emit protocolError(reason);
    m_hasCurrentRequest = false;
    runNextRequest();
}

void SerialWorker::onRequestTimeout()
{
    failCurrentRequest(
        tr("Timeout while %1").arg(m_currentRequest.description));
}

void SerialWorker::scheduleNextPoll()
{
    if (!m_connected) {
        return;
    }
    m_pollTimer.start(m_pollIntervalMs);
}

void SerialWorker::onPollTimer()
{
    requestImmediatePoll();
}

void SerialWorker::onSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError
        || error == QSerialPort::TimeoutError) {
        return;
    }

    const QString message = m_serial.errorString();
    if (m_serial.isOpen()) {
        closePort();
    }
    emit protocolError(tr("Serial port error: %1").arg(message));
}
