import ppef
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
    seq = ppef.Sequence(values, block_size=block_size)
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
    tmp = NamedTemporaryFile(suffix=".ppef")
    seq.save(tmp.name)
    seq2 = ppef.Sequence(tmp.name)
    recon = seq2.decode()
    assert (np.array(recon) == values).all()


def test_serialization():
    max_value = 1 << 18
    n_elem = 1 << 16
    block_size = 1 << 7
    values = np.random.randint(0, max_value, size=n_elem)
    values.sort()
    seq = ppef.Sequence(values)
    serialized = seq.serialize()
    print(len(serialized))
    seq2 = ppef.deserialize(serialized)
    assert (np.array(seq.decode()) == values).all()


def test_pickling():
    max_value = 1 << 16
    n_elem = 1 << 12
    block_size = 1 << 7
    values = np.random.randint(0, max_value, size=n_elem)
    values.sort()
    seq = ppef.Sequence(values, block_size=block_size)
    serialized = pickle.dumps(seq)
    seq2 = pickle.loads(serialized)
    assert (np.array(seq2.decode()) == values).all()


def test_empty():
    """Stability test."""
    seq = ppef.Sequence([])
    assert seq.n_elem == 0
    serialized = seq.serialize()
    seq2 = ppef.deserialize(serialized)
    assert seq2.n_elem == 0


def test_intersect():
    values_0 = np.random.randint(0, 1 << 16, size=(1 << 16))
    values_1 = np.random.randint(0, 1 << 16, size=(1 << 16))
    values_0.sort()
    values_1.sort()
    seq0 = ppef.Sequence(values_0)
    seq1 = ppef.Sequence(values_1)
    seq2 = seq0 & seq1
    expected = set(seq0.decode()) & set(seq1.decode())
    assert set(seq2.decode()) == expected


def test_union():
    values_0 = np.random.randint(0, 1 << 16, size=(1 << 16))
    values_1 = np.random.randint(0, 1 << 16, size=(1 << 16))
    values_0.sort()
    values_1.sort()
    seq0 = ppef.Sequence(values_0)
    seq1 = ppef.Sequence(values_1)
    seq2 = seq0 | seq1
    expected = set(seq0.decode()) | set(seq1.decode())
    assert set(seq2.decode()) == expected


def test_version():
    assert isinstance(ppef.__version__, str) and len(ppef.__version__) > 0
