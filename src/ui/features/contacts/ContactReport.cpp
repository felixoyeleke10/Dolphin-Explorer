#include "ui/features/contacts/ContactReport.h"
#include "ui/shared/CoordFormat.h"
#include "util/ZipWriter.h"
#include "app/project/Project.h"
#include "app/layers/DataLayer.h"
#include "app/layers/LayerUtils.h"
#include "core/SpatialRef.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfWriter>
#include <QTextDocument>

namespace dolphin::ui {
namespace {

// One report row, derived once and shared by the PDF + DOCX writers.
struct Row {
    QString label, sensor, source, cls, conf, position, depth, range;
};

QString modalityTag(app::Modality m)
{
    switch (m) {
    case app::Modality::Sidescan:     return QStringLiteral("SSS");
    case app::Modality::SubBottom:    return QStringLiteral("SBP");
    case app::Modality::Magnetometer: return QStringLiteral("MAG");
    case app::Modality::Multibeam:    return QStringLiteral("MBES");
    case app::Modality::Raster:       return QStringLiteral("RAS");
    default:                          return QStringLiteral("—");
    }
}

QString confidenceLabel(core::Confidence c)
{
    switch (c) {
    case core::Confidence::Probable: return QObject::tr("Probable");
    case core::Confidence::Certain:  return QObject::tr("Certain");
    default:                         return QObject::tr("Possible");
    }
}

std::vector<Row> buildRows(const std::vector<core::Contact>& contacts, app::Project* project)
{
    std::vector<Row> rows;
    rows.reserve(contacts.size());
    for (const auto& c : contacts) {
        app::DataLayer* layer = (project && !c.line_id.empty()) ? project->findLayer(c.line_id) : nullptr;
        const bool proj = core::spatialRefIsProjected(c.spatial_ref);
        Row r;
        r.label    = QString::fromStdString(c.label);
        r.sensor   = layer ? modalityTag(layer->modality) : QObject::tr("Map");
        r.source   = layer ? QString::fromStdString(layer->label)
                           : (c.line_id.empty() ? QObject::tr("(map pick)")
                                                : QString::fromStdString(c.line_id));
        r.cls      = QString::fromStdString(c.classification);
        r.conf     = confidenceLabel(c.confidence);
        r.position = formatPosition(c.lat, c.lon, proj);
        r.depth    = c.depth_m > 0.f ? QString::number(c.depth_m, 'f', 1) : QStringLiteral("—");
        r.range    = c.range_m > 0.f ? QString::number(c.range_m, 'f', 1) : QStringLiteral("—");
        rows.push_back(std::move(r));
    }
    return rows;
}

const char* kHeaders[] = { "Label", "Sensor", "Line / Source", "Class",
                           "Confidence", "Position", "Depth (m)", "Range (m)" };

QString metaLine(const QString& title, app::Project* project, int n)
{
    const QString proj = project ? QString::fromStdString(project->name()) : QObject::tr("(no project)");
    return QObject::tr("Project: %1   ·   Generated: %2   ·   %3 contact(s)")
        .arg(proj, QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm"), QString::number(n));
}

// -- HTML (PDF) ---------------------------------------------------------------

QString rowsToHtml(const QString& title, const QString& meta, const std::vector<Row>& rows)
{
    auto esc = [](const QString& s) { return s.toHtmlEscaped(); };
    QString h;
    h += QStringLiteral("<html><body style='font-family:sans-serif;'>");
    h += QStringLiteral("<h2 style='margin-bottom:2px;'>%1</h2>").arg(esc(title));
    h += QStringLiteral("<p style='color:#555;font-size:9pt;margin-top:0;'>%1</p>").arg(esc(meta));
    h += QStringLiteral("<table border='1' cellspacing='0' cellpadding='4' width='100%' "
                        "style='border-collapse:collapse;font-size:9pt;'>");
    h += QStringLiteral("<tr style='background:#eaeaea;font-weight:bold;'>");
    for (const char* col : kHeaders)
        h += QStringLiteral("<td>%1</td>").arg(esc(QObject::tr(col)));
    h += QStringLiteral("</tr>");
    for (const auto& r : rows) {
        h += QStringLiteral("<tr>");
        for (const QString& cell : { r.label, r.sensor, r.source, r.cls, r.conf, r.position, r.depth, r.range })
            h += QStringLiteral("<td>%1</td>").arg(esc(cell));
        h += QStringLiteral("</tr>");
    }
    h += QStringLiteral("</table></body></html>");
    return h;
}

// -- OOXML (DOCX) -------------------------------------------------------------

QString xml(const QString& s)
{
    QString o; o.reserve(s.size());
    for (QChar c : s) {
        const ushort u = c.unicode();
        // Drop characters invalid in XML 1.0 (control chars other than tab/LF/CR);
        // an unescaped one would make the .docx unreadable in Word.
        if (u < 0x20 && u != 0x09 && u != 0x0A && u != 0x0D) continue;
        switch (u) {
        case '&':  o += QStringLiteral("&amp;");  break;
        case '<':  o += QStringLiteral("&lt;");   break;
        case '>':  o += QStringLiteral("&gt;");   break;
        case '"':  o += QStringLiteral("&quot;"); break;
        default:   o += c;                        break;
        }
    }
    return o;
}

QString wText(const QString& s)   // a run with preserved whitespace
{
    return QStringLiteral("<w:r><w:t xml:space=\"preserve\">%1</w:t></w:r>").arg(xml(s));
}

QString wPara(const QString& s, bool bold = false, int half_pt = 0)
{
    QString rpr;
    if (bold || half_pt) {
        rpr = QStringLiteral("<w:rPr>");
        if (bold)    rpr += QStringLiteral("<w:b/>");
        if (half_pt) rpr += QStringLiteral("<w:sz w:val=\"%1\"/>").arg(half_pt);
        rpr += QStringLiteral("</w:rPr>");
    }
    return QStringLiteral("<w:p><w:r>%1<w:t xml:space=\"preserve\">%2</w:t></w:r></w:p>")
        .arg(rpr, xml(s));
}

QString wCell(const QString& text, bool header)
{
    QString rpr = header ? QStringLiteral("<w:rPr><w:b/></w:rPr>") : QString();
    QString shd = header ? QStringLiteral("<w:shd w:val=\"clear\" w:fill=\"EAEAEA\"/>") : QString();
    return QStringLiteral("<w:tc><w:tcPr>%1</w:tcPr>"
                          "<w:p><w:r>%2<w:t xml:space=\"preserve\">%3</w:t></w:r></w:p></w:tc>")
        .arg(shd, rpr, xml(text));
}

QString docxDocumentXml(const QString& title, const QString& meta, const std::vector<Row>& rows)
{
    QString body;
    body += wPara(title, /*bold=*/true, /*half_pt=*/32);   // 16pt
    body += wPara(meta);

    // Table: single borders all round.
    QString tbl = QStringLiteral(
        "<w:tbl><w:tblPr><w:tblW w:w=\"0\" w:type=\"auto\"/>"
        "<w:tblBorders>"
        "<w:top w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"888888\"/>"
        "<w:left w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"888888\"/>"
        "<w:bottom w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"888888\"/>"
        "<w:right w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"888888\"/>"
        "<w:insideH w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"CCCCCC\"/>"
        "<w:insideV w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"CCCCCC\"/>"
        "</w:tblBorders></w:tblPr>");

    tbl += QStringLiteral("<w:tr>");
    for (const char* col : kHeaders) tbl += wCell(QObject::tr(col), /*header=*/true);
    tbl += QStringLiteral("</w:tr>");
    for (const auto& r : rows) {
        tbl += QStringLiteral("<w:tr>");
        for (const QString& cell : { r.label, r.sensor, r.source, r.cls, r.conf, r.position, r.depth, r.range })
            tbl += wCell(cell, false);
        tbl += QStringLiteral("</w:tr>");
    }
    tbl += QStringLiteral("</w:tbl>");
    body += tbl;

    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
        "<w:body>%1<w:sectPr><w:pgSz w:w=\"16838\" w:h=\"11906\" w:orient=\"landscape\"/></w:sectPr>"
        "</w:body></w:document>").arg(body);
}

} // namespace

namespace ContactReport {

bool writeCsv(const QString& path, const QString& /*title*/,
              const std::vector<core::Contact>& contacts, app::Project* project)
{
    auto esc = [](const QString& s) -> QString {
        if (s.contains(QLatin1Char(',')) || s.contains(QLatin1Char('"')) || s.contains(QLatin1Char('\n'))) {
            QString t = s; t.replace(QLatin1Char('"'), QStringLiteral("\"\""));
            return QLatin1Char('"') + t + QLatin1Char('"');
        }
        return s;
    };

    QString out;
    QStringList hdr;
    for (const char* col : kHeaders) hdr << esc(QObject::tr(col));
    out += hdr.join(QLatin1Char(',')) + QLatin1Char('\n');

    for (const auto& r : buildRows(contacts, project)) {
        const QStringList f{ r.label, r.sensor, r.source, r.cls, r.conf, r.position, r.depth, r.range };
        QStringList cells;
        for (const QString& cell : f) cells << esc(cell);
        out += cells.join(QLatin1Char(',')) + QLatin1Char('\n');
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    file.write("\xEF\xBB\xBF");                 // UTF-8 BOM so Excel reads Unicode
    file.write(out.toUtf8());
    return true;
}

bool writePdf(const QString& path, const QString& title,
              const std::vector<core::Contact>& contacts, app::Project* project)
{
    const auto rows = buildRows(contacts, project);
    const QString html = rowsToHtml(title, metaLine(title, project, static_cast<int>(rows.size())), rows);

    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageOrientation(QPageLayout::Landscape);
    writer.setPageMargins(QMarginsF(12, 12, 12, 12), QPageLayout::Millimeter);

    {
        QTextDocument doc;
        doc.setHtml(html);
        doc.print(&writer);   // QPdfWriter is a QPagedPaintDevice → paginates automatically
    }   // writer flushes on destruction
    QFileInfo fi(path);
    return fi.exists() && fi.size() > 0;   // don't report success on an unwritable path
}

bool writeDocx(const QString& path, const QString& title,
               const std::vector<core::Contact>& contacts, app::Project* project)
{
    const auto rows = buildRows(contacts, project);
    const QString doc_xml = docxDocumentXml(
        title, metaLine(title, project, static_cast<int>(rows.size())), rows);

    static const char* kContentTypes =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/word/document.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "</Types>";

    static const char* kRels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
        "Target=\"word/document.xml\"/></Relationships>";

    util::ZipWriter zip;
    zip.addFile("[Content_Types].xml", kContentTypes);
    zip.addFile("_rels/.rels", kRels);
    zip.addFile("word/document.xml", doc_xml.toStdString());
    return zip.writeToFile(path.toStdString());
}

} // namespace ContactReport

} // namespace dolphin::ui
