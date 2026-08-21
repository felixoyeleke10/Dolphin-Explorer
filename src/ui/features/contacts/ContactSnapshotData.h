#pragma once

#include <QPixmap>

namespace dolphin::ui {

// A source image and the physical geometry of its pixels. Keeping these in one
// value prevents a calibration derived for one render/zoom from being applied
// to a different persisted snapshot.
struct ContactSnapshotData {
    QPixmap pixmap;
    float across_m_per_px = 0.f;
    float along_m_per_px  = 0.f;
    float altitude_m      = 0.f;

    bool calibrated() const {
        return !pixmap.isNull() && across_m_per_px > 0.f && along_m_per_px > 0.f;
    }
};

} // namespace dolphin::ui
