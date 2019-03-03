//** Defines taken from ntddscsi.h in MS Windows DDK CD
#define SCSI_IOCTL_DATA_OUT             0 //Give data to SCSI device (e.g. for writing)
#define SCSI_IOCTL_DATA_IN              1 //Get data from SCSI device (e.g. for reading)
#define SCSI_IOCTL_DATA_UNSPECIFIED     2 //No data (e.g. for ejecting)

#define IOCTL_SCSI_PASS_THROUGH         0x4D004
typedef struct ScsiPassThrough {
    unsigned short  Length;
    unsigned char   ScsiStatus;
    unsigned char   PathId;
    unsigned char   TargetId;
    unsigned char   Lun;
    unsigned char   CdbLength;
    unsigned char   SenseInfoLength;
    unsigned char   DataIn;
    unsigned int    DataTransferLength;
    unsigned int    TimeOutValue;
    unsigned int    DataBufferOffset;
    unsigned int    SenseInfoOffset;
    unsigned char   Cdb[16];
} SCSI_PASS_THROUGH;

#define IOCTL_SCSI_PASS_THROUGH_DIRECT  0x4D014
typedef struct _SCSI_PASS_THROUGH_DIRECT {
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
//** End of defines taken from ntddscsi.h from MS Windows DDK CD


// SCSI Status Codes: (from SCSI.H)
#define SCSISTAT_GOOD                  0x00
#define SCSISTAT_CHECK_CONDITION       0x02
#define SCSISTAT_CONDITION_MET         0x04
#define SCSISTAT_BUSY                  0x08
#define SCSISTAT_INTERMEDIATE          0x10
#define SCSISTAT_INTERMEDIATE_COND_MET 0x14
#define SCSISTAT_RESERVATION_CONFLICT  0x18
#define SCSISTAT_COMMAND_TERMINATED    0x22
#define SCSISTAT_QUEUE_FULL            0x28
