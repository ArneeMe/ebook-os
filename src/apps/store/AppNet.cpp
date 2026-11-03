#include "AppNet.h"

#include <HalStorage.h>
#include <WiFi.h>

#include "core/network/HttpDownloader.h"
#include "core/waddle/WaddleWifi.h"

namespace apps::net {

namespace {

constexpr unsigned long WIFI_TIMEOUT_MS = 15000;
bool turnedOn = false;

bool online(std::string& err) {
  if (WiFi.status() == WL_CONNECTED) return true;
  if (!waddle::wifi::joinSaved()) {
    err = "ingen lagret Wi-Fi";
    return false;
  }
  turnedOn = true;
  const unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (waddle::wifi::joinFailed(started) || millis() - started > WIFI_TIMEOUT_MS) {
      err = "fikk ikke koblet til Wi-Fi";
      return false;
    }
    delay(100);
  }
  return true;
}

bool enoughHeap(std::string& err) {
  if (ESP.getFreeHeap() >= HttpDownloader::MIN_TLS_FREE_HEAP && ESP.getMaxAllocHeap() >= HttpDownloader::MIN_TLS_MAX_ALLOC) {
    return true;
  }
  err = "for lite minne til nedlasting";
  return false;
}

}  // namespace

bool get(const std::string& url, std::string& body, const size_t maxBytes, std::string& err) {
  body.clear();
  if (!online(err) || !enoughHeap(err)) return false;
  bool tooBig = false;
  const bool ok = HttpDownloader::fetchUrl(url, [&](const uint8_t* data, const size_t len) {
    if (body.size() + len > maxBytes) {
      tooBig = true;
      return false;
    }
    body.append(reinterpret_cast<const char*>(data), len);
    return true;
  });
  if (ok) return true;
  err = tooBig ? "svaret er for stort" : "fikk ikke hentet " + url;
  return false;
}

bool download(const std::string& url, const std::string& path, std::string& err) {
  if (!online(err) || !enoughHeap(err)) return false;
  if (HttpDownloader::downloadToFile(url, path) == HttpDownloader::OK) return true;
  err = "fikk ikke lastet ned " + url;
  return false;
}

void off() {
  if (turnedOn) waddle::wifi::off();
  turnedOn = false;
}

}  // namespace apps::net
