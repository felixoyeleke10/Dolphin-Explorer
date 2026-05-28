// xtf_diag.cpp  –  dump XTF file header and first few ping records
// Build: g++ -std=c++17 -o xtf_diag xtf_diag.cpp
// Run:   xtf_diag <file.xtf>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>

#pragma pack(push, 1)
struct XtfFileHeader {
    uint8_t  FileFormat;
    uint8_t  SystemType;
    char     RecordingProgramName[8];
    char     RecordingProgramVersion[8];
    char     SonarName[16];
    uint16_t SonarType;
    char     NoteString[64];
    char     ThisFileName[64];
    uint16_t NavUnits;
    uint16_t NumberOfSonarChannels;
    uint16_t NumberOfBathymetryChannels;
    uint8_t  Reserved[86];
};

struct XtfChanInfo {
    uint8_t  TypeOfChannel;
    uint8_t  SubChannelNumber;
    uint16_t CorrectionFlags;
    uint16_t UniPolar;
    uint16_t BytesPerSample;
    uint32_t SamplesPerChannel;
    char     ChannelName[16];
    float    VoltScale, Frequency, HorizBeamAngle, TiltAngle, BeamWidth;
    float    OffsetX, OffsetY, OffsetZ;
    float    OffsetYaw, OffsetPitch, OffsetRoll;
    uint16_t BeamsPerArray;
    uint8_t  Reserved[54];
};

struct XtfPacketHeader {
    uint16_t MagicNumber;
    uint8_t  HeaderType;
    uint8_t  SubChannelNumber;
    uint16_t NumChansToFollow;
    uint16_t Reserved1[2];
    uint32_t NumBytesThisRecord;
    uint16_t Year;
    uint8_t  Month, Day, Hour, Minute, Second, HSeconds;
    uint16_t JulianDay;
    uint32_t EventNumber;
    uint32_t PingNumber;
    float    SoundVelocity, OceanTide;
    uint32_t Reserved2;
    float    ConductivityFreq, TemperatureFreq, PressureFreq, PressureTemp;
    float    Conductivity, WaterTemperature, Pressure, ComputedSoundVelocity;
    float    MagX, MagY, MagZ;
    float    AuxVal1, AuxVal2, AuxVal3, AuxVal4, AuxVal5, AuxVal6;
    float    SpeedLog, Turbidity, ShipSpeed, ShipGyro;
    double   ShipYcoordinate, ShipXcoordinate;
    uint16_t ShipAltitude, ShipDepth;
    uint8_t  FixTimeHour, FixTimeMinute, FixTimeSecond, FixTimeHSeconds;
    float    SensorSpeed, KP;
    double   SensorYcoordinate, SensorXcoordinate;
    uint16_t SonarStatus, RangeToFish, BearingToFish, CableOut;
    float    Layback, CableTension, SensorDepth, SensorPrimaryAltitude;
    float    SensorAuxAltitude, SensorPitch, SensorRoll, SensorHeading;
    float    Heave, Yaw;
    uint32_t AttitudeTimeTag;
    float    DOT;
    uint32_t NavFixMilliseconds;
    uint8_t  ComputerClockHour, ComputerClockMinute, ComputerClockSecond, ComputerClockHsec;
    int16_t  FishPositionDeltaX, FishPositionDeltaY;
    uint8_t  FishPositionErrorCode;
    uint32_t OptionalOffset;
    uint8_t  CableOutHundredths;
    uint8_t  ReservedSpace2[6];
};

struct XtfPingChanHeader {
    uint16_t ChannelNumber;
    uint16_t DownsampleMethod;
    float    SlantRange, GroundRange, TimeDelay, TimeDuration, SecondsPerPing;
    uint16_t ProcessingFlags, Frequency, InitialGainCode, GainCode, BandWidth;
    uint32_t ContactNumber;
    uint16_t ContactClassification;
    uint8_t  ContactSubNumber, ContactType;
    uint32_t NumSamples;
    uint16_t MillivoltScale;
    float    ContactTimeOffTrack;
    uint8_t  ContactCloseNumber, Reserved;
    float    FixedVSOP;
    int16_t  Weight;
    uint8_t  ReservedSpace[4];
};
#pragma pack(pop)

static_assert(sizeof(XtfFileHeader)    == 256, "XtfFileHeader wrong");
static_assert(sizeof(XtfChanInfo)      == 128, "XtfChanInfo wrong");
static_assert(sizeof(XtfPacketHeader)  == 256, "XtfPacketHeader wrong");
static_assert(sizeof(XtfPingChanHeader)== 64,  "XtfPingChanHeader wrong");

int main(int argc, char* argv[])
{
    if (argc < 2) { fprintf(stderr, "Usage: xtf_diag <file.xtf>\n"); return 1; }
    FILE* f = nullptr;
#ifdef _WIN32
    fopen_s(&f, argv[1], "rb");
#else
    f = fopen(argv[1], "rb");
#endif
    if (!f) { perror("open"); return 1; }

    // --- File header ---
    XtfFileHeader fhdr{};
    fread(&fhdr, sizeof(fhdr), 1, f);
    printf("=== XTF FILE HEADER ===\n");
    printf("  FileFormat       : 0x%02X (%s)\n", fhdr.FileFormat, fhdr.FileFormat == 0x7B ? "OK" : "BAD");
    printf("  SonarName        : %.16s\n", fhdr.SonarName);
    printf("  RecProgram       : %.8s\n", fhdr.RecordingProgramName);
    printf("  NavUnits         : %u  (0=degrees,1=degrees, 3=metres UTM)\n", fhdr.NavUnits);
    printf("  NumSonarChannels : %u\n", fhdr.NumberOfSonarChannels);
    printf("  NumBathyChannels : %u\n", fhdr.NumberOfBathymetryChannels);
    printf("\n");

    // --- Channel info ---
    int nchans = fhdr.NumberOfSonarChannels + fhdr.NumberOfBathymetryChannels;
    for (int c = 0; c < nchans && c < 8; ++c) {
        long off = 256L + c * (long)sizeof(XtfChanInfo);
        fseek(f, off, SEEK_SET);
        XtfChanInfo ci{};
        fread(&ci, sizeof(ci), 1, f);
        const char* type_str = ci.TypeOfChannel == 0 ? "SubBottom"
                             : ci.TypeOfChannel == 1 ? "Port-SSS"
                             : ci.TypeOfChannel == 2 ? "Stbd-SSS"
                             : "Other";
        printf("=== CHANNEL %d ===\n", c);
        printf("  TypeOfChannel    : %u (%s)\n", ci.TypeOfChannel, type_str);
        printf("  SubChannelNumber : %u\n", ci.SubChannelNumber);
        printf("  BytesPerSample   : %u  (0=unset, 1=8bit, 2=16bit)\n", ci.BytesPerSample);
        printf("  SamplesPerChannel: %u\n", ci.SamplesPerChannel);
        printf("  ChannelName      : %.16s\n", ci.ChannelName);
        printf("  Frequency        : %.1f Hz\n", ci.Frequency);
        printf("  SlantRange via header: (check ping header)\n");
        printf("\n");
    }

    // --- Seek to data start (1024) ---
    fseek(f, 1024, SEEK_SET);

    // --- Dump first 5 ping records ---
    printf("=== FIRST PING RECORDS ===\n");
    int ping_count = 0;
    while (ping_count < 5) {
        long rec_off = ftell(f);

        XtfPacketHeader phdr{};
        if (fread(&phdr, sizeof(phdr), 1, f) != 1) break;
        if (phdr.MagicNumber != 0xFACE) {
            printf("  [offset %ld] Bad magic 0x%04X — skipping byte\n", rec_off, phdr.MagicNumber);
            fseek(f, rec_off + 1, SEEK_SET);
            continue;
        }
        if (phdr.HeaderType != 0) {
            // Not a ping — skip
            fseek(f, rec_off + phdr.NumBytesThisRecord, SEEK_SET);
            continue;
        }

        printf("--- Ping record at offset %ld ---\n", rec_off);
        printf("  NumBytesThisRecord: %u\n", phdr.NumBytesThisRecord);
        printf("  NumChansToFollow  : %u\n", phdr.NumChansToFollow);
        printf("  PingNumber        : %u\n", phdr.PingNumber);
        printf("  SensorLat/Lon     : %.6f / %.6f\n", phdr.SensorYcoordinate, phdr.SensorXcoordinate);
        printf("  ShipLat/Lon       : %.6f / %.6f\n", phdr.ShipYcoordinate, phdr.ShipXcoordinate);

        long chan_off = rec_off + (long)sizeof(XtfPacketHeader);
        for (int ch = 0; ch < (int)phdr.NumChansToFollow && ch < 4; ++ch) {
            fseek(f, chan_off, SEEK_SET);
            XtfPingChanHeader chdr{};
            if (fread(&chdr, sizeof(chdr), 1, f) != 1) break;

            printf("  Chan[%d]: ChannelNum=%u NumSamples=%u SlantRange=%.1fm TimeDuration=%.4fs\n",
                ch, chdr.ChannelNumber, chdr.NumSamples, chdr.SlantRange, chdr.TimeDuration);

            // Compute expected bps from record geometry
            long data_bytes = (long)phdr.NumBytesThisRecord
                - (long)sizeof(XtfPacketHeader)
                - (long)phdr.NumChansToFollow * (long)sizeof(XtfPingChanHeader);
            long total_samples = (long)phdr.NumChansToFollow * (long)chdr.NumSamples;
            int inferred_bps = (total_samples > 0) ? (int)(data_bytes / total_samples) : 0;
            printf("         inferred_bps=%d (data_bytes=%ld, total_samples=%ld)\n",
                inferred_bps, data_bytes, total_samples);

            // Sample first 8 bytes of sample data raw
            long sample_start = chan_off + (long)sizeof(XtfPingChanHeader);
            fseek(f, sample_start, SEEK_SET);
            uint8_t raw8[16]{};
            fread(raw8, 1, 16, f);
            printf("         first 16 raw bytes: ");
            for (int i = 0; i < 16; ++i) printf("%02X ", raw8[i]);
            printf("\n");

            if (inferred_bps == 2) {
                // Print first 8 samples as uint16
                printf("         first 8 uint16 samples: ");
                for (int i = 0; i < 8; ++i) {
                    uint16_t s = (uint16_t)raw8[i*2] | ((uint16_t)raw8[i*2+1] << 8);
                    printf("%u ", s);
                }
                printf("\n");
            } else {
                printf("         first 16 uint8 samples: ");
                for (int i = 0; i < 16; ++i) printf("%u ", raw8[i]);
                printf("\n");
            }

            chan_off = sample_start + (long)chdr.NumSamples * inferred_bps;
        }
        printf("\n");
        fseek(f, rec_off + phdr.NumBytesThisRecord, SEEK_SET);
        ++ping_count;
    }

    fclose(f);
    return 0;
}
