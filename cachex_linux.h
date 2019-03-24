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
    sg_io_hdr_t io;
    ::memset(&io, 0, sizeof(io));
    io.interface_id = 'S';
    io.dxfer_direction = SG_DXFER_FROM_DEV;
    io.cmd_len = CDBLength;
    io.dxfer_len = static_cast<unsigned int>(rv.Data.size());
    io.dxferp = rv.Data.data();
    io.cmdp = const_cast<unsigned char *>(cdb.data());
    io.timeout = 60000;
    auto io_rv = ::ioctl(fd, SG_IO, &io);
    rv.Valid = (io_rv == 0);
    rv.ScsiStatus = io.status;
    rv.Duration = io.duration;
  }
};

typedef platform_linux platform;

#endif
