#ifndef CACHEX_NETBSD_H
#define CACHEX_NETBSD_H

#define _POSIX_C_SOURCE 200112L
#define _NETBSD_SOURCE

#include "result.h"
#include <array>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstring>
#include <time.h>

#include <sys/scsiio.h>
#include <sys/ioctl.h>

namespace netbsd_detail
{
void req_io_exec(int fd, scsireq_t &io, CommandResult &rv)
{
  struct timespec ts1, ts2, ts;
  ::clock_gettime(CLOCK_MONOTONIC, &ts1);
  auto io_rv = ::ioctl(fd, SCIOCCOMMAND, &io);
  ::clock_gettime(CLOCK_MONOTONIC, &ts2);
  timespecsub(&ts2, &ts1, &ts);
  rv.Valid = (io_rv == 0);
  rv.ScsiStatus = io.retsts;
  rv.Duration = (double) ((ts.tv_sec * 100000) + (ts.tv_nsec / 10000)) / 100;
}

template <std::size_t CDBLength>
scsireq_t req_io_common(const std::array<std::uint8_t, CDBLength> &cdb)
{
  scsireq_t io;
  ::memset(&io, 0, sizeof(io));
  io.timeout = 60000;
  std::copy(std::begin(cdb), std::end(cdb), io.cmd);
  io.cmdlen = CDBLength;
  return io;
}

template <std::size_t CDBLength>
scsireq_t req_io_for_read(CommandResult &rv,
                           const std::array<std::uint8_t, CDBLength> &cdb)
{
  auto io = req_io_common(cdb);
  io.flags = SCCMD_READ;
  io.datalen = static_cast<unsigned int>(rv.Data.size());
  io.databuf = rv.Data.data();
  return io;
}

template <std::size_t CDBLength>
scsireq_t req_io_for_write(const std::vector<std::uint8_t> &data,
                            const std::array<std::uint8_t, CDBLength> &cdb)
{
  auto io = req_io_common(cdb);
  io.flags = SCCMD_WRITE;
  io.datalen = static_cast<unsigned int>(data.size());
  io.databuf = const_cast<std::uint8_t *>(data.data());
  return io;
}
} // namespace netbsd_detail

struct platform_netbsd
{
  using device_handle = int;
  static device_handle open_volume(const char *path)
  {
    return ::open(path, O_RDWR | O_NONBLOCK | O_EXCL);
  }
  static bool handle_is_valid(device_handle fd) { return fd != -1; }
  static void close_handle(device_handle fd) { ::close(fd); }
  static std::uint32_t monotonic_clock()
  {
    struct timespec ts;
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint32_t>((ts.tv_sec * 1000) +
                                      (ts.tv_nsec / 1000000));
  }
  static void set_critical_priority() {}
  static void set_normal_priority() {}

  template <std::size_t CDBLength>
  static void exec_command(device_handle fd, CommandResult &rv,
                           const std::array<std::uint8_t, CDBLength> &cdb)
  {
    auto io = netbsd_detail::req_io_for_read(rv, cdb);
    netbsd_detail::req_io_exec(fd, io, rv);
  }

  template <std::size_t CDBLength>
  static void send_data(device_handle fd, CommandResult &rv,
                        const std::array<std::uint8_t, CDBLength> &cdb,
                        const std::vector<std::uint8_t> &data)
  {
    auto io = netbsd_detail::req_io_for_write(data, cdb);
    netbsd_detail::req_io_exec(fd, io, rv);
  }
};

using platform = platform_netbsd;

#endif
