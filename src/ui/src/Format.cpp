#include "comp/ui/Format.h"

#include <algorithm>
#include <cmath>

namespace comp::ui {

QString formatTimecode(double seconds, double fps) {
    const int rate = std::max(1, static_cast<int>(std::round(fps)));
    const int totalFrames = static_cast<int>(std::round(seconds * static_cast<double>(rate)));
    const int f = totalFrames % rate;
    const int totalSeconds = totalFrames / rate;
    return QStringLiteral("%1:%2:%3")
        .arg(totalSeconds / 60, 2, 10, QLatin1Char('0'))
        .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'))
        .arg(f, 2, 10, QLatin1Char('0'));
}

QString formatPropertyValue(const core::Property& prop, double seconds,
                            const core::TimeContext& ctx) {
    const core::Value v = prop.evaluate(seconds, ctx);

    QString out;
    for (int i = 0; i < v.count; ++i) {
        if (i > 0) {
            out += QStringLiteral(", ");
        }
        out += QString::number(v.c[static_cast<std::size_t>(i)], 'f', 1);
    }
    // The suffix goes once at the end, not per component: "100.0, 100.0%".
    out += QString::fromUtf8(core::unitSuffix(prop.unit));
    return out;
}

}  // namespace comp::ui
