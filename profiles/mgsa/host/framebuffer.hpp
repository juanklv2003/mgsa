// EDRAM framebuffer -> RGBA8888 blit + minimal PNG writer (no external libs).
#ifndef MGSA_FRAMEBUFFER_HPP
#define MGSA_FRAMEBUFFER_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace psprecomp { class GuestMemory; }

namespace mgsa {

// Convert a 5551 EDRAM surface at topaddr (stride = buffer_width) to RGBA8888.
// width/height are the visible display size (480x272); the buffer may be wider.
std::vector<std::uint8_t> blit_edram_rgba(const psprecomp::GuestMemory &memory,
                                          std::uint32_t topaddr,
                                          std::uint32_t buffer_width,
                                          std::uint32_t width,
                                          std::uint32_t height);

// Write an RGBA8888 buffer as a PNG (uses stored DEFLATE blocks, so no zlib).
bool write_png(const std::string &path,
               const std::uint8_t *rgba, std::uint32_t width, std::uint32_t height);

}  // namespace mgsa

#endif  // MGSA_FRAMEBUFFER_HPP
