#include "runtime/igi_sound_bank.h"

// The retail sound container and sample format. Every rule is cited to IGI-RE
// by HOOK address and file:line; see the header for the layer overview.

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace igi {
namespace soundbank {

namespace {

constexpr std::uint32_t ChunkId(char a, char b, char c, char d) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24);
}

// res/resource.h:40
constexpr std::uint32_t kIlff = ChunkId('I', 'L', 'F', 'F');
constexpr std::uint32_t kIres = ChunkId('I', 'R', 'E', 'S');
constexpr std::uint32_t kName = ChunkId('N', 'A', 'M', 'E');
constexpr std::uint32_t kPath = ChunkId('P', 'A', 'T', 'H');
constexpr std::uint32_t kBody = ChunkId('B', 'O', 'D', 'Y');
constexpr std::uint32_t kDbug = ChunkId('D', 'B', 'U', 'G');
constexpr std::uint32_t kIlsf = ChunkId('I', 'L', 'S', 'F');

bool IsPowerOfTwo(std::uint32_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

// res/resource.h:32-38. The chunk header is 20 bytes and `next` is a byte
// distance from the chunk's own start, not from the form.
struct ChunkHeader {
    std::uint32_t id = 0;
    std::uint32_t size = 0;
    std::uint32_t align = 0;
    std::uint32_t next = 0;
    std::uint32_t form = 0;
};

bool ReadChunk(const std::uint8_t* data, std::size_t size, std::size_t offset,
               ChunkHeader& out) {
    if (offset > size || size - offset < 20) {
        return false;
    }
    std::memcpy(&out, data + offset, 20);
    return true;
}

std::uint32_t ReadU32(const std::uint8_t* data, std::size_t size,
                      std::size_t offset) {
    if (offset > size || size - offset < 4) {
        return 0;
    }
    std::uint32_t value = 0;
    std::memcpy(&value, data + offset, 4);
    return value;
}

// Chunk_FirstChild, HOOK 0x4B7490, res/chunk.c:17-31:
//   off = (c->align + 0x13) & ~(c->align - 1);
//   if (off < c->size) return (Chunk *)((char *)c + off);
// The 0x13 is the 20-byte header rounded up to the alignment, minus one.
std::size_t FirstChildOffset(const ChunkHeader& parent) {
    if (!IsPowerOfTwo(parent.align)) {
        return 0;
    }
    const std::size_t offset =
        (static_cast<std::size_t>(parent.align) + 0x13) &
        ~(static_cast<std::size_t>(parent.align) - 1);
    if (offset >= parent.size) {
        return 0;
    }
    return offset;
}

// Chunk_Next, HOOK 0x4B7470, res/chunk.c:9-15: c + c->next, NULL when zero.
std::size_t NextSiblingOffset(const ChunkHeader& chunk) {
    return chunk.next;
}

// Chunk_Data, HOOK 0x4B74D0, res/chunk.c:34-49:
//   parent + ((c->align + (c - parent) + 0xF) & ~(c->align - 1))
// The engine validates the alignment relationship and returns NULL otherwise;
// the same checks are made here so a malformed archive cannot produce a pointer
// the engine would have refused.
std::size_t ChunkDataOffset(std::size_t parent_offset, const ChunkHeader& parent,
                            std::size_t chunk_offset, const ChunkHeader& chunk) {
    if (!IsPowerOfTwo(parent.align) || !IsPowerOfTwo(chunk.align) ||
        chunk.align > parent.align) {
        return 0;
    }
    const std::size_t relative = chunk_offset - parent_offset;
    if (relative >= parent.size) {
        return 0;
    }
    return parent_offset + ((static_cast<std::size_t>(chunk.align) + relative + 0xF) &
                            ~(static_cast<std::size_t>(chunk.align) - 1));
}

// Resource_Add's "RESOURCE" spelling, HOOK 0x4B5F80, res/resource.c:293-313:
//   count  = ((unsigned int *)data)[2]           -> the word at +8
//   p      = data + 0x20
//   body   = p + ((namelen + 0x2B) & ~0x1F)
//   p_next = body + ((size + 0x1F) & ~0x1F)
// struct PackEntry { int unk0; int size; int namelen; char name[]; }
bool IsResourcePack(const std::uint8_t* data, std::size_t size) {
    return size >= 8 && std::memcmp(data, "RESOURCE", 8) == 0;
}

} // namespace

// ---------------------------------------------------------------------------
// The 20-byte sample header
// ---------------------------------------------------------------------------

SampleHeader ParseSampleHeader(const std::uint8_t* data, std::size_t size) {
    SampleHeader header;
    if (data == nullptr || size < kHeaderSize) {
        return header;
    }

    std::int32_t magic = 0;
    std::memcpy(&magic, data, 4);
    header.magic = magic;
    if (static_cast<std::uint32_t>(magic) != kIlsf) {
        // d3d/sound.c:548: the loader refuses anything else.
        return header;
    }

    std::memcpy(&header.format, data + 0x04, 2);
    std::memcpy(&header.bits, data + 0x06, 2);
    std::memcpy(&header.channels, data + 0x08, 2);
    std::memcpy(&header.unk1a, data + 0x0A, 2);
    std::memcpy(&header.rate, data + 0x0C, 4);
    std::memcpy(&header.samples, data + 0x10, 4);

    // A header the loader would accept but that cannot describe any bytes is
    // reported as invalid rather than silently yielding a zero-size sample.
    if (header.channels <= 0 || header.bits <= 0 || header.rate <= 0 ||
        header.samples < 0) {
        return header;
    }
    header.valid = true;
    return header;
}

std::size_t PayloadSizeForHeader(const SampleHeader& header) {
    if (!header.valid) {
        return 0;
    }
    const std::size_t channels = static_cast<std::size_t>(header.channels);
    const std::size_t samples = static_cast<std::size_t>(header.samples);

    switch (header.Format()) {
        case SampleFormat::Pcm:
            // d3d/sound.c:579 and :602: channels * bits * samples / 8.
            return samples * channels * static_cast<std::size_t>(header.bits) / 8;
        case SampleFormat::StreamedPcm:
        case SampleFormat::StreamedAdpcm:
            // voice.c:480 `bytes = ch * frames / 2`, plus the mid-byte byte
            // (voice.c:482-487). Ceiling division by 2 of the sample count.
            return (samples * channels + 1) / 2;
        case SampleFormat::Adpcm:
            return 0; // not established; see PayloadSizeEstablished
    }
    return 0;
}

bool PayloadSizeEstablished(const SampleHeader& header) {
    if (!header.valid) {
        return false;
    }
    switch (header.Format()) {
        case SampleFormat::Pcm:
        case SampleFormat::StreamedPcm:
        case SampleFormat::StreamedAdpcm:
            return true;
        case SampleFormat::Adpcm:
            // d3d/sound.c:586 passes `res + 5` to Adpcm_Decode without deriving
            // a byte count from the header at all, so the payload length of a
            // format-3 member is whatever the pack stored. The shipped
            // format-3 data is consistent with ceil(samples*channels/2) plus up
            // to 3 bytes of pad, but that is a measurement of one archive, not
            // a rule read out of the engine - so it is reported, not predicted.
            return false;
    }
    return false;
}

std::size_t DecodedPcmSize(const SampleHeader& header) {
    if (!header.valid) {
        return 0;
    }
    return static_cast<std::size_t>(header.samples) *
           static_cast<std::size_t>(header.channels) *
           static_cast<std::size_t>(header.bits) / 8;
}

// ---------------------------------------------------------------------------
// IMA ADPCM - Adpcm_Decode, HOOK 0x496280, transctx/voice.c:98-148
// ---------------------------------------------------------------------------

namespace {

// voice.c:739-740, at 0x543920 and 0x543930. Both are the standard IMA tables:
// the index table is {-1,-1,-1,-1,2,4,6,8} repeated, and the step table holds
// the 89 canonical IMA steps 7..32767 followed by data the decoder can never
// reach because voice.c:124-125 clamps the index to 0..88.
const signed char kAdpcmIndexTable[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

const short kAdpcmStepTable[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,
    19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
    337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
    876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
    5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

} // namespace

void DecodeImaAdpcm(const std::uint8_t* src, std::size_t src_size,
                    int channels, int frames, std::uint8_t* out) {
    if (out == nullptr || frames <= 0) {
        return;
    }
    if (channels <= 0) {
        channels = 1;
    }
    if (channels > 2) {
        // AdpcmState holds two channels (voice.c:35-39); the engine's own
        // decoder would read past its state for anything else.
        channels = 2;
    }

    const std::size_t out_bytes =
        static_cast<std::size_t>(frames) * static_cast<std::size_t>(channels) * 2;
    std::memset(out, 0, out_bytes);
    if (src == nullptr || src_size == 0) {
        return;
    }

    // Adpcm_Init, voice.c:150-158.
    int sample[2] = {0, 0};
    int index[2] = {0, 0};
    int step[2] = {kAdpcmStepTable[0], kAdpcmStepTable[0]};
    int odd = 0;       // voice.c:110-111 `st->odd`; 0 means the high nibble is next
    unsigned byte = 0; // voice.c:101
    std::size_t out_position = 0;

    // The body below is voice.c:112-142 with the engine's two `for` headers
    // flattened and `*dst++` replaced by an index into `out`. The nibble/byte
    // bookkeeping is kept verbatim: `odd` is toggled on every SAMPLE
    // (voice.c:120), a byte is fetched only when the high nibble is due
    // (voice.c:117), and `step[ch]` is refreshed at the END of each sample
    // (voice.c:139) - so the difference for one sample uses the step the
    // previous sample's index produced, which is one sample behind.
    //
    // The difference formula is the engine's and is NOT the standard IMA one:
    // voice.c:128-133 adds the FULL step for nibble bit 2, where textbook IMA
    // adds step >> 2. A nibble of 4 therefore moves by 1.125 * step, not
    // 0.25 * step. tests/test_igi_sound_bank.cpp pins the whole table.
    for (int frame = 0; frame < frames; ++frame) {
        for (int channel = 0; channel < channels; ++channel) {
            int nibble;
            if (odd != 0) {
                nibble = static_cast<int>(byte & 0xF);
            } else {
                // The byte this frame needs. For one channel that is
                // frame / 2 (two samples per byte); for two interleaved
                // channels it is frame, because each frame consumes one whole
                // byte, one nibble per channel.
                const std::size_t byte_index =
                    static_cast<std::size_t>(frame) *
                    static_cast<std::size_t>(channels) / 2;
                if (byte_index >= src_size) {
                    // The engine reads ahead of the frames it decodes
                    // (voice.c:490 reads `ch * frames / 2` bytes for `frames`
                    // frames), so a short source means silence for the
                    // remainder rather than a read past the buffer.
                    out_position += 2;
                    continue;
                }
                byte = src[byte_index];
                nibble = static_cast<int>((byte >> 4) & 0xF);
            }
            odd = !odd;

            index[channel] += kAdpcmIndexTable[nibble];
            if (index[channel] < 0) {
                index[channel] = 0;
            } else if (index[channel] > 88) {
                index[channel] = 88;
            }
            const int current_step = step[channel];
            int difference = current_step >> 3;
            if ((nibble & 4) != 0) {
                difference += current_step;
            }
            if ((nibble & 2) != 0) {
                difference += current_step >> 1;
            }
            if ((nibble & 1) != 0) {
                difference += current_step >> 2;
            }
            sample[channel] = (nibble & 8) != 0 ? sample[channel] - difference
                                                : sample[channel] + difference;
            if (sample[channel] < -0x8000) {
                sample[channel] = -0x8000;
            } else if (sample[channel] > 0x7FFF) {
                sample[channel] = 0x7FFF;
            }
            step[channel] = kAdpcmStepTable[index[channel]];

            // Little-endian 16-bit, which is what the engine's `short` write
            // becomes in the DirectSound buffer (voice.c:140).
            out[out_position] = static_cast<std::uint8_t>(sample[channel] & 0xFF);
            out[out_position + 1] =
                static_cast<std::uint8_t>((sample[channel] >> 8) & 0xFF);
            out_position += 2;
        }
    }
}

// ---------------------------------------------------------------------------
// Pack scanning
// ---------------------------------------------------------------------------

namespace {

// One NAME/BODY pair, or a lone chunk the engine's Resource_AddChunks would
// reject. Returns false only on a structurally impossible archive.
struct MemberWalk {
    bool ok = false;
    std::string error;
    std::vector<PackEntry> entries;
};

std::string ReadChunkString(const std::uint8_t* data, std::size_t size,
                            std::size_t offset, std::size_t max_length) {
    if (offset >= size) {
        return {};
    }
    const std::size_t available = size - offset;
    const std::size_t limit = available < max_length ? available : max_length;
    const void* terminator = std::memchr(data + offset, 0, limit);
    const std::size_t length = terminator != nullptr
        ? static_cast<std::size_t>(static_cast<const std::uint8_t*>(terminator) - (data + offset))
        : limit;
    return std::string(reinterpret_cast<const char*>(data + offset), length);
}

// Resource_AddChunks, HOOK 0x4B60F0, res/resource.c:328-375. Walks the
// children of an IRES form. A NAME is followed by its BODY (or by a nested
// IRES form, which the engine also records as a member with size 0); the
// pairs are what the sound loader iterates.
MemberWalk WalkFormMembers(const std::uint8_t* data, std::size_t size,
                           std::size_t form_offset, const ChunkHeader& form) {
    MemberWalk walk;
    const std::size_t first = FirstChildOffset(form);
    if (first == 0) {
        walk.ok = true; // an empty form is not an error
        return walk;
    }

    std::size_t offset = form_offset + first;
    while (offset < form_offset + form.size) {
        ChunkHeader chunk;
        if (!ReadChunk(data, size, offset, chunk)) {
            walk.error = "truncated chunk header";
            return walk;
        }

        if (chunk.id == kDbug) {
            // res/resource.c:365: a DBUG chunk is skipped.
            if (NextSiblingOffset(chunk) == 0) {
                break;
            }
            offset += chunk.next;
            continue;
        }

        if (chunk.id != kName) {
            walk.error = "unexpected chunk in an IRES form";
            return walk;
        }

        const std::size_t name_data = ChunkDataOffset(form_offset, form, offset, chunk);
        if (name_data == 0) {
            walk.error = "NAME chunk has no data";
            return walk;
        }
        const std::string name = ReadChunkString(data, size, name_data, chunk.size);

        const std::size_t next_offset = NextSiblingOffset(chunk);
        if (next_offset == 0) {
            walk.error = "NAME chunk is not followed by a chunk";
            return walk;
        }
        const std::size_t body_offset = offset + next_offset;
        ChunkHeader body;
        if (!ReadChunk(data, size, body_offset, body)) {
            walk.error = "truncated chunk after NAME";
            return walk;
        }

        PackEntry entry;
        entry.name = name;

        if (body.id == kBody) {
            const std::size_t body_data = ChunkDataOffset(form_offset, form, body_offset, body);
            if (body_data == 0) {
                walk.error = "BODY chunk has no data";
                return walk;
            }
            // Resource_AddChunks records the BODY's size (resource.c:349-350).
            entry.kind = ResourceKind::Body;
            entry.data_offset = body_data;
            entry.data_size = body.size;
            entry.body_size = body.size;
        } else if (body.id == kPath) {
            // resource.c:359-361: a PATH member carries the ';' separated
            // member list that Resource_ForEach walks. Both shipped sound packs
            // end with one, named after the directory ("LOCAL:common/sounds"),
            // and it is NOT a sample: Sound_LoadResource only reaches BODY
            // members, and the PATH list is what points it at them.
            const std::size_t body_data = ChunkDataOffset(form_offset, form, body_offset, body);
            if (body_data == 0) {
                walk.error = "PATH chunk has no data";
                return walk;
            }
            entry.kind = ResourceKind::Path;
            entry.data_offset = body_data;
            entry.data_size = body.size;
            entry.body_size = 0;
        } else if (body.id == kIlff) {
            // A nested IRES form is recorded as a size-0 member and then
            // recursed into (resource.c:351-355). Neither shipped sound pack
            // contains one, so this branch is here to keep the walk faithful to
            // the reader rather than to describe real data.
            entry.data_offset = 0;
            entry.data_size = 0;
            entry.body_size = 0;
            walk.entries.push_back(entry);
            if (body.form == kIres) {
                MemberWalk nested = WalkFormMembers(data, size, body_offset, body);
                if (!nested.ok) {
                    return nested;
                }
                for (PackEntry& child : nested.entries) {
                    walk.entries.push_back(std::move(child));
                }
            }
            const std::size_t nested_next = NextSiblingOffset(body);
            if (nested_next == 0) {
                break;
            }
            offset = body_offset + nested_next;
            continue;
        } else {
            // res/resource.c:356-358 also accepts a CSTR chunk, whose role is
            // not established here. It is refused with a named reason instead of
            // being given a guessed meaning, so a caller that meets one learns
            // which chunk it was.
            char id[5] = {0, 0, 0, 0, 0};
            std::memcpy(id, &body.id, 4);
            walk.error = std::string("unsupported chunk '") + id +
                         "' after NAME; only BODY members are handled";
            return walk;
        }

        // Parse the sample header out of the member's own bytes. The BODY data
        // for a sound member starts with it (d3d/sound.c:547 reads the resource
        // and d3d/sound.c:548 checks the first word). A PATH member is a name
        // list, never a sample, so it is left without a header.
        if (entry.kind == ResourceKind::Body && entry.data_offset != 0 &&
            entry.data_size >= kHeaderSize && entry.data_offset <= size &&
            entry.data_size <= size - entry.data_offset) {
            entry.header = ParseSampleHeader(data + entry.data_offset, entry.data_size);
        }

        walk.entries.push_back(std::move(entry));

        const std::size_t body_next = NextSiblingOffset(body);
        if (body_next == 0) {
            break;
        }
        offset = body_offset + body_next;
    }

    walk.ok = true;
    return walk;
}

} // namespace

PackScanResult ScanPack(const std::uint8_t* data, std::size_t size) {
    PackScanResult result;
    if (data == nullptr || size < 20) {
        result.error = "archive is smaller than a chunk header";
        return result;
    }

    // res/resource.c:287: the ILFF/IRES spelling is tried first, and the
    // "RESOURCE" spelling second (:293).
    ChunkHeader root;
    if (!ReadChunk(data, size, 0, root)) {
        result.error = "archive is smaller than a chunk header";
        return result;
    }

    if (root.id == kIlff && root.form == kIres) {
        MemberWalk walk = WalkFormMembers(data, size, 0, root);
        if (!walk.ok) {
            result.error = std::move(walk.error);
            return result;
        }
        result.ilff = true;
        result.entries = std::move(walk.entries);
        result.ok = true;
        return result;
    }

    if (IsResourcePack(data, size)) {
        const std::uint32_t count = ReadU32(data, size, 8);
        std::size_t offset = 0x20;
        for (std::uint32_t index = 0; index < count; ++index) {
            if (offset > size || size - offset < 12) {
                result.error = "truncated RESOURCE pack entry header";
                return result;
            }
            const std::int32_t member_size = static_cast<std::int32_t>(ReadU32(data, size, offset + 4));
            const std::int32_t name_length = static_cast<std::int32_t>(ReadU32(data, size, offset + 8));
            if (member_size < 0 || name_length < 0) {
                result.error = "malformed RESOURCE pack entry";
                return result;
            }
            PackEntry entry;
            entry.name = ReadChunkString(data, size, offset + 12,
                                         static_cast<std::size_t>(name_length));
            entry.data_offset =
                offset + ((static_cast<std::size_t>(name_length) + 0x2B) & ~static_cast<std::size_t>(0x1F));
            entry.data_size = static_cast<std::size_t>(member_size);
            entry.body_size = entry.data_size;
            if (entry.data_offset > size || entry.data_size > size - entry.data_offset) {
                result.error = "RESOURCE pack member lies outside the file";
                return result;
            }
            if (entry.data_size >= kHeaderSize) {
                entry.header = ParseSampleHeader(data + entry.data_offset, entry.data_size);
            }
            result.entries.push_back(std::move(entry));
            offset = entry.data_offset +
                     ((static_cast<std::size_t>(member_size) + 0x1F) & ~static_cast<std::size_t>(0x1F));
        }
        result.ilff = false;
        result.ok = true;
        return result;
    }

    result.error = "not an ILFF/IRES archive or a RESOURCE pack";
    return result;
}

PackScanResult ScanPackFile(const std::string& path) {
    PackScanResult result;
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        result.error = "could not open " + path;
        return result;
    }
    std::vector<std::uint8_t> bytes(
        (std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        result.error = "empty archive " + path;
        return result;
    }
    return ScanPack(bytes.data(), bytes.size());
}

// ---------------------------------------------------------------------------
// Payload + transcode
// ---------------------------------------------------------------------------

std::vector<std::uint8_t> ReadEntryPayload(const std::uint8_t* archive_data,
                                           std::size_t archive_size,
                                           const PackEntry& entry) {
    std::vector<std::uint8_t> payload;
    if (archive_data == nullptr || entry.kind != ResourceKind::Body ||
        !entry.header.valid) {
        return payload;
    }
    // The header lives inside the member; the payload follows it.
    const std::size_t payload_offset = entry.data_offset + kHeaderSize;
    if (payload_offset >= archive_size || payload_offset < entry.data_offset) {
        return payload;
    }
    const std::size_t declared = entry.data_size > kHeaderSize
        ? entry.data_size - kHeaderSize
        : 0;
    const std::size_t available = archive_size - payload_offset;
    const std::size_t length = declared < available ? declared : available;
    payload.assign(archive_data + payload_offset, archive_data + payload_offset + length);
    return payload;
}

std::vector<std::string> SplitPathList(const std::uint8_t* archive_data,
                                       std::size_t archive_size,
                                       const PackEntry& entry) {
    std::vector<std::string> names;
    if (archive_data == nullptr || entry.kind != ResourceKind::Path) {
        return names;
    }
    if (entry.data_offset >= archive_size) {
        return names;
    }
    const std::size_t available = archive_size - entry.data_offset;
    const std::size_t length = entry.data_size < available ? entry.data_size : available;

    // Resource_ForEach, HOOK 0x4B5D80, resource.c:199-215: split on ';', skip
    // empty runs, stop at the terminating NUL. The engine copies each name into
    // a 512-byte buffer (resource.c:189) with no length check, so a longer name
    // would overflow it there; such a name is dropped here rather than
    // truncated silently into a different resource name.
    std::size_t start = 0;
    std::size_t position = 0;
    while (position < length) {
        const std::uint8_t byte = archive_data[entry.data_offset + position];
        if (byte == 0) {
            break;
        }
        if (byte == ';') {
            if (position > start && position - start < 512) {
                names.emplace_back(
                    reinterpret_cast<const char*>(archive_data + entry.data_offset + start),
                    position - start);
            }
            start = position + 1;
        }
        ++position;
    }
    if (position > start && position - start < 512) {
        names.emplace_back(
            reinterpret_cast<const char*>(archive_data + entry.data_offset + start),
            position - start);
    }
    return names;
}

namespace {

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

} // namespace

bool TranscodeToRiffWav(const std::uint8_t* data, std::size_t size,
                        std::vector<std::uint8_t>& out, std::size_t& pcm_size,
                        std::string& reason) {
    out.clear();
    pcm_size = 0;
    reason.clear();

    const SampleHeader header = ParseSampleHeader(data, size);
    if (!header.valid) {
        reason = "not an ILSF sample";
        return false;
    }
    if (!PayloadSizeEstablished(header)) {
        reason = "payload layout for format " + std::to_string(header.format) +
                 " is not established by the decompilation";
        return false;
    }
    if ((header.bits != 8 && header.bits != 16) ||
        (header.channels != 1 && header.channels != 2)) {
        reason = "unsupported sample shape";
        return false;
    }

    const std::size_t payload_offset = kHeaderSize;
    if (size <= payload_offset) {
        reason = "sample has no payload";
        return false;
    }
    const std::uint8_t* payload = data + payload_offset;
    const std::size_t payload_size = size - payload_offset;

    const std::size_t decoded_size = DecodedPcmSize(header);
    std::vector<std::uint8_t> pcm(decoded_size, 0);

    switch (header.Format()) {
        case SampleFormat::Pcm:
        case SampleFormat::StreamedPcm: {
            // Raw PCM. The engine reads `samples * blockAlign` bytes
            // (voice.c:504 for the streamed path, d3d/sound.c:602 for the
            // whole-buffer path); anything the pack padded past that is not
            // part of the sample.
            const std::size_t copy = payload_size < decoded_size ? payload_size : decoded_size;
            if (copy != 0) {
                std::memcpy(pcm.data(), payload, copy);
            }
            break;
        }
        case SampleFormat::StreamedAdpcm: {
            DecodeImaAdpcm(payload, payload_size, header.channels, header.samples,
                           pcm.data());
            break;
        }
        case SampleFormat::Adpcm:
            reason = "unreachable";
            return false;
    }

    // Canonical 44-byte RIFF/WAVE PCM header. fmt tag 1 and PCM are what the
    // engine itself asks DirectSound for (d3d/sound.c:590-596), so the
    // transcoded file describes exactly the buffer the engine would have built.
    const std::size_t block_align =
        static_cast<std::size_t>(header.channels) * static_cast<std::size_t>(header.bits) / 8;
    const std::uint32_t byte_rate =
        static_cast<std::uint32_t>(block_align * static_cast<std::size_t>(header.rate));

    out.reserve(44 + pcm.size());
    AppendU32(out, ChunkId('R', 'I', 'F', 'F'));
    AppendU32(out, static_cast<std::uint32_t>(36 + pcm.size()));
    AppendU32(out, ChunkId('W', 'A', 'V', 'E'));
    AppendU32(out, ChunkId('f', 'm', 't', ' '));
    AppendU32(out, 16);
    AppendU16(out, 1); // WAVE_FORMAT_PCM
    AppendU16(out, static_cast<std::uint16_t>(header.channels));
    AppendU32(out, static_cast<std::uint32_t>(header.rate));
    AppendU32(out, byte_rate);
    AppendU16(out, static_cast<std::uint16_t>(block_align));
    AppendU16(out, static_cast<std::uint16_t>(header.bits));
    AppendU32(out, ChunkId('d', 'a', 't', 'a'));
    AppendU32(out, static_cast<std::uint32_t>(pcm.size()));
    out.insert(out.end(), pcm.begin(), pcm.end());

    pcm_size = pcm.size();
    return true;
}

} // namespace soundbank
} // namespace igi
