import itertools, os, struct, time, base64, hashlib, glob, random
from math import ceil, floor, log, log2, sqrt
from binascii import hexlify
from PIL import Image, ImageChops, ImageOps 
import piexif
import pyqrcode
from pyzbar.pyzbar import decode
from tqdm import tqdm

sigma = "expand 32-byte k"
tau = "expand 16-byte k"
#pip install pyqrcode, pypng, numpy

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
    assert (len(leafs) & len(leafs) - 1) == 0  # test for full binary tree
    return l_tree(H, leafs)  # binary hash trees are special cases of L-Trees

def l_tree(H, leafs):
    layer = leafs
    yield layer
    for ii in range(ceil(log2(len(leafs)))):
        next_layer = [H(l, r, ii) for l, r in zip(layer[0::2], layer[1::2])]
        if len(layer) & 1:  # if there is a node left on this layer
            next_layer.append(layer[-1])
        layer = next_layer
        yield layer

def auth_path(tree, idx):
    path = []
    for layer in tree:
        if len(layer) == 1:  # if there are no neighbors
            break
        idx += 1 if (idx & 1 == 0) else -1  # neighbor node
        path.append(layer[idx])
        idx >>= 1  # parent node
    return path

def construct_root(H, auth_path, leaf, idx):
    node = leaf
    for ii, neighbor in enumerate(auth_path):
        if idx & 1 == 0:
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
        assert rounds & 1 == 0
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
            return ((x << n) & 0xFFFFFFFF) | (x >> (32 - n))

        def quarterround(x, a, b, c, d):
            x[a] = (x[a] + x[b] & 0xFFFFFFFF); x[d] = ROL32(x[d] ^ x[a], 16)
            x[c] = (x[c] + x[d] & 0xFFFFFFFF); x[b] = ROL32(x[b] ^ x[c], 12)
            x[a] = (x[a] + x[b] & 0xFFFFFFFF); x[d] = ROL32(x[d] ^ x[a], 8)
            x[c] = (x[c] + x[d] & 0xFFFFFFFF); x[b] = ROL32(x[b] ^ x[c], 7)

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
                x[ii] = (x[ii] + a[ii] & 0xFFFFFFFF)
        return b''.join(ints_to_4bytes(x))

    def keystream(self, N=64):
        output = bytes()
        for n in range(N, 0, -64):
            output += self.permuted(self.state)[:min(n, 64)]
            self.state[12] += 1
            if self.state[12] & 0xFFFFFFFF == 0:
                self.state[13] += 1
        return output


class HORST(object):
    def __init__(self, n, m, k, tau, F, H, Gt):
        assert k*tau == m
        self.n = n
        self.m = m
        self.k = k
        self.tau = tau
        self.t = 1 << tau
        self.F = F
        self.H = H
        # minimising k(tau - x + 1) + 2^{x} implies maximising 'k*x - 2^{x}'
        self.x = max((k * x - (1 << x), x) for x in range(tau))[1]
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
            r = construct_root(H, path, leaf, Mi) # there is an error in the SPHINCS paper for this formula, as itstates that y_i = floor(M_i / 2^tau - x) rather than y_i = floor(M_i / 2^{tau - x})
            yi = Mi // (1 << (self.tau - self.x))
            if r != sigma_k[yi]:
                return False
        Qtop = masks[2*(self.tau - self.x):]
        H = lambda x, y, i: self.H(xor(x, Qtop[2*i]), xor(y, Qtop[2*i+1]))
        return root(hash_tree(H, sigma_k))
    
class WOTSplus(object):
    def __init__(self, n, w, F, Gl):
        """Initializes WOTS+
        n -- length of hashes (in bits)
        w -- Winternitz parameter; chain length and block size trade-off
        F -- function used to construct chains (n/8 bytes -> n/8 bytes)
        Gl -- PRG to generate the chain bases, based on seed and no. of bytes
        """
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
            v[12] = v[12] ^ (self.t & MASK)
            v[13] = v[13] ^ (self.t & MASK)
            v[14] = v[14] ^ (self.t >> self.WORDBITS)
            v[15] = v[15] ^ (self.t >> self.WORDBITS)
            
        def G(a, b, c, d, ii):
            va = v[a]   # it's faster to deref and reref later
            vb = v[b]
            vc = v[c]
            vd = v[d]
            sri  = SIGMA[round][ii]
            sri1 = SIGMA[round][ii+1]
            va = ((va + vb) + (m[sri] ^ cxx[sri1]) ) & MASK
            x  =  vd ^ va
            vd = (x >> rot1) | ((x << (WORDBITS-rot1)) & MASK)
            vc = (vc + vd) & MASK
            x  =  vb ^ vc
            vb = (x >> rot2) | ((x << (WORDBITS-rot2)) & MASK)
            va = ((va + vb) + (m[sri1] ^ cxx[sri]) ) & MASK
            x  =  vd ^ va
            vd = (x >> rot3) | ((x << (WORDBITS-rot3)) & MASK)
            vc = (vc + vd) & MASK
            x  =  vb ^ vc
            vb = (x >> rot4) | ((x << (WORDBITS-rot4)) & MASK)
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
        
        self.h = [self.h[ii]^v[ii]^v[ii+8]^self.salt[ii&0x3] for ii in range(8)] # save current hash value   (use i&0x3 to get 0,1,2,3,0,1,2,3)
    
    def addsalt(self, salt):
        if self.state != 1:
            raise Exception('addsalt() not called after init() and before update()')
        # salt size is to be 4x word size
        saltsize = self.WORDBYTES * 4
        # if too short, prefix with null bytes.  if too long, 
        # truncate high order bytes
        if len(salt) < saltsize:
            salt = (chr(0)*(saltsize-len(salt)) + salt)
        else:
            salt = salt[-saltsize:]
        # prep the salt array
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
        tt = self.t + (len(self.cache) << 3)
        if self.BLKBYTES == 64:
            msglen = self._int2eightByte(tt)
        else:
            low  = tt & self.MASK
            high = tt >> self.WORDBITS
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
                self.t -= (sizewithout - len(self.cache)) << 3
                self.update(PADDING[:sizewithout - len(self.cache)])
            else: 
                self.t -= (self.BLKBYTES - len(self.cache)) << 3 
                self.update(PADDING[:self.BLKBYTES - len(self.cache)])
                self.t -= (sizewithout+1) << 3
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
        self.hash  = b''.join(hashval)[:self.hashbitlen >> 3]
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
        self.t = 1 << tau
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
        t = level | (subtree << 4) | (leaf << 59)
        return int.to_bytes(t, length=8, byteorder='little')

    def wots_leaf(self, address, SK1, masks):
        seed = self.Fa(address, SK1)
        pk_A = self.wots.keygen(seed, masks)
        H = lambda x, y, i: self.H(xor(x, masks[2*i]), xor(y, masks[2*i+1]))
        return root(l_tree(H, pk_A))

    def wots_path(self, a, SK1, Q, subh):
        ta = dict(a)
        leafs = []
        for subleaf in range(1 << subh):
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
                     for ii in range(1 << (self.h//self.d))]
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
        ii >>= self.n - self.h
        subh = self.h // self.d
        a = {'level': self.d,
             'subtree': ii >> subh,
             'leaf': ii & ((1 << subh) - 1)}
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
            a['leaf'] = a['subtree'] & ((1 << subh) - 1)
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
            pk = construct_root(Ht, wots_path, leaf, ii & 0x1f)
            ii >>= subh
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
            sigma_k, sig = prefix(sig, (1 << self.horst.x) * n)
            sig_horst.append(self.unpack(byteseq=sigma_k))
            wots = []
            for _ in range(self.d):
                wots_sig, sig = prefix(sig, self.wots.l*n)
                path, sig = prefix(sig, self.h//self.d*n)
                wots.append(self.unpack(byteseq=wots_sig))
                wots.append(self.unpack(byteseq=path))
            return (ii, R1, sig_horst) + tuple(wots)

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
    rgb_tuple_merged = (r1[:4] + r2[:4],  g1[:4] + g2[:4],  b1[:4] + b2[:4],  a1[:4] + a2[:4])
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
            rgb2 = convert_integer_tuple_to_binary_string_tuple_func( (0, 0, 0, 128) ) # Use a black pixel as default
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
            rgb = ( r[4:] + '0000', g[4:] + '0000', b[4:] + '0000', a[4:] + '0000' ) # Extract the last 4 bits (corresponding to the hidden image);  Concatenate 4 zero bits because we are working with 8 bit values
            pixels_new[ii, jj] = convert_binary_string_tuple_to_integer_tuple_func(rgb) # Convert it to an integer tuple
            if pixels_new[ii, jj] != (0, 0, 0, 128): # If this is a 'valid' position, store it as the last valid position
                original_size = (ii + 1, jj + 1)
    embedded_image = embedded_image.crop((0, 0, original_size[0], original_size[1]))  # Crop the image based on the 'valid' pixels
    return embedded_image

def check_if_pil_image_is_all_black_func(pil_image):
    return not pil_image.getbbox() # Image.getbbox() returns the falsy None if there are no non-black pixels in the image, otherwise it returns a tuple of points, which is truthy

def check_if_pil_image_is_all_white_func(pil_image):
    return not ImageChops.invert(pil_image).getbbox()

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

def encode_data_as_qr_codes_func(input_data):
    if isinstance(input_data, str):
        input_data = input_data.encode('utf-8')
    input_data_hash = hashlib.sha3_256(input_data).hexdigest().encode('utf-8')
    input_data_combined = input_data_hash + input_data
    input_data_base32_encoded = base64.b32encode(input_data_combined).decode('utf-8')
    input_data_base32_encoded_character_length = len(input_data_base32_encoded)
    print('Total data length is ' + str(input_data_base32_encoded_character_length) + ' characters.')
    pyqrcode_version_number = 15 #Max is 40;
    qr_error_correcting_level = 'H' # L, M, Q, H
    qr_encoding_type = 'alphanumeric'
    qr_encoding_type_index = pyqrcode.tables.modes[qr_encoding_type]
    qrcode_capacity_dict = pyqrcode.tables.data_capacity[pyqrcode_version_number] # Using max error corrections; see: pyqrcode.tables.data_capacity[40]
    max_characters_in_single_qr_code = qrcode_capacity_dict[qr_error_correcting_level][qr_encoding_type_index]
    required_number_of_qr_codes = ceil(float(input_data_base32_encoded_character_length)/float(max_characters_in_single_qr_code))
    print('A total of '+str(required_number_of_qr_codes) +' QR codes is required.')
    list_of_light_hex_color_strings = ['#C9D6FF','#DBE6F6','#F0F2F0','#C4E0E5','#E0EAFC','#ffdde1','#FFEEEE','#E4E5E6','#DAE2F8','#D4D3DD','#EFEFBB']
    list_of_dark_hex_color_strings = ['#203A43','#2C5364','#493240','#373B44','#3c1053','#333333','#23074d','#302b63','#24243e','#0f0c29','#2F0743']
    list_of_encoded_data_blobs = list()
    for cnt in range(required_number_of_qr_codes):
        starting_index = cnt*max_characters_in_single_qr_code
        ending_index = min([len(input_data_base32_encoded), (cnt+1)*max_characters_in_single_qr_code])
        encoded_data_for_current_qr_code = input_data_base32_encoded[starting_index:ending_index]
        encoded_data_for_current_qr_code = encoded_data_for_current_qr_code.replace('=','A')
        list_of_encoded_data_blobs.append(encoded_data_for_current_qr_code)
        current_qr_code = pyqrcode.create(encoded_data_for_current_qr_code, error=qr_error_correcting_level, version=pyqrcode_version_number, mode=qr_encoding_type)
        qr_code_png_output_file_name = 'ANIME_QR_CODE__'+ input_data_hash.decode('utf-8') + '__' '{0:03}'.format(cnt) + '.png'
        print('Saving: '+qr_code_png_output_file_name)
        current_qr_code.png(qr_code_png_output_file_name, scale=5, module_color=random.choice(list_of_light_hex_color_strings), background=random.choice(list_of_dark_hex_color_strings))
    list_of_generated_qr_image_filepaths = glob.glob('ANIME_QR_CODE__' + input_data_hash.decode('utf-8') + '*.png')
    list_of_generated_qr_images = list()
    for current_qr_image_filepath in list_of_generated_qr_image_filepaths:
        current_qr_image_data = Image.open(current_qr_image_filepath)
        list_of_generated_qr_images.append(current_qr_image_data)
    image_pixel_width, image_pixel_height = current_qr_image_data.size
    print('Pixel width/height of QR codes generated: '+str(image_pixel_width)+', '+str(image_pixel_height))
    N = pow(ceil(sqrt(required_number_of_qr_codes)), 2) #Find nearest square
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
            if image_counter < len(list_of_generated_qr_images):
                current_image_data = list_of_generated_qr_images[image_counter]
                new_image_coords =  (ii*image_pixel_width, jj*image_pixel_height)
                print('New Image Upper-Left Corner: '+str(new_image_coords[0])+', '+str(new_image_coords[1]))
                combined_qr_code_image.paste(current_image_data,new_image_coords)
                image_counter = image_counter + 1
    combined_qr_code_png_output_file_name = 'ANIME_QR_CODE_COMBINED__' + input_data_hash.decode('utf-8') + '.png'
    combined_qr_code_image.save(combined_qr_code_png_output_file_name)
    return list_of_encoded_data_blobs, image_pixel_width, input_data_base32_encoded

def decode_data_from_qr_code_images_func(path_to_image_file_with_qr_codes, image_pixel_width):
    standard_qr_code_pixel_width = image_pixel_width
    standard_qr_code_pixel_height = standard_qr_code_pixel_width
    combined_qr_code_image = Image.open(path_to_image_file_with_qr_codes)
    combined_width, combined_height = combined_qr_code_image.size
    N = pow((combined_width//standard_qr_code_pixel_width), 2)
    number_in_each_row = int(sqrt(N))
    number_in_each_column = int(sqrt(N))
    list_of_qr_code_image_data = []
    iteration_counter = 0
    for ii in range(number_in_each_row):
        for jj in range(number_in_each_column):
            print('Row Counter: '+str(ii)+'; Column Counter: '+str(jj)+';')
            current_subimage_area = (ii*standard_qr_code_pixel_width, jj*standard_qr_code_pixel_height, (ii+1)*standard_qr_code_pixel_width, (jj+1)*standard_qr_code_pixel_height)
            current_qr_image_data = combined_qr_code_image.crop(current_subimage_area)
            iteration_counter = iteration_counter + 1
            if (not check_if_pil_image_is_all_black_func(current_qr_image_data)) and (not check_if_pil_image_is_all_white_func(current_qr_image_data)):
                print('Iteration Count: '+str(iteration_counter)+'; Current Sub-Image Area Tuple: ('+str(current_subimage_area[0])+', '+str(current_subimage_area[1])+', '+str(current_subimage_area[2])+', '+str(current_subimage_area[3])+')')
                list_of_qr_code_image_data.append(current_qr_image_data)
            else:
                print('Skipping blank grid square!')
    list_of_decoded_data_blobs = []    
    combined_reconstructed_string = ''
    for current_qr_image in list_of_qr_code_image_data:
        rgb_version_of_current_qr_image = Image.new('RGB', current_qr_image.size, (255, 255, 255))
        rgb_version_of_current_qr_image.paste(current_qr_image, mask=current_qr_image.split()[3])
        current_qr_image_inverted = ImageOps.invert(rgb_version_of_current_qr_image)
        current_qr_image_inverted = current_qr_image_inverted.convert('L').point(lambda p: p > 100 and 255)
        decoded_data = decode(current_qr_image_inverted)
        decoded_data_raw = decoded_data[0].data.decode('utf-8')
        list_of_decoded_data_blobs.append(decoded_data_raw)
        combined_reconstructed_string = combined_reconstructed_string + decoded_data_raw
    return combined_reconstructed_string, list_of_decoded_data_blobs
    
################################################################################
#  Demo:
################################################################################
path_to_animecoin_html_ticket_file = 'C:\\animecoin\\art_folders_to_encode\\Carlo_Angelo__Felipe_Number_02\\artwork_metadata_ticket__2018_05_27__23_42_33_964953.html'
with open(path_to_animecoin_html_ticket_file,'r') as f:
   animecoin_ticket_html_string = f.read()
   
message = animecoin_ticket_html_string.encode('utf-8')
use_demonstrate_sphincs_crypto = 0
use_demonstrate_qr_code_generation = 1
use_demonstrate_qr_steganography = 1
use_demonstrate_exif_data_storage = 0

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

if use_demonstrate_qr_code_generation:
    with open('sphincs_secret_key.dat','rb') as f:
        sphincs_secret_key_raw_bytes = f.read()
    input_data = sphincs_secret_key_raw_bytes
    list_of_encoded_data_blobs, image_pixel_width, input_data_base32_encoded = encode_data_as_qr_codes_func(input_data)
    path_to_image_file_with_qr_codes = 'ANIME_QR_CODE_COMBINED__b4200a5dc865c3c96748340aa6cf8143cd6e7328e84acc00e121dfa92fd6680b.png'
    combined_reconstructed_string, list_of_decoded_data_blobs = decode_data_from_qr_code_images_func(path_to_image_file_with_qr_codes, image_pixel_width)
    assert(list_of_encoded_data_blobs == list_of_decoded_data_blobs)
    combined_reconstructed_string_stripped = combined_reconstructed_string.rstrip('A')
    combined_reconstructed_string_stripped_padded = add_base32_padding_characters_func(combined_reconstructed_string_stripped)
    input_data_base32_decoded = base64.b32decode(combined_reconstructed_string_stripped_padded)
    input_data_hash = input_data_base32_decoded[:64]
    reconstructed_data = input_data_base32_decoded[64:]
    reconstructed_data_hash = hashlib.sha3_256(reconstructed_data).hexdigest().encode('utf-8')
    assert(reconstructed_data_hash== input_data_hash)

    
if use_demonstrate_qr_steganography:
    path_to_image_file = 'C:\\animecoin\\art_folders_to_encode\\Carlo_Angelo__Felipe_Number_02\\Carlo_Angelo__Felipe_Number_02.png'
    art_image_data = Image.open(path_to_image_file)
    art_image_data_width, art_image_data_height = art_image_data.size
    magnification_factor = 1.5
    art_image_data_width_new= round(art_image_data_width*magnification_factor)
    art_image_data_height_new = round(art_image_data_height*magnification_factor)
    art_image_data_resized = art_image_data.resize((art_image_data_width_new, art_image_data_height_new), Image.LANCZOS)
    combined_qr_code_data = Image.open(path_to_image_file_with_qr_codes)
    merged_image_data = merge_two_images_func(art_image_data_resized, combined_qr_code_data)
    merged_image_data_png_output_file_name = 'ANIME_ART_Watermarked.png'
    merged_image_data.save(merged_image_data_png_output_file_name)
    embedded_image = unmerge_two_images_func(merged_image_data)
    embedded_image_data_png_output_file_name = 'ANIME_ART_SIGNATURE.png'
    embedded_image.save(embedded_image_data_png_output_file_name)

if use_demonstrate_exif_data_storage:
    exif_dict = piexif.load(art_image_data_resized.info["exif"])
    # process im and exif_dict...
    w, h = art_image_data_resized.size
    exif_dict["0th"][piexif.ImageIFD.XResolution] = (w, 1)
    exif_dict["0th"][piexif.ImageIFD.YResolution] = (h, 1)
    exif_bytes = piexif.dump(exif_dict)
    im.save(new_file, "jpeg", exif=exif_bytes)