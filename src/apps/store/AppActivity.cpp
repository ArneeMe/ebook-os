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

void AppActivity::onExit() {
  {
    RenderLock lock(*this);
    if (app) app->call("stop");
    app.reset();
  }
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
  RenderLock lock(*this);
  for (const auto& [button, name] : BUTTONS) {
    if (mappedInput.wasReleased(button)) app->callButton(name);
  }
  int x = 0, y = 0;
  if (mappedInput.wasScreenTouchPressed(x, y)) {
    touching = true;
    lastX = x;
    lastY = y;
    app->call("press", x, y);
  } else if (touching && mappedInput.isScreenTouchHeld(x, y) && (x != lastX || y != lastY)) {
    lastX = x;
    lastY = y;
    app->call("move", x, y);
  }
  if (touching && mappedInput.wasScreenTouchReleased()) {
    touching = false;
    app->call("release", lastX, lastY);
  }
  if (mappedInput.wasScreenTapped(x, y)) app->call("tap", x, y);
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Left) app->callButton("next");
  if (swipe == MappedInputManager::SwipeDir::Right) app->callButton("prev");

  if (app->running() && app->timerMs() && millis() >= nextTick) {
    nextTick = millis() + app->timerMs();
    app->call("tick");
  }
  lock.unlock();
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
