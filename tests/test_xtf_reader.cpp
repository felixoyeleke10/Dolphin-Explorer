// Stage 02 — XTF parser compatibility tests.
//
// Each test case corresponds to one fixture entry in XTF_FIXTURE_CATALOG.md.
//
// FIX-001  open() rejects non-XTF file (wrong magic byte)
// FIX-002  dual-channel SSS ping → 2 sidescan index entries, correct lat/lon
// FIX-003  unknown channel number out-of-range → skipped (classifyChannel fix)
// FIX-004  PACKET_NAV backfill for pings with zero nav coordinates
// FIX-005  NavUnits=3 → coordinate_ref is Projected
// FIX-006  BytesPerSample=0 in header → inferred from record geometry
// FIX-007  truncated record → partial index, no crash
// FIX-008  sub-bottom channel (TypeOfChannel=0) → SubBottom ArtifactType
// FIX-009  bad packet magic → ResyncedPacket diagnostic emitted
// FIX-010  BytesPerSample=0 in header → InferredBytesPerSample diagnostic emitted
// FIX-011  zero-nav pings backfilled from PACKET_NAV → InterpolatedNavigation diagnostic
// FIX-012  bathymetry channel (TypeOfChannel=3) → UnsupportedChannelType diagnostic
// FIX-013  unknown packet HeaderType → UnsupportedPacketType diagnostic, pings kept
// FIX-014  split-packet sidescan (one channel per PACKET_PING) → 2 entries, port+stbd
// FIX-015  dual-frequency (Edgetech 4200-style SubChannelNumber routing) → LF+HF bands
// FIX-016  real Edgetech 4200.E via Isis → 4-chan dual-freq, NavUnits=3 overridden Geographic
// FIX-017  real TST 500 kHz, 32-bit samples → NavUnits=0 overridden Projected
//
// Run via: ctest --output-on-failure
// No external test framework — same minimal assertion helper as test_parsed_cache.

#include "io/xtf/XtfReader.h"
#include "core/Artifact.h"
#include "core/ArtifactIndex.h"
#include "core/ImportDiagnostic.h"
#include "core/SidescanPing.h"
#include "core/SubBottomTrace.h"
#include "core/SpatialRef.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Tiny assertion helper
// ─────────────────────────────────────────────────────────────────────────────

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        std::fprintf(stderr, "FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr); \
        ++g_fail; \
    } else { \
        ++g_pass; \
    } \
} while (false)

#define CHECK_CLOSE(a, b, eps) do { \
    auto _a = (a); auto _b = (b); \
    if (std::abs(static_cast<double>(_a) - static_cast<double>(_b)) > (eps)) { \
        std::fprintf(stderr, "FAIL  %s:%d  |%s - %s| > %g  (got %g vs %g)\n", \
            __FILE__, __LINE__, #a, #b, static_cast<double>(eps), \
            static_cast<double>(_a), static_cast<double>(_b)); \
        ++g_fail; \
    } else { \
        ++g_pass; \
    } \
} while (false)

// ─────────────────────────────────────────────────────────────────────────────
//  TempFile RAII
// ─────────────────────────────────────────────────────────────────────────────

struct TempFile {
    std::string path;
    explicit TempFile(const std::string& suffix = ".xtf")
    {
        namespace fs = std::filesystem;
        path = (fs::temp_directory_path()
               / ("dolphin_xtf_test_" + std::to_string(
                   static_cast<uint64_t>(
                       std::chrono::steady_clock::now().time_since_epoch().count()
                   )) + suffix)).string();
    }
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(std::filesystem::path(path), ec);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  Minimal binary XTF builder
//
//  Builds a byte buffer matching the tightly-packed XTF structures (rev 40).
//  All sizes are verified against the static_asserts in XtfReader_p.h:
//    XtfFileHeader     = 256 bytes
//    XtfChanInfo       = 128 bytes
//    XtfPacketHeader   = 256 bytes
//    XtfPingChanHeader =  64 bytes
//
//  Offsets below are byte-exact for #pragma pack(push, 1) layout.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

static void w16(std::vector<uint8_t>& buf, size_t off, uint16_t v) {
    buf[off]   = static_cast<uint8_t>(v & 0xFF);
    buf[off+1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}
static void w32(std::vector<uint8_t>& buf, size_t off, uint32_t v) {
    buf[off]   = static_cast<uint8_t>(v & 0xFF);
    buf[off+1] = static_cast<uint8_t>((v >>  8) & 0xFF);
    buf[off+2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    buf[off+3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}
static void wf32(std::vector<uint8_t>& buf, size_t off, float v) {
    uint32_t bits = 0; std::memcpy(&bits, &v, 4); w32(buf, off, bits);
}
static void wf64(std::vector<uint8_t>& buf, size_t off, double v) {
    uint64_t bits = 0; std::memcpy(&bits, &v, 8);
    w32(buf, off,   static_cast<uint32_t>(bits & 0xFFFFFFFFu));
    w32(buf, off+4, static_cast<uint32_t>(bits >> 32));
}

struct XtfBuilder {
    std::vector<uint8_t> bytes;

    // XtfFileHeader (256 bytes).
    // Offsets: 0=FileFormat, 164=NavUnits, 166=NumSonarChans, 168=NumBathyChans.
    void writeFileHeader(uint16_t nav_units, uint16_t num_sss, uint16_t num_bathy = 0) {
        const size_t base = bytes.size();
        bytes.resize(base + 256, 0);
        bytes[base + 0] = 0x7B;   // FileFormat magic
        w16(bytes, base + 164, nav_units);
        w16(bytes, base + 166, num_sss);
        w16(bytes, base + 168, num_bathy);
    }

    // XtfChanInfo (128 bytes each).
    // Offsets: 0=TypeOfChannel, 1=SubChannelNumber, 6=BytesPerSample,
    //          8=SamplesPerChannel, 12=ChannelName[16], 28=VoltScale, 32=Frequency.
    void writeChanInfo(uint8_t type, uint8_t sub_chan, uint16_t bps,
                       uint32_t samples_per_chan, const char* name,
                       float frequency_khz = 100.f) {
        const size_t base = bytes.size();
        bytes.resize(base + 128, 0);
        bytes[base + 0] = type;
        bytes[base + 1] = sub_chan;
        w16(bytes, base + 6,  bps);
        w32(bytes, base + 8,  samples_per_chan);
        for (size_t i = 0; name[i] && i < 16; ++i)
            bytes[base + 12 + i] = static_cast<uint8_t>(name[i]);
        wf32(bytes, base + 28, 1.0f);             // VoltScale
        wf32(bytes, base + 32, frequency_khz);    // Frequency in kHz
    }

    // Pad to 1024 bytes (minimum file-header block boundary).
    void padTo1024() {
        while (bytes.size() < 1024)
            bytes.push_back(0);
    }

    struct ChanData {
        uint16_t chan_number = 0;
        float    slant_range = 75.f;
        std::vector<uint16_t> samples;
    };

    // PACKET_PING (HeaderType=0).
    // XtfPacketHeader offsets used:
    //   0=MagicNumber, 2=HeaderType, 3=SubChanNumber, 4=NumChansToFollow,
    //   10=NumBytesThisRecord, 14=Year, 22=JulianDay, 18-20=H/M/S,
    //   28=PingNumber, 32=SoundVelocity, 160=SensorYcoord, 168=SensorXcoord.
    // XtfPingChanHeader offsets:
    //   0=ChannelNumber, 4=SlantRange, 26=Frequency, 42=NumSamples.
    size_t writePing(double sensor_lat, double sensor_lon,
                     uint16_t year, uint16_t julian_day,
                     uint8_t hour, uint8_t min, uint8_t sec,
                     uint32_t ping_num,
                     const std::vector<ChanData>& chans) {
        const uint32_t data_bytes = [&]() {
            uint32_t n = 256; // XtfPacketHeader
            for (const auto& c : chans)
                n += 64u + static_cast<uint32_t>(c.samples.size()) * 2u;
            return n;
        }();

        const size_t pkt = bytes.size();
        bytes.resize(pkt + 256, 0);

        w16(bytes, pkt +   0, 0xFACE);          // MagicNumber
        bytes[pkt + 2] = 0;                     // HeaderType = PING
        bytes[pkt + 3] = 0;                     // SubChannelNumber
        w16(bytes, pkt +   4, static_cast<uint16_t>(chans.size()));
        w32(bytes, pkt +  10, data_bytes);       // NumBytesThisRecord
        w16(bytes, pkt +  14, year);
        bytes[pkt + 22] = static_cast<uint8_t>(julian_day & 0xFF);
        bytes[pkt + 23] = static_cast<uint8_t>((julian_day >> 8) & 0xFF);
        bytes[pkt + 18] = hour;
        bytes[pkt + 19] = min;
        bytes[pkt + 20] = sec;
        w32(bytes, pkt +  28, ping_num);
        wf32(bytes, pkt + 32, 1500.f);           // SoundVelocity
        wf64(bytes, pkt + 160, sensor_lat);      // SensorYcoordinate
        wf64(bytes, pkt + 168, sensor_lon);      // SensorXcoordinate

        for (const auto& c : chans) {
            const size_t ch = bytes.size();
            bytes.resize(ch + 64, 0);
            w16(bytes, ch +  0, c.chan_number);
            wf32(bytes, ch + 4, c.slant_range);
            w32(bytes, ch + 42, static_cast<uint32_t>(c.samples.size()));
            for (uint16_t s : c.samples) {
                bytes.push_back(static_cast<uint8_t>(s & 0xFF));
                bytes.push_back(static_cast<uint8_t>((s >> 8) & 0xFF));
            }
        }
        return pkt;
    }

    // PACKET_NAV (HeaderType=42).
    // Uses only the XtfPacketHeader (256 bytes); NumChansToFollow=0.
    size_t writeNav(double lat, double lon,
                    uint16_t year, uint16_t julian_day,
                    uint8_t hour, uint8_t min, uint8_t sec) {
        const size_t pkt = bytes.size();
        bytes.resize(pkt + 256, 0);
        w16(bytes, pkt +   0, 0xFACE);
        bytes[pkt + 2] = 42;                    // HeaderType = NAV
        w32(bytes, pkt +  10, 256);             // NumBytesThisRecord
        w16(bytes, pkt +  14, year);
        bytes[pkt + 22] = static_cast<uint8_t>(julian_day & 0xFF);
        bytes[pkt + 23] = static_cast<uint8_t>((julian_day >> 8) & 0xFF);
        bytes[pkt + 18] = hour;
        bytes[pkt + 19] = min;
        bytes[pkt + 20] = sec;
        wf64(bytes, pkt + 160, lat);
        wf64(bytes, pkt + 168, lon);
        return pkt;
    }

    // Generic 256-byte packet with an arbitrary HeaderType and no channels.
    // Used to exercise unsupported/unknown packet-type handling.
    size_t writeSimplePacket(uint8_t header_type,
                             uint16_t year, uint16_t julian_day,
                             uint8_t hour, uint8_t min, uint8_t sec) {
        const size_t pkt = bytes.size();
        bytes.resize(pkt + 256, 0);
        w16(bytes, pkt +  0, 0xFACE);
        bytes[pkt + 2] = header_type;
        w32(bytes, pkt + 10, 256);              // NumBytesThisRecord
        w16(bytes, pkt + 14, year);
        bytes[pkt + 22] = static_cast<uint8_t>(julian_day & 0xFF);
        bytes[pkt + 23] = static_cast<uint8_t>((julian_day >> 8) & 0xFF);
        bytes[pkt + 18] = hour;
        bytes[pkt + 19] = min;
        bytes[pkt + 20] = sec;
        return pkt;
    }

    bool saveTo(const std::string& path) const {
        FILE* f = nullptr;
#ifdef _WIN32
        fopen_s(&f, path.c_str(), "wb");
#else
        f = std::fopen(path.c_str(), "wb");
#endif
        if (!f) return false;
        const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
        std::fclose(f);
        return ok;
    }
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
//  FIX-001 — open() rejects file with wrong magic byte
// ─────────────────────────────────────────────────────────────────────────────

static void test_fix001_open_rejects_non_xtf()
{
    TempFile tmp;
    {
        FILE* f = nullptr;
#ifdef _WIN32
        fopen_s(&f, tmp.path.c_str(), "wb");
#else
        f = std::fopen(tmp.path.c_str(), "wb");
#endif
        if (f) {
            // Write 256 zero bytes — FileFormat = 0x00 (not 0x7B)
            std::vector<uint8_t> zeros(256, 0);
            std::fwrite(zeros.data(), 1, zeros.size(), f);
            std::fclose(f);
        }
    }

    dolphin::io::XtfReader reader;
    CHECK(!reader.open(tmp.path));
    CHECK(!reader.isOpen());
}

// ─────────────────────────────────────────────────────────────────────────────
//  FIX-002 — dual-channel SSS → 2 sidescan entries, correct lat/lon
// ─────────────────────────────────────────────────────────────────────────────

static void test_fix002_dual_channel_sss()
{
    TempFile tmp;
    {
        XtfBuilder b;
        b.writeFileHeader(/*nav_units=*/0, /*num_sss=*/2);
        b.writeChanInfo(/*type=*/1, 0, 2, 4, "Port",      100.f);
        b.writeChanInfo(/*type=*/2, 0, 2, 4, "Starboard", 100.f);
        b.padTo1024();
        b.writePing(48.0, 2.0, 2000, 100, 12, 0, 0, 1,
                    { {0, 75.f, {1000, 2000, 3000, 4000}},
                      {1, 75.f, {5000, 6000, 7000, 8000}} });
        CHECK(b.saveTo(tmp.path));
    }

    dolphin::io::XtfReader reader;
    CHECK(reader.open(tmp.path));

    const auto index = reader.buildIndex();
    CHECK(index.size() == 2);
    if (index.size() < 2) return;

    CHECK(index.entries[0].type == dolphin::core::ArtifactType::Sidescan);
    CHECK(index.entries[1].type == dolphin::core::ArtifactType::Sidescan);
    CHECK_CLOSE(index.entries[0].lat, 48.0, 1e-9);
    CHECK_CLOSE(index.entries[0].lon,  2.0, 1e-9);
    CHECK_CLOSE(index.entries[1].lat, 48.0, 1e-9);
    CHECK_CLOSE(index.entries[1].lon,  2.0, 1e-9);

    // One entry must be Port, the other Starboard
    bool found_port  = false;
    bool found_stbd  = false;
    for (const auto& e : index.entries) {
        auto art = reader.readArtifact(e);
        CHECK(art.has_value());
        if (!art) continue;
        CHECK(dolphin::core::artifactType(*art) == dolphin::core::ArtifactType::Sidescan);
        const auto& ping = std::get<dolphin::core::SidescanPing>(*art);
        CHECK(ping.samples.size() == 4);
        CHECK_CLOSE(ping.slant_range_m, 75.f, 1e-4f);
        if (ping.channel == dolphin::core::SidescanChannel::Port)  found_port = true;
        if (ping.channel == dolphin::core::SidescanChannel::Starboard) found_stbd = true;
    }
    CHECK(found_port);
    CHECK(found_stbd);
}

// ─────────────────────────────────────────────────────────────────────────────
//  FIX-003 — channel number out of range → entry skipped (classifyChannel fix)
//
//  Before fix: classifyChannel returned ArtifactType::Sidescan for any
//  chan_number >= m_chan_info.size() → bogus entries added to index.
//  After fix:  returns std::nullopt → entries are skipped.
// ─────────────────────────────────────────────────────────────────────────────

static void test_fix003_unknown_channel_skipped()
{
    TempFile tmp;
    {
        XtfBuilder b;
        // 1 channel in file header, but ping references channel number 5
        b.writeFileHeader(0, /*num_sss=*/1);
        b.writeChanInfo(1, 0, 2, 4, "Port");
        b.padTo1024();
        // chan_number=5 is out of range (m_chan_info.size()=1)
        b.writePing(48.0, 2.0, 2000, 100, 12, 0, 0, 1,
                    { {5, 75.f, {1000, 2000, 3000, 4000}} });
        CHECK(b.saveTo(tmp.path));
    }

    dolphin::io::XtfReader reader;
    CHECK(reader.open(tmp.path));

    const auto index = reader.buildIndex();
    // classifyChannel(5) with size=1 must return nullopt → no entry added
    CHECK(index.size() == 0);
}

// ─────────────────────────────────────────────────────────────────────────────
//  FIX-004 — PACKET_NAV backfill for pings that carry zero lat/lon
// ─────────────────────────────────────────────────────────────────────────────

static void test_fix004_nav_backfill()
{
    TempFile tmp;
    {
        XtfBuilder b;
        b.writeFileHeader(0, 1);
        b.writeChanInfo(1, 0, 2, 4, "Port");
        b.padTo1024();
        // NAV fix at Year=2000, JulianDay=1, 12:00:00
        b.writeNav(55.5, -3.0, 2000, 1, 12, 0, 0);
        // Ping with zero coords at Year=2000, JulianDay=1, 12:00:01 (1 s later)
        b.writePing(0.0, 0.0, 2000, 1, 12, 0, 1, 1,
                    { {0, 75.f, {100, 200, 300, 400}} });
        CHECK(b.saveTo(tmp.path));
    }

    dolphin::io::XtfReader reader;
    CHECK(reader.open(tmp.path));

    const auto index = reader.buildIndex();
    CHECK(index.size() == 1);
    if (index.empty()) return;

    // Ping had zero nav → backfilled from the preceding PACKET_NAV
    CHECK_CLOSE(index.entries[0].lat, 55.5, 1e-6);
    CHECK_CLOSE(index.entries[0].lon, -3.0, 1e-6);
}

// ─────────────────────────────────────────────────────────────────────────────
//  FIX-005 — NavUnits=3 → coordinate_ref is Projected
// ─────────────────────────────────────────────────────────────────────────────

static void test_fix005_projected_nav_units()
{
    TempFile tmp;
    {
        XtfBuilder b;
        b.writeFileHeader(/*nav_units=*/3, 1);
        b.writeChanInfo(1, 0, 2, 4, "Port");
        b.padTo1024();
        // Projected coordinates (UTM-style magnitudes)
        b.writePing(500000.0, 200000.0, 2000, 100, 12, 0, 0, 1,
                    { {0, 75.f, {100, 200, 300, 400}} });
        CHECK(b.saveTo(tmp.path));
    }

    dolphin::io::XtfReader reader;
    CHECK(reader.open(tmp.path));

    const auto index = reader.buildIndex();
    CHECK(index.size() == 1);
    if (index.empty()) return;

    CHECK(index.entries[0].is_projected);

    const auto meta = reader.metadata();
    CHECK(meta.coordinate_ref.kind == dolphin::core::SpatialRefKind::Projected);
}

// ─────────────────────────────────────────────────────────────────────────────
//  FIX-006 — BytesPerSample=0 in header → inferred from record geometry
// ─────────────────────────────────────────────────────────────────────────────

static void test_fix006_bps_inference()
{
    TempFile tmp;
    {
        XtfBuilder b;
        b.writeFileHeader(0, 1);
        // BytesPerSample=0 → bytes_per_sample_unknown=true, falls back to inference
        b.writeChanInfo(/*type=*/2/*stbd*/, 0, /*bps=*/0, 4, "Stbd");
        b.padTo1024();
        // 4 uint16_t samples → record = 256+64+8 = 328 bytes → inferred bps=2
        b.writePing(48.0, 2.0, 2000, 100, 12, 0, 0, 1,
                    { {0, 75.f, {1000, 2000, 3000, 4000}} });
        CHECK(b.saveTo(tmp.path));
    }

    dolphin::io::XtfReader reader;
    CHECK(reader.open(tmp.path));

    const auto index = reader.buildIndex();
    CHECK(index.size() == 1);
    if (index.empty()) return;

    auto art = reader.readArtifact(index.entries[0]);
    CHECK(art.has_value());
    if (!art) return;

    const auto& ping = std::get<dolphin::core::SidescanPing>(*art);
    // Starboard: no reversal — samples stored nadir-outward as written
    CHECK(ping.samples.size() == 4);
    CHECK(ping.samples[0].amplitude == 1000);
    CHECK(ping.samples[3].amplitude == 4000);
}

// ─────────────────────────────────────────────────────────────────────────────
//  FIX-007 — truncated record → partial index, no crash
// ─────────────────────────────────────────────────────────────────────────────

static void test_fix007_truncated_file()
{
    TempFile tmp;
    {
        XtfBuilder b;
        b.writeFileHeader(0, 1);
        b.writeChanInfo(1, 0, 2, 4, "Port");
        b.padTo1024();
        // First complete ping
        b.writePing(48.0, 2.0, 2000, 100, 12, 0, 0, 1,
                    { {0, 75.f, {100, 200, 300, 400}} });
        // Second ping header only — NumBytesThisRecord claims 328 bytes but we
        // write only the 256-byte XtfPacketHeader and stop.
        // buildIndex stops at this point: offset + record_bytes > fileSize.
        const size_t trunc_pkt = b.bytes.size();
        b.bytes.resize(trunc_pkt + 256, 0);
        // MagicNumber
        b.bytes[trunc_pkt + 0] = 0xCE;
        b.bytes[trunc_pkt + 1] = 0xFA;
        // HeaderType = PING
        b.bytes[trunc_pkt + 2] = 0;
        // NumChansToFollow = 1
        b.bytes[trunc_pkt + 4] = 1;
        // NumBytesThisRecord = 328 (but file ends before the channel data)
        w32(b.bytes, trunc_pkt + 10, 328);
        CHECK(b.saveTo(tmp.path));
    }

    dolphin::io::XtfReader reader;
    CHECK(reader.open(tmp.path));

    // Must not crash; second ping is beyond EOF so only 1 entry is indexed
    const auto index = reader.buildIndex();
    CHECK(index.size() == 1);
}

// ─────────────────────────────────────────────────────────────────────────────
//  FIX-008 — sub-bottom channel (TypeOfChannel=0) → SubBottom ArtifactType
// ─────────────────────────────────────────────────────────────────────────────

static void test_fix008_subbottom_channel()
{
    TempFile tmp;
    {
        XtfBuilder b;
        b.writeFileHeader(0, /*num_sss=*/0, /*num_bathy=*/0);
        // Override: treat this as 1 sonar channel of type 0 (sub-bottom)
        // NumberOfSonarChannels drives the chan_info read loop.
        // Re-write NumSonarChans = 1 into the already-written header (offset 166).
        b.bytes[166] = 1;
        b.writeChanInfo(/*type=*/0, 0, 2, 4, "SubBot", 3.5f);
        b.padTo1024();
        b.writePing(51.0, 4.0, 2000, 100, 12, 0, 0, 1,
                    { {0, 75.f, {1000, 32768, 40000, 65000}} });
        CHECK(b.saveTo(tmp.path));
    }

    dolphin::io::XtfReader reader;
    CHECK(reader.open(tmp.path));

    const auto index = reader.buildIndex();
    CHECK(index.size() == 1);
    if (index.empty()) return;

    CHECK(index.entries[0].type == dolphin::core::ArtifactType::SubBottom);

    auto art = reader.readArtifact(index.entries[0]);
    CHECK(art.has_value());
    if (!art) return;

    CHECK(dolphin::core::artifactType(*art) == dolphin::core::ArtifactType::SubBottom);
    const auto& trace = std::get<dolphin::core::SubBottomTrace>(*art);
    CHECK(trace.samples.size() == 4);
    // raw uint16_t 32768 normalizes to (32768 - 32768) / 32768 = 0.0
    CHECK_CLOSE(trace.samples[1], 0.0f, 1e-5f);
    // raw uint16_t 1000 normalizes to (1000 - 32768) / 32768 ≈ -0.9695
    CHECK_CLOSE(trace.samples[0], (1000.f - 32768.f) / 32768.f, 1e-4f);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Diagnostic query helpers
// ─────────────────────────────────────────────────────────────────────────────

static bool hasDiagnostic(const dolphin::io::XtfReader& r,
                          dolphin::core::ImportDiagnosticCode code)
{
    for (const auto& d : r.diagnostics())
        if (d.code == code) return true;
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
//  FIX-009 — bad packet magic → ResyncedPacket diagnostic emitted
//
//  A file where the first packet block has a wrong magic number, followed by
//  a valid ping packet.  buildIndex must emit at least one ResyncedPacket
//  diagnostic and still index the valid ping.
// ─────────────────────────────────────────────────────────────────────────────

static void test_fix009_bad_magic_diagnostic()
{
    TempFile tmp;
    {
        XtfBuilder b;
        b.writeFileHeader(0, 1);
        b.writeChanInfo(1, 0, 2, 4, "Port");
        b.padTo1024();

        // Write a junk block (256 bytes, magic = 0xDEAD) before the valid ping.
        const size_t junk = b.bytes.size();
        b.bytes.resize(junk + 256, 0xAB);
        b.bytes[junk + 0] = 0xAD;   // wrong magic 0xDEAD (LE)
        b.bytes[junk + 1] = 0xDE;
        w32(b.bytes, junk + 10, 256);  // NumBytesThisRecord — irrelevant since magic is wrong

        // Valid ping follows immediately after.
        b.writePing(48.0, 2.0, 2000, 100, 12, 0, 0, 1,
                    { {0, 75.f, {100, 200, 300, 400}} });
        CHECK(b.saveTo(tmp.path));
    }

    dolphin::io::XtfReader reader;
    CHECK(reader.open(tmp.path));
    const auto index = reader.buildIndex();

    CHECK(index.size() == 1);
    CHECK(hasDiagnostic(reader, dolphin::core::ImportDiagnosticCode::ResyncedPacket));
}

// ─────────────────────────────────────────────────────────────────────────────
//  FIX-010 — BytesPerSample=0 in channel header → InferredBytesPerSample emitted
//
//  Reuses the same fixture as FIX-006 but asserts the diagnostic side-effect.
// ─────────────────────────────────────────────────────────────────────────────

static void test_fix010_bps_inference_diagnostic()
{
    TempFile tmp;
    {
        XtfBuilder b;
        b.writeFileHeader(0, 1);
        b.writeChanInfo(/*type=*/2, 0, /*bps=*/0, 4, "Stbd");  // BPS = 0 → unknown
        b.padTo1024();
        b.writePing(48.0, 2.0, 2000, 100, 12, 0, 0, 1,
                    { {0, 75.f, {1000, 2000, 3000, 4000}} });
        CHECK(b.saveTo(tmp.path));
    }

    dolphin::io::XtfReader reader;
    CHECK(reader.open(tmp.path));
    reader.buildIndex();

    CHECK(hasDiagnostic(reader,
                        dolphin::core::ImportDiagnosticCode::InferredBytesPerSample));
}

// ─────────────────────────────────────────────────────────────────────────────
//  FIX-011 — zero-nav pings backfilled from PACKET_NAV → InterpolatedNavigation
//
//  Two pings bracketed by two PACKET_NAV fixes; both pings have zero sensor
//  coords so their nav must be interpolated.  Expects InterpolatedNavigation
//  diagnostic with count ≥ 1.
// ─────────────────────────────────────────────────────────────────────────────

static void test_fix011_interpolated_nav_diagnostic()
{
    TempFile tmp;
    {
        XtfBuilder b;
        b.writeFileHeader(0, 1);
        b.writeChanInfo(1, 0, 2, 4, "Port");
        b.padTo1024();

        // NAV fix before the pings.
        b.writeNav(55.0, -3.0, 2000, 1, 12, 0, 0);
        // Ping 1 — zero nav, timestamp between the two fixes.
        b.writePing(0.0, 0.0, 2000, 1, 12, 0, 1, 1,
                    { {0, 75.f, {100, 200, 300, 400}} });
        // Ping 2 — zero nav, timestamp between the two fixes.
        b.writePing(0.0, 0.0, 2000, 1, 12, 0, 2, 2,
                    { {0, 75.f, {500, 600, 700, 800}} });
        // NAV fix after the pings.
        b.writeNav(55.1, -2.9, 2000, 1, 12, 0, 3);
        CHECK(b.saveTo(tmp.path));
    }

    dolphin::io::XtfReader reader;
    CHECK(reader.open(tmp.path));
    const auto index = reader.buildIndex();

    CHECK(index.size() == 2);

    // Both pings had zero coords and fall between two nav fixes → interpolated.
    CHECK(hasDiagnostic(reader,
                        dolphin::core::ImportDiagnosticCode::InterpolatedNavigation));

    // Spot-check: interpolated positions must be between the two fixes.
    for (const auto& e : index.entries) {
        CHECK(e.lat >= 55.0 && e.lat <= 55.1);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  FIX-012 — bathymetry channel (TypeOfChannel=3) → UnsupportedChannelType
//
//  A file declaring one port SSS channel plus one bathymetry channel.  The SSS
//  ping is still indexed, while the recognized-but-unsupported bathymetry
//  channel produces an UnsupportedChannelType diagnostic instead of silently
//  vanishing.
// ─────────────────────────────────────────────────────────────────────────────

static void test_fix012_bathymetry_channel_unsupported()
{
    TempFile tmp;
    {
        XtfBuilder b;
        b.writeFileHeader(/*nav_units=*/0, /*num_sss=*/1, /*num_bathy=*/1);
        b.writeChanInfo(/*type=*/1, 0, 2, 4, "Port");          // supported SSS
        b.writeChanInfo(/*type=*/3, 0, 2, 4, "Bathy", 200.f);  // bathymetry
        b.padTo1024();
        b.writePing(48.0, 2.0, 2000, 100, 12, 0, 0, 1,
                    { {0, 75.f, {1000, 2000, 3000, 4000}} });
        CHECK(b.saveTo(tmp.path));
    }

    dolphin::io::XtfReader reader;
    CHECK(reader.open(tmp.path));
    const auto index = reader.buildIndex();

    // Only the SSS ping is indexed; the bathymetry channel is not.
    CHECK(index.size() == 1);
    if (!index.empty())
        CHECK(index.entries[0].type == dolphin::core::ArtifactType::Sidescan);

    // The declared bathymetry channel must be reported, not silently dropped.
    CHECK(hasDiagnostic(reader,
                        dolphin::core::ImportDiagnosticCode::UnsupportedChannelType));
}

// ─────────────────────────────────────────────────────────────────────────────
//  FIX-013 — unknown packet type → UnsupportedPacketType, valid ping still kept
//
//  A junk packet with an unrecognized HeaderType sits between two valid pings.
//  buildIndex must emit an UnsupportedPacketType diagnostic and still index both
//  surrounding sidescan pings.
// ─────────────────────────────────────────────────────────────────────────────

static void test_fix013_unknown_packet_type()
{
    TempFile tmp;
    {
        XtfBuilder b;
        b.writeFileHeader(0, 1);
        b.writeChanInfo(1, 0, 2, 4, "Port");
        b.padTo1024();
        b.writePing(48.0, 2.0, 2000, 100, 12, 0, 0, 1,
                    { {0, 75.f, {100, 200, 300, 400}} });
        // Unrecognized packet type 99 between the two pings.
        b.writeSimplePacket(/*header_type=*/99, 2000, 100, 12, 0, 1);
        b.writePing(48.1, 2.1, 2000, 100, 12, 0, 2, 2,
                    { {0, 75.f, {500, 600, 700, 800}} });
        CHECK(b.saveTo(tmp.path));
    }

    dolphin::io::XtfReader reader;
    CHECK(reader.open(tmp.path));
    const auto index = reader.buildIndex();

    // Both valid pings indexed; the unknown packet between them is skipped.
    CHECK(index.size() == 2);
    CHECK(hasDiagnostic(reader,
                        dolphin::core::ImportDiagnosticCode::UnsupportedPacketType));
}

// ─────────────────────────────────────────────────────────────────────────────
//  FIX-014 — split-packet sidescan: one channel per PACKET_PING
//
//  Unlike FIX-002 (both channels in one ping), here port and starboard arrive as
//  two separate PACKET_PING records, each with NumChansToFollow=1.  Both must be
//  indexed and classified as Port and Starboard respectively.
// ─────────────────────────────────────────────────────────────────────────────

static void test_fix014_split_packet_sidescan()
{
    TempFile tmp;
    {
        XtfBuilder b;
        b.writeFileHeader(/*nav_units=*/0, /*num_sss=*/2);
        b.writeChanInfo(/*type=*/1, 0, 2, 4, "Port",      100.f);
        b.writeChanInfo(/*type=*/2, 0, 2, 4, "Starboard", 100.f);
        b.padTo1024();
        // Port arrives in its own packet …
        b.writePing(48.0, 2.0, 2000, 100, 12, 0, 0, 1,
                    { {0, 75.f, {1000, 2000, 3000, 4000}} });
        // … starboard in the next packet.
        b.writePing(48.0, 2.0, 2000, 100, 12, 0, 1, 2,
                    { {1, 75.f, {5000, 6000, 7000, 8000}} });
        CHECK(b.saveTo(tmp.path));
    }

    dolphin::io::XtfReader reader;
    CHECK(reader.open(tmp.path));

    const auto index = reader.buildIndex();
    CHECK(index.size() == 2);
    if (index.size() < 2) return;

    bool found_port = false;
    bool found_stbd = false;
    for (const auto& e : index.entries) {
        CHECK(e.type == dolphin::core::ArtifactType::Sidescan);
        auto art = reader.readArtifact(e);
        CHECK(art.has_value());
        if (!art) continue;
        const auto& ping = std::get<dolphin::core::SidescanPing>(*art);
        if (ping.channel == dolphin::core::SidescanChannel::Port)      found_port = true;
        if (ping.channel == dolphin::core::SidescanChannel::Starboard) found_stbd = true;
    }
    CHECK(found_port);
    CHECK(found_stbd);
}

// ─────────────────────────────────────────────────────────────────────────────
//  FIX-015 — dual-frequency sidescan (Edgetech 4200-style SubChannelNumber routing)
//
//  File header declares 4 SSS channels: LF port/stbd (100 kHz) and HF port/stbd
//  (400 kHz).  Packets reuse ChannelNumber 0/1 and select the band via
//  pkt.SubChannelNumber: 0 → LF (chan-info index 0/1), 1 → HF (index 2/3).
//  The reader must route each ping to the correct channel-info entry so both
//  frequency bands are detected and stamped onto the right artifacts.
// ─────────────────────────────────────────────────────────────────────────────

static void test_fix015_dual_frequency_routing()
{
    TempFile tmp;
    {
        XtfBuilder b;
        b.writeFileHeader(/*nav_units=*/0, /*num_sss=*/4);
        b.writeChanInfo(/*type=*/1, 0, 2, 4, "LF Port", 100.f);
        b.writeChanInfo(/*type=*/2, 1, 2, 4, "LF Stbd", 100.f);
        b.writeChanInfo(/*type=*/1, 0, 2, 4, "HF Port", 400.f);
        b.writeChanInfo(/*type=*/2, 1, 2, 4, "HF Stbd", 400.f);
        b.padTo1024();
        // LF ping — SubChannelNumber=0 (default), ChannelNumbers 0/1 → index 0/1.
        b.writePing(48.0, 2.0, 2000, 100, 12, 0, 0, 1,
                    { {0, 75.f, {100, 200, 300, 400}},
                      {1, 75.f, {500, 600, 700, 800}} });
        // HF ping — SubChannelNumber=1, ChannelNumbers 0/1 → index 2/3.
        const size_t hf_pkt =
            b.writePing(48.0, 2.0, 2000, 100, 12, 0, 1, 2,
                        { {0, 75.f, {110, 220, 330, 440}},
                          {1, 75.f, {550, 660, 770, 880}} });
        b.bytes[hf_pkt + 3] = 1;   // XtfPacketHeader::SubChannelNumber = 1 (HF band)
        CHECK(b.saveTo(tmp.path));
    }

    dolphin::io::XtfReader reader;
    CHECK(reader.open(tmp.path));

    const auto index = reader.buildIndex();
    CHECK(index.size() == 4);
    if (index.size() < 4) return;

    // Both frequency bands must be discovered and ordered HF (primary) > LF.
    const auto meta = reader.metadata();
    CHECK_CLOSE(meta.frequency_hz,     400000.f, 1.f);
    CHECK_CLOSE(meta.low_frequency_hz, 100000.f, 1.f);

    // Each entry must carry the frequency of the band it was routed to.
    int lf_count = 0, hf_count = 0;
    for (const auto& e : index.entries) {
        if (std::abs(e.frequency_hz - 100000.f) < 1.f) ++lf_count;
        if (std::abs(e.frequency_hz - 400000.f) < 1.f) ++hf_count;
    }
    CHECK(lf_count == 2);
    CHECK(hf_count == 2);

    // Round-trip the HF entries: frequency and channel must survive decode.
    for (const auto& e : index.entries) {
        if (std::abs(e.frequency_hz - 400000.f) >= 1.f) continue;
        auto art = reader.readArtifact(e);
        CHECK(art.has_value());
        if (!art) continue;
        const auto& ping = std::get<dolphin::core::SidescanPing>(*art);
        CHECK_CLOSE(ping.frequency_hz, 400000.f, 1.f);
        CHECK(ping.channel == dolphin::core::SidescanChannel::Port
           || ping.channel == dolphin::core::SidescanChannel::Starboard);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Real-vendor reduced fixtures (Slice 02D)
//
//  Unlike FIX-001…FIX-015 (synthetic XtfBuilder byte streams), these load
//  reduced captures of real vendor files checked into tests/fixtures/.  Each
//  keeps the original 1024-byte file-header block plus the first 8 ping packets
//  (see scripts/xtf_reduce.ps1).  They satisfy the Stage 02 ">=2 real
//  vendor/recorder families" bar.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef XTF_FIXTURE_DIR
#define XTF_FIXTURE_DIR "fixtures"
#endif

static std::string fixturePath(const char* name)
{
    return (std::filesystem::path(XTF_FIXTURE_DIR) / name).string();
}

// FIX-016 — Edgetech 4200.E recorded via Triton Isis (NBP0505 survey).
// 4 SSS channels (PORT_LOW/STBD_LOW @120 kHz, PORT_HI/STBD_HI @410 kHz),
// 16-bit samples.  The header declares NavUnits=3 (projected), but the fixes
// are real lat/lon degrees, so the reader overrides the CRS to Geographic.
static void test_fix016_edgetech4200_isis_real()
{
    const std::string path = fixturePath("fix016_edgetech4200_isis_reduced.xtf");

    dolphin::io::XtfReader reader;
    CHECK(reader.open(path));
    if (!reader.isOpen()) return;

    const auto index = reader.buildIndex();
    const auto meta  = reader.metadata();

    // Sonar identified from header (SonarType=38 / SonarName "Edgetech_4200.E").
    CHECK(meta.sonar_name.find("4200") != std::string::npos);

    // NavUnits=3 declared Projected, but magnitudes fit WGS-84 → overridden.
    CHECK(meta.coordinate_ref.kind == dolphin::core::SpatialRefKind::Geographic);
    CHECK(hasDiagnostic(reader,
                        dolphin::core::ImportDiagnosticCode::CoordinateSystemOverridden));

    // Dual-frequency must be detected: 410 kHz primary, 120 kHz secondary.
    CHECK_CLOSE(meta.frequency_hz,     410000.f, 1.f);
    CHECK_CLOSE(meta.low_frequency_hz, 120000.f, 1.f);

    // 8 kept pings × 4 channels → 32 sidescan entries, all flagged geographic.
    CHECK(index.size() == 32);
    bool found_port = false, found_stbd = false, found_lf = false, found_hf = false;
    bool samples_ok = false;
    for (const auto& e : index.entries) {
        CHECK(e.type == dolphin::core::ArtifactType::Sidescan);
        CHECK(!e.is_projected);
        if (std::abs(e.frequency_hz - 120000.f) < 1.f) found_lf = true;
        if (std::abs(e.frequency_hz - 410000.f) < 1.f) found_hf = true;
        auto art = reader.readArtifact(e);
        CHECK(art.has_value());
        if (!art) continue;
        const auto& ping = std::get<dolphin::core::SidescanPing>(*art);
        if (!ping.samples.empty()) samples_ok = true;
        if (ping.channel == dolphin::core::SidescanChannel::Port)      found_port = true;
        if (ping.channel == dolphin::core::SidescanChannel::Starboard) found_stbd = true;
    }
    CHECK(found_port);
    CHECK(found_stbd);
    CHECK(found_lf);
    CHECK(found_hf);
    CHECK(samples_ok);
}

// FIX-017 — TST 2024 recorder, 500 kHz, 32-bit samples (BytesPerSample=4),
// 2 SSS channels (port + starboard).  The header declares NavUnits=0
// (geographic), but the fixes are projected metres, so the reader overrides
// the CRS to Projected.  This is the only fixture exercising the 32-bit path.
static void test_fix017_tst500k_32bit_real()
{
    const std::string path = fixturePath("fix017_tst500k_32bit_reduced.xtf");

    dolphin::io::XtfReader reader;
    CHECK(reader.open(path));
    if (!reader.isOpen()) return;

    const auto index = reader.buildIndex();
    const auto meta  = reader.metadata();

    // NavUnits=0 declared Geographic, but magnitudes exceed WGS-84 → overridden.
    CHECK(meta.coordinate_ref.kind == dolphin::core::SpatialRefKind::Projected);
    CHECK(hasDiagnostic(reader,
                        dolphin::core::ImportDiagnosticCode::CoordinateSystemOverridden));

    // Single 500 kHz band: primary set, no second band.
    CHECK_CLOSE(meta.frequency_hz, 500000.f, 1.f);
    CHECK(meta.low_frequency_hz == 0.f);

    // 8 kept pings × 2 channels → 16 sidescan entries, all flagged projected.
    // The 32-bit sample path must decode to non-empty pings on both channels.
    CHECK(index.size() == 16);
    bool found_port = false, found_stbd = false, samples_ok = false;
    for (const auto& e : index.entries) {
        CHECK(e.type == dolphin::core::ArtifactType::Sidescan);
        CHECK(e.is_projected);
        auto art = reader.readArtifact(e);
        CHECK(art.has_value());
        if (!art) continue;
        const auto& ping = std::get<dolphin::core::SidescanPing>(*art);
        if (!ping.samples.empty()) samples_ok = true;
        if (ping.channel == dolphin::core::SidescanChannel::Port)      found_port = true;
        if (ping.channel == dolphin::core::SidescanChannel::Starboard) found_stbd = true;
    }
    CHECK(found_port);
    CHECK(found_stbd);
    CHECK(samples_ok);
}

// ─────────────────────────────────────────────────────────────────────────────
//  main
// ─────────────────────────────────────────────────────────────────────────────

int main()
{
    test_fix001_open_rejects_non_xtf();
    test_fix002_dual_channel_sss();
    test_fix003_unknown_channel_skipped();
    test_fix004_nav_backfill();
    test_fix005_projected_nav_units();
    test_fix006_bps_inference();
    test_fix007_truncated_file();
    test_fix008_subbottom_channel();
    test_fix009_bad_magic_diagnostic();
    test_fix010_bps_inference_diagnostic();
    test_fix011_interpolated_nav_diagnostic();
    test_fix012_bathymetry_channel_unsupported();
    test_fix013_unknown_packet_type();
    test_fix014_split_packet_sidescan();
    test_fix015_dual_frequency_routing();
    test_fix016_edgetech4200_isis_real();
    test_fix017_tst500k_32bit_real();

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
