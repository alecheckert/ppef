# ppef: Partitioned Elias-Fano encoding

Compact C++11 : Python implementation of the partitioned Elias-Fano (PEF) encoding from Ottoviano & Venturini (https://doi.org/10.1145/2600428.2609615).

Partly for fun (it's a neat method), partly because I needed a Python-facing implementation that was simple/hackable but still reasonably performant.

The main interface is a `Sequence` object that provides a compressed in-memory representation of a nondecreasing sequence of unsigned integers. Following Ottoviano & Venturini, we divide this sequence into "blocks" that are each independently encoded with Elias-Fano using adaptive high/low bit ratios. This partitioning scheme increases compression efficiency: for large sets, we're usually able to get compression ratios of >=10-20X. Apart from that, the scheme has some other benefits that we exploit here:
 - $O(1)$ random access _without decompression_
 - $O(n+m)$ intersections and unions _without decompression_
 - $O(\log (n))$ set membership tests _without decompression_
 - Trivial $O(n)$ serialization and deserialization

These properties make the `Sequence` well-suited to storing large inverted indices in search algorithms. All operations maintain sorting on the input sequence.

"Without decompression" above means without decompressing the entire `Sequence`. We still need to decompress individual partitions, but we can do all of the above while only holding a single decompressed partition in memory at a time.

Our implementation also has some other benefits:
 - No external C/C++ dependencies
 - Thin Python bindings
 - Pickleable

Limitations include:
 - No insert operation; requires full decompression
 - Currently doesn't support mmap (this is a future improvement)

## Python example

```
import numpy as np
from ppef import Sequence, deserialize

# Sample a sequence of integers. These are uniformly distributed, which is
# a worst-case situation for Elias-Fano encoding.
values = np.random.randint(0, 1<<16, size=1<<22)
values.sort()

# Encode
seq = Sequence(values)

# Show some info
seq.info()

# Total number of compressed elements
n_elements = len(seq)
assert n_elements == len(values)

# Random access: get the i^th element without decompressing
idx = 5000
val: int = seq[idx]
assert val == values[idx]

# Set membership testing
val_is_present = val in seq
assert val_is_present

# Decode the entire sequence
values: list[int] = seq.decode()

# Decode only the 50th partition block
chunk: list[int] = seq.decode_block(50)

# Total number of partition blocks
print(seq.n_blocks)

# Serialize to a file
seq.save("myfile.ppef")

# Deserialize from a file
seq2 = Sequence("myfile.ppef")

# Serialize to a bytestring
serialized: bytes = seq.serialize()

# Deserialize from a bytestring
seq2: Sequence = deserialize(serialized)

# Define another Sequence for testing intersections and unions
values2 = np.random.randint(0, 1<<16, size=1<<22)
values2.sort()
seq2 = Sequence(values2)

# Get the intersection between two Sequences (without decompressing)
new_seq: Sequence = seq & seq2

# Get the union between two Sequences (without decompressing)
new_seq: Sequence = seq | seq2
```

## Building, testing

Compile the Python package:
```
pip install .
```

Build and run the C++ tests:
```
cd tests
make
./test_driver
```

Run the Python tests:
```
pytest tests
```
