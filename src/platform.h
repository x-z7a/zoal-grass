// The few file-system operations that differ between macOS, Windows and Linux. Paths are UTF-8 strings with '/'
// separators (Windows accepts them); fs_path() turns one into a std::filesystem::path for streams.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace zg {

std::filesystem::path fs_path(const std::string& utf8);
std::string to_utf8(const std::filesystem::path& p);   // with '/' separators

bool is_file(const std::string& path);
bool make_dir(const std::string& path);
bool remove_file(const std::string& path);
bool rename_file(const std::string& from, const std::string& to);
std::vector<std::string> list_dir(const std::string& path);           // entry names, without "." and ".."
bool file_key(const std::string& path, long long& size, long long& mtime);   // size and modification time

// True when path lies on a FUSE / FSKit / WinFsp / Dokan / network mount, such as a streaming-ortho virtual file system
// (XEarthLayer, AutoOrtho). Decided from the mount table or volume information, without reading the mount's contents.
bool on_virtual_fs(const std::string& path);

// Read-only memory map of a whole file (empty when the file is missing or empty).
class MappedFile {
public:
    explicit MappedFile(const std::string& path);
    ~MappedFile();
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    const uint8_t* data() const { return data_; }
    size_t size() const { return size_; }

private:
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
#ifdef _WIN32
    void* file_ = nullptr;
    void* mapping_ = nullptr;
#else
    int fd_ = -1;
#endif
};

}  // namespace zg
