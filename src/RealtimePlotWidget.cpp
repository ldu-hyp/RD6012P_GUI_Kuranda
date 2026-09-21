#include "RealtimePlotWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QtMath>

RealtimePlotWidget::RealtimePlotWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(560, 360);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent, true);

    m_clock.start();

    // Rendering cadence is decoupled from the instrument sample cadence.
    // The graph scrolls smoothly at ~60 FPS without inventing/interpolating
    // measurement samples.
    m_renderTimer.setInterval(16);
    m_renderTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_renderTimer, &QTimer::timeout,
            this, QOverload<>::of(&RealtimePlotWidget::update));
    m_renderTimer.start();
}

void RealtimePlotWidget::addSample(double voltage, double current, double power)
{
    m_samples.push_back({m_clock.elapsed(), voltage, current, power});
    trimSamples();
}

void RealtimePlotWidget::clear()
{
    m_samples.clear();
    m_clock.restart();
    update();
}

void RealtimePlotWidget::setTimeWindowSeconds(int seconds)
{
    m_timeWindowSeconds = qBound(10, seconds, 1800);
    trimSamples();
    update();
}

void RealtimePlotWidget::trimSamples()
{
    if (m_samples.isEmpty()) {
        return;
    }

    const qint64 cutoff = m_clock.elapsed()
                          - static_cast<qint64>(m_timeWindowSeconds) * 1000;
    int firstKeep = 0;
    while (firstKeep < m_samples.size()
           && m_samples.at(firstKeep).t < cutoff) {
        ++firstKeep;
    }
    if (firstKeep > 0) {
        m_samples.remove(0, firstKeep);
    }

    // Hard ceiling keeps repaint cost bounded even with aggressive polling.
    constexpr int kMaxSamples = 6000;
    if (m_samples.size() > kMaxSamples) {
        m_samples.remove(0, m_samples.size() - kMaxSamples);
    }
}

void RealtimePlotWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.fillRect(rect(), QColor(14, 18, 25));

    const QRectF outer = QRectF(rect()).adjusted(14, 14, -14, -14);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(18, 24, 33));
    painter.drawRoundedRect(outer, 12, 12);

    const QRectF plot = outer.adjusted(54, 18, -18, -34);
    const qreal gap = 12.0;
    const qreal laneHeight = (plot.height() - 2.0 * gap) / 3.0;
    const qint64 now = m_clock.elapsed();

    drawLane(painter,
             QRectF(plot.left(), plot.top(), plot.width(), laneHeight),
             tr("Voltage"), QStringLiteral("V"),
             QColor(92, 181, 255), 0, now);
    drawLane(painter,
             QRectF(plot.left(), plot.top() + laneHeight + gap,
                    plot.width(), laneHeight),
             tr("Current"), QStringLiteral("A"),
             QColor(95, 218, 154), 1, now);
    drawLane(painter,
             QRectF(plot.left(), plot.top() + 2.0 * (laneHeight + gap),
                    plot.width(), laneHeight),
             tr("Power"), QStringLiteral("W"),
             QColor(255, 184, 92), 2, now);

    painter.setPen(QColor(116, 128, 145));
    const QString timeText =
        tr("Last %1 s  •  %2 samples")
            .arg(m_timeWindowSeconds)
            .arg(m_samples.size());
    painter.drawText(QRectF(outer.left() + 16, outer.bottom() - 28,
                            outer.width() - 32, 20),
                     Qt::AlignRight | Qt::AlignVCenter,
                     timeText);
}

void RealtimePlotWidget::drawLane(QPainter &painter,
                                  const QRectF &rect,
                                  const QString &title,
                                  const QString &unit,
                                  const QColor &color,
                                  int valueSelector,
                                  qint64 nowMs)
{
    painter.save();

    painter.setPen(QPen(QColor(43, 52, 65), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rect, 7, 7);

    for (int i = 1; i < 4; ++i) {
        const qreal y = rect.top() + rect.height() * i / 4.0;
        painter.setPen(QPen(QColor(37, 46, 58), 1));
        painter.drawLine(QPointF(rect.left(), y),
                         QPointF(rect.right(), y));
    }

    auto valueOf = [valueSelector](const Sample &s) {
        if (valueSelector == 0) {
            return s.voltage;
        }
        if (valueSelector == 1) {
            return s.current;
        }
        return s.power;
    };

    double maxValue = 0.0;
    double latest = 0.0;
    for (const Sample &sample : m_samples) {
        const double value = qMax(0.0, valueOf(sample));
        maxValue = qMax(maxValue, value);
        latest = valueOf(sample);
    }

    const double minimumScale =
        (valueSelector == 0) ? 1.0 : (valueSelector == 1 ? 0.1 : 1.0);
    maxValue = qMax(minimumScale, maxValue * 1.12);

    painter.setPen(QColor(141, 154, 171));
    painter.drawText(QRectF(rect.left() - 50, rect.top(),
                            44, 18),
                     Qt::AlignRight | Qt::AlignVCenter,
                     QString::number(maxValue, 'f',
                                     valueSelector == 1 ? 3 : 2));

    painter.setPen(color);
    QFont labelFont = painter.font();
    labelFont.setBold(true);
    painter.setFont(labelFont);
    painter.drawText(QRectF(rect.left() + 10, rect.top() + 7,
                            180, 20),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("%1   %2 %3")
                         .arg(title,
                              QString::number(latest, 'f',
                                              valueSelector == 1 ? 4 : 3),
                              unit));

    if (m_samples.size() >= 2) {
        const qint64 windowMs =
            static_cast<qint64>(m_timeWindowSeconds) * 1000;
        const qint64 startMs = nowMs - windowMs;

        QPainterPath path;
        bool first = true;
        for (const Sample &sample : m_samples) {
            if (sample.t < startMs) {
                continue;
            }
            const qreal x = rect.left()
                            + rect.width()
                                  * static_cast<qreal>(sample.t - startMs)
                                  / static_cast<qreal>(windowMs);
            const double normalized =
                qBound(0.0, valueOf(sample) / maxValue, 1.0);
            const qreal y =
                rect.bottom() - normalized * (rect.height() - 5.0);

            if (first) {
                path.moveTo(x, y);
                first = false;
            } else {
                path.lineTo(x, y);
            }
        }

        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(color, 1.8));
        painter.drawPath(path);
    }

    painter.restore();
}
