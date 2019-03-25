#ifndef CACHEX_SCSI_STATUS_H
#define CACHEX_SCSI_STATUS_H

#include <cstdint>

namespace ScsiStatus
{
static const std::uint8_t GOOD = 0x00;
static const std::uint8_t CHECK_CONDITION = 0x02;
static const std::uint8_t CONDITION_MET = 0x04;
static const std::uint8_t BUSY = 0x08;
static const std::uint8_t INTERMEDIATE = 0x10;
static const std::uint8_t INTERMEDIATE_COND_MET = 0x14;
static const std::uint8_t RESERVATION_CONFLICT = 0x18;
static const std::uint8_t COMMAND_TERMINATED = 0x22;
static const std::uint8_t QUEUE_FULL = 0x28;
} // namespace ScsiStatus

#endif
