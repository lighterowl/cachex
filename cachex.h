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
//------------------------------------- FUNCTION PROTOTYPES ----------------------------------------------
//--------------------------------------------------------------------------------------------------------

// Help functions
HANDLE OpenVolume(char DriveLetter);
BOOL CloseVolume(HANDLE hVolume);
void PrintIDString(unsigned char* dataChars, int dataLength);
void ShowCacheValues(char DriveLetter);
BOOL ClearCache();
BOOL SpinDrive(char DriveLetter, unsigned int Seconds);
void ClearCDB();
BOOL PrintDriveInfo(char DriveLetter);
BOOL SetCacheRCDBit(char DriveLetter, BOOL RCDBitValue);

// MMC Read commands
BOOL Read_BEh(char DriveLetter, long int TargetSector, int NbSectors, bool FUAbit);
BOOL Read_A8h(char DriveLetter, long int TargetSector, int NbSectors, bool FUAbit);
BOOL Read_28h(char DriveLetter, long int TargetSector, int NbSectors, bool FUAbit);
BOOL Read_D4h(char DriveLetter, long int TargetSector, int NbSectors, bool FUAbit);
BOOL Read_D5h(char DriveLetter, long int TargetSector, int NbSectors, bool FUAbit);
BOOL Read_D8h(char DriveLetter, long int TargetSector, int NbSectors, bool FUAbit);

// Other MMC commands
BOOL ModeSense(char DriveLetter, unsigned char PageCode, unsigned char SubPageCode, int size);
BOOL ModeSelect(char DriveLetter, unsigned char PageCode, unsigned char SubPageCode, int size);
BOOL RequestSense(char DriveLetter);
BOOL PlextorFUAFlush(char DriveLetter, long int TargetSector);
BOOL SetDriveSpeed(char DriveLetter, unsigned char ReadSpeedX, unsigned char WriteSpeedX);
BOOL Prefetch(char DriveLetter, long int TargetSector, unsigned int NbSectors);

// Tests
bool TestPlextorFUACommand(char DriveLetter, int NbIterations);
void TimeMultipleReads (char DriveLetter, unsigned char ReadCommand, long int TargetSector, int NbReads, bool FUAbit);
void TestCacheSpeedImpact(char DriveLetter, long int TargetSector, int NbReads);
int TestCacheLineSize_Straight(char DriveLetter, unsigned char ReadCommand, long int TargetSector, int NbMeasures);
int TestCacheLineSize_Wrap(char DriveLetter, unsigned char ReadCommand, long int TargetSector, int NbMeasures);
int TestCacheLineSize_Stat(char DriveLetter, unsigned char ReadCommand, long int TargetSector, int NbMeasures, int BurstSize);
int TestCacheLineSizeWrapper(char DriveLetter, long int TargetSector, int NbMeasures, int BurstSize, short method);
int TestCacheLineNumber(char DriveLetter, unsigned char ReadCommand, long int TargetSector, int NbMeasures);
int TestCacheLineNumberWrapper(char DriveLetter, long int TargetSector, int NbMeasures);
int TestPlextorFUAInvalidationSize(char DriveLetter, unsigned char ReadCommand, long int TargetSector, int NbMeasures);
int TestPlextorFUAInvalidationSizeWrapper(char DriveLetter, long int TargetSector, int NbMeasures);
int TestCacheLineSizePrefetch(char DriveLetter, long int TargetSector);
int TestPlextorFUACommandWorks(char DriveLetter, int ReadCommand, long int TargetSector, int NbTests);
int TestPlextorFUACommandWorksWrapper(char DriveLetter, long int TargetSector, int NbTests);
BOOL TestRCDBitSupport(char DriveLetter, long int TargetSector);
int TestRCDBitWorks(char DriveLetter, int ReadCommand, long int TargetSector, int NbTests);
int TestRCDBitWorksWrapper(char DriveLetter, long int TargetSector, int NbTests);

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
