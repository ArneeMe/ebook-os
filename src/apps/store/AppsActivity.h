#pragma once

#include <string>
#include <vector>

#include "AppStore.h"
#include "core/activities/Activity.h"

// Apper: the device's built-in apps, the script apps on the SD card and
// "Hent flere apper" (the same screen on the reader and the T5).
class AppsActivity final : public Activity {
 public:
  AppsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::vector<apps::Manifest> scripts;
  std::vector<int> rowY;  // filled by render()
  int rowH = 84;
  int focus = 0;
  int first = 0;

  int count() const;
  void open(int row);
};

// Hent flere apper: the list in the waddleos repo, fetched when this opens.
// A tap installs, updates or removes an app.
class StoreActivity final : public Activity {
 public:
  StoreActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == State::LOADING || !busy.empty(); }

 private:
  enum class State { LOADING, LIST, FAILED };
  State state = State::LOADING;
  bool fetched = false;
  std::string problem;
  std::string busy;  // the app being installed, shown while it downloads
  std::vector<apps::StoreEntry> entries;
  std::vector<int> rowY;
  int rowH = 84;
  int focus = 0;
  int first = 0;

  int installedVersion(const std::string& id) const;
  void act(int row);
};
