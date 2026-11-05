#pragma once

#include <memory>

#include "LuaApp.h"
#include "core/activities/Activity.h"

// Runs one script app full screen. Back (or the home gesture) closes it and
// returns to Apper; an error shows on the screen instead of the app.
class AppActivity final : public Activity {
 public:
  AppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, apps::Manifest manifest);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return touching; }

 private:
  apps::Manifest manifest;
  std::unique_ptr<apps::LuaApp> app;
  bool touching = false;
  int lastX = -1, lastY = -1;
  unsigned long lastTick = 0;
  int pendingMode = 0;
  int renders = 0;

  void afterCall();
};
