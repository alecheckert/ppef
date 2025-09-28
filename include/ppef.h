#pragma once
/* ppef - Partitioned Elias-Fano encoding of a sequence of integers. */

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>
#include <cassert>
#include <cstdint>

namespace ppef {

/*
 * Class: BitReader
 * ----------------
 * Read integers out of a densely-encoded bitarray.
*/
struct BitReader {
    // Words to read
    const uint64_t* words = nullptr;
    // Number of words available in *p*
    size_t n_words = 0;
    // Current word we're on
    size_t idx = 0;
    // Bits already consumed from the current word
    unsigned consumed = 0;
    // Current word (first *consumed* bits will have already been read)
    uint64_t cur = 0;

    // Construct from a raw bitstream, represented as a sequence of
    // 64-bit "words".
    BitReader(const uint64_t* words, size_t n_words);

    // Read *w* bits from the input words, and return those bits
    // packed into the least-significant positions of uint64_t.
    // Doesn't make sense to use w > 64.
    uint64_t get(unsigned w);
};

/*
 * Class: BitWriter
 * ----------------
 * Pack integers densely into a bitarray.
*/
struct BitWriter {
    // finished words
    std::vector<uint64_t> words;
    // current word we're writing to
    uint64_t cur = 0;
    // number of bits already used in *cur*
    unsigned filled = 0;

    // Write the *w* least-significant bits from *val* into
    // *words*, creating a new word if necessary.
    void put(uint64_t val, unsigned w);

    // Start a new word, regardless of how many bits we've used in
    // the current word.
    void flush();
};

} // end namespace ppef
