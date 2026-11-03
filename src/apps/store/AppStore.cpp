#include "AppStore.h"

#include <ArduinoJson.h>
#include <HalStorage.h>

#include <algorithm>

#include "AppNet.h"

namespace apps {

namespace {

constexpr const char* BASE_URL = "https://raw.githubusercontent.com/ArneeMe/waddleos/waddle/apps/";
constexpr size_t MAX_INDEX = 64 * 1024;

// Lowercase letters, digits and '-': a folder name and part of a URL.
bool validId(const std::string& id) {
  if (id.empty() || id.size() > 32) return false;
  return std::all_of(id.begin(), id.end(), [](const char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
}

bool validFile(const std::string& f) {
  if (f.empty() || f.size() > 64 || f[0] == '.') return false;
  return std::all_of(f.begin(), f.end(), [](const char c) { return isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_'; });
}

}  // namespace

std::vector<Manifest> installed() {
  std::vector<Manifest> out;
  for (const auto& name : Storage.listFiles(DIR, 100)) {
    Manifest m;
    if (readManifest(name.c_str(), m)) out.push_back(m);
  }
  std::sort(out.begin(), out.end(), [](const Manifest& a, const Manifest& b) { return a.name < b.name; });
  return out;
}

bool fetchIndex(std::vector<StoreEntry>& out, std::string& err) {
  out.clear();
  std::string body;
  if (!net::get(std::string(BASE_URL) + "index.json", body, MAX_INDEX, err)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    err = "listen over apper er ødelagt";
    return false;
  }
  for (JsonObject a : doc["apps"].as<JsonArray>()) {
    StoreEntry e;
    e.id = a["id"] | "";
    e.name = a["name"] | "";
    e.description = a["description"] | "";
    e.version = a["version"] | 0;
    bool here = a["devices"].isNull();
    for (JsonVariant d : a["devices"].as<JsonArray>()) here = here || d.as<std::string>() == deviceName();
    bool filesOk = true;
    for (JsonVariant f : a["files"].as<JsonArray>()) {
      e.files.push_back(f.as<std::string>());
      filesOk = filesOk && validFile(e.files.back());
    }
    const bool hasMain = std::find(e.files.begin(), e.files.end(), "main.lua") != e.files.end() &&
                         std::find(e.files.begin(), e.files.end(), "app.json") != e.files.end();
    if (here && validId(e.id) && !e.name.empty() && filesOk && hasMain) out.push_back(e);
  }
  return true;
}

bool install(const StoreEntry& entry, std::string& err) {
  if (!validId(entry.id)) return false;
  const std::string dir = std::string(DIR) + "/" + entry.id;
  Storage.mkdir(dir.c_str());
  // Everything as .new first; app.json last, so a half install never runs.
  for (const auto& f : entry.files) {
    if (!net::download(std::string(BASE_URL) + entry.id + "/" + f, dir + "/" + f + ".new", err)) return false;
  }
  for (const auto& f : entry.files) {
    if (f != "app.json") Storage.replaceFile((dir + "/" + f + ".new").c_str(), (dir + "/" + f).c_str());
  }
  return Storage.replaceFile((dir + "/app.json.new").c_str(), (dir + "/app.json").c_str());
}

void uninstall(const std::string& id) {
  if (validId(id)) Storage.removeDir((std::string(DIR) + "/" + id).c_str());
}

}  // namespace apps
