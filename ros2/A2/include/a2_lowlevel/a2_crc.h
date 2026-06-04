#ifndef A2_LOWLEVEL_A2_CRC_H_
#define A2_LOWLEVEL_A2_CRC_H_

#include <cstddef>
#include <cstdint>

#include "unitree_hg/msg/low_cmd.hpp"

namespace a2_lowlevel {
namespace a2_crc {

std::uint32_t crc32_core(const std::uint32_t *words,
                         std::size_t word_count);
std::uint32_t compute_low_cmd_crc(const unitree_hg::msg::LowCmd &command);
void update_low_cmd_crc(unitree_hg::msg::LowCmd &command);

}  // namespace a2_crc
}  // namespace a2_lowlevel

#endif  // A2_LOWLEVEL_A2_CRC_H_
