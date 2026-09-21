#pragma once

#include <QMetaType>
#include <QString>
#include <QtGlobal>

struct DeviceInfo
{
    quint16 productId = 0;
    quint32 serialNumber = 0;
    quint16 firmwareRaw = 0;
    QString modelName = QStringLiteral("Unknown");

    QString firmwareString() const
    {
        return QString::number(static_cast<double>(firmwareRaw) / 100.0, 'f', 2);
    }
};

struct DeviceSnapshot
{
    qint64 timestampMs = 0;

    double voltageSet = 0.0;
    double currentSet = 0.0;
    double voltageOut = 0.0;
    double currentOut = 0.0;
    double powerOut = 0.0;
    double inputVoltage = 0.0;
    double internalTemperatureC = 0.0;

    bool keypadLocked = false;
    int protection = 0;
    int regulationMode = 0; // 0=CV, 1=CC
    bool outputEnabled = false;
    int preset = 0;
    int currentRange = 0; // RD6012P: 0=6 A, 1=12 A
};

Q_DECLARE_METATYPE(DeviceInfo)
Q_DECLARE_METATYPE(DeviceSnapshot)
