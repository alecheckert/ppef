import pef
import pickle
import numpy as np
import multiprocessing as mp
from tempfile import NamedTemporaryFile


def test_pef():
    max_value = 1 << 16
    n_elem = 1 << 12
    block_size = 1 << 7
    values = np.random.randint(0, max_value, size=n_elem)
    values.sort()
    seq = pef.Sequence(values, block_size=block_size)
    assert seq.n_elem == n_elem
    assert seq.block_size == block_size

    # test blockwise decompression
    recon = seq.decode_block(0)
    assert (np.array(recon) == values[: len(recon)]).all()

    # test full decompression
    recon = seq.decode()
    assert (np.array(recon) == values).all()

    # "does-it-even-run?" tests
    _ = seq.get_meta()
    _ = seq.n_blocks

    # test serialization to a file
    tmp = NamedTemporaryFile(suffix=".pef")
    seq.save(tmp.name)
    seq2 = pef.Sequence(tmp.name)
    recon = seq2.decode()
    assert (np.array(recon) == values).all()


def test_serialization():
    max_value = 1 << 18
    n_elem = 1 << 16
    block_size = 1 << 7
    values = np.random.randint(0, max_value, size=n_elem)
    values.sort()
    seq = pef.Sequence(values)
    serialized = seq.serialize()
    print(len(serialized))
    seq2 = pef.deserialize(serialized)
    assert (np.array(seq.decode()) == values).all()


def test_pickling():
    max_value = 1 << 16
    n_elem = 1 << 12
    block_size = 1 << 7
    values = np.random.randint(0, max_value, size=n_elem)
    values.sort()
    seq = pef.Sequence(values, block_size=block_size)
    serialized = pickle.dumps(seq)
    seq2 = pickle.loads(serialized)
    assert (np.array(seq2.decode()) == values).all()


def test_empty():
    """Stability test."""
    seq = pef.Sequence([])
    assert seq.n_elem == 0
    serialized = seq.serialize()
    seq2 = pef.deserialize(serialized)
    assert seq2.n_elem == 0


def test_intersect():
    values_0 = np.random.randint(0, 1 << 16, size=(1 << 16))
    values_1 = np.random.randint(0, 1 << 16, size=(1 << 16))
    values_0.sort()
    values_1.sort()
    seq0 = pef.Sequence(values_0)
    seq1 = pef.Sequence(values_1)
    seq2 = seq0 & seq1
    expected = set(seq0.decode()) & set(seq1.decode())
    assert set(seq2.decode()) == expected


def test_union():
    values_0 = np.random.randint(0, 1 << 16, size=(1 << 16))
    values_1 = np.random.randint(0, 1 << 16, size=(1 << 16))
    values_0.sort()
    values_1.sort()
    seq0 = pef.Sequence(values_0)
    seq1 = pef.Sequence(values_1)
    seq2 = seq0 | seq1
    expected = set(seq0.decode()) | set(seq1.decode())
    assert set(seq2.decode()) == expected


def test_difference():
    values_0 = np.random.randint(0, 1 << 16, size=(1 << 16))
    values_1 = np.random.randint(0, 1 << 16, size=(1 << 16))
    values_0.sort()
    values_1.sort()
    seq0 = pef.Sequence(values_0)
    seq1 = pef.Sequence(values_1)
    seq0 = seq0.unique()
    seq1 = seq1.unique()
    seq2 = seq0 - seq1
    expected = set(seq0.decode()) - set(seq1.decode())
    assert set(seq2.decode()) == expected


def test_filter_by_count():
    size = 16
    # size = 1 << 16
    values = np.random.randint(0, size, size=size)
    values.sort()
    counts = np.bincount(values)
    seq = pef.Sequence(values)
    min_count = 2
    max_count = 3
    seq = seq.filter_by_count(
        min_count=min_count, max_count=max_count, write_multiset=True
    )
    counts = np.bincount(values)
    expected = set(
        v for v in values if counts[v] >= min_count and counts[v] <= max_count
    )
    assert set(seq.decode()) == set(expected)


def test_version():
    assert isinstance(pef.__version__, str) and len(pef.__version__) > 0
