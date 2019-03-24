#ifndef CACHEX_LINUX_H
#define CACHEX_LINUX_H

#define _POSIX_C_SOURCE 200112L

#include "result.h"
#include <array>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstring>
#include <time.h>

#include <scsi/sg.h>
#include <sys/ioctl.h>

namespace linux_detail
{
void sg_io_exec(int fd, sg_io_hdr_t &io, CommandResult &rv)
{
  auto io_rv = ::ioctl(fd, SG_IO, &io);
  rv.Valid = (io_rv == 0);
  rv.ScsiStatus = io.status;
  rv.Duration = io.duration;
}

template <std::size_t CDBLength>
sg_io_hdr_t sg_io_common(const std::array<std::uint8_t, CDBLength> &cdb)
{
  sg_io_hdr_t io;
  ::memset(&io, 0, sizeof(io));
  io.interface_id = 'S';
  io.timeout = 60000;
  io.cmdp = const_cast<unsigned char *>(cdb.data());
  io.cmd_len = CDBLength;
  return io;
}

template <std::size_t CDBLength>
sg_io_hdr_t sg_io_for_read(CommandResult &rv,
                           const std::array<std::uint8_t, CDBLength> &cdb)
{
  auto io = sg_io_common(cdb);
  io.dxfer_direction = SG_DXFER_FROM_DEV;
  io.dxfer_len = static_cast<unsigned int>(rv.Data.size());
  io.dxferp = rv.Data.data();
  return io;
}

template <std::size_t CDBLength>
sg_io_hdr_t sg_io_for_write(const std::vector<std::uint8_t> &data,
                            const std::array<std::uint8_t, CDBLength> &cdb)
{
  auto io = sg_io_common(cdb);
  io.dxfer_direction = SG_DXFER_TO_DEV;
  io.dxfer_len = static_cast<unsigned int>(data.size());
  io.dxferp = const_cast<std::uint8_t*>(data.data());
  return io;
}
} // namespace linux_detail

struct platform_linux
{
  typedef int device_handle;
  static device_handle open_volume(const char *path)
  {
    return ::open(path, O_RDWR | O_NONBLOCK | O_DIRECT);
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
    auto io = linux_detail::sg_io_for_read(rv, cdb);
    linux_detail::sg_io_exec(fd, io, rv);
  }

  template <std::size_t CDBLength>
  static void send_data(device_handle fd, CommandResult &rv,
                        const std::array<std::uint8_t, CDBLength> &cdb,
                        const std::vector<std::uint8_t> &data)
  {
    auto io = linux_detail::sg_io_for_write(data, cdb);
    linux_detail::sg_io_exec(fd, io, rv);
  }
};

typedef platform_linux platform;

#endif
