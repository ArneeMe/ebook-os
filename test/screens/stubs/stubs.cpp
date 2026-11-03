// What the screens need from the rest of the firmware, made small for the
// computer: the activity manager (records what would open next), the buttons
// (nothing pressed), the radio and GPS (driven by screens.cpp), settings files.

#include <HalDisplay.h>
#include <PersistableStore.h>

#include <random>

#include "Stubs.h"
#include "apps/reader/ReaderSettings.h"
#include "core/CrossPointSettings.h"
#include "core/MappedInputManager.h"
#include "core/activities/Activity.h"
#include "core/activities/ActivityManager.h"
#include "core/activities/RenderLock.h"
#include "services/mesh/MeshGps.h"
#include "services/mesh/MeshRadio.h"

// --- The simulated world (test/sim/stubs) -------------------------------------

namespace sim {
uint32_t nowMs = 1000;
uint32_t nowUnix = 0;
std::string rootDir;
std::mt19937 rng(7);
bool wifiAvailable = false;
bool wifiOn = false;
std::vector<std::vector<uint8_t>> rxQueue;
std::vector<std::vector<uint8_t>> sent;
mesh::gps::Fix fix;
int httpExchange(const std::string&, std::string&, std::string&) { return -1; }
}  // namespace sim

// Every time() in the firmware code (linked with -Wl,--wrap=time).
extern "C" time_t __wrap_time(time_t* out) {
  if (out) *out = sim::nowUnix;
  return sim::nowUnix;
}

namespace mesh::radio {
static bool up = false;
bool begin(const LoraParams&) { return up = true; }
bool isUp() { return up; }
bool receive(Received& out) {
  if (sim::rxQueue.empty()) return false;
  const auto p = sim::rxQueue.front();
  sim::rxQueue.erase(sim::rxQueue.begin());
  out.len = p.size();
  memcpy(out.data, p.data(), p.size());
  out.rssi = -90;
  out.snr = 5;
  return true;
}
bool send(const uint8_t* data, size_t len) {
  sim::sent.emplace_back(data, data + len);
  return true;
}
void pause() {}
void resume() {}
bool paused() { return false; }
int irqPin() { return 10; }
void rearm() {}
void end() { up = false; }
}  // namespace mesh::radio

namespace mesh::gps {
static bool isAwake = false;
void wake() { isAwake = true; }
void poll() {}
void sleep() { isAwake = false; }
bool awake() { return isAwake; }
const Fix& lastFix() { return sim::fix; }
uint32_t lastFixAtMs() { return sim::nowMs; }
const char* model() { return "sim"; }
}  // namespace mesh::gps

// --- Display -------------------------------------------------------------------

HalDisplay display;

// --- Activities ------------------------------------------------------------------

std::unique_ptr<Activity> sim::opened;

ActivityManager::ActivityManager(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : renderer(renderer), mappedInput(mappedInput) {}
ActivityManager::~ActivityManager() = default;
void ActivityManager::renderTaskLoop() { std::abort(); }
void ActivityManager::replaceActivity(std::unique_ptr<Activity>&& a) { sim::opened = std::move(a); }
void ActivityManager::pushActivity(std::unique_ptr<Activity>&& a) { sim::opened = std::move(a); }
void ActivityManager::popActivity() {}
void ActivityManager::goHome(bool) {}
void ActivityManager::goToFileTransfer() {}
void ActivityManager::goToLibrary() {}
void ActivityManager::goToSettings(SettingsSection) {}
void ActivityManager::goToReader(std::string, bool) {}
void ActivityManager::requestUpdate(bool) {}
void ActivityManager::requestUpdateAndWait() {}

RenderLock::RenderLock(Mode) : isLocked(true) {}
RenderLock::RenderLock(Activity&) : isLocked(true) {}
RenderLock::~RenderLock() = default;
void RenderLock::unlock() { isLocked = false; }
bool RenderLock::peek() { return false; }

// Nothing is pressed; a tap only when screens.cpp asks for one.
namespace {
bool tapPending = false;
int tapX = 0, tapY = 0;
}  // namespace
void sim::tap(const int x, const int y) {
  tapPending = true;
  tapX = x;
  tapY = y;
}
bool MappedInputManager::wasScreenTapped(int& x, int& y) const {
  if (!tapPending) return false;
  tapPending = false;
  x = tapX;
  y = tapY;
  return true;
}
bool MappedInputManager::wasPressed(Button) const { return false; }
bool MappedInputManager::wasReleased(Button) const { return false; }
bool MappedInputManager::isPressed(Button) const { return false; }
bool MappedInputManager::hasTouch() const { return true; }
bool MappedInputManager::wasScreenTouchDown(int&, int&) const { return false; }
bool MappedInputManager::isScreenTouchHeld(int&, int&) const { return false; }
bool MappedInputManager::wasHomeGesture() const { return false; }
bool MappedInputManager::wasBackGesture() const { return false; }
bool MappedInputManager::wasScreenTouchPressed(int&, int&) const { return false; }
bool MappedInputManager::wasScreenTouchReleased() const { return false; }
bool MappedInputManager::wasScreenLongPress(int&, int&) const { return false; }
MappedInputManager::SwipeDir MappedInputManager::wasSwipe(int*, int*) const { return SwipeDir::None; }
unsigned long MappedInputManager::getHeldTime() const { return 0; }
MappedInputManager::Labels MappedInputManager::mapLabels(const char* back, const char* confirm, const char* previous,
                                                         const char* next) const {
  return {back, confirm, previous, next};
}

// --- Settings: defaults, never saved -----------------------------------------------

void CrossPointSettings::toJson(JsonDocument&) const {}
bool PersistableStoreBase::writeDocToFile(const char*, const JsonDocument&) { return true; }
ReaderSettings::StatusBarSpec ReaderSettings::statusBarSpec() const { return {}; }

// --- uzlib checksums (only for zlib/gzip streams; fonts are raw deflate) ---------

extern "C" uint32_t uzlib_adler32(const void*, unsigned int, uint32_t prev) { return prev; }
extern "C" uint32_t uzlib_crc32(const void*, unsigned int, uint32_t crc) { return crc; }

// The screens behind the settings rows are not drawn here.
#include "products/t5s3/T5SettingsActivity.h"
void openT5Setting(T5SettingsActivity::Row) {}
// Apper: the T5's built-in rows (T5SettingsLinks.cpp), and the web for script
// apps read from this checkout's apps/ folder instead of GitHub.
#include <filesystem>
#include <fstream>
#include <sstream>

#include "apps/store/AppNet.h"
#include "apps/store/AppStore.h"
const std::vector<apps::Builtin>& apps::builtins() {
  static const std::vector<Builtin> list = {{"Bøker", "Biblioteket på minnekortet"},
                                            {"Nyheter", "NRK, hentes når du åpner den"}};
  return list;
}
void apps::openBuiltin(size_t) {}
bool apps::net::get(const std::string& url, std::string& body, size_t, std::string& err) {
  // .../waddle/<path>: sd/web/<path> when the screen sets one up, else this checkout's.
  const size_t at = url.find("/waddle/");
  const std::string path = at == std::string::npos ? "" : url.substr(at + 8);
  std::ifstream in(std::filesystem::exists(sim::rootDir + "/web/" + path) ? sim::rootDir + "/web/" + path : path);
  if (!in) {
    err = "ikke i simulatoren";
    return false;
  }
  std::stringstream text;
  text << in.rdbuf();
  body = text.str();
  return true;
}
bool apps::net::download(const std::string&, const std::string&, std::string& err) {
  err = "ikke i simulatoren";
  return false;
}
void apps::net::off() {}
