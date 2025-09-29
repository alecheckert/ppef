# ppef: Partitioned Elias-Fano encoding

Compact C++ implementation of the partitioned Elias-Fano (PEF) encoding from Ottoviano & Venturini (https://doi.org/10.1145/2600428.2609615):

Notes:
 - Python bindings
 - Pickleable
 - No external C/C++ dependencies
 - Set-like operations - like intersections, unions, etc. - can be performed on the compressed representation (without decompression). Still working on these algorithms; stay tuned.

## Python example

```
from _ppef import Sequence, deserialize

# Simulate a bunch of random integers
values = np.random.randint(0, 1<<16, size=(1<<22))
values.sort()

# Encode
seq = Sequence(values)

# Show some info
seq.show_meta()

# Decode
values: list[int] = seq.decode()

# Decode only one partition block
chunk: list[int] = seq.decode_block(50)

# Serialize to a file
seq.save("myfile.ppef")

# Deserialize from a file
seq2 = _Sequence("myfile.ppef")

# Serialize to a bytestring
serialized: bytes = seq.serialize()

# Deserialize from a bytestring
seq2: Sequence = deserialize(serialized)

# Decode only the i^th value without decoding the whole thing
val: int = seq.get(5167)
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
