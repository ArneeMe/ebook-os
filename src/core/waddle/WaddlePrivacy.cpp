#include "WaddlePrivacy.h"

#include <esp_random.h>

namespace waddle::privacy {

std::string hotspotPassword() {
  // New at every start, so it cannot be worked out from the device (the old
  // one came from its MAC address, which the hotspot sends out). Shown on the
  // screen and in the QR code. No 0/O or 1/l/I, so it is easy to type.
  static std::string password;
  if (password.empty()) {
    constexpr char ALPHABET[] = "abcdefghjkmnpqrstuvwxyz23456789";
    for (int i = 0; i < 10; i++) password += ALPHABET[esp_random() % (sizeof(ALPHABET) - 1)];
  }
  return password;
}

}  // namespace waddle::privacy
