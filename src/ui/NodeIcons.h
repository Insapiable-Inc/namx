#pragma once

#include <QColor>
#include <QRectF>
#include <QString>

class QPainter;

namespace namx
{

// A line-art icon per node category (the card's icon box and the parameter panel's
// header): Amp, Cab, Drive, Dynamics, Filter, Pitch, Modulation, Delay, Reverb,
// Utility, IO; Merge has its own (type "merge"). Drawn in `color` inside `r`.
void paint_node_icon(QPainter& p, const QRectF& r, const QString& type_id, const QString& category,
                     const QColor& color);

} // namespace namx
