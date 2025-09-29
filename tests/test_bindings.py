import _ppef
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
    pef = _ppef.Sequence(values, block_size=block_size)
    assert pef.n_elem == n_elem
    assert pef.block_size == block_size

    # test blockwise decompression
    recon = pef.decode_block(0)
    assert (np.array(recon) == values[: len(recon)]).all()

    # test full decompression
    recon = pef.decode()
    assert (np.array(recon) == values).all()

    # "does-it-even-run?" tests
    _ = pef.get_meta()
    _ = pef.n_blocks

    # test serialization to a file
    tmp = NamedTemporaryFile(suffix=".ppef")
    pef.save(tmp.name)
    pef2 = _ppef.Sequence(tmp.name)
    recon = pef.decode()
    assert (np.array(recon) == values).all()


def test_serialization():
    max_value = 1 << 18
    n_elem = 1 << 16
    block_size = 1 << 7
    values = np.random.randint(0, max_value, size=n_elem)
    values.sort()
    seq = _ppef.Sequence(values)
    serialized = seq.serialize()
    print(len(serialized))
    seq2 = _ppef.deserialize(serialized)
    assert (np.array(seq.decode()) == values).all()


def test_pickling():
    max_value = 1 << 16
    n_elem = 1 << 12
    block_size = 1 << 7
    values = np.random.randint(0, max_value, size=n_elem)
    values.sort()
    pef = _ppef.Sequence(values, block_size=block_size)
    serialized = pickle.dumps(pef)
    pef2 = pickle.loads(serialized)
    assert (np.array(pef2.decode()) == values).all()
