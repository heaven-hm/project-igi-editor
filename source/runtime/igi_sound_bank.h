#pragma once

// igi_sound_bank.h - the retail sound container and sample format of Project
// IGI 1, established from IGI-RE (the decompiled retail source, read-only) and
// proven against the shipped data.
//
// THE ORACLE. Every rule below is cited to a HOOK address and a file:line in
// /Users/haseeb-mir/Documents/Code/IGI-RE. Nothing here is taken from open-igi
// or from web research, and no field is named that the engine's own reader does
// not name. The two fields whose purpose the decompilation does not establish
// (`unk1A` and format code 3's payload layout) are reported as raw numbers and
// deliberately left uninterpreted - see igi_sound_bank.cpp.
//
// WHAT THIS IS, AND WHAT IT IS NOT
//
//   * This IS the engine's container/sample layer: the pack index, the member
//     record, the 20-byte sample header, and the IMA ADPCM decoder.
//   * This is NOT a playback device. The retail engine feeds DirectSound; this
//     host has no DirectSound and cannot run the GUI (GL 4.1 needed, host has
//     2.1), so nothing here proves a sound is *audible*. `TranscodeToRiffWav`
//     exists so that a plain host API (`PlaySoundA`/MCI `waveaudio`) can consume
//     the shipped samples - the format conversion is oracle-backed, the output
//     device is not.
//
// THE THREE LAYERS, top to bottom
//
//   1. The pack. Every shipped sound archive is one ILFF form of type "IRES"
//      (resource.c:287, HOOK 0x4B5F80). Members are NAME/BODY chunk pairs;
//      Resource_AddChunks (HOOK 0x4B60F0, resource.c:328-375) is the reader.
//      The second, rarer pack spelling is the "RESOURCE" record list
//      (resource.c:293-313) - handled here too, because the same function
//      handles both and a shipped archive may use either.
//   2. The member. A member's bytes are a sample file with a 20-byte header,
//      magic "ILSF" (d3d/sound.c:548 rejects any other first word).
//   3. The sample. Format 1 is raw PCM, 0 and 2 are streamed, 3 is ADPCM
//      decoded at load (transctxtypes.h:51-56, d3d/sound.c:564-588).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace igi {
namespace soundbank {

// ---------------------------------------------------------------------------
// The 20-byte sample header ("ILSF")
// ---------------------------------------------------------------------------

// Field layout and offsets, cited:
//
//   0x00  int   magic     "ILSF" (0x46534C49); d3d/sound.c:548 rejects any
//                         other first word ("SOUND - File %s not valid format!")
//   0x04  short format    transctxtypes.h:51 - 0/2 streamed, 1 PCM, 3 ADPCM
//   0x06  short bits      transctxtypes.h:52
//   0x08  short channels  transctxtypes.h:53
//   0x0A  short unk1A     transctxtypes.h:54 - purpose NOT established. The
//                         engine copies it and never reads it (d3d/sound.c:563
//                         copies 0x14 bytes into Sound; the only reads of the
//                         struct are format/bits/channels/rate/samples). Kept
//                         as a raw number; NOT interpreted.
//   0x0C  int   rate      transctxtypes.h:55 - Hz
//   0x10  int   samples   transctxtypes.h:56 - FRAMES, not interleaved values:
//                         d3d/sound.c:579 allocates channels*bits*samples/8 for
//                         the decoded output and :602 sizes the DirectSound
//                         buffer samples*blockAlign
//   0x14        payload
//
// `kHeaderSize` is 0x14 because the engine's own reader seeks there:
// voice.c:372 (Voice_Create) and voice.c:453 (the loop rewind) both
// File_Seek(v->file, 0x14, 0), and d3d/sound.c:576 sets the in-memory data
// pointer to the resource base plus 5 words.
constexpr std::size_t kHeaderSize = 0x14;

// Format codes. d3d/sound.c:564-588 switches on exactly these; 0 and 2 are
// never given an in-memory buffer there because they are streamed from the
// file by Voice_Create instead (voice.c:349-387).
enum class SampleFormat : std::int16_t {
    StreamedPcm = 0,   // raw PCM, streamed (voice.c:443 Voice_ReadPcm)
    Pcm = 1,           // raw PCM, whole buffer (d3d/sound.c:576)
    StreamedAdpcm = 2, // 4-bit IMA ADPCM, streamed (voice.c:440-441)
    Adpcm = 3,         // ADPCM, decoded at load (d3d/sound.c:578-587)
};

struct SampleHeader {
    bool valid = false;
    std::int32_t magic = 0;
    std::int16_t format = 0;
    std::int16_t bits = 0;
    std::int16_t channels = 0;
    std::int16_t unk1a = 0;  // purpose unestablished; raw
    std::int32_t rate = 0;
    std::int32_t samples = 0;  // frames per channel

    SampleFormat Format() const {
        return static_cast<SampleFormat>(format);
    }
};

// Reads the 20-byte header. `valid` is true only for a recognised "ILSF" magic
// with a payload byte count the caller can actually back - pass `available`.
SampleHeader ParseSampleHeader(const std::uint8_t* data, std::size_t size);

// The payload byte count the engine's reader implies for a header.
//
//   format 1  : samples * channels * bits / 8     (d3d/sound.c:579, :602)
//   formats 0 and 2 : ceil(samples * channels / 2)
//       4 bits per sample, two samples per byte: voice.c:480 computes
//       `bytes = ch * frames / 2` for a whole number of frames, and
//       voice.c:482-487 adds one byte when the frame count or the read
//       position lands mid-byte. Verified against all 669 shipped format-2
//       files and all 6 format-0 files: every one is exactly this size, with
//       no padding (scratch/ evidence in tests/test_igi_sound_bank.cpp).
//
// Returns 0 for a header whose size cannot be derived (see
// `PayloadSizeEstablished`).
std::size_t PayloadSizeForHeader(const SampleHeader& header);

// False for format 3, whose payload layout the decompilation does not
// establish: d3d/sound.c:586 hands `res + 5` straight to Adpcm_Decode without
// ever computing a byte count from it, and the only shipped format-3 data
// (menusystem/SOUND/SOUNDS.RES) is a per-entry byte count that this module
// reports rather than predicts. Treating an unestablished layout as if it were
// known is exactly the kind of guess this module refuses to make.
bool PayloadSizeEstablished(const SampleHeader& header);

// The decoded PCM byte count for a header: channels * bits/8 * samples.
// d3d/sound.c:579 allocates exactly this for format 3.
std::size_t DecodedPcmSize(const SampleHeader& header);

// ---------------------------------------------------------------------------
// IMA ADPCM
// ---------------------------------------------------------------------------

// Decodes `frames` frames per channel from a 4-bit IMA ADPCM stream into
// interleaved little-endian 16-bit PCM, reproducing Adpcm_Decode
// (HOOK 0x496280, transctx/voice.c:98-148) and Adpcm_Init (HOOK 0x496420,
// voice.c:150-158).
//
// `out` must hold at least frames * channels * 2 bytes and is zeroed first, so
// a short or absent input leaves silence rather than stale bytes.
//
// Two details are load-bearing and both are pinned by tests:
//   * The HIGH nibble of a byte is decoded first (voice.c:118).
//   * A frame ending mid-byte leaves the low nibble for the NEXT frame, and the
//     decoder is stateful across calls (voice.c:110-120 `st->odd`). The engine
//     relies on this when it rereads a byte (voice.c:492-495); this function
//     does the same thing by carrying `odd` in its local state, so decoding a
//     stream in one call and in many calls agree.
void DecodeImaAdpcm(const std::uint8_t* src, std::size_t src_size,
                    int channels, int frames, std::uint8_t* out);

// ---------------------------------------------------------------------------
// The pack ("ILFF"/"IRES", and the "RESOURCE" record list)
// ---------------------------------------------------------------------------

// res/resource.h:7-13. Which of Resource_AddChunks' four member spellings this
// entry is. Only `Body` carries sample bytes.
enum class ResourceKind {
    Body, // an ILFF "BODY" chunk - the sample bytes (resource.c:348-350)
    Path, // an ILFF "PATH" chunk - a ';' separated list of member names
          // (resource.c:359-361), read by Resource_ForEach, HOOK 0x4B5D80
};

struct PackEntry {
    // The engine's resource name, verbatim: upper-cased with '/' separators by
    // Resource_Create (HOOK 0x4B59F0, resource.c:65-104) when the engine loads
    // it. Here it is the shipped spelling, e.g.
    // "LOCAL:common/sounds/wire_drag_1.wav".
    std::string name;

    ResourceKind kind = ResourceKind::Body;

    // The member's bytes, exactly as Resource_Load/Resource_ForEach hand them
    // to the engine, i.e. the BODY or PATH chunk's data - which for a sound
    // member STARTS with the ILSF header.
    std::size_t data_offset = 0;
    std::size_t data_size = 0;

    // The member's own sample header, parsed from `data_offset`. `header.valid`
    // is false for a PATH member, and for any BODY whose bytes are not an ILSF
    // sample.
    SampleHeader header;

    // The BODY chunk's declared size, i.e. header plus payload as the pack
    // stores it. For a sound member this is `data_size`; 0 for a PATH member.
    std::size_t body_size = 0;
};

struct PackScanResult {
    bool ok = false;
    bool ilff = false;          // true: "ILFF"/"IRES" form; false: "RESOURCE" list
    std::string error;          // set when !ok
    std::vector<PackEntry> entries;
};

// Indexes a pack. Reads the whole archive into memory: the shipped sound packs
// are 1.7 MB and 15 MB, and the format's member chain is a linked walk, so a
// streaming reader would have to re-seek per chunk anyway.
//
// A malformed archive sets `ok = false` and an `error`; it never partially
// succeeds silently, and it never reads outside the buffer.
PackScanResult ScanPack(const std::uint8_t* data, std::size_t size);
PackScanResult ScanPackFile(const std::string& path);

// ---------------------------------------------------------------------------
// Transcoding to a plain host-playable RIFF/WAVE
// ---------------------------------------------------------------------------

// Converts one ILSF sample to a canonical RIFF/WAVE PCM file.
//
// `out` receives a 44-byte RIFF header (fmt tag 1, PCM) followed by the decoded
// samples. `pcm_size` receives the decoded byte count. Returns false and fills
// `reason` when the sample cannot be converted - notably format 3, whose
// payload layout is not established.
//
// The conversion is format work and is oracle-backed (the header fields and the
// ADPCM decoder are the engine's own); the resulting file being *playable* by a
// host device is not something this module can prove, and it is not claimed.
bool TranscodeToRiffWav(const std::uint8_t* data, std::size_t size,
                        std::vector<std::uint8_t>& out, std::size_t& pcm_size,
                        std::string& reason);

// ---------------------------------------------------------------------------
// Reading a pack member's payload out of an already-scanned archive
// ---------------------------------------------------------------------------

// The payload (the bytes after the 20-byte header) of one entry, clamped to
// what the archive actually holds. Empty for a PATH member or a BODY that is
// not a sample.
std::vector<std::uint8_t> ReadEntryPayload(const std::uint8_t* archive_data,
                                           std::size_t archive_size,
                                           const PackEntry& entry);

// Splits a PATH member's data the way Resource_ForEach does
// (HOOK 0x4B5D80, resource.c:185-217): ';' separated, no trimming, and a
// trailing ';' yields no empty final name. An empty name is skipped because
// Resource_IsLoaded would not match one.
std::vector<std::string> SplitPathList(const std::uint8_t* archive_data,
                                       std::size_t archive_size,
                                       const PackEntry& entry);

} // namespace soundbank
} // namespace igi
