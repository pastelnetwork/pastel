import itertools, os, struct, time, base64, hashlib, glob, random, sys, binascii, shutil, io
from math import ceil, floor, log, log2, sqrt
from binascii import hexlify
from PIL import Image, ImageChops, ImageOps, ImageEnhance
import pyqrcode
from pyzbar.pyzbar import decode
import nacl.encoding
import nacl.signing
import imageio
import zstd
import moviepy.editor as mpy
from fs.memoryfs import MemoryFS
from fs.copy import copy_fs
from fs.osfs import OSFS
from tqdm import tqdm
from anime_utility_functions_v1 import get_sha256_hash_of_input_data_func
from anime_fountain_coding_v1 import PRNG, BlockGraph

sys.setrecursionlimit(2000)
sigma = "expand 32-byte k"
tau = "expand 16-byte k"
#pip install pyqrcode, pypng, pyzbar, tqdm, numpy, piexif, pynacl, imageio, zstd

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
        return struct.unpack('!L', bytestr)[0]
    
    def _eightByte2int(self, bytestr): #convert a 8-byte string to an int (long long) 
        return struct.unpack('!Q', bytestr)[0]
    
    def _int2fourByte(self, x): #convert a number to a 4-byte string, high order truncation possible (in Python x could be a BIGNUM) see also long2byt() below
        return struct.pack('!L', x)
    
    def _int2eightByte(self, x): #convert a number to a 8-byte string, high order truncation possible (in Python x could be a BIGNUM)
        return struct.pack('!Q', x)

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

    def pack(self, x):
        if type(x) is bytes:
            return x
        if type(x) is int:  # needed for index i
            return int.to_bytes(x, length=(self.h+7)//8, byteorder='little')
        return b''.join([self.pack(a) for a in iter(x)])

    def unpack(self, sk=None, pk=None, sig=None, byteseq=None):
        n = self.n // 8
        if sk:
            return sk[:n], sk[n:2*n], self.unpack(byteseq=sk[2*n:])
        elif pk:
            return pk[:n], self.unpack(byteseq=pk[n:])
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
                sig_horst.append((sk, self.unpack(byteseq=auth)))
            sigma_k, sig = prefix(sig, (1<< self.horst.x) * n)
            sig_horst.append(self.unpack(byteseq=sigma_k))
            wots = []
            for _ in range(self.d):
                wots_sig, sig = prefix(sig, self.wots.l*n)
                path, sig = prefix(sig, self.h//self.d*n)
                wots.append(self.unpack(byteseq=wots_sig))
                wots.append(self.unpack(byteseq=path))
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
            w = list(struct.unpack('16Q', bytes(m.encode('utf-8'))))
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
            return ''.join([struct.pack('!Q', i) for i in self.h])

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
            rgb = ( r[-1] + '0000000', g[-1] + '0000000', b[-1] + '0000000', a[-1] + '0000000') # Extract the last 4 bits (corresponding to the hidden image);  Concatenate 4 zero bits because we are working with 8 bit values
            pixels_new[ii, jj] = convert_binary_string_tuple_to_integer_tuple_func(rgb) # Convert it to an integer tuple
            #print(pixels_new[ii, jj])
            if pixels_new[ii, jj] != (0, 0, 0, 128): # If this is a 'valid' position, store it as the last valid position
                original_size = (ii + 1, jj + 1)
    embedded_image = embedded_image.crop((0, 0, original_size[0], original_size[1]))  # Crop the image based on the 'valid' pixels
    return embedded_image

def binarize_image_func(pil_image):
    thresh = 60
    fn = lambda x : 255 if x > thresh else 0
    monochrome_pil_image = pil_image.convert('L').point(fn, mode='1')
    return monochrome_pil_image

def check_if_image_is_all_black_func(pil_image):
    return not pil_image.getbbox() # Image.getbbox() returns the falsy None if there are no non-black pixels in the image, otherwise it returns a tuple of points, which is truthy

def check_if_image_is_all_white_func(pil_image):
    return not ImageChops.invert(pil_image).getbbox()

def invert_image_if_needed_func(pil_image):
    upper_left_pixel = pil_image.getpixel((0, 0))
    threshold = 20
    try:
        average_pixel_value = (upper_left_pixel[0] + upper_left_pixel[1] + upper_left_pixel[2])/3
        if average_pixel_value < threshold:
            pil_image = ImageOps.invert(pil_image)
        return pil_image
    except:
        return pil_image
    
def add_base32_padding_characters_func(encoded_string):
    padding = ''
    remainder = len(encoded_string) % 8 
    if remainder == 2:
        padding = '======'
    elif remainder == 4:
        padding = '===='
    elif remainder == 5:
        padding = '==='
    elif remainder == 7:
        padding = '=='
    encoded_string = encoded_string + padding
    return encoded_string

def clear_existing_qr_code_image_files_func():
    existing_qr_code_filepaths = glob.glob('ANIME_QR_CODE__*.png')
    for current_filepath in existing_qr_code_filepaths:
        os.remove(current_filepath)

def compress_data_with_zstd_func(input_data):
    zstd_compression_level = 22 #Highest (best) compression level is 22
    zstandard_compressor = zstd.ZstdCompressor(level=zstd_compression_level, write_content_size=True)
    if isinstance(input_data, str):
        input_data = input_data.encode('utf-8')
    zstd_compressed_data = zstandard_compressor.compress(input_data)
    return zstd_compressed_data

def decompress_data_with_zstd_func(zstd_compressed_data):
    zstandard_decompressor = zstd.ZstdDecompressor()
    uncompressed_data = zstandard_decompressor.decompress(zstd_compressed_data)
    return uncompressed_data

def encode_file_into_lt_block_qr_codes_func(path_to_input_file): #sphincs_secret_key_raw_bytes
    if isinstance(path_to_input_file, str): 
        with open(path_to_input_file,'rb') as f:
            f_bytes = f.read()
        input_data_original_size_in_bytes = os.path.getsize(path_to_input_file)
    else:
        f_bytes = path_to_input_file
        input_data_original_size_in_bytes = len(f_bytes)
    existing_qr_code_filepaths = glob.glob('ANIME_QR_CODE__*.png')
    for current_filepath in existing_qr_code_filepaths:
        os.remove(current_filepath)
    block_redundancy_factor = 2
    desired_block_size_in_bytes = 2500
    c_constant = 0.1
    delta_constant = 0.5
    seed = random.randint(0, 1 << 31 - 1)
    if not isinstance(path_to_input_file, str):
        filename = ''
    else:
        filename = os.path.split(path_to_input_file)[-1] 
    print('Now encoding file ' + filename + ' (' + str(round(input_data_original_size_in_bytes/1000000)) + 'mb)\n\n')
    total_number_of_blocks_to_generate = ceil((1.00*block_redundancy_factor*input_data_original_size_in_bytes) / desired_block_size_in_bytes)
    print('Total number of blocks to generate for target level of redundancy: '+str(total_number_of_blocks_to_generate))
    pbar = tqdm(total=total_number_of_blocks_to_generate)
    input_data_hash = get_sha256_hash_of_input_data_func(f_bytes)
    blocks = [int.from_bytes(f_bytes[ii:ii+desired_block_size_in_bytes].ljust(desired_block_size_in_bytes, b'0'), sys.byteorder) for ii in range(0, len(f_bytes), desired_block_size_in_bytes)]
    number_of_blocks = len(blocks)
    print('The length of the blocks list: ' + str(number_of_blocks))
    prng = PRNG(params=(number_of_blocks, delta_constant, c_constant))
    prng.set_seed(seed)
    number_of_blocks_generated = 0
    list_of_compressed_block_data = list()
    list_of_compressed_block_data_hashes = list()
    while number_of_blocks_generated <= total_number_of_blocks_to_generate:
        update_skip = 1
        if (number_of_blocks_generated % update_skip) == 0:
            pbar.update(update_skip)
        blockseed, d, ix_samples = prng.get_src_blocks()
        block_data = 0
        for ix in ix_samples:
            block_data ^= blocks[ix]
        block = (input_data_original_size_in_bytes, desired_block_size_in_bytes, blockseed, int.to_bytes(block_data, desired_block_size_in_bytes, sys.byteorder)) # Generate blocks of XORed data in network byte order
        number_of_blocks_generated = number_of_blocks_generated + 1
        packed_block_data = struct.pack('!III%ss'%desired_block_size_in_bytes, *block)
        compressed_packed_block_data = compress_data_with_zstd_func(packed_block_data)
        list_of_compressed_block_data.append(compressed_packed_block_data)
        list_of_compressed_block_data_hashes.append(get_sha256_hash_of_input_data_func(compressed_packed_block_data))
    print('\n\nFinished processing! \nOriginal file ('+input_data_hash+') was encoded into ' + str(number_of_blocks_generated) + ' blocks of ' + str(ceil(desired_block_size_in_bytes/1000)) + ' kilobytes each. Total size of all blocks is ~' + str(ceil((number_of_blocks_generated*desired_block_size_in_bytes))) + ' bytes\n')
    overall_counter = 0
    for block_cnt, current_block_data in enumerate(list_of_compressed_block_data):
        current_block_data_hash = list_of_compressed_block_data_hashes[block_cnt]
        current_block_data_base32_encoded = base64.b32encode(current_block_data).decode('utf-8')
        current_block_data_base32_encoded_character_length = len(current_block_data_base32_encoded)
        print('Total data length is ' + str(current_block_data_base32_encoded_character_length) + ' characters.')
        pyqrcode_version_number = 25 #Max is 40;
        qr_error_correcting_level = 'Q' # L, M, Q, H
        qr_encoding_type = 'alphanumeric'
        qr_scale_factor = 4
        qr_encoding_type_index = pyqrcode.tables.modes[qr_encoding_type]
        qrcode_capacity_dict = pyqrcode.tables.data_capacity[pyqrcode_version_number] # Using max error corrections; see: pyqrcode.tables.data_capacity[40]
        max_characters_in_single_qr_code = qrcode_capacity_dict[qr_error_correcting_level][qr_encoding_type_index]
        required_number_of_qr_codes = ceil(float(current_block_data_base32_encoded_character_length)/float(max_characters_in_single_qr_code))
        print('A total of '+str(required_number_of_qr_codes) +' QR codes is required.')
        list_of_light_hex_color_strings = ['#C9D6FF','#DBE6F6','#F0F2F0','#E0EAFC','#ffdde1','#FFEEEE','#E4E5E6','#DAE2F8','#D4D3DD','#d9a7c7','#fffcdc','#f2fcfe','#F8FFAE','#F0F2F0']
        list_of_dark_hex_color_strings = ['#203A43','#2C5364','#373B44','#3c1053','#333333','#23074d','#302b63','#24243e','#0f0c29','#2F0743','#3C3B3F','#000046','#200122','#1D4350','#2948ff']
        combined_hex_color_strings = [list_of_light_hex_color_strings,list_of_dark_hex_color_strings]
        list_of_encoded_data_blobs = list()
        for cnt in range(required_number_of_qr_codes):
            starting_index = cnt*max_characters_in_single_qr_code
            ending_index = min([len(current_block_data_base32_encoded), (cnt+1)*max_characters_in_single_qr_code])
            encoded_data_for_current_qr_code = current_block_data_base32_encoded[starting_index:ending_index]
            encoded_data_for_current_qr_code = encoded_data_for_current_qr_code.replace('=','A')
            list_of_encoded_data_blobs.append(encoded_data_for_current_qr_code)
            current_qr_code = pyqrcode.create(encoded_data_for_current_qr_code, error=qr_error_correcting_level, version=pyqrcode_version_number, mode=qr_encoding_type)
            qr_code_png_output_file_name = 'ANIME_QR_CODE__'+ str(current_block_data_hash) + '__' '{0:03}'.format(overall_counter) + '.png'
            print('Saving: '+qr_code_png_output_file_name)
            first_random_color = random.choice(random.choice(combined_hex_color_strings))
            if first_random_color in list_of_light_hex_color_strings:
                second_random_color = random.choice(list_of_dark_hex_color_strings)
            else:
                second_random_color = random.choice(list_of_light_hex_color_strings)
            current_qr_code.png(qr_code_png_output_file_name, scale=qr_scale_factor, module_color=first_random_color, background=second_random_color)
            overall_counter = overall_counter + 1
    qr_code_file_paths = glob.glob('ANIME_QR_CODE__*.png')
    if len(qr_code_file_paths) > 0:
        first_path = qr_code_file_paths[0]
        first_qr_image_data = Image.open(first_path)
        image_pixel_width, image_pixel_height = first_qr_image_data.size
    print('Pixel width/height of QR codes generated: '+str(image_pixel_width)+', '+str(image_pixel_height))
    N = pow(ceil(sqrt(len(qr_code_file_paths))), 2) #Find nearest square
    total_width = int(sqrt(N)*image_pixel_width)
    total_height = int(sqrt(N)*image_pixel_height)
    print('Combined Image Dimensions: '+str(total_width)+', '+str(total_height))
    combined_qr_code_image = Image.new('RGBA', (total_width, total_height))
    number_in_each_row = int(sqrt(N))
    number_in_each_column = int(sqrt(N))
    image_counter = 0
    for ii in range(number_in_each_row):
        for jj in range(number_in_each_column):
            print('Row Counter: '+str(ii)+'; Column Counter: '+str(jj)+';')
            if image_counter < len(qr_code_file_paths):
                current_image_file_path = qr_code_file_paths[image_counter]
                current_image_data = Image.open(current_image_file_path)
                new_image_coords =  (ii*image_pixel_width, jj*image_pixel_height)
                print('New Image Upper-Left Corner: '+str(new_image_coords[0])+', '+str(new_image_coords[1]))
                combined_qr_code_image.paste(current_image_data, new_image_coords)
                image_counter = image_counter + 1
    combined_qr_code_png_output_file_name = 'ANIME_QR_CODE_COMBINED__' + input_data_hash + '.png'
    combined_qr_code_image.save(combined_qr_code_png_output_file_name)
    return image_pixel_width, combined_qr_code_png_output_file_name
    
def encode_data_as_qr_codes_func(input_data):
    if isinstance(input_data, str):
        input_data = input_data.encode('utf-8')
    input_data_hash = hashlib.sha3_256(input_data).hexdigest().encode('utf-8')
    input_data_combined = input_data_hash + input_data
    input_data_base32_encoded = base64.b32encode(input_data_combined).decode('utf-8')
    input_data_base32_encoded_character_length = len(input_data_base32_encoded)
    print('Total data length is ' + str(input_data_base32_encoded_character_length) + ' characters.')
    pyqrcode_version_number = 4 #Max is 40;
    qr_error_correcting_level = 'M' # L, M, Q, H
    qr_encoding_type = 'alphanumeric'
    qr_scale_factor = 6
    qr_encoding_type_index = pyqrcode.tables.modes[qr_encoding_type]
    qrcode_capacity_dict = pyqrcode.tables.data_capacity[pyqrcode_version_number] # Using max error corrections; see: pyqrcode.tables.data_capacity[40]
    max_characters_in_single_qr_code = qrcode_capacity_dict[qr_error_correcting_level][qr_encoding_type_index]
    required_number_of_qr_codes = ceil(float(input_data_base32_encoded_character_length)/float(max_characters_in_single_qr_code))
    print('A total of '+str(required_number_of_qr_codes) +' QR codes is required.')
    list_of_light_hex_color_strings = ['#C9D6FF','#DBE6F6','#F0F2F0','#E0EAFC','#ffdde1','#FFEEEE','#E4E5E6','#DAE2F8','#D4D3DD','#d9a7c7','#fffcdc','#f2fcfe','#F8FFAE','#F0F2F0']
    list_of_dark_hex_color_strings = ['#203A43','#2C5364','#373B44','#3c1053','#333333','#23074d','#302b63','#24243e','#0f0c29','#2F0743','#3C3B3F','#000046','#200122','#1D4350','#2948ff']
    combined_hex_color_strings = [list_of_light_hex_color_strings,list_of_dark_hex_color_strings]
    list_of_encoded_data_blobs = list()
    list_of_generated_qr_images = list()
    for cnt in range(required_number_of_qr_codes):
        starting_index = cnt*max_characters_in_single_qr_code
        ending_index = min([len(input_data_base32_encoded), (cnt+1)*max_characters_in_single_qr_code])
        encoded_data_for_current_qr_code = input_data_base32_encoded[starting_index:ending_index]
        encoded_data_for_current_qr_code = encoded_data_for_current_qr_code.replace('=','A')
        list_of_encoded_data_blobs.append(encoded_data_for_current_qr_code)
        current_qr_code = pyqrcode.create(encoded_data_for_current_qr_code, error=qr_error_correcting_level, version=pyqrcode_version_number, mode=qr_encoding_type)
        qr_code_png_output_file_name = 'ANIME_QR_CODE__'+ input_data_hash.decode('utf-8') + '__' '{0:03}'.format(cnt) + '.png'
        print('Saving: '+qr_code_png_output_file_name)
        first_random_color = random.choice(random.choice(combined_hex_color_strings))
        if first_random_color in list_of_light_hex_color_strings:
            second_random_color = random.choice(list_of_dark_hex_color_strings)
        else:
            second_random_color = random.choice(list_of_light_hex_color_strings)
        current_qr_code.png(qr_code_png_output_file_name, scale=qr_scale_factor, module_color=first_random_color, background=second_random_color)
        qr_code_png_image_data = Image.open(qr_code_png_output_file_name)
        list_of_generated_qr_images.append(qr_code_png_image_data)
        image_pixel_width, image_pixel_height = list_of_generated_qr_images[0].size
    print('Pixel width/height of QR codes generated: '+str(image_pixel_width)+', '+str(image_pixel_height))
    N = pow(ceil(sqrt(required_number_of_qr_codes)), 2) #Find nearest square
    total_width = int(sqrt(N)*image_pixel_width)
    total_height = int(sqrt(N)*image_pixel_height)
    print('Combined Image Dimensions: '+str(total_width)+', '+str(total_height))
    combined_qr_code_image = Image.new('RGBA ', (total_width, total_height))
    number_in_each_row = int(sqrt(N))
    number_in_each_column = int(sqrt(N))
    image_counter = 0
    for ii in range(number_in_each_row):
        for jj in range(number_in_each_column):
            print('Row Counter: '+str(ii)+'; Column Counter: '+str(jj)+';')
            if image_counter < len(list_of_generated_qr_images):
                current_image_data = list_of_generated_qr_images[image_counter]
                new_image_coords =  (ii*image_pixel_width, jj*image_pixel_height)
                print('New Image Upper-Left Corner: '+str(new_image_coords[0])+', '+str(new_image_coords[1]))
                combined_qr_code_image.paste(current_image_data,new_image_coords)
                image_counter = image_counter + 1
    combined_qr_code_png_output_file_name = 'ANIME_QR_CODE_COMBINED__' + input_data_hash.decode('utf-8') + '.png'
    combined_qr_code_image.save(combined_qr_code_png_output_file_name)
    existing_qr_code_filepaths = glob.glob('ANIME_QR_CODE__*.png')
    for current_filepath in existing_qr_code_filepaths:
        os.remove(current_filepath)
    return list_of_encoded_data_blobs, image_pixel_width, input_data_base32_encoded

def generate_data_animation_func(input_data):
    if isinstance(input_data, str):
        input_data = input_data.encode('utf-8')
    input_data_hash = hashlib.sha3_256(input_data).hexdigest().encode('utf-8')
    input_data_combined = input_data_hash + input_data
    input_data_base32_encoded = base64.b32encode(input_data_combined).decode('utf-8')
    input_data_base32_encoded_character_length = len(input_data_base32_encoded)
    print('Total data length is ' + str(input_data_base32_encoded_character_length) + ' characters.')
    pyqrcode_version_number = 3 #Max is 40;
    qr_error_correcting_level = 'H' # L, M, Q, H
    qr_encoding_type = 'alphanumeric'
    qr_scale_factor = 5
    qr_encoding_type_index = pyqrcode.tables.modes[qr_encoding_type]
    qrcode_capacity_dict = pyqrcode.tables.data_capacity[pyqrcode_version_number] # Using max error corrections; see: pyqrcode.tables.data_capacity[40]
    max_characters_in_single_qr_code = qrcode_capacity_dict[qr_error_correcting_level][qr_encoding_type_index]
    required_number_of_qr_codes = ceil(float(input_data_base32_encoded_character_length)/float(max_characters_in_single_qr_code))
    print('A total of '+str(required_number_of_qr_codes) +' QR codes is required.')
    list_of_light_hex_color_strings = ['#C9D6FF','#DBE6F6','#F0F2F0','#E0EAFC','#ffdde1','#FFEEEE','#E4E5E6','#DAE2F8','#D4D3DD','#d9a7c7','#fffcdc','#f2fcfe','#F8FFAE','#F0F2F0']
    list_of_dark_hex_color_strings = ['#203A43','#2C5364','#373B44','#3c1053','#333333','#23074d','#302b63','#24243e','#0f0c29','#2F0743','#3C3B3F','#000046','#200122','#1D4350','#2948ff']
    combined_hex_color_strings = [list_of_light_hex_color_strings,list_of_dark_hex_color_strings]
    list_of_encoded_data_blobs = list()
    list_of_frames = list()
    for cnt in range(required_number_of_qr_codes):
        starting_index = cnt*max_characters_in_single_qr_code
        ending_index = min([len(input_data_base32_encoded), (cnt+1)*max_characters_in_single_qr_code])
        encoded_data_for_current_qr_code = input_data_base32_encoded[starting_index:ending_index]
        encoded_data_for_current_qr_code = encoded_data_for_current_qr_code.replace('=','A')
        list_of_encoded_data_blobs.append(encoded_data_for_current_qr_code)
        current_qr_code = pyqrcode.create(encoded_data_for_current_qr_code, error=qr_error_correcting_level, version=pyqrcode_version_number, mode=qr_encoding_type)
        first_random_color = random.choice(random.choice(combined_hex_color_strings))
        if first_random_color in list_of_light_hex_color_strings:
            second_random_color = random.choice(list_of_dark_hex_color_strings)
        else:
            second_random_color = random.choice(list_of_light_hex_color_strings)
        qr_code_png_output_file_name = 'ANIME_QR_CODE__'+ input_data_hash.decode('utf-8') + '__' '{0:03}'.format(cnt) + '.png'
        #current_qr_code.png(qr_code_png_output_file_name, scale=qr_scale_factor, module_color=first_random_color, background=second_random_color)
        current_qr_code.png(qr_code_png_output_file_name, scale=qr_scale_factor, module_color=first_random_color, background=second_random_color)
        image_pixel_width, image_pixel_height = current_qr_code.size
    return image_pixel_width
#    gif_name = 'outputName'
#    fps = 12
#    file_list = glob.glob('*.png') # Get all the pngs in the current directory
#    list.sort(file_list, key=lambda x: int(x.split('_')[1].split('.png')[0])) # Sort the images by #, this may need to be tweaked for your use case
#    clip = mpy.ImageSequenceClip(file_list, fps=fps)
#    clip.write_gif('{}.gif'.format(gif_name), fps=fps)
#    writer = imageio.get_writer('ANIME_QR_VID__' + input_data_hash.decode('utf-8') + '__' '{0:03}'.format(cnt) + '.mp4', 45)
  
#    print('Pixel width/height of QR codes generated: '+str(image_pixel_width)+', '+str(image_pixel_height))
if 0: #Debugging:
    image_to_embed = combined_qr_code_data
    host_image = art_image_data_resized
    pil_image = art_image_data_resized
    input_data = combined_qr_code_data
    
def enhance_pil_image_func(pil_image, enhancement_amount):
    try:
        x = pil_image
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
    
def decode_data_lt_coded_qr_code_images_func(path_to_image_file_with_qr_codes, image_pixel_width):
    enhancement_amount = 5
    standard_qr_code_pixel_width = image_pixel_width
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
            print('Row Counter: '+str(ii)+'; Column Counter: '+str(jj)+';')
            current_subimage_area = (ii*standard_qr_code_pixel_width, jj*standard_qr_code_pixel_height, (ii+1)*standard_qr_code_pixel_width, (jj+1)*standard_qr_code_pixel_height)
            current_qr_image_data = combined_qr_code_image.crop(current_subimage_area)
            iteration_counter = iteration_counter + 1
            print('Iteration Count: '+str(iteration_counter)+'; Current Sub-Image Area Tuple: ('+str(current_subimage_area[0])+', '+str(current_subimage_area[1])+', '+str(current_subimage_area[2])+', '+str(current_subimage_area[3])+')')
            list_of_qr_code_image_data.append(current_qr_image_data)
    list_of_decoded_data_blobs = list()   
    combined_reconstructed_string = ''
    for current_qr_image in list_of_qr_code_image_data:
        rgb_version_of_current_qr_image = Image.new('RGB', current_qr_image.size, (0, 0, 0))
        rgb_version_of_current_qr_image.paste(current_qr_image, mask=current_qr_image.split()[3])
        rgb_version_of_current_qr_image_enhanced = enhance_pil_image_func(rgb_version_of_current_qr_image, enhancement_amount)
        rgb_version_of_current_qr_image_enhanced = invert_image_if_needed_func(rgb_version_of_current_qr_image_enhanced)
        #rgb_version_of_current_qr_image_enhanced = binarize_image_func(rgb_version_of_current_qr_image_enhanced)
        decoded_data = decode(rgb_version_of_current_qr_image_enhanced)
        if len(decoded_data) == 0:
            decoded_data = decode(invert_image_if_needed_func(rgb_version_of_current_qr_image_enhanced))
        if len(decoded_data) == 0:
            decoded_data = decode(enhance_pil_image_func(rgb_version_of_current_qr_image_enhanced, 9))
        if len(decoded_data) > 0:
            decoded_data_raw = decoded_data[0].data.decode('utf-8')
            list_of_decoded_data_blobs.append(decoded_data_raw)
            combined_reconstructed_string = combined_reconstructed_string + decoded_data_raw
    combined_reconstructed_string = combined_reconstructed_string.split('AAAAAA')[0]
    combined_reconstructed_string_padded = add_base32_padding_characters_func(combined_reconstructed_string)
    reconstructed_input_data_base32_decoded = base64.b32decode(combined_reconstructed_string_padded)
    reconstructed_input_data_hash = reconstructed_input_data_base32_decoded[:64]
    reconstructed_data = reconstructed_input_data_base32_decoded[64:]
    reconstructed_data_hash = hashlib.sha3_256(reconstructed_data).hexdigest().encode('utf-8')
    assert(reconstructed_input_data_hash == reconstructed_data_hash)
        
def decode_data_from_qr_code_images_func(path_to_image_file_with_qr_codes, image_pixel_width):
    enhancement_amount = 5
    standard_qr_code_pixel_width = image_pixel_width
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
            print('Row Counter: '+str(ii)+'; Column Counter: '+str(jj)+';')
            current_subimage_area = (ii*standard_qr_code_pixel_width, jj*standard_qr_code_pixel_height, (ii+1)*standard_qr_code_pixel_width, (jj+1)*standard_qr_code_pixel_height)
            current_qr_image_data = combined_qr_code_image.crop(current_subimage_area)
            iteration_counter = iteration_counter + 1
            print('Iteration Count: '+str(iteration_counter)+'; Current Sub-Image Area Tuple: ('+str(current_subimage_area[0])+', '+str(current_subimage_area[1])+', '+str(current_subimage_area[2])+', '+str(current_subimage_area[3])+')')
            list_of_qr_code_image_data.append(current_qr_image_data)
    list_of_decoded_data_blobs = list()   
    combined_reconstructed_string = ''
    for current_qr_image in list_of_qr_code_image_data:
        rgb_version_of_current_qr_image = Image.new('RGB', current_qr_image.size, (0, 0, 0))
        rgb_version_of_current_qr_image.paste(current_qr_image, mask=current_qr_image.split()[3])
        rgb_version_of_current_qr_image_enhanced = enhance_pil_image_func(rgb_version_of_current_qr_image, enhancement_amount)
        rgb_version_of_current_qr_image_enhanced = invert_image_if_needed_func(rgb_version_of_current_qr_image_enhanced)
        #rgb_version_of_current_qr_image_enhanced = binarize_image_func(rgb_version_of_current_qr_image_enhanced)
        decoded_data = decode(rgb_version_of_current_qr_image_enhanced)
        if len(decoded_data) == 0:
            decoded_data = decode(invert_image_if_needed_func(rgb_version_of_current_qr_image_enhanced))
        if len(decoded_data) == 0:
            decoded_data = decode(enhance_pil_image_func(rgb_version_of_current_qr_image_enhanced, 9))
        if len(decoded_data) > 0:
            decoded_data_raw = decoded_data[0].data.decode('utf-8')
            list_of_decoded_data_blobs.append(decoded_data_raw)
            combined_reconstructed_string = combined_reconstructed_string + decoded_data_raw
    combined_reconstructed_string = combined_reconstructed_string.split('AAAAAA')[0]
    return combined_reconstructed_string, list_of_decoded_data_blobs

def decode_block_files_into_art_zipfile_func(sha256_hash_of_art_file):
    global block_storage_folder_path
    global reconstructed_files_destination_folder_path
    global prepared_final_art_zipfiles_folder_path
    start_time = time()
    reconstructed_file_destination_file_path = os.path.join(reconstructed_files_destination_folder_path,'Final_Art_Zipfile_Hash__' + sha256_hash_of_art_file + '.zip')
    list_of_block_file_paths = glob.glob(os.path.join(block_storage_folder_path,'*'+sha256_hash_of_art_file+'*.block'))
    reported_file_sha256_hash = list_of_block_file_paths[0].split(os.sep)[-1].split('__')[1]
    print('\nFound '+str(len(list_of_block_file_paths))+' block files in folder! The SHA256 hash of the original zip file is reported to be: '+reported_file_sha256_hash+'\n')
    c_constant = 0.1
    delta_constant = 0.5
    block_graph = BlockGraph(len(list_of_block_file_paths))
    for block_count, current_block_file_path in enumerate(list_of_block_file_paths):
        with open(current_block_file_path,'rb') as f:
            packed_block_data = f.read()
        hash_of_block = get_sha256_hash_of_input_data_func(packed_block_data)
        reported_hash_of_block = current_block_file_path.split('__')[-1].replace('.block','').replace('BlockHash_','')
        if hash_of_block == reported_hash_of_block:
            pass #Block hash matches reported hash, so block is not corrupted
        else:
            print('\nError, the block hash does NOT match the reported hash, so this block is corrupted! Skipping to next block...\n')
            continue
        input_stream = io.BufferedReader(io.BytesIO(packed_block_data)) #,buffer_size=1000000
        header = struct.unpack('!III', input_stream.read(12))
        filesize = header[0]
        blocksize = header[1]
        blockseed = header[2]
        number_of_blocks_required = ceil(filesize/blocksize)
        if block_count == 0:
            print('\nA total of '+str(number_of_blocks_required)+' blocks are required!\n')
        block = int.from_bytes(input_stream.read(blocksize),'big')
        input_stream.close
        if (block_count % 1) == 0:
            name_parts_list = current_block_file_path.split(os.sep)[-1].split('_')
            parsed_block_hash = name_parts_list[-1].replace('.block','')
            parsed_block_number = name_parts_list[6]
            parsed_file_hash = name_parts_list[2]
            print('\nNow decoding:\nBlock Number: ' + parsed_block_number + '\nFile Hash: ' + parsed_file_hash + '\nBlock Hash: '+ parsed_block_hash)
        prng = PRNG(params=(number_of_blocks_required, delta_constant, c_constant))
        _, _, src_blocks = prng.get_src_blocks(seed = blockseed)
        file_reconstruction_complete = block_graph.add_block(src_blocks, block)
        if file_reconstruction_complete:
            print('\nDone building file! Processed a total of '+str(block_count)+' blocks\n')
            break
        with open(reconstructed_file_destination_file_path,'wb') as f: 
            for ix, block_bytes in enumerate(map(lambda p: int.to_bytes(p[1], blocksize, 'big'), sorted(block_graph.eliminated.items(), key = lambda p:p[0]))):
                if ix < number_of_blocks_required - 1 or filesize % blocksize == 0:
                    f.write(block_bytes)
                else:
                    f.write(block_bytes[:filesize%blocksize])
    try:
        with open(reconstructed_file_destination_file_path,'rb') as f:
            reconstructed_file = f.read()
            reconstructed_file_hash = get_sha256_hash_of_input_data_func(reconstructed_file)
            if reported_file_sha256_hash == reconstructed_file_hash:
                completed_successfully = 1
                print('\nThe SHA256 hash of the reconstructed file matches the reported file hash-- file is valid! Now copying to prepared final art zipfiles folder...\n')
                shutil.copy(reconstructed_file_destination_file_path, prepared_final_art_zipfiles_folder_path)
                print('Done!')
            else:
                completed_successfully = 0
                print('\nProblem! The SHA256 hash of the reconstructed file does NOT match the expected hash! File is not valid.\n')
    except Exception as e:
        print('Error: '+ str(e))
    duration_in_seconds = round(time() - start_time, 1)
    print('\n\nFinished processing in '+str(duration_in_seconds) + ' seconds!')
    return completed_successfully

# encode_file_into_lt_block_qr_codes_func(path_to_input_file)

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

if use_demonstrate_sphincs_crypto:
    with MyTimer():
        print('Now generating SPHINCS public/secret keypair...')
        sphincs256 = SPHINCS()
        sk, pk = sphincs256.keygen()
        secret_key = sphincs256.pack(sk)
        public_key = sphincs256.pack(pk)
        with open('sphincs_secret_key.dat','wb') as f:
            f.write(secret_key)
        with open('sphincs_public_key.dat','wb') as f:
            f.write(public_key)    
        print('Done generating key!')
    
    with MyTimer():
        print('Now signing art metadata with key...')
        with open('sphincs_secret_key.dat','rb') as f:
            sphincs_secret_key = sphincs256.unpack(f.read())
        signature = sphincs256.sign(message, sphincs_secret_key)
        print('Done! Writing signature file now...')
        with open('sphincs_signature.dat','wb') as f:
            f.write(sphincs256.pack(signature))
    
    with MyTimer():
        print('Now Verifying SPHINCS Signature...')
        with open('sphincs_signature.dat','rb') as f:
            signature_data = f.read()
            sphincs_signature = sphincs256.unpack(signature_data)
        with open('sphincs_public_key.dat','rb') as f:
            private_key_data = sphincs256.unpack(f.read())
            sphincs_public_key = sphincs256.unpack(private_key_data)
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
    with open('sphincs_secret_key.dat','rb') as f:
        sphincs_secret_key_raw_bytes = f.read()
    input_data = sphincs_secret_key_raw_bytes
    existing_qr_code_filepaths = glob.glob('ANIME_QR_CODE__*.png')
    for current_filepath in existing_qr_code_filepaths:
        os.remove(current_filepath)
    list_of_encoded_data_blobs, image_pixel_width, input_data_base32_encoded = encode_data_as_qr_codes_func(input_data)
    existing_qr_code_filepaths = glob.glob('ANIME_QR_CODE__*.png')
    path_to_image_file_with_qr_codes = glob.glob('ANIME_QR_CODE_COMBINED__*.png')
    path_to_image_file_with_qr_codes = path_to_image_file_with_qr_codes[0]
    combined_reconstructed_string, list_of_decoded_data_blobs = decode_data_from_qr_code_images_func(path_to_image_file_with_qr_codes, image_pixel_width)
    image_file_with_qr_codes = Image.open(path_to_image_file_with_qr_codes)
    excess_padding = len(list_of_decoded_data_blobs) - len(list_of_encoded_data_blobs)
    if excess_padding > 0:
        list_of_decoded_data_blobs = list_of_decoded_data_blobs[:-excess_padding]
    combined_reconstructed_string_stripped = combined_reconstructed_string.rstrip('A')
    combined_reconstructed_string_stripped_padded = add_base32_padding_characters_func(combined_reconstructed_string_stripped)
    input_data_base32_decoded = base64.b32decode(combined_reconstructed_string_stripped_padded)
    print('Input length: '+ str(len(input_data_base32_encoded)) + '; Reconstructed Length: ' + str(len(combined_reconstructed_string_stripped_padded)))
    assert(input_data_base32_encoded == combined_reconstructed_string_stripped_padded)
    input_data_hash = input_data_base32_decoded[:64]
    reconstructed_data = input_data_base32_decoded[64:]
    reconstructed_data_hash = hashlib.sha3_256(reconstructed_data).hexdigest().encode('utf-8')
    assert(reconstructed_data_hash== input_data_hash)
    
if 0: 
    #path_to_input_file = 'pride_and_prejudice.txt'
    with open('sphincs_secret_key.dat','rb') as f:
        sphincs_secret_key_raw_bytes = f.read()
    input_data = sphincs_secret_key_raw_bytes
    image_pixel_width, combined_qr_code_png_output_file_name = encode_file_into_lt_block_qr_codes_func(sphincs_secret_key_raw_bytes)
    decode_data_lt_coded_qr_code_images_func(combined_qr_code_png_output_file_name, image_pixel_width)

if use_demonstrate_qr_steganography:
    path_to_image_file = 'C:\\animecoin\\art_folders_to_encode\\Carlo_Angelo__Felipe_Number_02\\Carlo_Angelo__Felipe_Number_02.png'
    path_to_image_file_with_qr_codes = glob.glob('ANIME_QR_CODE_COMBINED__*.png')
    path_to_image_file_with_qr_codes = path_to_image_file_with_qr_codes[0]
    art_image_data = Image.open(path_to_image_file)
    art_image_data_width, art_image_data_height = art_image_data.size
    art_image_data_total_pixels = art_image_data_width*art_image_data_height
    combined_qr_code_data = Image.open(path_to_image_file_with_qr_codes)
    combined_qr_code_data_total_pixels = combined_qr_code_data.size[0]*combined_qr_code_data.size[1]
    required_total_art_image_pixels = 2.0*combined_qr_code_data_total_pixels
    magnification_factor = required_total_art_image_pixels/art_image_data_total_pixels
    if magnification_factor > 1:
        print('Magnification Factor: '+str(round(magnification_factor,3)))
        art_image_data_width_new= ceil(art_image_data_width*magnification_factor)
        art_image_data_height_new = ceil(art_image_data_height*magnification_factor)
        art_image_data_resized = art_image_data.resize((art_image_data_width_new, art_image_data_height_new), Image.LANCZOS)
    else:
        art_image_data_resized = art_image_data
    merged_image_data = merge_two_images_func(art_image_data_resized, combined_qr_code_data)
    merged_image_data_png_output_file_name = 'ANIME_ART_Watermarked.png'
    merged_image_data.save(merged_image_data_png_output_file_name)
    #Now try to find the hidden image:
    embedded_image = unmerge_two_images_func(merged_image_data)
    embedded_image_data_png_output_file_name = 'ANIME_ART_SIGNATURE.png'
    embedded_image.save(embedded_image_data_png_output_file_name)
    path_to_reconstructed_image_file_with_qr_codes = 'ANIME_ART_SIGNATURE.png'
    combined_reconstructed_string_stego, list_of_decoded_data_blobs_stego = decode_data_from_qr_code_images_func(path_to_reconstructed_image_file_with_qr_codes, image_pixel_width)
    assert(list_of_encoded_data_blobs == list_of_decoded_data_blobs_stego)
    combined_reconstructed_string_stego_stripped = combined_reconstructed_string_stego.rstrip('A')
    combined_reconstructed_string_stego_stripped_padded = add_base32_padding_characters_func(combined_reconstructed_string_stego_stripped)
    input_data_base32_decoded_stego = base64.b32decode(combined_reconstructed_string_stego_stripped_padded)
    input_data_hash_stego = input_data_base32_decoded_stego[:64]
    reconstructed_data_stego = input_data_base32_decoded_stego[64:]
    reconstructed_data_hash_stego = hashlib.sha3_256(reconstructed_data_stego).hexdigest().encode('utf-8')
    assert(reconstructed_data_hash_stego == input_data_hash_stego)
