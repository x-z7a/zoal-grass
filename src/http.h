// A blocking HTTPS POST for the worker thread, without a build-time dependency: WinHTTP on Windows, the system libcurl
// (loaded at run time) on macOS and Linux. Used only to ask an Overpass server for OpenStreetMap roads.
#pragma once

#include <string>

namespace zg {

struct HttpResult {
    bool ok = false;        // transport worked and status is 200
    long status = 0;
    std::string body;
    std::string error;      // when !ok
};

// POST application/x-www-form-urlencoded `form` to `url`; gives up after timeout_s.
HttpResult http_post_form(const std::string& url, const std::string& form, int timeout_s);

// Abort every request in flight (and refuse new ones) so threads waiting on a download can be joined at shutdown.
void http_cancel_all();

// application/x-www-form-urlencoded encoding of one value.
std::string url_encode(const std::string& s);

}  // namespace zg
