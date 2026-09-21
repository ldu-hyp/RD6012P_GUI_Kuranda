#pragma once

#include "DeviceData.h"

#include <QMainWindow>
#include <QThread>

class QLabel;
class QComboBox;
class QPushButton;
class QDoubleSpinBox;
class QSpinBox;
class RealtimePlotWidget;
class SerialWorker;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

signals:
    void openPortRequested(const QString &portName);
    void closePortRequested();
    void immediatePollRequested();
    void voltageSetRequested(double volts);
    void currentSetRequested(double amps);
    void outputSetRequested(bool enabled);
    void currentRangeSetRequested(int range);
    void pollIntervalSetRequested(int milliseconds);

private slots:
    void refreshPorts();
    void toggleConnection();
    void onConnectionChanged(bool connected, const QString &message);
    void onDeviceInfo(const DeviceInfo &info);
    void onSnapshot(const DeviceSnapshot &snapshot);
    void onProtocolError(const QString &message);
    void onCommandAcknowledged(const QString &message);
    void applyVoltage();
    void applyCurrent();
    void toggleOutput();
    void applyRange();

private:
    QWidget *createMetricCard(const QString &caption,
                              const QString &unit,
                              QLabel **valueLabel);
    QWidget *createRightPanel();
    void buildUi();
    void applyTheme();
    void setConnectedUi(bool connected);
    void updateCurrentSpinResolution(int range);

    QThread m_workerThread;
    SerialWorker *m_worker = nullptr;

    bool m_connected = false;
    bool m_updatingControls = false;

    QComboBox *m_portCombo = nullptr;
    QPushButton *m_connectButton = nullptr;
    QLabel *m_connectionStatus = nullptr;

    QLabel *m_voltageValue = nullptr;
    QLabel *m_currentValue = nullptr;
    QLabel *m_powerValue = nullptr;

    RealtimePlotWidget *m_plot = nullptr;

    QDoubleSpinBox *m_voltageSet = nullptr;
    QDoubleSpinBox *m_currentSet = nullptr;
    QPushButton *m_voltageApply = nullptr;
    QPushButton *m_currentApply = nullptr;
    QPushButton *m_outputButton = nullptr;
    QComboBox *m_rangeCombo = nullptr;
    QComboBox *m_pollCombo = nullptr;

    QLabel *m_modelValue = nullptr;
    QLabel *m_productIdValue = nullptr;
    QLabel *m_serialValue = nullptr;
    QLabel *m_firmwareValue = nullptr;
    QLabel *m_inputVoltageValue = nullptr;
    QLabel *m_temperatureValue = nullptr;
    QLabel *m_modeValue = nullptr;
    QLabel *m_protectionValue = nullptr;
    QLabel *m_presetValue = nullptr;
    QLabel *m_lockValue = nullptr;
};
