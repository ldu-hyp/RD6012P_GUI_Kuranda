#include "MainWindow.h"

#include "RealtimePlotWidget.h"
#include "RidenProtocol.h"
#include "SerialWorker.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSerialPortInfo>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>
#include <QtMath>

namespace
{
QLabel *makeInfoLabel()
{
    auto *label = new QLabel(QStringLiteral("--"));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setObjectName(QStringLiteral("infoValue"));
    return label;
}

QString portDisplayName(const QSerialPortInfo &port)
{
    QString text = port.portName();
    if (!port.description().isEmpty()) {
        text += QStringLiteral(" — ") + port.description();
    }
    return text;
}

void setLabelTextIfChanged(QLabel *label, const QString &text)
{
    if (label->text() != text) {
        label->setText(text);
    }
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    qRegisterMetaType<DeviceInfo>("DeviceInfo");
    qRegisterMetaType<DeviceSnapshot>("DeviceSnapshot");

    buildUi();
    applyTheme();
    refreshPorts();

    m_worker = new SerialWorker;
    m_worker->moveToThread(&m_workerThread);

    connect(&m_workerThread, &QThread::finished,
            m_worker, &QObject::deleteLater);

    connect(this, &MainWindow::openPortRequested,
            m_worker, &SerialWorker::openPort);
    connect(this, &MainWindow::closePortRequested,
            m_worker, &SerialWorker::closePort);
    connect(this, &MainWindow::immediatePollRequested,
            m_worker, &SerialWorker::requestImmediatePoll);
    connect(this, &MainWindow::voltageSetRequested,
            m_worker, &SerialWorker::setVoltage);
    connect(this, &MainWindow::currentSetRequested,
            m_worker, &SerialWorker::setCurrent);
    connect(this, &MainWindow::outputSetRequested,
            m_worker, &SerialWorker::setOutputEnabled);
    connect(this, &MainWindow::currentRangeSetRequested,
            m_worker, &SerialWorker::setCurrentRange);
    connect(this, &MainWindow::pollIntervalSetRequested,
            m_worker, &SerialWorker::setPollInterval);

    connect(m_worker, &SerialWorker::connectionChanged,
            this, &MainWindow::onConnectionChanged);
    connect(m_worker, &SerialWorker::deviceInfoReceived,
            this, &MainWindow::onDeviceInfo);
    connect(m_worker, &SerialWorker::snapshotReceived,
            this, &MainWindow::onSnapshot);
    connect(m_worker, &SerialWorker::protocolError,
            this, &MainWindow::onProtocolError);
    connect(m_worker, &SerialWorker::commandAcknowledged,
            this, &MainWindow::onCommandAcknowledged);

    m_workerThread.setObjectName(QStringLiteral("RD6012P Serial Worker"));
    m_workerThread.start();

    setConnectedUi(false);
}

MainWindow::~MainWindow()
{
    if (m_workerThread.isRunning()) {
        emit closePortRequested();
        m_workerThread.quit();
        m_workerThread.wait(1200);
    }
}

QWidget *MainWindow::createMetricCard(const QString &caption,
                                      const QString &unit,
                                      QLabel **valueLabel)
{
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("metricCard"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(1);

    auto *captionLabel = new QLabel(caption);
    captionLabel->setObjectName(QStringLiteral("metricCaption"));

    auto *row = new QHBoxLayout;
    row->setSpacing(6);

    auto *value = new QLabel(QStringLiteral("0.000"));
    value->setObjectName(QStringLiteral("metricValue"));
    auto *unitLabel = new QLabel(unit);
    unitLabel->setObjectName(QStringLiteral("metricUnit"));

    row->addWidget(value);
    row->addWidget(unitLabel, 0, Qt::AlignBottom);
    row->addStretch();

    layout->addWidget(captionLabel);
    layout->addLayout(row);

    *valueLabel = value;
    return card;
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("RD6012P Control Center — Kuranda"));
    resize(1360, 820);
    setMinimumSize(1080, 680);

    auto *central = new QWidget;
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(18, 16, 18, 16);
    root->setSpacing(12);

    auto *titleRow = new QHBoxLayout;
    auto *titleBlock = new QVBoxLayout;
    titleBlock->setSpacing(0);

    auto *title = new QLabel(QStringLiteral("RD6012P Control Center"));
    title->setObjectName(QStringLiteral("appTitle"));
    auto *subtitle = new QLabel(
        QStringLiteral("Low-latency Modbus RTU monitor & controller"));
    subtitle->setObjectName(QStringLiteral("appSubtitle"));

    titleBlock->addWidget(title);
    titleBlock->addWidget(subtitle);

    m_connectionStatus = new QLabel(QStringLiteral("●  Disconnected"));
    m_connectionStatus->setObjectName(QStringLiteral("connectionBadge"));

    titleRow->addLayout(titleBlock);
    titleRow->addStretch();
    titleRow->addWidget(m_connectionStatus, 0, Qt::AlignVCenter);
    root->addLayout(titleRow);

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);

    auto *left = new QWidget;
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(12);

    auto *metricRow = new QHBoxLayout;
    metricRow->setSpacing(10);
    metricRow->addWidget(
        createMetricCard(tr("OUTPUT VOLTAGE"), QStringLiteral("V"),
                         &m_voltageValue));
    metricRow->addWidget(
        createMetricCard(tr("OUTPUT CURRENT"), QStringLiteral("A"),
                         &m_currentValue));
    metricRow->addWidget(
        createMetricCard(tr("OUTPUT POWER"), QStringLiteral("W"),
                         &m_powerValue));
    leftLayout->addLayout(metricRow);

    m_plot = new RealtimePlotWidget;
    leftLayout->addWidget(m_plot, 1);

    splitter->addWidget(left);
    splitter->addWidget(createRightPanel());
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({920, 390});

    root->addWidget(splitter, 1);
    setCentralWidget(central);

    statusBar()->showMessage(tr("Ready"));
}

QWidget *MainWindow::createRightPanel()
{
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMinimumWidth(360);
    scroll->setMaximumWidth(470);

    auto *panel = new QWidget;
    panel->setObjectName(QStringLiteral("rightPanel"));
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 0, 4, 4);
    layout->setSpacing(12);

    auto *connectionGroup = new QGroupBox(tr("Connection"));
    auto *connectionLayout = new QVBoxLayout(connectionGroup);

    auto *portRow = new QHBoxLayout;
    m_portCombo = new QComboBox;
    auto *refreshButton = new QPushButton(tr("Refresh"));
    refreshButton->setObjectName(QStringLiteral("secondaryButton"));
    portRow->addWidget(m_portCombo, 1);
    portRow->addWidget(refreshButton);

    m_connectButton = new QPushButton(tr("Connect"));
    m_connectButton->setObjectName(QStringLiteral("primaryButton"));
    connectionLayout->addLayout(portRow);
    connectionLayout->addWidget(m_connectButton);

    connect(refreshButton, &QPushButton::clicked,
            this, &MainWindow::refreshPorts);
    connect(m_connectButton, &QPushButton::clicked,
            this, &MainWindow::toggleConnection);

    layout->addWidget(connectionGroup);

    auto *controlGroup = new QGroupBox(tr("Output Control"));
    auto *controlForm = new QFormLayout(controlGroup);
    controlForm->setFieldGrowthPolicy(
        QFormLayout::AllNonFixedFieldsGrow);

    m_voltageSet = new QDoubleSpinBox;
    m_voltageSet->setRange(0.000, 60.000);
    m_voltageSet->setDecimals(3);
    m_voltageSet->setSingleStep(0.001);
    m_voltageSet->setSuffix(QStringLiteral(" V"));

    m_voltageApply = new QPushButton(tr("Apply voltage"));
    m_voltageApply->setObjectName(QStringLiteral("secondaryButton"));

    auto *voltageBox = new QWidget;
    auto *voltageLayout = new QHBoxLayout(voltageBox);
    voltageLayout->setContentsMargins(0, 0, 0, 0);
    voltageLayout->addWidget(m_voltageSet, 1);
    voltageLayout->addWidget(m_voltageApply);

    m_currentSet = new QDoubleSpinBox;
    m_currentSet->setRange(0.0, 6.0);
    m_currentSet->setDecimals(4);
    m_currentSet->setSingleStep(0.0001);
    m_currentSet->setSuffix(QStringLiteral(" A"));

    m_currentApply = new QPushButton(tr("Apply current"));
    m_currentApply->setObjectName(QStringLiteral("secondaryButton"));

    auto *currentBox = new QWidget;
    auto *currentLayout = new QHBoxLayout(currentBox);
    currentLayout->setContentsMargins(0, 0, 0, 0);
    currentLayout->addWidget(m_currentSet, 1);
    currentLayout->addWidget(m_currentApply);

    m_rangeCombo = new QComboBox;
    m_rangeCombo->addItem(tr("6 A / 0.1 mA resolution"), 0);
    m_rangeCombo->addItem(tr("12 A / 1 mA resolution"), 1);
    auto *rangeApply = new QPushButton(tr("Apply range"));
    rangeApply->setObjectName(QStringLiteral("secondaryButton"));

    auto *rangeBox = new QWidget;
    auto *rangeLayout = new QHBoxLayout(rangeBox);
    rangeLayout->setContentsMargins(0, 0, 0, 0);
    rangeLayout->addWidget(m_rangeCombo, 1);
    rangeLayout->addWidget(rangeApply);

    m_outputButton = new QPushButton(tr("OUTPUT OFF"));
    m_outputButton->setCheckable(true);
    m_outputButton->setObjectName(QStringLiteral("outputButton"));

    controlForm->addRow(tr("Voltage"), voltageBox);
    controlForm->addRow(tr("Current"), currentBox);
    controlForm->addRow(tr("Range"), rangeBox);
    controlForm->addRow(QString(), m_outputButton);

    connect(m_voltageApply, &QPushButton::clicked,
            this, &MainWindow::applyVoltage);
    connect(m_currentApply, &QPushButton::clicked,
            this, &MainWindow::applyCurrent);
    connect(m_outputButton, &QPushButton::clicked,
            this, &MainWindow::toggleOutput);
    connect(rangeApply, &QPushButton::clicked,
            this, &MainWindow::applyRange);

    layout->addWidget(controlGroup);

    auto *performanceGroup = new QGroupBox(tr("Acquisition"));
    auto *performanceForm = new QFormLayout(performanceGroup);
    m_pollCombo = new QComboBox;
    m_pollCombo->addItem(tr("Maximum — continuous"), 0);
    m_pollCombo->addItem(tr("Fast — 20 ms idle"), 20);
    m_pollCombo->addItem(tr("Balanced — 100 ms idle"), 100);
    m_pollCombo->addItem(tr("Slow — 500 ms idle"), 500);

    m_rateValue = makeInfoLabel();
    m_rttValue = makeInfoLabel();
    performanceForm->addRow(tr("Acquisition mode"), m_pollCombo);
    performanceForm->addRow(tr("Actual update rate"), m_rateValue);
    performanceForm->addRow(tr("Last round trip"), m_rttValue);

    connect(m_pollCombo, &QComboBox::currentIndexChanged,
            this, [this](int index) {
                emit pollIntervalSetRequested(
                    m_pollCombo->itemData(index).toInt());
            });

    layout->addWidget(performanceGroup);

    auto *infoGroup = new QGroupBox(tr("Device Information"));
    auto *infoForm = new QFormLayout(infoGroup);
    m_modelValue = makeInfoLabel();
    m_productIdValue = makeInfoLabel();
    m_serialValue = makeInfoLabel();
    m_firmwareValue = makeInfoLabel();
    m_inputVoltageValue = makeInfoLabel();
    m_temperatureValue = makeInfoLabel();
    m_modeValue = makeInfoLabel();
    m_protectionValue = makeInfoLabel();
    m_presetValue = makeInfoLabel();
    m_lockValue = makeInfoLabel();

    infoForm->addRow(tr("Model"), m_modelValue);
    infoForm->addRow(tr("Product ID"), m_productIdValue);
    infoForm->addRow(tr("Serial"), m_serialValue);
    infoForm->addRow(tr("Firmware"), m_firmwareValue);
    infoForm->addRow(tr("Input"), m_inputVoltageValue);
    infoForm->addRow(tr("Internal temp."), m_temperatureValue);
    infoForm->addRow(tr("Regulation"), m_modeValue);
    infoForm->addRow(tr("Protection"), m_protectionValue);
    infoForm->addRow(tr("Preset"), m_presetValue);
    infoForm->addRow(tr("Keypad"), m_lockValue);

    layout->addWidget(infoGroup);
    layout->addStretch();

    scroll->setWidget(panel);
    return scroll;
}

void MainWindow::applyTheme()
{
    setStyleSheet(QStringLiteral(R"(
        QMainWindow, QWidget {
            background: #0d1118;
            color: #e8edf5;
            font-family: "Segoe UI";
            font-size: 10pt;
        }

        QLabel#appTitle {
            font-size: 21pt;
            font-weight: 700;
            color: #f5f8fc;
        }

        QLabel#appSubtitle {
            color: #78869a;
            font-size: 9.5pt;
        }

        QLabel#connectionBadge {
            background: #171e29;
            border: 1px solid #273244;
            border-radius: 13px;
            padding: 6px 12px;
            color: #9aabba;
            font-weight: 600;
        }

        QFrame#metricCard {
            background: #121821;
            border: 1px solid #202a38;
            border-radius: 12px;
        }

        QLabel#metricCaption {
            color: #8190a4;
            font-size: 8.5pt;
            font-weight: 600;
        }

        QLabel#metricValue {
            color: #f7f9fc;
            font-family: "Cascadia Mono";
            font-size: 24pt;
            font-weight: 700;
        }

        QLabel#metricUnit {
            color: #7e8da1;
            font-size: 11pt;
            padding-bottom: 4px;
        }

        QGroupBox {
            background: #121821;
            border: 1px solid #202a38;
            border-radius: 11px;
            margin-top: 13px;
            padding-top: 10px;
            font-weight: 600;
        }

        QGroupBox::title {
            subcontrol-origin: margin;
            left: 12px;
            padding: 0 5px;
            color: #aab6c7;
        }

        QComboBox, QDoubleSpinBox {
            background: #0d121a;
            border: 1px solid #2b3748;
            border-radius: 7px;
            min-height: 30px;
            padding: 3px 9px;
            selection-background-color: #2877d4;
        }

        QComboBox:focus, QDoubleSpinBox:focus {
            border: 1px solid #4c9aff;
        }

        QPushButton {
            min-height: 32px;
            border-radius: 7px;
            padding: 3px 12px;
            font-weight: 600;
        }

        QPushButton#primaryButton {
            background: #2d7eea;
            color: white;
            border: 1px solid #388bf4;
        }

        QPushButton#primaryButton:hover {
            background: #388bf4;
        }

        QPushButton#secondaryButton {
            background: #182231;
            border: 1px solid #2a3a50;
            color: #d7e0ec;
        }

        QPushButton#secondaryButton:hover {
            background: #202d3e;
        }

        QPushButton#outputButton {
            background: #2a171b;
            border: 1px solid #603139;
            color: #ff9da8;
            min-height: 38px;
            font-weight: 700;
        }

        QPushButton#outputButton:checked {
            background: #123424;
            border: 1px solid #267b53;
            color: #8ff0ba;
        }

        QLabel#infoValue {
            color: #dce5f1;
            font-family: "Cascadia Mono";
        }

        QScrollArea {
            background: transparent;
        }

        QStatusBar {
            color: #7e8da1;
        }

        QSplitter::handle {
            background: transparent;
            width: 10px;
        }

        QToolTip {
            background: #1c2634;
            color: white;
            border: 1px solid #34445b;
        }
    )"));
}

void MainWindow::refreshPorts()
{
    const QString selected =
        m_portCombo->currentData().toString();

    m_portCombo->clear();
    for (const QSerialPortInfo &port : QSerialPortInfo::availablePorts()) {
        m_portCombo->addItem(portDisplayName(port), port.portName());
    }

    const int previous = m_portCombo->findData(selected);
    if (previous >= 0) {
        m_portCombo->setCurrentIndex(previous);
    }
}

void MainWindow::toggleConnection()
{
    if (m_connected) {
        emit closePortRequested();
        return;
    }

    const QString portName =
        m_portCombo->currentData().toString();
    if (portName.isEmpty()) {
        statusBar()->showMessage(tr("No serial port selected."), 3000);
        return;
    }

    m_connectButton->setEnabled(false);
    statusBar()->showMessage(tr("Connecting to %1...").arg(portName));
    emit openPortRequested(portName);
}

void MainWindow::onConnectionChanged(bool connected,
                                     const QString &message)
{
    m_connected = connected;
    setConnectedUi(connected);
    m_connectButton->setEnabled(true);
    m_connectButton->setText(connected ? tr("Disconnect")
                                       : tr("Connect"));
    m_connectionStatus->setText(
        connected ? QStringLiteral("●  Connected")
                  : QStringLiteral("●  Disconnected"));
    m_connectionStatus->setStyleSheet(
        connected
            ? QStringLiteral("color:#8ff0ba; border-color:#245b42;")
            : QString());
    statusBar()->showMessage(message, 5000);

    if (!connected) {
        m_outputButton->setChecked(false);
        m_outputButton->setText(tr("OUTPUT OFF"));
        m_lastCurrentRange = -1;
        if (m_rateValue) {
            m_rateValue->setText(QStringLiteral("--"));
        }
        if (m_rttValue) {
            m_rttValue->setText(QStringLiteral("--"));
        }
    }
}

void MainWindow::onDeviceInfo(const DeviceInfo &info)
{
    m_modelValue->setText(info.modelName);
    m_productIdValue->setText(QString::number(info.productId));
    m_serialValue->setText(QString::number(info.serialNumber));
    m_firmwareValue->setText(
        QStringLiteral("V%1").arg(info.firmwareString()));

    if (info.modelName != QStringLiteral("RD6012P")) {
        statusBar()->showMessage(
            tr("Warning: detected %1 (ID %2); this build is tuned for RD6012P.")
                .arg(info.modelName)
                .arg(info.productId),
            8000);
    }

    emit immediatePollRequested();
}

void MainWindow::onSnapshot(const DeviceSnapshot &s)
{
    // The high-rate path touches only widgets whose displayed value changed.
    // Avoiding unconditional setText()/setValue() calls substantially reduces
    // layout/style work when the acquisition loop runs back-to-back.
    setLabelTextIfChanged(
        m_voltageValue, QString::number(s.voltageOut, 'f', 3));
    setLabelTextIfChanged(
        m_currentValue,
        QString::number(s.currentOut, 'f',
                        s.currentRange == 0 ? 4 : 3));
    setLabelTextIfChanged(
        m_powerValue, QString::number(s.powerOut, 'f', 2));

    m_plot->addSample(s.voltageOut, s.currentOut, s.powerOut);

    setLabelTextIfChanged(
        m_rateValue,
        s.updateRateHz > 0.0
            ? QStringLiteral("%1 Hz").arg(s.updateRateHz, 0, 'f', 1)
            : QStringLiteral("measuring..."));
    setLabelTextIfChanged(
        m_rttValue,
        QStringLiteral("%1 ms").arg(s.roundTripMs, 0, 'f', 0));

    setLabelTextIfChanged(
        m_inputVoltageValue,
        QStringLiteral("%1 V").arg(s.inputVoltage, 0, 'f', 2));
    setLabelTextIfChanged(
        m_temperatureValue,
        QStringLiteral("%1 °C").arg(s.internalTemperatureC, 0, 'f', 0));
    setLabelTextIfChanged(
        m_modeValue, RidenProtocol::regulationText(s.regulationMode));
    setLabelTextIfChanged(
        m_protectionValue, RidenProtocol::protectionText(s.protection));
    setLabelTextIfChanged(
        m_presetValue, QStringLiteral("M%1").arg(s.preset));
    setLabelTextIfChanged(
        m_lockValue, s.keypadLocked ? tr("Locked") : tr("Unlocked"));

    m_updatingControls = true;

    // Never fight the user while a set-point editor has focus.
    if (!m_voltageSet->hasFocus()
        && qAbs(m_voltageSet->value() - s.voltageSet) >= 0.0005) {
        m_voltageSet->setValue(s.voltageSet);
    }

    if (m_lastCurrentRange != s.currentRange) {
        m_lastCurrentRange = s.currentRange;
        const int index = m_rangeCombo->findData(s.currentRange);
        if (index >= 0 && m_rangeCombo->currentIndex() != index) {
            m_rangeCombo->setCurrentIndex(index);
        }
        updateCurrentSpinResolution(s.currentRange);
    }

    const double currentThreshold =
        (s.currentRange == 0) ? 0.00005 : 0.0005;
    if (!m_currentSet->hasFocus()
        && qAbs(m_currentSet->value() - s.currentSet) >= currentThreshold) {
        m_currentSet->setValue(s.currentSet);
    }

    if (m_outputButton->isChecked() != s.outputEnabled) {
        m_outputButton->setChecked(s.outputEnabled);
    }
    const QString outputText =
        s.outputEnabled ? tr("OUTPUT ON") : tr("OUTPUT OFF");
    if (m_outputButton->text() != outputText) {
        m_outputButton->setText(outputText);
    }

    m_updatingControls = false;
}

void MainWindow::onProtocolError(const QString &message)
{
    statusBar()->showMessage(message, 6000);
}

void MainWindow::onCommandAcknowledged(const QString &message)
{
    statusBar()->showMessage(tr("ACK: %1").arg(message), 2500);
}

void MainWindow::applyVoltage()
{
    emit voltageSetRequested(m_voltageSet->value());
}

void MainWindow::applyCurrent()
{
    emit currentSetRequested(m_currentSet->value());
}

void MainWindow::toggleOutput()
{
    if (m_updatingControls) {
        return;
    }
    emit outputSetRequested(m_outputButton->isChecked());
}

void MainWindow::applyRange()
{
    emit currentRangeSetRequested(
        m_rangeCombo->currentData().toInt());
}

void MainWindow::setConnectedUi(bool connected)
{
    m_portCombo->setEnabled(!connected);
    m_voltageSet->setEnabled(connected);
    m_currentSet->setEnabled(connected);
    m_voltageApply->setEnabled(connected);
    m_currentApply->setEnabled(connected);
    m_outputButton->setEnabled(connected);
    m_rangeCombo->setEnabled(connected);
}

void MainWindow::updateCurrentSpinResolution(int range)
{
    if (range == 0) {
        m_currentSet->setRange(0.0, 6.0);
        m_currentSet->setDecimals(4);
        m_currentSet->setSingleStep(0.0001);
    } else {
        m_currentSet->setRange(0.0, 12.0);
        m_currentSet->setDecimals(3);
        m_currentSet->setSingleStep(0.001);
    }
}
