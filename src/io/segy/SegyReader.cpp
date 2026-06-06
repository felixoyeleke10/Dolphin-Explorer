// SegyReader.cpp — open, close, metadata, and sample-format helpers.
//   Index building   → SegyReader.Index.cpp
//   Trace decoding   → SegyReader.Decode.cpp
#include "io/segy/SegyReader_p.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace dolphin::io {

using namespace detail_segy;

SegyReader::SegyReader()  = default;
SegyReader::~SegyReader() { close(); }

// -- open -----------------------------------------------------------------------

namespace {

// Return bytes-per-sample for a given format code (same table as bytesPerSample).
static uint32_t bpsForFmt(int fmt)
{
    switch (fmt) {
    case 1:  return 4;
    case 2:  return 4;
    case 3:  return 2;
    case 5:  return 4;
    case 6:  return 8;
    case 7:  return 3;
    case 8:  return 1;
    case 9:  return 8;
    case 10: return 4;
    case 11: return 2;
    case 12: return 1;
    default: return 4;
    }
}

struct ProbeScore {
    int         score;   // 0–10
    std::string rationale;
};

// Score a (little_endian, sample_format) interpretation against the first
// trace at data_offset plus a chain check against the next trace.
static ProbeScore probeInterpretation(FILE* file, uint64_t data_offset,
                                       uint64_t file_size, bool le, int fmt,
                                       uint16_t file_si, uint32_t file_ns)
{
    ProbeScore r{0, ""};
    if (data_offset + kTraceHdrBytes > file_size) return r;

    uint8_t thdr[kTraceHdrBytes];
    if (!detail::seekAbs(file, data_offset)) return r;
    if (std::fread(thdr, kTraceHdrBytes, 1, file) != 1) return r;

    r.score = scoreTraceHeader(thdr, le, file_ns, file_si);

    // Chain check: does the declared next-trace offset also hold a plausible header?
    const uint32_t ns    = static_cast<uint32_t>(rdUint16(&thdr[114], le));
    const uint32_t eff_ns = (ns > 0) ? ns : file_ns;
    if (eff_ns > 0) {
        const uint64_t next_off = data_offset + kTraceHdrBytes
                                + static_cast<uint64_t>(eff_ns) * bpsForFmt(fmt);
        if (next_off + kTraceHdrBytes <= file_size) {
            uint8_t thdr2[kTraceHdrBytes];
            if (detail::seekAbs(file, next_off) &&
                    std::fread(thdr2, kTraceHdrBytes, 1, file) == 1) {
                const int chain = scoreTraceHeader(thdr2, le, file_ns, file_si);
                if (chain >= 3) r.score += 2;
            }
        }
    }
    return r;
}

} // namespace

bool SegyReader::open(const std::string& path)
{
    close();

    m_file = std::fopen(path.c_str(), "rb");
    if (!m_file) return false;

    if (!detail::seekEnd(m_file) || !detail::tellFile(m_file, m_fileSize)) {
        close(); return false;
    }
    if (m_fileSize < kMinFileBytes) { close(); return false; }

    // -- Text header (3200 bytes at offset 0) ---------------------------------
    {
        uint8_t txthdr[kTextHeaderBytes];
        if (detail::seekAbs(m_file, 0) &&
                std::fread(txthdr, kTextHeaderBytes, 1, m_file) == 1) {
            m_textHeaderDecoded = decodeTextHeader(txthdr, kTextHeaderBytes);
        }
    }

    // Binary file header starts immediately after the 3200-byte text header.
    if (!detail::seekAbs(m_file, kTextHeaderBytes)) { close(); return false; }

    uint8_t binhdr[kBinHeaderBytes];
    if (std::fread(binhdr, kBinHeaderBytes, 1, m_file) != 1) { close(); return false; }

    // -- SEG-Y revision --------------------------------------------------------
    // Binary header offset 300–301: major.minor revision (Rev 1 = 0x01 0x00).
    m_segyRevision = binhdr[300];

    // -- Endian + format detection ---------------------------------------------
    // Bytes 25-26 of the binary header carry the sample format code.
    const int16_t fmt_be = beInt16(&binhdr[24]);
    const int16_t fmt_le = leInt16(&binhdr[24]);

    const bool be_valid = isKnownSampleFormat(static_cast<int>(fmt_be));
    const bool le_valid = isKnownSampleFormat(static_cast<int>(fmt_le));

    if (fmt_be == 0 && fmt_le == 0) {
        // Format code 0: Rev 0 file — assume big-endian IEEE float.
        m_littleEndian          = false;
        m_sampleFormat          = 5;
        m_sampleFormatDefaulted = true;
    } else if (!be_valid && !le_valid) {
        addDiagnostic(core::ImportDiagnosticSeverity::Error,
             core::ImportDiagnosticCode::UnsupportedSampleEncoding,
             "Unrecognised SEG-Y sample format code "
             + std::to_string(static_cast<int>(fmt_be))
             + " (big-endian) / "
             + std::to_string(static_cast<int>(fmt_le))
             + " (little-endian); file cannot be read");
        close(); return false;
    } else if (be_valid && !le_valid) {
        m_littleEndian = false;
        m_sampleFormat = static_cast<int>(fmt_be);
    } else if (!be_valid && le_valid) {
        m_littleEndian = true;
        m_sampleFormat = static_cast<int>(fmt_le);
    } else {
        // Both format codes are valid — use probe to pick the better interpretation.
        m_littleEndian = false;
        m_sampleFormat = static_cast<int>(fmt_be);
    }

    // -- Extended text headers -------------------------------------------------
    const int16_t  ext_raw          = rdInt16(&binhdr[60], m_littleEndian);
    const uint64_t after_fixed_hdrs = kTextHeaderBytes + kBinHeaderBytes;
    const uint64_t remaining        = (m_fileSize > after_fixed_hdrs)
                                    ? (m_fileSize - after_fixed_hdrs) : 0;
    const uint32_t max_from_size    = static_cast<uint32_t>(remaining / 3200ULL);

    uint32_t num_ext;
    if (ext_raw == -1) {
        static const char     k_ascii[]  = "((SEG: EndText))";
        static const uint8_t  k_ebcdic[] = { 0x4D,0x4D,0xE2,0xC5,0xC7,0x7A,0x40 };
        num_ext = 0;
        const uint32_t scan_limit = std::min(kMaxExtendedHdrs, max_from_size);
        for (uint32_t n = 1; n <= scan_limit; ++n) {
            const uint64_t hoff = after_fixed_hdrs + static_cast<uint64_t>(n - 1) * 3200ULL;
            uint8_t buf[16];
            if (!detail::seekAbs(m_file, hoff)) break;
            if (std::fread(buf, sizeof(buf), 1, m_file) != 1) break;
            if (std::memcmp(buf, k_ascii,  16) == 0 ||
                std::memcmp(buf, k_ebcdic,  7) == 0) { num_ext = n; break; }
        }
        if (num_ext == 0 && scan_limit > 0) {
            num_ext = scan_limit;
            m_extHdrScanFailed = true;
        }
    } else {
        num_ext = static_cast<uint32_t>(std::max(0, static_cast<int>(ext_raw)));
    }
    const uint32_t capped_ext = std::min({num_ext, kMaxExtendedHdrs, max_from_size});

    m_dataOffset = after_fixed_hdrs + static_cast<uint64_t>(capped_ext) * 3200ULL;
    if (m_dataOffset >= m_fileSize) { close(); return false; }
    if (m_dataOffset + kTraceHdrBytes > m_fileSize) { close(); return false; }

    // -- Resolve si / ns with chosen endian -------------------------------------
    m_fileSampleInterval  = rdUint16(&binhdr[16], m_littleEndian);
    m_fileSamplesPerTrace = static_cast<uint32_t>(rdUint16(&binhdr[20], m_littleEndian));

    // -- Probe phase -----------------------------------------------------------
    // Score each plausible (endian, format) combination against the actual first
    // trace(s) at m_dataOffset and pick the best interpretation.
    {
        struct Candidate { bool le; int fmt; };
        std::vector<Candidate> candidates;
        if (m_sampleFormatDefaulted) {
            // Format 0: probe the most common formats in decreasing frequency.
            for (int f : { 5, 1, 3, 2, 8, 6 })
                for (bool le : { false, true })
                    candidates.push_back({le, f});
        } else if (be_valid && le_valid) {
            candidates.push_back({false, static_cast<int>(fmt_be)});
            candidates.push_back({true,  static_cast<int>(fmt_le)});
        } else {
            // Unambiguous — still probe as validation.
            candidates.push_back({m_littleEndian, m_sampleFormat});
        }

        int best_score = -1;
        bool best_le   = m_littleEndian;
        int  best_fmt  = m_sampleFormat;

        for (const auto& c : candidates) {
            const uint16_t si_hint = rdUint16(&binhdr[16], c.le);
            const uint32_t ns_hint = static_cast<uint32_t>(rdUint16(&binhdr[20], c.le));
            auto ps = probeInterpretation(m_file, m_dataOffset, m_fileSize,
                                          c.le, c.fmt, si_hint, ns_hint);
            if (ps.score > best_score) {
                best_score = ps.score;
                best_le    = c.le;
                best_fmt   = c.fmt;
            }
        }
        const bool changed = (best_le != m_littleEndian || best_fmt != m_sampleFormat);
        m_littleEndian  = best_le;
        m_sampleFormat  = best_fmt;
        m_probeScore    = static_cast<uint8_t>(std::min(best_score, 10));
        m_endianFromProbe = changed || m_sampleFormatDefaulted || (be_valid && le_valid);

        // Re-read si/ns with the probe-confirmed endian.
        m_fileSampleInterval  = rdUint16(&binhdr[16], m_littleEndian);
        m_fileSamplesPerTrace = static_cast<uint32_t>(rdUint16(&binhdr[20], m_littleEndian));
    }

    m_path = path;
    m_meta = {};
    m_meta.format_name                = "SEG-Y";
    m_meta.text_header                = m_textHeaderDecoded;
    m_meta.segy_revision              = m_segyRevision;
    m_meta.binary_sample_interval_us  = m_fileSampleInterval;
    m_meta.binary_samples_per_trace   = m_fileSamplesPerTrace;

    return true;
}

// -- close ----------------------------------------------------------------------

void SegyReader::close()
{
    if (m_file) { std::fclose(m_file); m_file = nullptr; }
    m_path.clear();
    m_meta                    = {};
    m_fileSize                = 0;
    m_dataOffset              = 0;
    m_sampleFormat            = 5;
    m_fileSampleInterval      = 0;
    m_fileSamplesPerTrace     = 0;
    m_littleEndian            = false;
    m_sampleFormatDefaulted   = false;
    m_extHdrScanFailed        = false;
    m_endianFromProbe         = false;
    m_textHeaderDecoded.clear();
    m_segyRevision            = 0;
    m_probeScore              = 0;
}

// -- metadata ------------------------------------------------------------------

FormatMeta SegyReader::metadata() { return m_meta; }

// -- bytesPerSample ------------------------------------------------------------

uint32_t SegyReader::bytesPerSample() const
{
    return bpsForFmt(m_sampleFormat);
}

} // namespace dolphin::io
