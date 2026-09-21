#pragma once

#include "DeviceData.h"

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace RidenProtocol
{
constexpr quint8 kSlaveAddress = 0x01;
constexpr int kBaudRate = 115200;

quint16 crc16(const QByteArray &data);
bool validateFrame(const QByteArray &frame);

QByteArray readHoldingRegisters(quint16 startRegister, quint16 count);
QByteArray writeSingleRegister(quint16 registerAddress, quint16 value);

QVector<quint16> decodeReadRegisters(const QByteArray &frame, bool *ok = nullptr);
DeviceInfo decodeDeviceInfo(const QVector<quint16> &registers);

// One-time connection snapshot: 0x0004..0x0014.
DeviceSnapshot decodeInitialSnapshot(const QVector<quint16> &registers);

// High-rate path: 0x000A..0x000B only.
// Power is deliberately calculated on the PC as V * I.
void applyFastVI(const QVector<quint16> &registers,
                 int currentRange,
                 DeviceSnapshot &snapshot);

QString modelNameFromId(quint16 productId);
QString protectionText(int protection);
QString regulationText(int regulationMode);
} // namespace RidenProtocol
