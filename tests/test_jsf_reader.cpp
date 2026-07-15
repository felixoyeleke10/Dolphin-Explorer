// JSF framing and EdgeTech Message Type 80 regression tests.

#include "io/jsf/JsfReader.h"
#include "io/ProbeDispatch.h"
#include "core/Artifact.h"

#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

static int g_pass = 0;
static int g_fail = 0;

static void check(bool cond, const char* expr, const char* file, int line)
{
    if (cond) ++g_pass;
    else {
        ++g_fail;
        std::fprintf(stderr, "FAIL  %s:%d  %s\n", file, line, expr);
    }
}
#define CHECK(x) check((x), #x, __FILE__, __LINE__)
#define CHECK_CLOSE(a, b, eps) \
    check(std::abs(static_cast<double>(a) - static_cast<double>(b)) <= (eps), \
          #a " ~= " #b, __FILE__, __LINE__)

namespace {

struct TempFile {
    std::string path;
    TempFile()
    {
        path = (std::filesystem::temp_directory_path()
            / ("dolphin_jsf_" + std::to_string(static_cast<uint64_t>(
                std::chrono::steady_clock::now().time_since_epoch().count()))
                + ".jsf")).string();
    }
    ~TempFile()
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

void putU16(uint8_t* p, uint16_t value)
{
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
}

void putU32(uint8_t* p, uint32_t value)
{
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
    p[2] = static_cast<uint8_t>(value >> 16);
    p[3] = static_cast<uint8_t>(value >> 24);
}

void putI16(uint8_t* p, int16_t value)
{
    putU16(p, static_cast<uint16_t>(value));
}

void putI32(uint8_t* p, int32_t value)
{
    putU32(p, static_cast<uint32_t>(value));
}

void putF32(uint8_t* p, float value)
{
    putU32(p, std::bit_cast<uint32_t>(value));
}

struct SonarSpec {
    uint8_t subsystem = 20;
    uint8_t channel = 0;
    uint16_t data_format = 0;
    uint32_t time_sec = 1'700'000'000u;
    uint32_t ping_number = 42;
    uint32_t interval_ns = 100'000u;
    uint16_t frequency_dahz = 10'000u; // 100 kHz
    std::vector<int16_t> words{1000, 2000};
};

void writeMessageHeader(std::ofstream& out, uint16_t type, uint8_t subsystem,
                        uint8_t channel, uint32_t body_size)
{
    std::array<uint8_t, 16> hdr{};
    putU16(&hdr[0], 0x1601);
    hdr[2] = 0x0D;
    putU16(&hdr[4], type);
    hdr[6] = 2;
    hdr[7] = subsystem;
    hdr[8] = channel;
    putU32(&hdr[12], body_size);
    out.write(reinterpret_cast<const char*>(hdr.data()), hdr.size());
}

void writeSonarFile(const std::string& path, const SonarSpec& spec)
{
    std::array<uint8_t, 240> trace{};
    putU32(&trace[0], spec.time_sec);
    putU32(&trace[8], spec.ping_number);
    putU16(&trace[30], static_cast<uint16_t>(
        (1u << 0) | (1u << 3) | (1u << 5) | (1u << 6)
        | (1u << 9) | (1u << 11) | (1u << 13)));
    putU16(&trace[34], spec.data_format);
    putI32(&trace[80], -52 * 600'000); // longitude, 1/10000 arc-minute
    putI32(&trace[84],  48 * 600'000); // latitude
    putU16(&trace[88], 2);

    const uint32_t components = (spec.data_format == 1 || spec.data_format == 9) ? 2u : 1u;
    const uint32_t sample_count = components > 0
        ? static_cast<uint32_t>(spec.words.size()) / components : 0u;
    putU16(&trace[114], static_cast<uint16_t>(sample_count));
    putU32(&trace[116], spec.interval_ns);
    putU16(&trace[120], 7);
    putU16(&trace[126], spec.frequency_dahz);
    putU16(&trace[128], spec.frequency_dahz);
    putI32(&trace[136], 5'000);   // depth mm
    putI32(&trace[144], 10'000);  // altitude mm
    putU16(&trace[172], 12'345);  // heading 123.45 degrees
    putI16(&trace[174], 1'820);   // pitch ~10 degrees
    putI16(&trace[176], -910);    // roll ~-5 degrees
    putU32(&trace[200], 250);     // millisecond component
    putF32(&trace[228], 12.5f);
    putU16(&trace[236], 250);     // cable out 25.0 m

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    const uint32_t body_size = static_cast<uint32_t>(trace.size()
        + spec.words.size() * sizeof(uint16_t));
    writeMessageHeader(out, 80, spec.subsystem, spec.channel, body_size);
    out.write(reinterpret_cast<const char*>(trace.data()), trace.size());
    for (const int16_t word : spec.words) {
        uint8_t bytes[2];
        putI16(bytes, word);
        out.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
    }
}

} // namespace

static void testRejectsJunkAndTruncation()
{
    using dolphin::io::JsfReader;

    TempFile junk;
    {
        std::ofstream out(junk.path, std::ios::binary);
        out << "not a JSF file";
    }
    JsfReader reader;
    CHECK(!reader.open(junk.path));
    const auto junk_probe = reader.probe(junk.path);
    CHECK(!junk_probe.success);
    CHECK(!junk_probe.error_message.empty());

    TempFile truncated;
    {
        std::ofstream out(truncated.path, std::ios::binary);
        writeMessageHeader(out, 80, 20, 0, 244); // body deliberately absent
    }
    CHECK(!reader.open(truncated.path));
    const auto truncated_probe = reader.probe(truncated.path);
    CHECK(!truncated_probe.success);
    CHECK(!truncated_probe.error_message.empty());

    TempFile metadata_only;
    {
        std::ofstream out(metadata_only.path, std::ios::binary);
        writeMessageHeader(out, 182, 0, 0, 4);
        const uint8_t body[4]{};
        out.write(reinterpret_cast<const char*>(body), sizeof(body));
    }
    CHECK(reader.open(metadata_only.path));
    reader.close();
    const auto metadata_probe = reader.probe(metadata_only.path);
    CHECK(!metadata_probe.success);
    CHECK(metadata_probe.error_message.find("no supported sonar") != std::string::npos);
}

static void testSidescanMessage80()
{
    using namespace dolphin;
    TempFile file;
    writeSonarFile(file.path, {});

    io::JsfReader reader;
    const auto probe = reader.probe(file.path);
    CHECK(probe.success);
    CHECK(probe.has_sidescan);
    CHECK(!probe.has_subbottom);
    CHECK(probe.channels.size() == 1);
    CHECK(probe.channels[0].name == "Port Sidescan");
    CHECK_CLOSE(probe.channels[0].frequency_khz, 100.0, 1e-4);
    CHECK(probe.coord_valid);
    CHECK_CLOSE(probe.coord_min_x, -52.0, 1e-9);
    CHECK_CLOSE(probe.coord_min_y, 48.0, 1e-9);
    CHECK(probe.heading_valid);
    CHECK_CLOSE(probe.heading_mean, 123.45, 1e-3);

    CHECK(reader.open(file.path));
    const auto index = reader.buildIndex();
    CHECK(index.size() == 1);
    CHECK(index.entries[0].type == core::ArtifactType::Sidescan);
    CHECK(index.entries[0].ping_number == 42);
    CHECK(index.entries[0].timestamp_us == 1'700'000'000'250'000LL);
    CHECK_CLOSE(index.entries[0].frequency_hz, 100'000.0, 1.0);
    CHECK_CLOSE(index.entries[0].lon, -52.0, 1e-9);
    CHECK_CLOSE(index.entries[0].lat, 48.0, 1e-9);

    const auto artifact = reader.readArtifact(index.entries[0]);
    CHECK(artifact.has_value());
    const auto* ping = artifact ? std::get_if<core::SidescanPing>(&*artifact) : nullptr;
    CHECK(ping != nullptr);
    if (!ping) return;
    CHECK(ping->ping_number == 42);
    CHECK(ping->channel == core::SidescanChannel::Port);
    CHECK(ping->samples.size() == 2);
    CHECK(ping->samples[0].amplitude == 1000);
    CHECK(ping->samples[1].amplitude == 2000);
    CHECK_CLOSE(ping->nav.lon, -52.0, 1e-9);
    CHECK_CLOSE(ping->nav.lat, 48.0, 1e-9);
    CHECK_CLOSE(ping->nav.heading_deg, 123.45, 1e-3);
    CHECK_CLOSE(ping->sample_rate_hz, 10'000.0, 1e-3);
    CHECK_CLOSE(ping->slant_range_m, 0.15, 1e-5);
    CHECK_CLOSE(ping->tow_depth_m, 5.0, 1e-5);
    CHECK_CLOSE(ping->nav.altitude_m, 10.0, 1e-5);
    CHECK_CLOSE(ping->layback_m, 12.5, 1e-5);
    CHECK_CLOSE(ping->cable_out_m, 25.0, 1e-5);
}

static void testSubbottomAndUnsupportedEncoding()
{
    using namespace dolphin;

    TempFile sbp_file;
    SonarSpec sbp;
    sbp.subsystem = 0;
    sbp.channel = 0;
    sbp.data_format = 2;
    sbp.words = {static_cast<int16_t>(-32768), 0, 16384};
    writeSonarFile(sbp_file.path, sbp);

    io::JsfReader reader;
    const auto probe = reader.probe(sbp_file.path);
    CHECK(probe.success);
    CHECK(probe.has_subbottom);
    CHECK(!probe.has_sidescan);
    CHECK(probe.channels.size() == 1);
    CHECK(probe.channels[0].name == "Sub-Bottom");

    CHECK(reader.open(sbp_file.path));
    const auto index = reader.buildIndex();
    CHECK(index.size() == 1);
    CHECK(index.entries[0].type == core::ArtifactType::SubBottom);
    const auto artifact = reader.readArtifact(index.entries[0]);
    const auto* trace = artifact ? std::get_if<core::SubBottomTrace>(&*artifact) : nullptr;
    CHECK(trace != nullptr);
    if (trace) {
        CHECK(trace->samples.size() == 3);
        CHECK_CLOSE(trace->samples[0], -1.0, 1e-6);
        CHECK_CLOSE(trace->samples[1],  0.0, 1e-6);
        CHECK_CLOSE(trace->samples[2],  0.5, 1e-6);
    }
    reader.close();

    TempFile compressed;
    SonarSpec unsupported;
    unsupported.data_format = 300;
    writeSonarFile(compressed.path, unsupported);
    const auto unsupported_probe = reader.probe(compressed.path);
    CHECK(!unsupported_probe.success);
    CHECK(unsupported_probe.error_message.find("compressed") != std::string::npos);
    CHECK(reader.open(compressed.path));
    CHECK(reader.buildIndex().empty());
    CHECK(!reader.diagnostics().empty());
}

static void testTruthfulImportFilters()
{
    using dolphin::core::ArtifactType;
    using dolphin::io::fileFilterForArtifactType;

    const std::string sss = fileFilterForArtifactType(ArtifactType::Sidescan);
    CHECK(sss.find("*.xtf") != std::string::npos);
    CHECK(sss.find("*.jsf") != std::string::npos);
    CHECK(sss.find("*.dlpd") != std::string::npos);
    CHECK(sss.find("*.segy") == std::string::npos);

    const std::string sbp = fileFilterForArtifactType(ArtifactType::SubBottom);
    CHECK(sbp.find("*.xtf") != std::string::npos);
    CHECK(sbp.find("*.jsf") != std::string::npos);
    CHECK(sbp.find("*.segy") != std::string::npos);
    CHECK(sbp.find("*.dlpd") != std::string::npos);

    const std::string multibeam =
        fileFilterForArtifactType(ArtifactType::Multibeam);
    CHECK(multibeam.find("*.dlpd") == std::string::npos);
    CHECK(multibeam.find("*.unsupported") != std::string::npos);
}

int main()
{
    testRejectsJunkAndTruncation();
    testSidescanMessage80();
    testSubbottomAndUnsupportedEncoding();
    testTruthfulImportFilters();
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
