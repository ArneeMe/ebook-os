#pragma once

// A script app (docs/waddle/APPS.md): a folder /Apper/<id>/ with app.json and
// main.lua, run in a Lua 5.4 state that can only draw, read input, keep files
// in its own data folder and, when app.json allows it, fetch from the web.

#include <cstdint>
#include <string>

class GfxRenderer;
struct lua_State;

namespace apps {

constexpr const char* DIR = "/Apper";

struct Manifest {
  std::string id;  // the folder name
  std::string name;
  std::string description;
  int version = 0;
  bool net = false;    // may use net.get / net.download
  bool books = false;  // may save books to /Books
};

// Reads /Apper/<id>/app.json. False when it is missing or has no name.
bool readManifest(const std::string& id, Manifest& out);
// "x4pro" or "t5": what app.json's "devices" lists.
const char* deviceName();

class LuaApp {
 public:
  explicit LuaApp(GfxRenderer& renderer);
  ~LuaApp();

  // Loads main.lua and runs it. False with error() set when it fails.
  bool start(const Manifest& manifest);
  void stop();

  // Calls the app's function of that name, when it has one. False when the
  // call fails (error() says why; the app is then stopped).
  bool call(const char* function);
  bool call(const char* function, int x, int y);
  bool callButton(const char* name);
  // answer(text) after app.ask(); nil when the keyboard was closed.
  bool callAnswer(const char* text);

  bool running() const { return L_ != nullptr; }
  const std::string& error() const { return error_; }

  // Set by app.redraw(mode) and app.every(ms), read and cleared by AppActivity.
  bool takeRedraw(int& mode);
  // Set by app.ask(prompt, text): AppActivity opens the keyboard.
  bool takeAsk(std::string& prompt, std::string& text);
  unsigned long timerMs() const { return timerMs_; }
  bool wantsClose() const { return close_; }

  // For the bindings (LuaApp.cpp).
  GfxRenderer& renderer;
  Manifest manifest;
  bool redraw_ = false;
  int redrawMode_ = 0;  // HalDisplay refresh: 0 fast, 1 half, 2 full
  unsigned long timerMs_ = 0;
  bool close_ = false;
  bool ask_ = false;
  std::string askPrompt_, askText_;
  unsigned long callStartedMs_ = 0;
  size_t memoryUsed_ = 0;

 private:
  lua_State* L_ = nullptr;
  std::string error_;

  bool finish(int status);
};

}  // namespace apps
