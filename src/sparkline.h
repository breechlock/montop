#pragma once

#include <QColor>
#include <QList>
#include <QQuickPaintedItem>

// A filled history graph, newest sample on the right. With a second series
// it draws that one mirrored below a center line, the way btop pairs
// download with upload and reads with writes.
class Sparkline : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QList<qreal> values MEMBER m_values NOTIFY changed)
    Q_PROPERTY(QList<qreal> mirror MEMBER m_mirror NOTIFY changed)
    Q_PROPERTY(QColor color MEMBER m_color NOTIFY changed)
    Q_PROPERTY(QColor mirrorColor MEMBER m_mirrorColor NOTIFY changed)
    Q_PROPERTY(QColor gridColor MEMBER m_gridColor NOTIFY changed)
    // 0 scales to the largest value on screen; 1 suits fractions.
    Q_PROPERTY(qreal maximum MEMBER m_maximum NOTIFY changed)
    Q_PROPERTY(int capacity MEMBER m_capacity NOTIFY changed)

public:
    explicit Sparkline(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;

    // The scale an auto-scaled graph uses: the peak, rounded up so the line
    // doesn't touch the top, never below a floor that keeps idle noise flat.
    static qreal autoMaximum(const QList<qreal> &a, const QList<qreal> &b);

signals:
    void changed();

private:
    void drawSeries(QPainter *painter, const QList<qreal> &values, const QColor &color, qreal baseline,
                    qreal height, int direction, qreal maximum);

    QList<qreal> m_values, m_mirror;
    QColor m_color{"#ffd60a"}, m_mirrorColor{"#ffd60a"}, m_gridColor{"#20ffffff"};
    qreal m_maximum = 0;
    int m_capacity = 150;
};
