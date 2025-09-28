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

/*
 * Struct: PEFMetadata
 * -------------------
 * Metadata relevant for a PPEF-compressed file. We write this once in
 * the first 40 bytes of the file.
*/
#pragma pack(push, 1)
struct PEFMetadata {
    char     magic[4];       // file magic ("PPEF")
    uint32_t version;        // 1
    uint64_t n_elem;         // total number of compressed elements
    uint32_t block_size;     // compression block size (in # elements)
    uint32_t reserved;       // always 0
    uint64_t n_blocks;       // ceil(n_elem / block_size)
    uint64_t payload_offset; // byte offset of actual data part of file
};
#pragma pack(pop)
// Desirable for byte-level alignment.
static_assert(sizeof(PEFMetadata) == 40, "PEFMetadata must be 40 bytes");


/*
 * Class: PEF
 * ----------
 * A nondecreasing sequence of integers in partitioned Elias-Fano (PEF)
 * format. Provides methods to serialize the sequence to a file.
*/
class PEF {
public:
    // Construct from a raw sequence of nondecreasing integers.
    explicit PEF(
        const std::vector<uint64_t>& values, // must be sorted!
        uint32_t block_size = 256
    );

    // Construct from a compressed PPEF file.
    explicit PEF(const std::string& filepath);

    // Save contents to a file with magic "PPEF". Throws runtime_error
    // if we can't save.
    void save(const std::string& path) const;

    // Decode the i^th EFBlock, returning its original integers.
    std::vector<uint64_t> decode_block(uint64_t i) const;

    // Decode the entire *PEF* object, returning the whole original sequence.
    std::vector<uint64_t> decode() const;

    // Number of integers encoded in this PEF.
    uint64_t n_elem() const;

    // Maximum number of integers per EFBlock.
    uint32_t block_size() const;

    // Total number of EFBlocks.
    uint64_t n_blocks() const;

    // Print all PEFMetadata to stdout.
    void show_meta() const;

    // Get a copy of the PEFMetadata.
    PEFMetadata get_meta() const;

private:
    PEFMetadata meta {};
    // Highest element in each block (size *n_blocks_*).
    std::vector<uint64_t> block_last_;
    // Byte offset of the start of each block in the file (size *n_blocks_*).
    std::vector<uint64_t> block_offs_;
    // All EFBlocks written end-to-end:
    // header0, low0, high0, header1, low1, high1, ...
    std::vector<uint8_t> payload_;

    // Write a new chunk of data to *payload_*.
    void append_bytes(const void* src, size_t n) {
        size_t old = payload_.size();
        payload_.resize(old + n);
        std::memcpy(payload_.data() + old, src, n);
    }
};

} // end namespace ppef
