#include "T5SettingsActivity.h"
#include "apps/news/NewsActivity.h"
#include "apps/news/NewsPaper.h"
#include "apps/store/AppStore.h"
#include "core/activities/network/WifiSelectionActivity.h"
#include "core/activities/settings/ClockSettingsActivity.h"
#include "core/waddle/UpdateActivity.h"

void openT5Setting(const T5SettingsActivity::Row row) {
  auto& renderer = activityManager.getRenderer();
  auto& input = activityManager.getMappedInput();
  switch (row) {
    case T5SettingsActivity::WIFI:
      activityManager.replaceActivity(std::make_unique<WifiSelectionActivity>(renderer, input, false));
      break;
    case T5SettingsActivity::CLOCK:
      activityManager.replaceActivity(std::make_unique<ClockSettingsActivity>(renderer, input));
      break;
    case T5SettingsActivity::UPDATE:
      activityManager.replaceActivity(std::make_unique<UpdateActivity>(renderer, input));
      break;
    case T5SettingsActivity::READING:
      activityManager.goToSettings(SettingsSection::Reader);
      break;
    default:
      break;
  }
}

// Apper on the T5: what is not about the mesh, before the script apps.
const std::vector<apps::Builtin>& apps::builtins() {
  static const std::vector<Builtin> list = {{"Bøker", "Biblioteket på minnekortet"},
                                            {"Nyheter", "NRK, hentes når du åpner den"}};
  return list;
}

void apps::openBuiltin(const size_t index) {
  if (index == 0) {
    activityManager.goToLibrary();
  } else if (index == 1) {
    activityManager.replaceActivity(std::make_unique<NewsActivity>(
        activityManager.getRenderer(), activityManager.getMappedInput(), waddle::news::due()));
  }
}
