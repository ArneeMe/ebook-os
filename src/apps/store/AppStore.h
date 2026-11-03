#pragma once

// The script apps on the SD card, and the list of apps to fetch from the
// waddleos repo (apps/index.json). Nothing is fetched until Arne opens
// "Hent flere apper".

#include <string>
#include <vector>

#include "LuaApp.h"

namespace apps {

struct StoreEntry {
  std::string id;
  std::string name;
  std::string description;
  int version = 0;
  std::vector<std::string> files;  // app.json, main.lua, ...
};

// The apps in /Apper for this device, by name.
std::vector<Manifest> installed();

// apps/index.json, only the apps for this device. False with err on failure.
bool fetchIndex(std::vector<StoreEntry>& out, std::string& err);
// Downloads every file; the old version stays until all are in.
bool install(const StoreEntry& entry, std::string& err);
void uninstall(const std::string& id);

// Built-in apps listed first in Apper, from the device's product folder.
struct Builtin {
  const char* name;
  const char* note;
};
const std::vector<Builtin>& builtins();
void openBuiltin(size_t index);

}  // namespace apps
