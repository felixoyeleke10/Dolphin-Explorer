#pragma once
#include <QPointer>
#include <QWidget>
#include <QString>
#include <string>
#include <vector>

class QVariantAnimation;
class QPropertyAnimation;

namespace dolphin::ui {

// Single glass panel shown bottom-centre of the map viewport during import.
//
// Files are dispatched sequentially by ImportController; this panel shows the
// active file prominently (filename, size, progress bar, CRS, frequency) and
// keeps completed files as compact summary rows below it.
//
// API:
//   setQueueTotal(n)     — call whenever the batch total grows (e.g. 8 files dropped)
//   addJob(...)          — file has started parsing
//   updateJob(...)       — progress [0..100]
//   finishJob(...)       — parsing done; pass metadata for the summary row
//   failJob(...)         — parsing failed
//   reanchor()           — call from parent resizeEvent
class ImportProgressOverlay : public QWidget {
    Q_OBJECT
public:
    explicit ImportProgressOverlay(QWidget* parent = nullptr);

    void setQueueTotal(int n);

    void addJob(const std::string& layer_id,
                const QString&     filename,
                const QString&     format,
                float              size_mb);

    void updateJob(const std::string& layer_id, int percent);

    void finishJob(const std::string& layer_id,
                   int                artifact_count,
                   float              freq_khz,
                   const QString&     coord_sys);

    void failJob(const std::string& layer_id, const QString& error);

    void reanchor();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    enum class RowState { Parsing, Success, Failed };

    struct JobRow {
        std::string        layer_id;
        QString            display_name;
        QString            format;
        float              size_mb        = 0.f;
        QString            status_text;
        RowState           state          = RowState::Parsing;
        float              fill           = 0.f;
        QPointer<QVariantAnimation> fill_anim;
        // populated on finishJob:
        int                artifact_count = 0;
        float              freq_khz       = 0.f;
        QString            coord_sys;
    };

    JobRow* findRow(const std::string& layer_id);
    int     panelHeight() const;
    void    relayout();
    void    checkAllDone();
    void    animateFillTo(JobRow& row, float target);
    void    fadeIn();
    void    fadeOut();

    static constexpr int    kPanelW      = 560;
    static constexpr int    kHeaderH     = 48;
    static constexpr int    kActiveRowH  = 100;   // current file being parsed
    static constexpr int    kCompactRowH = 44;    // finished / failed rows
    static constexpr int    kMarginB     = 28;
    static constexpr double kRadius      = 14.0;
    static constexpr double kPadH        = 16.0;
    static constexpr double kIconW       = 30.0;
    static constexpr double kIconGap     = 10.0;

    std::vector<JobRow>  m_rows;
    int                  m_queue_total = 0;
    bool                 m_dismissing  = false;
    QPointer<QPropertyAnimation> m_fade_anim;
};

} // namespace dolphin::ui
