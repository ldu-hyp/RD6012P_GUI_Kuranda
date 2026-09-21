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

// High-rate meter block: 0x000A..0x000D (VOUT, IOUT, power).
DeviceSnapshot decodeMeterSnapshot(const QVector<quint16> &registers,
                                   int currentRange);

// Low-rate blocks update a cached snapshot without disturbing meter cadence.
void applyStatusRegisters(const QVector<quint16> &registers,
                          DeviceSnapshot &snapshot); // 0x000E..0x0014
void applySetpointRegisters(const QVector<quint16> &registers,
                            int currentRange,
                            DeviceSnapshot &snapshot); // 0x0008..0x0009

QString modelNameFromId(quint16 productId);
QString protectionText(int protection);
QString regulationText(int regulationMode);
} // namespace RidenProtocol
