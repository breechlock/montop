#include "sparkline.h"

#include "monitor.h"

#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGTransformNode>
#include <QSGVertexColorMaterial>
#include <algorithm>
#include <cmath>

namespace {

// Vertex colors are premultiplied.
QSGGeometry::ColoredPoint2D vertex(float x, float y, const QColor &color, qreal opacity) {
    const float a = float(color.alphaF() * opacity);
    QSGGeometry::ColoredPoint2D v;
    v.set(x, y, uchar(color.redF() * a * 255 + .5f), uchar(color.greenF() * a * 255 + .5f),
          uchar(color.blueF() * a * 255 + .5f), uchar(a * 255 + .5f));
    return v;
}

// Qt merges small nodes into shared batches by transforming their vertices
// on the CPU, which for a graph that moves every frame would mean redoing
// thousands of vertices per frame. Asking for the full matrix keeps each graph
// in its own batch: its vertices go to the GPU once per sample and the
// per-frame slide is a single uniform.
class GraphMaterial : public QSGVertexColorMaterial {
public:
    GraphMaterial() { setFlag(RequiresFullMatrix); }
};

QSGGeometryNode *colorNode(unsigned int mode) {
    auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
    geometry->setDrawingMode(mode);
    auto *node = new QSGGeometryNode;
    node->setGeometry(geometry);
    node->setFlag(QSGNode::OwnsGeometry);
    node->setMaterial(new GraphMaterial);
    node->setFlag(QSGNode::OwnsMaterial);
    return node;
}

void fillNode(QSGGeometryNode *node, const QList<QSGGeometry::ColoredPoint2D> &vertices) {
    QSGGeometry *geometry = node->geometry();
    geometry->allocate(int(vertices.size()));
    std::copy(vertices.begin(), vertices.end(), geometry->vertexDataAsColoredPoint2D());
    node->markDirty(QSGNode::DirtyGeometry);
}

struct Root : QSGNode {
    QSGGeometryNode *grid = colorNode(QSGGeometry::DrawTriangles);
    QSGTransformNode *motion = new QSGTransformNode;
    QSGGeometryNode *fills[2] = {colorNode(QSGGeometry::DrawTriangleStrip), colorNode(QSGGeometry::DrawTriangleStrip)};
    QSGGeometryNode *lines[2] = {colorNode(QSGGeometry::DrawTriangles), colorNode(QSGGeometry::DrawTriangles)};

    Root() {
        appendChildNode(grid);
        appendChildNode(motion);
        for (int i = 0; i < 2; ++i) {
            motion->appendChildNode(fills[i]);
            motion->appendChildNode(lines[i]);
        }
    }
};

} // namespace

Sparkline::Sparkline(QQuickItem *parent) : QQuickItem(parent) {
    setFlag(ItemHasContents);
    // The newest point waits just past the right edge and slides in.
    setClip(true);
}

void Sparkline::invalidate() {
    m_shapeDirty = true;
    update();
    emit changed();
}

void Sparkline::setSource(Monitor *source) {
    if (source == m_source)
        return;
    disconnect(m_sourceConnection);
    m_source = source;
    if (m_source)
        m_sourceConnection = connect(m_source, &Monitor::sampled, this, &Sparkline::reload);
    reload();
}

void Sparkline::setSeries(const QString &series) { m_series = series; reload(); }
void Sparkline::setMirrorSeries(const QString &series) { m_mirrorSeries = series; reload(); }

// The lists are implicitly shared, so taking them costs no copy.
void Sparkline::reload() {
    m_values = m_source && !m_series.isEmpty() ? m_source->history(m_series) : QList<qreal>();
    m_mirror = m_source && !m_mirrorSeries.isEmpty() ? m_source->history(m_mirrorSeries) : QList<qreal>();
    invalidate();
}

void Sparkline::setColor(const QColor &color) { m_color = color; invalidate(); }
void Sparkline::setMirrorColor(const QColor &color) { m_mirrorColor = color; invalidate(); }
void Sparkline::setMaximum(qreal maximum) { m_maximum = maximum; invalidate(); }
void Sparkline::setStep(qreal step) { m_step = std::max(0.5, step); invalidate(); }

void Sparkline::setGridColor(const QColor &color) {
    m_gridColor = color;
    m_gridDirty = true;
    update();
    emit changed();
}

qreal Sparkline::devicePixelRatio() const {
    return window() ? window()->effectiveDevicePixelRatio() : 1.0;
}

// How far the graph has slid, in whole device pixels.
int Sparkline::offsetFor(qreal phase) const {
    return int(std::round(phase * m_step * devicePixelRatio()));
}

// Enough samples to span the width, plus the one sliding in on the right
// and the one sliding out on the left.
int Sparkline::visibleCount() const {
    return int(std::ceil(width() / m_step)) + 2;
}

void Sparkline::setPhase(qreal phase) {
    phase = std::clamp(phase, 0.0, 1.0);
    if (phase == m_phase)
        return;
    m_phase = phase;
    // A frame costs the same whether the graph moved a whole pixel or a
    // hundredth of one, so only ask for one when it visibly moves (or while
    // a change of scale is easing in).
    if (offsetFor(phase) != m_shownOffset || m_scaleFrom != 1)
        update();
    emit phaseChanged();
}

void Sparkline::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        m_shapeDirty = m_gridDirty = true;
        update();
    }
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
    // visible jumps (which are then eased) instead of with every sample.
    const qreal magnitude = std::pow(10.0, std::floor(std::log10(peak)));
    for (qreal step : {1.0, 2.0, 5.0, 10.0})
        if (peak <= step * magnitude)
            return step * magnitude;
    return peak;
}

QSGNode *Sparkline::updatePaintNode(QSGNode *old, UpdatePaintNodeData *) {
    auto *root = static_cast<Root *>(old);
    if (!root)
        root = new Root;

    const float w = float(width()), h = float(height());
    const bool mirrored = !m_mirror.isEmpty();
    const float baseline = mirrored ? std::round(h / 2) : h;
    const float step = float(m_step);
    // One device pixel of feathering on each side of a line stands in for
    // multisampling: smooth edges at no extra cost per frame.
    const float feather = 1.0f / float(devicePixelRatio());

    if (m_gridDirty) {
        m_gridDirty = false;
        // Faint quarter lines give the eye a scale without competing with data.
        QList<QSGGeometry::ColoredPoint2D> grid;
        for (int i = 1; i < 4; ++i) {
            const float y = std::round(h * i / 4), y2 = y + feather;
            grid << vertex(0, y, m_gridColor, 1) << vertex(w, y, m_gridColor, 1) << vertex(0, y2, m_gridColor, 1)
                 << vertex(w, y, m_gridColor, 1) << vertex(w, y2, m_gridColor, 1) << vertex(0, y2, m_gridColor, 1);
        }
        fillNode(root->grid, grid);
    }

    if (m_shapeDirty) {
        m_shapeDirty = false;
        // Scale to what's on screen, not to a peak that scrolled off long ago.
        const int visible = visibleCount();
        const qreal maximum = m_maximum > 0 ? m_maximum
                                            : autoMaximum(m_values.mid(std::max<qsizetype>(0, m_values.size() - visible)),
                                                          m_mirror.mid(std::max<qsizetype>(0, m_mirror.size() - visible)));
        // When the scale changes, start drawn at the old scale and ease to
        // the new one, rather than snapping every line to a new height.
        m_scaleFrom = m_shownMaximum > 0 && maximum > 0 ? maximum / m_shownMaximum : 1;
        m_shownMaximum = maximum;

        const QList<qreal> *series[2] = {&m_values, &m_mirror};
        const QColor colors[2] = {m_color, m_mirrorColor};
        for (int s = 0; s < 2; ++s) {
            const QList<qreal> &values = *series[s];
            const int count = std::min<int>(values.size(), visibleCount());
            if (count < 2 || maximum <= 0 || (s == 1 && !mirrored)) {
                fillNode(root->fills[s], {});
                fillNode(root->lines[s], {});
                continue;
            }
            const int first = int(values.size()) - count;
            const float direction = s == 0 ? -1 : 1;
            const float height = s == 0 ? baseline : h - baseline;

            // Laid out one step to the right: at phase 0 the newest point is
            // just out of sight, at phase 1 it has reached the edge.
            QList<QPointF> points;
            QList<float> levels;
            for (int i = 0; i < count; ++i) {
                const float v = float(std::clamp(values[first + i] / maximum, 0.0, 1.0));
                points.append({w + step - (count - 1 - i) * step, baseline + direction * v * (height - 1)});
                levels.append(v);
            }

            // The area under the line, more opaque where the values are high.
            QList<QSGGeometry::ColoredPoint2D> fill;
            for (int i = 0; i < count; ++i) {
                fill.append(vertex(points[i].x(), points[i].y(), colors[s], 0.08 + 0.37 * levels[i]));
                fill.append(vertex(points[i].x(), baseline, colors[s], 0.05));
            }
            fillNode(root->fills[s], fill);

            // The line as quads: a solid core with a feathered edge each side.
            const float half = 0.75f;
            const float offsets[4] = {-(half + feather), -half, half, half + feather};
            const qreal alphas[4] = {0, 1, 1, 0};
            QList<QPointF> normals;
            for (int i = 0; i < count; ++i) {
                const QPointF a = points[std::max(0, i - 1)], b = points[std::min(count - 1, i + 1)];
                QPointF n(-(b.y() - a.y()), b.x() - a.x());
                const qreal length = std::hypot(n.x(), n.y());
                normals.append(length > 0 ? n / length : QPointF(0, -1));
            }
            QList<QSGGeometry::ColoredPoint2D> line;
            line.reserve((count - 1) * 18);
            for (int i = 0; i + 1 < count; ++i) {
                for (int lane = 0; lane < 3; ++lane) {
                    const QPointF a0 = points[i] + normals[i] * offsets[lane];
                    const QPointF a1 = points[i] + normals[i] * offsets[lane + 1];
                    const QPointF b0 = points[i + 1] + normals[i + 1] * offsets[lane];
                    const QPointF b1 = points[i + 1] + normals[i + 1] * offsets[lane + 1];
                    const auto v = [&](const QPointF &p, qreal alpha) { return vertex(p.x(), p.y(), colors[s], alpha); };
                    line << v(a0, alphas[lane]) << v(a1, alphas[lane + 1]) << v(b0, alphas[lane])
                         << v(a1, alphas[lane + 1]) << v(b1, alphas[lane + 1]) << v(b0, alphas[lane]);
                }
            }
            fillNode(root->lines[s], line);
        }
    }

    // Per frame: slide left by the phase in whole device pixels, so the
    // lines stay crisp, and ease any change of scale out.
    m_shownOffset = offsetFor(m_phase);
    const qreal eased = 1 - (1 - m_phase) * (1 - m_phase);
    const qreal scale = m_scaleFrom + (1 - m_scaleFrom) * eased;
    if (m_phase >= 1)
        m_scaleFrom = 1;
    QMatrix4x4 matrix;
    matrix.translate(float(-m_shownOffset / devicePixelRatio()), baseline);
    matrix.scale(1, float(scale));
    matrix.translate(0, -baseline);
    root->motion->setMatrix(matrix);
    return root;
}
