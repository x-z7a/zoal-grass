#include "platform.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/mount.h>
#include <sys/param.h>
#endif
#endif

namespace fs = std::filesystem;

namespace zg {

fs::path fs_path(const std::string& utf8) {
#if defined(__cpp_char8_t)
    return fs::path(std::u8string(utf8.begin(), utf8.end()));
#else
    return fs::u8path(utf8);
#endif
}

std::string to_utf8(const fs::path& p) {
#if defined(__cpp_char8_t)
    auto u = p.generic_u8string();
    return std::string(u.begin(), u.end());
#else
    return p.generic_u8string();
#endif
}

bool is_file(const std::string& path) {
    std::error_code ec;
    return fs::is_regular_file(fs_path(path), ec);
}

bool make_dir(const std::string& path) {
    std::error_code ec;
    fs::create_directories(fs_path(path), ec);
    return fs::is_directory(fs_path(path), ec);
}

bool remove_file(const std::string& path) {
    std::error_code ec;
    return fs::remove(fs_path(path), ec);
}

bool rename_file(const std::string& from, const std::string& to) {
    std::error_code ec;
    fs::rename(fs_path(from), fs_path(to), ec);
    return !ec;
}

std::vector<std::string> list_dir(const std::string& path) {
    std::vector<std::string> out;
    std::error_code ec;
    for (fs::directory_iterator it(fs_path(path), ec), end; !ec && it != end; it.increment(ec))
        out.push_back(to_utf8(it->path().filename()));
    return out;
}

bool file_key(const std::string& path, long long& size, long long& mtime) {
    std::error_code ec;
    fs::path p = fs_path(path);
    auto sz = fs::file_size(p, ec);
    if (ec) return false;
    auto t = fs::last_write_time(p, ec);
    if (ec) return false;
    size = (long long)sz;
    mtime = (long long)t.time_since_epoch().count();
    return true;
}

namespace {

bool under(const std::string& path, std::string mount) {
    while (mount.size() > 1 && mount.back() == '/') mount.pop_back();
    if (mount.empty()) return false;
    return path.size() >= mount.size() && path.compare(0, mount.size(), mount) == 0 &&
           (path.size() == mount.size() || path[mount.size()] == '/' || mount == "/");
}

std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool virtual_type(const std::string& type) {
    std::string t = lower(type);
    return t.find("fuse") != std::string::npos || t.find("fskit") != std::string::npos ||
           t.find("winfsp") != std::string::npos || t.find("dokan") != std::string::npos || t == "nfs" ||
           t == "nfs4" || t == "smbfs" || t == "cifs" || t == "webdav";
}

// The path with symlinks in its parent resolved. Resolving the path itself could call into a FUSE mount.
std::string resolved(const std::string& path) {
    fs::path p = fs_path(path);
    std::error_code ec;
    fs::path parent = fs::canonical(p.parent_path(), ec);
    if (ec) return path;
    return to_utf8(parent / p.filename());
}

}  // namespace

bool on_virtual_fs(const std::string& path) {
    std::string target = resolved(path);
#if defined(_WIN32)
    std::wstring w = fs_path(target).wstring();
    wchar_t volume[MAX_PATH + 1] = {0};
    if (!GetVolumePathNameW(w.c_str(), volume, MAX_PATH)) return false;
    if (GetDriveTypeW(volume) == DRIVE_REMOTE) return true;
    wchar_t fsname[MAX_PATH + 1] = {0};
    if (GetVolumeInformationW(volume, nullptr, 0, nullptr, nullptr, nullptr, fsname, MAX_PATH))
        return virtual_type(to_utf8(fs::path(fsname)));
    return false;
#elif defined(__APPLE__)
    struct statfs* mounts = nullptr;
    int n = getmntinfo(&mounts, MNT_NOWAIT);
    for (int i = 0; i < n; ++i) {
        if (!virtual_type(mounts[i].f_fstypename)) continue;
        if (under(path, mounts[i].f_mntonname) || under(target, mounts[i].f_mntonname)) return true;
    }
    return false;
#else
    std::ifstream in("/proc/self/mounts");
    std::string dev, mnt, type;
    std::string rest;
    while (in >> dev >> mnt >> type) {
        std::getline(in, rest);
        if (!virtual_type(type)) continue;
        // /proc/mounts escapes spaces as \040
        std::string m;
        for (size_t k = 0; k < mnt.size(); ++k) {
            if (mnt[k] == '\\' && k + 3 < mnt.size()) {
                m += char(std::stoi(mnt.substr(k + 1, 3), nullptr, 8));
                k += 3;
            } else {
                m += mnt[k];
            }
        }
        if (under(path, m) || under(target, m)) return true;
    }
    return false;
#endif
}

MappedFile::MappedFile(const std::string& path) {
#ifdef _WIN32
    std::wstring w = fs_path(path).wstring();
    HANDLE f = CreateFileW(w.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    file_ = f;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(f, &sz) || sz.QuadPart == 0) return;
    HANDLE m = CreateFileMappingW(f, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!m) return;
    mapping_ = m;
    void* p = MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0);
    if (!p) return;
    data_ = static_cast<const uint8_t*>(p);
    size_ = size_t(sz.QuadPart);
#else
    fd_ = ::open(path.c_str(), O_RDONLY);
    if (fd_ < 0) return;
    struct stat st;
    if (fstat(fd_, &st) != 0 || st.st_size == 0) return;
    void* p = mmap(nullptr, size_t(st.st_size), PROT_READ, MAP_PRIVATE, fd_, 0);
    if (p == MAP_FAILED) return;
    data_ = static_cast<const uint8_t*>(p);
    size_ = size_t(st.st_size);
#endif
}

MappedFile::~MappedFile() {
#ifdef _WIN32
    if (data_) UnmapViewOfFile(data_);
    if (mapping_) CloseHandle(static_cast<HANDLE>(mapping_));
    if (file_) CloseHandle(static_cast<HANDLE>(file_));
#else
    if (data_) munmap(const_cast<uint8_t*>(data_), size_);
    if (fd_ >= 0) ::close(fd_);
#endif
}

}  // namespace zg
