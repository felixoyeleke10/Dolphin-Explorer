// test_segy_reader.cpp — SegyReader unit tests using synthetic in-memory files.
// Each test builds a minimal valid SEG-Y byte sequence, writes it to a temp
// file, exercises the reader, and asserts expected results.
#include "io/segy/SegyReader.h"
#include "core/ImportDiagnostic.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace core = dolphin::core;

static void removeTempFile(const std::string& path)
{
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

static uint32_t floatToIbm(float v);

// ── Minimal SEG-Y builder ─────────────────────────────────────────────────────

struct SegyBuilder {
    std::vector<uint8_t> data;

    void beUint16(uint16_t v) { data.push_back(v >> 8); data.push_back(v & 0xFF); }
    void beInt16(int16_t  v) { beUint16(static_cast<uint16_t>(v)); }
    void beInt32(int32_t  v) {
        data.push_back((v >> 24) & 0xFF); data.push_back((v >> 16) & 0xFF);
        data.push_back((v >>  8) & 0xFF); data.push_back( v        & 0xFF);
    }
    void leUint16(uint16_t v) { data.push_back(v & 0xFF); data.push_back(v >> 8); }
    void leInt16(int16_t  v)  { leUint16(static_cast<uint16_t>(v)); }
    void leInt32(int32_t  v)  {
        data.push_back( v        & 0xFF); data.push_back((v >>  8) & 0xFF);
        data.push_back((v >> 16) & 0xFF); data.push_back((v >> 24) & 0xFF);
    }
    void zeros(size_t n) { data.insert(data.end(), n, 0x00); }
    void pad_to(size_t target) {
        if (data.size() < target) zeros(target - data.size());
    }
    size_t size() const { return data.size(); }

    // Write as big-endian IEEE float.
    void bef32(float v) {
        uint32_t u; std::memcpy(&u, &v, 4);
        data.push_back((u >> 24) & 0xFF); data.push_back((u >> 16) & 0xFF);
        data.push_back((u >>  8) & 0xFF); data.push_back( u        & 0xFF);
    }
    void lef32(float v) {
        uint32_t u; std::memcpy(&u, &v, 4);
        data.push_back( u        & 0xFF); data.push_back((u >>  8) & 0xFF);
        data.push_back((u >> 16) & 0xFF); data.push_back((u >> 24) & 0xFF);
    }
};

// Build a complete minimal SEG-Y file.
// Parameters control byte order, sample format, and per-trace content.
struct TraceSpec {
    double  lon         = 0.0;   // source X (geographical or projected)
    double  lat         = 0.0;   // source Y
    double  grp_lon     = 0.0;   // receiver group X (bytes 81-84, fallback when source = 0)
    double  grp_lat     = 0.0;   // receiver group Y (bytes 85-88)
    int16_t coord_units = 4;     // 4 = decimal degrees, 1 = metres
    int16_t coord_scale = -1000; // coordinate scalar (neg = divide)
    int16_t ident_code  = 1;     // 1 = seismic, 2 = dead, 3 = dummy
    int16_t delay_ms    = 0;
    int16_t year        = 2023;
    int16_t doy         = 100;
    int16_t hour        = 12;
    int16_t minute      = 0;
    int16_t second      = 0;
    std::vector<float> samples = { 0.f, 0.1f, 0.5f, 0.8f, 0.3f, 0.1f, 0.f };
};

std::vector<uint8_t> buildSegy(int sample_format,
                                bool little_endian,
                                const std::vector<TraceSpec>& traces,
                                int extended_hdrs = 0,
                                const std::string& text_insert = "")
{
    SegyBuilder b;

    // ── Text header (3200 bytes, ASCII 'C' lines) ──────────────────────────────
    std::string text(3200, ' ');
    text[0] = 'C'; text[1] = '1';  // first line marker
    if (!text_insert.empty())
        text.replace(4, std::min(text_insert.size(), size_t{80}), text_insert);
    for (char c : text) b.data.push_back(static_cast<uint8_t>(c));

    // ── Binary header (400 bytes) ──────────────────────────────────────────────
    const size_t bin_start = b.size();  // = 3200
    b.zeros(400);
    // Bytes 17-18 of binary header (offset 16 from bin_start): sample interval µs
    const uint16_t si_us = 1000;  // 1 ms = 1000 Hz
    if (little_endian) {
        b.data[bin_start + 16] = si_us & 0xFF;
        b.data[bin_start + 17] = si_us >> 8;
    } else {
        b.data[bin_start + 16] = si_us >> 8;
        b.data[bin_start + 17] = si_us & 0xFF;
    }
    // Bytes 21-22 (offset 20): samples per trace
    const uint16_t spt = traces.empty() ? 0
        : static_cast<uint16_t>(traces[0].samples.size());
    if (little_endian) {
        b.data[bin_start + 20] = spt & 0xFF;
        b.data[bin_start + 21] = spt >> 8;
    } else {
        b.data[bin_start + 20] = spt >> 8;
        b.data[bin_start + 21] = spt & 0xFF;
    }
    // Bytes 25-26 (offset 24): sample format
    const auto sfmt = static_cast<uint16_t>(sample_format);
    if (little_endian) {
        b.data[bin_start + 24] = sfmt & 0xFF;
        b.data[bin_start + 25] = sfmt >> 8;
    } else {
        b.data[bin_start + 24] = sfmt >> 8;
        b.data[bin_start + 25] = sfmt & 0xFF;
    }
    // Bytes 61-62 (offset 60): number of extended headers
    const auto ext = static_cast<uint16_t>(extended_hdrs);
    if (little_endian) {
        b.data[bin_start + 60] = ext & 0xFF;
        b.data[bin_start + 61] = ext >> 8;
    } else {
        b.data[bin_start + 60] = ext >> 8;
        b.data[bin_start + 61] = ext & 0xFF;
    }

    // ── Extended text headers ──────────────────────────────────────────────────
    for (int e = 0; e < extended_hdrs; ++e) b.zeros(3200);

    // ── Traces ────────────────────────────────────────────────────────────────
    for (const TraceSpec& tr : traces) {
        const size_t trace_start = b.size();
        b.zeros(240);  // trace header

        auto wr16 = [&](size_t off, int16_t v) {
            if (little_endian) {
                b.data[trace_start + off]     = static_cast<uint8_t>(v & 0xFF);
                b.data[trace_start + off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
            } else {
                b.data[trace_start + off]     = static_cast<uint8_t>((v >> 8) & 0xFF);
                b.data[trace_start + off + 1] = static_cast<uint8_t>(v & 0xFF);
            }
        };
        auto wr32 = [&](size_t off, int32_t v) {
            if (little_endian) {
                b.data[trace_start + off]     = static_cast<uint8_t>( v        & 0xFF);
                b.data[trace_start + off + 1] = static_cast<uint8_t>((v >>  8) & 0xFF);
                b.data[trace_start + off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
                b.data[trace_start + off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
            } else {
                b.data[trace_start + off]     = static_cast<uint8_t>((v >> 24) & 0xFF);
                b.data[trace_start + off + 1] = static_cast<uint8_t>((v >> 16) & 0xFF);
                b.data[trace_start + off + 2] = static_cast<uint8_t>((v >>  8) & 0xFF);
                b.data[trace_start + off + 3] = static_cast<uint8_t>( v        & 0xFF);
            }
        };

        // offset 28: trace identification code
        wr16(28, tr.ident_code);
        // offset 70: coordinate scalar
        wr16(70, tr.coord_scale);
        // offsets 72,76: source X, Y
        const int32_t raw_x = static_cast<int32_t>(tr.lon * (-tr.coord_scale));
        const int32_t raw_y = static_cast<int32_t>(tr.lat * (-tr.coord_scale));
        wr32(72, raw_x);
        wr32(76, raw_y);
        // offsets 80,84: group X, Y (bytes 81-88; reader falls back here when source = 0)
        const int32_t raw_gx = static_cast<int32_t>(tr.grp_lon * (-tr.coord_scale));
        const int32_t raw_gy = static_cast<int32_t>(tr.grp_lat * (-tr.coord_scale));
        wr32(80, raw_gx);
        wr32(84, raw_gy);
        // offset 88: coordinate units
        wr16(88, tr.coord_units);
        // offset 108: delay recording time (ms)
        wr16(108, tr.delay_ms);
        // offset 114: number of samples
        wr16(114, static_cast<int16_t>(tr.samples.size()));
        // offset 116: sample interval (µs)
        wr16(116, static_cast<int16_t>(si_us));
        // timestamp offsets 156-164
        wr16(156, tr.year); wr16(158, tr.doy);
        wr16(160, tr.hour); wr16(162, tr.minute); wr16(164, tr.second);

        // Sample data
        for (float s : tr.samples) {
            switch (sample_format) {
            case 1: {  // IBM float (true encoding, always BE regardless of file order)
                const uint32_t ibm = floatToIbm(s);
                b.data.push_back((ibm >> 24) & 0xFF);
                b.data.push_back((ibm >> 16) & 0xFF);
                b.data.push_back((ibm >>  8) & 0xFF);
                b.data.push_back( ibm        & 0xFF);
                break;
            }
            case 2: {  // 32-bit integer
                const int32_t v = static_cast<int32_t>(s * 2147483647.f);
                if (little_endian) {
                    b.data.push_back(static_cast<uint8_t>( v        & 0xFF));
                    b.data.push_back(static_cast<uint8_t>((v >>  8) & 0xFF));
                    b.data.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
                    b.data.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
                } else {
                    b.data.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
                    b.data.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
                    b.data.push_back(static_cast<uint8_t>((v >>  8) & 0xFF));
                    b.data.push_back(static_cast<uint8_t>( v        & 0xFF));
                }
                break;
            }
            case 3: {  // 16-bit integer
                const int16_t v = static_cast<int16_t>(s * 32767.f);
                if (little_endian) {
                    b.data.push_back(static_cast<uint8_t>( v        & 0xFF));
                    b.data.push_back(static_cast<uint8_t>((v >>  8) & 0xFF));
                } else {
                    b.data.push_back(static_cast<uint8_t>((v >>  8) & 0xFF));
                    b.data.push_back(static_cast<uint8_t>( v        & 0xFF));
                }
                break;
            }
            case 5: {  // IEEE float
                if (little_endian) b.lef32(s); else b.bef32(s);
                break;
            }
            case 8: {  // 8-bit integer
                const int8_t v = static_cast<int8_t>(s * 127.f);
                b.data.push_back(static_cast<uint8_t>(v));
                break;
            }
            case 6: {  // IEEE 64-bit double
                const double d = static_cast<double>(s);
                uint64_t u; std::memcpy(&u, &d, 8);
                if (little_endian) {
                    for (int j = 0; j < 8; ++j)
                        b.data.push_back(static_cast<uint8_t>((u >> (j * 8)) & 0xFF));
                } else {
                    for (int j = 7; j >= 0; --j)
                        b.data.push_back(static_cast<uint8_t>((u >> (j * 8)) & 0xFF));
                }
                break;
            }
            case 7: {  // 24-bit signed integer
                const int32_t v = static_cast<int32_t>(s * 8388607.f);
                if (little_endian) {
                    b.data.push_back(static_cast<uint8_t>( v        & 0xFF));
                    b.data.push_back(static_cast<uint8_t>((v >>  8) & 0xFF));
                    b.data.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
                } else {
                    b.data.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
                    b.data.push_back(static_cast<uint8_t>((v >>  8) & 0xFF));
                    b.data.push_back(static_cast<uint8_t>( v        & 0xFF));
                }
                break;
            }
            case 9: {  // 64-bit signed integer
                const int64_t v = static_cast<int64_t>(
                    static_cast<double>(s) * 9223372036854775807.0);
                if (little_endian) {
                    for (int j = 0; j < 8; ++j)
                        b.data.push_back(static_cast<uint8_t>((v >> (j * 8)) & 0xFF));
                } else {
                    for (int j = 7; j >= 0; --j)
                        b.data.push_back(static_cast<uint8_t>((v >> (j * 8)) & 0xFF));
                }
                break;
            }
            case 10: {  // 32-bit unsigned integer (centred at 2^31)
                const uint32_t v = static_cast<uint32_t>(
                    static_cast<int64_t>(s * 2147483647.f) + 2147483648LL);
                if (little_endian) {
                    b.data.push_back(static_cast<uint8_t>( v        & 0xFF));
                    b.data.push_back(static_cast<uint8_t>((v >>  8) & 0xFF));
                    b.data.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
                    b.data.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
                } else {
                    b.data.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
                    b.data.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
                    b.data.push_back(static_cast<uint8_t>((v >>  8) & 0xFF));
                    b.data.push_back(static_cast<uint8_t>( v        & 0xFF));
                }
                break;
            }
            case 11: {  // 16-bit unsigned integer (centred at 2^15)
                const uint16_t v = static_cast<uint16_t>(
                    static_cast<int32_t>(s * 32767.f) + 32768);
                if (little_endian) {
                    b.data.push_back(static_cast<uint8_t>( v        & 0xFF));
                    b.data.push_back(static_cast<uint8_t>((v >>  8) & 0xFF));
                } else {
                    b.data.push_back(static_cast<uint8_t>((v >>  8) & 0xFF));
                    b.data.push_back(static_cast<uint8_t>( v        & 0xFF));
                }
                break;
            }
            case 12: {  // 8-bit unsigned integer (centred at 2^7)
                const uint8_t v = static_cast<uint8_t>(
                    static_cast<int32_t>(s * 127.f) + 128);
                b.data.push_back(v);
                break;
            }
            default: b.zeros(4); break;
            }
        }
    }
    return b.data;
}

// ── IBM float encoding ────────────────────────────────────────────────────────
// Converts an IEEE single to the IBM System/360 32-bit float format (big-endian
// bit pattern).  IBM format: sign(1) | exponent(7, bias 64, base 16) | mantissa(24).
// Value = M * 16^(E-64), where M = mantissa_int / 2^24 ∈ [1/16, 1).
static uint32_t floatToIbm(float v)
{
    if (v == 0.f) return 0;
    uint32_t sign = 0;
    if (v < 0.f) { sign = 0x80000000u; v = -v; }

    int e;
    const float frac = std::frexp(v, &e);  // v = frac * 2^e, 0.5 ≤ frac < 1

    // IBM exponent k = ceil(e/4); shift = 4k-e ∈ [0,3] tells how many bits
    // to shift the mantissa right so it fits the base-16 exponent grid.
    const int k     = (e <= 0) ? e / 4 : (e - 1) / 4 + 1;
    const int shift = 4 * k - e;

    int ibm_exp = k + 64;
    if (ibm_exp < 0)   return sign;                   // underflow → signed zero
    if (ibm_exp > 127) return sign | 0x7FFFFFFFu;     // overflow  → max magnitude

    const auto mant = static_cast<uint32_t>(
        frac * std::exp2(static_cast<float>(24 - shift)));

    return sign | (static_cast<uint32_t>(ibm_exp) << 24) | (mant & 0x00FFFFFFu);
}

// Write bytes to a temp file, return the path.  Caller must delete the file.
std::string writeTempFile(const std::vector<uint8_t>& bytes)
{
    namespace fs = std::filesystem;
    const fs::path p = fs::temp_directory_path() / "dolphin_test_segy.sgy";
    FILE* f = std::fopen(p.string().c_str(), "wb");
    assert(f && "could not create temp file");
    std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    return p.string();
}

// ── Helper ────────────────────────────────────────────────────────────────────
static int g_pass = 0, g_fail = 0;

#define CHECK(cond, msg)                                                          \
    do { if (cond) { ++g_pass; }                                                  \
         else { ++g_fail; std::fprintf(stderr, "FAIL: %s  (%s)\n", msg, #cond); } \
    } while (0)

// ── Tests ─────────────────────────────────────────────────────────────────────

void test_open_rejects_tiny_file()
{
    const std::string path = writeTempFile({ 0x00, 0x01, 0x02 });
    dolphin::io::SegyReader r;
    CHECK(!r.open(path), "open rejects file < 3600 bytes");
    r.close();
    removeTempFile(path);
}

void test_open_rejects_unknown_format()
{
    // Build a header where format code = 99 (unknown) in BE, and also 99 in LE.
    std::vector<uint8_t> hdr(3600, 0x00);
    hdr[0] = 'C';  // ASCII text header start
    hdr[3224] = 0x00; hdr[3225] = 99;  // bytes 25-26 of bin header: format 99
    const std::string path = writeTempFile(hdr);
    dolphin::io::SegyReader r;
    CHECK(!r.open(path), "open rejects unrecognised sample format");
    r.close();
    removeTempFile(path);
}

void test_big_endian_float_roundtrip()
{
    TraceSpec tr;
    tr.lat = 59.5; tr.lon = 10.5;
    tr.coord_units = 4; tr.coord_scale = -1000;
    tr.samples = { 0.0f, 0.0f, 0.3f, 0.7f, 0.9f, 0.4f, 0.1f };

    const auto bytes = buildSegy(5, false, { tr });
    const std::string path = writeTempFile(bytes);

    dolphin::io::SegyReader r;
    CHECK(r.open(path), "open BE float file");

    const auto idx = r.buildIndex();
    CHECK(idx.entries.size() == 1, "BE file has 1 trace");

    if (!idx.entries.empty()) {
        const auto& e = idx.entries[0];
        CHECK(std::abs(e.lat  - 59.5) < 0.01, "BE coord lat correct");
        CHECK(std::abs(e.lon  - 10.5) < 0.01, "BE coord lon correct");
        CHECK(!e.is_projected,                  "BE geo coords not projected");

        const auto art = r.readArtifact(e);
        CHECK(art.has_value(), "readArtifact returns a value");
        if (art) {
            const auto& t = std::get<core::SubBottomTrace>(*art);
            CHECK(t.samples.size() == tr.samples.size(), "sample count matches");
            CHECK(t.sample_rate_hz > 0.f, "sample rate populated");
            CHECK(t.two_way_time_s > 0.f, "two_way_time_s populated");
        }
    }
    r.close();
    removeTempFile(path);
}

void test_little_endian_detection()
{
    TraceSpec tr;
    tr.lat = 48.9; tr.lon = 2.3;
    tr.coord_units = 4; tr.coord_scale = -1000;
    tr.samples = { 0.1f, 0.4f, 0.8f, 0.5f, 0.2f };

    const auto bytes = buildSegy(5, true, { tr });
    const std::string path = writeTempFile(bytes);

    dolphin::io::SegyReader r;
    CHECK(r.open(path), "open LE float file");

    const auto idx = r.buildIndex();
    CHECK(idx.entries.size() == 1, "LE file has 1 trace");
    if (!idx.entries.empty()) {
        CHECK(std::abs(idx.entries[0].lat - 48.9) < 0.01, "LE coord lat correct");
        CHECK(std::abs(idx.entries[0].lon -  2.3) < 0.01, "LE coord lon correct");
    }
    r.close();
    removeTempFile(path);
}

void test_dead_trace_skipped()
{
    TraceSpec live, dead;
    live.samples = { 0.1f, 0.5f, 0.3f };
    live.ident_code = 1;
    dead.samples = { 0.0f, 0.0f, 0.0f };
    dead.ident_code = 2;  // dead trace — must be filtered

    const auto bytes = buildSegy(5, false, { live, dead, live });
    const std::string path = writeTempFile(bytes);

    dolphin::io::SegyReader r;
    CHECK(r.open(path), "open file with dead trace");
    const auto idx = r.buildIndex();
    CHECK(idx.entries.size() == 2, "dead trace filtered out (2 of 3 kept)");
    r.close();
    removeTempFile(path);
}

void test_dummy_trace_skipped()
{
    TraceSpec live, dummy;
    live.ident_code  = 1;
    dummy.ident_code = 3;  // dummy
    live.samples = dummy.samples = { 0.2f, 0.6f, 0.4f };

    const auto bytes = buildSegy(3, false, { dummy, live, dummy });
    const std::string path = writeTempFile(bytes);

    dolphin::io::SegyReader r;
    CHECK(r.open(path), "open file with dummy traces");
    const auto idx = r.buildIndex();
    CHECK(idx.entries.size() == 1, "dummy traces filtered (1 of 3 kept)");
    r.close();
    removeTempFile(path);
}

void test_projected_coordinates()
{
    TraceSpec tr;
    tr.coord_units = 1;           // metres
    tr.coord_scale = 1;           // raw value = metres directly
    tr.lon = 432000.0;            // UTM easting
    tr.lat = 6350000.0;           // UTM northing
    // Scale = 1 means raw_x = lon*1 — but we encode as integers so use a
    // smaller scale for the builder to keep int32 range OK:
    tr.coord_scale = -1;          // divide by 1 = identity
    tr.samples = { 0.0f, 0.2f, 0.8f };

    const auto bytes = buildSegy(5, false, { tr });
    const std::string path = writeTempFile(bytes);

    dolphin::io::SegyReader r;
    CHECK(r.open(path), "open projected-coord file");
    const auto idx = r.buildIndex();
    if (!idx.entries.empty()) {
        CHECK(idx.entries[0].is_projected, "UTM-metre coords detected as projected");
    }
    r.close();
    removeTempFile(path);
}

void test_int16_format()
{
    TraceSpec tr;
    tr.coord_units = 4; tr.coord_scale = -1000;
    tr.lat = 55.0; tr.lon = 12.0;
    tr.samples = { -0.5f, 0.0f, 0.5f, 0.9f };

    const auto bytes = buildSegy(3, false, { tr });
    const std::string path = writeTempFile(bytes);
    dolphin::io::SegyReader r;
    CHECK(r.open(path), "open int16 file");
    const auto idx = r.buildIndex();
    if (!idx.entries.empty()) {
        const auto art = r.readArtifact(idx.entries[0]);
        CHECK(art.has_value(), "int16 readArtifact ok");
    }
    r.close();
    removeTempFile(path);
}

void test_int8_format()
{
    TraceSpec tr;
    tr.coord_units = 4; tr.coord_scale = -1000;
    tr.lat = 55.0; tr.lon = 12.0;
    tr.samples = { -0.5f, 0.0f, 0.5f, 0.9f };

    const auto bytes = buildSegy(8, false, { tr });
    const std::string path = writeTempFile(bytes);
    dolphin::io::SegyReader r;
    CHECK(r.open(path), "open int8 file");
    const auto idx = r.buildIndex();
    if (!idx.entries.empty()) {
        const auto art = r.readArtifact(idx.entries[0]);
        CHECK(art.has_value(), "int8 readArtifact ok");
        if (art) {
            const auto& t = std::get<core::SubBottomTrace>(*art);
            CHECK(!t.samples.empty(), "int8 samples decoded");
        }
    }
    r.close();
    removeTempFile(path);
}

void test_delay_recording_time()
{
    TraceSpec tr;
    tr.coord_units = 4; tr.coord_scale = -1000;
    tr.lat = 59.0; tr.lon = 5.0;
    tr.delay_ms = 50;   // 50 ms delay
    tr.samples  = { 0.1f, 0.5f, 0.3f };

    const auto bytes = buildSegy(5, false, { tr });
    const std::string path = writeTempFile(bytes);
    dolphin::io::SegyReader r;
    CHECK(r.open(path), "open file with delay");
    const auto idx = r.buildIndex();
    if (!idx.entries.empty()) {
        const auto art = r.readArtifact(idx.entries[0]);
        CHECK(art.has_value(), "delay file readArtifact ok");
        if (art) {
            const auto& t = std::get<core::SubBottomTrace>(*art);
            // two_way_time_s must include the 50 ms delay.
            CHECK(t.two_way_time_s > 0.050f, "two_way_time_s includes delay");
        }
    }
    r.close();
    removeTempFile(path);
}

void test_extended_headers()
{
    TraceSpec tr;
    tr.coord_units = 4; tr.coord_scale = -1000;
    tr.lat = 60.0; tr.lon = 5.0;
    tr.samples = { 0.3f, 0.7f };

    const auto bytes = buildSegy(5, false, { tr }, 2 /*extended*/);
    const std::string path = writeTempFile(bytes);
    dolphin::io::SegyReader r;
    CHECK(r.open(path), "open file with 2 extended headers");
    const auto idx = r.buildIndex();
    CHECK(idx.entries.size() == 1, "trace found after extended headers");
    r.close();
    removeTempFile(path);
}

void test_text_header_epsg_crs()
{
    TraceSpec tr;
    tr.coord_units = 1; tr.coord_scale = -1;
    tr.lon = 432000.0; tr.lat = 6350000.0;
    tr.samples = { 0.1f, 0.5f };

    const auto bytes = buildSegy(5, false, { tr }, 0, "EPSG:32632");
    const std::string path = writeTempFile(bytes);
    dolphin::io::SegyReader r;
    CHECK(r.open(path), "open file with EPSG in text header");
    const auto idx = r.buildIndex();
    const auto meta = r.metadata();
    CHECK(meta.coordinate_ref.id == "EPSG:32632", "EPSG extracted from text header");
    r.close();
    removeTempFile(path);
}

void test_group_coordinate_fallback()
{
    // Source X/Y are zero; reader must fall back to group X/Y (bytes 81-88).
    TraceSpec tr;
    tr.lon     = 0.0;   tr.lat     = 0.0;    // source stays zero
    tr.grp_lon = 10.5;  tr.grp_lat = 59.5;   // group coords carry the position
    tr.coord_units = 4; tr.coord_scale = -1000;
    tr.samples = { 0.2f, 0.4f, 0.6f };

    const auto bytes = buildSegy(5, false, { tr });
    const std::string path = writeTempFile(bytes);
    dolphin::io::SegyReader r;
    CHECK(r.open(path), "open file with group-coord fallback");
    const auto idx = r.buildIndex();
    CHECK(idx.entries.size() == 1, "group-coord trace indexed");
    if (!idx.entries.empty()) {
        CHECK(std::abs(idx.entries[0].lon - 10.5) < 0.01, "group lon read correctly");
        CHECK(std::abs(idx.entries[0].lat - 59.5) < 0.01, "group lat read correctly");
    }
    r.close();
    removeTempFile(path);
}

void test_multiple_traces_timestamp_order()
{
    TraceSpec t1, t2, t3;
    t1.year = 2022; t1.doy = 200; t1.hour = 8;  t1.minute = 0;  t1.second = 0;
    t2.year = 2022; t2.doy = 200; t2.hour = 8;  t2.minute = 0;  t2.second = 5;
    t3.year = 2022; t3.doy = 200; t3.hour = 8;  t3.minute = 0;  t3.second = 10;
    for (auto* tp : { &t1, &t2, &t3 }) {
        tp->coord_units = 4; tp->coord_scale = -1000;
        tp->lat = 59.0; tp->lon = 5.0;
        tp->samples = { 0.1f, 0.5f, 0.3f };
    }

    const auto bytes = buildSegy(5, false, { t1, t2, t3 });
    const std::string path = writeTempFile(bytes);
    dolphin::io::SegyReader r;
    CHECK(r.open(path), "open 3-trace file");
    const auto idx = r.buildIndex();
    CHECK(idx.entries.size() == 3, "3 traces indexed");
    if (idx.entries.size() == 3) {
        CHECK(idx.entries[0].timestamp_us < idx.entries[1].timestamp_us,
              "timestamps in ascending order (0<1)");
        CHECK(idx.entries[1].timestamp_us < idx.entries[2].timestamp_us,
              "timestamps in ascending order (1<2)");
    }
    const auto meta = r.metadata();
    CHECK(meta.start_time > 0.0, "start_time set");
    CHECK(meta.end_time   > meta.start_time, "end_time > start_time");
    r.close();
    removeTempFile(path);
}

void test_int32_format_roundtrip()
{
    // Format 2: 32-bit signed integer, BE and LE.  Quantization step = 1/2^31 ≈ 4.7e-10.
    const std::vector<float> src = { -0.9f, -0.5f, 0.0f, 0.5f, 0.9f };
    for (bool le : { false, true }) {
        TraceSpec tr;
        tr.coord_units = 4; tr.coord_scale = -1000;
        tr.lat = 60.0; tr.lon = 5.0;
        tr.samples = src;
        const auto bytes = buildSegy(2, le, { tr });
        const std::string path = writeTempFile(bytes);
        dolphin::io::SegyReader r;
        CHECK(r.open(path), le ? "open int32 LE file" : "open int32 BE file");
        const auto idx = r.buildIndex();
        if (!idx.entries.empty()) {
            const auto art = r.readArtifact(idx.entries[0]);
            CHECK(art.has_value(), le ? "int32 LE readArtifact ok" : "int32 BE readArtifact ok");
            if (art) {
                const auto& t = std::get<core::SubBottomTrace>(*art);
                CHECK(t.samples.size() == src.size(),
                      le ? "int32 LE sample count" : "int32 BE sample count");
                // Largest value is 0.9, peak ≤ 1 so no normalisation applied.
                // Quantization error at 32-bit is negligible — use 1e-5 tolerance.
                bool ok = true;
                for (size_t i = 0; i < t.samples.size(); ++i)
                    if (std::abs(t.samples[i] - src[i]) > 1e-5f) { ok = false; break; }
                CHECK(ok, le ? "int32 LE amplitude round-trip" : "int32 BE amplitude round-trip");
            }
        }
        r.close();
        removeTempFile(path);
    }
}

void test_ieee64_format_roundtrip()
{
    // Format 6: IEEE 64-bit double → float.  Round-trip through double is exact for
    // simple binary fractions; tolerance covers the float→double→float cast.
    const std::vector<float> src = { -0.75f, -0.25f, 0.0f, 0.25f, 0.75f };
    for (bool le : { false, true }) {
        TraceSpec tr;
        tr.coord_units = 4; tr.coord_scale = -1000;
        tr.lat = 60.0; tr.lon = 5.0;
        tr.samples = src;
        const auto bytes = buildSegy(6, le, { tr });
        const std::string path = writeTempFile(bytes);
        dolphin::io::SegyReader r;
        CHECK(r.open(path), le ? "open f64 LE file" : "open f64 BE file");
        const auto idx = r.buildIndex();
        if (!idx.entries.empty()) {
            const auto art = r.readArtifact(idx.entries[0]);
            CHECK(art.has_value(), le ? "f64 LE readArtifact ok" : "f64 BE readArtifact ok");
            if (art) {
                const auto& t = std::get<core::SubBottomTrace>(*art);
                bool ok = true;
                for (size_t i = 0; i < t.samples.size() && i < src.size(); ++i)
                    if (std::abs(t.samples[i] - src[i]) > 1e-6f) { ok = false; break; }
                CHECK(ok, le ? "f64 LE amplitude round-trip" : "f64 BE amplitude round-trip");
            }
        }
        r.close();
        removeTempFile(path);
    }
}

void test_ibm_float_roundtrip()
{
    // Format 1: IBM 32-bit float (always big-endian).
    // Use values that are exactly representable in IBM format (binary fractions
    // that align to hex digit boundaries) so the round-trip is bit-perfect.
    const std::vector<float> src = { 0.0f, 0.5f, -0.5f, 0.25f, -0.25f, 1.0f };
    TraceSpec tr;
    tr.coord_units = 4; tr.coord_scale = -1000;
    tr.lat = 60.0; tr.lon = 5.0;
    tr.samples = src;
    const auto bytes = buildSegy(1, false /*IBM is always BE*/, { tr });
    const std::string path = writeTempFile(bytes);
    dolphin::io::SegyReader r;
    CHECK(r.open(path), "open IBM float file");
    const auto idx = r.buildIndex();
    if (!idx.entries.empty()) {
        const auto art = r.readArtifact(idx.entries[0]);
        CHECK(art.has_value(), "IBM float readArtifact ok");
        if (art) {
            const auto& t = std::get<core::SubBottomTrace>(*art);
            CHECK(t.samples.size() == src.size(), "IBM float sample count");
            // IBM→IEEE conversion introduces at most 1 ULP of error for these values.
            bool ok = true;
            for (size_t i = 0; i < t.samples.size() && i < src.size(); ++i)
                if (std::abs(t.samples[i] - src[i]) > 1e-6f) { ok = false; break; }
            CHECK(ok, "IBM float amplitude round-trip");
        }
    }
    r.close();
    removeTempFile(path);
}

void test_corrupt_trace_recovery()
{
    // A trace with ns=0 and si=0 in its trace header is corrupt.  The reader
    // must skip it using the binary-header sample count and find the next valid trace.
    TraceSpec valid_tr;
    valid_tr.coord_units = 4; valid_tr.coord_scale = -1000;
    valid_tr.lat = 59.0; valid_tr.lon = 5.0;
    valid_tr.samples = { 0.1f, 0.5f, 0.3f, 0.7f, 0.2f };

    // Build a single valid trace to get the correct file headers.
    auto bytes = buildSegy(5, false, { valid_tr });

    // Zero the binary-header fallback values (si at +16, spt at +20) so that
    // m_fileSampleInterval and m_fileSamplesPerTrace are both 0.  Without this,
    // the reader rescues the all-zero corrupt trace via those fallbacks and never
    // enters the resync path — the test would pass structurally but miss the
    // ResyncedPacket assertion.
    bytes[3200 + 16] = 0x00; bytes[3200 + 17] = 0x00;  // sample interval
    bytes[3200 + 20] = 0x00; bytes[3200 + 21] = 0x00;  // samples per trace

    // Splice in a corrupt trace (all-zero 240-byte header + matching sample block)
    // immediately after the 3600-byte fixed headers.  The trace header ns=0 and
    // si=0 with no binary-header fallback forces the resync scanner to fire.
    const size_t data_start   = 3600;
    const size_t corrupt_size = 240 + valid_tr.samples.size() * 4;
    bytes.insert(bytes.begin() + static_cast<ptrdiff_t>(data_start),
                 corrupt_size, 0x00);

    const std::string path = writeTempFile(bytes);
    dolphin::io::SegyReader r;
    CHECK(r.open(path), "open file with corrupt first trace");
    const auto idx = r.buildIndex();
    // At least the valid trace must be recovered.
    CHECK(!idx.entries.empty(), "valid trace recovered after corrupt trace");
    // The corrupt trace must produce a ResyncedPacket diagnostic.
    bool found_resync = false;
    for (const auto& d : r.diagnostics())
        if (d.code == core::ImportDiagnosticCode::ResyncedPacket) { found_resync = true; break; }
    CHECK(found_resync, "corrupt trace emits ResyncedPacket diagnostic");
    r.close();
    removeTempFile(path);
}

void test_lowercase_crs_in_text_header()
{
    // CRS strings may appear in lower- or mixed-case in real files.
    TraceSpec tr;
    tr.coord_units = 1; tr.coord_scale = -1;
    tr.lon = 432000.0; tr.lat = 6350000.0;
    tr.samples = { 0.1f, 0.5f };

    // "epsg:32632" — all lowercase, must still be recognized.
    const auto bytes = buildSegy(5, false, { tr }, 0, "epsg:32632");
    const std::string path = writeTempFile(bytes);
    dolphin::io::SegyReader r;
    CHECK(r.open(path), "open file with lowercase EPSG in text header");
    const auto idx = r.buildIndex();
    const auto meta = r.metadata();
    CHECK(meta.coordinate_ref.id == "EPSG:32632",
          "lowercase epsg: extracted and normalised to EPSG:32632");
    r.close();
    removeTempFile(path);
}

void test_le_int16_roundtrip()
{
    // Format 3 (16-bit integer), little-endian.  Quantization step = 1/32768.
    const std::vector<float> src = { -0.9f, -0.5f, 0.0f, 0.5f, 0.9f };
    TraceSpec tr;
    tr.coord_units = 4; tr.coord_scale = -1000;
    tr.lat = 55.0; tr.lon = 12.0;
    tr.samples = src;
    const auto bytes = buildSegy(3, true /*LE*/, { tr });
    const std::string path = writeTempFile(bytes);
    dolphin::io::SegyReader r;
    CHECK(r.open(path), "open LE int16 file");
    const auto idx = r.buildIndex();
    if (!idx.entries.empty()) {
        const auto art = r.readArtifact(idx.entries[0]);
        CHECK(art.has_value(), "LE int16 readArtifact ok");
        if (art) {
            const auto& t = std::get<core::SubBottomTrace>(*art);
            // Tolerance = 2 * quantization step (1/32768 ≈ 3e-5)
            bool ok = true;
            for (size_t i = 0; i < t.samples.size() && i < src.size(); ++i)
                if (std::abs(t.samples[i] - src[i]) > 6e-5f) { ok = false; break; }
            CHECK(ok, "LE int16 amplitude round-trip");
        }
    }
    r.close();
    removeTempFile(path);
}

void test_projected_crs_inferred_diagnostic()
{
    // A file with projected coordinates and no text-header CRS must emit
    // CoordinateSystemInferred so the user knows the CRS was guessed.
    TraceSpec tr;
    tr.coord_units = 1;   // metres — forces projected path
    tr.coord_scale = -1;
    tr.lon = 432000.0;
    tr.lat = 6350000.0;
    tr.samples = { 0.1f, 0.5f, 0.3f };

    // No text_insert → no EPSG in header → fallback to PROJECTED:SEGY.
    const auto bytes = buildSegy(5, false, { tr });
    const std::string path = writeTempFile(bytes);
    dolphin::io::SegyReader r;
    CHECK(r.open(path), "open projected file with no text-header CRS");
    r.buildIndex();

    bool found_inferred = false;
    for (const auto& d : r.diagnostics())
        if (d.code == core::ImportDiagnosticCode::CoordinateSystemInferred)
            { found_inferred = true; break; }
    CHECK(found_inferred, "projected file without CRS emits CoordinateSystemInferred");
    r.close();
    removeTempFile(path);
}

void test_int24_format_roundtrip()
{
    // Format 7: 24-bit signed integer, quantization step ≈ 1.2e-7.
    const std::vector<float> src = { -0.5f, -0.25f, 0.0f, 0.25f, 0.5f };
    for (bool le : { false, true }) {
        TraceSpec tr;
        tr.coord_units = 4; tr.coord_scale = -1000;
        tr.lat = 60.0; tr.lon = 5.0;
        tr.samples = src;
        const auto bytes = buildSegy(7, le, { tr });
        const std::string path = writeTempFile(bytes);
        dolphin::io::SegyReader r;
        CHECK(r.open(path), le ? "open int24 LE file" : "open int24 BE file");
        const auto idx = r.buildIndex();
        if (!idx.entries.empty()) {
            const auto art = r.readArtifact(idx.entries[0]);
            CHECK(art.has_value(), le ? "int24 LE readArtifact ok" : "int24 BE readArtifact ok");
            if (art) {
                const auto& t = std::get<core::SubBottomTrace>(*art);
                CHECK(t.samples.size() == src.size(),
                      le ? "int24 LE sample count" : "int24 BE sample count");
                bool ok = true;
                for (size_t i = 0; i < t.samples.size() && i < src.size(); ++i)
                    if (std::abs(t.samples[i] - src[i]) > 1e-6f) { ok = false; break; }
                CHECK(ok, le ? "int24 LE amplitude round-trip" : "int24 BE amplitude round-trip");
            }
        }
        r.close();
        removeTempFile(path);
    }
}

void test_int64_format_roundtrip()
{
    // Format 9: 64-bit signed integer — round-trip error is dominated by float precision.
    const std::vector<float> src = { -0.5f, -0.25f, 0.0f, 0.25f, 0.5f };
    for (bool le : { false, true }) {
        TraceSpec tr;
        tr.coord_units = 4; tr.coord_scale = -1000;
        tr.lat = 60.0; tr.lon = 5.0;
        tr.samples = src;
        const auto bytes = buildSegy(9, le, { tr });
        const std::string path = writeTempFile(bytes);
        dolphin::io::SegyReader r;
        CHECK(r.open(path), le ? "open int64 LE file" : "open int64 BE file");
        const auto idx = r.buildIndex();
        if (!idx.entries.empty()) {
            const auto art = r.readArtifact(idx.entries[0]);
            CHECK(art.has_value(), le ? "int64 LE readArtifact ok" : "int64 BE readArtifact ok");
            if (art) {
                const auto& t = std::get<core::SubBottomTrace>(*art);
                bool ok = true;
                for (size_t i = 0; i < t.samples.size() && i < src.size(); ++i)
                    if (std::abs(t.samples[i] - src[i]) > 1e-5f) { ok = false; break; }
                CHECK(ok, le ? "int64 LE amplitude round-trip" : "int64 BE amplitude round-trip");
            }
        }
        r.close();
        removeTempFile(path);
    }
}

void test_uint32_format_roundtrip()
{
    // Format 10: 32-bit unsigned integer, centred at 2^31. Tolerance ≈ 1e-6.
    const std::vector<float> src = { -0.5f, -0.25f, 0.0f, 0.25f, 0.5f };
    for (bool le : { false, true }) {
        TraceSpec tr;
        tr.coord_units = 4; tr.coord_scale = -1000;
        tr.lat = 60.0; tr.lon = 5.0;
        tr.samples = src;
        const auto bytes = buildSegy(10, le, { tr });
        const std::string path = writeTempFile(bytes);
        dolphin::io::SegyReader r;
        CHECK(r.open(path), le ? "open uint32 LE file" : "open uint32 BE file");
        const auto idx = r.buildIndex();
        if (!idx.entries.empty()) {
            const auto art = r.readArtifact(idx.entries[0]);
            CHECK(art.has_value(), le ? "uint32 LE readArtifact ok" : "uint32 BE readArtifact ok");
            if (art) {
                const auto& t = std::get<core::SubBottomTrace>(*art);
                bool ok = true;
                for (size_t i = 0; i < t.samples.size() && i < src.size(); ++i)
                    if (std::abs(t.samples[i] - src[i]) > 1e-5f) { ok = false; break; }
                CHECK(ok, le ? "uint32 LE amplitude round-trip" : "uint32 BE amplitude round-trip");
            }
        }
        r.close();
        removeTempFile(path);
    }
}

void test_uint16_format_roundtrip()
{
    // Format 11: 16-bit unsigned integer, centred at 2^15. Quantization ≈ 1/32768.
    const std::vector<float> src = { -0.5f, -0.25f, 0.0f, 0.25f, 0.5f };
    for (bool le : { false, true }) {
        TraceSpec tr;
        tr.coord_units = 4; tr.coord_scale = -1000;
        tr.lat = 60.0; tr.lon = 5.0;
        tr.samples = src;
        const auto bytes = buildSegy(11, le, { tr });
        const std::string path = writeTempFile(bytes);
        dolphin::io::SegyReader r;
        CHECK(r.open(path), le ? "open uint16 LE file" : "open uint16 BE file");
        const auto idx = r.buildIndex();
        if (!idx.entries.empty()) {
            const auto art = r.readArtifact(idx.entries[0]);
            CHECK(art.has_value(), le ? "uint16 LE readArtifact ok" : "uint16 BE readArtifact ok");
            if (art) {
                const auto& t = std::get<core::SubBottomTrace>(*art);
                bool ok = true;
                for (size_t i = 0; i < t.samples.size() && i < src.size(); ++i)
                    if (std::abs(t.samples[i] - src[i]) > 6e-5f) { ok = false; break; }
                CHECK(ok, le ? "uint16 LE amplitude round-trip" : "uint16 BE amplitude round-trip");
            }
        }
        r.close();
        removeTempFile(path);
    }
}

void test_uint8_format_roundtrip()
{
    // Format 12: 8-bit unsigned integer, centred at 128. Quantization ≈ 1/128.
    const std::vector<float> src = { -0.5f, -0.25f, 0.0f, 0.25f, 0.5f };
    TraceSpec tr;
    tr.coord_units = 4; tr.coord_scale = -1000;
    tr.lat = 60.0; tr.lon = 5.0;
    tr.samples = src;
    const auto bytes = buildSegy(12, false, { tr });
    const std::string path = writeTempFile(bytes);
    dolphin::io::SegyReader r;
    CHECK(r.open(path), "open uint8 file");
    const auto idx = r.buildIndex();
    if (!idx.entries.empty()) {
        const auto art = r.readArtifact(idx.entries[0]);
        CHECK(art.has_value(), "uint8 readArtifact ok");
        if (art) {
            const auto& t = std::get<core::SubBottomTrace>(*art);
            bool ok = true;
            for (size_t i = 0; i < t.samples.size() && i < src.size(); ++i)
                if (std::abs(t.samples[i] - src[i]) > 0.012f) { ok = false; break; }
            CHECK(ok, "uint8 amplitude round-trip");
        }
    }
    r.close();
    removeTempFile(path);
}

void test_coord_unit3_decimal_degrees()
{
    // SEG-Y Rev 1 §12.1: coordinate units value 3 = decimal degrees.
    // No conversion should be applied; values must pass through unchanged.
    TraceSpec tr;
    tr.coord_units = 3;     // decimal degrees per Rev 1 spec
    tr.coord_scale = -1000;
    tr.lon = 10.5; tr.lat = 59.5;
    tr.samples = { 0.2f, 0.5f, 0.3f };

    const auto bytes = buildSegy(5, false, { tr });
    const std::string path = writeTempFile(bytes);
    dolphin::io::SegyReader r;
    CHECK(r.open(path), "open file with unit-3 decimal-degrees coordinates");
    const auto idx = r.buildIndex();
    CHECK(idx.entries.size() == 1, "unit-3 file indexed");
    if (!idx.entries.empty()) {
        const auto& e = idx.entries[0];
        CHECK(std::abs(e.lon - 10.5) < 0.01, "unit-3 lon read as decimal degrees (no conversion)");
        CHECK(std::abs(e.lat - 59.5) < 0.01, "unit-3 lat read as decimal degrees (no conversion)");
        CHECK(!e.is_projected, "unit-3 coords classified as geographic");
    }
    r.close();
    removeTempFile(path);
}

void test_format_zero_defaults_to_float()
{
    // A file with format code 0x00 0x00 in the binary header (unset / Rev 0 style)
    // must open successfully, default to big-endian IEEE float, and emit
    // InferredBytesPerSample so the caller knows the format was inferred.
    TraceSpec tr;
    tr.coord_units = 4; tr.coord_scale = -1000;
    tr.lat = 60.0; tr.lon = 5.0;
    tr.samples = { 0.2f, 0.5f, 0.3f, 0.7f };

    // Build as format 5 BE (the inferred default), then zero out the format bytes.
    auto bytes = buildSegy(5, false, { tr });
    bytes[3200 + 24] = 0x00;  // binary header offset 24 (format code byte 1)
    bytes[3200 + 25] = 0x00;  // binary header offset 25 (format code byte 2)

    const std::string path = writeTempFile(bytes);
    dolphin::io::SegyReader r;
    CHECK(r.open(path), "format-0 file opens successfully");
    const auto idx = r.buildIndex();
    CHECK(!idx.entries.empty(), "format-0 file has traces");

    bool found_inferred = false;
    for (const auto& d : r.diagnostics())
        if (d.code == core::ImportDiagnosticCode::InferredBytesPerSample)
            { found_inferred = true; break; }
    CHECK(found_inferred, "format-0 emits InferredBytesPerSample diagnostic");

    if (!idx.entries.empty()) {
        const auto art = r.readArtifact(idx.entries[0]);
        CHECK(art.has_value(), "format-0 readArtifact decodes correctly as float");
    }
    r.close();
    removeTempFile(path);
}

void test_ebcdic_crs_in_text_header()
{
    // Build a text header that starts with EBCDIC bytes so the reader's EBCDIC
    // detection fires, then contains an EBCDIC-encoded "EPSG:32632".
    // EBCDIC space = 0x40; bytes > 0x7E trigger EBCDIC detection.
    // EBCDIC encoding of "EPSG:32632":
    //   E=0xC5  P=0xD7  S=0xE2  G=0xC7  :=0x7A  3=0xF3  2=0xF2  6=0xF6  3=0xF3  2=0xF2
    std::vector<uint8_t> txthdr(3200, 0x40);  // all EBCDIC spaces
    // Place EBCDIC 'C' (0xC3) at byte 0 — triggers detection (> 0x7E).
    txthdr[0] = 0xC3;
    // Write EBCDIC "EPSG:32632" starting at offset 4.
    const uint8_t epsg_ebcdic[] = { 0xC5,0xD7,0xE2,0xC7,0x7A,0xF3,0xF2,0xF6,0xF3,0xF2 };
    std::memcpy(&txthdr[4], epsg_ebcdic, sizeof(epsg_ebcdic));

    // Build a full SEG-Y file with the EBCDIC text header.
    TraceSpec tr;
    tr.coord_units = 1; tr.coord_scale = -1;
    tr.lon = 432000.0; tr.lat = 6350000.0;
    tr.samples = { 0.1f, 0.5f };
    auto bytes = buildSegy(5, false, { tr });
    // Replace the first 3200 bytes (text header) with our EBCDIC one.
    std::copy(txthdr.begin(), txthdr.end(), bytes.begin());

    const std::string path = writeTempFile(bytes);
    dolphin::io::SegyReader r;
    CHECK(r.open(path), "open file with EBCDIC text header");
    const auto idx = r.buildIndex();
    const auto meta = r.metadata();
    CHECK(meta.coordinate_ref.id == "EPSG:32632",
          "EPSG code extracted from EBCDIC text header");
    r.close();
    removeTempFile(path);
}

void test_neg1_extended_header_fallback()
{
    // A file with ext_raw == -1 and no ((SEG: EndText)) marker must:
    //   1. Skip all candidate 3200-byte blocks conservatively.
    //   2. Emit a ResyncedPacket warning (m_extHdrScanFailed path).
    //   3. Still find the trace that follows all the skipped blocks.
    TraceSpec tr;
    tr.coord_units = 4; tr.coord_scale = -1000;
    tr.lat = 60.0; tr.lon = 5.0;
    tr.samples = { 0.3f, 0.7f, 0.5f };

    // Build a normal 1-trace file then splice:
    //  - Binary header ext_raw field (offset 3200+60) set to 0xFF 0xFF = -1 (BE).
    //  - One 3200-byte dummy extended header block (no EndText) inserted.
    auto bytes = buildSegy(5, false, { tr });
    // Set ext_raw = -1 (big-endian) at binary header offset 60 (file offset 3260).
    bytes[3200 + 60] = 0xFF;
    bytes[3200 + 61] = 0xFF;
    // Insert one 3200-byte dummy block between binary header end (3600) and traces.
    bytes.insert(bytes.begin() + 3600, 3200, 0x00);

    const std::string path = writeTempFile(bytes);
    dolphin::io::SegyReader r;
    CHECK(r.open(path), "open file with ext_raw=-1 and no EndText");
    const auto idx = r.buildIndex();
    CHECK(!idx.entries.empty(), "trace found after conservative ext header skip");

    bool found_resync = false;
    for (const auto& d : r.diagnostics())
        if (d.code == core::ImportDiagnosticCode::ResyncedPacket)
            { found_resync = true; break; }
    CHECK(found_resync, "ext_raw=-1 without EndText emits ResyncedPacket warning");
    r.close();
    removeTempFile(path);
}

void test_stale_index_readartifact()
{
    // A fabricated ArtifactIndexEntry pointing past the end of the open file
    // must return nullopt from readArtifact rather than attempting an OOB seek.
    TraceSpec tr;
    tr.coord_units = 4; tr.coord_scale = -1000;
    tr.lat = 60.0; tr.lon = 5.0;
    tr.samples = { 0.2f, 0.4f };

    const auto bytes = buildSegy(5, false, { tr });
    const std::string path = writeTempFile(bytes);
    dolphin::io::SegyReader r;
    CHECK(r.open(path), "open file for stale-index test");
    r.buildIndex();

    // Construct a fake entry that points well past EOF.
    core::ArtifactIndexEntry fake;
    fake.type        = core::ArtifactType::SubBottom;
    fake.artifact_id = 0;
    fake.file_offset = 999999;
    fake.byte_length = 100;

    const auto art = r.readArtifact(fake);
    CHECK(!art.has_value(), "stale/OOB index entry returns nullopt from readArtifact");
    r.close();
    removeTempFile(path);
}

void test_metadata_cleared_after_failed_open()
{
    // After a successful open+buildIndex, a subsequent failed open must reset
    // the metadata so stale artifact_count / format_name are not visible.
    TraceSpec tr;
    tr.coord_units = 4; tr.coord_scale = -1000;
    tr.lat = 60.0; tr.lon = 5.0;
    tr.samples = { 0.1f, 0.5f, 0.3f };

    const auto bytes = buildSegy(5, false, { tr });
    const std::string path = writeTempFile(bytes);
    dolphin::io::SegyReader r;
    CHECK(r.open(path), "first open succeeds");
    r.buildIndex();
    CHECK(r.metadata().artifact_count == 1, "metadata has 1 artifact after first open");

    // Now attempt to open a non-existent file — must fail and clear state.
    const bool second_ok = r.open("__nonexistent_file__.sgy");
    CHECK(!second_ok, "second open with bad path fails");
    CHECK(r.metadata().artifact_count == 0, "artifact_count cleared after failed open");
    CHECK(r.metadata().format_name.empty(), "format_name cleared after failed open");

    r.close();
    removeTempFile(path);
}

void test_non_leap_doy366_rejected()
{
    // Day-of-year 366 is only valid in leap years.  2023 is not a leap year, so
    // a trace with doy=366 must produce a zero timestamp (ts_us == 0).
    TraceSpec tr;
    tr.coord_units = 4; tr.coord_scale = -1000;
    tr.lat = 60.0; tr.lon = 5.0;
    tr.year   = 2023;
    tr.doy    = 366;   // invalid for 2023
    tr.hour   = 12;
    tr.minute = 0;
    tr.second = 0;
    tr.samples = { 0.2f, 0.5f, 0.3f };

    const auto bytes = buildSegy(5, false, { tr });
    const std::string path = writeTempFile(bytes);
    dolphin::io::SegyReader r;
    CHECK(r.open(path), "open file with doy=366 in non-leap year");
    const auto idx = r.buildIndex();
    CHECK(!idx.entries.empty(), "trace indexed despite bad timestamp");
    if (!idx.entries.empty()) {
        CHECK(idx.entries[0].timestamp_us == 0,
              "doy=366 in non-leap year produces zero timestamp_us");
    }
    // start_time should remain 0.0 since no valid timestamp was recorded.
    CHECK(r.metadata().start_time == 0.0,
          "start_time is 0.0 when only trace has invalid doy");
    r.close();
    removeTempFile(path);
}

// ── New field-grade tests ──────────────────────────────────────────────────────

void test_text_header_populated_in_metadata()
{
    // After open(), metadata().text_header must be non-empty (first char 'C').
    TraceSpec tr;
    tr.coord_units = 4; tr.coord_scale = -1000;
    tr.lat = 60.0; tr.lon = 5.0;
    tr.samples = { 0.1f, 0.5f };

    const auto bytes = buildSegy(5, false, { tr }, 0, "SURVEY NORTH SEA");
    const std::string path = writeTempFile(bytes);
    dolphin::io::SegyReader r;
    CHECK(r.open(path), "open file for text-header metadata test");
    const auto meta = r.metadata();
    CHECK(!meta.text_header.empty(), "text_header populated after open");
    CHECK(meta.text_header[0] == 'C', "text_header starts with 'C' (ASCII line marker)");
    r.close();
    removeTempFile(path);
}

void test_segy_revision_in_metadata()
{
    // Binary header offset 300 = major SEG-Y revision byte.
    // Injecting 0x01 there makes metadata().segy_revision == 1.
    TraceSpec tr;
    tr.coord_units = 4; tr.coord_scale = -1000;
    tr.lat = 60.0; tr.lon = 5.0;
    tr.samples = { 0.2f, 0.5f };

    auto bytes = buildSegy(5, false, { tr });
    // Binary header starts at file offset 3200; revision is at byte 300 within it.
    bytes[3200 + 300] = 0x01;  // major revision = 1
    bytes[3200 + 301] = 0x00;  // minor revision = 0

    const std::string path = writeTempFile(bytes);
    dolphin::io::SegyReader r;
    CHECK(r.open(path), "open file with SEG-Y Rev 1 marker");
    const auto meta = r.metadata();
    CHECK(meta.segy_revision == 1, "segy_revision == 1 from binary header");
    r.close();
    removeTempFile(path);
}

void test_binary_header_si_ns_in_metadata()
{
    // metadata() must expose the binary-header sample interval and samples/trace.
    TraceSpec tr;
    tr.coord_units = 4; tr.coord_scale = -1000;
    tr.lat = 60.0; tr.lon = 5.0;
    tr.samples = { 0.1f, 0.2f, 0.3f, 0.4f };

    const auto bytes = buildSegy(5, false, { tr });
    const std::string path = writeTempFile(bytes);
    dolphin::io::SegyReader r;
    CHECK(r.open(path), "open file for metadata si/ns test");
    const auto meta = r.metadata();
    // buildSegy writes si_us = 1000 and spt = sample count in binary header.
    CHECK(meta.binary_sample_interval_us == 1000,
          "binary_sample_interval_us == 1000 µs (as written by builder)");
    CHECK(meta.binary_samples_per_trace  == 4,
          "binary_samples_per_trace == 4 (as written by builder)");
    r.close();
    removeTempFile(path);
}

void test_resync_forward_scan()
{
    // To reliably trigger the resync scanner, we need a trace header with ns==0
    // and si==0 that can't be rescued by the binary-header fallback values.
    // Strategy:
    //   1. Zero the binary header si and spt (so fallback = 0/0).
    //   2. Insert 512 zero bytes at the second-trace position — the resulting
    //      "header" has ns=0, si=0 and no fallback → triggers ResyncedPacket.
    //   3. The real second trace follows; the forward scanner must find it.
    TraceSpec tr;
    tr.coord_units = 4; tr.coord_scale = -1000;
    tr.lat = 59.0; tr.lon = 5.0;
    tr.year = 2022; tr.doy = 100; tr.hour = 10;
    tr.samples = { 0.1f, 0.5f, 0.3f, 0.7f, 0.2f };  // 5 samples, 4 bps = 20 bytes

    // Build a 2-trace file: [text 3200][bin 400][trace1 260][trace2 260] = 4120 bytes.
    auto bytes = buildSegy(5, false, { tr, tr });

    // Zero binary-header si (offset +16) and spt (offset +20) so fallback = 0.
    bytes[3200 + 16] = 0x00; bytes[3200 + 17] = 0x00;
    bytes[3200 + 20] = 0x00; bytes[3200 + 21] = 0x00;

    // Insert 512 zero bytes just after the first trace (file offset 3860).
    // This pushes the real second trace to offset 4372.
    const size_t trace1_end = 3600 + 240 + 5 * 4;  // = 3860
    bytes.insert(bytes.begin() + static_cast<ptrdiff_t>(trace1_end), 512, 0x00);

    const std::string path = writeTempFile(bytes);
    dolphin::io::SegyReader r;
    CHECK(r.open(path), "open file with zero-padded gap between traces");
    const auto idx = r.buildIndex();
    CHECK(!idx.entries.empty(), "first trace indexed despite gap");

    bool found_resync = false;
    for (const auto& d : r.diagnostics())
        if (d.code == core::ImportDiagnosticCode::ResyncedPacket)
            { found_resync = true; break; }
    CHECK(found_resync, "zero-padded inter-trace gap emits ResyncedPacket diagnostic");
    r.close();
    removeTempFile(path);
}

void test_samples_per_trace_mismatch_diagnostic()
{
    // Binary header says spt=7; traces declare ns=5.
    // SamplesPerTraceMismatch diagnostic must be emitted.
    TraceSpec tr;
    tr.coord_units = 4; tr.coord_scale = -1000;
    tr.lat = 60.0; tr.lon = 5.0;
    tr.samples = { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f };  // 5 samples per trace

    auto bytes = buildSegy(5, false, { tr });
    // Overwrite binary header spt (offset 20 within binary header = file offset 3220)
    // with 7 (big-endian).
    bytes[3200 + 20] = 0x00;
    bytes[3200 + 21] = 0x07;

    const std::string path = writeTempFile(bytes);
    dolphin::io::SegyReader r;
    CHECK(r.open(path), "open file with spt mismatch");
    r.buildIndex();

    bool found = false;
    for (const auto& d : r.diagnostics())
        if (d.code == core::ImportDiagnosticCode::SamplesPerTraceMismatch)
            { found = true; break; }
    CHECK(found, "spt mismatch emits SamplesPerTraceMismatch diagnostic");
    r.close();
    removeTempFile(path);
}

void test_encoding_inferred_diagnostic_emitted()
{
    // A format-0 file causes the probe to run — EncodingInferred must be emitted.
    TraceSpec tr;
    tr.coord_units = 4; tr.coord_scale = -1000;
    tr.lat = 60.0; tr.lon = 5.0;
    tr.samples = { 0.2f, 0.5f, 0.3f, 0.7f };

    // Build as format 5, then zero the format code bytes.
    auto bytes = buildSegy(5, false, { tr });
    bytes[3200 + 24] = 0x00;
    bytes[3200 + 25] = 0x00;

    const std::string path = writeTempFile(bytes);
    dolphin::io::SegyReader r;
    CHECK(r.open(path), "open format-0 file for encoding-inferred test");
    r.buildIndex();

    bool found = false;
    for (const auto& d : r.diagnostics())
        if (d.code == core::ImportDiagnosticCode::EncodingInferred)
            { found = true; break; }
    CHECK(found, "format-0 file emits EncodingInferred diagnostic after probe");
    r.close();
    removeTempFile(path);
}

void test_declared_geo_with_projected_coords()
{
    // Regression test for the coordinate classification bug:
    // coord_units=3 (declared decimal degrees) but actual values are UTM-scale
    // metres — e.g. easting 432241, northing 6352750.  The reader must classify
    // these as projected (is_projected=true) and emit CoordinateSystemOverridden.
    TraceSpec tr;
    tr.coord_units = 3;   // declared decimal degrees — but values are UTM metres
    tr.coord_scale = -1;  // divide raw by 1 = identity; keeps int32 range safe
    tr.lon = 432241.0;
    tr.lat = 6352750.0;
    tr.samples = { 0.1f, 0.5f, 0.3f };

    const auto bytes = buildSegy(5, false, { tr });
    const std::string path = writeTempFile(bytes);
    dolphin::io::SegyReader r;
    CHECK(r.open(path), "open file with declared-geo but projected-sized coords");
    const auto idx = r.buildIndex();
    CHECK(!idx.entries.empty(), "declared-geo-projected file indexed");
    if (!idx.entries.empty())
        CHECK(idx.entries[0].is_projected,
              "UTM-scale coords with units=3 classified as projected");

    bool found_override = false;
    for (const auto& d : r.diagnostics())
        if (d.code == core::ImportDiagnosticCode::CoordinateSystemOverridden)
            { found_override = true; break; }
    CHECK(found_override,
          "declared-geo + projected-magnitude coords emit CoordinateSystemOverridden");
    r.close();
    removeTempFile(path);
}

void test_swapped_geographic_coordinates()
{
    // Regression guard for the swap-detection path in parseTraceCoordsEx.
    // coord_units=3 (decimal degrees), X=55 (fits lat range ≤90°),
    // Y=120 (outside lat range but inside lon range).  The values are geographic
    // degrees with X/Y transposed — must NOT be classified as projected.
    TraceSpec tr;
    tr.coord_units = 3;
    tr.coord_scale = -1000;
    tr.lon = 55.0;   // written to X field
    tr.lat = 120.0;  // written to Y field — outside valid lat range
    tr.samples = { 0.2f, 0.4f, 0.6f };

    const auto bytes = buildSegy(5, false, { tr });
    const std::string path = writeTempFile(bytes);
    dolphin::io::SegyReader r;
    CHECK(r.open(path), "open file with swapped geographic X/Y");
    const auto idx = r.buildIndex();
    CHECK(!idx.entries.empty(), "swapped-geographic trace indexed");
    if (!idx.entries.empty())
        CHECK(!idx.entries[0].is_projected,
              "swapped geographic coords (x=55, y=120) not misclassified as projected");

    bool found_swap = false;
    for (const auto& d : r.diagnostics())
        if (d.code == core::ImportDiagnosticCode::CoordinateSwappedXY)
            { found_swap = true; break; }
    CHECK(found_swap, "swapped geographic X/Y emits CoordinateSwappedXY diagnostic");
    r.close();
    removeTempFile(path);
}

// ── main ──────────────────────────────────────────────────────────────────────

int main()
{
    test_open_rejects_tiny_file();
    test_open_rejects_unknown_format();
    test_big_endian_float_roundtrip();
    test_little_endian_detection();
    test_dead_trace_skipped();
    test_dummy_trace_skipped();
    test_projected_coordinates();
    test_int16_format();
    test_int8_format();
    test_delay_recording_time();
    test_extended_headers();
    test_text_header_epsg_crs();
    test_group_coordinate_fallback();
    test_multiple_traces_timestamp_order();
    test_int32_format_roundtrip();
    test_ieee64_format_roundtrip();
    test_ibm_float_roundtrip();
    test_corrupt_trace_recovery();
    test_lowercase_crs_in_text_header();
    test_le_int16_roundtrip();
    test_projected_crs_inferred_diagnostic();
    test_int24_format_roundtrip();
    test_int64_format_roundtrip();
    test_uint32_format_roundtrip();
    test_uint16_format_roundtrip();
    test_uint8_format_roundtrip();
    test_coord_unit3_decimal_degrees();
    test_format_zero_defaults_to_float();
    test_ebcdic_crs_in_text_header();
    test_neg1_extended_header_fallback();
    test_stale_index_readartifact();
    test_metadata_cleared_after_failed_open();
    test_non_leap_doy366_rejected();
    test_text_header_populated_in_metadata();
    test_segy_revision_in_metadata();
    test_binary_header_si_ns_in_metadata();
    test_resync_forward_scan();
    test_samples_per_trace_mismatch_diagnostic();
    test_encoding_inferred_diagnostic_emitted();
    test_declared_geo_with_projected_coords();
    test_swapped_geographic_coordinates();

    std::printf("SegyReader: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
