import itertools, os, time, base64, hashlib, glob, random, sys, binascii, io, struct, subprocess
from math import ceil, floor, log, log2, sqrt
from time import sleep
from binascii import hexlify
from struct import pack, unpack
from PIL import Image, ImageChops, ImageOps, ImageEnhance
from pyzbar.pyzbar import decode
import pyqrcode
import zstd
import nacl.encoding
import nacl.signing
import moviepy.editor as mpy
import numpy as np
import pygame
import cv2
from selenium import webdriver
from selenium.webdriver.chrome.options import Options
from tqdm import tqdm
from anime_utility_functions_v1 import get_sha256_hash_of_input_data_func
from anime_fountain_coding_v1 import PRNG, BlockGraph\

sys.setrecursionlimit(10000)
sigma = "expand 32-byte k"
tau = "expand 16-byte k"
current_base_folder = os.getcwd() + os.path.sep
#pip install pyqrcode, pypng, pyzbar, tqdm, numpy, pygame, pynacl, selenium, zstd, cv2
np.seterr(over='ignore')

def get_blake2b_hash_func(input_data):
    try:
        hash_of_input_data = hashlib.blake2b(input_data).digest()
    except:
        hash_of_input_data = hashlib.blake2b(input_data.encode('utf-8')).digest()
    return hash_of_input_data

def get_blake2s_hash_func(input_data):
    try:
        hash_of_input_data = hashlib.blake2s(input_data).digest()
    except:
        hash_of_input_data = hashlib.blake2s(input_data.encode('utf-8')).digest()
    return hash_of_input_data

def get_blake2b_sha3_512_merged_hash_func(input_data_or_string):
    if isinstance(input_data_or_string, str):
        input_data_or_string = input_data_or_string.encode('utf-8')
    hash_of_input_data = hashlib.sha3_512(input_data_or_string).hexdigest() + hashlib.blake2b(input_data_or_string).hexdigest()
    return hash_of_input_data

def get_raw_blake2b_sha3_512_merged_hash_func(input_data):
    hash_of_input_data = hashlib.sha3_512(input_data).digest() + hashlib.blake2b(input_data).digest()
    return hash_of_input_data

def xor_func(b1, b2): # Expects two bytes objects of equal length, returns their XOR
    assert len(b1) == len(b2)
    return bytes([x ^ y for x, y in zip(b1, b2)])

def chunkbytes_func(a, n):
    return [a[ii:ii+n] for ii in range(0, len(a), n)]

def ints_from_4bytes_func(a):
    for chunk in chunkbytes_func(a, 4):
        yield int.from_bytes(chunk, byteorder= 'little')

def ints_to_4bytes_func(x):
    for v in x:
        yield int.to_bytes(v, length=4, byteorder= 'little')

def hash_tree_func(H, leafs):
    assert (len(leafs)& len(leafs) - 1) == 0  # test for full binary tree
    return l_tree_func(H, leafs)  # binary hash trees are special cases of L-Trees

def l_tree_func(H, leafs):
    layer = leafs
    yield layer
    for ii in range(ceil(log2(len(leafs)))):
        next_layer = [H(l, r, ii) for l, r in zip(layer[0::2], layer[1::2])]
        if len(layer)& 1:  # if there is a node left on this layer
            next_layer.append(layer[-1])
        layer = next_layer
        yield layer

def auth_path_func(tree, idx):
    path = []
    for layer in tree:
        if len(layer) == 1:  # if there are no neighbors
            break
        idx += 1 if (idx& 1 == 0) else -1  # neighbor node
        path.append(layer[idx])
        idx>>= 1  # parent node
    return path

def construct_root_func(H, auth_path, leaf, idx):
    node = leaf
    for ii, neighbor in enumerate(auth_path):
        if idx& 1 == 0:
            node = H(node, neighbor, ii)
        else:
            node = H(neighbor, node, ii)
        idx >>= 1
    return node

def root_func(tree):
    return list(tree)[-1][0]

class MyTimer():
    def __init__(self):
        self.start = time.time()
    def __enter__(self):
        return self
    def __exit__(self, exc_type, exc_val, exc_tb):
        end = time.time()
        runtime = end - self.start
        msg = '({time} seconds to complete)'
        print(msg.format(time=round(runtime,5)))

class ChaCha(object):
    def __init__(self, key=None, iv=None, rounds=12):
        assert rounds& 1 == 0
        self.rounds = rounds
        if key is None:
            key = bytes(32)
        if iv is None:
            iv = bytes(8)
        assert len(key) in [16, 32]
        assert len(iv) == 8
        self.state = []
        if len(key) == 32:
            c = bytes(sigma, 'latin-1')
            self.state += ints_from_4bytes_func(c)
            self.state += ints_from_4bytes_func(key)
        elif len(key) == 16:
            c = bytes(tau, 'latin-1')
            self.state += ints_from_4bytes_func(c)
            self.state += ints_from_4bytes_func(key)
            self.state += ints_from_4bytes_func(key)
        self.state += [0, 0]
        self.state += ints_from_4bytes_func(iv)

    def permuted(self, a):
        assert (len(a) == 16 and all(type(ii) is int for ii in a) or
                len(a) == 64 and type(a) in [bytes, bytearray])
        if len(a) == 64:
            x = list(ints_from_4bytes_func(a))
        else:
            x = list(a)

        def ROL32(x, n):
            return ((x<< n)& 0xFFFFFFFF) | (x>> (32 - n))

        def quarterround(x, a, b, c, d):
            x[a] = (x[a] + x[b]& 0xFFFFFFFF); x[d] = ROL32(x[d] ^ x[a], 16)
            x[c] = (x[c] + x[d]& 0xFFFFFFFF); x[b] = ROL32(x[b] ^ x[c], 12)
            x[a] = (x[a] + x[b]& 0xFFFFFFFF); x[d] = ROL32(x[d] ^ x[a], 8)
            x[c] = (x[c] + x[d]& 0xFFFFFFFF); x[b] = ROL32(x[b] ^ x[c], 7)

        for ii in range(0, self.rounds, 2):
            quarterround(x, 0, 4,  8, 12)
            quarterround(x, 1, 5,  9, 13)
            quarterround(x, 2, 6, 10, 14)
            quarterround(x, 3, 7, 11, 15)
            quarterround(x, 0, 5, 10, 15)
            quarterround(x, 1, 6, 11, 12)
            quarterround(x, 2, 7,  8, 13)
            quarterround(x, 3, 4,  9, 14)

        if len(a) == 16:
            for ii in range(16):
                x[ii] = (x[ii] + a[ii]& 0xFFFFFFFF)
        return b''.join(ints_to_4bytes_func(x))

    def keystream(self, N=64):
        output = bytes()
        for n in range(N, 0, -64):
            output += self.permuted(self.state)[:min(n, 64)]
            self.state[12] += 1
            if self.state[12]& 0xFFFFFFFF == 0:
                self.state[13] += 1
        return output

class HORST(object):
    def __init__(self, n, m, k, tau, F, H, Gt):
        assert k*tau == m
        self.n = n
        self.m = m
        self.k = k
        self.tau = tau
        self.t = 1<< tau
        self.F = F
        self.H = H
        self.x = max((k * x - (1<< x), x) for x in range(tau))[1] # minimising k(tau - x + 1) + 2^{x} implies maximising 'k*x - 2^{x}'
        self.Gt = lambda seed: Gt(seed=seed, n=self.t * self.n // 8)

    def message_indices(self, m):
        M = chunkbytes_func(m, self.tau // 8)
        M = [int.from_bytes(Mi, byteorder='little') for Mi in M]  # the reference implementation uses 'idx = m[2*i] + (m[2*i+1]<<8)' which suggests using little-endian byte order
        return M

    def keygen(self, seed, masks):
        assert len(seed) == self.n // 8
        assert len(masks) >= 2 * self.tau
        sk = self.Gt(seed)
        sk = chunkbytes_func(sk, self.n // 8)
        L = list(map(self.F, sk))
        H = lambda x, y, i: self.H(xor_func(x, masks[2*i]), xor_func(y, masks[2*i+1]))
        return root_func(hash_tree_func(H, L))

    def sign(self, m, seed, masks):
        assert len(m) == self.m // 8
        assert len(seed) == self.n // 8
        assert len(masks) >= 2 * self.tau
        sk = self.Gt(seed)
        sk = chunkbytes_func(sk, self.n // 8)
        L = list(map(self.F, sk))
        H = lambda x, y, i: self.H(xor_func(x, masks[2*i]), xor_func(y, masks[2*i+1]))
        tree = hash_tree_func(H, L)
        trunk = list(itertools.islice(tree, 0, self.tau - self.x))
        sigma_k = next(tree)
        M = self.message_indices(m)
        pk = root_func(tree)# the SPHINCS paper suggests to put sigma_k at the end of sigma but the reference code places it at the front
        return ([(sk[Mi], auth_path_func(trunk, Mi)) for Mi in M] + [sigma_k], pk)

    def verify(self, m, sig, masks):
        assert len(m) == self.m // 8
        assert len(masks) >= 2 * self.tau
        M = self.message_indices(m)
        H = lambda x, y, i: self.H(xor_func(x, masks[2*i]), xor_func(y, masks[2*i+1]))
        sigma_k = sig[-1]
        for (sk, path), Mi in zip(sig, M):
            leaf = self.F(sk)
            r = construct_root_func(H, path, leaf, Mi) # there is an error in the SPHINCS paper for this formula, as itstates that y_i = floor(M_i // 2^tau - x) rather than y_i = floor(M_i // 2^{tau - x})
            yi = Mi // (1<< (self.tau - self.x))
            if r != sigma_k[yi]:
                return False
        Qtop = masks[2*(self.tau - self.x):]
        H = lambda x, y, i: self.H(xor_func(x, Qtop[2*i]), xor_func(y, Qtop[2*i+1]))
        return root_func(hash_tree_func(H, sigma_k))

class WOTSplus(object):
    def __init__(self, n, w, F, Gl):
        self.n = n
        self.w = w
        self.l1 = ceil(n / log2(w))
        self.l2 = floor(log2(self.l1 * (w - 1)) / log2(w)) + 1
        self.l = self.l1 + self.l2
        self.F = F
        self.Gl = lambda seed: Gl(seed=seed, n=self.l * self.n // 8)

    def chains(self, x, masks, chainrange):
        x = list(x)
        for ii in range(self.l):
            for jj in chainrange[ii]:
                x[ii] = self.F(xor_func(x[ii], masks[jj]))
        return x

    def int_to_basew(self, x, base):
        for _ in range(self.l1):
            yield x % base
            x //= base

    def chainlengths(self, m):
        M = int.from_bytes(m, byteorder='little')
        M = list(self.int_to_basew(M, self.w))
        C = sum(self.w - 1 - M[ii] for ii in range(self.l1))
        C = list(self.int_to_basew(C, self.w))
        return M + C

    def keygen(self, seed, masks):
        sk = self.Gl(seed)
        sk = chunkbytes_func(sk, self.n // 8)
        return self.chains(sk, masks, [range(0, self.w-1)]*self.l)

    def sign(self, m, seed, masks):
        sk = self.Gl(seed)
        sk = chunkbytes_func(sk, self.n // 8)
        B = self.chainlengths(m)
        return self.chains(sk, masks, [range(0, b) for b in B])

    def verify(self, m, sig, masks):
        B = self.chainlengths(m)
        return self.chains(sig, masks, [range(b, self.w-1) for b in B])

class BLAKE(object):
    IV64 = [
        0x6A09E667F3BCC908, 0xBB67AE8584CAA73B,
        0x3C6EF372FE94F82B, 0xA54FF53A5F1D36F1,
        0x510E527FADE682D1, 0x9B05688C2B3E6C1F,
        0x1F83D9ABFB41BD6B, 0x5BE0CD19137E2179,
    ]

    IV48 = [
        0xCBBB9D5DC1059ED8, 0x629A292A367CD507,
        0x9159015A3070DD17, 0x152FECD8F70E5939,
        0x67332667FFC00B31, 0x8EB44A8768581511,
        0xDB0C2E0D64F98FA7, 0x47B5481DBEFA4FA4,
    ]

    IV32 = [
        0x6A09E667, 0xBB67AE85,
        0x3C6EF372, 0xA54FF53A,
        0x510E527F, 0x9B05688C,
        0x1F83D9AB, 0x5BE0CD19,
    ]

    IV28 = [
        0xC1059ED8, 0x367CD507,
        0x3070DD17, 0xF70E5939,
        0xFFC00B31, 0x68581511,
        0x64F98FA7, 0xBEFA4FA4,
    ]

    C64 = [
        0x243F6A8885A308D3, 0x13198A2E03707344,
        0xA4093822299F31D0, 0x082EFA98EC4E6C89,
        0x452821E638D01377, 0xBE5466CF34E90C6C,
        0xC0AC29B7C97C50DD, 0x3F84D5B5B5470917,
        0x9216D5D98979FB1B, 0xD1310BA698DFB5AC,
        0x2FFD72DBD01ADFB7, 0xB8E1AFED6A267E96,
        0xBA7C9045F12C7F99, 0x24A19947B3916CF7,
        0x0801F2E2858EFC16, 0x636920D871574E69,
    ]

    C32 = [
        0x243F6A88, 0x85A308D3,
        0x13198A2E, 0x03707344,
        0xA4093822, 0x299F31D0,
        0x082EFA98, 0xEC4E6C89,
        0x452821E6, 0x38D01377,
        0xBE5466CF, 0x34E90C6C,
        0xC0AC29B7, 0xC97C50DD,
        0x3F84D5B5, 0xB5470917,
    ]

    SIGMA = [
        [ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15],
        [14,10, 4, 8, 9,15,13, 6, 1,12, 0, 2,11, 7, 5, 3],
        [11, 8,12, 0, 5, 2,15,13,10,14, 3, 6, 7, 1, 9, 4],
        [ 7, 9, 3, 1,13,12,11,14, 2, 6, 5,10, 4, 0,15, 8],
        [ 9, 0, 5, 7, 2, 4,10,15,14, 1,11,12, 6, 8, 3,13],
        [ 2,12, 6,10, 0,11, 8, 3, 4,13, 7, 5,15,14, 1, 9],
        [12, 5, 1,15,14,13, 4,10, 0, 7, 6, 3, 9, 2, 8,11],
        [13,11, 7,14,12, 1, 3, 9, 5, 0,15, 4, 8, 6, 2,10],
        [ 6,15,14, 9,11, 3, 0, 8,12, 2,13, 7, 1, 4,10, 5],
        [10, 2, 8, 4, 7, 6, 1, 5,15,11, 9,14, 3,12,13, 0],
        [ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15],
        [14,10, 4, 8, 9,15,13, 6, 1,12, 0, 2,11, 7, 5, 3],
        [11, 8,12, 0, 5, 2,15,13,10,14, 3, 6, 7, 1, 9, 4],
        [ 7, 9, 3, 1,13,12,11,14, 2, 6, 5,10, 4, 0,15, 8],
        [ 9, 0, 5, 7, 2, 4,10,15,14, 1,11,12, 6, 8, 3,13],
        [ 2,12, 6,10, 0,11, 8, 3, 4,13, 7, 5,15,14, 1, 9],
        [12, 5, 1,15,14,13, 4,10, 0, 7, 6, 3, 9, 2, 8,11],
        [13,11, 7,14,12, 1, 3, 9, 5, 0,15, 4, 8, 6, 2,10],
        [ 6,15,14, 9,11, 3, 0, 8,12, 2,13, 7, 1, 4,10, 5],
        [10, 2, 8, 4, 7, 6, 1, 5,15,11, 9,14, 3,12,13, 0],
    ]

    MASK32BITS = 0xFFFFFFFF
    MASK64BITS = 0xFFFFFFFFFFFFFFFF

    def __init__(self, hashbitlen):
        if hashbitlen not in [224, 256, 384, 512]:
            raise Exception('hash length not 224, 256, 384 or 512')
        self.hashbitlen = hashbitlen
        self.h     = [0]*8  # current chain value (initialized to the IV)
        self.t     = 0      # number of *BITS* hashed so far
        self.cache = b''    # cached leftover data not yet compressed
        self.salt  = [0]*4  # salt (null by default)
        self.state = 1      # set to 2 by update and 3 by final
        self.nullt = 0      # Boolean value for special case \ell_i=0
        if (hashbitlen == 224) or (hashbitlen == 256):
            self.byte2int  = self._fourByte2int
            self.int2byte  = self._int2fourByte
            self.MASK      = self.MASK32BITS
            self.WORDBYTES = 4
            self.WORDBITS  = 32
            self.BLKBYTES  = 64
            self.BLKBITS   = 512
            self.ROUNDS    = 14     # was 10 before round 3
            self.cxx  = self.C32
            self.rot1 = 16          # num bits to shift in G
            self.rot2 = 12          # num bits to shift in G
            self.rot3 = 8           # num bits to shift in G
            self.rot4 = 7           # num bits to shift in G
            self.mul  = 0   # for 32-bit words, 32<<self.mul where self.mul = 0
            if hashbitlen == 224:
                self.h = self.IV28[:]
            else:
                self.h = self.IV32[:]

        elif (hashbitlen == 384) or (hashbitlen == 512):
            self.byte2int  = self._eightByte2int
            self.int2byte  = self._int2eightByte
            self.MASK      = self.MASK64BITS
            self.WORDBYTES = 8
            self.WORDBITS  = 64
            self.BLKBYTES  = 128
            self.BLKBITS   = 1024
            self.ROUNDS    = 16     # was 14 before round 3
            self.cxx  = self.C64
            self.rot1 = 32          # num bits to shift in G
            self.rot2 = 25          # num bits to shift in G
            self.rot3 = 16          # num bits to shift in G
            self.rot4 = 11          # num bits to shift in G
            self.mul  = 1   # for 64-bit words, 32<<self.mul where self.mul = 1
            if hashbitlen == 384:
                self.h = self.IV48[:]
            else:
                self.h = self.IV64[:]

    def _compress(self, block):
        byte2int = self.byte2int
        mul      = self.mul       # de-reference these for  ...speed?  ;-)
        cxx      = self.cxx
        rot1     = self.rot1
        rot2     = self.rot2
        rot3     = self.rot3
        rot4     = self.rot4
        MASK     = self.MASK
        WORDBITS = self.WORDBITS
        SIGMA    = self.SIGMA
        m = [byte2int(block[ii<<2<<mul:(ii<<2<<mul)+(4<<mul)]) for ii in range(16)]         # get message       (<<2 is the same as *4 but faster)
        v = [0]*16 # initialization
        v[ 0: 8] = [self.h[ii] for ii in range(8)]
        v[ 8:16] = [self.cxx[ii] for ii in range(8)]
        v[ 8:12] = [v[8+ii] ^ self.salt[ii] for ii in range(4)]
        if self.nullt == 0:        #    (i>>1 is the same as i/2 but faster)
            v[12] = v[12] ^ (self.t& MASK)
            v[13] = v[13] ^ (self.t& MASK)
            v[14] = v[14] ^ (self.t>> self.WORDBITS)
            v[15] = v[15] ^ (self.t>> self.WORDBITS)

        def G(a, b, c, d, ii):
            va = v[a]   # it's faster to deref and reref later
            vb = v[b]
            vc = v[c]
            vd = v[d]
            sri  = SIGMA[round][ii]
            sri1 = SIGMA[round][ii+1]
            va = ((va + vb) + (m[sri] ^ cxx[sri1]) )& MASK
            x  =  vd ^ va
            vd = (x>> rot1) | ((x<< (WORDBITS-rot1))& MASK)
            vc = (vc + vd)& MASK
            x  =  vb ^ vc
            vb = (x>> rot2) | ((x<< (WORDBITS-rot2))& MASK)
            va = ((va + vb) + (m[sri1] ^ cxx[sri]) )& MASK
            x  =  vd ^ va
            vd = (x>> rot3) | ((x<< (WORDBITS-rot3))& MASK)
            vc = (vc + vd)& MASK
            x  =  vb ^ vc
            vb = (x>> rot4) | ((x<< (WORDBITS-rot4))& MASK)
            v[a] = va
            v[b] = vb
            v[c] = vc
            v[d] = vd
        for round in range(self.ROUNDS):
            G( 0, 4, 8,12, 0) # column step
            G( 1, 5, 9,13, 2)
            G( 2, 6,10,14, 4)
            G( 3, 7,11,15, 6)
            G( 0, 5,10,15, 8) # diagonal step
            G( 1, 6,11,12,10)
            G( 2, 7, 8,13,12)
            G( 3, 4, 9,14,14)
        self.h = [self.h[ii]^v[ii]^v[ii+8]^self.salt[ii& 0x3] for ii in range(8)] # save current hash value   (use i&0x3 to get 0,1,2,3,0,1,2,3)

    def addsalt(self, salt):
        if self.state != 1:
            raise Exception('addsalt() not called after init() and before update()')
        saltsize = self.WORDBYTES * 4  #  salt size is to be 4x word size; if too short, prefix with null bytes.  if too long,truncate high order bytes
        if len(salt) < saltsize:
            salt = (bytes(0)*(saltsize-len(salt)) + salt)
        else:
            salt = salt[-saltsize:] # prep the salt array:
        self.salt[0] = self.byte2int(salt[            : 4<<self.mul])
        self.salt[1] = self.byte2int(salt[ 4<<self.mul: 8<<self.mul])
        self.salt[2] = self.byte2int(salt[ 8<<self.mul:12<<self.mul])
        self.salt[3] = self.byte2int(salt[12<<self.mul:            ])

    def update(self, data):
        self.state = 2
        BLKBYTES = self.BLKBYTES   # de-referenced for improved readability
        BLKBITS  = self.BLKBITS
        datalen = len(data)
        if not datalen:  return
        if type(data) == type(u''):
            data = data.encode('UTF-8')         # converts to byte string
        left = len(self.cache)
        fill = BLKBYTES - left
        if left and datalen >= fill:  # if any cached data and any added new data will fill a full block, fill and compress
            self.cache = self.cache + data[:fill]
            self.t += BLKBITS           # update counter
            self._compress(self.cache)
            self.cache = b''
            data = data[fill:]
            datalen -= fill
        while datalen >= BLKBYTES:          # compress new data until not enough for a full block
            self.t += BLKBITS           # update counter
            self._compress(data[:BLKBYTES])
            data = data[BLKBYTES:]
            datalen -= BLKBYTES
        if datalen > 0:
            self.cache = self.cache + data[:datalen] # cache all leftover bytes until next call to update()

    def final(self, data=''):
        if self.state == 3:
            return self.hash
        if data:
            self.update(data)
        ZZ = b'\x00'
        ZO = b'\x01'
        OZ = b'\x80'
        OO = b'\x81'
        PADDING = OZ + ZZ*128   # pre-formatted padding data
        tt = self.t + (len(self.cache)<< 3)
        if self.BLKBYTES == 64:
            msglen = self._int2eightByte(tt)
        else:
            low  = tt& self.MASK
            high = tt>> self.WORDBITS
            msglen = self._int2eightByte(high) + self._int2eightByte(low)
        sizewithout = self.BLKBYTES -  ((self.WORDBITS>>2)+1)
        if len(self.cache) == sizewithout:
            self.t -= 8
            if self.hashbitlen in [224, 384]:
                self.update(OZ)
            else:
                self.update(OO)
        else:
            if len(self.cache) < sizewithout:
                if len(self.cache) == 0:
                    self.nullt=1
                self.t -= (sizewithout - len(self.cache))<< 3
                self.update(PADDING[:sizewithout - len(self.cache)])
            else:
                self.t -= (self.BLKBYTES - len(self.cache))<< 3
                self.update(PADDING[:self.BLKBYTES - len(self.cache)])
                self.t -= (sizewithout+1)<< 3
                self.update(PADDING[1:sizewithout+1]) # pad with zeroes
                self.nullt = 1 # raise flag to set t=0 at the next _compress
            if self.hashbitlen in [224, 384]:
                self.update(ZZ)
            else:
                self.update(ZO)
            self.t -= 8
        self.t -= self.BLKBYTES
        self.update(msglen)
        hashval = []
        if self.BLKBYTES == 64:
            for h in self.h:
                hashval.append(self._int2fourByte(h))
        else:
            for h in self.h:
                hashval.append(self._int2eightByte(h))
        self.hash  = b''.join(hashval)[:self.hashbitlen>> 3]
        self.state = 3
        return self.hash
    digest = final      # may use digest() as a synonym for final()

    def hexdigest(self, data=''):
        return hexlify(self.final(data)).decode('UTF-8')

    def _fourByte2int(self, bytestr):      # convert a 4-byte string to an int (long); see also long2byt() below
        return unpack('!L', bytestr)[0]

    def _eightByte2int(self, bytestr): #convert a 8-byte string to an int (long long)
        return unpack('!Q', bytestr)[0]

    def _int2fourByte(self, x): #convert a number to a 4-byte string, high order truncation possible (in Python x could be a BIGNUM) see also long2byt() below
        return pack('!L', x)

    def _int2eightByte(self, x): #convert a number to a 8-byte string, high order truncation possible (in Python x could be a BIGNUM)
        return pack('!Q', x)

class SPHINCS(object): #Source: https://github.com/joostrijneveld/SPHINCS-256-py
    def __init__(self, n=256, m=512, h=60, d=12, w=16, tau=16, k=32):
        self.n = n
        self.m = m
        self.h = h
        self.d = d
        self.w = w
        self.tau = tau
        self.t = 1<< tau
        self.k = k
        #self.Hdigest = lambda r, m: BLAKE(512).digest(r + m)
        #self.Fa = lambda a, k: BLAKE(256).digest(k + a)
        #self.Frand = lambda m, k: BLAKE(512).digest(k + m)
        self.Hdigest = lambda r, m: get_blake2b_hash_func(r + m)
        self.Fa = lambda a, k: get_blake2s_hash_func(k + a)
        self.Frand = lambda m, k: get_blake2b_hash_func(k + m)
        C = bytes("expand 32-byte to 64-byte state!", 'latin-1')
        perm = ChaCha().permuted
        self.Glambda = lambda seed, n: ChaCha(key=seed).keystream(n)
        self.F = lambda m: perm(m + C)[:32]
        self.H = lambda m1, m2: perm(xor_func(perm(m1 + C), m2 + bytes(32)))[:32]
        self.wots = WOTSplus(n=n, w=w, F=self.F, Gl=self.Glambda)
        self.horst = HORST(n=n, m=m, k=k, tau=tau, F=self.F, H=self.H, Gt=self.Glambda)

    def address(self, level, subtree, leaf):
        t = level | (subtree<< 4) | (leaf<< 59)
        return int.to_bytes(t, length=8, byteorder='little')

    def wots_leaf(self, address, SK1, masks):
        seed = self.Fa(address, SK1)
        pk_A = self.wots.keygen(seed, masks)
        H = lambda x, y, i: self.H(xor_func(x, masks[2*i]), xor_func(y, masks[2*i+1]))
        return root_func(l_tree_func(H, pk_A))

    def wots_path(self, a, SK1, Q, subh):
        ta = dict(a)
        leafs = []
        for subleaf in range(1<< subh):
            ta['leaf'] = subleaf
            leafs.append(self.wots_leaf(self.address(**ta), SK1, Q))
        Qtree = Q[2 * ceil(log(self.wots.l, 2)):]
        H = lambda x, y, i: self.H(xor_func(x, Qtree[2*i]), xor_func(y, Qtree[2*i+1]))
        tree = list(hash_tree_func(H, leafs))
        return auth_path_func(tree, a['leaf']), root_func(tree)

    def keygen(self):
        SK1 = os.urandom(self.n // 8)
        SK2 = os.urandom(self.n // 8)
        p = max(self.w-1, 2 * (self.h + ceil(log(self.wots.l, 2))), 2*self.tau)
        Q = [os.urandom(self.n // 8) for _ in range(p)]
        PK1 = self.keygen_pub(SK1, Q)
        return (SK1, SK2, Q), (PK1, Q)

    def keygen_pub(self, SK1, Q):
        addresses = [self.address(self.d - 1, 0, ii)
                     for ii in range(1<< (self.h//self.d))]
        leafs = [self.wots_leaf(A, SK1, Q) for A in addresses]
        Qtree = Q[2 * ceil(log(self.wots.l, 2)):]
        H = lambda x, y, ii: self.H(xor_func(x, Qtree[2*ii]), xor_func(y, Qtree[2*ii+1]))
        PK1 = root_func(hash_tree_func(H, leafs))
        return PK1

    def sign(self, M, SK):
        SK1, SK2, Q = SK
        R = self.Frand(M, SK2)
        R1, R2 = R[:self.n // 8], R[self.n // 8:]
        D = self.Hdigest(R1, M)
        ii = int.from_bytes(R2, byteorder='little') # ii = int.from_bytes(R2, byteorder='big')
        ii>>= self.n - self.h
        subh = self.h // self.d
        a = {'level': self.d,
             'subtree': ii>> subh,
             'leaf': ii& ((1<< subh) - 1)}
        a_horst = self.address(**a)
        seed_horst = self.Fa(a_horst, SK1)
        sig_horst, pk_horst = self.horst.sign(D, seed_horst, Q)
        pk = pk_horst
        sig = [ii, R1, sig_horst]
        for level in range(self.d):
            a['level'] = level
            a_wots = self.address(**a)
            seed_wots = self.Fa(a_wots, SK1)
            wots_sig = self.wots.sign(pk, seed_wots, Q)
            sig.append(wots_sig)
            path, pk = self.wots_path(a, SK1, Q, subh)
            sig.append(path)
            a['leaf'] = a['subtree']& ((1<< subh) - 1)
            a['subtree'] >>= subh
        return tuple(sig)

    def verify(self, M, sig, PK):
        ii, R1, sig_horst, *sig = sig
        PK1, Q = PK
        Qtree = Q[2 * ceil(log(self.wots.l, 2)):]
        D = self.Hdigest(R1, M)
        pk = pk_horst = self.horst.verify(D, sig_horst, Q)
        if pk_horst is False:
            return False
        subh = self.h // self.d
        H = lambda x, y, ii: self.H(xor_func(x, Q[2*ii]), xor_func(y, Q[2*ii+1]))
        Ht = lambda x, y, ii: self.H(xor_func(x, Qtree[2*ii]), xor_func(y, Qtree[2*ii+1]))
        for _ in range(self.d):
            wots_sig, wots_path, *sig = sig
            pk_wots = self.wots.verify(pk, wots_sig, Q)
            leaf = root_func(l_tree_func(H, pk_wots))
            pk = construct_root_func(Ht, wots_path, leaf, ii& 0x1f)
            ii>>= subh
        return PK1 == pk

    def my_pack(self, x):
        if type(x) is bytes:
            return x
        if type(x) is int:  # needed for index i
            return int.to_bytes(x, length=(self.h+7)//8, byteorder='little')
        return b''.join([self.my_pack(a) for a in iter(x)])

    def my_unpack(self, sk=None, pk=None, sig=None, byteseq=None):
        n = self.n // 8
        if sk:
            return sk[:n], sk[n:2*n], self.my_unpack(byteseq=sk[2*n:])
        elif pk:
            return pk[:n], self.my_unpack(byteseq=pk[n:])
        elif byteseq:
            return [byteseq[ii:ii+n] for ii in range(0, len(byteseq), n)]
        elif sig:
            def prefix(x, n):
                return x[:n], x[n:]
            ii, sig = prefix(sig, (self.h+7)//8)
            ii = int.from_bytes(ii, byteorder='little')
            R1, sig = prefix(sig, n)
            sig_horst = []
            for _ in range(self.k):
                sk, sig = prefix(sig, n)
                auth, sig = prefix(sig, (self.tau - self.horst.x)*n)
                sig_horst.append((sk, self.my_unpack(byteseq=auth)))
            sigma_k, sig = prefix(sig, (1<< self.horst.x) * n)
            sig_horst.append(self.my_unpack(byteseq=sigma_k))
            wots = []
            for _ in range(self.d):
                wots_sig, sig = prefix(sig, self.wots.l*n)
                path, sig = prefix(sig, self.h//self.d*n)
                wots.append(self.my_unpack(byteseq=wots_sig))
                wots.append(self.my_unpack(byteseq=path))
            return (ii, R1, sig_horst) + tuple(wots)

def sqrt4k3(x,p): return pow(x,(p + 1)//4,p)

#Compute candidate square root of x modulo p, with p = 5 (mod 8).
def sqrt8k5(x,p):
    y = pow(x,(p+3)//8,p)
    #If the square root exists, it is either y, or y*2^(p-1)/4.
    if (y * y) % p == x % p: return y
    else:
        z = pow(2,(p - 1)//4,p)
        return (y * z) % p

#Decode a hexadecimal string representation of integer.
def hexi(s): return int.from_bytes(bytes.fromhex(s), byteorder="big")

#Rotate a word x by b places to the left.
def rol(x,b): return ((x << b) | (x >> (64 - b))) & (2**64-1)

#From little-endian.
def from_le(s): return int.from_bytes(s, byteorder="little")

#Do the SHA-3 state transform on state s.
def sha3_transform(s):
    ROTATIONS = [0,1,62,28,27,36,44,6,55,20,3,10,43,25,39,41,45,15,\
                 21,8,18,2,61,56,14]
    PERMUTATION = [1,6,9,22,14,20,2,12,13,19,23,15,4,24,21,8,16,5,3,\
                   18,17,11,7,10]
    RC = [0x0000000000000001,0x0000000000008082,0x800000000000808a,\
          0x8000000080008000,0x000000000000808b,0x0000000080000001,\
          0x8000000080008081,0x8000000000008009,0x000000000000008a,\
          0x0000000000000088,0x0000000080008009,0x000000008000000a,\
          0x000000008000808b,0x800000000000008b,0x8000000000008089,\
          0x8000000000008003,0x8000000000008002,0x8000000000000080,\
          0x000000000000800a,0x800000008000000a,0x8000000080008081,\
          0x8000000000008080,0x0000000080000001,0x8000000080008008]
    for rnd in range(0,24):
        #AddColumnParity (Theta)
        c = [0]*5
        d = [0]*5
        for i in range(0,25): c[i%5]^=s[i]
        for i in range(0,5): d[i]=c[(i+4)%5]^rol(c[(i+1)%5],1)
        for i in range(0,25): s[i]^=d[i%5]
        #RotateWords (Rho).
        for i in range(0,25): s[i]=rol(s[i],ROTATIONS[i])
        #PermuteWords (Pi)
        t = s[PERMUTATION[0]]
        for i in range(0,len(PERMUTATION)-1):
            s[PERMUTATION[i]]=s[PERMUTATION[i+1]]
        s[PERMUTATION[-1]]=t
        #NonlinearMixRows (Chi)
        for i in range(0,25,5):
            t=[s[i],s[i+1],s[i+2],s[i+3],s[i+4],s[i],s[i+1]]
            for j in range(0,5): s[i+j]=t[j]^((~t[j+1])&(t[j+2]))
        #AddRoundConstant (Iota)
        s[0]^=RC[rnd]

#Reinterpret octet array b to word array and XOR it to state s.
def reinterpret_to_words_and_xor(s,b):
    for j in range(0,len(b)//8):
        s[j]^=from_le(b[8*j:][:8])

#Reinterpret word array w to octet array and return it.
def reinterpret_to_octets(w):
    mp=bytearray()
    for j in range(0,len(w)):
        mp+=w[j].to_bytes(8,byteorder="little")
    return mp

#(semi-)generic SHA-3 implementation
def sha3_raw(msg,r_w,o_p,e_b):
    r_b=8*r_w
    s=[0]*25
    #Handle whole blocks.
    idx=0
    blocks=len(msg)//r_b
    for i in range(0,blocks):
        reinterpret_to_words_and_xor(s,msg[idx:][:r_b])
        idx+=r_b
        sha3_transform(s)
    #Handle last block padding.
    m=bytearray(msg[idx:])
    m.append(o_p)
    while len(m) < r_b: m.append(0)
    m[len(m)-1]|=128
    #Handle padded last block.
    reinterpret_to_words_and_xor(s,m)
    sha3_transform(s)
    #Output.
    out = bytearray()
    while len(out)<e_b:
        out+=reinterpret_to_octets(s[:r_w])
        sha3_transform(s)
    return out[:e_b]

#Implementation of SHAKE256 functions.
def shake256(msg,olen):
    return sha3_raw(msg,17,31,olen)

#A (prime) field element.
class Field:
    #Construct number x (mod p).
    def __init__(self,x,p):
        self.__x=x%p
        self.__p=p
    #Check that fields of self and y are the same.
    def __check_fields(self,y):
        if type(y) is not Field or self.__p!=y.__p:
            raise ValueError("Fields don't match")
    #Field addition. The fields must match.
    def __add__(self,y):
        self.__check_fields(y)
        return Field(self.__x+y.__x,self.__p)
    #Field subtraction. The fields must match.
    def __sub__(self,y):
        self.__check_fields(y)
        return Field(self.__p+self.__x-y.__x,self.__p)
    #Field negation.
    def __neg__(self):
        return Field(self.__p-self.__x,self.__p)
    #Field multiplication. The fields must match.
    def __mul__(self,y):
        self.__check_fields(y)
        return Field(self.__x*y.__x,self.__p)
    #Field division. The fields must match.
    def __truediv__(self,y):
        return self*y.inv()
    #Field inverse (inverse of 0 is 0).
    def inv(self):
        return Field(pow(self.__x,self.__p-2,self.__p),self.__p)
    #Field square root. Returns none if square root does not exist.
    #Note: not presently implemented for p mod 8 = 1 case.
    def sqrt(self):
        #Compute candidate square root.
        if self.__p%4==3: y=sqrt4k3(self.__x,self.__p)
        elif self.__p%8==5: y=sqrt8k5(self.__x,self.__p)
        else: raise NotImplementedError("sqrt(_,8k+1)")
        _y=Field(y,self.__p)
        #Check square root candidate valid.
        return _y if _y*_y==self else None
    #Make Field element with the same field as this, but different
    #value.
    def make(self,ival): return Field(ival,self.__p)
    #Is field element the additive identity?
    def iszero(self): return self.__x==0
    #Are field elements equal?
    def __eq__(self,y): return self.__x==y.__x and self.__p==y.__p
    #Are field elements not equal?
    def __ne__(self,y): return not (self==y)
    #Serialize number to b-1 bits.
    def tobytes(self,b):
        return self.__x.to_bytes(b//8,byteorder="little")
    #Unserialize number from bits.
    def frombytes(self,x,b):
        rv=from_le(x)%(2**(b-1))
        return Field(rv,self.__p) if rv<self.__p else None
    #Compute sign of number, 0 or 1. The sign function
    #has the following property:
    #sign(x) = 1 - sign(-x) if x != 0.
    def sign(self): return self.__x%2

#A point on (twisted) Edwards curve.
class EdwardsPoint:
    base_field = None
    x = None
    y = None
    z = None
    def initpoint(self, x, y):
        self.x=x
        self.y=y
        self.z=self.base_field.make(1)
    def decode_base(self,s,b):
        #Check that point encoding is of correct length.
        if len(s)!=b//8: return (None,None)
        #Extract signbit.
        xs=s[(b-1)//8]>>((b-1)&7)
        #Decode y. If this fails, fail.
        y = self.base_field.frombytes(s,b)
        if y is None: return (None,None)
        #Try to recover x. If it does not exist, or is zero and xs is
        #wrong, fail.
        x=self.solve_x2(y).sqrt()
        if x is None or (x.iszero() and xs!=x.sign()):
            return (None,None)
        #If sign of x isn't correct, flip it.
        if x.sign()!=xs: x=-x
        # Return the constructed point.
        return (x,y)
    def encode_base(self,b):
        xp,yp=self.x/self.z,self.y/self.z
        #Encode y.
        s=bytearray(yp.tobytes(b))
        #Add sign bit of x to encoding.
        if xp.sign()!=0: s[(b-1)//8]|=1<<(b-1)%8
        return s
    def __mul__(self,x):
        r=self.zero_elem()
        s=self
        while x > 0:
            if (x%2)>0:
                r=r+s
            s=s.double()
            x=x//2
        return r
    #Check two points are equal.
    def __eq__(self,y):
        #Need to check x1/z1 == x2/z2 and similarly for y, so cross-
        #multiply to eliminate divisions.
        xn1=self.x*y.z
        xn2=y.x*self.z
        yn1=self.y*y.z
        yn2=y.y*self.z
        return xn1==xn2 and yn1==yn2
    #Check two points are not equal.
    def __ne__(self,y): return not (self==y)

#A point on Edwards25519
class Edwards25519Point(EdwardsPoint):
    #Create a new point on curve.
    base_field=Field(1,2**255-19)
    d=-base_field.make(121665)/base_field.make(121666)
    f0=base_field.make(0)
    f1=base_field.make(1)
    xb=base_field.make(hexi("216936D3CD6E53FEC0A4E231FDD6DC5C692CC7609525A7B2C9562D608F25D51A"))
    yb=base_field.make(hexi("6666666666666666666666666666666666666666666666666666666666666658"))
    #The standard base point.
    @staticmethod
    def stdbase():
        return Edwards25519Point(Edwards25519Point.xb,\
            Edwards25519Point.yb)
    def __init__(self,x,y):
        #Check the point is actually on the curve.
        if y*y-x*x!=self.f1+self.d*x*x*y*y:
            raise ValueError("Invalid point")
        self.initpoint(x, y)
        self.t=x*y
    #Decode a point representation.
    def decode(self,s):
        x,y=self.decode_base(s,256)
        return Edwards25519Point(x, y) if x is not None else None
    #Encode a point representation
    def encode(self):
        return self.encode_base(256)
    #Construct neutral point on this curve.
    def zero_elem(self):
        return Edwards25519Point(self.f0,self.f1)
    #Solve for x^2.
    def solve_x2(self,y):
        return ((y*y-self.f1)/(self.d*y*y+self.f1))
    #Point addition.
    def __add__(self,y):
        #The formulas are from EFD.
        tmp=self.zero_elem()
        zcp=self.z*y.z
        A=(self.y-self.x)*(y.y-y.x)
        B=(self.y+self.x)*(y.y+y.x)
        C=(self.d+self.d)*self.t*y.t
        D=zcp+zcp
        E,H=B-A,B+A
        F,G=D-C,D+C
        tmp.x,tmp.y,tmp.z,tmp.t=E*F,G*H,F*G,E*H
        return tmp
    #Point doubling.
    def double(self):
        #The formulas are from EFD (with assumption a=-1 propagated).
        tmp=self.zero_elem()
        A=self.x*self.x
        B=self.y*self.y
        Ch=self.z*self.z
        C=Ch+Ch
        H=A+B
        xys=self.x+self.y
        E=H-xys*xys
        G=A-B
        F=C+G
        tmp.x,tmp.y,tmp.z,tmp.t=E*F,G*H,F*G,E*H
        return tmp
    #Order of basepoint.
    def l(self):
        return hexi("1000000000000000000000000000000014def9dea2f79cd65812631a5cf5d3ed")
    #The logarithm of cofactor.
    def c(self): return 3
    #The highest set bit
    def n(self): return 254
    #The coding length
    def b(self): return 256
    #Validity check (for debugging)
    def is_valid_point(self):
        x,y,z,t=self.x,self.y,self.z,self.t
        x2=x*x
        y2=y*y
        z2=z*z
        lhs=(y2-x2)*z2
        rhs=z2*z2+self.d*x2*y2
        assert(lhs == rhs)
        assert(t*z == x*y)

#A point on Edward448
class Edwards448Point(EdwardsPoint):
    #Create a new point on curve.
    base_field=Field(1,2**448-2**224-1)
    d=base_field.make(-39081)
    f0=base_field.make(0)
    f1=base_field.make(1)
    xb=base_field.make(hexi("4F1970C66BED0DED221D15A622BF36DA9E146570470F1767EA6DE324A3D3A46412AE1AF72AB66511433B80E18B00938E2626A82BC70CC05E"))
    yb=base_field.make(hexi("693F46716EB6BC248876203756C9C7624BEA73736CA3984087789C1E05A0C2D73AD3FF1CE67C39C4FDBD132C4ED7C8AD9808795BF230FA14"))
    #The standard base point.
    @staticmethod
    def stdbase():
        return Edwards448Point(Edwards448Point.xb,Edwards448Point.yb)
    def __init__(self,x,y):
        #Check the point is actually on the curve.
        if y*y+x*x!=self.f1+self.d*x*x*y*y:
            raise ValueError("Invalid point")
        self.initpoint(x, y)
    #Decode a point representation.
    def decode(self,s):
        x,y=self.decode_base(s,456)
        return Edwards448Point(x, y) if x is not None else None
    #Encode a point representation
    def encode(self):
        return self.encode_base(456)
    #Construct neutral point on this curve.
    def zero_elem(self):
        return Edwards448Point(self.f0,self.f1)
    #Solve for x^2.
    def solve_x2(self,y):
        return ((y*y-self.f1)/(self.d*y*y-self.f1))
    #Point addition.
    def __add__(self,y):
        #The formulas are from EFD.
        tmp=self.zero_elem()
        xcp,ycp,zcp=self.x*y.x,self.y*y.y,self.z*y.z
        B=zcp*zcp
        E=self.d*xcp*ycp
        F,G=B-E,B+E
        tmp.x=zcp*F*((self.x+self.y)*(y.x+y.y)-xcp-ycp)
        tmp.y,tmp.z=zcp*G*(ycp-xcp),F*G
        return tmp
    #Point doubling.
    def double(self):
        #The formulas are from EFD.
        tmp=self.zero_elem()
        x1s,y1s,z1s=self.x*self.x,self.y*self.y,self.z*self.z
        xys=self.x+self.y
        F=x1s+y1s
        J=F-(z1s+z1s)
        tmp.x,tmp.y,tmp.z=(xys*xys-x1s-y1s)*J,F*(x1s-y1s),F*J
        return tmp
    #Order of basepoint.
    def l(self):
        return hexi("3fffffffffffffffffffffffffffffffffffffffffffffffffffffff7cca23e9c44edb49aed63690216cc2728dc58f552378c292ab5844f3")
    #The logarithm of cofactor.
    def c(self): return 2
    #The highest set bit
    def n(self): return 447
    #The coding length
    def b(self): return 456
    #Validity check (for debugging)
    def is_valid_point(self):
        x,y,z=self.x,self.y,self.z
        x2=x*x
        y2=y*y
        z2=z*z
        lhs=(x2+y2)*z2
        rhs=z2*z2+self.d*x2*y2
        assert(lhs == rhs)


class Edwards521Point(EdwardsPoint): #By JE based on https://mojzis.com/software/eddsa/eddsa.py
    #Create a new point on curve.
    base_field=Field(1,2**521 - 1)
    d=base_field.make(-376014)
    f0=base_field.make(0)
    f1=base_field.make(1)
    xb=base_field.make(hexi("752cb45c48648b189df90cb2296b2878a3bfd9f42fc6c818ec8bf3c9c0c6203913f6ecc5ccc72434b1ae949d568fc99c6059d0fb13364838aa302a940a2f19ba6c"))
    yb=base_field.make(hexi("0c")) # JE: See https://safecurves.cr.yp.to/base.html
    #The standard base point.
    @staticmethod
    def stdbase():
        return Edwards521Point(Edwards521Point.xb, Edwards521Point.yb)
    def __init__(self,x,y):
        #Check the point is actually on the curve.
        if y*y+x*x!=self.f1+self.d*x*x*y*y:
            raise ValueError("Invalid point")
        self.initpoint(x, y)
    #Decode a point representation.
    def decode(self,s):
        x,y=self.decode_base(s,528)
        return Edwards521Point(x, y) if x is not None else None
    #Encode a point representation
    def encode(self):
        return self.encode_base(528)
    #Construct neutral point on this curve.
    def zero_elem(self):
        return Edwards521Point(self.f0,self.f1)
    #Solve for x^2.
    def solve_x2(self,y):
        return ((y*y-self.f1)/(self.d*y*y-self.f1))
    #Point addition.
    def __add__(self,y):
        #The formulas are from EFD.
        tmp=self.zero_elem()
        xcp,ycp,zcp=self.x*y.x,self.y*y.y,self.z*y.z
        B=zcp*zcp
        E=self.d*xcp*ycp
        F,G=B-E,B+E
        tmp.x=zcp*F*((self.x+self.y)*(y.x+y.y)-xcp-ycp)
        tmp.y,tmp.z=zcp*G*(ycp-xcp),F*G
        return tmp
    #Point doubling.
    def double(self):
        #The formulas are from EFD.
        tmp=self.zero_elem()
        x1s,y1s,z1s=self.x*self.x,self.y*self.y,self.z*self.z
        xys=self.x+self.y
        F=x1s+y1s
        J=F-(z1s+z1s)
        tmp.x,tmp.y,tmp.z=(xys*xys-x1s-y1s)*J,F*(x1s-y1s),F*J
        return tmp
    #Order of basepoint.
    def l(self):
        return hexi("7ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffd15b6c64746fc85f736b8af5e7ec53f04fbd8c4569a8f1f4540ea2435f5180d6b")
    #The logarithm of cofactor.
    def c(self): return 2
    #The highest set bit
    def n(self): return 520
    #The coding length
    def b(self): return 528
    #Validity check (for debugging)
    def is_valid_point(self):
        x,y,z=self.x,self.y,self.z
        x2=x*x
        y2=y*y
        z2=z*z
        lhs=(x2+y2)*z2
        rhs=z2*z2+self.d*x2*y2
        assert(lhs == rhs)


class Edwards19937Point(EdwardsPoint): #By JE, made up: https://www.mersenne.org/primes/ %The usual recommendation for best performance is a = −1 if q mod 4 = 1, and a = 1 if q mod 4 = 3.
    #Create a new point on curve.
    base_field=Field(1,2**19937 - 1)
    d=base_field.make(-376014)
    f0=base_field.make(0)
    f1=base_field.make(1)
    xb=base_field.make(hexi("752cb45c48648b189df90cb2296b2878a3bfd9f42fc6c818ec8bf3c9c0c6203913f6ecc5ccc72434b1ae949d568fc99c6059d0fb13364838aa302a940a2f19ba6c"))
    yb=base_field.make(hexi("0c")) # JE: See https://safecurves.cr.yp.to/base.html
    #The standard base point.
    @staticmethod
    def stdbase():
        return Edwards521Point(Edwards521Point.xb, Edwards521Point.yb)
    def __init__(self,x,y):
        #Check the point is actually on the curve.
        if y*y+x*x!=self.f1+self.d*x*x*y*y:
            raise ValueError("Invalid point")
        self.initpoint(x, y)
    #Decode a point representation.
    def decode(self,s):
        x,y=self.decode_base(s,528)
        return Edwards521Point(x, y) if x is not None else None
    #Encode a point representation
    def encode(self):
        return self.encode_base(528)
    #Construct neutral point on this curve.
    def zero_elem(self):
        return Edwards521Point(self.f0,self.f1)
    #Solve for x^2.
    def solve_x2(self,y):
        return ((y*y-self.f1)/(self.d*y*y-self.f1))
    #Point addition.
    def __add__(self,y):
        #The formulas are from EFD.
        tmp=self.zero_elem()
        xcp,ycp,zcp=self.x*y.x,self.y*y.y,self.z*y.z
        B=zcp*zcp
        E=self.d*xcp*ycp
        F,G=B-E,B+E
        tmp.x=zcp*F*((self.x+self.y)*(y.x+y.y)-xcp-ycp)
        tmp.y,tmp.z=zcp*G*(ycp-xcp),F*G
        return tmp
    #Point doubling.
    def double(self):
        #The formulas are from EFD.
        tmp=self.zero_elem()
        x1s,y1s,z1s=self.x*self.x,self.y*self.y,self.z*self.z
        xys=self.x+self.y
        F=x1s+y1s
        J=F-(z1s+z1s)
        tmp.x,tmp.y,tmp.z=(xys*xys-x1s-y1s)*J,F*(x1s-y1s),F*J
        return tmp
    #Order of basepoint.
    def l(self):
        return hexi("7ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffd15b6c64746fc85f736b8af5e7ec53f04fbd8c4569a8f1f4540ea2435f5180d6b")
    #The logarithm of cofactor.
    def c(self): return 2
    #The highest set bit
    def n(self): return 19936
    #The coding length
    def b(self): return 528
    #Validity check (for debugging)
    def is_valid_point(self):
        x,y,z=self.x,self.y,self.z
        x2=x*x
        y2=y*y
        z2=z*z
        lhs=(x2+y2)*z2
        rhs=z2*z2+self.d*x2*y2
        assert(lhs == rhs)

#Simple self-check.
def curve_self_check(point):
    p=point
    q=point.zero_elem()
    z=q
    l=p.l()+1
    p.is_valid_point()
    q.is_valid_point()
    for i in range(0,point.b()):
        if (l>>i)&1 != 0:
            q=q+p
            q.is_valid_point()
        p=p.double()
        p.is_valid_point()
    assert q.encode() == point.encode()
    assert q.encode() != p.encode()
    assert q.encode() != z.encode()

#Simple self-check.
def self_check_curves():
    curve_self_check(Edwards25519Point.stdbase())
    curve_self_check(Edwards448Point.stdbase())

#PureEdDSA scheme.
#Limitation: Only b mod 8 = 0 is handled.
class PureEdDSA:
    #Create a new object.
    def __init__(self,properties):
        self.B=properties["B"]
        self.H=properties["H"]
        self.l=self.B.l()
        self.n=self.B.n()
        self.b=self.B.b()
        self.c=self.B.c()
    #Clamp a private scalar.
    def __clamp(self,a):
        _a = bytearray(a)
        for i in range(0,self.c): _a[i//8]&=~(1<<(i%8))
        _a[self.n//8]|=1<<(self.n%8)
        for i in range(self.n+1,self.b): _a[i//8]&=~(1<<(i%8))
        return _a
    #Generate a key. If privkey is None, random one is generated.
    #In any case, privkey, pubkey pair is returned.
    def keygen(self,privkey):
        #If no private key data given, generate random.
        if privkey is None: privkey= os.urandom(self.b//8)
        #Expand key.
        khash=self.H(privkey,None,None)
        a=from_le(self.__clamp(khash[:self.b//8]))
        #Return the keypair (public key is A=Enc(aB).
        return privkey,(self.B*a).encode()
    #Sign with keypair.
    def sign(self,privkey,pubkey,msg,ctx,hflag):
        #Expand key.
        khash=self.H(privkey,None,None)
        a=from_le(self.__clamp(khash[:self.b//8]))
        seed=khash[self.b//8:]
        #Calculate r and R (R only used in encoded form)
        r=from_le(self.H(seed+msg,ctx,hflag))%self.l
        R=(self.B*r).encode()
        #Calculate h.
        h=from_le(self.H(R+pubkey+msg,ctx,hflag))%self.l
        #Calculate s.
        S=((r+h*a)%self.l).to_bytes(self.b//8,byteorder="little")
        #The final signature is concatenation of R and S.
        return R+S
    #Verify signature with public key.
    def verify(self,pubkey,msg,sig,ctx,hflag):
        #Sanity-check sizes.
        if len(sig)!=self.b//4: return False
        if len(pubkey)!=self.b//8: return False
        #Split signature into R and S, and parse.
        Rraw,Sraw=sig[:self.b//8],sig[self.b//8:]
        R,S=self.B.decode(Rraw),from_le(Sraw)
        #Parse public key.
        A=self.B.decode(pubkey)
        #Check parse results.
        if (R is None) or (A is None) or S>=self.l: return False
        #Calculate h.
        h=from_le(self.H(Rraw+pubkey+msg,ctx,hflag))%self.l
        #Calculate left and right sides of check eq.
        rhs=R+(A*h)
        lhs=self.B*S
        for i in range(0, self.c):
            lhs = lhs.double()
            rhs = rhs.double()
        #Check eq. holds?
        return lhs==rhs

def Ed25519_inthash(data,ctx,hflag):
    if (ctx is not None and len(ctx) > 0) or hflag:
        raise ValueError("Contexts/hashes not supported")
    return hashlib.sha512(data).digest()

#The base PureEdDSA schemes.
pEd25519=PureEdDSA({\
    "B":Edwards25519Point.stdbase(),\
    "H":Ed25519_inthash\
})

def Ed25519ctx_inthash(data,ctx,hflag):
    dompfx = b""
    PREFIX=b"SigEd25519 no Ed25519 collisions"
    if ctx is not None:
        if len(ctx) > 255: raise ValueError("Context too big")
        dompfx=PREFIX+bytes([1 if hflag else 0,len(ctx)])+ctx
    return hashlib.sha512(dompfx+data).digest()

pEd25519ctx=PureEdDSA({\
    "B":Edwards25519Point.stdbase(),\
    "H":Ed25519ctx_inthash\
})

def Ed448_inthash(data,ctx,hflag):
    dompfx = b""
    if ctx is not None:
        if len(ctx) > 255: raise ValueError("Context too big")
        dompfx=b"SigEd448"+bytes([1 if hflag else 0,len(ctx)])+ctx
    return shake256(dompfx+data,114)

pEd448 = PureEdDSA({\
    "B":Edwards448Point.stdbase(),\
    "H":Ed448_inthash\
})

def Ed521_inthash(data, ctx, hflag):
    if (ctx is not None and len(ctx) > 0) or hflag:
        raise ValueError("Contexts/hashes not supported")
    return get_raw_blake2b_sha3_512_merged_hash_func(data)

pEd521 = PureEdDSA({\
    "B":Edwards521Point.stdbase(),\
    "H":Ed521_inthash\
})


#EdDSA scheme.
class EdDSA:
    #Create a new scheme object, with specified PureEdDSA base scheme and specified prehash.
    def __init__(self,pure_scheme,prehash):
        self.__pflag = True
        self.__pure=pure_scheme
        self.__prehash=prehash
        if self.__prehash is None:
            self.__prehash = lambda x,y:x
            self.__pflag = False
    # Generate a key. If privkey is none, generates a random privkey key, otherwise uses specified private key. Returns pair (privkey, pubkey).
    def keygen(self,privkey): return self.__pure.keygen(privkey)
    # Sign message msg using specified keypair.
    def sign(self,privkey,pubkey,msg,ctx=None):
        if ctx is None: ctx=b""
        return self.__pure.sign(privkey,pubkey,self.__prehash(msg,ctx),\
            ctx,self.__pflag)
    # Verify signature sig on message msg using public key pubkey.
    def verify(self,pubkey,msg,sig,ctx=None):
        if ctx is None: ctx=b""
        return self.__pure.verify(pubkey,self.__prehash(msg,ctx),sig,\
            ctx,self.__pflag)

def Ed448ph_prehash(data,ctx):
    return shake256(data,64)

#Our signature schemes.
Ed25519 = EdDSA(pEd25519,None)
Ed25519ctx = EdDSA(pEd25519ctx,None)
Ed25519ph = EdDSA(pEd25519ctx,lambda x,y:hashlib.sha512(x).digest())
Ed448 = EdDSA(pEd448,None)
Ed448ph = EdDSA(pEd448,Ed448ph_prehash)
Ed521 = EdDSA(pEd521,None)

def eddsa_obj(name):
    if name == "Ed25519": return Ed25519
    if name == "Ed25519ctx": return Ed25519ctx
    if name == "Ed25519ph": return Ed25519ph
    if name == "Ed448": return Ed448
    if name == "Ed448ph": return Ed448ph
    if name == "Ed521": Ed521
    raise NotImplementedError("Algorithm not implemented")


#Steganography Functions:
def convert_integer_tuple_to_binary_string_tuple_func(rgb_integer_tuple):
    r, g, b, a = rgb_integer_tuple
    rgb_binary_string_tuple = ('{0:08b}'.format(r), '{0:08b}'.format(g), '{0:08b}'.format(b), '{0:08b}'.format(a))
    return rgb_binary_string_tuple

def convert_binary_string_tuple_to_integer_tuple_func(rgb_binary_string_tuple):
    r, g, b, a = rgb_binary_string_tuple
    rgb_integer_tuple = (int(r, 2), int(g, 2), int(b, 2), int(a, 2))
    return rgb_integer_tuple

def merge_rgb_tuples_func(rgb_tuple_1, rgb_tuple_2): #Merge two RGB tuples.
    r1, g1, b1, a1 = rgb_tuple_1
    r2, g2, b2, a2 = rgb_tuple_2
    rgb_tuple_merged = (r1[:7] + r2[1],  g1[:7] + g2[1],  b1[:7] + b2[1],  a1[:7] + a2[1])
    return rgb_tuple_merged

def merge_two_images_func(host_image, image_to_embed):  #Merge two images. The second one will be merged into the first one.
    if image_to_embed.size[0] > host_image.size[0] or image_to_embed.size[1] > host_image.size[1]:
        raise ValueError('Error: Image 1 size must be largert than image 2 size!')
    pixel_map1 = host_image.load()
    pixel_map2 = image_to_embed.load()
    merged_image = Image.new(host_image.mode, host_image.size)
    pixels_new = merged_image.load()
    print('\nSteganographically merging the combined QR-code image with the host image...')
    pbar = tqdm(total=host_image.size[0]*host_image.size[1])
    for ii in range(host_image.size[0]):
        for jj in range(host_image.size[1]):
            pbar.update(1)
            rgb1 = convert_integer_tuple_to_binary_string_tuple_func( pixel_map1[ii, jj] )
            rgb2 = convert_integer_tuple_to_binary_string_tuple_func( (0, 0, 0, 0) ) # Use a black pixel as default
            if ii < image_to_embed.size[0] and jj < image_to_embed.size[1]:
                rgb2 = convert_integer_tuple_to_binary_string_tuple_func( pixel_map2[ii, jj] )
            rgb = merge_rgb_tuples_func(rgb1, rgb2)
            pixels_new[ii, jj] = convert_binary_string_tuple_to_integer_tuple_func(rgb)
    return merged_image

def unmerge_two_images_func(merged_image): #Unmerge an image. img: The input image. return: The unmerged/extracted image
    pixel_map = merged_image.load()
    embedded_image = Image.new(merged_image.mode, merged_image.size)
    pixels_new = embedded_image.load()
    original_size = merged_image.size
    print('\nNow attempting to separate the embedded image from the combined image...')
    pbar = tqdm(total=merged_image.size[0]*merged_image.size[1])
    for ii in range(merged_image.size[0]):
        for jj in range(merged_image.size[1]):
            pbar.update(1)
            r, g, b, a = convert_integer_tuple_to_binary_string_tuple_func( pixel_map[ii, jj] ) # Get the RGB (as a string tuple) from the current pixel
            rgb = ( r[-1] + '0000000', g[-1] + '0000000', b[-1] + '0000000', a[-1] + '0000000') # Extract the last 1 bit (corresponding to the hidden image);
            pixels_new[ii, jj] = convert_binary_string_tuple_to_integer_tuple_func(rgb) # Convert it to an integer tuple
            if pixels_new[ii, jj] != (0, 0, 0, 128): # If this is a 'valid' position, store it as the last valid position
                original_size = (ii + 1, jj + 1)
    embedded_image = embedded_image.crop((0, 0, original_size[0], original_size[1]))  # Crop the image based on the 'valid' pixels
    return embedded_image

def binarize_image_func(pil_image):
    thresh = 50
    fn = lambda x : 255 if x > thresh else 0
    monochrome_pil_image = pil_image.convert('L').point(fn, mode='1')
    return monochrome_pil_image

def check_if_image_is_all_black_func(pil_image):
    return not pil_image.getbbox() # Image.getbbox() returns the falsy None if there are no non-black pixels in the image, otherwise it returns a tuple of points, which is truthy

def check_if_image_is_all_white_func(pil_image):
    return not ImageChops.invert(pil_image).getbbox()

def invert_image_if_needed_func(pil_image):
    threshold = 120
    pil_image = pil_image.convert('RGB')
    upper_left_pixel = pil_image.getpixel((0, 0))
    average_pixel_value = (upper_left_pixel[0] + upper_left_pixel[1] + upper_left_pixel[2])/3
    if average_pixel_value < threshold:
        pil_image = ImageOps.invert(pil_image)
    pil_image = enhance_pil_image_func(pil_image, enhancement_amount=9)
    return pil_image

def enhance_pil_image_func(pil_image, enhancement_amount):
    try:
        x = pil_image.convert('RGB')
        contrast = ImageEnhance.Contrast(x)
        x = contrast.enhance(enhancement_amount)
        brightness = ImageEnhance.Brightness(x)
        x = brightness.enhance(enhancement_amount)
        color = ImageEnhance.Color(x)
        x = color.enhance(enhancement_amount)
        improved_pil_image = x
        return improved_pil_image
    except:
        return pil_image

def robust_base32_decode_func(s):
    if not isinstance(s, str):
        s = s.decode('utf-8')
    s = s + '=========='
    s = s.encode('utf-8')
    decoded = 0
    attempt_counter = 0
    decoded_binary_data = b''
    while not decoded and (attempt_counter < 50):
        try:
            attempt_counter = attempt_counter + 1
            s = s[:-1]
            decoded_binary_data = base64.b32decode(s)
            if type(decoded_binary_data) in [bytes, bytearray]:
                decoded = 1
        except:
            pass
    return decoded_binary_data

def get_screen_resolution_func():
    screen_resolutions = pygame.display.list_modes()
    desired_resolution = screen_resolutions[0]
    screen_width = desired_resolution[0]
    screen_height = desired_resolution[1]
    return screen_width, screen_height

def compress_data_with_zstd_func(input_data):
    zstd_compression_level = 1 #Highest (best) compression level is 22
    zstandard_compressor = zstd.ZstdCompressor(level=zstd_compression_level, write_content_size=True)
    zstd_compressed_data = zstandard_compressor.compress(input_data)
    return zstd_compressed_data

def decompress_data_with_zstd_func(zstd_compressed_data):
    zstandard_decompressor = zstd.ZstdDecompressor()
    uncompressed_data = zstandard_decompressor.decompress(zstd_compressed_data)
    return uncompressed_data

def encode_file_into_ztd_luby_block_qr_codes_func(input_data_or_path): #sphincs_secret_key_raw_bytes
    block_redundancy_factor = 1.5
    desired_block_size_in_bytes = 500
    c_constant = 0.1 #Don't touch
    delta_constant = 0.5 #Don't touch
    pyqrcode_version_number = 17 #Max is 40;
    qr_error_correcting_level = 'L' # L, M, Q, H
    qr_encoding_type = 'alphanumeric'
    qr_scale_factor = 4
    print('\nEncoding file into a collection of redundant QR codes...')
    if isinstance(input_data_or_path, str):
        if os.path.exists(input_data_or_path):
            with open(input_data_or_path,'rb') as f:
                input_data = f.read()
            filename = os.path.split(input_data_or_path)[-1]
    else:
        if type(input_data_or_path) in [bytes, bytearray]:
            input_data = input_data_or_path
            filename = ''
    uncompressed_input_data_size_in_bytes = len(input_data)
    print('Now compressing input data with Z-standard at level 22 (original file size: ' + str(uncompressed_input_data_size_in_bytes) + ' bytes)...')
    compressed_input_data = compress_data_with_zstd_func(input_data)
    decompressed_input_data = decompress_data_with_zstd_func(compressed_input_data)
    assert(decompressed_input_data==input_data)
    compressed_input_data_size_in_bytes = len(compressed_input_data)
    compressed_input_data_hash = get_sha256_hash_of_input_data_func(compressed_input_data)
    print('Done compressing! (compressed file size: ' + str(compressed_input_data_size_in_bytes) + ' bytes, or ' +str(round(float(compressed_input_data_size_in_bytes/uncompressed_input_data_size_in_bytes)*100.0,3)) + '% of original data size)...')
    seed = random.randint(0, 1 << 31 - 1)
    print('Now encoding data - ' + filename + ' (' + str(compressed_input_data_size_in_bytes) + ' bytes)\n\n')
    total_encoded_size_in_bytes = ceil(1.00*block_redundancy_factor*compressed_input_data_size_in_bytes)
    total_number_of_blocks_to_generate = ceil(total_encoded_size_in_bytes / desired_block_size_in_bytes)
    print('Total number of blocks to generate for target level of redundancy: ' + str(total_number_of_blocks_to_generate))
    blocks = [int.from_bytes(compressed_input_data[ii:ii+desired_block_size_in_bytes].ljust(desired_block_size_in_bytes, b'0'), 'little') for ii in range(0, compressed_input_data_size_in_bytes, desired_block_size_in_bytes)]
    prng = PRNG(params=(len(blocks), delta_constant, c_constant))
    prng.set_seed(seed)
    output_blocks_list = list()
    number_of_blocks_generated = 0
    while number_of_blocks_generated < total_number_of_blocks_to_generate:
        random_seed, d, ix_samples = prng.get_src_blocks()
        block_data = 0
        for ix in ix_samples:
            block_data ^= blocks[ix]
        block_data_bytes = int.to_bytes(block_data, desired_block_size_in_bytes, 'little')
        block_data_hash = hashlib.sha3_256(block_data_bytes).digest()
        block = (compressed_input_data_size_in_bytes, desired_block_size_in_bytes, random_seed, block_data_hash, block_data_bytes)
        header_bit_packing_pattern_string = '<3I32s'
        bit_packing_pattern_string = header_bit_packing_pattern_string + str(desired_block_size_in_bytes) + 's'
        length_of_header_in_bytes = struct.calcsize(header_bit_packing_pattern_string)
        packed_block_data = pack(bit_packing_pattern_string, *block)
        if number_of_blocks_generated == 0: #Test that the bit-packing is working correctly:
            with io.BufferedReader(io.BytesIO(packed_block_data)) as f:
                header_data = f.read(length_of_header_in_bytes)
                first_generated_block_raw_data = f.read(desired_block_size_in_bytes)
            compressed_input_data_size_in_bytes_test, desired_block_size_in_bytes_test, random_seed_test, block_data_hash_test = unpack(header_bit_packing_pattern_string, header_data)
            if block_data_hash_test != block_data_hash:
                print('Error! Block data hash does not match the hash reported in the block header!')
        output_blocks_list.append(packed_block_data)
        number_of_blocks_generated = number_of_blocks_generated + 1
    overall_counter = 0
    blocks_actually_generated = len(output_blocks_list)
    print('\nFinished processing compressed data into LT blocks! \nOriginal data (' + compressed_input_data_hash + ') was encoded into ' + str(blocks_actually_generated) + ' blocks of ' + str(ceil(desired_block_size_in_bytes)) + ' bytes each. Total size of all blocks is ~' + str(ceil((number_of_blocks_generated*desired_block_size_in_bytes))) + ' bytes.\n')
    list_of_png_base64_encoded_image_blobs = list()
    pbar = tqdm(total=number_of_blocks_generated)
    list_of_qr_code_svg_blobs = list()
    for current_block_data in output_blocks_list:
        pbar.update(1)
        current_block_data_base32_encoded = base64.b32encode(current_block_data).decode('utf-8')
        current_block_data_base32_encoded_character_length = len(current_block_data_base32_encoded)
        qr_encoding_type_index = pyqrcode.tables.modes[qr_encoding_type]
        qrcode_capacity_dict = pyqrcode.tables.data_capacity[pyqrcode_version_number] # Using max error corrections; see: pyqrcode.tables.data_capacity[40]
        max_characters_in_single_qr_code = qrcode_capacity_dict[qr_error_correcting_level][qr_encoding_type_index]
        required_number_of_qr_codes = ceil(float(current_block_data_base32_encoded_character_length)/float(max_characters_in_single_qr_code))
        list_of_light_hex_color_strings = ['#C9D6FF','#DBE6F6','#F0F2F0','#E0EAFC','#ffdde1','#FFEEEE','#E4E5E6','#DAE2F8','#D4D3DD','#d9a7c7','#fffcdc','#f2fcfe','#F8FFAE','#F0F2F0']
        list_of_dark_hex_color_strings = ['#203A43','#2C5364','#373B44','#3c1053','#333333','#23074d','#302b63','#24243e','#0f0c29','#2F0743','#3C3B3F','#000046','#200122','#1D4350','#2948ff']
        combined_hex_color_strings = [list_of_light_hex_color_strings,list_of_dark_hex_color_strings]
        list_of_encoded_data_blobs = list()
        for cnt in range(required_number_of_qr_codes):
            starting_index = cnt*max_characters_in_single_qr_code
            ending_index = min([len(current_block_data_base32_encoded), (cnt+1)*max_characters_in_single_qr_code])
            encoded_data_for_current_qr_code = current_block_data_base32_encoded[starting_index:ending_index]
            encoded_data_for_current_qr_code = encoded_data_for_current_qr_code.replace('=','')
            list_of_encoded_data_blobs.append(encoded_data_for_current_qr_code)
            current_qr_code = pyqrcode.create(encoded_data_for_current_qr_code, error=qr_error_correcting_level, version=pyqrcode_version_number, mode=qr_encoding_type)
            qr_code_png_output_file_name = 'ANIME_QR_CODE__'+ str(compressed_input_data_hash) + '__' '{0:05}'.format(overall_counter)
            first_random_color = random.choice(random.choice(combined_hex_color_strings))
            if first_random_color in list_of_light_hex_color_strings:
                second_random_color = random.choice(list_of_dark_hex_color_strings)
            else:
                second_random_color = random.choice(list_of_light_hex_color_strings)
            status_string = 'Total data length is ' + str(current_block_data_base32_encoded_character_length) + ' characters; A total of '+str(required_number_of_qr_codes) +' QR codes required for current LT block; Saving: ' + qr_code_png_output_file_name
            pbar.set_description(status_string)
            current_qr_code_png_base64_encoded = current_qr_code.png_as_base64_str(scale=qr_scale_factor, module_color=first_random_color, background=second_random_color)
            list_of_png_base64_encoded_image_blobs.append(current_qr_code_png_base64_encoded)
            with io.BytesIO() as svg_dummy_file:
                current_qr_code.svg(file=svg_dummy_file,scale=qr_scale_factor, module_color=first_random_color, background=second_random_color)
                svg_dummy_file.seek(0)
                svg_data = svg_dummy_file.read()
            list_of_qr_code_svg_blobs.append(svg_data)
            overall_counter = overall_counter + 1
    number_of_generated_qr_codes = len(list_of_png_base64_encoded_image_blobs)
    print('\nDone generating '+str(number_of_generated_qr_codes)+' individual QR code images!')
    if number_of_generated_qr_codes > 0: #Test that the qr codes work and that we can recover the exact data:
        first_qr_image_data_binary = base64.b64decode(list_of_png_base64_encoded_image_blobs[0])
        with io.BytesIO() as f:
            f.write(first_qr_image_data_binary)
            first_qr_image_data = Image.open(f)
            qr_image_pixel_width, qr_image_pixel_height = first_qr_image_data.size
            first_qr_image_data = enhance_pil_image_func(first_qr_image_data, enhancement_amount=9)
            first_qr_image_data = invert_image_if_needed_func(first_qr_image_data)
            first_qr_image_data = enhance_pil_image_func(first_qr_image_data, enhancement_amount=9)
            decoded_data = decode(first_qr_image_data)
        if len(decoded_data) == 0:
            print('Warning, QR code was not decoded!')
        decoded_data_raw = decoded_data[0][0]
        decoded_data_raw_binary = robust_base32_decode_func(decoded_data_raw)
        with io.BufferedReader(io.BytesIO(decoded_data_raw_binary)) as f:
            header_data = f.read(length_of_header_in_bytes)
            reconstructed_first_generated_block_raw_data = f.read(desired_block_size_in_bytes)
        assert(reconstructed_first_generated_block_raw_data == first_generated_block_raw_data)
        if type(decoded_data_raw_binary) not in [bytes, bytearray]:
            print('\nWarning: problem decoding base32 encoded stream!')
    print('\nPixel width/height of QR codes generated: ' + str(qr_image_pixel_width) + ', ' + str(qr_image_pixel_height))
    N = pow(ceil(sqrt(len(list_of_png_base64_encoded_image_blobs))), 2) #Find nearest square
    combined_qr_code_image_width = int(sqrt(N)*qr_image_pixel_width)
    combined_qr_code_image_height = int(sqrt(N)*qr_image_pixel_height)
    print('\nCombined Image Dimensions: '+str(combined_qr_code_image_width)+', '+str(combined_qr_code_image_height))
    combined_qr_code_image = Image.new('RGBA', (combined_qr_code_image_width, combined_qr_code_image_height) )
    number_in_each_row = int(sqrt(N))
    number_in_each_column = int(sqrt(N))
    image_counter = 0
    for ii in range(number_in_each_row):
        for jj in range(number_in_each_column):
            if image_counter < len(list_of_png_base64_encoded_image_blobs):
                current_qr_code_png_base64_encoded = list_of_png_base64_encoded_image_blobs[image_counter]
                current_qr_code_image_data_binary = base64.b64decode(current_qr_code_png_base64_encoded)
                with io.BytesIO() as f:
                    f.write(current_qr_code_image_data_binary)
                    current_image_data = Image.open(f)
                    new_image_coords =  (ii*qr_image_pixel_width, jj*qr_image_pixel_height)
                    print('Row Counter: ' + str(ii) + '; Column Counter: ' + str(jj)+'; New Image Upper-Left Corner: ' + str(new_image_coords[0]) + ', ' + str(new_image_coords[1]))
                    combined_qr_code_image.paste(current_image_data, new_image_coords)
                    current_image_data.close()
                    image_counter = image_counter + 1
    print('Done generating combined '+str(len(list_of_png_base64_encoded_image_blobs))+' QR code image containing compressed LT block data!')
    combined_qr_code_png_output_file_name = 'ANIME_QR_CODE_COMBINED__' + compressed_input_data_hash + '.png'
    combined_qr_code_image.save(combined_qr_code_png_output_file_name)
    list_of_qr_code_svg_blobs = [x.decode('utf-8') for x in list_of_qr_code_svg_blobs]
    return qr_image_pixel_width, combined_qr_code_png_output_file_name, output_blocks_list, list_of_qr_code_svg_blobs, list_of_png_base64_encoded_image_blobs


def reconstruct_data_from_list_of_luby_blocks(list_of_luby_block_data_binaries):
    c_constant = 0.1
    delta_constant = 0.5
    header_bit_packing_pattern_string = '<3I32s'
    length_of_header_in_bytes = struct.calcsize(header_bit_packing_pattern_string)
    first_block_data = list_of_luby_block_data_binaries[0]
    with io.BytesIO(first_block_data) as f:
        compressed_input_data_size_in_bytes, desired_block_size_in_bytes, _, _ = unpack(header_bit_packing_pattern_string, f.read(length_of_header_in_bytes))
    minimum_number_of_blocks_required = ceil(compressed_input_data_size_in_bytes / desired_block_size_in_bytes)
    block_graph = BlockGraph(minimum_number_of_blocks_required)
    number_of_lt_blocks_found = len(list_of_luby_block_data_binaries)
    print('Found ' + str(number_of_lt_blocks_found) + ' LT blocks to use...')
    for block_count, current_packed_block_data in enumerate(list_of_luby_block_data_binaries):
        with io.BytesIO(current_packed_block_data) as f:
            header_data = f.read(length_of_header_in_bytes)
            compressed_input_data_size_in_bytes, desired_block_size_in_bytes, random_seed, reported_block_data_hash = unpack(header_bit_packing_pattern_string, header_data)
            block_data_bytes = f.read(desired_block_size_in_bytes)
            calculated_block_data_hash = hashlib.sha3_256(block_data_bytes).digest()
            if calculated_block_data_hash == reported_block_data_hash:
                if block_count == 0:
                    print('Calculated block data hash matches the reported blocks from the block header!')
                prng = PRNG(params=(minimum_number_of_blocks_required, delta_constant, c_constant))
                _, _, src_blocks = prng.get_src_blocks(seed=random_seed)
                block_data_bytes_decoded = int.from_bytes(block_data_bytes, 'little' )
                file_reconstruction_complete = block_graph.add_block(src_blocks, block_data_bytes_decoded)
                if file_reconstruction_complete:
                    break
            else:
                print('Block data hash does not match reported block hash from block header file! Skipping to next block...')
    if file_reconstruction_complete:
        print('\nDone reconstructing data from blocks! Processed a total of ' + str(block_count + 1) + ' blocks\n')
        incomplete_file = 0
    else:
        print('Warning! Processed all available LT blocks but still do not have the entire file!')
        incomplete_file = 1
    if not incomplete_file:
        with io.BytesIO() as f:
            for ix, block_bytes in enumerate(map(lambda p: int.to_bytes(p[1], desired_block_size_in_bytes, 'little'), sorted(block_graph.eliminated.items(), key=lambda p:p[0]))):
                if (ix < minimum_number_of_blocks_required - 1) or ( (compressed_input_data_size_in_bytes % desired_block_size_in_bytes) == 0 ):
                    f.write(block_bytes)
                else:
                    f.write(block_bytes[:compressed_input_data_size_in_bytes % desired_block_size_in_bytes])
                reconstructed_data = f.getvalue()
    if not incomplete_file and type(reconstructed_data) in [bytes, bytearray]:
        print('Successfully reconstructed file from LT blocks! Reconstucted file size is '+str(len(reconstructed_data))+' bytes.')
        return reconstructed_data
    else:
        print('\nError: Data reconstruction from LT blocks failed!\n\n')

def decode_combined_ztd_luby_block_qr_code_image_func(path_to_image_file_with_qr_codes, qr_image_pixel_width):
    standard_qr_code_pixel_width = qr_image_pixel_width
    standard_qr_code_pixel_height = standard_qr_code_pixel_width
    combined_qr_code_image = Image.open(path_to_image_file_with_qr_codes)
    combined_width, combined_height = combined_qr_code_image.size
    N = pow((combined_width/standard_qr_code_pixel_width), 2)
    number_in_each_row = int(sqrt(N))
    number_in_each_column = int(sqrt(N))
    list_of_qr_code_image_data = list()
    iteration_counter = 0
    for ii in range(number_in_each_row):
        for jj in range(number_in_each_column):
            current_subimage_area = (ii*standard_qr_code_pixel_width, jj*standard_qr_code_pixel_height, (ii+1)*standard_qr_code_pixel_width, (jj+1)*standard_qr_code_pixel_height)
            current_qr_image_data = combined_qr_code_image.crop(current_subimage_area)
            #if jj < 4: current_qr_image_data.show()
            print('Row Counter: '+str(ii)+'; Column Counter: '+str(jj)+';')
            iteration_counter = iteration_counter + 1
            print('Iteration Count: '+str(iteration_counter)+'; Current Sub-Image Area Tuple: ('+str(current_subimage_area[0])+', '+str(current_subimage_area[1])+', '+str(current_subimage_area[2])+', '+str(current_subimage_area[3])+')')
            current_qr_image_data_enhanced = invert_image_if_needed_func(current_qr_image_data)
            if check_if_image_is_all_black_func(current_qr_image_data_enhanced) or check_if_image_is_all_white_func(current_qr_image_data_enhanced):
                print('Skipping empty QR code...')
            else:
                list_of_qr_code_image_data.append(current_qr_image_data)
    print('Found '+str(len(list_of_qr_code_image_data))+' QR code images in combined image.\n')
    list_of_luby_block_data_binaries = list()
    for current_qr_image in list_of_qr_code_image_data:
        current_qr_image_enhanced = enhance_pil_image_func(current_qr_image, enhancement_amount=9)
        current_qr_image_enhanced = invert_image_if_needed_func(current_qr_image_enhanced)
        decoded_data = decode(current_qr_image_enhanced)
        if len(decoded_data) > 0:
            decoded_data_raw = decoded_data[0][0]
            current_luby_block_data_binary = robust_base32_decode_func(decoded_data_raw)
            list_of_luby_block_data_binaries.append(current_luby_block_data_binary)
    print('Successfully decoded '+str(len(list_of_luby_block_data_binaries))+' QR codes!')
    reconstructed_compressed_data = reconstruct_data_from_list_of_luby_blocks(list_of_luby_block_data_binaries)
    print('Now attempting to reconstruct compressed data from recovered Luby blocks...')
    if len(reconstructed_compressed_data) > 0:
        reconstructed_uncompressed_data = decompress_data_with_zstd_func(reconstructed_compressed_data)
        print('Successfully decompressed file!')
        combined_qr_code_image.close()
    else:
        print('Error, data could not be decompressed!')
    return list_of_luby_block_data_binaries, reconstructed_uncompressed_data

def generate_data_animation_func(input_data_or_path):
    global list_of_frame_filepaths
    qr_image_pixel_width, combined_qr_code_png_output_file_name, output_blocks_list, list_of_qr_code_svg_blobs, list_of_png_base64_encoded_image_blobs = encode_file_into_ztd_luby_block_qr_codes_func(input_data_or_path)
    standard_qr_code_pixel_width = qr_image_pixel_width
    standard_qr_code_pixel_height = standard_qr_code_pixel_width
    combined_qr_code_image = Image.open(combined_qr_code_png_output_file_name)
    combined_width, combined_height = combined_qr_code_image.size
    N = pow((combined_width/standard_qr_code_pixel_width), 2)
    number_in_each_row = int(sqrt(N))
    number_in_each_column = int(sqrt(N))
    list_of_qr_code_image_data = list()
    iteration_counter = 0
    for ii in range(number_in_each_row):
        for jj in range(number_in_each_column):
            current_subimage_area = (ii*standard_qr_code_pixel_width, jj*standard_qr_code_pixel_height, (ii+1)*standard_qr_code_pixel_width, (jj+1)*standard_qr_code_pixel_height)
            current_qr_image_data = combined_qr_code_image.crop(current_subimage_area)
            print('Row Counter: '+str(ii)+'; Column Counter: '+str(jj)+';')
            iteration_counter = iteration_counter + 1
            print('Iteration Count: '+str(iteration_counter)+'; Current Sub-Image Area Tuple: ('+str(current_subimage_area[0])+', '+str(current_subimage_area[1])+', '+str(current_subimage_area[2])+', '+str(current_subimage_area[3])+')')
            current_qr_image_data_enhanced = invert_image_if_needed_func(current_qr_image_data)
            if check_if_image_is_all_black_func(current_qr_image_data_enhanced) or check_if_image_is_all_white_func(current_qr_image_data_enhanced):
                print('Skipping empty QR code...')
            else:
                list_of_qr_code_image_data.append(current_qr_image_data)
    print('Found '+str(len(list_of_qr_code_image_data))+' QR code images in combined image.\n')
    list_of_frame_filepaths = list()
    print('Now saving frames as png images...')
    for cnt, current_qr_code_image_data in enumerate(list_of_qr_code_image_data):
       current_output_file_name = 'ANIMECOIN_QR_ANIMATION__Frame_'+'{0:03}'.format(cnt) + '.png'
       current_qr_code_image_data.save(current_output_file_name)
       print(current_output_file_name)
       list_of_frame_filepaths.append(current_output_file_name)
    return list_of_frame_filepaths, combined_qr_code_png_output_file_name, list_of_qr_code_svg_blobs


#QR-code camera reconstruction functions:
def order_points_func(pts):
    rect = np.zeros((4, 2), dtype = "float32")
    s = pts.sum(axis = 1)
    rect[0] = pts[np.argmin(s)]
    rect[2] = pts[np.argmax(s)]
    diff = np.diff(pts, axis = 1)
    rect[1] = pts[np.argmin(diff)]
    rect[3] = pts[np.argmax(diff)]
    return rect

def four_point_transform_func(image, pts):
    rect = order_points_func(pts)
    (tl, tr, br, bl) = rect
    widthA = np.sqrt(((br[0] - bl[0]) ** 2) + ((br[1] - bl[1]) ** 2))
    widthB = np.sqrt(((tr[0] - tl[0]) ** 2) + ((tr[1] - tl[1]) ** 2))
    maxWidth = max(int(widthA), int(widthB))
    heightA = np.sqrt(((tr[0] - br[0]) ** 2) + ((tr[1] - br[1]) ** 2))
    heightB = np.sqrt(((tl[0] - bl[0]) ** 2) + ((tl[1] - bl[1]) ** 2))
    maxHeight = max(int(heightA), int(heightB))
    dst = np.array([ [0, 0], [maxWidth - 1, 0], [maxWidth - 1, maxHeight - 1], [0, maxHeight - 1]], dtype = "float32")
    M = cv2.getPerspectiveTransform(rect, dst)
    warped = cv2.warpPerspective(image, M, (maxWidth, maxHeight))
    return warped

def distance_func(pt1, pt2):
    (x1, y1), (x2, y2) = pt1, pt2
    dist = sqrt( (x2 - x1)**2 + (y2 - y1)**2 )
    return dist


################################################################################
#  Demo:
################################################################################


use_demonstrate_sphincs_crypto = 0
use_demonstrate_eddsa_crypto = 0
use_demonstrate_libsodium_crypto = 0
use_demonstrate_qr_code_generation = 0
use_demonstrate_qr_steganography = 0
use_demonstrate_qr_data_transmission_animation = 0
use_demonstrate_qr_animation_video_recovery = 0

if use_demonstrate_sphincs_crypto:
    with MyTimer():
        print('Now generating SPHINCS public/secret keypair...')
        sphincs256 = SPHINCS()
        sk, pk = sphincs256.keygen()
        secret_key = sphincs256.my_pack(sk)
        public_key = sphincs256.my_pack(pk)
        with open('sphincs_secret_key.dat','wb') as f:
            f.write(secret_key)
        with open('sphincs_public_key.dat','wb') as f:
            f.write(public_key)
        print('Done generating key!')

    with MyTimer():
        print('Now signing art metadata with key...')
        path_to_animecoin_html_ticket_file = 'C:\\animecoin\\art_folders_to_encode\\Carlo_Angelo__Felipe_Number_02\\artwork_metadata_ticket__2018_05_27__23_42_33_964953.html'
        with open(path_to_animecoin_html_ticket_file,'rb') as f:
           animecoin_ticket_html_string = f.read()
        message = animecoin_ticket_html_string
        with open('sphincs_secret_key.dat','rb') as f:
            sphincs_secret_key = sphincs256.my_unpack(f.read())
        signature = sphincs256.sign(message, sphincs_secret_key)
        print('Done! Writing signature file now...')
        with open('sphincs_signature.dat','wb') as f:
            f.write(sphincs256.my_pack(signature))

    with MyTimer():
        print('Now Verifying SPHINCS Signature...')
        with open('sphincs_signature.dat','rb') as f:
            signature_data = f.read()
            sphincs_signature = sphincs256.my_unpack(signature_data)
        with open('sphincs_public_key.dat','rb') as f:
            private_key_data = sphincs256.my_unpack(f.read())
            sphincs_public_key = sphincs256.my_unpack(private_key_data)
        signature_verified = sphincs256.verify(message, signature_data, sphincs_public_key)
        if signature_verified:
            print('Verification succeeded!')
        else:
            print('Error! Verification failed.')

if use_demonstrate_eddsa_crypto:
   #path_to_input_file = 'us_constitution.txt'
    path_to_input_file = 'pride_and_prejudice.txt'
    with open(path_to_input_file,'rb') as f:
        input_data_raw_bytes = f.read()
    input_data = input_data_raw_bytes

    print('Generating Eddsa 448 (Goldilocks) keypair now...')
    with MyTimer():
        privkey, pubkey = Ed448.keygen(os.urandom(448*2))
    print('Now Signing message with secret key...')
    with MyTimer():
        signature = Ed448.sign(privkey, pubkey, input_data)
    print('Now verifying signature with public key...')
    with MyTimer():
        verified = Ed448.verify(pubkey, input_data, signature)

    print('Generating Eddsa 521 keypair now...')
    with MyTimer():
        privkey, pubkey = Ed521.keygen(os.urandom(521*2))
    print('Now Signing message with secret key...')
    with MyTimer():
        signature = Ed521.sign(privkey, pubkey, input_data)
    print('Now verifying signature with public key...')
    with MyTimer():
        verified = Ed521.verify(pubkey, input_data, signature)

if use_demonstrate_libsodium_crypto:
    signing_key = nacl.signing.SigningKey.generate() # Generate a new random signing key
    signed = signing_key.sign(b"Attack at Dawn") # Sign a message with the signing key
    verify_key = signing_key.verify_key # Obtain the verify key for a given signing key
    verify_key_hex = verify_key.encode(encoder=nacl.encoding.HexEncoder) # Serialize the verify key to send it to a third party
    verified = verify_key.verify(signed)


if use_demonstrate_qr_code_generation:
    #path_to_input_file = 'us_constitution.txt'
    #path_to_input_file = 'pride_and_prejudice.txt'
    path_to_input_file = 'sphincs_secret_key.dat'
    with open(path_to_input_file,'rb') as f:
        sphincs_secret_key_raw_bytes = f.read()
    input_data = sphincs_secret_key_raw_bytes
    input_data_hash = get_sha256_hash_of_input_data_func(input_data)
    input_data_or_path = input_data
    qr_image_pixel_width, combined_qr_code_png_output_file_name, output_blocks_list, list_of_qr_code_svg_blobs, list_of_png_base64_encoded_image_blobs = encode_file_into_ztd_luby_block_qr_codes_func(input_data_or_path)
    svg_html_string = '<html><body> svg_data'
    for cnt, current_qr_code_svg_data in enumerate(list_of_qr_code_svg_blobs):
        if cnt == 0:
            svg_in_html = svg_html_string.replace('svg_data', current_qr_code_svg_data)
        else:
            svg_in_html = svg_in_html + current_qr_code_svg_data
        if cnt == len(list_of_qr_code_svg_blobs) - 1:
            svg_in_html = svg_in_html + current_qr_code_svg_data + '</html></body>'
    with open('svg_in_html.html','w') as f:
        f.write(svg_in_html)
    path_to_image_file_with_qr_codes = combined_qr_code_png_output_file_name
    list_of_luby_block_data_binaries, reconstructed_uncompressed_data = decode_combined_ztd_luby_block_qr_code_image_func(path_to_image_file_with_qr_codes, qr_image_pixel_width)
    reconstructed_uncompressed_data_hash = get_sha256_hash_of_input_data_func(reconstructed_uncompressed_data)
    if input_data_hash != reconstructed_uncompressed_data_hash:
        reconstructed_uncompressed_data = reconstructed_uncompressed_data.decode('utf-8')
        reconstructed_uncompressed_data_hash = get_sha256_hash_of_input_data_func(reconstructed_uncompressed_data)
    assert(input_data_hash == reconstructed_uncompressed_data_hash)
    print('Decompressed file hash matches original file!')

if use_demonstrate_qr_data_transmission_animation:
    existing_frame_paths = glob.glob('ANIMECOIN_QR_ANIMATION__Frame_*.png')
    for current_frame_path in existing_frame_paths:
        os.remove(current_frame_path)

    list_of_frame_filepaths, combined_qr_code_png_output_file_name, list_of_qr_code_svg_blobs = generate_data_animation_func(input_data_or_path)
    svg_fix_command_string = 'C:\\Program Files\\Inkscape\\inkscape.exe -f REPLACE_WITH_FILE_PATH --verb EditSelectAll --verb SelectionUnGroup --verb EditUnlinkClone --verb SelectionUnGroup   --verb EditUnlinkClone --verb SelectionUnGroup  --verb EditUnlinkClone --verb SelectionUnGroup  --verb StrokeToPath --verb SelectionUnion  --verb FileSave --verb FileQuit'
    list_of_fixed_svg_blobs = list()
    for current_svg_blob in list_of_qr_code_svg_blobs:
        with open('current_blob.svg','w') as f:
            f.write(current_svg_blob)
            print('Fixing SVG file now in Inkscape: ')
            svg_fix_command_string = svg_fix_command_string.replace('REPLACE_WITH_FILE_PATH','current_blob.svg')
            command_result = subprocess.call(svg_fix_command_string)
            with open('current_blob.svg','r') as g:
                fixed_svg_blob = g.read()
            list_of_fixed_svg_blobs.append(fixed_svg_blob)
            print('Done!')


    if 0: #make into animated gif:
        gif_name = combined_qr_code_png_output_file_name.replace('.png','').replace('ANIME_QR_CODE_COMBINED__','ANIMECOIN_QR_ANIMATION__')
        fps = 3
        file_list = glob.glob('ANIMECOIN_QR_ANIMATION__Frame_*.png') # Get all the pngs in the current directory
        clip = mpy.ImageSequenceClip(file_list, fps=fps)
        clip.write_gif('{}.gif'.format(gif_name), fps=fps)
        animation_gif_file_paths = glob.glob('ANIMECOIN_QR_ANIMATION__*.gif')
        animation_gif_file_path = animation_gif_file_paths[0]
        animated_qr_code_file_output_file_path = animation_gif_file_path
        animated_qr_player_html_output_file_path = 'animated_gif_player.html'
    chrome_options = Options()
    chrome_options.add_argument('--start-fullscreen')
    chrome_options.add_argument('--disable-infobars')
    driver = webdriver.Chrome(chrome_options=chrome_options)
    animated_qr_player_html_output_file_path = 'qr_code_svg_player.html'
    player_uri_string = 'file:///C:/Users/jeffr/Cointel%20Dropbox/Animecoin_Code/'+animated_qr_player_html_output_file_path
    for cnt, current_svg_blob in enumerate(list_of_qr_code_svg_blobs):
        animated_qr_code_file_output_file_path = 'ANIMECOIN_QR_ANIMATION__Frame_'+'{0:04}'.format(cnt) +'.png'
        cleaned_svg = current_svg_blob.replace('<?xml version="1.0" encoding="UTF-8"?>\n','')
        svg_base64_string = base64.b64encode(cleaned_svg.encode('utf-8')).decode('utf-8')
        html_string = '<html><body> <style> html {background-size: contain; height: 100%; background-position: center; background-repeat: no-repeat;} body {height: 100%; background: url("data:image/svg;base64, '+ svg_base64_string +'"); </style></body></html>'
        with open(animated_qr_player_html_output_file_path, 'w') as f:
            f.write(html_string)
        driver.get(player_uri_string)
        sleep(0.4)
    driver.quit()

if use_demonstrate_qr_animation_video_recovery:
    cv2.startWindowThread()
    vidcap = cv2.VideoCapture('example_recorded_qr_code_animation.mov')
    success, image_frame = vidcap.read()
    count = 0
    success = True
    skip_first_n_frames = 50
    video_frame_paths = glob.glob('C:\\Users\\jeffr\\Cointel Dropbox\\Animecoin_Code\\video_frames\\*.jpg')
    for current_frame_path in video_frame_paths:
        os.remove(current_frame_path)
    while success:
      success, image_frame = vidcap.read()
      if count > skip_first_n_frames:
          cv2.imwrite('C:\\Users\\jeffr\\Cointel Dropbox\\Animecoin_Code\\video_frames\\frame__'+'{0:04}'.format(count) +'.png', image_frame)
      count = count + 1
    video_frame_paths = glob.glob('C:\\Users\\jeffr\\Cointel Dropbox\\Animecoin_Code\\video_frames\\*.jpg')
    for current_frame_path in video_frame_paths:
        img = Image.open(current_frame_path)
        img = enhance_pil_image_func(img, 9)
        img = invert_image_if_needed_func(img)
        img.save(current_frame_path)
        img.close()
    video_frame_paths = glob.glob('C:\\Users\\jeffr\\Cointel Dropbox\\Animecoin_Code\\video_frames\\*.jpg')
    current_frame_path = video_frame_paths[1]
    current_frame_path = video_frame_paths[10]
    current_frame_path = video_frame_paths[20]
    list_of_kps = list()
    list_of_deses = list()
    for cnt, current_frame_path in enumerate(video_frame_paths):
        img = cv2.imread(current_frame_path)
        orb = cv2.ORB_create()
        kp1, des1 = orb.detectAndCompute(img, None)
        list_of_kps.append(kp1)
        list_of_deses.append(des1)
#        params = cv2.SimpleBlobDetector_Params()
#        params.minThreshold = 10
#        params.maxThreshold = 200
#        params.filterByArea = True
#        params.minArea = 1500
#        params.filterByCircularity = True
#        params.minCircularity = 0.1
#        params.filterByConvexity = True
#        params.minConvexity = 0.87
#        params.filterByInertia = True
#        params.minInertiaRatio = 0.01
#        detector = cv2.SimpleBlobDetector_create(params)
#        keypoints = detector.detect(img)
#        im_with_keypoints = cv2.drawKeypoints(img, keypoints, np.array([]), (0,0,255), cv2.DRAW_MATCHES_FLAGS_DRAW_RICH_KEYPOINTS)
#        cv2.imwrite('C:\\Users\\jeffr\\Cointel Dropbox\\Animecoin_Code\\video_frames\\block_detctor__' + str(cnt) + '.png', im_with_keypoints)
#        squares = find_squares(img)
#        flatten = lambda l: [item for sublist in l for item in sublist]
#        flat_squares = flatten(flatten(squares))
#        flat_squares.sort()
#        flat_squares_diff = np.diff(flat_squares)
#        lower_left = sorted(set(flat_squares))[10]
#        upper_right = sorted(set(flat_squares))[-10]
#        coords = [lower_left,lower_left,upper_right,upper_right]
#        warped = four_point_transform_func(img, coords)


def angle_cos(p0, p1, p2):
    d1, d2 = (p0-p1).astype('float'), (p2-p1).astype('float')
    return abs( np.dot(d1, d2) / np.sqrt( np.dot(d1, d1)*np.dot(d2, d2) ) )

def find_squares(img):
    img = cv2.GaussianBlur(img, (5, 5), 0)
    squares = []
    for gray in cv2.split(img):
        for thrs in range(0, 255, 26):
            if thrs == 0:
                bin = cv2.Canny(gray, 0, 50, apertureSize=5)
                bin = cv2.dilate(bin, None)
            else:
                _retval, bins = cv2.threshold(gray, thrs, 255, cv2.THRESH_BINARY)
            bins, contours, _hierarchy = cv2.findContours(bins, cv2.RETR_LIST, cv2.CHAIN_APPROX_SIMPLE)
            for cnt in contours:
                cnt_len = cv2.arcLength(cnt, True)
                cnt = cv2.approxPolyDP(cnt, 0.02*cnt_len, True)
                if len(cnt) == 4 and cv2.contourArea(cnt) > 1000 and cv2.isContourConvex(cnt):
                    cnt = cnt.reshape(-1, 2)
                    max_cos = np.max([angle_cos( cnt[i], cnt[(i+1) % 4], cnt[(i+2) % 4] ) for i in range(4)])
                    if max_cos < 0.1:
                        squares.append(cnt)

#        imgray = cv2.cvtColor(img,cv2.COLOR_BGR2GRAY)
#        plt.imshow(imgray),plt.show()
#        imgray = cv2.blur(imgray,(15,15))
#        plt.imshow(imgray),plt.show()
#        ret, thresh = cv2.threshold(imgray, floor(np.average(imgray)), 255, cv2.THRESH_BINARY_INV)
#        plt.imshow(thresh),plt.show()
#        dilated = cv2.morphologyEx(thresh, cv2.MORPH_OPEN, cv2.getStructuringElement(cv2.MORPH_ELLIPSE,(10,10)))
#        _, contours ,_ = cv2.findContours(dilated, cv2.RETR_LIST, cv2.CHAIN_APPROX_SIMPLE)
#        new_contours=[]
#        for c in contours:
#            if cv2.contourArea(c) < img.size:
#                new_contours.append(c)
#        best_box=[-1,-1,-1,-1]
#        for c in new_contours:
#           x,y,w,h = cv2.boundingRect(c)
#           if best_box[0] < 0:
#               best_box=[x,y,x+w,y+h]
#           else:
#               if x<best_box[0]:
#                   best_box[0]=x
#               if y<best_box[1]:
#                   best_box[1]=y
#               if x+w>best_box[2]:
#                   best_box[2]=x+w
#               if y+h>best_box[3]:
#                   best_box[3]=y+h

#
#
#          if len(decoded_data) == 0:
#            print('Warning, QR code was not decoded!')
#        decoded_data_raw = decoded_data[0][0]
#        decoded_data_raw_binary = robust_base32_decode_func(decoded_data_raw)
#        with io.BufferedReader(io.BytesIO(decoded_data_raw_binary)) as f:
#            header_data = f.read(length_of_header_in_bytes)
#            reconstructed_first_generated_block_raw_data = f.read(desired_block_size_in_bytes)
#
#        gray = cv2.cvtColor(img,cv2.COLOR_BGR2GRAY)
#        bi = cv2.bilateralFilter(gray, 5, 75, 75)
#        dst = cv2.cornerHarris(bi, 2, 3, 0.04)
#        mask = np.zeros_like(gray)
#        mask[dst>0.01*dst.max()] = 255
#
#        coordinates = np.argwhere(mask)
#
#        from scipy.spatial import ConvexHull
#    points = np.random.rand(30, 2)   # 30 random points in 2-D
#    hull = ConvexHull(mask)
#
#        thresh = 50
#        thresh, thresholded_img = cv2.threshold(img, 250, 255, cv2.THRESH_BINARY_INV);
#        if thresholded_img is not None:
#            res = thresholded_img[1]
#            kernel = cv2.getStructuringElement(cv2.MORPH_CROSS,(3,3))
#            for ii in range(10):
#                res = cv2.dilate(res, kernel)
#            img2, contours,hierarchy = cv2.findContours(res, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
#            cpt=0
#            for contour in contours:
#                rect = cv2.boundingRect(contour)
#                img2 = img[rect[1]:rect[1]+rect[3],rect[0]:rect[0]+rect[2]]
#                cv2.imwrite('C:\\Users\\jeffr\\Cointel Dropbox\\Animecoin_Code\\video_frames\\contour__' + str(cpt) + '.png', img2)
#                cpt = cpt + 1
#

#
#
#        pts = np.array(coor_tuples_copy, dtype = "float32")
#        warped = four_point_transform_func(img, pts)


if use_demonstrate_qr_steganography:
    path_to_image_file = 'C:\\animecoin\\art_folders_to_encode\\Carlo_Angelo__Felipe_Number_02\\Carlo_Angelo__Felipe_Number_02.png'
    path_to_image_file_with_qr_codes = glob.glob('ANIME_QR_CODE_COMBINED__*.png')
    path_to_image_file_with_qr_codes = path_to_image_file_with_qr_codes[0]
    art_image_data = Image.open(path_to_image_file)
    art_image_data_width, art_image_data_height = art_image_data.size
    art_image_data_total_pixels = art_image_data_width*art_image_data_height
    combined_qr_code_data = Image.open(path_to_image_file_with_qr_codes)
    combined_qr_code_data_total_pixels = combined_qr_code_data.size[0]*combined_qr_code_data.size[1]
    required_total_art_image_pixels = 2*combined_qr_code_data_total_pixels
    magnification_factor = required_total_art_image_pixels/art_image_data_total_pixels
    if magnification_factor > 1:
        print('Magnification Factor: '+str(round(magnification_factor,3)))
        art_image_data_width_new= ceil(art_image_data_width*magnification_factor)
        art_image_data_height_new = ceil(art_image_data_height*magnification_factor)
        art_image_data_resized = art_image_data.resize((art_image_data_width_new, art_image_data_height_new), Image.LANCZOS)
    else:
        art_image_data_resized = art_image_data
    art_image_data.close()
    merged_image_data = merge_two_images_func(art_image_data_resized, combined_qr_code_data)
    merged_image_data_png_output_file_name = 'ANIME_ART_Watermarked.png'
    merged_image_data.save(merged_image_data_png_output_file_name)
    #Now try to find the hidden image:
    embedded_image = unmerge_two_images_func(merged_image_data)
    embedded_image_data_png_output_file_name = 'ANIME_ART_SIGNATURE.png'
    embedded_image.save(embedded_image_data_png_output_file_name)
    path_to_reconstructed_image_file_with_qr_codes = 'ANIME_ART_SIGNATURE.png'
    combined_reconstructed_string_stego, list_of_decoded_data_blobs_stego = decode_data_from_qr_code_images_func(path_to_reconstructed_image_file_with_qr_codes, qr_image_pixel_width)
    assert(list_of_encoded_data_blobs == list_of_decoded_data_blobs_stego)
    combined_reconstructed_string_stego_stripped_padded = add_base32_padding_characters_func(combined_reconstructed_string_stego)
    input_data_base32_decoded_stego = robust_base32_decode_func(combined_reconstructed_string_stego_stripped_padded)
    input_data_hash_stego = input_data_base32_decoded_stego[:64]
    reconstructed_data_stego = input_data_base32_decoded_stego[64:]
    reconstructed_data_hash_stego = hashlib.sha3_256(reconstructed_data_stego).hexdigest().encode('utf-8')
    assert(reconstructed_data_hash_stego == input_data_hash_stego)
