#pragma once

// The web for script apps (net.get, net.download, books.download): joins the
// saved Wi-Fi when needed and leaves it on until off() (when the app closes).

#include <cstddef>
#include <string>

namespace apps::net {

// The body, at most maxBytes. False with err set on failure.
bool get(const std::string& url, std::string& body, size_t maxBytes, std::string& err);
// Saved to path, through a temporary file. False with err set on failure.
bool download(const std::string& url, const std::string& path, std::string& err);
// Turns Wi-Fi off again if get() or download() turned it on.
void off();

}  // namespace apps::net
