#pragma once

#include <cstdint>

namespace dolphin::ui {

// Monotonic CPU->GPU synchronization contract. Data changes subsume geometry;
// geometry-only edits never force amplitude re-upload. Acknowledgement records
// the exact generation uploaded, so a later invalidation cannot be lost.
class WaterfallRenderGeneration {
public:
    void dataChanged() noexcept { ++m_data; ++m_geometry; }
    void geometryChanged() noexcept { ++m_geometry; }
    void gpuReset() noexcept { m_uploaded_data = 0; m_uploaded_geometry = 0; }

    bool needsDataUpload() const noexcept { return m_uploaded_data != m_data; }
    bool needsGeometryUpload() const noexcept {
        return m_uploaded_geometry != m_geometry;
    }
    void acknowledgeDataUpload() noexcept {
        m_uploaded_data = m_data;
        m_uploaded_geometry = m_geometry;
    }
    void acknowledgeGeometryUpload() noexcept {
        m_uploaded_geometry = m_geometry;
    }

private:
    std::uint64_t m_data = 1;
    std::uint64_t m_geometry = 1;
    std::uint64_t m_uploaded_data = 0;
    std::uint64_t m_uploaded_geometry = 0;
};

} // namespace dolphin::ui
