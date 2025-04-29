#include "FWA/DeviceParser.hpp"
#include "FWA/AudioDevice.h"
#include "FWA/CommandInterface.h" // Include CommandInterface definition
#include "FWA/UnitPlugDiscoverer.hpp"
#include "FWA/SubunitDiscoverer.hpp"
#include "FWA/PlugDetailParser.hpp"
#include "FWA/DescriptorReader.hpp"         // Can likely be removed now
#include "FWA/DescriptorAccessor.hpp"       // <-- Add Include
#include "FWA/DescriptorUtils.hpp"        // <-- Add Include
#include "FWA/MusicSubunitDescriptorParser.hpp"
#include "FWA/AudioPlug.hpp"
#include "FWA/Helpers.h"
#include <spdlog/spdlog.h>
#include <vector>
#include <memory>
#include <stdexcept>
#include "FWA/MusicSubunitCapabilities.hpp"
#include "FWA/AudioStreamFormat.hpp"
#include "FWA/DescriptorUtils.hpp"
#include <tuple> // Include tuple header

namespace FWA {

namespace {
    struct UnitDescriptorSizes {
        uint8_t generationId = 0;
        size_t sizeOfListId = 0;
        size_t sizeOfObjectId = 0;
        size_t sizeOfEntryPos = 0;
        bool valid = false;
    };

    UnitDescriptorSizes parseUnitIdentifierDescriptorSizes(const std::vector<uint8_t>& data) {
        UnitDescriptorSizes sizes;
        if (data.size() < 6) {
            spdlog::error("parseUnitIdentifierDescriptorSizes: Data too short ({}) for essential fields.", data.size());
            return sizes;
        }
        sizes.generationId = data[2];
        sizes.sizeOfListId = data[3];
        sizes.sizeOfObjectId = data[4];
        sizes.sizeOfEntryPos = data[5];
        sizes.valid = true;
        spdlog::debug("Parsed Unit Descriptor Sizes: GenID=0x{:02x}, ListIDSize={}, ObjIDSize={}, EntryPosSize={}",
                      sizes.generationId, sizes.sizeOfListId, sizes.sizeOfObjectId, sizes.sizeOfEntryPos);
        return sizes;
    }

    using ParsedMusicIdentifierData = std::tuple<
        MusicSubunitCapabilities,          // Parsed static capabilities
        std::vector<uint8_t>,              // Raw bytes of subunit_type_dependent_information
        std::vector<uint8_t>               // Raw bytes of music_subunit_specific_information
    >;

    std::optional<ParsedMusicIdentifierData> parseMusicSubunitIdentifier(
        const std::vector<uint8_t>& subunitData,
        size_t unitSizeOfListId,
        size_t unitSizeOfObjectId,
        size_t unitSizeOfEntryPos)
    {
        // <--- START Logging inside parseMusicSubunitIdentifier --->
        spdlog::debug("Entering parseMusicSubunitIdentifier (Data size: {})", subunitData.size());
        size_t previewSize = std::min(subunitData.size(), (size_t)20);
        spdlog::debug("  Subunit Data Preview: {}", Helpers::formatHexBytes(std::vector<uint8_t>(subunitData.begin(), subunitData.begin() + previewSize)));

        if (subunitData.size() < 10) {
            spdlog::error("  parseMusicSubunitIdentifier: Data too short ({}) for header. Returning nullopt.", subunitData.size());
            return std::nullopt;
        }
        // ...existing code...
        uint16_t numRootLists = DescriptorUtils::readBytes(subunitData.data() + 6, 2);
        size_t rootListArraySize = numRootLists * unitSizeOfListId;
        size_t subunitDepInfoLenOffset = 8 + rootListArraySize;

        if (subunitData.size() < subunitDepInfoLenOffset + 2) {
            spdlog::error("  parseMusicSubunitIdentifier: Data too short ({}) for subunit_type_dependent_information_length at offset {}. Returning nullopt.", subunitData.size(), subunitDepInfoLenOffset);
            return std::nullopt;
        }
        uint16_t subunitDepInfoLen = DescriptorUtils::readBytes(subunitData.data() + subunitDepInfoLenOffset, 2);
        size_t subunitDepInfoOffset = subunitDepInfoLenOffset + 2;
        spdlog::debug("  Parsed subunitDepInfoLen = {} at offset {}", subunitDepInfoLen, subunitDepInfoLenOffset);

        if (subunitData.size() < subunitDepInfoOffset + subunitDepInfoLen) {
             spdlog::error("  parseMusicSubunitIdentifier: Data too short ({}) for claimed subunit_type_dependent_information (len {}) at offset {}. Returning nullopt.", subunitData.size(), subunitDepInfoLen, subunitDepInfoOffset);
             return std::nullopt;
        }

        const uint8_t* musicInfoPtr = subunitData.data() + subunitDepInfoOffset;
        size_t musicInfoAvailableLen = subunitDepInfoLen;
        spdlog::debug("  Parsing Music Specific Dependent Info (Available length: {})", musicInfoAvailableLen);

        if (musicInfoAvailableLen < 5) {
            spdlog::error("  parseMusicSubunitIdentifier: Music Subunit Dependent Info too short ({}) for its header. Returning nullopt.", musicInfoAvailableLen);
            return std::nullopt;
        }

        MusicSubunitCapabilities caps;
        caps.musicSubunitVersion = musicInfoPtr[3];
        uint16_t musicSpecificInfoLen = DescriptorUtils::readBytes(musicInfoPtr + 4, 2);
        size_t musicSpecificInfoOffset = 6;

        spdlog::debug("  Music Subunit Version: 0x{:02x}, Specific Info Length: {}", caps.musicSubunitVersion, musicSpecificInfoLen);

        if (musicInfoAvailableLen < musicSpecificInfoOffset + musicSpecificInfoLen) {
            spdlog::error("  parseMusicSubunitIdentifier: Music Subunit Dependent Info too short ({}) for claimed specific_information length ({}). Returning nullopt.", musicInfoAvailableLen, musicSpecificInfoLen);
            return std::nullopt;
        }

        const uint8_t* specificPtr = musicInfoPtr + musicSpecificInfoOffset;
        size_t specificAvailableLen = musicSpecificInfoLen;
        size_t currentSpecificOffset = 0;
        spdlog::debug("  Parsing music_subunit_specific_information (Available length: {})", specificAvailableLen);

        if (specificAvailableLen < 1) {
             spdlog::warn("  parseMusicSubunitIdentifier: Music Subunit Specific Info area is empty. Returning partially filled caps.");
             return std::make_tuple(caps, std::vector<uint8_t>(musicInfoPtr, musicInfoPtr + subunitDepInfoLen), std::vector<uint8_t>());
        }

        uint8_t capAttribs = specificPtr[currentSpecificOffset];
        currentSpecificOffset++;
        caps.hasGeneralCapability = (capAttribs & 0x01) != 0;
        caps.hasAudioCapability = (capAttribs & 0x02) != 0;
        caps.hasMidiCapability = (capAttribs & 0x04) != 0;
        caps.hasSmpteTimeCodeCapability = (capAttribs & 0x08) != 0;
        caps.hasSampleCountCapability = (capAttribs & 0x10) != 0;
        caps.hasAudioSyncCapability = (capAttribs & 0x20) != 0;
        spdlog::debug("    Capability Attribs Byte: 0x{:02x}", capAttribs);
        spdlog::debug("    -> Flags: Gen={}, Aud={}, MIDI={}, SMPTE={}, Samp={}, Sync={}",
                      caps.hasGeneralCapability, caps.hasAudioCapability, caps.hasMidiCapability,
                      caps.hasSmpteTimeCodeCapability, caps.hasSampleCountCapability, caps.hasAudioSyncCapability);

        // --- Parse General Capability ---
        if (caps.hasGeneralCapability) {
            spdlog::debug("    Parsing General Capability...");
            if (specificAvailableLen < currentSpecificOffset + 1) goto parse_error_exit;
            uint8_t genCapLenByte = specificPtr[currentSpecificOffset];
            size_t genCapTotalBlockSize = static_cast<size_t>(genCapLenByte) + 1;
            spdlog::debug("      genCapLenByte = {}", genCapLenByte);
            if (specificAvailableLen < currentSpecificOffset + genCapTotalBlockSize || genCapLenByte < 6) {
                spdlog::error("      General Capability block size invalid or data too short.");
                goto parse_error_exit;
            }
            const uint8_t* genCapPtr = specificPtr + currentSpecificOffset + 1;
            caps.transmitCapabilityFlags = genCapPtr[0];
            caps.receiveCapabilityFlags = genCapPtr[1];
            caps.latencyCapability = DescriptorUtils::readBytes(genCapPtr + 2, 4);
            spdlog::debug("      -> TxFlags=0x{:02x}, RxFlags=0x{:02x}, Latency=0x{:08x}",
                           caps.transmitCapabilityFlags.value(), caps.receiveCapabilityFlags.value(), caps.latencyCapability.value());
            currentSpecificOffset += genCapTotalBlockSize;
        } else {
             spdlog::debug("    Skipping General Capability (flag not set).");
        }

        // --- Parse Audio Capability ---
        if (caps.hasAudioCapability) {
            spdlog::debug("    Parsing Audio Capability...");
            if (specificAvailableLen < currentSpecificOffset + 1) goto parse_error_exit;
            uint8_t audioCapLenByte = specificPtr[currentSpecificOffset];
            size_t audioCapTotalBlockSize = static_cast<size_t>(audioCapLenByte) + 1;
            spdlog::debug("      audioCapLenByte = {}", audioCapLenByte);
             if (specificAvailableLen < currentSpecificOffset + audioCapTotalBlockSize || audioCapLenByte < 5) {
                spdlog::error("      Audio Capability block size invalid or data too short.");
                goto parse_error_exit;
             }
            const uint8_t* audioCapPtr = specificPtr + currentSpecificOffset + 1;
            uint8_t numFormats = audioCapPtr[0];
            size_t minRequiredDataLen = 1 + 4 + (numFormats * 6);
            if (audioCapLenByte < minRequiredDataLen) {
                spdlog::error("      Audio Capability data length ({}) insufficient for {} formats.", audioCapLenByte, numFormats);
                goto parse_error_exit;
            }

            caps.maxAudioInputChannels = DescriptorUtils::readBytes(audioCapPtr + 1, 2);
            caps.maxAudioOutputChannels = DescriptorUtils::readBytes(audioCapPtr + 3, 2);
            spdlog::debug("      -> MaxInCh={}, MaxOutCh={}, NumFormats={}", caps.maxAudioInputChannels.value_or(0), caps.maxAudioOutputChannels.value_or(0), numFormats);

            std::vector<AudioStreamFormat> formats;
            size_t formatOffset = 5;
            for (uint8_t i = 0; i < numFormats; ++i) {
                 if (audioCapLenByte < formatOffset + 6) {
                     spdlog::error("      Audio format list truncated at index {}.", i);
                     goto parse_error_exit;
                 }
                 AudioStreamFormat fmt;
                 spdlog::debug("      -> Format {}: FDF=0x{:02x}, Label=0x{:02x} (Parsing TBD)", i, audioCapPtr[formatOffset], audioCapPtr[formatOffset+1]);
                 formats.push_back(fmt);
                 formatOffset += 6;
            }
            caps.availableAudioFormats = std::move(formats);
            currentSpecificOffset += audioCapTotalBlockSize;
        } else {
             spdlog::debug("    Skipping Audio Capability (flag not set).");
        }

        // --- Parse MIDI Capability ---
        if (caps.hasMidiCapability) {
             spdlog::debug("    Parsing MIDI Capability...");
             if (specificAvailableLen < currentSpecificOffset + 1) goto parse_error_exit;
             uint8_t midiCapLenByte = specificPtr[currentSpecificOffset];
             size_t midiCapTotalBlockSize = static_cast<size_t>(midiCapLenByte) + 1;
             spdlog::debug("      midiCapLenByte = {}", midiCapLenByte);
             if (specificAvailableLen < currentSpecificOffset + midiCapTotalBlockSize || midiCapLenByte < 6) goto parse_error_exit;
             const uint8_t* midiCapPtr = specificPtr + currentSpecificOffset + 1;
             caps.midiVersionMajor = midiCapPtr[0] >> 4;
             caps.midiVersionMinor = midiCapPtr[0] & 0x0F;
             caps.midiAdaptationLayerVersion = midiCapPtr[1];
             caps.maxMidiInputPorts = DescriptorUtils::readBytes(midiCapPtr + 2, 2);
             caps.maxMidiOutputPorts = DescriptorUtils::readBytes(midiCapPtr + 4, 2);
             spdlog::debug("      -> MaxIn={}, MaxOut={}, MIDI Ver={}.{}, Adapt Ver=0x{:02x}",
                           caps.maxMidiInputPorts.value_or(0), caps.maxMidiOutputPorts.value_or(0),
                           caps.midiVersionMajor.value_or(0), caps.midiVersionMinor.value_or(0),
                           caps.midiAdaptationLayerVersion.value_or(0));
             currentSpecificOffset += midiCapTotalBlockSize;
        } else {
             spdlog::debug("    Skipping MIDI Capability (flag not set).");
        }

        // --- Parse SMPTE ---
        if (caps.hasSmpteTimeCodeCapability) {
            spdlog::debug("    Parsing SMPTE Capability...");
            if (specificAvailableLen < currentSpecificOffset + 1) goto parse_error_exit;
            uint8_t smpteCapLenByte = specificPtr[currentSpecificOffset];
            size_t smpteCapTotalBlockSize = static_cast<size_t>(smpteCapLenByte) + 1;
            spdlog::debug("      smpteCapLenByte = {}", smpteCapLenByte);
             if (specificAvailableLen < currentSpecificOffset + smpteCapTotalBlockSize || smpteCapLenByte < 1) goto parse_error_exit;
            caps.smpteTimeCodeCapabilityFlags = specificPtr[currentSpecificOffset + 1];
            spdlog::debug("      -> Flags=0x{:02x}", caps.smpteTimeCodeCapabilityFlags.value());
            currentSpecificOffset += smpteCapTotalBlockSize;
        } else {
             spdlog::debug("    Skipping SMPTE Capability (flag not set).");
        }

         // --- Parse Sample Count ---
         if (caps.hasSampleCountCapability) {
            spdlog::debug("    Parsing Sample Count Capability...");
            if (specificAvailableLen < currentSpecificOffset + 1) goto parse_error_exit;
            uint8_t sampleCapLenByte = specificPtr[currentSpecificOffset];
            size_t sampleCapTotalBlockSize = static_cast<size_t>(sampleCapLenByte) + 1;
            spdlog::debug("      sampleCapLenByte = {}", sampleCapLenByte);
             if (specificAvailableLen < currentSpecificOffset + sampleCapTotalBlockSize || sampleCapLenByte < 1) goto parse_error_exit;
            caps.sampleCountCapabilityFlags = specificPtr[currentSpecificOffset + 1];
            spdlog::debug("      -> Flags=0x{:02x}", caps.sampleCountCapabilityFlags.value());
            currentSpecificOffset += sampleCapTotalBlockSize;
         } else {
              spdlog::debug("    Skipping Sample Count Capability (flag not set).");
         }

         // --- Parse Audio Sync ---
         if (caps.hasAudioSyncCapability) {
            spdlog::debug("    Parsing Audio Sync Capability...");
             if (specificAvailableLen < currentSpecificOffset + 1) goto parse_error_exit;
             uint8_t syncCapLenByte = specificPtr[currentSpecificOffset];
             size_t syncCapTotalBlockSize = static_cast<size_t>(syncCapLenByte) + 1;
             spdlog::debug("      syncCapLenByte = {}", syncCapLenByte);
             if (specificAvailableLen < currentSpecificOffset + syncCapTotalBlockSize || syncCapLenByte < 1) goto parse_error_exit;
             caps.audioSyncCapabilityFlags = specificPtr[currentSpecificOffset + 1];
             spdlog::debug("      -> Flags=0x{:02x}", caps.audioSyncCapabilityFlags.value());
             currentSpecificOffset += syncCapTotalBlockSize;
         } else {
              spdlog::debug("    Skipping Audio Sync Capability (flag not set).");
         }

        spdlog::debug("Exiting parseMusicSubunitIdentifier successfully.");
        return std::make_tuple(caps, std::vector<uint8_t>(musicInfoPtr, musicInfoPtr + subunitDepInfoLen), std::vector<uint8_t>(specificPtr, specificPtr + musicSpecificInfoLen));

    parse_error_exit:
        spdlog::error("parseMusicSubunitIdentifier: Error during parsing specific capability block at offset {}. Returning nullopt.", currentSpecificOffset);
        return std::nullopt;
        // <--- END Logging inside parseMusicSubunitIdentifier --->
    }
}

DeviceParser::DeviceParser(AudioDevice* device)
    : device_(device), commandInterface_(nullptr), info_(device->info_), descriptorMechanismSupported_{false}
{
    if (!device_) {
        spdlog::critical("DeviceParser: AudioDevice pointer is null.");
        throw std::runtime_error("DeviceParser requires a valid AudioDevice.");
    }
    if (auto iface = device_->getCommandInterface()) {
         commandInterface_ = iface.get();
         if (!commandInterface_) {
              spdlog::critical("DeviceParser: Failed to get CommandInterface from AudioDevice.");
              throw std::runtime_error("DeviceParser could not obtain CommandInterface.");
         }
    } else {
         spdlog::critical("DeviceParser: AudioDevice has no CommandInterface.");
         throw std::runtime_error("DeviceParser requires an AudioDevice with a CommandInterface.");
    }
}

// ...existing code...
// --- REVISED: Helper for Non-Standard Read ---
static std::expected<std::vector<uint8_t>, IOKitError> readDescriptorNonStandard(
    CommandInterface* commandInterface,
    uint8_t targetAddr,
    const std::vector<uint8_t>& specifier,
    uint16_t offset, // Initial offset requested (usually 0 for full read)
    uint16_t length) // Requested length (0 means read all based on internal length)
{
    if (!commandInterface) {
        return std::unexpected(IOKitError(kIOReturnNotReady));
    }

    spdlog::warn("readDescriptorNonStandard: Attempting non-standard READ (CONTROL ctype) for Addr=0x{:02x}, Specifier={}",
                  targetAddr, Helpers::formatHexBytes(specifier));

    std::vector<uint8_t> accumulatedData;
    uint16_t currentDeviceOffset = offset; // Offset *within the descriptor* to request
    uint16_t totalExpectedDataLength = 0;  // Length determined from first response
    bool lengthDetermined = false;
    int attempts = 0;
    const int MAX_ATTEMPTS = 256;
    const uint16_t MAX_CHUNK_REQUEST = 128; // How much data to ask for per request

    do {
        if (++attempts > MAX_ATTEMPTS) {
            spdlog::error("readDescriptorNonStandard: Exceeded max attempts.");
            return std::unexpected(IOKitError(kIOReturnTimeout));
        }

        // Determine chunk size to request
        uint16_t readChunkSize = MAX_CHUNK_REQUEST;
        if (lengthDetermined && length == 0) { // Read all mode, length known
            if (totalExpectedDataLength >= accumulatedData.size()) {
                uint16_t remaining = totalExpectedDataLength - static_cast<uint16_t>(accumulatedData.size());
                readChunkSize = std::min(remaining, MAX_CHUNK_REQUEST);
            } else {
                readChunkSize = 0;
            }
        } else if (length > 0) { // Specific length requested
            uint16_t remaining = length - static_cast<uint16_t>(accumulatedData.size());
            readChunkSize = std::min(remaining, MAX_CHUNK_REQUEST);
        }
        // If length=0 and not determined yet, request MAX_CHUNK

        if (readChunkSize == 0 && (lengthDetermined || length > 0)) {
            spdlog::debug("readDescriptorNonStandard: Requested read length achieved.");
            break; // Already have enough data
        }

        std::vector<uint8_t> cmd = {
            kAVCControlCommand,             // Using CONTROL (0x00)
            targetAddr,
            kAVCReadDescriptorOpcode        // Using READ OPCODE (0x09)
        };
        cmd.insert(cmd.end(), specifier.begin(), specifier.end());
        cmd.push_back(0xFF); // read_result_status - Placeholder
        cmd.push_back(0x00); // reserved
        cmd.push_back(static_cast<uint8_t>(readChunkSize >> 8));
        cmd.push_back(static_cast<uint8_t>(readChunkSize & 0xFF));
        cmd.push_back(static_cast<uint8_t>(currentDeviceOffset >> 8));
        cmd.push_back(static_cast<uint8_t>(currentDeviceOffset & 0xFF));

        spdlog::trace("Sending Non-Standard READ (Attempt {}): Offset={}, ChunkSize={}, Cmd={}",
                      attempts, currentDeviceOffset, readChunkSize, Helpers::formatHexBytes(cmd));
        auto result = commandInterface->sendCommand(cmd);

        // --- Check Response ---
        if (!result) {
            spdlog::error("readDescriptorNonStandard: sendCommand failed: 0x{:x}", static_cast<int>(result.error()));
            return std::unexpected(result.error());
        }
        const auto& response = result.value();
        if (response.empty()) {
             spdlog::error("readDescriptorNonStandard: Received empty response.");
             return std::unexpected(IOKitError(kIOReturnBadResponse));
        }
        uint8_t responseStatus = response[0];
        spdlog::trace("Non-Standard READ response status: 0x{:02x}", responseStatus);

        if (responseStatus != kAVCAcceptedStatus) { // Expecting 0x09
            spdlog::warn("readDescriptorNonStandard: Expected ACCEPTED (0x09) but got 0x{:02x}. Stopping read.", responseStatus);
            if (accumulatedData.empty()) {
                if(responseStatus == kAVCRejectedStatus) return std::unexpected(IOKitError(kIOReturnNotPermitted));
                if(responseStatus == kAVCNotImplementedStatus) return std::unexpected(IOKitError(kIOReturnUnsupported));
                return std::unexpected(IOKitError(kIOReturnBadResponse));
            } else {
                 break; // Stop reading, return partial data
            }
        }

        // --- Parse Response based on Apple Logs ---
        // Minimum size needed: Status(1)+Addr(1)+Opcode(1)+Specifier(1) + Metadata(8) + Data(>=0) = 12
        const size_t MIN_RESPONSE_SIZE_NON_STD = 12;
        if (response.size() < MIN_RESPONSE_SIZE_NON_STD) {
            spdlog::warn("readDescriptorNonStandard: ACCEPTED response too short ({}) for expected metadata. Stopping read.", response.size());
            break; // Stop reading, return what we have
        }

        // Extract length ONLY from the first successful response chunk
        if (!lengthDetermined && attempts == 1) {
            // Length seems to be at bytes 10 & 11 of the response frame
            totalExpectedDataLength = (static_cast<uint16_t>(response[10]) << 8) | response[11];
            lengthDetermined = true;
            spdlog::info("readDescriptorNonStandard: Determined expected descriptor data length: {}", totalExpectedDataLength);
            // If specific length was requested, override totalExpectedDataLength
            if (length > 0) {
                 totalExpectedDataLength = length;
                 spdlog::info("readDescriptorNonStandard: Requested specific length {}, overriding internal length.", length);
            }
            // Pre-allocate buffer if reading all (length == 0 initially)
             if (length == 0 && totalExpectedDataLength > 0) {
                 try {
                     accumulatedData.reserve(totalExpectedDataLength); // Reserve space
                 } catch (const std::length_error& le) {
                      spdlog::error("readDescriptorNonStandard: Failed to reserve memory ({} bytes): {}", totalExpectedDataLength, le.what());
                      return std::unexpected(IOKitError::NoMemory);
                 } catch (...) {
                      spdlog::error("readDescriptorNonStandard: Unknown exception during reserve.");
                      return std::unexpected(IOKitError::NoMemory);
                 }
             }
        }

        // Extract data payload - starts at byte 12
        size_t dataStartOffset = 12;
        if (response.size() <= dataStartOffset) {
            spdlog::warn("readDescriptorNonStandard: ACCEPTED response has no data payload (size {}). Stopping read.", response.size());
            break; // Stop reading
        }

        size_t bytesAvailableInPayload = response.size() - dataStartOffset;
        size_t bytesToAppend = bytesAvailableInPayload;

        // If we know the total expected length, don't append more than needed
        if (lengthDetermined) {
            size_t remainingNeeded = (totalExpectedDataLength >= accumulatedData.size()) ? (totalExpectedDataLength - accumulatedData.size()) : 0;
            bytesToAppend = std::min(bytesToAppend, remainingNeeded);
        }

        spdlog::trace("  Non-Standard Read: Appending {} bytes from response payload.", bytesToAppend);
        if (bytesToAppend > 0) {
            accumulatedData.insert(accumulatedData.end(), response.begin() + dataStartOffset, response.begin() + dataStartOffset + bytesToAppend);
            currentDeviceOffset += bytesToAppend; // Advance offset for next request
        }

        // Check if we have read the expected amount
        if (lengthDetermined && accumulatedData.size() >= totalExpectedDataLength) {
            spdlog::debug("readDescriptorNonStandard: Reached expected total length {}. Stopping.", totalExpectedDataLength);
             if (accumulatedData.size() > totalExpectedDataLength) {
                  spdlog::warn("readDescriptorNonStandard: Read slightly more ({}) than expected ({}). Trimming.", accumulatedData.size(), totalExpectedDataLength);
                  accumulatedData.resize(totalExpectedDataLength);
             }
            break; // Finished
        }

    } while (true); // Loop controlled by break conditions

    spdlog::info("readDescriptorNonStandard: Finished. Read {} bytes total.", accumulatedData.size());

    // *** CRITICAL: Prepend the 2-byte length field to match what MusicSubunitDescriptorParser expects ***
    if (lengthDetermined) {
        std::vector<uint8_t> finalData;
        finalData.reserve(totalExpectedDataLength + 2);
        finalData.push_back(static_cast<uint8_t>(totalExpectedDataLength >> 8));
        finalData.push_back(static_cast<uint8_t>(totalExpectedDataLength & 0xFF));
        finalData.insert(finalData.end(), accumulatedData.begin(), accumulatedData.end());
        spdlog::debug("readDescriptorNonStandard: Prepended length 0x{:04X}. Final data size: {}", totalExpectedDataLength, finalData.size());
        return finalData;
    } else if (!accumulatedData.empty()) {
         // This case shouldn't happen if the first response always contains the length, but handle defensively
         spdlog::warn("readDescriptorNonStandard: Length was never determined, but data was read. Prepending length based on accumulated size {}.", accumulatedData.size());
         std::vector<uint8_t> finalData;
         uint16_t accumulatedSize16 = static_cast<uint16_t>(std::min(accumulatedData.size(), (size_t)0xFFFF));
         finalData.reserve(accumulatedSize16 + 2);
         finalData.push_back(static_cast<uint8_t>(accumulatedSize16 >> 8));
         finalData.push_back(static_cast<uint8_t>(accumulatedSize16 & 0xFF));
         finalData.insert(finalData.end(), accumulatedData.begin(), accumulatedData.end());
         return finalData;
    } else {
        // No length determined and no data read, likely an error occurred earlier
        spdlog::error("readDescriptorNonStandard: Failed to determine length and read any data.");
        return std::unexpected(IOKitError(kIOReturnBadResponse)); // Or the last error recorded
    }
}
// ...existing code...
std::expected<void, IOKitError> DeviceParser::parse() {
    DescriptorAccessor descriptorAccessor(commandInterface_);
    UnitPlugDiscoverer unitDiscoverer(commandInterface_);
    SubunitDiscoverer subunitDiscoverer(commandInterface_);
    PlugDetailParser plugDetailParser(commandInterface_);
    MusicSubunitDescriptorParser musicDescParser(descriptorAccessor);

    spdlog::info("DeviceParser: Starting capability parsing for GUID: 0x{:x}", device_->getGuid());

    UnitDescriptorSizes unitSizes; // Stores sizes if read succeeds
    descriptorMechanismSupported_ = false; // Assume not supported initially
    bool triedUnitOpen = false;
    bool triedSubunitOpen = false;

    spdlog::debug("DeviceParser [Phase 1]: Attempting to read Unit Identifier Descriptor (Addr 0xFF)...");
    {
        std::vector<uint8_t> unitIdSpecifier = DescriptorUtils::buildDescriptorSpecifier(
            DescriptorSpecifierType::UnitSubunitIdentifier, 0, 0, 0);
        triedUnitOpen = true;
        auto openResult = descriptorAccessor.openForRead(0xFF, unitIdSpecifier);

        if (!openResult) {
            if (openResult.error() == IOKitError::Unsupported) {
                spdlog::info("DeviceParser [Phase 1]: Standard Descriptor Mechanism (OPEN) not supported by UNIT 0xFF.");
                // Continue to potentially try subunit open
            } else {
                spdlog::error("DeviceParser [Phase 1]: Failed to OPEN Unit Identifier Descriptor: 0x{:x}.", static_cast<int>(openResult.error()));
                // Potentially return error, or just log and continue to basic discovery
                // Let's try continuing for now, maybe basic commands still work
            }
        } else {
            // UNIT OPEN succeeded! Try READ
            spdlog::debug("DeviceParser [Phase 1]: OPEN Unit Identifier successful. Reading...");
            auto readResult = descriptorAccessor.read(0xFF, unitIdSpecifier, 0, 0);
            auto closeResult = descriptorAccessor.close(0xFF, unitIdSpecifier);
            if (!closeResult) { /* warn */ }

            if (readResult) {
                unitSizes = parseUnitIdentifierDescriptorSizes(readResult.value());
                if (unitSizes.valid) {
                    descriptorMechanismSupported_ = true; // Mark as supported *at the unit level*
                    descriptorAccessor.updateDescriptorSizes(unitSizes.sizeOfListId, unitSizes.sizeOfObjectId, unitSizes.sizeOfEntryPos);
                    spdlog::info("DeviceParser [Phase 1]: Successfully read Unit ID and parsed sizes. Descriptor Mechanism SUPPORTED by Unit.");
                } else {
                     spdlog::warn("DeviceParser [Phase 1]: Read Unit ID data but failed to parse sizes. Descriptor mechanism support uncertain.");
                }
            } else {
                 spdlog::warn("DeviceParser [Phase 1]: Failed to READ Unit Identifier after successful OPEN: 0x{:x}.", static_cast<int>(readResult.error()));
            }
        }
    } // End Unit ID scope

    // --- Phase 2: Basic Discovery (Run regardless of descriptor support) ---
    spdlog::debug("DeviceParser [Phase 2]: Discovering subunits and plug counts...");
    auto unitPlugResult = unitDiscoverer.discoverUnitPlugs(info_);
    if (!unitPlugResult) {/* warn or handle */}
    auto subunitResult = subunitDiscoverer.discoverSubunits(info_);
    if (!subunitResult) { return subunitResult; } // Critical failure
    auto plugCountResult = subunitDiscoverer.queryPlugCounts(info_);
    if (!plugCountResult) {/* warn */}

    // --- Phase 3: Attempt Subunit Descriptor Access (Static Caps) ---
    // Try ONLY if Unit OPEN failed AND a Music Subunit exists
    if (info_.hasMusicSubunit() && !descriptorMechanismSupported_) {
        spdlog::info("DeviceParser [Phase 3]: Unit OPEN failed, attempting to OPEN Music Subunit Identifier directly (Addr 0x{:02x})...",
                      Helpers::getSubunitAddress(SubunitType::Music, info_.musicSubunit_.getId()));
        uint8_t musicSubunitAddr = Helpers::getSubunitAddress(SubunitType::Music, info_.musicSubunit_.getId());
        // Still use specifier type 0x00 (UnitSubunitIdentifier) but send to subunit address
        std::vector<uint8_t> subunitIdSpecifier = DescriptorUtils::buildDescriptorSpecifier(
            DescriptorSpecifierType::UnitSubunitIdentifier, 0, 0, 0);
        triedSubunitOpen = true;

        auto openSubunitResult = descriptorAccessor.openForRead(musicSubunitAddr, subunitIdSpecifier);
        if (!openSubunitResult) {
            if (openSubunitResult.error() == IOKitError::Unsupported) {
                 spdlog::info("DeviceParser [Phase 3]: Standard Descriptor Mechanism (OPEN) also not supported by Music SUBUNIT 0x{:02x}.", musicSubunitAddr);
            } else {
                 spdlog::warn("DeviceParser [Phase 3]: Failed to OPEN Music Subunit Identifier: 0x{:x}.", static_cast<int>(openSubunitResult.error()));
            }
            info_.musicSubunit_.capabilities_.reset(); // Ensure no capabilities if open fails
        } else {
            // Music SUBUNIT OPEN Succeeded! Try to READ its Identifier
            spdlog::debug("DeviceParser [Phase 3]: OPEN Music Subunit Identifier successful. Reading...");
            auto readSubunitResult = descriptorAccessor.read(musicSubunitAddr, subunitIdSpecifier, 0, 0);
            auto closeSubunitResult = descriptorAccessor.close(musicSubunitAddr, subunitIdSpecifier);
             if (!closeSubunitResult) { /* warn */ }

            if (!readSubunitResult) {
                 spdlog::warn("DeviceParser [Phase 3]: Failed to READ Music Subunit Identifier after successful OPEN: 0x{:x}. Static capabilities unavailable.", static_cast<int>(readSubunitResult.error()));
                 info_.musicSubunit_.capabilities_.reset();
            } else {
                // Successfully read Music Subunit Identifier! Parse capabilities.
                // We still need the Unit's size definitions if available, otherwise use defaults.
                size_t listIdSizeToUse = unitSizes.valid ? unitSizes.sizeOfListId : DescriptorUtils::DEFAULT_SIZE_OF_LIST_ID;
                size_t objIdSizeToUse = unitSizes.valid ? unitSizes.sizeOfObjectId : DescriptorUtils::DEFAULT_SIZE_OF_OBJECT_ID;
                size_t entryPosSizeToUse = unitSizes.valid ? unitSizes.sizeOfEntryPos : DescriptorUtils::DEFAULT_SIZE_OF_ENTRY_POS;

                auto parsedResult = parseMusicSubunitIdentifier(readSubunitResult.value(),
                                                                listIdSizeToUse, objIdSizeToUse, entryPosSizeToUse);
                if (parsedResult) {
                    auto& [caps, depBlock, specBlock] = parsedResult.value();
                    info_.musicSubunit_.capabilities_ = std::move(caps);
                    // Optionally store the extracted blocks if needed
                    // info_.musicSubunit_.setDependentInfoBlock(std::move(depBlock));
                    // info_.musicSubunit_.setSpecificInfoBlock(std::move(specBlock));
                    // spdlog::debug(" -> Stored Dependent Block ({} bytes), Specific Block ({} bytes)", depBlock.size(), specBlock.size());
                    spdlog::info("DeviceParser [Phase 3]: Successfully parsed Music Subunit static capabilities via direct subunit access.");
                    // If we got this far, the *subunit* supports descriptors, even if the unit didn't
                    descriptorMechanismSupported_ = true; // Indicate support at subunit level
                    // Update accessor sizes based on subunit's own values if different (though unlikely needed now)
                    // uint8_t subGenId = readSubunitResult.value()[2];
                    // size_t subSizeListId = readSubunitResult.value()[3];
                    // size_t subSizeObjId = readSubunitResult.value()[4];
                    // size_t subSizeEntryPos = readSubunitResult.value()[5];
                    // if (subSizeListId != listIdSizeToUse || ...) {
                    //     descriptorAccessor.updateDescriptorSizes(subSizeListId, subSizeObjId, subSizeEntryPos);
                    // }
                } else {
                    spdlog::warn("DeviceParser [Phase 3]: Failed to parse static capabilities from Music Subunit Identifier data (read via direct access).");
                    info_.musicSubunit_.capabilities_.reset();
                }
            }
        }
    } else if (info_.hasMusicSubunit() && descriptorMechanismSupported_) {
         // Unit open succeeded, attempt to read subunit identifier (as originally planned)
         // This path likely won't be hit for the Duet if Unit OPEN always fails.
         spdlog::debug("DeviceParser [Phase 3]: Unit supports descriptors, attempting Music Subunit Identifier read (Addr 0x{:02x})...",
                      Helpers::getSubunitAddress(SubunitType::Music, info_.musicSubunit_.getId()));
         // ...(Logic to read/parse subunit identifier as before, using accessor with unitSizes)...
          uint8_t musicSubunitAddr = Helpers::getSubunitAddress(SubunitType::Music, info_.musicSubunit_.getId());
          std::vector<uint8_t> subunitIdSpecifier = DescriptorUtils::buildDescriptorSpecifier(
              DescriptorSpecifierType::UnitSubunitIdentifier, 0, 0, 0);
          // ... (rest of the read/parse logic for static capabilities) ...
            std::vector<uint8_t> subunitDescriptorData;
            bool subunitReadOK = false;
            auto openResult = descriptorAccessor.openForRead(musicSubunitAddr, subunitIdSpecifier);
            if (!openResult) {
                spdlog::warn("DeviceParser [Phase 3 - Unit OK path]: Failed to OPEN Music Subunit Identifier (0x{:x}). Cannot read static capabilities.", static_cast<int>(openResult.error()));
            } else {
                auto readResult = descriptorAccessor.read(musicSubunitAddr, subunitIdSpecifier, 0, 0);
                auto closeResult = descriptorAccessor.close(musicSubunitAddr, subunitIdSpecifier);
                if (!closeResult) { /* warn */ }
                if (!readResult) {
                    spdlog::warn("DeviceParser [Phase 3 - Unit OK path]: Failed to READ Music Subunit Identifier (0x{:x}). Cannot read static capabilities.", static_cast<int>(readResult.error()));
                } else {
                    subunitDescriptorData = readResult.value();
                    subunitReadOK = true;
                }
            }
            if (subunitReadOK) {
                 auto parsedResult = parseMusicSubunitIdentifier(subunitDescriptorData,
                                                                 unitSizes.sizeOfListId, unitSizes.sizeOfObjectId, unitSizes.sizeOfEntryPos);
                 if (parsedResult) {
                     auto& [caps, depBlock, specBlock] = parsedResult.value();
                     info_.musicSubunit_.capabilities_ = std::move(caps);
                     // Optionally store the extracted blocks if needed
                     // info_.musicSubunit_.setDependentInfoBlock(std::move(depBlock));
                     // info_.musicSubunit_.setSpecificInfoBlock(std::move(specBlock));
                     // spdlog::debug(" -> Stored Dependent Block ({} bytes), Specific Block ({} bytes)", depBlock.size(), specBlock.size());
                     spdlog::info("DeviceParser [Phase 3 - Unit OK path]: Successfully parsed Music Subunit static capabilities.");
                 } else {
                     spdlog::warn("DeviceParser [Phase 3 - Unit OK path]: Failed to parse static capabilities from Music Subunit Identifier data.");
                     info_.musicSubunit_.capabilities_.reset();
                 }
            } else {
                 info_.musicSubunit_.capabilities_.reset();
            }
    }

    // --- Phase 4: Attempt Music Subunit *Status* Descriptor Read ---
    // Run ONLY if descriptor mechanism was confirmed working (either at unit or subunit level)
    if (info_.hasMusicSubunit()) {
        spdlog::debug("DeviceParser [Phase 4]: Fetching Music Subunit Status Descriptor...");
        bool statusReadSuccess = false;
        std::vector<uint8_t> statusDescriptorData;
        if (!descriptorMechanismSupported_) { // TODO: remove this check if we always support descriptors
            spdlog::debug(" -> Attempting standard read via MusicSubunitDescriptorParser...");
            auto result = musicDescParser.fetchAndParse(info_.musicSubunit_);
            if (result) {
                spdlog::info("DeviceParser [Phase 4]: Successfully parsed Music Subunit Status Descriptor Info Blocks via standard mechanism.");
                statusReadSuccess = true;
            } else {
                 spdlog::warn("DeviceParser [Phase 4 WARNING]: Standard read/parse of Music Subunit Status Descriptor failed: 0x{:x}.", static_cast<int>(result.error()));
            }
        } else {
             spdlog::info("DeviceParser [Phase 4]: Skipping standard Music Subunit Status Descriptor read because Descriptor Mechanism not supported.");
        }
        if (!statusReadSuccess) {
             spdlog::warn("DeviceParser [Phase 4]: Standard Status Descriptor read failed or skipped. Attempting non-standard CONTROL read...");
             uint8_t musicSubunitAddr = Helpers::getSubunitAddress(SubunitType::Music, info_.musicSubunit_.getId());
             std::vector<uint8_t> statusDescSpecifier = {static_cast<uint8_t>(0x80)}; // 0x80 for Music Subunit Status Descriptor
             auto nonStdReadResult = readDescriptorNonStandard(commandInterface_, musicSubunitAddr, statusDescSpecifier, 0, 0);
             if (nonStdReadResult) {
                 statusDescriptorData = nonStdReadResult.value();
                 auto parseResult = musicDescParser.parseMusicSubunitStatusDescriptor(statusDescriptorData, info_.musicSubunit_);
                 if (parseResult) {
                      spdlog::info("DeviceParser [Phase 4]: Successfully parsed Music Subunit Status Descriptor Info Blocks via NON-STANDARD read.");
                      statusReadSuccess = true;
                 } else {
                      spdlog::error("DeviceParser [Phase 4]: Failed to parse data obtained from NON-STANDARD read: 0x{:x}", static_cast<int>(parseResult.error()));
                      info_.musicSubunit_.setStatusDescriptorData(statusDescriptorData);
                 }
             } else {
                 spdlog::error("DeviceParser [Phase 4]: Non-standard read also failed: 0x{:x}. Status info blocks unavailable.", static_cast<int>(nonStdReadResult.error()));
                  info_.musicSubunit_.setStatusDescriptorData({});
             }
        }
    }
    // Add similar logic for Audio Subunit Status Descriptor...

    // --- Phase 5: Parse Plug Details (Always attempt) ---
    spdlog::debug("DeviceParser [Phase 5]: Parsing detailed plug information...");
    if (auto result = parseUnitPlugs(plugDetailParser, info_); !result) { /* warn */ }
    if (auto result = parseSubunitPlugs(plugDetailParser, info_); !result) { /* warn */ }

    spdlog::info("DeviceParser: Capability parsing finished for GUID: 0x{:x}", device_->getGuid());
    return {}; // Return success
}

std::expected<void, IOKitError> DeviceParser::parseUnitPlugs(PlugDetailParser& plugDetailParser, DeviceInfo& info) {
    // ... (Keep existing implementation for now) ...
    // This uses PlugDetailParser, which uses CommandInterface.
    // It might need updates later if plug details depend on parsed descriptor sizes.
    spdlog::debug("Parsing Unit Iso Plugs (In: {}, Out: {})...", info.getNumIsoInputPlugs(), info.getNumIsoOutputPlugs());
    info.isoInputPlugs_.clear();
    info.isoInputPlugs_.reserve(info.getNumIsoInputPlugs());
    for (uint8_t i = 0; i < info.getNumIsoInputPlugs(); ++i) {
        auto plugResult = plugDetailParser.parsePlugDetails(0xFF, i, PlugDirection::Input, PlugUsage::Isochronous);
        if (plugResult) {
            info.isoInputPlugs_.push_back(plugResult.value());
        } else {
            spdlog::warn("Failed to parse details for Iso In plug {}: 0x{:x}", i, static_cast<int>(plugResult.error()));
        }
    }
    info.isoOutputPlugs_.clear();
    info.isoOutputPlugs_.reserve(info.getNumIsoOutputPlugs());
    for (uint8_t i = 0; i < info.getNumIsoOutputPlugs(); ++i) {
         auto plugResult = plugDetailParser.parsePlugDetails(0xFF, i, PlugDirection::Output, PlugUsage::Isochronous);
         if (plugResult) {
             info.isoOutputPlugs_.push_back(plugResult.value());
         } else {
             spdlog::warn("Failed to parse details for Iso Out plug {}: 0x{:x}", i, static_cast<int>(plugResult.error()));
         }
    }
    // ... (parse external plugs similarly) ...
     spdlog::debug("Parsing Unit External Plugs (In: {}, Out: {})...", info.getNumExternalInputPlugs(), info.getNumExternalOutputPlugs());
     info.externalInputPlugs_.clear();
     info.externalInputPlugs_.reserve(info.getNumExternalInputPlugs());
     for (uint8_t i = 0; i < info.getNumExternalInputPlugs(); ++i) {
         uint8_t plugNum = 0x80 + i;
         auto plugResult = plugDetailParser.parsePlugDetails(0xFF, plugNum, PlugDirection::Input, PlugUsage::External);
         if (plugResult) {
             info.externalInputPlugs_.push_back(plugResult.value());
         } else {
             spdlog::warn("Failed to parse details for External In plug {}: 0x{:x}", i, static_cast<int>(plugResult.error()));
         }
     }
     info.externalOutputPlugs_.clear();
     info.externalOutputPlugs_.reserve(info.getNumExternalOutputPlugs());
     for (uint8_t i = 0; i < info.getNumExternalOutputPlugs(); ++i) {
         uint8_t plugNum = 0x80 + i;
         auto plugResult = plugDetailParser.parsePlugDetails(0xFF, plugNum, PlugDirection::Output, PlugUsage::External);
         if (plugResult) {
             info.externalOutputPlugs_.push_back(plugResult.value());
         } else {
             spdlog::warn("Failed to parse details for External Out plug {}: 0x{:x}", i, static_cast<int>(plugResult.error()));
         }
     }
    return {};
}

std::expected<void, IOKitError> DeviceParser::parseSubunitPlugs(PlugDetailParser& plugDetailParser, DeviceInfo& info) {
     // ... (Keep existing implementation for now) ...
     // This uses PlugDetailParser, which uses CommandInterface.
     // It might need updates later if plug details depend on parsed descriptor sizes.
     if (info.hasMusicSubunit()) {
        auto& music = info.musicSubunit_;
        music.clearMusicSourcePlugs();
        music.clearMusicDestPlugs();
        uint8_t musicSubunitAddr = FWA::Helpers::getSubunitAddress(music.getSubunitType(), music.getId());
        for (uint8_t i = 0; i < music.getMusicDestPlugCount(); ++i) {
            auto plugResult = plugDetailParser.parsePlugDetails(musicSubunitAddr, i, PlugDirection::Input, PlugUsage::MusicSubunit);
            if (plugResult) {
                music.addMusicDestPlug(plugResult.value());
            } else {
                spdlog::warn("Failed to parse details for Music Dest plug {}: 0x{:x}", i, static_cast<int>(plugResult.error()));
            }
        }
        for (uint8_t i = 0; i < music.getMusicSourcePlugCount(); ++i) {
             auto plugResult = plugDetailParser.parsePlugDetails(musicSubunitAddr, i, PlugDirection::Output, PlugUsage::MusicSubunit);
             if (plugResult) {
                 music.addMusicSourcePlug(plugResult.value());
             } else {
                 spdlog::warn("Failed to parse details for Music Source plug {}: 0x{:x}", i, static_cast<int>(plugResult.error()));
             }
        }
     }
      if (info.hasAudioSubunit()) {
        // ... (similar logic for audio subunit plugs) ...
        auto& audio = info.audioSubunit_;
        audio.clearAudioSourcePlugs();
        audio.clearAudioDestPlugs();
        uint8_t audioSubunitAddr = FWA::Helpers::getSubunitAddress(audio.getSubunitType(), audio.getId());
         for (uint8_t i = 0; i < audio.getAudioDestPlugCount(); ++i) {
             auto plugResult = plugDetailParser.parsePlugDetails(audioSubunitAddr, i, PlugDirection::Input, PlugUsage::AudioSubunit);
             if (plugResult) {
                 audio.addAudioDestPlug(plugResult.value());
             } else {
                 spdlog::warn("Failed to parse details for Audio Dest plug {}: 0x{:x}", i, static_cast<int>(plugResult.error()));
             }
         }
         for (uint8_t i = 0; i < audio.getAudioSourcePlugCount(); ++i) {
              auto plugResult = plugDetailParser.parsePlugDetails(audioSubunitAddr, i, PlugDirection::Output, PlugUsage::AudioSubunit);
              if (plugResult) {
                  audio.addAudioSourcePlug(plugResult.value());
              } else {
                  spdlog::warn("Failed to parse details for Audio Source plug {}: 0x{:x}", i, static_cast<int>(plugResult.error()));
              }
         }
     }
    return {};
}


} // namespace FWA