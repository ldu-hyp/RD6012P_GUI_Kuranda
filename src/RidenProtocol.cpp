#include "RidenProtocol.h"

#include <QDateTime>

namespace
{
void appendU16BE(QByteArray &buffer, quint16 value)
{
    buffer.append(static_cast<char>((value >> 8) & 0xFF));
    buffer.append(static_cast<char>(value & 0xFF));
}

quint16 u16At(const QByteArray &data, int offset)
{
    return (static_cast<quint16>(static_cast<quint8>(data.at(offset))) << 8)
           | static_cast<quint16>(static_cast<quint8>(data.at(offset + 1)));
}
}

namespace RidenProtocol
{
quint16 crc16(const QByteArray &data)
{
    quint16 crc = 0xFFFF;
    for (const char byte : data) {
        crc ^= static_cast<quint8>(byte);
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x0001) {
                crc = static_cast<quint16>((crc >> 1) ^ 0xA001);
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

bool validateFrame(const QByteArray &frame)
{
    if (frame.size() < 5) {
        return false;
    }

    const QByteArray payload = frame.left(frame.size() - 2);
    const quint16 expected = crc16(payload);
    const quint16 received = static_cast<quint8>(frame.at(frame.size() - 2))
                             | (static_cast<quint16>(
                                    static_cast<quint8>(frame.at(frame.size() - 1)))
                                << 8);
    return expected == received;
}

QByteArray readHoldingRegisters(quint16 startRegister, quint16 count)
{
    QByteArray frame;
    frame.reserve(8);
    frame.append(static_cast<char>(kSlaveAddress));
    frame.append(static_cast<char>(0x03));
    appendU16BE(frame, startRegister);
    appendU16BE(frame, count);

    const quint16 crc = crc16(frame);
    frame.append(static_cast<char>(crc & 0xFF));
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    return frame;
}

QByteArray writeSingleRegister(quint16 registerAddress, quint16 value)
{
    QByteArray frame;
    frame.reserve(8);
    frame.append(static_cast<char>(kSlaveAddress));
    frame.append(static_cast<char>(0x06));
    appendU16BE(frame, registerAddress);
    appendU16BE(frame, value);

    const quint16 crc = crc16(frame);
    frame.append(static_cast<char>(crc & 0xFF));
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    return frame;
}

QVector<quint16> decodeReadRegisters(const QByteArray &frame, bool *ok)
{
    bool valid = false;
    QVector<quint16> registers;

    if (frame.size() >= 5
        && static_cast<quint8>(frame.at(0)) == kSlaveAddress
        && static_cast<quint8>(frame.at(1)) == 0x03
        && validateFrame(frame)) {
        const int byteCount = static_cast<quint8>(frame.at(2));
        if ((byteCount % 2) == 0 && frame.size() == byteCount + 5) {
            registers.reserve(byteCount / 2);
            for (int offset = 3; offset < 3 + byteCount; offset += 2) {
                registers.push_back(u16At(frame, offset));
            }
            valid = true;
        }
    }

    if (ok) {
        *ok = valid;
    }
    return registers;
}

QString modelNameFromId(quint16 productId)
{
    if (productId >= 60125 && productId <= 60129) {
        return QStringLiteral("RD6012P");
    }
    if (productId >= 60120 && productId <= 60124) {
        return QStringLiteral("RD6012");
    }
    if (productId >= 60060 && productId <= 60064) {
        return QStringLiteral("RD6006");
    }
    if (productId == 60065) {
        return QStringLiteral("RD6006P");
    }
    if (productId >= 60180 && productId <= 60189) {
        return QStringLiteral("RD6018");
    }
    if (productId >= 60241 && productId <= 60249) {
        return QStringLiteral("RD6024");
    }
    return QStringLiteral("RD60xx");
}

DeviceInfo decodeDeviceInfo(const QVector<quint16> &registers)
{
    DeviceInfo info;
    if (registers.size() < 4) {
        return info;
    }

    info.productId = registers.at(0);
    info.serialNumber = (static_cast<quint32>(registers.at(1)) << 16)
                        | static_cast<quint32>(registers.at(2));
    info.firmwareRaw = registers.at(3);
    info.modelName = modelNameFromId(info.productId);
    return info;
}

DeviceSnapshot decodeInitialSnapshot(const QVector<quint16> &r)
{
    DeviceSnapshot s;
    s.timestampMs = QDateTime::currentMSecsSinceEpoch();

    if (r.size() < 17) {
        return s;
    }

    s.internalTemperatureC = (r.at(0) == 0 ? 1.0 : -1.0) * r.at(1);
    s.currentRange = static_cast<int>(r.at(16));

    const double currentScale = (s.currentRange == 0) ? 10000.0 : 1000.0;

    s.voltageSet = r.at(4) / 1000.0;
    s.currentSet = r.at(5) / currentScale;
    s.voltageOut = r.at(6) / 1000.0;
    s.currentOut = r.at(7) / currentScale;

    // v0.2.2 intentionally does not use the device power registers.
    s.powerOut = s.voltageOut * s.currentOut;

    s.inputVoltage = r.at(10) / 100.0;
    s.keypadLocked = r.at(11) != 0;
    s.protection = static_cast<int>(r.at(12));
    s.regulationMode = static_cast<int>(r.at(13));
    s.outputEnabled = r.at(14) != 0;
    s.preset = static_cast<int>(r.at(15));

    return s;
}

void applyFastVI(const QVector<quint16> &r,
                 int currentRange,
                 DeviceSnapshot &s)
{
    if (r.size() < 2) {
        return;
    }

    const double currentScale = (currentRange == 0) ? 10000.0 : 1000.0;

    s.timestampMs = QDateTime::currentMSecsSinceEpoch();
    s.currentRange = currentRange;
    s.voltageOut = r.at(0) / 1000.0;
    s.currentOut = r.at(1) / currentScale;
    s.powerOut = s.voltageOut * s.currentOut;
}

QString protectionText(int protection)
{
    switch (protection) {
    case 0:
        return QStringLiteral("Normal");
    case 1:
        return QStringLiteral("OVP");
    case 2:
        return QStringLiteral("OCP");
    case 3:
        return QStringLiteral("OPP");
    default:
        return QStringLiteral("Unknown (%1)").arg(protection);
    }
}

QString regulationText(int regulationMode)
{
    switch (regulationMode) {
    case 0:
        return QStringLiteral("CV");
    case 1:
        return QStringLiteral("CC");
    default:
        return QStringLiteral("--");
    }
}
} // namespace RidenProtocol
