#include "ppef.h"

namespace ppef {

inline uint32_t floor_log2_u64(uint64_t x) {
#if defined(_MSC_VER) && !defined(__clang__)
    unsigned long idx;
    _BitScanReverse64(&idx, x);
    return (uint32_t)idx;
#else
    // 63u: unsigned integer literal with value 63.
    // __builtin_clzll: GCC builtin that counts the number of leading
    //   zero bits in an unsigned long long (uint64_t). Undefined behavior
    //   when passed the number 0. __builtin_clzll(1) is 63.
    return 63u - (uint32_t)__builtin_clzll(x);
#endif
}

inline uint64_t ceil_div_u64(uint64_t a, uint64_t b) {
    return (a + b - 1) / b;
}

inline uint32_t ctz64(uint64_t x) {
    assert(x != 0);
#if defined(_MSC_VER) && !defined(__clang__)
    unsigned long idx;
    _BitScanForward64(&idx, x);
    return (uint32_t)idx;
#else
    return (uint32_t)__builtin_ctzll(x);
#endif
}

inline uint64_t next_one_at_or_after(
    const uint64_t* H,
    size_t nwords,
    uint64_t pos
) {
    // translate bit offset *pos* into a word index and a bit offset
    // within that word.
    size_t wi = (size_t)(pos >> 6);
    unsigned bo = (unsigned)(pos & 63ULL);

    // represents "no such bit"
    if (wi >= nwords) return UINT64_MAX;

    // Scan to the next nonzero word. For the first word, we ignore
    // the first *bo* bits of that word.
    uint64_t w = H[wi] & (~0ULL << bo);
    while (w == 0) {
        ++wi;
        if (wi >= nwords) return UINT64_MAX;
        w = H[wi];
    }
    // ctz64(w): bit offset of the first '1' in this word.
    // wi<<6: total bits in the previous words.
    // So overall this is the bit offset of the set bit.
    return (uint64_t)((wi << 6) + ctz64(w));
}

// Return the index of the element in a sorted vector *v* corresponding
// to the smallest value greater than or equal to *q*.
size_t supremum_index(const std::vector<uint64_t>& v, const uint64_t q) {
    if (v.size() == 0) {
        throw std::runtime_error("supremum_index: v must be nonempty");
    }
    if (v.back() < q) {
        throw std::runtime_error("supremum_index: q is larger than the largest element in v");
    }
    if (q <= v.front()) {
        return 0;
    }
    size_t lo = 0,
           hi = v.size() - 1,
           mid;
    while (lo + 1 < hi) {
        mid = lo + (hi - lo) / 2;
        if (q <= v[mid]) {
            hi = mid;
        } else {
            lo = mid;
        }
    }
    return hi;
}

BitReader::BitReader(const uint64_t* words, size_t n_words):
    words(words),
    n_words(n_words),
    idx(0),
    consumed(0),
    cur(n_words ? words[0] : 0)
{}

// Scan to a particular bit position *pos*.
void BitReader::scan(uint64_t pos) {
    idx = pos >> 6;
    if (idx >= n_words) {
        throw std::runtime_error("BitReader::scan: out-of-bounds");
    }
    consumed = static_cast<unsigned>(pos & 63ULL);
    cur = words[idx];
}

uint64_t BitReader::get(unsigned w) {
    if (w == 0) return 0;
    uint64_t res = 0;
    // number of bits we've already used in *res* (LSB first)
    unsigned produced = 0;
    while (w) {
        // check if we need to get a new word from the bitstream.
        // If we're at the end of the stream, we just read 0s.
        if (consumed == 64) {
            ++idx;
            cur = (idx < n_words) ? words[idx] : 0;
            consumed = 0;
        }
        // number of bits remaining in the current word
        unsigned space = 64 - consumed;
        // number of bits to read in this iteration (don't read past
        // the current word!)
        unsigned take = (w < space) ? w : space;
        // discard the bits we've already read from the current word
        uint64_t chunk = (cur >> consumed);
        // retain the *take* least significant bits from *chunk*
        if (take < 64) chunk &= ((1ULL << take) - 1ULL);
        // pack those into *res*
        res |= (chunk << produced);
        // account for the bits we've read from *cur*
        consumed += take;
        // account for the bits we've written to *res*
        produced += take;
        // number of bits remaining to read
        w -= take;
    }
    return res;
}

void BitWriter::put(uint64_t val, unsigned w) {
    if (w == 0) return;
    // the *w* least significant bits of *val*.
    if (w < 64) val &= ((1ULL << w) - 1ULL);
    // number of bits remaining to write in *v*
    unsigned remain = w;
    while (remain) {
        // number of bits available in the current block.
        unsigned space = 64 - filled;
        // number of bits to write from *val* to the current block.
        unsigned take = (remain < space) ? remain : space;
        // *chunk* contains the *take* least significant bits from *val*.
        uint64_t chunk = val & ((take == 64) ? ~0ULL : ((1ULL << take) - 1ULL));
        // add *chunk* to the current block, skipping already-filled bits.
        cur |= (chunk << filled);
        // account for the new bits we've added to this block.
        filled += take;
        // flush out the least significant bits we've already written.
        val >>= take;
        // number of bits remaining to write.
        remain -= take;
        // check if we've filled up the current block. If so, add it to the
        // record and start a new empty block.
        if (filled == 64) {
            words.push_back(cur);
            cur = 0;
            filled = 0;
        }
    }
}

void BitWriter::flush() {
    if (filled) {
        words.push_back(cur);
        cur = 0;
        filled = 0;
    }
}

EFBlock::EFBlock(
    EFBlockMetadata meta,
    std::vector<uint64_t> low,
    std::vector<uint64_t> high
):
    meta(std::move(meta)),
    low(std::move(low)),
    high(std::move(high))
{}

inline uint32_t EFBlock::choose_l(uint64_t range, uint32_t n) {
    if (n == 0) return 0;
    uint64_t q = range / (uint64_t)n; // floor(range/n)
    if (q == 0) return 0;
    return floor_log2_u64(q);
}

EFBlock::EFBlock(const uint64_t* values, uint32_t n_elem) {
    if (n_elem == 0) {
        std::ostringstream msg;
        msg << "EFBlock cannot be constructed from zero elements, "
            << "since we need at least one element to estimate the lo/hi "
            << "bit boundary";
        throw std::runtime_error(msg.str());
    }
    meta.n_elem = n_elem;

    // smallest element in the universe
    meta.floor = values[0];

    // biggest element in the universe
    const uint64_t last = values[n_elem - 1];

    // number of bits required to span universe
    const uint64_t range = (last - values[0]) + 1ULL;

    // choose the partition between the "low" and "high" bits.
    // this is essentially the number of bits required to encode
    // the distance between adjacent elements if the elements were
    // uniformly spaced.
    const uint32_t l = choose_l(range, n_elem);
    meta.l = (uint8_t)l;

    const uint64_t one = 1ULL;

    // Write the *l* least significant bits from each element in *v*
    // to a dense bitvector.
    BitWriter bw;
    if (l > 0) {
        for (uint32_t i = 0; i < n_elem; ++i) {
            // value of this element relative to the least element
            uint64_t x = values[i] - meta.floor;
            // assign the *l* least significant bits in *x* to *low*
            uint64_t low = x & ((one << l) - 1ULL);
            // write these bits to the output bitvector
            bw.put(low, l);
        }
        // write the last word, even if it's incomplete
        bw.flush();
    }
    // get the output packed bitvector
    low = std::move(bw.words);

    // bits_hi is the number of bits required for the high bit representation.
    // How much space do we need?
    // We have *n* elements: each one contributes a single "1" bit.
    // And we have a total span of range//(2^l), so we'll have range//(2^l) "0"
    // bits to represent gaps between the elements.
    // So we need a total of n + range//(2^l) bits.
    const uint64_t range_hi = (l == 0) ? range
        : ((range + ((one << l) - 1ULL)) >> l);
    const uint64_t bits_hi = (uint64_t)n_elem + range_hi;

    // Number of 8-byte "blocks" required to for *bits_hi* bits.
    size_t hw = (size_t)ceil_div_u64(bits_hi, 64);
    // Allocate these blocks and initialize to zero.
    high.assign(hw, 0ULL);
    // for each element in the input...
    for (uint32_t i = 0; i < n_elem; ++i) {
        // value of this element relative to the least element
        uint64_t x = values[i] - meta.floor;
        // discard the *l* least significant bits
        uint64_t hi = (l == 0) ? x : (x >> l);
        // which bit to set to 1. this is the last bit in the unary
        // representation of element *i* when densely packed with all
        // the other elements.
        uint64_t pos = hi + i;
        // set this bit to 1.
        high[pos >> 6] |= (1ULL << (pos & 63ULL));

        // Aside: Why does pos = hi + i;
        // Consider each element as strictly represented by its high bits.
        // Say that element *i* has value *x* relative to the least value.
        // (*x* is equivalent to *hi* in the code above.)
        // We're going to represent the high bits as a dense unary encoding, so
        // that we need to figure out where to write the "1" for element *i*.
        // We know exactly *i* ones must have preceded this in the sequence,
        // since each of these represents one of the *i* elements that precede *i*.
        // We must also have exactly *x* zeros in the sequence prior to our element's
        // position, since, each zero represents a gap of size 1 between elements and
        // the total gaps must sum to *x*.
        // So the actual position of the set bit is exactly *x+i*.
    }

    // Number of 8-byte blocks in the (uncompressed) low bit representation.
    meta.low_words = low.size();

    // Number of 8-byte blocks in the (unary-compressed) high bit representation.
    meta.high_words = high.size();

    // Total number of bits in the high bit representation (bits_hi <= high_words * 64).
    meta.high_bits_len = bits_hi;
}

std::vector<uint64_t> EFBlock::decode() const {
    // Low bits are written densely, so we can read them by simply striding
    // across the *low* bitarray.
    BitReader br(low.data(), low.size());
    // Bit position of the previous element in the high bits.
    uint64_t prev_hi_pos = UINT64_MAX;
    // Pointer to start of the unary encoding.
    const uint64_t *highw = high.data();

    std::vector<uint64_t> out(meta.n_elem);
    for (uint32_t i = 0; i < meta.n_elem; ++i) {
        // Start looking for the next set bit, after the previous elements'
        // set bit. *pos* is the bit position relative to *highw*.
        uint64_t start = (prev_hi_pos == UINT64_MAX) ? 0ULL : (prev_hi_pos + 1ULL);
        uint64_t pos = next_one_at_or_after(highw, (size_t)meta.high_words, start);
        prev_hi_pos = pos;
        // Since pos = (# of prev elements) + (value of curr. element - floor),
        // this is the value of the current element (minus the floor).
        uint64_t hi = pos - i;
        // Write the low bits into *lo*, LSB first.
        uint64_t lo = (meta.l ? br.get(meta.l) : 0ULL);
        // Combine low and high bits to get the original element's value.
        out[i] = meta.floor + ((hi << meta.l) | lo);
    }
    return out;
}

void EFBlock::show() const {
    std::cout << "Header:\n";
    std::cout << "  n_elem:        " << meta.n_elem << std::endl;
    std::cout << "  l:             " << meta.l << std::endl;
    std::cout << "  pad[0]:        " << meta.pad[0] << std::endl;
    std::cout << "  pad[1]:        " << meta.pad[1] << std::endl;
    std::cout << "  pad[2]:        " << meta.pad[2] << std::endl;
    std::cout << "  floor:         " << meta.floor << std::endl;
    std::cout << "  low_words:     " << meta.low_words << std::endl;
    std::cout << "  high_words:    " << meta.high_words << std::endl;
    std::cout << "  high_bits_len: " << meta.high_bits_len << std::endl;
    std::cout << "  low.size():    " << low.size() << std::endl;
    std::cout << "  high.size():   " << high.size() << std::endl;
    std::cout << "Compressed low representation:\n";
    for(size_t i = 0; i < low.size(); ++i) {
        std::cout << i << "\t" << std::setw(32) << low.at(i) << std::endl;
    }
    std::cout << "Compressed high representation:\n";
    for(size_t i = 0; i < high.size(); ++i) {
        std::cout << i << "\t" << std::setw(32) << high.at(i) << std::endl;
    }
    const double compression_ratio = static_cast<double>(meta.n_elem) / (low.size() + high.size());
    std::cout << "Overall compression ratio: " << compression_ratio << std::endl;
}

Sequence::Sequence(uint32_t block_size):
    block_last_(0),
    block_offs_(0),
    payload_(0)
{
    meta.magic[0] = 'P';
    meta.magic[1] = 'P';
    meta.magic[2] = 'E';
    meta.magic[3] = 'F';
    meta.version = 1;
    meta.block_size = block_size;
    meta.reserved = 0;
    meta.n_elem = 0;
    meta.n_blocks = 0;
    meta.payload_offset = sizeof(SequenceMetadata); // + meta.n_blocks * sizeof(uint64_t) * 2;
}

Sequence::Sequence(
    const std::vector<uint64_t>& values,
    uint32_t block_size
) {
    if (!std::is_sorted(values.begin(), values.end())) {
        throw std::runtime_error(
            "input sequence must be nondecreasing"
        );
    }
    const uint64_t n_elem = static_cast<uint64_t>(values.size()),
                   n_blocks = (uint64_t)ceil_div_u64(n_elem, block_size);
    block_last_.resize(n_blocks);
    block_offs_.resize(n_blocks);
    payload_.clear();

    // Current byte offset into the block. We require that all blocks
    // have 8-byte alignment here for performance resaons, so there can
    // be unused space in each EFBlock.
    size_t cursor = 0; // payload size so far (bytes)

    for (uint64_t bi = 0; bi < n_blocks; ++bi) {
        // Start of this block.
        const uint64_t begin = bi * block_size;
        // End of this block; could be less than *block_size* if we're at
        // the end of the sequence.
        const uint64_t end = std::min(n_elem, begin + block_size);
        // Number of elements in the block (n <= block_size).
        const uint32_t n = (uint32_t)(end - begin);
        // Pointer to the first element in the block.
        const uint64_t* p = values.data() + begin;
        // Record the byte offset of this block in the file.
        block_offs_[bi] = cursor;
        // Record the value of the highest element in this block.
        block_last_[bi] = p[n - 1];
        // Compress this block.
        EFBlock blk(p, n);
        // Write the BlockHeader.
        append_bytes(&blk.meta, sizeof(blk.meta));
        cursor += sizeof(blk.meta);
        // Write the low bit representation (u64)
        if (!blk.low.empty()) {
            append_bytes(blk.low.data(), blk.low.size() * sizeof(uint64_t));
            cursor += blk.low.size() * sizeof(uint64_t);
        }
        // Write the high bit representation (u64)
        if (!blk.high.empty()) {
            append_bytes(blk.high.data(), blk.high.size() * sizeof(uint64_t));
            cursor += blk.high.size() * sizeof(uint64_t);
        }
    }

    // Configure SequenceMetadata
    meta.magic[0] = 'P';
    meta.magic[1] = 'P';
    meta.magic[2] = 'E';
    meta.magic[3] = 'F';
    meta.version = 1;
    meta.block_size = block_size;
    meta.reserved = 0;
    meta.n_elem = n_elem;
    meta.n_blocks = n_blocks;
    meta.payload_offset = sizeof(SequenceMetadata) + n_blocks * sizeof(uint64_t) * 2;
}

Sequence::Sequence(const Sequence& other):
    meta(other.meta),
    block_last_(other.block_last_),
    block_offs_(other.block_offs_),
    payload_(other.payload_)
{}

Sequence::Sequence(Sequence&& other) noexcept:
    meta(std::move(other.meta)),
    block_last_(std::move(other.block_last_)),
    block_offs_(std::move(other.block_offs_)),
    payload_(std::move(other.payload_))
{}

void file_error(
    const std::string& operation,
    const std::string& path,
    const std::string& reason
) {
    std::ostringstream o;
    o << "failed to " << operation << " to " << path
      << " due to " << reason << std::endl;
    throw std::runtime_error(o.str());
}

void Sequence::serialize_to_stream(std::ostream& out) const {
    if (!out) {
        throw std::runtime_error("failed to open stream for writing");
    }
    out.write(
        reinterpret_cast<const char*>(&meta),
        sizeof(meta)
    );
    if (!out) {
        throw std::runtime_error("failed to write header");
    }
    out.write(
        reinterpret_cast<const char*>(block_last_.data()),
        static_cast<std::streamsize>(block_last_.size() * sizeof(uint64_t))
    );
    if (!out) {
        throw std::runtime_error("failed to write block_last_");
    }
    out.write(
        reinterpret_cast<const char*>(block_offs_.data()),
        static_cast<std::streamsize>(block_offs_.size() * sizeof(uint64_t))
    );
    if (!out) {
        throw std::runtime_error("failed to write block_offs_");
    }
    out.write(
        reinterpret_cast<const char*>(payload_.data()),
        static_cast<std::streamsize>(payload_.size())
    );
    if (!out) {
        throw std::runtime_error("failed to write payload_");
    }
}

std::string Sequence::serialize() const {
    std::ostringstream o;
    serialize_to_stream(o);
    return o.str();
}

void Sequence::save(const std::string& path) const {
    std::ofstream o(path, std::ios::binary);
    if (!o) {
        file_error("save", path, "failure to open file");
    }
    serialize_to_stream(o);
}

void Sequence::init_from_stream(std::istream& in) {
    if (!in) {
        throw std::runtime_error("stream is not readable");
    }

    // Total file size (bytes)
    in.seekg(0, std::ios::end);
    auto sz = in.tellg();
    if (static_cast<size_t>(sz) < sizeof(SequenceMetadata)) {
        throw std::runtime_error("stream is missing header");
    }

    // Read the header
    in.seekg(0);
    in.read(reinterpret_cast<char*>(&meta), sizeof(meta));
    if (!in) {
        throw std::runtime_error("failure to read header");
    }

    // Check that it's a PPEF filetype and has version 1
    if (std::strncmp(meta.magic, "PPEF", 4) != 0 || meta.version != 1) {
        throw std::runtime_error("invalid magic and/or version");
    }

    // Special case: zero elements.
    if (meta.n_elem == 0) {
        block_last_.resize(0);
        block_offs_.resize(0);
        payload_.resize(0);
    }

    // Read the array of byte offsets for each EFBlock in the file
    block_last_.resize(meta.n_blocks);
    in.read(
        reinterpret_cast<char*>(block_last_.data()),
        meta.n_blocks * sizeof(uint64_t)
    );
    if (!in) {
        throw std::runtime_error("failure to read block_last_ array");
    }

    // Read the array of highest values for each EFBlock in the file
    block_offs_.resize(meta.n_blocks);
    in.read(
        reinterpret_cast<char*>(block_offs_.data()),
        meta.n_blocks * sizeof(uint64_t)
    );
    if (!in) {
        throw std::runtime_error("failure to read block_offs_ array");
    }

    // Read all of the EFBlocks into memory (TODO: replace with mmap)
    const size_t size_so_far = sizeof(SequenceMetadata)
        + meta.n_blocks * sizeof(uint64_t) * 2;
    const size_t bytes_to_read = static_cast<size_t>(sz) - size_so_far;
    payload_.resize(bytes_to_read);
    in.read(
        reinterpret_cast<char*>(payload_.data()),
        bytes_to_read
    );
    if (!in) {
        throw std::runtime_error("failure to read payload_ array");
    }
}

Sequence::Sequence(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("failed to open file for reading");
    init_from_stream(in);
}

Sequence::Sequence(std::istream& in) {
    init_from_stream(in);
}

std::vector<uint64_t> Sequence::decode_block(uint64_t bi) const {
    // Check for out-of-bounds
    if (bi >= meta.n_blocks) {
        std::ostringstream msg;
        msg << "invalid block index " << bi << "; total blocks = " << meta.n_blocks;
        throw std::runtime_error(msg.str());
    }
    // Pointer to the start of this block in the raw file.
    const uint8_t* base = payload_.data() + block_offs_.at(bi);
    // Read the EFBlock header.
    EFBlockMetadata block_meta {};
    std::memcpy(&block_meta, base, sizeof(block_meta));

    // Pointer to the start of the low bit representation.
    const uint64_t* loww = reinterpret_cast<const uint64_t*>(
        base + sizeof(block_meta)
    );
    // Pointer to the start of the high bit representation.
    const uint64_t* highw = loww + block_meta.low_words;
    // Allocate enough space for all elements in this block.
    std::vector<uint64_t> o(block_meta.n_elem);
    // The low bits are written densely, so we can read them by simply striding
    // across the byte array.
    BitReader br(loww, (size_t)block_meta.low_words);
    // Bit position of the previous element in the high bits.
    // Set to UINT64_MAX for element 0, so we know
    uint64_t prev_hi_pos = UINT64_MAX;
    for (uint32_t i = 0; i < block_meta.n_elem; ++i) {
        // Start looking for the next set bit, after the previous element's set bit.
        uint64_t start = (prev_hi_pos == UINT64_MAX) ? 0ULL : (prev_hi_pos + 1ULL);
        // Find the next set bit in *highw*. This is the bit position relative to
        // the start of *highw*.
        uint64_t pos = next_one_at_or_after(highw, (size_t)block_meta.high_words, start);
        prev_hi_pos = pos;
        // Since pos = (# of previous elements) + (value of current element-floor),
        // this is the value of the current element (minus the floor).
        uint64_t hi = pos - i;
        // Number of bits per element in the low bit representation.
        uint64_t lo = (block_meta.l ? br.get(block_meta.l) : 0ULL);
        // Combine the low bits and high bits to get the original element's
        // difference from the floor, and then add back the floor.
        o[i] = block_meta.floor + ((hi << block_meta.l) | lo);
    }
    return o;
}

uint64_t Sequence::get(uint64_t i) const {
    if (i >= meta.n_elem) {
        throw std::runtime_error("Sequence::get: out-of-bounds");
    }
    const uint64_t block_idx = i / meta.block_size,
                   block_pos = i % meta.block_size;
    std::vector<uint64_t> values = decode_block(block_idx);
    return values.at(block_pos);
}

uint64_t Sequence::operator[](uint64_t i) const {
    if (i >= meta.n_elem) {
        throw std::runtime_error("Sequence::get: out-of-bounds");
    }
    const uint64_t block_idx = i / meta.block_size,
                   block_pos = i % meta.block_size;
    std::vector<uint64_t> values = decode_block(block_idx);
    return values.at(block_pos);
}

EFBlock Sequence::get_efblock(uint64_t bi) const {
    if (bi >= meta.n_blocks) {
        throw std::runtime_error("Sequence::get_efblock: out-of-bounds");
    }
    const uint8_t* base = payload_.data() + block_offs_.at(bi);
    EFBlockMetadata meta;
    std::memcpy(&meta, base, sizeof(meta));
    const uint64_t* loww = reinterpret_cast<const uint64_t*>(base+sizeof(meta));
    std::vector<uint64_t> low(meta.low_words);
    std::memcpy(low.data(), loww, sizeof(uint64_t)*meta.low_words);
    const uint64_t* highw = loww + meta.low_words;
    std::vector<uint64_t> high(meta.high_words);
    std::memcpy(high.data(), highw, sizeof(uint64_t)*meta.high_words);
    EFBlock blk {
        meta, low, high
    };
    return blk;
}

bool Sequence::contains(uint64_t q) const {
    if (meta.n_elem == 0) {
        return false;
    }
    else if (block_last_.back() < q) {
        return false;
    }
    // binary search to identify the EFBlock that would contain this element
    size_t block_idx = supremum_index(block_last_, q);
    // decompress this block
    std::vector<uint64_t> values = decode_block(block_idx);
    // binary search to identify the element within this block
    return std::binary_search(values.begin(), values.end(), q);
}

SequenceMetadata Sequence::get_meta() const {
    SequenceMetadata o = meta;
    return o;
}

void Sequence::info() const {
    std::cout << "version = " << meta.version << "\n";
    std::cout << "n_elem = " << meta.n_elem << "\n";
    std::cout << "block_size = " << meta.block_size << "\n";
    std::cout << "n_blocks = " << meta.n_blocks << "\n";
    std::cout << "payload_offset = " << meta.payload_offset << "\n";

    // Compute compression ratio
    const float compression_ratio = 1.f
        - static_cast<float>(40 + 2*8*meta.n_blocks + payload_.size())
        / (meta.n_elem * 8);
    std::cout << "compression factor = " << compression_ratio << std::endl;
}

std::vector<uint64_t> Sequence::decode() const {
    std::vector<uint64_t> o;
    for (uint64_t bi = 0; bi < meta.n_blocks; ++bi) {
        std::vector<uint64_t> block = decode_block(bi);
        o.insert(o.end(), block.begin(), block.end());
    }
    return o;
}

Sequence Sequence::intersect(const Sequence& other) const {
    const uint64_t block_size_0 = static_cast<uint64_t>(meta.block_size),
                   block_size_1 = static_cast<uint64_t>(other.block_size());
    Sequence o(block_size_0);
    if (meta.n_elem == 0 || other.meta.n_elem == 0) {
        return o;
    }

    // Values in the current block to be compressed; flush at block_size_0
    std::vector<uint64_t> new_values;

    // Global index from 0 to n_elem
    uint64_t idx_0 = 0,
             idx_1 = 0,
    // Corresponding values
             val_0,
             val_1,
    // Index within the current block
             idx_in_block_0 = 0,
             idx_in_block_1 = 0,
    // Current block indices
             block_idx_0 = 0, 
             block_idx_1 = 0,
    // Byte offset within encoded payload
             cursor = 0;

    // Current values within each block
    std::vector<uint64_t> values_0 = decode_block(block_idx_0),
                          values_1 = other.decode_block(block_idx_1);

    while (idx_0 < meta.n_elem && idx_1 < other.meta.n_elem) {
        // Decode the next block(s) if necessary
        if (idx_in_block_0 == block_size_0) {
            ++block_idx_0;
            idx_in_block_0 = 0;
            // skip forward to the next relevant block, if we can
            while (block_idx_0 < meta.n_blocks && block_last_.at(block_idx_0) < val_1) {
                idx_0 += block_size_0;
                block_idx_0 += 1;
            }
            values_0 = decode_block(block_idx_0);
        }
        if (idx_in_block_1 == block_size_1) {
            ++block_idx_1;
            idx_in_block_1 = 0;
            // skip forward to the next relevant block, if we can
            while (block_idx_1 < other.meta.n_blocks && other.block_last_.at(block_idx_1) < val_0) {
                idx_1 += block_size_1;
                block_idx_1 += 1;
            }
            values_1 = other.decode_block(block_idx_1);
        }

        // Get the values to compare
        val_0 = values_0.at(idx_in_block_0);
        val_1 = values_1.at(idx_in_block_1);

        // Actually do the intersection and increment indices
        if (val_0 == val_1) {
            new_values.push_back(val_0);
            ++o.meta.n_elem;
            if (new_values.size() == block_size_0) {
                EFBlock blk(new_values.data(), new_values.size());
                o.block_offs_.push_back(cursor);
                // Write the BlockHeader.
                o.append_bytes(&blk.meta, sizeof(blk.meta));
                cursor += sizeof(blk.meta);
                // Write the low bit representation (u64)
                if (!blk.low.empty()) {
                    o.append_bytes(blk.low.data(), blk.low.size() * sizeof(uint64_t));
                    cursor += blk.low.size() * sizeof(uint64_t);
                }
                // Write the high bit representation (u64)
                if (!blk.high.empty()) {
                    o.append_bytes(blk.high.data(), blk.high.size() * sizeof(uint64_t));
                    cursor += blk.high.size() * sizeof(uint64_t);
                }
                // Update indices
                o.block_last_.push_back(new_values.back());
                ++o.meta.n_blocks;
                new_values.clear();
            }
            ++idx_0;
            ++idx_in_block_0;
            ++idx_1;
            ++idx_in_block_1;
        } else if (val_0 < val_1) {
            ++idx_0;
            ++idx_in_block_0;
        } else {
            ++idx_1;
            ++idx_in_block_1;
        }
    }

    // Flush the last block, if necessary
    if (new_values.size() > 0) {
        EFBlock blk(new_values.data(), new_values.size());
        o.block_offs_.push_back(cursor);
        // Write the BlockHeader.
        o.append_bytes(&blk.meta, sizeof(blk.meta));
        cursor += sizeof(blk.meta);
        // Write the low bit representation (u64)
        if (!blk.low.empty()) {
            o.append_bytes(blk.low.data(), blk.low.size() * sizeof(uint64_t));
            cursor += blk.low.size() * sizeof(uint64_t);
        }
        // Write the high bit representation (u64)
        if (!blk.high.empty()) {
            o.append_bytes(blk.high.data(), blk.high.size() * sizeof(uint64_t));
            cursor += blk.high.size() * sizeof(uint64_t);
        }
        // Update indices
        o.block_last_.push_back(new_values.back());
        ++o.meta.n_blocks;
    }

    // Update payload offset for output files.
    o.meta.payload_offset = sizeof(SequenceMetadata) + o.meta.n_blocks * sizeof(uint64_t) * 2;

    return o;
}

Sequence Sequence::operator|(const Sequence& other) const {
    const uint64_t block_size_0 = static_cast<uint64_t>(meta.block_size),
                   block_size_1 = static_cast<uint64_t>(other.block_size());

    // Special cases: if either Sequence is empty, we just copy the existing
    // object. In this case, the block size of the output object is equal to
    // whatever the block size of the nonempty Sequence was.
    if (meta.n_elem == 0) {
        Sequence seq = other;
        return seq;
    }
    else if (other.meta.n_elem == 0) {
        Sequence seq = *this;
        return seq;
    }

    Sequence o(block_size_0);

    // Values in the current block to be compressed; flush at block_size_0
    std::vector<uint64_t> new_values;

    // Global index from 0 to n_elem
    uint64_t idx_0 = 0,
             idx_1 = 0,
    // Corresponding values
             val_0,
             val_1,
    // Index within the current block
             idx_in_block_0 = 0,
             idx_in_block_1 = 0,
    // Current block indices
             block_idx_0 = 0, 
             block_idx_1 = 0,
    // Byte offset within encoded payload
             cursor = 0;

    // Current values within each block
    std::vector<uint64_t> values_0 = decode_block(block_idx_0),
                          values_1 = other.decode_block(block_idx_1);

    while (idx_0 < meta.n_elem || idx_1 < other.meta.n_elem) {
        // Decode new block(s) if necessary
        if (idx_0 < meta.n_elem && idx_in_block_0 == block_size_0) {
            ++block_idx_0;
            values_0 = decode_block(block_idx_0);
            idx_in_block_0 = 0;
        }
        if (idx_1 < other.meta.n_elem && idx_in_block_1 == block_size_1) {
            ++block_idx_1;
            values_1 = other.decode_block(block_idx_1);
            idx_in_block_1 = 0;
        }

        // If we've reached the end of the first Sequence, write the next value
        // from the second Sequence.
        if (idx_0 == meta.n_elem) {
            new_values.push_back(values_1.at(idx_in_block_1));
            ++o.meta.n_elem;
            ++idx_1;
            ++idx_in_block_1;
        }

        // If we've reached the end of the second Sequence, write the next value
        // from the first Sequence.
        else if (idx_1 == other.meta.n_elem) {
            new_values.push_back(values_0.at(idx_in_block_0));
            ++o.meta.n_elem;
            ++idx_0;
            ++idx_in_block_0;
        }

        // Otherwise, write the smaller of the two values and increment
        // the corresponding Sequence's index. This maintains sorting order.
        else {
            val_0 = values_0.at(idx_in_block_0);
            val_1 = values_1.at(idx_in_block_1);
            // Determine which value to write to the output.
            if (val_0 == val_1) {
                new_values.push_back(val_0);
                ++o.meta.n_elem;
                ++idx_0;
                ++idx_in_block_0;
                ++idx_1;
                ++idx_in_block_1;
            }
            else if (val_0 < val_1) {
                new_values.push_back(val_0);
                ++o.meta.n_elem;
                ++idx_0;
                ++idx_in_block_0;
            } else {
                new_values.push_back(val_1);
                ++o.meta.n_elem;
                ++idx_1;
                ++idx_in_block_1;
            }
        }

        // Compress a new block, if we've reached the block size
        if (new_values.size() == block_size_0) {
            EFBlock blk(new_values.data(), new_values.size());
            o.block_offs_.push_back(cursor);
            // Write the BlockHeader.
            o.append_bytes(&blk.meta, sizeof(blk.meta));
            cursor += sizeof(blk.meta);
            // Write the low bit representation (u64)
            if (!blk.low.empty()) {
                o.append_bytes(blk.low.data(), blk.low.size() * sizeof(uint64_t));
                cursor += blk.low.size() * sizeof(uint64_t);
            }
            // Write the high bit representation (u64)
            if (!blk.high.empty()) {
                o.append_bytes(blk.high.data(), blk.high.size() * sizeof(uint64_t));
                cursor += blk.high.size() * sizeof(uint64_t);
            }
            // Update indices
            o.block_last_.push_back(new_values.back());
            ++o.meta.n_blocks;
            new_values.clear();
        }
    }

    // Flush the last block, if necessary
    if (new_values.size() > 0) {
        EFBlock blk(new_values.data(), new_values.size());
        o.block_offs_.push_back(cursor);
        // Write the BlockHeader.
        o.append_bytes(&blk.meta, sizeof(blk.meta));
        cursor += sizeof(blk.meta);
        // Write the low bit representation (u64)
        if (!blk.low.empty()) {
            o.append_bytes(blk.low.data(), blk.low.size() * sizeof(uint64_t));
            cursor += blk.low.size() * sizeof(uint64_t);
        }
        // Write the high bit representation (u64)
        if (!blk.high.empty()) {
            o.append_bytes(blk.high.data(), blk.high.size() * sizeof(uint64_t));
            cursor += blk.high.size() * sizeof(uint64_t);
        }
        // Update indices
        o.block_last_.push_back(new_values.back());
        ++o.meta.n_blocks;
    }

    // Update payload offset for output files.
    o.meta.payload_offset = sizeof(SequenceMetadata) + o.meta.n_blocks * sizeof(uint64_t) * 2;

    return o;
}

uint64_t Sequence::n_elem() const {
    return meta.n_elem;
}

uint32_t Sequence::block_size() const {
    return meta.block_size;
}

uint64_t Sequence::n_blocks() const {
    return meta.n_blocks;
}

} // end namespace ppef
