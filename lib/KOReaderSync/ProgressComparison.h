#pragma once

#include <cstdint>

#include "WaddlePosition.h"

enum class ProgressComparison : uint8_t { LocalAhead, Synchronized, RemoteAhead, Unknown };

enum class RemoteRecordChoice : uint8_t { Primary, Alternate };

ProgressComparison compareProgress(const WaddlePosition& local, float localPercentage,
                                   const WaddlePosition& remote, float remotePercentage);

RemoteRecordChoice selectRemoteRecord(const WaddlePosition& primary, float primaryPercentage,
                                      const WaddlePosition& alternate, float alternatePercentage);
