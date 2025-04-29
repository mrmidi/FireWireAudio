// include/FWA/Enums.hpp
#pragma once
#include <cstdint>

namespace FWA {

constexpr uint8_t kAVCOpenDescriptorOpcode = 0x08;
constexpr uint8_t kAVCReadDescriptorOpcode = 0x09;
constexpr uint8_t kAVCWriteDescriptorOpcode = 0x0A;
constexpr uint8_t kAVCCreateDescriptorOpcode = 0x0C;
constexpr uint8_t kAVCReadInfoBlockOpcode = 0x06;
constexpr uint8_t kAVCWriteInfoBlockOpcode = 0x07;
constexpr uint8_t kMusicSubunitIdentifierSpecifier = 0x80; // For Music/Audio subunit status descriptor
constexpr uint8_t kReadResultComplete = 0x10;
constexpr uint8_t kReadResultMoreData = 0x11;
constexpr uint8_t kReadResultDataLengthTooLarge = 0x12;

#ifndef kIOReturnBadResponse
#define kIOReturnBadResponse kIOReturnError
#endif
#ifndef kIOReturnIOError
#define kIOReturnIOError kIOReturnError
#endif

// Move all AVC constants from FWA::AVC to FWA namespace directly
constexpr uint8_t kAVCStreamFormatOpcodePrimary   = 0xBF;
constexpr uint8_t kAVCStreamFormatOpcodeAlternate = 0x2F;
constexpr uint8_t kAVCDestinationPlugConfigureOpcode = 0x40;
constexpr uint8_t kAVCSourcePlugConfigureOpcode      = 0x41;
constexpr uint8_t kAVCDestinationConfigurationsOpcode= 0x42;
constexpr uint8_t kAVCSourceConfigurationsOpcode     = 0x43;
constexpr uint8_t kAVCMusicPlugInfoOpcode            = 0xC0;
constexpr uint8_t kAVCCurrentCapabilityOpcode        = 0xC1;
constexpr uint8_t kAVCStreamFormatCurrentQuerySubfunction    = 0xC0;
constexpr uint8_t kAVCStreamFormatSupportedQuerySubfunction  = 0xC1;
constexpr uint8_t kAVCStreamFormatSetSubfunction             = 0xC2; // <-- New
constexpr uint8_t kAVCDestPlugSubfuncConnect             = 0x00;
constexpr uint8_t kAVCDestPlugSubfuncChangeConnection    = 0x01;
constexpr uint8_t kAVCDestPlugSubfuncDisconnect          = 0x02;
constexpr uint8_t kAVCDestPlugSubfuncDisconnectAll       = 0x03;
constexpr uint8_t kAVCDestPlugSubfuncDefaultConfigure    = 0x04;
constexpr uint8_t kAVCDestPlugResultStatusOK              = 0x00;
constexpr uint8_t kAVCDestPlugResultNoConnection          = 0x01; // Renamed from UnknownSubfunction for clarity in context
constexpr uint8_t kAVCDestPlugResultUnknownMusicPlugType  = 0x02;
constexpr uint8_t kAVCDestPlugResultMusicPlugNotExist     = 0x03;
constexpr uint8_t kAVCDestPlugResultSubunitPlugNotExist   = 0x04;
constexpr uint8_t kAVCDestPlugResultMusicPlugConnected    = 0x05;

// OPEN DESCRIPTOR Subfunctions (Table 29, TA 2002013)
constexpr uint8_t kAVCOpenDescSubfuncClose     = 0x00;
constexpr uint8_t kAVCOpenDescSubfuncReadOpen  = 0x01;
constexpr uint8_t kAVCOpenDescSubfuncWriteOpen = 0x03;

// WRITE DESCRIPTOR Subfunctions (Table 37, TA 2002013)
constexpr uint8_t kAVCWriteDescSubfuncChange         = 0x10; // (Not recommended)
constexpr uint8_t kAVCWriteDescSubfuncReplace        = 0x20;
constexpr uint8_t kAVCWriteDescSubfuncInsert         = 0x30;
constexpr uint8_t kAVCWriteDescSubfuncDelete         = 0x40;
constexpr uint8_t kAVCWriteDescSubfuncPartialReplace = 0x50;

// WRITE INFO BLOCK Subfunctions (Section 7.9.1, TA 2002013)
constexpr uint8_t kAVCWriteInfoBlockSubfuncPartialReplace = 0x50;

// CREATE DESCRIPTOR Subfunctions (Table 22, TA 2002013)
constexpr uint8_t kAVCCreateDescSubfuncListOrEntry = 0x00;
constexpr uint8_t kAVCCreateDescSubfuncEntryAndChild = 0x01;

/**
 * @brief Descriptor specifier type (matches AV/C specification)
 */
enum class DescriptorSpecifierType : uint8_t {
    UnitSubunitIdentifier              = 0x00,
    ListById                           = 0x10,
    ListByType                         = 0x11,
    EntryByPositionInListId            = 0x20,
    EntryByObjectIdInListTypeRoot      = 0x21,
    EntryByTypeCreate                  = 0x22,
    EntryByObjectIdGeneral             = 0x23,
    EntryByObjectIdInSubunitListTypeRoot = 0x24, // Not fully implemented
    EntryByObjectIdInSubunit           = 0x25,   // Not fully implemented
    InfoByTypeAndInstance              = 0x30, // Added for info block specifier
    InfoByPosition                     = 0x31, // Added for info block specifier
    // Subunit-dependent types (0x80-0xBF) are not fully implemented
    SubunitDependentStart              = 0x80,
    SubunitDependentEnd                = 0xBF,
    Unknown                            = 0xFF
};

/**
 * @brief Direction of audio plug (input or output)
 */
enum class PlugDirection : uint8_t {
    Input,    ///< Input direction (receiving audio)
    Output    ///< Output direction (sending audio)
};

/**
 * @brief Type of plug usage in the audio device
 */
enum class PlugUsage : uint8_t {
    Isochronous,    ///< Used for isochronous streaming (Unit PCR)
    External,       ///< External connection (Unit External Plug)
    MusicSubunit,   ///< Belongs to Music Subunit
    AudioSubunit,   ///< Belongs to Audio Subunit
    Unknown         ///< Unknown usage type
};

/**
 * @brief Audio stream format type identifier
 */
enum class FormatType : uint8_t {
    CompoundAM824,  ///< Compound AM824 format (0x9040)
    AM824,          ///< Standard AM824 format (0x9000)
    Unknown         ///< Unknown format type
};

/**
 * @brief Sample rates supported by AV/C Stream Format specification
 */
enum class SampleRate : uint8_t {
    SR_22050  = 0x00,     ///< 22.05 kHz
    SR_24000  = 0x01,     ///< 24 kHz
    SR_32000  = 0x02,     ///< 32 kHz
    SR_44100  = 0x03,     ///< 44.1 kHz
    SR_48000  = 0x04,     ///< 48 kHz
    SR_96000  = 0x05,     ///< 96 kHz
    SR_176400 = 0x06,    ///< 176.4 kHz
    SR_192000 = 0x07,    ///< 192 kHz
    // 0x08, 0x09 reserved
    SR_88200  = 0x0A,     ///< 88.2 kHz
    // 0x0B - 0x0E reserved
    DontCare  = 0x0F,     ///< Sample rate doesn't matter / not specified
    Unknown   = 0xFF       ///< Unknown or invalid sample rate
};

/**
 * @brief Sample rates specifically for music subunit (TA 2001007) - May differ from stream format enums
 */
enum class MusicSubunitSampleRate : uint8_t {
    SR_32000  = 0x00,     ///< 32 kHz
    SR_44100  = 0x01,     ///< 44.1 kHz
    SR_48000  = 0x02,     ///< 48 kHz
    SR_88200  = 0x03,     ///< 88.2 kHz
    SR_96000  = 0x04,     ///< 96 kHz
    SR_176400 = 0x05,    ///< 176.4 kHz
    SR_192000 = 0x06,    ///< 192 kHz
    Unknown   = 0xFF       ///< Unknown sample rate
};

/**
 * @brief Standard AV/C Subunit Types (based on TA 2004006 Table 11)
 */
enum class SubunitType : uint8_t {
    Monitor             = 0x00,
    Audio               = 0x01, // Defined in TA 1999008
    Printer             = 0x02,
    Disc                = 0x03,
    TapeRecorderPlayer  = 0x04,
    Tuner               = 0x05,
    CA                  = 0x06,
    Camera              = 0x07,
    // 0x08 Reserved
    Panel               = 0x09,
    BulletinBoard       = 0x0A,
    CameraStorage       = 0x0B,
    Music               = 0x0C, // Defined in TA 2001007
    // 0x0D - 0x1B Reserved
    VendorUnique        = 0x1C,
    // 0x1D Reserved for all subunit types
    Extended            = 0x1E, // Subunit_type extended to next byte
    Unit                = 0x1F, // Addresses the AV/C unit itself
    Unknown             = 0xFF  // Represents an invalid or unknown type
};

/**
 * @brief AV/C Information Block Types (selected values based on provided documents)
 * Using uint16_t as underlying type.
 */
enum class InfoBlockType : uint16_t {
    // General Info Blocks (TA 2002013)
    RawText             = 0x000A,
    Name                = 0x000B,

    // Music Subunit Specific Info Blocks (TA 2001007)
    GeneralMusicStatus  = 0x8100,
    MusicOutputPlugStatus = 0x8101,
    SourcePlugStatus    = 0x8102,
    AudioInfo           = 0x8103,
    MidiInfo            = 0x8104,
    SmpteTimeCodeInfo   = 0x8105,
    SampleCountInfo     = 0x8106,
    AudioSyncInfo       = 0x8107,
    RoutingStatus       = 0x8108,
    SubunitPlugInfo     = 0x8109,
    ClusterInfo         = 0x810A,
    MusicPlugInfo       = 0x810B,

    // Placeholder
    Unknown             = 0xFFFF
};

/**
 * @brief AM824 Stream Format Codes (based on TA 2001007 and parsing code)
 * These codes identify the type of data within an AM824 stream or field.
 */
enum class StreamFormatCode : uint8_t {
    IEC60958_3            = 0x00,
    IEC61937_3            = 0x01,
    IEC61937_4            = 0x02,
    IEC61937_5            = 0x03,
    IEC61937_6            = 0x04,
    IEC61937_7            = 0x05,
    MBLA                  = 0x06, // Multi-Bit Linear Audio
    DVDAudio              = 0x07, // MBLA (DVD-Audio variant)
    OneBit                = 0x08, // One Bit Audio (Raw)
    OneBitSACD            = 0x09, // One Bit Audio (SACD DSD)
    OneBitEncoded         = 0x0A, // One Bit Audio (Encoded Raw DST?)
    OneBitSACDEncoded     = 0x0B, // One Bit Audio (Encoded SACD DST?)
    HiPrecisionMBLA       = 0x0C, // High Precision Multi-bit Linear Audio
    MidiConf              = 0x0D, // MIDI Conformant
    SMPTETimeCode         = 0x0E, // SMPTE Time Code
    SampleCount           = 0x0F, // Sample Count
    AncillaryData         = 0x10,
    // 0x11 - 0x3F Reserved
    SyncStream            = 0x40,
    // 0x41 - 0xFE Reserved
    DontCare              = 0xFF
};


} // namespace FWA