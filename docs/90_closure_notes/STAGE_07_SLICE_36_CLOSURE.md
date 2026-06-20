# Stage 07 Slice 36 — Contact report generator (PDF + Word .docx)

User request: the Contact Manager needs a report generator, both PDF and Word. User
chose **real .docx** (accepting a new capability) over RTF / HTML-.doc.

## Owned ZIP writer (no third-party blob)
`util/ZipWriter.{h,cpp}` — a minimal, store-only (uncompressed) ZIP archive writer with a
table-driven CRC-32. Std-only (no Qt, no dependency); buffers the archive and writes once
— sized for documents, not bulk data. Word opens uncompressed `.docx` fine. Chosen over
vendoring miniz (a large blob to maintain) or Qt's private `QZipWriter` (fragile private
API); this is small, owned, and to-spec (PKZIP APPNOTE).

## Report generator
`ui/features/contacts/ContactReport.{h,cpp}` — one shared row model → two writers:
- **PDF**: builds an HTML table → `QTextDocument` → `QPdfWriter` (QtGui; A4 landscape,
  auto-paginated). No PrintSupport module needed.
- **DOCX**: builds OOXML (`[Content_Types].xml`, `_rels/.rels`, `word/document.xml` with a
  bordered `w:tbl`) and packages it via `util::ZipWriter`.
- Columns: Label · Sensor · Line/Source · Class · Confidence · Position · Depth · Range,
  with a title + "Project / Generated / N contacts" header line. Sensor/line resolved from
  each contact's `line_id` → DataLayer.

## Wiring
- Contact Manager command bar gains a **Report** action (`report.svg`).
- `generateReport()` collects the **visible** contacts (current folder + search, whichever
  view is active), titles the report from the current folder (breadcrumb), then a save
  dialog with **PDF / Word** filters picks the format and calls the matching writer; status
  bar confirms the saved path.

## Build
- `util/ZipWriter.cpp` → `dolphin-util`; `ContactReport.cpp` → `dolphin-ui-contacts`
  (now also links `dolphin-util`). Full build + link clean.

## Runtime verification (manual)
- Contact Manager → **Report** → save as `.pdf` → opens, table paginates.
- Save as `.docx` → opens in Word (no warnings), editable, table intact.
- Report respects the current folder filter + search (reports what's listed).

## Possible follow-ups
- Per-sensor summary counts / a cover section; embed a small map thumbnail.
- Hook the dormant app-wide Export ▸ PDF (`onExportPdf`) into the same generator.
