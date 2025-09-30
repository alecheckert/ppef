# ppef: Partitioned Elias-Fano encoding

Compact C++ implementation of the partitioned Elias-Fano (PEF) encoding from Ottoviano & Venturini (https://doi.org/10.1145/2600428.2609615):

Notes:
 - No external C/C++ dependencies
 - Supports random access without decompression
 - Python bindings
 - Pickleable
 - Set-like operations - like intersections, unions, etc. - can be performed on the compressed representation (without decompression). Still working on these algorithms; stay tuned.

## Python example

```
import numpy as np
from ppef import Sequence, deserialize

# Simulate a bunch of random integers
values = np.random.randint(0, 1<<16, size=1<<22)
values.sort()

# Encode
seq = Sequence(values)

# Show some info
seq.info()

# Total number of compressed elements
print(len(seq))

# Random access: get the i^th element without decompressing
print(seq[5000])

# Decode the entire sequence
values: list[int] = seq.decode()

# Decode only one partition block
chunk: list[int] = seq.decode_block(50)

# Serialize to a file
seq.save("myfile.ppef")

# Deserialize from a file
seq2 = Sequence("myfile.ppef")

# Serialize to a bytestring
serialized: bytes = seq.serialize()

# Deserialize from a bytestring
seq2: Sequence = deserialize(serialized)
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
