#include "AppActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>

#include "AppsActivity.h"
#include "core/MappedInputManager.h"
#include "core/activities/ActivityManager.h"
#include "core/components/UITheme.h"
#include "core/fontIds.h"

AppActivity::AppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, apps::Manifest manifest)
    : Activity("App", renderer, mappedInput), manifest(std::move(manifest)) {}

void AppActivity::onEnter() {
  Activity::onEnter();
  app = std::make_unique<apps::LuaApp>(renderer);
  {
    RenderLock lock(*this);
    app->start(manifest);
  }
  requestUpdate();
}

// ActivityManager calls this holding the render lock (not recursive): no lock here.
void AppActivity::onExit() {
  if (app) app->call("stop");
  app.reset();
  Activity::onExit();
}

// After each call into the app: redraw when it asked, close when it asked.
void AppActivity::afterCall() {
  int mode = 0;
  if (app->wantsClose()) {
    activityManager.replaceActivity(std::make_unique<AppsActivity>(renderer, mappedInput));
    return;
  }
  if (!app->running() || app->takeRedraw(mode)) {
    pendingMode = std::max(pendingMode, mode);
    requestUpdate();
  }
}

void AppActivity::loop() {
  using Button = MappedInputManager::Button;
  if (mappedInput.wasReleased(Button::Back) || mappedInput.wasHomeGesture() || mappedInput.wasBackGesture()) {
    activityManager.replaceActivity(std::make_unique<AppsActivity>(renderer, mappedInput));
    return;
  }
  if (!app || !app->running()) return;

  static constexpr std::pair<Button, const char*> BUTTONS[] = {
      {Button::Up, "up"},     {Button::Down, "down"},         {Button::Left, "left"},        {Button::Right, "right"},
      {Button::Confirm, "ok"}, {Button::PageForward, "next"}, {Button::PageBack, "prev"},
  };
  // What happened since the last pass, read before taking the lock: the lock
  // is taken only when there is something for the app, so drawing (render)
  // always gets its turn.
  struct Event {
    const char* function;  // press, move, release, tap, or button with a name
    const char* name;
    int x, y;
  };
  Event events[12];
  int count = 0;
  auto add = [&](const char* function, const char* name, const int x, const int y) {
    if (count < 12) events[count++] = {function, name, x, y};
  };
  for (const auto& [button, name] : BUTTONS) {
    if (mappedInput.wasReleased(button)) add("button", name, 0, 0);
  }
  int x = 0, y = 0;
  if (mappedInput.wasScreenTouchPressed(x, y)) {
    touching = true;
    lastX = x;
    lastY = y;
    add("press", nullptr, x, y);
  } else if (touching && mappedInput.isScreenTouchHeld(x, y)) {
    if (x != lastX || y != lastY) {
      lastX = x;
      lastY = y;
      add("move", nullptr, x, y);
    }
  } else if (touching) {
    // Released, also when the release edge itself was missed.
    touching = false;
    add("release", nullptr, lastX, lastY);
  }
  if (mappedInput.wasScreenTapped(x, y)) add("tap", nullptr, x, y);
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Left) add("button", "next", 0, 0);
  if (swipe == MappedInputManager::SwipeDir::Right) add("button", "prev", 0, 0);
  const bool tick = app->timerMs() && millis() - lastTick >= app->timerMs();  // safe across the millis() wrap
  if (tick) lastTick = millis();
  if (!count && !tick) return;

  {
    RenderLock lock(*this);
    for (int i = 0; i < count && app->running(); i++) {
      const Event& e = events[i];
      if (e.name) {
        app->callButton(e.name);
      } else {
        app->call(e.function, e.x, e.y);
      }
    }
    if (tick && app->running()) app->call("tick");
  }
  afterCall();
}

void AppActivity::render(RenderLock&&) {
  if (app && app->running()) {
    app->call("draw");
    if (app->running()) {
      // A clean refresh now and then against e-ink ghosting.
      const int mode = ++renders % 30 == 0 ? std::max(pendingMode, 1) : pendingMode;
      pendingMode = 0;
      renderer.displayBuffer(mode == 2   ? HalDisplay::FULL_REFRESH
                             : mode == 1 ? HalDisplay::HALF_REFRESH
                                         : HalDisplay::FAST_REFRESH);
      return;
    }
  }
  // The app failed: what went wrong, and how to get out.
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, manifest.name.c_str());
  int y = metrics.topPadding + metrics.headerHeight + 40;
  renderer.drawText(UI_12_FONT_ID, 20, y, "Appen stoppet", true, EpdFontFamily::BOLD);
  y += 50;
  const std::string why = app ? app->error() : "";
  for (const auto& line : renderer.wrappedText(UI_10_FONT_ID, why.c_str(), width - 40, 10)) {
    renderer.drawText(UI_10_FONT_ID, 20, y, line.c_str());
    y += renderer.getLineHeight(UI_10_FONT_ID);
  }
  renderer.drawText(SMALL_FONT_ID, 20, y + 30, "Tilbake lukker den.");
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
