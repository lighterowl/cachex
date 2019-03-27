#ifndef CACHEX_WIN_H
#define CACHEX_WIN_H

#include "result.h"
#include <array>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace windows_detail
{
static const UCHAR SCSI_IOCTL_DATA_OUT = 0;
static const UCHAR SCSI_IOCTL_DATA_IN = 1;
static const UCHAR SCSI_IOCTL_DATA_UNSPECIFIED = 2;

static const DWORD IOCTL_SCSI_PASS_THROUGH_DIRECT = 0x4D014;

static double init_qpc_freq()
{
  LARGE_INTEGER freq;
  QueryPerformanceFrequency(&freq);
  return static_cast<double>((freq.QuadPart / 1000));
}

static void MP_QueryPerformanceCounter(LARGE_INTEGER *lpCounter)
{
  HANDLE hCurThread = GetCurrentThread();
  unsigned long dwOldMask = SetThreadAffinityMask(hCurThread, 1);

  QueryPerformanceCounter(lpCounter);
  SetThreadAffinityMask(hCurThread, dwOldMask);
}

typedef struct _SCSI_PASS_THROUGH_DIRECT
{
  USHORT Length;
  UCHAR ScsiStatus;
  UCHAR PathId;
  UCHAR TargetId;
  UCHAR Lun;
  UCHAR CdbLength;
  UCHAR SenseInfoLength;
  UCHAR DataIn;
  ULONG DataTransferLength;
  ULONG TimeOutValue;
  PVOID DataBuffer;
  ULONG SenseInfoOffset;
  UCHAR Cdb[16];
} SCSI_PASS_THROUGH_DIRECT, *PSCSI_PASS_THROUGH_DIRECT;

void sptd_exec(HANDLE handle, SCSI_PASS_THROUGH_DIRECT &sptd, CommandResult &rv)
{
  LARGE_INTEGER PerfCountStart, PerfCountEnd;
  static const double freq = windows_detail::init_qpc_freq();
  DWORD dwBytesReturned;

  windows_detail::MP_QueryPerformanceCounter(&PerfCountStart);
  auto io_ok = DeviceIoControl(handle, IOCTL_SCSI_PASS_THROUGH_DIRECT, &sptd,
                               sizeof(SCSI_PASS_THROUGH_DIRECT), NULL, 0,
                               &dwBytesReturned, NULL);
  windows_detail::MP_QueryPerformanceCounter(&PerfCountEnd);

  rv.Valid = io_ok ? true : false;
  rv.Duration = (PerfCountEnd.QuadPart - PerfCountStart.QuadPart) / freq;
  rv.Data.resize(sptd.DataTransferLength);
  rv.ScsiStatus = sptd.ScsiStatus;
}

template <std::size_t CDBLength>
SCSI_PASS_THROUGH_DIRECT
sptd_common(const std::array<std::uint8_t, CDBLength> &cdb)
{
  SCSI_PASS_THROUGH_DIRECT sptd;
  sptd.Length = sizeof(sptd);
  sptd.PathId = 0;            // SCSI card ID will be filled in automatically
  sptd.TargetId = 0;          // SCSI target ID will also be filled in
  sptd.Lun = 0;               // SCSI lun ID will also be filled in
  sptd.CdbLength = CDBLength; // CDB size
  sptd.SenseInfoLength = 0;   // Don't return any sense data
  sptd.TimeOutValue = 60;     // SCSI timeout value
  sptd.SenseInfoOffset = 0;
  std::copy(std::begin(cdb), std::end(cdb), sptd.Cdb);
  return sptd;
}

template <std::size_t CDBLength>
SCSI_PASS_THROUGH_DIRECT
sptd_for_read(CommandResult &rv, const std::array<std::uint8_t, CDBLength> &cdb)
{
  auto sptd = sptd_common(cdb);
  sptd.DataIn = SCSI_IOCTL_DATA_IN;
  sptd.DataTransferLength = rv.Data.size();
  sptd.DataBuffer = rv.Data.data();
  return sptd;
}

template <std::size_t CDBLength>
SCSI_PASS_THROUGH_DIRECT
sptd_for_write(const std::vector<std::uint8_t> &data,
               const std::array<std::uint8_t, CDBLength> &cdb)
{
  auto sptd = sptd_common(cdb);
  sptd.DataIn = SCSI_IOCTL_DATA_OUT;
  sptd.DataTransferLength = static_cast<ULONG>(data.size());
  sptd.DataBuffer = const_cast<std::uint8_t *>(data.data());
  return sptd;
}
} // namespace windows_detail

struct platform_windows
{
  using device_handle = HANDLE;

  static std::uint32_t monotonic_clock() { return GetTickCount(); }

  static device_handle open_volume(const char *DrivePath)
  {
    HANDLE hVolume;
    UINT uDriveType;
    char szVolumeName[8];
    char szRootName[5];
    DWORD dwAccessFlags;
    char DriveLetter = DrivePath[0];

    szRootName[0] = DriveLetter;
    szRootName[1] = ':';
    szRootName[2] = '\\';
    szRootName[3] = '\0';

    uDriveType = GetDriveType(szRootName);

    switch (uDriveType)
    {
    case DRIVE_CDROM:
      dwAccessFlags = GENERIC_READ | GENERIC_WRITE;
      break;

    default:
      printf("\nError: invalid drive type\n");
      return INVALID_HANDLE_VALUE;
    }

    szVolumeName[0] = '\\';
    szVolumeName[1] = '\\';
    szVolumeName[2] = '.';
    szVolumeName[3] = '\\';
    szVolumeName[4] = DriveLetter;
    szVolumeName[5] = ':';
    szVolumeName[6] = '\0';

    hVolume = CreateFile(szVolumeName, dwAccessFlags,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hVolume == INVALID_HANDLE_VALUE)
      printf("\nError: invalid handle");

    return hVolume;
  }

  static void close_handle(device_handle h) { CloseHandle(h); }

  static bool handle_is_valid(device_handle h)
  {
    return h != INVALID_HANDLE_VALUE;
  }

  static void set_critical_priority()
  {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
  }

  static void set_normal_priority()
  {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
  }

  template <std::size_t CDBLength>
  static void exec_command(device_handle handle, CommandResult &rv,
                           const std::array<std::uint8_t, CDBLength> &cdb)
  {
    using namespace windows_detail;
    auto sptd = sptd_for_read(rv, cdb);
    sptd_exec(handle, sptd, rv);
  }

  template <std::size_t CDBLength>
  static void send_data(device_handle handle, CommandResult &rv,
                        const std::array<std::uint8_t, CDBLength> &cdb,
                        const std::vector<std::uint8_t> &data)
  {
    using namespace windows_detail;
    auto sptd = sptd_for_write(data, cdb);
    windows_detail::sptd_exec(handle, sptd, rv);
  }
};

using platform = platform_windows;

#endif
