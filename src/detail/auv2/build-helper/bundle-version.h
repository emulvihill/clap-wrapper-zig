#pragma once

/*
 * The AudioComponents `version` integer the build helper writes into the
 * AUv2 Info.plist, derived from the dotted bundle version string.
 *
 * Kept in a header with no dependencies beyond the standard library so it can
 * be compiled and exercised on a machine without the macOS SDK.
 */

#include <algorithm>
#include <cstdint>
#include <string>

namespace Clap::AUv2
{

/*
 * 0xMMMMmmpp from "MAJOR.MINOR.PATCH". Up to three '.'-separated components
 * are read; missing ones are 0 ("1.0" and "1" both give 0x00010000) and any
 * after the third are ignored. The final component needs no trailing '.':
 * "0.1.1" is 0x0101, not 0x0100 -- the previous parser stopped before an
 * undelimited last component, so a patch bump never reached the host.
 *
 * A component is its leading decimal digits (none reads as 0), saturated to
 * the field it lands in -- MAJOR to 16 bits, MINOR and PATCH to 8 -- so an
 * oversized part can neither overflow nor spill into its neighbour. The result
 * keeps the helper's historic floor of 1.
 */
inline uint32_t bundleVersionToAUVersion(const std::string &bundleVersion)
{
  const uint32_t limit[3]{0xFFFF, 0xFF, 0xFF};
  uint32_t part[3]{0, 0, 0};
  std::string::size_type pos = 0;
  for (int i = 0; i < 3; ++i)
  {
    while (pos < bundleVersion.size() && bundleVersion[pos] >= '0' && bundleVersion[pos] <= '9')
    {
      // part[i] <= 0xFFFF here, so the multiply cannot overflow.
      part[i] = std::min<uint32_t>(part[i] * 10 + uint32_t(bundleVersion[pos] - '0'), limit[i]);
      ++pos;
    }
    auto dot = bundleVersion.find('.', pos);
    if (dot == std::string::npos) break;
    pos = dot + 1;
  }
  return std::max<uint32_t>((part[0] << 16) | (part[1] << 8) | part[2], 1);
}

}  // namespace Clap::AUv2
