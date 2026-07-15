// Structural regression test for util::ZipWriter (backs the .docx export).
// Validates a real ZIP container without an external unzip: local-file-header and
// EOCD signatures, the EOCD entry count, and that STORE'd payloads appear verbatim.
//
// Minimal CHECK harness. Entry: ctest --output-on-failure
#include "util/ZipWriter.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

static int g_pass = 0, g_fail = 0;
static void check(bool cond, const char* expr, const char* file, int line)
{
    if (cond) ++g_pass;
    else { ++g_fail; std::fprintf(stderr, "FAIL  %s:%d  %s\n", file, line, expr); }
}
#define CHECK(x) check((x), #x, __FILE__, __LINE__)

static bool contains(const std::string& hay, const std::string& needle)
{
    return hay.find(needle) != std::string::npos;
}

int main()
{
    using dolphin::util::ZipWriter;

    const std::string ct  = R"(<?xml version="1.0"?><Types/>)";
    const std::string doc = R"(<?xml version="1.0"?><w:document>HELLO_ZIP_PAYLOAD</w:document>)";

    auto tmp = std::filesystem::temp_directory_path() / "dolphin_ziptest.zip";
    const std::string path = tmp.string();

    {
        ZipWriter z;
        z.addFile("[Content_Types].xml", ct);
        z.addFile("word/document.xml", doc);
        CHECK(z.writeToFile(path));
    }

    std::ifstream in(path, std::ios::binary);
    CHECK(in.good());
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    CHECK(bytes.size() > 64);

    // Local file header signature "PK\x03\x04" at the very start.
    CHECK(bytes.size() >= 4 &&
          (unsigned char)bytes[0] == 0x50 && (unsigned char)bytes[1] == 0x4B &&
          (unsigned char)bytes[2] == 0x03 && (unsigned char)bytes[3] == 0x04);

    // End-of-central-directory record "PK\x05\x06".
    const std::string eocdSig = std::string("PK\x05\x06", 4);
    const auto eocd = bytes.rfind(eocdSig);
    CHECK(eocd != std::string::npos);
    if (eocd != std::string::npos && eocd + 12 <= bytes.size()) {
        // total-entries field is a 2-byte LE at EOCD+10.
        const uint16_t total = (uint8_t)bytes[eocd + 10] | ((uint8_t)bytes[eocd + 11] << 8);
        CHECK(total == 2);
    }

    // Central directory headers "PK\x01\x02" — two of them.
    const std::string cdSig = std::string("PK\x01\x02", 4);
    int cd_count = 0;
    for (size_t p = bytes.find(cdSig); p != std::string::npos; p = bytes.find(cdSig, p + 1))
        ++cd_count;
    CHECK(cd_count == 2);

    // Entry names + STORE'd payload present verbatim (no compression).
    CHECK(contains(bytes, "[Content_Types].xml"));
    CHECK(contains(bytes, "word/document.xml"));
    CHECK(contains(bytes, "HELLO_ZIP_PAYLOAD"));

    // Publishing a second archive to the same path must atomically replace the
    // first archive on Windows as well as POSIX.
    {
        ZipWriter replacement;
        replacement.addFile("replacement.txt", "REPLACEMENT_PAYLOAD");
        CHECK(replacement.writeToFile(path));
    }
    std::ifstream replaced_in(path, std::ios::binary);
    CHECK(replaced_in.good());
    const std::string replaced(
        (std::istreambuf_iterator<char>(replaced_in)),
        std::istreambuf_iterator<char>());
    replaced_in.close();
    CHECK(contains(replaced, "replacement.txt"));
    CHECK(contains(replaced, "REPLACEMENT_PAYLOAD"));
    CHECK(!contains(replaced, "HELLO_ZIP_PAYLOAD"));

    std::filesystem::remove(tmp);

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
