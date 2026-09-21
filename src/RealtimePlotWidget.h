#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QRectF>
#include <QString>
#include <QVector>
#include <QWidget>

class QPainter;

class RealtimePlotWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit RealtimePlotWidget(QWidget *parent = nullptr);

    void addSample(double voltage, double current, double power);
    void clear();
    void setTimeWindowSeconds(int seconds);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    struct Sample {
        qint64 t = 0;
        double voltage = 0.0;
        double current = 0.0;
        double power = 0.0;
    };

    void trimSamples();
    void drawLane(QPainter &painter, const QRectF &rect,
                  const QString &title, const QString &unit,
                  const QColor &color, int valueSelector,
                  qint64 nowMs);

    QVector<Sample> m_samples;
    QElapsedTimer m_clock;
    int m_timeWindowSeconds = 60;
};
