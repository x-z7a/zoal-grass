#include "dsf_archive.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <stdexcept>

extern "C" {
#include "7z.h"
#include "7zCrc.h"
}

namespace zg {
namespace {
constexpr size_t kMaxBytes = 512u * 1024 * 1024;
struct MemoryStream {
    ILookInStream api;
    const uint8_t* bytes;
    size_t size, pos = 0;
    static MemoryStream& self(ILookInStreamPtr p) {
        return *reinterpret_cast<MemoryStream*>(const_cast<ILookInStream*>(p));
    }
    static SRes look(ILookInStreamPtr p, const void** buf, size_t* n) {
        auto& s = self(p);
        *n = std::min(*n, s.size - s.pos);
        *buf = s.bytes + s.pos;
        return SZ_OK;
    }
    static SRes skip(ILookInStreamPtr p, size_t n) {
        auto& s = self(p);
        if (n > s.size - s.pos) return SZ_ERROR_INPUT_EOF;
        s.pos += n;
        return SZ_OK;
    }
    static SRes read(ILookInStreamPtr p, void* buf, size_t* n) {
        const void* src;
        look(p, &src, n);
        std::memcpy(buf, src, *n);
        return skip(p, *n);
    }
    static SRes seek(ILookInStreamPtr p, Int64* offset, ESzSeek origin) {
        auto& s = self(p);
        Int64 base = origin == SZ_SEEK_SET ? 0 : origin == SZ_SEEK_CUR ? Int64(s.pos) : Int64(s.size);
        if (*offset < -base || *offset > Int64(s.size) - base) return SZ_ERROR_INPUT_EOF;
        s.pos = size_t(base + *offset);
        *offset = Int64(s.pos);
        return SZ_OK;
    }
};
// Bound total allocations (including archive metadata, dictionaries and solid blocks).
struct Allocator {
    ISzAlloc api;
    size_t used = 0;
    struct alignas(std::max_align_t) Header { size_t size; };
    static void* alloc(ISzAllocPtr p, size_t n) {
        auto& a = *reinterpret_cast<Allocator*>(const_cast<ISzAlloc*>(p));
        if (n == 0 || n > kMaxBytes - a.used) return nullptr;
        auto* h = static_cast<Header*>(std::malloc(sizeof(Header) + n));
        if (!h) return nullptr;
        h->size = n;
        a.used += n;
        return h + 1;
    }
    static void free(ISzAllocPtr p, void* ptr) {
        if (!ptr) return;
        auto& a = *reinterpret_cast<Allocator*>(const_cast<ISzAlloc*>(p));
        auto* h = static_cast<Header*>(ptr) - 1;
        a.used -= h->size;
        std::free(h);
    }
};
}

std::vector<uint8_t> unpack_dsf(const uint8_t* data, size_t size) {
    static std::once_flag crc;
    std::call_once(crc, CrcGenerateTable);
    MemoryStream stream{{MemoryStream::look, MemoryStream::skip, MemoryStream::read, MemoryStream::seek}, data, size};
    Allocator alloc{{Allocator::alloc, Allocator::free}};
    struct Archive {
        CSzArEx db;
        Byte* buffer = nullptr;
        ISzAllocPtr allocator;
        explicit Archive(ISzAllocPtr a) : allocator(a) { SzArEx_Init(&db); }
        ~Archive() { ISzAlloc_Free(allocator, buffer); SzArEx_Free(&db, allocator); }
    } archive(&alloc.api);
    if (SzArEx_Open(&archive.db, &stream.api, &alloc.api, &alloc.api) != SZ_OK)
        throw std::runtime_error("invalid or oversized 7z DSF");
    if (archive.db.NumFiles != 1 || SzArEx_IsDir(&archive.db, 0) || SzArEx_GetFileSize(&archive.db, 0) > kMaxBytes)
        throw std::runtime_error("7z DSF must contain one file of at most 512 MiB");
    UInt32 block = 0xffffffff;
    size_t buffer_size = 0, offset = 0, length = 0;
    if (SzArEx_Extract(&archive.db, &stream.api, 0, &block, &archive.buffer, &buffer_size, &offset, &length,
                       &alloc.api, &alloc.api) != SZ_OK)
        throw std::runtime_error("could not decode 7z DSF");
    if (length < 8 || std::memcmp(archive.buffer + offset, "XPLNEDSF", 8) != 0)
        throw std::runtime_error("7z archive does not contain a DSF");
    return {archive.buffer + offset, archive.buffer + offset + length};
}
}
