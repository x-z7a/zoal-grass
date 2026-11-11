#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace zg {
// Decode a single-file 7z DSF in memory, never extracting paths to disk. Throws on invalid/oversized archives.
std::vector<uint8_t> unpack_dsf(const uint8_t* data, size_t size);
}
