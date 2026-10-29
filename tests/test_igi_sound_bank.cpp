// test_igi_sound_bank.cpp - the retail sound container and sample format (#70).
//
// The oracle for every number here is IGI-RE (read-only). Key citations:
//   res/resource.h:32-38   the 20-byte chunk header (id,size,align,next,form)
//   res/resource.h:7-13    RES_PACK/MEMBER/FILE/USER/PATH
//   chunk.c:9-15           Chunk_Next: c + c->next
//   chunk.c:17-31          Chunk_FirstChild: (align + 0x13) & ~(align-1)
//   chunk.c:34-49          Chunk_Data: parent + ((align + off + 0xF) & ~(align-1))
//   resource.c:287         the ILFF/IRES spelling is tried first
//   resource.c:293-313     the "RESOURCE" record-list spelling
//   resource.c:328-375     Resource_AddChunks: NAME + BODY / nested IRES / CSTR / PATH
//   resource.c:185-217     Resource_ForEach: the ';' separated PATH list
//   transctxtypes.h:46-60  the Sound struct: magic, format, bits, channels, rate, samples
//   d3d/sound.c:535-621    Sound_Load: the ILSF check, the format switch, the buffer size
//   voice.c:98-148         Adpcm_Decode, the decoder ported here
//   voice.c:150-158        Adpcm_Init
//   voice.c:329-400        Voice_Create: the 0x14 seek and the streamed formats
//   voice.c:406-507        Voice_Fill / Voice_ReadAdpcm / Voice_ReadPcm
//   voice.c:739-740        gAdpcmIndexTable / gAdpcmStepTable at 0x543920/0x543930
//
// The corpus half reads the shipped COMMON/SOUNDS/SOUNDS.RES and
// menusystem/SOUND/SOUNDS.RES and pins the member counts, the header fields, the
// payload arithmetic and the PATH member. It skips (GTEST_SKIP) when
// IGI_GAME_PATH is absent or unpopulated.

#include "../source/runtime/igi_sound_bank.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using igi::soundbank::PackEntry;
using igi::soundbank::PackScanResult;
using igi::soundbank::ResourceKind;
using igi::soundbank::SampleFormat;
using igi::soundbank::SampleHeader;

fs::path GameRoot() {
    if (const char* env = std::getenv("IGI_GAME_PATH"); env != nullptr && *env != '\0')
        return fs::path(env);
    return fs::path("/Users/haseeb-mir/Downloads/IGI");
}

// The retail tree spells the same directory several ways; resolve the first
// candidate that exists so the suite runs on a case-sensitive filesystem too.
fs::path ExistingPath(const std::vector<fs::path>& candidates) {
    std::error_code error_code;
    for (const fs::path& candidate : candidates) {
        if (fs::is_regular_file(candidate, error_code))
            return candidate;
        error_code.clear();
    }
    return {};
}

std::vector<std::uint8_t> ReadFileBytes(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::vector<std::uint8_t>(
        (std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

void AppendU16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void AppendU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

// A synthetic ILSF sample: 20-byte header (d3d/sound.c:563 copies 0x14 bytes)
// followed by `payload`.
std::vector<std::uint8_t> MakeIlsf(std::int16_t format, std::int16_t bits,
                                   std::int16_t channels, std::int16_t unk1a,
                                   std::int32_t rate, std::int32_t samples,
                                   const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> out;
    out.push_back('I');
    out.push_back('L');
    out.push_back('S');
    out.push_back('F');
    AppendU16(out, static_cast<std::uint16_t>(format));
    AppendU16(out, static_cast<std::uint16_t>(bits));
    AppendU16(out, static_cast<std::uint16_t>(channels));
    AppendU16(out, static_cast<std::uint16_t>(unk1a));
    AppendU32(out, static_cast<std::uint32_t>(rate));
    AppendU32(out, static_cast<std::uint32_t>(samples));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::int16_t PcmAt(const std::vector<std::uint8_t>& pcm, std::size_t index) {
    return static_cast<std::int16_t>(
        static_cast<std::uint16_t>(pcm[index * 2]) |
        (static_cast<std::uint16_t>(pcm[index * 2 + 1]) << 8));
}

// A minimal ILFF/IRES archive built the way the engine reads one: a 20-byte
// root form header, then NAME/BODY pairs at 4-byte alignment.
std::vector<std::uint8_t> MakePack(
    const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& members) {
    std::vector<std::uint8_t> out;
    AppendU32(out, 0x46464C49u);  // 'ILFF'
    AppendU32(out, 0);            // size, patched below
    AppendU32(out, 4);            // align
    AppendU32(out, 0);            // next
    AppendU32(out, 0x53455249u);  // 'IRES'

    for (const auto& [name, body] : members) {
        // NAME chunk: header, then the data at (align + off + 0xF) & ~(align-1).
        const std::size_t name_chunk = out.size();
        const std::size_t name_data =
            (4 + name_chunk + 0xF) & ~static_cast<std::size_t>(3);
        AppendU32(out, 0x454D414Eu);  // 'NAME'
        AppendU32(out, static_cast<std::uint32_t>(name.size() + 1));
        AppendU32(out, 4);
        const std::size_t name_next_placeholder = out.size();
        AppendU32(out, 0);  // next, patched
        AppendU32(out, 0);  // form
        out.resize(name_data, 0);
        out.insert(out.end(), name.begin(), name.end());
        out.push_back(0);

        const std::size_t body_chunk = out.size();
        const std::size_t body_data =
            (4 + body_chunk + 0xF) & ~static_cast<std::size_t>(3);
        AppendU32(out, 0x59444F42u);  // 'BODY'
        AppendU32(out, static_cast<std::uint32_t>(body.size()));
        AppendU32(out, 4);
        const std::size_t body_next_placeholder = out.size();
        AppendU32(out, 0);  // next, patched
        AppendU32(out, 0);  // form
        out.resize(body_data, 0);
        out.insert(out.end(), body.begin(), body.end());

        // next of NAME = distance to the BODY chunk.
        const std::uint32_t name_next =
            static_cast<std::uint32_t>(body_chunk - name_chunk);
        std::memcpy(out.data() + name_next_placeholder, &name_next, 4);
        (void)body_next_placeholder;  // the last member keeps next = 0
    }

    const std::uint32_t total = static_cast<std::uint32_t>(out.size());
    std::memcpy(out.data() + 4, &total, 4);
    return out;
}

// ---------------------------------------------------------------------------
// The 20-byte header
// ---------------------------------------------------------------------------

TEST(SoundBankHeaderTest, RejectsAnythingWithoutTheIlsfMagic) {
    // d3d/sound.c:548: `if (res[0] != 0x46534C49)` -> "not valid format".
    std::vector<std::uint8_t> riff = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A',
                                      'V', 'E', 'f', 'm', 't', ' ', 0, 0, 0, 0};
    const SampleHeader header =
        igi::soundbank::ParseSampleHeader(riff.data(), riff.size());
    EXPECT_FALSE(header.valid);
    EXPECT_EQ(header.magic, 0x46464952);  // 'RIFF' little-endian
}

TEST(SoundBankHeaderTest, RejectsAShortBufferEvenWithTheMagic) {
    std::vector<std::uint8_t> short_sample = {'I', 'L', 'S', 'F', 1, 0, 16, 0};
    const SampleHeader header =
        igi::soundbank::ParseSampleHeader(short_sample.data(), short_sample.size());
    EXPECT_FALSE(header.valid);
    EXPECT_EQ(short_sample.size(), 8u);
    EXPECT_LT(short_sample.size(), igi::soundbank::kHeaderSize);
}

TEST(SoundBankHeaderTest, RejectsAHeaderThatCannotDescribeBytes) {
    // channels 0, bits 0 and rate 0 are all accepted by the field copy but
    // cannot produce a DirectSound buffer (d3d/sound.c:592-602).
    for (const std::int16_t channels : {0, -1}) {
        const std::vector<std::uint8_t> sample =
            MakeIlsf(1, 16, channels, 0, 22050, 100, std::vector<std::uint8_t>(200, 0));
        EXPECT_FALSE(igi::soundbank::ParseSampleHeader(sample.data(), sample.size()).valid)
            << "channels=" << channels;
    }
    for (const std::int16_t bits : {0, -16}) {
        const std::vector<std::uint8_t> sample =
            MakeIlsf(1, bits, 1, 0, 22050, 100, std::vector<std::uint8_t>(200, 0));
        EXPECT_FALSE(igi::soundbank::ParseSampleHeader(sample.data(), sample.size()).valid)
            << "bits=" << bits;
    }
}

TEST(SoundBankHeaderTest, ReadsEveryFieldAtTheOffsetTheEngineUses) {
    // transctxtypes.h:46-60 field order; offsets 0x00,0x04,0x06,0x08,0x0A,0x0C,0x10.
    const std::vector<std::uint8_t> sample =
        MakeIlsf(2, 16, 2, 0x1002, 44100, 1234567, std::vector<uint8_t>(8, 0));
    const SampleHeader header =
        igi::soundbank::ParseSampleHeader(sample.data(), sample.size());
    ASSERT_TRUE(header.valid);
    EXPECT_EQ(header.magic, 0x46534C49);
    EXPECT_EQ(header.format, 2);
    EXPECT_EQ(header.bits, 16);
    EXPECT_EQ(header.channels, 2);
    EXPECT_EQ(header.unk1a, 0x1002);  // raw: purpose not established
    EXPECT_EQ(header.rate, 44100);
    EXPECT_EQ(header.samples, 1234567);
    EXPECT_EQ(header.Format(), SampleFormat::StreamedAdpcm);
}

// ---------------------------------------------------------------------------
// Payload arithmetic
// ---------------------------------------------------------------------------

TEST(SoundBankPayloadTest, PcmPayloadIsSamplesTimesChannelsTimesBitsOverEight) {
    // d3d/sound.c:579 allocates (channels * bits * samples) >> 3 and
    // d3d/sound.c:602 sizes the buffer samples * blockAlign.
    const std::vector<std::uint8_t> sample =
        MakeIlsf(1, 16, 2, 0, 22050, 26016, std::vector<std::uint8_t>(8, 0));
    const SampleHeader parsed =
        igi::soundbank::ParseSampleHeader(sample.data(), sample.size());
    ASSERT_TRUE(parsed.valid);
    EXPECT_EQ(igi::soundbank::PayloadSizeForHeader(parsed), 26016u * 2u * 2u);
    EXPECT_TRUE(igi::soundbank::PayloadSizeEstablished(parsed));
    EXPECT_EQ(igi::soundbank::DecodedPcmSize(parsed), 26016u * 2u * 2u);
}

TEST(SoundBankPayloadTest, StreamedPayloadIsCeilSamplesTimesChannelsOverTwo) {
    // voice.c:480 `bytes = ch * frames / 2` plus the mid-byte byte
    // (voice.c:482-487). Odd frame counts and stereo both round up.
    struct Case {
        std::int16_t format;
        std::int16_t channels;
        std::int32_t samples;
        std::size_t expected;
    };
    const Case cases[] = {
        {2, 1, 18342, 9171},   // even mono -> samples/2
        {2, 1, 45723, 22862},  // odd mono -> (samples+1)/2
        {2, 2, 200241, 200241},// stereo -> samples
        {0, 1, 483324, 241662},
        {0, 2, 363551, 363551},
    };
    for (const Case& test_case : cases) {
        const std::vector<std::uint8_t> sample = MakeIlsf(
            test_case.format, 16, test_case.channels, 0, 22050, test_case.samples, {});
        const SampleHeader header =
            igi::soundbank::ParseSampleHeader(sample.data(), sample.size());
        ASSERT_TRUE(header.valid);
        EXPECT_TRUE(igi::soundbank::PayloadSizeEstablished(header));
        EXPECT_EQ(igi::soundbank::PayloadSizeForHeader(header), test_case.expected)
            << "format=" << test_case.format << " channels=" << test_case.channels
            << " samples=" << test_case.samples;
    }
}

TEST(SoundBankPayloadTest, FormatThreePayloadLayoutIsReportedAsUnestablished) {
    // d3d/sound.c:586 hands `res + 5` to Adpcm_Decode without deriving a byte
    // count from the header, so the payload length of a format-3 member is not
    // established by the decompilation.
    const std::vector<std::uint8_t> sample =
        MakeIlsf(3, 16, 1, 0x1002, 22050, 2840, std::vector<std::uint8_t>(64, 0));
    const SampleHeader header =
        igi::soundbank::ParseSampleHeader(sample.data(), sample.size());
    ASSERT_TRUE(header.valid);
    EXPECT_FALSE(igi::soundbank::PayloadSizeEstablished(header));
    EXPECT_EQ(igi::soundbank::PayloadSizeForHeader(header), 0u);
    // The DECODED size is established, and is what d3d/sound.c:579 allocates.
    EXPECT_EQ(igi::soundbank::DecodedPcmSize(header), 2840u * 1u * 2u);
}

// ---------------------------------------------------------------------------
// IMA ADPCM - hand-computed against the tables at voice.c:739-740
// ---------------------------------------------------------------------------

TEST(SoundBankAdpcmTest, DecodesHighNibbleFirstThenTheLowNibble) {
    // step = kAdpcmStepTable[0] = 7, index = 0 (Adpcm_Init, voice.c:150-158).
    //
    //   0x10 -> high nibble 1: index += -1 -> 0 (clamped, voice.c:122-123)
    //           diff = 7>>3 = 0; nibble&1 -> +7>>2 = 1; sample = 0 + 1 = 1
    //   0x10 -> low  nibble 0: index += -1 -> 0; diff = 0; sample = 1
    //   0x00 -> high nibble 0: index += -1 -> 0; diff = 0; sample = 1
    //   0x00 -> low  nibble 0: index += -1 -> 0; diff = 0; sample = 1
    const std::vector<std::uint8_t> encoded = {0x10, 0x00};
    std::vector<std::uint8_t> pcm(4 * 2, 0xAA);
    igi::soundbank::DecodeImaAdpcm(encoded.data(), encoded.size(), 1, 4, pcm.data());
    EXPECT_EQ(PcmAt(pcm, 0), 1);
    EXPECT_EQ(PcmAt(pcm, 1), 1);
    EXPECT_EQ(PcmAt(pcm, 2), 1);
    EXPECT_EQ(PcmAt(pcm, 3), 1);
}

TEST(SoundBankAdpcmTest, TheDifferenceFormulaIsTheEnginesNotTheStandardIma) {
    // voice.c:126-133, verbatim:
    //     s = step[ch];
    //     diff = s >> 3;
    //     if (nib & 4) diff += s;        <-- the FULL step, not s >> 2
    //     if (nib & 2) diff += s >> 1;
    //     if (nib & 1) diff += s >> 2;
    //
    // The standard IMA predictor uses `s >> 3` plus `s >> 2` for bit 2. The
    // engine adds the whole step for bit 2, so a nibble of 4 moves by 1.125 *
    // step where IMA would move by 0.25 * step. This test exists so the port
    // cannot be "corrected" to the textbook formula: it is the engine's
    // arithmetic, and the retail samples decode through it.
    //
    // At index 0 the step is 7, giving this table for nibbles 0..15:
    const int expected[16] = {0, 1, 3, 4, 7, 8, 10, 11, 0, 1, 3, 4, 7, 8, 10, 11};
    for (int nibble = 0; nibble < 16; ++nibble) {
        // One byte whose high nibble is `nibble`; only sample 0 is checked, and
        // every nibble leaves index[0] at 0 because kAdpcmIndexTable is
        // -1 for 0..3 and 8 and +2 for 4..7, and +2 clamps back to 0 at the
        // next -1. So each case starts from the same state.
        const std::vector<std::uint8_t> encoded = {
            static_cast<std::uint8_t>((nibble << 4) | 0x0)};
        std::vector<std::uint8_t> pcm(1 * 2, 0);
        igi::soundbank::DecodeImaAdpcm(encoded.data(), encoded.size(), 1, 1, pcm.data());
        const int step = 7;
        int difference = step >> 3;
        if ((nibble & 4) != 0) difference += step;
        if ((nibble & 2) != 0) difference += step >> 1;
        if ((nibble & 1) != 0) difference += step >> 2;
        const int sign = (nibble & 8) != 0 ? -1 : 1;
        EXPECT_EQ(PcmAt(pcm, 0), sign * difference) << "nibble " << nibble;
        EXPECT_EQ(difference, expected[nibble]) << "nibble " << nibble;
    }
}

TEST(SoundBankAdpcmTest, WalksTheStepTableOnPositiveNibbles) {
    // A positive nibble moves index by +2 (kAdpcmIndexTable[4..7] = 2,4,6,8),
    // so the step grows and the increments grow with it.
    //
    // kAdpcmStepTable is 7,8,9,10,11,12,13,14,16,... at indices 0,1,2,...
    //
    // The ORDER inside one sample matters (voice.c:121-139): the index is
    // updated and clamped first, the step is looked up from the NEW index, and
    // only then is the difference computed from that step.
    //
    //   sample 0: nib 4 -> index 0+2=2, step  9, diff (9>>3=1, +9=10),  sample 7
    //   sample 1: nib 4 -> index 2+2=4, step 12, diff (12>>3=1, +12=13), sample 17
    //   sample 2: nib 4 -> index 4+2=6, step 14, diff (14>>3=1, +14=15), sample 29
    //   sample 3: nib 4 -> index 6+2=8, step 16, diff (16>>3=2, +16=18), sample 43
    const std::vector<std::uint8_t> encoded = {0x44, 0x44};
    std::vector<std::uint8_t> pcm(4 * 2, 0);
    igi::soundbank::DecodeImaAdpcm(encoded.data(), encoded.size(), 1, 4, pcm.data());
    EXPECT_EQ(PcmAt(pcm, 0), 7);
    EXPECT_EQ(PcmAt(pcm, 1), 17);
    EXPECT_EQ(PcmAt(pcm, 2), 29);
    EXPECT_EQ(PcmAt(pcm, 3), 43);
}

TEST(SoundBankAdpcmTest, TheStepIsRefreshedAtTheEndOfEachSample) {
    // 0xF8 is high nibble F and LOW nibble 8: two samples out of one byte.
    // The step starts at kAdpcmStepTable[0] = 7 and index[0] at 0.
    //
    //   nibble F (0b1111): index += kAdpcmIndexTable[15] = +8 -> 8.
    //     The difference uses the step CURRENTLY in the register, 7:
    //     diff = (7>>3=0) + (bit2: +7) + (bit1: +3) + (bit0: +1) = 11;
    //     sign set -> sample = 0 - 11 = -11.
    //     Then voice.c:139 refreshes step = kAdpcmStepTable[8] = 16.
    //   nibble 8 (0b1000): index += kAdpcmIndexTable[8] = -1 -> 7.
    //     diff uses the step from the previous sample, 16: 16>>3 = 2;
    //     sign set -> sample = -11 - 2 = -13.
    //
    // So the index update and the step refresh are one sample apart, and the
    // second sample's magnitude comes from the FIRST sample's index. Both
    // numbers below are what a literal transcription of voice.c:98-148
    // produces, not a derivation from the tables.
    const std::vector<std::uint8_t> encoded = {0xF8};
    std::vector<std::uint8_t> pcm(2 * 2, 0);
    igi::soundbank::DecodeImaAdpcm(encoded.data(), encoded.size(), 1, 2, pcm.data());
    EXPECT_EQ(PcmAt(pcm, 0), -11);
    EXPECT_EQ(PcmAt(pcm, 1), -13);
}

TEST(SoundBankAdpcmTest, OneBytePerFrameForStereoNibblePerChannel) {
    // voice.c:480-487: bytes = ch * frames / 2, so a stereo frame consumes one
    // whole byte - the high nibble for channel 0 and the low nibble for
    // channel 1 (voice.c:112-121).
    //
    //   byte 0x19: high 1 -> channel 0: diff 0 + (7>>2)=1 -> +1
    //              low  9 -> channel 1: sign set; index += -1 -> 0; diff 0+1 -> -1
    const std::vector<std::uint8_t> encoded = {0x19};
    std::vector<std::uint8_t> pcm(1 * 2 * 2, 0);
    igi::soundbank::DecodeImaAdpcm(encoded.data(), encoded.size(), 2, 1, pcm.data());
    EXPECT_EQ(PcmAt(pcm, 0), 1);
    EXPECT_EQ(PcmAt(pcm, 1), -1);
}

TEST(SoundBankAdpcmTest, AStereoFrameCountIsFramesNotSamples) {
    // d3d/sound.c:579 allocates channels * bits * samples / 8 for `samples`
    // frames, so `samples` counts frames and the decoded output is
    // frames * channels samples.
    const std::vector<std::uint8_t> encoded = {0x00, 0x00, 0x00};
    std::vector<std::uint8_t> pcm(3 * 2 * 2, 0);
    igi::soundbank::DecodeImaAdpcm(encoded.data(), encoded.size(), 2, 3, pcm.data());
    // Three frames of two channels: each byte carries one frame, and the two
    // channels carry independent index/sample state (voice.c:100-109), so both
    // start at index 0 and each frame's two samples are equal.
    //   every nibble is 0 -> index stays 0, step stays 7, and the difference
    //   for nibble 0 is 7>>3 = 0 (voice.c:127-133 adds nothing for a zero
    //   nibble), so the output is silence.
    EXPECT_EQ(PcmAt(pcm, 0), 0);
    EXPECT_EQ(PcmAt(pcm, 1), 0);
    EXPECT_EQ(PcmAt(pcm, 2), 0);
    EXPECT_EQ(PcmAt(pcm, 3), 0);
    EXPECT_EQ(PcmAt(pcm, 4), 0);
    EXPECT_EQ(PcmAt(pcm, 5), 0);
}

TEST(SoundBankAdpcmTest, ShortInputLeavesSilenceRatherThanReadingPastIt) {
    // The engine reads ahead of the frames it decodes (voice.c:490 reads
    // ch*frames/2 bytes for `frames` frames), so a truncated stream decodes
    // silence for the remainder.
    const std::vector<std::uint8_t> encoded = {0x00};
    std::vector<std::uint8_t> pcm(4 * 2, 0x7F);
    igi::soundbank::DecodeImaAdpcm(encoded.data(), encoded.size(), 1, 4, pcm.data());
    EXPECT_EQ(PcmAt(pcm, 0), 0);  // high nibble 0 of the one byte that exists
    EXPECT_EQ(PcmAt(pcm, 1), 0);  // its low nibble, still from that byte
    EXPECT_EQ(PcmAt(pcm, 2), 0);  // past the input: silence, not a stale byte
    EXPECT_EQ(PcmAt(pcm, 3), 0);
}

// ---------------------------------------------------------------------------
// The pack walker
// ---------------------------------------------------------------------------

TEST(SoundBankPackTest, WalksNameBodyPairsAtTheEngineAlignment) {
    const std::vector<std::uint8_t> body = MakeIlsf(
        1, 16, 1, 0, 22050, 4, std::vector<std::uint8_t>(8, 0));
    const std::vector<std::uint8_t> pack = MakePack({{"LOCAL:common/sounds/a.wav", body}});

    const PackScanResult result = igi::soundbank::ScanPack(pack.data(), pack.size());
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_TRUE(result.ilff);
    ASSERT_EQ(result.entries.size(), 1u);
    EXPECT_EQ(result.entries[0].name, "LOCAL:common/sounds/a.wav");
    EXPECT_EQ(result.entries[0].kind, ResourceKind::Body);
    EXPECT_EQ(result.entries[0].data_size, body.size());
    ASSERT_TRUE(result.entries[0].header.valid);
    EXPECT_EQ(result.entries[0].header.samples, 4);
}

TEST(SoundBankPackTest, RejectsAnArchiveThatIsNeitherSpelling) {
    const std::vector<std::uint8_t> garbage(64, 0x5A);
    const PackScanResult result =
        igi::soundbank::ScanPack(garbage.data(), garbage.size());
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.empty());
}

TEST(SoundBankPackTest, RejectsATruncatedArchiveWithoutReadingPastIt) {
    const std::vector<std::uint8_t> body = MakeIlsf(
        1, 16, 1, 0, 22050, 4, std::vector<std::uint8_t>(8, 0));
    std::vector<std::uint8_t> pack = MakePack({{"LOCAL:common/sounds/a.wav", body}});
    pack.resize(40);  // cut inside the first NAME chunk
    const PackScanResult result = igi::soundbank::ScanPack(pack.data(), pack.size());
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.empty());
}

TEST(SoundBankPackTest, ReadsTheResourcePackSpellingToo) {
    // resource.c:293-313: "RESOURCE", count at +8, records from +0x20, each a
    // 12-byte header + name, body at align32(12 + namelen), next record at
    // align32(body + size).
    const std::vector<std::uint8_t> body = MakeIlsf(
        1, 16, 1, 0, 22050, 4, std::vector<std::uint8_t>(8, 0));
    const std::string name = "LOCAL:common/sounds/b.wav";

    std::vector<std::uint8_t> pack;
    pack.insert(pack.end(), {'R', 'E', 'S', 'O', 'U', 'R', 'C', 'E'});
    AppendU32(pack, 0);          // +8: patched to the member count below
    AppendU32(pack, 0);          // +12: unread
    pack.resize(0x20, 0);        // records start at +0x20 (resource.c:295)
    AppendU32(pack, 0);          // unk0
    AppendU32(pack, static_cast<std::uint32_t>(body.size()));
    AppendU32(pack, static_cast<std::uint32_t>(name.size() + 1));
    pack.insert(pack.end(), name.begin(), name.end());
    pack.push_back(0);
    const std::size_t body_offset =
        ((name.size() + 1) + 0x2B) & ~static_cast<std::size_t>(0x1F);
    pack.resize(0x20 + body_offset, 0);
    pack.insert(pack.end(), body.begin(), body.end());

    // resource.c:299 reads the count from +8 (`((unsigned int *)data)[2]`).
    const std::uint32_t member_count = 1;
    std::memcpy(pack.data() + 8, &member_count, 4);

    const PackScanResult result = igi::soundbank::ScanPack(pack.data(), pack.size());
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_FALSE(result.ilff);
    ASSERT_EQ(result.entries.size(), 1u);
    EXPECT_EQ(result.entries[0].name, name);
    ASSERT_TRUE(result.entries[0].header.valid);
    EXPECT_EQ(result.entries[0].header.rate, 22050);
}

// ---------------------------------------------------------------------------
// Transcoding
// ---------------------------------------------------------------------------

TEST(SoundBankTranscodeTest, FormatOneBecomesARiffWavWithTheEnginesPcmShape) {
    // d3d/sound.c:590-596 asks DirectSound for WAVE_FORMAT_PCM with the
    // header's channels/rate/bits, so the transcoded file describes exactly
    // that buffer.
    const std::vector<std::uint8_t> payload = {1, 2, 3, 4, 5, 6, 7, 8};
    const std::vector<std::uint8_t> sample =
        MakeIlsf(1, 16, 1, 0x1002, 22050, 4, payload);

    std::vector<std::uint8_t> wav;
    std::size_t pcm_size = 0;
    std::string reason;
    ASSERT_TRUE(igi::soundbank::TranscodeToRiffWav(
        sample.data(), sample.size(), wav, pcm_size, reason)) << reason;

    EXPECT_EQ(pcm_size, 8u);
    ASSERT_EQ(wav.size(), 44u + 8u);
    EXPECT_EQ(std::string(wav.begin(), wav.begin() + 4), "RIFF");
    EXPECT_EQ(std::string(wav.begin() + 8, wav.begin() + 12), "WAVE");
    EXPECT_EQ(std::string(wav.begin() + 12, wav.begin() + 16), "fmt ");
    EXPECT_EQ(std::string(wav.begin() + 36, wav.begin() + 40), "data");
    const auto read_u16 = [&wav](std::size_t offset) {
        return static_cast<std::uint16_t>(wav[offset] | (wav[offset + 1] << 8));
    };
    const auto read_u32 = [&wav](std::size_t offset) {
        return static_cast<std::uint32_t>(
            wav[offset] | (wav[offset + 1] << 8) | (wav[offset + 2] << 16) |
            (wav[offset + 3] << 24));
    };
    EXPECT_EQ(read_u32(4), 36u + 8u);       // RIFF size
    EXPECT_EQ(read_u32(16), 16u);           // fmt chunk size
    EXPECT_EQ(read_u16(20), 1u);            // WAVE_FORMAT_PCM
    EXPECT_EQ(read_u16(22), 1u);            // channels
    EXPECT_EQ(read_u32(24), 22050u);        // rate
    EXPECT_EQ(read_u32(28), 22050u * 2u);   // byte rate
    EXPECT_EQ(read_u16(32), 2u);            // block align
    EXPECT_EQ(read_u16(34), 16u);           // bits
    EXPECT_EQ(read_u32(40), 8u);            // data size
    EXPECT_EQ(std::vector<std::uint8_t>(wav.begin() + 44, wav.end()), payload);
}

TEST(SoundBankTranscodeTest, FormatTwoIsDecodedBeforeItBecomesPcm) {
    // 0x44 0x44 -> 7, 17, 29, 43 (see the ADPCM test above).
    const std::vector<std::uint8_t> sample =
        MakeIlsf(2, 16, 1, 0, 22050, 4, {0x44, 0x44});
    std::vector<std::uint8_t> wav;
    std::size_t pcm_size = 0;
    std::string reason;
    ASSERT_TRUE(igi::soundbank::TranscodeToRiffWav(
        sample.data(), sample.size(), wav, pcm_size, reason)) << reason;

    EXPECT_EQ(pcm_size, 8u);  // 4 frames * 1 channel * 2 bytes
    ASSERT_EQ(wav.size(), 52u);
    const std::vector<std::uint8_t> pcm(wav.begin() + 44, wav.end());
    EXPECT_EQ(PcmAt(pcm, 0), 7);
    EXPECT_EQ(PcmAt(pcm, 1), 17);
    EXPECT_EQ(PcmAt(pcm, 2), 29);
    EXPECT_EQ(PcmAt(pcm, 3), 43);
}

TEST(SoundBankTranscodeTest, RefusesFormatThreeWithANamedReason) {
    const std::vector<std::uint8_t> sample =
        MakeIlsf(3, 16, 1, 0x1002, 22050, 64, std::vector<std::uint8_t>(64, 0));
    std::vector<std::uint8_t> wav;
    std::size_t pcm_size = 0;
    std::string reason;
    EXPECT_FALSE(igi::soundbank::TranscodeToRiffWav(
        sample.data(), sample.size(), wav, pcm_size, reason));
    EXPECT_NE(reason.find("format 3"), std::string::npos) << reason;
    EXPECT_TRUE(wav.empty());
    EXPECT_EQ(pcm_size, 0u);
}

TEST(SoundBankTranscodeTest, RefusesBytesThatAreNotAnIlsfSample) {
    const std::vector<std::uint8_t> riff(64, 0x11);
    std::vector<std::uint8_t> wav;
    std::size_t pcm_size = 0;
    std::string reason;
    EXPECT_FALSE(igi::soundbank::TranscodeToRiffWav(
        riff.data(), riff.size(), wav, pcm_size, reason));
    EXPECT_FALSE(reason.empty());
}

// ---------------------------------------------------------------------------
// The shipped corpus
// ---------------------------------------------------------------------------

class SoundBankCorpusTest : public ::testing::Test {
protected:
    void SetUp() override {
        common_res_ = ExistingPath({
            GameRoot() / "COMMON" / "SOUNDS" / "SOUNDS.RES",
            GameRoot() / "common" / "sounds" / "sounds.res",
        });
        menu_res_ = ExistingPath({
            GameRoot() / "menusystem" / "SOUND" / "SOUNDS.RES",
            GameRoot() / "menusystem" / "sound" / "sounds.res",
        });
        if (common_res_.empty()) {
            GTEST_SKIP() << "no shipped COMMON/SOUNDS/SOUNDS.RES under "
                         << GameRoot().string();
        }
        common_bytes_ = ReadFileBytes(common_res_);
        ASSERT_FALSE(common_bytes_.empty());
        common_ = igi::soundbank::ScanPack(common_bytes_.data(), common_bytes_.size());
        ASSERT_TRUE(common_.ok) << common_.error;

        if (!menu_res_.empty()) {
            menu_bytes_ = ReadFileBytes(menu_res_);
            menu_ = igi::soundbank::ScanPack(menu_bytes_.data(), menu_bytes_.size());
        }
    }

    std::size_t CountKind(const PackScanResult& pack, ResourceKind kind) const {
        return static_cast<std::size_t>(std::count_if(
            pack.entries.begin(), pack.entries.end(),
            [kind](const PackEntry& entry) { return entry.kind == kind; }));
    }

    fs::path common_res_;
    fs::path menu_res_;
    std::vector<std::uint8_t> common_bytes_;
    std::vector<std::uint8_t> menu_bytes_;
    PackScanResult common_;
    PackScanResult menu_;
};

TEST_F(SoundBankCorpusTest, CommonPackHoldsThreeHundredThirtyThreeSamplesAndOnePath) {
    // Measured from the shipped archive. The engine's own reader produces the
    // same split: Sound_LoadResource iterates the members and Sound_Load
    // (d3d/sound.c:548) rejects everything that is not ILSF - which is the
    // PATH member and nothing else.
    EXPECT_EQ(common_.entries.size(), 334u);
    EXPECT_EQ(CountKind(common_, ResourceKind::Body), 333u);
    EXPECT_EQ(CountKind(common_, ResourceKind::Path), 1u);

    std::size_t with_header = 0;
    for (const PackEntry& entry : common_.entries) {
        if (entry.header.valid) ++with_header;
    }
    EXPECT_EQ(with_header, 333u);
}

TEST_F(SoundBankCorpusTest, CommonSamplesAreAllRawPcmSixteenBit) {
    // d3d/sound.c:564 `switch (s->format)`: case 1 is the in-memory PCM path.
    std::size_t mono = 0;
    std::size_t stereo = 0;
    for (const PackEntry& entry : common_.entries) {
        if (entry.kind != ResourceKind::Body) continue;
        ASSERT_TRUE(entry.header.valid) << entry.name;
        EXPECT_EQ(entry.header.Format(), SampleFormat::Pcm) << entry.name;
        EXPECT_EQ(entry.header.bits, 16) << entry.name;
        EXPECT_GT(entry.header.rate, 0) << entry.name;
        EXPECT_GT(entry.header.samples, 0) << entry.name;
        if (entry.header.channels == 1) ++mono;
        if (entry.header.channels == 2) ++stereo;
    }
    EXPECT_EQ(mono, 331u);
    EXPECT_EQ(stereo, 2u);
}

TEST_F(SoundBankCorpusTest, EveryCommonPayloadIsExactlyTheEnginesFormula) {
    // The rule this pins: for format 1 the payload is
    // samples * channels * bits / 8 (d3d/sound.c:579, :602), with no padding.
    std::size_t checked = 0;
    for (const PackEntry& entry : common_.entries) {
        if (entry.kind != ResourceKind::Body) continue;
        ASSERT_TRUE(entry.header.valid) << entry.name;
        const std::size_t expected = igi::soundbank::PayloadSizeForHeader(entry.header);
        const std::size_t actual = entry.data_size - igi::soundbank::kHeaderSize;
        EXPECT_EQ(actual, expected) << entry.name;
        ++checked;
    }
    EXPECT_EQ(checked, 333u);
}

TEST_F(SoundBankCorpusTest, EveryCommonSampleTranscodesToAWellFormedRiffWav) {
    std::size_t converted = 0;
    std::size_t total_pcm = 0;
    for (const PackEntry& entry : common_.entries) {
        if (entry.kind != ResourceKind::Body) continue;
        const std::size_t length = entry.data_size;
        ASSERT_LE(entry.data_offset + length, common_bytes_.size()) << entry.name;
        std::vector<std::uint8_t> wav;
        std::size_t pcm_size = 0;
        std::string reason;
        ASSERT_TRUE(igi::soundbank::TranscodeToRiffWav(
            common_bytes_.data() + entry.data_offset, length, wav, pcm_size, reason))
            << entry.name << ": " << reason;
        EXPECT_EQ(pcm_size, igi::soundbank::DecodedPcmSize(entry.header)) << entry.name;
        ASSERT_EQ(wav.size(), 44u + pcm_size) << entry.name;
        EXPECT_EQ(std::string(wav.begin(), wav.begin() + 4), "RIFF") << entry.name;
        EXPECT_EQ(std::string(wav.begin() + 8, wav.begin() + 12), "WAVE") << entry.name;
        ++converted;
        total_pcm += pcm_size;
    }
    EXPECT_EQ(converted, 333u);
    // The whole pack decodes to 15,039,330 bytes of PCM. Pinned so a future
    // change to the decoder or the walker cannot shift it unnoticed.
    EXPECT_EQ(total_pcm, 15039330u);
}

TEST_F(SoundBankCorpusTest, ThePathMemberListsEverySampleInThePack) {
    // resource.c:185-217: the PATH member's data is a ';' separated list of the
    // pack's member names. Sound_LoadResource reaches the samples through it.
    std::vector<std::string> path_names;
    for (const PackEntry& entry : common_.entries) {
        if (entry.kind != ResourceKind::Path) continue;
        EXPECT_EQ(entry.name, "LOCAL:common/sounds");
        EXPECT_FALSE(entry.header.valid);
        path_names = igi::soundbank::SplitPathList(
            common_bytes_.data(), common_bytes_.size(), entry);
    }
    ASSERT_EQ(path_names.size(), 333u);
    EXPECT_EQ(path_names.front(), "LOCAL:common/sounds/wire_drag_1.wav");

    // The list and the BODY members are the same set, in the same order.
    std::vector<std::string> body_names;
    for (const PackEntry& entry : common_.entries) {
        if (entry.kind == ResourceKind::Body) body_names.push_back(entry.name);
    }
    EXPECT_EQ(path_names, body_names);
}

TEST_F(SoundBankCorpusTest, TheFirstCommonSampleIsWireDragOneAtTheShippedShape) {
    // A single pinned member, so an offset change cannot pass by shifting every
    // count by one.
    ASSERT_FALSE(common_.entries.empty());
    const PackEntry& first = common_.entries.front();
    EXPECT_EQ(first.name, "LOCAL:common/sounds/wire_drag_1.wav");
    EXPECT_EQ(first.kind, ResourceKind::Body);
    ASSERT_TRUE(first.header.valid);
    EXPECT_EQ(first.header.magic, 0x46534C49);
    EXPECT_EQ(first.header.format, 1);
    EXPECT_EQ(first.header.bits, 16);
    EXPECT_EQ(first.header.channels, 1);
    EXPECT_EQ(first.header.unk1a, 0x1002);  // raw: purpose not established
    EXPECT_EQ(first.header.rate, 22050);
    EXPECT_EQ(first.header.samples, 26016);
    EXPECT_EQ(first.body_size, 52052u);
    EXPECT_EQ(first.body_size - igi::soundbank::kHeaderSize, 52032u);
    EXPECT_EQ(first.body_size - igi::soundbank::kHeaderSize,
              igi::soundbank::PayloadSizeForHeader(first.header));
}

TEST_F(SoundBankCorpusTest, MenuPackMixesPcmAndFormatThreeAdpcm) {
    if (menu_.entries.empty()) {
        GTEST_SKIP() << "no shipped menusystem/SOUND/SOUNDS.RES";
    }
    ASSERT_TRUE(menu_.ok) << menu_.error;
    EXPECT_EQ(menu_.entries.size(), 14u);
    EXPECT_EQ(CountKind(menu_, ResourceKind::Body), 13u);
    EXPECT_EQ(CountKind(menu_, ResourceKind::Path), 1u);

    std::size_t pcm = 0;
    std::size_t adpcm = 0;
    for (const PackEntry& entry : menu_.entries) {
        if (entry.kind != ResourceKind::Body) continue;
        ASSERT_TRUE(entry.header.valid) << entry.name;
        if (entry.header.Format() == SampleFormat::Pcm) {
            ++pcm;
            EXPECT_EQ(entry.data_size - igi::soundbank::kHeaderSize,
                      igi::soundbank::PayloadSizeForHeader(entry.header))
                << entry.name;
        } else if (entry.header.Format() == SampleFormat::Adpcm) {
            ++adpcm;
            // The payload layout for format 3 is NOT established (see the
            // header). What is established is that the engine never reads it
            // from the header (d3d/sound.c:586), so this module must not claim
            // to predict it either.
            EXPECT_FALSE(igi::soundbank::PayloadSizeEstablished(entry.header))
                << entry.name;
            EXPECT_EQ(entry.header.unk1a, 0x1002) << entry.name;
        } else {
            ADD_FAILURE() << entry.name << " has unexpected format "
                          << entry.header.format;
        }
    }
    EXPECT_EQ(pcm, 5u);
    EXPECT_EQ(adpcm, 8u);
}

TEST_F(SoundBankCorpusTest, NoCommonSampleLiesOutsideItsArchive) {
    for (const PackEntry& entry : common_.entries) {
        EXPECT_LE(entry.data_offset, common_bytes_.size()) << entry.name;
        EXPECT_LE(entry.data_offset + entry.data_size, common_bytes_.size())
            << entry.name;
    }
}

} // namespace
