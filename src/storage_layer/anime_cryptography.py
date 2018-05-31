import itertools, os, time, base64, hashlib, glob, random, sys, binascii, io, struct
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
from matplotlib import pyplot as plt
from tqdm import tqdm
from anime_utility_functions_v1 import get_sha256_hash_of_input_data_func
from anime_fountain_coding_v1 import PRNG, BlockGraph
sys.setrecursionlimit(1000)
sigma = "expand 32-byte k"
tau = "expand 16-byte k"
current_base_folder = os.getcwd() + os.path.sep

#pip install pyqrcode, pypng, pyzbar, tqdm, numpy, pygame, pynacl, selenium, zstd, cv2

def xor(b1, b2): # Expects two bytes objects of equal length, returns their XOR
    assert len(b1) == len(b2)
    return bytes([x ^ y for x, y in zip(b1, b2)])

def chunkbytes(a, n):
    return [a[ii:ii+n] for ii in range(0, len(a), n)]

def ints_from_4bytes(a):
    for chunk in chunkbytes(a, 4):
        yield int.from_bytes(chunk, byteorder='little')

def ints_to_4bytes(x):
    for v in x:
        yield int.to_bytes(v, length=4, byteorder='little')

def hash_tree(H, leafs):
    assert (len(leafs)& len(leafs) - 1) == 0  # test for full binary tree
    return l_tree(H, leafs)  # binary hash trees are special cases of L-Trees

def l_tree(H, leafs):
    layer = leafs
    yield layer
    for ii in range(ceil(log2(len(leafs)))):
        next_layer = [H(l, r, ii) for l, r in zip(layer[0::2], layer[1::2])]
        if len(layer)& 1:  # if there is a node left on this layer
            next_layer.append(layer[-1])
        layer = next_layer
        yield layer

def auth_path(tree, idx):
    path = []
    for layer in tree:
        if len(layer) == 1:  # if there are no neighbors
            break
        idx += 1 if (idx& 1 == 0) else -1  # neighbor node
        path.append(layer[idx])
        idx>>= 1  # parent node
    return path

def construct_root(H, auth_path, leaf, idx):
    node = leaf
    for ii, neighbor in enumerate(auth_path):
        if idx& 1 == 0:
            node = H(node, neighbor, ii)
        else:
            node = H(neighbor, node, ii)
        idx >>= 1
    return node

def root(tree):
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
            self.state += ints_from_4bytes(c)
            self.state += ints_from_4bytes(key)
        elif len(key) == 16:
            c = bytes(tau, 'latin-1')
            self.state += ints_from_4bytes(c)
            self.state += ints_from_4bytes(key)
            self.state += ints_from_4bytes(key)
        self.state += [0, 0]
        self.state += ints_from_4bytes(iv)

    def permuted(self, a):
        assert (len(a) == 16 and all(type(ii) is int for ii in a) or
                len(a) == 64 and type(a) in [bytes, bytearray])
        if len(a) == 64:
            x = list(ints_from_4bytes(a))
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
        return b''.join(ints_to_4bytes(x))

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
        M = chunkbytes(m, self.tau // 8)
        M = [int.from_bytes(Mi, byteorder='little') for Mi in M]  # the reference implementation uses 'idx = m[2*i] + (m[2*i+1]<<8)' which suggests using little-endian byte order
        return M

    def keygen(self, seed, masks):
        assert len(seed) == self.n // 8
        assert len(masks) >= 2 * self.tau
        sk = self.Gt(seed)
        sk = chunkbytes(sk, self.n // 8)
        L = list(map(self.F, sk))
        H = lambda x, y, i: self.H(xor(x, masks[2*i]), xor(y, masks[2*i+1]))
        return root(hash_tree(H, L))

    def sign(self, m, seed, masks):
        assert len(m) == self.m // 8
        assert len(seed) == self.n // 8
        assert len(masks) >= 2 * self.tau
        sk = self.Gt(seed)
        sk = chunkbytes(sk, self.n // 8)
        L = list(map(self.F, sk))
        H = lambda x, y, i: self.H(xor(x, masks[2*i]), xor(y, masks[2*i+1]))
        tree = hash_tree(H, L)
        trunk = list(itertools.islice(tree, 0, self.tau - self.x))
        sigma_k = next(tree)
        M = self.message_indices(m)
        pk = root(tree)# the SPHINCS paper suggests to put sigma_k at the end of sigma but the reference code places it at the front
        return ([(sk[Mi], auth_path(trunk, Mi)) for Mi in M] + [sigma_k], pk)

    def verify(self, m, sig, masks):
        assert len(m) == self.m // 8
        assert len(masks) >= 2 * self.tau
        M = self.message_indices(m)
        H = lambda x, y, i: self.H(xor(x, masks[2*i]), xor(y, masks[2*i+1]))
        sigma_k = sig[-1]
        for (sk, path), Mi in zip(sig, M):
            leaf = self.F(sk)
            r = construct_root(H, path, leaf, Mi) # there is an error in the SPHINCS paper for this formula, as itstates that y_i = floor(M_i // 2^tau - x) rather than y_i = floor(M_i // 2^{tau - x})
            yi = Mi // (1<< (self.tau - self.x))
            if r != sigma_k[yi]:
                return False
        Qtop = masks[2*(self.tau - self.x):]
        H = lambda x, y, i: self.H(xor(x, Qtop[2*i]), xor(y, Qtop[2*i+1]))
        return root(hash_tree(H, sigma_k))
    
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
                x[ii] = self.F(xor(x[ii], masks[jj]))
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
        sk = chunkbytes(sk, self.n // 8)
        return self.chains(sk, masks, [range(0, self.w-1)]*self.l)

    def sign(self, m, seed, masks):
        sk = self.Gl(seed)
        sk = chunkbytes(sk, self.n // 8)
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
        self.Hdigest = lambda r, m: BLAKE(512).digest(r + m)
        self.Fa = lambda a, k: BLAKE(256).digest(k + a)
        self.Frand = lambda m, k: BLAKE(512).digest(k + m)
        C = bytes("expand 32-byte to 64-byte state!", 'latin-1')
        perm = ChaCha().permuted
        self.Glambda = lambda seed, n: ChaCha(key=seed).keystream(n)
        self.F = lambda m: perm(m + C)[:32]
        self.H = lambda m1, m2: perm(xor(perm(m1 + C), m2 + bytes(32)))[:32]
        self.wots = WOTSplus(n=n, w=w, F=self.F, Gl=self.Glambda)
        self.horst = HORST(n=n, m=m, k=k, tau=tau, F=self.F, H=self.H, Gt=self.Glambda)

    @classmethod
    def address(self, level, subtree, leaf):
        t = level | (subtree<< 4) | (leaf<< 59)
        return int.to_bytes(t, length=8, byteorder='little')

    def wots_leaf(self, address, SK1, masks):
        seed = self.Fa(address, SK1)
        pk_A = self.wots.keygen(seed, masks)
        H = lambda x, y, i: self.H(xor(x, masks[2*i]), xor(y, masks[2*i+1]))
        return root(l_tree(H, pk_A))

    def wots_path(self, a, SK1, Q, subh):
        ta = dict(a)
        leafs = []
        for subleaf in range(1<< subh):
            ta['leaf'] = subleaf
            leafs.append(self.wots_leaf(self.address(**ta), SK1, Q))
        Qtree = Q[2 * ceil(log(self.wots.l, 2)):]
        H = lambda x, y, i: self.H(xor(x, Qtree[2*i]), xor(y, Qtree[2*i+1]))
        tree = list(hash_tree(H, leafs))
        return auth_path(tree, a['leaf']), root(tree)

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
        H = lambda x, y, ii: self.H(xor(x, Qtree[2*ii]), xor(y, Qtree[2*ii+1]))
        PK1 = root(hash_tree(H, leafs))
        return PK1

    def sign(self, M, SK):
        SK1, SK2, Q = SK
        R = self.Frand(M, SK2)
        R1, R2 = R[:self.n // 8], R[self.n // 8:]
        D = self.Hdigest(R1, M)
        ii = int.from_bytes(R2, byteorder='big')
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
        H = lambda x, y, ii: self.H(xor(x, Q[2*ii]), xor(y, Q[2*ii+1]))
        Ht = lambda x, y, ii: self.H(xor(x, Qtree[2*ii]), xor(y, Qtree[2*ii+1]))
        for _ in range(self.d):
            wots_sig, wots_path, *sig = sig
            pk_wots = self.wots.verify(pk, wots_sig, Q)
            leaf = root(l_tree(H, pk_wots))
            pk = construct_root(Ht, wots_path, leaf, ii& 0x1f)
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

class Sha512():
        K =    (0x428a2f98d728ae22, 0x7137449123ef65cd, 0xb5c0fbcfec4d3b2f, 0xe9b5dba58189dbbc,
                0x3956c25bf348b538, 0x59f111f1b605d019, 0x923f82a4af194f9b, 0xab1c5ed5da6d8118,
                0xd807aa98a3030242, 0x12835b0145706fbe, 0x243185be4ee4b28c, 0x550c7dc3d5ffb4e2,
                0x72be5d74f27b896f, 0x80deb1fe3b1696b1, 0x9bdc06a725c71235, 0xc19bf174cf692694,
                0xe49b69c19ef14ad2, 0xefbe4786384f25e3, 0x0fc19dc68b8cd5b5, 0x240ca1cc77ac9c65,
                0x2de92c6f592b0275, 0x4a7484aa6ea6e483, 0x5cb0a9dcbd41fbd4, 0x76f988da831153b5,
                0x983e5152ee66dfab, 0xa831c66d2db43210, 0xb00327c898fb213f, 0xbf597fc7beef0ee4,
                0xc6e00bf33da88fc2, 0xd5a79147930aa725, 0x06ca6351e003826f, 0x142929670a0e6e70,
                0x27b70a8546d22ffc, 0x2e1b21385c26c926, 0x4d2c6dfc5ac42aed, 0x53380d139d95b3df,
                0x650a73548baf63de, 0x766a0abb3c77b2a8, 0x81c2c92e47edaee6, 0x92722c851482353b,
                0xa2bfe8a14cf10364, 0xa81a664bbc423001, 0xc24b8b70d0f89791, 0xc76c51a30654be30,
                0xd192e819d6ef5218, 0xd69906245565a910, 0xf40e35855771202a, 0x106aa07032bbd1b8,
                0x19a4c116b8d2d0c8, 0x1e376c085141ab53, 0x2748774cdf8eeb99, 0x34b0bcb5e19b48a8,
                0x391c0cb3c5c95a63, 0x4ed8aa4ae3418acb, 0x5b9cca4f7763e373, 0x682e6ff3d6b2b8a3,
                0x748f82ee5defb2fc, 0x78a5636f43172f60, 0x84c87814a1f0ab72, 0x8cc702081a6439ec,
                0x90befffa23631e28, 0xa4506cebde82bde9, 0xbef9a3f7b2c67915, 0xc67178f2e372532b,
                0xca273eceea26619c, 0xd186b8c721c0c207, 0xeada7dd6cde0eb1e, 0xf57d4f7fee6ed178,
                0x06f067aa72176fba, 0x0a637dc5a2c898a6, 0x113f9804bef90dae, 0x1b710b35131c471b,
                0x28db77f523047d84, 0x32caab7b40c72493, 0x3c9ebe0a15c9bebc, 0x431d67c49c100d4c,
                0x4cc5d4becb3e42b6, 0x597f299cfc657e2a, 0x5fcb6fab3ad6faec, 0x6c44198c4a475817)
        
        def R(self, x, c): return ((x >> c) | ((x & 0xffffffffffffffff) << (64 - c)))
        def Ch(self, x, y, z): return (x & y) ^ (~x & z)
        def Maj(self, x, y, z): return (x & y) ^ (x & z) ^ (y & z)
        def Sigma0(self, x): return self.R(x, 28) ^ self.R(x, 34) ^ self.R(x, 39)
        def Sigma1(self, x): return self.R(x, 14) ^ self.R(x, 18) ^ self.R(x, 41)
        def sigma0(self, x): return self.R(x,  1) ^ self.R(x,  8) ^ (x >> 7)
        def sigma1(self, x): return self.R(x, 19) ^ self.R(x, 61) ^ (x >> 6)

        def __init__(self, m = None, iv = None):
            self.h = [0x6a09e667f3bcc908, 0xbb67ae8584caa73b, 0x3c6ef372fe94f82b, 0xa54ff53a5f1d36f1, 0x510e527fade682d1, 0x9b05688c2b3e6c1f, 0x1f83d9abfb41bd6b, 0x5be0cd19137e2179]
            if (type(iv) == type(()) or type(iv) == type([])):
                for i in range(8): 
                    self.h[i] = iv[i]
            b = len(m)
            while len(m) >= 128:
                self.block(m[:128])
                m = m[128:]
            self._buffer = ''
            self._counter = 0
            n = b & 127
            x = [0] * 256
            for i in range(n): 
                x[i] = m[i]
            x[n] = 128
            n = 256 - 128 * (n < 112)
            x[n - 9] = b >> 61
            x[n - 8] = b >> 53
            x[n - 7] = b >> 45
            x[n - 6] = b >> 37
            x[n - 5] = b >> 29
            x[n - 4] = b >> 21
            x[n - 3] = b >> 13
            x[n - 2] = b >>  5
            x[n - 1] = b <<  3
            m = ''.join(chr(i & 0xff) for i in x[0:n])
            while len(m) >= 128:
                    self.block(m[:128])
                    m = m[128:]
        
        def block(self, m):
            a = [int(), int(), int(), int(), int(), int(), int(), int()]
            b = [int(), int(), int(), int(), int(), int(), int(), int()]
            for i in range(8): 
                a[i] = self.h[i]
            w = list(unpack('16Q', bytes(m.encode('utf-8'))))
            for i in range(80):
                for j in range(8): b[j] = a[j]
                t = a[7] + self.Sigma1(a[4]) + self.Ch(a[4], a[5], a[6]) + self.K[i] + w[i % 16]
                b[7] = t + self.Sigma0(a[0]) + self.Maj(a[0], a[1], a[2])
                b[3] += t
                for j in range(8): a[(j + 1) % 8] = b[j] & 0xffffffffffffffff
                if i % 16 == 15:
                    for j in range(16):
                        w[j] += w[(j + 9) % 16] + self.sigma0(w[(j + 1) % 16]) + self.sigma1(w[(j + 14) % 16])
                        w[j] &= 0xffffffffffffffff
            for i in range(8):
                a[i] += self.h[i]; a[i] &= 0xffffffffffffffff; self.h[i] = a[i]

        def digest(self):
            return ''.join([pack('!Q', i) for i in self.h])

def sha512iv(self,m):
    iv = [ 0x6a09e667f3bcc908, 0xbb67ae8584caa73b, 0x3c6ef372fe94f82b, 0xa54ff53a5f1d36f1,  0x510e527fade682d1, 0x9b05688c2b3e6c1f, 0x1f83d9abfb41bd6b, 0x5be0cd19137e2179]
    for i in range(8): iv[i] ^= 0xa5a5a5a5a5a5a5a5
    return Sha512(m = m, iv = iv).digest()

def crypto_hash_sha512(m):
    return Sha512(m = m).digest()

def crypto_hash_sha512832(m):
    #SHA-512/832part0/2
    iv0 = [0xadb38ac93977be48, 0xde19da4cc66a210e, 0xd0f0fe22cce3cca1, 0x1126918ee6857bef, 0x8435943d7cfaa62d, 0x3d255b5a57a53250, 0x7bc8799f87eb2b0c, 0x0496cb2627276608]
    #SHA-512/832part1/2 
    iv1 = [0xf475be9f2bd41066, 0x8ed2d4aea6b90bab, 0x39a84d2ff27612f7, 0x6e2ef9453fecfcaf, 0x88e668f8ca37e0fa, 0x907111b322a56566, 0xb9e409a1a6782c79, 0x869d6aea2d9163a5]
    return Sha512(m = m, iv = iv0).digest()[0:52] + Sha512(m = m, iv = iv1).digest()[0:52]

def crypto_hash_sha512912(m):
    #SHA-512/912part0/2
    iv0 = [0x49974036cc4d00ec, 0x02986dda107d2c0b, 0x4fa7a30420ddc238, 0x85d93b8e59c64929, 0x872da4989431e9b1, 0x87737329cea0afcc, 0x1dcfbb605680d9e1, 0xf4c4a359cbed9394]
    #SHA-512/912part1/2
    iv1 = [0x1606b2f16b6193a3, 0xae53786e17d1b425, 0x2417a9a933672ecc, 0x73ddc4e58a733bca, 0x129089a9482bb3a8, 0xc800824e1b6a3fca, 0xa0fe65956d432c3b, 0x1d2fda618aa701b7]
    return Sha512(m = m, iv = iv0).digest()[0:57] + Sha512(m = m, iv = iv1).digest()[0:57]

def crypto_hash_sha5121056(m):
    #SHA-512/1056part0/4
    iv0 = [0x53f5d9b94c7ada62, 0xd50bca4c2b8dca99, 0xc714eb54c6c22168, 0xa0112a76dcce91d8, 0xb460bfc16f3c2d00, 0xd5ed29c01ec45df6, 0x24eb7ea0c050609a, 0x719b3cbe599c4fb9]
    #SHA-512/1056part1/4
    iv1 = [0x301e8bc126fc9ba0, 0xc27be7242ee18707, 0x9945eafca13a29b3, 0xc6f45e155648276c, 0xe3b97bae72c6d9b7, 0xb366dd6115f31f7c, 0x5f5f3816d639e9f2, 0x82538c19ab67424f]
    #SHA-512/1056part2/4
    iv2 = [0xe120e988bef03dd9, 0x519455168da7afbd, 0x263666fb5b2a6a8e, 0x31aff17f5b73216b, 0xcf98dadd49c924b1, 0x706a97b05cae0e84, 0x98e11d126b8a4660, 0x169b2ef3f2c003c4]
    #SHA-512/1056part3/4
    iv3 = [0xaf9cc87355edb24f, 0x5c033bfb9e8bff71, 0x6fcf94bed3d334be, 0xd3fbd41503ea3f26, 0x56296480281bd390, 0x045aad788858d876, 0xb27891b0300de5ab, 0x744e5f1bdc5fc133]
    h1 = Sha512(m = m, iv = iv0).digest()[0:33]
    h2 = Sha512(m = m, iv = iv1).digest()[0:33]
    h3 = Sha512(m = m, iv = iv2).digest()[0:33]
    h4 = Sha512(m = m, iv = iv3).digest()[0:33]
    return h1 + h2 + h3 + h4

class Eddsa(object):
    def H(self, m): # hash function, example: return crypto_hash_sha512(m)
        raise Exception("need to be overridden")
    
    def expmod(self, b, e, m):
        result = 1
        while e > 0:
            if e % 2 == 1:
                result = (result * b) % m
            b = (b * b) % m
            e = e // 2
        return result
    
    def edwards(self, P, Q):
        x1 = P[0]
        y1 = P[1]
        x2 = Q[0]
        y2 = Q[1]
        x3 = (x1*y2+y1*x2)*self.inv(1+self.d*x1*x2*y1*y2)
        y3 = (y1*y2-self.a*x1*x2)*self.inv(1-self.d*x1*x2*y1*y2)
        return [x3 % self.q, y3 % self.q]
    
    def inv(self, x):
        return self.expmod(x, self.q - 2, self.q)
    
    def scalarmult(self, P, e):
        if e == 0:
            return [0, 1]
        Q = self.scalarmult(P, e // 2)
        Q = self.edwards(Q, Q)
        if e& 1: 
            Q = self.edwards(Q, P)
        return Q
    
    def encodeint(self, y):
        bits = [(y>> i)& 1 for i in range(self.b)]
        return ''.join([chr(sum([bits[i * 8 + j]<< j for j in range(8)])) for i in range(self.b // 8)])
    
    def encodepoint(self, P):
        x = P[0]
        y = P[1]
        bits = [(y>> i)& 1 for i in range(self.b - 1)] + [x& 1]
        return ''.join([chr(sum([bits[i * 8 + j]<< j for j in range(8)])) for i in range(self.b // 8)])
    
    def bit(self, h, i):
        return (h[i // 8]>> (i % 8))& 1
    
    def publickey(self, sk):
        h = self.H(sk)
        a = 2**(self.n) + sum(2**i * self.bit(h, i) for i in range(self.c, self.n))
        A = self.scalarmult(self.B, a)
        return self.encodepoint(A)
    
    def Hint(self, m):
        h = self.H(m)
        return sum(2**i * self.bit(h, i) for i in range(2 * self.b))
    
    def sign(self, m, sk):
        if len(sk) != self.b//4: 
            raise Exception('secret-key length is wrong')
        pk = sk[(self.b // 8):]
        sk = sk[0:(self.b // 8)]
        h = self.H(sk)
        a = 2**(self.n) + sum(2**i * self.bit(h, i) for i in range(self.c, self.n))
        r = self.Hint(''.join([h[i] for i in range(self.b // 8, self.b // 4)]) + m)
        R = self.scalarmult(self.B, r)
        S = (r + self.Hint(self.encodepoint(R) + pk + m) * a) % self.l
        return self.encodepoint(R) + self.encodeint(S) + m
    
    def keypair(self):
        sk = os.urandom(self.b // 8)
        pk = self.publickey(sk)
        return [pk, sk + pk]
    
    def xrecover(self, y):
        if self.q % 8 == 5:
            xx = (y*y-1) * self.inv(self.d*y*y-self.a)
            x = self.expmod(xx,(self.q+3)//8,self.q)
            if (x*x - xx) % self.q != 0: x = (x*self.I) % self.q
            if x % 2 != 0: x = self.q-x
            return x
        if self.q % 4 == 3:
            xx = (y*y-1) * self.inv(self.d*y*y-self.a)
            x = self.expmod(xx,(self.q+1)//4,self.q)
            return x
        raise Exception('unsupported prime q')
        
    def decodeint(self, s):
        return sum(2**i * self.bit(s,i) for i in range(0,self.b))
    
    def isoncurve(self, P):
        x = P[0]
        y = P[1]
        return (self.a*x*x + y*y - 1 - self.d*x*x*y*y) % self.q == 0
    
    def decodepoint(self, s):
        y = sum(2**i * self.bit(s,i) for i in range(0,self.b-1))
        x = self.xrecover(y)
        if x& 1 != self.bit(s,self.b-1): x = self.q-x
        P = [x,y]
        if not self.isoncurve(P): raise Exception('decoding point that is not on curve')
        return P
    
    def open(self, m, pk):
        if len(m) < self.b//4: 
            raise Exception('signed message too short')
        if len(pk) != self.b//8: 
            raise Exception('public-key length is wrong')
        R = self.decodepoint(m[0:self.b//8])
        A = self.decodepoint(pk)
        S = self.decodeint(m[self.b//8:self.b//4])
        m = m[self.b//4:]
        h = self.Hint(self.encodepoint(R) + pk + m)
        if self.scalarmult(self.b, S) != self.edwards(R, self.scalarmult(A,h)):
            raise Exception('signature does not pass verification')
        return m

class Ed25519(Eddsa):
        b = 256
        n = 254
        q = 2**255 - 19
        d = -4513249062541557337682894930092624173785641285191125241628941591882900924598840740
        c = 3
        a = -1
        B = [15112221349535400772501151409588531511454012693041857206046113283949847762202, 46316835694926478169428394003475163141307993866256225615783033603165251855960]
        l = 2**252 + 27742317777372353535851937790883648493
        I = 19681161376707505956807079304988542015446066515923890162744021073123829784752
        name = 'Ed25519-SHA-512'
        def H(self, m):
            return crypto_hash_sha512(m)
        #crypto_hash_sha512(m)
#            combined_hash = hexlify(hashlib.sha3_512(m).digest())
#            return combined_hash

class Ed41417(Eddsa):
        b = 416
        n = 414
        q = 2**414 - 17
        d = 3617
        c = 3
        a = 1
        B = [17319886477121189177719202498822615443556957307604340815256226171904769976866975908866528699294134494857887698432266169206165, 34]
        l = 2**411 - 33364140863755142520810177694098385178984727200411208589594759
        I = None
        name = 'Ed4417-SHA-512+BLAKE2b/832'
        def H(self, m):
            combined_hash = hashlib.sha3_512(m).digest()[0:52] + hashlib.blake2b(m).digest()[0:52]
            return combined_hash

class Ed448(Eddsa):
        b = 456
        n = 448
        q = 2**448 - 2**224 - 1
        d = -39081
        c = 2
        a = 1
        B = [117812161263436946737282484343310064665180535357016373416879082147939404277809514858788439644911793978499419995990477371552926308078495, 19]
        l = 2**446 - 13818066809895115352007386748515426880336692474882178609894547503885
        I = None
        name = 'Ed448-SHA-512+BLAKE2b/912'
        def H(self, m):
            combined_hash = hashlib.sha3_512(m).hexdigest()[0:57] + hashlib.blake2b(m).hexdigest()[0:57]
            return combined_hash

class Ed521(Eddsa):
        b = 528
        n = 521
        q = 2**521 - 1
        d = -376014
        c = 2
        a = 1
        B = [1571054894184995387535939749894317568645297350402905821437625181152304994381188529632591196067604100772673927915114267193389905003276673749012051148356041324, 12] 
        l = 2**519 - 337554763258501705789107630418782636071904961214051226618635150085779108655765
        I = None
        name = 'Ed521-SHA3_512+SHA3_384+BLAKE2b+BLAKE2s/1056'
        def H(self, m):
            combined_hash = hashlib.sha3_512(m).hexdigest()[0:44] + hashlib.sha512(m).hexdigest()[0:44] + hashlib.blake2b(m).hexdigest()[0:44]
            combined_hash_digest = combined_hash.encode('utf-8')
            return combined_hash_digest
        
def crypto_sign_ed25519_keypair():
    return Ed25519().keypair()

def crypto_sign_ed25519(m, sk):
    return Ed25519().sign(m, sk)

def crypto_sign_ed25519_open(sm, pk):
    return Ed25519().open(sm, pk)
    
def crypto_sign_ed41417_keypair():
    return Ed41417().keypair()
    
def crypto_sign_ed41417(m, sk):
    return Ed41417().sign(m, sk)
    
def crypto_sign_ed41417_open(sm, pk):
    return Ed41417().open(sm, pk)

def crypto_sign_ed521_keypair():
    return Ed521().keypair()
    
def crypto_sign_ed521(m, sk):
    return Ed521().sign(m, sk)
    
def crypto_sign_ed521_open(sm, pk):
    return Ed521().open(sm, pk)

def crypto_sign_ed448_keypair():
    return Ed448().keypair()
    
def crypto_sign_ed448(m, sk):
    return Ed448().sign(m, sk)
    
def crypto_sign_ed448_open(sm, pk):
    return Ed448().open(sm, pk)

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
    return qr_image_pixel_width, combined_qr_code_png_output_file_name, output_blocks_list
    

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
    qr_image_pixel_width, combined_qr_code_png_output_file_name, output_blocks_list = encode_file_into_ztd_luby_block_qr_codes_func(input_data_or_path)
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
    return list_of_frame_filepaths, combined_qr_code_png_output_file_name

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
path_to_animecoin_html_ticket_file = 'C:\\animecoin\\art_folders_to_encode\\Carlo_Angelo__Felipe_Number_02\\artwork_metadata_ticket__2018_05_27__23_42_33_964953.html'
with open(path_to_animecoin_html_ticket_file,'rb') as f:
   animecoin_ticket_html_string = f.read()
   
message = animecoin_ticket_html_string
use_demonstrate_sphincs_crypto = 0
use_demonstrate_eddsa_crypto = 0
use_demonstrate_libsodium_crypto = 0
use_demonstrate_qr_code_generation = 0
use_demonstrate_qr_steganography = 0
use_demonstrate_qr_data_transmission_animation = 1
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
    if 0:
        crypto_sign = crypto_sign_ed521
        crypto_sign_keypair = crypto_sign_ed521_keypair
        crypto_sign_open = crypto_sign_ed521_open
        ed = Ed521()
    
    crypto_sign = crypto_sign_ed25519
    crypto_sign_keypair = crypto_sign_ed25519_keypair
    crypto_sign_open = crypto_sign_ed25519_open
    ed = Ed25519()

    pk, sk = crypto_sign_keypair()
    print('#%s' % (ed.name))
    print('PUBLICKEY=%s' % (pk.encode('hex')))
    print('SECRETKEY=%s' % (sk.encode('hex')))
   
    print('Now Signing message with secret key...')
    sk = binascii.unhexlify(sk)
    sys.stdout.write(crypto_sign(sys.stdin.read(), sk))
    sys.stdout.flush()
    
    print('Now verifying signature with public key...')
    pk = binascii.unhexlify(pk)
    sys.stdout.write(crypto_sign_open(sys.stdin.read(), pk))
    sys.stdout.flush()

if use_demonstrate_libsodium_crypto:
    signing_key = nacl.signing.SigningKey.generate() # Generate a new random signing key
    signed = signing_key.sign(b"Attack at Dawn") # Sign a message with the signing key
    verify_key = signing_key.verify_key # Obtain the verify key for a given signing key
    verify_key_hex = verify_key.encode(encoder=nacl.encoding.HexEncoder) # Serialize the verify key to send it to a third party
    
if use_demonstrate_qr_code_generation:
    path_to_input_file = 'us_constitution.txt'
    path_to_input_file = 'pride_and_prejudice.txt'
    #path_to_input_file = 'sphincs_secret_key.dat'
    with open(path_to_input_file,'rb') as f:
        sphincs_secret_key_raw_bytes = f.read()
    input_data = sphincs_secret_key_raw_bytes
    input_data_hash = get_sha256_hash_of_input_data_func(input_data)
    input_data_or_path = input_data
    qr_image_pixel_width, combined_qr_code_png_output_file_name, output_blocks_list = encode_file_into_ztd_luby_block_qr_codes_func(input_data_or_path)
    path_to_image_file_with_qr_codes = combined_qr_code_png_output_file_name
    list_of_luby_block_data_binaries, reconstructed_uncompressed_data = decode_combined_ztd_luby_block_qr_code_image_func(path_to_image_file_with_qr_codes, qr_image_pixel_width)
    reconstructed_uncompressed_data_hash = get_sha256_hash_of_input_data_func(reconstructed_uncompressed_data)
    if input_data_hash != reconstructed_uncompressed_data_hash:
        reconstructed_uncompressed_data = reconstructed_uncompressed_data.decode('utf-8')
        reconstructed_uncompressed_data_hash = get_sha256_hash_of_input_data_func(reconstructed_uncompressed_data)
    assert(input_data_hash == reconstructed_uncompressed_data_hash)
    print('Decompressed file hash matches original file!')
    
if use_demonstrate_qr_data_transmission_animation:
    path_to_input_file = 'pride_and_prejudice.txt'
    input_data_or_path = path_to_input_file
    existing_frame_paths = glob.glob('ANIMECOIN_QR_ANIMATION__Frame_*.png')
    for current_frame_path in existing_frame_paths:
        os.remove(current_frame_path)
    list_of_frame_filepaths, combined_qr_code_png_output_file_name = generate_data_animation_func(input_data_or_path)
    gif_name = combined_qr_code_png_output_file_name.replace('.png','').replace('ANIME_QR_CODE_COMBINED__','ANIMECOIN_QR_ANIMATION__')
    fps = 3
    file_list = glob.glob('ANIMECOIN_QR_ANIMATION__Frame_*.png') # Get all the pngs in the current directory
    clip = mpy.ImageSequenceClip(file_list, fps=fps)
    clip.write_gif('{}.gif'.format(gif_name), fps=fps)
    animation_gif_file_paths = glob.glob('ANIMECOIN_QR_ANIMATION__*.gif')
    animation_gif_file_path = animation_gif_file_paths[0]
    chrome_options = Options()
    chrome_options.add_argument('--start-fullscreen')
    chrome_options.add_argument('--disable-infobars')
    
    driver = webdriver.Chrome(chrome_options=chrome_options)
    animated_gif_player_html_output_file_path = 'animated_gif_player.html'
    image_uri_string = 'file:///C:/Users/jeffr/Cointel%20Dropbox/Animecoin_Code/'+animation_gif_file_path
    player_uri_string = 'file:///C:/Users/jeffr/Cointel%20Dropbox/Animecoin_Code/'+animated_gif_player_html_output_file_path
    html_string = '<html><body> <style> html {background:  url('+ image_uri_string +'); background-size: contain; height: 100%;  background-position: center; background-repeat: no-repeat;}  body {height: 100%;} </style></body></html>'
    with open(animated_gif_player_html_output_file_path, 'w') as f:
        f.write(html_string)
    driver.get(player_uri_string)
    sleep(5)
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
        squares = find_squares(img)
        flatten = lambda l: [item for sublist in l for item in sublist]
        flat_squares = flatten(flatten(squares))
        flat_squares.sort()
        flat_squares_diff = np.diff(flat_squares)
        lower_left = sorted(set(flat_squares))[10]
        upper_right = sorted(set(flat_squares))[-10]
        coords = [lower_left,lower_left,upper_right,upper_right]
        warped = four_point_transform_func(img, coords)
        
        
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
