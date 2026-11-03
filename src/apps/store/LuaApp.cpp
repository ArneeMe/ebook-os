#include "LuaApp.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdlib>
#include <cstring>
#include <ctime>

#include "AppNet.h"
#include "core/components/UITheme.h"
#include "core/fontIds.h"

#if __has_include(<esp_heap_caps.h>)
#include <esp_heap_caps.h>
#define APP_HEAP_PSRAM 1
#endif

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

namespace apps {

namespace {

constexpr size_t MEMORY_LIMIT = 2 * 1024 * 1024;   // in PSRAM
constexpr unsigned long CALL_LIMIT_MS = 5000;     // one callback, before it is stopped
constexpr size_t MAX_SCRIPT = 256 * 1024;
constexpr size_t MAX_FILE = 256 * 1024;           // storage.read / net.get
constexpr int REFRESH_FAST = 0, REFRESH_HALF = 1, REFRESH_FULL = 2;

LuaApp* current = nullptr;  // one app runs at a time

void* allocate(void* ud, void* ptr, const size_t oldSize, const size_t newSize) {
  auto* app = static_cast<LuaApp*>(ud);
  const size_t old = ptr ? oldSize : 0;
  if (newSize == 0) {
    app->memoryUsed_ -= old;
#ifdef APP_HEAP_PSRAM
    heap_caps_free(ptr);
#else
    std::free(ptr);
#endif
    return nullptr;
  }
  if (app->memoryUsed_ - old + newSize > MEMORY_LIMIT) return nullptr;
#ifdef APP_HEAP_PSRAM
  void* out = heap_caps_realloc(ptr, newSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  void* out = std::realloc(ptr, newSize);
#endif
  if (out) app->memoryUsed_ = app->memoryUsed_ - old + newSize;
  return out;
}

// A callback that runs too long (an endless loop) is stopped.
void watchdog(lua_State* L, lua_Debug*) {
  if (current && millis() - current->callStartedMs_ > CALL_LIMIT_MS) luaL_error(L, "appen svarer ikke");
}

LuaApp& app() { return *current; }
GfxRenderer& gfx() { return current->renderer; }

int fontOf(const char* size) {
  const std::string s = size ? size : "";
  if (s == "small") return SMALL_FONT_ID;
  if (s == "big") return NOTOSANS_14_FONT_ID;
  return UI_12_FONT_ID;
}

// Options table at index: {size=, bold=, white=}.
struct TextStyle {
  int font = UI_12_FONT_ID;
  EpdFontFamily::Style style = EpdFontFamily::REGULAR;
  bool black = true;
};
TextStyle textStyle(lua_State* L, const int index) {
  TextStyle t;
  if (!lua_istable(L, index)) return t;
  lua_getfield(L, index, "size");
  t.font = fontOf(lua_tostring(L, -1));
  lua_getfield(L, index, "bold");
  if (lua_toboolean(L, -1)) t.style = EpdFontFamily::BOLD;
  lua_getfield(L, index, "white");
  t.black = !lua_toboolean(L, -1);
  lua_pop(L, 3);
  return t;
}

int optInt(lua_State* L, const int index, const char* field, const int fallback) {
  if (!lua_istable(L, index)) return fallback;
  lua_getfield(L, index, field);
  const int v = lua_isnumber(L, -1) ? int(lua_tointeger(L, -1)) : fallback;
  lua_pop(L, 1);
  return v;
}

bool optBool(lua_State* L, const int index, const char* field) {
  if (!lua_istable(L, index)) return false;
  lua_getfield(L, index, field);
  const bool v = lua_toboolean(L, -1);
  lua_pop(L, 1);
  return v;
}

// --- screen ---

int screenWidth(lua_State* L) {
  lua_pushinteger(L, gfx().getScreenWidth());
  return 1;
}
int screenHeight(lua_State* L) {
  lua_pushinteger(L, gfx().getScreenHeight());
  return 1;
}
int screenClear(lua_State*) {
  gfx().clearScreen();
  return 0;
}
// screen.header(title): the title bar; returns the y where content starts.
int screenHeader(lua_State* L) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(gfx(), Rect{0, metrics.topPadding, gfx().getScreenWidth(), metrics.headerHeight}, luaL_checkstring(L, 1));
  lua_pushinteger(L, metrics.topPadding + metrics.headerHeight + 10);
  return 1;
}
// screen.text(x, y, text, {size=, bold=, white=}): returns its width.
int screenText(lua_State* L) {
  const TextStyle t = textStyle(L, 4);
  const char* text = luaL_checkstring(L, 3);
  gfx().drawText(t.font, int(luaL_checkinteger(L, 1)), int(luaL_checkinteger(L, 2)), text, t.black, t.style);
  lua_pushinteger(L, gfx().getTextWidth(t.font, text, t.style));
  return 1;
}
int screenTextWidth(lua_State* L) {
  const TextStyle t = textStyle(L, 2);
  lua_pushinteger(L, gfx().getTextWidth(t.font, luaL_checkstring(L, 1), t.style));
  return 1;
}
int screenLineHeight(lua_State* L) {
  lua_pushinteger(L, gfx().getLineHeight(fontOf(luaL_optstring(L, 1, ""))));
  return 1;
}
// screen.wrap(text, width, {size=, bold=, lines=}): a table of lines.
int screenWrap(lua_State* L) {
  const TextStyle t = textStyle(L, 3);
  const auto lines = gfx().wrappedText(t.font, luaL_checkstring(L, 1), int(luaL_checkinteger(L, 2)),
                                       optInt(L, 3, "lines", 20), t.style);
  lua_createtable(L, int(lines.size()), 0);
  for (size_t i = 0; i < lines.size(); i++) {
    lua_pushstring(L, lines[i].c_str());
    lua_rawseti(L, -2, lua_Integer(i + 1));
  }
  return 1;
}
// screen.line(x1, y1, x2, y2, {width=, white=})
int screenLine(lua_State* L) {
  gfx().drawLine(int(luaL_checkinteger(L, 1)), int(luaL_checkinteger(L, 2)), int(luaL_checkinteger(L, 3)),
                 int(luaL_checkinteger(L, 4)), optInt(L, 5, "width", 1), !optBool(L, 5, "white"));
  return 0;
}
// screen.rect(x, y, w, h, {fill=, white=, radius=, width=})
int screenRect(lua_State* L) {
  const int x = int(luaL_checkinteger(L, 1)), y = int(luaL_checkinteger(L, 2));
  const int w = int(luaL_checkinteger(L, 3)), h = int(luaL_checkinteger(L, 4));
  const int radius = optInt(L, 5, "radius", 0);
  const bool black = !optBool(L, 5, "white");
  if (w <= 0 || h <= 0) return 0;
  if (optBool(L, 5, "fill")) {
    if (radius > 0) {
      gfx().fillRoundedRect(x, y, w, h, radius, black ? Color::Black : Color::White);
    } else {
      gfx().fillRect(x, y, w, h, black);
    }
  } else if (radius > 0) {
    gfx().drawRoundedRect(x, y, w, h, optInt(L, 5, "width", 1), radius, black);
  } else {
    gfx().drawRect(x, y, w, h, optInt(L, 5, "width", 1), black);
  }
  return 0;
}
// screen.dot(x, y, size, {white=}): a filled round dot, for pens.
int screenDot(lua_State* L) {
  const int size = std::max(1, int(luaL_checkinteger(L, 3)));
  const int x = int(luaL_checkinteger(L, 1)) - size / 2, y = int(luaL_checkinteger(L, 2)) - size / 2;
  const bool black = !optBool(L, 4, "white");
  if (size <= 2) {
    gfx().fillRect(x, y, size, size, black);
  } else {
    gfx().fillRoundedRect(x, y, size, size, size / 2, black ? Color::Black : Color::White);
  }
  return 0;
}

// --- app ---

int refreshOf(const char* mode) {
  const std::string m = mode ? mode : "";
  return m == "full" ? REFRESH_FULL : m == "half" ? REFRESH_HALF : REFRESH_FAST;
}
// app.redraw("fast" | "half" | "full"): draw() runs again soon.
int appRedraw(lua_State* L) {
  const int mode = refreshOf(luaL_optstring(L, 1, "fast"));
  app().redrawMode_ = app().redraw_ ? std::max(app().redrawMode_, mode) : mode;
  app().redraw_ = true;
  return 0;
}
// app.every(ms): tick() that often; 0 stops it.
int appEvery(lua_State* L) {
  const lua_Integer ms = luaL_checkinteger(L, 1);
  app().timerMs_ = ms <= 0 ? 0 : std::max<lua_Integer>(ms, 50);
  return 0;
}
int appClose(lua_State*) {
  app().close_ = true;
  return 0;
}
int appMillis(lua_State* L) {
  lua_pushinteger(L, lua_Integer(millis()));
  return 1;
}
int appTime(lua_State* L) {
  const time_t t = time(nullptr);
  lua_pushinteger(L, t > 1600000000 ? lua_Integer(t) : 0);
  return 1;
}
int appDevice(lua_State* L) {
  lua_pushstring(L, deviceName());
  return 1;
}

// --- storage: files in /Apper/<id>/data/ ---

// A plain file name: letters, digits, '.', '-' and '_', not starting with '.'.
// Checked before any std::string exists: luaL_error does not run destructors.
bool plainName(const char* name) {
  const size_t len = strlen(name);
  bool ok = len > 0 && len <= 64 && name[0] != '.';
  for (size_t i = 0; i < len; i++) {
    const char c = name[i];
    ok = ok && (isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_');
  }
  return ok;
}

std::string dataPath(lua_State* L, const int index) {
  const char* name = luaL_checkstring(L, index);
  if (!plainName(name)) luaL_error(L, "ugyldig filnavn: %s", name);
  return std::string(DIR) + "/" + app().manifest.id + "/data/" + name;
}

int storageRead(lua_State* L) {
  const std::string path = dataPath(L, 1);
  if (!Storage.exists(path.c_str())) {
    lua_pushnil(L);
    return 1;
  }
  const String text = Storage.readFile(path.c_str());
  lua_pushlstring(L, text.c_str(), text.length());
  return 1;
}
int storageWrite(lua_State* L) {
  size_t len = 0;
  const char* text = luaL_checklstring(L, 2, &len);
  if (len > MAX_FILE) return luaL_error(L, "for stor fil");
  const std::string path = dataPath(L, 1);
  const std::string dir = std::string(DIR) + "/" + app().manifest.id + "/data";
  Storage.mkdir(dir.c_str());
  lua_pushboolean(L, Storage.writeFile(path.c_str(), String(text)));
  return 1;
}
int storageRemove(lua_State* L) {
  lua_pushboolean(L, Storage.remove(dataPath(L, 1).c_str()));
  return 1;
}
int storageList(lua_State* L) {
  const std::string dir = std::string(DIR) + "/" + app().manifest.id + "/data";
  const auto names = Storage.listFiles(dir.c_str(), 200);
  lua_createtable(L, int(names.size()), 0);
  for (size_t i = 0; i < names.size(); i++) {
    lua_pushstring(L, names[i].c_str());
    lua_rawseti(L, -2, lua_Integer(i + 1));
  }
  return 1;
}

// --- net and books (only when app.json allows it) ---

int netGet(lua_State* L) {
  if (!app().manifest.net) return luaL_error(L, "appen har ikke lov til å bruke nett");
  const char* url = luaL_checkstring(L, 1);
  std::string body, err;
  if (!net::get(url, body, MAX_FILE, err)) {
    lua_pushnil(L);
    lua_pushstring(L, err.c_str());
    return 2;
  }
  lua_pushlstring(L, body.data(), body.size());
  return 1;
}
int netDownload(lua_State* L) {
  if (!app().manifest.net) return luaL_error(L, "appen har ikke lov til å bruke nett");
  const char* url = luaL_checkstring(L, 1);
  const std::string path = dataPath(L, 2);
  const std::string dir = std::string(DIR) + "/" + app().manifest.id + "/data";
  Storage.mkdir(dir.c_str());
  std::string err;
  const bool ok = net::download(url, path, err);
  lua_pushboolean(L, ok);
  if (ok) return 1;
  lua_pushstring(L, err.c_str());
  return 2;
}
// books.download(url, "Tittel.epub"): saves to /Books.
int booksDownload(lua_State* L) {
  if (!app().manifest.net || !app().manifest.books) return luaL_error(L, "appen har ikke lov til å lagre bøker");
  const char* url = luaL_checkstring(L, 1);
  const char* name = luaL_checkstring(L, 2);
  const size_t len = strlen(name);
  if (!plainName(name) || len < 6 || strcmp(name + len - 5, ".epub") != 0) {
    return luaL_error(L, "ugyldig boknavn: %s", name);
  }
  Storage.mkdir("/Books");
  std::string err;
  const bool ok = net::download(url, std::string("/Books/") + name, err);
  lua_pushboolean(L, ok);
  if (ok) return 1;
  lua_pushstring(L, err.c_str());
  return 2;
}
int booksHas(lua_State* L) {
  const char* name = luaL_checkstring(L, 1);
  lua_pushboolean(L, plainName(name) && Storage.exists((std::string("/Books/") + name).c_str()));
  return 1;
}

int logPrint(lua_State* L) {
  std::string line;
  for (int i = 1; i <= lua_gettop(L); i++) {
    if (i > 1) line += " ";
    line += luaL_tolstring(L, i, nullptr);
    lua_pop(L, 1);
  }
  LOG_INF("APP", "%s: %s", app().manifest.id.c_str(), line.c_str());
  return 0;
}

void addLibrary(lua_State* L, const char* name, const luaL_Reg* functions) {
  luaL_newlib(L, functions);
  lua_setglobal(L, name);
}

}  // namespace

const char* deviceName() {
#if FREEINK_DEVICE_LILYGO
  return "t5";
#else
  return "x4pro";
#endif
}

bool readManifest(const std::string& id, Manifest& out) {
  const std::string path = std::string(DIR) + "/" + id + "/app.json";
  if (id.empty() || id[0] == '.' || !Storage.exists(path.c_str())) return false;
  JsonDocument doc;
  const String text = Storage.readFile(path.c_str());
  if (deserializeJson(doc, text.c_str())) return false;
  out = Manifest{};
  out.id = id;
  out.name = doc["name"] | "";
  out.description = doc["description"] | "";
  out.version = doc["version"] | 0;
  out.net = doc["net"] | false;
  out.books = doc["books"] | false;
  bool here = doc["devices"].isNull();
  for (JsonVariant d : doc["devices"].as<JsonArray>()) here = here || d.as<std::string>() == deviceName();
  return !out.name.empty() && here;
}

LuaApp::LuaApp(GfxRenderer& renderer) : renderer(renderer) {}

LuaApp::~LuaApp() { stop(); }

bool LuaApp::finish(const int status) {
  if (status == LUA_OK) return true;
  error_ = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "ukjent feil";
  LOG_ERR("APP", "%s: %s", manifest.id.c_str(), error_.c_str());
  lua_pop(L_, 1);
  stop();
  return false;
}

bool LuaApp::start(const Manifest& m) {
  stop();
  manifest = m;
  error_.clear();
  const std::string path = std::string(DIR) + "/" + m.id + "/main.lua";
  if (!Storage.exists(path.c_str())) {
    error_ = "main.lua mangler";
    return false;
  }
  const String script = Storage.readFile(path.c_str());
  if (script.length() == 0 || script.length() > MAX_SCRIPT) {
    error_ = "main.lua er tom eller for stor";
    return false;
  }
  current = this;
  memoryUsed_ = 0;
  L_ = lua_newstate(allocate, this);
  if (!L_) {
    error_ = "for lite minne";
    return false;
  }
  // Only the safe parts of Lua: no files, os, modules or debug library.
  luaL_requiref(L_, LUA_GNAME, luaopen_base, 1);
  luaL_requiref(L_, LUA_TABLIBNAME, luaopen_table, 1);
  luaL_requiref(L_, LUA_STRLIBNAME, luaopen_string, 1);
  luaL_requiref(L_, LUA_MATHLIBNAME, luaopen_math, 1);
  luaL_requiref(L_, LUA_UTF8LIBNAME, luaopen_utf8, 1);
  luaL_requiref(L_, LUA_COLIBNAME, luaopen_coroutine, 1);
  lua_settop(L_, 0);
  for (const char* name : {"dofile", "loadfile", "load", "require", "collectgarbage"}) {
    lua_pushnil(L_);
    lua_setglobal(L_, name);
  }
  lua_pushcfunction(L_, logPrint);
  lua_setglobal(L_, "print");

  static const luaL_Reg screen[] = {{"width", screenWidth},   {"height", screenHeight},     {"clear", screenClear},
                                    {"header", screenHeader}, {"text", screenText},         {"textWidth", screenTextWidth},
                                    {"lineHeight", screenLineHeight}, {"wrap", screenWrap}, {"line", screenLine},
                                    {"rect", screenRect},     {"dot", screenDot},           {nullptr, nullptr}};
  static const luaL_Reg appFns[] = {{"redraw", appRedraw}, {"every", appEvery},   {"close", appClose},
                                    {"millis", appMillis}, {"time", appTime},     {"device", appDevice},
                                    {nullptr, nullptr}};
  static const luaL_Reg storage[] = {{"read", storageRead},     {"write", storageWrite},
                                     {"remove", storageRemove}, {"list", storageList},
                                     {nullptr, nullptr}};
  static const luaL_Reg netFns[] = {{"get", netGet}, {"download", netDownload}, {nullptr, nullptr}};
  static const luaL_Reg books[] = {{"download", booksDownload}, {"has", booksHas}, {nullptr, nullptr}};
  addLibrary(L_, "screen", screen);
  addLibrary(L_, "app", appFns);
  addLibrary(L_, "storage", storage);
  addLibrary(L_, "net", netFns);
  addLibrary(L_, "books", books);
  lua_sethook(L_, watchdog, LUA_MASKCOUNT, 10000);

  callStartedMs_ = millis();
  const std::string chunk = "@" + m.id + "/main.lua";
  if (!finish(luaL_loadbufferx(L_, script.c_str(), script.length(), chunk.c_str(), "t"))) return false;
  if (!finish(lua_pcall(L_, 0, 0, 0))) return false;
  redraw_ = true;
  redrawMode_ = REFRESH_HALF;
  return call("start");
}

void LuaApp::stop() {
  if (L_) lua_close(L_);
  L_ = nullptr;
  if (current == this) current = nullptr;
  timerMs_ = 0;
  net::off();
}

namespace {
// Pushes the app's global function `name`; false (nothing pushed) when it has none.
bool pushFunction(lua_State* L, const char* name) {
  if (lua_getglobal(L, name) == LUA_TFUNCTION) return true;
  lua_pop(L, 1);
  return false;
}
}  // namespace

bool LuaApp::call(const char* function) {
  if (!L_ || !pushFunction(L_, function)) return L_ != nullptr;
  current = this;
  callStartedMs_ = millis();
  return finish(lua_pcall(L_, 0, 0, 0));
}

bool LuaApp::call(const char* function, const int x, const int y) {
  if (!L_ || !pushFunction(L_, function)) return L_ != nullptr;
  current = this;
  lua_pushinteger(L_, x);
  lua_pushinteger(L_, y);
  callStartedMs_ = millis();
  return finish(lua_pcall(L_, 2, 0, 0));
}

bool LuaApp::callButton(const char* name) {
  if (!L_ || !pushFunction(L_, "button")) return L_ != nullptr;
  current = this;
  lua_pushstring(L_, name);
  callStartedMs_ = millis();
  return finish(lua_pcall(L_, 1, 0, 0));
}

bool LuaApp::takeRedraw(int& mode) {
  if (!redraw_) return false;
  redraw_ = false;
  mode = redrawMode_;
  return true;
}

}  // namespace apps
