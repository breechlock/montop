#include "sparkline.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

Sparkline::Sparkline(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    connect(this, &Sparkline::changed, this, [this] { update(); });
}

qreal Sparkline::autoMaximum(const QList<qreal> &a, const QList<qreal> &b) {
    qreal peak = 0;
    for (qreal v : a)
        peak = std::max(peak, v);
    for (qreal v : b)
        peak = std::max(peak, v);
    // 16 KiB/s keeps an idle link from filling the graph with noise.
    peak = std::max(peak * 1.15, 16.0 * 1024);
    // Round up to 1, 2, or 5 times a power of ten, so the scale moves in
    // visible jumps instead of jittering with every sample.
    const qreal magnitude = std::pow(10.0, std::floor(std::log10(peak)));
    for (qreal step : {1.0, 2.0, 5.0, 10.0})
        if (peak <= step * magnitude)
            return step * magnitude;
    return peak;
}

void Sparkline::paint(QPainter *painter) {
    const qreal w = width(), h = height();
    if (w <= 0 || h <= 0)
        return;
    painter->setRenderHint(QPainter::Antialiasing);

    // Faint quarter lines give the eye a scale without competing with data.
    painter->setPen(QPen(m_gridColor, 1, Qt::DotLine));
    for (int i = 1; i < 4; ++i) {
        const qreal y = std::round(h * i / 4) + 0.5;
        painter->drawLine(QPointF(0, y), QPointF(w, y));
    }

    const qreal maximum = m_maximum > 0 ? m_maximum : autoMaximum(m_values, m_mirror);
    if (m_mirror.isEmpty()) {
        drawSeries(painter, m_values, m_color, h, h, -1, maximum);
    } else {
        const qreal middle = std::round(h / 2);
        drawSeries(painter, m_values, m_color, middle, middle, -1, maximum);
        drawSeries(painter, m_mirror, m_mirrorColor, middle, h - middle, 1, maximum);
    }
}

void Sparkline::drawSeries(QPainter *painter, const QList<qreal> &values, const QColor &color, qreal baseline,
                           qreal height, int direction, qreal maximum) {
    if (values.isEmpty() || maximum <= 0)
        return;
    const qreal w = width();
    const qreal step = w / std::max(1, m_capacity - 1);
    const int count = std::min<int>(values.size(), m_capacity);
    const int first = values.size() - count;

    QPainterPath line;
    for (int i = 0; i < count; ++i) {
        const qreal x = w - (count - 1 - i) * step;
        const qreal v = std::clamp(values[first + i] / maximum, 0.0, 1.0);
        const QPointF point(x, baseline + direction * v * (height - 1));
        if (i == 0)
            line.moveTo(point);
        else
            line.lineTo(point);
    }

    QPainterPath area = line;
    area.lineTo(w, baseline);
    area.lineTo(w - (count - 1) * step, baseline);
    area.closeSubpath();

    QLinearGradient gradient(0, baseline + direction * height, 0, baseline);
    QColor strong = color, faint = color;
    strong.setAlphaF(0.45);
    faint.setAlphaF(0.06);
    gradient.setColorAt(0, strong);
    gradient.setColorAt(1, faint);
    painter->fillPath(area, gradient);

    painter->setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(line);
}
