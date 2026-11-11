#include "http.h"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <set>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#else
#include <dlfcn.h>
#endif

namespace zg {

namespace {
const char* kUserAgent = "zoal-grass X-Plane plugin (grass placement; https://github.com/x-z7a/zoal-grass)";
std::atomic<bool> g_cancel{false};
}

std::string url_encode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            out += char(c);
        } else if (c == ' ') {
            out += '+';
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

#ifdef _WIN32

namespace {
std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), &w[0], n);
    return w;
}
struct Handle {
    HINTERNET h = nullptr;
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
};
// requests in flight: http_cancel_all closes them, which makes the blocked WinHTTP call return
std::mutex g_mu;
std::set<HINTERNET> g_live;
struct Live {
    HINTERNET& h;
    explicit Live(HINTERNET& handle) : h(handle) {
        std::lock_guard<std::mutex> lock(g_mu);
        g_live.insert(h);
    }
    ~Live() {
        std::lock_guard<std::mutex> lock(g_mu);
        if (g_live.erase(h) == 0) h = nullptr;   // already closed by http_cancel_all
    }
};
}  // namespace

void http_cancel_all() {
    g_cancel.store(true);
    std::lock_guard<std::mutex> lock(g_mu);
    for (HINTERNET h : g_live) WinHttpCloseHandle(h);
    g_live.clear();
}

HttpResult http_post_form(const std::string& url, const std::string& form, int timeout_s) {
    HttpResult r;
    std::wstring wurl = widen(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof uc;
    wchar_t host[256] = {0}, path[2048] = {0};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        r.error = "bad URL " + url;
        return r;
    }
    if (g_cancel.load()) {
        r.error = "cancelled";
        return r;
    }
    Handle session, connect, request;
    std::wstring ua = widen(kUserAgent);
    session.h = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.h) {
        r.error = "WinHttpOpen failed";
        return r;
    }
    int ms = timeout_s * 1000;
    WinHttpSetTimeouts(session.h, ms, ms, ms, ms);
    connect.h = WinHttpConnect(session.h, host, uc.nPort, 0);
    if (!connect.h) {
        r.error = "could not connect";
        return r;
    }
    DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    request.h = WinHttpOpenRequest(connect.h, L"POST", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                   flags);
    if (!request.h) {
        r.error = "could not open the request";
        return r;
    }
    Live live(request.h);   // http_cancel_all closes the request handle, which ends a blocked call on it
    const wchar_t* header = L"Content-Type: application/x-www-form-urlencoded\r\n";
    if (!WinHttpSendRequest(request.h, header, DWORD(-1L), const_cast<char*>(form.data()), DWORD(form.size()),
                            DWORD(form.size()), 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "request failed (error %lu)", GetLastError());
        r.error = g_cancel.load() ? "cancelled" : buf;
        return r;
    }
    DWORD status = 0, size = sizeof status;
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    r.status = long(status);
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request.h, &avail) || avail == 0) break;
        std::string chunk(avail, '\0');
        DWORD got = 0;
        if (!WinHttpReadData(request.h, &chunk[0], avail, &got) || got == 0) break;
        r.body.append(chunk.data(), got);
        if (r.body.size() > (64u << 20)) {
            r.error = "response too large";
            return r;
        }
    }
    r.ok = r.status == 200;
    if (!r.ok) r.error = "HTTP status " + std::to_string(r.status);
    return r;
}

#else  // libcurl, loaded at run time (no headers or link-time dependency; the ABI constants below are stable)

namespace {
using CurlInit = void* (*)();
using CurlSetopt = int (*)(void*, int, ...);
using CurlPerform = int (*)(void*);
using CurlCleanup = void (*)(void*);
using CurlGetinfo = int (*)(void*, int, ...);
using CurlStrerror = const char* (*)(int);

constexpr int kOptWriteData = 10001, kOptUrl = 10002, kOptPostFields = 10015, kOptUserAgent = 10018,
              kOptAcceptEncoding = 10102, kOptPostFieldSize = 60, kOptWriteFunction = 20011, kOptTimeout = 13,
              kOptConnectTimeout = 78, kOptFollowLocation = 52, kOptNoSignal = 99, kOptNoProgress = 43,
              kOptXferInfoFunction = 20219;
constexpr int kInfoResponseCode = 0x200000 + 2;

struct Curl {
    void* lib = nullptr;
    CurlInit init = nullptr;
    CurlSetopt setopt = nullptr;
    CurlPerform perform = nullptr;
    CurlCleanup cleanup = nullptr;
    CurlGetinfo getinfo = nullptr;
    CurlStrerror strerror_ = nullptr;
    Curl() {
#ifdef __APPLE__
        const char* names[] = {"/usr/lib/libcurl.4.dylib", "libcurl.4.dylib", "libcurl.dylib"};
#else
        const char* names[] = {"libcurl.so.4", "libcurl-gnutls.so.4", "libcurl.so"};
#endif
        for (const char* n : names)
            if ((lib = dlopen(n, RTLD_NOW | RTLD_LOCAL))) break;
        if (!lib) return;
        init = reinterpret_cast<CurlInit>(dlsym(lib, "curl_easy_init"));
        setopt = reinterpret_cast<CurlSetopt>(dlsym(lib, "curl_easy_setopt"));
        perform = reinterpret_cast<CurlPerform>(dlsym(lib, "curl_easy_perform"));
        cleanup = reinterpret_cast<CurlCleanup>(dlsym(lib, "curl_easy_cleanup"));
        getinfo = reinterpret_cast<CurlGetinfo>(dlsym(lib, "curl_easy_getinfo"));
        strerror_ = reinterpret_cast<CurlStrerror>(dlsym(lib, "curl_easy_strerror"));
    }
    bool ok() const { return init && setopt && perform && cleanup && getinfo; }
};

int on_progress(void*, long long, long long, long long, long long) { return g_cancel.load() ? 1 : 0; }

size_t on_data(char* ptr, size_t size, size_t n, void* user) {
    auto* body = static_cast<std::string*>(user);
    if (g_cancel.load() || body->size() > (64u << 20)) return 0;   // abort: shutting down / response too large
    body->append(ptr, size * n);
    return size * n;
}
}  // namespace

void http_cancel_all() { g_cancel.store(true); }

HttpResult http_post_form(const std::string& url, const std::string& form, int timeout_s) {
    static Curl curl;   // loaded once, kept for the life of the plugin
    HttpResult r;
    if (g_cancel.load()) {
        r.error = "cancelled";
        return r;
    }
    if (!curl.ok()) {
        r.error = "libcurl not available";
        return r;
    }
    void* h = curl.init();
    if (!h) {
        r.error = "curl_easy_init failed";
        return r;
    }
    using WriteFn = size_t (*)(char*, size_t, size_t, void*);
    WriteFn write_fn = &on_data;
    curl.setopt(h, kOptUrl, url.c_str());
    curl.setopt(h, kOptPostFields, form.c_str());
    curl.setopt(h, kOptPostFieldSize, long(form.size()));
    curl.setopt(h, kOptUserAgent, kUserAgent);
    curl.setopt(h, kOptAcceptEncoding, "");
    curl.setopt(h, kOptWriteFunction, write_fn);
    curl.setopt(h, kOptWriteData, static_cast<void*>(&r.body));
    curl.setopt(h, kOptTimeout, long(timeout_s));
    curl.setopt(h, kOptConnectTimeout, long(timeout_s < 15 ? timeout_s : 15));
    curl.setopt(h, kOptFollowLocation, 1L);
    curl.setopt(h, kOptNoSignal, 1L);
    using ProgressFn = int (*)(void*, long long, long long, long long, long long);
    ProgressFn progress_fn = &on_progress;
    curl.setopt(h, kOptXferInfoFunction, progress_fn);
    curl.setopt(h, kOptNoProgress, 0L);
    int rc = curl.perform(h);
    long status = 0;
    curl.getinfo(h, kInfoResponseCode, &status);
    curl.cleanup(h);
    r.status = status;
    if (rc != 0) {
        r.error = std::string("curl: ") + (curl.strerror_ ? std::string(curl.strerror_(rc)) : std::to_string(rc));
        return r;
    }
    r.ok = status == 200;
    if (!r.ok) r.error = "HTTP status " + std::to_string(status);
    return r;
}

#endif

}  // namespace zg
