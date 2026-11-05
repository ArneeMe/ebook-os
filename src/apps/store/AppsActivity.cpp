#include "AppsActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <algorithm>

#include "AppActivity.h"
#include "AppNet.h"
#include "core/MappedInputManager.h"
#include "core/activities/ActivityManager.h"
#include "core/components/UITheme.h"
#include "core/fontIds.h"

namespace {

constexpr int SIDE = 20;

int drawTop(const GfxRenderer& renderer, const char* title) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight}, title);
  return metrics.topPadding + metrics.headerHeight + 18;
}

void drawRow(const GfxRenderer& renderer, const int y, const int h, const bool focused, const std::string& name,
             const std::string& note, const char* right) {
  const int width = renderer.getScreenWidth();
  if (focused) renderer.fillRect(0, y, 6, h - 4, true);
  const int rightW = renderer.getTextWidth(UI_12_FONT_ID, right, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, SIDE, y + 10,
                    renderer.truncatedText(UI_12_FONT_ID, name.c_str(), width - 3 * SIDE - rightW, EpdFontFamily::BOLD).c_str(),
                    true, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, SIDE, y + 44,
                    renderer.truncatedText(UI_10_FONT_ID, note.c_str(), width - 3 * SIDE - rightW).c_str());
  renderer.drawText(UI_12_FONT_ID, width - SIDE - rightW, y + 24, right, true, EpdFontFamily::BOLD);
  renderer.drawLine(SIDE, y + h - 2, width - SIDE, y + h - 2);
}

// Up/Down and taps on a list of rows; returns the row to open, or -1.
int listInput(const MappedInputManager& input, const std::vector<int>& rowY, const int rowH, const int count,
              int& focus) {
  using Button = MappedInputManager::Button;
  if (count == 0) return -1;
  if (input.wasReleased(Button::Down) || input.wasReleased(Button::PageForward)) {
    focus = (focus + 1) % count;
    return -2;
  }
  if (input.wasReleased(Button::Up) || input.wasReleased(Button::PageBack)) {
    focus = (focus + count - 1) % count;
    return -2;
  }
  if (input.wasReleased(Button::Confirm)) return focus;
  int x = 0, y = 0;
  if (input.wasScreenTapped(x, y)) {
    for (size_t r = 0; r < rowY.size(); r++) {
      if (rowY[r] >= 0 && y >= rowY[r] && y < rowY[r] + rowH) return int(r);
    }
  }
  return -1;
}

// Rows from `first`, scrolled so focus shows; fills rowY (-1 off screen).
int visibleFrom(const int top, const int height, const int rowH, const int count, const int focus, int& first,
                std::vector<int>& rowY) {
  const int fit = std::max(1, (height - top) / rowH);
  if (focus < first) first = focus;
  if (focus >= first + fit) first = focus - fit + 1;
  rowY.assign(size_t(count), -1);
  for (int r = first; r < std::min(count, first + fit); r++) rowY[size_t(r)] = top + (r - first) * rowH;
  return fit;
}

}  // namespace

// --- Apper ------------------------------------------------------------------

AppsActivity::AppsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Apps", renderer, mappedInput) {}

void AppsActivity::onEnter() {
  Activity::onEnter();
  auto found = apps::installed();
  {
    RenderLock lock(*this);
    scripts = std::move(found);
  }
  requestUpdate();
}

// Built-ins, then the script apps, then "Hent flere apper".
int AppsActivity::count() const { return int(apps::builtins().size() + scripts.size()) + 1; }

void AppsActivity::open(const int row) {
  const int builtins = int(apps::builtins().size());
  if (row < builtins) {
    apps::openBuiltin(size_t(row));
  } else if (row < builtins + int(scripts.size())) {
    activityManager.replaceActivity(std::make_unique<AppActivity>(renderer, mappedInput, scripts[size_t(row - builtins)]));
  } else {
    activityManager.replaceActivity(std::make_unique<StoreActivity>(renderer, mappedInput));
  }
}

void AppsActivity::loop() {
  using Button = MappedInputManager::Button;
  if (mappedInput.wasReleased(Button::Back) || mappedInput.wasHomeGesture()) {
    activityManager.goHome();
    return;
  }
  // Hold a downloaded app to remove it from the card (also one no longer in the store).
  int x = 0, y = 0;
  if (mappedInput.wasScreenLongPress(x, y)) {
    const int builtins = int(apps::builtins().size());
    for (int r = builtins; r < builtins + int(scripts.size()); r++) {
      if (rowY[size_t(r)] >= 0 && y >= rowY[size_t(r)] && y < rowY[size_t(r)] + rowH) {
        apps::uninstall(scripts[size_t(r - builtins)].id);
        auto left = apps::installed();
        {
          RenderLock lock(*this);
          scripts = std::move(left);
          focus = 0;
        }
        requestUpdate();
        return;
      }
    }
    return;
  }
  const int row = listInput(mappedInput, rowY, rowH, count(), focus);
  if (row == -2) requestUpdate();
  if (row >= 0) open(row);
}

void AppsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int top = drawTop(renderer, "Apper");
  visibleFrom(top, renderer.getScreenHeight(), rowH, count(), focus, first, rowY);
  const auto& builtins = apps::builtins();
  for (int r = 0; r < count(); r++) {
    const int y = rowY[size_t(r)];
    if (y < 0) continue;
    if (r < int(builtins.size())) {
      drawRow(renderer, y, rowH, focus == r, builtins[size_t(r)].name, builtins[size_t(r)].note, ">");
    } else if (r < int(builtins.size() + scripts.size())) {
      const auto& m = scripts[size_t(r) - builtins.size()];
      drawRow(renderer, y, rowH, focus == r, m.name, m.description, ">");
    } else {
      drawRow(renderer, y, rowH, focus == r, "+ Hent flere apper", "Fra waddleos på GitHub, når du trykker", "");
    }
  }
  renderer.displayBuffer();
}

// --- Hent flere apper -------------------------------------------------------------

StoreActivity::StoreActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Store", renderer, mappedInput) {}

void StoreActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void StoreActivity::onExit() {
  apps::net::off();
  Activity::onExit();
}

int StoreActivity::installedVersion(const std::string& id) const {
  apps::Manifest m;
  return apps::readManifest(id, m) ? std::max(m.version, 1) : 0;
}

// Not installed or older: fetch it. Up to date: nothing (hold to remove).
void StoreActivity::act(const int row) {
  if (row < 0 || row >= int(entries.size())) return;
  const auto& e = entries[size_t(row)];
  const int have = installedVersion(e.id);
  if (have && have >= e.version) return;
  {
    RenderLock lock(*this);
    busy = e.name;
    problem.clear();
  }
  requestUpdateAndWait();
  std::string err;
  const bool ok = apps::install(e, err);
  {
    RenderLock lock(*this);
    busy.clear();
    if (!ok) problem = err;
  }
  requestUpdate();
}

void StoreActivity::loop() {
  using Button = MappedInputManager::Button;
  if (mappedInput.wasReleased(Button::Back) || mappedInput.wasHomeGesture()) {
    activityManager.replaceActivity(std::make_unique<AppsActivity>(renderer, mappedInput));
    return;
  }
  // Fetched once the "Henter" screen is up.
  if (!fetched) {
    fetched = true;
    requestUpdateAndWait();
    std::vector<apps::StoreEntry> list;
    std::string err;
    const bool ok = apps::fetchIndex(list, err);
    {
      RenderLock lock(*this);
      entries = std::move(list);
      problem = ok ? "" : err;
      state = ok ? State::LIST : State::FAILED;
    }
    requestUpdate();
    return;
  }
  if (state == State::FAILED) {
    int x = 0, y = 0;
    if (mappedInput.wasReleased(Button::Confirm) || mappedInput.wasScreenTapped(x, y)) {
      fetched = false;
      state = State::LOADING;
      requestUpdate();
    }
    return;
  }
  int x = 0, y = 0;
  if (mappedInput.wasScreenLongPress(x, y)) {
    for (size_t r = 0; r < rowY.size(); r++) {
      if (rowY[r] >= 0 && y >= rowY[r] && y < rowY[r] + rowH && installedVersion(entries[r].id)) {
        apps::uninstall(entries[r].id);
        requestUpdate();
      }
    }
    return;
  }
  const int row = listInput(mappedInput, rowY, rowH, int(entries.size()), focus);
  if (row == -2) requestUpdate();
  if (row >= 0) act(row);
}

void StoreActivity::render(RenderLock&&) {
  const int width = renderer.getScreenWidth();
  renderer.clearScreen();
  int top = drawTop(renderer, "Hent apper");
  if (state != State::LIST) {
    const char* text = state == State::LOADING ? "Henter listen ..." : "Fikk ikke hentet listen";
    renderer.drawCenteredText(UI_12_FONT_ID, top + 60, text, true, EpdFontFamily::BOLD);
    if (state == State::FAILED) {
      renderer.drawCenteredText(UI_10_FONT_ID, top + 110, problem.c_str());
      renderer.drawCenteredText(SMALL_FONT_ID, top + 150, "Trykk for å prøve igjen");
    }
    renderer.displayBuffer();
    return;
  }
  if (!busy.empty() || !problem.empty()) {
    const std::string line = !busy.empty() ? "Henter " + busy + " ..." : problem;
    renderer.drawText(UI_10_FONT_ID, SIDE, top, renderer.truncatedText(UI_10_FONT_ID, line.c_str(), width - 2 * SIDE).c_str(),
                      true, EpdFontFamily::BOLD);
  }
  top += 40;
  if (entries.empty()) renderer.drawCenteredText(UI_12_FONT_ID, top + 40, "Ingen apper for denne enheten ennå");
  visibleFrom(top, renderer.getScreenHeight(), rowH, int(entries.size()), focus, first, rowY);
  for (size_t r = 0; r < entries.size(); r++) {
    if (rowY[r] < 0) continue;
    const auto& e = entries[r];
    const int have = installedVersion(e.id);
    const char* right = !have ? "Hent" : have < e.version ? "Oppdater" : "Har";
    drawRow(renderer, rowY[r], rowH, focus == int(r), e.name, e.description, right);
  }
  if (!entries.empty()) {
    renderer.drawCenteredText(SMALL_FONT_ID, renderer.getScreenHeight() - 40, "Hold på en app du har for å fjerne den");
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
