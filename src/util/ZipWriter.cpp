#include "util/ZipWriter.h"
#include "util/AtomicFile.h"

#include <array>
#include <filesystem>
#include <fstream>

namespace dolphin::util {

namespace {

// CRC-32 (IEEE 802.3, polynomial 0xEDB88320) — table built once on first use.
uint32_t crc32(const std::string& data)
{
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();

    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char b : data)
        crc = table[(crc ^ b) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

void putU16(std::string& s, uint16_t v)
{
    s.push_back(static_cast<char>(v & 0xFF));
    s.push_back(static_cast<char>((v >> 8) & 0xFF));
}

void putU32(std::string& s, uint32_t v)
{
    s.push_back(static_cast<char>(v & 0xFF));
    s.push_back(static_cast<char>((v >> 8) & 0xFF));
    s.push_back(static_cast<char>((v >> 16) & 0xFF));
    s.push_back(static_cast<char>((v >> 24) & 0xFF));
}

constexpr uint16_t kDosDate = 0x0021;   // 1980-01-01 (0 is an invalid DOS date)

} // namespace

void ZipWriter::addFile(const std::string& name, const std::string& data)
{
    Entry e;
    e.name   = name;
    e.crc    = crc32(data);
    e.size   = static_cast<uint32_t>(data.size());
    e.offset = static_cast<uint32_t>(m_buf.size());

    // Local file header.
    putU32(m_buf, 0x04034b50);                       // signature
    putU16(m_buf, 20);                               // version needed
    putU16(m_buf, 0);                                // flags
    putU16(m_buf, 0);                                // method 0 = stored
    putU16(m_buf, 0);                                // mod time
    putU16(m_buf, kDosDate);                         // mod date
    putU32(m_buf, e.crc);
    putU32(m_buf, e.size);                           // compressed size == size
    putU32(m_buf, e.size);                           // uncompressed size
    putU16(m_buf, static_cast<uint16_t>(e.name.size()));
    putU16(m_buf, 0);                                // extra length
    m_buf += e.name;
    m_buf += data;

    m_entries.push_back(std::move(e));
}

bool ZipWriter::writeToFile(const std::string& path)
{
    if (path.empty()) return false;

    const uint32_t cd_offset = static_cast<uint32_t>(m_buf.size());

    for (const auto& e : m_entries) {
        putU32(m_buf, 0x02014b50);                   // central dir signature
        putU16(m_buf, 20);                           // version made by
        putU16(m_buf, 20);                           // version needed
        putU16(m_buf, 0);                            // flags
        putU16(m_buf, 0);                            // method
        putU16(m_buf, 0);                            // mod time
        putU16(m_buf, kDosDate);                     // mod date
        putU32(m_buf, e.crc);
        putU32(m_buf, e.size);
        putU32(m_buf, e.size);
        putU16(m_buf, static_cast<uint16_t>(e.name.size()));
        putU16(m_buf, 0);                            // extra
        putU16(m_buf, 0);                            // comment
        putU16(m_buf, 0);                            // disk number start
        putU16(m_buf, 0);                            // internal attrs
        putU32(m_buf, 0);                            // external attrs
        putU32(m_buf, e.offset);                     // local header offset
        m_buf += e.name;
    }

    const uint32_t cd_size = static_cast<uint32_t>(m_buf.size()) - cd_offset;

    // End of central directory.
    putU32(m_buf, 0x06054b50);
    putU16(m_buf, 0);                                // disk number
    putU16(m_buf, 0);                                // disk with CD
    putU16(m_buf, static_cast<uint16_t>(m_entries.size()));
    putU16(m_buf, static_cast<uint16_t>(m_entries.size()));
    putU32(m_buf, cd_size);
    putU32(m_buf, cd_offset);
    putU16(m_buf, 0);                                // comment length

    const std::filesystem::path destination(path);
    const std::filesystem::path temp = siblingTempPath(destination, "zip");

    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(m_buf.data(), static_cast<std::streamsize>(m_buf.size()));
    out.flush();
    if (!out) {
        out.close();
        std::error_code ec;
        std::filesystem::remove(temp, ec);
        return false;
    }
    out.close();
    std::error_code ec;
    if (!out || !replaceFileAtomically(temp, destination, ec)) {
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}

} // namespace dolphin::util
