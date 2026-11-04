#pragma once

#include <string>

// WaddleOS privacy build (WADDLE_PRIVATE).
namespace waddle::privacy {

// Neutral DHCP hostname and user agent, no model name or MAC in them.
constexpr const char* HOSTNAME = "reader";
constexpr const char* USER_AGENT = "reader";
constexpr const char* HOTSPOT_SSID = "Waddle";

// WPA2 password for the hotspot: 10 random characters, new at every start.
std::string hotspotPassword();

}  // namespace waddle::privacy
