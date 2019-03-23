/***********************************************************************************************
  CacheExplorer 0.9   spath@cdfreaks.com  2006/xx
/***********************************************************************************************/
#include <windows.h>
#include <winioctl.h>
#include <ntddscsi.h>
#define _NTSCSI_USER_MODE_
#include <scsi.h>

#include <cstdint>
#include <cstdio>

#include <array>
#include <vector>
#include <algorithm>

//#define RELEASE_VERSION
#undef RELEASE_VERSION
//--------------------------------------------------------------------------------------------------------
//------------------------------------------ CONSTANTS ---------------------------------------------------
//--------------------------------------------------------------------------------------------------------
#define NBPEAKMEASURES           100
#define NBDELTA                   50
#define MAX_CACHE_LINES           10
#define NB_IGNORE_MEASURES         5
#define NB_READ_COMMANDS           6


#define DESCRIPTOR_BLOCK_1         8   // offset of first block descriptor = size of mode parameter header

#define CACHING_MODE_PAGE         0x08
#define CD_DVD_CAPABILITIES_PAGE  0x2A

#define RCD_BIT                    1
#define RCD_READ_CACHE_ENABLED     0
#define RCD_READ_CACHE_DISABLED    1

//--------------------------------------------------------------------------------------------------------
//------------------------------------------- DEBUG ----------------------------------------------------
//--------------------------------------------------------------------------------------------------------
#define DEBUG(fmt, ...) if (DebugMode) printf(fmt, __VA_ARGS__);

#define SUPERDEBUG(fmt, ...) if (SuperDebugMode) printf(fmt, __VA_ARGS__);
//--------------------------------------------------------------------------------------------------------
//------------------------------------------- STRINGS ----------------------------------------------------
//--------------------------------------------------------------------------------------------------------
#ifndef RELEASE_VERSION
#define TESTINGSTRING  "\n[+] Testing %Xh... "
#define SUPPORTEDREADCOMMANDS "\n[+] Supported read commands:"
#define FUATEST        " FUA:"
#define FUAMSG         "(FUA)"
#define NOTSUPPORTED   "not supported"
#define OK             "ok"
#define ACCEPTED       "accepted"
#define REJECTED       "rejected"
#define TESTINGPLEXFUA "\n[+] Plextor flush command: "
#define TESTINGPLEXFUA2 "\n[+] Testing invalidation of Plextor flush command: "
#define AVERAGE_NORMAL "\n[+] Read at %d, %.2f ms"
#define AVERAGE_FUA    ", with FUA %f"
#define CACHELINESIZE  "\n[+] Cache line avg size (%d) = %5.0f kb"
#define CACHELINENB    "\n[+] Cache line numbers (%d) = %d"
#define READCOMMANDSNOTTESTED "\nError: No read command specified, use -i or -r switch\n"
#define FUAINVALIDATIONSIZE "\n-> Invalidated : %d sectors"
#define CACHELINENBTEST "\n[+] Testing cache line numbers:"
#define CACHELINESIZETEST "\n[+] Testing cache line size (method %d):"
#define CACHELINESIZETEST2 "\n[+] Testing cache line size:"
#define SPINNINGDRIVE  "\ninfo: spinning the drive... "
#define CACHELINESIZE2 "\n %d kB / %d sectors"
#else
#define TESTINGSTRING  "%X"
#define SUPPORTEDREADCOMMANDS "\nSRC:"
#define FUATEST        "|"
#define NOTSUPPORTED   "-"
#define OK             "+"
#define ACCEPTED       "accepted"
#define REJECTED       "rejected"
#define FUA            ""
#define FUAMSG         "f"
#define TESTINGPLEXFUA " PF:"
#define AVERAGE_NORMAL "\n0x%2X avn = %f"
#define AVERAGE_FUA    " avf = %f"
#define CACHELINESIZE  "\ncls(%d) = %5.0f"
#define CACHELINENB    "\ncln(%d) = %d"
#define READCOMMANDSNOTTESTED ""
#define FUAINVALIDATIONSIZE "\ninvfua = %d"
#define SPINNINGDRIVE  ""
#define CACHELINESIZE2  "\n%d (%.2f / %.2f -> %.2f)"
#endif

// global variables
static SCSI_PASS_THROUGH_DIRECT sptd;
static uint8_t* DataBuf;
static int NbBurstReadSectors = 1;
static uint8_t SenseBuf[255];
static LARGE_INTEGER PerfCountStart, PerfCountEnd;
static LARGE_INTEGER freq = {0};
static double Delay = 0, Delay2 = 0, InitDelay = 0;
static double AverageDelay = 0;
static int NbMeasures = 0;
static bool ReadCommandsDetected = false;
static unsigned int UserReadCommand = 0;
static HANDLE hVolume;
static bool DebugMode = false;
static bool SuperDebugMode = false;
static double ThresholdRatioMethod2 = 0.9;
static int CachedNonCachedSpeedFactor = 4;
static int MaxCacheSectors = 1000;
static int PeakMeasuresIndexes[NBPEAKMEASURES];

typedef struct
{
    int delta;
    int frequency;
    short divider;
} sDeltaArray;
static sDeltaArray DeltaArray[NBDELTA];

static void MP_QueryPerformanceCounter(LARGE_INTEGER* lpCounter)
{
    HANDLE hCurThread = GetCurrentThread();
    unsigned long dwOldMask = SetThreadAffinityMask(hCurThread, 1);

    QueryPerformanceCounter(lpCounter);
    SetThreadAffinityMask(hCurThread, dwOldMask);
}

static void ClearCDB()
{
    int i;
    for (i=0; i<16; i++)
    {
        sptd.Cdb[i] = 0;
    }
}

namespace Command {
  std::array<std::uint8_t, 12> Read_A8h(long int TargetSector, int NbSectors, bool FUAbit) {
    std::array<std::uint8_t, 12> rv = {
          0xA8,
          (FUAbit << 3),
          (TargetSector>>24)&0xFF, // msb
          (TargetSector>>16)&0xFF,
          (TargetSector>>8)&0xFF,
          (TargetSector)&0xFF,     // lsb
          (NbSectors>>24)&0xFF,    // msb
          (NbSectors>>16)&0xFF,
          (NbSectors>>8)&0xFF,
          (NbSectors)&0xFF,        // lsb
          0, 0
    };
    return rv;
  }

  std::array<std::uint8_t, 10> Read_28h(long int TargetSector, int NbSectors, bool FUAbit) {
    std::array<std::uint8_t, 10> rv = {
      0x28,
      (FUAbit << 3),
      uint8_t((TargetSector>>24)&0xFF), // msb
      uint8_t((TargetSector>>16)&0xFF),
      uint8_t((TargetSector>>8)&0xFF),
      uint8_t((TargetSector)&0xFF),     // lsb
      0,
      uint8_t((NbSectors>>8)&0xFF),     // msb
      uint8_t((NbSectors)&0xFF),        // lsb
      0
    };
    return rv;
  }

  std::array<std::uint8_t, 12> Read_BEh(long int TargetSector, int NbSectors) {
    std::array<std::uint8_t, 12> rv = {
      0xBE,
      0x00,                             // 0x04 = audio data only, 0x00 = any type
      uint8_t((TargetSector>>24)&0xFF), // address
      uint8_t((TargetSector>>16)&0xFF),
      uint8_t((TargetSector>>8)&0xFF),
      uint8_t((TargetSector)&0xFF),
      uint8_t((NbSectors>>16)&0xFF),    // size
      uint8_t((NbSectors>>8)&0xFF),
      uint8_t((NbSectors)&0xFF),
      0x10,                             // just data
      0,                                // no subcode
      0
    };
    return rv;
  }

  std::array<std::uint8_t, 10> Read_D4h(long int TargetSector, int NbSectors, bool FUAbit) {
    std::array<std::uint8_t, 10> rv = {
    0xD4,
    (FUAbit << 3),
    uint8_t((TargetSector>>24)&0xFF), // msb
    uint8_t((TargetSector>>16)&0xFF),
    uint8_t((TargetSector>>8)&0xFF),
    uint8_t((TargetSector)&0xFF),     // lsb
    uint8_t((NbSectors>>16)&0xFF),
    uint8_t((NbSectors>>8)&0xFF),
    uint8_t((NbSectors)&0xFF),        // lsb
    0
    };
    return rv;
  }

  std::array<std::uint8_t, 10> Read_D5h(long int TargetSector, int NbSectors, bool FUAbit) {
    std::array<std::uint8_t, 10> rv = {
    0xD5,
    (FUAbit << 3),
    uint8_t((TargetSector>>24)&0xFF), // msb
    uint8_t((TargetSector>>16)&0xFF),
    uint8_t((TargetSector>>8)&0xFF),
    uint8_t((TargetSector)&0xFF),     // lsb
    uint8_t((NbSectors>>16)&0xFF),
    uint8_t((NbSectors>>8)&0xFF),
    uint8_t((NbSectors)&0xFF),        // lsb
    0
    };
    return rv;
  }

  std::array<std::uint8_t, 12> Read_D8h(long int TargetSector, int NbSectors, bool FUAbit) {
    std::array<std::uint8_t, 12> rv = {
    0xD8,
    (FUAbit << 3),
    uint8_t((TargetSector>>24)&0xFF), // msb
    uint8_t((TargetSector>>16)&0xFF),
    uint8_t((TargetSector>>8)&0xFF),
    uint8_t((TargetSector)&0xFF),     // lsb
    uint8_t((NbSectors>>24)&0xFF),    // msb
    uint8_t((NbSectors>>16)&0xFF),
    uint8_t((NbSectors>>8)&0xFF),
    uint8_t((NbSectors)&0xFF),        // lsb
    0, 0
    };
    return rv;
  }

  std::array<std::uint8_t, 12> PlextorFUAFlush(long int TargetSector) {
    // this is just a Read28h with NbSectors = 0 and FUAbit = true. however, the original
    // code declared the CDB size as 12. whether this was a typo or not remains unknown,
    // which is why this code replicates the original behaviour.
    std::array<std::uint8_t, 12> rv = {
    0x28,       // READ(10) command
    0x08,       // FUA
    uint8_t((TargetSector>>24)&0xFF), // msb
    uint8_t((TargetSector>>16)&0xFF),
    uint8_t((TargetSector>>8)&0xFF),
    uint8_t((TargetSector)&0xFF),     // lsb
    0, 0, 0, 0, 0, 0
    // size stays zero, that's how this command works
    // MMC spec specifies that "A Transfer Length of zero indicates that no
    // logical blocks shall be transferred. This condition shall not be
    // considered an error"
    };
    return rv;
  }
  
  std::array<std::uint8_t, 6> RequestSense(std::uint8_t AllocationLength) {
    std::array<std::uint8_t, 6> rv = {
    3,                  // REQUEST SENSE
    0, 0, 0,
    AllocationLength,   // allocation size
    0
    };
    return rv;
  }

  std::array<std::uint8_t, 10> ModeSense(unsigned char PageCode, unsigned char SubPageCode, int size) {
    std::array<std::uint8_t, 10> rv = {
    0x5A,                     // MODE SENSE(10)
    0,
    PageCode,
    SubPageCode,
    0, 0, 0,
    uint8_t((size>>8)&0xFF),     // size
    uint8_t((size)&0xFF),
    0
    };
    return rv;
  }

  std::array<std::uint8_t, 10> ModeSelect(unsigned char PageCode, unsigned char SubPageCode, int size) {
    // SPC-4 declares bytes 2 to 6 of MODE SELECT as reserved - they should thus be set to zero. this replicates the behaviour of the original code,
    // which puts the page and subpage codes there. this might've been just a copypasta error, though, since
    // the assignment of the operation code contained a "MODE SENSE(10)".
    std::array<std::uint8_t, 10> rv = {
    0x55,                     // MODE SENSE(10)
    0,
    PageCode,
    SubPageCode,
    0, 0, 0,
    uint8_t((size>>8)&0xFF),     // size
    uint8_t((size)&0xFF),
    0
    };
    return rv;
  }

  std::array<std::uint8_t, 10> Prefetch(long int TargetSector, unsigned int NbSectors) {
    std::array<std::uint8_t, 10> rv = {
    0x34,    // PREFETCH
    0,
    uint8_t((TargetSector>>24)&0xFF), // target sector
    uint8_t((TargetSector>>16)&0xFF),
    uint8_t((TargetSector>>8)&0xFF),
    uint8_t((TargetSector)&0xFF),
    0,
    uint8_t((NbSectors>>8)&0xFF),     // size
    uint8_t((NbSectors)&0xFF),
    0
    };
    return rv;
  }

  std::array<std::uint8_t, 6> Inquiry(std::uint8_t AllocationLength) {
    std::array<std::uint8_t, 6> rv = { 0x12, 0, 0, 0, AllocationLength, 0 };
    return rv;
  }

  std::array<std::uint8_t, 12> SetCDSpeed(unsigned char ReadSpeedX, unsigned char WriteSpeedX) {
    unsigned int ReadSpeedkB  = (ReadSpeedX  * 176) + 2;  // 1x CD = 176kB/s
    unsigned int WriteSpeedkB = (WriteSpeedX * 176);
    std::array<std::uint8_t, 12> rv = {
    0xBB,    // SET CD SPEED
    0,
    (ReadSpeedX == 0) ? 0xFF : (ReadSpeedkB>>8)&0xFF,
    (ReadSpeedX == 0) ? 0xFF : ReadSpeedkB&0xFF,
    (WriteSpeedkB>>8)&0xFF,
     WriteSpeedkB&0xFF,
    0, 0, 0, 0, 0, 0
    };
    return rv;
  }
}

template<std::size_t CDBLength>
static void InitSPTDStructureForREAD(unsigned int NbSectors, const std::array<std::uint8_t, CDBLength> &cdb) {
  // fill in sptd structure
  sptd.Length             = sizeof(sptd);
  sptd.PathId             = 0;                  // SCSI card ID will be filled in automatically
  sptd.TargetId           = 0;                  // SCSI target ID will also be filled in
  sptd.Lun                = 0;                  // SCSI lun ID will also be filled in
  sptd.CdbLength          = CDBLength;          // CDB size
  sptd.SenseInfoLength    = 0;                  // Don't return any sense data
  sptd.DataIn             = SCSI_IOCTL_DATA_IN; // There will be data from drive
  sptd.DataTransferLength = 2448 * NbSectors;   // Size of data
  sptd.TimeOutValue       = 60;                 // SCSI timeout value
  sptd.DataBuffer         = (PVOID)(DataBuf);
  sptd.SenseInfoOffset    = 0;
  std::copy(std::begin(cdb), std::end(cdb), sptd.Cdb);
}

//--------------------------------------------------------------------------------------------------------
//-------------------------------------- Read functions --------------------------------------------------
//--------------------------------------------------------------------------------------------------------

static bool Read_A8h(char DriveLetter, long int TargetSector, int NbSectors, bool FUAbit)
{
    bool retval;
    DWORD dwBytesReturned;

    InitSPTDStructureForREAD(NbSectors, Command::Read_A8h(TargetSector, NbSectors, FUAbit));

    // send command
    MP_QueryPerformanceCounter(&PerfCountStart);
    retval = DeviceIoControl(hVolume,IOCTL_SCSI_PASS_THROUGH_DIRECT,(PVOID)&sptd,
                             (DWORD)sizeof(SCSI_PASS_THROUGH_DIRECT), NULL, 0, &dwBytesReturned,NULL);
    MP_QueryPerformanceCounter(&PerfCountEnd);

    return retval;
}

static bool Read_28h(char DriveLetter, long int TargetSector, int NbSectors, bool FUAbit)
{
    bool retval;
    DWORD dwBytesReturned;

    InitSPTDStructureForREAD(NbSectors, Command::Read_28h(TargetSector, NbSectors, FUAbit));

    // send command
    MP_QueryPerformanceCounter(&PerfCountStart);
    retval = DeviceIoControl(hVolume,IOCTL_SCSI_PASS_THROUGH_DIRECT,(PVOID)&sptd,
                             (DWORD)sizeof(SCSI_PASS_THROUGH_DIRECT), NULL, 0, &dwBytesReturned,NULL);
    MP_QueryPerformanceCounter(&PerfCountEnd);

    return retval;
}

static bool Read_BEh(char DriveLetter, long int TargetSector, int NbSectors, bool FUAbit)
{
    bool retval;
    DWORD dwBytesReturned;

    InitSPTDStructureForREAD(NbSectors, Command::Read_BEh(TargetSector, NbSectors));

    // send command
    MP_QueryPerformanceCounter(&PerfCountStart);
    retval = DeviceIoControl(hVolume,IOCTL_SCSI_PASS_THROUGH_DIRECT,(PVOID)&sptd,
                             (DWORD)sizeof(SCSI_PASS_THROUGH_DIRECT), NULL, 0, &dwBytesReturned,NULL);
    MP_QueryPerformanceCounter(&PerfCountEnd);

    return retval;
}

static bool Read_D4h(char DriveLetter, long int TargetSector, int NbSectors, bool FUAbit)
{
    bool retval;
    DWORD dwBytesReturned;

    InitSPTDStructureForREAD(NbSectors, Command::Read_D4h(TargetSector, NbSectors, FUAbit));

    // send command
    MP_QueryPerformanceCounter(&PerfCountStart);
    retval = DeviceIoControl(hVolume,IOCTL_SCSI_PASS_THROUGH_DIRECT,(PVOID)&sptd,
                             (DWORD)sizeof(SCSI_PASS_THROUGH_DIRECT), NULL, 0, &dwBytesReturned,NULL);
    MP_QueryPerformanceCounter(&PerfCountEnd);

    return retval;
}

static bool Read_D5h(char DriveLetter, long int TargetSector, int NbSectors, bool FUAbit)
{
    bool retval;
    DWORD dwBytesReturned;

    InitSPTDStructureForREAD(NbSectors, Command::Read_D5h(TargetSector, NbSectors, FUAbit));

    // send command
    MP_QueryPerformanceCounter(&PerfCountStart);
    retval = DeviceIoControl(hVolume,IOCTL_SCSI_PASS_THROUGH_DIRECT,(PVOID)&sptd,
                             (DWORD)sizeof(SCSI_PASS_THROUGH_DIRECT), NULL, 0, &dwBytesReturned,NULL);
    MP_QueryPerformanceCounter(&PerfCountEnd);

    return retval;
}


static bool Read_D8h(char DriveLetter, long int TargetSector, int NbSectors, bool FUAbit)
{
    bool retval;
    DWORD dwBytesReturned;

    InitSPTDStructureForREAD(NbSectors, Command::Read_D8h(TargetSector, NbSectors, FUAbit));

    // send command
    MP_QueryPerformanceCounter(&PerfCountStart);
    retval = DeviceIoControl(hVolume,IOCTL_SCSI_PASS_THROUGH_DIRECT,(PVOID)&sptd,
                             (DWORD)sizeof(SCSI_PASS_THROUGH_DIRECT), NULL, 0, &dwBytesReturned,NULL);
    MP_QueryPerformanceCounter(&PerfCountEnd);

    return retval;
}

// drive characteristics
static int CacheLineSizeSectors = 0;
static int CacheLineNumbers = 0;
static int NbCacheLines = 0;

typedef struct
{
    unsigned char FuncByte;
    bool (*pFunc)(char, long int, int, bool);
    bool Supported;
    bool FUAbitSupported;
} sReadCommand;

static sReadCommand Commands[NB_READ_COMMANDS] = {
    {0xBE, &Read_BEh, false, false},
    {0xA8, &Read_A8h, false, true},
    {0x28, &Read_28h, false, true},
    {0xD4, &Read_D4h, false, true},
    {0xD5, &Read_D5h, false, true},
    {0xD8, &Read_D8h, false, true}
};

//--------------------------------------------------------------------------------------------------------
//------------------------------------------------- CODE -------------------------------------------------
//--------------------------------------------------------------------------------------------------------

static bool PlextorFUAFlush(char DriveLetter, long int TargetSector)
{
    bool retval;
    DWORD dwBytesReturned;

    InitSPTDStructureForREAD(0, Command::PlextorFUAFlush(TargetSector));

    // send command
    MP_QueryPerformanceCounter(&PerfCountStart);
    retval = DeviceIoControl(hVolume,IOCTL_SCSI_PASS_THROUGH_DIRECT,(PVOID)&sptd,
                             (DWORD)sizeof(SCSI_PASS_THROUGH_DIRECT), NULL, 0, &dwBytesReturned,NULL);
    MP_QueryPerformanceCounter(&PerfCountEnd);
    return retval;
}

static bool RequestSense(char DriveLetter)
{
    bool retval;
    DWORD dwBytesReturned;
    const std::uint8_t AllocationLength = 18;

    // fill in sptd structure
    InitSPTDStructureForREAD(0, Command::RequestSense(AllocationLength));   // command is 5 bytes long, we set the buffer size at next line
    sptd.DataTransferLength = AllocationLength;    // Size of data

    // send command
    retval = DeviceIoControl(hVolume,IOCTL_SCSI_PASS_THROUGH_DIRECT,(PVOID)&sptd,
                             (DWORD)sizeof(SCSI_PASS_THROUGH_DIRECT), NULL, 0, &dwBytesReturned,NULL);
    return retval;
}

static bool ModeSense(char DriveLetter, unsigned char PageCode, unsigned char SubPageCode, int size)
{
    bool retval;
    DWORD dwBytesReturned;

    // fill in sptd structure
    InitSPTDStructureForREAD(0, Command::ModeSense(PageCode, SubPageCode, size));    // command is 10 bytes long, we set the buffer size at next line
    sptd.DataTransferLength = size;    // Size of data

    // send command
    retval = DeviceIoControl(hVolume,IOCTL_SCSI_PASS_THROUGH_DIRECT,(PVOID)&sptd,
                             (DWORD)sizeof(SCSI_PASS_THROUGH_DIRECT), NULL, 0, &dwBytesReturned,NULL);
    return retval;
}

// WARNING: this ModeSelect function should always be called just after a ModeSense call
// because the Mode PArameter List is not rebuilt !
static bool ModeSelect(char DriveLetter, unsigned char PageCode, unsigned char SubPageCode, int size)
{
    bool retval;
    DWORD dwBytesReturned;

    // fill in sptd structure
    InitSPTDStructureForREAD(0, Command::ModeSelect(PageCode, SubPageCode, size));    // command is 10 bytes long, we set the buffer size at next line
    sptd.DataTransferLength = size;    // Size of data

    // send command
    retval = DeviceIoControl(hVolume,IOCTL_SCSI_PASS_THROUGH_DIRECT,(PVOID)&sptd,
                             (DWORD)sizeof(SCSI_PASS_THROUGH_DIRECT), NULL, 0, &dwBytesReturned,NULL);
    return retval;
}

static bool Prefetch(char DriveLetter, long int TargetSector, unsigned int NbSectors)
{
    bool retval;
    DWORD dwBytesReturned;

    // fill in sptd structure
    InitSPTDStructureForREAD(0, Command::Prefetch(TargetSector, NbSectors));   // command is 10 bytes long, we set the buffer size at next line
    sptd.DataTransferLength = 18;     // Size of data

    // send command
    retval = DeviceIoControl(hVolume,IOCTL_SCSI_PASS_THROUGH_DIRECT,(PVOID)&sptd,
                             (DWORD)sizeof(SCSI_PASS_THROUGH_DIRECT), NULL, 0, &dwBytesReturned,NULL);
    return retval;
}

static HANDLE OpenVolume(char DriveLetter)
{
    HANDLE hVolume;
    UINT uDriveType;
    char szVolumeName[8];
    char szRootName[5];
    DWORD dwAccessFlags;

    szRootName[0]=DriveLetter;
    szRootName[1]=':';
    szRootName[2]='\\';
    szRootName[3]='\0';

    uDriveType = GetDriveType(szRootName);

    switch(uDriveType) {
    case DRIVE_CDROM:
        dwAccessFlags = GENERIC_READ | GENERIC_WRITE;
        break;

    default:
        printf("\nError: invalid drive type\n");
        return INVALID_HANDLE_VALUE;
    }

    szVolumeName[0]='\\';
    szVolumeName[1]='\\';
    szVolumeName[2]='.';
    szVolumeName[3]='\\';
    szVolumeName[4]=DriveLetter;
    szVolumeName[5]=':';
    szVolumeName[6]='\0';

    hVolume = CreateFile( szVolumeName, dwAccessFlags, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );

    if (hVolume == INVALID_HANDLE_VALUE)
        printf("\nError: invalid handle");

    return hVolume;
}

static bool CloseVolume(HANDLE hVolume)
{
    return CloseHandle(hVolume);
}

static void PrintIDString(unsigned char* dataChars, int dataLength)
{
    if (dataChars != NULL)
    {
        printf(" ");
        while (0 < dataLength--)
        {
            char cc = *dataChars++;
            cc &= 0x7F;
            if (!((0x20 <= cc) && (cc <= 0x7E)))
            {
                cc ^= 0x40;
            }
            printf("%c",cc);
        }
    }
}

static bool PrintDriveInfo(char DriveLetter)
{
    bool retval;
    DWORD dwBytesReturned;

    // fill in sptd structure
    const std::uint8_t AllocationLength = 36;
    InitSPTDStructureForREAD(0, Command::Inquiry(AllocationLength));    // INQUIRY command is 6 bytes long
    sptd.DataTransferLength = AllocationLength;     // Size of data

    // send command
    retval = DeviceIoControl(hVolume,IOCTL_SCSI_PASS_THROUGH_DIRECT,(PVOID)&sptd,
                             (DWORD)sizeof(SCSI_PASS_THROUGH_DIRECT), NULL, 0, &dwBytesReturned,NULL);

    // print info
    PrintIDString(&DataBuf[8], 8);         // vendor Id
    PrintIDString(&DataBuf[0x10], 0x10);   // product Id
    PrintIDString(&DataBuf[0x20], 4);      // product RevisionLevel

    return retval;
}

// bool ClearCache()
//
// fills the cache by reading backwards several areas at the beginning of the disc
//
static bool ClearCache(char DriveLetter)
{
    int i,j;
    bool retval = false;

    for (i=0 ; i<NB_READ_COMMANDS ; i++)
    {
        if (Commands[i].Supported)
        {
            for (j=0 ; j<MAX_CACHE_LINES ; j++)
            {
                retval += Commands[i].pFunc(DriveLetter, (MAX_CACHE_LINES-j+1)*1000, 1, false);
            }
            retval = true;
            break;
        }
    }
    return retval;

}

static bool SpinDrive(char DriveLetter, unsigned int Seconds)
{
    bool retval = false;
    int i = 0, j=0;
    DWORD TimeStart;

    for (i=0 ; i<NB_READ_COMMANDS ; i++)
    {
        if (Commands[i].Supported)
        {
            retval = true;
            break;
        }
    }

    if (retval)
    {
        DEBUG(SPINNINGDRIVE);
        TimeStart = GetTickCount();
        while( GetTickCount() - TimeStart <= (unsigned long)(Seconds * 1000) )
        {
            Commands[i].pFunc(DriveLetter, (10000+(j++))%50000, 1, false);
        }
    }
    return(retval);
}

static bool SetDriveSpeed(char DriveLetter, unsigned char ReadSpeedX, unsigned char WriteSpeedX)
{
    bool retval = false;

    DWORD dwBytesReturned;

    // fill in sptd structure
    InitSPTDStructureForREAD(0, Command::SetCDSpeed(ReadSpeedX, WriteSpeedX));    // command is 12 bytes long, we set the buffer size at next line
    sptd.DataTransferLength = 18;      // Size of data

    // send command
    retval = DeviceIoControl(hVolume,IOCTL_SCSI_PASS_THROUGH_DIRECT,(PVOID)&sptd,
                             (DWORD)sizeof(SCSI_PASS_THROUGH_DIRECT), NULL, 0, &dwBytesReturned,NULL);
    return retval;
}

static void ShowCacheValues(char DriveLetter)
{
    if (ModeSense(DriveLetter, CD_DVD_CAPABILITIES_PAGE, 0, 32))
    {
        printf("\n[+] Buffer size: %d kB",(DataBuf[DESCRIPTOR_BLOCK_1+12]<<8)+DataBuf[DESCRIPTOR_BLOCK_1+13]);
    }
    else
    {
        SUPERDEBUG("\ninfo: cannot read CD/DVD Capabilities page");
        RequestSense(DriveLetter);
    }
    if (ModeSense(DriveLetter, CACHING_MODE_PAGE, 0, 18))
    {
        printf(", read cache is %s", (DataBuf[DESCRIPTOR_BLOCK_1+2] & RCD_BIT) ? "disabled" : "enabled");
    }
    else
    {
        SUPERDEBUG("\ninfo: cannot read Caching Mode page");
        RequestSense(DriveLetter);
    }
}

static bool SetCacheRCDBit(char DriveLetter, bool RCDBitValue)
{
    bool retval = false;

    if (ModeSense(DriveLetter, CACHING_MODE_PAGE, 0, 18))
    {
        DataBuf[DESCRIPTOR_BLOCK_1+2] = (DataBuf[DESCRIPTOR_BLOCK_1+2] & 0xFE) + RCDBitValue;
        if (ModeSelect(DriveLetter, CACHING_MODE_PAGE, 0, 20))
        {
            ModeSense(DriveLetter, CACHING_MODE_PAGE, 0, 18);
            if ((DataBuf[DESCRIPTOR_BLOCK_1+2] & RCD_BIT) == RCDBitValue)
            {
                retval = true;
            }
        }

        if (!retval)
        {
            DEBUG("\ninfo: cannot write Caching Mode page");
            RequestSense(DriveLetter);
        }
    }
    else
    {
        DEBUG("\ninfo: cannot read Caching Mode page");
        RequestSense(DriveLetter);
    }
    return(retval);
}

//--------------------------------------------------------------------------------------------------------
//-------------------------------------- Test functions --------------------------------------------------
//--------------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------------
// void TestSupportedReadCommands(char DriveLetter)
//
// test and display which read commands are supported by the current drive
// and if any of these commands supports the FUA bit
//--------------------------------------------------------------------------------------------------------
static void TestSupportedReadCommands(char DriveLetter)
{
    int i;

    printf(SUPPORTEDREADCOMMANDS);
    for (i=0; i<NB_READ_COMMANDS; i++)
    {
        if (Commands[i].pFunc(DriveLetter, 10000, 1, false))
        {
            printf(" %2Xh",(Commands[i].FuncByte)&0xFF);
            Commands[i].Supported = true;
            if (Commands[i].FUAbitSupported)
            {
                if (Commands[i].pFunc(DriveLetter, 9900, 1, true))
                {
                    printf(FUAMSG);
                }
                else
                {
                    SUPERDEBUG("\ncommand %2Xh with FUA bit rejected",Commands[i].FuncByte);
                    Commands[i].FUAbitSupported = false;
                    RequestSense(DriveLetter);
                }
            }
        }
        else
        {
            SUPERDEBUG("\ncommand %2Xh rejected",Commands[i].FuncByte);
            RequestSense(DriveLetter);
        }
    }
    ReadCommandsDetected = true;
}

//
// TestPlextorFUACommand
//
// test if Plextor's flushing command is supported
static bool TestPlextorFUACommand(char DriveLetter, int NbIterations)
{
    printf(TESTINGPLEXFUA);
    if (!PlextorFUAFlush(DriveLetter, 100000))
    {
        printf(REJECTED);
        DEBUG(" (status = %d)",sptd.ScsiStatus);
        return(false);
    }
    else
    {
        printf(ACCEPTED);
        DEBUG(" (status = %d)",sptd.ScsiStatus);
        return(true);
    }
}

//
// TestPlextorFUACommandWorks
//
// test if Plextor's flushing command actually works
static int TestPlextorFUACommandWorks(char DriveLetter, int ReadCommand, long int TargetSector, int NbTests)
{
    int i;
    int InvalidationSuccess = 0;
    double InitDelay2 = 0;

    DEBUG("\ninfo: %d test(s), c/nc ratio: %d, burst: %d, max: %d",
           NbTests, CachedNonCachedSpeedFactor, NbBurstReadSectors, MaxCacheSectors);

    for (i=0; i<NbTests; i++)
    {
        // first test : normal cache test
        ClearCache(DriveLetter);
        Commands[ReadCommand].pFunc(DriveLetter, TargetSector, NbBurstReadSectors, false); // init read
        InitDelay = ((double)PerfCountEnd.QuadPart - (double)PerfCountStart.QuadPart) / (double)freq.QuadPart;
        Commands[ReadCommand].pFunc(DriveLetter, TargetSector + NbBurstReadSectors, NbBurstReadSectors, false);
        Delay = ((double)PerfCountEnd.QuadPart - (double)PerfCountStart.QuadPart) / (double)freq.QuadPart;

        // second test : add a Plextor FUA flush command in between
        ClearCache(DriveLetter);
        Commands[ReadCommand].pFunc(DriveLetter, TargetSector, NbBurstReadSectors, false); // init read
        InitDelay2 = ((double)PerfCountEnd.QuadPart - (double)PerfCountStart.QuadPart) / (double)freq.QuadPart;
        PlextorFUAFlush(DriveLetter, TargetSector);
        Commands[ReadCommand].pFunc(DriveLetter, TargetSector + NbBurstReadSectors, NbBurstReadSectors, false);
        Delay2 = ((double)PerfCountEnd.QuadPart - (double)PerfCountStart.QuadPart) / (double)freq.QuadPart;
        DEBUG("\n %.2f ms / %.2f ms -> %.2f ms / %.2f ms", InitDelay, Delay, InitDelay2, Delay2);

        // compare times
        if (Delay2 > (CachedNonCachedSpeedFactor * Delay))
        {
            InvalidationSuccess++;
        }
    }
    DEBUG("\nresult: ");
    return(InvalidationSuccess);
}

// wrapper for TestPlextorFUACommandWorks
static int TestPlextorFUACommandWorksWrapper(char DriveLetter, long int TargetSector, int NbTests)
{
    int ValidReadCommand;
    int retval = 0;

    if (ReadCommandsDetected)
    {
        for (ValidReadCommand = 0 ; ValidReadCommand < NB_READ_COMMANDS ; ValidReadCommand++)
        {
            if (Commands[ValidReadCommand].Supported)
            {
                DEBUG("\ninfo: using command %02Xh",Commands[ValidReadCommand].FuncByte);
                retval = TestPlextorFUACommandWorks(DriveLetter, ValidReadCommand, TargetSector, NbTests);
                break;
            }
        }
    }
    else
    {
        printf(READCOMMANDSNOTTESTED);
        exit(-1);
    }
    return retval;
}

//
// TimeMultipleReads
//
static void TimeMultipleReads(char DriveLetter, unsigned char ReadCommand, long int TargetSector, int NbReads, bool FUAbit)
{
    int i = 0;

    AverageDelay = 0;

    for (i=0; i<NbReads; i++)
    {
        Commands[ReadCommand].pFunc(DriveLetter, TargetSector, NbBurstReadSectors, FUAbit);
        Delay = ((double)PerfCountEnd.QuadPart - (double)PerfCountStart.QuadPart) / (double)freq.QuadPart;
        AverageDelay = (((AverageDelay * i) + Delay) / (i + 1));
    }
}

//
// TestCacheSpeedImpact
//
// compare reading times with FUA bit (to disc) and without FUA (from cache)
static void TestCacheSpeedImpact(char DriveLetter, long int TargetSector, int NbReads)
{
    int i;

    for (i=0; i<NB_READ_COMMANDS; i++)
    {
        if ((Commands[i].Supported) && (Commands[i].FUAbitSupported))
        {
            Commands[i].pFunc(DriveLetter, TargetSector, NbBurstReadSectors, false);             // initial load

            TimeMultipleReads(DriveLetter, i, TargetSector, NbReads, false);
            printf(AVERAGE_NORMAL, (Commands[i].FuncByte)&0xFF, AverageDelay);

            TimeMultipleReads(DriveLetter, i, TargetSector, NbReads, true);     // with FUA
            printf(AVERAGE_FUA, AverageDelay);

            i = NB_READ_COMMANDS;
        }
    }
}

//
// TestRCDBitWorks
//
// test if cache can be disabled via RCD bit
static int TestRCDBitWorks(char DriveLetter, int ReadCommand, long int TargetSector, int NbTests)
{
    int i;
    int InvalidationSuccess = 0;

    DEBUG("\ninfo: %d test(s), c/nc ratio: %d, burst: %d, max: %d",
           NbTests, CachedNonCachedSpeedFactor, NbBurstReadSectors, MaxCacheSectors);
    for (i=0; i<NbTests; i++)
    {
        // enable caching
        if (!SetCacheRCDBit(DriveLetter,RCD_READ_CACHE_ENABLED))
        {
            i = NbTests;
            break;
        }

        // first test : normal cache test
        ClearCache(DriveLetter);
        Commands[ReadCommand].pFunc(DriveLetter, TargetSector, NbBurstReadSectors, false); // init read
        InitDelay = ((double)PerfCountEnd.QuadPart - (double)PerfCountStart.QuadPart) / (double)freq.QuadPart;
        Commands[ReadCommand].pFunc(DriveLetter, TargetSector + NbBurstReadSectors, NbBurstReadSectors, false);
        Delay = ((double)PerfCountEnd.QuadPart - (double)PerfCountStart.QuadPart) / (double)freq.QuadPart;
        DEBUG("\n1) %d : %.2f ms / %d : %.2f ms", TargetSector, InitDelay, TargetSector + NbBurstReadSectors, Delay);

        // disable caching
        if (!SetCacheRCDBit(DriveLetter,RCD_READ_CACHE_DISABLED))
        {
            i = NbTests;
            break;
        }

        // second test : with cache disabled
        ClearCache(DriveLetter);
        Commands[ReadCommand].pFunc(DriveLetter, TargetSector, NbBurstReadSectors, false); // init read
        InitDelay = ((double)PerfCountEnd.QuadPart - (double)PerfCountStart.QuadPart) / (double)freq.QuadPart;
        Commands[ReadCommand].pFunc(DriveLetter, TargetSector + NbBurstReadSectors, NbBurstReadSectors, false);
        Delay2 = ((double)PerfCountEnd.QuadPart - (double)PerfCountStart.QuadPart) / (double)freq.QuadPart;
        DEBUG("\n2) %d : %.2f ms / %d : %.2f ms", TargetSector, InitDelay, TargetSector + NbBurstReadSectors, Delay2);

        // compare times
        if (Delay2 > (CachedNonCachedSpeedFactor * Delay))
        {
            InvalidationSuccess++;
        }
    }
    DEBUG("\nresult: %d/%d\n", InvalidationSuccess, NbTests);
    return(InvalidationSuccess);
}

// wrapper for TestRCDBit
static int TestRCDBitWorksWrapper(char DriveLetter, long int TargetSector, int NbTests)
{
    int ValidReadCommand;
    int retval = -1;

    if (ReadCommandsDetected)
    {
        for (ValidReadCommand=0 ; ValidReadCommand<NB_READ_COMMANDS ; ValidReadCommand++)
        {
            if (Commands[ValidReadCommand].Supported)
            {
                DEBUG("\ninfo: using command %02Xh",Commands[ValidReadCommand].FuncByte);
                retval = TestRCDBitWorks(DriveLetter, ValidReadCommand, TargetSector, NbTests);
                break;
            }
        }
    }
    else
    {
        printf(READCOMMANDSNOTTESTED);
        exit(-1);
    }
    return retval;
}

//--------------------------------------------------------------------------------------------------------
// TestCacheLineSize_Straight  (METHOD 1 : STRAIGHT)
//
// The initial read should fill in the cache. Thus, following ones should be read much
// faster until the end of the cache. Therefore, a sudden increase of durations of the
// read accesses should indicate the size of the cache line. We have to be careful though
// that the cache cannot be refilled while we try to find the limits of the cache, otherwise
// we will get a multiple of the cache line size and not the cache line size itself.
//
//--------------------------------------------------------------------------------------------------------
static int TestCacheLineSize_Straight(char DriveLetter, unsigned char ReadCommand, long int TargetSector, int NbMeasures)
{
    int i, TargetSectorOffset, CacheLineSize;
    int MaxCacheLineSize= 0;
    double PreviousDelay, InitialDelay;

    DEBUG("\ninfo: %d test(s), c/nc ratio: %d, burst: %d, max: %d",
           NbMeasures, CachedNonCachedSpeedFactor, NbBurstReadSectors, MaxCacheSectors);
    for (i=0; i<NbMeasures; i++)
    {
        ClearCache(DriveLetter);
        PreviousDelay = 50;

        // initial read. After this the drive's cache should be filled
        // with a number of sectors following this one.
        Commands[ReadCommand].pFunc(DriveLetter, TargetSector, NbBurstReadSectors, false);
        InitialDelay = ((double)PerfCountEnd.QuadPart - (double)PerfCountStart.QuadPart) / (double)freq.QuadPart;
        SUPERDEBUG("\n init %d: %f",TargetSector, InitialDelay);

        // read 1 sector at a time and time the reads until one takes more
        // than [CachedNonCachedSpeedFactor] times the delay taken by the previous read
        for (TargetSectorOffset = 0; TargetSectorOffset < MaxCacheSectors ; TargetSectorOffset += NbBurstReadSectors)
        {
            Commands[ReadCommand].pFunc(DriveLetter, TargetSector + TargetSectorOffset, NbBurstReadSectors, false);
            Delay = ((double)PerfCountEnd.QuadPart - (double)PerfCountStart.QuadPart) / (double)freq.QuadPart;
            SUPERDEBUG("\n %d: %f",TargetSector + TargetSectorOffset,Delay);

            if (Delay >= (CachedNonCachedSpeedFactor * PreviousDelay))
            {
                break;
            }
            else
            {
                PreviousDelay = Delay;
            }
        }

        if (TargetSectorOffset < MaxCacheSectors)
        {
            CacheLineSize = TargetSectorOffset;
            printf(CACHELINESIZE2,(int)((CacheLineSize*2352)/1024),CacheLineSize);
            DEBUG(" (%.2f .. %.2f -> %.2f)",InitialDelay,PreviousDelay,Delay);

            if ((i > NB_IGNORE_MEASURES) && (CacheLineSize > MaxCacheLineSize))
            {
                MaxCacheLineSize = CacheLineSize;
            }
        }
        else
        {
            printf("\n test aborted.");
        }

    }
    return MaxCacheLineSize;
}

//--------------------------------------------------------------------------------------------------------
// TestCacheLineSize_Wrap  (METHOD 2 : WRAPAROUND)
//
// The initial read should fill in the cache. Thus, following ones should be read much
// faster until the end of the cache. However, there's the risk that at each read new
// following sectors are cached in, thus showing an infinitely large cache with method 1.
// In this case, we detect the cache size by reading again the initial sector : when new
// sectors are read and cached in, the initial sector must be cached-out, thus reading
// it will be longer. Should work fine on Plextor drives.
//
// This method allows to avoid the "infinite cache" problem due to background reloading
// even in case of cache hits. However, cache reloading could be triggered when a given
// threshold is reached. So we might be measuring the threshold value and not really
// the cache size.
//
//--------------------------------------------------------------------------------------------------------
static int TestCacheLineSize_Wrap(char DriveLetter, unsigned char ReadCommand, long int TargetSector, int NbMeasures)
{
    int i, TargetSectorOffset, CacheLineSize;
    int MaxCacheLineSize= 0;
    double InitialDelay, PreviousInitDelay;

    DEBUG("\ninfo: %d test(s), c/nc ratio: %d, burst: %d, max: %d",
           NbMeasures, CachedNonCachedSpeedFactor, NbBurstReadSectors, MaxCacheSectors);
    for (i=0; i<NbMeasures; i++)
    {
        ClearCache(DriveLetter);

        // initial read. After this the drive's cache should be filled
        // with a number of sectors following this one.
        Commands[ReadCommand].pFunc(DriveLetter, TargetSector, NbBurstReadSectors, false);
        InitialDelay = ((double)PerfCountEnd.QuadPart - (double)PerfCountStart.QuadPart) / (double)freq.QuadPart;
        SUPERDEBUG("\n init %d: %f",TargetSector, InitialDelay);
        Commands[ReadCommand].pFunc(DriveLetter, TargetSector, NbBurstReadSectors, false);
        PreviousInitDelay = ((double)PerfCountEnd.QuadPart - (double)PerfCountStart.QuadPart) / (double)freq.QuadPart;
        SUPERDEBUG("\n %d: %f",TargetSector, PreviousInitDelay);

        // read 1 sector forward and the initial sector. If the original sector takes more
        // than [CachedNonCachedSpeedFactor] times the delay taken by the previous read of,
        // the initial sector, then we reached the limits of the cache
        for (TargetSectorOffset = 1; TargetSectorOffset < MaxCacheSectors ; TargetSectorOffset += NbBurstReadSectors)
        {
            Commands[ReadCommand].pFunc(DriveLetter, TargetSector + TargetSectorOffset, NbBurstReadSectors, false);
            Delay = ((double)PerfCountEnd.QuadPart - (double)PerfCountStart.QuadPart) / (double)freq.QuadPart;
            SUPERDEBUG("\n %d: %f",TargetSector + TargetSectorOffset,Delay);

            Commands[ReadCommand].pFunc(DriveLetter, TargetSector, NbBurstReadSectors, false);
            Delay2 = ((double)PerfCountEnd.QuadPart - (double)PerfCountStart.QuadPart) / (double)freq.QuadPart;
            SUPERDEBUG("\n %d: %f",TargetSector,Delay2);

            if (Delay2 >= (CachedNonCachedSpeedFactor * PreviousInitDelay))
            {
                break;
            }

            PreviousInitDelay = Delay2;
        }

        // did we find a timing drop within the expected limits ?
        if (TargetSectorOffset < MaxCacheSectors)
        {
            // sometimes the first sector can be read so much faster than the next one that
            // is incredibly fast, avoid this by increasing the ratio
            if (TargetSectorOffset <= 1)
            {
                CachedNonCachedSpeedFactor++;
                DEBUG("\ninfo: increasing c/nc ratio to %d", CachedNonCachedSpeedFactor);
                i--;
            }
            else
            {
                CacheLineSize = TargetSectorOffset;
                printf(CACHELINESIZE2,(int)((CacheLineSize*2352)/1024),CacheLineSize);
                DEBUG(" (%.2f .. %.2f -> %.2f)",InitialDelay,PreviousInitDelay,Delay2);

                if ((i > NB_IGNORE_MEASURES) && (CacheLineSize > MaxCacheLineSize))
                {
                    MaxCacheLineSize = CacheLineSize;
                }
            }
        }
        else
        {
            printf("\n no cache detected");
        }
    }
    return MaxCacheLineSize;
}

//--------------------------------------------------------------------------------------------------------
// TestCacheLineSize_Stat
//
// finds cache line size with a single long burst read of NbMeasures * BurstSize sectors,
// then try to find the cache size with statistical calculations
//--------------------------------------------------------------------------------------------------------
static int TestCacheLineSize_Stat(char DriveLetter, unsigned char ReadCommand, long int TargetSector, int NbMeasures, int BurstSize)
{
    int i,j;
    int NbPeakMeasures;
    double Maxdelay = 0.0;
    double Threshold = 0.0;
    std::vector<double> Measures;
    Measures.reserve(NbMeasures);
    int CurrentDelta = 0;
    int MostFrequentDeltaIndex = 0;
    int MaxDeltaFrequency = 0;

    // init
    for (i = 0; i < NBPEAKMEASURES; i++)
    {
        PeakMeasuresIndexes[i] = 0;
        if (i < NBDELTA)
        {
            DeltaArray[i].delta     = 0;
            DeltaArray[i].frequency = 0;
            DeltaArray[i].divider   = 0;
        }
    }

    // initial read.
    ClearCache(DriveLetter);
    Commands[ReadCommand].pFunc(DriveLetter, TargetSector, BurstSize, false);
    Measures.push_back(((double)PerfCountEnd.QuadPart - (double)PerfCountStart.QuadPart) / (double)freq.QuadPart);

    // fill in measures buffer
    for (i=1; i<NbMeasures; i++)
    {
        Commands[ReadCommand].pFunc(DriveLetter, TargetSector + i*BurstSize, BurstSize, false);
        Measures.push_back(((double)PerfCountEnd.QuadPart - (double)PerfCountStart.QuadPart) / (double)freq.QuadPart);
    }

    // find max time
    Maxdelay = *(std::max_element(std::begin(Measures), std::end(Measures)));
    DEBUG("\ninitial: %.2f ms, max: %.2f ms",Measures[0], Maxdelay);

    // find all values above 90% of max
    Threshold = Maxdelay * ThresholdRatioMethod2;
    for (i=1, NbPeakMeasures=0; (i < NbMeasures) && (NbPeakMeasures < NBPEAKMEASURES); i++)
    {
        if ( Measures[i] > Threshold) PeakMeasuresIndexes[NbPeakMeasures++] = i;
    }
    DEBUG("\nmeas: %d/%d above %.2f ms (%.2f)",NbPeakMeasures, NbMeasures, Threshold, ThresholdRatioMethod2);

    // calculate stats on differences and keep max
    for (i=1; i<NbPeakMeasures; i++)
    {
        CurrentDelta = PeakMeasuresIndexes[i] - PeakMeasuresIndexes[i-1];
        SUPERDEBUG("\ndelta = %d", CurrentDelta);

        for (j=0; j<NbPeakMeasures; j++)
        {
            // current delta already seen before
            if (DeltaArray[j].delta == CurrentDelta)
            {
                DeltaArray[j].frequency++;
                if (DeltaArray[j].frequency > MaxDeltaFrequency)
                {
                    MaxDeltaFrequency = DeltaArray[j].frequency;
                    MostFrequentDeltaIndex = j;
                }
                break;
            }

            // new delta, count it in
            if (DeltaArray[j].delta == 0)
            {
                DeltaArray[j].delta = CurrentDelta;
                DeltaArray[j].frequency = 1;
                if (DeltaArray[j].frequency > MaxDeltaFrequency)
                {
                    MaxDeltaFrequency = 1;
                    MostFrequentDeltaIndex = j;
                }
                break;
            }
        }
    }

    // find which sizes are multiples of others
    for (i=0 ; DeltaArray[i].delta != 0 ; i++)
    {
        for (j=0 ; DeltaArray[j].delta != 0 ; j++)
        {
            if ((DeltaArray[j].delta % DeltaArray[i].delta == 0) && (i!=j))
            {
                DeltaArray[i].divider++;
            }
        }
    }

    printf("\nsizes: ");
    for (i=0 ; DeltaArray[i].delta != 0 ; i++)
    {
        if (i%5==0) printf("\n");
        printf(" %d (%d%%, div=%d)",DeltaArray[i].delta,
               (int)(100 * DeltaArray[i].frequency / NbPeakMeasures),
               DeltaArray[i].divider);
    }

    printf("\nfmax = %d (%d%%) : %d kB, %d sectors",
           DeltaArray[MostFrequentDeltaIndex].frequency,
           (int)(100 * DeltaArray[MostFrequentDeltaIndex].frequency / NbPeakMeasures),
           (int)((DeltaArray[MostFrequentDeltaIndex].delta*2352)/1024),
           DeltaArray[MostFrequentDeltaIndex].delta);
    return(MostFrequentDeltaIndex);
}

// wrapper for TestCacheLineSize
static int TestCacheLineSizeWrapper(char DriveLetter, long int TargetSector, int NbMeasures, int BurstSize, short method)
{
    int ValidReadCommand;
    int retval = -1;

    if (ReadCommandsDetected)
    {
        for (ValidReadCommand=0 ; ValidReadCommand<NB_READ_COMMANDS ; ValidReadCommand++)
        {
            if (Commands[ValidReadCommand].Supported)
            {
                DEBUG("\ninfo: using command %02Xh",Commands[ValidReadCommand].FuncByte);

                switch(method)
                {
                case 1:
                    retval = TestCacheLineSize_Wrap(DriveLetter, ValidReadCommand, TargetSector, NbMeasures);
                    break;
                case 2:
                    retval = TestCacheLineSize_Straight(DriveLetter, ValidReadCommand, TargetSector, NbMeasures);
                    break;
                case 3:
                    retval = TestCacheLineSize_Stat(DriveLetter, ValidReadCommand, TargetSector, NbMeasures, BurstSize);
                    break;
                default:
                    printf("\nError: invalid method !!\n");
                }
                break;
            }
        }
    }
    else
    {
        printf(READCOMMANDSNOTTESTED);
        exit(-1);
    }
    return retval;
}

//--------------------------------------------------------------------------------------------------------
// TestCacheLineNumber
//
// finds number of cache lines by reading 1 sector at N (loading the cache), then another sector at M>>N,
// then at N+1 and N+2. If There are multiple cache lines, the read at N+1 should be done from the already
// loaded cache, so it will be very fast and the same time as the read at N+2. Otherwise, the read at N+1
// will reload the cache and it will be much slower than the one at N+2. To find out the number of cache
// lines, we read multiple M sectors at various positions
//--------------------------------------------------------------------------------------------------------
static int TestCacheLineNumber(char DriveLetter, unsigned char ReadCommand, long int TargetSector, int NbMeasures)
{
    int i, j;
    int NbCacheLines = 1;
    double PreviousDelay;
    long int LocalTargetSector = TargetSector;

    DEBUG("\ninfo: using c/nc ratio : %d", CachedNonCachedSpeedFactor);
    if (!DebugMode)
    {
        printf("\n");
    }

    for (i=0; i<NbMeasures; i++)
    {
        ClearCache(DriveLetter);
        NbCacheLines = 1;

        // initial read. After this the drive's cache should be filled
        // with a number of sectors following this one.
        Commands[ReadCommand].pFunc(DriveLetter, LocalTargetSector, 1, false);
        PreviousDelay = ((double)PerfCountEnd.QuadPart - (double)PerfCountStart.QuadPart) / (double)freq.QuadPart;
        SUPERDEBUG("\n first read at %d: %.2f",LocalTargetSector ,PreviousDelay);

        for (j=1; j<MAX_CACHE_LINES; j++)
        {
            // second read to load another (?) cache line
            Commands[ReadCommand].pFunc(DriveLetter, LocalTargetSector + 10000, 1, false);

            // read 1 sector next to the original one
            Commands[ReadCommand].pFunc(DriveLetter, LocalTargetSector + 2*j, 1, false);
            Delay = ((double)PerfCountEnd.QuadPart - (double)PerfCountStart.QuadPart) / (double)freq.QuadPart;
            SUPERDEBUG("\n read at %d: %.2f",LocalTargetSector + 2*j, Delay);

            if (DebugMode || SuperDebugMode)
            {
                printf("\n%.2f / %.2f -> ",PreviousDelay, Delay);
            }
            if (Delay <= (PreviousDelay / CachedNonCachedSpeedFactor))
            {
                NbCacheLines++;
            }
            else
            {
                break;
            }
        }
        printf(" %d", NbCacheLines);
        LocalTargetSector += 2000;
    }
    return NbCacheLines;
}

// wrapper for TestCacheLineNumber
static int TestCacheLineNumberWrapper(char DriveLetter, long int TargetSector, int NbMeasures)
{
    int ValidReadCommand;
    int retval = -1;

    if (ReadCommandsDetected)
    {
        for (ValidReadCommand=0 ; ValidReadCommand<NB_READ_COMMANDS ; ValidReadCommand++)
        {
            if (Commands[ValidReadCommand].Supported)
            {
                DEBUG("\ninfo: using command %02Xh",Commands[ValidReadCommand].FuncByte);
                retval = TestCacheLineNumber(DriveLetter, ValidReadCommand, TargetSector, NbMeasures);
                break;
            }
        }
    }
    else
    {
        printf(READCOMMANDSNOTTESTED);
        exit(-1);
    }
    return retval;
}

//--------------------------------------------------------------------------------------------------------
// TestPlextorFUAInvalidationSize
//
// find size of cache invalidated by Plextor FUA command
//--------------------------------------------------------------------------------------------------------
static int TestPlextorFUAInvalidationSize(char DriveLetter, unsigned char ReadCommand, long int TargetSector, int NbMeasures)
{
#define CACHE_TEST_BLOCK  20

    int i, TargetSectorOffset;
    int InvalidatedSize = 0;
    double InitialDelay;

    DEBUG("\ninfo: using c/nc ratio : %d", CachedNonCachedSpeedFactor);

    for (i=0; i<NbMeasures; i++)
    {
        for (TargetSectorOffset=2000; TargetSectorOffset>=0; TargetSectorOffset -= CACHE_TEST_BLOCK)
        {
            ClearCache(DriveLetter);

            // initial read of 1 sector. After this the drive's cache should be filled
            // with a number of sectors following this one.
            Commands[ReadCommand].pFunc(DriveLetter, TargetSector, 1, false);
            InitialDelay = ((double)PerfCountEnd.QuadPart - (double)PerfCountStart.QuadPart) / (double)freq.QuadPart;
            SUPERDEBUG("\n(%d) init = %.2f, thr = %.2f",i, InitialDelay, (double)(InitialDelay / CachedNonCachedSpeedFactor));

            // invalidate cache with Plextor FUA command
            PlextorFUAFlush(DriveLetter, TargetSector);

            // now we should get this :
            //
            //  cache :             |-- invalidated --|--- still cached ---|
            //  reading speeds :    |- slow (flushed)-|--- fast (cached) --|-- slow (not read yet) --|
            //                                        ^
            //                                        |
            // read sectors backwards to find this ---|  spot
            Commands[ReadCommand].pFunc(DriveLetter, TargetSector + TargetSectorOffset, 1, false);
            Delay = ((double)PerfCountEnd.QuadPart - (double)PerfCountStart.QuadPart) / (double)freq.QuadPart;
            SUPERDEBUG(" (%d) %d: %.2f",i, TargetSector + TargetSectorOffset,Delay);

            if (Delay <= (InitialDelay / CachedNonCachedSpeedFactor))
            {
                InvalidatedSize = TargetSectorOffset;
                break;
            }
        }
    }
    return (InvalidatedSize - CACHE_TEST_BLOCK);
}

// wrapper for TestPlextorFUAInvalidationSize
static int TestPlextorFUAInvalidationSizeWrapper(char DriveLetter, long int TargetSector, int NbMeasures)
{
    int ValidReadCommand;
    int retval = -1;

    if (ReadCommandsDetected)
    {
        for (ValidReadCommand=0 ; ValidReadCommand<NB_READ_COMMANDS ; ValidReadCommand++)
        {
            if (Commands[ValidReadCommand].Supported)
            {
                DEBUG("\ninfo: using command %02Xh",Commands[ValidReadCommand].FuncByte);
                retval = TestPlextorFUAInvalidationSize(DriveLetter, ValidReadCommand, TargetSector, NbMeasures);
                break;
            }
        }
    }
    else
    {
        printf(READCOMMANDSNOTTESTED);
        exit(-1);
    }
    return retval;
}

static bool TestRCDBitSupport(char DriveLetter, long int TargetSector)
{
    bool retval = false;

    if (ModeSense(DriveLetter, CACHING_MODE_PAGE, 0, 18))
    {
        retval = true;
    }
    else
    {
        printf("not supported");
    }
    return(retval);
}

//--------------------------------------------------------------------------------------------------------
// TestCacheLineSizePrefetch
//
// This method is using only the Prefetch command, which is described as follows in SBC3 :
// - if the specified logical blocks were successfully transferred to the cache, the device server
//   shall return CONDITION MET
// - if the cache does not have sufficient capacity to accept all of the specified logical blocks,
//   the device server shall transfer to the cache as many of the specified logical blocks that fit.
//   If these logical blocks are transferred successfully it shall return GOOD status
//
//--------------------------------------------------------------------------------------------------------
static int TestCacheLineSizePrefetch(char DriveLetter, long int TargetSector)
{
    int NbSectors = 1;

    Prefetch(DriveLetter, TargetSector, NbSectors);
    while (sptd.ScsiStatus == SCSISTAT_CONDITION_MET)
    {
        Prefetch(DriveLetter, TargetSector, NbSectors++);
    }
    if ((sptd.ScsiStatus == SCSISTAT_GOOD) && (NbSectors > 1))
    {
        printf("\n-> cache size = %d kB, %d sectors",(int)((NbSectors - 1)*2352/1024), NbSectors - 1);
    }
    else
    {
        printf("\nError: this method does not seem to work on this drive");
    }
    return(NbSectors - 1);
}

/*

  modifiers      l   b   x   r   s   m   n
commands
   c1        v           v   v   .   v
   c2        v           v   v   v   .
   c3        .           v   v   .   .
   p         v           v   v   .   v
   k         .   .   v   v   v   .   v
   i         .   .   .   .   .   .   .
   w         v   .   v   .   v   .   v

*/

static void PrintUsage()
{
    printf("\nUsage:   cachex <commands> <options> <drive letter>\n");
    printf("\nCommands:  -i     : show drive info\n");
    printf("           -c     : test drive cache\n");
//    printf("           -c2    : test drive cache (method 2)\n");
//    printf("           -c3    : test drive cache (method 3)\n");
    printf("           -p     : test plextor FUA command\n");
    printf("           -k     : test cache disabling\n");
    printf("           -w     : test cache line numbers\n");
    printf("\nOptions:   -d     : show debug info\n");
    printf("           -l xx  : spin drive for xx seconds before starting to measure\n");
//    printf("           -b xx  : use burst reads of size xx\n");
//    printf("           -t xx  : threshold at xx%% for cache tests\n");
    printf("           -x xx  : use cached/non cached ratio xx\n");
    printf("           -r xx  : use read command xx (one of 0xbe, 0x28, 0xd5, 0xd4, 0xd8)\n");
    printf("           -s xx  : set read speed to xx (0=max)\n");
    printf("           -m xx  : look for cache size up to xx sectors\n");
//    printf("           -y xx  : use xx sectors for cache test method 2\n");
    printf("           -n xx  : perform xx tests\n");
}

//--------------------------------------------------------------------------------------------------------
//------------------------------------------ MAIN MAIN MAIN MAIN -----------------------------------------
//--------------------------------------------------------------------------------------------------------
int main(int argc, char **argv)
{
    char DriveLetter = 'a';
    int MaxIndex, i, j, v;
    int MaxReadSpeed;
    bool SpinDriveFlag = false;
    bool ShowDriveInfos = false;
    bool TestPlextorFUA = false;
    bool SetMaxDriveSpeed = false;
    bool TestDriveCache = false;
    bool CacheMethod1 = false;
    bool CacheMethod2 = false;
    bool CacheMethod3 = false;
    bool CacheMethod4 = false;
    bool CacheNbTest = false;
    bool PFUAInvalidationSizeTest = false;
    bool TestRCDBit = false;
    int NbSecsDriveSpin = 10;
    int NbSectorsMethod2 = 1000;
    int InvalidatedSectors = 0;
    int Nbtests = 0;

    // --------------- setup ---------------------------
    printf("\nCacheExplorer 0.9 - spath@cdfreaks.com\n");
    QueryPerformanceFrequency(&freq);
    freq.QuadPart /= 1000;

    // ------------ command line parsing --------------
    if (argc < 2)
    {
        PrintUsage();
        return(-1);
    }
    for(i=1; i<argc; i++)
    {
        if(argv[i][0] == '-')
        {
            switch(argv[i][1])
            {
            case 'c' :
                if (argv[i][2] == '2')
                {
                    CacheMethod2 = true;
                }
                else if (argv[i][2] == '3')
                {
                    CacheMethod3 = true;
                }
                else if (argv[i][2] == '4')
                {
                    CacheMethod4 = true;
                }
                else
                {
                    CacheMethod1 = true;    // default method
                }
                break;
            case 'p' :
                TestPlextorFUA = true;
                break;
            case 'k' :
                TestRCDBit = true;
                break;
            case 'i' :
                ShowDriveInfos = true;
                break;
            case 'd' :
                DebugMode = true;
                break;
            case 'l' :
                NbSecsDriveSpin = atoi(argv[++i]);
                SpinDriveFlag = true;
                break;
            case 'b' :
                NbBurstReadSectors = atoi(argv[++i]);
                break;
            case 'r' :
                i++;
                sscanf(argv[i],"0x%x",&UserReadCommand);
                break;
            case 's' :
                SetMaxDriveSpeed = true;
                MaxReadSpeed = atoi(argv[++i]);
                break;
            case 'w' :
                CacheNbTest = true;
                break;
            case 'm' :
                MaxCacheSectors = atoi(argv[++i]);
                break;
            case 'y':
                NbSectorsMethod2 = atoi(argv[++i]);
                break;
            case 't' :
                v = atoi(argv[++i]);
                ThresholdRatioMethod2 = (double)(v/100.0);
                break;
            case 'x' :
                CachedNonCachedSpeedFactor = atoi(argv[++i]);
                break;
            case 'n' :
                Nbtests = atoi(argv[++i]);
                break;

                // non documented options
            case '/' :
                PFUAInvalidationSizeTest = true;
                break;
            case '.':
                SuperDebugMode = true;
                break;
            default :
                PrintUsage();
                return(-1);
            }
        }
        else  // must be drive letter then
        {
            if (((argv[i][0]>0x41) && (argv[i][0]<0x5A)) || ((argv[i][0]>0x61) && (argv[i][0]<0x7A)))
            {
                DriveLetter = argv[i][0];
            }
            else
            {
                printf("\nError: invalid drive letter");
                exit(-1);
            }
        }
    }

    if (DriveLetter == 'a')
    {
        printf("\nError: no drive selected\n");
        PrintUsage();
        exit(-1);
    }

    // ------------ actual stuff --------------
    std::vector<std::uint8_t> data_buf(NbBurstReadSectors * 2448);
    DataBuf = data_buf.data();

    //
    // print drive info
    //
    hVolume = OpenVolume(DriveLetter);
    if (hVolume == INVALID_HANDLE_VALUE)
    {
        return(-1);
    }
    printf("\nDrive on %c is ",(DriveLetter>0x60)?DriveLetter-0x20:DriveLetter);
    if (!PrintDriveInfo(DriveLetter))
    {
        printf("\nError: cannot read drive info");
        return(-1);
    }
    printf("\n");

    //-------------------------------------------------------------------

    //
    // 0) Test all supported read commands / test user selected command
    //
    if (ShowDriveInfos)
    {
        ShowCacheValues(DriveLetter);
        TestSupportedReadCommands(DriveLetter);
    }

    if (UserReadCommand != 0)
    {
        ReadCommandsDetected = false;
        for (j=0; j<NB_READ_COMMANDS; j++)
        {
            Commands[j].Supported = false; // override commands detection by user selection
        }

        for (j=0; j<NB_READ_COMMANDS; j++)
        {
            if (UserReadCommand == Commands[j].FuncByte)
            {
                if (Commands[j].pFunc(DriveLetter, 10000, 1, false))
                {
                    Commands[j].Supported = true;
                    ReadCommandsDetected = true;
                    break;
                }
                else
                {
                    printf("\nError: command %02Xh not supported\n",UserReadCommand&0xFF);
                    exit(-1);
                }
            }
        }
        if (j == NB_READ_COMMANDS)
        {
            printf("\nError: command %02Xh is not recognized\n",UserReadCommand&0xFF);
            exit(-1);
        }
    }


    //
    // 1) Set drive speed
    //
    if (SetMaxDriveSpeed)
    {
        if (DebugMode)
        {
            printf("\n[+] Changing read speed to ");
            if (MaxReadSpeed == 0)
            {
                printf("max\n");
            }
            else
            {
                printf("%dx\n",MaxReadSpeed);
            }
            SetDriveSpeed(DriveLetter, MaxReadSpeed,0);
        }
    }

    //
    // 2) Test support for Plextor's FUA cache clearing command
    //
    if (TestPlextorFUA)
    {
        Nbtests = (Nbtests == 0) ? 5 : Nbtests;
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        if (SpinDriveFlag)
        {
            SpinDrive(DriveLetter, NbSecsDriveSpin);
        }

        if (TestPlextorFUACommand(DriveLetter, 1))
        {
            printf("\n[+] Plextor flush tests: ");
            printf("%d/%d",TestPlextorFUACommandWorksWrapper(DriveLetter, 15000, Nbtests),Nbtests);

            if (PFUAInvalidationSizeTest)
            {
                // 4) Find the size of data invalidated  by Plextor FUA command
                printf(TESTINGPLEXFUA2);
                InvalidatedSectors = TestPlextorFUAInvalidationSizeWrapper(DriveLetter, 15000, 1);
                DEBUG("\nresult: ");
                if (InvalidatedSectors > 0)
                {
                    printf("ok (%d)", InvalidatedSectors);
                }
                else
                {
                    printf("not working (%d)", InvalidatedSectors);
                }
            }
        }
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
    }

    //
    // 3) Explore cache structure
    //
    if (CacheMethod1)
    {
        Nbtests = (Nbtests == 0) ? 10 : Nbtests;
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        if (SpinDriveFlag)
        {
            SpinDrive(DriveLetter, NbSecsDriveSpin);
        }

        // SIZE : method 1
        printf(CACHELINESIZETEST2);
        CacheLineSizeSectors = TestCacheLineSizeWrapper(DriveLetter, 15000, Nbtests, 0, 1);

        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
    }

    if (CacheMethod2)
    {
        Nbtests = (Nbtests == 0) ? 20 : Nbtests;
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        if (SpinDriveFlag)
        {
            SpinDrive(DriveLetter, NbSecsDriveSpin);
        }

        // SIZE : method 2
        printf(CACHELINESIZETEST,2);
        CacheLineSizeSectors = TestCacheLineSizeWrapper(DriveLetter, 15000, Nbtests, 0, 2);

        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
    }

    if (CacheNbTest)
    {
        Nbtests = (Nbtests == 0) ? 5 : Nbtests;
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        if (SpinDriveFlag)
        {
            SpinDrive(DriveLetter, NbSecsDriveSpin);
        }

        // NUMBER
        printf(CACHELINENBTEST);
        CacheLineNumbers = TestCacheLineNumberWrapper(DriveLetter, 15000, Nbtests);
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
    }

    if (CacheMethod3)
    {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        if (SpinDriveFlag)
        {
            SpinDrive(DriveLetter, NbSecsDriveSpin);
        }

        // SIZE : method 3 (STATS)
        printf(CACHELINESIZETEST,3);
        MaxIndex = TestCacheLineSizeWrapper(DriveLetter, 15000, NbSectorsMethod2, NbBurstReadSectors, 3);
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
    }

    if (CacheMethod4)
    {
        // SIZE : method 4 (PREFETCH)
        printf(CACHELINESIZETEST,4);
        TestCacheLineSizePrefetch(DriveLetter, 10000);
    }

    if (TestRCDBit)
    {
        printf("\n[+] Testing cache disabling: ");

        Nbtests = (Nbtests == 0) ? 3 : Nbtests;
        if (TestRCDBitSupport(DriveLetter, 10000))
        {
            if (TestRCDBitWorksWrapper(DriveLetter, 15000, Nbtests) > 0)
            {
                printf("ok");
            }
            else
            {
                printf("not supported");
            }
        }
    }

    printf("\n");
    CloseVolume(hVolume);
    return 0;
}
