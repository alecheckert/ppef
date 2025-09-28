#include "ppef.h"

namespace ppef {

inline uint32_t floor_log2_u64(uint64_t x) {
    // 63u: unsigned integer literal with value 63.
    // __builtin_clzll: GCC builtin that counts the number of leading
    //   zero bits in an unsigned long long (uint64_t). Undefined behavior
    //   when passed the number 0. __builtin_clzll(1) is 63.
    return 63u - (uint32_t)__builtin_clzll(x);
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

BitReader::BitReader(const uint64_t* words, size_t n_words):
    words(words),
    n_words(n_words),
    idx(0),
    consumed(0),
    cur(n_words ? words[0] : 0)
{}

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

} // end namespace ppef
