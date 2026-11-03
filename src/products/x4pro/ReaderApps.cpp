#include "apps/store/AppStore.h"

// Apper on the reader: only script apps; the rest is on Hjem.
namespace apps {

const std::vector<Builtin>& builtins() {
  static const std::vector<Builtin> none;
  return none;
}

void openBuiltin(size_t) {}

}  // namespace apps
