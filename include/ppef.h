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

// floor(log2(x))
inline uint32_t floor_log2_u64(uint64_t x);

// ceil(a / b)
inline uint64_t ceil_div_u64(uint64_t a, uint64_t b);

// number of trailing zero bits in an ULL - that is, the number
// of least significant zero bits before the first 1.
inline uint32_t ctz64(uint64_t x);

// bit position of the next set ('1') bit from the position *pos*
// onwards in a bitarray *H*.
inline uint64_t next_one_at_or_after(
    const uint64_t *H, // size n_words
    size_t n_words,
    uint64_t pos
);

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


/*
 * Struct: FileHeader
 * ------------------
 * Metadata relevant for a PPEF-compressed file. We write this once in
 * the first 40 bytes of the file.
*/
#pragma pack(push, 1)
struct FileHeader {
    char     magic[4];       // file magic; we use "PEF1"
    uint32_t version;        // 1
    uint64_t n_elem;         // total number of compressed elements
    uint32_t block_size;     // compression block size (in # elements)
    uint32_t reserved;       // always 0
    uint64_t n_blocks;       // ceil(n_elem / block_size)
    uint64_t payload_offset; // byte offset of actual data part of file
};
#pragma pack(pop)
// Desirable for byte-level alignment.
static_assert(sizeof(FileHeader) == 40, "FileHeader must be 40 bytes");

/*
 * Struct: EFBlockMetadata
 * -----------------------
 * Metadata for a PEF-compressed block of integers.
*/
#pragma pack(push, 1)
struct EFBlockMetadata {
    uint32_t n_elem;         // total number of integers ("elements") in this block.
    uint8_t  l;              // number of least significant bits in the "low" bits.
    uint8_t  pad[3];         // so that the whole block remains divisible by 8 bytes.
    uint64_t floor;          // the least element.
    uint64_t low_words;      // total 8-byte blocks in the low bit representation.
    uint64_t high_words;     // total 8-byte blocks in the high bit representation.
    uint64_t high_bits_len;  // total number of bits in the high bit representation.
                             // note: high_bits_len <= high_words * 64.
};
#pragma pack(pop)
// Desirable for maintaining byte alignment.
static_assert(
    sizeof(EFBlockMetadata) == 40,
    "EFBlockMetadata must be 40 bytes"
);

/*
 * Struct: EFBlock
 * ---------------
 * Elias-Fano encoding of a non-decreasing sequence of integers.
*/
struct EFBlock {
    EFBlockMetadata meta {};
    // Packed low bits
    std::vector<uint64_t> low;
    // Unary-encoded high bits
    std::vector<uint64_t> high;

    // Choose how many bits from each integer to encode in the "low" vs.
    // "high" parts. This optimizes the compression ratio for *n*
    // integers uniformly distributed between 0 and *range*.
    static inline uint32_t choose_l(uint64_t range, uint32_t n);

    // Print out everything in this block to stdout.
    void show() const;

    // Construct from a raw sequence of integers.
    EFBlock(const uint64_t* values, uint32_t n_elem);

    // Decode to the original sequence of integers.
    std::vector<uint64_t> decode() const;
};



} // end namespace ppef
