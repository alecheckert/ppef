#include "ppef.h"

namespace ppef {

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

} // end namespace ppef
