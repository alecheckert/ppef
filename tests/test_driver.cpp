#include <iostream>
#include <random>
#include <cassert>
#include "ppef.h"

static std::random_device rd;

using namespace ppef;

// Generate a vector of *n* random integers between 0 and *max_value*
// (noninclusive).
std::vector<uint64_t> random_sorted_integers(
    size_t n,
    uint64_t max_value
) {
    std::mt19937 gen(rd());
    if (max_value == 0) {
        return std::vector<uint64_t>(n, 0);
    }
    std::vector<uint64_t> o(n);
    std::uniform_int_distribution<uint64_t> dist(0, max_value-1);
    for (size_t i = 0; i < n; ++i) {
        o[i] = dist(gen);
    }
    std::sort(o.begin(), o.end());
    return o;
}

// Test that a bitstream written by BitWriter can be parsed
// by BitReader.
void test_bit_writer_and_reader() {
    // 100 integers from 0 to 127: can be represented in 7 bits
    const size_t n = 100;
    const uint64_t max_value = 1<<7;
    const std::vector<uint64_t> seq = random_sorted_integers(n, max_value);
    assert(std::is_sorted(seq.begin(), seq.end()));
    assert(seq.size() == 100);

    BitWriter writer;
    for (size_t i = 0; i < n; ++i) {
        writer.put(seq.at(i), 7);
    }

    // Check that we've written the correct number of words.
    const size_t n_bits = n * 7;
    assert(writer.filled == static_cast<unsigned>(n_bits % 64));
    writer.flush();
    assert(writer.words.size() == (n_bits + 64 - 1) / 64);

    // Try to unpack these into the original sequence of integers
    BitReader reader(writer.words.data(), writer.words.size());
    for (size_t i = 0; i < n; ++i) {
        assert (reader.get(7) == seq.at(i));
    }
}

// Edge case: reading from end of stream.
void test_bit_reader_end_of_stream() {
    std::vector<uint64_t> words;
    BitReader reader (words.data(), words.size());
    for (size_t i = 0; i < 10; ++i) {
        assert (reader.get(7) == 0ULL);
    }
}

// Edge case: writing 0 bits.
void test_bit_writer_zero_bits() {
    BitWriter writer;
    writer.put(7ULL, 0);
    assert (writer.words.size() == 0);
    assert (writer.filled == 0);
}

void test_efblock() {
    // 1024 integers from 0 to 4096: can be represented in 7 bits
    const size_t n = 1<<10;
    const uint64_t max_value = 1<<12;
    const std::vector<uint64_t> values = random_sorted_integers(n, max_value);
    assert(std::is_sorted(values.begin(), values.end()));
    assert(values.size() == n);

    // Floor of the range
    auto it = std::min(values.begin(), values.end());
    if (it == values.end()) {
        throw std::runtime_error("n must not be zero");
    }
    const uint64_t floor = *it;

    // Encode
    EFBlock blk(values.data(), values.size());
    assert (blk.meta.n_elem == static_cast<uint32_t>(n));
    assert (blk.meta.floor == floor);

    // Decode
    const std::vector<uint64_t> recon = blk.decode();

    // Check that we get exactly the same thing
    assert (recon.size() == values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        assert (recon.at(i) == values.at(i));
    }
}

// Edge case: a single integer.
void test_efblock_size_one() {
    const size_t n = 1;
    const uint64_t max_value = 1<<12;
    const std::vector<uint64_t> values = random_sorted_integers(n, max_value);
    assert(std::is_sorted(values.begin(), values.end()));
    assert(values.size() == 1);

    // Floor of the range
    auto it = std::min(values.begin(), values.end());
    if (it == values.end()) {
        throw std::runtime_error("n must not be zero");
    }
    const uint64_t floor = *it;

    // Encode
    EFBlock blk(values.data(), values.size());
    assert (blk.meta.n_elem == static_cast<uint32_t>(n));
    assert (blk.meta.floor == floor);

    // Decode
    const std::vector<uint64_t> recon = blk.decode();

    // Check that we get exactly the same thing
    assert (recon.size() == values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        assert (recon.at(i) == values.at(i));
    }
}

void test_pef_construct_from_sequence() {
    const size_t n = 1<<10;
    const uint64_t max_value = 1<<12;
    const uint32_t block_size = 1<<8;

    const std::vector<uint64_t> values = random_sorted_integers(n, max_value);
    assert(std::is_sorted(values.begin(), values.end()));
    assert(values.size() == n);

    // compress
    PEF pef(values, block_size);
    assert(pef.n_elem() == static_cast<uint64_t>(values.size()));
    assert(pef.block_size() == block_size);
    assert(pef.n_blocks() == 4ULL); // 4 blocks of 256 elements to cover n=1024

    // test PEF::decode_block
    std::vector<uint64_t> recon = pef.decode_block(0);
    assert(recon.size() == 256);
    for (size_t i = 0; i < 256; ++i) {
        assert(recon.at(i) == values.at(i));
    }

    // second block
    recon = pef.decode_block(1);
    assert(recon.size() == 256);
    for (size_t i = 0; i < 256; ++i) {
        assert(recon.at(i) == values.at(i + 256));
    }

    // test PEF::decode, which should decode the whole sequence
    recon = pef.decode();
    assert(recon.size() == values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        assert(recon.at(i) == values.at(i));
    }
}

void test_pef_construct_from_sequence_ragged() {
    const size_t n = 1333;
    const uint64_t max_value = 1<<12;
    const uint32_t block_size = 1<<8;

    const std::vector<uint64_t> values = random_sorted_integers(n, max_value);
    assert(std::is_sorted(values.begin(), values.end()));
    assert(values.size() == n);

    // compress
    PEF pef(values, block_size);
    assert(pef.n_elem() == static_cast<uint64_t>(values.size()));
    assert(pef.block_size() == block_size);
    assert(pef.n_blocks() == 6ULL); // blocks of 256 elements to cover n=1333

    // test PEF::decode, which should decode the whole sequence
    std::vector<uint64_t> recon = pef.decode();
    assert(recon.size() == values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        assert(recon.at(i) == values.at(i));
    }
}

void test_pef_construct_from_sequence_empty() {
    const size_t n = 0;
    const uint32_t block_size = 1<<8;
    const std::vector<uint64_t> values;
    assert(std::is_sorted(values.begin(), values.end()));
    assert(values.size() == n);

    // compress
    PEF pef(values, block_size);
    assert(pef.n_elem() == static_cast<uint64_t>(values.size()));
    assert(pef.block_size() == block_size);

    // test PEF::decode, which should decode the whole sequence
    std::vector<uint64_t> recon = pef.decode();
    assert(recon.size() == values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        assert(recon.at(i) == values.at(i));
    }
}

void test_pef_construct_from_file() {
    const size_t n = 1333;
    const uint64_t max_value = 1<<12;
    const uint32_t block_size = 1<<8;

    const std::vector<uint64_t> values = random_sorted_integers(n, max_value);
    assert(std::is_sorted(values.begin(), values.end()));
    assert(values.size() == n);

    const std::string filepath("_test_file.ppef");
    PEF pef(values, block_size);
    pef.save(filepath);

    PEF pef2(filepath);

    // Check that metadata is identical
    const PEFMetadata meta = pef.get_meta();
    const PEFMetadata meta2 = pef2.get_meta();
    assert (meta2.magic[0] == meta.magic[0]);
    assert (meta2.magic[1] == meta.magic[1]);
    assert (meta2.magic[2] == meta.magic[2]);
    assert (meta2.magic[3] == meta.magic[3]);
    assert (meta2.version == meta.version);
    assert (meta2.block_size == meta.block_size);
    assert (meta2.reserved == meta.reserved);
    assert (meta2.n_elem == meta.n_elem);
    assert (meta2.n_blocks == meta.n_blocks);
    assert (meta2.payload_offset == meta.payload_offset);

    // Check that values survived the roundtrip
    const std::vector<uint64_t> recon = pef2.decode();
    assert (recon.size() == values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        assert (recon.at(i) == values.at(i));
    }
}

void test_driver() {
    std::cout << "test_bit_writer_and_reader\n";
    test_bit_writer_and_reader();

    std::cout << "test_bit_reader_end_of_stream\n";
    test_bit_reader_end_of_stream();

    std::cout << "test_bit_writer_zero_bits\n";
    test_bit_writer_zero_bits();

    std::cout << "test_efblock\n";
    test_efblock();

    std::cout << "test_efblock_size_one\n";
    test_efblock_size_one();

    std::cout << "test_pef_construct_from_sequence\n";
    test_pef_construct_from_sequence();

    std::cout << "test_pef_construct_from_sequence_ragged\n";
    test_pef_construct_from_sequence_ragged();

    std::cout << "test_pef_construct_from_sequence_empty\n";
    test_pef_construct_from_sequence_empty();

    std::cout << "test_pef_construct_from_file\n";
    test_pef_construct_from_file();
}

int main() {
    test_driver();
    return 0;
}
