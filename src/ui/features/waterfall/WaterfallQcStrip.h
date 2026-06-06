#pragma once
#include <QWidget>
#include <vector>
#include <utility>

namespace dolphin::ui {

// -----------------------------------------------------------------------------
//  WaterfallQcStrip — narrow vertical QC progress bar beside the waterfall.
//
//  The full widget height represents the complete survey line.
//  Segments that the user has scrolled through are painted green; unviewed
//  rows remain dark.  The current viewport is marked with bright edge lines.
//
//  All state is set via setData(); the widget never reads from the data model
//  directly — WaterfallWindow owns the tracking logic.
// -----------------------------------------------------------------------------

class WaterfallQcStrip : public QWidget {
    Q_OBJECT
public:
    explicit WaterfallQcStrip(QWidget* parent = nullptr);

    // Update display state.  Call after every scroll event or layer load.
    //   total_rows     — estimated number of rows in the full survey
    //   viewed_ranges  — sorted, merged [first, last) absolute row ranges
    //   cur_abs_first  — absolute index of the first visible row
    //   cur_visible    — number of rows currently on screen
    //   fraction       — pre-computed fraction in [0, 1]
    void setData(int                                    total_rows,
                 const std::vector<std::pair<int,int>>& viewed_ranges,
                 int                                    cur_abs_first,
                 int                                    cur_visible,
                 float                                  fraction);

    void reset();   // clear to zero state (called on layer change)

protected:
    void paintEvent(QPaintEvent*) override;

private:
    int   m_total_rows  = 0;
    int   m_cur_first   = 0;
    int   m_cur_visible = 0;
    float m_fraction    = 0.f;
    std::vector<std::pair<int,int>> m_viewed;
};

} // namespace dolphin::ui
