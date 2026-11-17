#include "framebuffer.hpp"
#include "psprecomp/guest_memory.hpp"

#include <cstdint>
#include <fstream>

namespace mgsa {

std::vector<std::uint8_t> blit_edram_rgba(const psprecomp::GuestMemory &memory,
                                          std::uint32_t topaddr,
                                          std::uint32_t buffer_width,
                                          std::uint32_t width,
                                          std::uint32_t height) {
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4u, 0u);
    if (buffer_width == 0u) return rgba;
    for (std::uint32_t y = 0u; y < height; ++y) {
        for (std::uint32_t x = 0u; x < width; ++x) {
            const std::uint32_t addr = topaddr + (y * buffer_width + x) * 2u;
            std::uint16_t px = 0u;
            if (memory.contains(addr, 2u)) px = memory.aot_load16(addr);
            // PSP 5551: bit15 = alpha (1 = opaque), bits 10-14 R, 5-9 G, 0-4 B.
            const std::uint8_t r5 = (px >> 10) & 0x1Fu;
            const std::uint8_t g5 = (px >> 5) & 0x1Fu;
            const std::uint8_t b5 = px & 0x1Fu;
            const std::uint8_t a = (px & 0x8000u) ? 0xFFu : 0x00u;
            // Expand 5-bit to 8-bit (x*255/31).
            const std::uint8_t r = static_cast<std::uint8_t>((r5 * 255u + 15u) / 31u);
            const std::uint8_t g = static_cast<std::uint8_t>((g5 * 255u + 15u) / 31u);
            const std::uint8_t b = static_cast<std::uint8_t>((b5 * 255u + 15u) / 31u);
            const std::size_t o = (static_cast<std::size_t>(y) * width + x) * 4u;
            rgba[o + 0] = r;
            rgba[o + 1] = g;
            rgba[o + 2] = b;
            rgba[o + 3] = a;
        }
    }
    return rgba;
}

namespace {

std::uint32_t crc32(const std::uint8_t *data, std::size_t len) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) {
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

void put32(std::vector<std::uint8_t> &v, std::uint32_t x) {
    v.push_back(static_cast<std::uint8_t>(x >> 24));
    v.push_back(static_cast<std::uint8_t>(x >> 16));
    v.push_back(static_cast<std::uint8_t>(x >> 8));
    v.push_back(static_cast<std::uint8_t>(x));
}

void chunk(std::vector<std::uint8_t> &out, const char *type,
           const std::uint8_t *data, std::uint32_t len) {
    put32(out, len);
    const std::size_t type_start = out.size();
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(type[i]));
    if (data && len) out.insert(out.end(), data, data + len);
    const std::uint32_t crc = crc32(out.data() + type_start,
                                   static_cast<std::size_t>(out.size() - type_start));
    put32(out, crc);
}

}  // namespace

bool write_png(const std::string &path,
               const std::uint8_t *rgba, std::uint32_t width, std::uint32_t height) {
    if (!rgba || width == 0u || height == 0u) return false;
    // Build raw scanlines: filter byte 0 + RGBA rows.
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(height) * (1 + static_cast<std::size_t>(width) * 4));
    for (std::uint32_t y = 0u; y < height; ++y) {
        raw.push_back(0u);
        const std::size_t row = static_cast<std::size_t>(y) * width * 4u;
        raw.insert(raw.end(), rgba + row, rgba + row + static_cast<std::size_t>(width) * 4u);
    }
    // Stored DEFLATE: zlib header, then uncompressed blocks (max 65535 each), then adler32.
    std::vector<std::uint8_t> z;
    z.push_back(0x78); z.push_back(0x01);  // zlib, no compression
    std::size_t off = 0;
    do {
        std::uint32_t n = static_cast<std::uint32_t>(
            raw.size() - off > 65535u ? 65535u : raw.size() - off);
        const bool last = (off + n == raw.size());
        z.push_back(last ? 0x01u : 0x00u);  // BFINAL, BTYPE=00 stored
        z.push_back(static_cast<std::uint8_t>(n & 0xFFu));
        z.push_back(static_cast<std::uint8_t>((n >> 8) & 0xFFu));
        z.push_back(static_cast<std::uint8_t>((~n) & 0xFFu));
        z.push_back(static_cast<std::uint8_t>((~n >> 8) & 0xFFu));
        z.insert(z.end(), raw.begin() + off, raw.begin() + off + n);
        off += n;
    } while (off < raw.size());
    std::uint32_t a = 1u, b = 0u;
    for (std::uint8_t byte : raw) { a = (a + byte) % 65521u; b = (b + a) % 65521u; }
    put32(z, (b << 16) | a);

    std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::uint8_t ihdr[13];
    for (int i = 0; i < 4; ++i) ihdr[i] = static_cast<std::uint8_t>(width >> (8 * (3 - i)));
    for (int i = 0; i < 4; ++i) ihdr[4 + i] = static_cast<std::uint8_t>(height >> (8 * (3 - i)));
    ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;  // 8-bit RGBA
    chunk(png, "IHDR", ihdr, 13);
    chunk(png, "IDAT", z.data(), static_cast<std::uint32_t>(z.size()));
    chunk(png, "IEND", nullptr, 0);

    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char *>(png.data()), static_cast<std::streamsize>(png.size()));
    return out.good();
}

}  // namespace mgsa
