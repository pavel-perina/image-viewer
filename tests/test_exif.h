#pragma once

#include <cstdint>
#include <vector>

namespace iv::test {

// A complete APP1 payload: orientation 6, a Canon EOS 5D with a 50mm lens at 1/250 s, f/2.8,
// ISO 400, -2/3 EV, pattern metering and manual white balance, taken 2024-05-01 12:30:00.
std::vector<std::uint8_t> exampleExifBlock(bool littleEndian);

}  // namespace iv::test
