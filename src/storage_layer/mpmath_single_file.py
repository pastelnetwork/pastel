from __future__ import division
from colorsys import hsv_to_rgb, hls_to_rgb
from copy import copy
from random import Random
from bisect import bisect
from operator import gt, lt
import operator
import math
import cmath
import sys
import os
import types
import random
import tempfile
import pickle
import io
import traceback
import pylab
import time
import re
import urllib
import numpy
import pylab
import matplotlib
import builtins
import mpl_toolkits.mplot3d as mplot3d
from timeit import default_timer as clock

pi = 3.1415926535897932385
e = 2.7182818284590452354
sqrt2 = 1.4142135623730950488
sqrt5 = 2.2360679774997896964
phi = 1.6180339887498948482
ln2 = 0.69314718055994530942
ln10 = 2.302585092994045684
euler = 0.57721566490153286061
catalan = 0.91596559417721901505
khinchin = 2.6854520010653064453
apery = 1.2020569031595942854
logpi = 1.1447298858494001741
INF = 1e300*1e300
NINF = -INF
NAN = INF-INF
EPS = 2.2204460492503131e-16
string_types = str,
integer_types = int,
class_types = type,
text_type = str
binary_type = bytes
MAXSIZE = sys.maxsize
rowsep = '\n'
colsep = '  '
stddigits = '0123456789abcdefghijklmnopqrstuvwxyz'
powers = [1<<_ for _ in range(300)]

class CalculusMethods(object):
    pass

class NoConvergence(Exception):
    pass

class Eigen(object):
    pass

class Context(object):
    pass

class ODEMethods(object):
    pass

class ComplexResult(ValueError):
    pass


def _add_doc(func, doc):
    func.__doc__ = doc

def _import_module(name):
    __import__(name)
    return sys.modules[name]

int2byte = operator.methodcaller("to_bytes", 1, "big")
StringIO = io.StringIO
BytesIO = io.BytesIO

def b(s):
    return s.encode("latin-1")

def u(s):
    return s

def reraise(tp, value, tb=None):
    if value.__traceback__ is not tb:
        raise value.with_traceback(tb)
    raise value

_add_doc(b, """Byte literal""")
_add_doc(u, """Text literal""")
exec_ = getattr(builtins, "exec")


class _LazyDescr(object):
    def __init__(self, name):
        self.name = name
    def __get__(self, obj, tp):
        result = self._resolve()
        setattr(obj, self.name, result)
        delattr(tp, self.name)
        return result

class MovedModule(_LazyDescr):
    def __init__(self, name, old, new=None):
        super(MovedModule, self).__init__(name)
        if new is None:
            new = name
        self.mod = new
    def _resolve(self):
        return _import_module(self.mod)

class MovedAttribute(_LazyDescr):
    def __init__(self, name, old_mod, new_mod, old_attr=None, new_attr=None):
        super(MovedAttribute, self).__init__(name)
        if new_mod is None:
            new_mod = name
        self.mod = new_mod
        if new_attr is None:
            if old_attr is None:
                new_attr = name
            else:
                new_attr = old_attr
        self.attr = new_attr
    def _resolve(self):
        module = _import_module(self.mod)
        return getattr(module, self.attr)

_meth_func = "__func__"
_meth_self = "__self__"

_func_code = "__code__"
_func_defaults = "__defaults__"

_iterkeys = "keys"
_itervalues = "values"
_iteritems = "items"

try:
    advance_iterator = next
except NameError:
    def advance_iterator(it):
        return it.next()
next = advance_iterator

def get_unbound_function(unbound):
    return unbound

Iterator = object

def callable(obj):
    return any("__call__" in klass.__dict__ for klass in type(obj).__mro__)

_add_doc(get_unbound_function, """Get the function out of a possibly unbound function""")

get_method_function = operator.attrgetter(_meth_func)
get_method_self = operator.attrgetter(_meth_self)
get_function_code = operator.attrgetter(_func_code)
get_function_defaults = operator.attrgetter(_func_defaults)


def iterkeys(d):
    return iter(getattr(d, _iterkeys)())

def itervalues(d):
    return iter(getattr(d, _itervalues)())

def iteritems(d):
    return iter(getattr(d, _iteritems)())

print_ = getattr(builtins, "print")
del builtins

def python_bitcount(n):
    bc = bisect(powers, n)
    if bc != 300:
        return bc
    bc = int(math.log(n, 2)) - 4
    return bc + bctable[n>>bc]

def gmpy_bitcount(n):
    if n: return MPZ(n).numdigits(2)
    else: return 0

def sage_trailing(n):
    return MPZ(n).trailing_zero_bits()

def mpmathify(ctx, *args, **kwargs):
    return ctx.convert(*args, **kwargs)

def pslq(ctx, x, tol=None, maxcoeff=1000, maxsteps=100, verbose=False):
    n = len(x)
    if n < 2:
        raise ValueError("n cannot be less than 2")
    prec = ctx.prec
    if prec < 53:
        raise ValueError("prec cannot be less than 53")
    if verbose and prec // max(2,n) < 5:
        print("Warning: precision for PSLQ may be too low")
    target = int(prec*0.75)
    if tol is None:
        tol = ctx.mpf(2)**(-target)
    else:
        tol = ctx.convert(tol)
    extra = 60
    prec += extra
    if verbose:
        print("PSLQ using prec %i and tol %s" % (prec, ctx.nstr(tol)))
    tol = ctx.to_fixed(tol, prec)
    assert tol
    x = [None] + [ctx.to_fixed(ctx.mpf(xk), prec) for xk in x]
    minx = min(abs(xx) for xx in x[1:])
    if not minx:
        raise ValueError("PSLQ requires a vector of nonzero numbers")
    if minx < tol//100:
        if verbose:
            print("STOPPING: (one number is too small)")
        return None
    g = sqrt_fixed((4<<prec)//3, prec)
    A = {}
    B = {}
    H = {}
    for i in range(1, n+1):
        for j in range(1, n+1):
            A[i,j] = B[i,j] = (i==j) << prec
            H[i,j] = 0
    s = [None] + [0]*n
    for k in range(1, n+1):
        t = 0
        for j in range(k, n+1):
            t += (x[j]**2 >> prec)
        s[k] = sqrt_fixed(t, prec)
    t = s[1]
    y = x[:]
    for k in range(1, n+1):
        y[k] = (x[k] << prec) // t
        s[k] = (s[k] << prec) // t
    for i in range(1, n+1):
        for j in range(i+1, n):
            H[i,j] = 0
        if i <= n-1:
            if s[i]:
                H[i,i] = (s[i+1] << prec) // s[i]
            else:
                H[i,i] = 0
        for j in range(1, i):
            sjj1 = s[j]*s[j+1]
            if sjj1:
                H[i,j] = ((-y[i]*y[j])<<prec)//sjj1
            else:
                H[i,j] = 0
    for i in range(2, n+1):
        for j in range(i-1, 0, -1):
            if H[j,j]:
                t = round_fixed((H[i,j] << prec)//H[j,j], prec)
            else:
                continue
            y[j] = y[j] + (t*y[i] >> prec)
            for k in range(1, j+1):
                H[i,k] = H[i,k] - (t*H[j,k] >> prec)
            for k in range(1, n+1):
                A[i,k] = A[i,k] - (t*A[j,k] >> prec)
                B[k,j] = B[k,j] + (t*B[k,i] >> prec)
    for REP in range(maxsteps):
        m = -1
        szmax = -1
        for i in range(1, n):
            h = H[i,i]
            sz = (g**i*abs(h)) >> (prec*(i-1))
            if sz > szmax:
                m = i
                szmax = sz
        y[m], y[m+1] = y[m+1], y[m]
        tmp = {}
        for i in range(1,n+1): H[m,i], H[m+1,i] = H[m+1,i], H[m,i]
        for i in range(1,n+1): A[m,i], A[m+1,i] = A[m+1,i], A[m,i]
        for i in range(1,n+1): B[i,m], B[i,m+1] = B[i,m+1], B[i,m]
        if m <= n - 2:
            t0 = sqrt_fixed((H[m,m]**2 + H[m,m+1]**2)>>prec, prec)
            if not t0:
                break
            t1 = (H[m,m] << prec) // t0
            t2 = (H[m,m+1] << prec) // t0
            for i in range(m, n+1):
                t3 = H[i,m]
                t4 = H[i,m+1]
                H[i,m] = (t1*t3+t2*t4) >> prec
                H[i,m+1] = (-t2*t3+t1*t4) >> prec
        for i in range(m+1, n+1):
            for j in range(min(i-1, m+1), 0, -1):
                try:
                    t = round_fixed((H[i,j] << prec)//H[j,j], prec)
                except ZeroDivisionError:
                    break
                y[j] = y[j] + ((t*y[i]) >> prec)
                for k in range(1, j+1):
                    H[i,k] = H[i,k] - (t*H[j,k] >> prec)
                for k in range(1, n+1):
                    A[i,k] = A[i,k] - (t*A[j,k] >> prec)
                    B[k,j] = B[k,j] + (t*B[k,i] >> prec)
        best_err = maxcoeff<<prec
        for i in range(1, n+1):
            err = abs(y[i])
            if err < tol:
                vec = [int(round_fixed(B[j,i], prec) >> prec) for j in \
                range(1,n+1)]
                if max(abs(v) for v in vec) < maxcoeff:
                    if verbose:
                        print("FOUND relation at iter %i/%i, error: %s" % \
                            (REP, maxsteps, ctx.nstr(err / ctx.mpf(2)**prec, 1)))
                    return vec
            best_err = min(err, best_err)
        recnorm = max(abs(h) for h in H.values())
        if recnorm:
            norm = ((1 << (2*prec)) // recnorm) >> prec
            norm //= 100
        else:
            norm = ctx.inf
        if verbose:
            print("%i/%i:  Error: %8s   Norm: %s" % \
                (REP, maxsteps, ctx.nstr(best_err / ctx.mpf(2)**prec, 1), norm))
        if norm >= maxcoeff:
            break
    if verbose:
        print("CANCELLING after step %i/%i." % (REP, maxsteps))
        print("Could not find an integer relation. Norm bound: %s" % norm)
    return None

def fracgcd(p, q):
    x, y = p, q
    while y:
        x, y = y, x % y
    if x != 1:
        p //= x
        q //= x
    if q == 1:
        return p
    return p, q

def ifac(n, memo={0:1, 1:1}):
    f = memo.get(n)
    if f:
        return f
    k = len(memo)
    p = memo[k-1]
    MAX = MAX_FACTORIAL_CACHE
    while k <= n:
        p *= k
        if k <= MAX:
            memo[k] = p
        k += 1
    return p

def ifac2(n, memo_pair=[{0:1}, {1:1}]):
    memo = memo_pair[n&1]
    f = memo.get(n)
    if f:
        return f
    k = max(memo)
    p = memo[k]
    MAX = MAX_FACTORIAL_CACHE
    while k < n:
        k += 2
        p *= k
        if k <= MAX:
            memo[k] = p
    return p

def gcd(*args):
    a = 0
    for b in args:
        if a:
            while b:
                a, b = b, a % b
        else:
            a = b
    return a

def list_primes(n):
    n = n + 1
    sieve = list(range(n))
    sieve[:2] = [0, 0]
    for i in range(2, int(n**0.5)+1):
        if sieve[i]:
            for j in range(i**2, n, i):
                sieve[j] = 0
    return [p for p in sieve if p]

def cos_sin_fixed(x, prec, pi2=None):
    if pi2 is None:
        pi2 = pi_fixed(prec-1)
    n, t = divmod(x, pi2)
    n = int(n)
    c, s = cos_sin_basecase(t, prec)
    m = n & 3
    if m == 0: return c, s
    if m == 1: return -s, c
    if m == 2: return -c, -s
    if m == 3: return s, -c

def exp_fixed(x, prec, ln2=None):
    if ln2 is None:
        ln2 = ln2_fixed(prec)
    n, t = divmod(x, ln2)
    n = int(n)
    v = exp_basecase(t, prec)
    if n >= 0:
        return v << n
    else:
        return v >> (-n)


def pslqstring(r, constants):
    q = r[0]
    r = r[1:]
    s = []
    for i in range(len(r)):
        p = r[i]
        if p:
            z = fracgcd(-p,q)
            cs = constants[i][1]
            if cs == '1':
                cs = ''
            else:
                cs = '*' + cs
            if isinstance(z, int_types):
                if z > 0: term = str(z) + cs
                else:     term = ("(%s)" % z) + cs
            else:
                term = ("(%s/%s)" % z) + cs
            s.append(term)
    s = ' + '.join(s)
    if '+' in s or '*' in s:
        s = '(' + s + ')'
    return s or '0'

def prodstring(r, constants):
    q = r[0]
    r = r[1:]
    num = []
    den = []
    for i in range(len(r)):
        p = r[i]
        if p:
            z = fracgcd(-p,q)
            cs = constants[i][1]
            if isinstance(z, int_types):
                if abs(z) == 1: t = cs
                else:           t = '%s**%s' % (cs, abs(z))
                ([num,den][z<0]).append(t)
            else:
                t = '%s**(%s/%s)' % (cs, abs(z[0]), z[1])
                ([num,den][z[0]<0]).append(t)
    num = '*'.join(num)
    den = '*'.join(den)
    if num and den: return "(%s)/(%s)" % (num, den)
    if num: return num
    if den: return "1/(%s)" % den

class IdentificationMethods(object):
    pass

IdentificationMethods.pslq = pslq

def round_fixed(x, prec):
    return ((x + (1<<(prec-1))) >> prec) << prec

def findpoly(ctx, x, n=1, **kwargs):
    x = ctx.mpf(x)
    if n < 1:
        raise ValueError("n cannot be less than 1")
    if x == 0:
        return [1, 0]
    xs = [ctx.mpf(1)]
    for i in range(1,n+1):
        xs.append(x**i)
        a = ctx.pslq(xs, **kwargs)
        if a is not None:
            return a[::-1]

def quadraticstring(ctx,t,a,b,c):
    if c < 0:
        a,b,c = -a,-b,-c
    u1 = (-b+ctx.sqrt(b**2-4*a*c))/(2*c)
    u2 = (-b-ctx.sqrt(b**2-4*a*c))/(2*c)
    if abs(u1-t) < abs(u2-t):
        if b:  s = '((%s+sqrt(%s))/%s)' % (-b,b**2-4*a*c,2*c)
        else:  s = '(sqrt(%s)/%s)' % (-4*a*c,2*c)
    else:
        if b:  s = '((%s-sqrt(%s))/%s)' % (-b,b**2-4*a*c,2*c)
        else:  s = '(-sqrt(%s)/%s)' % (-4*a*c,2*c)
    return s

IdentificationMethods.findpoly = findpoly

def identify(ctx, x, constants=[], tol=None, maxcoeff=1000, full=False, verbose=False):
    solutions = []
    def addsolution(s):
        if verbose: print("Found: ", s)
        solutions.append(s)
    x = ctx.mpf(x)
    if x == 0:
        if full: return ['0']
        else:    return '0'
    if x < 0:
        sol = ctx.identify(-x, constants, tol, maxcoeff, full, verbose)
        if sol is None:
            return sol
        if full:
            return ["-(%s)"%s for s in sol]
        else:
            return "-(%s)" % sol
    if tol:
        tol = ctx.mpf(tol)
    else:
        tol = mp.eps**0.7
    M = maxcoeff
    if constants:
        if isinstance(constants, dict):
            constants = [(ctx.mpf(v), name) for (name, v) in sorted(constants.items())]
        else:
            namespace = dict((name, getattr(ctx,name)) for name in dir(ctx))
            constants = [(eval(p, namespace), p) for p in constants]
    else:
        constants = []
    if 1 not in [value for (name, value) in constants]:
        constants = [(ctx.mpf(1), '1')] + constants
    for ft, ftn, red in transforms:
        for c, cn in constants:
            if red and cn == '1':
                continue
            t = ft(ctx,x,c)
            if abs(t) > M**2 or abs(t) < tol:
                continue
            r = ctx.pslq([t] + [a[0] for a in constants], tol, M)
            s = None
            if r is not None and max(abs(uw) for uw in r) <= M and r[0]:
                s = pslqstring(r, constants)
            else:
                q = ctx.pslq([ctx.one, t, t**2], tol, M)
                if q is not None and len(q) == 3 and q[2]:
                    aa, bb, cc = q
                    if max(abs(aa),abs(bb),abs(cc)) <= M:
                        s = quadraticstring(ctx,t,aa,bb,cc)
            if s:
                if cn == '1' and ('/$c' in ftn):
                    s = ftn.replace('$y', s).replace('/$c', '')
                else:
                    s = ftn.replace('$y', s).replace('$c', cn)
                addsolution(s)
                if not full: return solutions[0]
            if verbose:
                print(".")
    if x != 1:
        ilogs = [2,3,5,7]
        logs = []
        for a, s in constants:
            if not sum(bool(ctx.findpoly(ctx.ln(a)/ctx.ln(i),1)) for i in ilogs):
                logs.append((ctx.ln(a), s))
        logs = [(ctx.ln(i),str(i)) for i in ilogs] + logs
        r = ctx.pslq([ctx.ln(x)] + [a[0] for a in logs], tol, M)
        if r is not None and max(abs(uw) for uw in r) <= M and r[0]:
            addsolution(prodstring(r, logs))
            if not full: return solutions[0]
    if full:
        return sorted(solutions, key=len)
    else:
        return None

IdentificationMethods.identify = identify
gmpy = None
sage = None
sage_utils = None
python3 = True
BACKEND = 'python'
MPZ = int
range = range
basestring = str
HASH_MODULUS = sys.hash_info.modulus
if sys.hash_info.width == 32:
    HASH_BITS = 31
else:
    HASH_BITS = 61

# if 'MPMATH_NOGMPY' not in os.environ:
#     try:
#         try:
#             import gmpy2 as gmpy
#         except ImportError:
#             try:
#                 import gmpy
#             except ImportError:
#                 raise ImportError
#         if gmpy.version() >= '1.03':
#             BACKEND = 'gmpy'
#             MPZ = gmpy.mpz
#     except:
#         pass

# if 'MPMATH_NOSAGE' not in os.environ:
#     try:
#         import sage.all
#         import sage.libs.mpmath.utils as _sage_utils
#         sage = sage.all
#         sage_utils = _sage_utils
#         BACKEND = 'sage'
#         MPZ = sage.Integer
#     except:
#         pass

if 'MPMATH_STRICT' in os.environ:
    STRICT = True
else:
    STRICT = False

MPZ_TYPE = type(MPZ(0))
MPZ_ZERO = MPZ(0)
MPZ_ONE = MPZ(1)
MPZ_TWO = MPZ(2)
MPZ_THREE = MPZ(3)
MPZ_FIVE = MPZ(5)
fzero = (0, MPZ_ZERO, 0, 0)
fnzero = (1, MPZ_ZERO, 0, 0)
fone = (0, MPZ_ONE, 0, 1)
fnone = (1, MPZ_ONE, 0, 1)
ftwo = (0, MPZ_ONE, 1, 1)
ften = (0, MPZ_FIVE, 1, 3)
fhalf = (0, MPZ_ONE, -1, 1)
fnan = (0, MPZ_ZERO, -123, -1)
finf = (0, MPZ_ZERO, -456, -2)
fninf = (1, MPZ_ZERO, -789, -3)
math_float_inf = 1e300*1e300

def python_trailing(n):
    if not n:
        return 0
    t = 0
    while not n & 1:
        n >>= 1
        t += 1
    return t

def mpi_add(s, t, prec=0):
    sa, sb = s
    ta, tb = t
    a = mpf_add(sa, ta, prec, round_floor)
    b = mpf_add(sb, tb, prec, round_ceiling)
    if a == fnan: a = fninf
    if b == fnan: b = finf
    return a, b

def mpi_sub(s, t, prec=0):
    sa, sb = s
    ta, tb = t
    a = mpf_sub(sa, tb, prec, round_floor)
    b = mpf_sub(sb, ta, prec, round_ceiling)
    if a == fnan: a = fninf
    if b == fnan: b = finf
    return a, b

def mpi_delta(s, prec):
    sa, sb = s
    return mpf_sub(sb, sa, prec, round_up)

def mpi_mid(s, prec):
    sa, sb = s
    return mpf_shift(mpf_add(sa, sb, prec, round_nearest), -1)

def mpi_pos(s, prec):
    sa, sb = s
    a = mpf_pos(sa, prec, round_floor)
    b = mpf_pos(sb, prec, round_ceiling)
    return a, b

def mpi_neg(s, prec=0):
    sa, sb = s
    a = mpf_neg(sb, prec, round_floor)
    b = mpf_neg(sa, prec, round_ceiling)
    return a, b

def mpi_abs(s, prec=0):
    sa, sb = s
    sas = mpf_sign(sa)
    sbs = mpf_sign(sb)
    if sas >= 0:
        a = mpf_pos(sa, prec, round_floor)
        b = mpf_pos(sb, prec, round_ceiling)
    elif sbs >= 0:
        a = fzero
        negsa = mpf_neg(sa)
        if mpf_lt(negsa, sb):
            b = mpf_pos(sb, prec, round_ceiling)
        else:
            b = mpf_pos(negsa, prec, round_ceiling)
    else:
        a = mpf_neg(sb, prec, round_floor)
        b = mpf_neg(sa, prec, round_ceiling)
    return a, b

def mpi_mul_mpf(s, t, prec):
    return mpi_mul(s, (t, t), prec)

def mpi_div_mpf(s, t, prec):
    return mpi_div(s, (t, t), prec)

def mpi_mul(s, t, prec=0):
    sa, sb = s
    ta, tb = t
    sas = mpf_sign(sa)
    sbs = mpf_sign(sb)
    tas = mpf_sign(ta)
    tbs = mpf_sign(tb)
    if sas == sbs == 0:
        if ta == fninf or tb == finf:
            return fninf, finf
        return fzero, fzero
    if tas == tbs == 0:
        if sa == fninf or sb == finf:
            return fninf, finf
        return fzero, fzero
    if sas >= 0:
        if tas >= 0:
            a = mpf_mul(sa, ta, prec, round_floor)
            b = mpf_mul(sb, tb, prec, round_ceiling)
            if a == fnan: a = fzero
            if b == fnan: b = finf
        elif tbs <= 0:
            a = mpf_mul(sb, ta, prec, round_floor)
            b = mpf_mul(sa, tb, prec, round_ceiling)
            if a == fnan: a = fninf
            if b == fnan: b = fzero
        else:
            a = mpf_mul(sb, ta, prec, round_floor)
            b = mpf_mul(sb, tb, prec, round_ceiling)
            if a == fnan: a = fninf
            if b == fnan: b = finf
    elif sbs <= 0:
        if tas >= 0:
            a = mpf_mul(sa, tb, prec, round_floor)
            b = mpf_mul(sb, ta, prec, round_ceiling)
            if a == fnan: a = fninf
            if b == fnan: b = fzero
        elif tbs <= 0:
            a = mpf_mul(sb, tb, prec, round_floor)
            b = mpf_mul(sa, ta, prec, round_ceiling)
            if a == fnan: a = fzero
            if b == fnan: b = finf
        else:
            a = mpf_mul(sa, tb, prec, round_floor)
            b = mpf_mul(sa, ta, prec, round_ceiling)
            if a == fnan: a = fninf
            if b == fnan: b = finf
    else:
        cases = [mpf_mul(sa, ta), mpf_mul(sa, tb), mpf_mul(sb, ta), mpf_mul(sb, tb)]
        if fnan in cases:
            a, b = (fninf, finf)
        else:
            a, b = mpf_min_max(cases)
            a = mpf_pos(a, prec, round_floor)
            b = mpf_pos(b, prec, round_ceiling)
    return a, b

def mpi_square(s, prec=0):
    sa, sb = s
    if mpf_ge(sa, fzero):
        a = mpf_mul(sa, sa, prec, round_floor)
        b = mpf_mul(sb, sb, prec, round_ceiling)
    elif mpf_le(sb, fzero):
        a = mpf_mul(sb, sb, prec, round_floor)
        b = mpf_mul(sa, sa, prec, round_ceiling)
    else:
        sa = mpf_neg(sa)
        sa, sb = mpf_min_max([sa, sb])
        a = fzero
        b = mpf_mul(sb, sb, prec, round_ceiling)
    return a, b

def mpi_div(s, t, prec):
    sa, sb = s
    ta, tb = t
    sas = mpf_sign(sa)
    sbs = mpf_sign(sb)
    tas = mpf_sign(ta)
    tbs = mpf_sign(tb)
    if sas == sbs == 0:
        if (tas < 0 and tbs > 0) or (tas == 0 or tbs == 0):
            return fninf, finf
        return fzero, fzero
    if tas < 0 and tbs > 0:
        return fninf, finf
    if tas < 0:
        return mpi_div(mpi_neg(s), mpi_neg(t), prec)
    if tas == 0:
        if sas < 0 and sbs > 0:
            return fninf, finf
        if tas == tbs:
            return fninf, finf
        if sas >= 0:
            a = mpf_div(sa, tb, prec, round_floor)
            b = finf
        if sbs <= 0:
            a = fninf
            b = mpf_div(sb, tb, prec, round_ceiling)
    else:
        if sas >= 0:
            a = mpf_div(sa, tb, prec, round_floor)
            b = mpf_div(sb, ta, prec, round_ceiling)
            if a == fnan: a = fzero
            if b == fnan: b = finf
        elif sbs <= 0:
            a = mpf_div(sa, ta, prec, round_floor)
            b = mpf_div(sb, tb, prec, round_ceiling)
            if a == fnan: a = fninf
            if b == fnan: b = fzero
        else:
            a = mpf_div(sa, ta, prec, round_floor)
            b = mpf_div(sb, ta, prec, round_ceiling)
            if a == fnan: a = fninf
            if b == fnan: b = finf
    return a, b

def mpi_pi(prec):
    a = mpf_pi(prec, round_floor)
    b = mpf_pi(prec, round_ceiling)
    return a, b

def mpi_exp(s, prec):
    sa, sb = s
    a = mpf_exp(sa, prec, round_floor)
    b = mpf_exp(sb, prec, round_ceiling)
    return a, b

def mpi_log(s, prec):
    sa, sb = s
    a = mpf_log(sa, prec, round_floor)
    b = mpf_log(sb, prec, round_ceiling)
    return a, b

def mpi_sqrt(s, prec):
    sa, sb = s
    a = mpf_sqrt(sa, prec, round_floor)
    b = mpf_sqrt(sb, prec, round_ceiling)
    return a, b

def mpi_atan(s, prec):
    sa, sb = s
    a = mpf_atan(sa, prec, round_floor)
    b = mpf_atan(sb, prec, round_ceiling)
    return a, b

def mpi_pow_int(s, n, prec):
    sa, sb = s
    if n < 0:
        return mpi_div((fone, fone), mpi_pow_int(s, -n, prec+20), prec)
    if n == 0:
        return (fone, fone)
    if n == 1:
        return s
    if n == 2:
        return mpi_square(s, prec)
    if n & 1:
        a = mpf_pow_int(sa, n, prec, round_floor)
        b = mpf_pow_int(sb, n, prec, round_ceiling)
    else:
        sas = mpf_sign(sa)
        sbs = mpf_sign(sb)
        if sas >= 0:
            a = mpf_pow_int(sa, n, prec, round_floor)
            b = mpf_pow_int(sb, n, prec, round_ceiling)
        elif sbs <= 0:
            a = mpf_pow_int(sb, n, prec, round_floor)
            b = mpf_pow_int(sa, n, prec, round_ceiling)
        else:
            a = fzero
            sa = mpf_neg(sa)
            if mpf_ge(sa, sb):
                b = mpf_pow_int(sa, n, prec, round_ceiling)
            else:
                b = mpf_pow_int(sb, n, prec, round_ceiling)
    return a, b

def mpi_pow(s, t, prec):
    ta, tb = t
    if ta == tb and ta not in (finf, fninf):
        if ta == from_int(to_int(ta)):
            return mpi_pow_int(s, to_int(ta), prec)
        if ta == fhalf:
            return mpi_sqrt(s, prec)
    u = mpi_log(s, prec + 20)
    v = mpi_mul(u, t, prec + 20)
    return mpi_exp(v, prec)

def MIN(x, y):
    if mpf_le(x, y):
        return x
    return y

def MAX(x, y):
    if mpf_ge(x, y):
        return x
    return y

def cos_sin_quadrant(x, wp):
    sign, man, exp, bc = x
    if x == fzero:
        return fone, fzero, 0
    c, s = mpf_cos_sin(x, wp)
    t, n, wp_ = mod_pi2(man, exp, exp+bc, 15)
    if sign:
        n = -1-n
    return c, s, n

def mpi_cos_sin(x, prec):
    a, b = x
    if a == b == fzero:
        return (fone, fone), (fzero, fzero)
    if (finf in x) or (fninf in x):
        return (fnone, fone), (fnone, fone)
    wp = prec + 20
    ca, sa, na = cos_sin_quadrant(a, wp)
    cb, sb, nb = cos_sin_quadrant(b, wp)
    ca, cb = mpf_min_max([ca, cb])
    sa, sb = mpf_min_max([sa, sb])
    if na == nb:
        pass
    elif nb - na >= 4:
        return (fnone, fone), (fnone, fone)
    else:
        if na//4 != nb//4:
            cb = fone
        if (na-2)//4 != (nb-2)//4:
            ca = fnone
        if (na-1)//4 != (nb-1)//4:
            sb = fone
        if (na-3)//4 != (nb-3)//4:
            sa = fnone
    more = from_man_exp((MPZ_ONE<<wp) + (MPZ_ONE<<10), -wp)
    less = from_man_exp((MPZ_ONE<<wp) - (MPZ_ONE<<10), -wp)
    def finalize(v, rounding):
        if bool(v[0]) == (rounding == round_floor):
            p = more
        else:
            p = less
        v = mpf_mul(v, p, prec, rounding)
        sign, man, exp, bc = v
        if exp+bc >= 1:
            if sign:
                return fnone
            return fone
        return v
    ca = finalize(ca, round_floor)
    cb = finalize(cb, round_ceiling)
    sa = finalize(sa, round_floor)
    sb = finalize(sb, round_ceiling)
    return (ca,cb), (sa,sb)

def mpi_cos(x, prec):
    return mpi_cos_sin(x, prec)[0]

def mpi_sin(x, prec):
    return mpi_cos_sin(x, prec)[1]

def mpi_tan(x, prec):
    cos, sin = mpi_cos_sin(x, prec+20)
    return mpi_div(sin, cos, prec)

def mpi_cot(x, prec):
    cos, sin = mpi_cos_sin(x, prec+20)
    return mpi_div(cos, sin, prec)

def mpi_from_str_a_b(x, y, percent, prec):
    wp = prec + 20
    xa = from_str(x, wp, round_floor)
    xb = from_str(x, wp, round_ceiling)
    y = from_str(y, wp, round_ceiling)
    assert mpf_ge(y, fzero)
    if percent:
        y = mpf_mul(MAX(mpf_abs(xa), mpf_abs(xb)), y, wp, round_ceiling)
        y = mpf_div(y, from_int(100), wp, round_ceiling)
    a = mpf_sub(xa, y, prec, round_floor)
    b = mpf_add(xb, y, prec, round_ceiling)
    return a, b

def mpi_from_str(s, prec):
    e = ValueError("Improperly formed interval number '%s'" % s)
    s = s.replace(" ", "")
    wp = prec + 20
    if "+-" in s:
        x, y = s.split("+-")
        return mpi_from_str_a_b(x, y, False, prec)
    elif "(" in s:
        if s[0] == "(" or ")" not in s:
            raise e
        s = s.replace(")", "")
        percent = False
        if "%" in s:
            if s[-1] != "%":
                raise e
            percent = True
            s = s.replace("%", "")
        x, y = s.split("(")
        return mpi_from_str_a_b(x, y, percent, prec)
    elif "," in s:
        if ('[' not in s) or (']' not in s):
            raise e
        if s[0] == '[':
            # case 3
            s = s.replace("[", "")
            s = s.replace("]", "")
            a, b = s.split(",")
            a = from_str(a, prec, round_floor)
            b = from_str(b, prec, round_ceiling)
            return a, b
        else:
            # case 4
            x, y = s.split('[')
            y, z = y.split(',')
            if 'e' in s:
                z, e = z.split(']')
            else:
                z, e = z.rstrip(']'), ''
            a = from_str(x+y+e, prec, round_floor)
            b = from_str(x+z+e, prec, round_ceiling)
            return a, b
    else:
        a = from_str(s, prec, round_floor)
        b = from_str(s, prec, round_ceiling)
        return a, b

def mpi_to_str(x, dps, use_spaces=True, brackets='[]', mode='brackets', error_dps=4, **kwargs):
    prec = dps_to_prec(dps)
    wp = prec + 20
    a, b = x
    mid = mpi_mid(x, prec)
    delta = mpi_delta(x, prec)
    a_str = to_str(a, dps, **kwargs)
    b_str = to_str(b, dps, **kwargs)
    mid_str = to_str(mid, dps, **kwargs)
    sp = ""
    if use_spaces:
        sp = " "
    br1, br2 = brackets
    if mode == 'plusminus':
        delta_str = to_str(mpf_shift(delta,-1), dps, **kwargs)
        s = mid_str + sp + "+-" + sp + delta_str
    elif mode == 'percent':
        if mid == fzero:
            p = fzero
        else:
            p = mpf_mul(delta, from_int(100))
            p = mpf_div(p, mpf_mul(mid, from_int(2)), wp)
        s = mid_str + sp + "(" + to_str(p, error_dps) + "%)"
    elif mode == 'brackets':
        s = br1 + a_str + "," + sp + b_str + br2
    elif mode == 'diff':
        if a_str == b_str:
            a_str = to_str(a, dps+3, **kwargs)
            b_str = to_str(b, dps+3, **kwargs)
        a = a_str.split('e')
        if len(a) == 1:
            a.append('')
        b = b_str.split('e')
        if len(b) == 1:
            b.append('')
        if a[1] == b[1]:
            if a[0] != b[0]:
                for i in range(len(a[0]) + 1):
                    if a[0][i] != b[0][i]:
                        break
                s = (a[0][:i] + br1 + a[0][i:] + ',' + sp + b[0][i:] + br2
                     + 'e'*min(len(a[1]), 1) + a[1])
            else:
                s = a[0] + br1 + br2 + 'e'*min(len(a[1]), 1) + a[1]
        else:
            s = br1 + 'e'.join(a) + ',' + sp + 'e'.join(b) + br2
    else:
        raise ValueError("'%s' is unknown mode for printing mpi" % mode)
    return s

def mpci_add(x, y, prec):
    a, b = x
    c, d = y
    return mpi_add(a, c, prec), mpi_add(b, d, prec)

def mpci_sub(x, y, prec):
    a, b = x
    c, d = y
    return mpi_sub(a, c, prec), mpi_sub(b, d, prec)

def mpci_neg(x, prec=0):
    a, b = x
    return mpi_neg(a, prec), mpi_neg(b, prec)

def mpci_pos(x, prec):
    a, b = x
    return mpi_pos(a, prec), mpi_pos(b, prec)

def mpci_mul(x, y, prec):
    a, b = x
    c, d = y
    r1 = mpi_mul(a,c)
    r2 = mpi_mul(b,d)
    re = mpi_sub(r1,r2,prec)
    i1 = mpi_mul(a,d)
    i2 = mpi_mul(b,c)
    im = mpi_add(i1,i2,prec)
    return re, im

def mpci_div(x, y, prec):
    a, b = x
    c, d = y
    wp = prec+20
    m1 = mpi_square(c)
    m2 = mpi_square(d)
    m = mpi_add(m1,m2,wp)
    re = mpi_add(mpi_mul(a,c), mpi_mul(b,d), wp)
    im = mpi_sub(mpi_mul(b,c), mpi_mul(a,d), wp)
    re = mpi_div(re, m, prec)
    im = mpi_div(im, m, prec)
    return re, im

def mpci_exp(x, prec):
    a, b = x
    wp = prec+20
    r = mpi_exp(a, wp)
    c, s = mpi_cos_sin(b, wp)
    a = mpi_mul(r, c, prec)
    b = mpi_mul(r, s, prec)
    return a, b

def mpi_shift(x, n):
    a, b = x
    return mpf_shift(a,n), mpf_shift(b,n)

def mpi_cosh_sinh(x, prec):
    wp = prec+20
    e1 = mpi_exp(x, wp)
    e2 = mpi_div(mpi_one, e1, wp)
    c = mpi_add(e1, e2, prec)
    s = mpi_sub(e1, e2, prec)
    c = mpi_shift(c, -1)
    s = mpi_shift(s, -1)
    return c, s

def mpci_cos(x, prec):
    a, b = x
    wp = prec+10
    c, s = mpi_cos_sin(a, wp)
    ch, sh = mpi_cosh_sinh(b, wp)
    re = mpi_mul(c, ch, prec)
    im = mpi_mul(s, sh, prec)
    return re, mpi_neg(im)

def mpci_sin(x, prec):
    a, b = x
    wp = prec+10
    c, s = mpi_cos_sin(a, wp)
    ch, sh = mpi_cosh_sinh(b, wp)
    re = mpi_mul(s, ch, prec)
    im = mpi_mul(c, sh, prec)
    return re, im

def mpci_abs(x, prec):
    a, b = x
    if a == mpi_zero:
        return mpi_abs(b)
    if b == mpi_zero:
        return mpi_abs(a)
    a = mpi_square(a)
    b = mpi_square(b)
    t = mpi_add(a, b, prec+20)
    return mpi_sqrt(t, prec)

def mpi_atan2(y, x, prec):
    ya, yb = y
    xa, xb = x
    if ya == yb == fzero:
        if mpf_ge(xa, fzero):
            return mpi_zero
        return mpi_pi(prec)
    if mpf_ge(xa, fzero):
        if mpf_ge(ya, fzero):
            a = mpf_atan2(ya, xb, prec, round_floor)
        else:
            a = mpf_atan2(ya, xa, prec, round_floor)
        if mpf_ge(yb, fzero):
            b = mpf_atan2(yb, xa, prec, round_ceiling)
        else:
            b = mpf_atan2(yb, xb, prec, round_ceiling)
    elif mpf_ge(ya, fzero):
        b = mpf_atan2(ya, xa, prec, round_ceiling)
        if mpf_le(xb, fzero):
            a = mpf_atan2(yb, xb, prec, round_floor)
        else:
            a = mpf_atan2(ya, xb, prec, round_floor)
    elif mpf_le(yb, fzero):
        a = mpf_atan2(yb, xa, prec, round_floor)
        if mpf_le(xb, fzero):
            b = mpf_atan2(ya, xb, prec, round_ceiling)
        else:
            b = mpf_atan2(yb, xb, prec, round_ceiling)
    else:
        b = mpf_pi(prec, round_ceiling)
        a = mpf_neg(b)
    return a, b

def mpci_arg(z, prec):
    x, y = z
    return mpi_atan2(y, x, prec)

def mpci_log(z, prec):
    x, y = z
    re = mpi_log(mpci_abs(z, prec+20), prec)
    im = mpci_arg(z, prec)
    return re, im

def mpci_pow(x, y, prec):
    yre, yim = y
    if yim == mpi_zero:
        ya, yb = yre
        if ya == yb:
            sign, man, exp, bc = yb
            if man and exp >= 0:
                return mpci_pow_int(x, (-1)**sign*int(man<<exp), prec)
            if yb == fzero:
                return mpci_pow_int(x, 0, prec)
    wp = prec+20
    return mpci_exp(mpci_mul(y, mpci_log(x, wp), wp), prec)

def mpci_square(x, prec):
    a, b = x
    re = mpi_sub(mpi_square(a), mpi_square(b), prec)
    im = mpi_mul(a, b, prec)
    im = mpi_shift(im, 1)
    return re, im

def mpci_pow_int(x, n, prec):
    if n < 0:
        return mpci_div((mpi_one,mpi_zero), mpci_pow_int(x, -n, prec+20), prec)
    if n == 0:
        return mpi_one, mpi_zero
    if n == 1:
        return mpci_pos(x, prec)
    if n == 2:
        return mpci_square(x, prec)
    wp = prec + 20
    result = (mpi_one, mpi_zero)
    while n:
        if n & 1:
            result = mpci_mul(result, x, wp)
            n -= 1
        x = mpci_square(x, wp)
        n >>= 1
    return mpci_pos(result, prec)

bitcount = python_bitcount
trailing = python_trailing
int_types = (int, MPZ_TYPE)
EXP_COSH_CUTOFF = 600
COS_SIN_CACHE_PREC = 400
MAX_FACTORIAL_CACHE = 1000
EXP_SERIES_U_CUTOFF = 1500
small_odd_primes = (3,5,7,11,13,17,19,23,29,31,37,41,43,47)
small_odd_primes_set = set(small_odd_primes)

# if BACKEND == 'gmpy':
#     if gmpy.version() >= '2':
#         def gmpy_trailing(n):
#             """Count the number of trailing zero bits in abs(n) using gmpy."""
#             if n: return MPZ(n).bit_scan1()
#             else: return 0
# if BACKEND == 'gmpy':
#     bitcount = gmpy_bitcount
#     trailing = gmpy_trailing
# elif BACKEND == 'sage':
#     sage_bitcount = sage_utils.bitcount
#     bitcount = sage_bitcount
#     trailing = sage_trailing
# else:
#     bitcount = python_bitcount
#     trailing = python_trailing

# if BACKEND == 'gmpy' and 'bit_length' in dir(gmpy):
#     bitcount = gmpy.bit_length
# try:
#     if BACKEND == 'python':
#         int_types = (int, )
#     else:
#         int_types = (int, MPZ_TYPE)
# except NameError:
#     if BACKEND == 'python':
#         int_types = (int,)
#     else:
#         int_types = (int, MPZ_TYPE)
# if BACKEND == 'sage':
#     import operator
#     rshift = operator.rshift
#     lshift = operator.lshift
# if BACKEND == 'python':
#     EXP_COSH_CUTOFF = 600
# else:
#     EXP_COSH_CUTOFF = 400
# EXP_SERIES_U_CUTOFF = 1500
# if BACKEND == 'python':
#     COS_SIN_CACHE_PREC = 400
# else:
#     COS_SIN_CACHE_PREC = 200
# MAX_FACTORIAL_CACHE = 1000
# if BACKEND == 'gmpy':
#     ifac = gmpy.fac
# elif BACKEND == 'sage':
#     ifac = lambda n: int(sage.factorial(n))
#     ifib = sage.fibonacci
# if BACKEND == 'sage':
#     def list_primes(n):
#         return [int(_) for _ in sage.primes(n+1)]
# small_odd_primes = (3,5,7,11,13,17,19,23,29,31,37,41,43,47)
# small_odd_primes_set = set(small_odd_primes)

# if BACKEND == 'sage':
#     try:
#         import sage.libs.mpmath.ext_libmp as _lbmp
#         mpf_sqrt = _lbmp.mpf_sqrt
#         mpf_exp = _lbmp.mpf_exp
#         mpf_log = _lbmp.mpf_log
#         mpf_cos = _lbmp.mpf_cos
#         mpf_sin = _lbmp.mpf_sin
#         mpf_pow = _lbmp.mpf_pow
#         exp_fixed = _lbmp.exp_fixed
#         cos_sin_fixed = _lbmp.cos_sin_fixed
#         log_int_fixed = _lbmp.log_int_fixed
#     except (ImportError, AttributeError):
#         print("Warning: Sage imports in libelefun failed")

#     def convert(ctx, x):
#         try:
#             return float(x)
#         except:
#             return complex(x)
#     power = staticmethod(pow)
#     sqrt = staticmethod(sqrt)
#     exp = staticmethod(exp)
#     ln = log = staticmethod(log)
#     cos = staticmethod(cos)
#     sin = staticmethod(sin)
#     tan = staticmethod(tan)
#     cos_sin = staticmethod(cos_sin)
#     acos = staticmethod(acos)
#     asin = staticmethod(asin)
#     atan = staticmethod(atan)
#     cosh = staticmethod(cosh)
#     sinh = staticmethod(sinh)
#     tanh = staticmethod(tanh)
#     gamma = staticmethod(gamma)
#     rgamma = staticmethod(rgamma)
#     fac = factorial = staticmethod(factorial)
#     floor = staticmethod(floor)
#     ceil = staticmethod(ceil)
#     cospi = staticmethod(cospi)
#     sinpi = staticmethod(sinpi)
#     cbrt = staticmethod(cbrt)
#     _nthroot = staticmethod(nthroot)
#     _ei = staticmethod(ei)
#     _e1 = staticmethod(e1)
#     _zeta = _zeta_int = staticmethod(zeta)

#     def arg(ctx, z):
#         z = complex(z)
#         return math.atan2(z.imag, z.real)

#     def expj(ctx, x):
#         return ctx.exp(ctx.j*x)

#     def expjpi(ctx, x):
#         return ctx.exp(ctx.j*mp.pi*x)

#     ldexp = math.ldexp
#     frexp = math.frexp

#     def mag(ctx, z):
#         if z:
#             return ctx.frexp(abs(z))[1]
#         return ctx.ninf

#     def isint(ctx, z):
#         if hasattr(z, "imag"):
#             if z.imag:
#                 return False
#             z = z.real
#         try:
#             return z == int(z)
#         except:
#             return False

#     def nint_distance(ctx, z):
#         if hasattr(z, "imag"):
#             n = round(z.real)
#         else:
#             n = round(z)
#         if n == z:
#             return n, ctx.ninf
#         return n, ctx.mag(abs(z-n))

#     def _convert_param(ctx, z):
#         if type(z) is tuple:
#             p, q = z
#             return ctx.mpf(p) / q, 'R'
#         if hasattr(z, "imag"):
#             intz = int(z.real)
#         else:
#             intz = int(z)
#         if z == intz:
#             return intz, 'Z'
#         return z, 'R'

#     def _is_real_type(ctx, z):
#         return isinstance(z, float) or isinstance(z, int_types)

#     def _is_complex_type(ctx, z):
#         return isinstance(z, complex)

#     def hypsum(ctx, p, q, types, coeffs, z, maxterms=6000, **kwargs):
#         coeffs = list(coeffs)
#         num = range(p)
#         den = range(p,p+q)
#         tol = mp.eps
#         s = t = 1.0
#         k = 0
#         while 1:
#             for i in num: t *= (coeffs[i]+k)
#             for i in den: t /= (coeffs[i]+k)
#             k += 1; t /= k; t *= z; s += t
#             if abs(t) < tol:
#                 return s
#             if k > maxterms:
#                 raise ctx.NoConvergence

#     def atan2(ctx, x, y):
#         return math.atan2(x, y)

#     def psi(ctx, m, z):
#         m = int(m)
#         if m == 0:
#             return ctx.digamma(z)
#         return (-1)**(m+1)*ctx.fac(m)*ctx.zeta(m+1, z)

#     def harmonic(ctx, x):
#         x = ctx.convert(x)
#         if x == 0 or x == 1:
#             return x
#         return ctx.digamma(x+1) + ctx.euler

#     nstr = str

#     def to_fixed(ctx, x, prec):
#         return int(math.ldexp(x, prec))

#     def rand(ctx):
#         return random.random()


#     def sum_accurately(ctx, terms, check_step=1):
#         s = ctx.zero
#         k = 0
#         for term in terms():
#             s += term
#             if (not k % check_step) and term:
#                 if abs(term) <= 1e-18*abs(s):
#                     break
#             k += 1
#         return s

try:
    intern
except NameError:
    intern = lambda x: x

round_nearest = intern('n')
round_floor = intern('f')
round_ceiling = intern('c')
round_up = intern('u')
round_down = intern('d')
round_fast = round_down

def round_int(x, n, rnd):
    if rnd == round_nearest:
        if x >= 0:
            t = x >> (n-1)
            if t & 1 and ((t & 2) or (x & h_mask[n<300][n])):
                return (t>>1)+1
            else:
                return t>>1
        else:
            return -round_int(-x, n, rnd)
    if rnd == round_floor:
        return x >> n
    if rnd == round_ceiling:
        return -((-x) >> n)
    if rnd == round_down:
        if x >= 0:
            return x >> n
        return -((-x) >> n)
    if rnd == round_up:
        if x >= 0:
            return -((-x) >> n)
        return x >> n

class h_mask_big:
    def __getitem__(self, n):
        return (MPZ_ONE<<(n-1))-1

h_mask_small = [0]+[((MPZ_ONE<<(_-1))-1) for _ in range(1, 300)]
h_mask = [h_mask_big(), h_mask_small]
shifts_down = {round_floor:(1,0), round_ceiling:(0,1), round_down:(1,1), round_up:(0,0)}
trailtable = [trailing(n) for n in range(256)]
bctable = [bitcount(n) for n in range(1024)]

def _normalize(sign, man, exp, bc, prec, rnd):
    if not man:
        return fzero
    n = bc - prec
    if n > 0:
        if rnd == round_nearest:
            t = man >> (n-1)
            if t & 1 and ((t & 2) or (man & h_mask[n<300][n])):
                man = (t>>1)+1
            else:
                man = t>>1
        elif shifts_down[rnd][sign]:
            man >>= n
        else:
            man = -((-man)>>n)
        exp += n
        bc = prec
    if not man & 1:
        t = trailtable[int(man & 255)]
        if not t:
            while not man & 255:
                man >>= 8
                exp += 8
                bc -= 8
            t = trailtable[int(man & 255)]
        man >>= t
        exp += t
        bc -= t
    if man == 1:
        bc = 1
    return sign, man, exp, bc

def _normalize1(sign, man, exp, bc, prec, rnd):
    if not man:
        return fzero
    if bc <= prec:
        return sign, man, exp, bc
    n = bc - prec
    if rnd == round_nearest:
        t = man >> (n-1)
        if t & 1 and ((t & 2) or (man & h_mask[n<300][n])):
            man = (t>>1)+1
        else:
            man = t>>1
    elif shifts_down[rnd][sign]:
        man >>= n
    else:
        man = -((-man)>>n)
    exp += n
    bc = prec
    if not man & 1:
        t = trailtable[int(man & 255)]
        if not t:
            while not man & 255:
                man >>= 8
                exp += 8
                bc -= 8
            t = trailtable[int(man & 255)]
        man >>= t
        exp += t
        bc -= t
    if man == 1:
        bc = 1
    return sign, man, exp, bc

try:
    _exp_types = (int, long)
except NameError:
    _exp_types = (int,)

def strict_normalize(sign, man, exp, bc, prec, rnd):
    assert type(man) == MPZ_TYPE
    assert type(bc) in _exp_types
    assert type(exp) in _exp_types
    assert bc == bitcount(man)
    return _normalize(sign, man, exp, bc, prec, rnd)

def strict_normalize1(sign, man, exp, bc, prec, rnd):
    assert type(man) == MPZ_TYPE
    assert type(bc) in _exp_types
    assert type(exp) in _exp_types
    assert bc == bitcount(man)
    assert (not man) or (man & 1)
    return _normalize1(sign, man, exp, bc, prec, rnd)

# if BACKEND == 'gmpy' and '_mpmath_normalize' in dir(gmpy):
#     _normalize = gmpy._mpmath_normalize
#     _normalize1 = gmpy._mpmath_normalize

# if BACKEND == 'sage':
#     _normalize = _normalize1 = sage_utils.normalize

if STRICT:
    normalize = strict_normalize
    normalize1 = strict_normalize1
else:
    normalize = _normalize
    normalize1 = _normalize1

def from_man_exp(man, exp, prec=None, rnd=round_fast):
    man = MPZ(man)
    sign = 0
    if man < 0:
        sign = 1
        man = -man
    if man < 1024:
        bc = bctable[int(man)]
    else:
        bc = bitcount(man)
    if not prec:
        if not man:
            return fzero
        if not man & 1:
            if man & 2:
                return (sign, man >> 1, exp + 1, bc - 1)
            t = trailtable[int(man & 255)]
            if not t:
                while not man & 255:
                    man >>= 8
                    exp += 8
                    bc -= 8
                t = trailtable[int(man & 255)]
            man >>= t
            exp += t
            bc -= t
        return (sign, man, exp, bc)
    return normalize(sign, man, exp, bc, prec, rnd)

def from_float(x, prec=53, rnd=round_fast):
    if x != x:
        return fnan
    try:
        m, e = math.frexp(x)
    except:
        if x == math_float_inf: return finf
        if x == -math_float_inf: return fninf
        return fnan
    if x == math_float_inf: return finf
    if x == -math_float_inf: return fninf
    return from_man_exp(int(m*(1<<53)), e-53, prec, rnd)

def to_float(s, strict=False, rnd=round_fast):
    sign, man, exp, bc = s
    if not man:
        if s == fzero: return 0.0
        if s == finf: return math_float_inf
        if s == fninf: return -math_float_inf
        return math_float_inf/math_float_inf
    if bc > 53:
        sign, man, exp, bc = normalize1(sign, man, exp, bc, 53, rnd)
    if sign:
        man = -man
    try:
        return math.ldexp(man, exp)
    except OverflowError:
        if strict:
            raise
        if exp + bc > 0:
            if sign:
                return -math_float_inf
            else:
                return math_float_inf
        return 0.0

def from_rational(p, q, prec, rnd=round_fast):
    return mpf_div(from_int(p), from_int(q), prec, rnd)

def to_rational(s):
    sign, man, exp, bc = s
    if sign:
        man = -man
    if bc == -1:
        raise ValueError("cannot convert %s to a rational number" % man)
    if exp >= 0:
        return man*(1<<exp), 1
    else:
        return man, 1<<(-exp)

def to_fixed(s, prec):
    sign, man, exp, bc = s
    offset = exp + prec
    if sign:
        if offset >= 0: return (-man) << offset
        else:           return (-man) >> (-offset)
    else:
        if offset >= 0: return man << offset
        else:           return man >> (-offset)

gamma_min_a = from_float(1.46163214496)
gamma_min_b = from_float(1.46163214497)
gamma_min = (gamma_min_a, gamma_min_b)
gamma_mono_imag_a = from_float(-1.1)
gamma_mono_imag_b = from_float(1.1)

def mpi_overlap(x, y):
    a, b = x
    c, d = y
    if mpf_lt(d, a): return False
    if mpf_gt(c, b): return False
    return True

def mpi_gamma(z, prec, type=0):
    a, b = z
    wp = prec+20

    if type == 1:
        return mpi_gamma(mpi_add(z, mpi_one, wp), prec, 0)

    if mpf_gt(a, gamma_min_b):
        if type == 0:
            c = mpf_gamma(a, prec, round_floor)
            d = mpf_gamma(b, prec, round_ceiling)
        elif type == 2:
            c = mpf_rgamma(b, prec, round_floor)
            d = mpf_rgamma(a, prec, round_ceiling)
        elif type == 3:
            c = mpf_loggamma(a, prec, round_floor)
            d = mpf_loggamma(b, prec, round_ceiling)

    elif mpf_gt(a, fzero) and mpf_lt(b, gamma_min_a):
        if type == 0:
            c = mpf_gamma(b, prec, round_floor)
            d = mpf_gamma(a, prec, round_ceiling)
        elif type == 2:
            c = mpf_rgamma(a, prec, round_floor)
            d = mpf_rgamma(b, prec, round_ceiling)
        elif type == 3:
            c = mpf_loggamma(b, prec, round_floor)
            d = mpf_loggamma(a, prec, round_ceiling)
    else:
        znew = mpi_add(z, mpi_one, wp)
        if type == 0: return mpi_div(mpi_gamma(znew, prec+2, 0), z, prec)
        if type == 2: return mpi_mul(mpi_gamma(znew, prec+2, 2), z, prec)
        if type == 3: return mpi_sub(mpi_gamma(znew, prec+2, 3), mpi_log(z, prec+2), prec)
    return c, d

def mpci_gamma(z, prec, type=0):
    (a1,a2), (b1,b2) = z

    if b1 == b2 == fzero and (type != 3 or mpf_gt(a1,fzero)):
        return mpi_gamma(z, prec, type), mpi_zero

    wp = prec+20
    if type != 3:
        amag = a2[2]+a2[3]
        bmag = b2[2]+b2[3]
        if a2 != fzero:
            mag = max(amag, bmag)
        else:
            mag = bmag
        an = abs(to_int(a2))
        bn = abs(to_int(b2))
        absn = max(an, bn)
        gamma_size = max(0,absn*mag)
        wp += bitcount(gamma_size)

    if type == 1:
        (a1,a2) = mpi_add((a1,a2), mpi_one, wp); z = (a1,a2), (b1,b2)
        type = 0

    if mpf_lt(a1, gamma_min_b):
        if mpi_overlap((b1,b2), (gamma_mono_imag_a, gamma_mono_imag_b)):
            znew = mpi_add((a1,a2), mpi_one, wp), (b1,b2)
            if type == 0: return mpci_div(mpci_gamma(znew, prec+2, 0), z, prec)
            if type == 2: return mpci_mul(mpci_gamma(znew, prec+2, 2), z, prec)
            if type == 3: return mpci_sub(mpci_gamma(znew, prec+2, 3), mpci_log(z,prec+2), prec)
    if mpf_ge(b1, fzero):
        minre = mpc_loggamma((a1,b2), wp, round_floor)
        maxre = mpc_loggamma((a2,b1), wp, round_ceiling)
        minim = mpc_loggamma((a1,b1), wp, round_floor)
        maxim = mpc_loggamma((a2,b2), wp, round_ceiling)
    # lower half-plane
    elif mpf_le(b2, fzero):
        minre = mpc_loggamma((a1,b1), wp, round_floor)
        maxre = mpc_loggamma((a2,b2), wp, round_ceiling)
        minim = mpc_loggamma((a2,b1), wp, round_floor)
        maxim = mpc_loggamma((a1,b2), wp, round_ceiling)
    # crosses real axis
    else:
        maxre = mpc_loggamma((a2,fzero), wp, round_ceiling)
        # stretches more into the lower half-plane
        if mpf_gt(mpf_neg(b1), b2):
            minre = mpc_loggamma((a1,b1), wp, round_ceiling)
        else:
            minre = mpc_loggamma((a1,b2), wp, round_ceiling)
        minim = mpc_loggamma((a2,b1), wp, round_floor)
        maxim = mpc_loggamma((a2,b2), wp, round_floor)

    w = (minre[0], maxre[0]), (minim[1], maxim[1])
    if type == 3:
        return mpi_pos(w[0], prec), mpi_pos(w[1], prec)
    if type == 2:
        w = mpci_neg(w)
    return mpci_exp(w, prec)

def mpi_loggamma(z, prec): return mpi_gamma(z, prec, type=3)
def mpci_loggamma(z, prec): return mpci_gamma(z, prec, type=3)

def mpi_rgamma(z, prec): return mpi_gamma(z, prec, type=2)
def mpci_rgamma(z, prec): return mpci_gamma(z, prec, type=2)

def mpi_factorial(z, prec): return mpi_gamma(z, prec, type=1)
def mpci_factorial(z, prec): return mpci_gamma(z, prec, type=1)


def isprime(n):
    n = int(n)
    if not n & 1:
        return n == 2
    if n < 50:
        return n in small_odd_primes_set
    for p in small_odd_primes:
        if not n % p:
            return False
    m = n-1
    s = trailing(m)
    d = m >> s
    def test(a):
        x = pow(a,d,n)
        if x == 1 or x == m:
            return True
        for r in range(1,s):
            x = x**2 % n
            if x == m:
                return True
        return False
    if n < 1373653:
        witnesses = [2,3]
    elif n < 341550071728321:
        witnesses = [2,3,5,7,11,13,17]
    else:
        witnesses = small_odd_primes
    for a in witnesses:
        if not test(a):
            return False
    return True

def moebius(n):
    n = abs(int(n))
    if n < 2:
        return n
    factors = []
    for p in range(2, n+1):
        if not (n % p):
            if not (n % p**2):
                return 0
            if not sum(p % f for f in factors):
                factors.append(p)
    return (-1)**len(factors)

MAX_EULER_CACHE = 500

def eulernum(m, _cache={0:MPZ_ONE}):
    if m & 1:
        return MPZ_ZERO
    f = _cache.get(m)
    if f:
        return f
    MAX = MAX_EULER_CACHE
    n = m
    a = [MPZ(_) for _ in [0,0,1,0,0,0]]
    for  n in range(1, m+1):
        for j in range(n+1, -1, -2):
            a[j+1] = (j-1)*a[j] + (j+1)*a[j+2]
        a.append(0)
        suma = 0
        for k in range(n+1, -1, -2):
            suma += a[k+1]
            if n <= MAX:
                _cache[n] = ((-1)**(n//2))*(suma // 2**n)
        if n == m:
            return ((-1)**(n//2))*suma // 2**n

def stirling1(n, k):
    if n < 0 or k < 0:
        raise ValueError
    if k >= n:
        return MPZ(n == k)
    if k < 1:
        return MPZ_ZERO
    L = [MPZ_ZERO]*(k+1)
    L[1] = MPZ_ONE
    for m in range(2, n+1):
        for j in range(min(k, m), 0, -1):
            L[j] = (m-1)*L[j] + L[j-1]
    return (-1)**(n+k)*L[k]

def stirling2(n, k):
    if n < 0 or k < 0:
        raise ValueError
    if k >= n:
        return MPZ(n == k)
    if k <= 1:
        return MPZ(k == 1)
    s = MPZ_ZERO
    t = MPZ_ONE
    for j in range(k+1):
        if (k + j) & 1:
            s -= t*MPZ(j)**n
        else:
            s += t*MPZ(j)**n
        t = t*(k - j) // (j + 1)
    return s // ifac(k)

mpc_one = fone, fzero
mpc_zero = fzero, fzero
mpc_two = ftwo, fzero
mpc_half = (fhalf, fzero)

_infs = (finf, fninf)
_infs_nan = (finf, fninf, fnan)

def mpf_bernoulli_huge(n, prec, rnd=None):
    wp = prec + 10
    piprec = wp + int(math.log(n,2))
    v = mpf_gamma_int(n+1, wp)
    v = mpf_mul(v, mpf_zeta_int(n, wp), wp)
    v = mpf_mul(v, mpf_pow_int(mpf_pi(piprec), -n, wp))
    v = mpf_shift(v, 1-n)
    if not n & 3:
        v = mpf_neg(v)
    return mpf_pos(v, prec, rnd or round_fast)

def bernfrac(n):
    n = int(n)
    if n < 3:
        return [(1, 1), (-1, 2), (1, 6)][n]
    if n & 1:
        return (0, 1)
    q = 1
    for k in list_primes(n+1):
        if not (n % (k-1)):
            q *= k
    prec = bernoulli_size(n) + int(math.log(q,2)) + 20
    b = mpf_bernoulli(n, prec)
    p = mpf_mul(b, from_int(q))
    pint = to_int(p, round_nearest)
    return (pint, q)

class PrecisionManager:
    def __init__(self, ctx, precfun, dpsfun, normalize_output=False):
        self.ctx = ctx
        self.precfun = precfun
        self.dpsfun = dpsfun
        self.normalize_output = normalize_output
    def __call__(self, f):
        def g(*args, **kwargs):
            orig = self.ctx.prec
            try:
                if self.precfun:
                    self.ctx.prec = self.precfun(self.ctx.prec)
                else:
                    self.ctx.dps = self.dpsfun(self.ctx.dps)
                if self.normalize_output:
                    v = f(*args, **kwargs)
                    if type(v) is tuple:
                        return tuple([+a for a in v])
                    return +v
                else:
                    return f(*args, **kwargs)
            finally:
                self.ctx.prec = orig
        g.__name__ = f.__name__
        g.__doc__ = f.__doc__
        return g
    def __enter__(self):
        self.origp = self.ctx.prec
        if self.precfun:
            self.ctx.prec = self.precfun(self.ctx.prec)
        else:
            self.ctx.dps = self.dpsfun(self.ctx.dps)
    def __exit__(self, exc_type, exc_val, exc_tb):
        self.ctx.prec = self.origp
        return False

class OptimizationMethods(object):
    def __init__(ctx):
        pass

class RSCache(object):
    def __init__(ctx):
        ctx._rs_cache = [0, 10, {}, {}]

def zetazero(ctx, n, info=False, round=True):
    n = int(n)
    if n < 0:
        return ctx.zetazero(-n).conjugate()
    if n == 0:
        raise ValueError("n must be nonzero")
    wpinitial = ctx.prec
    try:
        wpz, fp_tolerance = comp_fp_tolerance(ctx, n)
        ctx.prec = wpz
        if n < 400000000:
            my_zero_number, block, T, V =\
             find_rosser_block_zero(ctx, n)
        else:
            my_zero_number, block, T, V =\
             search_supergood_block(ctx, n, fp_tolerance)
        zero_number_block = block[1]-block[0]
        T, V, separated = separate_zeros_in_block(ctx, zero_number_block, T, V,
            limitloop=ctx.inf, fp_tolerance=fp_tolerance)
        if info:
            pattern = pattern_construct(ctx,block,T,V)
        prec = max(wpinitial, wpz)
        t = separate_my_zero(ctx, my_zero_number, zero_number_block,T,V,prec)
        v = ctx.mpc(0.5,t)
    finally:
        ctx.prec = wpinitial
    if round:
        v =+v
    if info:
        return (v,block,my_zero_number,pattern)
    else:
        return v

def memoize(ctx, f):
    f_cache = {}
    def f_cached(*args, **kwargs):
        if kwargs:
            key = args, tuple(kwargs.items())
        else:
            key = args
        prec = ctx.prec
        if key in f_cache:
            cprec, cvalue = f_cache[key]
            if cprec >= prec:
                return +cvalue
        value = f(*args, **kwargs)
        f_cache[key] = (prec, value)
        return value
    f_cached.__name__ = f.__name__
    f_cached.__doc__ = f.__doc__
    return f_cached

class SpecialFunctions(object):
    defined_functions = {}
    THETA_Q_LIM = 1 - 10**-7
    def __init__(self):
        cls = self.__class__
        for name in cls.defined_functions:
            f, wrap = cls.defined_functions[name]
            cls._wrap_specfun(name, f, wrap)
        self.mpq_1 = self._mpq((1,1))
        self.mpq_0 = self._mpq((0,1))
        self.mpq_1_2 = self._mpq((1,2))
        self.mpq_3_2 = self._mpq((3,2))
        self.mpq_1_4 = self._mpq((1,4))
        self.mpq_1_16 = self._mpq((1,16))
        self.mpq_3_16 = self._mpq((3,16))
        self.mpq_5_2 = self._mpq((5,2))
        self.mpq_3_4 = self._mpq((3,4))
        self.mpq_7_4 = self._mpq((7,4))
        self.mpq_5_4 = self._mpq((5,4))
        self.mpq_1_3 = self._mpq((1,3))
        self.mpq_2_3 = self._mpq((2,3))
        self.mpq_4_3 = self._mpq((4,3))
        self.mpq_1_6 = self._mpq((1,6))
        self.mpq_5_6 = self._mpq((5,6))
        self.mpq_5_3 = self._mpq((5,3))
        self._misc_const_cache = {}
        self._aliases.update({
            'phase' : 'arg',
            'conjugate' : 'conj',
            'nthroot' : 'root',
            'polygamma' : 'psi',
            'hurwitz' : 'zeta',
            'fibonacci' : 'fib',
            'factorial' : 'fac',
        })
        #self.zetazero_memoized = memoize(cls, zetazero)
    @classmethod
    def _wrap_specfun(cls, name, f, wrap):
        setattr(cls, name, f)
    def _besselj(ctx, n, z): raise NotImplementedError
    def _erf(ctx, z): raise NotImplementedError
    def _erfc(ctx, z): raise NotImplementedError
    def _gamma_upper_int(ctx, z, a): raise NotImplementedError
    def _expint_int(ctx, n, z): raise NotImplementedError
    def _zeta(ctx, s): raise NotImplementedError
    def _zetasum_fast(ctx, s, a, n, derivatives, reflect): raise NotImplementedError
    def _ei(ctx, z): raise NotImplementedError
    def _e1(ctx, z): raise NotImplementedError
    def _ci(ctx, z): raise NotImplementedError
    def _si(ctx, z): raise NotImplementedError
    def _altzeta(ctx, s): raise NotImplementedError


class QuadratureRule(object):
    def __init__(self, ctx):
        self.ctx = ctx
        self.standard_cache = {}
        self.transformed_cache = {}
        self.interval_count = {}

    def clear(self):
        self.standard_cache = {}
        self.transformed_cache = {}
        self.interval_count = {}

    def calc_nodes(self, degree, prec, verbose=False):
        raise NotImplementedError

    def get_nodes(self, a, b, degree, prec, verbose=False):
        key = (a, b, degree, prec)
        if key in self.transformed_cache:
            return self.transformed_cache[key]
        orig = self.ctx.prec
        try:
            self.ctx.prec = prec+20
            if (degree, prec) in self.standard_cache:
                nodes = self.standard_cache[degree, prec]
            else:
                nodes = self.calc_nodes(degree, prec, verbose)
                self.standard_cache[degree, prec] = nodes
            nodes = self.transform_nodes(nodes, a, b, verbose)
            if key in self.interval_count:
                self.transformed_cache[key] = nodes
            else:
                self.interval_count[key] = True
        finally:
            self.ctx.prec = orig
        return nodes

    def transform_nodes(self, nodes, a, b, verbose=False):
        ctx = self.ctx
        a = ctx.convert(a)
        b = ctx.convert(b)
        one = ctx.one
        if (a, b) == (-one, one):
            return nodes
        half = ctx.mpf(0.5)
        new_nodes = []
        if ctx.isinf(a) or ctx.isinf(b):
            if (a, b) == (ctx.ninf, ctx.inf):
                p05 = -half
                for x, w in nodes:
                    x2 = x*x
                    px1 = one-x2
                    spx1 = px1**p05
                    x = x*spx1
                    w *= spx1/px1
                    new_nodes.append((x, w))
            elif a == ctx.ninf:
                b1 = b+1
                for x, w in nodes:
                    u = 2/(x+one)
                    x = b1-u
                    w *= half*u**2
                    new_nodes.append((x, w))
            elif b == ctx.inf:
                a1 = a-1
                for x, w in nodes:
                    u = 2/(x+one)
                    x = a1+u
                    w *= half*u**2
                    new_nodes.append((x, w))
            elif a == ctx.inf or b == ctx.ninf:
                return [(x,-w) for (x,w) in self.transform_nodes(nodes, b, a, verbose)]
            else:
                raise NotImplementedError
        else:
            C = (b-a)/2
            D = (b+a)/2
            for x, w in nodes:
                new_nodes.append((D+C*x, C*w))
        return new_nodes

    def guess_degree(self, prec):
        g = int(4 + max(0, self.ctx.log(prec/30.0, 2)))
        g += 2
        return g

    def estimate_error(self, results, prec, epsilon):
        if len(results) == 2:
            return abs(results[0]-results[1])
        try:
            if results[-1] == results[-2] == results[-3]:
                return self.ctx.zero
            D1 = self.ctx.log(abs(results[-1]-results[-2]), 10)
            D2 = self.ctx.log(abs(results[-1]-results[-3]), 10)
        except ValueError:
            return epsilon
        D3 = -prec
        D4 = min(0, max(D1**2/D2, 2*D1, D3))
        return self.ctx.mpf(10) ** int(D4)

    def summation(self, f, points, prec, epsilon, max_degree, verbose=False):
        ctx = self.ctx
        I = err = ctx.zero
        for i in range(len(points)-1):
            a, b = points[i], points[i+1]
            if a == b:
                continue
            if (a, b) == (ctx.ninf, ctx.inf):
                _f = f
                f = lambda x: _f(-x) + _f(x)
                a, b = (ctx.zero, ctx.inf)
            results = []
            for degree in range(1, max_degree+1):
                nodes = self.get_nodes(a, b, degree, prec, verbose)
                if verbose:
                    print("Integrating from %s to %s (degree %s of %s)" % \
                        (ctx.nstr(a), ctx.nstr(b), degree, max_degree))
                results.append(self.sum_next(f, nodes, degree, prec, results, verbose))
                if degree > 1:
                    err = self.estimate_error(results, prec, epsilon)
                    if err <= epsilon:
                        break
                    if verbose:
                        print("Estimated error:", ctx.nstr(err))
            I += results[-1]
        if err > epsilon:
            if verbose:
                print("Failed to reach full accuracy. Estimated error:", ctx.nstr(err))
        return I, err

    def sum_next(self, f, nodes, degree, prec, previous, verbose=False):
        return self.ctx.fdot((w, f(x)) for (x,w) in nodes)

class TanhSinh(QuadratureRule):
    def sum_next(self, f, nodes, degree, prec, previous, verbose=False):
        h = self.ctx.mpf(2)**(-degree)
        if previous:
            S = previous[-1]/(h*2)
        else:
            S = self.ctx.zero
        S += self.ctx.fdot((w,f(x)) for (x,w) in nodes)
        return h*S

    def calc_nodes(self, degree, prec, verbose=False):
        ctx = self.ctx
        nodes = []
        extra = 20
        ctx.prec += extra
        tol = ctx.ldexp(1, -prec-10)
        pi4 = mp.pi/4
        t0 = ctx.ldexp(1, -degree)
        if degree == 1:
            nodes.append((ctx.zero, mp.pi/2))
            h = t0
        else:
            h = t0*2
        expt0 = ctx.exp(t0)
        a = pi4*expt0
        b = pi4 / expt0
        udelta = ctx.exp(h)
        urdelta = 1/udelta

        for k in range(0, 20*2**degree+1):
            c = ctx.exp(a-b)
            d = 1/c
            co = (c+d)/2
            si = (c-d)/2
            x = si / co
            w = (a+b) / co**2
            diff = abs(x-1)
            if diff <= tol:
                break
            nodes.append((x, w))
            nodes.append((-x, w))
            a *= udelta
            b *= urdelta
            if verbose and k % 300 == 150:
                print("Calculating nodes:", ctx.nstr(-ctx.log(diff, 10) / prec))
        ctx.prec -= extra
        return nodes

class GaussLegendre(QuadratureRule):
    def calc_nodes(self, degree, prec, verbose=False):
        ctx = self.ctx
        epsilon = ctx.ldexp(1, -prec-8)
        orig = ctx.prec
        ctx.prec = int(prec*1.5)
        if degree == 1:
            x = ctx.sqrt(ctx.mpf(3)/5)
            w = ctx.mpf(5)/9
            nodes = [(-x,w),(ctx.zero,ctx.mpf(8)/9),(x,w)]
            ctx.prec = orig
            return nodes
        nodes = []
        n = 3*2**(degree-1)
        upto = n//2 + 1
        for j in range(1, upto):
            r = ctx.mpf(math.cos(math.pi*(j-0.25)/(n+0.5)))
            while 1:
                t1, t2 = 1, 0
                for j1 in range(1,n+1):
                    t3, t2, t1 = t2, t1, ((2*j1-1)*r*t1 - (j1-1)*t2)/j1
                t4 = n*(r*t1- t2)/(r**2-1)
                t5 = r
                a = t1/t4
                r = r - a
                if abs(a) < epsilon:
                    break
            x = r
            w = 2/((1-r**2)*t4**2)
            if verbose  and j % 30 == 15:
                print("Computing nodes (%i of %i)" % (j, upto))
            nodes.append((x, w))
            nodes.append((-x, w))
        ctx.prec = orig
        return nodes

mpi_zero = (fzero, fzero)
new = object.__new__

class mpq(object):
    __slots__ = ["_mpq_"]
    def __new__(cls, p, q=1):
        if type(p) is tuple:
            p, q = p
        elif hasattr(p, '_mpq_'):
            p, q = p._mpq_
        return create_reduced(p, q)

    def __repr__(s):
        return "mpq(%s,%s)" % s._mpq_

    def __str__(s):
        return "(%s/%s)" % s._mpq_

    def __int__(s):
        a, b = s._mpq_
        return a // b

    def __nonzero__(s):
        return bool(s._mpq_[0])

    __bool__ = __nonzero__

    def __hash__(s):
        a, b = s._mpq_
        inverse = pow(b, HASH_MODULUS-2, HASH_MODULUS)
        if not inverse:
            h = sys.hash_info.inf
        else:
            h = (abs(a)*inverse) % HASH_MODULUS
        if a < 0: h = -h
        if h == -1: h = -2
        return h

    def __eq__(s, t):
        ttype = type(t)
        if ttype is mpq:
            return s._mpq_ == t._mpq_
        if ttype in int_types:
            a, b = s._mpq_
            if b != 1:
                return False
            return a == t
        return NotImplemented

    def __ne__(s, t):
        ttype = type(t)
        if ttype is mpq:
            return s._mpq_ != t._mpq_
        if ttype in int_types:
            a, b = s._mpq_
            if b != 1:
                return True
            return a != t
        return NotImplemented

    def _cmp(s, t, op):
        ttype = type(t)
        if ttype in int_types:
            a, b = s._mpq_
            return op(a, t*b)
        if ttype is mpq:
            a, b = s._mpq_
            c, d = t._mpq_
            return op(a*d, b*c)
        return NotImplementedError

    def __lt__(s, t): return s._cmp(t, operator.lt)
    def __le__(s, t): return s._cmp(t, operator.le)
    def __gt__(s, t): return s._cmp(t, operator.gt)
    def __ge__(s, t): return s._cmp(t, operator.ge)

    def __abs__(s):
        a, b = s._mpq_
        if a >= 0:
            return s
        v = new(mpq)
        v._mpq_ = -a, b
        return v

    def __neg__(s):
        a, b = s._mpq_
        v = new(mpq)
        v._mpq_ = -a, b
        return v

    def __pos__(s):
        return s

    def __add__(s, t):
        ttype = type(t)
        if ttype is mpq:
            a, b = s._mpq_
            c, d = t._mpq_
            return create_reduced(a*d+b*c, b*d)
        if ttype in int_types:
            a, b = s._mpq_
            v = new(mpq)
            v._mpq_ = a+b*t, b
            return v
        return NotImplemented

    __radd__ = __add__

    def __sub__(s, t):
        ttype = type(t)
        if ttype is mpq:
            a, b = s._mpq_
            c, d = t._mpq_
            return create_reduced(a*d-b*c, b*d)
        if ttype in int_types:
            a, b = s._mpq_
            v = new(mpq)
            v._mpq_ = a-b*t, b
            return v
        return NotImplemented

    def __rsub__(s, t):
        ttype = type(t)
        if ttype is mpq:
            a, b = s._mpq_
            c, d = t._mpq_
            return create_reduced(b*c-a*d, b*d)
        if ttype in int_types:
            a, b = s._mpq_
            v = new(mpq)
            v._mpq_ = b*t-a, b
            return v
        return NotImplemented

    def __mul__(s, t):
        ttype = type(t)
        if ttype is mpq:
            a, b = s._mpq_
            c, d = t._mpq_
            return create_reduced(a*c, b*d)
        if ttype in int_types:
            a, b = s._mpq_
            return create_reduced(a*t, b)
        return NotImplemented

    __rmul__ = __mul__

    def __div__(s, t):
        ttype = type(t)
        if ttype is mpq:
            a, b = s._mpq_
            c, d = t._mpq_
            return create_reduced(a*d, b*c)
        if ttype in int_types:
            a, b = s._mpq_
            return create_reduced(a, b*t)
        return NotImplemented

    def __rdiv__(s, t):
        ttype = type(t)
        if ttype is mpq:
            a, b = s._mpq_
            c, d = t._mpq_
            return create_reduced(b*c, a*d)
        if ttype in int_types:
            a, b = s._mpq_
            return create_reduced(b*t, a)
        return NotImplemented

    def __pow__(s, t):
        ttype = type(t)
        if ttype in int_types:
            a, b = s._mpq_
            if t:
                if t < 0:
                    a, b, t = b, a, -t
                v = new(mpq)
                v._mpq_ = a**t, b**t
                return v
            raise ZeroDivisionError
        return NotImplemented


    @classmethod
    def _wrap_specfun(cls, name, f, wrap):
        if wrap:
            def f_wrapped(ctx, *args, **kwargs):
                convert = ctx.convert
                args = [convert(a) for a in args]
                prec = ctx.prec
                try:
                    ctx.prec += 10
                    retval = f(ctx, *args, **kwargs)
                finally:
                    ctx.prec = prec
                return +retval
        else:
            f_wrapped = f
        setattr(cls, name, f_wrapped)

    def isnan(ctx, x):
        if hasattr(x, "_mpf_"):
            return x._mpf_ == fnan
        if hasattr(x, "_mpc_"):
            return fnan in x._mpc_
        if isinstance(x, int_types) or isinstance(x, mpq):
            return False
        x = ctx.convert(x)
        if hasattr(x, '_mpf_') or hasattr(x, '_mpc_'):
            return ctx.isnan(x)
        raise TypeError("isnan() needs a number as input")

    def isinf(ctx, x):
        if hasattr(x, "_mpf_"):
            return x._mpf_ in (finf, fninf)
        if hasattr(x, "_mpc_"):
            re, im = x._mpc_
            return re in (finf, fninf) or im in (finf, fninf)
        if isinstance(x, int_types) or isinstance(x, mpq):
            return False
        x = ctx.convert(x)
        if hasattr(x, '_mpf_') or hasattr(x, '_mpc_'):
            return ctx.isinf(x)
        raise TypeError("isinf() needs a number as input")

    def isnormal(ctx, x):
        if hasattr(x, "_mpf_"):
            return bool(x._mpf_[1])
        if hasattr(x, "_mpc_"):
            re, im = x._mpc_
            re_normal = bool(re[1])
            im_normal = bool(im[1])
            if re == fzero: return im_normal
            if im == fzero: return re_normal
            return re_normal and im_normal
        if isinstance(x, int_types) or isinstance(x, mpq):
            return bool(x)
        x = ctx.convert(x)
        if hasattr(x, '_mpf_') or hasattr(x, '_mpc_'):
            return ctx.isnormal(x)
        raise TypeError("isnormal() needs a number as input")

    def isint(ctx, x, gaussian=False):
        if isinstance(x, int_types):
            return True
        if hasattr(x, "_mpf_"):
            sign, man, exp, bc = xval = x._mpf_
            return bool((man and exp >= 0) or xval == fzero)
        if hasattr(x, "_mpc_"):
            re, im = x._mpc_
            rsign, rman, rexp, rbc = re
            isign, iman, iexp, ibc = im
            re_isint = (rman and rexp >= 0) or re == fzero
            if gaussian:
                im_isint = (iman and iexp >= 0) or im == fzero
                return re_isint and im_isint
            return re_isint and im == fzero
        if isinstance(x, mpq):
            p, q = x._mpq_
            return p % q == 0
        x = ctx.convert(x)
        if hasattr(x, '_mpf_') or hasattr(x, '_mpc_'):
            return ctx.isint(x, gaussian)
        raise TypeError("isint() needs a number as input")

    def fsum(ctx, terms, absolute=False, squared=False):
        prec, rnd = ctx._prec_rounding
        real = []
        imag = []
        other = 0
        for term in terms:
            reval = imval = 0
            if hasattr(term, "_mpf_"):
                reval = term._mpf_
            elif hasattr(term, "_mpc_"):
                reval, imval = term._mpc_
            else:
                term = ctx.convert(term)
                if hasattr(term, "_mpf_"):
                    reval = term._mpf_
                elif hasattr(term, "_mpc_"):
                    reval, imval = term._mpc_
                else:
                    if absolute: term = ctx.absmax(term)
                    if squared: term = term**2
                    other += term
                    continue
            if imval:
                if squared:
                    if absolute:
                        real.append(mpf_mul(reval,reval))
                        real.append(mpf_mul(imval,imval))
                    else:
                        reval, imval = mpc_pow_int((reval,imval),2,prec+10)
                        real.append(reval)
                        imag.append(imval)
                elif absolute:
                    real.append(mpc_abs((reval,imval), prec))
                else:
                    real.append(reval)
                    imag.append(imval)
            else:
                if squared:
                    reval = mpf_mul(reval, reval)
                elif absolute:
                    reval = mpf_abs(reval)
                real.append(reval)
        s = mpf_sum(real, prec, rnd, absolute)
        if imag:
            s = ctx.make_mpc((s, mpf_sum(imag, prec, rnd)))
        else:
            s = ctx.make_mpf(s)
        if other is 0:
            return s
        else:
            return s + other

    def fdot(ctx, A, B=None, conjugate=False):
        if B:
            A = zip(A, B)
        prec, rnd = ctx._prec_rounding
        real = []
        imag = []
        other = 0
        hasattr_ = hasattr
        types = (ctx.mpf, ctx.mpc)
        for a, b in A:
            if type(a) not in types: a = ctx.convert(a)
            if type(b) not in types: b = ctx.convert(b)
            a_real = hasattr_(a, "_mpf_")
            b_real = hasattr_(b, "_mpf_")
            if a_real and b_real:
                real.append(mpf_mul(a._mpf_, b._mpf_))
                continue
            a_complex = hasattr_(a, "_mpc_")
            b_complex = hasattr_(b, "_mpc_")
            if a_real and b_complex:
                aval = a._mpf_
                bre, bim = b._mpc_
                if conjugate:
                    bim = mpf_neg(bim)
                real.append(mpf_mul(aval, bre))
                imag.append(mpf_mul(aval, bim))
            elif b_real and a_complex:
                are, aim = a._mpc_
                bval = b._mpf_
                real.append(mpf_mul(are, bval))
                imag.append(mpf_mul(aim, bval))
            elif a_complex and b_complex:
                are, aim = a._mpc_
                bre, bim = b._mpc_
                if conjugate:
                    bim = mpf_neg(bim)
                real.append(mpf_mul(are, bre))
                real.append(mpf_neg(mpf_mul(aim, bim)))
                imag.append(mpf_mul(are, bim))
                imag.append(mpf_mul(aim, bre))
            else:
                if conjugate:
                    other += a*ctx.conj(b)
                else:
                    other += a*b
        s = mpf_sum(real, prec, rnd)
        if imag:
            s = ctx.make_mpc((s, mpf_sum(imag, prec, rnd)))
        else:
            s = ctx.make_mpf(s)
        if other is 0:
            return s
        else:
            return s + other

    def _wrap_libmp_function(ctx, mpf_f, mpc_f=None, mpi_f=None, doc="<no doc>"):
        def f(x, **kwargs):
            if type(x) not in ctx.types:
                x = ctx.convert(x)
            prec, rounding = ctx._prec_rounding
            if kwargs:
                prec = kwargs.get('prec', prec)
                if 'dps' in kwargs:
                    prec = dps_to_prec(kwargs['dps'])
                rounding = kwargs.get('rounding', rounding)
            if hasattr(x, '_mpf_'):
                try:
                    return ctx.make_mpf(mpf_f(x._mpf_, prec, rounding))
                except ComplexResult:
                    if ctx.trap_complex:
                        raise
                    return ctx.make_mpc(mpc_f((x._mpf_, fzero), prec, rounding))
            elif hasattr(x, '_mpc_'):
                return ctx.make_mpc(mpc_f(x._mpc_, prec, rounding))
            raise NotImplementedError("%s of a %s" % (name, type(x)))
        name = mpf_f.__name__[4:]
        return f

    def _convert_param(ctx, x):
        if hasattr(x, "_mpc_"):
            v, im = x._mpc_
            if im != fzero:
                return x, 'C'
        elif hasattr(x, "_mpf_"):
            v = x._mpf_
        else:
            if type(x) in int_types:
                return int(x), 'Z'
            p = None
            if isinstance(x, tuple):
                p, q = x
            elif hasattr(x, '_mpq_'):
                p, q = x._mpq_
            elif isinstance(x, basestring) and '/' in x:
                p, q = x.split('/')
                p = int(p)
                q = int(q)
            if p is not None:
                if not p % q:
                    return p // q, 'Z'
                return ctx.mpq(p,q), 'Q'
            x = ctx.convert(x)
            if hasattr(x, "_mpc_"):
                v, im = x._mpc_
                if im != fzero:
                    return x, 'C'
            elif hasattr(x, "_mpf_"):
                v = x._mpf_
            else:
                return x, 'U'
        sign, man, exp, bc = v
        if man:
            if exp >= -4:
                if sign:
                    man = -man
                if exp >= 0:
                    return int(man) << exp, 'Z'
                if exp >= -4:
                    p, q = int(man), (1<<(-exp))
                    return ctx.mpq(p,q), 'Q'
            x = ctx.make_mpf(v)
            return x, 'R'
        elif not exp:
            return 0, 'Z'
        else:
            return x, 'U'

    def _mpf_mag(ctx, x):
        sign, man, exp, bc = x
        if man:
            return exp+bc
        if x == fzero:
            return ctx.ninf
        if x == finf or x == fninf:
            return ctx.inf
        return ctx.nan

    def mag(ctx, x):
        if hasattr(x, "_mpf_"):
            return ctx._mpf_mag(x._mpf_)
        elif hasattr(x, "_mpc_"):
            r, i = x._mpc_
            if r == fzero:
                return ctx._mpf_mag(i)
            if i == fzero:
                return ctx._mpf_mag(r)
            return 1+max(ctx._mpf_mag(r), ctx._mpf_mag(i))
        elif isinstance(x, int_types):
            if x:
                return bitcount(abs(x))
            return ctx.ninf
        elif isinstance(x, mpq):
            p, q = x._mpq_
            if p:
                return 1 + bitcount(abs(p)) - bitcount(q)
            return ctx.ninf
        else:
            x = ctx.convert(x)
            if hasattr(x, "_mpf_") or hasattr(x, "_mpc_"):
                return ctx.mag(x)
            else:
                raise TypeError("requires an mpf/mpc")

class LinearAlgebraMethods(object):
    def LU_decomp(ctx, A, overwrite=False, use_cache=True):
        if not A.rows == A.cols:
            raise ValueError('need n*n matrix')
        if use_cache and isinstance(A, ctx.matrix) and A._LU:
            return A._LU
        if not overwrite:
            orig = A
            A = A.copy()
        tol = ctx.absmin(ctx.mnorm(A,1)*mp.eps)
        n = A.rows
        p = [None]*(n - 1)
        for j in range(n - 1):
            biggest = 0
            for k in range(j, n):
                s = ctx.fsum([ctx.absmin(A[k,l]) for l in range(j, n)])
                if ctx.absmin(s) <= tol:
                    raise ZeroDivisionError('matrix is numerically singular')
                current = 1/s*ctx.absmin(A[k,j])
                if current > biggest:
                    biggest = current
                    p[j] = k
            ctx.swap_row(A, j, p[j])
            if ctx.absmin(A[j,j]) <= tol:
                raise ZeroDivisionError('matrix is numerically singular')
            for i in range(j + 1, n):
                A[i,j] /= A[j,j]
                for k in range(j + 1, n):
                    A[i,k] -= A[i,j]*A[j,k]
        if ctx.absmin(A[n - 1,n - 1]) <= tol:
            raise ZeroDivisionError('matrix is numerically singular')
        if not overwrite and isinstance(orig, ctx.matrix):
            orig._LU = (A, p)
        return A, p

    def L_solve(ctx, L, b, p=None):
        if L.rows != L.cols:
            raise RuntimeError("need n*n matrix")
        n = L.rows
        if len(b) != n:
            raise ValueError("Value should be equal to n")
        b = copy(b)
        if p:
            for k in range(0, len(p)):
                ctx.swap_row(b, k, p[k])
        for i in range(1, n):
            for j in range(i):
                b[i] -= L[i,j]*b[j]
        return b

    def U_solve(ctx, U, y):
        if U.rows != U.cols:
            raise RuntimeError("need n*n matrix")
        n = U.rows
        if len(y) != n:
            raise ValueError("Value should be equal to n")
        x = copy(y)
        for i in range(n - 1, -1, -1):
            for j in range(i + 1, n):
                x[i] -= U[i,j]*x[j]
            x[i] /= U[i,i]
        return x

    def lu_solve(ctx, A, b, **kwargs):

        prec = ctx.prec
        try:
            ctx.prec += 10
            A, b = ctx.matrix(A, **kwargs).copy(), ctx.matrix(b, **kwargs).copy()
            if A.rows < A.cols:
                raise ValueError('cannot solve underdetermined system')
            if A.rows > A.cols:
                AH = A.H
                A = AH*A
                b = AH*b
                if (kwargs.get('real', False) or
                    not sum(type(i) is ctx.mpc for i in A)):
                    x = ctx.cholesky_solve(A, b)
                else:
                    x = ctx.lu_solve(A, b)
            else:
                A, p = ctx.LU_decomp(A)
                b = ctx.L_solve(A, b, p)
                x = ctx.U_solve(A, b)
        finally:
            ctx.prec = prec
        return x

    def improve_solution(ctx, A, x, b, maxsteps=1):
        if A.rows != A.cols:
            raise RuntimeError("need n*n matrix")
        for _ in range(maxsteps):
            r = ctx.residual(A, x, b)
            if ctx.norm(r, 2) < 10*mp.eps:
                break
            dx = ctx.lu_solve(A, -r)
            x += dx
        return x

    def lu(ctx, A):
        A, p = ctx.LU_decomp(A)
        n = A.rows
        L = ctx.matrix(n)
        U = ctx.matrix(n)
        for i in range(n):
            for j in range(n):
                if i > j:
                    L[i,j] = A[i,j]
                elif i == j:
                    L[i,j] = 1
                    U[i,j] = A[i,j]
                else:
                    U[i,j] = A[i,j]
        P = ctx.eye(n)
        for k in range(len(p)):
            ctx.swap_row(P, k, p[k])
        return P, L, U

    def unitvector(ctx, n, i):
        assert 0 < i <= n, 'this unit vector does not exist'
        return [ctx.zero]*(i-1) + [ctx.one] + [ctx.zero]*(n-i)

    def inverse(ctx, A, **kwargs):
        prec = ctx.prec
        try:
            ctx.prec += 10
            A = ctx.matrix(A, **kwargs).copy()
            n = A.rows
            A, p = ctx.LU_decomp(A)
            cols = []
            for i in range(1, n + 1):
                e = ctx.unitvector(n, i)
                y = ctx.L_solve(A, e, p)
                cols.append(ctx.U_solve(A, y))
            inv = []
            for i in range(n):
                row = []
                for j in range(n):
                    row.append(cols[j][i])
                inv.append(row)
            result = ctx.matrix(inv, **kwargs)
        finally:
            ctx.prec = prec
        return result

    def householder(ctx, A):
        if not isinstance(A, ctx.matrix):
            raise TypeError("A should be a type of ctx.matrix")
        m = A.rows
        n = A.cols
        if m < n - 1:
            raise RuntimeError("Columns should not be less than rows")
        p = []
        for j in range(0, n - 1):
            s = ctx.fsum((A[i,j])**2 for i in range(j, m))
            if not abs(s) > mp.eps:
                raise ValueError('matrix is numerically singular')
            p.append(-ctx.sign(A[j,j])*ctx.sqrt(s))
            kappa = ctx.one / (s - p[j]*A[j,j])
            A[j,j] -= p[j]
            for k in range(j+1, n):
                y = ctx.fsum(A[i,j]*A[i,k] for i in range(j, m))*kappa
                for i in range(j, m):
                    A[i,k] -= A[i,j]*y
        x = [A[i,n - 1] for i in range(n - 1)]
        for i in range(n - 2, -1, -1):
            x[i] -= ctx.fsum(A[i,j]*x[j] for j in range(i + 1, n - 1))
            x[i] /= p[i]
        if not m == n - 1:
            r = [A[m-1-i, n-1] for i in range(m - n + 1)]
        else:
            r = [0]*m
        return A, p, x, r

    def residual(ctx, A, x, b, **kwargs):
        oldprec = ctx.prec
        try:
            ctx.prec *= 2
            A, x, b = ctx.matrix(A, **kwargs), ctx.matrix(x, **kwargs), ctx.matrix(b, **kwargs)
            return A*x - b
        finally:
            ctx.prec = oldprec

    def qr_solve(ctx, A, b, norm=None, **kwargs):
        if norm is None:
            norm = ctx.norm
        prec = ctx.prec
        try:
            ctx.prec += 10
            A, b = ctx.matrix(A, **kwargs).copy(), ctx.matrix(b, **kwargs).copy()
            if A.rows < A.cols:
                raise ValueError('cannot solve underdetermined system')
            H, p, x, r = ctx.householder(ctx.extend(A, b))
            res = ctx.norm(r)
            if res == 0:
                res = ctx.norm(ctx.residual(A, x, b))
            return ctx.matrix(x, **kwargs), res
        finally:
            ctx.prec = prec

    def cholesky(ctx, A, tol=None):
        if not isinstance(A, ctx.matrix):
            raise RuntimeError("A should be a type of ctx.matrix")
        if not A.rows == A.cols:
            raise ValueError('need n*n matrix')
        if tol is None:
            tol = +mp.eps
        n = A.rows
        L = ctx.matrix(n)
        for j in range(n):
            c = ctx.re(A[j,j])
            if abs(c-A[j,j]) > tol:
                raise ValueError('matrix is not Hermitian')
            s = c - ctx.fsum((L[j,k] for k in range(j)),
                absolute=True, squared=True)
            if s < tol:
                raise ValueError('matrix is not positive-definite')
            L[j,j] = ctx.sqrt(s)
            for i in range(j, n):
                it1 = (L[i,k] for k in range(j))
                it2 = (L[j,k] for k in range(j))
                t = ctx.fdot(it1, it2, conjugate=True)
                L[i,j] = (A[i,j] - t) / L[j,j]
        return L

    def cholesky_solve(ctx, A, b, **kwargs):
        prec = ctx.prec
        try:
            ctx.prec += 10
            A, b = ctx.matrix(A, **kwargs).copy(), ctx.matrix(b, **kwargs).copy()
            if A.rows !=  A.cols:
                raise ValueError('can only solve determined system')
            L = ctx.cholesky(A)
            n = L.rows
            if len(b) != n:
                raise ValueError("Value should be equal to n")
            for i in range(n):
                b[i] -= ctx.fsum(L[i,j]*b[j] for j in range(i))
                b[i] /= L[i,i]
            x = ctx.U_solve(L.T, b)
            return x
        finally:
            ctx.prec = prec

    def det(ctx, A):
        prec = ctx.prec
        try:
            A = ctx.matrix(A).copy()
            try:
                R, p = ctx.LU_decomp(A)
            except ZeroDivisionError:
                return 0
            z = 1
            for i, e in enumerate(p):
                if i != e:
                    z *= -1
            for i in range(A.rows):
                z *= R[i,i]
            return z
        finally:
            ctx.prec = prec

    def cond(ctx, A, norm=None):
        if norm is None:
            norm = lambda x: ctx.mnorm(x,1)
        return norm(A)*norm(ctx.inverse(A))

    def lu_solve_mat(ctx, a, b):
        r
        r = ctx.matrix(a.rows, b.cols)
        for i in range(b.cols):
            c = ctx.lu_solve(a, b.column(i))
            for j in range(len(c)):
                r[j, i] = c[j]
        return r

    def qr(ctx, A, mode = 'full', edps = 10):
        assert isinstance(A, ctx.matrix)
        m = A.rows
        n = A.cols
        assert n > 1
        assert m >= n
        assert edps >= 0
        cmplx = any(type(x) is ctx.mpc for x in A)
        with ctx.extradps(edps):
            tau = ctx.matrix(n,1)
            A = A.copy()
            if cmplx:
                one = ctx.mpc('1.0', '0.0')
                zero = ctx.mpc('0.0', '0.0')
                rzero = ctx.mpf('0.0')
                for j in range(0, n):
                    alpha = A[j,j]
                    alphr = ctx.re(alpha)
                    alphi = ctx.im(alpha)
                    if (m-j) >= 2:
                        xnorm = ctx.fsum( A[i,j]*ctx.conj(A[i,j]) for i in range(j+1, m) )
                        xnorm = ctx.re( ctx.sqrt(xnorm) )
                    else:
                        xnorm = rzero
                    if (xnorm == rzero) and (alphi == rzero):
                        tau[j] = zero
                        continue
                    if alphr < rzero:
                        beta = ctx.sqrt(alphr**2 + alphi**2 + xnorm**2)
                    else:
                        beta = -ctx.sqrt(alphr**2 + alphi**2 + xnorm**2)
                    tau[j] = ctx.mpc( (beta - alphr) / beta, -alphi / beta )
                    t = -ctx.conj(tau[j])
                    za = one / (alpha - beta)
                    for i in range(j+1, m):
                        A[i,j] *= za
                    A[j,j] = one
                    for k in range(j+1, n):
                        y = ctx.fsum(A[i,j]*ctx.conj(A[i,k]) for i in range(j, m))
                        temp = t*ctx.conj(y)
                        for i in range(j, m):
                            A[i,k] += A[i,j]*temp
                    A[j,j] = ctx.mpc(beta, '0.0')
            else:
                one = ctx.mpf('1.0')
                zero = ctx.mpf('0.0')
                for j in range(0, n):
                    alpha = A[j,j]
                    if (m-j) > 2:
                        xnorm = ctx.fsum( (A[i,j])**2 for i in range(j+1, m) )
                        xnorm = ctx.sqrt(xnorm)
                    elif (m-j) == 2:
                        xnorm = abs( A[m-1,j] )
                    else:
                        xnorm = zero
                    if xnorm == zero:
                        tau[j] = zero
                        continue
                    if alpha < zero:
                        beta = ctx.sqrt(alpha**2 + xnorm**2)
                    else:
                        beta = -ctx.sqrt(alpha**2 + xnorm**2)
                    tau[j] = (beta - alpha) / beta
                    t = -tau[j]
                    da = one / (alpha - beta)
                    for i in range(j+1, m):
                        A[i,j] *= da
                    A[j,j] = one
                    for k in range(j+1, n):
                        y = ctx.fsum( A[i,j]*A[i,k] for i in range(j, m) )
                        temp = t*y
                        for i in range(j,m):
                            A[i,k] += A[i,j]*temp
                    A[j,j] = beta
            if (mode == 'raw') or (mode == 'RAW'):
                return A, tau
            R = A.copy()
            for j in range(0, n):
                for i in range(j+1, m):
                    R[i,j] = zero
            p = m
            if (mode == 'skinny') or (mode == 'SKINNY'):
                p = n
            A.cols += (p-n)
            for j in range(0, p):
                A[j,j] = one
                for i in range(0, j):
                    A[i,j] = zero
            for j in range(n-1, -1, -1):
                t = -tau[j]
                A[j,j] += t
                for k in range(j+1, p):
                    if cmplx:
                        y = ctx.fsum(A[i,j]*ctx.conj(A[i,k]) for i in range(j+1, m))
                        temp = t*ctx.conj(y)
                    else:
                        y = ctx.fsum(A[i,j]*A[i,k] for i in range(j+1, m))
                        temp = t*y
                    A[j,k] = temp
                    for i in range(j+1, m):
                        A[i,k] += A[i,j]*temp
                for i in range(j+1, m):
                    A[i, j] *= t
            return A, R[0:p,0:n]

rootdir = os.path.abspath(os.getcwd())
class VisualizationMethods(object):
    plot_ignore = (ValueError, ArithmeticError, ZeroDivisionError, NoConvergence)


def defun_wrapped(f):
    SpecialFunctions.defined_functions[f.__name__] = f, True

def defun(f):
    SpecialFunctions.defined_functions[f.__name__] = f, False

def defun_static(f):
    setattr(SpecialFunctions, f.__name__, f)
    
@defun
def zetazero(ctx, n, info=False, round=True):
    n = int(n)
    if n < 0:
        return ctx.zetazero(-n).conjugate()
    if n == 0:
        raise ValueError("n must be nonzero")
    wpinitial = ctx.prec
    try:
        wpz, fp_tolerance = comp_fp_tolerance(ctx, n)
        ctx.prec = wpz
        if n < 400000000:
            my_zero_number, block, T, V =\
             find_rosser_block_zero(ctx, n)
        else:
            my_zero_number, block, T, V =\
             search_supergood_block(ctx, n, fp_tolerance)
        zero_number_block = block[1]-block[0]
        T, V, separated = separate_zeros_in_block(ctx, zero_number_block, T, V,
            limitloop=ctx.inf, fp_tolerance=fp_tolerance)
        if info:
            pattern = pattern_construct(ctx,block,T,V)
        prec = max(wpinitial, wpz)
        t = separate_my_zero(ctx, my_zero_number, zero_number_block,T,V,prec)
        v = ctx.mpc(0.5,t)
    finally:
        ctx.prec = wpinitial
    if round:
        v =+v
    if info:
        return (v,block,my_zero_number,pattern)
    else:
        return v

def log_int_fixed(n, prec, ln2=None):
    if n in log_int_cache:
        value, vprec = log_int_cache[n]
        if vprec >= prec:
            return value >> (vprec - prec)
    wp = prec + 10
    if wp <= LOG_TAYLOR_SHIFT:
        if ln2 is None:
            ln2 = ln2_fixed(wp)
        r = bitcount(n)
        x = n << (wp-r)
        v = log_taylor_cached(x, wp) + r*ln2
    else:
        v = to_fixed(mpf_log(from_int(n), wp+5), wp)
    if n < MAX_LOG_INT_CACHE:
        log_int_cache[n] = (v, wp)
    return v >> (wp-prec)

def convert_mpf_(x, prec, rounding):
    if hasattr(x, "_mpf_"): return x._mpf_
    if isinstance(x, int_types): return from_int(x, prec, rounding)
    if isinstance(x, float): return from_float(x, prec, rounding)
    if isinstance(x, basestring): return from_str(x, prec, rounding)

class QuadratureMethods(object):
    def __init__(ctx, *args, **kwargs):
        ctx._gauss_legendre = GaussLegendre(ctx)
        ctx._tanh_sinh = TanhSinh(ctx)
    def quad(ctx, f, *points, **kwargs):
        rule = kwargs.get('method', 'tanh-sinh')
        if type(rule) is str:
            if rule == 'tanh-sinh':
                rule = ctx._tanh_sinh
            elif rule == 'gauss-legendre':
                rule = ctx._gauss_legendre
            else:
                raise ValueError("unknown quadrature rule: %s" % rule)
        else:
            rule = rule(ctx)
        verbose = kwargs.get('verbose')
        dim = len(points)
        orig = prec = ctx.prec
        epsilon = mp.eps/8
        m = kwargs.get('maxdegree') or rule.guess_degree(prec)
        points = [ctx._as_points(p) for p in points]
        try:
            ctx.prec += 20
            if dim == 1:
                v, err = rule.summation(f, points[0], prec, epsilon, m, verbose)
            elif dim == 2:
                v, err = rule.summation(lambda x: \
                        rule.summation(lambda y: f(x,y), \
                        points[1], prec, epsilon, m)[0],
                    points[0], prec, epsilon, m, verbose)
            elif dim == 3:
                v, err = rule.summation(lambda x: \
                        rule.summation(lambda y: \
                            rule.summation(lambda z: f(x,y,z), \
                            points[2], prec, epsilon, m)[0],
                        points[1], prec, epsilon, m)[0],
                    points[0], prec, epsilon, m, verbose)
            else:
                raise NotImplementedError("quadrature must have dim 1, 2 or 3")
        finally:
            ctx.prec = orig
        if kwargs.get("error"):
            return +v, err
        return +v

    def quadts(ctx, *args, **kwargs):
        kwargs['method'] = 'tanh-sinh'
        return ctx.quad(*args, **kwargs)

    def quadgl(ctx, *args, **kwargs):
        kwargs['method'] = 'gauss-legendre'
        return ctx.quad(*args, **kwargs)

    def quadosc(ctx, f, interval, omega=None, period=None, zeros=None):
        a, b = ctx._as_points(interval)
        a = ctx.convert(a)
        b = ctx.convert(b)
        if [omega, period, zeros].count(None) != 2:
            raise ValueError( \
                "must specify exactly one of omega, period, zeros")
        if a == ctx.ninf and b == ctx.inf:
            s1 = ctx.quadosc(f, [a, 0], omega=omega, zeros=zeros, period=period)
            s2 = ctx.quadosc(f, [0, b], omega=omega, zeros=zeros, period=period)
            return s1 + s2
        if a == ctx.ninf:
            if zeros:
                return ctx.quadosc(lambda x:f(-x), [-b,-a], lambda n: zeros(-n))
            else:
                return ctx.quadosc(lambda x:f(-x), [-b,-a], omega=omega, period=period)
        if b != ctx.inf:
            raise ValueError("quadosc requires an infinite integration interval")
        if not zeros:
            if omega:
                period = 2*mp.pi/omega
            zeros = lambda n: n*period/2
        n = 1
        s = ctx.quadgl(f, [a, zeros(n)])
        def term(k):
            return ctx.quadgl(f, [zeros(k), zeros(k+1)])
        s += ctx.nsum(term, [n, ctx.inf])
        return s

def testit(line):
    if filt in line:
        print(line)
        t1 = clock()
        exec_(line, globals(), locals())
        t2 = clock()
        elapsed = t2-t1
        print("Time:", elapsed, "for", line, "(OK)")

def monitor(f, input='print', output='print'):
    if not input:
        input = lambda v: None
    elif input == 'print':
        incount = [0]
        def input(value):
            args, kwargs = value
            print("in  %s %r %r" % (incount[0], args, kwargs))
            incount[0] += 1
    if not output:
        output = lambda v: None
    elif output == 'print':
        outcount = [0]
        def output(value):
            print("out %s %r" % (outcount[0], value))
            outcount[0] += 1
    def f_monitored(*args, **kwargs):
        input((args, kwargs))
        v = f(*args, **kwargs)
        output(v)
        return v
    return f_monitored

def timing(f, *args, **kwargs):
    once = kwargs.get('once')
    if 'once' in kwargs:
        del kwargs['once']
    if args or kwargs:
        if len(args) == 1 and not kwargs:
            arg = args[0]
            g = lambda: f(arg)
        else:
            g = lambda: f(*args, **kwargs)
    else:
        g = f
    from timeit import default_timer as clock
    t1=clock(); v=g(); t2=clock(); t=t2-t1
    if t > 0.05 or once:
        return t
    for i in range(3):
        t1=clock()
        g();g();g();g();g();g();g();g();g();g()
        t2=clock()
        t=min(t,(t2-t1)/10)
    return t

class InverseLaplaceTransform(object):
    def __init__(self,ctx):
        self.ctx = ctx

    def calc_laplace_parameter(self,t,**kwargs):
        raise NotImplementedError

    def calc_time_domain_solution(self,fp):
        raise NotImplementedError

class FixedTalbot(InverseLaplaceTransform):
    def calc_laplace_parameter(self,t,**kwargs):
        self.t = self.ctx.convert(t)
        self.tmax = self.ctx.convert(kwargs.get('tmax',self.t))
        if 'degree' in kwargs:
            self.degree = kwargs['degree']
            self.dps_goal = self.degree
        else:
            self.dps_goal = int(1.72*self.ctx.dps)
            self.degree = max(12,int(1.38*self.dps_goal))
        M = self.degree
        self.dps_orig = self.ctx.dps
        self.ctx.dps = self.dps_goal
        self.r = kwargs.get('r',self.ctx.fraction(2,5)*M)
        self.theta = self.ctx.linspace(0.0, self.mp.pi, M+1)
        self.cot_theta = self.ctx.matrix(M,1)
        self.cot_theta[0] = 0
        self.delta = self.ctx.matrix(M,1)
        self.delta[0] = self.r

        for i in range(1,M):
            self.cot_theta[i] = self.ctx.cot(self.theta[i])
            self.delta[i] = self.r*self.theta[i]*(self.cot_theta[i] + 1j)

        self.p = self.ctx.matrix(M,1)
        self.p = self.delta/self.tmax


    def calc_time_domain_solution(self,fp,t,manual_prec=False):
        self.t = self.ctx.convert(t)
        theta = self.theta
        delta = self.delta
        M = self.degree
        p = self.p
        r = self.r
        ans = self.ctx.matrix(M,1)
        ans[0] = self.ctx.exp(delta[0])*fp[0]/2
        for i in range(1,M):
            ans[i] = self.ctx.exp(delta[i])*fp[i]*(
                1 + 1j*theta[i]*(1 + self.cot_theta[i]**2) -
                1j*self.cot_theta[i])
        result = self.ctx.fraction(2,5)*self.ctx.fsum(ans)/self.t
        if not manual_prec:
            self.ctx.dps = self.dps_orig

        return result.real

class Stehfest(InverseLaplaceTransform):
    def calc_laplace_parameter(self,t,**kwargs):
        self.t = self.ctx.convert(t)
        if 'degree' in kwargs:
            self.degree = kwargs['degree']
            self.dps_goal = int(1.38*self.degree)
        else:
            self.dps_goal = int(2.93*self.ctx.dps)
            self.degree = max(16,self.dps_goal)
        if self.degree%2 > 0:
            self.degree += 1
        M = self.degree
        self.dps_orig = self.ctx.dps
        self.ctx.dps = self.dps_goal
        self.V = self._coeff()
        self.p = self.ctx.matrix(self.ctx.arange(1,M+1))*self.ctx.ln2/self.t

    def _coeff(self):
        self.t = self.ctx.convert(t)
        result = self.ctx.fdot(self.V,fp)*self.ctx.ln2/self.t
        if not manual_prec:
            self.ctx.dps = self.dps_orig
        return result.real

class deHoog(InverseLaplaceTransform):
    def calc_laplace_parameter(self,t,**kwargs):
        self.t = self.ctx.convert(t)
        self.tmax = kwargs.get('tmax',self.t)
        if 'degree' in kwargs:
            self.degree = kwargs['degree']
            self.dps_goal = int(1.38*self.degree)
        else:
            self.dps_goal = int(self.ctx.dps*1.36)
            self.degree = max(10,self.dps_goal)
        M = self.degree
        tmp = self.ctx.power(10.0,-self.dps_goal)
        self.alpha = self.ctx.convert(kwargs.get('alpha',tmp))
        self.tol = self.ctx.convert(kwargs.get('tol',self.alpha*10.0))
        self.np = 2*self.degree+1
        self.dps_orig = self.ctx.dps
        self.ctx.dps = self.dps_goal
        self.scale = kwargs.get('scale',2)
        self.T = self.ctx.convert(kwargs.get('T',self.scale*self.tmax))
        self.p = self.ctx.matrix(2*M+1,1)
        self.gamma = self.alpha - self.ctx.log(self.tol)/(self.scale*self.T)
        self.p = (self.gamma + self.mp.pi*self.ctx.matrix(self.ctx.arange(self.np))/self.T*1j)

    def calc_time_domain_solution(self,fp,t,manual_prec=False):
        M = self.degree
        np = self.np
        T = self.T
        self.t = self.ctx.convert(t)
        e = self.ctx.zeros(np,M+1)
        q = self.ctx.matrix(np,M)
        d = self.ctx.matrix(np,1)
        A = self.ctx.zeros(np+2,1)
        B = self.ctx.ones(np+2,1)
        q[0,0] = fp[1]/(fp[0]/2)
        for i in range(1,2*M):
            q[i,0] = fp[i+1]/fp[i]
        for r in range(1,M+1):
            mr = 2*(M-r)
            e[0:mr,r] = q[1:mr+1,r-1] - q[0:mr,r-1] + e[1:mr+1,r-1]
            if not r == M:
                rq = r+1
                mr = 2*(M-rq)+1
                for i in range(mr):
                    q[i,rq-1] = q[i+1,rq-2]*e[i+1,rq-1]/e[i,rq-1]
        d[0] = fp[0]/2
        for r in range(1,M+1):
            d[2*r-1] = -q[0,r-1] # even terms
            d[2*r]   = -e[0,r]   # odd terms
        A[1] = d[0]
        z = self.ctx.expjpi(self.t/T) # i*pi is already in fcn
        for i in range(1,2*M):
            A[i+1] = A[i] + d[i]*A[i-1]*z
            B[i+1] = B[i] + d[i]*B[i-1]*z
        brem  = (1 + (d[2*M-1] - d[2*M])*z)/2
        rem = brem*self.ctx.powm1(1 + d[2*M]*z/brem, self.ctx.fraction(1,2))
        A[np] = A[2*M] + rem*A[2*M-1]
        B[np] = B[2*M] + rem*B[2*M-1]
        result = self.ctx.exp(self.gamma*self.t)/T*(A[np]/B[np]).real
        if not manual_prec:
            self.ctx.dps = self.dps_orig
        return result

class LaplaceTransformInversionMethods(object):
    def __init__(ctx, *args, **kwargs):
        ctx._fixed_talbot = FixedTalbot(ctx)
        ctx._stehfest = Stehfest(ctx)
        ctx._de_hoog = deHoog(ctx)

    def invertlaplace(ctx, f, t, **kwargs):
        rule = kwargs.get('method','dehoog')
        if type(rule) is str:
            lrule = rule.lower()
            if lrule == 'talbot':
                rule = ctx._fixed_talbot
            elif lrule == 'stehfest':
                rule = ctx._stehfest
            elif lrule == 'dehoog':
                rule = ctx._de_hoog
            else:
                raise ValueError("unknown invlap algorithm: %s" % rule)
        else:
            rule = rule(ctx)
        rule.calc_laplace_parameter(t,**kwargs)
        fp = [f(p) for p in rule.p]
        return rule.calc_time_domain_solution(fp,t)

    def invlaptalbot(ctx, *args, **kwargs):
        kwargs['method'] = 'talbot'
        return ctx.invertlaplace(*args, **kwargs)

    def invlapstehfest(ctx, *args, **kwargs):
        kwargs['method'] = 'stehfest'
        return ctx.invertlaplace(*args, **kwargs)

    def invlapdehoog(ctx, *args, **kwargs):
        kwargs['method'] = 'dehoog'
        return ctx.invertlaplace(*args, **kwargs)


class _matrix(object):
    def __init__(self, *args, **kwargs):
        self.__data = {}
        self._LU = None
        convert = kwargs.get('force_type', self.ctx.convert)
        if not convert:
            convert = lambda x: x
        if isinstance(args[0], (list, tuple)):
            if isinstance(args[0][0], (list, tuple)):
                A = args[0]
                self.__rows = len(A)
                self.__cols = len(A[0])
                for i, row in enumerate(A):
                    for j, a in enumerate(row):
                        self[i, j] = convert(a)
            else:
                v = args[0]
                self.__rows = len(v)
                self.__cols = 1
                for i, e in enumerate(v):
                    self[i, 0] = e
        elif isinstance(args[0], int):
            if len(args) == 1:
                self.__rows = self.__cols = args[0]
            else:
                if not isinstance(args[1], int):
                    raise TypeError("expected int")
                self.__rows = args[0]
                self.__cols = args[1]
        elif isinstance(args[0], _matrix):
            A = args[0].copy()
            self.__data = A._matrix__data
            self.__rows = A._matrix__rows
            self.__cols = A._matrix__cols
            for i in range(A.__rows):
                for j in range(A.__cols):
                    A[i,j] = convert(A[i,j])
        elif hasattr(args[0], 'tolist'):
            A = self.ctx.matrix(args[0].tolist())
            self.__data = A._matrix__data
            self.__rows = A._matrix__rows
            self.__cols = A._matrix__cols
        else:
            raise TypeError('could not interpret given arguments')

    def apply(self, f):
        new = self.ctx.matrix(self.__rows, self.__cols)
        for i in range(self.__rows):
            for j in range(self.__cols):
                new[i,j] = f(self[i,j])
        return new

    def __nstr__(self, n=None, **kwargs):
        res = []
        maxlen = [0]*self.cols
        for i in range(self.rows):
            res.append([])
            for j in range(self.cols):
                if n:
                    string = self.ctx.nstr(self[i,j], n, **kwargs)
                else:
                    string = str(self[i,j])
                res[-1].append(string)
                maxlen[j] = max(len(string), maxlen[j])
        for i, row in enumerate(res):
            for j, elem in enumerate(row):
                row[j] = elem.rjust(maxlen[j])
            res[i] = "[" + colsep.join(row) + "]"
        return rowsep.join(res)

    def __str__(self):
        return self.__nstr__()

    def _toliststr(self, avoid_type=False):
        typ = self.ctx.mpf
        s = '['
        for i in range(self.__rows):
            s += '['
            for j in range(self.__cols):
                if not avoid_type or not isinstance(self[i,j], typ):
                    a = repr(self[i,j])
                else:
                    a = "'" + str(self[i,j]) + "'"
                s += a + ', '
            s = s[:-2]
            s += '],\n '
        s = s[:-3]
        s += ']'
        return s

    def tolist(self):
        return [[self[i,j] for j in range(self.__cols)] for i in range(self.__rows)]

    def __repr__(self):
        if self.ctx.pretty:
            return self.__str__()
        s = 'matrix(\n'
        s += self._toliststr(avoid_type=True) + ')'
        return s

    def __get_element(self, key):
        if key in self.__data:
            return self.__data[key]
        else:
            return self.ctx.zero

    def __set_element(self, key, value):
        if value: # only store non-zeros
            self.__data[key] = value
        elif key in self.__data:
            del self.__data[key]


    def __getitem__(self, key):
        if isinstance(key, int) or isinstance(key,slice):
            if self.__rows == 1:
                key = (0, key)
            elif self.__cols == 1:
                key = (key, 0)
            else:
                raise IndexError('insufficient indices for matrix')

        if isinstance(key[0],slice) or isinstance(key[1],slice):
            if isinstance(key[0],slice):
                if (key[0].start is None or key[0].start >= 0) and \
                    (key[0].stop is None or key[0].stop <= self.__rows+1):
                    rows = range(*key[0].indices(self.__rows))
                else:
                    raise IndexError('Row index out of bounds')
            else:
                rows = [key[0]]

            if isinstance(key[1],slice):
                if (key[1].start is None or key[1].start >= 0) and \
                    (key[1].stop is None or key[1].stop <= self.__cols+1):
                    columns = range(*key[1].indices(self.__cols))
                else:
                    raise IndexError('Column index out of bounds')
            else:
                columns = [key[1]]
            m = self.ctx.matrix(len(rows),len(columns))
            for i,x in enumerate(rows):
                for j,y in enumerate(columns):
                    m.__set_element((i,j),self.__get_element((x,y)))
            return m
        else:
            if key[0] >= self.__rows or key[1] >= self.__cols:
                raise IndexError('matrix index out of range')
            if key in self.__data:
                return self.__data[key]
            else:
                return self.ctx.zero

    def __setitem__(self, key, value):
        if isinstance(key, int) or isinstance(key,slice):
            if self.__rows == 1:
                key = (0, key)
            elif self.__cols == 1:
                key = (key, 0)
            else:
                raise IndexError('insufficient indices for matrix')
        if isinstance(key[0],slice) or isinstance(key[1],slice):
            if isinstance(key[0],slice):
                if (key[0].start is None or key[0].start >= 0) and \
                    (key[0].stop is None or key[0].stop <= self.__rows+1):
                    rows = range(*key[0].indices(self.__rows))
                else:
                    raise IndexError('Row index out of bounds')
            else:
                rows = [key[0]]
            if isinstance(key[1],slice):
                if (key[1].start is None or key[1].start >= 0) and \
                    (key[1].stop is None or key[1].stop <= self.__cols+1):
                    columns = range(*key[1].indices(self.__cols))
                else:
                    raise IndexError('Column index out of bounds')
            else:
                columns = [key[1]]
            if isinstance(value,self.ctx.matrix):
                if len(rows) == value.rows and len(columns) == value.cols:
                    for i,x in enumerate(rows):
                        for j,y in enumerate(columns):
                            self.__set_element((x,y), value.__get_element((i,j)))
                else:
                    raise ValueError('Dimensions do not match')
            else:
                value = self.ctx.convert(value)
                for i in rows:
                    for j in columns:
                        self.__set_element((i,j), value)
        else:
            if key[0] >= self.__rows or key[1] >= self.__cols:
                raise IndexError('matrix index out of range')
            value = self.ctx.convert(value)
            if value: # only store non-zeros
                self.__data[key] = value
            elif key in self.__data:
                del self.__data[key]

        if self._LU:
            self._LU = None
        return

    def __iter__(self):
        for i in range(self.__rows):
            for j in range(self.__cols):
                yield self[i,j]

    def __mul__(self, other):
        if isinstance(other, self.ctx.matrix):
            if self.__cols != other.__rows:
                raise ValueError('dimensions not compatible for multiplication')
            new = self.ctx.matrix(self.__rows, other.__cols)
            for i in range(self.__rows):
                for j in range(other.__cols):
                    new[i, j] = self.ctx.fdot((self[i,k], other[k,j])
                                     for k in range(other.__rows))
            return new
        else:
            new = self.ctx.matrix(self.__rows, self.__cols)
            for i in range(self.__rows):
                for j in range(self.__cols):
                    new[i, j] = other*self[i, j]
            return new

    def __rmul__(self, other):
        if isinstance(other, self.ctx.matrix):
            raise TypeError("other should not be type of ctx.matrix")
        return self.__mul__(other)

    def __pow__(self, other):
        if not isinstance(other, int):
            raise ValueError('only integer exponents are supported')
        if not self.__rows == self.__cols:
            raise ValueError('only powers of square matrices are defined')
        n = other
        if n == 0:
            return self.ctx.eye(self.__rows)
        if n < 0:
            n = -n
            neg = True
        else:
            neg = False
        i = n
        y = 1
        z = self.copy()
        while i != 0:
            if i % 2 == 1:
                y = y*z
            z = z*z
            i = i // 2
        if neg:
            y = self.ctx.inverse(y)
        return y

    def __div__(self, other):
        assert not isinstance(other, self.ctx.matrix)
        new = self.ctx.matrix(self.__rows, self.__cols)
        for i in range(self.__rows):
            for j in range(self.__cols):
                new[i,j] = self[i,j] / other
        return new

    __truediv__ = __div__

    def __add__(self, other):
        if isinstance(other, self.ctx.matrix):
            if not (self.__rows == other.__rows and self.__cols == other.__cols):
                raise ValueError('incompatible dimensions for addition')
            new = self.ctx.matrix(self.__rows, self.__cols)
            for i in range(self.__rows):
                for j in range(self.__cols):
                    new[i,j] = self[i,j] + other[i,j]
            return new
        else:
            new = self.ctx.matrix(self.__rows, self.__cols)
            for i in range(self.__rows):
                for j in range(self.__cols):
                    new[i,j] += self[i,j] + other
            return new

    def __radd__(self, other):
        return self.__add__(other)

    def __sub__(self, other):
        if isinstance(other, self.ctx.matrix) and not (self.__rows == other.__rows
                                              and self.__cols == other.__cols):
            raise ValueError('incompatible dimensions for substraction')
        return self.__add__(other*(-1))

    def __neg__(self):
        return (-1)*self

    def __rsub__(self, other):
        return -self + other

    def __eq__(self, other):
        return self.__rows == other.__rows and self.__cols == other.__cols \
               and self.__data == other.__data

    def __len__(self):
        if self.rows == 1:
            return self.cols
        elif self.cols == 1:
            return self.rows
        else:
            return self.rows

    def __getrows(self):
        return self.__rows

    def __setrows(self, value):
        for key in self.__data.copy():
            if key[0] >= value:
                del self.__data[key]
        self.__rows = value
    rows = property(__getrows, __setrows, doc='number of rows')

    def __getcols(self):
        return self.__cols

    def __setcols(self, value):
        for key in self.__data.copy():
            if key[1] >= value:
                del self.__data[key]
        self.__cols = value
    cols = property(__getcols, __setcols, doc='number of columns')

    def transpose(self):
        new = self.ctx.matrix(self.__cols, self.__rows)
        for i in range(self.__rows):
            for j in range(self.__cols):
                new[j,i] = self[i,j]
        return new
    T = property(transpose)

    def conjugate(self):
        return self.apply(self.ctx.conj)

    def transpose_conj(self):
        return self.conjugate().transpose()
    H = property(transpose_conj)

    def copy(self):
        new = self.ctx.matrix(self.__rows, self.__cols)
        new.__data = self.__data.copy()
        return new
    __copy__ = copy

    def column(self, n):
        m = self.ctx.matrix(self.rows, 1)
        for i in range(self.rows):
            m[i] = self[i,n]
        return m

new = object.__new__

def create_reduced(p, q, _cache={}):
    key = p, q
    if key in _cache:
        return _cache[key]
    x, y = p, q
    while y:
        x, y = y, x % y
    if x != 1:
        p //= x
        q //= x
    v = new(mpq)
    v._mpq_ = p, q
    if q <= 4 and abs(key[0]) < 100:
        _cache[key] = v
    return v

class MatrixMethods(object):
    def __init__(ctx):
        ctx.matrix = type('matrix', (_matrix,), {})
        ctx.matrix.ctx = ctx
        ctx.matrix.convert = ctx.convert

    def eye(ctx, n, **kwargs):
        A = ctx.matrix(n, **kwargs)
        for i in range(n):
            A[i,i] = 1
        return A

    def diag(ctx, diagonal, **kwargs):
        A = ctx.matrix(len(diagonal), **kwargs)
        for i in range(len(diagonal)):
            A[i,i] = diagonal[i]
        return A

    def zeros(ctx, *args, **kwargs):
        if len(args) == 1:
            m = n = args[0]
        elif len(args) == 2:
            m = args[0]
            n = args[1]
        else:
            raise TypeError('zeros expected at most 2 arguments, got %i' % len(args))
        A = ctx.matrix(m, n, **kwargs)
        for i in range(m):
            for j in range(n):
                A[i,j] = 0
        return A

    def ones(ctx, *args, **kwargs):
        if len(args) == 1:
            m = n = args[0]
        elif len(args) == 2:
            m = args[0]
            n = args[1]
        else:
            raise TypeError('ones expected at most 2 arguments, got %i' % len(args))
        A = ctx.matrix(m, n, **kwargs)
        for i in range(m):
            for j in range(n):
                A[i,j] = 1
        return A

    def hilbert(ctx, m, n=None):
        if n is None:
            n = m
        A = ctx.matrix(m, n)
        for i in range(m):
            for j in range(n):
                A[i,j] = ctx.one / (i + j + 1)
        return A

    def randmatrix(ctx, m, n=None, min=0, max=1, **kwargs):
        if not n:
            n = m
        A = ctx.matrix(m, n, **kwargs)
        for i in range(m):
            for j in range(n):
                A[i,j] = ctx.rand()*(max - min) + min
        return A

    def swap_row(ctx, A, i, j):
        if i == j:
            return
        if isinstance(A, ctx.matrix):
            for k in range(A.cols):
                A[i,k], A[j,k] = A[j,k], A[i,k]
        elif isinstance(A, list):
            A[i], A[j] = A[j], A[i]
        else:
            raise TypeError('could not interpret type')

    def extend(ctx, A, b):
        if not isinstance(A, ctx.matrix):
            raise TypeError("A should be a type of ctx.matrix")
        if A.rows != len(b):
            raise ValueError("Value should be equal to len(b)")
        A = A.copy()
        A.cols += 1
        for i in range(A.rows):
            A[i, A.cols-1] = b[i]
        return A

    def norm(ctx, x, p=2):
        try:
            iter(x)
        except TypeError:
            return ctx.absmax(x)
        if type(p) is not int:
            p = (p)
        if p == ctx.inf:
            return max(ctx.absmax(i) for i in x)
        elif p == 1:
            return ctx.fsum(x, absolute=1)
        elif p == 2:
            return ctx.sqrt(ctx.fsum(x, absolute=1, squared=1))
        elif p > 1:
            return ctx.nthroot(ctx.fsum(abs(i)**p for i in x), p)
        else:
            raise ValueError('p has to be >= 1')

    def mnorm(ctx, A, p=1):
        A = ctx.matrix(A)
        if type(p) is not int:
            if type(p) is str and 'frobenius'.startswith(p.lower()):
                return ctx.norm(A, 2)
            p = ctx.convert(p)
        m, n = A.rows, A.cols
        if p == 1:
            return max(ctx.fsum((A[i,j] for i in range(m)), absolute=1) for j in range(n))
        elif p == ctx.inf:
            return max(ctx.fsum((A[i,j] for j in range(n)), absolute=1) for i in range(m))
        else:
            raise NotImplementedError("matrix p-norm for arbitrary p")

class MatrixCalculusMethods(object):
    def _exp_pade(ctx, a):
        def eps_pade(p):
            return ctx.mpf(2)**(3-2*p)*\
                ctx.factorial(p)**2/(ctx.factorial(2*p)**2*(2*p + 1))
        q = 4
        extraq = 8
        while 1:
            if eps_pade(q) < mp.eps:
                break
            q += 1
        q += extraq
        j = int(max(1, ctx.mag(ctx.mnorm(a,'inf'))))
        extra = q
        prec = ctx.prec
        ctx.dps += extra + 3
        try:
            a = a/2**j
            na = a.rows
            den = ctx.eye(na)
            num = ctx.eye(na)
            x = ctx.eye(na)
            c = ctx.mpf(1)
            for k in range(1, q+1):
                c *= ctx.mpf(q - k + 1)/((2*q - k + 1)*k)
                x = a*x
                cx = c*x
                num += cx
                den += (-1)**k*cx
            f = ctx.lu_solve_mat(den, num)
            for k in range(j):
                f = f*f
        finally:
            ctx.prec = prec
        return f*1

    def expm(ctx, A, method='taylor'):
        if method == 'pade':
            prec = ctx.prec
            try:
                A = ctx.matrix(A)
                ctx.prec += 2*A.rows
                res = ctx._exp_pade(A)
            finally:
                ctx.prec = prec
            return res
        A = ctx.matrix(A)
        prec = ctx.prec
        j = int(max(1, ctx.mag(ctx.mnorm(A,'inf'))))
        j += int(0.5*prec**0.5)
        try:
            ctx.prec += 10 + 2*j
            tol = +mp.eps
            A = A/2**j
            T = A
            Y = A**0 + A
            k = 2
            while 1:
                T *= A*(1/ctx.mpf(k))
                if ctx.mnorm(T, 'inf') < tol:
                    break
                Y += T
                k += 1
            for k in range(j):
                Y = Y*Y
        finally:
            ctx.prec = prec
        Y *= 1
        return Y

    def cosm(ctx, A):
        B = 0.5*(ctx.expm(A*ctx.j) + ctx.expm(A*(-ctx.j)))
        if not sum(A.apply(ctx.im).apply(abs)):
            B = B.apply(ctx.re)
        return B

    def sinm(ctx, A):
        B = (-0.5j)*(ctx.expm(A*ctx.j) - ctx.expm(A*(-ctx.j)))
        if not sum(A.apply(ctx.im).apply(abs)):
            B = B.apply(ctx.re)
        return B

    def _sqrtm_rot(ctx, A, _may_rotate):
        u = ctx.j**0.3
        return ctx.sqrtm(u*A, _may_rotate) / ctx.sqrt(u)

    def sqrtm(ctx, A, _may_rotate=2):
        A = ctx.matrix(A)
        if A*0 == A:
            return A
        prec = ctx.prec
        if _may_rotate:
            d = ctx.det(A)
            if abs(ctx.im(d)) < 16*mp.eps and ctx.re(d) < 0:
                return ctx._sqrtm_rot(A, _may_rotate-1)
        try:
            ctx.prec += 10
            tol = mp.eps*128
            Y = A
            Z = I = A**0
            k = 0
            while 1:
                Yprev = Y
                try:
                    Y, Z = 0.5*(Y+ctx.inverse(Z)), 0.5*(Z+ctx.inverse(Y))
                except ZeroDivisionError:
                    if _may_rotate:
                        Y = ctx._sqrtm_rot(A, _may_rotate-1)
                        break
                    else:
                        raise
                mag1 = ctx.mnorm(Y-Yprev, 'inf')
                mag2 = ctx.mnorm(Y, 'inf')
                if mag1 <= mag2*tol:
                    break
                if _may_rotate and k > 6 and not mag1 < mag2*0.001:
                    return ctx._sqrtm_rot(A, _may_rotate-1)
                k += 1
                if k > ctx.prec:
                    raise ctx.NoConvergence
        finally:
            ctx.prec = prec
        Y *= 1
        return Y

    def logm(ctx, A):
        A = ctx.matrix(A)
        prec = ctx.prec
        try:
            ctx.prec += 10
            tol = mp.eps*128
            I = A**0
            B = A
            n = 0
            while 1:
                B = ctx.sqrtm(B)
                n += 1
                if ctx.mnorm(B-I, 'inf') < 0.125:
                    break
            T = X = B-I
            L = X*0
            k = 1
            while 1:
                if k & 1:
                    L += T / k
                else:
                    L -= T / k
                T *= X
                if ctx.mnorm(T, 'inf') < tol:
                    break
                k += 1
                if k > ctx.prec:
                    raise ctx.NoConvergence
        finally:
            ctx.prec = prec
        L *= 2**n
        return L

    def powm(ctx, A, r):
        A = ctx.matrix(A)
        r = ctx.convert(r)
        prec = ctx.prec
        try:
            ctx.prec += 10
            if ctx.isint(r):
                v = A ** int(r)
            elif ctx.isint(r*2):
                y = int(r*2)
                v = ctx.sqrtm(A) ** y
            else:
                v = ctx.expm(r*ctx.logm(A))
        finally:
            ctx.prec = prec
        v *= 1
        return v

class StandardBaseContext(Context,
    SpecialFunctions,
    RSCache,
    QuadratureMethods,
    LaplaceTransformInversionMethods,
    CalculusMethods,
    MatrixMethods,
    MatrixCalculusMethods,
    LinearAlgebraMethods,
    Eigen,
    IdentificationMethods,
    OptimizationMethods,
    ODEMethods,
    VisualizationMethods):
    def __init__(ctx):
        ctx._aliases = {}
        SpecialFunctions.__init__(ctx)
        RSCache.__init__(ctx)
        QuadratureMethods.__init__(ctx)
        LaplaceTransformInversionMethods.__init__(ctx)
        CalculusMethods.__init__(ctx)
        MatrixMethods.__init__(ctx)

    def _init_aliases(ctx):
        for alias, value in ctx._aliases.items():
            try:
                setattr(ctx, alias, getattr(ctx, value))
            except AttributeError:
                pass

    _fixed_precision = False
    verbose = False
    def warn(ctx, msg):
        print("Warning:", msg)

    def bad_domain(ctx, msg):
        raise ValueError(msg)

    def _re(ctx, x):
        if hasattr(x, "real"):
            return x.real
        return x

    def _im(ctx, x):
        if hasattr(x, "imag"):
            return x.imag
        return ctx.zero

    def _as_points(ctx, x):
        return x

    def fneg(ctx, x, **kwargs):
        return -ctx.convert(x)

    def fadd(ctx, x, y, **kwargs):
        return ctx.convert(x)+ctx.convert(y)

    def fsub(ctx, x, y, **kwargs):
        return ctx.convert(x)-ctx.convert(y)

    def fmul(ctx, x, y, **kwargs):
        return ctx.convert(x)*ctx.convert(y)

    def fdiv(ctx, x, y, **kwargs):
        return ctx.convert(x)/ctx.convert(y)

    def fsum(ctx, args, absolute=False, squared=False):
        if absolute:
            if squared:
                return sum((abs(x)**2 for x in args), ctx.zero)
            return sum((abs(x) for x in args), ctx.zero)
        if squared:
            return sum((x**2 for x in args), ctx.zero)
        return sum(args, ctx.zero)

    def fdot(ctx, xs, ys=None, conjugate=False):
        if ys is not None:
            xs = zip(xs, ys)
        if conjugate:
            cf = ctx.conj
            return sum((x*cf(y) for (x,y) in xs), ctx.zero)
        else:
            return sum((x*y for (x,y) in xs), ctx.zero)

    def fprod(ctx, args):
        prod = ctx.one
        for arg in args:
            prod *= arg
        return prod

    def nprint(ctx, x, n=6, **kwargs):
        print(ctx.nstr(x, n, **kwargs))

    def chop(ctx, x, tol=None):
        if tol is None:
            tol = 100*mp.eps
        try:
            x = ctx.convert(x)
            absx = abs(x)
            if abs(x) < tol:
                return ctx.zero
            if ctx._is_complex_type(x):
                part_tol = max(tol, absx*tol)
                if abs(x.imag) < part_tol:
                    return x.real
                if abs(x.real) < part_tol:
                    return ctx.mpc(0, x.imag)
        except TypeError:
            if isinstance(x, ctx.matrix):
                return x.apply(lambda a: ctx.chop(a, tol))
            if hasattr(x, "__iter__"):
                return [ctx.chop(a, tol) for a in x]
        return x

    def almosteq(ctx, s, t, rel_eps=None, abs_eps=None):
        t = ctx.convert(t)
        if abs_eps is None and rel_eps is None:
            rel_eps = abs_eps = ctx.ldexp(1, -ctx.prec+4)
        if abs_eps is None:
            abs_eps = rel_eps
        elif rel_eps is None:
            rel_eps = abs_eps
        diff = abs(s-t)
        if diff <= abs_eps:
            return True
        abss = abs(s)
        abst = abs(t)
        if abss < abst:
            err = diff/abst
        else:
            err = diff/abss
        return err <= rel_eps

    def arange(ctx, *args):
        if not len(args) <= 3:
            raise TypeError('arange expected at most 3 arguments, got %i'
                            % len(args))
        if not len(args) >= 1:
            raise TypeError('arange expected at least 1 argument, got %i'
                            % len(args))
        a = 0
        dt = 1
        if len(args) == 1:
            b = args[0]
        elif len(args) >= 2:
            a = args[0]
            b = args[1]
        if len(args) == 3:
            dt = args[2]
        a, b, dt = ctx.mpf(a), ctx.mpf(b), ctx.mpf(dt)
        assert a + dt != a, 'dt is too small and would cause an infinite loop'
        if a > b:
            if dt > 0:
                return []
            op = gt
        else:
            if dt < 0:
                return []
            op = lt
        result = []
        i = 0
        t = a
        while 1:
            t = a + dt*i
            i += 1
            if op(t, b):
                result.append(t)
            else:
                break
        return result

    def linspace(ctx, *args, **kwargs):
        if len(args) == 3:
            a = ctx.mpf(args[0])
            b = ctx.mpf(args[1])
            n = int(args[2])
        elif len(args) == 2:
            assert hasattr(args[0], '_mpi_')
            a = args[0].a
            b = args[0].b
            n = int(args[1])
        else:
            raise TypeError('linspace expected 2 or 3 arguments, got %i' \
                            % len(args))
        if n < 1:
            raise ValueError('n must be greater than 0')
        if not 'endpoint' in kwargs or kwargs['endpoint']:
            if n == 1:
                return [ctx.mpf(a)]
            step = (b - a) / ctx.mpf(n - 1)
            y = [i*step + a for i in range(n)]
            y[-1] = b
        else:
            step = (b - a) / ctx.mpf(n)
            y = [i*step + a for i in range(n)]
        return y

    def cos_sin(ctx, z, **kwargs):
        return ctx.cos(z, **kwargs), ctx.sin(z, **kwargs)

    def cospi_sinpi(ctx, z, **kwargs):
        return ctx.cospi(z, **kwargs), ctx.sinpi(z, **kwargs)

    def _default_hyper_maxprec(ctx, p):
        return int(1000*p**0.25 + 4*p)

    _gcd = staticmethod(gcd)
    list_primes = staticmethod(list_primes)
    isprime = staticmethod(isprime)
    bernfrac = staticmethod(bernfrac)
    moebius = staticmethod(moebius)
    _ifac = staticmethod(ifac)
    _eulernum = staticmethod(eulernum)
    _stirling1 = staticmethod(stirling1)
    _stirling2 = staticmethod(stirling2)

    def sum_accurately(ctx, terms, check_step=1):
        prec = ctx.prec
        try:
            extraprec = 10
            while 1:
                ctx.prec = prec + extraprec + 5
                max_mag = ctx.ninf
                s = ctx.zero
                k = 0
                for term in terms():
                    s += term
                    if (not k % check_step) and term:
                        term_mag = ctx.mag(term)
                        max_mag = max(max_mag, term_mag)
                        sum_mag = ctx.mag(s)
                        if sum_mag - term_mag > ctx.prec:
                            break
                    k += 1
                cancellation = max_mag - sum_mag
                if cancellation != cancellation:
                    break
                if cancellation < extraprec or ctx._fixed_precision:
                    break
                extraprec += min(ctx.prec, cancellation)
            return s
        finally:
            ctx.prec = prec

    def mul_accurately(ctx, factors, check_step=1):
        prec = ctx.prec
        try:
            extraprec = 10
            while 1:
                ctx.prec = prec + extraprec + 5
                max_mag = ctx.ninf
                one = ctx.one
                s = one
                k = 0
                for factor in factors():
                    s *= factor
                    term = factor - one
                    if (not k % check_step):
                        term_mag = ctx.mag(term)
                        max_mag = max(max_mag, term_mag)
                        sum_mag = ctx.mag(s-one)
                        if -term_mag > ctx.prec:
                            break
                    k += 1
                cancellation = max_mag - sum_mag
                if cancellation != cancellation:
                    break
                if cancellation < extraprec or ctx._fixed_precision:
                    break
                extraprec += min(ctx.prec, cancellation)
            return s
        finally:
            ctx.prec = prec

    def power(ctx, x, y):
        return ctx.convert(x)**ctx.convert(y)

    def _zeta_int(ctx, n):
        return ctx.zeta(n)

    def maxcalls(ctx, f, N):
        counter = [0]
        def f_maxcalls_wrapped(*args, **kwargs):
            counter[0] += 1
            if counter[0] > N:
                raise ctx.NoConvergence("maxcalls: function evaluated %i times" % N)
            return f(*args, **kwargs)
        return f_maxcalls_wrapped


def loggamma(x):
    if type(x) not in (float, complex):
        try:
            x = float(x)
        except (ValueError, TypeError):
            x = complex(x)
    try:
        xreal = x.real
        ximag = x.imag
    except AttributeError:   # py2.5
        xreal = x
        ximag = 0.0
    if xreal < 0.0:
        if abs(x) < 0.5:
            v = log(gamma(x))
            if ximag == 0:
                v = v.conjugate()
            return v
        z = 1-x
        try:
            re = z.real
            im = z.imag
        except AttributeError:   # py2.5
            re = z
            im = 0.0
        refloor = floor(re)
        if im == 0.0:
            imsign = 0
        elif im < 0.0:
            imsign = -1
        else:
            imsign = 1
        return (-pi*1j)*abs(refloor)*(1-abs(imsign)) + logpi - \
            log(sinpi(z-refloor)) - loggamma(z) + 1j*pi*refloor*imsign
    if x == 1.0 or x == 2.0:
        return x*0
    p = 0.
    while abs(x) < 11:
        p -= log(x)
        x += 1.0
    s = 0.918938533204672742 + (x-0.5)*log(x) - x
    r = 1./x
    r2 = r*r
    s += 0.083333333333333333333*r; r *= r2
    s += -0.0027777777777777777778*r; r *= r2
    s += 0.00079365079365079365079*r; r *= r2
    s += -0.0005952380952380952381*r; r *= r2
    s += 0.00084175084175084175084*r; r *= r2
    s += -0.0019175269175269175269*r; r *= r2
    s += 0.0064102564102564102564*r; r *= r2
    s += -0.02955065359477124183*r
    return s + p

class FPContext(StandardBaseContext):
    def __init__(ctx):
        StandardBaseContext.__init__(ctx)
        #ctx.loggamma = loggamma
        ctx._bernoulli_cache = {}
        ctx.pretty = False
        ctx._init_aliases()
    _mpq = lambda cls, x: float(x[0])/x[1]
    NoConvergence = NoConvergence
    def _get_prec(ctx): return 53
    def _set_prec(ctx, p): return
    def _get_dps(ctx): return 15
    def _set_dps(ctx, p): return
    _fixed_precision = True
    prec = property(_get_prec, _set_prec)
    dps = property(_get_dps, _set_dps)
    zero = 0.0
    one = 1.0
    eps = EPS
    inf = INF
    ninf = NINF
    nan = NAN
    j = 1j
    @classmethod
    def _wrap_specfun(cls, name, f, wrap):
        if wrap:
            def f_wrapped(ctx, *args, **kwargs):
                convert = ctx.convert
                args = [convert(a) for a in args]
                return f(ctx, *args, **kwargs)
        else:
            f_wrapped = f
        setattr(cls, name, f_wrapped)

    def bernoulli(ctx, n):
        cache = ctx._bernoulli_cache
        if n in cache:
            return cache[n]
        cache[n] = to_float(mpf_bernoulli(n, 53, 'n'), strict=True)
        return cache[n]
    pi = pi
    e = e
    euler = euler
    sqrt2 = 1.4142135623730950488
    sqrt5 = 2.2360679774997896964
    phi = 1.6180339887498948482
    ln2 = 0.69314718055994530942
    ln10 = 2.302585092994045684
    euler = 0.57721566490153286061
    catalan = 0.91596559417721901505
    khinchin = 2.6854520010653064453
    apery = 1.2020569031595942854
    glaisher = 1.2824271291006226369
    absmin = absmax = abs

    def is_special(ctx, x):
        return x - x != 0.0

    def isnan(ctx, x):
        return x != x

    def isinf(ctx, x):
        return abs(x) == math.INF

    def isnormal(ctx, x):
        if x:
            return x - x == 0.0
        return False

    def isnpint(ctx, x):
        if type(x) is complex:
            if x.imag:
                return False
            x = x.real
        return x <= 0.0 and round(x) == x

    mpf = float
    mpc = complex

    def convert(ctx, x):
        try:
            return float(x)
        except:
            return complex(x)
    power = staticmethod(math.pow)
    sqrt = staticmethod(math.sqrt)
    exp = staticmethod(math.exp)
    ln = log = staticmethod(math.log)
    cos = staticmethod(math.cos)
    sin = staticmethod(math.sin)
    tan = staticmethod(math.tan)
    #cos_sin = staticmethod(cos_sin)
    acos = staticmethod(math.acos)
    asin = staticmethod(math.asin)
    atan = staticmethod(math.atan)
    cosh = staticmethod(math.cosh)
    sinh = staticmethod(math.sinh)
    tanh = staticmethod(math.tanh)
    gamma = staticmethod(math.gamma)
    #rgamma = staticmethod(rgamma)
    fac = factorial = staticmethod(math.factorial)
    floor = staticmethod(math.floor)
    ceil = staticmethod(math.ceil)
    #cospi = staticmethod(math.cospi)
    #sinpi = staticmethod(math.sinpi)
    #cbrt = staticmethod(cbrt)
    #_nthroot = staticmethod(math.nthroot)
    #_ei = staticmethod(math.ei)
    #_e1 = staticmethod(math.e1)
    #_zeta = _zeta_int = staticmethod(math.zeta)

    def arg(ctx, z):
        z = complex(z)
        return math.atan2(z.imag, z.real)

    def expj(ctx, x):
        return ctx.exp(ctx.j*x)

    def expjpi(ctx, x):
        return ctx.exp(ctx.j*ctx.pi*x)

    ldexp = math.ldexp
    frexp = math.frexp

    def mag(ctx, z):
        if z:
            return ctx.frexp(abs(z))[1]
        return ctx.ninf

    def isint(ctx, z):
        if hasattr(z, "imag"):   # float/int don't have .real/.imag in py2.5
            if z.imag:
                return False
            z = z.real
        try:
            return z == int(z)
        except:
            return False

    def nint_distance(ctx, z):
        if hasattr(z, "imag"):   # float/int don't have .real/.imag in py2.5
            n = round(z.real)
        else:
            n = round(z)
        if n == z:
            return n, ctx.ninf
        return n, ctx.mag(abs(z-n))

    def _convert_param(ctx, z):
        if type(z) is tuple:
            p, q = z
            return ctx.mpf(p) / q, 'R'
        if hasattr(z, "imag"):    # float/int don't have .real/.imag in py2.5
            intz = int(z.real)
        else:
            intz = int(z)
        if z == intz:
            return intz, 'Z'
        return z, 'R'

    def _is_real_type(ctx, z):
        return isinstance(z, float) or isinstance(z, int_types)

    def _is_complex_type(ctx, z):
        return isinstance(z, complex)

    def hypsum(ctx, p, q, types, coeffs, z, maxterms=6000, **kwargs):
        coeffs = list(coeffs)
        num = range(p)
        den = range(p,p+q)
        tol = ctx.eps
        s = t = 1.0
        k = 0
        while 1:
            for i in num: t *= (coeffs[i]+k)
            for i in den: t /= (coeffs[i]+k)
            k += 1; t /= k; t *= z; s += t
            if abs(t) < tol:
                return s
            if k > maxterms:
                raise ctx.NoConvergence

    def atan2(ctx, x, y):
        return math.atan2(x, y)

    def psi(ctx, m, z):
        m = int(m)
        if m == 0:
            return ctx.digamma(z)
        return (-1)**(m+1) * ctx.fac(m) * ctx.zeta(m+1, z)

    #digamma = staticmethod(math.digamma)

    def harmonic(ctx, x):
        x = ctx.convert(x)
        if x == 0 or x == 1:
            return x
        return ctx.digamma(x+1) + ctx.euler

    nstr = str

    def to_fixed(ctx, x, prec):
        return int(math.ldexp(x, prec))

    def rand(ctx):
        import random
        return random.random()

    _erf = staticmethod(math.erf)
    _erfc = staticmethod(math.erfc)

    def sum_accurately(ctx, terms, check_step=1):
        s = ctx.zero
        k = 0
        for term in terms():
            s += term
            if (not k % check_step) and term:
                if abs(term) <= 1e-18*abs(s):
                    break
            k += 1
        return s

COS_SIN_CACHE_STEP = 8
cos_sin_cache = {}

MAX_LOG_INT_CACHE = 2000
log_int_cache = {}

LOG_TAYLOR_PREC = 2500  # Use Taylor series with caching up to this prec
LOG_TAYLOR_SHIFT = 9    # Cache log values in steps of size 2^-N
log_taylor_cache = {}
LOG_AGM_MAG_PREC_RATIO = 20

ATAN_TAYLOR_PREC = 3000  # Same as for log
ATAN_TAYLOR_SHIFT = 7   # steps of size 2^-N
atan_taylor_cache = {}

cache_prec_steps = [22,22]
for k in range(1, bitcount(LOG_TAYLOR_PREC)+1):
    cache_prec_steps += [min(2**k,LOG_TAYLOR_PREC)+20]*2**(k-1)

def constant_memo(f):
    f.memo_prec = -1
    f.memo_val = None
    def g(prec, **kwargs):
        memo_prec = f.memo_prec
        if prec <= memo_prec:
            return f.memo_val >> (memo_prec-prec)
        newprec = int(prec*1.05+10)
        f.memo_val = f(newprec, **kwargs)
        f.memo_prec = newprec
        return f.memo_val >> (newprec-prec)
    g.__name__ = f.__name__
    return g

def def_mpf_constant(fixed):
    def f(prec, rnd=round_fast):
        wp = prec + 20
        v = fixed(wp)
        if rnd in (round_up, round_ceiling):
            v += 1
        return normalize(0, v, -wp, bitcount(v), prec, rnd)
    return f

@constant_memo
def catalan_fixed(prec):
    prec = prec + 20
    a = one = MPZ_ONE << prec
    s, t, n = 0, 1, 1
    while t:
        a *= 32*n**3*(2*n-1)
        a //= (3-16*n+16*n**2)**2
        t = a*(-1)**(n-1)*(40*n**2-24*n+3) // (n**3*(2*n-1))
        s += t
        n += 1
    return s >> (20 + 6)

@constant_memo
def khinchin_fixed(prec):
    wp = int(prec + prec**0.5 + 15)
    s = MPZ_ZERO
    fac = from_int(4)
    t = ONE = MPZ_ONE << wp
    pi = mpf_pi(wp)
    pipow = twopi2 = mpf_shift(mpf_mul(pi, pi, wp), 2)
    n = 1
    while 1:
        zeta2n = mpf_abs(mpf_bernoulli(2*n, wp))
        zeta2n = mpf_mul(zeta2n, pipow, wp)
        zeta2n = mpf_div(zeta2n, fac, wp)
        zeta2n = to_fixed(zeta2n, wp)
        term = (((zeta2n - ONE)*t) // n) >> wp
        if term < 100:
            break
        s += term
        t += ONE//(2*n+1) - ONE//(2*n)
        n += 1
        fac = mpf_mul_int(fac, (2*n)*(2*n-1), wp)
        pipow = mpf_mul(pipow, twopi2, wp)
    s = (s << wp) // ln2_fixed(wp)
    K = mpf_exp(from_man_exp(s, -wp), wp)
    K = to_fixed(K, prec)
    return K

@constant_memo
def glaisher_fixed(prec):
    wp = prec + 30
    N = int(0.33*prec + 5)
    ONE = MPZ_ONE << wp
    s = MPZ_ZERO
    for k in range(2, N):
        s += log_int_fixed(k, wp) // k**2
    logN = log_int_fixed(N, wp)
    s += (ONE + logN) // N
    s += logN // (N**2*2)
    pN = N**3
    a = 1
    b = -2
    j = 3
    fac = from_int(2)
    k = 1
    while 1:
        D = ((a << wp) + b*logN) // pN
        D = from_man_exp(D, -wp)
        B = mpf_bernoulli(2*k, wp)
        term = mpf_mul(B, D, wp)
        term = mpf_div(term, fac, wp)
        term = to_fixed(term, wp)
        if abs(term) < 100:
            break
        s -= term
        a, b, pN, j = b-a*j, -j*b, pN*N, j+1
        a, b, pN, j = b-a*j, -j*b, pN*N, j+1
        k += 1
        fac = mpf_mul_int(fac, (2*k)*(2*k-1), wp)
    pi = pi_fixed(wp)
    s *= 6
    s = (s << wp) // (pi**2 >> wp)
    s += euler_fixed(wp)
    s += to_fixed(mpf_log(from_man_exp(2*pi, -wp), wp), wp)
    s //= 12
    A = mpf_exp(from_man_exp(s, -wp), wp)
    return to_fixed(A, prec)


@constant_memo
def apery_fixed(prec):
    prec += 20
    d = MPZ_ONE << prec
    term = MPZ(77) << prec
    n = 1
    s = MPZ_ZERO
    while term:
        s += term
        d *= (n**10)
        d //= (((2*n+1)**5)*(2*n)**5)
        term = (-1)**n*(205*(n**2) + 250*n + 77)*d
        n += 1
    return s >> (20 + 6)

@constant_memo
def euler_fixed(prec):
    extra = 30
    prec += extra
    p = int(math.log((prec/4)*math.log(2), 2)) + 1
    n = 2**p
    A = U = -p*ln2_fixed(prec)
    B = V = MPZ_ONE << prec
    k = 1
    while 1:
        B = B*n**2//k**2
        A = (A*n**2//k + B)//k
        U += A
        V += B
        if max(abs(A), abs(B)) < 100:
            break
        k += 1
    return (U<<(prec-extra))//V

@constant_memo
def mertens_fixed(prec):
    wp = prec + 20
    m = 2
    s = mpf_euler(wp)
    while 1:
        t = mpf_zeta_int(m, wp)
        if t == fone:
            break
        t = mpf_log(t, wp)
        t = mpf_mul_int(t, moebius(m), wp)
        t = mpf_div(t, from_int(m), wp)
        s = mpf_add(s, t)
        m += 1
    return to_fixed(s, prec)

@constant_memo
def twinprime_fixed(prec):
    def I(n):
        return sum(moebius(d)<<(n//d) for d in range(1,n+1) if not n%d)//n
    wp = 2*prec + 30
    res = fone
    primes = [from_rational(1,p,wp) for p in [2,3,5,7]]
    ppowers = [mpf_mul(p,p,wp) for p in primes]
    n = 2
    while 1:
        a = mpf_zeta_int(n, wp)
        for i in range(4):
            a = mpf_mul(a, mpf_sub(fone, ppowers[i]), wp)
            ppowers[i] = mpf_mul(ppowers[i], primes[i], wp)
        a = mpf_pow_int(a, -I(n), wp)
        if mpf_pos(a, prec+10, 'n') == fone:
            break
        res = mpf_mul(res, a, wp)
        n += 1
    res = mpf_mul(res, from_int(3*15*35), wp)
    res = mpf_div(res, from_int(4*16*36), wp)
    return to_fixed(res, prec)


def bernoulli_size(n):

    lgn = math.log(n,2)
    return int(2.326 + 0.5*lgn + n*(lgn - 4.094))

int_cache = dict((n, from_man_exp(n, 0)) for n in range(-10, 257))


def from_int(n, prec=0, rnd=round_fast):
    if not prec:
        if n in int_cache:
            return int_cache[n]
    return from_man_exp(n, 0, prec, rnd)

def to_man_exp(s):
    sign, man, exp, bc = s
    if (not man) and exp:
        raise ValueError("mantissa and exponent are undefined for %s" % man)
    return man, exp

def to_int(s, rnd=None):
    sign, man, exp, bc = s
    if (not man) and exp:
        raise ValueError("cannot convert inf or nan to int")
    if exp >= 0:
        if sign:
            return (-man) << exp
        return man << exp
    if not rnd:
        if sign:
            return -(man >> (-exp))
        else:
            return man >> (-exp)
    if sign:
        return round_int(-man, -exp, rnd)
    else:
        return round_int(man, -exp, rnd)

def mpf_round_int(s, rnd):
    sign, man, exp, bc = s
    if (not man) and exp:
        return s
    if exp >= 0:
        return s
    mag = exp+bc
    if mag < 1:
        if rnd == round_ceiling:
            if sign: return fzero
            else:    return fone
        elif rnd == round_floor:
            if sign: return fnone
            else:    return fzero
        elif rnd == round_nearest:
            if mag < 0 or man == MPZ_ONE: return fzero
            elif sign: return fnone
            else:      return fone
        else:
            raise NotImplementedError
    return mpf_pos(s, min(bc, mag), rnd)

def mpf_floor(s, prec=0, rnd=round_fast):
    v = mpf_round_int(s, round_floor)
    if prec:
        v = mpf_pos(v, prec, rnd)
    return v

def mpf_ceil(s, prec=0, rnd=round_fast):
    v = mpf_round_int(s, round_ceiling)
    if prec:
        v = mpf_pos(v, prec, rnd)
    return v

def mpf_nint(s, prec=0, rnd=round_fast):
    v = mpf_round_int(s, round_nearest)
    if prec:
        v = mpf_pos(v, prec, rnd)
    return v

def mpf_frac(s, prec=0, rnd=round_fast):
    return mpf_sub(s, mpf_floor(s), prec, rnd)

def mpf_rand(prec):
    global getrandbits
    if not getrandbits:
        getrandbits = random.getrandbits
    return from_man_exp(getrandbits(prec), -prec, prec, round_floor)

def mpf_eq(s, t):
    if not s[1] or not t[1]:
        if s == fnan or t == fnan:
            return False
    return s == t

def mpf_hash(s):
    if sys.version >= "3.2":
        ssign, sman, sexp, sbc = s
        if not sman:
            if s == fnan: return sys.hash_info.nan
            if s == finf: return sys.hash_info.inf
            if s == fninf: return -sys.hash_info.inf
        h = sman % HASH_MODULUS
        if sexp >= 0:
            sexp = sexp % HASH_BITS
        else:
            sexp = HASH_BITS - 1 - ((-1 - sexp) % HASH_BITS)
        h = (h << sexp) % HASH_MODULUS
        if ssign: h = -h
        if h == -1: h == -2
        return int(h)
    else:
        try:
            return hash(to_float(s, strict=1))
        except OverflowError:
            return hash(s)

def mpf_cmp(s, t):
    ssign, sman, sexp, sbc = s
    tsign, tman, texp, tbc = t
    if not sman or not tman:
        if s == fzero: return -mpf_sign(t)
        if t == fzero: return mpf_sign(s)
        if s == t: return 0
        if t == fnan: return 1
        if s == finf: return 1
        if t == fninf: return 1
        return -1
    if ssign != tsign:
        if not ssign: return 1
        return -1
    if sexp == texp:
        if sman == tman:
            return 0
        if sman > tman:
            if ssign: return -1
            else:     return 1
        else:
            if ssign: return 1
            else:     return -1
    a = sbc + sexp
    b = tbc + texp
    if ssign:
        if a < b: return 1
        if a > b: return -1
    else:
        if a < b: return -1
        if a > b: return 1
    delta = mpf_sub(s, t, 5, round_floor)
    if delta[0]:
        return -1
    return 1

def mpf_lt(s, t):
    if s == fnan or t == fnan:
        return False
    return mpf_cmp(s, t) < 0

def mpf_le(s, t):
    if s == fnan or t == fnan:
        return False
    return mpf_cmp(s, t) <= 0

def mpf_gt(s, t):
    if s == fnan or t == fnan:
        return False
    return mpf_cmp(s, t) > 0

def mpf_ge(s, t):
    if s == fnan or t == fnan:
        return False
    return mpf_cmp(s, t) >= 0

def mpf_min_max(seq):
    min = max = seq[0]
    for x in seq[1:]:
        if mpf_lt(x, min): min = x
        if mpf_gt(x, max): max = x
    return min, max

def mpf_pos(s, prec=0, rnd=round_fast):
    if prec:
        sign, man, exp, bc = s
        if (not man) and exp:
            return s
        return normalize1(sign, man, exp, bc, prec, rnd)
    return s

def mpf_neg(s, prec=None, rnd=round_fast):
    sign, man, exp, bc = s
    if not man:
        if exp:
            if s == finf: return fninf
            if s == fninf: return finf
        return s
    if not prec:
        return (1-sign, man, exp, bc)
    return normalize1(1-sign, man, exp, bc, prec, rnd)

def mpf_abs(s, prec=None, rnd=round_fast):
    sign, man, exp, bc = s
    if (not man) and exp:
        if s == fninf:
            return finf
        return s
    if not prec:
        if sign:
            return (0, man, exp, bc)
        return s
    return normalize1(0, man, exp, bc, prec, rnd)

def mpf_sign(s):
    sign, man, exp, bc = s
    if not man:
        if s == finf: return 1
        if s == fninf: return -1
        return 0
    return (-1) ** sign

def mpf_add(s, t, prec=0, rnd=round_fast, _sub=0):
    ssign, sman, sexp, sbc = s
    tsign, tman, texp, tbc = t
    tsign ^= _sub
    if sman and tman:
        offset = sexp - texp
        if offset:
            if offset > 0:
                if offset > 100 and prec:
                    delta = sbc + sexp - tbc - texp
                    if delta > prec + 4:
                        offset = prec + 4
                        sman <<= offset
                        if tsign == ssign: sman += 1
                        else:              sman -= 1
                        return normalize1(ssign, sman, sexp-offset,
                            bitcount(sman), prec, rnd)
                if ssign == tsign:
                    man = tman + (sman << offset)
                else:
                    if ssign: man = tman - (sman << offset)
                    else:     man = (sman << offset) - tman
                    if man >= 0:
                        ssign = 0
                    else:
                        man = -man
                        ssign = 1
                bc = bitcount(man)
                return normalize1(ssign, man, texp, bc, prec or bc, rnd)
            elif offset < 0:
                if offset < -100 and prec:
                    delta = tbc + texp - sbc - sexp
                    if delta > prec + 4:
                        offset = prec + 4
                        tman <<= offset
                        if ssign == tsign: tman += 1
                        else:              tman -= 1
                        return normalize1(tsign, tman, texp-offset,
                            bitcount(tman), prec, rnd)
                if ssign == tsign:
                    man = sman + (tman << -offset)
                else:
                    if tsign: man = sman - (tman << -offset)
                    else:     man = (tman << -offset) - sman
                    if man >= 0:
                        ssign = 0
                    else:
                        man = -man
                        ssign = 1
                bc = bitcount(man)
                return normalize1(ssign, man, sexp, bc, prec or bc, rnd)
        if ssign == tsign:
            man = tman + sman
        else:
            if ssign: man = tman - sman
            else:     man = sman - tman
            if man >= 0:
                ssign = 0
            else:
                man = -man
                ssign = 1
        bc = bitcount(man)
        return normalize(ssign, man, texp, bc, prec or bc, rnd)
    if _sub:
        t = mpf_neg(t)
    if not sman:
        if sexp:
            if s == t or tman or not texp:
                return s
            return fnan
        if tman:
            return normalize1(tsign, tman, texp, tbc, prec or tbc, rnd)
        return t
    if texp:
        return t
    if sman:
        return normalize1(ssign, sman, sexp, sbc, prec or sbc, rnd)
    return s

def mpf_sub(s, t, prec=0, rnd=round_fast):
    return mpf_add(s, t, prec, rnd, 1)

def mpf_sum(xs, prec=0, rnd=round_fast, absolute=False):
    man = 0
    exp = 0
    max_extra_prec = prec*2 or 1000000  # XXX
    special = None
    for x in xs:
        xsign, xman, xexp, xbc = x
        if xman:
            if xsign and not absolute:
                xman = -xman
            delta = xexp - exp
            if xexp >= exp:
                if (delta > max_extra_prec) and \
                    ((not man) or delta-bitcount(abs(man)) > max_extra_prec):
                    man = xman
                    exp = xexp
                else:
                    man += (xman << delta)
            else:
                delta = -delta
                if delta-xbc > max_extra_prec:
                    if not man:
                        man, exp = xman, xexp
                else:
                    man = (man << delta) + xman
                    exp = xexp
        elif xexp:
            if absolute:
                x = mpf_abs(x)
            special = mpf_add(special or fzero, x, 1)
    if special:
        return special
    return from_man_exp(man, exp, prec, rnd)

def gmpy_mpf_mul(s, t, prec=0, rnd=round_fast):
    ssign, sman, sexp, sbc = s
    tsign, tman, texp, tbc = t
    sign = ssign ^ tsign
    man = sman*tman
    if man:
        bc = bitcount(man)
        if prec:
            return normalize1(sign, man, sexp+texp, bc, prec, rnd)
        else:
            return (sign, man, sexp+texp, bc)
    s_special = (not sman) and sexp
    t_special = (not tman) and texp
    if not s_special and not t_special:
        return fzero
    if fnan in (s, t): return fnan
    if (not tman) and texp: s, t = t, s
    if t == fzero: return fnan
    return {1:finf, -1:fninf}[mpf_sign(s)*mpf_sign(t)]

def gmpy_mpf_mul_int(s, n, prec, rnd=round_fast):
    sign, man, exp, bc = s
    if not man:
        return mpf_mul(s, from_int(n), prec, rnd)
    if not n:
        return fzero
    if n < 0:
        sign ^= 1
        n = -n
    man *= n
    return normalize(sign, man, exp, bitcount(man), prec, rnd)

def python_mpf_mul(s, t, prec=0, rnd=round_fast):
    ssign, sman, sexp, sbc = s
    tsign, tman, texp, tbc = t
    sign = ssign ^ tsign
    man = sman*tman
    if man:
        bc = sbc + tbc - 1
        bc += int(man>>bc)
        if prec:
            return normalize1(sign, man, sexp+texp, bc, prec, rnd)
        else:
            return (sign, man, sexp+texp, bc)
    s_special = (not sman) and sexp
    t_special = (not tman) and texp
    if not s_special and not t_special:
        return fzero
    if fnan in (s, t): return fnan
    if (not tman) and texp: s, t = t, s
    if t == fzero: return fnan
    return {1:finf, -1:fninf}[mpf_sign(s)*mpf_sign(t)]

def python_mpf_mul_int(s, n, prec, rnd=round_fast):
    sign, man, exp, bc = s
    if not man:
        return mpf_mul(s, from_int(n), prec, rnd)
    if not n:
        return fzero
    if n < 0:
        sign ^= 1
        n = -n
    man *= n
    if n < 1024:
        bc += bctable[int(n)] - 1
    else:
        bc += bitcount(n) - 1
    bc += int(man>>bc)
    return normalize(sign, man, exp, bc, prec, rnd)

# if BACKEND == 'gmpy':
#     mpf_mul = gmpy_mpf_mul
#     mpf_mul_int = gmpy_mpf_mul_int
# else:
#     mpf_mul = python_mpf_mul
#     mpf_mul_int = python_mpf_mul_int
mpf_mul = python_mpf_mul
mpf_mul_int = python_mpf_mul_int

def mpf_shift(s, n):

    sign, man, exp, bc = s
    if not man:
        return s
    return sign, man, exp+n, bc

def mpf_frexp(x):
    sign, man, exp, bc = x
    if not man:
        if x == fzero:
            return (fzero, 0)
        else:
            raise ValueError
    return mpf_shift(x, -bc-exp), bc+exp

def mpf_div(s, t, prec, rnd=round_fast):
    ssign, sman, sexp, sbc = s
    tsign, tman, texp, tbc = t
    if not sman or not tman:
        if s == fzero:
            if t == fzero: raise ZeroDivisionError
            if t == fnan: return fnan
            return fzero
        if t == fzero:
            raise ZeroDivisionError
        s_special = (not sman) and sexp
        t_special = (not tman) and texp
        if s_special and t_special:
            return fnan
        if s == fnan or t == fnan:
            return fnan
        if not t_special:
            if t == fzero:
                return fnan
            return {1:finf, -1:fninf}[mpf_sign(s)*mpf_sign(t)]
        return fzero
    sign = ssign ^ tsign
    if tman == 1:
        return normalize1(sign, sman, sexp-texp, sbc, prec, rnd)

    extra = prec - sbc + tbc + 5
    if extra < 5:
        extra = 5
    quot, rem = divmod(sman<<extra, tman)
    if rem:
        quot = (quot<<1) + 1
        extra += 1
        return normalize1(sign, quot, sexp-texp-extra, bitcount(quot), prec, rnd)
    return normalize(sign, quot, sexp-texp-extra, bitcount(quot), prec, rnd)

def mpf_rdiv_int(n, t, prec, rnd=round_fast):
    sign, man, exp, bc = t
    if not n or not man:
        return mpf_div(from_int(n), t, prec, rnd)
    if n < 0:
        sign ^= 1
        n = -n
    extra = prec + bc + 5
    quot, rem = divmod(n<<extra, man)
    if rem:
        quot = (quot<<1) + 1
        extra += 1
        return normalize1(sign, quot, -exp-extra, bitcount(quot), prec, rnd)
    return normalize(sign, quot, -exp-extra, bitcount(quot), prec, rnd)

def mpf_mod(s, t, prec, rnd=round_fast):
    ssign, sman, sexp, sbc = s
    tsign, tman, texp, tbc = t
    if ((not sman) and sexp) or ((not tman) and texp):
        return fnan
    if ssign == tsign and texp > sexp+sbc:
        return s
    if tman == 1 and sexp > texp+tbc:
        return fzero
    base = min(sexp, texp)
    sman = (-1)**ssign*sman
    tman = (-1)**tsign*tman
    man = (sman << (sexp-base)) % (tman << (texp-base))
    if man >= 0:
        sign = 0
    else:
        man = -man
        sign = 1
    return normalize(sign, man, base, bitcount(man), prec, rnd)

reciprocal_rnd = {
  round_down : round_up,
  round_up : round_down,
  round_floor : round_ceiling,
  round_ceiling : round_floor,
  round_nearest : round_nearest
}

negative_rnd = {
  round_down : round_down,
  round_up : round_up,
  round_floor : round_ceiling,
  round_ceiling : round_floor,
  round_nearest : round_nearest
}

def mpf_pow_int(s, n, prec, rnd=round_fast):
    sign, man, exp, bc = s
    if (not man) and exp:
        if s == finf:
            if n > 0: return s
            if n == 0: return fnan
            return fzero
        if s == fninf:
            if n > 0: return [finf, fninf][n & 1]
            if n == 0: return fnan
            return fzero
        return fnan
    n = int(n)
    if n == 0: return fone
    if n == 1: return mpf_pos(s, prec, rnd)
    if n == 2:
        _, man, exp, bc = s
        if not man:
            return fzero
        man = man*man
        if man == 1:
            return (0, MPZ_ONE, exp+exp, 1)
        bc = bc + bc - 2
        bc += bctable[int(man>>bc)]
        return normalize1(0, man, exp+exp, bc, prec, rnd)
    if n == -1: return mpf_div(fone, s, prec, rnd)
    if n < 0:
        inverse = mpf_pow_int(s, -n, prec+5, reciprocal_rnd[rnd])
        return mpf_div(fone, inverse, prec, rnd)
    result_sign = sign & n
    if man == 1:
        return (result_sign, MPZ_ONE, exp*n, 1)
    if bc*n < 1000:
        man **= n
        return normalize1(result_sign, man, exp*n, bitcount(man), prec, rnd)
    rounds_down = (rnd == round_nearest) or shifts_down[rnd][result_sign]
    workprec = prec + 4*bitcount(n) + 4
    _, pm, pe, pbc = fone
    while 1:
        if n & 1:
            pm = pm*man
            pe = pe+exp
            pbc += bc - 2
            pbc = pbc + bctable[int(pm >> pbc)]
            if pbc > workprec:
                if rounds_down:
                    pm = pm >> (pbc-workprec)
                else:
                    pm = -((-pm) >> (pbc-workprec))
                pe += pbc - workprec
                pbc = workprec
            n -= 1
            if not n:
                break
        man = man*man
        exp = exp+exp
        bc = bc + bc - 2
        bc = bc + bctable[int(man >> bc)]
        if bc > workprec:
            if rounds_down:
                man = man >> (bc-workprec)
            else:
                man = -((-man) >> (bc-workprec))
            exp += bc - workprec
            bc = workprec
        n = n // 2
    return normalize(result_sign, pm, pe, pbc, prec, rnd)

def mpf_perturb(x, eps_sign, prec, rnd):
    if rnd == round_nearest:
        return mpf_pos(x, prec, rnd)
    sign, man, exp, bc = x
    eps = (eps_sign, MPZ_ONE, exp+bc-prec-1, 1)
    if sign:
        away = (rnd in (round_down, round_ceiling)) ^ eps_sign
    else:
        away = (rnd in (round_up, round_ceiling)) ^ eps_sign
    if away:
        return mpf_add(x, eps, prec, rnd)
    else:
        return mpf_pos(x, prec, rnd)

def to_digits_exp(s, dps):
    if s[0]:
        sign = '-'
        s = mpf_neg(s)
    else:
        sign = ''
    _sign, man, exp, bc = s
    if not man:
        return '', '0', 0
    bitprec = int(dps*math.log(10,2)) + 10
    exp_from_1 = exp + bc
    if abs(exp_from_1) > 3500:
        expprec = bitcount(abs(exp)) + 5
        tmp = from_int(exp)
        tmp = mpf_mul(tmp, mpf_ln2(expprec))
        tmp = mpf_div(tmp, mpf_ln10(expprec), expprec)
        b = to_int(tmp)
        s = mpf_div(s, mpf_pow_int(ften, b, bitprec), bitprec)
        _sign, man, exp, bc = s
        exponent = b
    else:
        exponent = 0
    fixprec = max(bitprec - exp - bc, 0)
    fixdps = int(fixprec / math.log(10,2) + 0.5)
    sf = to_fixed(s, fixprec)
    sd = bin_to_radix(sf, fixprec, 10, fixdps)
    digits = numeral(sd, base=10, size=dps)

    exponent += len(digits) - fixdps - 1
    return sign, digits, exponent

def to_str(s, dps, strip_zeros=True, min_fixed=None, max_fixed=None, show_zero_exponent=False):
    if not s[1]:
        if s == fzero:
            if dps: t = '0.0'
            else:   t = '.0'
            if show_zero_exponent:
                t += 'e+0'
            return t
        if s == finf: return '+inf'
        if s == fninf: return '-inf'
        if s == fnan: return 'nan'
        raise ValueError
    if min_fixed is None: min_fixed = min(-(dps//3), -5)
    if max_fixed is None: max_fixed = dps
    sign, digits, exponent = to_digits_exp(s, dps+3)
    if not dps:
        if digits[0] in '56789':
            exponent += 1
        digits = ".0"
    else:
        if len(digits) > dps and digits[dps] in '56789' and \
            (dps < 500 or digits[dps-4:dps] == '9999'):
            digits2 = str(int(digits[:dps]) + 1)
            if len(digits2) > dps:
                digits2 = digits2[:dps]
                exponent += 1
            digits = digits2
        else:
            digits = digits[:dps]
        if min_fixed < exponent < max_fixed:
            if exponent < 0:
                digits = ("0"*int(-exponent)) + digits
                split = 1
            else:
                split = exponent + 1
                if split > dps:
                    digits += "0"*(split-dps)
            exponent = 0
        else:
            split = 1
        digits = (digits[:split] + "." + digits[split:])
        if strip_zeros:
            digits = digits.rstrip('0')
            if digits[-1] == ".":
                digits += "0"
    if exponent == 0 and dps and not show_zero_exponent: return sign + digits
    if exponent >= 0: return sign + digits + "e+" + str(exponent)
    if exponent < 0: return sign + digits + "e" + str(exponent)

def str_to_man_exp(x, base=10):
    float(x)
    x = x.lower()
    parts = x.split('e')
    if len(parts) == 1:
        exp = 0
    else: # == 2
        x = parts[0]
        exp = int(parts[1])
    parts = x.split('.')
    if len(parts) == 2:
        a, b = parts[0], parts[1].rstrip('0')
        exp -= len(b)
        x = a + b
    x = MPZ(int(x, base))
    return x, exp

special_str = {'inf':finf, '+inf':finf, '-inf':fninf, 'nan':fnan}

def from_str(x, prec, rnd=round_fast):
    x = x.strip()
    if x in special_str:
        return special_str[x]

    if '/' in x:
        p, q = x.split('/')
        return from_rational(int(p), int(q), prec, rnd)

    man, exp = str_to_man_exp(x, base=10)
    if abs(exp) > 400:
        s = from_int(man, prec+10)
        s = mpf_mul(s, mpf_pow_int(ften, exp, prec+10), prec, rnd)
    else:
        if exp >= 0:
            s = from_int(man*10**exp, prec, rnd)
        else:
            s = from_rational(man, 10**-exp, prec, rnd)
    return s

def from_bstr(x):
    man, exp = str_to_man_exp(x, base=2)
    man = MPZ(man)
    sign = 0
    if man < 0:
        man = -man
        sign = 1
    bc = bitcount(man)
    return normalize(sign, man, exp, bc, bc, round_floor)

def to_bstr(x):
    sign, man, exp, bc = x
    return ['','-'][sign] + numeral(man, size=bitcount(man), base=2) + ("e%i" % exp)

def mpf_sqrt(s, prec, rnd=round_fast):
    sign, man, exp, bc = s
    if sign:
        raise ComplexResult("square root of a negative number")
    if not man:
        return s
    if exp & 1:
        exp -= 1
        man <<= 1
        bc += 1
    elif man == 1:
        return normalize1(sign, man, exp//2, bc, prec, rnd)
    shift = max(4, 2*prec-bc+4)
    shift += shift & 1
    if rnd in 'fd':
        man = isqrt(man<<shift)
    else:
        man, rem = sqrtrem(man<<shift)
        if rem:
            man = (man<<1)+1
            shift += 2
    return from_man_exp(man, (exp-shift)//2, prec, rnd)

def mpf_hypot(x, y, prec, rnd=round_fast):

    if y == fzero: return mpf_abs(x, prec, rnd)
    if x == fzero: return mpf_abs(y, prec, rnd)
    hypot2 = mpf_add(mpf_mul(x,x), mpf_mul(y,y), prec+4)
    return mpf_sqrt(hypot2, prec, rnd)

# if BACKEND == 'sage':
#     try:
#         import sage.libs.mpmath.ext_libmp as ext_lib
#         mpf_add = ext_lib.mpf_add
#         mpf_sub = ext_lib.mpf_sub
#         mpf_mul = ext_lib.mpf_mul
#         mpf_div = ext_lib.mpf_div
#         mpf_sqrt = ext_lib.mpf_sqrt
#     except ImportError:
#         pass

def mpi_str(s, prec):
    sa, sb = s
    dps = prec_to_dps(prec) + 5
    return "[%s, %s]" % (to_str(sa, dps), to_str(sb, dps))

mpi_zero = (fzero, fzero)
mpi_one = (fone, fone)

def mpi_eq(s, t):
    return s == t

def mpi_ne(s, t):
    return s != t

def mpi_lt(s, t):
    sa, sb = s
    ta, tb = t
    if mpf_lt(sb, ta): return True
    if mpf_ge(sa, tb): return False
    return None

def mpi_le(s, t):
    sa, sb = s
    ta, tb = t
    if mpf_le(sb, ta): return True
    if mpf_gt(sa, tb): return False
    return None

def mpi_gt(s, t): return mpi_lt(t, s)
def mpi_ge(s, t): return mpi_le(t, s)

def print_(*args, **kwargs):
    fp = kwargs.pop("file", sys.stdout)
    if fp is None:
        return
    def write(data):
        if not isinstance(data, basestring):
            data = str(data)
        fp.write(data)
    want_unicode = False
    sep = kwargs.pop("sep", None)
    if sep is not None:
        if isinstance(sep, unicode):
            want_unicode = True
        elif not isinstance(sep, str):
            raise TypeError("sep must be None or a string")
    end = kwargs.pop("end", None)
    if end is not None:
        if isinstance(end, unicode):
            want_unicode = True
        elif not isinstance(end, str):
            raise TypeError("end must be None or a string")
    if kwargs:
        raise TypeError("invalid keyword arguments to print()")
    if not want_unicode:
        for arg in args:
            if isinstance(arg, unicode):
                want_unicode = True
                break
    if want_unicode:
        newline = unicode("\n")
        space = unicode(" ")
    else:
        newline = "\n"
        space = " "
    if sep is None:
        sep = space
    if end is None:
        end = newline
    for i, arg in enumerate(args):
        if i:
            write(sep)
        write(arg)
    write(end)

_add_doc(reraise, """Reraise an exception.""")

def with_metaclass(meta, base=object):
    return meta("NewBase", (base,), {})


def _mathfun_real(f_real, f_complex):
    def f(x, **kwargs):
        if type(x) is float:
            return f_real(x)
        if type(x) is complex:
            return f_complex(x)
        try:
            x = float(x)
            return f_real(x)
        except (TypeError, ValueError):
            x = complex(x)
            return f_complex(x)
    f.__name__ = f_real.__name__
    return f

def _mathfun(f_real, f_complex):
    def f(x, **kwargs):
        if type(x) is complex:
            return f_complex(x)
        try:
            return f_real(float(x))
        except (TypeError, ValueError):
            return f_complex(complex(x))
    f.__name__ = f_real.__name__
    return f

def _mathfun_n(f_real, f_complex):
    def f(*args, **kwargs):
        try:
            return f_real(*(float(x) for x in args))
        except (TypeError, ValueError):
            return f_complex(*(complex(x) for x in args))
    f.__name__ = f_real.__name__
    return f

try:
    math.log(-2.0)
    def math_log(x):
        if x <= 0.0:
            raise ValueError("math domain error")
        return math.log(x)
    def math_sqrt(x):
        if x < 0.0:
            raise ValueError("math domain error")
        return math.sqrt(x)
except (ValueError, TypeError):
    math_log = math.log
    math_sqrt = math.sqrt

pow = _mathfun_n(operator.pow, lambda x, y: complex(x)**y)
log = _mathfun_n(math_log, cmath.log)
sqrt = _mathfun(math_sqrt, cmath.sqrt)
exp = _mathfun_real(math.exp, cmath.exp)

cos = _mathfun_real(math.cos, cmath.cos)
sin = _mathfun_real(math.sin, cmath.sin)
tan = _mathfun_real(math.tan, cmath.tan)

acos = _mathfun(math.acos, cmath.acos)
asin = _mathfun(math.asin, cmath.asin)
atan = _mathfun_real(math.atan, cmath.atan)

cosh = _mathfun_real(math.cosh, cmath.cosh)
sinh = _mathfun_real(math.sinh, cmath.sinh)
tanh = _mathfun_real(math.tanh, cmath.tanh)

floor = _mathfun_real(math.floor,
    lambda z: complex(math.floor(z.real), math.floor(z.imag)))
ceil = _mathfun_real(math.ceil,
    lambda z: complex(math.ceil(z.real), math.ceil(z.imag)))

cos_sin = _mathfun_real(lambda x: (math.cos(x), math.sin(x)),
                        lambda z: (cmath.cos(z), cmath.sin(z)))

cbrt = _mathfun(lambda x: x**(1./3), lambda z: z**(1./3))

def nthroot(x, n):
    r = 1./n
    try:
        return float(x) ** r
    except (ValueError, TypeError):
        return complex(x) ** r

def _sinpi_real(x):
    if x < 0:
        return -_sinpi_real(-x)
    n, r = divmod(x, 0.5)
    r *= pi
    n %= 4
    if n == 0: return math.sin(r)
    if n == 1: return math.cos(r)
    if n == 2: return -math.sin(r)
    if n == 3: return -math.cos(r)

def _cospi_real(x):
    if x < 0:
        x = -x
    n, r = divmod(x, 0.5)
    r *= pi
    n %= 4
    if n == 0: return math.cos(r)
    if n == 1: return -math.sin(r)
    if n == 2: return -math.cos(r)
    if n == 3: return math.sin(r)

def _sinpi_complex(z):
    if z.real < 0:
        return -_sinpi_complex(-z)
    n, r = divmod(z.real, 0.5)
    z = pi*complex(r, z.imag)
    n %= 4
    if n == 0: return cmath.sin(z)
    if n == 1: return cmath.cos(z)
    if n == 2: return -cmath.sin(z)
    if n == 3: return -cmath.cos(z)

def _cospi_complex(z):
    if z.real < 0:
        z = -z
    n, r = divmod(z.real, 0.5)
    z = pi*complex(r, z.imag)
    n %= 4
    if n == 0: return cmath.cos(z)
    if n == 1: return -cmath.sin(z)
    if n == 2: return -cmath.cos(z)
    if n == 3: return cmath.sin(z)

cospi = _mathfun_real(_cospi_real, _cospi_complex)
sinpi = _mathfun_real(_sinpi_real, _sinpi_complex)

def tanpi(x):
    try:
        return sinpi(x) / cospi(x)
    except OverflowError:
        if complex(x).imag > 10:
            return 1j
        if complex(x).imag < 10:
            return -1j
        raise

def cotpi(x):
    try:
        return cospi(x) / sinpi(x)
    except OverflowError:
        if complex(x).imag > 10:
            return -1j
        if complex(x).imag < 10:
            return 1j
        raise

INF = 1e300*1e300
NINF = -INF
NAN = INF-INF
EPS = 2.2204460492503131e-16

_exact_gamma = (INF, 1.0, 1.0, 2.0, 6.0, 24.0, 120.0, 720.0, 5040.0, 40320.0,
  362880.0, 3628800.0, 39916800.0, 479001600.0, 6227020800.0, 87178291200.0,
  1307674368000.0, 20922789888000.0, 355687428096000.0, 6402373705728000.0,
  121645100408832000.0, 2432902008176640000.0)

_max_exact_gamma = len(_exact_gamma)-1

_lanczos_g = 7
_lanczos_p = (0.99999999999980993, 676.5203681218851, -1259.1392167224028,
     771.32342877765313, -176.61502916214059, 12.507343278686905,
     -0.13857109526572012, 9.9843695780195716e-6, 1.5056327351493116e-7)

def _gamma_real(x):
    _intx = int(x)
    if _intx == x:
        if _intx <= 0:
            #return (-1)**_intx*INF
            raise ZeroDivisionError("gamma function pole")
        if _intx <= _max_exact_gamma:
            return _exact_gamma[_intx]
    if x < 0.5:
        # TODO: sinpi
        return pi / (_sinpi_real(x)*_gamma_real(1-x))
    else:
        x -= 1.0
        r = _lanczos_p[0]
        for i in range(1, _lanczos_g+2):
            r += _lanczos_p[i]/(x+i)
        t = x + _lanczos_g + 0.5
        return 2.506628274631000502417*t**(x+0.5)*math.exp(-t)*r

def _gamma_complex(x):
    if not x.imag:
        return complex(_gamma_real(x.real))
    if x.real < 0.5:
        # TODO: sinpi
        return pi / (_sinpi_complex(x)*_gamma_complex(1-x))
    else:
        x -= 1.0
        r = _lanczos_p[0]
        for i in range(1, _lanczos_g+2):
            r += _lanczos_p[i]/(x+i)
        t = x + _lanczos_g + 0.5
        return 2.506628274631000502417*t**(x+0.5)*cmath.exp(-t)*r

gamma = _mathfun_real(_gamma_real, _gamma_complex)

def rgamma(x):
    try:
        return 1./gamma(x)
    except ZeroDivisionError:
        return x*0.0

def factorial(x):
    return gamma(x+1.0)

def arg(x):
    if type(x) is float:
        return math.atan2(0.0,x)
    return math.atan2(x.imag,x.real)

_psi_coeff = [
0.083333333333333333333,
-0.0083333333333333333333,
0.003968253968253968254,
-0.0041666666666666666667,
0.0075757575757575757576,
-0.021092796092796092796,
0.083333333333333333333,
-0.44325980392156862745,
3.0539543302701197438,
-26.456212121212121212]

def _digamma_real(x):
    _intx = int(x)
    if _intx == x:
        if _intx <= 0:
            raise ZeroDivisionError("polygamma pole")
    if x < 0.5:
        x = 1.0-x
        s = pi*cotpi(x)
    else:
        s = 0.0
    while x < 10.0:
        s -= 1.0/x
        x += 1.0
    x2 = x**-2
    t = x2
    for c in _psi_coeff:
        s -= c*t
        if t < 1e-20:
            break
        t *= x2
    return s + math_log(x) - 0.5/x

def _digamma_complex(x):
    if not x.imag:
        return complex(_digamma_real(x.real))
    if x.real < 0.5:
        x = 1.0-x
        s = pi*cotpi(x)
    else:
        s = 0.0
    while abs(x) < 10.0:
        s -= 1.0/x
        x += 1.0
    x2 = x**-2
    t = x2
    for c in _psi_coeff:
        s -= c*t
        if abs(t) < 1e-20:
            break
        t *= x2
    return s + cmath.log(x) - 0.5/x

digamma = _mathfun_real(_digamma_real, _digamma_complex)

_erfc_coeff_P = [
    1.0000000161203922312,
    2.1275306946297962644,
    2.2280433377390253297,
    1.4695509105618423961,
    0.66275911699770787537,
    0.20924776504163751585,
    0.045459713768411264339,
    0.0063065951710717791934,
    0.00044560259661560421715][::-1]

_erfc_coeff_Q = [
    1.0000000000000000000,
    3.2559100272784894318,
    4.9019435608903239131,
    4.4971472894498014205,
    2.7845640601891186528,
    1.2146026030046904138,
    0.37647108453729465912,
    0.080970149639040548613,
    0.011178148899483545902,
    0.00078981003831980423513][::-1]

def _polyval(coeffs, x):
    p = coeffs[0]
    for c in coeffs[1:]:
        p = c + x*p
    return p

def _erf_taylor(x):
    x2 = x*x
    s = t = x
    n = 1
    while abs(t) > 1e-17:
        t *= x2/n
        s -= t/(n+n+1)
        n += 1
        t *= x2/n
        s += t/(n+n+1)
        n += 1
    return 1.1283791670955125739*s

def _erfc_mid(x):
    return exp(-x*x)*_polyval(_erfc_coeff_P,x)/_polyval(_erfc_coeff_Q,x)

def _erfc_asymp(x):
    x2 = x*x
    v = exp(-x2)/x*0.56418958354775628695
    r = t = 0.5 / x2
    s = 1.0
    for n in range(1,22,4):
        s -= t
        t *= r*(n+2)
        s += t
        t *= r*(n+4)
        if abs(t) < 1e-17:
            break
    return s*v

def erf(x):
    x = float(x)
    if x != x:
        return x
    if x < 0.0:
        return -erf(-x)
    if x >= 1.0:
        if x >= 6.0:
            return 1.0
        return 1.0 - _erfc_mid(x)
    return _erf_taylor(x)

def erfc(x):
    x = float(x)
    if x != x:
        return x
    if x < 0.0:
        if x < -6.0:
            return 2.0
        return 2.0-erfc(-x)
    if x > 9.0:
        return _erfc_asymp(x)
    if x >= 1.0:
        return _erfc_mid(x)
    return 1.0 - _erf_taylor(x)

gauss42 = [\
(0.99839961899006235, 0.0041059986046490839),
(-0.99839961899006235, 0.0041059986046490839),
(0.9915772883408609, 0.009536220301748501),
(-0.9915772883408609,0.009536220301748501),
(0.97934250806374812, 0.014922443697357493),
(-0.97934250806374812, 0.014922443697357493),
(0.96175936533820439,0.020227869569052644),
(-0.96175936533820439, 0.020227869569052644),
(0.93892355735498811, 0.025422959526113047),
(-0.93892355735498811,0.025422959526113047),
(0.91095972490412735, 0.030479240699603467),
(-0.91095972490412735, 0.030479240699603467),
(0.87802056981217269,0.03536907109759211),
(-0.87802056981217269, 0.03536907109759211),
(0.8402859832618168, 0.040065735180692258),
(-0.8402859832618168,0.040065735180692258),
(0.7979620532554873, 0.044543577771965874),
(-0.7979620532554873, 0.044543577771965874),
(0.75127993568948048,0.048778140792803244),
(-0.75127993568948048, 0.048778140792803244),
(0.70049459055617114, 0.052746295699174064),
(-0.70049459055617114,0.052746295699174064),
(0.64588338886924779, 0.056426369358018376),
(-0.64588338886924779, 0.056426369358018376),
(0.58774459748510932, 0.059798262227586649),
(-0.58774459748510932, 0.059798262227586649),
(0.5263957499311922, 0.062843558045002565),
(-0.5263957499311922, 0.062843558045002565),
(0.46217191207042191, 0.065545624364908975),
(-0.46217191207042191, 0.065545624364908975),
(0.39542385204297503, 0.067889703376521934),
(-0.39542385204297503, 0.067889703376521934),
(0.32651612446541151, 0.069862992492594159),
(-0.32651612446541151, 0.069862992492594159),
(0.25582507934287907, 0.071454714265170971),
(-0.25582507934287907, 0.071454714265170971),
(0.18373680656485453, 0.072656175243804091),
(-0.18373680656485453, 0.072656175243804091),
(0.11064502720851986, 0.073460813453467527),
(-0.11064502720851986, 0.073460813453467527),
(0.036948943165351772, 0.073864234232172879),
(-0.036948943165351772, 0.073864234232172879)]

EI_ASYMP_CONVERGENCE_RADIUS = 40.0

def ei_asymp(z, _e1=False):
    r = 1./z
    s = t = 1.0
    k = 1
    while 1:
        t *= k*r
        s += t
        if abs(t) < 1e-16:
            break
        k += 1
    v = s*exp(z)/z
    if _e1:
        if type(z) is complex:
            zreal = z.real
            zimag = z.imag
        else:
            zreal = z
            zimag = 0.0
        if zimag == 0.0 and zreal > 0.0:
            v += pi*1j
    else:
        if type(z) is complex:
            if z.imag > 0:
                v += pi*1j
            if z.imag < 0:
                v -= pi*1j
    return v

def ei_taylor(z, _e1=False):
    s = t = z
    k = 2
    while 1:
        t = t*z/k
        term = t/k
        if abs(term) < 1e-17:
            break
        s += term
        k += 1
    s += euler
    if _e1:
        s += log(-z)
    else:
        if type(z) is float or z.imag == 0.0:
            s += math_log(abs(z))
        else:
            s += cmath.log(z)
    return s

def ei(z, _e1=False):
    typez = type(z)
    if typez not in (float, complex):
        try:
            z = float(z)
            typez = float
        except (TypeError, ValueError):
            z = complex(z)
            typez = complex
    if not z:
        return -INF
    absz = abs(z)
    if absz > EI_ASYMP_CONVERGENCE_RADIUS:
        return ei_asymp(z, _e1)
    elif absz <= 2.0 or (typez is float and z > 0.0):
        return ei_taylor(z, _e1)
    # Integrate, starting from whichever is smaller of a Taylor
    # series value or an asymptotic series value
    if typez is complex and z.real > 0.0:
        zref = z / absz
        ref = ei_taylor(zref, _e1)
    else:
        zref = EI_ASYMP_CONVERGENCE_RADIUS*z / absz
        ref = ei_asymp(zref, _e1)
    C = (zref-z)*0.5
    D = (zref+z)*0.5
    s = 0.0
    if type(z) is complex:
        _exp = cmath.exp
    else:
        _exp = math.exp
    for x,w in gauss42:
        t = C*x+D
        s += w*_exp(t)/t
    ref -= C*s
    return ref

def e1(z):
    typez = type(z)
    if type(z) not in (float, complex):
        try:
            z = float(z)
            typez = float
        except (TypeError, ValueError):
            z = complex(z)
            typez = complex
    if typez is complex and not z.imag:
        z = complex(z.real, 0.0)
    # end hack
    return -ei(-z, _e1=True)

_zeta_int = [\
-0.5,
0.0,
1.6449340668482264365,1.2020569031595942854,1.0823232337111381915,
1.0369277551433699263,1.0173430619844491397,1.0083492773819228268,
1.0040773561979443394,1.0020083928260822144,1.0009945751278180853,
1.0004941886041194646,1.0002460865533080483,1.0001227133475784891,
1.0000612481350587048,1.0000305882363070205,1.0000152822594086519,
1.0000076371976378998,1.0000038172932649998,1.0000019082127165539,
1.0000009539620338728,1.0000004769329867878,1.0000002384505027277,
1.0000001192199259653,1.0000000596081890513,1.0000000298035035147,
1.0000000149015548284]

_zeta_P = [-3.50000000087575873, -0.701274355654678147,
-0.0672313458590012612, -0.00398731457954257841,
-0.000160948723019303141, -4.67633010038383371e-6,
-1.02078104417700585e-7, -1.68030037095896287e-9,
-1.85231868742346722e-11][::-1]

_zeta_Q = [1.00000000000000000, -0.936552848762465319,
-0.0588835413263763741, -0.00441498861482948666,
-0.000143416758067432622, -5.10691659585090782e-6,
-9.58813053268913799e-8, -1.72963791443181972e-9,
-1.83527919681474132e-11][::-1]

_zeta_1 = [3.03768838606128127e-10, -1.21924525236601262e-8,
2.01201845887608893e-7, -1.53917240683468381e-6,
-5.09890411005967954e-7, 0.000122464707271619326,
-0.000905721539353130232, -0.00239315326074843037,
0.084239750013159168, 0.418938517907442414, 0.500000001921884009]

_zeta_0 = [-3.46092485016748794e-10, -6.42610089468292485e-9,
1.76409071536679773e-7, -1.47141263991560698e-6, -6.38880222546167613e-7,
0.000122641099800668209, -0.000905894913516772796, -0.00239303348507992713,
0.0842396947501199816, 0.418938533204660256, 0.500000000000000052]

def zeta(s):
    if not isinstance(s, (float, int)):
        try:
            s = float(s)
        except (ValueError, TypeError):
            try:
                s = complex(s)
                if not s.imag:
                    return complex(zeta(s.real))
            except (ValueError, TypeError):
                pass
            raise NotImplementedError
    if s == 1:
        raise ValueError("zeta(1) pole")
    if s >= 27:
        return 1.0 + 2.0**(-s) + 3.0**(-s)
    n = int(s)
    if n == s:
        if n >= 0:
            return _zeta_int[n]
        if not (n % 2):
            return 0.0
    if s <= 0.0:
        return 2.**s*pi**(s-1)*_sinpi_real(0.5*s)*_gamma_real(1-s)*zeta(1-s)
    if s <= 2.0:
        if s <= 1.0:
            return _polyval(_zeta_0,s)/(s-1)
        return _polyval(_zeta_1,s)/(s-1)
    z = _polyval(_zeta_P,s) / _polyval(_zeta_Q,s)
    return 1.0 + 2.0**(-s) + 3.0**(-s) + 4.0**(-s)*z

def defun(f):
    setattr(Eigen, f.__name__, f)

def hessenberg_reduce_0(ctx, A, T):
    n = A.rows
    if n <= 2: return

    for i in range(n-1, 1, -1):
        scale = 0
        for k in range(0, i):
            scale += abs(ctx.re(A[i,k])) + abs(ctx.im(A[i,k]))
        scale_inv = 0
        if scale != 0:
            scale_inv = 1 / scale
        if scale == 0 or ctx.isinf(scale_inv):
            T[i] = 0
            A[i,i-1] = 0
            continue
        H = 0
        for k in range(0, i):
            A[i,k] *= scale_inv
            rr = ctx.re(A[i,k])
            ii = ctx.im(A[i,k])
            H += rr*rr + ii*ii

        F = A[i,i-1]
        f = abs(F)
        G = ctx.sqrt(H)
        A[i,i-1] = - G*scale

        if f == 0:
            T[i] = G
        else:
            ff = F / f
            T[i] = F + G*ff
            A[i,i-1] *= ff

        H += G*f
        H = 1 / ctx.sqrt(H)

        T[i] *= H
        for k in range(0, i - 1):
            A[i,k] *= H

        for j in range(0, i):
            G = ctx.conj(T[i])*A[j,i-1]
            for k in range(0, i-1):
                G += ctx.conj(A[i,k])*A[j,k]

            A[j,i-1] -= G*T[i]
            for k in range(0, i-1):
                A[j,k] -= G*A[i,k]

        for j in range(0, n):
            G = T[i]*A[i-1,j]
            for k in range(0, i-1):
                G += A[i,k]*A[k,j]

            A[i-1,j] -= G*ctx.conj(T[i])
            for k in range(0, i-1):
                A[k,j] -= G*ctx.conj(A[i,k])


def hessenberg_reduce_1(ctx, A, T):
    n = A.rows
    if n == 1:
        A[0,0] = 1
        return
    A[0,0] = A[1,1] = 1
    A[0,1] = A[1,0] = 0
    for i in range(2, n):
        if T[i] != 0:
            for j in range(0, i):
                G = T[i]*A[i-1,j]
                for k in range(0, i-1):
                    G += A[i,k]*A[k,j]
                A[i-1,j] -= G*ctx.conj(T[i])
                for k in range(0, i-1):
                    A[k,j] -= G*ctx.conj(A[i,k])
        A[i,i] = 1
        for j in range(0, i):
            A[j,i] = A[i,j] = 0

@defun
def hessenberg(ctx, A, overwrite_a = False):
    n = A.rows
    if n == 1:
        return (ctx.matrix([[1]]), A)
    if not overwrite_a:
        A = A.copy()
    T = ctx.matrix(n, 1)
    hessenberg_reduce_0(ctx, A, T)
    Q = A.copy()
    hessenberg_reduce_1(ctx, Q, T)
    for x in range(n):
        for y in range(x+2, n):
            A[y,x] = 0
    return Q, A

def qr_step(ctx, n0, n1, A, Q, shift):
    n = A.rows
    c = A[n0  ,n0] - shift
    s = A[n0+1,n0]
    v = ctx.hypot(ctx.hypot(ctx.re(c), ctx.im(c)), ctx.hypot(ctx.re(s), ctx.im(s)))
    if v == 0:
        v = 1
        c = 1
        s = 0
    else:
        c /= v
        s /= v

    for k in range(n0, n):
        x = A[n0  ,k]
        y = A[n0+1,k]
        A[n0  ,k] = ctx.conj(c)*x + ctx.conj(s)*y
        A[n0+1,k] =         -s *x +          c *y

    for k in range(min(n1, n0+3)):
        x = A[k,n0  ]
        y = A[k,n0+1]
        A[k,n0  ] =           c *x +          s *y
        A[k,n0+1] = -ctx.conj(s)*x + ctx.conj(c)*y

    if not isinstance(Q, bool):
        for k in range(n):
            x = Q[k,n0  ]
            y = Q[k,n0+1]
            Q[k,n0  ] =           c *x +          s *y
            Q[k,n0+1] = -ctx.conj(s)*x + ctx.conj(c)*y

    for j in range(n0, n1 - 2):
        c = A[j+1,j]
        s = A[j+2,j]

        v = ctx.hypot(ctx.hypot(ctx.re(c), ctx.im(c)), ctx.hypot(ctx.re(s), ctx.im(s)))

        if v == 0:
            A[j+1,j] = 0
            v = 1
            c = 1
            s = 0
        else:
            A[j+1,j] = v
            c /= v
            s /= v

        A[j+2,j] = 0

        for k in range(j+1, n):
            # apply givens rotation from the left
            x = A[j+1,k]
            y = A[j+2,k]
            A[j+1,k] = ctx.conj(c)*x + ctx.conj(s)*y
            A[j+2,k] =         -s *x +          c *y

        for k in range(0, min(n1, j+4)):
            # apply givens rotation from the right
            x = A[k,j+1]
            y = A[k,j+2]
            A[k,j+1] =           c *x +          s *y
            A[k,j+2] = -ctx.conj(s)*x + ctx.conj(c)*y

        if not isinstance(Q, bool):
            for k in range(0, n):
                # eigenvectors
                x = Q[k,j+1]
                y = Q[k,j+2]
                Q[k,j+1] =           c *x +          s *y
                Q[k,j+2] = -ctx.conj(s)*x + ctx.conj(c)*y

def hessenberg_qr(ctx, A, Q):
    n = A.rows
    norm = 0
    for x in range(n):
        for y in range(min(x+2, n)):
            norm += ctx.re(A[y,x]) ** 2 + ctx.im(A[y,x]) ** 2
    norm = ctx.sqrt(norm) / n
    if norm == 0:
        return
    n0 = 0
    n1 = n

    eps = mp.eps / (100*n)
    maxits = ctx.dps*4

    its = totalits = 0

    while 1:
        k = n0
        while k + 1 < n1:
            s = abs(ctx.re(A[k,k])) + abs(ctx.im(A[k,k])) + abs(ctx.re(A[k+1,k+1])) + abs(ctx.im(A[k+1,k+1]))
            if s < eps*norm:
                s = norm
            if abs(A[k+1,k]) < eps*s:
                break
            k += 1

        if k + 1 < n1:
            # deflation found at position (k+1, k)

            A[k+1,k] = 0
            n0 = k + 1

            its = 0

            if n0 + 1 >= n1:
                # block of size at most two has converged
                n0 = 0
                n1 = k + 1
                if n1 < 2:
                    # QR algorithm has converged
                    return
        else:
            if (its % 30) == 10:
                # exceptional shift
                shift = A[n1-1,n1-2]
            elif (its % 30) == 20:
                # exceptional shift
                shift = abs(A[n1-1,n1-2])
            elif (its % 30) == 29:
                # exceptional shift
                shift = norm
            else:
                t =  A[n1-2,n1-2] + A[n1-1,n1-1]
                s = (A[n1-1,n1-1] - A[n1-2,n1-2]) ** 2 + 4*A[n1-1,n1-2]*A[n1-2,n1-1]
                if ctx.re(s) > 0:
                    s = ctx.sqrt(s)
                else:
                    s = ctx.sqrt(-s)*1j
                a = (t + s) / 2
                b = (t - s) / 2
                if abs(A[n1-1,n1-1] - a) > abs(A[n1-1,n1-1] - b):
                    shift = b
                else:
                    shift = a
            its += 1
            totalits += 1
            qr_step(ctx, n0, n1, A, Q, shift)
            if its > maxits:
                raise RuntimeError("qr: failed to converge after %d steps" % its)

@defun
def schur(ctx, A, overwrite_a = False):
    n = A.rows
    if n == 1:
        return (ctx.matrix([[1]]), A)
    if not overwrite_a:
        A = A.copy()
    T = ctx.matrix(n, 1)
    hessenberg_reduce_0(ctx, A, T)
    Q = A.copy()
    hessenberg_reduce_1(ctx, Q, T)
    for x in range(n):
        for y in range(x + 2, n):
            A[y,x] = 0
    hessenberg_qr(ctx, A, Q)
    return Q, A


def eig_tr_r(ctx, A):
    n = A.rows
    ER = ctx.eye(n)
    eps = mp.eps
    unfl = ctx.ldexp(ctx.one, -ctx.prec*30)
    smlnum = unfl*(n / eps)
    simin = 1 / ctx.sqrt(eps)
    rmax = 1
    for i in range(1, n):
        s = A[i,i]
        smin = max(eps*abs(s), smlnum)
        for j in range(i - 1, -1, -1):
            r = 0
            for k in range(j + 1, i + 1):
                r += A[j,k]*ER[k,i]
            t = A[j,j] - s
            if abs(t) < smin:
                t = smin
            r = -r / t
            ER[j,i] = r
            rmax = max(rmax, abs(r))
            if rmax > simin:
                for k in range(j, i+1):
                    ER[k,i] /= rmax
                rmax = 1
        if rmax != 1:
            for k in range(0, i + 1):
                ER[k,i] /= rmax
    return ER

def eig_tr_l(ctx, A):
    n = A.rows
    EL = ctx.eye(n)
    eps = mp.eps
    unfl = ctx.ldexp(ctx.one, -ctx.prec*30)
    smlnum = unfl*(n / eps)
    simin = 1 / ctx.sqrt(eps)
    rmax = 1
    for i in range(0, n - 1):
        s = A[i,i]
        smin = max(eps*abs(s), smlnum)
        for j in range(i + 1, n):
            r = 0
            for k in range(i, j):
                r += EL[i,k]*A[k,j]
            t = A[j,j] - s
            if abs(t) < smin:
                t = smin
            r = -r / t
            EL[i,j] = r
            rmax = max(rmax, abs(r))
            if rmax > simin:
                for k in range(i, j + 1):
                    EL[i,k] /= rmax
                rmax = 1
        if rmax != 1:
            for k in range(i, n):
                EL[i,k] /= rmax
    return EL

@defun
def eig(ctx, A, left = False, right = True, overwrite_a = False):
    n = A.rows
    if n == 1:
        if left and (not right):
            return ([A[0]], ctx.matrix([[1]]))

        if right and (not left):
            return ([A[0]], ctx.matrix([[1]]))

        return ([A[0]], ctx.matrix([[1]]), ctx.matrix([[1]]))

    if not overwrite_a:
        A = A.copy()

    T = ctx.zeros(n, 1)

    hessenberg_reduce_0(ctx, A, T)

    if left or right:
        Q = A.copy()
        hessenberg_reduce_1(ctx, Q, T)
    else:
        Q = False

    for x in range(n):
        for y in range(x + 2, n):
            A[y,x] = 0

    hessenberg_qr(ctx, A, Q)

    E = [0 for i in range(n)]
    for i in range(n):
        E[i] = A[i,i]

    if not (left or right):
        return E

    if left:
        EL = eig_tr_l(ctx, A)
        EL = EL*Q.transpose_conj()

    if right:
        ER = eig_tr_r(ctx, A)
        ER = Q*ER

    if left and (not right):
        return (E, EL)

    if right and (not left):
        return (E, ER)

    return (E, EL, ER)

@defun
def eig_sort(ctx, E, EL = False, ER = False, f = "real"):
    if isinstance(f, str):
        if f == "real":
            f = ctx.re
        elif f == "imag":
            f = ctx.im
        elif cmp == "abs":
            f = abs
        else:
            raise RuntimeError("unknown function %s" % f)
    n = len(E)
    for i in range(n):
        imax = i
        s = f(E[i])
        for j in range(i + 1, n):
            c = f(E[j])
            if c < s:
                s = c
                imax = j
        if imax != i:
            z = E[i]
            E[i] = E[imax]
            E[imax] = z
            if not isinstance(EL, bool):
                for j in range(n):
                    z = EL[i,j]
                    EL[i,j] = EL[imax,j]
                    EL[imax,j] = z
            if not isinstance(ER, bool):
                for j in range(n):
                    z = ER[j,i]
                    ER[j,i] = ER[j,imax]
                    ER[j,imax] = z

    if isinstance(EL, bool) and isinstance(ER, bool):
        return E

    if isinstance(EL, bool) and not(isinstance(ER, bool)):
        return (E, ER)

    if isinstance(ER, bool) and not(isinstance(EL, bool)):
        return (E, EL)
    return (E, EL, ER)

def r_sy_tridiag(ctx, A, D, E, calc_ev = True):
    n = A.rows
    for i in range(n - 1, 0, -1):
        scale = 0
        for k in range(0, i):
            scale += abs(A[k,i])
        scale_inv = 0
        if scale != 0:
            scale_inv = 1/scale
        if i == 1 or scale == 0 or ctx.isinf(scale_inv):
            E[i] = A[i-1,i]
            D[i] = 0
            continue
        H = 0
        for k in range(0, i):
            A[k,i] *= scale_inv
            H += A[k,i]*A[k,i]

        F = A[i-1,i]
        G = ctx.sqrt(H)
        if F > 0:
            G = -G
        E[i] = scale*G
        H -= F*G
        A[i-1,i] = F - G
        F = 0
        for j in range(0, i):
            if calc_ev:
                A[i,j] = A[j,i] / H
            G = 0                  # calculate A*U
            for k in range(0, j + 1):
                G += A[k,j]*A[k,i]
            for k in range(j + 1, i):
                G += A[j,k]*A[k,i]
            E[j] = G / H           # calculate P
            F += E[j]*A[j,i]
        HH = F / (2*H)
        for j in range(0, i):     # calculate reduced A
            F = A[j,i]
            G = E[j] - HH*F      # calculate Q
            E[j] = G
            for k in range(0, j + 1):
                A[k,j] -= F*E[k] + G*A[k,i]
        D[i] = H
    for i in range(1, n):         # better for compatibility
        E[i-1] = E[i]
    E[n-1] = 0
    if calc_ev:
        D[0] = 0
        for i in range(0, n):
            if D[i] != 0:
                for j in range(0, i):     # accumulate transformation matrices
                    G = 0
                    for k in range(0, i):
                        G += A[i,k]*A[k,j]
                    for k in range(0, i):
                        A[k,j] -= G*A[k,i]
            D[i] = A[i,i]
            A[i,i] = 1
            for j in range(0, i):
                A[j,i] = A[i,j] = 0
    else:
        for i in range(0, n):
            D[i] = A[i,i]

def c_he_tridiag_0(ctx, A, D, E, T):
    n = A.rows
    T[n-1] = 1
    for i in range(n - 1, 0, -1):
        scale = 0
        for k in range(0, i):
            scale += abs(ctx.re(A[k,i])) + abs(ctx.im(A[k,i]))
        scale_inv = 0
        if scale != 0:
            scale_inv = 1 / scale
        if scale == 0 or ctx.isinf(scale_inv):
            E[i] = 0
            D[i] = 0
            T[i-1] = 1
            continue
        if i == 1:
            F = A[i-1,i]
            f = abs(F)
            E[i] = f
            D[i] = 0
            if f != 0:
                T[i-1] = T[i]*F / f
            else:
                T[i-1] = T[i]
            continue
        H = 0
        for k in range(0, i):
            A[k,i] *= scale_inv
            rr = ctx.re(A[k,i])
            ii = ctx.im(A[k,i])
            H += rr*rr + ii*ii
        F = A[i-1,i]
        f = abs(F)
        G = ctx.sqrt(H)
        H += G*f
        E[i] = scale*G
        if f != 0:
            F = F / f
            TZ = - T[i]*F
            G *= F
        else:
            TZ = -T[i]
        A[i-1,i] += G
        F = 0
        for j in range(0, i):
            A[i,j] = A[j,i] / H
            G = 0                        # calculate A*U
            for k in range(0, j + 1):
                G += ctx.conj(A[k,j])*A[k,i]
            for k in range(j + 1, i):
                G += A[j,k]*A[k,i]
            T[j] = G / H                 # calculate P
            F += ctx.conj(T[j])*A[j,i]
        HH = F / (2*H)
        for j in range(0, i):           # calculate reduced A
            F = A[j,i]
            G = T[j] - HH*F            # calculate Q
            T[j] = G

            for k in range(0, j + 1):
                A[k,j] -= ctx.conj(F)*T[k] + ctx.conj(G)*A[k,i]
        T[i-1] = TZ
        D[i] = H
    for i in range(1, n):                # better for compatibility
        E[i-1] = E[i]
    E[n-1] = 0

    D[0] = 0
    for i in range(0, n):
        zw = D[i]
        D[i] = ctx.re(A[i,i])
        A[i,i] = zw


def c_he_tridiag_1(ctx, A, T):
    n = A.rows
    for i in range(0, n):
        if A[i,i] != 0:
            for j in range(0, i):
                G = 0
                for k in range(0, i):
                    G += ctx.conj(A[i,k])*A[k,j]
                for k in range(0, i):
                    A[k,j] -= G*A[k,i]
        A[i,i] = 1
        for j in range(0, i):
            A[j,i] = A[i,j] = 0
    for i in range(0, n):
        for k in range(0, n):
            A[i,k] *= T[k]

def c_he_tridiag_2(ctx, A, T, B):
    n = A.rows
    for i in range(0, n):
        for k in range(0, n):
            B[k,i] *= T[k]
    for i in range(0, n):
        if A[i,i] != 0:
            for j in range(0, n):
                G = 0
                for k in range(0, i):
                    G += ctx.conj(A[i,k])*B[k,j]
                for k in range(0, i):
                    B[k,j] -= G*A[k,i]


def tridiag_eigen(ctx, d, e, z = False):
    n = len(d)
    e[n-1] = 0
    iterlim = 2*ctx.dps

    for l in range(n):
        j = 0
        while 1:
            m = l
            while 1:
                if m + 1 == n:
                    break
                if abs(e[m]) <= mp.eps*(abs(d[m]) + abs(d[m + 1])):
                    break
                m = m + 1
            if m == l:
                break

            if j >= iterlim:
                raise RuntimeError("tridiag_eigen: no convergence to an eigenvalue after %d iterations" % iterlim)

            j += 1
            p = d[l]
            g = (d[l + 1] - p) / (2*e[l])
            r = ctx.hypot(g, 1)

            if g < 0:
                s = g - r
            else:
                s = g + r

            g = d[m] - p + e[l] / s

            s, c, p = 1, 1, 0

            for i in range(m - 1, l - 1, -1):
                f = s*e[i]
                b = c*e[i]
                if abs(f) > abs(g):
                    c = g / f
                    r = ctx.hypot(c, 1)
                    e[i + 1] = f*r
                    s = 1 / r
                    c = c*s
                else:
                    s = f / g
                    r = ctx.hypot(s, 1)
                    e[i + 1] = g*r
                    c = 1 / r
                    s = s*c
                g = d[i + 1] - p
                r = (d[i] - g)*s + 2*c*b
                p = s*r
                d[i + 1] = g + p
                g = c*r - b

                if not isinstance(z, bool):
                    for w in range(z.rows):
                        f = z[w,i+1]
                        z[w,i+1] = s*z[w,i] + c*f
                        z[w,i  ] = c*z[w,i] - s*f

            d[l] = d[l] - p
            e[l] = g
            e[m] = 0

    for ii in range(1, n):
        i = ii - 1
        k = i
        p = d[i]
        for j in range(ii, n):
            if d[j] >= p:
                continue
            k = j
            p = d[k]
        if k == i:
            continue
        d[k] = d[i]
        d[i] = p

        if not isinstance(z, bool):
            for w in range(z.rows):
                p = z[w,i]
                z[w,i] = z[w,k]
                z[w,k] = p


@defun
def eigsy(ctx, A, eigvals_only = False, overwrite_a = False):
    if not overwrite_a:
        A = A.copy()

    d = ctx.zeros(A.rows, 1)
    e = ctx.zeros(A.rows, 1)

    if eigvals_only:
        r_sy_tridiag(ctx, A, d, e, calc_ev = False)
        tridiag_eigen(ctx, d, e, False)
        return d
    else:
        r_sy_tridiag(ctx, A, d, e, calc_ev = True)
        tridiag_eigen(ctx, d, e, A)
        return (d, A)

@defun
def eighe(ctx, A, eigvals_only = False, overwrite_a = False):
    if not overwrite_a:
        A = A.copy()

    d = ctx.zeros(A.rows, 1)
    e = ctx.zeros(A.rows, 1)
    t = ctx.zeros(A.rows, 1)

    if eigvals_only:
        c_he_tridiag_0(ctx, A, d, e, t)
        tridiag_eigen(ctx, d, e, False)
        return d
    else:
        c_he_tridiag_0(ctx, A, d, e, t)
        B = ctx.eye(A.rows)
        tridiag_eigen(ctx, d, e, B)
        c_he_tridiag_2(ctx, A, t, B)
        return (d, B)

@defun
def eigh(ctx, A, eigvals_only = False, overwrite_a = False):
    iscomplex = any(type(x) is ctx.mpc for x in A)

    if iscomplex:
        return ctx.eighe(A, eigvals_only = eigvals_only, overwrite_a = overwrite_a)
    else:
        return ctx.eigsy(A, eigvals_only = eigvals_only, overwrite_a = overwrite_a)

@defun
def gauss_quadrature(ctx, n, qtype = "legendre", alpha = 0, beta = 0):
    d = ctx.zeros(n, 1)
    e = ctx.zeros(n, 1)
    z = ctx.zeros(1, n)
    z[0,0] = 1
    if qtype == "legendre":
        w = 2
        for i in range(n):
            j = i + 1
            e[i] = ctx.sqrt(j*j / (4*j*j - ctx.mpf(1)))
    elif qtype == "legendre01":
        w = 1
        for i in range(n):
            d[i] = 1 / ctx.mpf(2)
            j = i + 1
            e[i] = ctx.sqrt(j*j / (16*j*j - ctx.mpf(4)))
    elif qtype == "hermite":
        w = ctx.sqrt(mp.pi)
        for i in range(n):
            j = i + 1
            e[i] = ctx.sqrt(j / ctx.mpf(2))
    elif qtype == "laguerre":
        w = 1
        for i in range(n):
            j = i + 1
            d[i] = 2*j - 1
            e[i] = j
    elif qtype=="chebyshev1":
        w = mp.pi
        for i in range(n):
            e[i] = 1 / ctx.mpf(2)
        e[0] = ctx.sqrt(1 / ctx.mpf(2))
    elif qtype == "chebyshev2":
        w = mp.pi / 2
        for i in range(n):
            e[i] = 1 / ctx.mpf(2)
    elif qtype == "glaguerre":
        w = ctx.gamma(1 + alpha)
        for i in range(n):
            j = i + 1
            d[i] = 2*j - 1 + alpha
            e[i] = ctx.sqrt(j*(j + alpha))
    elif qtype == "jacobi":
        alpha = ctx.mpf(alpha)
        beta = ctx.mpf(beta)
        ab = alpha + beta
        abi = ab + 2
        w = (2**(ab+1))*ctx.gamma(alpha + 1)*ctx.gamma(beta + 1) / ctx.gamma(abi)
        d[0] = (beta - alpha) / abi
        e[0] = ctx.sqrt(4*(1 + alpha)*(1 + beta) / ((abi + 1)*(abi*abi)))
        a2b2 = beta*beta - alpha*alpha
        for i in range(1, n):
            j = i + 1
            abi = 2*j + ab
            d[i] = a2b2 / ((abi - 2)*abi)
            e[i] = ctx.sqrt(4*j*(j + alpha)*(j + beta)*(j + ab) / ((abi*abi - 1)*abi*abi))
    elif isinstance(qtype, str):
        raise ValueError("unknown quadrature rule \"%s\"" % qtype)
    elif not isinstance(qtype, str):
        w = qtype(d, e)
    else:
        assert 0
    tridiag_eigen(ctx, d, e, z)
    for i in range(len(z)):
        z[i] *= z[i]
    z = z.transpose()
    return (d, w*z)

def svd_r_raw(ctx, A, V = False, calc_u = False):
    m, n = A.rows, A.cols
    S = ctx.zeros(n, 1)
    work = ctx.zeros(n, 1)
    g = scale = anorm = 0
    maxits = 3*ctx.dps
    for i in range(n):
        work[i] = scale*g
        g = s = scale = 0
        if i < m:
            for k in range(i, m):
                scale += ctx.fabs(A[k,i])
            if scale != 0:
                for k in range(i, m):
                    A[k,i] /= scale
                    s += A[k,i]*A[k,i]
                f = A[i,i]
                g = -ctx.sqrt(s)
                if f < 0:
                    g = -g
                h = f*g - s
                A[i,i] = f - g
                for j in range(i+1, n):
                    s = 0
                    for k in range(i, m):
                        s += A[k,i]*A[k,j]
                    f = s / h
                    for k in range(i, m):
                        A[k,j] += f*A[k,i]
                for k in range(i,m):
                    A[k,i] *= scale
        S[i] = scale*g
        g = s = scale = 0
        if i < m and i != n - 1:
            for k in range(i+1, n):
                scale += ctx.fabs(A[i,k])
            if scale:
                for k in range(i+1, n):
                    A[i,k] /= scale
                    s += A[i,k]*A[i,k]
                f = A[i,i+1]
                g = -ctx.sqrt(s)
                if f < 0:
                    g = -g
                h = f*g - s
                A[i,i+1] = f - g
                for k in range(i+1, n):
                    work[k] = A[i,k] / h
                for j in range(i+1, m):
                    s = 0
                    for k in range(i+1, n):
                        s += A[j,k]*A[i,k]
                    for k in range(i+1, n):
                        A[j,k] += s*work[k]
                for k in range(i+1, n):
                    A[i,k] *= scale
        anorm = max(anorm, ctx.fabs(S[i]) + ctx.fabs(work[i]))

    if not isinstance(V, bool):
        for i in range(n-2, -1, -1):
            V[i+1,i+1] = 1

            if work[i+1] != 0:
                for j in range(i+1, n):
                    V[i,j] = (A[i,j] / A[i,i+1]) / work[i+1]
                for j in range(i+1, n):
                    s = 0
                    for k in range(i+1, n):
                        s += A[i,k]*V[j,k]
                    for k in range(i+1, n):
                        V[j,k] += s*V[i,k]

            for j in range(i+1, n):
                V[j,i] = V[i,j] = 0

        V[0,0] = 1

    if m<n : minnm = m
    else   : minnm = n

    if calc_u:
        for i in range(minnm-1, -1, -1):
            g = S[i]
            for j in range(i+1, n):
                A[i,j] = 0
            if g != 0:
                g = 1 / g
                for j in range(i+1, n):
                    s = 0
                    for k in range(i+1, m):
                        s += A[k,i]*A[k,j]
                    f = (s / A[i,i])*g
                    for k in range(i, m):
                        A[k,j] += f*A[k,i]
                for j in range(i, m):
                    A[j,i] *= g
            else:
                for j in range(i, m):
                    A[j,i] = 0
            A[i,i] += 1

    for k in range(n - 1, -1, -1):
        its = 0
        while 1:
            its += 1
            flag = True

            for l in range(k, -1, -1):
                nm = l-1

                if ctx.fabs(work[l]) + anorm == anorm:
                    flag = False
                    break

                if ctx.fabs(S[nm]) + anorm == anorm:
                    break
            if flag:
                c = 0
                s = 1
                for i in range(l, k + 1):
                    f = s*work[i]
                    work[i] *= c
                    if ctx.fabs(f) + anorm == anorm:
                        break
                    g = S[i]
                    h = ctx.hypot(f, g)
                    S[i] = h
                    h = 1 / h
                    c = g*h
                    s = - f*h

                    if calc_u:
                        for j in range(m):
                            y = A[j,nm]
                            z = A[j,i]
                            A[j,nm] = y*c + z*s
                            A[j,i]  = z*c - y*s
            z = S[k]
            if l == k:
                if z < 0:
                    S[k] = -z
                    if not isinstance(V, bool):
                        for j in range(n):
                            V[k,j] = -V[k,j]
                break

            if its >= maxits:
                raise RuntimeError("svd: no convergence to an eigenvalue after %d iterations" % its)

            x = S[l]
            nm = k-1
            y = S[nm]
            g = work[nm]
            h = work[k]
            f = ((y - z)*(y + z) + (g - h)*(g + h))/(2*h*y)
            g = ctx.hypot(f, 1)
            if f >= 0: f = ((x - z)*(x + z) + h*((y / (f + g)) - h)) / x
            else:      f = ((x - z)*(x + z) + h*((y / (f - g)) - h)) / x

            c = s = 1

            for j in range(l, nm + 1):
                g = work[j+1]
                y = S[j+1]
                h = s*g
                g = c*g
                z = ctx.hypot(f, h)
                work[j] = z
                c = f / z
                s = h / z
                f = x*c + g*s
                g = g*c - x*s
                h = y*s
                y *= c
                if not isinstance(V, bool):
                    for jj in range(n):
                        x = V[j  ,jj]
                        z = V[j+1,jj]
                        V[j    ,jj]= x*c + z*s
                        V[j+1  ,jj]= z*c - x*s
                z = ctx.hypot(f, h)
                S[j] = z
                if z != 0:
                    z = 1 / z
                    c = f*z
                    s = h*z
                f = c*g + s*y
                x = c*y - s*g

                if calc_u:
                    for jj in range(m):
                        y = A[jj,j  ]
                        z = A[jj,j+1]
                        A[jj,j    ] = y*c + z*s
                        A[jj,j+1  ] = z*c - y*s
            work[l] = 0
            work[k] = f
            S[k] = x

    for i in range(n):
        imax = i
        s = ctx.fabs(S[i])
        for j in range(i + 1, n):
            c = ctx.fabs(S[j])
            if c > s:
                s = c
                imax = j

        if imax != i:
            z = S[i]
            S[i] = S[imax]
            S[imax] = z

            if calc_u:
                for j in range(m):
                    z = A[j,i]
                    A[j,i] = A[j,imax]
                    A[j,imax] = z

            if not isinstance(V, bool):
                for j in range(n):
                    z = V[i,j]
                    V[i,j] = V[imax,j]
                    V[imax,j] = z

    return S

def svd_c_raw(ctx, A, V = False, calc_u = False):
    m, n = A.rows, A.cols
    S = ctx.zeros(n, 1)
    work  = ctx.zeros(n, 1)
    lbeta = ctx.zeros(n, 1)
    rbeta = ctx.zeros(n, 1)
    dwork = ctx.zeros(n, 1)
    g = scale = anorm = 0
    maxits = 3*ctx.dps

    for i in range(n):
        dwork[i] = scale*g
        g = s = scale = 0
        if i < m:
            for k in range(i, m):
                scale += ctx.fabs(ctx.re(A[k,i])) + ctx.fabs(ctx.im(A[k,i]))
            if scale != 0:
                for k in range(i, m):
                    A[k,i] /= scale
                    ar = ctx.re(A[k,i])
                    ai = ctx.im(A[k,i])
                    s += ar*ar + ai*ai
                f = A[i,i]
                g = -ctx.sqrt(s)
                if ctx.re(f) < 0:
                    beta = -g - ctx.conj(f)
                    g = -g
                else:
                    beta = -g + ctx.conj(f)
                beta /= ctx.conj(beta)
                beta += 1
                h = 2*(ctx.re(f)*g - s)
                A[i,i] = f - g
                beta /= h
                lbeta[i] = (beta / scale) / scale
                for j in range(i+1, n):
                    s = 0
                    for k in range(i, m):
                        s += ctx.conj(A[k,i])*A[k,j]
                    f = beta*s
                    for k in range(i, m):
                        A[k,j] += f*A[k,i]
                for k in range(i, m):
                    A[k,i] *= scale
        S[i] = scale*g
        g = s = scale = 0

        if i < m and i != n - 1:
            for k in range(i+1, n):
                scale += ctx.fabs(ctx.re(A[i,k])) + ctx.fabs(ctx.im(A[i,k]))
            if scale:
                for k in range(i+1, n):
                    A[i,k] /= scale
                    ar = ctx.re(A[i,k])
                    ai = ctx.im(A[i,k])
                    s += ar*ar + ai*ai
                f = A[i,i+1]
                g = -ctx.sqrt(s)
                if ctx.re(f) < 0:
                    beta = -g - ctx.conj(f)
                    g = -g
                else:
                    beta = -g + ctx.conj(f)

                beta /= ctx.conj(beta)
                beta += 1

                h = 2*(ctx.re(f)*g - s)
                A[i,i+1] = f - g

                beta /= h
                rbeta[i] = (beta / scale) / scale

                for k in range(i+1, n):
                    work[k] = A[i, k]

                for j in range(i+1, m):
                    s = 0
                    for k in range(i+1, n):
                        s += ctx.conj(A[i,k])*A[j,k]
                    f = s*beta
                    for k in range(i+1,n):
                        A[j,k] += f*work[k]

                for k in range(i+1, n):
                    A[i,k] *= scale
        anorm = max(anorm,ctx.fabs(S[i]) + ctx.fabs(dwork[i]))

    if not isinstance(V, bool):
        for i in range(n-2, -1, -1):
            V[i+1,i+1] = 1

            if dwork[i+1] != 0:
                f = ctx.conj(rbeta[i])
                for j in range(i+1, n):
                    V[i,j] = A[i,j]*f
                for j in range(i+1, n):
                    s = 0
                    for k in range(i+1, n):
                        s += ctx.conj(A[i,k])*V[j,k]
                    for k in range(i+1, n):
                        V[j,k] += s*V[i,k]

            for j in range(i+1,n):
                V[j,i] = V[i,j] = 0

        V[0,0] = 1

    if m < n : minnm = m
    else     : minnm = n

    if calc_u:
        for i in range(minnm-1, -1, -1):
            g = S[i]
            for j in range(i+1, n):
                A[i,j] = 0
            if g != 0:
                g = 1 / g
                for j in range(i+1, n):
                    s = 0
                    for k in range(i+1, m):
                        s += ctx.conj(A[k,i])*A[k,j]
                    f = s*ctx.conj(lbeta[i])
                    for k in range(i, m):
                        A[k,j] += f*A[k,i]
                for j in range(i, m):
                    A[j,i] *= g
            else:
                for j in range(i, m):
                    A[j,i] = 0
            A[i,i] += 1

    for k in range(n-1, -1, -1):
        its = 0
        while 1:
            its += 1
            flag = True

            for l in range(k, -1, -1):
                nm = l - 1

                if ctx.fabs(dwork[l]) + anorm == anorm:
                    flag = False
                    break

                if ctx.fabs(S[nm]) + anorm == anorm:
                    break

            if flag:
                c = 0
                s = 1
                for i in range(l, k+1):
                    f = s*dwork[i]
                    dwork[i] *= c
                    if ctx.fabs(f) + anorm == anorm:
                        break
                    g = S[i]
                    h = ctx.hypot(f, g)
                    S[i] = h
                    h = 1 / h
                    c = g*h
                    s = -f*h

                    if calc_u:
                        for j in range(m):
                            y = A[j,nm]
                            z = A[j,i]
                            A[j,nm]= y*c + z*s
                            A[j,i] = z*c - y*s

            z = S[k]

            if l == k:         # convergence
                if z < 0:    # singular value is made nonnegative
                    S[k] = -z
                    if not isinstance(V, bool):
                        for j in range(n):
                            V[k,j] = -V[k,j]
                break

            if its >= maxits:
                raise RuntimeError("svd: no convergence to an eigenvalue after %d iterations" % its)

            x = S[l]         # shift from bottom 2 by 2 minor
            nm = k-1
            y = S[nm]
            g = dwork[nm]
            h = dwork[k]
            f = ((y - z)*(y + z) + (g - h)*(g + h)) / (2*h*y)
            g = ctx.hypot(f, 1)
            if f >=0: f = (( x - z) *( x + z) + h *((y / (f + g)) - h)) / x
            else:     f = (( x - z) *( x + z) + h *((y / (f - g)) - h)) / x

            c = s = 1         # next qt transformation

            for j in range(l, nm + 1):
                g = dwork[j+1]
                y = S[j+1]
                h = s*g
                g = c*g
                z = ctx.hypot(f, h)
                dwork[j] = z
                c = f / z
                s = h / z
                f = x*c + g*s
                g = g*c - x*s
                h = y*s
                y *= c
                if not isinstance(V, bool):
                    for jj in range(n):
                        x = V[j  ,jj]
                        z = V[j+1,jj]
                        V[j    ,jj]= x*c + z*s
                        V[j+1,jj  ]= z*c - x*s
                z = ctx.hypot(f, h)
                S[j] = z
                if z != 0:
                    z = 1 / z
                    c = f*z
                    s = h*z
                f = c*g + s*y
                x = c*y - s*g
                if calc_u:
                    for jj in range(m):
                        y = A[jj,j  ]
                        z = A[jj,j+1]
                        A[jj,j    ]= y*c + z*s
                        A[jj,j+1  ]= z*c - y*s

            dwork[l] = 0
            dwork[k] = f
            S[k] = x
    for i in range(n):
        imax = i
        s = ctx.fabs(S[i])
        for j in range(i + 1, n):
            c = ctx.fabs(S[j])
            if c > s:
                s = c
                imax = j
        if imax != i:
            z = S[i]
            S[i] = S[imax]
            S[imax] = z
            if calc_u:
                for j in range(m):
                    z = A[j,i]
                    A[j,i] = A[j,imax]
                    A[j,imax] = z

            if not isinstance(V, bool):
                for j in range(n):
                    z = V[i,j]
                    V[i,j] = V[imax,j]
                    V[imax,j] = z
    return S

@defun
def svd_r(ctx, A, full_matrices = False, compute_uv = True, overwrite_a = False):
    m, n = A.rows, A.cols
    if not compute_uv:
        if not overwrite_a:
            A = A.copy()
        S = svd_r_raw(ctx, A, V = False, calc_u = False)
        S = S[:min(m,n)]
        return S

    if full_matrices and n < m:
        V = ctx.zeros(m, m)
        A0 = ctx.zeros(m, m)
        A0[:,:n] = A
        S = svd_r_raw(ctx, A0, V, calc_u = True)

        S = S[:n]
        V = V[:n,:n]

        return (A0, S, V)
    else:
        if not overwrite_a:
            A = A.copy()
        V = ctx.zeros(n, n)
        S = svd_r_raw(ctx, A, V, calc_u = True)

        if n > m:
            if full_matrices == False:
                V = V[:m,:]

            S = S[:m]
            A = A[:,:m]

        return (A, S, V)


@defun
def svd_c(ctx, A, full_matrices = False, compute_uv = True, overwrite_a = False):
    m, n = A.rows, A.cols
    if not compute_uv:
        if not overwrite_a:
            A = A.copy()
        S = svd_c_raw(ctx, A, V = False, calc_u = False)
        S = S[:min(m,n)]
        return S

    if full_matrices and n < m:
        V = ctx.zeros(m, m)
        A0 = ctx.zeros(m, m)
        A0[:,:n] = A
        S = svd_c_raw(ctx, A0, V, calc_u = True)

        S = S[:n]
        V = V[:n,:n]

        return (A0, S, V)
    else:
        if not overwrite_a:
            A = A.copy()
        V = ctx.zeros(n, n)
        S = svd_c_raw(ctx, A, V, calc_u = True)

        if n > m:
            if full_matrices == False:
                V = V[:m,:]

            S = S[:m]
            A = A[:,:m]

        return (A, S, V)

@defun
def svd(ctx, A, full_matrices = False, compute_uv = True, overwrite_a = False):
    iscomplex = any(type(x) is ctx.mpc for x in A)
    if iscomplex:
        return ctx.svd_c(A, full_matrices = full_matrices, compute_uv = compute_uv, overwrite_a = overwrite_a)
    else:
        return ctx.svd_r(A, full_matrices = full_matrices, compute_uv = compute_uv, overwrite_a = overwrite_a)



V = 15
M = 15

jn_small_zeros = \
[[2.4048255576957728,
  5.5200781102863106,
  8.6537279129110122,
  11.791534439014282,
  14.930917708487786,
  18.071063967910923,
  21.211636629879259,
  24.352471530749303,
  27.493479132040255,
  30.634606468431975,
  33.775820213573569,
  36.917098353664044,
  40.058425764628239,
  43.19979171317673,
  46.341188371661814],
 [3.8317059702075123,
  7.0155866698156188,
  10.173468135062722,
  13.323691936314223,
  16.470630050877633,
  19.615858510468242,
  22.760084380592772,
  25.903672087618383,
  29.046828534916855,
  32.189679910974404,
  35.332307550083865,
  38.474766234771615,
  41.617094212814451,
  44.759318997652822,
  47.901460887185447],
 [5.1356223018406826,
  8.4172441403998649,
  11.619841172149059,
  14.795951782351261,
  17.959819494987826,
  21.116997053021846,
  24.270112313573103,
  27.420573549984557,
  30.569204495516397,
  33.7165195092227,
  36.86285651128381,
  40.008446733478192,
  43.153453778371463,
  46.297996677236919,
  49.442164110416873],
 [6.3801618959239835,
  9.7610231299816697,
  13.015200721698434,
  16.223466160318768,
  19.409415226435012,
  22.582729593104442,
  25.748166699294978,
  28.908350780921758,
  32.064852407097709,
  35.218670738610115,
  38.370472434756944,
  41.520719670406776,
  44.669743116617253,
  47.817785691533302,
  50.965029906205183],
 [7.5883424345038044,
  11.064709488501185,
  14.37253667161759,
  17.615966049804833,
  20.826932956962388,
  24.01901952477111,
  27.199087765981251,
  30.371007667117247,
  33.537137711819223,
  36.699001128744649,
  39.857627302180889,
  43.01373772335443,
  46.167853512924375,
  49.320360686390272,
  52.471551398458023],
 [8.771483815959954,
  12.338604197466944,
  15.700174079711671,
  18.980133875179921,
  22.217799896561268,
  25.430341154222704,
  28.626618307291138,
  31.811716724047763,
  34.988781294559295,
  38.159868561967132,
  41.326383254047406,
  44.489319123219673,
  47.649399806697054,
  50.80716520300633,
  53.963026558378149],
 [9.9361095242176849,
  13.589290170541217,
  17.003819667816014,
  20.320789213566506,
  23.58608443558139,
  26.820151983411405,
  30.033722386570469,
  33.233041762847123,
  36.422019668258457,
  39.603239416075404,
  42.778481613199507,
  45.949015998042603,
  49.11577372476426,
  52.279453903601052,
  55.440592068853149],
 [11.086370019245084,
  14.821268727013171,
  18.287582832481726,
  21.641541019848401,
  24.934927887673022,
  28.191188459483199,
  31.42279419226558,
  34.637089352069324,
  37.838717382853611,
  41.030773691585537,
  44.21540850526126,
  47.394165755570512,
  50.568184679795566,
  53.738325371963291,
  56.905249991978781],
 [12.225092264004655,
  16.037774190887709,
  19.554536430997055,
  22.94517313187462,
  26.266814641176644,
  29.54565967099855,
  32.795800037341462,
  36.025615063869571,
  39.240447995178135,
  42.443887743273558,
  45.638444182199141,
  48.825930381553857,
  52.007691456686903,
  55.184747939289049,
  58.357889025269694],
 [13.354300477435331,
  17.241220382489128,
  20.807047789264107,
  24.233885257750552,
  27.583748963573006,
  30.885378967696675,
  34.154377923855096,
  37.400099977156589,
  40.628553718964528,
  43.843801420337347,
  47.048700737654032,
  50.245326955305383,
  53.435227157042058,
  56.619580266508436,
  59.799301630960228],
 [14.475500686554541,
  18.433463666966583,
  22.046985364697802,
  25.509450554182826,
  28.887375063530457,
  32.211856199712731,
  35.499909205373851,
  38.761807017881651,
  42.004190236671805,
  45.231574103535045,
  48.447151387269394,
  51.653251668165858,
  54.851619075963349,
  58.043587928232478,
  61.230197977292681],
 [15.589847884455485,
  19.61596690396692,
  23.275853726263409,
  26.773322545509539,
  30.17906117878486,
  33.526364075588624,
  36.833571341894905,
  40.111823270954241,
  43.368360947521711,
  46.608132676274944,
  49.834653510396724,
  53.050498959135054,
  56.257604715114484,
  59.457456908388002,
  62.651217388202912],
 [16.698249933848246,
  20.789906360078443,
  24.494885043881354,
  28.026709949973129,
  31.45996003531804,
  34.829986990290238,
  38.156377504681354,
  41.451092307939681,
  44.721943543191147,
  47.974293531269048,
  51.211967004101068,
  54.437776928325074,
  57.653844811906946,
  60.8618046824805,
  64.062937824850136],
 [17.801435153282442,
  21.95624406783631,
  25.705103053924724,
  29.270630441874802,
  32.731053310978403,
  36.123657666448762,
  39.469206825243883,
  42.780439265447158,
  46.06571091157561,
  49.330780096443524,
  52.579769064383396,
  55.815719876305778,
  59.040934037249271,
  62.257189393731728,
  65.465883797232125],
 [18.899997953174024,
  23.115778347252756,
  26.907368976182104,
  30.505950163896036,
  33.993184984781542,
  37.408185128639695,
  40.772827853501868,
  44.100590565798301,
  47.400347780543231,
  50.678236946479898,
  53.93866620912693,
  57.184898598119301,
  60.419409852130297,
  63.644117508962281,
  66.860533012260103]]

jnp_small_zeros = \
[[0.0,
  3.8317059702075123,
  7.0155866698156188,
  10.173468135062722,
  13.323691936314223,
  16.470630050877633,
  19.615858510468242,
  22.760084380592772,
  25.903672087618383,
  29.046828534916855,
  32.189679910974404,
  35.332307550083865,
  38.474766234771615,
  41.617094212814451,
  44.759318997652822],
 [1.8411837813406593,
  5.3314427735250326,
  8.5363163663462858,
  11.706004902592064,
  14.863588633909033,
  18.015527862681804,
  21.16436985918879,
  24.311326857210776,
  27.457050571059246,
  30.601922972669094,
  33.746182898667383,
  36.889987409236811,
  40.033444053350675,
  43.176628965448822,
  46.319597561173912],
 [3.0542369282271403,
  6.7061331941584591,
  9.9694678230875958,
  13.170370856016123,
  16.347522318321783,
  19.512912782488205,
  22.671581772477426,
  25.826037141785263,
  28.977672772993679,
  32.127327020443474,
  35.275535050674691,
  38.422654817555906,
  41.568934936074314,
  44.714553532819734,
  47.859641607992093],
 [4.2011889412105285,
  8.0152365983759522,
  11.345924310743006,
  14.585848286167028,
  17.78874786606647,
  20.9724769365377,
  24.144897432909265,
  27.310057930204349,
  30.470268806290424,
  33.626949182796679,
  36.781020675464386,
  39.933108623659488,
  43.083652662375079,
  46.232971081836478,
  49.381300092370349],
 [5.3175531260839944,
  9.2823962852416123,
  12.681908442638891,
  15.964107037731551,
  19.196028800048905,
  22.401032267689004,
  25.589759681386733,
  28.767836217666503,
  31.938539340972783,
  35.103916677346764,
  38.265316987088158,
  41.423666498500732,
  44.579623137359257,
  47.733667523865744,
  50.886159153182682],
 [6.4156163757002403,
  10.519860873772308,
  13.9871886301403,
  17.312842487884625,
  20.575514521386888,
  23.803581476593863,
  27.01030789777772,
  30.20284907898166,
  33.385443901010121,
  36.560777686880356,
  39.730640230067416,
  42.896273163494417,
  46.058566273567043,
  49.218174614666636,
  52.375591529563596],
 [7.501266144684147,
  11.734935953042708,
  15.268181461097873,
  18.637443009666202,
  21.931715017802236,
  25.183925599499626,
  28.409776362510085,
  31.617875716105035,
  34.81339298429743,
  37.999640897715301,
  41.178849474321413,
  44.352579199070217,
  47.521956905768113,
  50.687817781723741,
  53.85079463676896],
 [8.5778364897140741,
  12.932386237089576,
  16.529365884366944,
  19.941853366527342,
  23.268052926457571,
  26.545032061823576,
  29.790748583196614,
  33.015178641375142,
  36.224380548787162,
  39.422274578939259,
  42.611522172286684,
  45.793999658055002,
  48.971070951900596,
  52.143752969301988,
  55.312820330403446],
 [9.6474216519972168,
  14.115518907894618,
  17.774012366915256,
  21.229062622853124,
  24.587197486317681,
  27.889269427955092,
  31.155326556188325,
  34.39662855427218,
  37.620078044197086,
  40.830178681822041,
  44.030010337966153,
  47.221758471887113,
  50.407020967034367,
  53.586995435398319,
  56.762598475105272],
 [10.711433970699945,
  15.28673766733295,
  19.004593537946053,
  22.501398726777283,
  25.891277276839136,
  29.218563499936081,
  32.505247352375523,
  35.763792928808799,
  39.001902811514218,
  42.224638430753279,
  45.435483097475542,
  48.636922645305525,
  51.830783925834728,
  55.01844255063594,
  58.200955824859509],
 [11.770876674955582,
  16.447852748486498,
  20.223031412681701,
  23.760715860327448,
  27.182021527190532,
  30.534504754007074,
  33.841965775135715,
  37.118000423665604,
  40.371068905333891,
  43.606764901379516,
  46.828959446564562,
  50.040428970943456,
  53.243223214220535,
  56.438892058982552,
  59.628631306921512],
 [12.826491228033465,
  17.600266557468326,
  21.430854238060294,
  25.008518704644261,
  28.460857279654847,
  31.838424458616998,
  35.166714427392629,
  38.460388720328256,
  41.728625562624312,
  44.977526250903469,
  48.211333836373288,
  51.433105171422278,
  54.645106240447105,
  57.849056857839799,
  61.046288512821078],
 [13.878843069697276,
  18.745090916814406,
  22.629300302835503,
  26.246047773946584,
  29.72897816891134,
  33.131449953571661,
  36.480548302231658,
  39.791940718940855,
  43.075486800191012,
  46.337772104541405,
  49.583396417633095,
  52.815686826850452,
  56.037118687012179,
  59.249577075517968,
  62.454525995970462],
 [14.928374492964716,
  19.88322436109951,
  23.81938909003628,
  27.474339750968247,
  30.987394331665278,
  34.414545662167183,
  37.784378506209499,
  41.113512376883377,
  44.412454519229281,
  47.688252845993366,
  50.945849245830813,
  54.188831071035124,
  57.419876154678179,
  60.641030026538746,
  63.853885828967512],
 [15.975438807484321,
  21.015404934568315,
  25.001971500138194,
  28.694271223110755,
  32.236969407878118,
  35.688544091185301,
  39.078998185245057,
  42.425854432866141,
  45.740236776624833,
  49.029635055514276,
  52.299319390331728,
  55.553127779547459,
  58.793933759028134,
  62.02393848337554,
  65.244860767043859]]

yn_small_zeros = \
[[0.89357696627916752,
  3.9576784193148579,
  7.0860510603017727,
  10.222345043496417,
  13.361097473872763,
  16.500922441528091,
  19.64130970088794,
  22.782028047291559,
  25.922957653180923,
  29.064030252728398,
  32.205204116493281,
  35.346452305214321,
  38.487756653081537,
  41.629104466213808,
  44.770486607221993],
 [2.197141326031017,
  5.4296810407941351,
  8.5960058683311689,
  11.749154830839881,
  14.897442128336725,
  18.043402276727856,
  21.188068934142213,
  24.331942571356912,
  27.475294980449224,
  30.618286491641115,
  33.761017796109326,
  36.90355531614295,
  40.045944640266876,
  43.188218097393211,
  46.330399250701687],
 [3.3842417671495935,
  6.7938075132682675,
  10.023477979360038,
  13.209986710206416,
  16.378966558947457,
  19.539039990286384,
  22.69395593890929,
  25.845613720902269,
  28.995080395650151,
  32.143002257627551,
  35.289793869635804,
  38.435733485446343,
  41.581014867297885,
  44.725777117640461,
  47.870122696676504],
 [4.5270246611496439,
  8.0975537628604907,
  11.396466739595867,
  14.623077742393873,
  17.81845523294552,
  20.997284754187761,
  24.166235758581828,
  27.328799850405162,
  30.486989604098659,
  33.642049384702463,
  36.794791029185579,
  39.945767226378749,
  43.095367507846703,
  46.2438744334407,
  49.391498015725107],
 [5.6451478942208959,
  9.3616206152445429,
  12.730144474090465,
  15.999627085382479,
  19.22442895931681,
  22.424810599698521,
  25.610267054939328,
  28.785893657666548,
  31.954686680031668,
  35.118529525584828,
  38.278668089521758,
  41.435960629910073,
  44.591018225353424,
  47.744288086361052,
  50.896105199722123],
 [6.7471838248710219,
  10.597176726782031,
  14.033804104911233,
  17.347086393228382,
  20.602899017175335,
  23.826536030287532,
  27.030134937138834,
  30.220335654231385,
  33.401105611047908,
  36.574972486670962,
  39.743627733020277,
  42.908248189569535,
  46.069679073215439,
  49.228543693445843,
  52.385312123112282],
 [7.8377378223268716,
  11.811037107609447,
  15.313615118517857,
  18.670704965906724,
  21.958290897126571,
  25.206207715021249,
  28.429037095235496,
  31.634879502950644,
  34.828638524084437,
  38.013473399691765,
  41.19151880917741,
  44.364272633271975,
  47.53281875312084,
  50.697961822183806,
  53.860312300118388],
 [8.919605734873789,
  13.007711435388313,
  16.573915129085334,
  19.974342312352426,
  23.293972585596648,
  26.5667563757203,
  29.809531451608321,
  33.031769327150685,
  36.239265816598239,
  39.435790312675323,
  42.623910919472727,
  45.805442883111651,
  48.981708325514764,
  52.153694518185572,
  55.322154420959698],
 [9.9946283820824834,
  14.190361295800141,
  17.817887841179873,
  21.26093227125945,
  24.612576377421522,
  27.910524883974868,
  31.173701563441602,
  34.412862242025045,
  37.634648706110989,
  40.843415321050884,
  44.04214994542435,
  47.232978012841169,
  50.417456447370186,
  53.596753874948731,
  56.771765754432457],
 [11.064090256031013,
  15.361301343575925,
  19.047949646361388,
  22.532765416313869,
  25.91620496332662,
  29.2394205079349,
  32.523270869465881,
  35.779715464475261,
  39.016196664616095,
  42.237627509803703,
  45.4474001519274,
  48.647941127433196,
  51.841036928216499,
  55.028034667184916,
  58.209970905250097],
 [12.128927704415439,
  16.522284394784426,
  20.265984501212254,
  23.791669719454272,
  27.206568881574774,
  30.555020011020762,
  33.859683872746356,
  37.133649760307504,
  40.385117593813002,
  43.619533085646856,
  46.840676630553575,
  50.051265851897857,
  53.253310556711732,
  56.448332488918971,
  59.637507005589829],
 [13.189846995683845,
  17.674674253171487,
  21.473493977824902,
  25.03913093040942,
  28.485081336558058,
  31.858644293774859,
  35.184165245422787,
  38.475796636190897,
  41.742455848758449,
  44.990096293791186,
  48.222870660068338,
  51.443777308699826,
  54.655042589416311,
  57.858358441436511,
  61.055036135780528],
 [14.247395665073945,
  18.819555894710682,
  22.671697117872794,
  26.276375544903892,
  29.752925495549038,
  33.151412708998983,
  36.497763772987645,
  39.807134090704376,
  43.089121522203808,
  46.350163579538652,
  49.594769786270069,
  52.82620892320143,
  56.046916910756961,
  59.258751140598783,
  62.463155567737854],
 [15.30200785858925,
  19.957808654258601,
  23.861599172945054,
  27.504429642227545,
  31.011103429019229,
  34.434283425782942,
  37.801385632318459,
  41.128514139788358,
  44.425913324440663,
  47.700482714581842,
  50.957073905278458,
  54.199216028087261,
  57.429547607017405,
  60.65008661807661,
  63.862406280068586],
 [16.354034360047551,
  21.090156519983806,
  25.044040298785627,
  28.724161640881914,
  32.260472459522644,
  35.708083982611664,
  39.095820003878235,
  42.440684315990936,
  45.75353669045622,
  49.041718113283529,
  52.310408280968073,
  55.56338698149062,
  58.803488508906895,
  62.032886550960831,
  65.253280088312461]]

ynp_small_zeros = \
[[2.197141326031017,
  5.4296810407941351,
  8.5960058683311689,
  11.749154830839881,
  14.897442128336725,
  18.043402276727856,
  21.188068934142213,
  24.331942571356912,
  27.475294980449224,
  30.618286491641115,
  33.761017796109326,
  36.90355531614295,
  40.045944640266876,
  43.188218097393211,
  46.330399250701687],
 [3.6830228565851777,
  6.9414999536541757,
  10.123404655436613,
  13.285758156782854,
  16.440058007293282,
  19.590241756629495,
  22.738034717396327,
  25.884314618788867,
  29.029575819372535,
  32.174118233366201,
  35.318134458192094,
  38.461753870997549,
  41.605066618873108,
  44.74813744908079,
  47.891014070791065],
 [5.0025829314460639,
  8.3507247014130795,
  11.574195465217647,
  14.760909306207676,
  17.931285939466855,
  21.092894504412739,
  24.249231678519058,
  27.402145837145258,
  30.552708880564553,
  33.70158627151572,
  36.849213419846257,
  39.995887376143356,
  43.141817835750686,
  46.287157097544201,
  49.432018469138281],
 [6.2536332084598136,
  9.6987879841487711,
  12.972409052292216,
  16.19044719506921,
  19.38238844973613,
  22.559791857764261,
  25.728213194724094,
  28.890678419054777,
  32.048984005266337,
  35.204266606440635,
  38.357281675961019,
  41.508551443818436,
  44.658448731963676,
  47.807246956681162,
  50.95515126455207],
 [7.4649217367571329,
  11.005169149809189,
  14.3317235192331,
  17.58443601710272,
  20.801062338411128,
  23.997004122902644,
  27.179886689853435,
  30.353960608554323,
  33.521797098666792,
  36.685048382072301,
  39.844826969405863,
  43.001910515625288,
  46.15685955107263,
  49.310088614282257,
  52.461911043685864],
 [8.6495562436971983,
  12.280868725807848,
  15.660799304540377,
  18.949739756016503,
  22.192841809428241,
  25.409072788867674,
  28.608039283077593,
  31.795195353138159,
  34.973890634255288,
  38.14630522169358,
  41.313923188794905,
  44.477791768537617,
  47.638672065035628,
  50.797131066967842,
  53.953600129601663],
 [9.8147970120105779,
  13.532811875789828,
  16.965526446046053,
  20.291285512443867,
  23.56186260680065,
  26.799499736027237,
  30.015665481543419,
  33.216968050039509,
  36.407516858984748,
  39.590015243560459,
  42.766320595957378,
  45.937754257017323,
  49.105283450953203,
  52.269633324547373,
  55.431358715604255],
 [10.965152105242974,
  14.765687379508912,
  18.250123150217555,
  21.612750053384621,
  24.911310600813573,
  28.171051927637585,
  31.40518108895689,
  34.621401012564177,
  37.824552065973114,
  41.017847386464902,
  44.203512240871601,
  47.3831408366063,
  50.557907466622796,
  53.728697478957026,
  56.896191727313342],
 [12.103641941939539,
  15.982840905145284,
  19.517731005559611,
  22.916962141504605,
  26.243700855690533,
  29.525960140695407,
  32.778568197561124,
  36.010261572392516,
  39.226578757802172,
  42.43122493258747,
  45.626783824134354,
  48.815117837929515,
  51.997606404328863,
  55.175294723956816,
  58.348990221754937],
 [13.232403808592215,
  17.186756572616758,
  20.770762917490496,
  24.206152448722253,
  27.561059462697153,
  30.866053571250639,
  34.137476603379774,
  37.385039772270268,
  40.614946085165892,
  43.831373184731238,
  47.037251786726299,
  50.234705848765229,
  53.425316228549359,
  56.610286079882087,
  59.790548623216652],
 [14.35301374369987,
  18.379337301642568,
  22.011118775283494,
  25.482116178696707,
  28.865046588695164,
  32.192853922166294,
  35.483296655830277,
  38.747005493021857,
  41.990815194320955,
  45.219355876831731,
  48.435892856078888,
  51.642803925173029,
  54.84186659475857,
  58.034439083840155,
  61.221578745109862],
 [15.466672066554263,
  19.562077985759503,
  23.240325531101082,
  26.746322986645901,
  30.157042415639891,
  33.507642948240263,
  36.817212798512775,
  40.097251300178642,
  43.355193847719752,
  46.596103410173672,
  49.823567279972794,
  53.040208868780832,
  56.247996968470062,
  59.448441365714251,
  62.642721301357187],
 [16.574317035530872,
  20.73617763753932,
  24.459631728238804,
  27.999993668839644,
  31.438208790267783,
  34.811512070805535,
  38.140243708611251,
  41.436725143893739,
  44.708963264433333,
  47.962435051891027,
  51.201037321915983,
  54.427630745992975,
  57.644369734615238,
  60.852911791989989,
  64.054555435720397],
 [17.676697936439624,
  21.9026148697762,
  25.670073356263225,
  29.244155124266438,
  32.709534477396028,
  36.105399554497548,
  39.453272918267025,
  42.766255701958017,
  46.052899215578358,
  49.319076602061401,
  52.568982147952547,
  55.805705507386287,
  59.031580956740466,
  62.248409689597653,
  65.457606670836759],
 [18.774423978290318,
  23.06220035979272,
  26.872520985976736,
  30.479680663499762,
  33.971869047372436,
  37.390118854896324,
  40.757072537673599,
  44.086572292170345,
  47.387688809191869,
  50.66667461073936,
  53.928009929563275,
  57.175005343085052,
  60.410169281219877,
  63.635442539153021,
  66.85235358587768]]


@defun
def j0(ctx, x):
    return ctx.besselj(0, x)

@defun
def j1(ctx, x):
    return ctx.besselj(1, x)

@defun
def besselj(ctx, n, z, derivative=0, **kwargs):
    if type(n) is int:
        n_isint = True
    else:
        n = ctx.convert(n)
        n_isint = ctx.isint(n)
        if n_isint:
            n = int(ctx._re(n))
    if n_isint and n < 0:
        return (-1)**n*ctx.besselj(-n, z, derivative, **kwargs)
    z = ctx.convert(z)
    M = ctx.mag(z)
    if derivative:
        d = ctx.convert(derivative)
        if ctx.isint(d) and d >= 0:
            d = int(d)
            orig = ctx.prec
            try:
                ctx.prec += 15
                v = ctx.fsum((-1)**k*ctx.binomial(d,k)*ctx.besselj(2*k+n-d,z)
                    for k in range(d+1))
            finally:
                ctx.prec = orig
            v *= ctx.mpf(2)**(-d)
        else:
            def h(n,d):
                r = ctx.fmul(ctx.fmul(z, z, prec=ctx.prec+M), -0.25, exact=True)
                B = [0.5*(n-d+1), 0.5*(n-d+2)]
                T = [([2,mp.pi,z],[d-2*n,0.5,n-d],[],B,[(n+1)*0.5,(n+2)*0.5],B+[n+1],r)]
                return T
            v = ctx.hypercomb(h, [n,d], **kwargs)
    else:
        if (not derivative) and n_isint and abs(M) < 10 and abs(n) < 20:
            try:
                return ctx._besselj(n, z)
            except NotImplementedError:
                pass
        if not z:
            if not n:
                v = ctx.one + n+z
            elif ctx.re(n) > 0:
                v = n*z
            else:
                v = ctx.inf + z + n
        else:
            orig = ctx.prec
            try:
                ctx.prec += min(3*abs(M), ctx.prec)
                w = ctx.fmul(z, 0.5, exact=True)
                def h(n):
                    r = ctx.fneg(ctx.fmul(w, w, prec=max(0,ctx.prec+M)), exact=True)
                    return [([w], [n], [], [n+1], [], [n+1], r)]
                v = ctx.hypercomb(h, [n], **kwargs)
            finally:
                ctx.prec = orig
        v = +v
    return v

@defun
def besseli(ctx, n, z, derivative=0, **kwargs):
    n = ctx.convert(n)
    z = ctx.convert(z)
    if not z:
        if derivative:
            raise ValueError
        if not n:
            return 1+n+z
        if ctx.isint(n):
            return 0*(n+z)
        r = ctx.re(n)
        if r == 0:
            return ctx.nan*(n+z)
        elif r > 0:
            return 0*(n+z)
        else:
            return ctx.inf+(n+z)
    M = ctx.mag(z)
    if derivative:
        d = ctx.convert(derivative)
        def h(n,d):
            r = ctx.fmul(ctx.fmul(z, z, prec=ctx.prec+M), 0.25, exact=True)
            B = [0.5*(n-d+1), 0.5*(n-d+2), n+1]
            T = [([2,mp.pi,z],[d-2*n,0.5,n-d],[n+1],B,[(n+1)*0.5,(n+2)*0.5],B,r)]
            return T
        v = ctx.hypercomb(h, [n,d], **kwargs)
    else:
        def h(n):
            w = ctx.fmul(z, 0.5, exact=True)
            r = ctx.fmul(w, w, prec=max(0,ctx.prec+M))
            return [([w], [n], [], [n+1], [], [n+1], r)]
        v = ctx.hypercomb(h, [n], **kwargs)
    return v

@defun_wrapped
def bessely(ctx, n, z, derivative=0, **kwargs):
    if not z:
        if derivative:
            raise ValueError
        if not n:
            return -ctx.inf + (n+z)
        if ctx.im(n):
            return ctx.nan*(n+z)
        r = ctx.re(n)
        q = n+0.5
        if ctx.isint(q):
            if n > 0:
                return -ctx.inf + (n+z)
            else:
                return 0*(n+z)
        if r < 0 and int(ctx.floor(q)) % 2:
            return ctx.inf + (n+z)
        else:
            return ctx.ninf + (n+z)
    ctx.prec += 10
    m, d = ctx.nint_distance(n)
    if d < -ctx.prec:
        h = +mp.eps
        ctx.prec *= 2
        n += h
    elif d < 0:
        ctx.prec -= d
    cos, sin = ctx.cospi_sinpi(n)
    return (ctx.besselj(n,z,derivative,**kwargs)*cos - \
        ctx.besselj(-n,z,derivative,**kwargs))/sin

@defun_wrapped
def besselk(ctx, n, z, **kwargs):
    if not z:
        return ctx.inf
    M = ctx.mag(z)
    if M < 1:
        def h(n):
            r = (z/2)**2
            T1 = [z, 2], [-n, n-1], [n], [], [], [1-n], r
            T2 = [z, 2], [n, -n-1], [-n], [], [], [1+n], r
            return T1, T2
    else:
        ctx.prec += M
        def h(n):
            return [([mp.pi/2, z, ctx.exp(-z)], [0.5,-0.5,1], [], [], \
                [n+0.5, 0.5-n], [], -1/(2*z))]
    return ctx.hypercomb(h, [n], **kwargs)

@defun_wrapped
def hankel1(ctx,n,x,**kwargs):
    return ctx.besselj(n,x,**kwargs) + ctx.j*ctx.bessely(n,x,**kwargs)

@defun_wrapped
def hankel2(ctx,n,x,**kwargs):
    return ctx.besselj(n,x,**kwargs) - ctx.j*ctx.bessely(n,x,**kwargs)

@defun_wrapped
def whitm(ctx,k,m,z,**kwargs):
    if z == 0:
        if ctx.re(m) > -0.5:
            return z
        elif ctx.re(m) < -0.5:
            return ctx.inf + z
        else:
            return ctx.nan*z
    x = ctx.fmul(-0.5, z, exact=True)
    y = 0.5+m
    return ctx.exp(x)*z**y*ctx.hyp1f1(y-k, 1+2*m, z, **kwargs)

@defun_wrapped
def whitw(ctx,k,m,z,**kwargs):
    if z == 0:
        g = abs(ctx.re(m))
        if g < 0.5:
            return z
        elif g > 0.5:
            return ctx.inf + z
        else:
            return ctx.nan*z
    x = ctx.fmul(-0.5, z, exact=True)
    y = 0.5+m
    return ctx.exp(x)*z**y*ctx.hyperu(y-k, 1+2*m, z, **kwargs)

@defun
def hyperu(ctx, a, b, z, **kwargs):
    a, atype = ctx._convert_param(a)
    b, btype = ctx._convert_param(b)
    z = ctx.convert(z)
    if not z:
        if ctx.re(b) <= 1:
            return ctx.gammaprod([1-b],[a-b+1])
        else:
            return ctx.inf + z
    bb = 1+a-b
    bb, bbtype = ctx._convert_param(bb)
    try:
        orig = ctx.prec
        try:
            ctx.prec += 10
            v = ctx.hypsum(2, 0, (atype, bbtype), [a, bb], -1/z, maxterms=ctx.prec)
            return v / z**a
        finally:
            ctx.prec = orig
    except ctx.NoConvergence:
        pass
    def h(a,b):
        w = ctx.sinpi(b)
        T1 = ([mp.pi,w],[1,-1],[],[a-b+1,b],[a],[b],z)
        T2 = ([-mp.pi,w,z],[1,-1,1-b],[],[a,2-b],[a-b+1],[2-b],z)
        return T1, T2
    return ctx.hypercomb(h, [a,b], **kwargs)

@defun
def struveh(ctx,n,z, **kwargs):
    n = ctx.convert(n)
    z = ctx.convert(z)
    def h(n):
        return [([z/2, 0.5*ctx.sqrt(mp.pi)], [n+1, -1], [], [n+1.5], [1], [1.5, n+1.5], -(z/2)**2)]
    return ctx.hypercomb(h, [n], **kwargs)

@defun
def struvel(ctx,n,z, **kwargs):
    n = ctx.convert(n)
    z = ctx.convert(z)
    def h(n):
        return [([z/2, 0.5*ctx.sqrt(mp.pi)], [n+1, -1], [], [n+1.5], [1], [1.5, n+1.5], (z/2)**2)]
    return ctx.hypercomb(h, [n], **kwargs)

def _anger(ctx,which,v,z,**kwargs):
    v = ctx._convert_param(v)[0]
    z = ctx.convert(z)
    def h(v):
        b = ctx.mpq_1_2
        u = v*b
        m = b*3
        a1,a2,b1,b2 = m-u, m+u, 1-u, 1+u
        c, s = ctx.cospi_sinpi(u)
        if which == 0:
            A, B = [b*z, s], [c]
        if which == 1:
            A, B = [b*z, -c], [s]
        w = ctx.square_exp_arg(z, mult=-0.25)
        T1 = A, [1, 1], [], [a1,a2], [1], [a1,a2], w
        T2 = B, [1], [], [b1,b2], [1], [b1,b2], w
        return T1, T2
    return ctx.hypercomb(h, [v], **kwargs)

@defun
def angerj(ctx, v, z, **kwargs):
    return _anger(ctx, 0, v, z, **kwargs)

@defun
def webere(ctx, v, z, **kwargs):
    return _anger(ctx, 1, v, z, **kwargs)

@defun
def lommels1(ctx, u, v, z, **kwargs):
    u = ctx._convert_param(u)[0]
    v = ctx._convert_param(v)[0]
    z = ctx.convert(z)
    def h(u,v):
        b = ctx.mpq_1_2
        w = ctx.square_exp_arg(z, mult=-0.25)
        return ([u-v+1, u+v+1, z], [-1, -1, u+1], [], [], [1], \
            [b*(u-v+3),b*(u+v+3)], w),
    return ctx.hypercomb(h, [u,v], **kwargs)

@defun
def lommels2(ctx, u, v, z, **kwargs):
    u = ctx._convert_param(u)[0]
    v = ctx._convert_param(v)[0]
    z = ctx.convert(z)
    def h(u,v):
        b = ctx.mpq_1_2
        w = ctx.square_exp_arg(z, mult=-0.25)
        T1 = [u-v+1, u+v+1, z], [-1, -1, u+1], [], [], [1], [b*(u-v+3),b*(u+v+3)], w
        T2 = [2, z], [u+v-1, -v], [v, b*(u+v+1)], [b*(v-u+1)], [], [1-v], w
        T3 = [2, z], [u-v-1, v], [-v, b*(u-v+1)], [b*(1-u-v)], [], [1+v], w
        return T1, T2, T3
    return ctx.hypercomb(h, [u,v], **kwargs)

@defun
def ber(ctx, n, z, **kwargs):
    n = ctx.convert(n)
    z = ctx.convert(z)
    def h(n):
        r = -(z/4)**4
        cos, sin = ctx.cospi_sinpi(-0.75*n)
        T1 = [cos, z/2], [1, n], [], [n+1], [], [0.5, 0.5*(n+1), 0.5*n+1], r
        T2 = [sin, z/2], [1, n+2], [], [n+2], [], [1.5, 0.5*(n+3), 0.5*n+1], r
        return T1, T2
    return ctx.hypercomb(h, [n], **kwargs)

@defun
def bei(ctx, n, z, **kwargs):
    n = ctx.convert(n)
    z = ctx.convert(z)
    def h(n):
        r = -(z/4)**4
        cos, sin = ctx.cospi_sinpi(0.75*n)
        T1 = [cos, z/2], [1, n+2], [], [n+2], [], [1.5, 0.5*(n+3), 0.5*n+1], r
        T2 = [sin, z/2], [1, n], [], [n+1], [], [0.5, 0.5*(n+1), 0.5*n+1], r
        return T1, T2
    return ctx.hypercomb(h, [n], **kwargs)

@defun
def ker(ctx, n, z, **kwargs):
    n = ctx.convert(n)
    z = ctx.convert(z)
    def h(n):
        r = -(z/4)**4
        cos1, sin1 = ctx.cospi_sinpi(0.25*n)
        cos2, sin2 = ctx.cospi_sinpi(0.75*n)
        T1 = [2, z, 4*cos1], [-n-3, n, 1], [-n], [], [], [0.5, 0.5*(1+n), 0.5*(n+2)], r
        T2 = [2, z, -sin1], [-n-3, 2+n, 1], [-n-1], [], [], [1.5, 0.5*(3+n), 0.5*(n+2)], r
        T3 = [2, z, 4*cos2], [n-3, -n, 1], [n], [], [], [0.5, 0.5*(1-n), 1-0.5*n], r
        T4 = [2, z, -sin2], [n-3, 2-n, 1], [n-1], [], [], [1.5, 0.5*(3-n), 1-0.5*n], r
        return T1, T2, T3, T4
    return ctx.hypercomb(h, [n], **kwargs)

@defun
def kei(ctx, n, z, **kwargs):
    n = ctx.convert(n)
    z = ctx.convert(z)
    def h(n):
        r = -(z/4)**4
        cos1, sin1 = ctx.cospi_sinpi(0.75*n)
        cos2, sin2 = ctx.cospi_sinpi(0.25*n)
        T1 = [-cos1, 2, z], [1, n-3, 2-n], [n-1], [], [], [1.5, 0.5*(3-n), 1-0.5*n], r
        T2 = [-sin1, 2, z], [1, n-1, -n], [n], [], [], [0.5, 0.5*(1-n), 1-0.5*n], r
        T3 = [-sin2, 2, z], [1, -n-1, n], [-n], [], [], [0.5, 0.5*(n+1), 0.5*(n+2)], r
        T4 = [-cos2, 2, z], [1, -n-3, n+2], [-n-1], [], [], [1.5, 0.5*(n+3), 0.5*(n+2)], r
        return T1, T2, T3, T4
    return ctx.hypercomb(h, [n], **kwargs)

def c_memo(f):
    name = f.__name__
    def f_wrapped(ctx):
        cache = ctx._misc_const_cache
        prec = ctx.prec
        p,v = cache.get(name, (-1,0))
        if p >= prec:
            return +v
        else:
            cache[name] = (prec, f(ctx))
            return cache[name][1]
    return f_wrapped

@c_memo
def _airyai_C1(ctx):
    return 1 / (ctx.cbrt(9)*ctx.gamma(ctx.mpf(2)/3))

@c_memo
def _airyai_C2(ctx):
    return -1 / (ctx.cbrt(3)*ctx.gamma(ctx.mpf(1)/3))

@c_memo
def _airybi_C1(ctx):
    return 1 / (ctx.nthroot(3,6)*ctx.gamma(ctx.mpf(2)/3))

@c_memo
def _airybi_C2(ctx):
    return ctx.nthroot(3,6) / ctx.gamma(ctx.mpf(1)/3)

def _airybi_n2_inf(ctx):
    prec = ctx.prec
    try:
        v = ctx.power(3,'2/3')*ctx.gamma('2/3')/(2*mp.pi)
    finally:
        ctx.prec = prec
    return +v

def _airyderiv_0(ctx, z, n, ntype, which):
    if ntype == 'Z':
        if n < 0:
            return z
        r = ctx.mpq_1_3
        prec = ctx.prec
        try:
            ctx.prec += 10
            v = ctx.gamma((n+1)*r)*ctx.power(3,n*r) / mp.pi
            if which == 0:
                v *= ctx.sinpi(2*(n+1)*r)
                v /= ctx.power(3,'2/3')
            else:
                v *= abs(ctx.sinpi(2*(n+1)*r))
                v /= ctx.power(3,'1/6')
        finally:
            ctx.prec = prec
        return +v + z
    else:
        raise NotImplementedError

@defun
def airyai(ctx, z, derivative=0, **kwargs):
    z = ctx.convert(z)
    if derivative:
        n, ntype = ctx._convert_param(derivative)
    else:
        n = 0
    if not ctx.isnormal(z) and z:
        if n and ntype == 'Z':
            if n == -1:
                if z == ctx.inf:
                    return ctx.mpf(1)/3 + 1/z
                if z == ctx.ninf:
                    return ctx.mpf(-2)/3 + 1/z
            if n < -1:
                if z == ctx.inf:
                    return z
                if z == ctx.ninf:
                    return (-1)**n*(-z)
        if (not n) and z == ctx.inf or z == ctx.ninf:
            return 1/z
        # TODO: limits
        raise ValueError("essential singularity of Ai(z)")
    if z:
        extraprec = max(0, int(1.5*ctx.mag(z)))
    else:
        extraprec = 0
    if n:
        if n == 1:
            def h():
                if ctx._re(z) > 4:
                    ctx.prec += extraprec
                    w = z**1.5; r = -0.75/w; u = -2*w/3
                    ctx.prec -= extraprec
                    C = -ctx.exp(u)/(2*ctx.sqrt(mp.pi))*ctx.nthroot(z,4)
                    return ([C],[1],[],[],[(-1,6),(7,6)],[],r),
                else:
                    ctx.prec += extraprec
                    w = z**3 / 9
                    ctx.prec -= extraprec
                    C1 = _airyai_C1(ctx)*0.5
                    C2 = _airyai_C2(ctx)
                    T1 = [C1,z],[1,2],[],[],[],[ctx.mpq_5_3],w
                    T2 = [C2],[1],[],[],[],[ctx.mpq_1_3],w
                    return T1, T2
            return ctx.hypercomb(h, [], **kwargs)
        else:
            if z == 0:
                return _airyderiv_0(ctx, z, n, ntype, 0)
            def h(n):
                ctx.prec += extraprec
                w = z**3/9
                ctx.prec -= extraprec
                q13,q23,q43 = ctx.mpq_1_3, ctx.mpq_2_3, ctx.mpq_4_3
                a1=q13; a2=1; b1=(1-n)*q13; b2=(2-n)*q13; b3=1-n*q13
                T1 = [3, z], [n-q23, -n], [a1], [b1,b2,b3], \
                    [a1,a2], [b1,b2,b3], w
                a1=q23; b1=(2-n)*q13; b2=1-n*q13; b3=(4-n)*q13
                T2 = [3, z, -z], [n-q43, -n, 1], [a1], [b1,b2,b3], \
                    [a1,a2], [b1,b2,b3], w
                return T1, T2
            v = ctx.hypercomb(h, [n], **kwargs)
            if ctx._is_real_type(z) and ctx.isint(n):
                v = ctx._re(v)
            return v
    else:
        def h():
            if ctx._re(z) > 4:
                ctx.prec += extraprec
                w = z**1.5; r = -0.75/w; u = -2*w/3
                ctx.prec -= extraprec
                C = ctx.exp(u)/(2*ctx.sqrt(mp.pi)*ctx.nthroot(z,4))
                return ([C],[1],[],[],[(1,6),(5,6)],[],r),
            else:
                ctx.prec += extraprec
                w = z**3 / 9
                ctx.prec -= extraprec
                C1 = _airyai_C1(ctx)
                C2 = _airyai_C2(ctx)
                T1 = [C1],[1],[],[],[],[ctx.mpq_2_3],w
                T2 = [z*C2],[1],[],[],[],[ctx.mpq_4_3],w
                return T1, T2
        return ctx.hypercomb(h, [], **kwargs)

@defun
def airybi(ctx, z, derivative=0, **kwargs):
    z = ctx.convert(z)
    if derivative:
        n, ntype = ctx._convert_param(derivative)
    else:
        n = 0
    if not ctx.isnormal(z) and z:
        if n and ntype == 'Z':
            if z == ctx.inf:
                return z
            if z == ctx.ninf:
                if n == -1:
                    return 1/z
                if n == -2:
                    return _airybi_n2_inf(ctx)
                if n < -2:
                    return (-1)**n*(-z)
        if not n:
            if z == ctx.inf:
                return z
            if z == ctx.ninf:
                return 1/z
        raise ValueError("essential singularity of Bi(z)")
    if z:
        extraprec = max(0, int(1.5*ctx.mag(z)))
    else:
        extraprec = 0
    if n:
        if n == 1:
            def h():
                ctx.prec += extraprec
                w = z**3 / 9
                ctx.prec -= extraprec
                C1 = _airybi_C1(ctx)*0.5
                C2 = _airybi_C2(ctx)
                T1 = [C1,z],[1,2],[],[],[],[ctx.mpq_5_3],w
                T2 = [C2],[1],[],[],[],[ctx.mpq_1_3],w
                return T1, T2
            return ctx.hypercomb(h, [], **kwargs)
        else:
            if z == 0:
                return _airyderiv_0(ctx, z, n, ntype, 1)
            def h(n):
                ctx.prec += extraprec
                w = z**3/9
                ctx.prec -= extraprec
                q13,q23,q43 = ctx.mpq_1_3, ctx.mpq_2_3, ctx.mpq_4_3
                q16 = ctx.mpq_1_6
                q56 = ctx.mpq_5_6
                a1=q13; a2=1; b1=(1-n)*q13; b2=(2-n)*q13; b3=1-n*q13
                T1 = [3, z], [n-q16, -n], [a1], [b1,b2,b3], \
                    [a1,a2], [b1,b2,b3], w
                a1=q23; b1=(2-n)*q13; b2=1-n*q13; b3=(4-n)*q13
                T2 = [3, z], [n-q56, 1-n], [a1], [b1,b2,b3], \
                    [a1,a2], [b1,b2,b3], w
                return T1, T2
            v = ctx.hypercomb(h, [n], **kwargs)
            if ctx._is_real_type(z) and ctx.isint(n):
                v = ctx._re(v)
            return v
    else:
        def h():
            ctx.prec += extraprec
            w = z**3 / 9
            ctx.prec -= extraprec
            C1 = _airybi_C1(ctx)
            C2 = _airybi_C2(ctx)
            T1 = [C1],[1],[],[],[],[ctx.mpq_2_3],w
            T2 = [z*C2],[1],[],[],[],[ctx.mpq_4_3],w
            return T1, T2
        return ctx.hypercomb(h, [], **kwargs)

def _airy_zero(ctx, which, k, derivative, complex=False):
    def U(t): return t**(2/3.)*(1-7/(t**2*48))
    def T(t): return t**(2/3.)*(1+5/(t**2*48))
    k = int(k)
    if k < 1:
        raise ValueError("k cannot be less than 1")
    if not derivative in (0,1):
        raise ValueError("Derivative should lie between 0 and 1")
    if which == 0:
        if derivative:
            return ctx.findroot(lambda z: ctx.airyai(z,1),
                -U(3*mp.pi*(4*k-3)/8))
        return ctx.findroot(ctx.airyai, -T(3*mp.pi*(4*k-1)/8))
    if which == 1 and complex == False:
        if derivative:
            return ctx.findroot(lambda z: ctx.airybi(z,1),
                -U(3*mp.pi*(4*k-1)/8))
        return ctx.findroot(ctx.airybi, -T(3*mp.pi*(4*k-3)/8))
    if which == 1 and complex == True:
        if derivative:
            t = 3*mp.pi*(4*k-3)/8 + 0.75j*ctx.ln2
            s = ctx.expjpi(ctx.mpf(1)/3)*T(t)
            return ctx.findroot(lambda z: ctx.airybi(z,1), s)
        t = 3*mp.pi*(4*k-1)/8 + 0.75j*ctx.ln2
        s = ctx.expjpi(ctx.mpf(1)/3)*U(t)
        return ctx.findroot(ctx.airybi, s)

@defun
def airyaizero(ctx, k, derivative=0):
    return _airy_zero(ctx, 0, k, derivative, False)

@defun
def airybizero(ctx, k, derivative=0, complex=False):
    return _airy_zero(ctx, 1, k, derivative, complex)

def _scorer(ctx, z, which, kwargs):
    z = ctx.convert(z)
    if ctx.isinf(z):
        if z == ctx.inf:
            if which == 0: return 1/z
            if which == 1: return z
        if z == ctx.ninf:
            return 1/z
        raise ValueError("essential singularity")
    if z:
        extraprec = max(0, int(1.5*ctx.mag(z)))
    else:
        extraprec = 0
    if kwargs.get('derivative'):
        raise NotImplementedError
    try:
        if ctx.mag(z) > 3:
            if which == 0 and abs(ctx.arg(z)) < mp.pi/3*0.999:
                def h():
                    return (([mp.pi,z],[-1,-1],[],[],[(1,3),(2,3),1],[],9/z**3),)
                return ctx.hypercomb(h, [], maxterms=ctx.prec, force_series=True)
            if which == 1 and abs(ctx.arg(-z)) < 2*mp.pi/3*0.999:
                def h():
                    return (([-mp.pi,z],[-1,-1],[],[],[(1,3),(2,3),1],[],9/z**3),)
                return ctx.hypercomb(h, [], maxterms=ctx.prec, force_series=True)
    except ctx.NoConvergence:
        pass
    def h():
        A = ctx.airybi(z, **kwargs)/3
        B = -2*mp(pi) #mp.pi
        if which == 1:
            A *= 2
            B *= -1
        ctx.prec += extraprec
        w = z**3/9
        ctx.prec -= extraprec
        T1 = [A], [1], [], [], [], [], 0
        T2 = [B,z], [-1,2], [], [], [1], [ctx.mpq_4_3,ctx.mpq_5_3], w
        return T1, T2
    return ctx.hypercomb(h, [], **kwargs)

@defun
def scorergi(ctx, z, **kwargs):
    return _scorer(ctx, z, 0, kwargs)

@defun
def scorerhi(ctx, z, **kwargs):
    return _scorer(ctx, z, 1, kwargs)

@defun_wrapped
def coulombc(ctx, l, eta, _cache={}):
    if (l, eta) in _cache and _cache[l,eta][0] >= ctx.prec:
        return +_cache[l,eta][1]
    G3 = ctx.loggamma(2*l+2)
    G1 = ctx.loggamma(1+l+ctx.j*eta)
    G2 = ctx.loggamma(1+l-ctx.j*eta)
    v = 2**l*ctx.exp((-mp.pi*eta+G1+G2)/2 - G3)
    if not (ctx.im(l) or ctx.im(eta)):
        v = ctx.re(v)
    _cache[l,eta] = (ctx.prec, v)
    return v

@defun_wrapped
def coulombf(ctx, l, eta, z, w=1, chop=True, **kwargs):
    def h(l, eta):
        try:
            jw = ctx.j*w
            jwz = ctx.fmul(jw, z, exact=True)
            jwz2 = ctx.fmul(jwz, -2, exact=True)
            C = ctx.coulombc(l, eta)
            T1 = [C, z, ctx.exp(jwz)], [1, l+1, 1], [], [], [1+l+jw*eta], \
                [2*l+2], jwz2
        except ValueError:
            T1 = [0], [-1], [], [], [], [], 0
        return (T1,)
    v = ctx.hypercomb(h, [l,eta], **kwargs)
    if chop and (not ctx.im(l)) and (not ctx.im(eta)) and (not ctx.im(z)) and \
        (ctx.re(z) >= 0):
        v = ctx.re(v)
    return v

@defun_wrapped
def _coulomb_chi(ctx, l, eta, _cache={}):
    if (l, eta) in _cache and _cache[l,eta][0] >= ctx.prec:
        return _cache[l,eta][1]
    def terms():
        l2 = -l-1
        jeta = ctx.j*eta
        return [ctx.loggamma(1+l+jeta)*(-0.5j),
            ctx.loggamma(1+l-jeta)*(0.5j),
            ctx.loggamma(1+l2+jeta)*(0.5j),
            ctx.loggamma(1+l2-jeta)*(-0.5j),
            -(l+0.5)*mp.pi]
    v = ctx.sum_accurately(terms, 1)
    _cache[l,eta] = (ctx.prec, v)
    return v

@defun_wrapped
def coulombg(ctx, l, eta, z, w=1, chop=True, **kwargs):
    if not ctx._im(l):
        l = ctx._re(l)  # XXX: for isint
    def h(l, eta):
        if ctx.isint(l*2):
            T1 = [0], [-1], [], [], [], [], 0
            return (T1,)
        l2 = -l-1
        try:
            chi = ctx._coulomb_chi(l, eta)
            jw = ctx.j*w
            s = ctx.sin(chi); c = ctx.cos(chi)
            C1 = ctx.coulombc(l,eta)
            C2 = ctx.coulombc(l2,eta)
            u = ctx.exp(jw*z)
            x = -2*jw*z
            T1 = [s, C1, z, u, c], [-1, 1, l+1, 1, 1], [], [], \
                [1+l+jw*eta], [2*l+2], x
            T2 = [-s, C2, z, u],   [-1, 1, l2+1, 1],    [], [], \
                [1+l2+jw*eta], [2*l2+2], x
            return T1, T2
        except ValueError:
            T1 = [0], [-1], [], [], [], [], 0
            return (T1,)
    v = ctx.hypercomb(h, [l,eta], **kwargs)
    if chop and (not ctx._im(l)) and (not ctx._im(eta)) and (not ctx._im(z)) and \
        (ctx._re(z) >= 0):
        v = ctx._re(v)
    return v

def mcmahon(ctx,kind,prime,v,m):
    u = 4*v**2
    if kind == 1 and not prime: b = (4*m+2*v-1)*mp.pi/4
    if kind == 2 and not prime: b = (4*m+2*v-3)*mp.pi/4
    if kind == 1 and prime: b = (4*m+2*v-3)*mp.pi/4
    if kind == 2 and prime: b = (4*m+2*v-1)*mp.pi/4
    if not prime:
        s1 = b
        s2 = -(u-1)/(8*b)
        s3 = -4*(u-1)*(7*u-31)/(3*(8*b)**3)
        s4 = -32*(u-1)*(83*u**2-982*u+3779)/(15*(8*b)**5)
        s5 = -64*(u-1)*(6949*u**3-153855*u**2+1585743*u-6277237)/(105*(8*b)**7)
    if prime:
        s1 = b
        s2 = -(u+3)/(8*b)
        s3 = -4*(7*u**2+82*u-9)/(3*(8*b)**3)
        s4 = -32*(83*u**3+2075*u**2-3039*u+3537)/(15*(8*b)**5)
        s5 = -64*(6949*u**4+296492*u**3-1248002*u**2+7414380*u-5853627)/(105*(8*b)**7)
    terms = [s1,s2,s3,s4,s5]
    s = s1
    err = 0.0
    for i in range(1,len(terms)):
        if abs(terms[i]) < abs(terms[i-1]):
            s += terms[i]
        else:
            err = abs(terms[i])
    if i == len(terms)-1:
        err = abs(terms[-1])
    return s, err

def generalized_bisection(ctx,f,a,b,n):

    if n < 1:
        raise ValueError("n cannot be less than 1")
    N = n+1
    points = []
    signs = []
    while 1:
        points = ctx.linspace(a,b,N)
        signs = [ctx.sign(f(x)) for x in points]
        ok_intervals = [(points[i],points[i+1]) for i in range(N-1) \
            if signs[i]*signs[i+1] == -1]
        if len(ok_intervals) == n:
            return ok_intervals
        N = N*2

def find_in_interval(ctx, f, ab):
    return ctx.findroot(f, ab, solver='illinois', verify=False)

def bessel_zero(ctx, kind, prime, v, m, isoltol=0.01, _interval_cache={}):
    prec = ctx.prec
    workprec = max(prec, ctx.mag(v), ctx.mag(m))+10
    try:
        ctx.prec = workprec
        v = ctx.mpf(v)
        m = int(m)
        prime = int(prime)
        if v < 0:
            raise ValueError("v cannot be negative")
        if m < 1:
            raise ValueError("m cannot be less than 1")
        if not prime in (0,1):
            raise ValueError("prime should lie between 0 and 1")
        if kind == 1:
            if prime: f = lambda x: ctx.besselj(v,x,derivative=1)
            else:     f = lambda x: ctx.besselj(v,x)
        if kind == 2:
            if prime: f = lambda x: ctx.bessely(v,x,derivative=1)
            else:     f = lambda x: ctx.bessely(v,x)
        if kind == 1 and prime and m == 1:
            if v == 0:
                return ctx.zero
            if v <= 1:
                r = 2*ctx.sqrt(v*(1+v)/(v+2))
                return find_in_interval(ctx, f, (r/10, 2*r))
        if (kind,prime,v,m) in _interval_cache:
            return find_in_interval(ctx, f, _interval_cache[kind,prime,v,m])
        r, err = mcmahon(ctx, kind, prime, v, m)
        if err < isoltol:
            return find_in_interval(ctx, f, (r-isoltol, r+isoltol))
        if kind == 1 and not prime: low = 2.4
        if kind == 1 and prime: low = 1.8
        if kind == 2 and not prime: low = 0.8
        if kind == 2 and prime: low = 2.0
        n = m+1
        while 1:
            r1, err = mcmahon(ctx, kind, prime, v, n)
            if err < isoltol:
                r2, err2 = mcmahon(ctx, kind, prime, v, n+1)
                intervals = generalized_bisection(ctx, f, low, 0.5*(r1+r2), n)
                for k, ab in enumerate(intervals):
                    _interval_cache[kind,prime,v,k+1] = ab
                return find_in_interval(ctx, f, intervals[m-1])
            else:
                n = n*2
    finally:
        ctx.prec = prec

@defun
def besseljzero(ctx, v, m, derivative=0):

    return +bessel_zero(ctx, 1, derivative, v, m)

@defun
def besselyzero(ctx, v, m, derivative=0):

    return +bessel_zero(ctx, 2, derivative, v, m)

def nome(ctx, m):
    m = ctx.convert(m)
    if not m:
        return m
    if m == ctx.one:
        return m
    if ctx.isnan(m):
        return m
    if ctx.isinf(m):
        if m == ctx.ninf:
            return type(m)(-1)
        else:
            return ctx.mpc(-1)
    a = ctx.ellipk(ctx.one-m)
    b = ctx.ellipk(m)
    v = ctx.exp(-mp.pi*a/b)
    if not ctx._im(m) and ctx._re(m) < 1:
        if ctx._is_real_type(m):
            return v.real
        else:
            return v.real + 0j
    elif m == 2:
        v = ctx.mpc(0, v.imag)
    return v

@defun_wrapped
def qfrom(ctx, q=None, m=None, k=None, tau=None, qbar=None):

    if q is not None:
        return ctx.convert(q)
    if m is not None:
        return nome(ctx, m)
    if k is not None:
        return nome(ctx, ctx.convert(k)**2)
    if tau is not None:
        return ctx.expjpi(tau)
    if qbar is not None:
        return ctx.sqrt(qbar)

@defun_wrapped
def qbarfrom(ctx, q=None, m=None, k=None, tau=None, qbar=None):
    if qbar is not None:
        return ctx.convert(qbar)
    if q is not None:
        return ctx.convert(q) ** 2
    if m is not None:
        return nome(ctx, m) ** 2
    if k is not None:
        return nome(ctx, ctx.convert(k)**2) ** 2
    if tau is not None:
        return ctx.expjpi(2*tau)

@defun_wrapped
def taufrom(ctx, q=None, m=None, k=None, tau=None, qbar=None):
    if tau is not None:
        return ctx.convert(tau)
    if m is not None:
        m = ctx.convert(m)
        return ctx.j*ctx.ellipk(1-m)/ctx.ellipk(m)
    if k is not None:
        k = ctx.convert(k)
        return ctx.j*ctx.ellipk(1-k**2)/ctx.ellipk(k**2)
    if q is not None:
        return ctx.log(q) / (mp.pi*ctx.j)
    if qbar is not None:
        qbar = ctx.convert(qbar)
        return ctx.log(qbar) / (2*mp.pi*ctx.j)

@defun_wrapped
def kfrom(ctx, q=None, m=None, k=None, tau=None, qbar=None):
    if k is not None:
        return ctx.convert(k)
    if m is not None:
        return ctx.sqrt(m)
    if tau is not None:
        q = ctx.expjpi(tau)
    if qbar is not None:
        q = ctx.sqrt(qbar)
    if q == 1:
        return q
    if q == -1:
        return ctx.mpc(0,'inf')
    return (ctx.jtheta(2,0,q)/ctx.jtheta(3,0,q))**2

@defun_wrapped
def mfrom(ctx, q=None, m=None, k=None, tau=None, qbar=None):
    if m is not None:
        return m
    if k is not None:
        return k**2
    if tau is not None:
        q = ctx.expjpi(tau)
    if qbar is not None:
        q = ctx.sqrt(qbar)
    if q == 1:
        return ctx.convert(q)
    if q == -1:
        return q*ctx.inf
    v = (ctx.jtheta(2,0,q)/ctx.jtheta(3,0,q))**4
    if ctx._is_real_type(q) and q < 0:
        v = v.real
    return v

jacobi_spec = {
  'sn' : ([3],[2],[1],[4], 'sin', 'tanh'),
  'cn' : ([4],[2],[2],[4], 'cos', 'sech'),
  'dn' : ([4],[3],[3],[4], '1', 'sech'),
  'ns' : ([2],[3],[4],[1], 'csc', 'coth'),
  'nc' : ([2],[4],[4],[2], 'sec', 'cosh'),
  'nd' : ([3],[4],[4],[3], '1', 'cosh'),
  'sc' : ([3],[4],[1],[2], 'tan', 'sinh'),
  'sd' : ([3,3],[2,4],[1],[3], 'sin', 'sinh'),
  'cd' : ([3],[2],[2],[3], 'cos', '1'),
  'cs' : ([4],[3],[2],[1], 'cot', 'csch'),
  'dc' : ([2],[3],[3],[2], 'sec', '1'),
  'ds' : ([2,4],[3,3],[3],[1], 'csc', 'csch'),
  'cc' : None,
  'ss' : None,
  'nn' : None,
  'dd' : None
}

@defun
def ellipfun(ctx, kind, u=None, m=None, q=None, k=None, tau=None):
    try:
        S = jacobi_spec[kind]
    except KeyError:
        raise ValueError("First argument must be a two-character string "
            "containing 's', 'c', 'd' or 'n', e.g.: 'sn'")
    if u is None:
        def f(*args, **kwargs):
            return ctx.ellipfun(kind, *args, **kwargs)
        f.__name__ = kind
        return f
    prec = ctx.prec
    try:
        ctx.prec += 10
        u = ctx.convert(u)
        q = ctx.qfrom(m=m, q=q, k=k, tau=tau)
        if S is None:
            v = ctx.one + 0*q*u
        elif q == ctx.zero:
            if S[4] == '1': v = ctx.one
            else:           v = getattr(ctx, S[4])(u)
            v += 0*q*u
        elif q == ctx.one:
            if S[5] == '1': v = ctx.one
            else:           v = getattr(ctx, S[5])(u)
            v += 0*q*u
        else:
            t = u / ctx.jtheta(3, 0, q)**2
            v = ctx.one
            for a in S[0]: v *= ctx.jtheta(a, 0, q)
            for b in S[1]: v /= ctx.jtheta(b, 0, q)
            for c in S[2]: v *= ctx.jtheta(c, t, q)
            for d in S[3]: v /= ctx.jtheta(d, t, q)
    finally:
        ctx.prec = prec
    return +v

@defun_wrapped
def kleinj(ctx, tau=None, **kwargs):
    q = ctx.qfrom(tau=tau, **kwargs)
    t2 = ctx.jtheta(2,0,q)
    t3 = ctx.jtheta(3,0,q)
    t4 = ctx.jtheta(4,0,q)
    P = (t2**8 + t3**8 + t4**8)**3
    Q = 54*(t2*t3*t4)**8
    return P/Q


def RF_calc(ctx, x, y, z, r):
    if y == z: return RC_calc(ctx, x, y, r)
    if x == z: return RC_calc(ctx, y, x, r)
    if x == y: return RC_calc(ctx, z, x, r)
    if not (ctx.isnormal(x) and ctx.isnormal(y) and ctx.isnormal(z)):
        if ctx.isnan(x) or ctx.isnan(y) or ctx.isnan(z):
            return x*y*z
        if ctx.isinf(x) or ctx.isinf(y) or ctx.isinf(z):
            return ctx.zero
    xm,ym,zm = x,y,z
    A0 = Am = (x+y+z)/3
    Q = ctx.root(3*r, -6)*max(abs(A0-x),abs(A0-y),abs(A0-z))
    g = ctx.mpf(0.25)
    pow4 = ctx.one
    m = 0
    while 1:
        xs = ctx.sqrt(xm)
        ys = ctx.sqrt(ym)
        zs = ctx.sqrt(zm)
        lm = xs*ys + xs*zs + ys*zs
        Am1 = (Am+lm)*g
        xm, ym, zm = (xm+lm)*g, (ym+lm)*g, (zm+lm)*g
        if pow4*Q < abs(Am):
            break
        Am = Am1
        m += 1
        pow4 *= g
    t = pow4/Am
    X = (A0-x)*t
    Y = (A0-y)*t
    Z = -X-Y
    E2 = X*Y-Z**2
    E3 = X*Y*Z
    return ctx.power(Am,-0.5)*(9240-924*E2+385*E2**2+660*E3-630*E2*E3)/9240

def RC_calc(ctx, x, y, r, pv=True):
    if not (ctx.isnormal(x) and ctx.isnormal(y)):
        if ctx.isinf(x) or ctx.isinf(y):
            return 1/(x*y)
        if y == 0:
            return ctx.inf
        if x == 0:
            return mp.pi / ctx.sqrt(y) / 2
        raise ValueError
    if pv and ctx._im(y) == 0 and ctx._re(y) < 0:
        return ctx.sqrt(x/(x-y))*RC_calc(ctx, x-y, -y, r)
    if x == y:
        return 1/ctx.sqrt(x)
    extraprec = 2*max(0,-ctx.mag(x-y)+ctx.mag(x))
    ctx.prec += extraprec
    if ctx._is_real_type(x) and ctx._is_real_type(y):
        x = ctx._re(x)
        y = ctx._re(y)
        a = ctx.sqrt(x/y)
        if x < y:
            b = ctx.sqrt(y-x)
            v = ctx.acos(a)/b
        else:
            b = ctx.sqrt(x-y)
            v = ctx.acosh(a)/b
    else:
        sx = ctx.sqrt(x)
        sy = ctx.sqrt(y)
        v = ctx.acos(sx/sy)/(ctx.sqrt((1-x/y))*sy)
    ctx.prec -= extraprec
    return v

def RJ_calc(ctx, x, y, z, p, r):
    if not (ctx.isnormal(x) and ctx.isnormal(y) and \
        ctx.isnormal(z) and ctx.isnormal(p)):
        if ctx.isnan(x) or ctx.isnan(y) or ctx.isnan(z) or ctx.isnan(p):
            return x*y*z
        if ctx.isinf(x) or ctx.isinf(y) or ctx.isinf(z) or ctx.isinf(p):
            return ctx.zero
    if not p:
        return ctx.inf
    xm,ym,zm,pm = x,y,z,p
    A0 = Am = (x + y + z + 2*p)/5
    delta = (p-x)*(p-y)*(p-z)
    Q = ctx.root(0.25*r, -6)*max(abs(A0-x),abs(A0-y),abs(A0-z),abs(A0-p))
    m = 0
    g = ctx.mpf(0.25)
    pow4 = ctx.one
    S = 0
    while 1:
        sx = ctx.sqrt(xm)
        sy = ctx.sqrt(ym)
        sz = ctx.sqrt(zm)
        sp = ctx.sqrt(pm)
        lm = sx*sy + sx*sz + sy*sz
        Am1 = (Am+lm)*g
        xm = (xm+lm)*g; ym = (ym+lm)*g; zm = (zm+lm)*g; pm = (pm+lm)*g
        dm = (sp+sx)*(sp+sy)*(sp+sz)
        em = delta*ctx.power(4, -3*m) / dm**2
        if pow4*Q < abs(Am):
            break
        T = RC_calc(ctx, ctx.one, ctx.one+em, r)*pow4 / dm
        S += T
        pow4 *= g
        m += 1
        Am = Am1
    t = ctx.ldexp(1,-2*m) / Am
    X = (A0-x)*t
    Y = (A0-y)*t
    Z = (A0-z)*t
    P = (-X-Y-Z)/2
    E2 = X*Y + X*Z + Y*Z - 3*P**2
    E3 = X*Y*Z + 2*E2*P + 4*P**3
    E4 = (2*X*Y*Z + E2*P + 3*P**3)*P
    E5 = X*Y*Z*P**2
    P = 24024 - 5148*E2 + 2457*E2**2 + 4004*E3 - 4158*E2*E3 - 3276*E4 + 2772*E5
    Q = 24024
    v1 = g**m*ctx.power(Am, -1.5)*P/Q
    v2 = 6*S
    return v1 + v2

@defun
def elliprf(ctx, x, y, z):
    x = ctx.convert(x)
    y = ctx.convert(y)
    z = ctx.convert(z)
    prec = ctx.prec
    try:
        ctx.prec += 20
        tol = mp.eps*2**10
        v = RF_calc(ctx, x, y, z, tol)
    finally:
        ctx.prec = prec
    return +v

@defun
def elliprc(ctx, x, y, pv=True):
    x = ctx.convert(x)
    y = ctx.convert(y)
    prec = ctx.prec
    try:
        ctx.prec += 20
        tol = mpf(eps)*2**10
        v = RC_calc(ctx, x, y, tol, pv)
    finally:
        ctx.prec = prec
    return +v

@defun
def elliprj(ctx, x, y, z, p):
    x = ctx.convert(x)
    y = ctx.convert(y)
    z = ctx.convert(z)
    p = ctx.convert(p)
    prec = ctx.prec
    try:
        ctx.prec += 20
        tol = mp.eps*2**10
        v = RJ_calc(ctx, x, y, z, p, tol)
    finally:
        ctx.prec = prec
    return +v

@defun
def elliprd(ctx, x, y, z):
    return ctx.elliprj(x,y,z,z)

@defun
def elliprg(ctx, x, y, z):
    x = ctx.convert(x)
    y = ctx.convert(y)
    z = ctx.convert(z)
    zeros = (not x) + (not y) + (not z)
    if zeros == 3:
        return (x+y+z)*0
    if zeros == 2:
        if x: return 0.5*ctx.sqrt(x)
        if y: return 0.5*ctx.sqrt(y)
        return 0.5*ctx.sqrt(z)
    if zeros == 1:
        if not z:
            x, z = z, x
    def terms():
        T1 = 0.5*z*ctx.elliprf(x,y,z)
        T2 = -0.5*(x-z)*(y-z)*ctx.elliprd(x,y,z)/3
        T3 = 0.5*ctx.sqrt(x)*ctx.sqrt(y)/ctx.sqrt(z)
        return T1,T2,T3
    return ctx.sum_accurately(terms)

@defun_wrapped
def ellipf(ctx, phi, m):
    z = phi
    if not (ctx.isnormal(z) and ctx.isnormal(m)):
        if m == 0:
            return z + m
        if z == 0:
            return z*m
        if m == ctx.inf or m == ctx.ninf: return z/m
        raise ValueError
    x = z.real
    ctx.prec += max(0, ctx.mag(x))
    pi = +mp.pi
    away = abs(x) > pi/2
    if m == 1:
        if away:
            return ctx.inf
    if away:
        d = ctx.nint(x/pi)
        z = z-pi*d
        P = 2*d*ctx.ellipk(m)
    else:
        P = 0
    c, s = ctx.cos_sin(z)
    return s*ctx.elliprf(c**2, 1-m*s**2, 1) + P

@defun_wrapped
def ellipe(ctx, *args):

    if len(args) == 1:
        return ctx._ellipe(args[0])
    else:
        phi, m = args
    z = phi
    if not (ctx.isnormal(z) and ctx.isnormal(m)):
        if m == 0:
            return z + m
        if z == 0:
            return z*m
        if m == ctx.inf or m == ctx.ninf:
            return ctx.inf
        raise ValueError
    x = z.real
    ctx.prec += max(0, ctx.mag(x))
    pi = +mp.pi
    away = abs(x) > pi/2
    if away:
        d = ctx.nint(x/pi)
        z = z-pi*d
        P = 2*d*ctx.ellipe(m)
    else:
        P = 0
    def terms():
        c, s = ctx.cos_sin(z)
        x = c**2
        y = 1-m*s**2
        RF = ctx.elliprf(x, y, 1)
        RD = ctx.elliprd(x, y, 1)
        return s*RF, -m*s**3*RD/3
    return ctx.sum_accurately(terms) + P

@defun_wrapped
def ellippi(ctx, *args):
    if len(args) == 2:
        n, m = args
        complete = True
        z = phi = mp.pi/2
    else:
        n, phi, m = args
        complete = False
        z = phi
    if not (ctx.isnormal(n) and ctx.isnormal(z) and ctx.isnormal(m)):
        if ctx.isnan(n) or ctx.isnan(z) or ctx.isnan(m):
            raise ValueError
        if complete:
            if m == 0:
                if n == 1:
                    return ctx.inf
                return mp.pi/(2*ctx.sqrt(1-n))
            if n == 0: return ctx.ellipk(m)
            if ctx.isinf(n) or ctx.isinf(m): return ctx.zero
        else:
            if z == 0: return z
            if ctx.isinf(n): return ctx.zero
            if ctx.isinf(m): return ctx.zero
        if ctx.isinf(n) or ctx.isinf(z) or ctx.isinf(m):
            raise ValueError
    if complete:
        if m == 1:
            if n == 1:
                return ctx.inf
            return -ctx.inf/ctx.sign(n-1)
        away = False
    else:
        x = z.real
        ctx.prec += max(0, ctx.mag(x))
        pi = +mp.pi
        away = abs(x) > pi/2
    if away:
        d = ctx.nint(x/pi)
        z = z-pi*d
        P = 2*d*ctx.ellippi(n,m)
        if ctx.isinf(P):
            return ctx.inf
    else:
        P = 0
    def terms():
        if complete:
            c, s = ctx.zero, ctx.one
        else:
            c, s = ctx.cos_sin(z)
        x = c**2
        y = 1-m*s**2
        RF = ctx.elliprf(x, y, 1)
        RJ = ctx.elliprj(x, y, 1, 1-n*s**2)
        return s*RF, n*s**3*RJ/3
    return ctx.sum_accurately(terms) + P

@defun_wrapped
def _erf_complex(ctx, z):
    z2 = ctx.square_exp_arg(z, -1)
    v = (2/ctx.sqrt(mp.pi))*z*ctx.hyp1f1((1,2),(3,2), z2)
    if not ctx._re(z):
        v = ctx._im(v)*ctx.j
    return v

@defun_wrapped
def _erfc_complex(ctx, z):
    if ctx.re(z) > 2:
        z2 = ctx.square_exp_arg(z)
        nz2 = ctx.fneg(z2, exact=True)
        v = ctx.exp(nz2)/ctx.sqrt(mp.pi)*ctx.hyperu((1,2),(1,2), z2)
    else:
        v = 1 - ctx._erf_complex(z)
    if not ctx._re(z):
        v = 1+ctx._im(v)*ctx.j
    return v

@defun
def erf(ctx, z):
    z = ctx.convert(z)
    if ctx._is_real_type(z):
        try:
            return ctx._erf(z)
        except NotImplementedError:
            pass
    if ctx._is_complex_type(z) and not z.imag:
        try:
            return type(z)(ctx._erf(z.real))
        except NotImplementedError:
            pass
    return ctx._erf_complex(z)

@defun
def erfc(ctx, z):
    z = ctx.convert(z)
    if ctx._is_real_type(z):
        try:
            return ctx._erfc(z)
        except NotImplementedError:
            pass
    if ctx._is_complex_type(z) and not z.imag:
        try:
            return type(z)(ctx._erfc(z.real))
        except NotImplementedError:
            pass
    return ctx._erfc_complex(z)

@defun
def square_exp_arg(ctx, z, mult=1, reciprocal=False):
    prec = ctx.prec*4+20
    if reciprocal:
        z2 = ctx.fmul(z, z, prec=prec)
        z2 = ctx.fdiv(ctx.one, z2, prec=prec)
    else:
        z2 = ctx.fmul(z, z, prec=prec)
    if mult != 1:
        z2 = ctx.fmul(z2, mult, exact=True)
    return z2

@defun_wrapped
def erfi(ctx, z):
    if not z:
        return z
    z2 = ctx.square_exp_arg(z)
    v = (2/ctx.sqrt(mp.pi)*z)*ctx.hyp1f1((1,2), (3,2), z2)
    if not ctx._re(z):
        v = ctx._im(v)*ctx.j
    return v

@defun_wrapped
def erfinv(ctx, x):
    xre = ctx._re(x)
    if (xre != x) or (xre < -1) or (xre > 1):
        return ctx.bad_domain("erfinv(x) is defined only for -1 <= x <= 1")
    x = xre
    if not x: return x
    if x == 1: return ctx.inf
    if x == -1: return ctx.ninf
    if abs(x) < 0.9:
        a = 0.53728*x**3 + 0.813198*x
    else:
        u = ctx.ln(2/mp.pi/(abs(x)-1)**2)
        a = ctx.sign(x)*ctx.sqrt(u - ctx.ln(u))/ctx.sqrt(2)
    ctx.prec += 10
    return ctx.findroot(lambda t: ctx.erf(t)-x, a)

@defun_wrapped
def npdf(ctx, x, mu=0, sigma=1):
    sigma = ctx.convert(sigma)
    return ctx.exp(-(x-mu)**2/(2*sigma**2)) / (sigma*ctx.sqrt(2*mp.pi))

@defun_wrapped
def ncdf(ctx, x, mu=0, sigma=1):
    a = (x-mu)/(sigma*ctx.sqrt(2))
    if a < 0:
        return ctx.erfc(-a)/2
    else:
        return (1+ctx.erf(a))/2

@defun_wrapped
def betainc(ctx, a, b, x1=0, x2=1, regularized=False):
    if x1 == x2:
        v = 0
    elif not x1:
        if x1 == 0 and x2 == 1:
            v = ctx.beta(a, b)
        else:
            v = x2**a*ctx.hyp2f1(a, 1-b, a+1, x2) / a
    else:
        m, d = ctx.nint_distance(a)
        if m <= 0:
            if d < -ctx.prec:
                h = +mp.eps
                ctx.prec *= 2
                a += h
            elif d < -4:
                ctx.prec -= d
        s1 = x2**a*ctx.hyp2f1(a,1-b,a+1,x2)
        s2 = x1**a*ctx.hyp2f1(a,1-b,a+1,x1)
        v = (s1 - s2) / a
    if regularized:
        v /= ctx.beta(a,b)
    return v

@defun
def gammainc(ctx, z, a=0, b=None, regularized=False):
    regularized = bool(regularized)
    z = ctx.convert(z)
    if a is None:
        a = ctx.zero
        lower_modified = False
    else:
        a = ctx.convert(a)
        lower_modified = a != ctx.zero
    if b is None:
        b = ctx.inf
        upper_modified = False
    else:
        b = ctx.convert(b)
        upper_modified = b != ctx.inf
    if not (upper_modified or lower_modified):
        if regularized:
            if ctx.re(z) < 0:
                return ctx.inf
            elif ctx.re(z) > 0:
                return ctx.one
            else:
                return ctx.nan
        return ctx.gamma(z)
    if a == b:
        return ctx.zero
    if ctx.re(a) > ctx.re(b):
        return -ctx.gammainc(z, b, a, regularized)
    if upper_modified and lower_modified:
        return +ctx._gamma3(z, a, b, regularized)
    elif lower_modified:
        return ctx._upper_gamma(z, a, regularized)
    elif upper_modified:
        return ctx._lower_gamma(z, b, regularized)

@defun
def _lower_gamma(ctx, z, b, regularized=False):
    if ctx.isnpint(z):
        return type(z)(ctx.inf)
    G = [z]*regularized
    negb = ctx.fneg(b, exact=True)
    def h(z):
        T1 = [ctx.exp(negb), b, z], [1, z, -1], [], G, [1], [1+z], b
        return (T1,)
    return ctx.hypercomb(h, [z])

@defun
def _upper_gamma(ctx, z, a, regularized=False):
    if ctx.isint(z):
        try:
            if regularized:
                if ctx.isnpint(z):
                    return type(z)(ctx.zero)
                orig = ctx.prec
                try:
                    ctx.prec += 10
                    return ctx._gamma_upper_int(z, a) / ctx.gamma(z)
                finally:
                    ctx.prec = orig
            else:
                return ctx._gamma_upper_int(z, a)
        except NotImplementedError:
            pass
    if z == 2 and a == -1:
        return (z+a)*0
    if z == 3 and (a == -1-1j or a == -1+1j):
        return (z+a)*0
    nega = ctx.fneg(a, exact=True)
    G = [z]*regularized
    try:
        def h(z):
            r = z-1
            return [([ctx.exp(nega), a], [1, r], [], G, [1, -r], [], 1/nega)]
        return ctx.hypercomb(h, [z], force_series=True)
    except ctx.NoConvergence:
        def h(z):
            T1 = [], [1, z-1], [z], G, [], [], 0
            T2 = [-ctx.exp(nega), a, z], [1, z, -1], [], G, [1], [1+z], a
            return T1, T2
        return ctx.hypercomb(h, [z])

@defun
def _gamma3(ctx, z, a, b, regularized=False):
    pole = ctx.isnpint(z)
    if regularized and pole:
        return ctx.zero
    try:
        ctx.prec += 15
        T1 = ctx.gammainc(z, a, regularized=regularized)
        T2 = ctx.gammainc(z, b, regularized=regularized)
        R = T1 - T2
        if ctx.mag(R) - max(ctx.mag(T1), ctx.mag(T2)) > -10:
            return R
        if not pole:
            T1 = ctx.gammainc(z, 0, b, regularized=regularized)
            T2 = ctx.gammainc(z, 0, a, regularized=regularized)
            R = T1 - T2
            if 1:
                return R
    finally:
        ctx.prec -= 15
    raise NotImplementedError

@defun_wrapped
def expint(ctx, n, z):
    if ctx.isint(n) and ctx._is_real_type(z):
        try:
            return ctx._expint_int(n, z)
        except NotImplementedError:
            pass
    if ctx.isnan(n) or ctx.isnan(z):
        return z*n
    if z == ctx.inf:
        return 1/z
    if z == 0:
        if ctx.re(n) <= 1:
            return type(z)(ctx.inf)
        else:
            return ctx.one/(n-1)
    if n == 0:
        return ctx.exp(-z)/z
    if n == -1:
        return ctx.exp(-z)*(z+1)/z**2
    return z**(n-1)*ctx.gammainc(1-n, z)

@defun_wrapped
def li(ctx, z, offset=False):
    if offset:
        if z == 2:
            return ctx.zero
        return ctx.ei(ctx.ln(z)) - ctx.ei(ctx.ln2)
    if not z:
        return z
    if z == 1:
        return ctx.ninf
    return ctx.ei(ctx.ln(z))

@defun
def ei(ctx, z):
    try:
        return ctx._ei(z)
    except NotImplementedError:
        return ctx._ei_generic(z)

@defun_wrapped
def _ei_generic(ctx, z):
    if z == ctx.inf:
        return z
    if z == ctx.ninf:
        return ctx.zero
    if ctx.mag(z) > 1:
        try:
            r = ctx.one/z
            v = ctx.exp(z)*ctx.hyper([1,1],[],r,
                maxterms=ctx.prec, force_series=True)/z
            im = ctx._im(z)
            if im > 0:
                v += mp.pi*ctx.j
            if im < 0:
                v -= mp.pi*ctx.j
            return v
        except ctx.NoConvergence:
            pass
    v = z*ctx.hyp2f2(1,1,2,2,z) + ctx.euler
    if ctx._im(z):
        v += 0.5*(ctx.log(z) - ctx.log(ctx.one/z))
    else:
        v += ctx.log(abs(z))
    return v

@defun
def e1(ctx, z):
    try:
        return ctx._e1(z)
    except NotImplementedError:
        return ctx.expint(1, z)

@defun
def ci(ctx, z):
    try:
        return ctx._ci(z)
    except NotImplementedError:
        return ctx._ci_generic(z)

@defun_wrapped
def _ci_generic(ctx, z):
    if ctx.isinf(z):
        if z == ctx.inf: return ctx.zero
        if z == ctx.ninf: return mp.pi*1j
    jz = ctx.fmul(ctx.j,z,exact=True)
    njz = ctx.fneg(jz,exact=True)
    v = 0.5*(ctx.ei(jz) + ctx.ei(njz))
    zreal = ctx._re(z)
    zimag = ctx._im(z)
    if zreal == 0:
        if zimag > 0: v += mp.pi*0.5j
        if zimag < 0: v -= mp.pi*0.5j
    if zreal < 0:
        if zimag >= 0: v += mp.pi*1j
        if zimag <  0: v -= mp.pi*1j
    if ctx._is_real_type(z) and zreal > 0:
        v = ctx._re(v)
    return v

@defun
def si(ctx, z):
    try:
        return ctx._si(z)
    except NotImplementedError:
        return ctx._si_generic(z)

@defun_wrapped
def _si_generic(ctx, z):
    if ctx.isinf(z):
        if z == ctx.inf: return 0.5*mp.pi
        if z == ctx.ninf: return -0.5*mp.pi
    if ctx.mag(z) >= -1:
        jz = ctx.fmul(ctx.j,z,exact=True)
        njz = ctx.fneg(jz,exact=True)
        v = (-0.5j)*(ctx.ei(jz) - ctx.ei(njz))
        zreal = ctx._re(z)
        if zreal > 0:
            v -= 0.5*mp.pi
        if zreal < 0:
            v += 0.5*mp.pi
        if ctx._is_real_type(z):
            v = ctx._re(v)
        return v
    else:
        return z*ctx.hyp1f2((1,2),(3,2),(3,2),-0.25*z*z)

@defun_wrapped
def chi(ctx, z):
    nz = ctx.fneg(z, exact=True)
    v = 0.5*(ctx.ei(z) + ctx.ei(nz))
    zreal = ctx._re(z)
    zimag = ctx._im(z)
    if zimag > 0:
        v += mp.pi*0.5j
    elif zimag < 0:
        v -= mp.pi*0.5j
    elif zreal < 0:
        v += mp.pi*1j
    return v

@defun_wrapped
def shi(ctx, z):
    if ctx.mag(z) >= -1:
        nz = ctx.fneg(z, exact=True)
        v = 0.5*(ctx.ei(z) - ctx.ei(nz))
        zimag = ctx._im(z)
        if zimag > 0: v -= 0.5j*mp.pi
        if zimag < 0: v += 0.5j*mp.pi
        return v
    else:
        return z*ctx.hyp1f2((1,2),(3,2),(3,2),0.25*z*z)

@defun_wrapped
def fresnels(ctx, z):
    if z == ctx.inf:
        return ctx.mpf(0.5)
    if z == ctx.ninf:
        return ctx.mpf(-0.5)
    return mp.pi*z**3/6*ctx.hyp1f2((3,4),(3,2),(7,4),-mp.pi**2*z**4/16)

@defun_wrapped
def fresnelc(ctx, z):
    if z == ctx.inf:
        return ctx.mpf(0.5)
    if z == ctx.ninf:
        return ctx.mpf(-0.5)
    return z*ctx.hyp1f2((1,4),(1,2),(5,4),-mp.pi**2*z**4/16)

@defun
def gammaprod(ctx, a, b, _infsign=False):
    a = [ctx.convert(x) for x in a]
    b = [ctx.convert(x) for x in b]
    poles_num = []
    poles_den = []
    regular_num = []
    regular_den = []
    for x in a: [regular_num, poles_num][ctx.isnpint(x)].append(x)
    for x in b: [regular_den, poles_den][ctx.isnpint(x)].append(x)
    if len(poles_num) < len(poles_den): return ctx.zero
    if len(poles_num) > len(poles_den):
        if _infsign:
            a = [x and x*(1+mp.eps) or x+mp.eps for x in poles_num]
            b = [x and x*(1+mp.eps) or x+mp.eps for x in poles_den]
            return ctx.sign(ctx.gammaprod(a+regular_num,b+regular_den))*ctx.inf
        else:
            return ctx.inf
    p = ctx.one
    orig = ctx.prec
    try:
        ctx.prec = orig + 15
        while poles_num:
            i = poles_num.pop()
            j = poles_den.pop()
            p *= (-1)**(i+j)*ctx.gamma(1-j) / ctx.gamma(1-i)
        for x in regular_num: p *= ctx.gamma(x)
        for x in regular_den: p /= ctx.gamma(x)
    finally:
        ctx.prec = orig
    return +p

@defun
def beta(ctx, x, y):
    x = ctx.convert(x)
    y = ctx.convert(y)
    if ctx.isinf(y):
        x, y = y, x
    if ctx.isinf(x):
        if x == ctx.inf and not ctx._im(y):
            if y == ctx.ninf:
                return ctx.nan
            if y > 0:
                return ctx.zero
            if ctx.isint(y):
                return ctx.nan
            if y < 0:
                return ctx.sign(ctx.gamma(y))*ctx.inf
        return ctx.nan
    return ctx.gammaprod([x, y], [x+y])

@defun
def binomial(ctx, n, k):
    return ctx.gammaprod([n+1], [k+1, n-k+1])

@defun
def rf(ctx, x, n):
    return ctx.gammaprod([x+n], [x])

@defun
def ff(ctx, x, n):
    return ctx.gammaprod([x+1], [x-n+1])

@defun_wrapped
def fac2(ctx, x):
    if ctx.isinf(x):
        if x == ctx.inf:
            return x
        return ctx.nan
    return 2**(x/2)*(mp.pi/2)**((ctx.cospi(x)-1)/4)*ctx.gamma(x/2+1)

@defun_wrapped
def barnesg(ctx, z):
    if ctx.isinf(z):
        if z == ctx.inf:
            return z
        return ctx.nan
    if ctx.isnan(z):
        return z
    if (not ctx._im(z)) and ctx._re(z) <= 0 and ctx.isint(ctx._re(z)):
        return z*0
    if abs(z) > 5:
        ctx.dps += 2*ctx.log(abs(z),2)
    if ctx.re(z) < -ctx.dps:
        w = 1-z
        pi2 = 2*mp.pi
        u = ctx.expjpi(2*w)
        v = ctx.j*mp.pi/12 - ctx.j*mp.pi*w**2/2 + w*ctx.ln(1-u) - \
            ctx.j*ctx.polylog(2, u)/pi2
        v = ctx.barnesg(2-z)*ctx.exp(v)/pi2**w
        if ctx._is_real_type(z):
            v = ctx._re(v)
        return v
    N = ctx.dps // 2 + 5
    G = 1
    while abs(z) < N or ctx.re(z) < 1:
        G /= ctx.gamma(z)
        z += 1
    z -= 1
    s = ctx.mpf(1)/12
    s -= ctx.log(ctx.glaisher)
    s += z*ctx.log(2*mp.pi)/2
    s += (z**2/2-ctx.mpf(1)/12)*ctx.log(z)
    s -= 3*z**2/4
    z2k = z2 = z**2
    for k in range(1, N+1):
        t = ctx.bernoulli(2*k+2) / (4*k*(k+1)*z2k)
        if abs(t) < mp.eps:
            break
        z2k *= z2
        s += t
    return G*ctx.exp(s)

@defun
def superfac(ctx, z):
    return ctx.barnesg(z+2)

@defun_wrapped
def hyperfac(ctx, z):
    if z == ctx.inf:
        return z
    if abs(z) > 5:
        extra = 4*int(ctx.log(abs(z),2))
    else:
        extra = 0
    ctx.prec += extra
    if not ctx._im(z) and ctx._re(z) < 0 and ctx.isint(ctx._re(z)):
        n = int(ctx.re(z))
        h = ctx.hyperfac(-n-1)
        if ((n+1)//2) & 1:
            h = -h
        if ctx._is_complex_type(z):
            return h + 0j
        return h
    zp1 = z+1
    v = ctx.exp(z*ctx.loggamma(zp1))
    ctx.prec -= extra
    return v / ctx.barnesg(zp1)

@defun_wrapped
def loggamma_old(ctx, z):
    a = ctx._re(z)
    b = ctx._im(z)
    if not b and a > 0:
        return ctx.ln(ctx.gamma_old(z))
    u = ctx.arg(z)
    w = ctx.ln(ctx.gamma_old(z))
    if b:
        gi = -b - u/2 + a*u + b*ctx.ln(abs(z))
        n = ctx.floor((gi-ctx._im(w))/(2*mp.pi)+0.5)*(2*mp.pi)
        return w + n*ctx.j
    elif a < 0:
        n = int(ctx.floor(a))
        w += (n-(n%2))*mp.pi*ctx.j
    return w

@defun_wrapped
def cot(ctx, z): return ctx.one / ctx.tan(z)

@defun_wrapped
def sec(ctx, z): return ctx.one / ctx.cos(z)

@defun_wrapped
def csc(ctx, z): return ctx.one / ctx.sin(z)

@defun_wrapped
def coth(ctx, z): return ctx.one / ctx.tanh(z)

@defun_wrapped
def sech(ctx, z): return ctx.one / ctx.cosh(z)

@defun_wrapped
def csch(ctx, z): return ctx.one / ctx.sinh(z)

@defun_wrapped
def acot(ctx, z):
    if not z:
        return mp.pi*0.5
    else:
        return ctx.atan(ctx.one / z)

@defun_wrapped
def asec(ctx, z): return ctx.acos(ctx.one / z)

@defun_wrapped
def acsc(ctx, z): return ctx.asin(ctx.one / z)

@defun_wrapped
def acoth(ctx, z):
    if not z:
        return mp.pi*0.5j
    else:
        return ctx.atanh(ctx.one / z)

@defun_wrapped
def asech(ctx, z): return ctx.acosh(ctx.one / z)

@defun_wrapped
def acsch(ctx, z): return ctx.asinh(ctx.one / z)

@defun
def sign(ctx, x):
    x = ctx.convert(x)
    if not x or ctx.isnan(x):
        return x
    if ctx._is_real_type(x):
        if x > 0:
            return ctx.one
        else:
            return -ctx.one
    return x / abs(x)

@defun
def agm(ctx, a, b=1):
    if b == 1:
        return ctx.agm1(a)
    a = ctx.convert(a)
    b = ctx.convert(b)
    return ctx._agm(a, b)

@defun_wrapped
def sinc(ctx, x):
    if ctx.isinf(x):
        return 1/x
    if not x:
        return x+1
    return ctx.sin(x)/x

@defun_wrapped
def sincpi(ctx, x):
    if ctx.isinf(x):
        return 1/x
    if not x:
        return x+1
    return ctx.sinpi(x)/(mp.pi*x)

@defun_wrapped
def expm1(ctx, x):
    if not x:
        return ctx.zero
    if ctx.mag(x) < -ctx.prec:
        return x + 0.5*x**2
    return ctx.sum_accurately(lambda: iter([ctx.exp(x),-1]),1)

@defun_wrapped
def powm1(ctx, x, y):
    mag = ctx.mag
    one = ctx.one
    w = x**y - one
    M = mag(w)
    if M > -8:
        return w
    if not w:
        if (not y) or (x in (1, -1, 1j, -1j) and ctx.isint(y)):
            return w
    x1 = x - one
    magy = mag(y)
    lnx = ctx.ln(x)
    if magy + mag(lnx) < -ctx.prec:
        return lnx*y + (lnx*y)**2/2
    return ctx.sum_accurately(lambda: iter([x**y, -1]), 1)

@defun
def _rootof1(ctx, k, n):
    k = int(k)
    n = int(n)
    k %= n
    if not k:
        return ctx.one
    elif 2*k == n:
        return -ctx.one
    elif 4*k == n:
        return ctx.j
    elif 4*k == 3*n:
        return -ctx.j
    return ctx.expjpi(2*ctx.mpf(k)/n)

@defun
def root(ctx, x, n, k=0):
    n = int(n)
    x = ctx.convert(x)
    if k:
        if (n & 1 and 2*k == n-1) and (not ctx.im(x)) and (ctx.re(x) < 0):
            return -ctx.root(-x, n)
        prec = ctx.prec
        try:
            ctx.prec += 10
            v = ctx.root(x, n, 0)*ctx._rootof1(k, n)
        finally:
            ctx.prec = prec
        return +v
    return ctx._nthroot(x, n)

@defun
def unitroots(ctx, n, primitive=False):
    gcd = ctx._gcd
    prec = ctx.prec
    try:
        ctx.prec += 10
        if primitive:
            v = [ctx._rootof1(k,n) for k in range(n) if gcd(k,n) == 1]
        else:
            v = [ctx._rootof1(k,n) for k in range(n)]
    finally:
        ctx.prec = prec
    return [+x for x in v]

@defun
def arg(ctx, x):
    x = ctx.convert(x)
    re = ctx._re(x)
    im = ctx._im(x)
    return ctx.atan2(im, re)

@defun
def fabs(ctx, x):
    return abs(ctx.convert(x))

@defun
def re(ctx, x):
    x = ctx.convert(x)
    if hasattr(x, "real"):
        return x.real
    return x

@defun
def im(ctx, x):
    x = ctx.convert(x)
    if hasattr(x, "imag"):
        return x.imag
    return ctx.zero

@defun
def conj(ctx, x):
    x = ctx.convert(x)
    try:
        return x.conjugate()
    except AttributeError:
        return x

@defun
def polar(ctx, z):
    return (ctx.fabs(z), ctx.arg(z))

@defun_wrapped
def rect(ctx, r, phi):
    return r*ctx.mpc(*ctx.cos_sin(phi))

@defun
def log(ctx, x, b=None):
    if b is None:
        return ctx.ln(x)
    wp = ctx.prec + 20
    return ctx.ln(x, prec=wp) / ctx.ln(b, prec=wp)

@defun
def log10(ctx, x):
    return ctx.log(x, 10)

@defun
def fmod(ctx, x, y):
    return ctx.convert(x) % ctx.convert(y)

@defun
def degrees(ctx, x):
    return x / ctx.degree

@defun
def radians(ctx, x):
    return x*ctx.degree

def _lambertw_special(ctx, z, k):
    if not z:
        if not k:
            return z
        return ctx.ninf + z
    if z == ctx.inf:
        if k == 0:
            return z
        else:
            return z + 2*k*mp.pi*ctx.j
    if z == ctx.ninf:
        return (-z) + (2*k+1)*mp.pi*ctx.j
    return ctx.ln(z)

def _lambertw_approx_hybrid(z, k):
    imag_sign = 0
    if hasattr(z, "imag"):
        x = float(z.real)
        y = z.imag
        if y:
            imag_sign = (-1) ** (y < 0)
        y = float(y)
    else:
        x = float(z)
        y = 0.0
        imag_sign = 0
    if not y:
        y = 0.0
    z = complex(x,y)
    if k == 0:
        if -4.0 < y < 4.0 and -1.0 < x < 2.5:
            if imag_sign:
                if y > 1.00: return (0.876+0.645j) + (0.118-0.174j)*(z-(0.75+2.5j))
                if y > 0.25: return (0.505+0.204j) + (0.375-0.132j)*(z-(0.75+0.5j))
                if y < -1.00: return (0.876-0.645j) + (0.118+0.174j)*(z-(0.75-2.5j))
                if y < -0.25: return (0.505-0.204j) + (0.375+0.132j)*(z-(0.75-0.5j))
            if x < -0.5:
                if imag_sign >= 0:
                    return (-0.318+1.34j) + (-0.697-0.593j)*(z+1)
                else:
                    return (-0.318-1.34j) + (-0.697+0.593j)*(z+1)
            r = -0.367879441171442
            if (not imag_sign) and x > r:
                z = x
            if x < -0.2:
                return -1 + 2.33164398159712*(z-r)**0.5 - 1.81218788563936*(z-r)
            if x < 0.5: return z
            return 0.2 + 0.3*z
        if (not imag_sign) and x > 0.0:
            L1 = math.log(x); L2 = math.log(L1)
        else:
            L1 = cmath.log(z); L2 = cmath.log(L1)
    elif k == -1:
        r = -0.367879441171442
        if (not imag_sign) and r < x < 0.0:
            z = x
        if (imag_sign >= 0) and y < 0.1 and -0.6 < x < -0.2:
            return -1 - 2.33164398159712*(z-r)**0.5 - 1.81218788563936*(z-r)
        if (not imag_sign) and -0.2 <= x < 0.0:
            L1 = math.log(-x)
            return L1 - math.log(-L1)
        else:
            if imag_sign == -1 and (not y) and x < 0.0:
                L1 = cmath.log(z) - 3.1415926535897932j
            else:
                L1 = cmath.log(z) - 6.2831853071795865j
            L2 = cmath.log(L1)
    return L1 - L2 + L2/L1 + L2*(L2-2)/(2*L1**2)

def _lambertw_series(ctx, z, k, tol):
    magz = ctx.mag(z)
    if (-10 < magz < 900) and (-1000 < k < 1000):
        if magz < 1 and abs(z+0.36787944117144) < 0.05:
            if k == 0 or (k == -1 and ctx._im(z) >= 0) or \
                         (k == 1  and ctx._im(z) < 0):
                delta = ctx.sum_accurately(lambda: [z, ctx.exp(-1)])
                cancellation = -ctx.mag(delta)
                ctx.prec += cancellation
                p = ctx.sqrt(2*(ctx.e*z+1))
                ctx.prec -= cancellation
                u = {0:ctx.mpf(-1), 1:ctx.mpf(1)}
                a = {0:ctx.mpf(2), 1:ctx.mpf(-1)}
                if k != 0:
                    p = -p
                s = ctx.zero
                for l in range(max(2,cancellation)):
                    if l not in u:
                        a[l] = ctx.fsum(u[j]*u[l+1-j] for j in range(2,l))
                        u[l] = (l-1)*(u[l-2]/2+a[l-2]/4)/(l+1)-a[l]/2-u[l-1]/(l+1)
                    term = u[l]*p**l
                    s += term
                    if ctx.mag(term) < -tol:
                        return s, True
                    l += 1
                ctx.prec += cancellation//2
                return s, False
        if k == 0 or k == -1:
            return _lambertw_approx_hybrid(z, k), False
    if k == 0:
        if magz < -1:
            return z*(1-z), False
        L1 = ctx.ln(z)
        L2 = ctx.ln(L1)
    elif k == -1 and (not ctx._im(z)) and (-0.36787944117144 < ctx._re(z) < 0):
        L1 = ctx.ln(-z)
        return L1 - ctx.ln(-L1), False
    else:
        L1 = ctx.ln(z) + 2j*mp.pi*k
        L2 = ctx.ln(L1)
    return L1 - L2 + L2/L1 + L2*(L2-2)/(2*L1**2), False

@defun
def lambertw(ctx, z, k=0):
    z = ctx.convert(z)
    k = int(k)
    if not ctx.isnormal(z):
        return _lambertw_special(ctx, z, k)
    prec = ctx.prec
    ctx.prec += 20 + ctx.mag(k or 1)
    wp = ctx.prec
    tol = wp - 5
    w, done = _lambertw_series(ctx, z, k, tol)
    if not done:
        two = ctx.mpf(2)
        for i in range(100):
            ew = ctx.exp(w)
            wew = w*ew
            wewz = wew-z
            wn = w - wewz/(wew+ew-(w+two)*wewz/(two*w+two))
            if ctx.mag(wn-w) <= ctx.mag(wn) - tol:
                w = wn
                break
            else:
                w = wn
        if i == 100:
            ctx.warn("Lambert W iteration failed to converge for z = %s" % z)
    ctx.prec = prec
    return +w

@defun_wrapped
def bell(ctx, n, x=1):
    x = ctx.convert(x)
    if not n:
        if ctx.isnan(x):
            return x
        return type(x)(1)
    if ctx.isinf(x) or ctx.isinf(n) or ctx.isnan(x) or ctx.isnan(n):
        return x**n
    if n == 1: return x
    if n == 2: return x*(x+1)
    if x == 0: return ctx.sincpi(n)
    return _polyexp(ctx, n, x, True) / ctx.exp(x)

def _polyexp(ctx, n, x, extra=False):
    def _terms():
        if extra:
            yield ctx.sincpi(n)
        t = x
        k = 1
        while 1:
            yield k**n*t
            k += 1
            t = t*x/k
    return ctx.sum_accurately(_terms, check_step=4)

@defun_wrapped
def polyexp(ctx, s, z):
    if ctx.isinf(z) or ctx.isinf(s) or ctx.isnan(z) or ctx.isnan(s):
        return z**s
    if z == 0: return z*s
    if s == 0: return ctx.expm1(z)
    if s == 1: return ctx.exp(z)*z
    if s == 2: return ctx.exp(z)*z*(z+1)
    return _polyexp(ctx, s, z)

@defun_wrapped
def cyclotomic(ctx, n, z):
    n = int(n)
    if n < 0:
        raise ValueError("n cannot be negative")
    p = ctx.one
    if n == 0:
        return p
    if n == 1:
        return z - p
    if n == 2:
        return z + p
    a_prod = 1
    b_prod = 1
    num_zeros = 0
    num_poles = 0
    for d in range(1,n+1):
        if not n % d:
            w = ctx.moebius(n//d)
            b = -ctx.powm1(z, d)
            if b:
                p *= b**w
            else:
                if w == 1:
                    a_prod *= d
                    num_zeros += 1
                elif w == -1:
                    b_prod *= d
                    num_poles += 1
    if num_zeros:
        if num_zeros > num_poles:
            p *= 0
        else:
            p *= a_prod
            p /= b_prod
    return p

@defun
def mangoldt(ctx, n):
    n = int(n)
    if n < 2:
        return ctx.zero
    if n % 2 == 0:
        if n & (n-1) == 0:
            return +ctx.ln2
        else:
            return ctx.zero
    for p in (3,5,7,11,13,17,19,23,29,31):
        if not n % p:
            q, r = n // p, 0
            while q > 1:
                q, r = divmod(q, p)
                if r:
                    return ctx.zero
            return ctx.ln(p)
    if ctx.isprime(n):
        return ctx.ln(n)
    if n > 10**30:
        raise NotImplementedError
    k = 2
    while 1:
        p = int(n**(1./k) + 0.5)
        if p < 2:
            return ctx.zero
        if p ** k == n:
            if ctx.isprime(p):
                return ctx.ln(p)
        k += 1

@defun
def stirling1(ctx, n, k, exact=False):
    v = ctx._stirling1(int(n), int(k))
    if exact:
        return int(v)
    else:
        return ctx.mpf(v)

@defun
def stirling2(ctx, n, k, exact=False):
    v = ctx._stirling2(int(n), int(k))
    if exact:
        return int(v)
    else:
        return ctx.mpf(v)

def _check_need_perturb(ctx, terms, prec, discard_known_zeros):
    perturb = recompute = False
    extraprec = 0
    discard = []
    for term_index, term in enumerate(terms):
        w_s, c_s, alpha_s, beta_s, a_s, b_s, z = term
        have_singular_nongamma_weight = False
        for k, w in enumerate(w_s):
            if not w:
                if ctx.re(c_s[k]) <= 0 and c_s[k]:
                    perturb = recompute = True
                    have_singular_nongamma_weight = True
        pole_count = [0, 0, 0]
        for data_index, data in enumerate([alpha_s, beta_s, b_s]):
            for i, x in enumerate(data):
                n, d = ctx.nint_distance(x)
                if n > 0:
                    continue
                if d == ctx.ninf:
                    ok = False
                    if data_index == 2:
                        for u in a_s:
                            if ctx.isnpint(u) and u >= int(n):
                                ok = True
                                break
                    if ok:
                        continue
                    pole_count[data_index] += 1
                elif d < -4:
                    extraprec += -d
                    recompute = True
        if discard_known_zeros and pole_count[1] > pole_count[0] + pole_count[2] \
            and not have_singular_nongamma_weight:
            discard.append(term_index)
        elif sum(pole_count):
            perturb = recompute = True
    return perturb, recompute, extraprec, discard

_hypercomb_msg = """
hypercomb() failed to converge to the requested %i bits of accuracy
using a working precision of %i bits. The function value may be zero or
infinite; try passing zeroprec=N or infprec=M to bound finite values between
2^(-N) and 2^M. Otherwise try a higher maxprec or maxterms.
"""

@defun
def hypercomb(ctx, function, params=[], discard_known_zeros=True, **kwargs):
    orig = ctx.prec
    sumvalue = ctx.zero
    dist = ctx.nint_distance
    ninf = ctx.ninf
    orig_params = params[:]
    verbose = kwargs.get('verbose', False)
    maxprec = kwargs.get('maxprec', ctx._default_hyper_maxprec(orig))
    kwargs['maxprec'] = maxprec
    zeroprec = kwargs.get('zeroprec')
    infprec = kwargs.get('infprec')
    perturbed_reference_value = None
    hextra = 0
    try:
        while 1:
            ctx.prec += 10
            if ctx.prec > maxprec:
                raise ValueError(_hypercomb_msg % (orig, ctx.prec))
            orig2 = ctx.prec
            params = orig_params[:]
            terms = function(*params)
            if verbose:
                print()
                print("ENTERING hypercomb main loop")
                print("prec =", ctx.prec)
                print("hextra", hextra)
            perturb, recompute, extraprec, discard = \
                _check_need_perturb(ctx, terms, orig, discard_known_zeros)
            ctx.prec += extraprec
            if perturb:
                if "hmag" in kwargs:
                    hmag = kwargs["hmag"]
                elif ctx._fixed_precision:
                    hmag = int(ctx.prec*0.3)
                else:
                    hmag = orig + 10 + hextra
                h = ctx.ldexp(ctx.one, -hmag)
                ctx.prec = orig2 + 10 + hmag + 10
                for k in range(len(params)):
                    params[k] += h
                    h += h/(k+1)
            if recompute:
                terms = function(*params)
            if discard_known_zeros:
                terms = [term for (i, term) in enumerate(terms) if i not in discard]
            if not terms:
                return ctx.zero
            evaluated_terms = []
            for term_index, term_data in enumerate(terms):
                w_s, c_s, alpha_s, beta_s, a_s, b_s, z = term_data
                if verbose:
                    print()
                    print("  Evaluating term %i/%i : %iF%i" % \
                        (term_index+1, len(terms), len(a_s), len(b_s)))
                    print("    powers", ctx.nstr(w_s), ctx.nstr(c_s))
                    print("    gamma", ctx.nstr(alpha_s), ctx.nstr(beta_s))
                    print("    hyper", ctx.nstr(a_s), ctx.nstr(b_s))
                    print("    z", ctx.nstr(z))
                v = ctx.fprod([ctx.hyper(a_s, b_s, z, **kwargs)] + \
                    [ctx.gamma(a) for a in alpha_s] + \
                    [ctx.rgamma(b) for b in beta_s] + \
                    [ctx.power(w,c) for (w,c) in zip(w_s,c_s)])
                if verbose:
                    print("    Value:", v)
                evaluated_terms.append(v)
            if len(terms) == 1 and (not perturb):
                sumvalue = evaluated_terms[0]
                break
            if ctx._fixed_precision:
                sumvalue = ctx.fsum(evaluated_terms)
                break
            sumvalue = ctx.fsum(evaluated_terms)
            term_magnitudes = [ctx.mag(x) for x in evaluated_terms]
            max_magnitude = max(term_magnitudes)
            sum_magnitude = ctx.mag(sumvalue)
            cancellation = max_magnitude - sum_magnitude
            if verbose:
                print()
                print("  Cancellation:", cancellation, "bits")
                print("  Increased precision:", ctx.prec - orig, "bits")
            precision_ok = cancellation < ctx.prec - orig
            if zeroprec is None:
                zero_ok = False
            else:
                zero_ok = max_magnitude - ctx.prec < -zeroprec
            if infprec is None:
                inf_ok = False
            else:
                inf_ok = max_magnitude > infprec

            if precision_ok and (not perturb) or ctx.isnan(cancellation):
                break
            elif precision_ok:
                if perturbed_reference_value is None:
                    hextra += 20
                    perturbed_reference_value = sumvalue
                    continue
                elif ctx.mag(sumvalue - perturbed_reference_value) <= \
                        ctx.mag(sumvalue) - orig:
                    break
                elif zero_ok:
                    sumvalue = ctx.zero
                    break
                elif inf_ok:
                    sumvalue = ctx.inf
                    break
                elif 'hmag' in kwargs:
                    break
                else:
                    hextra *= 2
                    perturbed_reference_value = sumvalue
            else:
                increment = min(max(cancellation, orig//2), max(extraprec,orig))
                ctx.prec += increment
                if verbose:
                    print("  Must start over with increased precision")
                continue
    finally:
        ctx.prec = orig
    return +sumvalue

@defun
def hyper(ctx, a_s, b_s, z, **kwargs):
    z = ctx.convert(z)
    p = len(a_s)
    q = len(b_s)
    a_s = [ctx._convert_param(a) for a in a_s]
    b_s = [ctx._convert_param(b) for b in b_s]
    if kwargs.get('eliminate', True):
        elim_nonpositive = kwargs.get('eliminate_all', False)
        i = 0
        while i < q and a_s:
            b = b_s[i]
            if b in a_s and (elim_nonpositive or not ctx.isnpint(b[0])):
                a_s.remove(b)
                b_s.remove(b)
                p -= 1
                q -= 1
            else:
                i += 1
    if p == 0:
        if   q == 1: return ctx._hyp0f1(b_s, z, **kwargs)
        elif q == 0: return ctx.exp(z)
    elif p == 1:
        if   q == 1: return ctx._hyp1f1(a_s, b_s, z, **kwargs)
        elif q == 2: return ctx._hyp1f2(a_s, b_s, z, **kwargs)
        elif q == 0: return ctx._hyp1f0(a_s[0][0], z)
    elif p == 2:
        if   q == 1: return ctx._hyp2f1(a_s, b_s, z, **kwargs)
        elif q == 2: return ctx._hyp2f2(a_s, b_s, z, **kwargs)
        elif q == 3: return ctx._hyp2f3(a_s, b_s, z, **kwargs)
        elif q == 0: return ctx._hyp2f0(a_s, b_s, z, **kwargs)
    elif p == q+1:
        return ctx._hypq1fq(p, q, a_s, b_s, z, **kwargs)
    elif p > q+1 and not kwargs.get('force_series'):
        return ctx._hyp_borel(p, q, a_s, b_s, z, **kwargs)
    coeffs, types = zip(*(a_s+b_s))
    return ctx.hypsum(p, q, types, coeffs, z, **kwargs)

@defun
def hyp0f1(ctx,b,z,**kwargs):
    return ctx.hyper([],[b],z,**kwargs)

@defun
def hyp1f1(ctx,a,b,z,**kwargs):
    return ctx.hyper([a],[b],z,**kwargs)

@defun
def hyp1f2(ctx,a1,b1,b2,z,**kwargs):
    return ctx.hyper([a1],[b1,b2],z,**kwargs)

@defun
def hyp2f1(ctx,a,b,c,z,**kwargs):
    return ctx.hyper([a,b],[c],z,**kwargs)

@defun
def hyp2f2(ctx,a1,a2,b1,b2,z,**kwargs):
    return ctx.hyper([a1,a2],[b1,b2],z,**kwargs)

@defun
def hyp2f3(ctx,a1,a2,b1,b2,b3,z,**kwargs):
    return ctx.hyper([a1,a2],[b1,b2,b3],z,**kwargs)

@defun
def hyp2f0(ctx,a,b,z,**kwargs):
    return ctx.hyper([a,b],[],z,**kwargs)

@defun
def hyp3f2(ctx,a1,a2,a3,b1,b2,z,**kwargs):
    return ctx.hyper([a1,a2,a3],[b1,b2],z,**kwargs)

@defun_wrapped
def _hyp1f0(ctx, a, z):
    return (1-z) ** (-a)

@defun
def _hyp0f1(ctx, b_s, z, **kwargs):
    (b, btype), = b_s
    if z:
        magz = ctx.mag(z)
    else:
        magz = 0
    if magz >= 8 and not kwargs.get('force_series'):
        try:
            orig = ctx.prec
            try:
                ctx.prec += 12 + magz//2
                def h():
                    w = ctx.sqrt(-z)
                    jw = ctx.j*w
                    u = 1/(4*jw)
                    c = ctx.mpq_1_2 - b
                    E = ctx.exp(2*jw)
                    T1 = ([-jw,E], [c,-1], [], [], [b-ctx.mpq_1_2, ctx.mpq_3_2-b], [], -u)
                    T2 = ([jw,E], [c,1], [], [], [b-ctx.mpq_1_2, ctx.mpq_3_2-b], [], u)
                    return T1, T2
                v = ctx.hypercomb(h, [], force_series=True)
                v = ctx.gamma(b)/(2*ctx.sqrt(mp.pi))*v
            finally:
                ctx.prec = orig
            if ctx._is_real_type(b) and ctx._is_real_type(z):
                v = ctx._re(v)
            return +v
        except ctx.NoConvergence:
            pass
    return ctx.hypsum(0, 1, (btype,), [b], z, **kwargs)

@defun
def _hyp1f1(ctx, a_s, b_s, z, **kwargs):
    (a, atype), = a_s
    (b, btype), = b_s
    if not z:
        return ctx.one+z
    magz = ctx.mag(z)
    if magz >= 7 and not (ctx.isint(a) and ctx.re(a) <= 0):
        if ctx.isinf(z):
            if ctx.sign(a) == ctx.sign(b) == ctx.sign(z) == 1:
                return ctx.inf
            return ctx.nan*z
        try:
            try:
                ctx.prec += magz
                sector = ctx._im(z) < 0
                def h(a,b):
                    if sector:
                        E = ctx.expjpi(ctx.fneg(a, exact=True))
                    else:
                        E = ctx.expjpi(a)
                    rz = 1/z
                    T1 = ([E,z], [1,-a], [b], [b-a], [a, 1+a-b], [], -rz)
                    T2 = ([ctx.exp(z),z], [1,a-b], [b], [a], [b-a, 1-a], [], rz)
                    return T1, T2
                v = ctx.hypercomb(h, [a,b], force_series=True)
                if ctx._is_real_type(a) and ctx._is_real_type(b) and ctx._is_real_type(z):
                    v = ctx._re(v)
                return +v
            except ctx.NoConvergence:
                pass
        finally:
            ctx.prec -= magz
    v = ctx.hypsum(1, 1, (atype, btype), [a, b], z, **kwargs)
    return v

def _hyp2f1_gosper(ctx,a,b,c,z,**kwargs):
    _a,_b,_c,_z = a, b, c, z
    orig = ctx.prec
    maxprec = kwargs.get('maxprec', 100*orig)
    extra = 10
    while 1:
        ctx.prec = orig + extra
        z = ctx.convert(_z)
        d = ctx.mpf(0)
        e = ctx.mpf(1)
        f = ctx.mpf(0)
        k = 0
        abz = a*b*z
        ch = c*ctx.mpq_1_2
        c1h = (c+1)*ctx.mpq_1_2
        nz = 1-z
        g = z/nz
        abg = a*b*g
        cba = c-b-a
        z2 = z-2
        tol = -ctx.prec - 10
        nstr = ctx.nstr
        nprint = ctx.nprint
        mag = ctx.mag
        maxmag = ctx.ninf
        while 1:
            kch = k+ch
            kakbz = (k+a)*(k+b)*z / (4*(k+1)*kch*(k+c1h))
            d1 = kakbz*(e-(k+cba)*d*g)
            e1 = kakbz*(d*abg+(k+c)*e)
            ft = d*(k*(cba*z+k*z2-c)-abz)/(2*kch*nz)
            f1 = f + e - ft
            maxmag = max(maxmag, mag(f1))
            if mag(f1-f) < tol:
                break
            d, e, f = d1, e1, f1
            k += 1
        cancellation = maxmag - mag(f1)
        if cancellation < extra:
            break
        else:
            extra += cancellation
            if extra > maxprec:
                raise ctx.NoConvergence
    return f1

@defun
def _hyp2f1(ctx, a_s, b_s, z, **kwargs):
    (a, atype), (b, btype) = a_s
    (c, ctype), = b_s
    if z == 1:
        convergent = ctx.re(c-a-b) > 0
        finite = (ctx.isint(a) and a <= 0) or (ctx.isint(b) and b <= 0)
        zerodiv = ctx.isint(c) and c <= 0 and not \
            ((ctx.isint(a) and c <= a <= 0) or (ctx.isint(b) and c <= b <= 0))
        if (convergent or finite) and not zerodiv:
            return ctx.gammaprod([c, c-a-b], [c-a, c-b], _infsign=True)
        return ctx.hyp2f1(a,b,c,1-mp.eps*2)*ctx.inf
    if not z:
        if c or a == 0 or b == 0:
            return 1+z
        return ctx.nan
    if ctx.isint(c) and c <= 0:
        if (ctx.isint(a) and c <= a <= 0) or \
           (ctx.isint(b) and c <= b <= 0):
            pass
        else:
            return ctx.inf
    absz = abs(z)
    if absz <= 0.8 or (ctx.isint(a) and a <= 0 and a >= -1000) or \
                      (ctx.isint(b) and b <= 0 and b >= -1000):
        return ctx.hypsum(2, 1, (atype, btype, ctype), [a, b, c], z, **kwargs)

    orig = ctx.prec
    try:
        ctx.prec += 10
        if absz >= 1.3:
            def h(a,b):
                t = ctx.mpq_1-c; ab = a-b; rz = 1/z
                T1 = ([-z],[-a], [c,-ab],[b,c-a], [a,t+a],[ctx.mpq_1+ab],  rz)
                T2 = ([-z],[-b], [c,ab],[a,c-b], [b,t+b],[ctx.mpq_1-ab],  rz)
                return T1, T2
            v = ctx.hypercomb(h, [a,b], **kwargs)
        elif abs(1-z) <= 0.75:
            def h(a,b):
                t = c-a-b; ca = c-a; cb = c-b; rz = 1-z
                T1 = [], [], [c,t], [ca,cb], [a,b], [1-t], rz
                T2 = [rz], [t], [c,a+b-c], [a,b], [ca,cb], [1+t], rz
                return T1, T2
            v = ctx.hypercomb(h, [a,b], **kwargs)

        elif abs(z/(z-1)) <= 0.75:
            v = ctx.hyp2f1(a, c-b, c, z/(z-1)) / (1-z)**a
        else:
            v = _hyp2f1_gosper(ctx,a,b,c,z,**kwargs)
    finally:
        ctx.prec = orig
    return +v

@defun
def _hypq1fq(ctx, p, q, a_s, b_s, z, **kwargs):
    a_s, a_types = zip(*a_s)
    b_s, b_types = zip(*b_s)
    a_s = list(a_s)
    b_s = list(b_s)
    absz = abs(z)
    ispoly = False
    for a in a_s:
        if ctx.isint(a) and a <= 0:
            ispoly = True
            break
    if absz < 1 or ispoly:
        try:
            return ctx.hypsum(p, q, a_types+b_types, a_s+b_s, z, **kwargs)
        except ctx.NoConvergence:
            if absz > 1.1 or ispoly:
                raise

    if z == 1:
        S = ctx.re(sum(b_s)-sum(a_s))
        if S <= 0:
            return ctx.hyper(a_s, b_s, 0.9, **kwargs)*ctx.inf
    if (p,q) == (3,2) and abs(z-1) < 0.05:
        a1,a2,a3 = a_s
        b1,b2 = b_s
        u = b1+b2-a3
        initial = ctx.gammaprod([b2-a3,b1-a3,a1,a2],[b2-a3,b1-a3,1,u])
        def term(k, _cache={0:initial}):
            u = b1+b2-a3+k
            if k in _cache:
                t = _cache[k]
            else:
                t = _cache[k-1]
                t *= (b1+k-a3-1)*(b2+k-a3-1)
                t /= k*(u-1)
                _cache[k] = t
            return t*ctx.hyp2f1(a1,a2,u,z)
        try:
            S = ctx.nsum(term, [0,ctx.inf], verbose=kwargs.get('verbose'),
                strict=kwargs.get('strict', True))
            return S*ctx.gammaprod([b1,b2],[a1,a2,a3])
        except ctx.NoConvergence:
            pass
    if absz < 1.1 and ctx._re(z) <= 1:

        def term(kk, _cache={0:ctx.one}):
            k = int(kk)
            if k != kk:
                t = z ** ctx.mpf(kk) / ctx.fac(kk)
                for a in a_s: t *= ctx.rf(a,kk)
                for b in b_s: t /= ctx.rf(b,kk)
                return t
            if k in _cache:
                return _cache[k]
            t = term(k-1)
            m = k-1
            for j in range(p): t *= (a_s[j]+m)
            for j in range(q): t /= (b_s[j]+m)
            t *= z
            t /= k
            _cache[k] = t
            return t

        sum_method = kwargs.get('sum_method', 'r+s+e')

        try:
            return ctx.nsum(term, [0,ctx.inf], verbose=kwargs.get('verbose'),
                strict=kwargs.get('strict', True),
                method=sum_method.replace('e',''))
        except ctx.NoConvergence:
            if 'e' not in sum_method:
                raise
            pass

        if kwargs.get('verbose'):
            print("Attempting Euler-Maclaurin summation")

        def log_diffs(k0):
            b2 = b_s + [1]
            yield sum(ctx.loggamma(a+k0) for a in a_s) - \
                sum(ctx.loggamma(b+k0) for b in b2) + k0*ctx.log(z)
            i = 0
            while 1:
                v = sum(ctx.psi(i,a+k0) for a in a_s) - \
                    sum(ctx.psi(i,b+k0) for b in b2)
                if i == 0:
                    v += ctx.log(z)
                yield v
                i += 1

        def hyper_diffs(k0):
            C = ctx.gammaprod([b for b in b_s], [a for a in a_s])
            for d in ctx.diffs_exp(log_diffs(k0)):
                v = C*d
                yield v

        tol = mp.eps / 1024
        prec = ctx.prec
        try:
            trunc = 50*ctx.dps
            ctx.prec += 20
            for i in range(5):
                head = ctx.fsum(term(k) for k in range(trunc))
                tail, err = ctx.sumem(term, [trunc, ctx.inf], tol=tol,
                    adiffs=hyper_diffs(trunc),
                    verbose=kwargs.get('verbose'),
                    error=True,
                    _fast_abort=True)
                if err < tol:
                    v = head + tail
                    break
                trunc *= 2
                ctx.prec += ctx.prec//2
                if i == 4:
                    raise ctx.NoConvergence(\
                        "Euler-Maclaurin summation did not converge")
        finally:
            ctx.prec = prec
        return +v

    def h(*args):
        a_s = list(args[:p])
        b_s = list(args[p:])
        Ts = []
        recz = ctx.one/z
        negz = ctx.fneg(z, exact=True)
        for k in range(q+1):
            ak = a_s[k]
            C = [negz]
            Cp = [-ak]
            Gn = b_s + [ak] + [a_s[j]-ak for j in range(q+1) if j != k]
            Gd = a_s + [b_s[j]-ak for j in range(q)]
            Fn = [ak] + [ak-b_s[j]+1 for j in range(q)]
            Fd = [1-a_s[j]+ak for j in range(q+1) if j != k]
            Ts.append((C, Cp, Gn, Gd, Fn, Fd, recz))
        return Ts
    return ctx.hypercomb(h, a_s+b_s, **kwargs)

@defun
def _hyp_borel(ctx, p, q, a_s, b_s, z, **kwargs):
    if a_s:
        a_s, a_types = zip(*a_s)
        a_s = list(a_s)
    else:
        a_s, a_types = [], ()
    if b_s:
        b_s, b_types = zip(*b_s)
        b_s = list(b_s)
    else:
        b_s, b_types = [], ()
    kwargs['maxterms'] = kwargs.get('maxterms', ctx.prec)
    try:
        return ctx.hypsum(p, q, a_types+b_types, a_s+b_s, z, **kwargs)
    except ctx.NoConvergence:
        pass
    prec = ctx.prec
    try:
        tol = kwargs.get('asymp_tol', mp.eps/4)
        ctx.prec += 10
        def term(k, cache={0:ctx.one}):
            if k in cache:
                return cache[k]
            t = term(k-1)
            for a in a_s: t *= (a+(k-1))
            for b in b_s: t /= (b+(k-1))
            t *= z
            t /= k
            cache[k] = t
            return t
        s = ctx.one
        for k in range(1, ctx.prec):
            t = term(k)
            s += t
            if abs(t) <= tol:
                return s
    finally:
        ctx.prec = prec
    if p <= q+3:
        contour = kwargs.get('contour')
        if not contour:
            if ctx.arg(z) < 0.25:
                u = z / max(1, abs(z))
                if ctx.arg(z) >= 0:
                    contour = [0, 2j, (2j+2)/u, 2/u, ctx.inf]
                else:
                    contour = [0, -2j, (-2j+2)/u, 2/u, ctx.inf]
            else:
                contour = [0, ctx.inf]
        quad_kwargs = kwargs.get('quad_kwargs', {})
        def g(t):
            return ctx.exp(-t)*ctx.hyper(a_s, b_s+[1], t*z)
        I, err = ctx.quad(g, contour, error=True, **quad_kwargs)
        if err <= abs(I)*mp.eps*8:
            return I
    raise ctx.NoConvergence

@defun
def _hyp2f2(ctx, a_s, b_s, z, **kwargs):
    (a1, a1type), (a2, a2type) = a_s
    (b1, b1type), (b2, b2type) = b_s
    absz = abs(z)
    magz = ctx.mag(z)
    orig = ctx.prec
    asymp_extraprec = magz
    can_use_asymptotic = (not kwargs.get('force_series')) and (ctx.mag(absz) > 3)
    if can_use_asymptotic:
        try:
            try:
                ctx.prec += asymp_extraprec
                def h(a1,a2,b1,b2):
                    X = a1+a2-b1-b2
                    A2 = a1+a2
                    B2 = b1+b2
                    c = {}
                    c[0] = ctx.one
                    c[1] = (A2-1)*X+b1*b2-a1*a2
                    s1 = 0
                    k = 0
                    tprev = 0
                    while 1:
                        if k not in c:
                            uu1 = 1-B2+2*a1+a1**2+2*a2+a2**2-A2*B2+a1*a2+b1*b2+(2*B2-3*(A2+1))*k+2*k**2
                            uu2 = (k-A2+b1-1)*(k-A2+b2-1)*(k-X-2)
                            c[k] = ctx.one/k*(uu1*c[k-1]-uu2*c[k-2])
                        t1 = c[k]*z**(-k)
                        if abs(t1) < 0.1*mp.eps:
                            break
                        if k > 5 and abs(tprev) / abs(t1) < 1.5:
                            raise ctx.NoConvergence
                        s1 += t1
                        tprev = t1
                        k += 1
                    S = ctx.exp(z)*s1
                    T1 = [z,S], [X,1], [b1,b2],[a1,a2],[],[],0
                    T2 = [-z],[-a1],[b1,b2,a2-a1],[a2,b1-a1,b2-a1],[a1,a1-b1+1,a1-b2+1],[a1-a2+1],-1/z
                    T3 = [-z],[-a2],[b1,b2,a1-a2],[a1,b1-a2,b2-a2],[a2,a2-b1+1,a2-b2+1],[-a1+a2+1],-1/z
                    return T1, T2, T3
                v = ctx.hypercomb(h, [a1,a2,b1,b2], force_series=True, maxterms=4*ctx.prec)
                if sum(ctx._is_real_type(u) for u in [a1,a2,b1,b2,z]) == 5:
                    v = ctx.re(v)
                return v
            except ctx.NoConvergence:
                pass
        finally:
            ctx.prec = orig

    return ctx.hypsum(2, 2, (a1type, a2type, b1type, b2type), [a1, a2, b1, b2], z, **kwargs)


@defun
def _hyp1f2(ctx, a_s, b_s, z, **kwargs):
    (a1, a1type), = a_s
    (b1, b1type), (b2, b2type) = b_s

    absz = abs(z)
    magz = ctx.mag(z)
    orig = ctx.prec
    asymp_extraprec = z and magz//2
    can_use_asymptotic = (not kwargs.get('force_series')) and \
        (ctx.mag(absz) > 19) and \
        (ctx.sqrt(absz) > 1.5*orig)
    if can_use_asymptotic:
        try:
            try:
                ctx.prec += asymp_extraprec
                def h(a1,b1,b2):
                    X = ctx.mpq_1_2*(a1-b1-b2+ctx.mpq_1_2)
                    c = {}
                    c[0] = ctx.one
                    c[1] = 2*(ctx.mpq_1_4*(3*a1+b1+b2-2)*(a1-b1-b2)+b1*b2-ctx.mpq_3_16)
                    c[2] = 2*(b1*b2+ctx.mpq_1_4*(a1-b1-b2)*(3*a1+b1+b2-2)-ctx.mpq_3_16)**2+\
                        ctx.mpq_1_16*(-16*(2*a1-3)*b1*b2 + \
                        4*(a1-b1-b2)*(-8*a1**2+11*a1+b1+b2-2)-3)
                    s1 = 0
                    s2 = 0
                    k = 0
                    tprev = 0
                    while 1:
                        if k not in c:
                            uu1 = (3*k**2+(-6*a1+2*b1+2*b2-4)*k + 3*a1**2 - \
                                (b1-b2)**2 - 2*a1*(b1+b2-2) + ctx.mpq_1_4)
                            uu2 = (k-a1+b1-b2-ctx.mpq_1_2)*(k-a1-b1+b2-ctx.mpq_1_2)*\
                                (k-a1+b1+b2-ctx.mpq_5_2)
                            c[k] = ctx.one/(2*k)*(uu1*c[k-1]-uu2*c[k-2])
                        w = c[k]*(-z)**(-0.5*k)
                        t1 = (-ctx.j)**k*ctx.mpf(2)**(-k)*w
                        t2 = ctx.j**k*ctx.mpf(2)**(-k)*w
                        if abs(t1) < 0.1*mp.eps:
                            break
                        if k > 5 and abs(tprev) / abs(t1) < 1.5:
                            raise ctx.NoConvergence
                        s1 += t1
                        s2 += t2
                        tprev = t1
                        k += 1
                    S = ctx.expj(mp.pi*X+2*ctx.sqrt(-z))*s1 + \
                        ctx.expj(-(mp.pi*X+2*ctx.sqrt(-z)))*s2
                    T1 = [0.5*S, mp.pi, -z], [1, -0.5, X], [b1, b2], [a1],\
                        [], [], 0
                    T2 = [-z], [-a1], [b1,b2],[b1-a1,b2-a1], \
                        [a1,a1-b1+1,a1-b2+1], [], 1/z
                    return T1, T2
                v = ctx.hypercomb(h, [a1,b1,b2], force_series=True, maxterms=4*ctx.prec)
                if sum(ctx._is_real_type(u) for u in [a1,b1,b2,z]) == 4:
                    v = ctx.re(v)
                return v
            except ctx.NoConvergence:
                pass
        finally:
            ctx.prec = orig

    return ctx.hypsum(1, 2, (a1type, b1type, b2type), [a1, b1, b2], z, **kwargs)


@defun
def _hyp2f3(ctx, a_s, b_s, z, **kwargs):
    (a1, a1type), (a2, a2type) = a_s
    (b1, b1type), (b2, b2type), (b3, b3type) = b_s

    absz = abs(z)
    magz = ctx.mag(z)

    asymp_extraprec = z and magz//2
    orig = ctx.prec
    can_use_asymptotic = (not kwargs.get('force_series')) and (ctx.mag(absz) > 19) and (ctx.sqrt(absz) > 1.5*orig)

    if can_use_asymptotic:
        try:
            try:
                ctx.prec += asymp_extraprec
                def h(a1,a2,b1,b2,b3):
                    X = ctx.mpq_1_2*(a1+a2-b1-b2-b3+ctx.mpq_1_2)
                    A2 = a1+a2
                    B3 = b1+b2+b3
                    A = a1*a2
                    B = b1*b2+b3*b2+b1*b3
                    R = b1*b2*b3
                    c = {}
                    c[0] = ctx.one
                    c[1] = 2*(B - A + ctx.mpq_1_4*(3*A2+B3-2)*(A2-B3) - ctx.mpq_3_16)
                    c[2] = ctx.mpq_1_2*c[1]**2 + ctx.mpq_1_16*(-16*(2*A2-3)*(B-A) + 32*R +\
                        4*(-8*A2**2 + 11*A2 + 8*A + B3 - 2)*(A2-B3)-3)
                    s1 = 0
                    s2 = 0
                    k = 0
                    tprev = 0
                    while 1:
                        if k not in c:
                            uu1 = (k-2*X-3)*(k-2*X-2*b1-1)*(k-2*X-2*b2-1)*\
                                (k-2*X-2*b3-1)
                            uu2 = (4*(k-1)**3 - 6*(4*X+B3)*(k-1)**2 + \
                                2*(24*X**2+12*B3*X+4*B+B3-1)*(k-1) - 32*X**3 - \
                                24*B3*X**2 - 4*B - 8*R - 4*(4*B+B3-1)*X + 2*B3-1)
                            uu3 = (5*(k-1)**2+2*(-10*X+A2-3*B3+3)*(k-1)+2*c[1])
                            c[k] = ctx.one/(2*k)*(uu1*c[k-3]-uu2*c[k-2]+uu3*c[k-1])
                        w = c[k]*ctx.power(-z, -0.5*k)
                        t1 = (-ctx.j)**k*ctx.mpf(2)**(-k)*w
                        t2 = ctx.j**k*ctx.mpf(2)**(-k)*w
                        if abs(t1) < 0.1*mp.eps:
                            break
                        if k > 5 and abs(tprev) / abs(t1) < 1.5:
                            raise ctx.NoConvergence
                        s1 += t1
                        s2 += t2
                        tprev = t1
                        k += 1
                    S = ctx.expj(mp.pi*X+2*ctx.sqrt(-z))*s1 + \
                        ctx.expj(-(mp.pi*X+2*ctx.sqrt(-z)))*s2
                    T1 = [0.5*S, mp.pi, -z], [1, -0.5, X], [b1, b2, b3], [a1, a2],\
                        [], [], 0
                    T2 = [-z], [-a1], [b1,b2,b3,a2-a1],[a2,b1-a1,b2-a1,b3-a1], \
                        [a1,a1-b1+1,a1-b2+1,a1-b3+1], [a1-a2+1], 1/z
                    T3 = [-z], [-a2], [b1,b2,b3,a1-a2],[a1,b1-a2,b2-a2,b3-a2], \
                        [a2,a2-b1+1,a2-b2+1,a2-b3+1],[-a1+a2+1], 1/z
                    return T1, T2, T3
                v = ctx.hypercomb(h, [a1,a2,b1,b2,b3], force_series=True, maxterms=4*ctx.prec)
                if sum(ctx._is_real_type(u) for u in [a1,a2,b1,b2,b3,z]) == 6:
                    v = ctx.re(v)
                return v
            except ctx.NoConvergence:
                pass
        finally:
            ctx.prec = orig

    return ctx.hypsum(2, 3, (a1type, a2type, b1type, b2type, b3type), [a1, a2, b1, b2, b3], z, **kwargs)

@defun
def _hyp2f0(ctx, a_s, b_s, z, **kwargs):
    (a, atype), (b, btype) = a_s
    try:
        kwargsb = kwargs.copy()
        kwargsb['maxterms'] = kwargsb.get('maxterms', ctx.prec)
        return ctx.hypsum(2, 0, (atype,btype), [a,b], z, **kwargsb)
    except ctx.NoConvergence:
        if kwargs.get('force_series'):
            raise
        pass
    def h(a, b):
        w = ctx.sinpi(b)
        rz = -1/z
        T1 = ([mp.pi,w,rz],[1,-1,a],[],[a-b+1,b],[a],[b],rz)
        T2 = ([-mp.pi,w,rz],[1,-1,1+a-b],[],[a,2-b],[a-b+1],[2-b],rz)
        return T1, T2
    return ctx.hypercomb(h, [a, 1+a-b], **kwargs)

@defun
def meijerg(ctx, a_s, b_s, z, r=1, series=None, **kwargs):
    an, ap = a_s
    bm, bq = b_s
    n = len(an)
    p = n + len(ap)
    m = len(bm)
    q = m + len(bq)
    a = an+ap
    b = bm+bq
    a = [ctx.convert(_) for _ in a]
    b = [ctx.convert(_) for _ in b]
    z = ctx.convert(z)
    if series is None:
        if p < q: series = 1
        if p > q: series = 2
        if p == q:
            if m+n == p and abs(z) > 1:
                series = 2
            else:
                series = 1
    if kwargs.get('verbose'):
        print("Meijer G m,n,p,q,series =", m,n,p,q,series)
    if series == 1:
        def h(*args):
            a = args[:p]
            b = args[p:]
            terms = []
            for k in range(m):
                bases = [z]
                expts = [b[k]/r]
                gn = [b[j]-b[k] for j in range(m) if j != k]
                gn += [1-a[j]+b[k] for j in range(n)]
                gd = [a[j]-b[k] for j in range(n,p)]
                gd += [1-b[j]+b[k] for j in range(m,q)]
                hn = [1-a[j]+b[k] for j in range(p)]
                hd = [1-b[j]+b[k] for j in range(q) if j != k]
                hz = (-ctx.one)**(p-m-n)*z**(ctx.one/r)
                terms.append((bases, expts, gn, gd, hn, hd, hz))
            return terms
    else:
        def h(*args):
            a = args[:p]
            b = args[p:]
            terms = []
            for k in range(n):
                bases = [z]
                if r == 1:
                    expts = [a[k]-1]
                else:
                    expts = [(a[k]-1)/ctx.convert(r)]
                gn = [a[k]-a[j] for j in range(n) if j != k]
                gn += [1-a[k]+b[j] for j in range(m)]
                gd = [a[k]-b[j] for j in range(m,q)]
                gd += [1-a[k]+a[j] for j in range(n,p)]
                hn = [1-a[k]+b[j] for j in range(q)]
                hd = [1+a[j]-a[k] for j in range(p) if j != k]
                hz = (-ctx.one)**(q-m-n) / z**(ctx.one/r)
                terms.append((bases, expts, gn, gd, hn, hd, hz))
            return terms
    return ctx.hypercomb(h, a+b, **kwargs)

@defun_wrapped
def appellf1(ctx,a,b1,b2,c,x,y,**kwargs):
    if abs(x) > abs(y):
        x, y = y, x
        b1, b2 = b2, b1
    def ok(x):
        return abs(x) < 0.99
    if ctx.isnpint(a):
        pass
    elif ctx.isnpint(b1):
        pass
    elif ctx.isnpint(b2):
        x, y, b1, b2 = y, x, b2, b1
    else:
        if not ok(x):
            u1 = (x-y)/(x-1)
            if not ok(u1):
                raise ValueError("Analytic continuation not implemented")
            return (1-x)**(-b1)*(1-y)**(c-a-b2)*\
                ctx.appellf1(c-a,b1,c-b1-b2,c,u1,y,**kwargs)
    return ctx.hyper2d({'m+n':[a],'m':[b1],'n':[b2]}, {'m+n':[c]}, x,y, **kwargs)

@defun
def appellf2(ctx,a,b1,b2,c1,c2,x,y,**kwargs):
    return ctx.hyper2d({'m+n':[a],'m':[b1],'n':[b2]},
        {'m':[c1],'n':[c2]}, x,y, **kwargs)

@defun
def appellf3(ctx,a1,a2,b1,b2,c,x,y,**kwargs):
    outer_polynomial = ctx.isnpint(a1) or ctx.isnpint(b1)
    inner_polynomial = ctx.isnpint(a2) or ctx.isnpint(b2)
    if not outer_polynomial:
        if inner_polynomial or abs(x) > abs(y):
            x, y = y, x
            a1,a2,b1,b2 = a2,a1,b2,b1
    return ctx.hyper2d({'m':[a1,b1],'n':[a2,b2]}, {'m+n':[c]},x,y,**kwargs)

@defun
def appellf4(ctx,a,b,c1,c2,x,y,**kwargs):
    return ctx.hyper2d({'m+n':[a,b]}, {'m':[c1],'n':[c2]},x,y,**kwargs)

@defun
def hyper2d(ctx, a, b, x, y, **kwargs):
    x = ctx.convert(x)
    y = ctx.convert(y)
    def parse(dct, key):
        args = dct.pop(key, [])
        try:
            args = list(args)
        except TypeError:
            args = [args]
        return [ctx.convert(arg) for arg in args]
    a_s = dict(a)
    b_s = dict(b)
    a_m = parse(a, 'm')
    a_n = parse(a, 'n')
    a_m_add_n = parse(a, 'm+n')
    a_m_sub_n = parse(a, 'm-n')
    a_n_sub_m = parse(a, 'n-m')
    a_2m_add_n = parse(a, '2m+n')
    a_2m_sub_n = parse(a, '2m-n')
    a_2n_sub_m = parse(a, '2n-m')
    b_m = parse(b, 'm')
    b_n = parse(b, 'n')
    b_m_add_n = parse(b, 'm+n')
    if a: raise ValueError("unsupported key: %r" % a.keys()[0])
    if b: raise ValueError("unsupported key: %r" % b.keys()[0])
    s = 0
    outer = ctx.one
    m = ctx.mpf(0)
    ok_count = 0
    prec = ctx.prec
    maxterms = kwargs.get('maxterms', 20*prec)
    try:
        ctx.prec += 10
        tol = +mp.eps
        while 1:
            inner_sign = 1
            outer_sign = 1
            inner_a = list(a_n)
            inner_b = list(b_n)
            outer_a = [a+m for a in a_m]
            outer_b = [b+m for b in b_m]
            for a in a_m_add_n:
                a = a+m
                inner_a.append(a)
                outer_a.append(a)
            for b in b_m_add_n:
                b = b+m
                inner_b.append(b)
                outer_b.append(b)
            for a in a_n_sub_m:
                inner_a.append(a-m)
                outer_b.append(a-m-1)
            for a in a_m_sub_n:
                inner_sign *= (-1)
                outer_sign *= (-1)**(m)
                inner_b.append(1-a-m)
                outer_a.append(-a-m)
            for a in a_2m_add_n:
                inner_a.append(a+2*m)
                outer_a.append((a+2*m)*(1+a+2*m))
            for a in a_2m_sub_n:
                inner_sign *= (-1)
                inner_b.append(1-a-2*m)
                outer_a.append((a+2*m)*(1+a+2*m))
            for a in a_2n_sub_m:
                inner_sign *= 4
                inner_a.append(0.5*(a-m))
                inner_a.append(0.5*(a-m+1))
                outer_b.append(a-m-1)
            inner = ctx.hyper(inner_a, inner_b, inner_sign*y,
                zeroprec=ctx.prec, **kwargs)
            term = outer*inner*outer_sign
            if abs(term) < tol:
                ok_count += 1
            else:
                ok_count = 0
            if ok_count >= 3 or not outer:
                break
            s += term
            for a in outer_a: outer *= a
            for b in outer_b: outer /= b
            m += 1
            outer = outer*x / m
            if m > maxterms:
                raise ctx.NoConvergence("maxterms exceeded in hyper2d")
    finally:
        ctx.prec = prec
    return +s

@defun
def bihyper(ctx, a_s, b_s, z, **kwargs):
    z = ctx.convert(z)
    c_s = a_s + b_s
    p = len(a_s)
    q = len(b_s)
    if (p, q) == (0,0) or (p, q) == (1,1):
        return ctx.zero*z
    neg = (p-q) % 2
    def h(*c_s):
        a_s = list(c_s[:p])
        b_s = list(c_s[p:])
        aa_s = [2-b for b in b_s]
        bb_s = [2-a for a in a_s]
        rp = [(-1)**neg*z] + [1-b for b in b_s] + [1-a for a in a_s]
        rc = [-1] + [1]*len(b_s) + [-1]*len(a_s)
        T1 = [], [], [], [], a_s + [1], b_s, z
        T2 = rp, rc, [], [], aa_s + [1], bb_s, (-1)**neg / z
        return T1, T2
    return ctx.hypercomb(h, c_s, **kwargs)

def _hermite_param(ctx, n, z, parabolic_cylinder):
    n, ntyp = ctx._convert_param(n)
    z = ctx.convert(z)
    q = -ctx.mpq_1_2

    if not z:
        T1 = [2, mp.pi], [n, 0.5], [], [q*(n-1)], [], [], 0
        if parabolic_cylinder:
            T1[1][0] += q*n
        return T1,
    can_use_2f0 = ctx.isnpint(-n) or ctx.re(z) > 0 or \
        (ctx.re(z) == 0 and ctx.im(z) > 0)
    expprec = ctx.prec*4 + 20
    if parabolic_cylinder:
        u = ctx.fmul(ctx.fmul(z,z,prec=expprec), -0.25, exact=True)
        w = ctx.fmul(z, ctx.sqrt(0.5,prec=expprec), prec=expprec)
    else:
        w = z
    w2 = ctx.fmul(w, w, prec=expprec)
    rw2 = ctx.fdiv(1, w2, prec=expprec)
    nrw2 = ctx.fneg(rw2, exact=True)
    nw = ctx.fneg(w, exact=True)
    if can_use_2f0:
        T1 = [2, w], [n, n], [], [], [q*n, q*(n-1)], [], nrw2
        terms = [T1]
    else:
        T1 = [2, nw], [n, n], [], [], [q*n, q*(n-1)], [], nrw2
        T2 = [2, mp.pi, nw], [n+2, 0.5, 1], [], [q*n], [q*(n-1)], [1-q], w2
        terms = [T1,T2]
    if parabolic_cylinder:
        expu = ctx.exp(u)
        for i in range(len(terms)):
            terms[i][1][0] += q*n
            terms[i][0].append(expu)
            terms[i][1].append(1)
    return tuple(terms)

@defun
def hermite(ctx, n, z, **kwargs):
    return ctx.hypercomb(lambda: _hermite_param(ctx, n, z, 0), [], **kwargs)

@defun
def pcfd(ctx, n, z, **kwargs):
    return ctx.hypercomb(lambda: _hermite_param(ctx, n, z, 1), [], **kwargs)

@defun
def pcfu(ctx, a, z, **kwargs):

    n, _ = ctx._convert_param(a)
    return ctx.pcfd(-n-ctx.mpq_1_2, z)

@defun
def pcfv(ctx, a, z, **kwargs):

    n, ntype = ctx._convert_param(a)
    z = ctx.convert(z)
    q = ctx.mpq_1_2
    r = ctx.mpq_1_4
    if ntype == 'Q' and ctx.isint(n*2):
        def h():
            jz = ctx.fmul(z, -1j, exact=True)
            T1terms = _hermite_param(ctx, -n-q, z, 1)
            T2terms = _hermite_param(ctx, n-q, jz, 1)
            for T in T1terms:
                T[0].append(1j)
                T[1].append(1)
                T[3].append(q-n)
            u = ctx.expjpi((q*n-r))*ctx.sqrt(2/mp.pi)
            for T in T2terms:
                T[0].append(u)
                T[1].append(1)
            return T1terms + T2terms
        v = ctx.hypercomb(h, [], **kwargs)
        if ctx._is_real_type(n) and ctx._is_real_type(z):
            v = ctx._re(v)
        return v
    else:
        def h(n):
            w = ctx.square_exp_arg(z, -0.25)
            u = ctx.square_exp_arg(z, 0.5)
            e = ctx.exp(w)
            l = [mp.pi, q, ctx.exp(w)]
            Y1 = l, [-q, n*q+r, 1], [r-q*n], [], [q*n+r], [q], u
            Y2 = l + [z], [-q, n*q-r, 1, 1], [1-r-q*n], [], [q*n+1-r], [1+q], u
            c, s = ctx.cospi_sinpi(r+q*n)
            Y1[0].append(s)
            Y2[0].append(c)
            for Y in (Y1, Y2):
                Y[1].append(1)
                Y[3].append(q-n)
            return Y1, Y2
        return ctx.hypercomb(h, [n], **kwargs)


@defun
def pcfw(ctx, a, z, **kwargs):
    n, _ = ctx._convert_param(a)
    z = ctx.convert(z)
    def terms():
        phi2 = ctx.arg(ctx.gamma(0.5 + ctx.j*n))
        phi2 = (ctx.loggamma(0.5+ctx.j*n) - ctx.loggamma(0.5-ctx.j*n))/2j
        rho = mp.pi/8 + 0.5*phi2
        k = ctx.sqrt(1 + ctx.exp(2*mp.pi*n)) - ctx.exp(mp.pi*n)
        C = ctx.sqrt(k/2)*ctx.exp(0.25*mp.pi*n)
        yield C*ctx.expj(rho)*ctx.pcfu(ctx.j*n, z*ctx.expjpi(-0.25))
        yield C*ctx.expj(-rho)*ctx.pcfu(-ctx.j*n, z*ctx.expjpi(0.25))
    v = ctx.sum_accurately(terms)
    if ctx._is_real_type(n) and ctx._is_real_type(z):
        v = ctx._re(v)
    return v


@defun_wrapped
def gegenbauer(ctx, n, a, z, **kwargs):
    if ctx.isnpint(a):
        return 0*(z+n)
    if ctx.isnpint(a+0.5):
        if ctx.isnpint(n+1):
            raise NotImplementedError("Gegenbauer function with two limits")
        def h(a):
            a2 = 2*a
            T = [], [], [n+a2], [n+1, a2], [-n, n+a2], [a+0.5], 0.5*(1-z)
            return [T]
        return ctx.hypercomb(h, [a], **kwargs)
    def h(n):
        a2 = 2*a
        T = [], [], [n+a2], [n+1, a2], [-n, n+a2], [a+0.5], 0.5*(1-z)
        return [T]
    return ctx.hypercomb(h, [n], **kwargs)

@defun_wrapped
def jacobi(ctx, n, a, b, x, **kwargs):
    if not ctx.isnpint(a):
        def h(n):
            return (([], [], [a+n+1], [n+1, a+1], [-n, a+b+n+1], [a+1], (1-x)*0.5),)
        return ctx.hypercomb(h, [n], **kwargs)
    if not ctx.isint(b):
        def h(n, a):
            return (([], [], [-b], [n+1, -b-n], [-n, a+b+n+1], [b+1], (x+1)*0.5),)
        return ctx.hypercomb(h, [n, a], **kwargs)
    return ctx.binomial(n+a,n)*ctx.hyp2f1(-n,1+n+a+b,a+1,(1-x)/2, **kwargs)

@defun_wrapped
def laguerre(ctx, n, a, z, **kwargs):
    def h(a):
        return (([], [], [a+n+1], [a+1, n+1], [-n], [a+1], z),)
    return ctx.hypercomb(h, [a], **kwargs)

@defun_wrapped
def legendre(ctx, n, x, **kwargs):
    if ctx.isint(n):
        n = int(n)
        if (n + (n < 0)) & 1:
            if not x:
                return x
            mag = ctx.mag(x)
            if mag < -2*ctx.prec-10:
                return x
            if mag < -5:
                ctx.prec += -mag
    return ctx.hyp2f1(-n,n+1,1,(1-x)/2, **kwargs)

@defun
def legenp(ctx, n, m, z, type=2, **kwargs):
    n = ctx.convert(n)
    m = ctx.convert(m)
    if not m:
        return ctx.legendre(n, z, **kwargs)
    if type == 2:
        def h(n,m):
            g = m*0.5
            T = [1+z, 1-z], [g, -g], [], [1-m], [-n, n+1], [1-m], 0.5*(1-z)
            return (T,)
        return ctx.hypercomb(h, [n,m], **kwargs)
    if type == 3:
        def h(n,m):
            g = m*0.5
            T = [z+1, z-1], [g, -g], [], [1-m], [-n, n+1], [1-m], 0.5*(1-z)
            return (T,)
        return ctx.hypercomb(h, [n,m], **kwargs)
    raise ValueError("requires type=2 or type=3")

@defun
def legenq(ctx, n, m, z, type=2, **kwargs):
    n = ctx.convert(n)
    m = ctx.convert(m)
    z = ctx.convert(z)
    if z in (1, -1):
        return ctx.nan
    if type == 2:
        def h(n, m):
            cos, sin = ctx.cospi_sinpi(m)
            s = 2*sin / mp.pi
            c = cos
            a = 1+z
            b = 1-z
            u = m/2
            w = (1-z)/2
            T1 = [s, c, a, b], [-1, 1, u, -u], [], [1-m], \
                [-n, n+1], [1-m], w
            T2 = [-s, a, b], [-1, -u, u], [n+m+1], [n-m+1, m+1], \
                [-n, n+1], [m+1], w
            return T1, T2
        return ctx.hypercomb(h, [n, m], **kwargs)
    if type == 3:
        if abs(z) > 1:
            def h(n, m):
                T1 = [ctx.expjpi(m), 2, mp.pi, z, z-1, z+1], \
                     [1, -n-1, 0.5, -n-m-1, 0.5*m, 0.5*m], \
                     [n+m+1], [n+1.5], \
                     [0.5*(2+n+m), 0.5*(1+n+m)], [n+1.5], z**(-2)
                return [T1]
            return ctx.hypercomb(h, [n, m], **kwargs)
        else:
            def h(n, m):
                s = 2*ctx.sinpi(m) / mp.pi
                c = ctx.expjpi(m)
                a = 1+z
                b = z-1
                u = m/2
                w = (1-z)/2
                T1 = [s, c, a, b], [-1, 1, u, -u], [], [1-m], \
                    [-n, n+1], [1-m], w
                T2 = [-s, c, a, b], [-1, 1, -u, u], [n+m+1], [n-m+1, m+1], \
                    [-n, n+1], [m+1], w
                return T1, T2
            return ctx.hypercomb(h, [n, m], **kwargs)
    raise ValueError("requires type=2 or type=3")

@defun_wrapped
def chebyt(ctx, n, x, **kwargs):
    if (not x) and ctx.isint(n) and int(ctx._re(n)) % 2 == 1:
        return x*0
    return ctx.hyp2f1(-n,n,(1,2),(1-x)/2, **kwargs)

@defun_wrapped
def chebyu(ctx, n, x, **kwargs):
    if (not x) and ctx.isint(n) and int(ctx._re(n)) % 2 == 1:
        return x*0
    return (n+1)*ctx.hyp2f1(-n, n+2, (3,2), (1-x)/2, **kwargs)

@defun
def spherharm(ctx, l, m, theta, phi, **kwargs):
    l = ctx.convert(l)
    m = ctx.convert(m)
    theta = ctx.convert(theta)
    phi = ctx.convert(phi)
    l_isint = ctx.isint(l)
    l_natural = l_isint and l >= 0
    m_isint = ctx.isint(m)
    if l_isint and l < 0 and m_isint:
        return ctx.spherharm(-(l+1), m, theta, phi, **kwargs)
    if theta == 0 and m_isint and m < 0:
        return ctx.zero*1j
    if l_natural and m_isint:
        if abs(m) > l:
            return ctx.zero*1j
        def h(l,m):
            absm = abs(m)
            C = [-1, ctx.expj(m*phi),
                 (2*l+1)*ctx.fac(l+absm)/mp.pi/ctx.fac(l-absm),
                 ctx.sin(theta)**2,
                 ctx.fac(absm), 2]
            P = [0.5*m*(ctx.sign(m)+1), 1, 0.5, 0.5*absm, -1, -absm-1]
            return ((C, P, [], [], [absm-l, l+absm+1], [absm+1],
                ctx.sin(0.5*theta)**2),)
    else:
        def h(l,m):
            if ctx.isnpint(l-m+1) or ctx.isnpint(l+m+1) or ctx.isnpint(1-m):
                return (([0], [-1], [], [], [], [], 0),)
            cos, sin = ctx.cos_sin(0.5*theta)
            C = [0.5*ctx.expj(m*phi), (2*l+1)/mp.pi,
                 ctx.gamma(l-m+1), ctx.gamma(l+m+1),
                 cos**2, sin**2]
            P = [1, 0.5, 0.5, -0.5, 0.5*m, -0.5*m]
            return ((C, P, [], [1-m], [-l,l+1], [1-m], sin**2),)
    return ctx.hypercomb(h, [l,m], **kwargs)

@defun
def qp(ctx, a, q=None, n=None, **kwargs):
    a = ctx.convert(a)
    if n is None:
        n = ctx.inf
    else:
        n = ctx.convert(n)
    if n < 0:
        raise ValueError("n cannot be negative")
    if q is None:
        q = a
    else:
        q = ctx.convert(q)
    if n == 0:
        return ctx.one + 0*(a+q)
    infinite = (n == ctx.inf)
    same = (a == q)
    if infinite:
        if abs(q) >= 1:
            if same and (q == -1 or q == 1):
                return ctx.zero*q
            raise ValueError("q-function only defined for |q| < 1")
        elif q == 0:
            return ctx.one - a
    maxterms = kwargs.get('maxterms', 50*ctx.prec)
    if infinite and same:
        def terms():
            t = 1
            yield t
            k = 1
            x1 = q
            x2 = q**2
            while 1:
                yield (-1)**k*x1
                yield (-1)**k*x2
                x1 *= q**(3*k+1)
                x2 *= q**(3*k+2)
                k += 1
                if k > maxterms:
                    raise ctx.NoConvergence
        return ctx.sum_accurately(terms)
    def factors():
        k = 0
        r = ctx.one
        while 1:
            yield 1 - a*r
            r *= q
            k += 1
            if k >= n:
                raise StopIteration
            if k > maxterms:
                raise ctx.NoConvergence
    return ctx.mul_accurately(factors)

@defun_wrapped
def qgamma(ctx, z, q, **kwargs):
    if abs(q) > 1:
        return ctx.qgamma(z,1/q)*q**((z-2)*(z-1)*0.5)
    return ctx.qp(q, q, None, **kwargs) / \
        ctx.qp(q**z, q, None, **kwargs)*(1-q)**(1-z)

@defun_wrapped
def qfac(ctx, z, q, **kwargs):

    if ctx.isint(z) and ctx._re(z) > 0:
        n = int(ctx._re(z))
        return ctx.qp(q, q, n, **kwargs) / (1-q)**n
    return ctx.qgamma(z+1, q, **kwargs)

@defun
def qhyper(ctx, a_s, b_s, q, z, **kwargs):
    a_s = [ctx.convert(a) for a in a_s]
    b_s = [ctx.convert(b) for b in b_s]
    q = ctx.convert(q)
    z = ctx.convert(z)
    r = len(a_s)
    s = len(b_s)
    d = 1+s-r
    maxterms = kwargs.get('maxterms', 50*ctx.prec)
    def terms():
        t = ctx.one
        yield t
        qk = 1
        k = 0
        x = 1
        while 1:
            for a in a_s:
                p = 1 - a*qk
                t *= p
            for b in b_s:
                p = 1 - b*qk
                if not p:
                    raise ValueError
                t /= p
            t *= z
            x *= (-1)**d*qk ** d
            qk *= q
            t /= (1 - qk)
            k += 1
            yield t*x
            if k > maxterms:
                raise ctx.NoConvergence
    return ctx.sum_accurately(terms)

def _coef(ctx, J, eps):
    newJ = J+2
    neweps6 = eps/2.
    wpvw = max(ctx.mag(10*(newJ+3)), 4*newJ+5-ctx.mag(neweps6))
    E = ctx._eulernum(2*newJ)
    wppi = max(ctx.mag(40*newJ), ctx.mag(newJ)+3 +wpvw)
    ctx.prec = wppi
    pipower = {}
    pipower[0] = ctx.one
    pipower[1] = mp.pi
    for n in range(2,2*newJ+1):
        pipower[n] = pipower[n-1]*mp.pi
    ctx.prec = wpvw+2
    v={}
    w={}
    for n in range(0,newJ+1):
        va = (-1)**n*ctx._eulernum(2*n)
        va = ctx.mpf(va)/ctx.fac(2*n)
        v[n]=va*pipower[2*n]
    for n in range(0,2*newJ+1):
        wa = ctx.one/ctx.fac(n)
        wa=wa/(2**n)
        w[n]=wa*pipower[n]
    ctx.prec = 15
    wpp1a = 9 - ctx.mag(neweps6)
    P1 = {}
    for n in range(0,newJ+1):
        ctx.prec = 15
        wpp1 = max(ctx.mag(10*(n+4)),4*n+wpp1a)
        ctx.prec = wpp1
        sump = 0
        for k in range(0,n+1):
            sump += ((-1)**k)*v[k]*w[2*n-2*k]
        P1[n]=((-1)**(n+1))*ctx.j*sump
    P2={}
    for n in range(0,newJ+1):
        ctx.prec = 15
        wpp2 = max(ctx.mag(10*(n+4)),4*n+wpp1a)
        ctx.prec = wpp2
        sump = 0
        for k in range(0,n+1):
            sump += (ctx.j**(n-k))*v[k]*w[n-k]
        P2[n]=sump
    ctx.prec = 15
    wpc0 = 5 - ctx.mag(neweps6)
    wpc = max(6,4*newJ+wpc0)
    ctx.prec = wpc
    mu = ctx.sqrt(ctx.mpf('2'))/2
    nu = ctx.expjpi(3./8)/2
    c={}
    for n in range(0,newJ):
        ctx.prec = 15
        wpc = max(6,4*n+wpc0)
        ctx.prec = wpc
        c[2*n] = mu*P1[n]+nu*P2[n]
    for n in range(1,2*newJ,2):
        c[n] = 0
    return [newJ, neweps6, c, pipower]

def coef(ctx, J, eps):
    _cache = ctx._rs_cache
    if J <= _cache[0] and eps >= _cache[1]:
        return _cache[2], _cache[3]
    orig = ctx._mp.prec
    try:
        data = _coef(ctx._mp, J, eps)
    finally:
        ctx._mp.prec = orig
    if ctx is not ctx._mp:
        data[2] = dict((k,ctx.convert(v)) for (k,v) in data[2].items())
        data[3] = dict((k,ctx.convert(v)) for (k,v) in data[3].items())
    ctx._rs_cache[:] = data
    return ctx._rs_cache[2], ctx._rs_cache[3]


def aux_M_Fp(ctx, xA, xeps4, a, xB1, xL):

    aux1 = 126.0657606*xA/xeps4   # 126.06.. = 316/sqrt(2*pi)
    aux1 = ctx.ln(aux1)
    aux2 = (2*ctx.ln(mp.pi)+ctx.ln(xB1)+ctx.ln(a))/3 -ctx.ln(2*mp.pi)/2
    m = 3*xL-3
    aux3= (ctx.loggamma(m+1)-ctx.loggamma(m/3.0+2))/2 -ctx.loggamma((m+1)/2.)
    while((aux1 < m*aux2+ aux3)and (m>1)):
        m = m - 1
        aux3 = (ctx.loggamma(m+1)-ctx.loggamma(m/3.0+2))/2 -ctx.loggamma((m+1)/2.)
    xM = m
    return xM

def aux_J_needed(ctx, xA, xeps4, a, xB1, xM):

    h1 = xeps4/(632*xA)
    h2 = xB1*a*126.31337419529260248  # = pi^2*e^2*sqrt(3)
    h2 = h1*ctx.power((h2/xM**2),(xM-1)/3) / xM
    h3 = min(h1,h2)
    return h3

def Rzeta_simul(ctx, s, der=0):
    wpinitial = ctx.prec
    t = ctx._im(s)
    xsigma = ctx._re(s)
    ysigma = 1 - xsigma
    ctx.prec = 15
    a = ctx.sqrt(t/(2*mp.pi))
    xasigma = a ** xsigma
    yasigma = a ** ysigma
    xA1=ctx.power(2, ctx.mag(xasigma)-1)
    yA1=ctx.power(2, ctx.mag(yasigma)-1)
    eps = ctx.power(2, -wpinitial)
    eps1 = eps/6.
    xeps2 = eps*xA1/3.
    yeps2 = eps*yA1/3.
    ctx.prec = 15
    if xsigma > 0:
        xb = 2.
        xc = math.pow(9,xsigma)/4.44288
        xA = math.pow(9,xsigma)
        xB1 = 1
    else:
        xb = 2.25158  #  math.sqrt( (3-2* math.log(2))*math.pi )
        xc = math.pow(2,-xsigma)/4.44288
        xA = math.pow(2,-xsigma)
        xB1 = 1.10789   #  = 2*sqrt(1-log(2))

    if(ysigma > 0):
        yb = 2.
        yc = math.pow(9,ysigma)/4.44288
        yA = math.pow(9,ysigma)
        yB1 = 1
    else:
        yb = 2.25158  #  math.sqrt( (3-2* math.log(2))*math.pi )
        yc = math.pow(2,-ysigma)/4.44288
        yA = math.pow(2,-ysigma)
        yB1 = 1.10789   #  = 2*sqrt(1-log(2))

    ctx.prec = 15
    xL = 1
    while 3*xc*ctx.gamma(xL*0.5)*ctx.power(xb*a,-xL) >= xeps2:
        xL = xL+1
    xL = max(2,xL)
    yL = 1
    while 3*yc*ctx.gamma(yL*0.5)*ctx.power(yb*a,-yL) >= yeps2:
        yL = yL+1
    yL = max(2,yL)


    if ((3*xL >= 2*a*a/25.) or (3*xL+2+xsigma<0) or (abs(xsigma) > a/2.) or \
        (3*yL >= 2*a*a/25.) or (3*yL+2+ysigma<0) or (abs(ysigma) > a/2.)):
        ctx.prec = wpinitial
        raise NotImplementedError("Riemann-Siegel can not compute with such precision")

    L = max(xL, yL)

    xeps3 =  xeps2/(4*xL)
    yeps3 =  yeps2/(4*yL)

    xeps4 = xeps3/(3*xL)
    yeps4 = yeps3/(3*yL)

    xM = aux_M_Fp(ctx, xA, xeps4, a, xB1, xL)
    yM = aux_M_Fp(ctx, yA, yeps4, a, yB1, yL)
    M = max(xM, yM)

    h3 = aux_J_needed(ctx, xA, xeps4, a, xB1, xM)
    h4 = aux_J_needed(ctx, yA, yeps4, a, yB1, yM)
    h3 = min(h3,h4)
    J = 12
    jvalue = (2*mp.pi)**J / ctx.gamma(J+1)
    while jvalue > h3:
        J = J+1
        jvalue = (2*mp.pi)*jvalue/J

    eps5={}
    xforeps5 = math.pi*math.pi*xB1*a
    yforeps5 = math.pi*math.pi*yB1*a
    for m in range(0,22):
        xaux1 = math.pow(xforeps5, m/3)/(316.*xA)
        yaux1 = math.pow(yforeps5, m/3)/(316.*yA)
        aux1 = min(xaux1, yaux1)
        aux2 = ctx.gamma(m+1)/ctx.gamma(m/3.0+0.5)
        aux2 = math.sqrt(aux2)
        eps5[m] = (aux1*aux2*min(xeps4,yeps4))

    twenty = min(3*L-3, 21)+1
    aux = 6812*J
    wpfp = ctx.mag(44*J)
    for m in range(0,twenty):
        wpfp = max(wpfp, ctx.mag(aux*ctx.gamma(m+1)/eps5[m]))

    ctx.prec = wpfp + ctx.mag(t)+20
    a = ctx.sqrt(t/(2*mp.pi))
    N = ctx.floor(a)
    p = 1-2*(a-N)

    num=ctx.floor(p*(ctx.mpf('2')**wpfp))
    difference = p*(ctx.mpf('2')**wpfp)-num
    if (difference < 0.5):
        num = num
    else:
        num = num+1
    p = ctx.convert(num*(ctx.mpf('2')**(-wpfp)))

    eps6 = ctx.power(ctx.convert(2*mp.pi), J)/(ctx.gamma(J+1)*3*J)
    cc = {}
    cont = {}
    cont, pipowers = coef(ctx, J, eps6)
    cc=cont.copy()   # we need a copy since we have to change his values.
    Fp={}            # this is the adequate locus of this
    for n in range(M, 3*L-2):
        Fp[n] = 0
    Fp={}
    ctx.prec = wpfp
    for m in range(0,M+1):
        sumP = 0
        for k in range(2*J-m-1,-1,-1):
            sumP = (sumP*p)+ cc[k]
        Fp[m] = sumP
        for k in range(0,2*J-m-1):
            cc[k] = (k+1)* cc[k+1]
    xwpd={}
    d1 = max(6,ctx.mag(40*L*L))
    xd2 = 13+ctx.mag((1+abs(xsigma))*xA)-ctx.mag(xeps4)-1
    xconst = ctx.ln(8/(mp.pi*mp.pi*a*a*xB1*xB1)) /2
    for n in range(0,L):
        xd3 = ctx.mag(ctx.sqrt(ctx.gamma(n-0.5)))-ctx.floor(n*xconst)+xd2
        xwpd[n]=max(xd3,d1)
    ctx.prec = xwpd[1]+10
    xpsigma = 1-(2*xsigma)
    xd = {}
    xd[0,0,-2]=0; xd[0,0,-1]=0; xd[0,0,0]=1; xd[0,0,1]=0
    xd[0,-1,-2]=0; xd[0,-1,-1]=0; xd[0,-1,0]=1; xd[0,-1,1]=0
    for n in range(1,L):
        ctx.prec = xwpd[n]+10
        for k in range(0,3*n//2+1):
            m = 3*n-2*k
            if(m!=0):
                m1 = ctx.one/m
                c1= m1/4
                c2=(xpsigma*m1)/2
                c3=-(m+1)
                xd[0,n,k]=c3*xd[0,n-1,k-2]+c1*xd[0,n-1,k]+c2*xd[0,n-1,k-1]
            else:
                xd[0,n,k]=0
                for r in range(0,k):
                    add=xd[0,n,r]*(ctx.mpf('1.0')*ctx.fac(2*k-2*r)/ctx.fac(k-r))
                    xd[0,n,k] -= ((-1)**(k-r))*add
        xd[0,n,-2]=0; xd[0,n,-1]=0; xd[0,n,3*n//2+1]=0
    for mu in range(-2,der+1):
        for n in range(-2,L):
            for k in range(-3,max(1,3*n//2+2)):
                if( (mu<0)or (n<0) or(k<0)or (k>3*n//2)):
                    xd[mu,n,k] = 0
    for mu in range(1,der+1):
        for n in range(0,L):
            ctx.prec = xwpd[n]+10
            for k in range(0,3*n//2+1):
                aux=(2*mu-2)*xd[mu-2,n-2,k-3]+2*(xsigma+n-2)*xd[mu-1,n-2,k-3]
                xd[mu,n,k] = aux - xd[mu-1,n-1,k-1]
    ywpd={}
    d1 = max(6,ctx.mag(40*L*L))
    yd2 = 13+ctx.mag((1+abs(ysigma))*yA)-ctx.mag(yeps4)-1
    yconst = ctx.ln(8/(mp.pi*mp.pi*a*a*yB1*yB1)) /2
    for n in range(0,L):
        yd3 = ctx.mag(ctx.sqrt(ctx.gamma(n-0.5)))-ctx.floor(n*yconst)+yd2
        ywpd[n]=max(yd3,d1)

    ctx.prec = ywpd[1]+10
    ypsigma = 1-(2*ysigma)
    yd = {}
    yd[0,0,-2]=0; yd[0,0,-1]=0; yd[0,0,0]=1; yd[0,0,1]=0
    yd[0,-1,-2]=0; yd[0,-1,-1]=0; yd[0,-1,0]=1; yd[0,-1,1]=0
    for n in range(1,L):
        ctx.prec = ywpd[n]+10
        for k in range(0,3*n//2+1):
            m = 3*n-2*k
            if(m!=0):
                m1 = ctx.one/m
                c1= m1/4
                c2=(ypsigma*m1)/2
                c3=-(m+1)
                yd[0,n,k]=c3*yd[0,n-1,k-2]+c1*yd[0,n-1,k]+c2*yd[0,n-1,k-1]
            else:
                yd[0,n,k]=0
                for r in range(0,k):
                    add=yd[0,n,r]*(ctx.mpf('1.0')*ctx.fac(2*k-2*r)/ctx.fac(k-r))
                    yd[0,n,k] -= ((-1)**(k-r))*add
        yd[0,n,-2]=0; yd[0,n,-1]=0; yd[0,n,3*n//2+1]=0

    for mu in range(-2,der+1):
        for n in range(-2,L):
            for k in range(-3,max(1,3*n//2+2)):
                if( (mu<0)or (n<0) or(k<0)or (k>3*n//2)):
                    yd[mu,n,k] = 0
    for mu in range(1,der+1):
        for n in range(0,L):
            ctx.prec = ywpd[n]+10
            for k in range(0,3*n//2+1):
                aux=(2*mu-2)*yd[mu-2,n-2,k-3]+2*(ysigma+n-2)*yd[mu-1,n-2,k-3]
                yd[mu,n,k] = aux - yd[mu-1,n-1,k-1]
    xwptcoef={}
    xwpterm={}
    ctx.prec = 15
    c1 = ctx.mag(40*(L+2))
    xc2 = ctx.mag(68*(L+2)*xA)
    xc4 = ctx.mag(xB1*a*math.sqrt(mp.pi))-1
    for k in range(0,L):
        xc3 = xc2 - k*xc4+ctx.mag(ctx.fac(k+0.5))/2.
        xwptcoef[k] = (max(c1,xc3-ctx.mag(xeps4)+1)+1 +20)*1.5
        xwpterm[k] = (max(c1,ctx.mag(L+2)+xc3-ctx.mag(xeps3)+1)+1 +20)
    ywptcoef={}
    ywpterm={}
    ctx.prec = 15
    c1 = ctx.mag(40*(L+2))
    yc2 = ctx.mag(68*(L+2)*yA)
    yc4 = ctx.mag(yB1*a*math.sqrt(mp.pi))-1
    for k in range(0,L):
        yc3 = yc2 - k*yc4+ctx.mag(ctx.fac(k+0.5))/2.
        ywptcoef[k] = ((max(c1,yc3-ctx.mag(yeps4)+1))+10)*1.5
        ywpterm[k] = (max(c1,ctx.mag(L+2)+yc3-ctx.mag(yeps3)+1)+1)+10
    xfortcoef={}
    for mu in range(0,der+1):
        for k in range(0,L):
            for ell in range(-2,3*k//2+1):
                xfortcoef[mu,k,ell]=0
    for mu in range(0,der+1):
        for k in range(0,L):
            ctx.prec = xwptcoef[k]
            for ell in range(0,3*k//2+1):
                xfortcoef[mu,k,ell]=xd[mu,k,ell]*Fp[3*k-2*ell]/pipowers[2*k-ell]
                xfortcoef[mu,k,ell]=xfortcoef[mu,k,ell]/((2*ctx.j)**ell)

    def trunc_a(t):
        wp = ctx.prec
        ctx.prec = wp + 2
        aa = ctx.sqrt(t/(2*mp.pi))
        ctx.prec = wp
        return aa

    xtcoef={}
    for mu in range(0,der+1):
        for k in range(0,L):
            for ell in range(-2,3*k//2+1):
                xtcoef[mu,k,ell]=0
    ctx.prec = max(xwptcoef[0],ywptcoef[0])+3
    aa= trunc_a(t)
    la = -ctx.ln(aa)

    for chi in range(0,der+1):
        for k in range(0,L):
            ctx.prec = xwptcoef[k]
            for ell in range(0,3*k//2+1):
                xtcoef[chi,k,ell] =0
                for mu in range(0, chi+1):
                    tcoefter=ctx.binomial(chi,mu)*ctx.power(la,mu)*xfortcoef[chi-mu,k,ell]
                    xtcoef[chi,k,ell] += tcoefter

    yfortcoef={}
    for mu in range(0,der+1):
        for k in range(0,L):
            for ell in range(-2,3*k//2+1):
                yfortcoef[mu,k,ell]=0
    for mu in range(0,der+1):
        for k in range(0,L):
            ctx.prec = ywptcoef[k]
            for ell in range(0,3*k//2+1):
                yfortcoef[mu,k,ell]=yd[mu,k,ell]*Fp[3*k-2*ell]/pipowers[2*k-ell]
                yfortcoef[mu,k,ell]=yfortcoef[mu,k,ell]/((2*ctx.j)**ell)
    ytcoef={}
    for chi in range(0,der+1):
        for k in range(0,L):
            for ell in range(-2,3*k//2+1):
                ytcoef[chi,k,ell]=0
    for chi in range(0,der+1):
        for k in range(0,L):
            ctx.prec = ywptcoef[k]
            for ell in range(0,3*k//2+1):
                ytcoef[chi,k,ell] =0
                for mu in range(0, chi+1):
                    tcoefter=ctx.binomial(chi,mu)*ctx.power(la,mu)*yfortcoef[chi-mu,k,ell]
                    ytcoef[chi,k,ell] += tcoefter
    ctx.prec = max(xwptcoef[0], ywptcoef[0])+2
    av = {}
    av[0] = 1
    av[1] = av[0]/a
    ctx.prec = max(xwptcoef[0],ywptcoef[0])
    for k in range(2,L):
        av[k] = av[k-1]*av[1]

    xtv = {}
    for chi in range(0,der+1):
        for k in range(0,L):
            ctx.prec = xwptcoef[k]
            for ell in range(0,3*k//2+1):
                xtv[chi,k,ell] = xtcoef[chi,k,ell]* av[k]
    ytv = {}
    for chi in range(0,der+1):
        for k in range(0,L):
            ctx.prec = ywptcoef[k]
            for ell in range(0,3*k//2+1):
                ytv[chi,k,ell] = ytcoef[chi,k,ell]* av[k]

    xterm = {}
    for chi in range(0,der+1):
        for n in range(0,L):
            ctx.prec = xwpterm[n]
            te = 0
            for k in range(0, 3*n//2+1):
                te += xtv[chi,n,k]
            xterm[chi,n] = te

    yterm = {}
    for chi in range(0,der+1):
        for n in range(0,L):
            ctx.prec = ywpterm[n]
            te = 0
            for k in range(0, 3*n//2+1):
                te += ytv[chi,n,k]
            yterm[chi,n] = te

    xrssum={}
    ctx.prec=15
    xrsbound = math.sqrt(mp.pi)*xc /(xb*a)
    ctx.prec=15
    xwprssum = ctx.mag(4.4*((L+3)**2)*xrsbound / xeps2)
    xwprssum = max(xwprssum, ctx.mag(10*(L+1)))
    ctx.prec = xwprssum
    for chi in range(0,der+1):
        xrssum[chi] = 0
        for k in range(1,L+1):
            xrssum[chi] += xterm[chi,L-k]
    yrssum={}
    ctx.prec=15
    yrsbound = math.sqrt(mp.pi)*yc /(yb*a)
    ctx.prec=15
    ywprssum = ctx.mag(4.4*((L+3)**2)*yrsbound / yeps2)
    ywprssum = max(ywprssum, ctx.mag(10*(L+1)))
    ctx.prec = ywprssum
    for chi in range(0,der+1):
        yrssum[chi] = 0
        for k in range(1,L+1):
            yrssum[chi] += yterm[chi,L-k]

    ctx.prec = 15
    A2 = 2**(max(ctx.mag(abs(xrssum[0])), ctx.mag(abs(yrssum[0]))))
    eps8 = eps/(3*A2)
    T = t *ctx.ln(t/(2*mp.pi))
    xwps3 = 5 +  ctx.mag((1+(2/eps8)*ctx.power(a,-xsigma))*T)
    ywps3 = 5 +  ctx.mag((1+(2/eps8)*ctx.power(a,-ysigma))*T)

    ctx.prec = max(xwps3, ywps3)

    tpi = t/(2*mp.pi)
    arg = (t/2)*ctx.ln(tpi)-(t/2)-mp.pi/8
    U = ctx.expj(-arg)
    a = trunc_a(t)
    xasigma = ctx.power(a, -xsigma)
    yasigma = ctx.power(a, -ysigma)
    xS3 = ((-1)**(N-1))*xasigma*U
    yS3 = ((-1)**(N-1))*yasigma*U

    ctx.prec = 15
    xwpsum =  4+ ctx.mag((N+ctx.power(N,1-xsigma))*ctx.ln(N) /eps1)
    ywpsum =  4+ ctx.mag((N+ctx.power(N,1-ysigma))*ctx.ln(N) /eps1)
    wpsum = max(xwpsum, ywpsum)

    ctx.prec = wpsum +10
    xS1, yS1 = ctx._zetasum(s, 1, int(N)-1, range(0,der+1), True)
    ctx.prec = 15
    xabsS1 = abs(xS1[der])
    xabsS2 = abs(xrssum[der]*xS3)
    xwpend = max(6, wpinitial+ctx.mag(6*(3*xabsS1+7*xabsS2) ) )

    ctx.prec = xwpend
    xrz={}
    for chi in range(0,der+1):
        xrz[chi] = xS1[chi]+xrssum[chi]*xS3

    ctx.prec = 15
    yabsS1 = abs(yS1[der])
    yabsS2 = abs(yrssum[der]*yS3)
    ywpend = max(6, wpinitial+ctx.mag(6*(3*yabsS1+7*yabsS2) ) )

    ctx.prec = ywpend
    yrz={}
    for chi in range(0,der+1):
        yrz[chi] = yS1[chi]+yrssum[chi]*yS3
        yrz[chi] = ctx.conj(yrz[chi])
    ctx.prec = wpinitial
    return xrz, yrz

def Rzeta_set(ctx, s, derivatives=[0]):
    der = max(derivatives)
    wpinitial = ctx.prec
    t = ctx._im(s)
    sigma = ctx._re(s)
    ctx.prec = 15
    a = ctx.sqrt(t/(2*mp.pi))     #  Careful
    asigma = ctx.power(a, sigma)  #  Careful
    A1 = ctx.power(2, ctx.mag(asigma)-1)
    eps = ctx.power(2, -wpinitial)
    eps1 = eps/6.
    eps2 = eps*A1/3.

    ctx.prec = 15
    if sigma > 0:
        b = 2.
        c = math.pow(9,sigma)/4.44288
        A = math.pow(9,sigma)
        B1 = 1
    else:
        b = 2.25158  #  math.sqrt( (3-2* math.log(2))*math.pi )
        c = math.pow(2,-sigma)/4.44288
        A = math.pow(2,-sigma)
        B1 = 1.10789   #  = 2*sqrt(1-log(2))

    ctx.prec = 15
    L = 1
    while 3*c*ctx.gamma(L*0.5)*ctx.power(b*a,-L) >= eps2:
        L = L+1
    L = max(2,L)

    if ((3*L >= 2*a*a/25.) or (3*L+2+sigma<0) or (abs(sigma)> a/2.)):
        ctx.prec = wpinitial
        raise NotImplementedError("Riemann-Siegel can not compute with such precision")
    eps3 =  eps2/(4*L)
    eps4 = eps3/(3*L)

    M = aux_M_Fp(ctx, A, eps4, a, B1, L)
    Fp = {}
    for n in range(M, 3*L-2):
        Fp[n] = 0

    h1 = eps4/(632*A)
    h2 = mp.pi*mp.pi*B1*a *ctx.sqrt(3)*math.e*math.e
    h2 = h1*ctx.power((h2/M**2),(M-1)/3) / M
    h3 = min(h1,h2)
    J=12
    jvalue = (2*mp.pi)**J / ctx.gamma(J+1)
    while jvalue > h3:
        J = J+1
        jvalue = (2*mp.pi)*jvalue/J

    eps5={}
    foreps5 = math.pi*math.pi*B1*a
    for m in range(0,22):
        aux1 = math.pow(foreps5, m/3)/(316.*A)
        aux2 = ctx.gamma(m+1)/ctx.gamma(m/3.0+0.5)
        aux2 = math.sqrt(aux2)
        eps5[m] = aux1*aux2*eps4

    twenty = min(3*L-3, 21)+1
    aux = 6812*J
    wpfp = ctx.mag(44*J)
    for m in range(0, twenty):
        wpfp = max(wpfp, ctx.mag(aux*ctx.gamma(m+1)/eps5[m]))

    ctx.prec = wpfp + ctx.mag(t) + 20
    a = ctx.sqrt(t/(2*mp.pi))
    N = ctx.floor(a)
    p = 1-2*(a-N)
    num = ctx.floor(p*(ctx.mpf(2)**wpfp))
    difference = p*(ctx.mpf(2)**wpfp)-num
    if difference < 0.5:
        num = num
    else:
        num = num+1
    p = ctx.convert(num*(ctx.mpf(2)**(-wpfp)))
    eps6 = ctx.power(2*mp.pi, J)/(ctx.gamma(J+1)*3*J)
    cc={}
    cont={}
    cont, pipowers = coef(ctx, J, eps6)
    cc = cont.copy()
    Fp={}
    for n in range(M, 3*L-2):
        Fp[n] = 0
    ctx.prec = wpfp
    for m in range(0,M+1):
        sumP = 0
        for k in range(2*J-m-1,-1,-1):
            sumP = (sumP*p) + cc[k]
        Fp[m] = sumP
        for k in range(0, 2*J-m-1):
            cc[k] = (k+1)*cc[k+1]

    wpd = {}
    d1 = max(6, ctx.mag(40*L*L))
    d2 = 13+ctx.mag((1+abs(sigma))*A)-ctx.mag(eps4)-1
    const = ctx.ln(8/(mp.pi*mp.pi*a*a*B1*B1)) /2
    for n in range(0,L):
        d3 = ctx.mag(ctx.sqrt(ctx.gamma(n-0.5)))-ctx.floor(n*const)+d2
        wpd[n] = max(d3,d1)

    ctx.prec = wpd[1]+10
    psigma = 1-(2*sigma)
    d = {}
    d[0,0,-2]=0; d[0,0,-1]=0; d[0,0,0]=1; d[0,0,1]=0
    d[0,-1,-2]=0; d[0,-1,-1]=0; d[0,-1,0]=1; d[0,-1,1]=0
    for n in range(1,L):
        ctx.prec = wpd[n]+10
        for k in range(0,3*n//2+1):
            m = 3*n-2*k
            if (m!=0):
                m1 = ctx.one/m
                c1 = m1/4
                c2 = (psigma*m1)/2
                c3 = -(m+1)
                d[0,n,k] = c3*d[0,n-1,k-2]+c1*d[0,n-1,k]+c2*d[0,n-1,k-1]
            else:
                d[0,n,k]=0
                for r in range(0,k):
                    add = d[0,n,r]*(ctx.one*ctx.fac(2*k-2*r)/ctx.fac(k-r))
                    d[0,n,k] -= ((-1)**(k-r))*add
        d[0,n,-2]=0; d[0,n,-1]=0; d[0,n,3*n//2+1]=0

    for mu in range(-2,der+1):
        for n in range(-2,L):
            for k in range(-3,max(1,3*n//2+2)):
                if ((mu<0)or (n<0) or(k<0)or (k>3*n//2)):
                    d[mu,n,k] = 0

    for mu in range(1,der+1):
        for n in range(0,L):
            ctx.prec = wpd[n]+10
            for k in range(0,3*n//2+1):
                aux=(2*mu-2)*d[mu-2,n-2,k-3]+2*(sigma+n-2)*d[mu-1,n-2,k-3]
                d[mu,n,k] = aux - d[mu-1,n-1,k-1]
    wptcoef = {}
    wpterm = {}
    ctx.prec = 15
    c1 = ctx.mag(40*(L+2))
    c2 = ctx.mag(68*(L+2)*A)
    c4 = ctx.mag(B1*a*math.sqrt(mp.pi))-1
    for k in range(0,L):
        c3 = c2 - k*c4+ctx.mag(ctx.fac(k+0.5))/2.
        wptcoef[k] = max(c1,c3-ctx.mag(eps4)+1)+1 +10
        wpterm[k] = max(c1,ctx.mag(L+2)+c3-ctx.mag(eps3)+1)+1 +10
    fortcoef={}
    for mu in derivatives:
        for k in range(0,L):
            for ell in range(-2,3*k//2+1):
                fortcoef[mu,k,ell]=0

    for mu in derivatives:
        for k in range(0,L):
            ctx.prec = wptcoef[k]
            for ell in range(0,3*k//2+1):
                fortcoef[mu,k,ell]=d[mu,k,ell]*Fp[3*k-2*ell]/pipowers[2*k-ell]
                fortcoef[mu,k,ell]=fortcoef[mu,k,ell]/((2*ctx.j)**ell)

    def trunc_a(t):
        wp = ctx.prec
        ctx.prec = wp + 2
        aa = ctx.sqrt(t/(2*mp.pi))
        ctx.prec = wp
        return aa

    tcoef={}
    for chi in derivatives:
        for k in range(0,L):
            for ell in range(-2,3*k//2+1):
                tcoef[chi,k,ell]=0
    ctx.prec = wptcoef[0]+3
    aa = trunc_a(t)
    la = -ctx.ln(aa)

    for chi in derivatives:
        for k in range(0,L):
            ctx.prec = wptcoef[k]
            for ell in range(0,3*k//2+1):
                tcoef[chi,k,ell] = 0
                for mu in range(0, chi+1):
                    tcoefter = ctx.binomial(chi,mu)*la**mu*\
                        fortcoef[chi-mu,k,ell]
                    tcoef[chi,k,ell] += tcoefter
    ctx.prec = wptcoef[0] + 2
    av = {}
    av[0] = 1
    av[1] = av[0]/a

    ctx.prec = wptcoef[0]
    for k in range(2,L):
        av[k] = av[k-1]*av[1]
    tv = {}
    for chi in derivatives:
        for k in range(0,L):
            ctx.prec = wptcoef[k]
            for ell in range(0,3*k//2+1):
                tv[chi,k,ell] = tcoef[chi,k,ell]* av[k]
    term = {}
    for chi in derivatives:
        for n in range(0,L):
            ctx.prec = wpterm[n]
            te = 0
            for k in range(0, 3*n//2+1):
                te += tv[chi,n,k]
            term[chi,n] = te
    rssum={}
    ctx.prec=15
    rsbound = math.sqrt(mp.pi)*c /(b*a)
    ctx.prec=15
    wprssum = ctx.mag(4.4*((L+3)**2)*rsbound / eps2)
    wprssum = max(wprssum, ctx.mag(10*(L+1)))
    ctx.prec = wprssum
    for chi in derivatives:
        rssum[chi] = 0
        for k in range(1,L+1):
            rssum[chi] += term[chi,L-k]
    ctx.prec = 15
    A2 = 2**(ctx.mag(rssum[0]))
    eps8 = eps/(3* A2)
    T = t*ctx.ln(t/(2*mp.pi))
    wps3 = 5 + ctx.mag((1+(2/eps8)*ctx.power(a,-sigma))*T)
    ctx.prec = wps3
    tpi = t/(2*mp.pi)
    arg = (t/2)*ctx.ln(tpi)-(t/2)-mp.pi/8
    U = ctx.expj(-arg)
    a = trunc_a(t)
    asigma = ctx.power(a, -sigma)
    S3 = ((-1)**(N-1))*asigma*U
    ctx.prec = 15
    wpsum = 4 + ctx.mag((N+ctx.power(N,1-sigma))*ctx.ln(N)/eps1)
    ctx.prec = wpsum + 10
    S1 = ctx._zetasum(s, 1, int(N)-1, derivatives)[0]
    ctx.prec = 15
    absS1 = abs(S1[der])
    absS2 = abs(rssum[der]*S3)
    wpend = max(6, wpinitial + ctx.mag(6*(3*absS1+7*absS2)))
    ctx.prec = wpend
    rz = {}
    for chi in derivatives:
        rz[chi] = S1[chi]+rssum[chi]*S3
    ctx.prec = wpinitial
    return rz

def z_half(ctx,t,der=0):
    s=ctx.mpf('0.5')+ctx.j*t
    wpinitial = ctx.prec
    ctx.prec = 15
    tt = t/(2*mp.pi)
    wptheta = wpinitial +1 + ctx.mag(3*(tt**1.5)*ctx.ln(tt))
    wpz = wpinitial + 1 + ctx.mag(12*tt*ctx.ln(tt))
    ctx.prec = wptheta
    theta = ctx.siegeltheta(t)
    ctx.prec = wpz
    rz = Rzeta_set(ctx,s, range(der+1))
    if der > 0: ps1 = ctx._re(ctx.psi(0,s/2)/2 - ctx.ln(mp.pi)/2)
    if der > 1: ps2 = ctx._re(ctx.j*ctx.psi(1,s/2)/4)
    if der > 2: ps3 = ctx._re(-ctx.psi(2,s/2)/8)
    if der > 3: ps4 = ctx._re(-ctx.j*ctx.psi(3,s/2)/16)
    exptheta = ctx.expj(theta)
    if der == 0:
        z = 2*exptheta*rz[0]
    if der == 1:
        zf = 2j*exptheta
        z = zf*(ps1*rz[0]+rz[1])
    if der == 2:
        zf = 2*exptheta
        z = -zf*(2*rz[1]*ps1+rz[0]*ps1**2+rz[2]-ctx.j*rz[0]*ps2)
    if der == 3:
        zf = -2j*exptheta
        z = 3*rz[1]*ps1**2+rz[0]*ps1**3+3*ps1*rz[2]
        z = zf*(z-3j*rz[1]*ps2-3j*rz[0]*ps1*ps2+rz[3]-rz[0]*ps3)
    if der == 4:
        zf = 2*exptheta
        z = 4*rz[1]*ps1**3+rz[0]*ps1**4+6*ps1**2*rz[2]
        z = z-12j*rz[1]*ps1*ps2-6j*rz[0]*ps1**2*ps2-6j*rz[2]*ps2-3*rz[0]*ps2*ps2
        z = z + 4*ps1*rz[3]-4*rz[1]*ps3-4*rz[0]*ps1*ps3+rz[4]+ctx.j*rz[0]*ps4
        z = zf*z
    ctx.prec = wpinitial
    return ctx._re(z)

def zeta_half(ctx, s, k=0):
    wpinitial = ctx.prec
    sigma = ctx._re(s)
    t = ctx._im(s)
    ctx.prec = 53
    if sigma > 0:
        X = ctx.sqrt(abs(s))
    else:
        X = (2*mp.pi)**(sigma-1)*abs(1-s)**(0.5-sigma)
    if sigma > 0:
        M1 = 2*ctx.sqrt(t/(2*mp.pi))
    else:
        M1 = 4*t*X
    abst = abs(0.5-s)
    T = 2* abst*math.log(abst)
    wpbasic = max(6,3+ctx.mag(t))
    wpbasic2 = 2+ctx.mag(2.12*M1+21.2*M1*X+1.3*M1*X*T)+wpinitial+1
    wpbasic = max(wpbasic, wpbasic2)
    wptheta = max(4, 3+ctx.mag(2.7*M1*X)+wpinitial+1)
    wpR = 3+ctx.mag(1.1+2*X)+wpinitial+1
    ctx.prec = wptheta
    theta = ctx.siegeltheta(t-ctx.j*(sigma-ctx.mpf('0.5')))
    if k > 0: ps1 = (ctx._re(ctx.psi(0,s/2)))/2 - ctx.ln(mp.pi)/2
    if k > 1: ps2 = -(ctx._im(ctx.psi(1,s/2)))/4
    if k > 2: ps3 = -(ctx._re(ctx.psi(2,s/2)))/8
    if k > 3: ps4 = (ctx._im(ctx.psi(3,s/2)))/16
    ctx.prec = wpR
    xrz = Rzeta_set(ctx,s,range(k+1))
    yrz={}
    for chi in range(0,k+1):
        yrz[chi] = ctx.conj(xrz[chi])
    ctx.prec = wpbasic
    exptheta = ctx.expj(-2*theta)
    if k==0:
        zv = xrz[0]+exptheta*yrz[0]
    if k==1:
        zv1 = -yrz[1] - 2*yrz[0]*ps1
        zv = xrz[1] + exptheta*zv1
    if k==2:
        zv1 = 4*yrz[1]*ps1+4*yrz[0]*(ps1**2)+yrz[2]+2j*yrz[0]*ps2
        zv = xrz[2]+exptheta*zv1
    if k==3:
        zv1 = -12*yrz[1]*ps1**2-8*yrz[0]*ps1**3-6*yrz[2]*ps1-6j*yrz[1]*ps2
        zv1 = zv1 - 12j*yrz[0]*ps1*ps2-yrz[3]+2*yrz[0]*ps3
        zv = xrz[3]+exptheta*zv1
    if k == 4:
        zv1 = 32*yrz[1]*ps1**3 +16*yrz[0]*ps1**4+24*yrz[2]*ps1**2
        zv1 = zv1 +48j*yrz[1]*ps1*ps2+48j*yrz[0]*(ps1**2)*ps2
        zv1 = zv1+12j*yrz[2]*ps2-12*yrz[0]*ps2**2+8*yrz[3]*ps1-8*yrz[1]*ps3
        zv1 = zv1-16*yrz[0]*ps1*ps3+yrz[4]-2j*yrz[0]*ps4
        zv = xrz[4]+exptheta*zv1
    ctx.prec = wpinitial
    return zv

def zeta_offline(ctx, s, k=0):

    wpinitial = ctx.prec
    sigma = ctx._re(s)
    t = ctx._im(s)
    ctx.prec = 53
    if sigma > 0:
        X = ctx.power(abs(s), 0.5)
    else:
        X = ctx.power(2*mp.pi, sigma-1)*ctx.power(abs(1-s),0.5-sigma)
    if (sigma > 0):
        M1 = 2*ctx.sqrt(t/(2*mp.pi))
    else:
        M1 = 4*t*X
    if (1-sigma > 0):
        M2 = 2*ctx.sqrt(t/(2*mp.pi))
    else:
        M2 = 4*t*ctx.power(2*mp.pi, -sigma)*ctx.power(abs(s),sigma-0.5)
    abst = abs(0.5-s)
    T = 2* abst*math.log(abst)
    wpbasic = max(6,3+ctx.mag(t))
    wpbasic2 = 2+ctx.mag(2.12*M1+21.2*M2*X+1.3*M2*X*T)+wpinitial+1
    wpbasic = max(wpbasic, wpbasic2)
    wptheta = max(4, 3+ctx.mag(2.7*M2*X)+wpinitial+1)
    wpR = 3+ctx.mag(1.1+2*X)+wpinitial+1
    ctx.prec = wptheta
    theta = ctx.siegeltheta(t-ctx.j*(sigma-ctx.mpf('0.5')))
    s1 = s
    s2 = ctx.conj(1-s1)
    ctx.prec = wpR
    xrz, yrz = Rzeta_simul(ctx, s, k)
    if k > 0: ps1 = (ctx.psi(0,s1/2)+ctx.psi(0,(1-s1)/2))/4 - ctx.ln(mp.pi)/2
    if k > 1: ps2 = ctx.j*(ctx.psi(1,s1/2)-ctx.psi(1,(1-s1)/2))/8
    if k > 2: ps3 = -(ctx.psi(2,s1/2)+ctx.psi(2,(1-s1)/2))/16
    if k > 3: ps4 = -ctx.j*(ctx.psi(3,s1/2)-ctx.psi(3,(1-s1)/2))/32
    ctx.prec = wpbasic
    exptheta = ctx.expj(-2*theta)
    if k == 0:
        zv = xrz[0]+exptheta*yrz[0]
    if k == 1:
        zv1 = -yrz[1]-2*yrz[0]*ps1
        zv = xrz[1]+exptheta*zv1
    if k == 2:
        zv1 = 4*yrz[1]*ps1+4*yrz[0]*(ps1**2) +yrz[2]+2j*yrz[0]*ps2
        zv = xrz[2]+exptheta*zv1
    if k == 3:
        zv1 = -12*yrz[1]*ps1**2 -8*yrz[0]*ps1**3-6*yrz[2]*ps1-6j*yrz[1]*ps2
        zv1 = zv1 - 12j*yrz[0]*ps1*ps2-yrz[3]+2*yrz[0]*ps3
        zv = xrz[3]+exptheta*zv1
    if k == 4:
        zv1 = 32*yrz[1]*ps1**3 +16*yrz[0]*ps1**4+24*yrz[2]*ps1**2
        zv1 = zv1 +48j*yrz[1]*ps1*ps2+48j*yrz[0]*(ps1**2)*ps2
        zv1 = zv1+12j*yrz[2]*ps2-12*yrz[0]*ps2**2+8*yrz[3]*ps1-8*yrz[1]*ps3
        zv1 = zv1-16*yrz[0]*ps1*ps3+yrz[4]-2j*yrz[0]*ps4
        zv = xrz[4]+exptheta*zv1
    ctx.prec = wpinitial
    return zv

def z_offline(ctx, w, k=0):
    s = ctx.mpf('0.5')+ctx.j*w
    s1 = s
    s2 = ctx.conj(1-s1)
    wpinitial = ctx.prec
    ctx.prec = 35

    if (ctx._re(s1) >= 0):
        M1 = 2*ctx.sqrt(ctx._im(s1)/(2*mp.pi))
        X = ctx.sqrt(abs(s1))
    else:
        X = (2*mp.pi)**(ctx._re(s1)-1)*abs(1-s1)**(0.5-ctx._re(s1))
        M1 = 4*ctx._im(s1)*X
    if (ctx._re(s2) >= 0):
        M2 = 2*ctx.sqrt(ctx._im(s2)/(2*mp.pi))
    else:
        M2 = 4*ctx._im(s2)*(2*mp.pi)**(ctx._re(s2)-1)*abs(1-s2)**(0.5-ctx._re(s2))
    T = 2*abs(ctx.siegeltheta(w))
    aux1 = ctx.sqrt(X)
    aux2 = aux1*(M1+M2)
    aux3 = 3 +wpinitial
    wpbasic = max(6, 3+ctx.mag(T), ctx.mag(aux2*(26+2*T))+aux3)
    wptheta = max(4,ctx.mag(2.04*aux2)+aux3)
    wpR = ctx.mag(4*aux1)+aux3
    ctx.prec = wptheta
    theta = ctx.siegeltheta(w)
    ctx.prec = wpR
    xrz, yrz = Rzeta_simul(ctx,s,k)
    pta = 0.25 + 0.5j*w
    ptb = 0.25 - 0.5j*w
    if k > 0: ps1 = 0.25*(ctx.psi(0,pta)+ctx.psi(0,ptb)) - ctx.ln(mp.pi)/2
    if k > 1: ps2 = (1j/8)*(ctx.psi(1,pta)-ctx.psi(1,ptb))
    if k > 2: ps3 = (-1./16)*(ctx.psi(2,pta)+ctx.psi(2,ptb))
    if k > 3: ps4 = (-1j/32)*(ctx.psi(3,pta)-ctx.psi(3,ptb))
    ctx.prec = wpbasic
    exptheta = ctx.expj(theta)
    if k == 0:
        zv = exptheta*xrz[0]+yrz[0]/exptheta
    j = ctx.j
    if k == 1:
        zv = j*exptheta*(xrz[1]+xrz[0]*ps1)-j*(yrz[1]+yrz[0]*ps1)/exptheta
    if k == 2:
        zv = exptheta*(-2*xrz[1]*ps1-xrz[0]*ps1**2-xrz[2]+j*xrz[0]*ps2)
        zv =zv + (-2*yrz[1]*ps1-yrz[0]*ps1**2-yrz[2]-j*yrz[0]*ps2)/exptheta
    if k == 3:
        zv1 = -3*xrz[1]*ps1**2-xrz[0]*ps1**3-3*xrz[2]*ps1+j*3*xrz[1]*ps2
        zv1 = (zv1+ 3j*xrz[0]*ps1*ps2-xrz[3]+xrz[0]*ps3)*j*exptheta
        zv2 = 3*yrz[1]*ps1**2+yrz[0]*ps1**3+3*yrz[2]*ps1+j*3*yrz[1]*ps2
        zv2 = j*(zv2 + 3j*yrz[0]*ps1*ps2+ yrz[3]-yrz[0]*ps3)/exptheta
        zv = zv1+zv2
    if k == 4:
        zv1 = 4*xrz[1]*ps1**3+xrz[0]*ps1**4 + 6*xrz[2]*ps1**2
        zv1 = zv1-12j*xrz[1]*ps1*ps2-6j*xrz[0]*ps1**2*ps2-6j*xrz[2]*ps2
        zv1 = zv1-3*xrz[0]*ps2*ps2+4*xrz[3]*ps1-4*xrz[1]*ps3-4*xrz[0]*ps1*ps3
        zv1 = zv1+xrz[4]+j*xrz[0]*ps4
        zv2 = 4*yrz[1]*ps1**3+yrz[0]*ps1**4 + 6*yrz[2]*ps1**2
        zv2 = zv2+12j*yrz[1]*ps1*ps2+6j*yrz[0]*ps1**2*ps2+6j*yrz[2]*ps2
        zv2 = zv2-3*yrz[0]*ps2*ps2+4*yrz[3]*ps1-4*yrz[1]*ps3-4*yrz[0]*ps1*ps3
        zv2 = zv2+yrz[4]-j*yrz[0]*ps4
        zv = exptheta*zv1+zv2/exptheta
    ctx.prec = wpinitial
    return zv

@defun
def rs_zeta(ctx, s, derivative=0, **kwargs):
    if derivative > 4:
        raise NotImplementedError
    s = ctx.convert(s)
    re = ctx._re(s); im = ctx._im(s)
    if im < 0:
        z = ctx.conj(ctx.rs_zeta(ctx.conj(s), derivative))
        return z
    critical_line = (re == 0.5)
    if critical_line:
        return zeta_half(ctx, s, derivative)
    else:
        return zeta_offline(ctx, s, derivative)

@defun
def rs_z(ctx, w, derivative=0):
    w = ctx.convert(w)
    re = ctx._re(w); im = ctx._im(w)
    if re < 0:
        return rs_z(ctx, -w, derivative)
    critical_line = (im == 0)
    if critical_line :
        return z_half(ctx, w, derivative)
    else:
        return z_offline(ctx, w, derivative)

@defun
def _jacobi_theta2(ctx, z, q):
    extra1 = 10
    extra2 = 20

    MIN = 2
    if z == ctx.zero:
        if (not ctx._im(q)):
            wp = ctx.prec + extra1
            x = ctx.to_fixed(ctx._re(q), wp)
            x2 = (x*x) >> wp
            a = b = x2
            s = x2
            while abs(a) > MIN:
                b = (b*x2) >> wp
                a = (a*b) >> wp
                s += a
            s = (1 << (wp+1)) + (s << 1)
            s = ctx.ldexp(s, -wp)
        else:
            wp = ctx.prec + extra1
            xre = ctx.to_fixed(ctx._re(q), wp)
            xim = ctx.to_fixed(ctx._im(q), wp)
            x2re = (xre*xre - xim*xim) >> wp
            x2im = (xre*xim) >> (wp-1)
            are = bre = x2re
            aim = bim = x2im
            sre = (1<<wp) + are
            sim = aim
            while are**2 + aim**2 > MIN:
                bre, bim = (bre*x2re - bim*x2im) >> wp, \
                           (bre*x2im + bim*x2re) >> wp
                are, aim = (are*bre - aim*bim) >> wp,   \
                           (are*bim + aim*bre) >> wp
                sre += are
                sim += aim
            sre = (sre << 1)
            sim = (sim << 1)
            sre = ctx.ldexp(sre, -wp)
            sim = ctx.ldexp(sim, -wp)
            s = ctx.mpc(sre, sim)
    else:
        if (not ctx._im(q)) and (not ctx._im(z)):
            wp = ctx.prec + extra1
            x = ctx.to_fixed(ctx._re(q), wp)
            x2 = (x*x) >> wp
            a = b = x2
            c1, s1 = ctx.cos_sin(ctx._re(z), prec=wp)
            cn = c1 = ctx.to_fixed(c1, wp)
            sn = s1 = ctx.to_fixed(s1, wp)
            c2 = (c1*c1 - s1*s1) >> wp
            s2 = (c1*s1) >> (wp - 1)
            cn, sn = (cn*c2 - sn*s2) >> wp, (sn*c2 + cn*s2) >> wp
            s = c1 + ((a*cn) >> wp)
            while abs(a) > MIN:
                b = (b*x2) >> wp
                a = (a*b) >> wp
                cn, sn = (cn*c2 - sn*s2) >> wp, (sn*c2 + cn*s2) >> wp
                s += (a*cn) >> wp
            s = (s << 1)
            s = ctx.ldexp(s, -wp)
            s *= ctx.nthroot(q, 4)
            return s
        # case z real, q complex
        elif not ctx._im(z):
            wp = ctx.prec + extra2
            xre = ctx.to_fixed(ctx._re(q), wp)
            xim = ctx.to_fixed(ctx._im(q), wp)
            x2re = (xre*xre - xim*xim) >> wp
            x2im = (xre*xim) >> (wp - 1)
            are = bre = x2re
            aim = bim = x2im
            c1, s1 = ctx.cos_sin(ctx._re(z), prec=wp)
            cn = c1 = ctx.to_fixed(c1, wp)
            sn = s1 = ctx.to_fixed(s1, wp)
            c2 = (c1*c1 - s1*s1) >> wp
            s2 = (c1*s1) >> (wp - 1)
            cn, sn = (cn*c2 - sn*s2) >> wp, (sn*c2 + cn*s2) >> wp
            sre = c1 + ((are*cn) >> wp)
            sim = ((aim*cn) >> wp)
            while are**2 + aim**2 > MIN:
                bre, bim = (bre*x2re - bim*x2im) >> wp, \
                           (bre*x2im + bim*x2re) >> wp
                are, aim = (are*bre - aim*bim) >> wp,   \
                           (are*bim + aim*bre) >> wp
                cn, sn = (cn*c2 - sn*s2) >> wp, (sn*c2 + cn*s2) >> wp
                sre += ((are*cn) >> wp)
                sim += ((aim*cn) >> wp)
            sre = (sre << 1)
            sim = (sim << 1)
            sre = ctx.ldexp(sre, -wp)
            sim = ctx.ldexp(sim, -wp)
            s = ctx.mpc(sre, sim)
        #case z complex, q real
        elif not ctx._im(q):
            wp = ctx.prec + extra2
            x = ctx.to_fixed(ctx._re(q), wp)
            x2 = (x*x) >> wp
            a = b = x2
            prec0 = ctx.prec
            ctx.prec = wp
            c1, s1 = ctx.cos_sin(z)
            ctx.prec = prec0
            cnre = c1re = ctx.to_fixed(ctx._re(c1), wp)
            cnim = c1im = ctx.to_fixed(ctx._im(c1), wp)
            snre = s1re = ctx.to_fixed(ctx._re(s1), wp)
            snim = s1im = ctx.to_fixed(ctx._im(s1), wp)
            #c2 = (c1*c1 - s1*s1) >> wp
            c2re = (c1re*c1re - c1im*c1im - s1re*s1re + s1im*s1im) >> wp
            c2im = (c1re*c1im - s1re*s1im) >> (wp - 1)
            #s2 = (c1*s1) >> (wp - 1)
            s2re = (c1re*s1re - c1im*s1im) >> (wp - 1)
            s2im = (c1re*s1im + c1im*s1re) >> (wp - 1)
            #cn, sn = (cn*c2 - sn*s2) >> wp, (sn*c2 + cn*s2) >> wp
            t1 = (cnre*c2re - cnim*c2im - snre*s2re + snim*s2im) >> wp
            t2 = (cnre*c2im + cnim*c2re - snre*s2im - snim*s2re) >> wp
            t3 = (snre*c2re - snim*c2im + cnre*s2re - cnim*s2im) >> wp
            t4 = (snre*c2im + snim*c2re + cnre*s2im + cnim*s2re) >> wp
            cnre = t1
            cnim = t2
            snre = t3
            snim = t4
            sre = c1re + ((a*cnre) >> wp)
            sim = c1im + ((a*cnim) >> wp)
            while abs(a) > MIN:
                b = (b*x2) >> wp
                a = (a*b) >> wp
                t1 = (cnre*c2re - cnim*c2im - snre*s2re + snim*s2im) >> wp
                t2 = (cnre*c2im + cnim*c2re - snre*s2im - snim*s2re) >> wp
                t3 = (snre*c2re - snim*c2im + cnre*s2re - cnim*s2im) >> wp
                t4 = (snre*c2im + snim*c2re + cnre*s2im + cnim*s2re) >> wp
                cnre = t1
                cnim = t2
                snre = t3
                snim = t4
                sre += ((a*cnre) >> wp)
                sim += ((a*cnim) >> wp)
            sre = (sre << 1)
            sim = (sim << 1)
            sre = ctx.ldexp(sre, -wp)
            sim = ctx.ldexp(sim, -wp)
            s = ctx.mpc(sre, sim)
        # case z and q complex
        else:
            wp = ctx.prec + extra2
            xre = ctx.to_fixed(ctx._re(q), wp)
            xim = ctx.to_fixed(ctx._im(q), wp)
            x2re = (xre*xre - xim*xim) >> wp
            x2im = (xre*xim) >> (wp - 1)
            are = bre = x2re
            aim = bim = x2im
            prec0 = ctx.prec
            ctx.prec = wp
            # cos(z), sin(z) with z complex
            c1, s1 = ctx.cos_sin(z)
            ctx.prec = prec0
            cnre = c1re = ctx.to_fixed(ctx._re(c1), wp)
            cnim = c1im = ctx.to_fixed(ctx._im(c1), wp)
            snre = s1re = ctx.to_fixed(ctx._re(s1), wp)
            snim = s1im = ctx.to_fixed(ctx._im(s1), wp)
            c2re = (c1re*c1re - c1im*c1im - s1re*s1re + s1im*s1im) >> wp
            c2im = (c1re*c1im - s1re*s1im) >> (wp - 1)
            s2re = (c1re*s1re - c1im*s1im) >> (wp - 1)
            s2im = (c1re*s1im + c1im*s1re) >> (wp - 1)
            t1 = (cnre*c2re - cnim*c2im - snre*s2re + snim*s2im) >> wp
            t2 = (cnre*c2im + cnim*c2re - snre*s2im - snim*s2re) >> wp
            t3 = (snre*c2re - snim*c2im + cnre*s2re - cnim*s2im) >> wp
            t4 = (snre*c2im + snim*c2re + cnre*s2im + cnim*s2re) >> wp
            cnre = t1
            cnim = t2
            snre = t3
            snim = t4
            n = 1
            termre = c1re
            termim = c1im
            sre = c1re + ((are*cnre - aim*cnim) >> wp)
            sim = c1im + ((are*cnim + aim*cnre) >> wp)
            n = 3
            termre = ((are*cnre - aim*cnim) >> wp)
            termim = ((are*cnim + aim*cnre) >> wp)
            sre = c1re + ((are*cnre - aim*cnim) >> wp)
            sim = c1im + ((are*cnim + aim*cnre) >> wp)
            n = 5
            while are**2 + aim**2 > MIN:
                bre, bim = (bre*x2re - bim*x2im) >> wp, \
                           (bre*x2im + bim*x2re) >> wp
                are, aim = (are*bre - aim*bim) >> wp,   \
                           (are*bim + aim*bre) >> wp
                t1 = (cnre*c2re - cnim*c2im - snre*s2re + snim*s2im) >> wp
                t2 = (cnre*c2im + cnim*c2re - snre*s2im - snim*s2re) >> wp
                t3 = (snre*c2re - snim*c2im + cnre*s2re - cnim*s2im) >> wp
                t4 = (snre*c2im + snim*c2re + cnre*s2im + cnim*s2re) >> wp
                cnre = t1
                cnim = t2
                snre = t3
                snim = t4
                termre = ((are*cnre - aim*cnim) >> wp)
                termim = ((aim*cnre + are*cnim) >> wp)
                sre += ((are*cnre - aim*cnim) >> wp)
                sim += ((aim*cnre + are*cnim) >> wp)
                n += 2
            sre = (sre << 1)
            sim = (sim << 1)
            sre = ctx.ldexp(sre, -wp)
            sim = ctx.ldexp(sim, -wp)
            s = ctx.mpc(sre, sim)
    s *= ctx.nthroot(q, 4)
    return s

@defun
def _djacobi_theta2(ctx, z, q, nd):
    MIN = 2
    extra1 = 10
    extra2 = 20
    if (not ctx._im(q)) and (not ctx._im(z)):
        wp = ctx.prec + extra1
        x = ctx.to_fixed(ctx._re(q), wp)
        x2 = (x*x) >> wp
        a = b = x2
        c1, s1 = ctx.cos_sin(ctx._re(z), prec=wp)
        cn = c1 = ctx.to_fixed(c1, wp)
        sn = s1 = ctx.to_fixed(s1, wp)
        c2 = (c1*c1 - s1*s1) >> wp
        s2 = (c1*s1) >> (wp - 1)
        cn, sn = (cn*c2 - sn*s2) >> wp, (sn*c2 + cn*s2) >> wp
        if (nd&1):
            s = s1 + ((a*sn*3**nd) >> wp)
        else:
            s = c1 + ((a*cn*3**nd) >> wp)
        n = 2
        while abs(a) > MIN:
            b = (b*x2) >> wp
            a = (a*b) >> wp
            cn, sn = (cn*c2 - sn*s2) >> wp, (sn*c2 + cn*s2) >> wp
            if nd&1:
                s += (a*sn*(2*n+1)**nd) >> wp
            else:
                s += (a*cn*(2*n+1)**nd) >> wp
            n += 1
        s = -(s << 1)
        s = ctx.ldexp(s, -wp)
    elif not ctx._im(z):
        wp = ctx.prec + extra2
        xre = ctx.to_fixed(ctx._re(q), wp)
        xim = ctx.to_fixed(ctx._im(q), wp)
        x2re = (xre*xre - xim*xim) >> wp
        x2im = (xre*xim) >> (wp - 1)
        are = bre = x2re
        aim = bim = x2im
        c1, s1 = ctx.cos_sin(ctx._re(z), prec=wp)
        cn = c1 = ctx.to_fixed(c1, wp)
        sn = s1 = ctx.to_fixed(s1, wp)
        c2 = (c1*c1 - s1*s1) >> wp
        s2 = (c1*s1) >> (wp - 1)
        cn, sn = (cn*c2 - sn*s2) >> wp, (sn*c2 + cn*s2) >> wp
        if (nd&1):
            sre = s1 + ((are*sn*3**nd) >> wp)
            sim = ((aim*sn*3**nd) >> wp)
        else:
            sre = c1 + ((are*cn*3**nd) >> wp)
            sim = ((aim*cn*3**nd) >> wp)
        n = 5
        while are**2 + aim**2 > MIN:
            bre, bim = (bre*x2re - bim*x2im) >> wp, \
                       (bre*x2im + bim*x2re) >> wp
            are, aim = (are*bre - aim*bim) >> wp,   \
                       (are*bim + aim*bre) >> wp
            cn, sn = (cn*c2 - sn*s2) >> wp, (sn*c2 + cn*s2) >> wp

            if (nd&1):
                sre += ((are*sn*n**nd) >> wp)
                sim += ((aim*sn*n**nd) >> wp)
            else:
                sre += ((are*cn*n**nd) >> wp)
                sim += ((aim*cn*n**nd) >> wp)
            n += 2
        sre = -(sre << 1)
        sim = -(sim << 1)
        sre = ctx.ldexp(sre, -wp)
        sim = ctx.ldexp(sim, -wp)
        s = ctx.mpc(sre, sim)
    elif not ctx._im(q):
        wp = ctx.prec + extra2
        x = ctx.to_fixed(ctx._re(q), wp)
        x2 = (x*x) >> wp
        a = b = x2
        prec0 = ctx.prec
        ctx.prec = wp
        c1, s1 = ctx.cos_sin(z)
        ctx.prec = prec0
        cnre = c1re = ctx.to_fixed(ctx._re(c1), wp)
        cnim = c1im = ctx.to_fixed(ctx._im(c1), wp)
        snre = s1re = ctx.to_fixed(ctx._re(s1), wp)
        snim = s1im = ctx.to_fixed(ctx._im(s1), wp)
        c2re = (c1re*c1re - c1im*c1im - s1re*s1re + s1im*s1im) >> wp
        c2im = (c1re*c1im - s1re*s1im) >> (wp - 1)
        s2re = (c1re*s1re - c1im*s1im) >> (wp - 1)
        s2im = (c1re*s1im + c1im*s1re) >> (wp - 1)
        t1 = (cnre*c2re - cnim*c2im - snre*s2re + snim*s2im) >> wp
        t2 = (cnre*c2im + cnim*c2re - snre*s2im - snim*s2re) >> wp
        t3 = (snre*c2re - snim*c2im + cnre*s2re - cnim*s2im) >> wp
        t4 = (snre*c2im + snim*c2re + cnre*s2im + cnim*s2re) >> wp
        cnre = t1
        cnim = t2
        snre = t3
        snim = t4
        if (nd&1):
            sre = s1re + ((a*snre*3**nd) >> wp)
            sim = s1im + ((a*snim*3**nd) >> wp)
        else:
            sre = c1re + ((a*cnre*3**nd) >> wp)
            sim = c1im + ((a*cnim*3**nd) >> wp)
        n = 5
        while abs(a) > MIN:
            b = (b*x2) >> wp
            a = (a*b) >> wp
            t1 = (cnre*c2re - cnim*c2im - snre*s2re + snim*s2im) >> wp
            t2 = (cnre*c2im + cnim*c2re - snre*s2im - snim*s2re) >> wp
            t3 = (snre*c2re - snim*c2im + cnre*s2re - cnim*s2im) >> wp
            t4 = (snre*c2im + snim*c2re + cnre*s2im + cnim*s2re) >> wp
            cnre = t1
            cnim = t2
            snre = t3
            snim = t4
            if (nd&1):
                sre += ((a*snre*n**nd) >> wp)
                sim += ((a*snim*n**nd) >> wp)
            else:
                sre += ((a*cnre*n**nd) >> wp)
                sim += ((a*cnim*n**nd) >> wp)
            n += 2
        sre = -(sre << 1)
        sim = -(sim << 1)
        sre = ctx.ldexp(sre, -wp)
        sim = ctx.ldexp(sim, -wp)
        s = ctx.mpc(sre, sim)
    else:
        wp = ctx.prec + extra2
        xre = ctx.to_fixed(ctx._re(q), wp)
        xim = ctx.to_fixed(ctx._im(q), wp)
        x2re = (xre*xre - xim*xim) >> wp
        x2im = (xre*xim) >> (wp - 1)
        are = bre = x2re
        aim = bim = x2im
        prec0 = ctx.prec
        ctx.prec = wp
        c1, s1 = ctx.cos_sin(z)
        ctx.prec = prec0
        cnre = c1re = ctx.to_fixed(ctx._re(c1), wp)
        cnim = c1im = ctx.to_fixed(ctx._im(c1), wp)
        snre = s1re = ctx.to_fixed(ctx._re(s1), wp)
        snim = s1im = ctx.to_fixed(ctx._im(s1), wp)
        c2re = (c1re*c1re - c1im*c1im - s1re*s1re + s1im*s1im) >> wp
        c2im = (c1re*c1im - s1re*s1im) >> (wp - 1)
        s2re = (c1re*s1re - c1im*s1im) >> (wp - 1)
        s2im = (c1re*s1im + c1im*s1re) >> (wp - 1)
        t1 = (cnre*c2re - cnim*c2im - snre*s2re + snim*s2im) >> wp
        t2 = (cnre*c2im + cnim*c2re - snre*s2im - snim*s2re) >> wp
        t3 = (snre*c2re - snim*c2im + cnre*s2re - cnim*s2im) >> wp
        t4 = (snre*c2im + snim*c2re + cnre*s2im + cnim*s2re) >> wp
        cnre = t1
        cnim = t2
        snre = t3
        snim = t4
        if (nd&1):
            sre = s1re + (((are*snre - aim*snim)*3**nd) >> wp)
            sim = s1im + (((are*snim + aim*snre)* 3**nd) >> wp)
        else:
            sre = c1re + (((are*cnre - aim*cnim)*3**nd) >> wp)
            sim = c1im + (((are*cnim + aim*cnre)* 3**nd) >> wp)
        n = 5
        while are**2 + aim**2 > MIN:
            bre, bim = (bre*x2re - bim*x2im) >> wp, \
                       (bre*x2im + bim*x2re) >> wp
            are, aim = (are*bre - aim*bim) >> wp,   \
                       (are*bim + aim*bre) >> wp
            t1 = (cnre*c2re - cnim*c2im - snre*s2re + snim*s2im) >> wp
            t2 = (cnre*c2im + cnim*c2re - snre*s2im - snim*s2re) >> wp
            t3 = (snre*c2re - snim*c2im + cnre*s2re - cnim*s2im) >> wp
            t4 = (snre*c2im + snim*c2re + cnre*s2im + cnim*s2re) >> wp
            cnre = t1
            cnim = t2
            snre = t3
            snim = t4
            if (nd&1):
                sre += (((are*snre - aim*snim)*n**nd) >> wp)
                sim += (((aim*snre + are*snim)*n**nd) >> wp)
            else:
                sre += (((are*cnre - aim*cnim)*n**nd) >> wp)
                sim += (((aim*cnre + are*cnim)*n**nd) >> wp)
            n += 2
        sre = -(sre << 1)
        sim = -(sim << 1)
        sre = ctx.ldexp(sre, -wp)
        sim = ctx.ldexp(sim, -wp)
        s = ctx.mpc(sre, sim)
    s *= ctx.nthroot(q, 4)
    if (nd&1):
        return (-1)**(nd//2)*s
    else:
        return (-1)**(1 + nd//2)*s

@defun
def _jacobi_theta3(ctx, z, q):
    extra1 = 10
    extra2 = 20
    MIN = 2
    if z == ctx.zero:
        if not ctx._im(q):
            wp = ctx.prec + extra1
            x = ctx.to_fixed(ctx._re(q), wp)
            s = x
            a = b = x
            x2 = (x*x) >> wp
            while abs(a) > MIN:
                b = (b*x2) >> wp
                a = (a*b) >> wp
                s += a
            s = (1 << wp) + (s << 1)
            s = ctx.ldexp(s, -wp)
            return s
        else:
            wp = ctx.prec + extra1
            xre = ctx.to_fixed(ctx._re(q), wp)
            xim = ctx.to_fixed(ctx._im(q), wp)
            x2re = (xre*xre - xim*xim) >> wp
            x2im = (xre*xim) >> (wp - 1)
            sre = are = bre = xre
            sim = aim = bim = xim
            while are**2 + aim**2 > MIN:
                bre, bim = (bre*x2re - bim*x2im) >> wp, \
                           (bre*x2im + bim*x2re) >> wp
                are, aim = (are*bre - aim*bim) >> wp,   \
                           (are*bim + aim*bre) >> wp
                sre += are
                sim += aim
            sre = (1 << wp) + (sre << 1)
            sim = (sim << 1)
            sre = ctx.ldexp(sre, -wp)
            sim = ctx.ldexp(sim, -wp)
            s = ctx.mpc(sre, sim)
            return s
    else:
        if (not ctx._im(q)) and (not ctx._im(z)):
            s = 0
            wp = ctx.prec + extra1
            x = ctx.to_fixed(ctx._re(q), wp)
            a = b = x
            x2 = (x*x) >> wp
            c1, s1 = ctx.cos_sin(ctx._re(z)*2, prec=wp)
            c1 = ctx.to_fixed(c1, wp)
            s1 = ctx.to_fixed(s1, wp)
            cn = c1
            sn = s1
            s += (a*cn) >> wp
            while abs(a) > MIN:
                b = (b*x2) >> wp
                a = (a*b) >> wp
                cn, sn = (cn*c1 - sn*s1) >> wp, (sn*c1 + cn*s1) >> wp
                s += (a*cn) >> wp
            s = (1 << wp) + (s << 1)
            s = ctx.ldexp(s, -wp)
            return s
        elif not ctx._im(z):
            wp = ctx.prec + extra2
            xre = ctx.to_fixed(ctx._re(q), wp)
            xim = ctx.to_fixed(ctx._im(q), wp)
            x2re = (xre*xre - xim*xim) >> wp
            x2im = (xre*xim) >> (wp - 1)
            are = bre = xre
            aim = bim = xim
            c1, s1 = ctx.cos_sin(ctx._re(z)*2, prec=wp)
            c1 = ctx.to_fixed(c1, wp)
            s1 = ctx.to_fixed(s1, wp)
            cn = c1
            sn = s1
            sre = (are*cn) >> wp
            sim = (aim*cn) >> wp
            while are**2 + aim**2 > MIN:
                bre, bim = (bre*x2re - bim*x2im) >> wp, \
                           (bre*x2im + bim*x2re) >> wp
                are, aim = (are*bre - aim*bim) >> wp,   \
                           (are*bim + aim*bre) >> wp
                cn, sn = (cn*c1 - sn*s1) >> wp, (sn*c1 + cn*s1) >> wp
                sre += (are*cn) >> wp
                sim += (aim*cn) >> wp
            sre = (1 << wp) + (sre << 1)
            sim = (sim << 1)
            sre = ctx.ldexp(sre, -wp)
            sim = ctx.ldexp(sim, -wp)
            s = ctx.mpc(sre, sim)
            return s
        elif not ctx._im(q):
            wp = ctx.prec + extra2
            x = ctx.to_fixed(ctx._re(q), wp)
            a = b = x
            x2 = (x*x) >> wp
            prec0 = ctx.prec
            ctx.prec = wp
            c1, s1 = ctx.cos_sin(2*z)
            ctx.prec = prec0
            cnre = c1re = ctx.to_fixed(ctx._re(c1), wp)
            cnim = c1im = ctx.to_fixed(ctx._im(c1), wp)
            snre = s1re = ctx.to_fixed(ctx._re(s1), wp)
            snim = s1im = ctx.to_fixed(ctx._im(s1), wp)
            sre = (a*cnre) >> wp
            sim = (a*cnim) >> wp
            while abs(a) > MIN:
                b = (b*x2) >> wp
                a = (a*b) >> wp
                t1 = (cnre*c1re - cnim*c1im - snre*s1re + snim*s1im) >> wp
                t2 = (cnre*c1im + cnim*c1re - snre*s1im - snim*s1re) >> wp
                t3 = (snre*c1re - snim*c1im + cnre*s1re - cnim*s1im) >> wp
                t4 = (snre*c1im + snim*c1re + cnre*s1im + cnim*s1re) >> wp
                cnre = t1
                cnim = t2
                snre = t3
                snim = t4
                sre += (a*cnre) >> wp
                sim += (a*cnim) >> wp
            sre = (1 << wp) + (sre << 1)
            sim = (sim << 1)
            sre = ctx.ldexp(sre, -wp)
            sim = ctx.ldexp(sim, -wp)
            s = ctx.mpc(sre, sim)
            return s
        else:
            wp = ctx.prec + extra2
            xre = ctx.to_fixed(ctx._re(q), wp)
            xim = ctx.to_fixed(ctx._im(q), wp)
            x2re = (xre*xre - xim*xim) >> wp
            x2im = (xre*xim) >> (wp - 1)
            are = bre = xre
            aim = bim = xim
            prec0 = ctx.prec
            ctx.prec = wp
            c1, s1 = ctx.cos_sin(2*z)
            ctx.prec = prec0
            cnre = c1re = ctx.to_fixed(ctx._re(c1), wp)
            cnim = c1im = ctx.to_fixed(ctx._im(c1), wp)
            snre = s1re = ctx.to_fixed(ctx._re(s1), wp)
            snim = s1im = ctx.to_fixed(ctx._im(s1), wp)
            sre = (are*cnre - aim*cnim) >> wp
            sim = (aim*cnre + are*cnim) >> wp
            while are**2 + aim**2 > MIN:
                bre, bim = (bre*x2re - bim*x2im) >> wp, \
                           (bre*x2im + bim*x2re) >> wp
                are, aim = (are*bre - aim*bim) >> wp,   \
                           (are*bim + aim*bre) >> wp
                t1 = (cnre*c1re - cnim*c1im - snre*s1re + snim*s1im) >> wp
                t2 = (cnre*c1im + cnim*c1re - snre*s1im - snim*s1re) >> wp
                t3 = (snre*c1re - snim*c1im + cnre*s1re - cnim*s1im) >> wp
                t4 = (snre*c1im + snim*c1re + cnre*s1im + cnim*s1re) >> wp
                cnre = t1
                cnim = t2
                snre = t3
                snim = t4
                sre += (are*cnre - aim*cnim) >> wp
                sim += (aim*cnre + are*cnim) >> wp
            sre = (1 << wp) + (sre << 1)
            sim = (sim << 1)
            sre = ctx.ldexp(sre, -wp)
            sim = ctx.ldexp(sim, -wp)
            s = ctx.mpc(sre, sim)
            return s

@defun
def _djacobi_theta3(ctx, z, q, nd):
    MIN = 2
    extra1 = 10
    extra2 = 20
    if (not ctx._im(q)) and (not ctx._im(z)):
        s = 0
        wp = ctx.prec + extra1
        x = ctx.to_fixed(ctx._re(q), wp)
        a = b = x
        x2 = (x*x) >> wp
        c1, s1 = ctx.cos_sin(ctx._re(z)*2, prec=wp)
        c1 = ctx.to_fixed(c1, wp)
        s1 = ctx.to_fixed(s1, wp)
        cn = c1
        sn = s1
        if (nd&1):
            s += (a*sn) >> wp
        else:
            s += (a*cn) >> wp
        n = 2
        while abs(a) > MIN:
            b = (b*x2) >> wp
            a = (a*b) >> wp
            cn, sn = (cn*c1 - sn*s1) >> wp, (sn*c1 + cn*s1) >> wp
            if nd&1:
                s += (a*sn*n**nd) >> wp
            else:
                s += (a*cn*n**nd) >> wp
            n += 1
        s = -(s << (nd+1))
        s = ctx.ldexp(s, -wp)
    elif not ctx._im(z):
        wp = ctx.prec + extra2
        xre = ctx.to_fixed(ctx._re(q), wp)
        xim = ctx.to_fixed(ctx._im(q), wp)
        x2re = (xre*xre - xim*xim) >> wp
        x2im = (xre*xim) >> (wp - 1)
        are = bre = xre
        aim = bim = xim
        c1, s1 = ctx.cos_sin(ctx._re(z)*2, prec=wp)
        c1 = ctx.to_fixed(c1, wp)
        s1 = ctx.to_fixed(s1, wp)
        cn = c1
        sn = s1
        if (nd&1):
            sre = (are*sn) >> wp
            sim = (aim*sn) >> wp
        else:
            sre = (are*cn) >> wp
            sim = (aim*cn) >> wp
        n = 2
        while are**2 + aim**2 > MIN:
            bre, bim = (bre*x2re - bim*x2im) >> wp, \
                       (bre*x2im + bim*x2re) >> wp
            are, aim = (are*bre - aim*bim) >> wp,   \
                       (are*bim + aim*bre) >> wp
            cn, sn = (cn*c1 - sn*s1) >> wp, (sn*c1 + cn*s1) >> wp
            if nd&1:
                sre += (are*sn*n**nd) >> wp
                sim += (aim*sn*n**nd) >> wp
            else:
                sre += (are*cn*n**nd) >> wp
                sim += (aim*cn*n**nd) >> wp
            n += 1
        sre = -(sre << (nd+1))
        sim = -(sim << (nd+1))
        sre = ctx.ldexp(sre, -wp)
        sim = ctx.ldexp(sim, -wp)
        s = ctx.mpc(sre, sim)
    elif not ctx._im(q):
        wp = ctx.prec + extra2
        x = ctx.to_fixed(ctx._re(q), wp)
        a = b = x
        x2 = (x*x) >> wp
        prec0 = ctx.prec
        ctx.prec = wp
        c1, s1 = ctx.cos_sin(2*z)
        ctx.prec = prec0
        cnre = c1re = ctx.to_fixed(ctx._re(c1), wp)
        cnim = c1im = ctx.to_fixed(ctx._im(c1), wp)
        snre = s1re = ctx.to_fixed(ctx._re(s1), wp)
        snim = s1im = ctx.to_fixed(ctx._im(s1), wp)
        if (nd&1):
            sre = (a*snre) >> wp
            sim = (a*snim) >> wp
        else:
            sre = (a*cnre) >> wp
            sim = (a*cnim) >> wp
        n = 2
        while abs(a) > MIN:
            b = (b*x2) >> wp
            a = (a*b) >> wp
            t1 = (cnre*c1re - cnim*c1im - snre*s1re + snim*s1im) >> wp
            t2 = (cnre*c1im + cnim*c1re - snre*s1im - snim*s1re) >> wp
            t3 = (snre*c1re - snim*c1im + cnre*s1re - cnim*s1im) >> wp
            t4 = (snre*c1im + snim*c1re + cnre*s1im + cnim*s1re) >> wp
            cnre = t1
            cnim = t2
            snre = t3
            snim = t4
            if (nd&1):
                sre += (a*snre*n**nd) >> wp
                sim += (a*snim*n**nd) >> wp
            else:
                sre += (a*cnre*n**nd) >> wp
                sim += (a*cnim*n**nd) >> wp
            n += 1
        sre = -(sre << (nd+1))
        sim = -(sim << (nd+1))
        sre = ctx.ldexp(sre, -wp)
        sim = ctx.ldexp(sim, -wp)
        s = ctx.mpc(sre, sim)
    else:
        wp = ctx.prec + extra2
        xre = ctx.to_fixed(ctx._re(q), wp)
        xim = ctx.to_fixed(ctx._im(q), wp)
        x2re = (xre*xre - xim*xim) >> wp
        x2im = (xre*xim) >> (wp - 1)
        are = bre = xre
        aim = bim = xim
        prec0 = ctx.prec
        ctx.prec = wp
        c1, s1 = ctx.cos_sin(2*z)
        ctx.prec = prec0
        cnre = c1re = ctx.to_fixed(ctx._re(c1), wp)
        cnim = c1im = ctx.to_fixed(ctx._im(c1), wp)
        snre = s1re = ctx.to_fixed(ctx._re(s1), wp)
        snim = s1im = ctx.to_fixed(ctx._im(s1), wp)
        if (nd&1):
            sre = (are*snre - aim*snim) >> wp
            sim = (aim*snre + are*snim) >> wp
        else:
            sre = (are*cnre - aim*cnim) >> wp
            sim = (aim*cnre + are*cnim) >> wp
        n = 2
        while are**2 + aim**2 > MIN:
            bre, bim = (bre*x2re - bim*x2im) >> wp, \
                       (bre*x2im + bim*x2re) >> wp
            are, aim = (are*bre - aim*bim) >> wp,   \
                       (are*bim + aim*bre) >> wp
            t1 = (cnre*c1re - cnim*c1im - snre*s1re + snim*s1im) >> wp
            t2 = (cnre*c1im + cnim*c1re - snre*s1im - snim*s1re) >> wp
            t3 = (snre*c1re - snim*c1im + cnre*s1re - cnim*s1im) >> wp
            t4 = (snre*c1im + snim*c1re + cnre*s1im + cnim*s1re) >> wp
            cnre = t1
            cnim = t2
            snre = t3
            snim = t4
            if(nd&1):
                sre += ((are*snre - aim*snim)*n**nd) >> wp
                sim += ((aim*snre + are*snim)*n**nd) >> wp
            else:
                sre += ((are*cnre - aim*cnim)*n**nd) >> wp
                sim += ((aim*cnre + are*cnim)*n**nd) >> wp
            n += 1
        sre = -(sre << (nd+1))
        sim = -(sim << (nd+1))
        sre = ctx.ldexp(sre, -wp)
        sim = ctx.ldexp(sim, -wp)
        s = ctx.mpc(sre, sim)
    if (nd&1):
        return (-1)**(nd//2)*s
    else:
        return (-1)**(1 + nd//2)*s

@defun
def _jacobi_theta2a(ctx, z, q):

    n = n0 = int(ctx._im(z)/ctx._re(ctx.log(q)) - 1/2)
    e2 = ctx.expj(2*z)
    e = e0 = ctx.expj((2*n+1)*z)
    a = q**(n*n + n)
    term = a*e
    s = term
    eps1 = mp.eps*abs(term)
    while 1:
        n += 1
        e = e*e2
        term = q**(n*n + n)*e
        if abs(term) < eps1:
            break
        s += term
    e = e0
    e2 = ctx.expj(-2*z)
    n = n0
    while 1:
        n -= 1
        e = e*e2
        term = q**(n*n + n)*e
        if abs(term) < eps1:
            break
        s += term
    s = s*ctx.nthroot(q, 4)
    return s

@defun
def _jacobi_theta3a(ctx, z, q):

    n = n0 = int(-ctx._im(z)/abs(ctx._re(ctx.log(q))))
    e2 = ctx.expj(2*z)
    e = e0 = ctx.expj(2*n*z)
    s = term = q**(n*n)*e
    eps1 = mp.eps*abs(term)
    while 1:
        n += 1
        e = e*e2
        term = q**(n*n)*e
        if abs(term) < eps1:
            break
        s += term
    e = e0
    e2 = ctx.expj(-2*z)
    n = n0
    while 1:
        n -= 1
        e = e*e2
        term = q**(n*n)*e
        if abs(term) < eps1:
            break
        s += term
    return s

@defun
def _djacobi_theta2a(ctx, z, q, nd):
    n = n0 = int(ctx._im(z)/ctx._re(ctx.log(q)) - 1/2)
    e2 = ctx.expj(2*z)
    e = e0 = ctx.expj((2*n + 1)*z)
    a = q**(n*n + n)
    term = (2*n+1)**nd*a*e
    s = term
    eps1 = mp.eps*abs(term)
    while 1:
        n += 1
        e = e*e2
        term = (2*n+1)**nd*q**(n*n + n)*e
        if abs(term) < eps1:
            break
        s += term
    e = e0
    e2 = ctx.expj(-2*z)
    n = n0
    while 1:
        n -= 1
        e = e*e2
        term = (2*n+1)**nd*q**(n*n + n)*e
        if abs(term) < eps1:
            break
        s += term
    return ctx.j**nd*s*ctx.nthroot(q, 4)

@defun
def _djacobi_theta3a(ctx, z, q, nd):
    n = n0 = int(-ctx._im(z)/abs(ctx._re(ctx.log(q))))
    e2 = ctx.expj(2*z)
    e = e0 = ctx.expj(2*n*z)
    a = q**(n*n)*e
    s = term = n**nd*a
    if n != 0:
        eps1 = mp.eps*abs(term)
    else:
        eps1 = mp.eps*abs(a)
    while 1:
        n += 1
        e = e*e2
        a = q**(n*n)*e
        term = n**nd*a
        if n != 0:
            aterm = abs(term)
        else:
            aterm = abs(a)
        if aterm < eps1:
            break
        s += term
    e = e0
    e2 = ctx.expj(-2*z)
    n = n0
    while 1:
        n -= 1
        e = e*e2
        a = q**(n*n)*e
        term = n**nd*a
        if n != 0:
            aterm = abs(term)
        else:
            aterm = abs(a)
        if aterm < eps1:
            break
        s += term
    return (2*ctx.j)**nd*s

@defun
def jtheta(ctx, n, z, q, derivative=0):
    if derivative:
        return ctx._djtheta(n, z, q, derivative)
    z = ctx.convert(z)
    q = ctx.convert(q)
    if abs(q) > ctx.THETA_Q_LIM:
        raise ValueError('abs(q) > THETA_Q_LIM = %f' % ctx.THETA_Q_LIM)

    extra = 10
    if z:
        M = ctx.mag(z)
        if M > 5 or (n == 1 and M < -5):
            extra += 2*abs(M)
    cz = 0.5
    extra2 = 50
    prec0 = ctx.prec
    try:
        ctx.prec += extra
        if n == 1:
            if ctx._im(z):
                if abs(ctx._im(z)) < cz*abs(ctx._re(ctx.log(q))):
                    ctx.dps += extra2
                    res = ctx._jacobi_theta2(z - mp.pi/2, q)
                else:
                    ctx.dps += 10
                    res = ctx._jacobi_theta2a(z - mp.pi/2, q)
            else:
                res = ctx._jacobi_theta2(z - mp.pi/2, q)
        elif n == 2:
            if ctx._im(z):
                if abs(ctx._im(z)) < cz*abs(ctx._re(ctx.log(q))):
                    ctx.dps += extra2
                    res = ctx._jacobi_theta2(z, q)
                else:
                    ctx.dps += 10
                    res = ctx._jacobi_theta2a(z, q)
            else:
                res = ctx._jacobi_theta2(z, q)
        elif n == 3:
            if ctx._im(z):
                if abs(ctx._im(z)) < cz*abs(ctx._re(ctx.log(q))):
                    ctx.dps += extra2
                    res = ctx._jacobi_theta3(z, q)
                else:
                    ctx.dps += 10
                    res = ctx._jacobi_theta3a(z, q)
            else:
                res = ctx._jacobi_theta3(z, q)
        elif n == 4:
            if ctx._im(z):
                if abs(ctx._im(z)) < cz*abs(ctx._re(ctx.log(q))):
                    ctx.dps += extra2
                    res = ctx._jacobi_theta3(z, -q)
                else:
                    ctx.dps += 10
                    res = ctx._jacobi_theta3a(z, -q)
            else:
                res = ctx._jacobi_theta3(z, -q)
        else:
            raise ValueError
    finally:
        ctx.prec = prec0
    return res

@defun
def _djtheta(ctx, n, z, q, derivative=1):
    z = ctx.convert(z)
    q = ctx.convert(q)
    nd = int(derivative)

    if abs(q) > ctx.THETA_Q_LIM:
        raise ValueError('abs(q) > THETA_Q_LIM = %f' % ctx.THETA_Q_LIM)
    extra = 10 + ctx.prec*nd // 10
    if z:
        M = ctx.mag(z)
        if M > 5 or (n != 1 and M < -5):
            extra += 2*abs(M)
    cz = 0.5
    extra2 = 50
    prec0 = ctx.prec
    try:
        ctx.prec += extra
        if n == 1:
            if ctx._im(z):
                if abs(ctx._im(z)) < cz*abs(ctx._re(ctx.log(q))):
                    ctx.dps += extra2
                    res = ctx._djacobi_theta2(z - mp.pi/2, q, nd)
                else:
                    ctx.dps += 10
                    res = ctx._djacobi_theta2a(z - mp.pi/2, q, nd)
            else:
                res = ctx._djacobi_theta2(z - mp.pi/2, q, nd)
        elif n == 2:
            if ctx._im(z):
                if abs(ctx._im(z)) < cz*abs(ctx._re(ctx.log(q))):
                    ctx.dps += extra2
                    res = ctx._djacobi_theta2(z, q, nd)
                else:
                    ctx.dps += 10
                    res = ctx._djacobi_theta2a(z, q, nd)
            else:
                res = ctx._djacobi_theta2(z, q, nd)
        elif n == 3:
            if ctx._im(z):
                if abs(ctx._im(z)) < cz*abs(ctx._re(ctx.log(q))):
                    ctx.dps += extra2
                    res = ctx._djacobi_theta3(z, q, nd)
                else:
                    ctx.dps += 10
                    res = ctx._djacobi_theta3a(z, q, nd)
            else:
                res = ctx._djacobi_theta3(z, q, nd)
        elif n == 4:
            if ctx._im(z):
                if abs(ctx._im(z)) < cz*abs(ctx._re(ctx.log(q))):
                    ctx.dps += extra2
                    res = ctx._djacobi_theta3(z, -q, nd)
                else:
                    ctx.dps += 10
                    res = ctx._djacobi_theta3a(z, -q, nd)
            else:
                res = ctx._djacobi_theta3(z, -q, nd)
        else:
            raise ValueError
    finally:
        ctx.prec = prec0
    return +res

@defun
def stieltjes(ctx, n, a=1):
    n = ctx.convert(n)
    a = ctx.convert(a)
    if n < 0:
        return ctx.bad_domain("Stieltjes constants defined for n >= 0")
    if hasattr(ctx, "stieltjes_cache"):
        stieltjes_cache = ctx.stieltjes_cache
    else:
        stieltjes_cache = ctx.stieltjes_cache = {}
    if a == 1:
        if n == 0:
            return +ctx.euler
        if n in stieltjes_cache:
            prec, s = stieltjes_cache[n]
            if prec >= ctx.prec:
                return +s
    mag = 1
    def f(x):
        xa = x/a
        v = (xa-ctx.j)*ctx.ln(a-ctx.j*x)**n/(1+xa**2)/(ctx.exp(2*mp.pi*x)-1)
        return ctx._re(v) / mag
    orig = ctx.prec
    try:
        if n > 50:
            ctx.prec = 20
            mag = ctx.quad(f, [0,ctx.inf], maxdegree=3)
        ctx.prec = orig + 10 + int(n**0.5)
        s = ctx.quad(f, [0,ctx.inf], maxdegree=20)
        v = ctx.ln(a)**n/(2*a) - ctx.ln(a)**(n+1)/(n+1) + 2*s/a*mag
    finally:
        ctx.prec = orig
    if a == 1 and ctx.isint(n):
        stieltjes_cache[n] = (ctx.prec, v)
    return +v

@defun_wrapped
def siegeltheta(ctx, t, derivative=0):
    d = int(derivative)
    if  (t == ctx.inf or t == ctx.ninf):
        if d < 2:
            if t == ctx.ninf and d == 0:
                return ctx.ninf
            return ctx.inf
        else:
            return ctx.zero
    if d == 0:
        if ctx._im(t):
            a = ctx.loggamma(0.25+0.5j*t)
            b = ctx.loggamma(0.25-0.5j*t)
            return -ctx.ln(mp.pi)/2*t - 0.5j*(a-b)
        else:
            if ctx.isinf(t):
                return t
            return ctx._im(ctx.loggamma(0.25+0.5j*t)) - ctx.ln(mp.pi)/2*t
    if d > 0:
        a = (-0.5j)**(d-1)*ctx.polygamma(d-1, 0.25-0.5j*t)
        b = (0.5j)**(d-1)*ctx.polygamma(d-1, 0.25+0.5j*t)
        if ctx._im(t):
            if d == 1:
                return -0.5*ctx.log(mp.pi)+0.25*(a+b)
            else:
                return 0.25*(a+b)
        else:
            if d == 1:
                return ctx._re(-0.5*ctx.log(mp.pi)+0.25*(a+b))
            else:
                return ctx._re(0.25*(a+b))

@defun_wrapped
def grampoint(ctx, n):
    g = 2*mp.pi*ctx.exp(1+ctx.lambertw((8*n+1)/(8*ctx.e)))
    return ctx.findroot(lambda t: ctx.siegeltheta(t)-mp.pi*n, g)


@defun_wrapped
def siegelz(ctx, t, **kwargs):
    d = int(kwargs.get("derivative", 0))
    t = ctx.convert(t)
    t1 = ctx._re(t)
    t2 = ctx._im(t)
    prec = ctx.prec
    try:
        if abs(t1) > 500*prec and t2**2 < t1:
            v = ctx.rs_z(t, d)
            if ctx._is_real_type(t):
                return ctx._re(v)
            return v
    except NotImplementedError:
        pass
    ctx.prec += 21
    e1 = ctx.expj(ctx.siegeltheta(t))
    z = ctx.zeta(0.5+ctx.j*t)
    if d == 0:
        v = e1*z
        ctx.prec=prec
        if ctx._is_real_type(t):
            return ctx._re(v)
        return +v
    z1 = ctx.zeta(0.5+ctx.j*t, derivative=1)
    theta1 = ctx.siegeltheta(t, derivative=1)
    if d == 1:
        v =  ctx.j*e1*(z1+z*theta1)
        ctx.prec=prec
        if ctx._is_real_type(t):
            return ctx._re(v)
        return +v
    z2 = ctx.zeta(0.5+ctx.j*t, derivative=2)
    theta2 = ctx.siegeltheta(t, derivative=2)
    comb1 = theta1**2-ctx.j*theta2
    if d == 2:
        def terms():
            return [2*z1*theta1, z2, z*comb1]
        v = ctx.sum_accurately(terms, 1)
        v =  -e1*v
        ctx.prec = prec
        if ctx._is_real_type(t):
            return ctx._re(v)
        return +v
    ctx.prec += 10
    z3 = ctx.zeta(0.5+ctx.j*t, derivative=3)
    theta3 = ctx.siegeltheta(t, derivative=3)
    comb2 = theta1**3-3*ctx.j*theta1*theta2-theta3
    if d == 3:
        def terms():
            return  [3*theta1*z2, 3*z1*comb1, z3+z*comb2]
        v = ctx.sum_accurately(terms, 1)
        v =  -ctx.j*e1*v
        ctx.prec = prec
        if ctx._is_real_type(t):
            return ctx._re(v)
        return +v
    z4 = ctx.zeta(0.5+ctx.j*t, derivative=4)
    theta4 = ctx.siegeltheta(t, derivative=4)
    def terms():
        return [theta1**4, -6*ctx.j*theta1**2*theta2, -3*theta2**2,
            -4*theta1*theta3, ctx.j*theta4]
    comb3 = ctx.sum_accurately(terms, 1)
    if d == 4:
        def terms():
            return  [6*theta1**2*z2, -6*ctx.j*z2*theta2, 4*theta1*z3,
                 4*z1*comb2, z4, z*comb3]
        v = ctx.sum_accurately(terms, 1)
        v =  e1*v
        ctx.prec = prec
        if ctx._is_real_type(t):
            return ctx._re(v)
        return +v
    if d > 4:
        h = lambda x: ctx.siegelz(x, derivative=4)
        return ctx.diff(h, t, n=d-4)

_zeta_zeros = [
14.134725142,21.022039639,25.010857580,30.424876126,32.935061588,
37.586178159,40.918719012,43.327073281,48.005150881,49.773832478,
52.970321478,56.446247697,59.347044003,60.831778525,65.112544048,
67.079810529,69.546401711,72.067157674,75.704690699,77.144840069,
79.337375020,82.910380854,84.735492981,87.425274613,88.809111208,
92.491899271,94.651344041,95.870634228,98.831194218,101.317851006,
103.725538040,105.446623052,107.168611184,111.029535543,111.874659177,
114.320220915,116.226680321,118.790782866,121.370125002,122.946829294,
124.256818554,127.516683880,129.578704200,131.087688531,133.497737203,
134.756509753,138.116042055,139.736208952,141.123707404,143.111845808,
146.000982487,147.422765343,150.053520421,150.925257612,153.024693811,
156.112909294,157.597591818,158.849988171,161.188964138,163.030709687,
165.537069188,167.184439978,169.094515416,169.911976479,173.411536520,
174.754191523,176.441434298,178.377407776,179.916484020,182.207078484,
184.874467848,185.598783678,187.228922584,189.416158656,192.026656361,
193.079726604,195.265396680,196.876481841,198.015309676,201.264751944,
202.493594514,204.189671803,205.394697202,207.906258888,209.576509717,
211.690862595,213.347919360,214.547044783,216.169538508,219.067596349,
220.714918839,221.430705555,224.007000255,224.983324670,227.421444280,
229.337413306,231.250188700,231.987235253,233.693404179,236.524229666,
]

def _load_zeta_zeros(url):
    d = urllib.urlopen(url)
    L = [float(x) for x in d.readlines()]
    # Sanity check
    assert round(L[0]) == 14
    _zeta_zeros[:] = L

def riemannr(ctx, x):
    if x == 0:
        return ctx.zero
    if abs(x) > 1000:
        a = ctx.li(x)
        b = 0.5*ctx.li(ctx.sqrt(x))
        if abs(b) < abs(a)*mp.eps:
            return a
    if abs(x) < 0.01:
        ctx.prec += int(-ctx.log(abs(x),2))
    s = t = ctx.one
    u = ctx.ln(x)
    k = 1
    while abs(t) > abs(s)*mp.eps:
        t = t*u / k
        s += t / (k*ctx._zeta_int(k+1))
        k += 1
    return s

@defun
def oldzetazero(ctx, n, url='http://www.dtc.umn.edu/~odlyzko/zeta_tables/zeros1'):
    n = int(n)
    if n < 0:
        return ctx.zetazero(-n).conjugate()
    if n == 0:
        raise ValueError("n must be nonzero")
    if n > len(_zeta_zeros) and n <= 100000:
        _load_zeta_zeros(url)
    if n > len(_zeta_zeros):
        raise NotImplementedError("n too large for zetazeros")
    return ctx.mpc(0.5, ctx.findroot(ctx.siegelz, _zeta_zeros[n-1]))

@defun_static
def primepi(ctx, x):
    x = int(x)
    if x < 2:
        return 0
    return len(ctx.list_primes(x))

@defun_wrapped
def primepi2(ctx, x):
    x = int(x)
    if x < 2:
        return ctx._iv.zero
    if x < 2657:
        return ctx._iv.mpf(ctx.primepi(x))
    mid = ctx.li(x)
    err = ctx.sqrt(x,rounding='u')*ctx.ln(x,rounding='u')/8/mp.pi(rounding='d')
    a = ctx.floor((ctx._iv.mpf(mid)-err).a, rounding='d')
    b = ctx.ceil((ctx._iv.mpf(mid)+err).b, rounding='u')
    return ctx._iv.mpf([a,b])

@defun_wrapped
def primezeta(ctx, s):
    if ctx.isnan(s):
        return s
    if ctx.re(s) <= 0:
        raise ValueError("prime zeta function defined only for re(s) > 0")
    if s == 1:
        return ctx.inf
    if s == 0.5:
        return ctx.mpc(ctx.ninf, mp.pi)
    r = ctx.re(s)
    if r > ctx.prec:
        return 0.5**s
    else:
        wp = ctx.prec + int(r)
        def terms():
            orig = ctx.prec
            k = 0
            while 1:
                k += 1
                u = ctx.moebius(k)
                if not u:
                    continue
                ctx.prec = wp
                t = u*ctx.ln(ctx.zeta(k*s))/k
                if not t:
                    return
                ctx.prec = orig
                yield t
    return ctx.sum_accurately(terms)

@defun_wrapped
def bernpoly(ctx, n, z):
    n = int(n)
    if n < 0:
        raise ValueError("Bernoulli polynomials only defined for n >= 0")
    if z == 0 or (z == 1 and n > 1):
        return ctx.bernoulli(n)
    if z == 0.5:
        return (ctx.ldexp(1,1-n)-1)*ctx.bernoulli(n)
    if n <= 3:
        if n == 0: return z ** 0
        if n == 1: return z - 0.5
        if n == 2: return (6*z*(z-1)+1)/6
        if n == 3: return z*(z*(z-1.5)+0.5)
    if ctx.isinf(z):
        return z ** n
    if ctx.isnan(z):
        return z
    if abs(z) > 2:
        def terms():
            t = ctx.one
            yield t
            r = ctx.one/z
            k = 1
            while k <= n:
                t = t*(n+1-k)/k*r
                if not (k > 2 and k & 1):
                    yield t*ctx.bernoulli(k)
                k += 1
        return ctx.sum_accurately(terms)*z**n
    else:
        def terms():
            yield ctx.bernoulli(n)
            t = ctx.one
            k = 1
            while k <= n:
                t = t*(n+1-k)/k*z
                m = n-k
                if not (m > 2 and m & 1):
                    yield t*ctx.bernoulli(m)
                k += 1
        return ctx.sum_accurately(terms)

@defun_wrapped
def eulerpoly(ctx, n, z):
    n = int(n)
    if n < 0:
        raise ValueError("Euler polynomials only defined for n >= 0")
    if n <= 2:
        if n == 0: return z ** 0
        if n == 1: return z - 0.5
        if n == 2: return z*(z-1)
    if ctx.isinf(z):
        return z**n
    if ctx.isnan(z):
        return z
    m = n+1
    if z == 0:
        return -2*(ctx.ldexp(1,m)-1)*ctx.bernoulli(m)/m*z**0
    if z == 1:
        return 2*(ctx.ldexp(1,m)-1)*ctx.bernoulli(m)/m*z**0
    if z == 0.5:
        if n % 2:
            return ctx.zero
        if n < 100 or n*ctx.mag(0.46839865*n) < ctx.prec*0.25:
            return ctx.ldexp(ctx._eulernum(n), -n)
    def terms():
        t = ctx.one
        k = 0
        w = ctx.ldexp(1,n+2)
        while 1:
            v = n-k+1
            if not (v > 2 and v & 1):
                yield (2-w)*ctx.bernoulli(v)*t
            k += 1
            if k > n:
                break
            t = t*z*(n-k+2)/k
            w *= 0.5
    return ctx.sum_accurately(terms) / m

@defun
def eulernum(ctx, n, exact=False):
    n = int(n)
    if exact:
        return int(ctx._eulernum(n))
    if n < 100:
        return ctx.mpf(ctx._eulernum(n))
    if n % 2:
        return ctx.zero
    return ctx.ldexp(ctx.eulerpoly(n,0.5), n)

def polylog_series(ctx, s, z):
    tol = +mp.eps
    l = ctx.zero
    k = 1
    zk = z
    while 1:
        term = zk / k**s
        l += term
        if abs(term) < tol:
            break
        zk *= z
        k += 1
    return l

def polylog_continuation(ctx, n, z):
    if n < 0:
        return z*0
    twopij = 2j*mp.pi
    a = -twopij**n/ctx.fac(n)*ctx.bernpoly(n, ctx.ln(z)/twopij)
    if ctx._is_real_type(z) and z < 0:
        a = ctx._re(a)
    if ctx._im(z) < 0 or (ctx._im(z) == 0 and ctx._re(z) >= 1):
        a -= twopij*ctx.ln(z)**(n-1)/ctx.fac(n-1)
    return a

def polylog_unitcircle(ctx, n, z):
    tol = +mp.eps
    if n > 1:
        l = ctx.zero
        logz = ctx.ln(z)
        logmz = ctx.one
        m = 0
        while 1:
            if (n-m) != 1:
                term = ctx.zeta(n-m)*logmz / ctx.fac(m)
                if term and abs(term) < tol:
                    break
                l += term
            logmz *= logz
            m += 1
        l += ctx.ln(z)**(n-1)/ctx.fac(n-1)*(ctx.harmonic(n-1)-ctx.ln(-ctx.ln(z)))
    elif n < 1:  # else
        l = ctx.fac(-n)*(-ctx.ln(z))**(n-1)
        logz = ctx.ln(z)
        logkz = ctx.one
        k = 0
        while 1:
            b = ctx.bernoulli(k-n+1)
            if b:
                term = b*logkz/(ctx.fac(k)*(k-n+1))
                if abs(term) < tol:
                    break
                l -= term
            logkz *= logz
            k += 1
    else:
        raise ValueError
    if ctx._is_real_type(z) and z < 0:
        l = ctx._re(l)
    return l

def polylog_general(ctx, s, z):
    v = ctx.zero
    u = ctx.ln(z)
    if not abs(u) < 5:
        raise NotImplementedError("polylog for arbitrary s and z")
    t = 1
    k = 0
    while 1:
        term = ctx.zeta(s-k)*t
        if abs(term) < EPS:
            break
        v += term
        k += 1
        t *= u
        t /= k
    return ctx.gamma(1-s)*(-u)**(s-1) + v

@defun_wrapped
def polylog(ctx, s, z):
    s = ctx.convert(s)
    z = ctx.convert(z)
    if z == 1:
        return ctx.zeta(s)
    if z == -1:
        return -ctx.altzeta(s)
    if s == 0:
        return z/(1-z)
    if s == 1:
        return -ctx.ln(1-z)
    if s == -1:
        return z/(1-z)**2
    if abs(z) <= 0.75 or (not ctx.isint(s) and abs(z) < 0.9):
        return polylog_series(ctx, s, z)
    if abs(z) >= 1.4 and ctx.isint(s):
        return (-1)**(s+1)*polylog_series(ctx, s, 1/z) + polylog_continuation(ctx, s, z)
    if ctx.isint(s):
        return polylog_unitcircle(ctx, int(s), z)
    return polylog_general(ctx, s, z)

@defun_wrapped
def clsin(ctx, s, z, pi=False):
    if ctx.isint(s) and s < 0 and int(s) % 2 == 1:
        return z*0
    if pi:
        a = ctx.expjpi(z)
    else:
        a = ctx.expj(z)
    if ctx._is_real_type(z) and ctx._is_real_type(s):
        return ctx.im(ctx.polylog(s,a))
    b = 1/a
    return (-0.5j)*(ctx.polylog(s,a) - ctx.polylog(s,b))

@defun_wrapped
def clcos(ctx, s, z, pi=False):
    if ctx.isint(s) and s < 0 and int(s) % 2 == 0:
        return z*0
    if pi:
        a = ctx.expjpi(z)
    else:
        a = ctx.expj(z)
    if ctx._is_real_type(z) and ctx._is_real_type(s):
        return ctx.re(ctx.polylog(s,a))
    b = 1/a
    return 0.5*(ctx.polylog(s,a) + ctx.polylog(s,b))

@defun
def altzeta(ctx, s, **kwargs):
    try:
        return ctx._altzeta(s, **kwargs)
    except NotImplementedError:
        return ctx._altzeta_generic(s)

@defun_wrapped
def _altzeta_generic(ctx, s):
    if s == 1:
        return ctx.ln2 + 0*s
    return -ctx.powm1(2, 1-s)*ctx.zeta(s)

@defun
def zeta(ctx, s, a=1, derivative=0, method=None, **kwargs):
    d = int(derivative)
    if a == 1 and not (d or method):
        try:
            return ctx._zeta(s, **kwargs)
        except NotImplementedError:
            pass
    s = ctx.convert(s)
    prec = ctx.prec
    method = kwargs.get('method')
    verbose = kwargs.get('verbose')
    if (not s) and (not derivative):
        return ctx.mpf(0.5) - ctx._convert_param(a)[0]
    if a == 1 and method != 'euler-maclaurin':
        im = abs(ctx._im(s))
        re = abs(ctx._re(s))

        if abs(im) > 500*prec and 10*re < prec and derivative <= 4 or \
            method == 'riemann-siegel':
            try:   #  py2.4 compatible try block
                try:
                    if verbose:
                        print("zeta: Attempting to use the Riemann-Siegel algorithm")
                    return ctx.rs_zeta(s, derivative, **kwargs)
                except NotImplementedError:
                    if verbose:
                        print("zeta: Could not use the Riemann-Siegel algorithm")
                    pass
            finally:
                ctx.prec = prec
    if s == 1:
        return ctx.inf
    abss = abs(s)
    if abss == ctx.inf:
        if ctx.re(s) == ctx.inf:
            if d == 0:
                return ctx.one
            return ctx.zero
        return s*0
    elif ctx.isnan(abss):
        return 1/s
    if ctx.re(s) > 2*ctx.prec and a == 1 and not derivative:
        return ctx.one + ctx.power(2, -s)
    return +ctx._hurwitz(s, a, d, **kwargs)

@defun
def _hurwitz(ctx, s, a=1, d=0, **kwargs):
    prec = ctx.prec
    verbose = kwargs.get('verbose')
    try:
        extraprec = 10
        ctx.prec += extraprec
        a, atype = ctx._convert_param(a)
        if ctx.re(s) < 0:
            if verbose:
                print("zeta: Attempting reflection formula")
            try:
                return _hurwitz_reflection(ctx, s, a, d, atype)
            except NotImplementedError:
                pass
            if verbose:
                print("zeta: Reflection formula failed")
        if verbose:
            print("zeta: Using the Euler-Maclaurin algorithm")
        while 1:
            ctx.prec = prec + extraprec
            T1, T2 = _hurwitz_em(ctx, s, a, d, prec+10, verbose)
            cancellation = ctx.mag(T1) - ctx.mag(T1+T2)
            if verbose:
                print_("Term 1:", T1)
                print_("Term 2:", T2)
                print_("Cancellation:", cancellation, "bits")
            if cancellation < extraprec:
                return T1 + T2
            else:
                extraprec = max(2*extraprec, min(cancellation + 5, 100*prec))
                if extraprec > kwargs.get('maxprec', 100*prec):
                    raise ctx.NoConvergence("zeta: too much cancellation")
    finally:
        ctx.prec = prec

def _hurwitz_reflection(ctx, s, a, d, atype):
    if d != 0:
        raise NotImplementedError
    res = ctx.re(s)
    negs = -s
    if ctx.isnpint(s):
        n = int(res)
        if n <= 0:
            return ctx.bernpoly(1-n, a) / (n-1)
    t = 1-s
    v = 0
    shift = 0
    b = a
    while ctx.re(b) > 1:
        b -= 1
        v -= b**negs
        shift -= 1
    while ctx.re(b) <= 0:
        v += b**negs
        b += 1
        shift += 1
    if atype == 'Q' or atype == 'Z':
        try:
            p, q = a._mpq_
        except:
            assert a == int(a)
            p = int(a)
            q = 1
        p += shift*q
        assert 1 <= p <= q
        g = ctx.fsum(ctx.cospi(t/2-2*k*b)*ctx._hurwitz(t,(k,q)) \
            for k in range(1,q+1))
        g *= 2*ctx.gamma(t)/(2*mp.pi*q)**t
        v += g
        return v
    else:
        C1, C2 = ctx.cospi_sinpi(0.5*t)
        if C1: C1 *= ctx.clcos(t, 2*a, pi=True)
        if C2: C2 *= ctx.clsin(t, 2*a, pi=True)
        v += 2*ctx.gamma(t)/(2*mp.pi)**t*(C1+C2)
        return v

def _hurwitz_em(ctx, s, a, d, prec, verbose):
    a = ctx.convert(a)
    tol = -prec
    M1 = 0
    M2 = prec // 3
    N = M2
    lsum = 0
    if ctx.isint(s):
        s = int(ctx._re(s))
    s1 = s-1
    while 1:
        l = ctx._zetasum(s, M1+a, M2-M1-1, [d])[0][0]
        lsum += l
        M2a = M2+a
        logM2a = ctx.ln(M2a)
        logM2ad = logM2a**d
        logs = [logM2ad]
        logr = 1/logM2a
        rM2a = 1/M2a
        M2as = rM2a**s
        if d:
            tailsum = ctx.gammainc(d+1, s1*logM2a) / s1**(d+1)
        else:
            tailsum = 1/((s1)*(M2a)**s1)
        tailsum += 0.5*logM2ad*M2as
        U = [1]
        r = M2as
        fact = 2
        for j in range(1, N+1):
            j2 = 2*j
            if j == 1:
                upds = [1]
            else:
                upds = [j2-2, j2-1]
            for m in upds:
                D = min(m,d+1)
                if m <= d:
                    logs.append(logs[-1]*logr)
                Un = [0]*(D+1)
                for i in range(D): Un[i] = (1-m-s)*U[i]
                for i in range(1,D+1): Un[i] += (d-(i-1))*U[i-1]
                U = Un
                r *= rM2a
            t = ctx.fdot(U, logs)*r*ctx.bernoulli(j2)/(-fact)
            tailsum += t
            if ctx.mag(t) < tol:
                return lsum, (-1)**d*tailsum
            fact *= (j2+1)*(j2+2)
        if verbose:
            print_("Sum range:", M1, M2, "term magnitude", ctx.mag(t), "tolerance", tol)
        M1, M2 = M2, M2*2
        if ctx.re(s) < 0:
            N += N//2


@defun
def _zetasum(ctx, s, a, n, derivatives=[0], reflect=False):
    if abs(ctx.re(s)) < 0.5*ctx.prec:
        try:
            return ctx._zetasum_fast(s, a, n, derivatives, reflect)
        except NotImplementedError:
            pass
    negs = ctx.fneg(s, exact=True)
    have_derivatives = derivatives != [0]
    have_one_derivative = len(derivatives) == 1
    if not reflect:
        if not have_derivatives:
            return [ctx.fsum((a+k)**negs for k in range(n+1))], []
        if have_one_derivative:
            d = derivatives[0]
            x = ctx.fsum(ctx.ln(a+k)**d*(a+k)**negs for k in range(n+1))
            return [(-1)**d*x], []
    maxd = max(derivatives)
    if not have_one_derivative:
        derivatives = range(maxd+1)
    xs = [ctx.zero for d in derivatives]
    if reflect:
        ys = [ctx.zero for d in derivatives]
    else:
        ys = []
    for k in range(n+1):
        w = a + k
        xterm = w ** negs
        if reflect:
            yterm = ctx.conj(ctx.one / (w*xterm))
        if have_derivatives:
            logw = -ctx.ln(w)
            if have_one_derivative:
                logw = logw ** maxd
                xs[0] += xterm*logw
                if reflect:
                    ys[0] += yterm*logw
            else:
                t = ctx.one
                for d in derivatives:
                    xs[d] += xterm*t
                    if reflect:
                        ys[d] += yterm*t
                    t *= logw
        else:
            xs[0] += xterm
            if reflect:
                ys[0] += yterm
    return xs, ys

@defun
def dirichlet(ctx, s, chi=[1], derivative=0):
    s = ctx.convert(s)
    q = len(chi)
    d = int(derivative)
    if d > 2:
        raise NotImplementedError("arbitrary order derivatives")
    prec = ctx.prec
    try:
        ctx.prec += 10
        if s == 1:
            have_pole = True
            for x in chi:
                if x and x != 1:
                    have_pole = False
                    h = +mp.eps
                    ctx.prec *= 2*(d+1)
                    s += h
            if have_pole:
                return +ctx.inf
        z = ctx.zero
        for p in range(1,q+1):
            if chi[p%q]:
                if d == 1:
                    z += chi[p%q]*(ctx.zeta(s, (p,q), 1) - \
                        ctx.zeta(s, (p,q))*ctx.log(q))
                else:
                    z += chi[p%q]*ctx.zeta(s, (p,q))
        z /= q**s
    finally:
        ctx.prec = prec
    return +z


def secondzeta_main_term(ctx, s, a, **kwargs):
    tol = mp.eps
    f = lambda n: ctx.gammainc(0.5*s, a*gamm**2, regularized=True)*gamm**(-s)
    totsum = term = ctx.zero
    mg = ctx.inf
    n = 0
    while mg > tol:
        totsum += term
        n += 1
        gamm = ctx.im(ctx.zetazero_memoized(n))
        term = f(n)
        mg = abs(term)
    err = 0
    if kwargs.get("error"):
        sg = ctx.re(s)
        err = 0.5*mp.pi**(-1)*max(1,sg)*a**(sg-0.5)*ctx.log(gamm/(2*mp.pi))*\
             ctx.gammainc(-0.5, a*gamm**2)/abs(ctx.gamma(s/2))
        err = abs(err)
    return +totsum, err, n

def secondzeta_prime_term(ctx, s, a, **kwargs):
    tol = mp.eps
    f = lambda n: ctx.gammainc(0.5*(1-s),0.25*ctx.log(n)**2*a**(-1))*\
        ((0.5*ctx.log(n))**(s-1))*ctx.mangoldt(n)/ctx.sqrt(n)/\
        (2*ctx.gamma(0.5*s)*ctx.sqrt(mp.pi))
    totsum = term = ctx.zero
    mg = ctx.inf
    n = 1
    while mg > tol or n < 9:
        totsum += term
        n += 1
        term = f(n)
        if term == 0:
            mg = ctx.inf
        else:
            mg = abs(term)
    if kwargs.get("error"):
        err = mg
    return +totsum, err, n

def secondzeta_exp_term(ctx, s, a):
    if ctx.isint(s) and ctx.re(s) <= 0:
        m = int(round(ctx.re(s)))
        if not m & 1:
            return ctx.mpf('-0.25')**(-m//2)
    tol = mp.eps
    f = lambda n: (0.25*a)**n/((n+0.5*s)*ctx.fac(n))
    totsum = ctx.zero
    term = f(0)
    mg = ctx.inf
    n = 0
    while mg > tol:
        totsum += term
        n += 1
        term = f(n)
        mg = abs(term)
    v = a**(0.5*s)*totsum/ctx.gamma(0.5*s)
    return v

def secondzeta_singular_term(ctx, s, a, **kwargs):
    factor = a**(0.5*(s-1))/(4*ctx.sqrt(mp.pi)*ctx.gamma(0.5*s))
    extraprec = ctx.mag(factor)
    ctx.prec += extraprec
    factor = a**(0.5*(s-1))/(4*ctx.sqrt(mp.pi)*ctx.gamma(0.5*s))
    tol = mp.eps
    f = lambda n: ctx.bernpoly(n,0.75)*(4*ctx.sqrt(a))**n*\
       ctx.gamma(0.5*n)/((s+n-1)*ctx.fac(n))
    totsum = ctx.zero
    mg1 = ctx.inf
    n = 1
    term = f(n)
    mg2 = abs(term)
    while mg2 > tol and mg2 <= mg1:
        totsum += term
        n += 1
        term = f(n)
        totsum += term
        n +=1
        term = f(n)
        mg1 = mg2
        mg2 = abs(term)
    totsum += term
    pole = -2*(s-1)**(-2)+(ctx.euler+ctx.log(16*mp.pi**2*a))*(s-1)**(-1)
    st = factor*(pole+totsum)
    err = 0
    if kwargs.get("error"):
        if not ((mg2 > tol) and (mg2 <= mg1)):
            if mg2 <= tol:
                err = ctx.mpf(10)**int(ctx.log(abs(factor*tol),10))
            if mg2 > mg1:
                err = ctx.mpf(10)**int(ctx.log(abs(factor*mg1),10))
        err = max(err, mp.eps*1.)
    ctx.prec -= extraprec
    return +st, err

@defun
def secondzeta(ctx, s, a = 0.015, **kwargs):
    s = ctx.convert(s)
    a = ctx.convert(a)
    tol = mp.eps
    if ctx.isint(s) and ctx.re(s) <= 1:
        if abs(s-1) < tol*1000:
            return ctx.inf
        m = int(round(ctx.re(s)))
        if m & 1:
            return ctx.inf
        else:
            return ((-1)**(-m//2)*\
                   ctx.fraction(8-ctx.eulernum(-m,exact=True),2**(-m+3)))
    prec = ctx.prec
    try:
        t3 = secondzeta_exp_term(ctx, s, a)
        extraprec = max(ctx.mag(t3),0)
        ctx.prec += extraprec + 3
        t1, r1, gt = secondzeta_main_term(ctx,s,a,error='True', verbose='True')
        t2, r2, pt = secondzeta_prime_term(ctx,s,a,error='True', verbose='True')
        t4, r4 = secondzeta_singular_term(ctx,s,a,error='True')
        t3 = secondzeta_exp_term(ctx, s, a)
        err = r1+r2+r4
        t = t1-t2+t3-t4
        if kwargs.get("verbose"):
            print_('main term =', t1)
            print_('    computed using', gt, 'zeros of zeta')
            print_('prime term =', t2)
            print_('    computed using', pt, 'values of the von Mangoldt function')
            print_('exponential term =', t3)
            print_('singular term =', t4)
    finally:
        ctx.prec = prec
    if kwargs.get("error"):
        w = max(ctx.mag(abs(t)),0)
        err = max(err*2**w, mp.eps*1.*2**w)
        return +t, err
    return +t

@defun_wrapped
def lerchphi(ctx, z, s, a):
    if z == 0:
        return a ** (-s)
    if z == 1:
        return ctx.zeta(s, a)
    if a == 1:
        return ctx.polylog(s, z) / z
    if ctx.re(a) < 1:
        if ctx.isnpint(a):
            raise ValueError("Lerch transcendent complex infinity")
        m = int(ctx.ceil(1-ctx.re(a)))
        v = ctx.zero
        zpow = ctx.one
        for n in range(m):
            v += zpow / (a+n)**s
            zpow *= z
        return zpow*ctx.lerchphi(z,s, a+m) + v
    g = ctx.ln(z)
    v = 1/(2*a**s) + ctx.gammainc(1-s, -a*g)*(-g)**(s-1) / z**a
    h = s / 2
    r = 2*mp.pi
    f = lambda t: ctx.sin(s*ctx.atan(t/a)-t*g) / \
        ((a**2+t**2)**h*ctx.expm1(r*t))
    v += 2*ctx.quad(f, [0, ctx.inf])
    if not ctx.im(z) and not ctx.im(s) and not ctx.im(a) and ctx.re(z) < 1:
        v = ctx.chop(v)
    return v

def find_rosser_block_zero(ctx, n):
    for k in range(len(_ROSSER_EXCEPTIONS)//2):
        a=_ROSSER_EXCEPTIONS[2*k][0]
        b=_ROSSER_EXCEPTIONS[2*k][1]
        if ((a<= n-2) and (n-1 <= b)):
            t0 = ctx.grampoint(a)
            t1 = ctx.grampoint(b)
            v0 = ctx._fp.siegelz(t0)
            v1 = ctx._fp.siegelz(t1)
            my_zero_number = n-a-1
            zero_number_block = b-a
            pattern = _ROSSER_EXCEPTIONS[2*k+1]
            return (my_zero_number, [a,b], [t0,t1], [v0,v1])
    k = n-2
    t,v,b = compute_triple_tvb(ctx, k)
    T = [t]
    V = [v]
    while b < 0:
        k -= 1
        t,v,b = compute_triple_tvb(ctx, k)
        T.insert(0,t)
        V.insert(0,v)
    my_zero_number = n-k-1
    m = n-1
    t,v,b = compute_triple_tvb(ctx, m)
    T.append(t)
    V.append(v)
    while b < 0:
        m += 1
        t,v,b = compute_triple_tvb(ctx, m)
        T.append(t)
        V.append(v)
    return (my_zero_number, [k,m], T, V)

def wpzeros(t):
    wp = 53
    if t > 3*10**8:
        wp = 63
    if t > 10**11:
        wp = 70
    if t > 10**14:
        wp = 83
    return wp

def separate_zeros_in_block(ctx, zero_number_block, T, V, limitloop=None,
    fp_tolerance=None):
    if limitloop is None:
        limitloop = ctx.inf
    loopnumber = 0
    variations = count_variations(V)
    while ((variations < zero_number_block) and (loopnumber <limitloop)):
        a = T[0]
        v = V[0]
        newT = [a]
        newV = [v]
        variations = 0
        for n in range(1,len(T)):
            b2 = T[n]
            u = V[n]
            if (u*v>0):
                alpha = ctx.sqrt(u/v)
                b= (alpha*a+b2)/(alpha+1)
            else:
                b = (a+b2)/2
            if fp_tolerance < 10:
                w = ctx._fp.siegelz(b)
                if abs(w)<fp_tolerance:
                    w = ctx.siegelz(b)
            else:
                w=ctx.siegelz(b)
            if v*w<0:
                variations += 1
            newT.append(b)
            newV.append(w)
            u = V[n]
            if u*w <0:
                variations += 1
            newT.append(b2)
            newV.append(u)
            a = b2
            v = u
        T = newT
        V = newV
        loopnumber +=1
        if (limitloop>ITERATION_LIMIT)and(loopnumber>2)and(variations+2==zero_number_block):
            dtMax=0
            dtSec=0
            kMax = 0
            for k1 in range(1,len(T)):
                dt = T[k1]-T[k1-1]
                if dt > dtMax:
                    kMax=k1
                    dtSec = dtMax
                    dtMax = dt
                elif  (dt<dtMax) and(dt >dtSec):
                    dtSec = dt
            if dtMax>3*dtSec:
                f = lambda x: ctx.rs_z(x,derivative=1)
                t0=T[kMax-1]
                t1 = T[kMax]
                t=ctx.findroot(f,  (t0,t1), solver ='illinois',verify=False, verbose=False)
                v = ctx.siegelz(t)
                if (t0<t) and (t<t1) and (v*V[kMax]<0):
                    T.insert(kMax,t)
                    V.insert(kMax,v)
        variations = count_variations(V)
    if variations == zero_number_block:
        separated = True
    else:
        separated = False
    return (T,V, separated)

def separate_my_zero(ctx, my_zero_number, zero_number_block, T, V, prec):
    variations = 0
    v0 = V[0]
    for k in range(1,len(V)):
        v1 = V[k]
        if v0*v1 < 0:
            variations +=1
            if variations == my_zero_number:
                k0 = k
                leftv = v0
                rightv = v1
        v0 = v1
    t1 = T[k0]
    t0 = T[k0-1]
    ctx.prec = prec
    wpz = wpzeros(my_zero_number*ctx.log(my_zero_number))

    guard = 4*ctx.mag(my_zero_number)
    precs = [ctx.prec+4]
    index=0
    while precs[0] > 2*wpz:
        index +=1
        precs = [precs[0] // 2 +3+2*index] + precs
    ctx.prec = precs[0] + guard
    r = ctx.findroot(lambda x:ctx.siegelz(x), (t0,t1), solver ='illinois', verbose=False)
    z=ctx.mpc(0.5,r)
    for prec in precs[1:]:
        ctx.prec = prec + guard
        znew = z - ctx.zeta(z) / ctx.zeta(z, derivative=1)
        z=ctx.mpc(0.5,ctx.im(znew))
    return ctx.im(z)

def sure_number_block(ctx, n):
    if n < 9*10**5:
        return(2)
    g = ctx.grampoint(n-100)
    lg = ctx._fp.ln(g)
    brent = 0.0061*lg**2 +0.08*lg
    trudgian = 0.0031*lg**2 +0.11*lg
    N = ctx.ceil(min(brent,trudgian))
    N = int(N)
    return N

def compute_triple_tvb(ctx, n):
    t = ctx.grampoint(n)
    v = ctx._fp.siegelz(t)
    if ctx.mag(abs(v))<ctx.mag(t)-45:
        v = ctx.siegelz(t)
    b = v*(-1)**n
    return t,v,b

ITERATION_LIMIT = 4

def search_supergood_block(ctx, n, fp_tolerance):
    sb = sure_number_block(ctx, n)
    number_goodblocks = 0
    m2 = n-1
    t, v, b = compute_triple_tvb(ctx, m2)
    Tf = [t]
    Vf = [v]
    while b < 0:
        m2 += 1
        t,v,b = compute_triple_tvb(ctx, m2)
        Tf.append(t)
        Vf.append(v)
    goodpoints = [m2]
    T = [t]
    V = [v]
    while number_goodblocks < 2*sb:
        m2 += 1
        t, v, b = compute_triple_tvb(ctx, m2)
        T.append(t)
        V.append(v)
        while b < 0:
            m2 += 1
            t,v,b = compute_triple_tvb(ctx, m2)
            T.append(t)
            V.append(v)
        goodpoints.append(m2)
        zn = len(T)-1
        A, B, separated =\
           separate_zeros_in_block(ctx, zn, T, V, limitloop=ITERATION_LIMIT,
                fp_tolerance=fp_tolerance)
        Tf.pop()
        Tf.extend(A)
        Vf.pop()
        Vf.extend(B)
        if separated:
            number_goodblocks += 1
        else:
            number_goodblocks = 0
        T = [t]
        V = [v]
    number_goodblocks = 0
    m2 = n-2
    t, v, b = compute_triple_tvb(ctx, m2)
    Tf.insert(0,t)
    Vf.insert(0,v)
    while b < 0:
        m2 -= 1
        t,v,b = compute_triple_tvb(ctx, m2)
        Tf.insert(0,t)
        Vf.insert(0,v)
    goodpoints.insert(0,m2)
    T = [t]
    V = [v]
    while number_goodblocks < 2*sb:
        m2 -= 1
        t, v, b = compute_triple_tvb(ctx, m2)
        T.insert(0,t)
        V.insert(0,v)
        while b < 0:
            m2 -= 1
            t,v,b = compute_triple_tvb(ctx, m2)
            T.insert(0,t)
            V.insert(0,v)
        goodpoints.insert(0,m2)
        zn = len(T)-1
        A, B, separated =\
           separate_zeros_in_block(ctx, zn, T, V, limitloop=ITERATION_LIMIT, fp_tolerance=fp_tolerance)
        A.pop()
        Tf = A+Tf
        B.pop()
        Vf = B+Vf
        if separated:
            number_goodblocks += 1
        else:
            number_goodblocks = 0
        T = [t]
        V = [v]
    r = goodpoints[2*sb]
    lg = len(goodpoints)
    s = goodpoints[lg-2*sb-1]
    tr, vr, br = compute_triple_tvb(ctx, r)
    ar = Tf.index(tr)
    ts, vs, bs = compute_triple_tvb(ctx, s)
    as1 = Tf.index(ts)
    T = Tf[ar:as1+1]
    V = Vf[ar:as1+1]
    zn = s-r
    A, B, separated =\
       separate_zeros_in_block(ctx, zn,T,V,limitloop=ITERATION_LIMIT, fp_tolerance=fp_tolerance)
    if separated:
        return (n-r-1,[r,s],A,B)
    q = goodpoints[sb]
    lg = len(goodpoints)
    t = goodpoints[lg-sb-1]
    tq, vq, bq = compute_triple_tvb(ctx, q)
    aq = Tf.index(tq)
    tt, vt, bt = compute_triple_tvb(ctx, t)
    at = Tf.index(tt)
    T = Tf[aq:at+1]
    V = Vf[aq:at+1]
    return (n-q-1,[q,t],T,V)

def count_variations(V):
    count = 0
    vold = V[0]
    for n in range(1, len(V)):
        vnew = V[n]
        if vold*vnew < 0:
            count +=1
        vold = vnew
    return count

def pattern_construct(ctx, block, T, V):
    pattern = '('
    a = block[0]
    b = block[1]
    t0,v0,b0 = compute_triple_tvb(ctx, a)
    k = 0
    k0 = 0
    for n in range(a+1,b+1):
        t1,v1,b1 = compute_triple_tvb(ctx, n)
        lgT =len(T)
        while (k < lgT) and (T[k] <= t1):
            k += 1
        L = V[k0:k]
        L.append(v1)
        L.insert(0,v0)
        count = count_variations(L)
        pattern = pattern + ("%s" % count)
        if b1 > 0:
            pattern = pattern + ')('
        k0 = k
        t0,v0,b0 = t1,v1,b1
    pattern = pattern[:-1]
    return pattern


_ROSSER_EXCEPTIONS = \
[[13999525, 13999528], '(00)3',
[30783329, 30783332], '(00)3',
[30930926, 30930929], '3(00)',
[37592215, 37592218], '(00)3',
[40870156, 40870159], '(00)3',
[43628107, 43628110], '(00)3',
[46082042, 46082045], '(00)3',
[46875667, 46875670], '(00)3',
[49624540, 49624543], '3(00)',
[50799238, 50799241], '(00)3',
[55221453, 55221456], '3(00)',
[56948779, 56948782], '3(00)',
[60515663, 60515666], '(00)3',
[61331766, 61331770], '(00)40',
[69784843, 69784846], '3(00)',
[75052114, 75052117], '(00)3',
[79545240, 79545243], '3(00)',
[79652247, 79652250], '3(00)',
[83088043, 83088046], '(00)3',
[83689522, 83689525], '3(00)',
[85348958, 85348961], '(00)3',
[86513820, 86513823], '(00)3',
[87947596, 87947599], '3(00)',
[88600095, 88600098], '(00)3',
[93681183, 93681186], '(00)3',
[100316551, 100316554], '3(00)',
[100788444, 100788447], '(00)3',
[106236172, 106236175], '(00)3',
[106941327, 106941330], '3(00)',
[107287955, 107287958], '(00)3',
[107532016, 107532019], '3(00)',
[110571044, 110571047], '(00)3',
[111885253, 111885256], '3(00)',
[113239783, 113239786], '(00)3',
[120159903, 120159906], '(00)3',
[121424391, 121424394], '3(00)',
[121692931, 121692934], '3(00)',
[121934170, 121934173], '3(00)',
[122612848, 122612851], '3(00)',
[126116567, 126116570], '(00)3',
[127936513, 127936516], '(00)3',
[128710277, 128710280], '3(00)',
[129398902, 129398905], '3(00)',
[130461096, 130461099], '3(00)',
[131331947, 131331950], '3(00)',
[137334071, 137334074], '3(00)',
[137832603, 137832606], '(00)3',
[138799471, 138799474], '3(00)',
[139027791, 139027794], '(00)3',
[141617806, 141617809], '(00)3',
[144454931, 144454934], '(00)3',
[145402379, 145402382], '3(00)',
[146130245, 146130248], '3(00)',
[147059770, 147059773], '(00)3',
[147896099, 147896102], '3(00)',
[151097113, 151097116], '(00)3',
[152539438, 152539441], '(00)3',
[152863168, 152863171], '3(00)',
[153522726, 153522729], '3(00)',
[155171524, 155171527], '3(00)',
[155366607, 155366610], '(00)3',
[157260686, 157260689], '3(00)',
[157269224, 157269227], '(00)3',
[157755123, 157755126], '(00)3',
[158298484, 158298487], '3(00)',
[160369050, 160369053], '3(00)',
[162962787, 162962790], '(00)3',
[163724709, 163724712], '(00)3',
[164198113, 164198116], '3(00)',
[164689301, 164689305], '(00)40',
[164880228, 164880231], '3(00)',
[166201932, 166201935], '(00)3',
[168573836, 168573839], '(00)3',
[169750763, 169750766], '(00)3',
[170375507, 170375510], '(00)3',
[170704879, 170704882], '3(00)',
[172000992, 172000995], '3(00)',
[173289941, 173289944], '(00)3',
[173737613, 173737616], '3(00)',
[174102513, 174102516], '(00)3',
[174284990, 174284993], '(00)3',
[174500513, 174500516], '(00)3',
[175710609, 175710612], '(00)3',
[176870843, 176870846], '3(00)',
[177332732, 177332735], '3(00)',
[177902861, 177902864], '3(00)',
[179979095, 179979098], '(00)3',
[181233726, 181233729], '3(00)',
[181625435, 181625438], '(00)3',
[182105255, 182105259], '22(00)',
[182223559, 182223562], '3(00)',
[191116404, 191116407], '3(00)',
[191165599, 191165602], '3(00)',
[191297535, 191297539], '(00)22',
[192485616, 192485619], '(00)3',
[193264634, 193264638], '22(00)',
[194696968, 194696971], '(00)3',
[195876805, 195876808], '(00)3',
[195916548, 195916551], '3(00)',
[196395160, 196395163], '3(00)',
[196676303, 196676306], '(00)3',
[197889882, 197889885], '3(00)',
[198014122, 198014125], '(00)3',
[199235289, 199235292], '(00)3',
[201007375, 201007378], '(00)3',
[201030605, 201030608], '3(00)',
[201184290, 201184293], '3(00)',
[201685414, 201685418], '(00)22',
[202762875, 202762878], '3(00)',
[202860957, 202860960], '3(00)',
[203832577, 203832580], '3(00)',
[205880544, 205880547], '(00)3',
[206357111, 206357114], '(00)3',
[207159767, 207159770], '3(00)',
[207167343, 207167346], '3(00)',
[207482539, 207482543], '3(010)',
[207669540, 207669543], '3(00)',
[208053426, 208053429], '(00)3',
[208110027, 208110030], '3(00)',
[209513826, 209513829], '3(00)',
[212623522, 212623525], '(00)3',
[213841715, 213841718], '(00)3',
[214012333, 214012336], '(00)3',
[214073567, 214073570], '(00)3',
[215170600, 215170603], '3(00)',
[215881039, 215881042], '3(00)',
[216274604, 216274607], '3(00)',
[216957120, 216957123], '3(00)',
[217323208, 217323211], '(00)3',
[218799264, 218799267], '(00)3',
[218803557, 218803560], '3(00)',
[219735146, 219735149], '(00)3',
[219830062, 219830065], '3(00)',
[219897904, 219897907], '(00)3',
[221205545, 221205548], '(00)3',
[223601929, 223601932], '(00)3',
[223907076, 223907079], '3(00)',
[223970397, 223970400], '(00)3',
[224874044, 224874048], '22(00)',
[225291157, 225291160], '(00)3',
[227481734, 227481737], '(00)3',
[228006442, 228006445], '3(00)',
[228357900, 228357903], '(00)3',
[228386399, 228386402], '(00)3',
[228907446, 228907449], '(00)3',
[228984552, 228984555], '3(00)',
[229140285, 229140288], '3(00)',
[231810024, 231810027], '(00)3',
[232838062, 232838065], '3(00)',
[234389088, 234389091], '3(00)',
[235588194, 235588197], '(00)3',
[236645695, 236645698], '(00)3',
[236962876, 236962879], '3(00)',
[237516723, 237516727], '04(00)',
[240004911, 240004914], '(00)3',
[240221306, 240221309], '3(00)',
[241389213, 241389217], '(010)3',
[241549003, 241549006], '(00)3',
[241729717, 241729720], '(00)3',
[241743684, 241743687], '3(00)',
[243780200, 243780203], '3(00)',
[243801317, 243801320], '(00)3',
[244122072, 244122075], '(00)3',
[244691224, 244691227], '3(00)',
[244841577, 244841580], '(00)3',
[245813461, 245813464], '(00)3',
[246299475, 246299478], '(00)3',
[246450176, 246450179], '3(00)',
[249069349, 249069352], '(00)3',
[250076378, 250076381], '(00)3',
[252442157, 252442160], '3(00)',
[252904231, 252904234], '3(00)',
[255145220, 255145223], '(00)3',
[255285971, 255285974], '3(00)',
[256713230, 256713233], '(00)3',
[257992082, 257992085], '(00)3',
[258447955, 258447959], '22(00)',
[259298045, 259298048], '3(00)',
[262141503, 262141506], '(00)3',
[263681743, 263681746], '3(00)',
[266527881, 266527885], '(010)3',
[266617122, 266617125], '(00)3',
[266628044, 266628047], '3(00)',
[267305763, 267305766], '(00)3',
[267388404, 267388407], '3(00)',
[267441672, 267441675], '3(00)',
[267464886, 267464889], '(00)3',
[267554907, 267554910], '3(00)',
[269787480, 269787483], '(00)3',
[270881434, 270881437], '(00)3',
[270997583, 270997586], '3(00)',
[272096378, 272096381], '3(00)',
[272583009, 272583012], '(00)3',
[274190881, 274190884], '3(00)',
[274268747, 274268750], '(00)3',
[275297429, 275297432], '3(00)',
[275545476, 275545479], '3(00)',
[275898479, 275898482], '3(00)',
[275953000, 275953003], '(00)3',
[277117197, 277117201], '(00)22',
[277447310, 277447313], '3(00)',
[279059657, 279059660], '3(00)',
[279259144, 279259147], '3(00)',
[279513636, 279513639], '3(00)',
[279849069, 279849072], '3(00)',
[280291419, 280291422], '(00)3',
[281449425, 281449428], '3(00)',
[281507953, 281507956], '3(00)',
[281825600, 281825603], '(00)3',
[282547093, 282547096], '3(00)',
[283120963, 283120966], '3(00)',
[283323493, 283323496], '(00)3',
[284764535, 284764538], '3(00)',
[286172639, 286172642], '3(00)',
[286688824, 286688827], '(00)3',
[287222172, 287222175], '3(00)',
[287235534, 287235537], '3(00)',
[287304861, 287304864], '3(00)',
[287433571, 287433574], '(00)3',
[287823551, 287823554], '(00)3',
[287872422, 287872425], '3(00)',
[288766615, 288766618], '3(00)',
[290122963, 290122966], '3(00)',
[290450849, 290450853], '(00)22',
[291426141, 291426144], '3(00)',
[292810353, 292810356], '3(00)',
[293109861, 293109864], '3(00)',
[293398054, 293398057], '3(00)',
[294134426, 294134429], '3(00)',
[294216438, 294216441], '(00)3',
[295367141, 295367144], '3(00)',
[297834111, 297834114], '3(00)',
[299099969, 299099972], '3(00)',
[300746958, 300746961], '3(00)',
[301097423, 301097426], '(00)3',
[301834209, 301834212], '(00)3',
[302554791, 302554794], '(00)3',
[303497445, 303497448], '3(00)',
[304165344, 304165347], '3(00)',
[304790218, 304790222], '3(010)',
[305302352, 305302355], '(00)3',
[306785996, 306785999], '3(00)',
[307051443, 307051446], '3(00)',
[307481539, 307481542], '3(00)',
[308605569, 308605572], '3(00)',
[309237610, 309237613], '3(00)',
[310509287, 310509290], '(00)3',
[310554057, 310554060], '3(00)',
[310646345, 310646348], '3(00)',
[311274896, 311274899], '(00)3',
[311894272, 311894275], '3(00)',
[312269470, 312269473], '(00)3',
[312306601, 312306605], '(00)40',
[312683193, 312683196], '3(00)',
[314499804, 314499807], '3(00)',
[314636802, 314636805], '(00)3',
[314689897, 314689900], '3(00)',
[314721319, 314721322], '3(00)',
[316132890, 316132893], '3(00)',
[316217470, 316217474], '(010)3',
[316465705, 316465708], '3(00)',
[316542790, 316542793], '(00)3',
[320822347, 320822350], '3(00)',
[321733242, 321733245], '3(00)',
[324413970, 324413973], '(00)3',
[325950140, 325950143], '(00)3',
[326675884, 326675887], '(00)3',
[326704208, 326704211], '3(00)',
[327596247, 327596250], '3(00)',
[328123172, 328123175], '3(00)',
[328182212, 328182215], '(00)3',
[328257498, 328257501], '3(00)',
[328315836, 328315839], '(00)3',
[328800974, 328800977], '(00)3',
[328998509, 328998512], '3(00)',
[329725370, 329725373], '(00)3',
[332080601, 332080604], '(00)3',
[332221246, 332221249], '(00)3',
[332299899, 332299902], '(00)3',
[332532822, 332532825], '(00)3',
[333334544, 333334548], '(00)22',
[333881266, 333881269], '3(00)',
[334703267, 334703270], '3(00)',
[334875138, 334875141], '3(00)',
[336531451, 336531454], '3(00)',
[336825907, 336825910], '(00)3',
[336993167, 336993170], '(00)3',
[337493998, 337494001], '3(00)',
[337861034, 337861037], '3(00)',
[337899191, 337899194], '(00)3',
[337958123, 337958126], '(00)3',
[342331982, 342331985], '3(00)',
[342676068, 342676071], '3(00)',
[347063781, 347063784], '3(00)',
[347697348, 347697351], '3(00)',
[347954319, 347954322], '3(00)',
[348162775, 348162778], '3(00)',
[349210702, 349210705], '(00)3',
[349212913, 349212916], '3(00)',
[349248650, 349248653], '(00)3',
[349913500, 349913503], '3(00)',
[350891529, 350891532], '3(00)',
[351089323, 351089326], '3(00)',
[351826158, 351826161], '3(00)',
[352228580, 352228583], '(00)3',
[352376244, 352376247], '3(00)',
[352853758, 352853761], '(00)3',
[355110439, 355110442], '(00)3',
[355808090, 355808094], '(00)40',
[355941556, 355941559], '3(00)',
[356360231, 356360234], '(00)3',
[356586657, 356586660], '3(00)',
[356892926, 356892929], '(00)3',
[356908232, 356908235], '3(00)',
[357912730, 357912733], '3(00)',
[358120344, 358120347], '3(00)',
[359044096, 359044099], '(00)3',
[360819357, 360819360], '3(00)',
[361399662, 361399666], '(010)3',
[362361315, 362361318], '(00)3',
[363610112, 363610115], '(00)3',
[363964804, 363964807], '3(00)',
[364527375, 364527378], '(00)3',
[365090327, 365090330], '(00)3',
[365414539, 365414542], '3(00)',
[366738474, 366738477], '3(00)',
[368714778, 368714783], '04(010)',
[368831545, 368831548], '(00)3',
[368902387, 368902390], '(00)3',
[370109769, 370109772], '3(00)',
[370963333, 370963336], '3(00)',
[372541136, 372541140], '3(010)',
[372681562, 372681565], '(00)3',
[373009410, 373009413], '(00)3',
[373458970, 373458973], '3(00)',
[375648658, 375648661], '3(00)',
[376834728, 376834731], '3(00)',
[377119945, 377119948], '(00)3',
[377335703, 377335706], '(00)3',
[378091745, 378091748], '3(00)',
[379139522, 379139525], '3(00)',
[380279160, 380279163], '(00)3',
[380619442, 380619445], '3(00)',
[381244231, 381244234], '3(00)',
[382327446, 382327450], '(010)3',
[382357073, 382357076], '3(00)',
[383545479, 383545482], '3(00)',
[384363766, 384363769], '(00)3',
[384401786, 384401790], '22(00)',
[385198212, 385198215], '3(00)',
[385824476, 385824479], '(00)3',
[385908194, 385908197], '3(00)',
[386946806, 386946809], '3(00)',
[387592175, 387592179], '22(00)',
[388329293, 388329296], '(00)3',
[388679566, 388679569], '3(00)',
[388832142, 388832145], '3(00)',
[390087103, 390087106], '(00)3',
[390190926, 390190930], '(00)22',
[390331207, 390331210], '3(00)',
[391674495, 391674498], '3(00)',
[391937831, 391937834], '3(00)',
[391951632, 391951636], '(00)22',
[392963986, 392963989], '(00)3',
[393007921, 393007924], '3(00)',
[393373210, 393373213], '3(00)',
[393759572, 393759575], '(00)3',
[394036662, 394036665], '(00)3',
[395813866, 395813869], '(00)3',
[395956690, 395956693], '3(00)',
[396031670, 396031673], '3(00)',
[397076433, 397076436], '3(00)',
[397470601, 397470604], '3(00)',
[398289458, 398289461], '3(00)',
[368714778, 368714783], '04(010)',
[437953499, 437953504], '04(010)',
[526196233, 526196238], '032(00)',
[744719566, 744719571], '(010)40',
[750375857, 750375862], '032(00)',
[958241932, 958241937], '04(010)',
[983377342, 983377347], '(00)410',
[1003780080, 1003780085], '04(010)',
[1070232754, 1070232759], '(00)230',
[1209834865, 1209834870], '032(00)',
[1257209100, 1257209105], '(00)410',
[1368002233, 1368002238], '(00)230'
]


transforms = [
  (lambda ctx,x,c: x*c, '$y/$c', 0),
  (lambda ctx,x,c: x/c, '$c*$y', 1),
  (lambda ctx,x,c: c/x, '$c/$y', 0),
  (lambda ctx,x,c: (x*c)**2, 'sqrt($y)/$c', 0),
  (lambda ctx,x,c: (x/c)**2, '$c*sqrt($y)', 1),
  (lambda ctx,x,c: (c/x)**2, '$c/sqrt($y)', 0),
  (lambda ctx,x,c: c*x**2, 'sqrt($y)/sqrt($c)', 1),
  (lambda ctx,x,c: x**2/c, 'sqrt($c)*sqrt($y)', 1),
  (lambda ctx,x,c: c/x**2, 'sqrt($c)/sqrt($y)', 1),
  (lambda ctx,x,c: ctx.sqrt(x*c), '$y**2/$c', 0),
  (lambda ctx,x,c: ctx.sqrt(x/c), '$c*$y**2', 1),
  (lambda ctx,x,c: ctx.sqrt(c/x), '$c/$y**2', 0),
  (lambda ctx,x,c: c*ctx.sqrt(x), '$y**2/$c**2', 1),
  (lambda ctx,x,c: ctx.sqrt(x)/c, '$c**2*$y**2', 1),
  (lambda ctx,x,c: c/ctx.sqrt(x), '$c**2/$y**2', 1),
  (lambda ctx,x,c: ctx.exp(x*c), 'log($y)/$c', 0),
  (lambda ctx,x,c: ctx.exp(x/c), '$c*log($y)', 1),
  (lambda ctx,x,c: ctx.exp(c/x), '$c/log($y)', 0),
  (lambda ctx,x,c: c*ctx.exp(x), 'log($y/$c)', 1),
  (lambda ctx,x,c: ctx.exp(x)/c, 'log($c*$y)', 1),
  (lambda ctx,x,c: c/ctx.exp(x), 'log($c/$y)', 0),
  (lambda ctx,x,c: ctx.ln(x*c), 'exp($y)/$c', 0),
  (lambda ctx,x,c: ctx.ln(x/c), '$c*exp($y)', 1),
  (lambda ctx,x,c: ctx.ln(c/x), '$c/exp($y)', 0),
  (lambda ctx,x,c: c*ctx.ln(x), 'exp($y/$c)', 1),
  (lambda ctx,x,c: ctx.ln(x)/c, 'exp($c*$y)', 1),
  (lambda ctx,x,c: c/ctx.ln(x), 'exp($c/$y)', 0),
]

def binary_op(name, with_mpf='', with_int='', with_mpc=''):
    code = mpf_binary_op
    code = code.replace("%WITH_INT%", with_int)
    code = code.replace("%WITH_MPC%", with_mpc)
    code = code.replace("%WITH_MPF%", with_mpf)
    code = code.replace("%NAME%", name)
    np = {}
    exec_(code, globals(), np)
    return np[name]

MAX_BERNOULLI_CACHE = 3000
bernoulli_cache = {}
f3 = from_int(3)
f6 = from_int(6)
BERNOULLI_PREC_CUTOFF = bernoulli_size(MAX_BERNOULLI_CACHE)

def mpf_bernoulli(n, prec, rnd=None):
    if n < 2:
        if n < 0:
            raise ValueError("Bernoulli numbers only defined for n >= 0")
        if n == 0:
            return fone
        if n == 1:
            return mpf_neg(fhalf)
    if n & 1:
        return fzero
    if prec > BERNOULLI_PREC_CUTOFF and prec > bernoulli_size(n)*1.1 + 1000:
        p, q = bernfrac(n)
        return from_rational(p, q, prec, rnd or round_floor)
    if n > MAX_BERNOULLI_CACHE:
        return mpf_bernoulli_huge(n, prec, rnd)
    wp = prec + 30
    wp += 32 - (prec & 31)
    cached = bernoulli_cache.get(wp)
    if cached:
        numbers, state = cached
        if n in numbers:
            if not rnd:
                return numbers[n]
            return mpf_pos(numbers[n], prec, rnd)
        m, bin, bin1 = state
        if n - m > 10:
            return mpf_bernoulli_huge(n, prec, rnd)
    else:
        if n > 10:
            return mpf_bernoulli_huge(n, prec, rnd)
        numbers = {0:fone}
        m, bin, bin1 = state = [2, MPZ(10), MPZ_ONE]
        bernoulli_cache[wp] = (numbers, state)
    while m <= n:
        case = m % 6
        szbm = bernoulli_size(m)
        s = 0
        sexp = max(0, szbm)  - wp
        if m < 6:
            a = MPZ_ZERO
        else:
            a = bin1
        for j in range(1, m//6+1):
            usign, uman, uexp, ubc = u = numbers[m-6*j]
            if usign:
                uman = -uman
            s += lshift(a*uman, uexp-sexp)
            j6 = 6*j
            a *= ((m-5-j6)*(m-4-j6)*(m-3-j6)*(m-2-j6)*(m-1-j6)*(m-j6))
            a //= ((4+j6)*(5+j6)*(6+j6)*(7+j6)*(8+j6)*(9+j6))
        if case == 0: b = mpf_rdiv_int(m+3, f3, wp)
        if case == 2: b = mpf_rdiv_int(m+3, f3, wp)
        if case == 4: b = mpf_rdiv_int(-m-3, f6, wp)
        s = from_man_exp(s, sexp, wp)
        b = mpf_div(mpf_sub(b, s, wp), from_int(bin), wp)
        numbers[m] = b
        m += 2
        bin = bin*((m+2)*(m+3)) // (m*(m-1))
        if m > 6:
            bin1 = bin1*((2+m)*(3+m)) // ((m-7)*(m-6))
        state[:] = [m, bin, bin1]
    return numbers[n]


def mpf_harmonic(x, prec, rnd):
    if x in (fzero, fnan, finf):
        return x
    a = mpf_psi0(mpf_add(fone, x, prec+5), prec)
    return mpf_add(a, mpf_euler(prec+5, rnd), prec, rnd)

def mpc_harmonic(z, prec, rnd):
    if z[1] == fzero:
        return (mpf_harmonic(z[0], prec, rnd), fzero)
    a = mpc_psi0(mpc_add_mpf(z, fone, prec+5), prec)
    return mpc_add_mpf(a, mpf_euler(prec+5, rnd), prec, rnd)

def mpf_psi0(x, prec, rnd=round_fast):
    sign, man, exp, bc = x
    wp = prec + 10
    if not man:
        if x == finf: return x
        if x == fninf or x == fnan: return fnan
    if x == fzero or (exp >= 0 and sign):
        raise ValueError("polygamma pole")
    if sign and exp+bc > 3:
        c, s = mpf_cos_sin_pi(x, wp)
        q = mpf_mul(mpf_div(c, s, wp), mpf_pi(wp), wp)
        p = mpf_psi0(mpf_sub(fone, x, wp), wp)
        return mpf_sub(p, q, prec, rnd)
    if (not sign) and bc + exp > wp:
        return mpf_log(mpf_sub(x, fone, wp), prec, rnd)
    m = to_int(x)
    n = int(0.11*wp) + 2
    s = MPZ_ZERO
    x = to_fixed(x, wp)
    one = MPZ_ONE << wp
    if m < n:
        for k in range(m, n):
            s -= (one << wp) // x
            x += one
    x -= one
    s += to_fixed(mpf_log(from_man_exp(x, -wp, wp), wp), wp)
    s += (one << wp) // (2*x)
    x2 = (x*x) >> wp
    t = one
    prev = 0
    k = 1
    while 1:
        t = (t*x2) >> wp
        bsign, bman, bexp, bbc = mpf_bernoulli(2*k, wp)
        offset = (bexp + 2*wp)
        if offset >= 0: term = (bman << offset) // (t*(2*k))
        else:           term = (bman >> (-offset)) // (t*(2*k))
        if k & 1: s -= term
        else:     s += term
        if k > 2 and term >= prev:
            break
        prev = term
        k += 1
    return from_man_exp(s, -wp, wp, rnd)

def mpc_psi0(z, prec, rnd=round_fast):
    re, im = z
    if im == fzero:
        return (mpf_psi0(re, prec, rnd), fzero)
    wp = prec + 20
    sign, man, exp, bc = re
    if sign and exp+bc > 3:
        c = mpc_cos_pi(z, wp)
        s = mpc_sin_pi(z, wp)
        q = mpc_mul_mpf(mpc_div(c, s, wp), mpf_pi(wp), wp)
        p = mpc_psi0(mpc_sub(mpc_one, z, wp), wp)
        return mpc_sub(p, q, prec, rnd)
    if (not sign) and bc + exp > wp:
        return mpc_log(mpc_sub(z, mpc_one, wp), prec, rnd)
    w = to_int(re)
    n = int(0.11*wp) + 2
    s = mpc_zero
    if w < n:
        for k in range(w, n):
            s = mpc_sub(s, mpc_reciprocal(z, wp), wp)
            z = mpc_add_mpf(z, fone, wp)
    z = mpc_sub(z, mpc_one, wp)
    s = mpc_add(s, mpc_log(z, wp), wp)
    s = mpc_add(s, mpc_div(mpc_half, z, wp), wp)
    z2 = mpc_square(z, wp)
    t = mpc_one
    prev = mpc_zero
    k = 1
    eps = mpf_shift(fone, -wp+2)
    while 1:
        t = mpc_mul(t, z2, wp)
        bern = mpf_bernoulli(2*k, wp)
        term = mpc_mpf_div(bern, mpc_mul_int(t, 2*k, wp), wp)
        s = mpc_sub(s, term, wp)
        szterm = mpc_abs(term, 10)
        if k > 2 and mpf_le(szterm, eps):
            break
        prev = term
        k += 1
    return s

def mpf_psi(m, x, prec, rnd=round_fast):
    if m == 0:
        return mpf_psi0(x, prec, rnd=round_fast)
    return mpc_psi(m, (x, fzero), prec, rnd)[0]

def mpc_psi(m, z, prec, rnd=round_fast):
    if m == 0:
        return mpc_psi0(z, prec, rnd)
    re, im = z
    wp = prec + 20
    sign, man, exp, bc = re
    if not im[1]:
        if im in (finf, fninf, fnan):
            return (fnan, fnan)
    if not man:
        if re == finf and im == fzero:
            return (fzero, fzero)
        if re == fnan:
            return (fnan, fnan)
    w = to_int(re)
    n = int(0.4*wp + 4*m)
    s = mpc_zero
    if w < n:
        for k in range(w, n):
            t = mpc_pow_int(z, -m-1, wp)
            s = mpc_add(s, t, wp)
            z = mpc_add_mpf(z, fone, wp)
    zm = mpc_pow_int(z, -m, wp)
    z2 = mpc_pow_int(z, -2, wp)
    integral_term = mpc_div_mpf(zm, from_int(m), wp)
    s = mpc_add(s, integral_term, wp)
    s = mpc_add(s, mpc_mul_mpf(mpc_div(zm, z, wp), fhalf, wp), wp)
    a = m + 1
    b = 2
    k = 1
    magn = mpc_abs(s, 10)
    magn = magn[2]+magn[3]
    eps = mpf_shift(fone, magn-wp+2)
    while 1:
        zm = mpc_mul(zm, z2, wp)
        bern = mpf_bernoulli(2*k, wp)
        scal = mpf_mul_int(bern, a, wp)
        scal = mpf_div(scal, from_int(b), wp)
        term = mpc_mul_mpf(zm, scal, wp)
        s = mpc_add(s, term, wp)
        szterm = mpc_abs(term, 10)
        if k > 2 and mpf_le(szterm, eps):
            break
        a *= (m+2*k)*(m+2*k+1)
        b *= (2*k+1)*(2*k+2)
        k += 1
    v = mpc_mul_mpf(s, mpf_gamma(from_int(m+1), wp), prec, rnd)
    if not (m & 1):
        v = mpf_neg(v[0]), mpf_neg(v[1])
    return v


borwein_cache = {}

def borwein_coefficients(n):
    if n in borwein_cache:
        return borwein_cache[n]
    ds = [MPZ_ZERO]*(n+1)
    d = MPZ_ONE
    s = ds[0] = MPZ_ONE
    for i in range(1, n+1):
        d = d*4*(n+i-1)*(n-i+1)
        d //= ((2*i)*((2*i)-1))
        s += d
        ds[i] = s
    borwein_cache[n] = ds
    return ds

ZETA_INT_CACHE_MAX_PREC = 1000
zeta_int_cache = {}

def mpf_zeta_int(s, prec, rnd=round_fast):
    wp = prec + 20
    s = int(s)
    if s in zeta_int_cache and zeta_int_cache[s][0] >= wp:
        return mpf_pos(zeta_int_cache[s][1], prec, rnd)
    if s < 2:
        if s == 1:
            raise ValueError("zeta(1) pole")
        if not s:
            return mpf_neg(fhalf)
        return mpf_div(mpf_bernoulli(-s+1, wp), from_int(s-1), prec, rnd)
    if s >= wp:
        return mpf_perturb(fone, 0, prec, rnd)
    elif s >= wp*0.431:
        t = one = 1 << wp
        t += 1 << (wp - s)
        t += one // (MPZ_THREE ** s)
        t += 1 << max(0, wp - s*2)
        return from_man_exp(t, -wp, prec, rnd)
    else:
        m = (float(wp)/(s-1) + 1)
        if m < 30:
            needed_terms = int(2.0**m + 1)
            if needed_terms < int(wp/2.54 + 5) / 10:
                t = fone
                for k in list_primes(needed_terms):
                    powprec = int(wp - s*math.log(k,2))
                    if powprec < 2:
                        break
                    a = mpf_sub(fone, mpf_pow_int(from_int(k), -s, powprec), wp)
                    t = mpf_mul(t, a, wp)
                return mpf_div(fone, t, wp)
    n = int(wp/2.54 + 5)
    d = borwein_coefficients(n)
    t = MPZ_ZERO
    s = MPZ(s)
    for k in range(n):
        t += (((-1)**k*(d[k] - d[n])) << wp) // (k+1)**s
    t = (t << wp) // (-d[n])
    t = (t << wp) // ((1 << wp) - (1 << (wp+1-s)))
    if (s in zeta_int_cache and zeta_int_cache[s][0] < wp) or (s not in zeta_int_cache):
        zeta_int_cache[s] = (wp, from_man_exp(t, -wp-wp))
    return from_man_exp(t, -wp-wp, prec, rnd)

def mpf_zeta(s, prec, rnd=round_fast, alt=0):
    sign, man, exp, bc = s
    if not man:
        if s == fzero:
            if alt:
                return fhalf
            else:
                return mpf_neg(fhalf)
        if s == finf:
            return fone
        return fnan
    wp = prec + 20
    if (not sign) and (exp + bc > (math.log(wp,2) + 2)):
        return mpf_perturb(fone, alt, prec, rnd)
    elif exp >= 0:
        if alt:
            if s == fone:
                return mpf_ln2(prec, rnd)
            z = mpf_zeta_int(to_int(s), wp, negative_rnd[rnd])
            q = mpf_sub(fone, mpf_pow(ftwo, mpf_sub(fone, s, wp), wp), wp)
            return mpf_mul(z, q, prec, rnd)
        else:
            return mpf_zeta_int(to_int(s), prec, rnd)

    if sign:
        if alt:
            q = mpf_sub(fone, mpf_pow(ftwo, mpf_sub(fone, s, wp), wp), wp)
            return mpf_mul(mpf_zeta(s, wp), q, prec, rnd)
        y = mpf_sub(fone, s, 10*wp)
        a = mpf_gamma(y, wp)
        b = mpf_zeta(y, wp)
        c = mpf_sin_pi(mpf_shift(s, -1), wp)
        wp2 = wp + max(0,exp+bc)
        pi = mpf_pi(wp+wp2)
        d = mpf_div(mpf_pow(mpf_shift(pi, 1), s, wp2), pi, wp2)
        return mpf_mul(a,mpf_mul(b,mpf_mul(c,d,wp),wp),prec,rnd)

    r = mpf_sub(fone, s, wp)
    asign, aman, aexp, abc = mpf_abs(r)
    pole_dist = -2*(aexp+abc)
    if pole_dist > wp:
        if alt:
            return mpf_ln2(prec, rnd)
        else:
            q = mpf_neg(mpf_div(fone, r, wp))
            return mpf_add(q, mpf_euler(wp), prec, rnd)
    else:
        wp += max(0, pole_dist)

    t = MPZ_ZERO
    n = int(wp/2.54 + 5)
    d = borwein_coefficients(n)
    t = MPZ_ZERO
    sf = to_fixed(s, wp)
    ln2 = ln2_fixed(wp)
    for k in range(n):
        u = (-sf*log_int_fixed(k+1, wp, ln2)) >> wp

        eman = exp_fixed(u, wp, ln2)
        w = (d[k] - d[n])*eman
        if k & 1:
            t -= w
        else:
            t += w
    t = t // (-d[n])
    t = from_man_exp(t, -wp, wp)
    if alt:
        return mpf_pos(t, prec, rnd)
    else:
        q = mpf_sub(fone, mpf_pow(ftwo, mpf_sub(fone, s, wp), wp), wp)
        return mpf_div(t, q, prec, rnd)

def mpc_zeta(s, prec, rnd=round_fast, alt=0, force=False):
    re, im = s
    if im == fzero:
        return mpf_zeta(re, prec, rnd, alt), fzero
    if (not force) and mpf_gt(mpc_abs(s, 10), from_int(prec)):
        raise NotImplementedError
    wp = prec + 20
    r = mpc_sub(mpc_one, s, wp)
    asign, aman, aexp, abc = mpc_abs(r, 10)
    pole_dist = -2*(aexp+abc)
    if pole_dist > wp:
        if alt:
            q = mpf_ln2(wp)
            y = mpf_mul(q, mpf_euler(wp), wp)
            g = mpf_shift(mpf_mul(q, q, wp), -1)
            g = mpf_sub(y, g)
            z = mpc_mul_mpf(r, mpf_neg(g), wp)
            z = mpc_add_mpf(z, q, wp)
            return mpc_pos(z, prec, rnd)
        else:
            q = mpc_neg(mpc_div(mpc_one, r, wp))
            q = mpc_add_mpf(q, mpf_euler(wp), wp)
            return mpc_pos(q, prec, rnd)
    else:
        wp += max(0, pole_dist)

    if mpf_lt(re, fzero):
        if alt:
            q = mpc_sub(mpc_one, mpc_pow(mpc_two, mpc_sub(mpc_one, s, wp),
                wp), wp)
            return mpc_mul(mpc_zeta(s, wp), q, prec, rnd)
        y = mpc_sub(mpc_one, s, 10*wp)
        a = mpc_gamma(y, wp)
        b = mpc_zeta(y, wp)
        c = mpc_sin_pi(mpc_shift(s, -1), wp)
        rsign, rman, rexp, rbc = re
        isign, iman, iexp, ibc = im
        mag = max(rexp+rbc, iexp+ibc)
        wp2 = wp + max(0, mag)
        pi = mpf_pi(wp+wp2)
        pi2 = (mpf_shift(pi, 1), fzero)
        d = mpc_div_mpf(mpc_pow(pi2, s, wp2), pi, wp2)
        return mpc_mul(a,mpc_mul(b,mpc_mul(c,d,wp),wp),prec,rnd)
    n = int(wp/2.54 + 5)
    n += int(0.9*abs(to_int(im)))
    d = borwein_coefficients(n)
    ref = to_fixed(re, wp)
    imf = to_fixed(im, wp)
    tre = MPZ_ZERO
    tim = MPZ_ZERO
    one = MPZ_ONE << wp
    one_2wp = MPZ_ONE << (2*wp)
    critical_line = re == fhalf
    ln2 = ln2_fixed(wp)
    pi2 = pi_fixed(wp-1)
    wp2 = wp+wp
    for k in range(n):
        log = log_int_fixed(k+1, wp, ln2)
        if critical_line:
            w = one_2wp // isqrt_fast((k+1) << wp2)
        else:
            w = exp_fixed((-ref*log) >> wp, wp)
        if k & 1:
            w *= (d[n] - d[k])
        else:
            w *= (d[k] - d[n])
        wre, wim = cos_sin_fixed((-imf*log)>>wp, wp, pi2)
        tre += (w*wre) >> wp
        tim += (w*wim) >> wp
    tre //= (-d[n])
    tim //= (-d[n])
    tre = from_man_exp(tre, -wp, wp)
    tim = from_man_exp(tim, -wp, wp)
    if alt:
        return mpc_pos((tre, tim), prec, rnd)
    else:
        q = mpc_sub(mpc_one, mpc_pow(mpc_two, r, wp), wp)
        return mpc_div((tre, tim), q, prec, rnd)

def mpf_altzeta(s, prec, rnd=round_fast):
    return mpf_zeta(s, prec, rnd, 1)

def mpc_altzeta(s, prec, rnd=round_fast):
    return mpc_zeta(s, prec, rnd, 1)

mpf_zetasum = None

def pow_fixed(x, n, wp):
    if n == 1:
        return x
    y = MPZ_ONE << wp
    while n:
        if n & 1:
            y = (y*x) >> wp
            n -= 1
        x = (x*x) >> wp
        n //= 2
    return y

sieve_cache = []
primes_cache = []
mult_cache = []

def primesieve(n):
    global sieve_cache, primes_cache, mult_cache
    if n < len(sieve_cache):
        sieve = sieve_cache#[:n+1]
        primes = primes_cache[:primes_cache.index(max(sieve))+1]
        mult = mult_cache#[:n+1]
        return sieve, primes, mult
    sieve = [0]*(n+1)
    mult = [0]*(n+1)
    primes = list_primes(n)
    for p in primes:
        for k in range(p,n+1,p):
            sieve[k] = p
    for i, p in enumerate(sieve):
        if i >= 2:
            m = 1
            n = i // p
            while not n % p:
                n //= p
                m += 1
            mult[i] = m
    sieve_cache = sieve
    primes_cache = primes
    mult_cache = mult
    return sieve, primes, mult

def zetasum_sieved(critical_line, sre, sim, a, n, wp):
    if a < 1:
        raise ValueError("a cannot be less than 1")
    sieve, primes, mult = primesieve(a+n)
    basic_powers = {}
    one = MPZ_ONE << wp
    one_2wp = MPZ_ONE << (2*wp)
    wp2 = wp+wp
    ln2 = ln2_fixed(wp)
    pi2 = pi_fixed(wp-1)
    for p in primes:
        if p*2 > a+n:
            break
        log = log_int_fixed(p, wp, ln2)
        cos, sin = cos_sin_fixed((-sim*log)>>wp, wp, pi2)
        if critical_line:
            u = one_2wp // isqrt_fast(p<<wp2)
        else:
            u = exp_fixed((-sre*log)>>wp, wp)
        pre = (u*cos) >> wp
        pim = (u*sin) >> wp
        basic_powers[p] = [(pre, pim)]
        tre, tim = pre, pim
        for m in range(1,int(math.log(a+n,p)+0.01)+1):
            tre, tim = ((pre*tre-pim*tim)>>wp), ((pim*tre+pre*tim)>>wp)
            basic_powers[p].append((tre,tim))
    xre = MPZ_ZERO
    xim = MPZ_ZERO
    if a == 1:
        xre += one
    aa = max(a,2)
    for k in range(aa, a+n+1):
        p = sieve[k]
        if p in basic_powers:
            m = mult[k]
            tre, tim = basic_powers[p][m-1]
            while 1:
                k //= p**m
                if k == 1:
                    break
                p = sieve[k]
                m = mult[k]
                pre, pim = basic_powers[p][m-1]
                tre, tim = ((pre*tre-pim*tim)>>wp), ((pim*tre+pre*tim)>>wp)
        else:
            log = log_int_fixed(k, wp, ln2)
            cos, sin = cos_sin_fixed((-sim*log)>>wp, wp, pi2)
            if critical_line:
                u = one_2wp // isqrt_fast(k<<wp2)
            else:
                u = exp_fixed((-sre*log)>>wp, wp)
            tre = (u*cos) >> wp
            tim = (u*sin) >> wp
        xre += tre
        xim += tim
    return xre, xim

ZETASUM_SIEVE_CUTOFF = 10

def mpc_zetasum(s, a, n, derivatives, reflect, prec):
    wp = prec + 10
    derivatives = list(derivatives)
    have_derivatives = derivatives != [0]
    have_one_derivative = len(derivatives) == 1
    sre, sim = s
    critical_line = (sre == fhalf)
    sre = to_fixed(sre, wp)
    sim = to_fixed(sim, wp)
    if a > 0 and n > ZETASUM_SIEVE_CUTOFF and not have_derivatives \
            and not reflect and (n < 4e7 or sys.maxsize > 2**32):
        re, im = zetasum_sieved(critical_line, sre, sim, a, n, wp)
        xs = [(from_man_exp(re, -wp, prec, 'n'), from_man_exp(im, -wp, prec, 'n'))]
        return xs, []
    maxd = max(derivatives)
    if not have_one_derivative:
        derivatives = range(maxd+1)
    xre = [MPZ_ZERO for d in derivatives]
    xim = [MPZ_ZERO for d in derivatives]
    if reflect:
        yre = [MPZ_ZERO for d in derivatives]
        yim = [MPZ_ZERO for d in derivatives]
    else:
        yre = yim = []
    one = MPZ_ONE << wp
    one_2wp = MPZ_ONE << (2*wp)
    ln2 = ln2_fixed(wp)
    pi2 = pi_fixed(wp-1)
    wp2 = wp+wp
    for w in range(a, a+n+1):
        log = log_int_fixed(w, wp, ln2)
        cos, sin = cos_sin_fixed((-sim*log)>>wp, wp, pi2)
        if critical_line:
            u = one_2wp // isqrt_fast(w<<wp2)
        else:
            u = exp_fixed((-sre*log)>>wp, wp)
        xterm_re = (u*cos) >> wp
        xterm_im = (u*sin) >> wp
        if reflect:
            reciprocal = (one_2wp // (u*w))
            yterm_re = (reciprocal*cos) >> wp
            yterm_im = (reciprocal*sin) >> wp
        if have_derivatives:
            if have_one_derivative:
                log = pow_fixed(log, maxd, wp)
                xre[0] += (xterm_re*log) >> wp
                xim[0] += (xterm_im*log) >> wp
                if reflect:
                    yre[0] += (yterm_re*log) >> wp
                    yim[0] += (yterm_im*log) >> wp
            else:
                t = MPZ_ONE << wp
                for d in derivatives:
                    xre[d] += (xterm_re*t) >> wp
                    xim[d] += (xterm_im*t) >> wp
                    if reflect:
                        yre[d] += (yterm_re*t) >> wp
                        yim[d] += (yterm_im*t) >> wp
                    t = (t*log) >> wp
        else:
            xre[0] += xterm_re
            xim[0] += xterm_im
            if reflect:
                yre[0] += yterm_re
                yim[0] += yterm_im
    if have_derivatives:
        if have_one_derivative:
            if maxd % 2:
                xre[0] = -xre[0]
                xim[0] = -xim[0]
                if reflect:
                    yre[0] = -yre[0]
                    yim[0] = -yim[0]
        else:
            xre = [(-1)**d*xre[d] for d in derivatives]
            xim = [(-1)**d*xim[d] for d in derivatives]
            if reflect:
                yre = [(-1)**d*yre[d] for d in derivatives]
                yim = [(-1)**d*yim[d] for d in derivatives]
    xs = [(from_man_exp(xa, -wp, prec, 'n'), from_man_exp(xb, -wp, prec, 'n'))
        for (xa, xb) in zip(xre, xim)]
    ys = [(from_man_exp(ya, -wp, prec, 'n'), from_man_exp(yb, -wp, prec, 'n'))
        for (ya, yb) in zip(yre, yim)]
    return xs, ys

MAX_GAMMA_TAYLOR_PREC = 5000
assert MAX_GAMMA_TAYLOR_PREC < 15000
GAMMA_STIRLING_BETA = 0.2
SMALL_FACTORIAL_CACHE_SIZE = 15
gamma_taylor_cache = {}
gamma_stirling_cache = {}
small_factorial_cache = [from_int(ifac(n)) for n in range(SMALL_FACTORIAL_CACHE_SIZE+1)]

def zeta_array(N, prec):
    extra = 30
    wp = prec+extra
    zeta_values = [MPZ_ZERO]*(N+2)
    pi = pi_fixed(wp)
    one = MPZ_ONE << wp
    zeta_values[0] = -one//2
    f_2pi = mpf_shift(mpf_pi(wp),1)
    exp_2pi_k = exp_2pi = mpf_exp(f_2pi, wp)
    exps3 = []
    k = 1
    while 1:
        tp = wp - 9*k
        if tp < 1:
            break
        q1 = mpf_div(fone, mpf_sub(exp_2pi_k, fone, tp), tp)
        q2 = mpf_mul(exp_2pi_k, mpf_mul(q1,q1,tp), tp)
        q1 = to_fixed(q1, wp)
        q2 = to_fixed(q2, wp)
        q2 = (k*q2*pi) >> wp
        exps3.append((q1, q2))
        exp_2pi_k = mpf_mul(exp_2pi_k, exp_2pi, wp)
        k += 1
    for n in range(3, N+1, 2):
        s = MPZ_ZERO
        k = 1
        for e1, e2 in exps3:
            if n%4 == 3:
                t = e1 // k**n
            else:
                U = (n-1)//4
                t = (e1 + e2//U) // k**n
            if not t:
                break
            s += t
            k += 1
        zeta_values[n] = -2*s
    B = [mpf_abs(mpf_bernoulli(k,wp)) for k in range(N+2)]
    pi_pow = fpi = mpf_pow_int(mpf_shift(mpf_pi(wp), 1), 2, wp)
    pi_pow = mpf_div(pi_pow, from_int(4), wp)
    for n in range(2,N+2,2):
        z = mpf_mul(B[n], pi_pow, wp)
        zeta_values[n] = to_fixed(z, wp)
        pi_pow = mpf_mul(pi_pow, fpi, wp)
        pi_pow = mpf_div(pi_pow, from_int((n+1)*(n+2)), wp)
    reciprocal_pi = (one << wp) // pi
    for n in range(3, N+1, 4):
        U = (n-3)//4
        s = zeta_values[4*U+4]*(4*U+7)//4
        for k in range(1, U+1):
            s -= (zeta_values[4*k]*zeta_values[4*U+4-4*k]) >> wp
        zeta_values[n] += (2*s*reciprocal_pi) >> wp
    for n in range(5, N+1, 4):
        U = (n-1)//4
        s = zeta_values[4*U+2]*(2*U+1)
        for k in range(1, 2*U+1):
            s += ((-1)**k*2*k* zeta_values[2*k]*zeta_values[4*U+2-2*k])>>wp
        zeta_values[n] += ((s*reciprocal_pi)>>wp)//(2*U)
    return [x>>extra for x in zeta_values]

def gamma_taylor_coefficients(inprec):
    if inprec < 400:
        prec = inprec + (10-(inprec%10))
    elif inprec < 1000:
        prec = inprec + (30-(inprec%30))
    else:
        prec = inprec
    if prec in gamma_taylor_cache:
        return gamma_taylor_cache[prec], prec
    if prec < 1000:
        N = int(prec**0.76 + 2)
    else:
        N = int(prec**0.787 + 2)
    for cprec in gamma_taylor_cache:
        if cprec > prec:
            coeffs = [x>>(cprec-prec) for x in gamma_taylor_cache[cprec][-N:]]
            if inprec < 1000:
                gamma_taylor_cache[prec] = coeffs
            return coeffs, prec
    if prec > 1000:
        prec = int(prec*1.2)
    wp = prec + 20
    A = [0]*N
    A[0] = MPZ_ZERO
    A[1] = MPZ_ONE << wp
    A[2] = euler_fixed(wp)
    zeta_values = zeta_array(N, wp)
    for k in range(3, N):
        a = (-A[2]*A[k-1])>>wp
        for j in range(2,k):
            a += ((-1)**j*zeta_values[j]*A[k-j]) >> wp
        a //= (1-k)
        A[k] = a
    A = [a>>20 for a in A]
    A = A[::-1]
    A = A[:-1]
    gamma_taylor_cache[prec] = A
    return gamma_taylor_coefficients(inprec)

def gamma_fixed_taylor(xmpf, x, wp, prec, rnd, type):
    nearest_int = ((x >> (wp-1)) + MPZ_ONE) >> 1
    one = MPZ_ONE << wp
    coeffs, cwp = gamma_taylor_coefficients(wp)
    if nearest_int > 0:
        r = one
        for i in range(nearest_int-1):
            x -= one
            r = (r*x) >> wp
        x -= one
        p = MPZ_ZERO
        for c in coeffs:
            p = c + ((x*p)>>wp)
        p >>= (cwp-wp)
        if type == 0:
            return from_man_exp((r<<wp)//p, -wp, prec, rnd)
        if type == 2:
            return mpf_shift(from_rational(p, (r<<wp), prec, rnd), wp)
        if type == 3:
            return mpf_log(mpf_abs(from_man_exp((r<<wp)//p, -wp)), prec, rnd)
    else:
        r = one
        for i in range(-nearest_int):
            r = (r*x) >> wp
            x += one
        p = MPZ_ZERO
        for c in coeffs:
            p = c + ((x*p)>>wp)
        p >>= (cwp-wp)
        if wp - bitcount(abs(x)) > 10:
            g = mpf_add(xmpf, from_int(-nearest_int))  # exact
            r = from_man_exp(p*r,-wp-wp)
            r = mpf_mul(r, g, wp)
            if type == 0:
                return mpf_div(fone, r, prec, rnd)
            if type == 2:
                return mpf_pos(r, prec, rnd)
            if type == 3:
                return mpf_log(mpf_abs(mpf_div(fone, r, wp)), prec, rnd)
        else:
            r = from_man_exp(x*p*r,-3*wp)
            if type == 0: return mpf_div(fone, r, prec, rnd)
            if type == 2: return mpf_pos(r, prec, rnd)
            if type == 3: return mpf_neg(mpf_log(mpf_abs(r), prec, rnd))

def stirling_coefficient(n):
    if n in gamma_stirling_cache:
        return gamma_stirling_cache[n]
    p, q = bernfrac(n)
    q *= MPZ(n*(n-1))
    gamma_stirling_cache[n] = p, q, bitcount(abs(p)), bitcount(q)
    return gamma_stirling_cache[n]

def real_stirling_series(x, prec):
    t = (MPZ_ONE<<(prec+prec)) // x   # t = 1/x
    u = (t*t)>>prec                  # u = 1/x**2
    s = ln_sqrt2pi_fixed(prec) - x
    s += t//12;            t = (t*u)>>prec
    s -= t//360;           t = (t*u)>>prec
    s += t//1260;          t = (t*u)>>prec
    s -= t//1680;          t = (t*u)>>prec
    if not t: return s
    s += t//1188;          t = (t*u)>>prec
    s -= 691*t//360360;    t = (t*u)>>prec
    s += t//156;           t = (t*u)>>prec
    if not t: return s
    s -= 3617*t//122400;   t = (t*u)>>prec
    s += 43867*t//244188;  t = (t*u)>>prec
    s -= 174611*t//125400;  t = (t*u)>>prec
    if not t: return s
    k = 22
    usize = bitcount(abs(u))
    tsize = bitcount(abs(t))
    texp = 0
    while 1:
        p, q, pb, qb = stirling_coefficient(k)
        term_mag = tsize + pb + texp
        shift = -texp
        m = pb - term_mag
        if m > 0 and shift < m:
            p >>= m
            shift -= m
        m = tsize - term_mag
        if m > 0 and shift < m:
            w = t >> m
            shift -= m
        else:
            w = t
        term = (t*p//q) >> shift
        if not term:
            break
        s += term
        t = (t*u) >> usize
        texp -= (prec - usize)
        k += 2
    return s

def complex_stirling_series(x, y, prec):
    _m = (x*x + y*y) >> prec
    tre = (x << prec) // _m
    tim = (-y << prec) // _m
    ure = (tre*tre - tim*tim) >> prec
    uim = tim*tre >> (prec-1)
    sre = ln_sqrt2pi_fixed(prec) - x
    sim = -y
    sre += tre//12; sim += tim//12;
    tre, tim = ((tre*ure-tim*uim)>>prec), ((tre*uim+tim*ure)>>prec)
    sre -= tre//360; sim -= tim//360;
    tre, tim = ((tre*ure-tim*uim)>>prec), ((tre*uim+tim*ure)>>prec)
    sre += tre//1260; sim += tim//1260;
    tre, tim = ((tre*ure-tim*uim)>>prec), ((tre*uim+tim*ure)>>prec)
    sre -= tre//1680; sim -= tim//1680;
    tre, tim = ((tre*ure-tim*uim)>>prec), ((tre*uim+tim*ure)>>prec)
    if abs(tre) + abs(tim) < 5: return sre, sim
    sre += tre//1188; sim += tim//1188;
    tre, tim = ((tre*ure-tim*uim)>>prec), ((tre*uim+tim*ure)>>prec)
    sre -= 691*tre//360360; sim -= 691*tim//360360;
    tre, tim = ((tre*ure-tim*uim)>>prec), ((tre*uim+tim*ure)>>prec)
    sre += tre//156; sim += tim//156;
    tre, tim = ((tre*ure-tim*uim)>>prec), ((tre*uim+tim*ure)>>prec)
    if abs(tre) + abs(tim) < 5: return sre, sim
    sre -= 3617*tre//122400; sim -= 3617*tim//122400;
    tre, tim = ((tre*ure-tim*uim)>>prec), ((tre*uim+tim*ure)>>prec)
    sre += 43867*tre//244188; sim += 43867*tim//244188;
    tre, tim = ((tre*ure-tim*uim)>>prec), ((tre*uim+tim*ure)>>prec)
    sre -= 174611*tre//125400; sim -= 174611*tim//125400;
    tre, tim = ((tre*ure-tim*uim)>>prec), ((tre*uim+tim*ure)>>prec)
    if abs(tre) + abs(tim) < 5: return sre, sim
    k = 22
    usize = bitcount(max(abs(ure), abs(uim)))
    tsize = bitcount(max(abs(tre), abs(tim)))
    texp = 0
    while 1:
        p, q, pb, qb = stirling_coefficient(k)
        term_mag = tsize + pb + texp
        shift = -texp
        m = pb - term_mag
        if m > 0 and shift < m:
            p >>= m
            shift -= m
        m = tsize - term_mag
        if m > 0 and shift < m:
            wre = tre >> m
            wim = tim >> m
            shift -= m
        else:
            wre = tre
            wim = tim
        termre = (tre*p//q) >> shift
        termim = (tim*p//q) >> shift
        if abs(termre) + abs(termim) < 5:
            break
        sre += termre
        sim += termim
        tre, tim = ((tre*ure - tim*uim)>>usize), \
            ((tre*uim + tim*ure)>>usize)
        texp -= (prec - usize)
        k += 2
    return sre, sim


def mpf_gamma(x, prec, rnd='d', type=0):
    sign, man, exp, bc = x
    if not man:
        if x == fzero:
            if type == 1: return fone
            if type == 2: return fzero
            raise ValueError("gamma function pole")
        if x == finf:
            if type == 2: return fzero
            return finf
        return fnan
    if type == 3:
        wp = prec+20
        if exp+bc > wp and not sign:
            return mpf_sub(mpf_mul(x, mpf_log(x, wp), wp), x, prec, rnd)
    is_integer = exp >= 0
    if is_integer:
        if sign:
            if type == 2:
                return fzero
            raise ValueError("gamma function pole")
        n = man << exp
        if n < SMALL_FACTORIAL_CACHE_SIZE:
            if type == 0:
                return mpf_pos(small_factorial_cache[n-1], prec, rnd)
            if type == 1:
                return mpf_pos(small_factorial_cache[n], prec, rnd)
            if type == 2:
                return mpf_div(fone, small_factorial_cache[n-1], prec, rnd)
            if type == 3:
                return mpf_log(small_factorial_cache[n-1], prec, rnd)
    else:
        n = int(man >> (-exp))
    mag = exp + bc
    gamma_size = n*mag
    if type == 3:
        wp = prec + 20
    else:
        wp = prec + bitcount(gamma_size) + 20
    if mag < -wp:
        if type == 0:
            return mpf_sub(mpf_div(fone,x, wp),mpf_shift(fone,-wp),prec,rnd)
        if type == 1: return mpf_sub(fone, x, prec, rnd)
        if type == 2: return mpf_add(x, mpf_shift(fone,mag-wp), prec, rnd)
        if type == 3: return mpf_neg(mpf_log(mpf_abs(x), prec, rnd))
    if type == 1:
        return mpf_gamma(mpf_add(x, fone), prec, rnd, 0)
    if exp >= -1:
        if is_integer:
            if gamma_size < 10*wp:
                if type == 0:
                    return from_int(ifac(n-1), prec, rnd)
                if type == 2:
                    return from_rational(MPZ_ONE, ifac(n-1), prec, rnd)
                if type == 3:
                    return mpf_log(from_int(ifac(n-1)), prec, rnd)
        if n < 100 or gamma_size < 10*wp:
            if sign:
                w = sqrtpi_fixed(wp)
                if n % 2: f = ifac2(2*n+1)
                else:     f = -ifac2(2*n+1)
                if type == 0:
                    return mpf_shift(from_rational(w, f, prec, rnd), -wp+n+1)
                if type == 2:
                    return mpf_shift(from_rational(f, w, prec, rnd), wp-n-1)
                if type == 3:
                    return mpf_log(mpf_shift(from_rational(w, abs(f),
                        prec, rnd), -wp+n+1), prec, rnd)
            elif n == 0:
                if type == 0: return mpf_sqrtpi(prec, rnd)
                if type == 2: return mpf_div(fone, mpf_sqrtpi(wp), prec, rnd)
                if type == 3: return mpf_log(mpf_sqrtpi(wp), prec, rnd)
            else:
                w = sqrtpi_fixed(wp)
                w = from_man_exp(w*ifac2(2*n-1), -wp-n)
                if type == 0: return mpf_pos(w, prec, rnd)
                if type == 2: return mpf_div(fone, w, prec, rnd)
                if type == 3: return mpf_log(mpf_abs(w), prec, rnd)
    offset = exp + wp
    if offset >= 0: absxman = man << offset
    else:           absxman = man >> (-offset)
    if type == 3 and not sign:
        one = MPZ_ONE << wp
        one_dist = abs(absxman-one)
        two_dist = abs(absxman-2*one)
        cancellation = (wp - bitcount(min(one_dist, two_dist)))
        if cancellation > 10:
            xsub1 = mpf_sub(fone, x)
            xsub2 = mpf_sub(ftwo, x)
            xsub1mag = xsub1[2]+xsub1[3]
            xsub2mag = xsub2[2]+xsub2[3]
            if xsub1mag < -wp:
                return mpf_mul(mpf_euler(wp), mpf_sub(fone, x), prec, rnd)
            if xsub2mag < -wp:
                return mpf_mul(mpf_sub(fone, mpf_euler(wp)),
                    mpf_sub(x, ftwo), prec, rnd)
            wp += max(-xsub1mag, -xsub2mag)
            offset = exp + wp
            if offset >= 0: absxman = man << offset
            else:           absxman = man >> (-offset)
    n_for_stirling = int(GAMMA_STIRLING_BETA*wp)
    if n < max(100, n_for_stirling) and wp < MAX_GAMMA_TAYLOR_PREC:
        if sign:
            absxman = -absxman
        return gamma_fixed_taylor(x, absxman, wp, prec, rnd, type)
    xorig = x
    r = 0
    if n < n_for_stirling:
        r = one = MPZ_ONE << wp
        d = n_for_stirling - n
        for k in range(d):
            r = (r*absxman) >> wp
            absxman += one
        x = xabs = from_man_exp(absxman, -wp)
        if sign:
            x = mpf_neg(x)
    else:
        xabs = mpf_abs(x)
    y = real_stirling_series(absxman, wp)
    u = to_fixed(mpf_log(xabs, wp), wp)
    u = ((absxman - (MPZ_ONE<<(wp-1)))*u) >> wp
    y += u
    w = from_man_exp(y, -wp)
    if sign:
        A = mpf_mul(mpf_sin_pi(xorig, wp), xorig, wp)
        B = mpf_neg(mpf_pi(wp))
        if type == 0 or type == 2:
            A = mpf_mul(A, mpf_exp(w, wp))
            if r:
                B = mpf_mul(B, from_man_exp(r, -wp), wp)
            if type == 0:
                return mpf_div(B, A, prec, rnd)
            if type == 2:
                return mpf_div(A, B, prec, rnd)
        if type == 3:
            if r:
                B = mpf_mul(B, from_man_exp(r, -wp), wp)
            A = mpf_add(mpf_log(mpf_abs(A), wp), w, wp)
            return mpf_sub(mpf_log(mpf_abs(B), wp), A, prec, rnd)
    else:
        if type == 0:
            if r:
                return mpf_div(mpf_exp(w, wp),
                    from_man_exp(r, -wp), prec, rnd)
            return mpf_exp(w, prec, rnd)
        if type == 2:
            if r:
                return mpf_div(from_man_exp(r, -wp),
                    mpf_exp(w, wp), prec, rnd)
            return mpf_exp(mpf_neg(w), prec, rnd)
        if type == 3:
            if r:
                return mpf_sub(w, mpf_log(from_man_exp(r,-wp), wp), prec, rnd)
            return mpf_pos(w, prec, rnd)

def mpc_gamma(z, prec, rnd='d', type=0):
    a, b = z
    asign, aman, aexp, abc = a
    bsign, bman, bexp, bbc = b
    if b == fzero:
        if type == 3 and asign:
            re = mpf_gamma(a, prec, rnd, 3)
            n = (-aman) >> (-aexp)
            im = mpf_mul_int(mpf_pi(prec+10), n, prec, rnd)
            return re, im
        return mpf_gamma(a, prec, rnd, type), fzero
    if (not aman and aexp) or (not bman and bexp):
        return (fnan, fnan)
    wp = prec + 20
    amag = aexp+abc
    bmag = bexp+bbc
    if aman:
        mag = max(amag, bmag)
    else:
        mag = bmag
    if mag < -8:
        if mag < -wp:
            v = mpc_add(z, mpc_mul_mpf(mpc_mul(z,z,wp),mpf_euler(wp),wp), wp)
            if type == 0: return mpc_reciprocal(v, prec, rnd)
            if type == 1: return mpc_div(z, v, prec, rnd)
            if type == 2: return mpc_pos(v, prec, rnd)
            if type == 3: return mpc_log(mpc_reciprocal(v, prec), prec, rnd)
        elif type != 1:
            wp += (-mag)
    if type == 3 and mag > wp and ((not asign) or (bmag >= amag)):
        return mpc_sub(mpc_mul(z, mpc_log(z, wp), wp), z, prec, rnd)
    if type == 1:
        return mpc_gamma((mpf_add(a, fone), b), prec, rnd, 0)
    an = abs(to_int(a))
    bn = abs(to_int(b))
    absn = max(an, bn)
    gamma_size = absn*mag
    if type == 3:
        pass
    else:
        wp += bitcount(gamma_size)
    need_reflection = asign
    zorig = z
    if need_reflection:
        z = mpc_neg(z)
        asign, aman, aexp, abc = a = z[0]
        bsign, bman, bexp, bbc = b = z[1]
    yfinal = 0
    balance_prec = 0
    if bmag < -10:
        if type == 3:
            zsub1 = mpc_sub_mpf(z, fone)
            if zsub1[0] == fzero:
                cancel1 = -bmag
            else:
                cancel1 = -max(zsub1[0][2]+zsub1[0][3], bmag)
            if cancel1 > wp:
                pi = mpf_pi(wp)
                x = mpc_mul_mpf(zsub1, pi, wp)
                x = mpc_mul(x, x, wp)
                x = mpc_div_mpf(x, from_int(12), wp)
                y = mpc_mul_mpf(zsub1, mpf_neg(mpf_euler(wp)), wp)
                yfinal = mpc_add(x, y, wp)
                if not need_reflection:
                    return mpc_pos(yfinal, prec, rnd)
            elif cancel1 > 0:
                wp += cancel1
            zsub2 = mpc_sub_mpf(z, ftwo)
            if zsub2[0] == fzero:
                cancel2 = -bmag
            else:
                cancel2 = -max(zsub2[0][2]+zsub2[0][3], bmag)
            if cancel2 > wp:
                pi = mpf_pi(wp)
                t = mpf_sub(mpf_mul(pi, pi), from_int(6))
                x = mpc_mul_mpf(mpc_mul(zsub2, zsub2, wp), t, wp)
                x = mpc_div_mpf(x, from_int(12), wp)
                y = mpc_mul_mpf(zsub2, mpf_sub(fone, mpf_euler(wp)), wp)
                yfinal = mpc_add(x, y, wp)
                if not need_reflection:
                    return mpc_pos(yfinal, prec, rnd)
            elif cancel2 > 0:
                wp += cancel2
        if bmag < -wp:
            pp = 2*(wp+10)
            aabs = mpf_abs(a)
            eps = mpf_shift(fone, amag-wp)
            x1 = mpf_gamma(aabs, pp, type=type)
            x2 = mpf_gamma(mpf_add(aabs, eps), pp, type=type)
            xprime = mpf_div(mpf_sub(x2, x1, pp), eps, pp)
            y = mpf_mul(b, xprime, prec, rnd)
            yfinal = (x1, y)
            if not need_reflection:
                return mpc_pos(yfinal, prec, rnd)
        else:
            balance_prec += (-bmag)

    wp += balance_prec
    n_for_stirling = int(GAMMA_STIRLING_BETA*wp)
    need_reduction = absn < n_for_stirling

    afix = to_fixed(a, wp)
    bfix = to_fixed(b, wp)

    r = 0
    if not yfinal:
        zprered = z
        # Argument reduction
        if absn < n_for_stirling:
            absn = complex(an, bn)
            d = int((1 + n_for_stirling**2 - bn**2)**0.5 - an)
            rre = one = MPZ_ONE << wp
            rim = MPZ_ZERO
            for k in range(d):
                rre, rim = ((afix*rre-bfix*rim)>>wp), ((afix*rim + bfix*rre)>>wp)
                afix += one
            r = from_man_exp(rre, -wp), from_man_exp(rim, -wp)
            a = from_man_exp(afix, -wp)
            z = a, b

        yre, yim = complex_stirling_series(afix, bfix, wp)
        # (z-1/2)*log(z) + S
        lre, lim = mpc_log(z, wp)
        lre = to_fixed(lre, wp)
        lim = to_fixed(lim, wp)
        yre = ((lre*afix - lim*bfix)>>wp) - (lre>>1) + yre
        yim = ((lre*bfix + lim*afix)>>wp) - (lim>>1) + yim
        y = from_man_exp(yre, -wp), from_man_exp(yim, -wp)

        if r and type == 3:

            y = mpc_sub(y, mpc_log(r, wp), wp)
            zfa = to_float(zprered[0])
            zfb = to_float(zprered[1])
            zfabs = math.hypot(zfa,zfb)
            #if not (zfa > 0.0 and zfabs <= 4):
            yfb = to_float(y[1])
            u = math.atan2(zfb, zfa)
            if zfabs <= 0.5:
                gi = 0.577216*zfb - u
            else:
                gi = -zfb - 0.5*u + zfa*u + zfb*math.log(zfabs)
            n = int(math.floor((gi-yfb)/(2*math.pi)+0.5))
            y = (y[0], mpf_add(y[1], mpf_mul_int(mpf_pi(wp), 2*n, wp), wp))

    if need_reflection:
        if type == 0 or type == 2:
            A = mpc_mul(mpc_sin_pi(zorig, wp), zorig, wp)
            B = (mpf_neg(mpf_pi(wp)), fzero)
            if yfinal:
                if type == 2:
                    A = mpc_div(A, yfinal, wp)
                else:
                    A = mpc_mul(A, yfinal, wp)
            else:
                A = mpc_mul(A, mpc_exp(y, wp), wp)
            if r:
                B = mpc_mul(B, r, wp)
            if type == 0: return mpc_div(B, A, prec, rnd)
            if type == 2: return mpc_div(A, B, prec, rnd)


        if type == 3:
            if yfinal:
                s1 = mpc_neg(yfinal)
            else:
                s1 = mpc_neg(y)
            s1 = mpc_sub(s1, mpc_log(mpc_neg(zorig), wp), wp)
            rezfloor = mpf_floor(zorig[0])
            imzsign = mpf_sign(zorig[1])
            pi = mpf_pi(wp)
            t = mpf_mul(pi, rezfloor)
            t = mpf_mul_int(t, imzsign, wp)
            s1 = (s1[0], mpf_add(s1[1], t, wp))
            s1 = mpc_add_mpf(s1, mpf_log(pi, wp), wp)
            t = mpc_sin_pi(mpc_sub_mpf(zorig, rezfloor), wp)
            t = mpc_log(t, wp)
            s1 = mpc_sub(s1, t, wp)
            if not imzsign:
                t = mpf_mul(pi, mpf_floor(rezfloor), wp)
                s1 = (s1[0], mpf_sub(s1[1], t, wp))
            return mpc_pos(s1, prec, rnd)
    else:
        if type == 0:
            if r:
                return mpc_div(mpc_exp(y, wp), r, prec, rnd)
            return mpc_exp(y, prec, rnd)
        if type == 2:
            if r:
                return mpc_div(r, mpc_exp(y, wp), prec, rnd)
            return mpc_exp(mpc_neg(y), prec, rnd)
        if type == 3:
            return mpc_pos(y, prec, rnd)

def mpf_factorial(x, prec, rnd='d'):
    return mpf_gamma(x, prec, rnd, 1)

def mpc_factorial(x, prec, rnd='d'):
    return mpc_gamma(x, prec, rnd, 1)

def mpf_rgamma(x, prec, rnd='d'):
    return mpf_gamma(x, prec, rnd, 2)

def mpc_rgamma(x, prec, rnd='d'):
    return mpc_gamma(x, prec, rnd, 2)

def mpf_loggamma(x, prec, rnd='d'):
    sign, man, exp, bc = x
    if sign:
        raise ComplexResult
    return mpf_gamma(x, prec, rnd, 3)

def mpc_loggamma(z, prec, rnd='d'):
    a, b = z
    asign, aman, aexp, abc = a
    bsign, bman, bexp, bbc = b
    if b == fzero and asign:
        re = mpf_gamma(a, prec, rnd, 3)
        n = (-aman) >> (-aexp)
        im = mpf_mul_int(mpf_pi(prec+10), n, prec, rnd)
        return re, im
    return mpc_gamma(z, prec, rnd, 3)

def mpf_gamma_int(n, prec, rnd=round_fast):
    if n < SMALL_FACTORIAL_CACHE_SIZE:
        return mpf_pos(small_factorial_cache[n-1], prec, rnd)
    return mpf_gamma(from_int(n), prec, rnd)

mpf_euler = def_mpf_constant(euler_fixed)
mpf_apery = def_mpf_constant(apery_fixed)
mpf_khinchin = def_mpf_constant(khinchin_fixed)
mpf_glaisher = def_mpf_constant(glaisher_fixed)
mpf_catalan = def_mpf_constant(catalan_fixed)
mpf_mertens = def_mpf_constant(mertens_fixed)

def bsp_acot(q, a, b, hyperbolic):
    if b - a == 1:
        a1 = MPZ(2*a + 3)
        if hyperbolic or a&1:
            return MPZ_ONE, a1*q**2, a1
        else:
            return -MPZ_ONE, a1*q**2, a1
    m = (a+b)//2
    p1, q1, r1 = bsp_acot(q, a, m, hyperbolic)
    p2, q2, r2 = bsp_acot(q, m, b, hyperbolic)
    return q2*p1 + r1*p2, q1*q2, r1*r2

def acot_fixed(a, prec, hyperbolic):

    N = int(0.35*prec/math.log(a) + 20)
    p, q, r = bsp_acot(a, 0,N, hyperbolic)
    return ((p+q)<<prec)//(q*a)

def machin(coefs, prec, hyperbolic=False):
    extraprec = 10
    s = MPZ_ZERO
    for a, b in coefs:
        s += MPZ(a)*acot_fixed(MPZ(b), prec+extraprec, hyperbolic)
    return (s >> extraprec)

@constant_memo
def ln2_fixed(prec):
    return machin([(18, 26), (-2, 4801), (8, 8749)], prec, True)

@constant_memo
def ln10_fixed(prec):
    return machin([(46, 31), (34, 49), (20, 161)], prec, True)

CHUD_A = MPZ(13591409)
CHUD_B = MPZ(545140134)
CHUD_C = MPZ(640320)
CHUD_D = MPZ(12)

def bs_chudnovsky(a, b, level, verbose):
    if b-a == 1:
        g = MPZ((6*b-5)*(2*b-1)*(6*b-1))
        p = b**3*CHUD_C**3 // 24
        q = (-1)**b*g*(CHUD_A+CHUD_B*b)
    else:
        if verbose and level < 4:
            print("  binary splitting", a, b)
        mid = (a+b)//2
        g1, p1, q1 = bs_chudnovsky(a, mid, level+1, verbose)
        g2, p2, q2 = bs_chudnovsky(mid, b, level+1, verbose)
        p = p1*p2
        g = g1*g2
        q = q1*p2 + q2*g1
    return g, p, q

@constant_memo
def pi_fixed(prec, verbose=False, verbose_base=None):
    N = int(prec/3.3219280948/14.181647462 + 2)
    if verbose:
        print("binary splitting with N =", N)
    g, p, q = bs_chudnovsky(0, N, 0, verbose)
    sqrtC = isqrt_fast(CHUD_C<<(2*prec))
    v = p*CHUD_C*sqrtC//((q+CHUD_A*p)*CHUD_D)
    return v

def degree_fixed(prec):
    return pi_fixed(prec)//180

def bspe(a, b):
    if b-a == 1:
        return MPZ_ONE, MPZ(b)
    m = (a+b)//2
    p1, q1 = bspe(a, m)
    p2, q2 = bspe(m, b)
    return p1*q2+p2, q1*q2

@constant_memo
def e_fixed(prec):
    N = int(1.1*prec/math.log(prec) + 20)
    p, q = bspe(0,N)
    return ((p+q)<<prec)//q

@constant_memo
def phi_fixed(prec):
    prec += 10
    a = isqrt_fast(MPZ_FIVE<<(2*prec)) + (MPZ_ONE << prec)
    return a >> 11

mpf_phi    = def_mpf_constant(phi_fixed)
mpf_pi     = def_mpf_constant(pi_fixed)
mpf_e      = def_mpf_constant(e_fixed)
mpf_degree = def_mpf_constant(degree_fixed)
mpf_ln2    = def_mpf_constant(ln2_fixed)
mpf_ln10   = def_mpf_constant(ln10_fixed)

@constant_memo
def ln_sqrt2pi_fixed(prec):
    wp = prec + 10
    # ln(sqrt(2*pi)) = ln(2*pi)/2
    return to_fixed(mpf_log(mpf_shift(mpf_pi(wp), 1), wp), prec-1)

@constant_memo
def sqrtpi_fixed(prec):
    return sqrt_fixed(pi_fixed(prec), prec)

mpf_sqrtpi   = def_mpf_constant(sqrtpi_fixed)
mpf_ln_sqrt2pi   = def_mpf_constant(ln_sqrt2pi_fixed)

def mpf_pow(s, t, prec, rnd=round_fast):
    ssign, sman, sexp, sbc = s
    tsign, tman, texp, tbc = t
    if ssign and texp < 0:
        raise ComplexResult("negative number raised to a fractional power")
    if texp >= 0:
        return mpf_pow_int(s, (-1)**tsign*(tman<<texp), prec, rnd)
    # s**(n/2) = sqrt(s)**n
    if texp == -1:
        if tman == 1:
            if tsign:
                return mpf_div(fone, mpf_sqrt(s, prec+10,
                    reciprocal_rnd[rnd]), prec, rnd)
            return mpf_sqrt(s, prec, rnd)
        else:
            if tsign:
                return mpf_pow_int(mpf_sqrt(s, prec+10,
                    reciprocal_rnd[rnd]), -tman, prec, rnd)
            return mpf_pow_int(mpf_sqrt(s, prec+10, rnd), tman, prec, rnd)
    c = mpf_log(s, prec+10, rnd)
    return mpf_exp(mpf_mul(t, c), prec, rnd)

def int_pow_fixed(y, n, prec):

    if n == 2:
        return (y*y), 0
    bc = bitcount(y)
    exp = 0
    workprec = 2*(prec + 4*bitcount(n) + 4)
    _, pm, pe, pbc = fone
    while 1:
        if n & 1:
            pm = pm*y
            pe = pe+exp
            pbc += bc - 2
            pbc = pbc + bctable[int(pm >> pbc)]
            if pbc > workprec:
                pm = pm >> (pbc-workprec)
                pe += pbc - workprec
                pbc = workprec
            n -= 1
            if not n:
                break
        y = y*y
        exp = exp+exp
        bc = bc + bc - 2
        bc = bc + bctable[int(y >> bc)]
        if bc > workprec:
            y = y >> (bc-workprec)
            exp += bc - workprec
            bc = workprec
        n = n // 2
    return pm, pe

def nthroot_fixed(y, n, prec, exp1):
    start = 50
    try:
        y1 = rshift(y, prec - n*start)
        r = MPZ(int(y1**(1.0/n)))
    except OverflowError:
        y1 = from_int(y1, start)
        fn = from_int(n)
        fn = mpf_rdiv_int(1, fn, start)
        r = mpf_pow(y1, fn, start)
        r = to_int(r)
    extra = 10
    extra1 = n
    prevp = start
    for p in giant_steps(start, prec+extra):
        pm, pe = int_pow_fixed(r, n-1, prevp)
        r2 = rshift(pm, (n-1)*prevp - p - pe - extra1)
        B = lshift(y, 2*p-prec+extra1)//r2
        r = (B + (n-1)*lshift(r, p-prevp))//n
        prevp = p
    return r

def mpf_nthroot(s, n, prec, rnd=round_fast):
    sign, man, exp, bc = s
    if sign:
        raise ComplexResult("nth root of a negative number")
    if not man:
        if s == fnan:
            return fnan
        if s == fzero:
            if n > 0:
                return fzero
            if n == 0:
                return fone
            return finf
        # Infinity
        if not n:
            return fnan
        if n < 0:
            return fzero
        return finf
    flag_inverse = False
    if n < 2:
        if n == 0:
            return fone
        if n == 1:
            return mpf_pos(s, prec, rnd)
        if n == -1:
            return mpf_div(fone, s, prec, rnd)
        # n < 0
        rnd = reciprocal_rnd[rnd]
        flag_inverse = True
        extra_inverse = 5
        prec += extra_inverse
        n = -n
    if n > 20 and (n >= 20000 or prec < int(233 + 28.3*n**0.62)):
        prec2 = prec + 10
        fn = from_int(n)
        nth = mpf_rdiv_int(1, fn, prec2)
        r = mpf_pow(s, nth, prec2, rnd)
        s = normalize(r[0], r[1], r[2], r[3], prec, rnd)
        if flag_inverse:
            return mpf_div(fone, s, prec-extra_inverse, rnd)
        else:
            return s
    prec2 = prec + 2*n - (prec%n)
    if n > 10:
        prec2 += prec2//10
        prec2 = prec2 - prec2%n
    shift = bc - prec2
    sign1 = 0
    es = exp+shift
    if es < 0:
        sign1 = 1
        es = -es
    if sign1:
        shift += es%n
    else:
        shift -= es%n
    man = rshift(man, shift)
    extra = 10
    exp1 = ((exp+shift-(n-1)*prec2)//n) - extra
    rnd_shift = 0
    if flag_inverse:
        if rnd == 'u' or rnd == 'c':
            rnd_shift = 1
    else:
        if rnd == 'd' or rnd == 'f':
            rnd_shift = 1
    man = nthroot_fixed(man+rnd_shift, n, prec2, exp1)
    s = from_man_exp(man, exp1, prec, rnd)
    if flag_inverse:
        return mpf_div(fone, s, prec-extra_inverse, rnd)
    else:
        return s

def mpf_cbrt(s, prec, rnd=round_fast):
    return mpf_nthroot(s, 3, prec, rnd)


def agm_fixed(a, b, prec):

    i = 0
    while 1:
        anew = (a+b)>>1
        if i > 4 and abs(a-anew) < 8:
            return a
        b = isqrt_fast(a*b)
        a = anew
        i += 1
    return a

def log_agm(x, prec):
    x2 = (x*x) >> prec
    s = a = b = x2
    while a:
        b = (b*x2) >> prec
        a = (a*b) >> prec
        s += a
    s += (MPZ_ONE<<prec)
    s = (s*s)>>(prec-2)
    s = (s*isqrt_fast(x<<prec))>>prec
    t = a = b = x
    while a:
        b = (b*x2) >> prec
        a = (a*b) >> prec
        t += a
    t = (MPZ_ONE<<prec) + (t<<1)
    t = (t*t)>>prec
    p = agm_fixed(s, t, prec)
    return (pi_fixed(prec) << prec) // p

def log_taylor(x, prec, r=0):
    for i in range(r):
        x = isqrt_fast(x<<prec)
    one = MPZ_ONE << prec
    v = ((x-one)<<prec)//(x+one)
    sign = v < 0
    if sign:
        v = -v
    v2 = (v*v) >> prec
    v4 = (v2*v2) >> prec
    s0 = v
    s1 = v//3
    v = (v*v4) >> prec
    k = 5
    while v:
        s0 += v // k
        k += 2
        s1 += v // k
        v = (v*v4) >> prec
        k += 2
    s1 = (s1*v2) >> prec
    s = (s0+s1) << (1+r)
    if sign:
        return -s
    return s

def log_taylor_cached(x, prec):
    n = x >> (prec-LOG_TAYLOR_SHIFT)
    cached_prec = cache_prec_steps[prec]
    dprec = cached_prec - prec
    if (n, cached_prec) in log_taylor_cache:
        a, log_a = log_taylor_cache[n, cached_prec]
    else:
        a = n << (cached_prec - LOG_TAYLOR_SHIFT)
        log_a = log_taylor(a, cached_prec, 8)
        log_taylor_cache[n, cached_prec] = (a, log_a)
    a >>= dprec
    log_a >>= dprec
    u = ((x - a) << prec) // a
    v = (u << prec) // ((MPZ_TWO << prec) + u)
    v2 = (v*v) >> prec
    v4 = (v2*v2) >> prec
    s0 = v
    s1 = v//3
    v = (v*v4) >> prec
    k = 5
    while v:
        s0 += v//k
        k += 2
        s1 += v//k
        v = (v*v4) >> prec
        k += 2
    s1 = (s1*v2) >> prec
    s = (s0+s1) << 1
    return log_a + s

def mpf_log(x, prec, rnd=round_fast):
    sign, man, exp, bc = x
    if not man:
        if x == fzero: return fninf
        if x == finf: return finf
        if x == fnan: return fnan
    if sign:
        raise ComplexResult("logarithm of a negative number")
    wp = prec + 20

    if man == 1:
        if not exp:
            return fzero
        return from_man_exp(exp*ln2_fixed(wp), -wp, prec, rnd)
    mag = exp+bc
    abs_mag = abs(mag)

    if abs_mag <= 1:
        tsign = 1-abs_mag
        if tsign:
            tman = (MPZ_ONE<<bc) - man
        else:
            tman = man - (MPZ_ONE<<(bc-1))
        tbc = bitcount(tman)
        cancellation = bc - tbc
        if cancellation > wp:
            t = normalize(tsign, tman, abs_mag-bc, tbc, tbc, 'n')
            return mpf_perturb(t, tsign, prec, rnd)
        else:
            wp += cancellation
    if abs_mag > 10000:
        if bitcount(abs_mag) > wp:
            return from_man_exp(exp*ln2_fixed(wp), -wp, prec, rnd)
    if wp <= LOG_TAYLOR_PREC:
        m = log_taylor_cached(lshift(man, wp-bc), wp)
        if mag:
            m += mag*ln2_fixed(wp)
    else:
        optimal_mag = -wp//LOG_AGM_MAG_PREC_RATIO
        n = optimal_mag - mag
        x = mpf_shift(x, n)
        wp += (-optimal_mag)
        m = -log_agm(to_fixed(x, wp), wp)
        m -= n*ln2_fixed(wp)
    return from_man_exp(m, -wp, prec, rnd)

def mpf_log_hypot(a, b, prec, rnd):
    if not b[1]:
        a, b = b, a
    if not a[1]:
        if not b[1]:
            if a == b == fzero:
                return fninf
            if fnan in (a, b):
                return fnan
            return finf
        if a == fzero:
            return mpf_log(mpf_abs(b), prec, rnd)
        if a == fnan:
            return fnan
        return finf
    a2 = mpf_mul(a,a)
    b2 = mpf_mul(b,b)
    extra = 20
    h2 = mpf_add(a2, b2, prec+extra)
    cancelled = mpf_add(h2, fnone, 10)
    mag_cancelled = cancelled[2]+cancelled[3]
    if cancelled == fzero or mag_cancelled < -extra//2:
        h2 = mpf_add(a2, b2, prec+extra-min(a2[2],b2[2]))
    return mpf_shift(mpf_log(h2, prec, rnd), -1)

def atan_newton(x, prec):
    if prec >= 100:
        r = math.atan(int((x>>(prec-53)))/2.0**53)
    else:
        r = math.atan(int(x)/2.0**prec)
    prevp = 50
    r = MPZ(int(r*2.0**53) >> (53-prevp))
    extra_p = 50
    for wp in giant_steps(prevp, prec):
        wp += extra_p
        r = r << (wp-prevp)
        cos, sin = cos_sin_fixed(r, wp)
        tan = (sin << wp) // cos
        a = ((tan-rshift(x, prec-wp)) << wp) // ((MPZ_ONE<<wp) + ((tan**2)>>wp))
        r = r - a
        prevp = wp
    return rshift(r, prevp-prec)

def atan_taylor_get_cached(n, prec):
    prec2 = (1<<(bitcount(prec-1))) + 20
    dprec = prec2 - prec
    if (n, prec2) in atan_taylor_cache:
        a, atan_a = atan_taylor_cache[n, prec2]
    else:
        a = n << (prec2 - ATAN_TAYLOR_SHIFT)
        atan_a = atan_newton(a, prec2)
        atan_taylor_cache[n, prec2] = (a, atan_a)
    return (a >> dprec), (atan_a >> dprec)

def atan_taylor(x, prec):
    n = (x >> (prec-ATAN_TAYLOR_SHIFT))
    a, atan_a = atan_taylor_get_cached(n, prec)
    d = x - a
    s0 = v = (d << prec) // ((a**2 >> prec) + (a*d >> prec) + (MPZ_ONE << prec))
    v2 = (v**2 >> prec)
    v4 = (v2*v2) >> prec
    s1 = v//3
    v = (v*v4) >> prec
    k = 5
    while v:
        s0 += v // k
        k += 2
        s1 += v // k
        v = (v*v4) >> prec
        k += 2
    s1 = (s1*v2) >> prec
    s = s0 - s1
    return atan_a + s

def atan_inf(sign, prec, rnd):
    if not sign:
        return mpf_shift(mpf_pi(prec, rnd), -1)
    return mpf_neg(mpf_shift(mpf_pi(prec, negative_rnd[rnd]), -1))

def mpf_atan(x, prec, rnd=round_fast):
    sign, man, exp, bc = x
    if not man:
        if x == fzero: return fzero
        if x == finf: return atan_inf(0, prec, rnd)
        if x == fninf: return atan_inf(1, prec, rnd)
        return fnan
    mag = exp + bc
    if mag > prec+20:
        return atan_inf(sign, prec, rnd)
    if -mag > prec+20:
        return mpf_perturb(x, 1-sign, prec, rnd)
    wp = prec + 30 + abs(mag)
    if mag >= 2:
        x = mpf_rdiv_int(1, x, wp)
        reciprocal = True
    else:
        reciprocal = False
    t = to_fixed(x, wp)
    if sign:
        t = -t
    if wp < ATAN_TAYLOR_PREC:
        a = atan_taylor(t, wp)
    else:
        a = atan_newton(t, wp)
    if reciprocal:
        a = ((pi_fixed(wp)>>1)+1) - a
    if sign:
        a = -a
    return from_man_exp(a, -wp, prec, rnd)

def mpf_atan2(y, x, prec, rnd=round_fast):
    xsign, xman, xexp, xbc = x
    ysign, yman, yexp, ybc = y
    if not yman:
        if y == fzero and x != fnan:
            if mpf_sign(x) >= 0:
                return fzero
            return mpf_pi(prec, rnd)
        if y in (finf, fninf):
            if x in (finf, fninf):
                return fnan
            if y == finf:
                return mpf_shift(mpf_pi(prec, rnd), -1)
            return mpf_neg(mpf_shift(mpf_pi(prec, negative_rnd[rnd]), -1))
        return fnan
    if ysign:
        return mpf_neg(mpf_atan2(mpf_neg(y), x, prec, negative_rnd[rnd]))
    if not xman:
        if x == fnan:
            return fnan
        if x == finf:
            return fzero
        if x == fninf:
            return mpf_pi(prec, rnd)
        if y == fzero:
            return fzero
        return mpf_shift(mpf_pi(prec, rnd), -1)
    tquo = mpf_atan(mpf_div(y, x, prec+4), prec+4)
    if xsign:
        return mpf_add(mpf_pi(prec+4), tquo, prec, rnd)
    else:
        return mpf_pos(tquo, prec, rnd)

def mpf_asin(x, prec, rnd=round_fast):
    sign, man, exp, bc = x
    if bc+exp > 0 and x not in (fone, fnone):
        raise ComplexResult("asin(x) is real only for -1 <= x <= 1")
    wp = prec + 15
    a = mpf_mul(x, x)
    b = mpf_add(fone, mpf_sqrt(mpf_sub(fone, a, wp), wp), wp)
    c = mpf_div(x, b, wp)
    return mpf_shift(mpf_atan(c, prec, rnd), 1)

def mpf_acos(x, prec, rnd=round_fast):
    sign, man, exp, bc = x
    if bc + exp > 0:
        if x not in (fone, fnone):
            raise ComplexResult("acos(x) is real only for -1 <= x <= 1")
        if x == fnone:
            return mpf_pi(prec, rnd)
    wp = prec + 15
    a = mpf_mul(x, x)
    b = mpf_sqrt(mpf_sub(fone, a, wp), wp)
    c = mpf_div(b, mpf_add(fone, x, wp), wp)
    return mpf_shift(mpf_atan(c, prec, rnd), 1)

def mpf_asinh(x, prec, rnd=round_fast):
    wp = prec + 20
    sign, man, exp, bc = x
    mag = exp+bc
    if mag < -8:
        if mag < -wp:
            return mpf_perturb(x, 1-sign, prec, rnd)
        wp += (-mag)
    q = mpf_sqrt(mpf_add(mpf_mul(x, x), fone, wp), wp)
    q = mpf_add(mpf_abs(x), q, wp)
    if sign:
        return mpf_neg(mpf_log(q, prec, negative_rnd[rnd]))
    else:
        return mpf_log(q, prec, rnd)

def mpf_acosh(x, prec, rnd=round_fast):
    wp = prec + 15
    if mpf_cmp(x, fone) == -1:
        raise ComplexResult("acosh(x) is real only for x >= 1")
    q = mpf_sqrt(mpf_add(mpf_mul(x,x), fnone, wp), wp)
    return mpf_log(mpf_add(x, q, wp), prec, rnd)

def mpf_atanh(x, prec, rnd=round_fast):
    sign, man, exp, bc = x
    if (not man) and exp:
        if x in (fzero, fnan):
            return x
        raise ComplexResult("atanh(x) is real only for -1 <= x <= 1")
    mag = bc + exp
    if mag > 0:
        if mag == 1 and man == 1:
            return [finf, fninf][sign]
        raise ComplexResult("atanh(x) is real only for -1 <= x <= 1")
    wp = prec + 15
    if mag < -8:
        if mag < -wp:
            return mpf_perturb(x, sign, prec, rnd)
        wp += (-mag)
    a = mpf_add(x, fone, wp)
    b = mpf_sub(fone, x, wp)
    return mpf_shift(mpf_log(mpf_div(a, b, wp), prec, rnd), -1)

def mpf_fibonacci(x, prec, rnd=round_fast):
    sign, man, exp, bc = x
    if not man:
        if x == fninf:
            return fnan
        return x
    size = abs(exp+bc)
    if exp >= 0:
        if size < 10 or size <= bitcount(prec):
            return from_int(ifib(to_int(x)), prec, rnd)
    wp = prec + size + 20
    a = mpf_phi(wp)
    b = mpf_add(mpf_shift(a, 1), fnone, wp)
    u = mpf_pow(a, x, wp)
    v = mpf_cos_pi(x, wp)
    v = mpf_div(v, u, wp)
    u = mpf_sub(u, v, wp)
    u = mpf_div(u, b, prec, rnd)
    return u

def exponential_series(x, prec, type=0):
    if x < 0:
        x = -x
        sign = 1
    else:
        sign = 0
    r = int(0.5*prec**0.5)
    xmag = bitcount(x) - prec
    r = max(0, xmag + r)
    extra = 10 + 2*max(r,-xmag)
    wp = prec + extra
    x <<= (extra - r)
    one = MPZ_ONE << wp
    alt = (type == 2)
    if prec < EXP_SERIES_U_CUTOFF:
        x2 = a = (x*x) >> wp
        x4 = (x2*x2) >> wp
        s0 = s1 = MPZ_ZERO
        k = 2
        while a:
            a //= (k-1)*k; s0 += a; k += 2
            a //= (k-1)*k; s1 += a; k += 2
            a = (a*x4) >> wp
        s1 = (x2*s1) >> wp
        if alt:
            c = s1 - s0 + one
        else:
            c = s1 + s0 + one
    else:
        u = int(0.3*prec**0.35)
        x2 = a = (x*x) >> wp
        xpowers = [one, x2]
        for i in range(1, u):
            xpowers.append((xpowers[-1]*x2)>>wp)
        sums = [MPZ_ZERO]*u
        k = 2
        while a:
            for i in range(u):
                a //= (k-1)*k
                if alt and k & 2: sums[i] -= a
                else:             sums[i] += a
                k += 2
            a = (a*xpowers[-1]) >> wp
        for i in range(1, u):
            sums[i] = (sums[i]*xpowers[i]) >> wp
        c = sum(sums) + one
    if type == 0:
        s = isqrt_fast(c*c - (one<<wp))
        if sign:
            v = c - s
        else:
            v = c + s
        for i in range(r):
            v = (v*v) >> wp
        return v >> extra
    else:
        pshift = wp-1
        for i in range(r):
            c = ((c*c) >> pshift) - one
        s = isqrt_fast(abs((one<<wp) - c*c))
        if sign:
            s = -s
        return (c>>extra), (s>>extra)

def exp_basecase(x, prec):
    if prec > EXP_COSH_CUTOFF:
        return exponential_series(x, prec, 0)
    r = int(prec**0.5)
    prec += r
    s0 = s1 = (MPZ_ONE << prec)
    k = 2
    a = x2 = (x*x) >> prec
    while a:
        a //= k; s0 += a; k += 1
        a //= k; s1 += a; k += 1
        a = (a*x2) >> prec
    s1 = (s1*x) >> prec
    s = s0 + s1
    u = r
    while r:
        s = (s*s) >> prec
        r -= 1
    return s >> u

def exp_expneg_basecase(x, prec):
    if prec > EXP_COSH_CUTOFF:
        cosh, sinh = exponential_series(x, prec, 1)
        return cosh+sinh, cosh-sinh
    a = exp_basecase(x, prec)
    b = (MPZ_ONE << (prec+prec)) // a
    return a, b

def cos_sin_basecase(x, prec):
    if prec > COS_SIN_CACHE_PREC:
        return exponential_series(x, prec, 2)
    precs = prec - COS_SIN_CACHE_STEP
    t = x >> precs
    n = int(t)
    if n not in cos_sin_cache:
        w = t<<(10+COS_SIN_CACHE_PREC-COS_SIN_CACHE_STEP)
        cos_t, sin_t = exponential_series(w, 10+COS_SIN_CACHE_PREC, 2)
        cos_sin_cache[n] = (cos_t>>10), (sin_t>>10)
    cos_t, sin_t = cos_sin_cache[n]
    offset = COS_SIN_CACHE_PREC - prec
    cos_t >>= offset
    sin_t >>= offset
    x -= t << precs
    cos = MPZ_ONE << prec
    sin = x
    k = 2
    a = -((x*x) >> prec)
    while a:
        a //= k; cos += a; k += 1; a = (a*x) >> prec
        a //= k; sin += a; k += 1; a = -((a*x) >> prec)
    return ((cos*cos_t-sin*sin_t) >> prec), ((sin*cos_t+cos*sin_t) >> prec)

def mpf_exp(x, prec, rnd=round_fast):
    sign, man, exp, bc = x
    if man:
        mag = bc + exp
        wp = prec + 14
        if sign:
            man = -man
        if prec > 600 and exp >= 0:
            e = mpf_e(wp+int(1.45*mag))
            return mpf_pow_int(e, man<<exp, prec, rnd)
        if mag < -wp:
            return mpf_perturb(fone, sign, prec, rnd)
        if mag > 1:
            wpmod = wp + mag
            offset = exp + wpmod
            if offset >= 0:
                t = man << offset
            else:
                t = man >> (-offset)
            lg2 = ln2_fixed(wpmod)
            n, t = divmod(t, lg2)
            n = int(n)
            t >>= mag
        else:
            offset = exp + wp
            if offset >= 0:
                t = man << offset
            else:
                t = man >> (-offset)
            n = 0
        man = exp_basecase(t, wp)
        return from_man_exp(man, n-wp, prec, rnd)
    if not exp:
        return fone
    if x == fninf:
        return fzero
    return x


def mpf_cosh_sinh(x, prec, rnd=round_fast, tanh=0):
    sign, man, exp, bc = x
    if (not man) and exp:
        if tanh:
            if x == finf: return fone
            if x == fninf: return fnone
            return fnan
        if x == finf: return (finf, finf)
        if x == fninf: return (finf, fninf)
        return fnan, fnan
    mag = exp+bc
    wp = prec+14
    if mag < -4:
        if mag < -wp:
            if tanh:
                return mpf_perturb(x, 1-sign, prec, rnd)
            cosh = mpf_perturb(fone, 0, prec, rnd)
            sinh = mpf_perturb(x, sign, prec, rnd)
            return cosh, sinh
        wp += (-mag)
    if mag > 10:
        if 3*(1<<(mag-1)) > wp:
            if tanh:
                return mpf_perturb([fone,fnone][sign], 1-sign, prec, rnd)
            c = s = mpf_shift(mpf_exp(mpf_abs(x), prec, rnd), -1)
            if sign:
                s = mpf_neg(s)
            return c, s
    if mag > 1:
        wpmod = wp + mag
        offset = exp + wpmod
        if offset >= 0:
            t = man << offset
        else:
            t = man >> (-offset)
        lg2 = ln2_fixed(wpmod)
        n, t = divmod(t, lg2)
        n = int(n)
        t >>= mag
    else:
        offset = exp + wp
        if offset >= 0:
            t = man << offset
        else:
            t = man >> (-offset)
        n = 0
    a, b = exp_expneg_basecase(t, wp)
    cosh = a + (b>>(2*n))
    sinh = a - (b>>(2*n))
    if sign:
        sinh = -sinh
    if tanh:
        man = (sinh << wp) // cosh
        return from_man_exp(man, -wp, prec, rnd)
    else:
        cosh = from_man_exp(cosh, n-wp-1, prec, rnd)
        sinh = from_man_exp(sinh, n-wp-1, prec, rnd)
        return cosh, sinh

def mod_pi2(man, exp, mag, wp):
    if mag > 0:
        i = 0
        while 1:
            cancellation_prec = 20 << i
            wpmod = wp + mag + cancellation_prec
            pi2 = pi_fixed(wpmod-1)
            pi4 = pi2 >> 1
            offset = wpmod + exp
            if offset >= 0:
                t = man << offset
            else:
                t = man >> (-offset)
            n, y = divmod(t, pi2)
            if y > pi4:
                small = pi2 - y
            else:
                small = y
            if small >> (wp+mag-10):
                n = int(n)
                t = y >> mag
                wp = wpmod - mag
                break
            i += 1
    else:
        wp += (-mag)
        offset = exp + wp
        if offset >= 0:
            t = man << offset
        else:
            t = man >> (-offset)
        n = 0
    return t, n, wp


def mpf_cos_sin(x, prec, rnd=round_fast, which=0, pi=False):
    sign, man, exp, bc = x
    if not man:
        if exp:
            c, s = fnan, fnan
        else:
            c, s = fone, fzero
        if which == 0: return c, s
        if which == 1: return c
        if which == 2: return s
        if which == 3: return s
    mag = bc + exp
    wp = prec + 10
    if mag < 0:
        if mag < -wp:
            if pi:
                x = mpf_mul(x, mpf_pi(wp))
            c = mpf_perturb(fone, 1, prec, rnd)
            s = mpf_perturb(x, 1-sign, prec, rnd)
            if which == 0: return c, s
            if which == 1: return c
            if which == 2: return s
            if which == 3: return mpf_perturb(x, sign, prec, rnd)
    if pi:
        if exp >= -1:
            if exp == -1:
                c = fzero
                s = (fone, fnone)[bool(man & 2) ^ sign]
            elif exp == 0:
                c, s = (fnone, fzero)
            else:
                c, s = (fone, fzero)
            if which == 0: return c, s
            if which == 1: return c
            if which == 2: return s
            if which == 3: return mpf_div(s, c, prec, rnd)
        n = ((man >> (-exp-2)) + 1) >> 1
        man = man - (n << (-exp-1))
        mag2 = bitcount(man) + exp
        wp = prec + 10 - mag2
        offset = exp + wp
        if offset >= 0:
            t = man << offset
        else:
            t = man >> (-offset)
        t = (t*pi_fixed(wp)) >> wp
    else:
        t, n, wp = mod_pi2(man, exp, mag, wp)
    c, s = cos_sin_basecase(t, wp)
    m = n & 3
    if   m == 1: c, s = -s, c
    elif m == 2: c, s = -c, -s
    elif m == 3: c, s = s, -c
    if sign:
        s = -s
    if which == 0:
        c = from_man_exp(c, -wp, prec, rnd)
        s = from_man_exp(s, -wp, prec, rnd)
        return c, s
    if which == 1:
        return from_man_exp(c, -wp, prec, rnd)
    if which == 2:
        return from_man_exp(s, -wp, prec, rnd)
    if which == 3:
        return from_rational(s, c, prec, rnd)


def make_hyp_summator(key):
    p, q, param_types, ztype = key
    pstring = "".join(param_types)
    fname = "hypsum_%i_%i_%s_%s_%s" % (p, q, pstring[:p], pstring[p:], ztype)
    have_complex_param = 'C' in param_types
    have_complex_arg = ztype == 'C'
    have_complex = have_complex_param or have_complex_arg
    source = []
    add = source.append
    aint = []
    arat = []
    bint = []
    brat = []
    areal = []
    breal = []
    acomplex = []
    bcomplex = []
    add("MAX = kwargs.get('maxterms', wp*100)")
    add("HIGH = MPZ_ONE<<epsshift")
    add("LOW = -HIGH")
    add("SRE = PRE = one = (MPZ_ONE << wp)")
    if have_complex:
        add("SIM = PIM = MPZ_ZERO")
    if have_complex_arg:
        add("xsign, xm, xe, xbc = z[0]")
        add("if xsign: xm = -xm")
        add("ysign, ym, ye, ybc = z[1]")
        add("if ysign: ym = -ym")
    else:
        add("xsign, xm, xe, xbc = z")
        add("if xsign: xm = -xm")
    add("offset = xe + wp")
    add("if offset >= 0:")
    add("    ZRE = xm << offset")
    add("else:")
    add("    ZRE = xm >> (-offset)")
    if have_complex_arg:
        add("offset = ye + wp")
        add("if offset >= 0:")
        add("    ZIM = ym << offset")
        add("else:")
        add("    ZIM = ym >> (-offset)")
    for i, flag in enumerate(param_types):
        W = ["A", "B"][i >= p]
        if flag == 'Z':
            ([aint,bint][i >= p]).append(i)
            add("%sINT_%i = coeffs[%i]" % (W, i, i))
        elif flag == 'Q':
            ([arat,brat][i >= p]).append(i)
            add("%sP_%i, %sQ_%i = coeffs[%i]._mpq_" % (W, i, W, i, i))
        elif flag == 'R':
            ([areal,breal][i >= p]).append(i)
            add("xsign, xm, xe, xbc = coeffs[%i]._mpf_" % i)
            add("if xsign: xm = -xm")
            add("offset = xe + wp")
            add("if offset >= 0:")
            add("    %sREAL_%i = xm << offset" % (W, i))
            add("else:")
            add("    %sREAL_%i = xm >> (-offset)" % (W, i))
        elif flag == 'C':
            ([acomplex,bcomplex][i >= p]).append(i)
            add("__re, __im = coeffs[%i]._mpc_" % i)
            add("xsign, xm, xe, xbc = __re")
            add("if xsign: xm = -xm")
            add("ysign, ym, ye, ybc = __im")
            add("if ysign: ym = -ym")

            add("offset = xe + wp")
            add("if offset >= 0:")
            add("    %sCRE_%i = xm << offset" % (W, i))
            add("else:")
            add("    %sCRE_%i = xm >> (-offset)" % (W, i))
            add("offset = ye + wp")
            add("if offset >= 0:")
            add("    %sCIM_%i = ym << offset" % (W, i))
            add("else:")
            add("    %sCIM_%i = ym >> (-offset)" % (W, i))
        else:
            raise ValueError

    l_areal = len(areal)
    l_breal = len(breal)
    cancellable_real = min(l_areal, l_breal)
    noncancellable_real_num = areal[cancellable_real:]
    noncancellable_real_den = breal[cancellable_real:]
    add("for n in range(1,10**8):")
    add("    if n in magnitude_check:")
    add("        p_mag = bitcount(abs(PRE))")
    if have_complex:
        add("        p_mag = max(p_mag, bitcount(abs(PIM)))")
    add("        magnitude_check[n] = wp-p_mag")
    multiplier = "*".join(["AINT_#".replace("#", str(i)) for i in aint] + \
                            ["AP_#".replace("#", str(i)) for i in arat] + \
                            ["BQ_#".replace("#", str(i)) for i in brat])

    divisor    = "*".join(["BINT_#".replace("#", str(i)) for i in bint] + \
                            ["BP_#".replace("#", str(i)) for i in brat] + \
                            ["AQ_#".replace("#", str(i)) for i in arat] + ["n"])
    if multiplier:
        add("    mul = " + multiplier)
    add("    div = " + divisor)
    add("    if not div:")
    if multiplier:
        add("        if not mul:")
        add("            break")
    add("        raise ZeroDivisionError")

    if have_complex:
        for k in range(cancellable_real): add("    PRE = PRE*AREAL_%i // BREAL_%i" % (areal[k], breal[k]))
        for i in noncancellable_real_num: add("    PRE = (PRE*AREAL_#) >> wp".replace("#", str(i)))
        for i in noncancellable_real_den: add("    PRE = (PRE << wp) // BREAL_#".replace("#", str(i)))
        for k in range(cancellable_real): add("    PIM = PIM*AREAL_%i // BREAL_%i" % (areal[k], breal[k]))
        for i in noncancellable_real_num: add("    PIM = (PIM*AREAL_#) >> wp".replace("#", str(i)))
        for i in noncancellable_real_den: add("    PIM = (PIM << wp) // BREAL_#".replace("#", str(i)))
        if multiplier:
            if have_complex_arg:
                add("    PRE, PIM = (mul*(PRE*ZRE-PIM*ZIM))//div, (mul*(PIM*ZRE+PRE*ZIM))//div")
                add("    PRE >>= wp")
                add("    PIM >>= wp")
            else:
                add("    PRE = ((mul*PRE*ZRE) >> wp) // div")
                add("    PIM = ((mul*PIM*ZRE) >> wp) // div")
        else:
            if have_complex_arg:
                add("    PRE, PIM = (PRE*ZRE-PIM*ZIM)//div, (PIM*ZRE+PRE*ZIM)//div")
                add("    PRE >>= wp")
                add("    PIM >>= wp")
            else:
                add("    PRE = ((PRE*ZRE) >> wp) // div")
                add("    PIM = ((PIM*ZRE) >> wp) // div")
        for i in acomplex:
            add("    PRE, PIM = PRE*ACRE_#-PIM*ACIM_#, PIM*ACRE_#+PRE*ACIM_#".replace("#", str(i)))
            add("    PRE >>= wp")
            add("    PIM >>= wp")
        for i in bcomplex:
            add("    mag = BCRE_#*BCRE_#+BCIM_#*BCIM_#".replace("#", str(i)))
            add("    re = PRE*BCRE_# + PIM*BCIM_#".replace("#", str(i)))
            add("    im = PIM*BCRE_# - PRE*BCIM_#".replace("#", str(i)))
            add("    PRE = (re << wp) // mag".replace("#", str(i)))
            add("    PIM = (im << wp) // mag".replace("#", str(i)))
    else:
        for k in range(cancellable_real): add("    PRE = PRE*AREAL_%i // BREAL_%i" % (areal[k], breal[k]))
        for i in noncancellable_real_num: add("    PRE = (PRE*AREAL_#) >> wp".replace("#", str(i)))
        for i in noncancellable_real_den: add("    PRE = (PRE << wp) // BREAL_#".replace("#", str(i)))
        if multiplier:
            add("    PRE = ((PRE*mul*ZRE) >> wp) // div")
        else:
            add("    PRE = ((PRE*ZRE) >> wp) // div")
    if have_complex:
        add("    SRE += PRE")
        add("    SIM += PIM")
        add("    if (HIGH > PRE > LOW) and (HIGH > PIM > LOW):")
        add("        break")
    else:
        add("    SRE += PRE")
        add("    if HIGH > PRE > LOW:")
        add("        break")
    add("    if n > MAX:")
    add("        raise NoConvergence('Hypergeometric series converges too slowly. Try increasing maxterms.')")
    for i in aint:     add("    AINT_# += 1".replace("#", str(i)))
    for i in bint:     add("    BINT_# += 1".replace("#", str(i)))
    for i in arat:     add("    AP_# += AQ_#".replace("#", str(i)))
    for i in brat:     add("    BP_# += BQ_#".replace("#", str(i)))
    for i in areal:    add("    AREAL_# += one".replace("#", str(i)))
    for i in breal:    add("    BREAL_# += one".replace("#", str(i)))
    for i in acomplex: add("    ACRE_# += one".replace("#", str(i)))
    for i in bcomplex: add("    BCRE_# += one".replace("#", str(i)))
    if have_complex:
        add("a = from_man_exp(SRE, -wp, prec, 'n')")
        add("b = from_man_exp(SIM, -wp, prec, 'n')")
        add("if SRE:")
        add("    if SIM:")
        add("        magn = max(a[2]+a[3], b[2]+b[3])")
        add("    else:")
        add("        magn = a[2]+a[3]")
        add("elif SIM:")
        add("    magn = b[2]+b[3]")
        add("else:")
        add("    magn = -wp+1")
        add("return (a, b), True, magn")
    else:
        add("a = from_man_exp(SRE, -wp, prec, 'n')")
        add("if SRE:")
        add("    magn = a[2]+a[3]")
        add("else:")
        add("    magn = -wp+1")
        add("return a, False, magn")
    source = "\n".join(("    " + line) for line in source)
    source = ("def %s(coeffs, z, prec, wp, epsshift, magnitude_check, **kwargs):\n" % fname) + source
    namespace = {}
    exec_(source, globals(), namespace)
    return source, namespace[fname]

# if BACKEND == 'sage':
#     def make_hyp_summator(key):
#         from sage.libs.mpmath.ext_main import hypsum_internal
#         p, q, param_types, ztype = key
#         def _hypsum(coeffs, z, prec, wp, epsshift, magnitude_check, **kwargs):
#             return hypsum_internal(p, q, param_types, ztype, coeffs, z,
#                 prec, wp, epsshift, magnitude_check, kwargs)
#         return "(none)", _hypsum

def mpf_erf(x, prec, rnd=round_fast):
    sign, man, exp, bc = x
    if not man:
        if x == fzero: return fzero
        if x == finf: return fone
        if x== fninf: return fnone
        return fnan
    size = exp + bc
    lg = math.log
    if size > 3 and 2*(size-1) + 0.528766 > lg(prec,2):
        if sign:
            return mpf_perturb(fnone, 0, prec, rnd)
        else:
            return mpf_perturb(fone, 1, prec, rnd)
    if size < -prec:
        x = mpf_shift(x,1)
        c = mpf_sqrt(mpf_pi(prec+20), prec+20)
        return mpf_div(x, c, prec, rnd)
    wp = prec + abs(size) + 25
    t = abs(to_fixed(x, wp))
    t2 = (t*t) >> wp
    s, term, k = t, 12345, 1
    while term:
        t = ((t*t2) >> wp) // k
        term = t // (2*k+1)
        if k & 1:
            s -= term
        else:
            s += term
        k += 1
    s = (s << (wp+1)) // sqrt_fixed(pi_fixed(wp), wp)
    if sign:
        s = -s
    return from_man_exp(s, -wp, prec, rnd)

def erfc_check_series(x, prec):
    n = to_int(x)
    if n**2*1.44 > prec:
        return True
    return False

def mpf_erfc(x, prec, rnd=round_fast):
    sign, man, exp, bc = x
    if not man:
        if x == fzero: return fone
        if x == finf: return fzero
        if x == fninf: return ftwo
        return fnan
    wp = prec + 20
    mag = bc+exp
    wp += max(0, 2*mag)
    regular_erf = sign or mag < 2
    if regular_erf or not erfc_check_series(x, wp):
        if regular_erf:
            return mpf_sub(fone, mpf_erf(x, prec+10, negative_rnd[rnd]), prec, rnd)
        n = to_int(x)+1
        return mpf_sub(fone, mpf_erf(x, prec + int(n**2*1.44) + 10), prec, rnd)
    s = term = MPZ_ONE << wp
    term_prev = 0
    t = (2*to_fixed(x, wp) ** 2) >> wp
    k = 1
    while 1:
        term = ((term*(2*k - 1)) << wp) // t
        if k > 4 and term > term_prev or not term:
            break
        if k & 1:
            s -= term
        else:
            s += term
        term_prev = term
        k += 1
    s = (s << wp) // sqrt_fixed(pi_fixed(wp), wp)
    s = from_man_exp(s, -wp, wp)
    z = mpf_exp(mpf_neg(mpf_mul(x,x,wp),wp),wp)
    y = mpf_div(mpf_mul(z, s, wp), x, prec, rnd)
    return y

def ei_taylor(x, prec):
    s = t = x
    k = 2
    while t:
        t = ((t*x) >> prec) // k
        s += t // k
        k += 1
    return s

def complex_ei_taylor(zre, zim, prec):
    _abs = abs
    sre = tre = zre
    sim = tim = zim
    k = 2
    while _abs(tre) + _abs(tim) > 5:
        tre, tim = ((tre*zre-tim*zim)//k)>>prec, ((tre*zim+tim*zre)//k)>>prec
        sre += tre // k
        sim += tim // k
        k += 1
    return sre, sim

def ei_asymptotic(x, prec):
    one = MPZ_ONE << prec
    x = t = ((one << prec) // x)
    s = one + x
    k = 2
    while t:
        t = (k*t*x) >> prec
        s += t
        k += 1
    return s

def complex_ei_asymptotic(zre, zim, prec):
    _abs = abs
    one = MPZ_ONE << prec
    M = (zim*zim + zre*zre) >> prec
    xre = tre = (zre << prec) // M
    xim = tim = ((-zim) << prec) // M
    sre = one + xre
    sim = xim
    k = 2
    while _abs(tre) + _abs(tim) > 1000:
        tre, tim = ((tre*xre-tim*xim)*k)>>prec, ((tre*xim+tim*xre)*k)>>prec
        sre += tre
        sim += tim
        k += 1
        if k > prec:
            raise NoConvergence
    return sre, sim

def mpf_ei(x, prec, rnd=round_fast, e1=False):
    if e1:
        x = mpf_neg(x)
    sign, man, exp, bc = x
    if e1 and not sign:
        if x == fzero:
            return finf
        raise ComplexResult("E1(x) for x < 0")
    if man:
        xabs = 0, man, exp, bc
        xmag = exp+bc
        wp = prec + 20
        can_use_asymp = xmag > wp
        if not can_use_asymp:
            if exp >= 0:
                xabsint = man << exp
            else:
                xabsint = man >> (-exp)
            can_use_asymp = xabsint > int(wp*0.693) + 10
        if can_use_asymp:
            if xmag > wp:
                v = fone
            else:
                v = from_man_exp(ei_asymptotic(to_fixed(x, wp), wp), -wp)
            v = mpf_mul(v, mpf_exp(x, wp), wp)
            v = mpf_div(v, x, prec, rnd)
        else:
            wp += 2*int(to_int(xabs))
            u = to_fixed(x, wp)
            v = ei_taylor(u, wp) + euler_fixed(wp)
            t1 = from_man_exp(v,-wp)
            t2 = mpf_log(xabs,wp)
            v = mpf_add(t1, t2, prec, rnd)
    else:
        if x == fzero: v = fninf
        elif x == finf: v = finf
        elif x == fninf: v = fzero
        else: v = fnan
    if e1:
        v = mpf_neg(v)
    return v

def mpc_ei(z, prec, rnd=round_fast, e1=False):
    if e1:
        z = mpc_neg(z)
    a, b = z
    asign, aman, aexp, abc = a
    bsign, bman, bexp, bbc = b
    if b == fzero:
        if e1:
            x = mpf_neg(mpf_ei(a, prec, rnd))
            if not asign:
                y = mpf_neg(mpf_pi(prec, rnd))
            else:
                y = fzero
            return x, y
        else:
            return mpf_ei(a, prec, rnd), fzero
    if a != fzero:
        if not aman or not bman:
            return (fnan, fnan)
    wp = prec + 40
    amag = aexp+abc
    bmag = bexp+bbc
    zmag = max(amag, bmag)
    can_use_asymp = zmag > wp
    if not can_use_asymp:
        zabsint = abs(to_int(a)) + abs(to_int(b))
        can_use_asymp = zabsint > int(wp*0.693) + 20
    try:
        if can_use_asymp:
            if zmag > wp:
                v = fone, fzero
            else:
                zre = to_fixed(a, wp)
                zim = to_fixed(b, wp)
                vre, vim = complex_ei_asymptotic(zre, zim, wp)
                v = from_man_exp(vre, -wp), from_man_exp(vim, -wp)
            v = mpc_mul(v, mpc_exp(z, wp), wp)
            v = mpc_div(v, z, wp)
            if e1:
                v = mpc_neg(v, prec, rnd)
            else:
                x, y = v
                if bsign:
                    v = mpf_pos(x, prec, rnd), mpf_sub(y, mpf_pi(wp), prec, rnd)
                else:
                    v = mpf_pos(x, prec, rnd), mpf_add(y, mpf_pi(wp), prec, rnd)
            return v
    except NoConvergence:
        pass
    wp += 2*int(to_int(mpc_abs(z, 5)))
    zre = to_fixed(a, wp)
    zim = to_fixed(b, wp)
    vre, vim = complex_ei_taylor(zre, zim, wp)
    vre += euler_fixed(wp)
    v = from_man_exp(vre,-wp), from_man_exp(vim,-wp)
    if e1:
        u = mpc_log(mpc_neg(z),wp)
    else:
        u = mpc_log(z,wp)
    v = mpc_add(v, u, prec, rnd)
    if e1:
        v = mpc_neg(v)
    return v

def mpf_e1(x, prec, rnd=round_fast):
    return mpf_ei(x, prec, rnd, True)

def mpc_e1(x, prec, rnd=round_fast):
    return mpc_ei(x, prec, rnd, True)

def mpf_expint(n, x, prec, rnd=round_fast, gamma=False):
    sign, man, exp, bc = x
    if not man:
        if gamma:
            if x == fzero:
                if n <= 0:
                    return finf, None
                return mpf_gamma_int(n, prec, rnd), None
            if x == finf:
                return fzero, None
            return fnan, fnan
        else:
            if x == fzero:
                if n > 1:
                    return from_rational(1, n-1, prec, rnd), None
                else:
                    return finf, None
            if x == finf:
                return fzero, None
            return fnan, fnan
    n_orig = n
    if gamma:
        n = 1-n
    wp = prec + 20
    xmag = exp + bc
    if xmag < -10:
        raise NotImplementedError
    nmag = bitcount(abs(n))
    have_imag = n > 0 and sign
    negx = mpf_neg(x)
    if n == 0 or 2*nmag - xmag < -wp:
        if gamma:
            v = mpf_exp(negx, wp)
            re = mpf_mul(v, mpf_pow_int(x, n_orig-1, wp), prec, rnd)
        else:
            v = mpf_exp(negx, wp)
            re = mpf_div(v, x, prec, rnd)
    else:
        can_use_asymptotic_series = -3*wp < n <= 0
        if not can_use_asymptotic_series:
            xi = abs(to_int(x))
            m = min(max(1, xi-n), 2*wp)
            siz = -n*nmag + (m+n)*bitcount(abs(m+n)) - m*xmag - (144*m//100)
            tol = -wp-10
            can_use_asymptotic_series = siz < tol
        if can_use_asymptotic_series:
            r = ((-MPZ_ONE) << (wp+wp)) // to_fixed(x, wp)
            m = n
            t = r*m
            s = MPZ_ONE << wp
            while m and t:
                s += t
                m += 1
                t = (m*r*t) >> wp
            v = mpf_exp(negx, wp)
            if gamma:
                v = mpf_mul(v, mpf_pow_int(x, n_orig-1, wp), wp)
            else:
                v = mpf_div(v, x, wp)
            re = mpf_mul(v, from_man_exp(s, -wp), prec, rnd)
        elif n == 1:
            re = mpf_neg(mpf_ei(negx, prec, rnd))
        elif n > 0 and n < 3*wp:
            T1 = mpf_neg(mpf_ei(negx, wp))
            if gamma:
                if n_orig & 1:
                    T1 = mpf_neg(T1)
            else:
                T1 = mpf_mul(T1, mpf_pow_int(negx, n-1, wp), wp)
            r = t = to_fixed(x, wp)
            facs = [1]*(n-1)
            for k in range(1,n-1):
                facs[k] = facs[k-1]*k
            facs = facs[::-1]
            s = facs[0] << wp
            for k in range(1, n-1):
                if k & 1:
                    s -= facs[k]*t
                else:
                    s += facs[k]*t
                t = (t*r) >> wp
            T2 = from_man_exp(s, -wp, wp)
            T2 = mpf_mul(T2, mpf_exp(negx, wp))
            if gamma:
                T2 = mpf_mul(T2, mpf_pow_int(x, n_orig, wp), wp)
            R = mpf_add(T1, T2)
            re = mpf_div(R, from_int(ifac(n-1)), prec, rnd)
        else:
            raise NotImplementedError
    if have_imag:
        M = from_int(-ifac(n-1))
        if gamma:
            im = mpf_div(mpf_pi(wp), M, prec, rnd)
            if n_orig & 1:
                im = mpf_neg(im)
        else:
            im = mpf_div(mpf_mul(mpf_pi(wp), mpf_pow_int(negx, n_orig-1, wp), wp), M, prec, rnd)
        return re, im
    else:
        return re, None

def mpf_ci_si_taylor(x, wp, which=0):
    x = to_fixed(x, wp)
    x2 = -(x*x) >> wp
    if which == 0:
        s, t, k = 0, (MPZ_ONE<<wp), 2
    else:
        s, t, k = x, x, 3
    while t:
        t = (t*x2//(k*(k-1)))>>wp
        s += t//k
        k += 2
    return from_man_exp(s, -wp)

def mpc_ci_si_taylor(re, im, wp, which=0):
    if re[1]:
        mag = re[2]+re[3]
    elif im[1]:
        mag = im[2]+im[3]
    if im[1]:
        mag = max(mag, im[2]+im[3])
    if mag > 2 or mag < -wp:
        raise NotImplementedError
    wp += (2-mag)
    zre = to_fixed(re, wp)
    zim = to_fixed(im, wp)
    z2re = (zim*zim-zre*zre)>>wp
    z2im = (-2*zre*zim)>>wp
    tre = zre
    tim = zim
    one = MPZ_ONE<<wp
    if which == 0:
        sre, sim, tre, tim, k = 0, 0, (MPZ_ONE<<wp), 0, 2
    else:
        sre, sim, tre, tim, k = zre, zim, zre, zim, 3
    while max(abs(tre), abs(tim)) > 2:
        f = k*(k-1)
        tre, tim = ((tre*z2re-tim*z2im)//f)>>wp, ((tre*z2im+tim*z2re)//f)>>wp
        sre += tre//k
        sim += tim//k
        k += 2
    return from_man_exp(sre, -wp), from_man_exp(sim, -wp)

def mpf_ci_si(x, prec, rnd=round_fast, which=2):
    wp = prec + 20
    sign, man, exp, bc = x
    ci, si = None, None
    if not man:
        if x == fzero:
            return (fninf, fzero)
        if x == fnan:
            return (x, x)
        ci = fzero
        if which != 0:
            if x == finf:
                si = mpf_shift(mpf_pi(prec, rnd), -1)
            if x == fninf:
                si = mpf_neg(mpf_shift(mpf_pi(prec, negative_rnd[rnd]), -1))
        return (ci, si)
    mag = exp+bc
    if mag < -wp:
        if which != 0:
            si = mpf_perturb(x, 1-sign, prec, rnd)
        if which != 1:
            y = mpf_euler(wp)
            xabs = mpf_abs(x)
            ci = mpf_add(y, mpf_log(xabs, wp), prec, rnd)
        return ci, si
    elif mag > wp:
        if which != 0:
            if sign:
                si = mpf_neg(mpf_pi(prec, negative_rnd[rnd]))
            else:
                si = mpf_pi(prec, rnd)
            si = mpf_shift(si, -1)
        if which != 1:
            ci = mpf_div(mpf_sin(x, wp), x, prec, rnd)
        return ci, si
    else:
        wp += abs(mag)
    asymptotic = mag-1 > math.log(wp, 2)
    if not asymptotic:
        if which != 0:
            si = mpf_pos(mpf_ci_si_taylor(x, wp, 1), prec, rnd)
        if which != 1:
            ci = mpf_ci_si_taylor(x, wp, 0)
            ci = mpf_add(ci, mpf_euler(wp), wp)
            ci = mpf_add(ci, mpf_log(mpf_abs(x), wp), prec, rnd)
        return ci, si
    x = mpf_abs(x)
    xf = to_fixed(x, wp)
    xr = (MPZ_ONE<<(2*wp)) // xf   # 1/x
    s1 = (MPZ_ONE << wp)
    s2 = xr
    t = xr
    k = 2
    while t:
        t = -t
        t = (t*xr*k)>>wp
        k += 1
        s1 += t
        t = (t*xr*k)>>wp
        k += 1
        s2 += t
    s1 = from_man_exp(s1, -wp)
    s2 = from_man_exp(s2, -wp)
    s1 = mpf_div(s1, x, wp)
    s2 = mpf_div(s2, x, wp)
    cos, sin = mpf_cos_sin(x, wp)
    if which != 0:
        si = mpf_add(mpf_mul(cos, s1), mpf_mul(sin, s2), wp)
        si = mpf_sub(mpf_shift(mpf_pi(wp), -1), si, wp)
        if sign:
            si = mpf_neg(si)
        si = mpf_pos(si, prec, rnd)
    if which != 1:
        ci = mpf_sub(mpf_mul(sin, s1), mpf_mul(cos, s2), prec, rnd)
    return ci, si

def mpf_ci(x, prec, rnd=round_fast):
    if mpf_sign(x) < 0:
        raise ComplexResult
    return mpf_ci_si(x, prec, rnd, 0)[0]

def mpf_si(x, prec, rnd=round_fast):
    return mpf_ci_si(x, prec, rnd, 1)[1]

def mpc_ci(z, prec, rnd=round_fast):
    re, im = z
    if im == fzero:
        ci = mpf_ci_si(re, prec, rnd, 0)[0]
        if mpf_sign(re) < 0:
            return (ci, mpf_pi(prec, rnd))
        return (ci, fzero)
    wp = prec + 20
    cre, cim = mpc_ci_si_taylor(re, im, wp, 0)
    cre = mpf_add(cre, mpf_euler(wp), wp)
    ci = mpc_add((cre, cim), mpc_log(z, wp), prec, rnd)
    return ci

def mpc_si(z, prec, rnd=round_fast):
    re, im = z
    if im == fzero:
        return (mpf_ci_si(re, prec, rnd, 1)[1], fzero)
    wp = prec + 20
    z = mpc_ci_si_taylor(re, im, wp, 1)
    return mpc_pos(z, prec, rnd)

def mpf_besseljn(n, x, prec, rounding=round_fast):
    prec += 50
    negate = n < 0 and n & 1
    mag = x[2]+x[3]
    n = abs(n)
    wp = prec + 20 + n*bitcount(n)
    if mag < 0:
        wp -= n*mag
    x = to_fixed(x, wp)
    x2 = (x**2) >> wp
    if not n:
        s = t = MPZ_ONE << wp
    else:
        s = t = (x**n // ifac(n)) >> ((n-1)*wp + n)
    k = 1
    while t:
        t = ((t*x2) // (-4*k*(k+n))) >> wp
        s += t
        k += 1
    if negate:
        s = -s
    return from_man_exp(s, -wp, prec, rounding)

def mpc_besseljn(n, z, prec, rounding=round_fast):
    negate = n < 0 and n & 1
    n = abs(n)
    origprec = prec
    zre, zim = z
    mag = max(zre[2]+zre[3], zim[2]+zim[3])
    prec += 20 + n*bitcount(n) + abs(mag)
    if mag < 0:
        prec -= n*mag
    zre = to_fixed(zre, prec)
    zim = to_fixed(zim, prec)
    z2re = (zre**2 - zim**2) >> prec
    z2im = (zre*zim) >> (prec-1)
    if not n:
        sre = tre = MPZ_ONE << prec
        sim = tim = MPZ_ZERO
    else:
        re, im = complex_int_pow(zre, zim, n)
        sre = tre = (re // ifac(n)) >> ((n-1)*prec + n)
        sim = tim = (im // ifac(n)) >> ((n-1)*prec + n)
    k = 1
    while abs(tre) + abs(tim) > 3:
        p = -4*k*(k+n)
        tre, tim = tre*z2re - tim*z2im, tim*z2re + tre*z2im
        tre = (tre // p) >> prec
        tim = (tim // p) >> prec
        sre += tre
        sim += tim
        k += 1
    if negate:
        sre = -sre
        sim = -sim
    re = from_man_exp(sre, -prec, origprec, rounding)
    im = from_man_exp(sim, -prec, origprec, rounding)
    return (re, im)

def mpf_agm(a, b, prec, rnd=round_fast):
    asign, aman, aexp, abc = a
    bsign, bman, bexp, bbc = b
    if asign or bsign:
        raise ComplexResult("agm of a negative number")
    if not (aman and bman):
        if a == fnan or b == fnan:
            return fnan
        if a == finf:
            if b == fzero:
                return fnan
            return finf
        if b == finf:
            if a == fzero:
                return fnan
            return finf
        return fzero
    wp = prec + 20
    amag = aexp+abc
    bmag = bexp+bbc
    mag_delta = amag - bmag
    abs_mag_delta = abs(mag_delta)
    if abs_mag_delta > 10:
        while abs_mag_delta > 10:
            a, b = mpf_shift(mpf_add(a,b,wp),-1), \
                mpf_sqrt(mpf_mul(a,b,wp),wp)
            abs_mag_delta //= 2
        asign, aman, aexp, abc = a
        bsign, bman, bexp, bbc = b
        amag = aexp+abc
        bmag = bexp+bbc
        mag_delta = amag - bmag
    min_mag = min(amag,bmag)
    max_mag = max(amag,bmag)
    n = 0
    if min_mag < -8:
        n = -min_mag
    elif max_mag > 20:
        n = -max_mag
    if n:
        a = mpf_shift(a, n)
        b = mpf_shift(b, n)
    af = to_fixed(a, wp)
    bf = to_fixed(b, wp)
    g = agm_fixed(af, bf, wp)
    return from_man_exp(g, -wp-n, prec, rnd)

def mpf_agm1(a, prec, rnd=round_fast):
    return mpf_agm(fone, a, prec, rnd)

def mpc_agm(a, b, prec, rnd=round_fast):

    if mpc_is_infnan(a) or mpc_is_infnan(b):
        return fnan, fnan
    if mpc_zero in (a, b):
        return fzero, fzero
    if mpc_neg(a) == b:
        return fzero, fzero
    wp = prec+20
    eps = mpf_shift(fone, -wp+10)
    while 1:
        a1 = mpc_shift(mpc_add(a, b, wp), -1)
        b1 = mpc_sqrt(mpc_mul(a, b, wp), wp)
        a, b = a1, b1
        size = mpf_min_max([mpc_abs(a,10), mpc_abs(b,10)])[1]
        err = mpc_abs(mpc_sub(a, b, 10), 10)
        if size == fzero or mpf_lt(err, mpf_mul(eps, size)):
            return a

def mpc_agm1(a, prec, rnd=round_fast):
    return mpc_agm(mpc_one, a, prec, rnd)

def mpf_ellipk(x, prec, rnd=round_fast):
    if not x[1]:
        if x == fzero:
            return mpf_shift(mpf_pi(prec, rnd), -1)
        if x == fninf:
            return fzero
        if x == fnan:
            return x
    if x == fone:
        return finf
    wp = prec + 15
    a = mpf_sqrt(mpf_sub(fone, x, wp), wp)
    v = mpf_agm1(a, wp)
    r = mpf_div(mpf_pi(wp), v, prec, rnd)
    return mpf_shift(r, -1)

def mpc_ellipk(z, prec, rnd=round_fast):
    re, im = z
    if im == fzero:
        if re == finf:
            return mpc_zero
        if mpf_le(re, fone):
            return mpf_ellipk(re, prec, rnd), fzero
    wp = prec + 15
    a = mpc_sqrt(mpc_sub(mpc_one, z, wp), wp)
    v = mpc_agm1(a, wp)
    r = mpc_mpf_div(mpf_pi(wp), v, prec, rnd)
    return mpc_shift(r, -1)

def mpf_ellipe(x, prec, rnd=round_fast):
    sign, man, exp, bc = x
    if not man:
        if x == fzero:
            return mpf_shift(mpf_pi(prec, rnd), -1)
        if x == fninf:
            return finf
        if x == fnan:
            return x
        if x == finf:
            raise ComplexResult
    if x == fone:
        return fone
    wp = prec+20
    mag = exp+bc
    if mag < -wp:
        return mpf_shift(mpf_pi(prec, rnd), -1)
    p = max(mag, 0) - wp
    h = mpf_shift(fone, p)
    K = mpf_ellipk(x, 2*wp)
    Kh = mpf_ellipk(mpf_sub(x, h), 2*wp)
    Kdiff = mpf_shift(mpf_sub(K, Kh), -p)
    t = mpf_sub(fone, x)
    b = mpf_mul(Kdiff, mpf_shift(x,1), wp)
    return mpf_mul(t, mpf_add(K, b), prec, rnd)

def mpc_ellipe(z, prec, rnd=round_fast):
    re, im = z
    if im == fzero:
        if re == finf:
            return (fzero, finf)
        if mpf_le(re, fone):
            return mpf_ellipe(re, prec, rnd), fzero
    wp = prec + 15
    mag = mpc_abs(z, 1)
    p = max(mag[2]+mag[3], 0) - wp
    h = mpf_shift(fone, p)
    K = mpc_ellipk(z, 2*wp)
    Kh = mpc_ellipk(mpc_add_mpf(z, h, 2*wp), 2*wp)
    Kdiff = mpc_shift(mpc_sub(Kh, K, wp), -p)
    t = mpc_sub(mpc_one, z, wp)
    b = mpc_mul(Kdiff, mpc_shift(z,1), wp)
    return mpc_mul(t, mpc_add(K, b, wp), prec, rnd)

def giant_steps(start, target, n=2):
    L = [target]
    while L[-1] > start*n:
        L = L + [L[-1]//n + 2]
    return L[::-1]

def rshift(x, n):
    if n >= 0: return x >> n
    else:      return x << (-n)

def lshift(x, n):
    if n >= 0: return x << n
    else:      return x >> (-n)

def bin_to_radix(x, xbits, base, bdigits):
    return x*(MPZ(base)**bdigits) >> xbits

def small_numeral(n, base=10, digits=stddigits):
    if base == 10:
        return str(n)
    digs = []
    while n:
        n, digit = divmod(n, base)
        digs.append(digits[digit])
    return "".join(digs[::-1])

def numeral_python(n, base=10, size=0, digits=stddigits):
    if n <= 0:
        if not n:
            return "0"
        return "-" + numeral(-n, base, size, digits)
    if size < 250:
        return small_numeral(n, base, digits)
    half = (size // 2) + (size & 1)
    A, B = divmod(n, base**half)
    ad = numeral(A, base, half, digits)
    bd = numeral(B, base, half, digits).rjust(half, "0")
    return ad + bd

def numeral_gmpy(n, base=10, size=0, digits=stddigits):
    if n < 0:
        return "-" + numeral(-n, base, size, digits)
    if size < 1500000:
        return gmpy.digits(n, base)
    half = (size // 2) + (size & 1)
    A, B = divmod(n, MPZ(base)**half)
    ad = numeral(A, base, half, digits)
    bd = numeral(B, base, half, digits).rjust(half, "0")
    return ad + bd

# if BACKEND == "gmpy":
#     numeral = numeral_gmpy
# else:
#     numeral = numeral_python
numeral = numeral_python

_1_800 = 1<<800
_1_600 = 1<<600
_1_400 = 1<<400
_1_200 = 1<<200
_1_100 = 1<<100
_1_50 = 1<<50

def isqrt_small_python(x):
    if not x:
        return x
    if x < _1_800:
        if x < _1_50:
            return int(x**0.5)
        r = int(x**0.5*1.00000000000001) + 1
    else:
        bc = bitcount(x)
        n = bc//2
        r = int((x>>(2*n-100))**0.5+2)<<(n-50)  # +2 is to round up
    while 1:
        y = (r+x//r)>>1
        if y >= r:
            return r
        r = y

def isqrt_fast_python(x):
    if x < _1_800:
        y = int(x**0.5)
        if x >= _1_100:
            y = (y + x//y) >> 1
            if x >= _1_200:
                y = (y + x//y) >> 1
                if x >= _1_400:
                    y = (y + x//y) >> 1
        return y
    bc = bitcount(x)
    guard_bits = 10
    x <<= 2*guard_bits
    bc += 2*guard_bits
    bc += (bc&1)
    hbc = bc//2
    startprec = min(50, hbc)
    r = int(2.0**(2*startprec)*(x >> (bc-2*startprec)) ** -0.5)
    pp = startprec
    for p in giant_steps(startprec, hbc):
        r2 = (r*r) >> (2*pp - p)
        xr2 = ((x >> (bc-p))*r2) >> p
        r = (r*((3<<p) - xr2)) >> (pp+1)
        pp = p
    return (r*(x>>hbc)) >> (p+guard_bits)

def sqrtrem_python(x):
    if x < _1_600:
        y = isqrt_small_python(x)
        return y, x - y*y
    y = isqrt_fast_python(x) + 1
    rem = x - y*y
    while rem < 0:
        y -= 1
        rem += (1+2*y)
    else:
        if rem:
            while rem > 2*(1+y):
                y += 1
                rem -= (1+2*y)
    return y, rem

def isqrt_python(x):

    return sqrtrem_python(x)[0]

def sqrt_fixed(x, prec):
    return isqrt_fast(x<<prec)

sqrt_fixed2 = sqrt_fixed

# if BACKEND == 'gmpy':
#     if gmpy.version() >= '2':
#         isqrt_small = isqrt_fast = isqrt = gmpy.isqrt
#         sqrtrem = gmpy.isqrt_rem
#     else:
#         isqrt_small = isqrt_fast = isqrt = gmpy.sqrt
#         sqrtrem = gmpy.sqrtrem
# elif BACKEND == 'sage':
#     isqrt_small = isqrt_fast = isqrt = \
#         getattr(sage_utils, "isqrt", lambda n: MPZ(n).isqrt())
#     sqrtrem = lambda n: MPZ(n).sqrtrem()
# else:
#     isqrt_small = isqrt_small_python
#     isqrt_fast = isqrt_fast_python
#     isqrt = isqrt_python
#     sqrtrem = sqrtrem_python

isqrt_small = isqrt_small_python
isqrt_fast = isqrt_fast_python
isqrt = isqrt_python
sqrtrem = sqrtrem_python


def ifib(n, _cache={}):
    if n < 0:
        return (-1)**(-n+1)*ifib(-n)
    if n in _cache:
        return _cache[n]
    m = n
    a, b, p, q = MPZ_ONE, MPZ_ZERO, MPZ_ZERO, MPZ_ONE
    while n:
        if n & 1:
            aq = a*q
            a, b = b*q+aq+a*p, b*p+aq
            n -= 1
        else:
            qq = q*q
            p, q = p*p+qq, qq+2*p*q
            n >>= 1
    if m < 250:
        _cache[m] = b
    return b


def mpc_is_inf(z):
    re, im = z
    if re in _infs: return True
    if im in _infs: return True
    return False

def mpc_is_infnan(z):
    re, im = z
    if re in _infs_nan: return True
    if im in _infs_nan: return True
    return False

def mpc_to_str(z, dps, **kwargs):
    re, im = z
    rs = to_str(re, dps)
    if im[0]:
        return rs + " - " + to_str(mpf_neg(im), dps, **kwargs) + "j"
    else:
        return rs + " + " + to_str(im, dps, **kwargs) + "j"

def mpc_to_complex(z, strict=False, rnd=round_fast):
    re, im = z
    return complex(to_float(re, strict, rnd), to_float(im, strict, rnd))

def mpc_hash(z):
    if sys.version >= "3.2":
        re, im = z
        h = mpf_hash(re) + sys.hash_info.imag*mpf_hash(im)
        h = h % (2**sys.hash_info.width)
        return int(h)
    else:
        try:
            return hash(mpc_to_complex(z, strict=True))
        except OverflowError:
            return hash(z)

def mpc_conjugate(z, prec, rnd=round_fast):
    re, im = z
    return re, mpf_neg(im, prec, rnd)

def mpc_is_nonzero(z):
    return z != mpc_zero

def mpc_add(z, w, prec, rnd=round_fast):
    a, b = z
    c, d = w
    return mpf_add(a, c, prec, rnd), mpf_add(b, d, prec, rnd)

def mpc_add_mpf(z, x, prec, rnd=round_fast):
    a, b = z
    return mpf_add(a, x, prec, rnd), b

def mpc_sub(z, w, prec=0, rnd=round_fast):
    a, b = z
    c, d = w
    return mpf_sub(a, c, prec, rnd), mpf_sub(b, d, prec, rnd)

def mpc_sub_mpf(z, p, prec=0, rnd=round_fast):
    a, b = z
    return mpf_sub(a, p, prec, rnd), b

def mpc_pos(z, prec, rnd=round_fast):
    a, b = z
    return mpf_pos(a, prec, rnd), mpf_pos(b, prec, rnd)

def mpc_neg(z, prec=None, rnd=round_fast):
    a, b = z
    return mpf_neg(a, prec, rnd), mpf_neg(b, prec, rnd)

def mpc_shift(z, n):
    a, b = z
    return mpf_shift(a, n), mpf_shift(b, n)

def mpc_abs(z, prec, rnd=round_fast):
    a, b = z
    return mpf_hypot(a, b, prec, rnd)

def mpc_arg(z, prec, rnd=round_fast):

    a, b = z
    return mpf_atan2(b, a, prec, rnd)

def mpc_floor(z, prec, rnd=round_fast):
    a, b = z
    return mpf_floor(a, prec, rnd), mpf_floor(b, prec, rnd)

def mpc_ceil(z, prec, rnd=round_fast):
    a, b = z
    return mpf_ceil(a, prec, rnd), mpf_ceil(b, prec, rnd)

def mpc_nint(z, prec, rnd=round_fast):
    a, b = z
    return mpf_nint(a, prec, rnd), mpf_nint(b, prec, rnd)

def mpc_frac(z, prec, rnd=round_fast):
    a, b = z
    return mpf_frac(a, prec, rnd), mpf_frac(b, prec, rnd)

def mpc_mul(z, w, prec, rnd=round_fast):
    a, b = z
    c, d = w
    p = mpf_mul(a, c)
    q = mpf_mul(b, d)
    r = mpf_mul(a, d)
    s = mpf_mul(b, c)
    re = mpf_sub(p, q, prec, rnd)
    im = mpf_add(r, s, prec, rnd)
    return re, im

def mpc_square(z, prec, rnd=round_fast):
    a, b = z
    p = mpf_mul(a,a)
    q = mpf_mul(b,b)
    r = mpf_mul(a,b, prec, rnd)
    re = mpf_sub(p, q, prec, rnd)
    im = mpf_shift(r, 1)
    return re, im

def mpc_mul_mpf(z, p, prec, rnd=round_fast):
    a, b = z
    re = mpf_mul(a, p, prec, rnd)
    im = mpf_mul(b, p, prec, rnd)
    return re, im

def mpc_mul_imag_mpf(z, x, prec, rnd=round_fast):
    a, b = z
    re = mpf_neg(mpf_mul(b, x, prec, rnd))
    im = mpf_mul(a, x, prec, rnd)
    return re, im

def mpc_mul_int(z, n, prec, rnd=round_fast):
    a, b = z
    re = mpf_mul_int(a, n, prec, rnd)
    im = mpf_mul_int(b, n, prec, rnd)
    return re, im

def mpc_div(z, w, prec, rnd=round_fast):
    a, b = z
    c, d = w
    wp = prec + 10
    mag = mpf_add(mpf_mul(c, c), mpf_mul(d, d), wp)
    t = mpf_add(mpf_mul(a,c), mpf_mul(b,d), wp)
    u = mpf_sub(mpf_mul(b,c), mpf_mul(a,d), wp)
    return mpf_div(t,mag,prec,rnd), mpf_div(u,mag,prec,rnd)

def mpc_div_mpf(z, p, prec, rnd=round_fast):

    a, b = z
    re = mpf_div(a, p, prec, rnd)
    im = mpf_div(b, p, prec, rnd)
    return re, im

def mpc_reciprocal(z, prec, rnd=round_fast):

    a, b = z
    m = mpf_add(mpf_mul(a,a),mpf_mul(b,b),prec+10)
    re = mpf_div(a, m, prec, rnd)
    im = mpf_neg(mpf_div(b, m, prec, rnd))
    return re, im

def mpc_mpf_div(p, z, prec, rnd=round_fast):

    a, b = z
    m = mpf_add(mpf_mul(a,a),mpf_mul(b,b), prec+10)
    re = mpf_div(mpf_mul(a,p), m, prec, rnd)
    im = mpf_div(mpf_neg(mpf_mul(b,p)), m, prec, rnd)
    return re, im

def complex_int_pow(a, b, n):
    wre = 1
    wim = 0
    while n:
        if n & 1:
            wre, wim = wre*a - wim*b, wim*a + wre*b
            n -= 1
        a, b = a*a - b*b, 2*a*b
        n //= 2
    return wre, wim

def mpc_pow(z, w, prec, rnd=round_fast):
    if w[1] == fzero:
        return mpc_pow_mpf(z, w[0], prec, rnd)
    return mpc_exp(mpc_mul(mpc_log(z, prec+10), w, prec+10), prec, rnd)

def mpc_pow_mpf(z, p, prec, rnd=round_fast):
    psign, pman, pexp, pbc = p
    if pexp >= 0:
        return mpc_pow_int(z, (-1)**psign*(pman<<pexp), prec, rnd)
    if pexp == -1:
        sqrtz = mpc_sqrt(z, prec+10)
        return mpc_pow_int(sqrtz, (-1)**psign*pman, prec, rnd)
    return mpc_exp(mpc_mul_mpf(mpc_log(z, prec+10), p, prec+10), prec, rnd)

def mpc_pow_int(z, n, prec, rnd=round_fast):
    a, b = z
    if b == fzero:
        return mpf_pow_int(a, n, prec, rnd), fzero
    if a == fzero:
        v = mpf_pow_int(b, n, prec, rnd)
        n %= 4
        if n == 0:
            return v, fzero
        elif n == 1:
            return fzero, v
        elif n == 2:
            return mpf_neg(v), fzero
        elif n == 3:
            return fzero, mpf_neg(v)
    if n == 0: return mpc_one
    if n == 1: return mpc_pos(z, prec, rnd)
    if n == 2: return mpc_square(z, prec, rnd)
    if n == -1: return mpc_reciprocal(z, prec, rnd)
    if n < 0: return mpc_reciprocal(mpc_pow_int(z, -n, prec+4), prec, rnd)
    asign, aman, aexp, abc = a
    bsign, bman, bexp, bbc = b
    if asign: aman = -aman
    if bsign: bman = -bman
    de = aexp - bexp
    abs_de = abs(de)
    exact_size = n*(abs_de + max(abc, bbc))
    if exact_size < 10000:
        if de > 0:
            aman <<= de
            aexp = bexp
        else:
            bman <<= (-de)
            bexp = aexp
        re, im = complex_int_pow(aman, bman, n)
        re = from_man_exp(re, int(n*aexp), prec, rnd)
        im = from_man_exp(im, int(n*bexp), prec, rnd)
        return re, im
    return mpc_exp(mpc_mul_int(mpc_log(z, prec+10), n, prec+10), prec, rnd)

def mpc_sqrt(z, prec, rnd=round_fast):
    a, b = z
    if b == fzero:
        if a == fzero:
            return (a, b)
        if a[0]:
            im = mpf_sqrt(mpf_neg(a), prec, rnd)
            return (fzero, im)
        else:
            re = mpf_sqrt(a, prec, rnd)
            return (re, fzero)
    wp = prec+20
    if not a[0]:                               # case a positive
        t  = mpf_add(mpc_abs((a, b), wp), a, wp)  # t = abs(a+bi) + a
        u = mpf_shift(t, -1)                      # u = t/2
        re = mpf_sqrt(u, prec, rnd)               # re = sqrt(u)
        v = mpf_shift(t, 1)                       # v = 2*t
        w  = mpf_sqrt(v, wp)                      # w = sqrt(v)
        im = mpf_div(b, w, prec, rnd)             # im = b / w
    else:                                      # case a negative
        t = mpf_sub(mpc_abs((a, b), wp), a, wp)   # t = abs(a+bi) - a
        u = mpf_shift(t, -1)                      # u = t/2
        im = mpf_sqrt(u, prec, rnd)               # im = sqrt(u)
        v = mpf_shift(t, 1)                       # v = 2*t
        w  = mpf_sqrt(v, wp)                      # w = sqrt(v)
        re = mpf_div(b, w, prec, rnd)             # re = b/w
        if b[0]:
            re = mpf_neg(re)
            im = mpf_neg(im)
    return re, im

def mpc_nthroot_fixed(a, b, n, prec):
    start = 50
    a1 = int(rshift(a, prec - n*start))
    b1 = int(rshift(b, prec - n*start))
    try:
        r = (a1 + 1j*b1)**(1.0/n)
        re = r.real
        im = r.imag
        re = MPZ(int(re))
        im = MPZ(int(im))
    except OverflowError:
        a1 = from_int(a1, start)
        b1 = from_int(b1, start)
        fn = from_int(n)
        nth = mpf_rdiv_int(1, fn, start)
        re, im = mpc_pow((a1, b1), (nth, fzero), start)
        re = to_int(re)
        im = to_int(im)
    extra = 10
    prevp = start
    extra1 = n
    for p in giant_steps(start, prec+extra):
        re2, im2 = complex_int_pow(re, im, n-1)
        re2 = rshift(re2, (n-1)*prevp - p - extra1)
        im2 = rshift(im2, (n-1)*prevp - p - extra1)
        r4 = (re2*re2 + im2*im2) >> (p + extra1)
        ap = rshift(a, prec - p)
        bp = rshift(b, prec - p)
        rec = (ap*re2 + bp*im2) >> p
        imc = (-ap*im2 + bp*re2) >> p
        reb = (rec << p) // r4
        imb = (imc << p) // r4
        re = (reb + (n-1)*lshift(re, p-prevp))//n
        im = (imb + (n-1)*lshift(im, p-prevp))//n
        prevp = p
    return re, im

def mpc_nthroot(z, n, prec, rnd=round_fast):
    a, b = z
    if a[0] == 0 and b == fzero:
        re = mpf_nthroot(a, n, prec, rnd)
        return (re, fzero)
    if n < 2:
        if n == 0:
            return mpc_one
        if n == 1:
            return mpc_pos((a, b), prec, rnd)
        if n == -1:
            return mpc_div(mpc_one, (a, b), prec, rnd)
        inverse = mpc_nthroot((a, b), -n, prec+5, reciprocal_rnd[rnd])
        return mpc_div(mpc_one, inverse, prec, rnd)
    if n <= 20:
        prec2 = int(1.2*(prec + 10))
        asign, aman, aexp, abc = a
        bsign, bman, bexp, bbc = b
        pf = mpc_abs((a,b), prec)
        if pf[-2] + pf[-1] > -10  and pf[-2] + pf[-1] < prec:
            af = to_fixed(a, prec2)
            bf = to_fixed(b, prec2)
            re, im = mpc_nthroot_fixed(af, bf, n, prec2)
            extra = 10
            re = from_man_exp(re, -prec2-extra, prec2, rnd)
            im = from_man_exp(im, -prec2-extra, prec2, rnd)
            return re, im
    fn = from_int(n)
    prec2 = prec+10 + 10
    nth = mpf_rdiv_int(1, fn, prec2)
    re, im = mpc_pow((a, b), (nth, fzero), prec2, rnd)
    re = normalize(re[0], re[1], re[2], re[3], prec, rnd)
    im = normalize(im[0], im[1], im[2], im[3], prec, rnd)
    return re, im

def mpc_cbrt(z, prec, rnd=round_fast):
    return mpc_nthroot(z, 3, prec, rnd)

def mpc_exp(z, prec, rnd=round_fast):

    a, b = z
    if a == fzero:
        return mpf_cos_sin(b, prec, rnd)
    if b == fzero:
        return mpf_exp(a, prec, rnd), fzero
    mag = mpf_exp(a, prec+4, rnd)
    c, s = mpf_cos_sin(b, prec+4, rnd)
    re = mpf_mul(mag, c, prec, rnd)
    im = mpf_mul(mag, s, prec, rnd)
    return re, im

def mpc_log(z, prec, rnd=round_fast):
    re = mpf_log_hypot(z[0], z[1], prec, rnd)
    im = mpc_arg(z, prec, rnd)
    return re, im

def mpc_cos(z, prec, rnd=round_fast):

    a, b = z
    if b == fzero:
        return mpf_cos(a, prec, rnd), fzero
    if a == fzero:
        return mpf_cosh(b, prec, rnd), fzero
    wp = prec + 6
    c, s = mpf_cos_sin(a, wp)
    ch, sh = mpf_cosh_sinh(b, wp)
    re = mpf_mul(c, ch, prec, rnd)
    im = mpf_mul(s, sh, prec, rnd)
    return re, mpf_neg(im)

def mpc_sin(z, prec, rnd=round_fast):
    a, b = z
    if b == fzero:
        return mpf_sin(a, prec, rnd), fzero
    if a == fzero:
        return fzero, mpf_sinh(b, prec, rnd)
    wp = prec + 6
    c, s = mpf_cos_sin(a, wp)
    ch, sh = mpf_cosh_sinh(b, wp)
    re = mpf_mul(s, ch, prec, rnd)
    im = mpf_mul(c, sh, prec, rnd)
    return re, im

def mpc_tan(z, prec, rnd=round_fast):
    a, b = z
    asign, aman, aexp, abc = a
    bsign, bman, bexp, bbc = b
    if b == fzero: return mpf_tan(a, prec, rnd), fzero
    if a == fzero: return fzero, mpf_tanh(b, prec, rnd)
    wp = prec + 15
    a = mpf_shift(a, 1)
    b = mpf_shift(b, 1)
    c, s = mpf_cos_sin(a, wp)
    ch, sh = mpf_cosh_sinh(b, wp)
    mag = mpf_add(c, ch, wp)
    re = mpf_div(s, mag, prec, rnd)
    im = mpf_div(sh, mag, prec, rnd)
    return re, im

def mpc_cos_pi(z, prec, rnd=round_fast):
    a, b = z
    if b == fzero:
        return mpf_cos_pi(a, prec, rnd), fzero
    b = mpf_mul(b, mpf_pi(prec+5), prec+5)
    if a == fzero:
        return mpf_cosh(b, prec, rnd), fzero
    wp = prec + 6
    c, s = mpf_cos_sin_pi(a, wp)
    ch, sh = mpf_cosh_sinh(b, wp)
    re = mpf_mul(c, ch, prec, rnd)
    im = mpf_mul(s, sh, prec, rnd)
    return re, mpf_neg(im)

def mpc_sin_pi(z, prec, rnd=round_fast):
    a, b = z
    if b == fzero:
        return mpf_sin_pi(a, prec, rnd), fzero
    b = mpf_mul(b, mpf_pi(prec+5), prec+5)
    if a == fzero:
        return fzero, mpf_sinh(b, prec, rnd)
    wp = prec + 6
    c, s = mpf_cos_sin_pi(a, wp)
    ch, sh = mpf_cosh_sinh(b, wp)
    re = mpf_mul(s, ch, prec, rnd)
    im = mpf_mul(c, sh, prec, rnd)
    return re, im

def mpc_cos_sin(z, prec, rnd=round_fast):
    a, b = z
    if a == fzero:
        ch, sh = mpf_cosh_sinh(b, prec, rnd)
        return (ch, fzero), (fzero, sh)
    if b == fzero:
        c, s = mpf_cos_sin(a, prec, rnd)
        return (c, fzero), (s, fzero)
    wp = prec + 6
    c, s = mpf_cos_sin(a, wp)
    ch, sh = mpf_cosh_sinh(b, wp)
    cre = mpf_mul(c, ch, prec, rnd)
    cim = mpf_mul(s, sh, prec, rnd)
    sre = mpf_mul(s, ch, prec, rnd)
    sim = mpf_mul(c, sh, prec, rnd)
    return (cre, mpf_neg(cim)), (sre, sim)

def mpc_cos_sin_pi(z, prec, rnd=round_fast):
    a, b = z
    if b == fzero:
        c, s = mpf_cos_sin_pi(a, prec, rnd)
        return (c, fzero), (s, fzero)
    b = mpf_mul(b, mpf_pi(prec+5), prec+5)
    if a == fzero:
        ch, sh = mpf_cosh_sinh(b, prec, rnd)
        return (ch, fzero), (fzero, sh)
    wp = prec + 6
    c, s = mpf_cos_sin_pi(a, wp)
    ch, sh = mpf_cosh_sinh(b, wp)
    cre = mpf_mul(c, ch, prec, rnd)
    cim = mpf_mul(s, sh, prec, rnd)
    sre = mpf_mul(s, ch, prec, rnd)
    sim = mpf_mul(c, sh, prec, rnd)
    return (cre, mpf_neg(cim)), (sre, sim)

def mpc_cosh(z, prec, rnd=round_fast):

    a, b = z
    return mpc_cos((b, mpf_neg(a)), prec, rnd)

def mpc_sinh(z, prec, rnd=round_fast):

    a, b = z
    b, a = mpc_sin((b, a), prec, rnd)
    return a, b

def mpc_tanh(z, prec, rnd=round_fast):

    a, b = z
    b, a = mpc_tan((b, a), prec, rnd)
    return a, b

def mpc_atan(z, prec, rnd=round_fast):
    a, b = z
    wp = prec + 15
    x = mpf_add(fone, b, wp), mpf_neg(a)
    y = mpf_sub(fone, b, wp), a
    l1 = mpc_log(x, wp)
    l2 = mpc_log(y, wp)
    a, b = mpc_sub(l1, l2, prec, rnd)
    v = mpf_neg(mpf_shift(b,-1)), mpf_shift(a,-1)
    if v[1] == fnan and mpc_is_inf(z):
        v = (v[0], fzero)
    return v

beta_crossover = from_float(0.6417)
alpha_crossover = from_float(1.5)

def acos_asin(z, prec, rnd, n):
    a, b = z
    wp = prec + 10
    if b == fzero:
        am = mpf_sub(fone, mpf_abs(a), wp)
        if not am[0]:
            if n == 0:
                return mpf_acos(a, prec, rnd), fzero
            else:
                return mpf_asin(a, prec, rnd), fzero
        else:
            if a[0]:
                pi = mpf_pi(prec, rnd)
                c = mpf_acosh(mpf_neg(a), prec, rnd)
                if n == 0:
                    return pi, mpf_neg(c)
                else:
                    return mpf_neg(mpf_shift(pi, -1)), c
            else:
                c = mpf_acosh(a, prec, rnd)
                if n == 0:
                    return fzero, c
                else:
                    pi = mpf_pi(prec, rnd)
                    return mpf_shift(pi, -1), mpf_neg(c)
    asign = bsign = 0
    if a[0]:
        a = mpf_neg(a)
        asign = 1
    if b[0]:
        b = mpf_neg(b)
        bsign = 1
    am = mpf_sub(fone, a, wp)
    ap = mpf_add(fone, a, wp)
    r = mpf_hypot(ap, b, wp)
    s = mpf_hypot(am, b, wp)
    alpha = mpf_shift(mpf_add(r, s, wp), -1)
    beta = mpf_div(a, alpha, wp)
    b2 = mpf_mul(b,b, wp)
    if not mpf_sub(beta_crossover, beta, wp)[0]:
        if n == 0:
            re = mpf_acos(beta, wp)
        else:
            re = mpf_asin(beta, wp)
    else:
        Ax = mpf_add(alpha, a, wp)
        if not am[0]:

            c = mpf_div(b2, mpf_add(r, ap, wp), wp)
            d = mpf_add(s, am, wp)
            re = mpf_shift(mpf_mul(Ax, mpf_add(c, d, wp), wp), -1)
            if n == 0:
                re = mpf_atan(mpf_div(mpf_sqrt(re, wp), a, wp), wp)
            else:
                re = mpf_atan(mpf_div(a, mpf_sqrt(re, wp), wp), wp)
        else:
            c = mpf_div(Ax, mpf_add(r, ap, wp), wp)
            d = mpf_div(Ax, mpf_sub(s, am, wp), wp)
            re = mpf_shift(mpf_add(c, d, wp), -1)
            re = mpf_mul(b, mpf_sqrt(re, wp), wp)
            if n == 0:
                re = mpf_atan(mpf_div(re, a, wp), wp)
            else:
                re = mpf_atan(mpf_div(a, re, wp), wp)
    if not mpf_sub(alpha_crossover, alpha, wp)[0]:
        c1 = mpf_div(b2, mpf_add(r, ap, wp), wp)
        if mpf_neg(am)[0]:
            c2 = mpf_add(s, am, wp)
            c2 = mpf_div(b2, c2, wp)
            Am1 = mpf_shift(mpf_add(c1, c2, wp), -1)
        else:
            c2 = mpf_sub(s, am, wp)
            Am1 = mpf_shift(mpf_add(c1, c2, wp), -1)
        im = mpf_mul(Am1, mpf_add(alpha, fone, wp), wp)
        im = mpf_log(mpf_add(fone, mpf_add(Am1, mpf_sqrt(im, wp), wp), wp), wp)
    else:
        im = mpf_sqrt(mpf_sub(mpf_mul(alpha, alpha, wp), fone, wp), wp)
        im = mpf_log(mpf_add(alpha, im, wp), wp)
    if asign:
        if n == 0:
            re = mpf_sub(mpf_pi(wp), re, wp)
        else:
            re = mpf_neg(re)
    if not bsign and n == 0:
        im = mpf_neg(im)
    if bsign and n == 1:
        im = mpf_neg(im)
    re = normalize(re[0], re[1], re[2], re[3], prec, rnd)
    im = normalize(im[0], im[1], im[2], im[3], prec, rnd)
    return re, im

def mpc_acos(z, prec, rnd=round_fast):
    return acos_asin(z, prec, rnd, 0)

def mpc_asin(z, prec, rnd=round_fast):
    return acos_asin(z, prec, rnd, 1)

def mpc_asinh(z, prec, rnd=round_fast):
    a, b = z
    a, b =  mpc_asin((b, mpf_neg(a)), prec, rnd)
    return mpf_neg(b), a

def mpc_acosh(z, prec, rnd=round_fast):
    a, b = mpc_acos(z, prec, rnd)
    if b[0] or b == fzero:
        return mpf_neg(b), a
    else:
        return b, mpf_neg(a)

def mpc_atanh(z, prec, rnd=round_fast):
    wp = prec + 15
    a = mpc_add(z, mpc_one, wp)
    b = mpc_sub(mpc_one, z, wp)
    a = mpc_log(a, wp)
    b = mpc_log(b, wp)
    v = mpc_shift(mpc_sub(a, b, wp), -1)

    if v[0] == fnan and mpc_is_inf(z):
        v = (fzero, v[1])
    return v

def mpc_fibonacci(z, prec, rnd=round_fast):
    re, im = z
    if im == fzero:
        return (mpf_fibonacci(re, prec, rnd), fzero)
    size = max(abs(re[2]+re[3]), abs(re[2]+re[3]))
    wp = prec + size + 20
    a = mpf_phi(wp)
    b = mpf_add(mpf_shift(a, 1), fnone, wp)
    u = mpc_pow((a, fzero), z, wp)
    v = mpc_cos_pi(z, wp)
    v = mpc_div(v, u, wp)
    u = mpc_sub(u, v, wp)
    u = mpc_div_mpf(u, b, prec, rnd)
    return u

def mpf_expj(x, prec, rnd='f'):
    raise ComplexResult

def mpc_expj(z, prec, rnd='f'):
    re, im = z
    if im == fzero:
        return mpf_cos_sin(re, prec, rnd)
    if re == fzero:
        return mpf_exp(mpf_neg(im), prec, rnd), fzero
    ey = mpf_exp(mpf_neg(im), prec+10)
    c, s = mpf_cos_sin(re, prec+10)
    re = mpf_mul(ey, c, prec, rnd)
    im = mpf_mul(ey, s, prec, rnd)
    return re, im

def mpf_expjpi(x, prec, rnd='f'):
    raise ComplexResult

def mpc_expjpi(z, prec, rnd='f'):
    re, im = z
    if im == fzero:
        return mpf_cos_sin_pi(re, prec, rnd)
    sign, man, exp, bc = im
    wp = prec+10
    if man:
        wp += max(0, exp+bc)
    im = mpf_neg(mpf_mul(mpf_pi(wp), im, wp))
    if re == fzero:
        return mpf_exp(im, prec, rnd), fzero
    ey = mpf_exp(im, prec+10)
    c, s = mpf_cos_sin_pi(re, prec+10)
    re = mpf_mul(ey, c, prec, rnd)
    im = mpf_mul(ey, s, prec, rnd)
    return re, im

# if BACKEND == 'sage':
#     try:
#         import sage.libs.mpmath.ext_libmp as _lbmp
#         mpc_exp = _lbmp.mpc_exp
#         mpc_sqrt = _lbmp.mpc_sqrt
#     except (ImportError, AttributeError):
#         print("Warning: Sage imports in libmpc failed")

__docformat__ = 'plaintext'
getrandbits = None

# if BACKEND == 'sage':
#     def to_pickable(x):
#         sign, man, exp, bc = x
#         return sign, hex(man), exp, bc
# else:
#     def to_pickable(x):
#         sign, man, exp, bc = x
#         return sign, hex(man)[2:], exp, bc

def to_pickable(x):
    sign, man, exp, bc = x
    return sign, hex(man)[2:], exp, bc

def from_pickable(x):
    sign, man, exp, bc = x
    return (sign, MPZ(man, 16), exp, bc)

def prec_to_dps(n):
    """Return number of accurate decimals that can be represented
    with a precision of n bits."""
    return max(1, int(round(int(n)/3.3219280948873626)-1))

def dps_to_prec(n):
    """Return the number of bits required to represent n decimals
    accurately."""
    return max(1, int(round((int(n)+1)*3.3219280948873626)))

def repr_dps(n):
    dps = prec_to_dps(n)
    if dps == 15:
        return 17
    return dps + 3

int_cache = dict((n, from_man_exp(n, 0)) for n in range(-10, 257))

# if BACKEND == 'gmpy' and '_mpmath_create' in dir(gmpy):
#     from_man_exp = gmpy._mpmath_create

# if BACKEND == 'sage':
#     from_man_exp = sage_utils.from_man_exp


class PythonMPContext(object):
    def __init__(ctx):
        ctx._prec_rounding = [53, round_nearest]
        ctx.mpf = type('mpf', (_mpf,), {})
        ctx.mpc = type('mpc', (_mpc,), {})
        ctx.mpf._ctxdata = [ctx.mpf, new, ctx._prec_rounding]
        ctx.mpc._ctxdata = [ctx.mpc, new, ctx._prec_rounding]
        ctx.mpf.context = ctx
        ctx.mpc.context = ctx
        ctx.constant = type('constant', (_constant,), {})
        ctx.constant._ctxdata = [ctx.mpf, new, ctx._prec_rounding]
        ctx.constant.context = ctx
        ctx.levin = levin
        ctx.cohen_alt = cohen_alt

    def make_mpf(ctx, v):
        a = new(ctx.mpf)
        a._mpf_ = v
        return a

    def make_mpc(ctx, v):
        a = new(ctx.mpc)
        a._mpc_ = v
        return a

    def default(ctx):
        ctx._prec = ctx._prec_rounding[0] = 53
        ctx._dps = 15
        ctx.trap_complex = False

    def _set_prec(ctx, n):
        ctx._prec = ctx._prec_rounding[0] = max(1, int(n))
        ctx._dps = prec_to_dps(n)

    def _set_dps(ctx, n):
        ctx._prec = ctx._prec_rounding[0] = dps_to_prec(n)
        ctx._dps = max(1, int(n))

    prec = property(lambda ctx: ctx._prec, _set_prec)

    dps = property(lambda ctx: ctx._dps, _set_dps)

    def convert(ctx, x, strings=True):
        if type(x) in ctx.types: return x
        if isinstance(x, int_types): return ctx.make_mpf(from_int(x))
        if isinstance(x, float): return ctx.make_mpf(from_float(x))
        if isinstance(x, complex):
            return ctx.make_mpc((from_float(x.real), from_float(x.imag)))
        prec, rounding = ctx._prec_rounding
        if isinstance(x, mpq):
            p, q = x._mpq_
            return ctx.make_mpf(from_rational(p, q, prec))
        if strings and isinstance(x, basestring):
            try:
                _mpf_ = from_str(x, prec, rounding)
                return ctx.make_mpf(_mpf_)
            except ValueError:
                pass
        if hasattr(x, '_mpf_'): return ctx.make_mpf(x._mpf_)
        if hasattr(x, '_mpc_'): return ctx.make_mpc(x._mpc_)
        if hasattr(x, '_mpmath_'):
            return ctx.convert(x._mpmath_(prec, rounding))
        return ctx._convert_fallback(x, strings)

    def isnan(ctx, x):
        if hasattr(x, "_mpf_"):
            return x._mpf_ == fnan
        if hasattr(x, "_mpc_"):
            return fnan in x._mpc_
        if isinstance(x, int_types) or isinstance(x, mpq):
            return False
        x = ctx.convert(x)
        if hasattr(x, '_mpf_') or hasattr(x, '_mpc_'):
            return ctx.isnan(x)
        raise TypeError("isnan() needs a number as input")

    def isinf(ctx, x):
        if hasattr(x, "_mpf_"):
            return x._mpf_ in (finf, fninf)
        if hasattr(x, "_mpc_"):
            re, im = x._mpc_
            return re in (finf, fninf) or im in (finf, fninf)
        if isinstance(x, int_types) or isinstance(x, mpq):
            return False
        x = ctx.convert(x)
        if hasattr(x, '_mpf_') or hasattr(x, '_mpc_'):
            return ctx.isinf(x)
        raise TypeError("isinf() needs a number as input")

    def isnormal(ctx, x):
        if hasattr(x, "_mpf_"):
            return bool(x._mpf_[1])
        if hasattr(x, "_mpc_"):
            re, im = x._mpc_
            re_normal = bool(re[1])
            im_normal = bool(im[1])
            if re == fzero: return im_normal
            if im == fzero: return re_normal
            return re_normal and im_normal
        if isinstance(x, int_types) or isinstance(x, mpq):
            return bool(x)
        x = ctx.convert(x)
        if hasattr(x, '_mpf_') or hasattr(x, '_mpc_'):
            return ctx.isnormal(x)
        raise TypeError("isnormal() needs a number as input")

    def isint(ctx, x, gaussian=False):
        if isinstance(x, int_types):
            return True
        if hasattr(x, "_mpf_"):
            sign, man, exp, bc = xval = x._mpf_
            return bool((man and exp >= 0) or xval == fzero)
        if hasattr(x, "_mpc_"):
            re, im = x._mpc_
            rsign, rman, rexp, rbc = re
            isign, iman, iexp, ibc = im
            re_isint = (rman and rexp >= 0) or re == fzero
            if gaussian:
                im_isint = (iman and iexp >= 0) or im == fzero
                return re_isint and im_isint
            return re_isint and im == fzero
        if isinstance(x, mpq):
            p, q = x._mpq_
            return p % q == 0
        x = ctx.convert(x)
        if hasattr(x, '_mpf_') or hasattr(x, '_mpc_'):
            return ctx.isint(x, gaussian)
        raise TypeError("isint() needs a number as input")

    def fsum(ctx, terms, absolute=False, squared=False):
        prec, rnd = ctx._prec_rounding
        real = []
        imag = []
        other = 0
        for term in terms:
            reval = imval = 0
            if hasattr(term, "_mpf_"):
                reval = term._mpf_
            elif hasattr(term, "_mpc_"):
                reval, imval = term._mpc_
            else:
                term = ctx.convert(term)
                if hasattr(term, "_mpf_"):
                    reval = term._mpf_
                elif hasattr(term, "_mpc_"):
                    reval, imval = term._mpc_
                else:
                    if absolute: term = ctx.absmax(term)
                    if squared: term = term**2
                    other += term
                    continue
            if imval:
                if squared:
                    if absolute:
                        real.append(mpf_mul(reval,reval))
                        real.append(mpf_mul(imval,imval))
                    else:
                        reval, imval = mpc_pow_int((reval,imval),2,prec+10)
                        real.append(reval)
                        imag.append(imval)
                elif absolute:
                    real.append(mpc_abs((reval,imval), prec))
                else:
                    real.append(reval)
                    imag.append(imval)
            else:
                if squared:
                    reval = mpf_mul(reval, reval)
                elif absolute:
                    reval = mpf_abs(reval)
                real.append(reval)
        s = mpf_sum(real, prec, rnd, absolute)
        if imag:
            s = ctx.make_mpc((s, mpf_sum(imag, prec, rnd)))
        else:
            s = ctx.make_mpf(s)
        if other is 0:
            return s
        else:
            return s + other

    def fdot(ctx, A, B=None, conjugate=False):
        if B is not None:
            A = zip(A, B)
        prec, rnd = ctx._prec_rounding
        real = []
        imag = []
        other = 0
        hasattr_ = hasattr
        types = (ctx.mpf, ctx.mpc)
        for a, b in A:
            if type(a) not in types: a = ctx.convert(a)
            if type(b) not in types: b = ctx.convert(b)
            a_real = hasattr_(a, "_mpf_")
            b_real = hasattr_(b, "_mpf_")
            if a_real and b_real:
                real.append(mpf_mul(a._mpf_, b._mpf_))
                continue
            a_complex = hasattr_(a, "_mpc_")
            b_complex = hasattr_(b, "_mpc_")
            if a_real and b_complex:
                aval = a._mpf_
                bre, bim = b._mpc_
                if conjugate:
                    bim = mpf_neg(bim)
                real.append(mpf_mul(aval, bre))
                imag.append(mpf_mul(aval, bim))
            elif b_real and a_complex:
                are, aim = a._mpc_
                bval = b._mpf_
                real.append(mpf_mul(are, bval))
                imag.append(mpf_mul(aim, bval))
            elif a_complex and b_complex:
                are, aim = a._mpc_
                bre, bim = b._mpc_
                if conjugate:
                    bim = mpf_neg(bim)
                real.append(mpf_mul(are, bre))
                real.append(mpf_neg(mpf_mul(aim, bim)))
                imag.append(mpf_mul(are, bim))
                imag.append(mpf_mul(aim, bre))
            else:
                if conjugate:
                    other += a*ctx.conj(b)
                else:
                    other += a*b
        s = mpf_sum(real, prec, rnd)
        if imag:
            s = ctx.make_mpc((s, mpf_sum(imag, prec, rnd)))
        else:
            s = ctx.make_mpf(s)
        if other is 0:
            return s
        else:
            return s + other

    def _wrap_libmp_function(ctx, mpf_f, mpc_f=None, mpi_f=None, doc="<no doc>"):
        def f(x, **kwargs):
            if type(x) not in ctx.types:
                x = ctx.convert(x)
            prec, rounding = ctx._prec_rounding
            if kwargs:
                prec = kwargs.get('prec', prec)
                if 'dps' in kwargs:
                    prec = dps_to_prec(kwargs['dps'])
                rounding = kwargs.get('rounding', rounding)
            if hasattr(x, '_mpf_'):
                try:
                    return ctx.make_mpf(mpf_f(x._mpf_, prec, rounding))
                except ComplexResult:
                    if ctx.trap_complex:
                        raise
                    return ctx.make_mpc(mpc_f((x._mpf_, fzero), prec, rounding))
            elif hasattr(x, '_mpc_'):
                return ctx.make_mpc(mpc_f(x._mpc_, prec, rounding))
            raise NotImplementedError("%s of a %s" % (name, type(x)))
        name = mpf_f.__name__[4:]
        return f

    @classmethod
    def _wrap_specfun(cls, name, f, wrap):
        if wrap:
            def f_wrapped(ctx, *args, **kwargs):
                convert = ctx.convert
                args = [convert(a) for a in args]
                prec = ctx.prec
                try:
                    ctx.prec += 10
                    retval = f(ctx, *args, **kwargs)
                finally:
                    ctx.prec = prec
                return +retval
        else:
            f_wrapped = f
        setattr(cls, name, f_wrapped)

    def _convert_param(ctx, x):
        if hasattr(x, "_mpc_"):
            v, im = x._mpc_
            if im != fzero:
                return x, 'C'
        elif hasattr(x, "_mpf_"):
            v = x._mpf_
        else:
            if type(x) in int_types:
                return int(x), 'Z'
            p = None
            if isinstance(x, tuple):
                p, q = x
            elif hasattr(x, '_mpq_'):
                p, q = x._mpq_
            elif isinstance(x, basestring) and '/' in x:
                p, q = x.split('/')
                p = int(p)
                q = int(q)
            if p is not None:
                if not p % q:
                    return p // q, 'Z'
                return ctx.mpq(p,q), 'Q'
            x = ctx.convert(x)
            if hasattr(x, "_mpc_"):
                v, im = x._mpc_
                if im != fzero:
                    return x, 'C'
            elif hasattr(x, "_mpf_"):
                v = x._mpf_
            else:
                return x, 'U'
        sign, man, exp, bc = v
        if man:
            if exp >= -4:
                if sign:
                    man = -man
                if exp >= 0:
                    return int(man) << exp, 'Z'
                if exp >= -4:
                    p, q = int(man), (1<<(-exp))
                    return ctx.mpq(p,q), 'Q'
            x = ctx.make_mpf(v)
            return x, 'R'
        elif not exp:
            return 0, 'Z'
        else:
            return x, 'U'

    def _mpf_mag(ctx, x):
        sign, man, exp, bc = x
        if man:
            return exp+bc
        if x == fzero:
            return ctx.ninf
        if x == finf or x == fninf:
            return ctx.inf
        return ctx.nan

    def mag(ctx, x):
        if hasattr(x, "_mpf_"):
            return ctx._mpf_mag(x._mpf_)
        elif hasattr(x, "_mpc_"):
            r, i = x._mpc_
            if r == fzero:
                return ctx._mpf_mag(i)
            if i == fzero:
                return ctx._mpf_mag(r)
            return 1+max(ctx._mpf_mag(r), ctx._mpf_mag(i))
        elif isinstance(x, int_types):
            if x:
                return bitcount(abs(x))
            return ctx.ninf
        elif isinstance(x, mpq):
            p, q = x._mpq_
            if p:
                return 1 + bitcount(abs(p)) - bitcount(q)
            return ctx.ninf
        else:
            x = ctx.convert(x)
            if hasattr(x, "_mpf_") or hasattr(x, "_mpc_"):
                return ctx.mag(x)
            else:
                raise TypeError("requires an mpf/mpc")

BaseMPContext = PythonMPContext
mpf_twinprime = def_mpf_constant(twinprime_fixed)
new = object.__new__

# get_complex = re.compile(r'^\(?(?P<re>[\+\-]?\d*\.?\d*(e[\+\-]?\d+)?)??'
#                          r'(?P<im>[\+\-]?\d*\.?\d*(e[\+\-]?\d+)?j)?\)?$')
class MPContext(BaseMPContext, StandardBaseContext):
    def __init__(ctx):
        BaseMPContext.__init__(ctx)
        ctx.trap_complex = False
        ctx.pretty = False
        ctx.types = [ctx.mpf, ctx.mpc, ctx.constant]
        ctx._mpq = mpq
        ctx.default()
        StandardBaseContext.__init__(ctx)
        ctx.mpq = mpq
        ctx.init_builtins()
        ctx.hyp_summators = {}
        ctx._init_aliases()
        ctx.NoConvergence = NoConvergence
        ctx.riemannr = riemannr
        ctx.memoize = memoize
        #ctx.convert = convert

    def init_builtins(ctx):
        mpf = ctx.mpf
        mpc = ctx.mpc
        ctx.one = ctx.make_mpf(fone)
        ctx.zero = ctx.make_mpf(fzero)
        ctx.j = ctx.make_mpc((fzero,fone))
        ctx.inf = ctx.make_mpf(finf)
        ctx.ninf = ctx.make_mpf(fninf)
        ctx.nan = ctx.make_mpf(fnan)
        eps = ctx.constant(lambda prec, rnd: (0, MPZ_ONE, 1-prec, 1), "epsilon of working precision", "eps")
        ctx.eps = eps
        # Approximate constants
        ctx.pi = ctx.constant(mpf_pi, "pi", "pi")
        ctx.ln2 = ctx.constant(mpf_ln2, "ln(2)", "ln2")
        ctx.ln10 = ctx.constant(mpf_ln10, "ln(10)", "ln10")
        ctx.phi = ctx.constant(mpf_phi, "Golden ratio phi", "phi")
        ctx.e = ctx.constant(mpf_e, "e = exp(1)", "e")
        ctx.euler = ctx.constant(mpf_euler, "Euler's constant", "euler")
        ctx.catalan = ctx.constant(mpf_catalan, "Catalan's constant", "catalan")
        ctx.khinchin = ctx.constant(mpf_khinchin, "Khinchin's constant", "khinchin")
        ctx.glaisher = ctx.constant(mpf_glaisher, "Glaisher's constant", "glaisher")
        ctx.apery = ctx.constant(mpf_apery, "Apery's constant", "apery")
        ctx.degree = ctx.constant(mpf_degree, "1 deg = pi / 180", "degree")
        ctx.twinprime = ctx.constant(mpf_twinprime, "Twin prime constant", "twinprime")
        ctx.mertens = ctx.constant(mpf_mertens, "Mertens' constant", "mertens")
        # Standard functions        
        ctx.sqrt = ctx._wrap_libmp_function(mpf_sqrt, mpc_sqrt)
        ctx.cbrt = ctx._wrap_libmp_function(mpf_cbrt, mpc_cbrt)
        ctx.ln = ctx._wrap_libmp_function(mpf_log, mpc_log)
        ctx.atan = ctx._wrap_libmp_function(mpf_atan, mpc_atan)
        ctx.exp = ctx._wrap_libmp_function(mpf_exp, mpc_exp)
        ctx.expj = ctx._wrap_libmp_function(mpf_expj, mpc_expj)
        ctx.expjpi = ctx._wrap_libmp_function(mpf_expjpi, mpc_expjpi)
        ctx.sin = ctx._wrap_libmp_function(mpf_sin, mpc_sin)
        ctx.cos = ctx._wrap_libmp_function(mpf_cos, mpc_cos)
        ctx.tan = ctx._wrap_libmp_function(mpf_tan, mpc_tan)
        ctx.sinh = ctx._wrap_libmp_function(mpf_sinh, mpc_sinh)
        ctx.cosh = ctx._wrap_libmp_function(mpf_cosh, mpc_cosh)
        ctx.tanh = ctx._wrap_libmp_function(mpf_tanh, mpc_tanh)
        ctx.asin = ctx._wrap_libmp_function(mpf_asin, mpc_asin)
        ctx.acos = ctx._wrap_libmp_function(mpf_acos, mpc_acos)
        ctx.atan = ctx._wrap_libmp_function(mpf_atan, mpc_atan)
        ctx.asinh = ctx._wrap_libmp_function(mpf_asinh, mpc_asinh)
        ctx.acosh = ctx._wrap_libmp_function(mpf_acosh, mpc_acosh)
        ctx.atanh = ctx._wrap_libmp_function(mpf_atanh, mpc_atanh)
        ctx.sinpi = ctx._wrap_libmp_function(mpf_sin_pi, mpc_sin_pi)
        ctx.cospi = ctx._wrap_libmp_function(mpf_cos_pi, mpc_cos_pi)
        ctx.floor = ctx._wrap_libmp_function(mpf_floor, mpc_floor)
        ctx.ceil = ctx._wrap_libmp_function(mpf_ceil, mpc_ceil)
        ctx.nint = ctx._wrap_libmp_function(mpf_nint, mpc_nint)
        ctx.frac = ctx._wrap_libmp_function(mpf_frac, mpc_frac)
        ctx.fib = ctx.fibonacci = ctx._wrap_libmp_function(mpf_fibonacci, mpc_fibonacci)
        ctx.gamma = ctx._wrap_libmp_function(mpf_gamma, mpc_gamma)
        ctx.rgamma = ctx._wrap_libmp_function(mpf_rgamma, mpc_rgamma)
        ctx.loggamma = ctx._wrap_libmp_function(mpf_loggamma, mpc_loggamma)
        ctx.fac = ctx.factorial = ctx._wrap_libmp_function(mpf_factorial, mpc_factorial)
        ctx.gamma_old = ctx._wrap_libmp_function(mpf_gamma_old, mpc_gamma_old)
        ctx.fac_old = ctx.factorial_old = ctx._wrap_libmp_function(mpf_factorial_old, mpc_factorial_old)
        ctx.digamma = ctx._wrap_libmp_function(mpf_psi0, mpc_psi0)
        ctx.harmonic = ctx._wrap_libmp_function(mpf_harmonic, mpc_harmonic)
        ctx.ei = ctx._wrap_libmp_function(mpf_ei, mpc_ei)
        ctx.e1 = ctx._wrap_libmp_function(mpf_e1, mpc_e1)
        ctx._ci = ctx._wrap_libmp_function(mpf_ci, mpc_ci)
        ctx._si = ctx._wrap_libmp_function(mpf_si, mpc_si)
        ctx.ellipk = ctx._wrap_libmp_function(mpf_ellipk, mpc_ellipk)
        ctx._ellipe = ctx._wrap_libmp_function(mpf_ellipe, mpc_ellipe)
        ctx.agm1 = ctx._wrap_libmp_function(mpf_agm1, mpc_agm1)
        ctx._erf = ctx._wrap_libmp_function(mpf_erf, None)
        ctx._erfc = ctx._wrap_libmp_function(mpf_erfc, None)
        ctx._zeta = ctx._wrap_libmp_function(mpf_zeta, mpc_zeta)
        ctx._altzeta = ctx._wrap_libmp_function(mpf_altzeta, mpc_altzeta)

    def _convert_fallback(ctx, x, strings):
        if strings and isinstance(x, basestring):
            if 'j' in x.lower():
                x = x.lower().replace(' ', '')
                match = get_complex.match(x)
                re = match.group('re')
                if not re:
                    re = 0
                im = match.group('im').rstrip('j')
                return ctx.mpc(ctx.convert(re), ctx.convert(im))
        if hasattr(x, "_mpi_"):
            ctx.make_mpf(a)
            # a, b = x._mpi_
            # if a == b:
            #     return ctx.make_mpf(a)
            # else:
            #     raise ValueError("can only create mpf from zero-width interval")
        
        raise TypeError("cannot create mpf from " + repr(x))

    def to_fixed(ctx, x, prec):
        return x.to_fixed(prec)

    def hypot(ctx, x, y):
        x = ctx.convert(x)
        y = ctx.convert(y)
        return ctx.make_mpf(mpf_hypot(x._mpf_, y._mpf_, *ctx._prec_rounding))

    def _gamma_upper_int(ctx, n, z):
        n = int(ctx._re(n))
        if n == 0:
            return ctx.e1(z)
        if not hasattr(z, '_mpf_'):
            raise NotImplementedError
        prec, rounding = ctx._prec_rounding
        real, imag = mpf_expint(n, z._mpf_, prec, rounding, gamma=True)
        if imag is None:
            return ctx.make_mpf(real)
        else:
            return ctx.make_mpc((real, imag))

    def _expint_int(ctx, n, z):
        n = int(n)
        if n == 1:
            return ctx.e1(z)
        if not hasattr(z, '_mpf_'):
            raise NotImplementedError
        prec, rounding = ctx._prec_rounding
        real, imag = mpf_expint(n, z._mpf_, prec, rounding)
        if imag is None:
            return ctx.make_mpf(real)
        else:
            return ctx.make_mpc((real, imag))

    def _nthroot(ctx, x, n):
        if hasattr(x, '_mpf_'):
            try:
                return ctx.make_mpf(mpf_nthroot(x._mpf_, n, *ctx._prec_rounding))
            except ComplexResult:
                if ctx.trap_complex:
                    raise
                x = (x._mpf_, fzero)
        else:
            x = x._mpc_
        return ctx.make_mpc(mpc_nthroot(x, n, *ctx._prec_rounding))

    def _besselj(ctx, n, z):
        prec, rounding = ctx._prec_rounding
        if hasattr(z, '_mpf_'):
            return ctx.make_mpf(mpf_besseljn(n, z._mpf_, prec, rounding))
        elif hasattr(z, '_mpc_'):
            return ctx.make_mpc(mpc_besseljn(n, z._mpc_, prec, rounding))

    def _agm(ctx, a, b=1):
        prec, rounding = ctx._prec_rounding
        if hasattr(a, '_mpf_') and hasattr(b, '_mpf_'):
            try:
                v = mpf_agm(a._mpf_, b._mpf_, prec, rounding)
                return ctx.make_mpf(v)
            except ComplexResult:
                pass
        if hasattr(a, '_mpf_'): a = (a._mpf_, fzero)
        else: a = a._mpc_
        if hasattr(b, '_mpf_'): b = (b._mpf_, fzero)
        else: b = b._mpc_
        return ctx.make_mpc(mpc_agm(a, b, prec, rounding))

    def bernoulli(ctx, n):
        return ctx.make_mpf(mpf_bernoulli(int(n), *ctx._prec_rounding))

    def _zeta_int(ctx, n):
        return ctx.make_mpf(mpf_zeta_int(int(n), *ctx._prec_rounding))

    def atan2(ctx, y, x):
        x = ctx.convert(x)
        y = ctx.convert(y)
        return ctx.make_mpf(mpf_atan2(y._mpf_, x._mpf_, *ctx._prec_rounding))

    def psi(ctx, m, z):
        z = ctx.convert(z)
        m = int(m)
        if ctx._is_real_type(z):
            return ctx.make_mpf(mpf_psi(m, z._mpf_, *ctx._prec_rounding))
        else:
            return ctx.make_mpc(mpc_psi(m, z._mpc_, *ctx._prec_rounding))

    def cos_sin(ctx, x, **kwargs):
        if type(x) not in ctx.types:
            x = ctx.convert(x)
        prec, rounding = ctx._parse_prec(kwargs)
        if hasattr(x, '_mpf_'):
            c, s = mpf_cos_sin(x._mpf_, prec, rounding)
            return ctx.make_mpf(c), ctx.make_mpf(s)
        elif hasattr(x, '_mpc_'):
            c, s = mpc_cos_sin(x._mpc_, prec, rounding)
            return ctx.make_mpc(c), ctx.make_mpc(s)
        else:
            return ctx.cos(x, **kwargs), ctx.sin(x, **kwargs)

    def cospi_sinpi(ctx, x, **kwargs):
        if type(x) not in ctx.types:
            x = ctx.convert(x)
        prec, rounding = ctx._parse_prec(kwargs)
        if hasattr(x, '_mpf_'):
            c, s = mpf_cos_sin_pi(x._mpf_, prec, rounding)
            return ctx.make_mpf(c), ctx.make_mpf(s)
        elif hasattr(x, '_mpc_'):
            c, s = mpc_cos_sin_pi(x._mpc_, prec, rounding)
            return ctx.make_mpc(c), ctx.make_mpc(s)
        else:
            return ctx.cos(x, **kwargs), ctx.sin(x, **kwargs)

    def clone(ctx):
        a = ctx.__class__()
        a.prec = ctx.prec
        return a
    def _is_real_type(ctx, x):
        if hasattr(x, '_mpc_') or type(x) is complex:
            return False
        return True

    def _is_complex_type(ctx, x):
        if hasattr(x, '_mpc_') or type(x) is complex:
            return True
        return False

    def isnan(ctx, x):
        if hasattr(x, "_mpf_"):
            return x._mpf_ == fnan
        if hasattr(x, "_mpc_"):
            return fnan in x._mpc_
        if isinstance(x, int_types) or isinstance(x, mpq):
            return False
        x = ctx.convert(x)
        if hasattr(x, '_mpf_') or hasattr(x, '_mpc_'):
            return ctx.isnan(x)
        raise TypeError("isnan() needs a number as input")

    def isfinite(ctx, x):
        if ctx.isinf(x) or ctx.isnan(x):
            return False
        return True

    def isnpint(ctx, x):
        if not x:
            return True
        if hasattr(x, '_mpf_'):
            sign, man, exp, bc = x._mpf_
            return sign and exp >= 0
        if hasattr(x, '_mpc_'):
            return not x.imag and ctx.isnpint(x.real)
        if type(x) in int_types:
            return x <= 0
        if isinstance(x, ctx.mpq):
            p, q = x._mpq_
            if not p:
                return True
            return q == 1 and p <= 0
        return ctx.isnpint(ctx.convert(x))

    def __str__(ctx):
        lines = ["Mpmath settings:",
            ("  mp.prec = %s" % ctx.prec).ljust(30) + "[default: 53]",
            ("  mp.dps = %s" % ctx.dps).ljust(30) + "[default: 15]",
            ("  mp.trap_complex = %s" % ctx.trap_complex).ljust(30) + "[default: False]",
        ]
        return "\n".join(lines)

    @property
    def _repr_digits(ctx):
        return repr_dps(ctx._prec)

    @property
    def _str_digits(ctx):
        return ctx._dps

    def extraprec(ctx, n, normalize_output=False):
        return PrecisionManager(ctx, lambda p: p + n, None, normalize_output)

    def extradps(ctx, n, normalize_output=False):
        return PrecisionManager(ctx, None, lambda d: d + n, normalize_output)

    def workprec(ctx, n, normalize_output=False):
        return PrecisionManager(ctx, lambda p: n, None, normalize_output)

    def workdps(ctx, n, normalize_output=False):
        return PrecisionManager(ctx, None, lambda d: n, normalize_output)

    def autoprec(ctx, f, maxprec=None, catch=(), verbose=False):
        def f_autoprec_wrapped(*args, **kwargs):
            prec = ctx.prec
            if maxprec is None:
                maxprec2 = ctx._default_hyper_maxprec(prec)
            else:
                maxprec2 = maxprec
            try:
                ctx.prec = prec + 10
                try:
                    v1 = f(*args, **kwargs)
                except catch:
                    v1 = ctx.nan
                prec2 = prec + 20
                while 1:
                    ctx.prec = prec2
                    try:
                        v2 = f(*args, **kwargs)
                    except catch:
                        v2 = ctx.nan
                    if v1 == v2:
                        break
                    err = ctx.mag(v2-v1) - ctx.mag(v2)
                    if err < (-prec):
                        break
                    if verbose:
                        print("autoprec: target=%s, prec=%s, accuracy=%s" \
                            % (prec, prec2, -err))
                    v1 = v2
                    if prec2 >= maxprec2:
                        raise ctx.NoConvergence(\
                        "autoprec: prec increased to %i without convergence"\
                        % prec2)
                    prec2 += int(prec2*2)
                    prec2 = min(prec2, maxprec2)
            finally:
                ctx.prec = prec
            return +v2
        return f_autoprec_wrapped

    def nstr(ctx, x, n=6, **kwargs):
        if isinstance(x, list):
            return "[%s]" % (", ".join(ctx.nstr(c, n, **kwargs) for c in x))
        if isinstance(x, tuple):
            return "(%s)" % (", ".join(ctx.nstr(c, n, **kwargs) for c in x))
        if hasattr(x, '_mpf_'):
            return to_str(x._mpf_, n, **kwargs)
        if hasattr(x, '_mpc_'):
            return "(" + mpc_to_str(x._mpc_, n, **kwargs)  + ")"
        if isinstance(x, basestring):
            return repr(x)
        if isinstance(x, ctx.matrix):
            return x.__nstr__(n, **kwargs)
        return str(x)

    def _parse_prec(ctx, kwargs):
        if kwargs:
            if kwargs.get('exact'):
                return 0, 'f'
            prec, rounding = ctx._prec_rounding
            if 'rounding' in kwargs:
                rounding = kwargs['rounding']
            if 'prec' in kwargs:
                prec = kwargs['prec']
                if prec == ctx.inf:
                    return 0, 'f'
                else:
                    prec = int(prec)
            elif 'dps' in kwargs:
                dps = kwargs['dps']
                if dps == ctx.inf:
                    return 0, 'f'
                prec = dps_to_prec(dps)
            return prec, rounding
        return ctx._prec_rounding

    _exact_overflow_msg = "the exact result does not fit in memory"

    _hypsum_msg = """hypsum() failed to converge to the requested %i bits of accuracy
using a working precision of %i bits. Try with a higher maxprec,
maxterms, or set zeroprec."""

    def hypsum(ctx, p, q, flags, coeffs, z, accurate_small=True, **kwargs):
        if hasattr(z, "_mpf_"):
            key = p, q, flags, 'R'
            v = z._mpf_
        elif hasattr(z, "_mpc_"):
            key = p, q, flags, 'C'
            v = z._mpc_
        if key not in ctx.hyp_summators:
            ctx.hyp_summators[key] = make_hyp_summator(key)[1]
        summator = ctx.hyp_summators[key]
        prec = ctx.prec
        maxprec = kwargs.get('maxprec', ctx._default_hyper_maxprec(prec))
        extraprec = 50
        epsshift = 25
        magnitude_check = {}
        max_total_jump = 0
        for i, c in enumerate(coeffs):
            if flags[i] == 'Z':
                if i >= p and c <= 0:
                    ok = False
                    for ii, cc in enumerate(coeffs[:p]):
                        if flags[ii] == 'Z' and cc <= 0 and c <= cc:
                            ok = True
                    if not ok:
                        raise ZeroDivisionError("pole in hypergeometric series")
                continue
            n, d = ctx.nint_distance(c)
            n = -int(n)
            d = -d
            if i >= p and n >= 0 and d > 4:
                if n in magnitude_check:
                    magnitude_check[n] += d
                else:
                    magnitude_check[n] = d
                extraprec = max(extraprec, d - prec + 60)
            max_total_jump += abs(d)
        while 1:
            if extraprec > maxprec:
                raise ValueError(ctx._hypsum_msg % (prec, prec+extraprec))
            wp = prec + extraprec
            if magnitude_check:
                mag_dict = dict((n,None) for n in magnitude_check)
            else:
                mag_dict = {}
            zv, have_complex, magnitude = summator(coeffs, v, prec, wp, \
                epsshift, mag_dict, **kwargs)
            cancel = -magnitude
            jumps_resolved = True
            if extraprec < max_total_jump:
                for n in mag_dict.values():
                    if (n is None) or (n < prec):
                        jumps_resolved = False
                        break
            accurate = (cancel < extraprec-25-5 or not accurate_small)
            if jumps_resolved:
                if accurate:
                    break
                zeroprec = kwargs.get('zeroprec')
                if zeroprec is not None:
                    if cancel > zeroprec:
                        if have_complex:
                            return ctx.mpc(0)
                        else:
                            return ctx.zero
            extraprec *= 2
            epsshift += 5
            extraprec += 5

        if type(zv) is tuple:
            if have_complex:
                return ctx.make_mpc(zv)
            else:
                return ctx.make_mpf(zv)
        else:
            return zv

    def ldexp(ctx, x, n):
        x = ctx.convert(x)
        return ctx.make_mpf(mpf_shift(x._mpf_, n))

    def frexp(ctx, x):
        x = ctx.convert(x)
        y, n = mpf_frexp(x._mpf_)
        return ctx.make_mpf(y), n

    def fneg(ctx, x, **kwargs):
        prec, rounding = ctx._parse_prec(kwargs)
        x = ctx.convert(x)
        if hasattr(x, '_mpf_'):
            return ctx.make_mpf(mpf_neg(x._mpf_, prec, rounding))
        if hasattr(x, '_mpc_'):
            return ctx.make_mpc(mpc_neg(x._mpc_, prec, rounding))
        raise ValueError("Arguments need to be mpf or mpc compatible numbers")

    def fadd(ctx, x, y, **kwargs):
        prec, rounding = ctx._parse_prec(kwargs)
        x = ctx.convert(x)
        y = ctx.convert(y)
        try:
            if hasattr(x, '_mpf_'):
                if hasattr(y, '_mpf_'):
                    return ctx.make_mpf(mpf_add(x._mpf_, y._mpf_, prec, rounding))
                if hasattr(y, '_mpc_'):
                    return ctx.make_mpc(mpc_add_mpf(y._mpc_, x._mpf_, prec, rounding))
            if hasattr(x, '_mpc_'):
                if hasattr(y, '_mpf_'):
                    return ctx.make_mpc(mpc_add_mpf(x._mpc_, y._mpf_, prec, rounding))
                if hasattr(y, '_mpc_'):
                    return ctx.make_mpc(mpc_add(x._mpc_, y._mpc_, prec, rounding))
        except (ValueError, OverflowError):
            raise OverflowError(ctx._exact_overflow_msg)
        raise ValueError("Arguments need to be mpf or mpc compatible numbers")

    def fsub(ctx, x, y, **kwargs):
        prec, rounding = ctx._parse_prec(kwargs)
        x = ctx.convert(x)
        y = ctx.convert(y)
        try:
            if hasattr(x, '_mpf_'):
                if hasattr(y, '_mpf_'):
                    return ctx.make_mpf(mpf_sub(x._mpf_, y._mpf_, prec, rounding))
                if hasattr(y, '_mpc_'):
                    return ctx.make_mpc(mpc_sub((x._mpf_, fzero), y._mpc_, prec, rounding))
            if hasattr(x, '_mpc_'):
                if hasattr(y, '_mpf_'):
                    return ctx.make_mpc(mpc_sub_mpf(x._mpc_, y._mpf_, prec, rounding))
                if hasattr(y, '_mpc_'):
                    return ctx.make_mpc(mpc_sub(x._mpc_, y._mpc_, prec, rounding))
        except (ValueError, OverflowError):
            raise OverflowError(ctx._exact_overflow_msg)
        raise ValueError("Arguments need to be mpf or mpc compatible numbers")

    def fmul(ctx, x, y, **kwargs):
        prec, rounding = ctx._parse_prec(kwargs)
        x = ctx.convert(x)
        y = ctx.convert(y)
        try:
            if hasattr(x, '_mpf_'):
                if hasattr(y, '_mpf_'):
                    return ctx.make_mpf(mpf_mul(x._mpf_, y._mpf_, prec, rounding))
                if hasattr(y, '_mpc_'):
                    return ctx.make_mpc(mpc_mul_mpf(y._mpc_, x._mpf_, prec, rounding))
            if hasattr(x, '_mpc_'):
                if hasattr(y, '_mpf_'):
                    return ctx.make_mpc(mpc_mul_mpf(x._mpc_, y._mpf_, prec, rounding))
                if hasattr(y, '_mpc_'):
                    return ctx.make_mpc(mpc_mul(x._mpc_, y._mpc_, prec, rounding))
        except (ValueError, OverflowError):
            raise OverflowError(ctx._exact_overflow_msg)
        raise ValueError("Arguments need to be mpf or mpc compatible numbers")

    def fdiv(ctx, x, y, **kwargs):
        prec, rounding = ctx._parse_prec(kwargs)
        if not prec:
            raise ValueError("division is not an exact operation")
        x = ctx.convert(x)
        y = ctx.convert(y)
        if hasattr(x, '_mpf_'):
            if hasattr(y, '_mpf_'):
                return ctx.make_mpf(mpf_div(x._mpf_, y._mpf_, prec, rounding))
            if hasattr(y, '_mpc_'):
                return ctx.make_mpc(mpc_div((x._mpf_, fzero), y._mpc_, prec, rounding))
        if hasattr(x, '_mpc_'):
            if hasattr(y, '_mpf_'):
                return ctx.make_mpc(mpc_div_mpf(x._mpc_, y._mpf_, prec, rounding))
            if hasattr(y, '_mpc_'):
                return ctx.make_mpc(mpc_div(x._mpc_, y._mpc_, prec, rounding))
        raise ValueError("Arguments need to be mpf or mpc compatible numbers")

    def nint_distance(ctx, x):
        typx = type(x)
        if typx in int_types:
            return int(x), ctx.ninf
        elif typx is mpq:
            p, q = x._mpq_
            n, r = divmod(p, q)
            if 2*r >= q:
                n += 1
            elif not r:
                return n, ctx.ninf
            d = bitcount(abs(p-n*q)) - bitcount(q)
            return n, d
        if hasattr(x, "_mpf_"):
            re = x._mpf_
            im_dist = ctx.ninf
        elif hasattr(x, "_mpc_"):
            re, im = x._mpc_
            isign, iman, iexp, ibc = im
            if iman:
                im_dist = iexp + ibc
            elif im == fzero:
                im_dist = ctx.ninf
            else:
                raise ValueError("requires a finite number")
        else:
            x = ctx.convert(x)
            if hasattr(x, "_mpf_") or hasattr(x, "_mpc_"):
                return ctx.nint_distance(x)
            else:
                raise TypeError("requires an mpf/mpc")
        sign, man, exp, bc = re
        mag = exp+bc
        if mag < 0:
            n = 0
            re_dist = mag
        elif man:
            if exp >= 0:
                n = man << exp
                re_dist = ctx.ninf
            elif exp == -1:
                n = (man>>1)+1
                re_dist = 0
            else:
                d = (-exp-1)
                t = man >> d
                if t & 1:
                    t += 1
                    man = (t<<d) - man
                else:
                    man -= (t<<d)
                n = t>>1
                re_dist = exp+bitcount(man)
            if sign:
                n = -n
        elif re == fzero:
            re_dist = ctx.ninf
            n = 0
        else:
            raise ValueError("requires a finite number")
        return n, max(re_dist, im_dist)

    def fprod(ctx, factors):
        orig = ctx.prec
        try:
            v = ctx.one
            for p in factors:
                v *= p
        finally:
            ctx.prec = orig
        return +v

    def rand(ctx):
        return ctx.make_mpf(mpf_rand(ctx._prec))

    def fraction(ctx, p, q):
        return ctx.constant(lambda prec, rnd: from_rational(p, q, prec, rnd),'%s/%s' % (p, q))

    def absmin(ctx, x):
        return abs(ctx.convert(x))

    def absmax(ctx, x):
        return abs(ctx.convert(x))

    def _as_points(ctx, x):
        if hasattr(x, '_mpi_'):
            a, b = x._mpi_
            return [ctx.make_mpf(a), ctx.make_mpf(b)]
        return x

    def _zetasum_fast(ctx, s, a, n, derivatives=[0], reflect=False):
        if not (ctx.isint(a) and hasattr(s, "_mpc_")):
            raise NotImplementedError
        a = int(a)
        prec = ctx._prec
        xs, ys = mpc_zetasum(s._mpc_, a, n, derivatives, reflect, prec)
        xs = [ctx.make_mpc(x) for x in xs]
        ys = [ctx.make_mpc(y) for y in ys]
        return xs, ys

    def __sub__(s, t):
        ttype = type(t)
        if ttype is mpq:
            a, b = s._mpq_
            c, d = t._mpq_
            return create_reduced(a*d-b*c, b*d)
        if ttype in int_types:
            a, b = s._mpq_
            v = new(mpq)
            v._mpq_ = a-b*t, b
            return v
        return NotImplemented

    def __rsub__(s, t):
        ttype = type(t)
        if ttype is mpq:
            a, b = s._mpq_
            c, d = t._mpq_
            return create_reduced(b*c-a*d, b*d)
        if ttype in int_types:
            a, b = s._mpq_
            v = new(mpq)
            v._mpq_ = b*t-a, b
            return v
        return NotImplemented

    def __mul__(s, t):
        ttype = type(t)
        if ttype is mpq:
            a, b = s._mpq_
            c, d = t._mpq_
            return create_reduced(a*c, b*d)
        if ttype in int_types:
            a, b = s._mpq_
            return create_reduced(a*t, b)
        return NotImplemented

    __rmul__ = __mul__

    def __div__(s, t):
        ttype = type(t)
        if ttype is mpq:
            a, b = s._mpq_
            c, d = t._mpq_
            return create_reduced(a*d, b*c)
        if ttype in int_types:
            a, b = s._mpq_
            return create_reduced(a, b*t)
        return NotImplemented

    def __rdiv__(s, t):
        ttype = type(t)
        if ttype is mpq:
            a, b = s._mpq_
            c, d = t._mpq_
            return create_reduced(b*c, a*d)
        if ttype in int_types:
            a, b = s._mpq_
            return create_reduced(b*t, a)
        return NotImplemented

    def __pow__(s, t):
        ttype = type(t)
        if ttype in int_types:
            a, b = s._mpq_
            if t:
                if t < 0:
                    a, b, t = b, a, -t
                v = new(mpq)
                v._mpq_ = a**t, b**t
                return v
            raise ZeroDivisionError
        return NotImplemented

    @classmethod
    def _wrap_specfun(cls, name, f, wrap):
        if wrap:
            def f_wrapped(ctx, *args, **kwargs):
                convert = ctx.convert
                args = [convert(a) for a in args]
                prec = ctx.prec
                try:
                    ctx.prec += 10
                    retval = f(ctx, *args, **kwargs)
                finally:
                    ctx.prec = prec
                return +retval
        else:
            f_wrapped = f
        setattr(cls, name, f_wrapped)

    def isnan(ctx, x):
        if hasattr(x, "_mpf_"):
            return x._mpf_ == fnan
        if hasattr(x, "_mpc_"):
            return fnan in x._mpc_
        if isinstance(x, int_types) or isinstance(x, mpq):
            return False
        x = ctx.convert(x)
        if hasattr(x, '_mpf_') or hasattr(x, '_mpc_'):
            return ctx.isnan(x)
        raise TypeError("isnan() needs a number as input")

    def isinf(ctx, x):
        if hasattr(x, "_mpf_"):
            return x._mpf_ in (finf, fninf)
        if hasattr(x, "_mpc_"):
            re, im = x._mpc_
            return re in (finf, fninf) or im in (finf, fninf)
        if isinstance(x, int_types) or isinstance(x, mpq):
            return False
        x = ctx.convert(x)
        if hasattr(x, '_mpf_') or hasattr(x, '_mpc_'):
            return ctx.isinf(x)
        raise TypeError("isinf() needs a number as input")

    def isnormal(ctx, x):
        if hasattr(x, "_mpf_"):
            return bool(x._mpf_[1])
        if hasattr(x, "_mpc_"):
            re, im = x._mpc_
            re_normal = bool(re[1])
            im_normal = bool(im[1])
            if re == fzero: return im_normal
            if im == fzero: return re_normal
            return re_normal and im_normal
        if isinstance(x, int_types) or isinstance(x, mpq):
            return bool(x)
        x = ctx.convert(x)
        if hasattr(x, '_mpf_') or hasattr(x, '_mpc_'):
            return ctx.isnormal(x)
        raise TypeError("isnormal() needs a number as input")

    def isint(ctx, x, gaussian=False):
        if isinstance(x, int_types):
            return True
        if hasattr(x, "_mpf_"):
            sign, man, exp, bc = xval = x._mpf_
            return bool((man and exp >= 0) or xval == fzero)
        if hasattr(x, "_mpc_"):
            re, im = x._mpc_
            rsign, rman, rexp, rbc = re
            isign, iman, iexp, ibc = im
            re_isint = (rman and rexp >= 0) or re == fzero
            if gaussian:
                im_isint = (iman and iexp >= 0) or im == fzero
                return re_isint and im_isint
            return re_isint and im == fzero
        if isinstance(x, mpq):
            p, q = x._mpq_
            return p % q == 0
        x = ctx.convert(x)
        if hasattr(x, '_mpf_') or hasattr(x, '_mpc_'):
            return ctx.isint(x, gaussian)
        raise TypeError("isint() needs a number as input")

    def fsum(ctx, terms, absolute=False, squared=False):
        prec, rnd = ctx._prec_rounding
        real = []
        imag = []
        other = 0
        for term in terms:
            reval = imval = 0
            if hasattr(term, "_mpf_"):
                reval = term._mpf_
            elif hasattr(term, "_mpc_"):
                reval, imval = term._mpc_
            else:
                term = ctx.convert(term)
                if hasattr(term, "_mpf_"):
                    reval = term._mpf_
                elif hasattr(term, "_mpc_"):
                    reval, imval = term._mpc_
                else:
                    if absolute: term = ctx.absmax(term)
                    if squared: term = term**2
                    other += term
                    continue
            if imval:
                if squared:
                    if absolute:
                        real.append(mpf_mul(reval,reval))
                        real.append(mpf_mul(imval,imval))
                    else:
                        reval, imval = mpc_pow_int((reval,imval),2,prec+10)
                        real.append(reval)
                        imag.append(imval)
                elif absolute:
                    real.append(mpc_abs((reval,imval), prec))
                else:
                    real.append(reval)
                    imag.append(imval)
            else:
                if squared:
                    reval = mpf_mul(reval, reval)
                elif absolute:
                    reval = mpf_abs(reval)
                real.append(reval)
        s = mpf_sum(real, prec, rnd, absolute)
        if imag:
            s = ctx.make_mpc((s, mpf_sum(imag, prec, rnd)))
        else:
            s = ctx.make_mpf(s)
        if other is 0:
            return s
        else:
            return s + other

    def fdot(ctx, A, B=None, conjugate=False):
        if B:
            A = zip(A, B)
        prec, rnd = ctx._prec_rounding
        real = []
        imag = []
        other = 0
        hasattr_ = hasattr
        types = (ctx.mpf, ctx.mpc)
        for a, b in A:
            if type(a) not in types: a = ctx.convert(a)
            if type(b) not in types: b = ctx.convert(b)
            a_real = hasattr_(a, "_mpf_")
            b_real = hasattr_(b, "_mpf_")
            if a_real and b_real:
                real.append(mpf_mul(a._mpf_, b._mpf_))
                continue
            a_complex = hasattr_(a, "_mpc_")
            b_complex = hasattr_(b, "_mpc_")
            if a_real and b_complex:
                aval = a._mpf_
                bre, bim = b._mpc_
                if conjugate:
                    bim = mpf_neg(bim)
                real.append(mpf_mul(aval, bre))
                imag.append(mpf_mul(aval, bim))
            elif b_real and a_complex:
                are, aim = a._mpc_
                bval = b._mpf_
                real.append(mpf_mul(are, bval))
                imag.append(mpf_mul(aim, bval))
            elif a_complex and b_complex:
                are, aim = a._mpc_
                bre, bim = b._mpc_
                if conjugate:
                    bim = mpf_neg(bim)
                real.append(mpf_mul(are, bre))
                real.append(mpf_neg(mpf_mul(aim, bim)))
                imag.append(mpf_mul(are, bim))
                imag.append(mpf_mul(aim, bre))
            else:
                if conjugate:
                    other += a*ctx.conj(b)
                else:
                    other += a*b
        s = mpf_sum(real, prec, rnd)
        if imag:
            s = ctx.make_mpc((s, mpf_sum(imag, prec, rnd)))
        else:
            s = ctx.make_mpf(s)
        if other is 0:
            return s
        else:
            return s + other

    def _convert_param(ctx, x):
        if hasattr(x, "_mpc_"):
            v, im = x._mpc_
            if im != fzero:
                return x, 'C'
        elif hasattr(x, "_mpf_"):
            v = x._mpf_
        else:
            if type(x) in int_types:
                return int(x), 'Z'
            p = None
            if isinstance(x, tuple):
                p, q = x
            elif hasattr(x, '_mpq_'):
                p, q = x._mpq_
            elif isinstance(x, basestring) and '/' in x:
                p, q = x.split('/')
                p = int(p)
                q = int(q)
            if p is not None:
                if not p % q:
                    return p // q, 'Z'
                return ctx.mpq(p,q), 'Q'
            x = ctx.convert(x)
            if hasattr(x, "_mpc_"):
                v, im = x._mpc_
                if im != fzero:
                    return x, 'C'
            elif hasattr(x, "_mpf_"):
                v = x._mpf_
            else:
                return x, 'U'
        sign, man, exp, bc = v
        if man:
            if exp >= -4:
                if sign:
                    man = -man
                if exp >= 0:
                    return int(man) << exp, 'Z'
                if exp >= -4:
                    p, q = int(man), (1<<(-exp))
                    return ctx.mpq(p,q), 'Q'
            x = ctx.make_mpf(v)
            return x, 'R'
        elif not exp:
            return 0, 'Z'
        else:
            return x, 'U'

    def _mpf_mag(ctx, x):
        sign, man, exp, bc = x
        if man:
            return exp+bc
        if x == fzero:
            return ctx.ninf
        if x == finf or x == fninf:
            return ctx.inf
        return ctx.nan

    def mag(ctx, x):
        if hasattr(x, "_mpf_"):
            return ctx._mpf_mag(x._mpf_)
        elif hasattr(x, "_mpc_"):
            r, i = x._mpc_
            if r == fzero:
                return ctx._mpf_mag(i)
            if i == fzero:
                return ctx._mpf_mag(r)
            return 1+max(ctx._mpf_mag(r), ctx._mpf_mag(i))
        elif isinstance(x, int_types):
            if x:
                return bitcount(abs(x))
            return ctx.ninf
        elif isinstance(x, mpq):
            p, q = x._mpq_
            if p:
                return 1 + bitcount(abs(p)) - bitcount(q)
            return ctx.ninf
        else:
            x = ctx.convert(x)
            if hasattr(x, "_mpf_") or hasattr(x, "_mpc_"):
                return ctx.mag(x)
            else:
                raise TypeError("requires an mpf/mpc")


#_constant(twinprime_fixed)

spouge_cache = {}

def calc_spouge_coefficients(a, prec):
    wp = prec + int(a*1.4)
    c = [0]*a
    b = mpf_exp(from_int(a-1), wp)
    e = mpf_exp(fone, wp)
    sq2pi = mpf_sqrt(mpf_shift(mpf_pi(wp), 1), wp)
    c[0] = to_fixed(sq2pi, prec)
    for k in range(1, a):
        term = mpf_mul_int(b, ((-1)**(k-1)*(a-k)**k), wp)
        term = mpf_div(term, mpf_sqrt(from_int(a-k), wp), wp)
        c[k] = to_fixed(term, prec)
        b = mpf_div(b, mpf_mul(e, from_int(k), wp), wp)
    return c

def get_spouge_coefficients(prec):
    if prec in spouge_cache:
        return spouge_cache[prec]
    for p in spouge_cache:
        if 0.8 <= prec/float(p) < 1:
            return spouge_cache[p]
    a = max(3, int(0.38*prec))
    coefs = calc_spouge_coefficients(a, prec)
    spouge_cache[prec] = (prec, a, coefs)
    return spouge_cache[prec]

def spouge_sum_real(x, prec, a, c):
    x = to_fixed(x, prec)
    s = c[0]
    for k in range(1, a):
        s += (c[k] << prec) // (x + (k << prec))
    return from_man_exp(s, -prec, prec, round_floor)


def spouge_sum_rational(p, q, prec, a, c):
    s = c[0]
    for k in range(1, a):
        s += c[k]*q // (p+q*k)
    return from_man_exp(s, -prec, prec, round_floor)


def spouge_sum_complex(re, im, prec, a, c):
    re = to_fixed(re, prec)
    im = to_fixed(im, prec)
    sre, sim = c[0], 0
    mag = ((re**2)>>prec) + ((im**2)>>prec)
    for k in range(1, a):
        M = mag + re*(2*k) + ((k**2) << prec)
        sre += (c[k]*(re + (k << prec))) // M
        sim -= (c[k]*im) // M
    re = from_man_exp(sre, -prec, prec, round_floor)
    im = from_man_exp(sim, -prec, prec, round_floor)
    return re, im

def mpf_gamma_int_old(n, prec, rounding=round_fast):
    if n < 1000:
        return from_int(ifac(n-1), prec, rounding)
    size = int(n*math.log(n,2))
    if prec > size/20.0:
        return from_int(ifac(n-1), prec, rounding)
    return mpf_gamma(from_int(n), prec, rounding)

def mpf_factorial_old(x, prec, rounding=round_fast):
    return mpf_gamma_old(x, prec, rounding, p1=0)

def mpc_factorial_old(x, prec, rounding=round_fast):
    return mpc_gamma_old(x, prec, rounding, p1=0)

def mpf_gamma_old(x, prec, rounding=round_fast, p1=1):
    sign, man, exp, bc = x
    if not man:
        if x == finf:
            return finf
        if x == fninf or x == fnan:
            return fnan
    size = exp + bc
    if size > 5:
        size = int(size*math.log(size,2))
    wp = prec + max(0, size) + 15
    if exp >= 0:
        if sign or (p1 and not man):
            raise ValueError("gamma function pole")
        if exp + bc <= 10:
            return from_int(ifac((man<<exp)-p1), prec, rounding)
    reflect = sign or exp+bc < -1
    if p1:
        x = mpf_sub(x, fone)
    if reflect:
        wp += 15
        pix = mpf_mul(x, mpf_pi(wp), wp)
        t = mpf_sin_pi(x, wp)
        g = mpf_gamma_old(mpf_sub(fone, x), wp)
        return mpf_div(pix, mpf_mul(t, g, wp), prec, rounding)
    sprec, a, c = get_spouge_coefficients(wp)
    s = spouge_sum_real(x, sprec, a, c)
    xpa = mpf_add(x, from_int(a), wp)
    logxpa = mpf_log(xpa, wp)
    xph = mpf_add(x, fhalf, wp)
    t = mpf_sub(mpf_mul(logxpa, xph, wp), xpa, wp)
    t = mpf_mul(mpf_exp(t, wp), s, prec, rounding)
    return t

def mpc_gamma_old(x, prec, rounding=round_fast, p1=1):
    re, im = x
    if im == fzero:
        return mpf_gamma_old(re, prec, rounding, p1), fzero
    sign, man, exp, bc = re
    isign, iman, iexp, ibc = im
    if re == fzero:
        size = iexp+ibc
    else:
        size = max(exp+bc, iexp+ibc)
    if size > 5:
        size = int(size*math.log(size,2))
    reflect = sign or (exp+bc < -1)
    wp = prec + max(0, size) + 25
    if p1:
        if size < -prec-5:
            return mpc_add_mpf(mpc_div(mpc_one, x, 2*prec+10), \
                mpf_neg(mpf_euler(2*prec+10)), prec, rounding)
        elif size < -5:
            wp += (-2*size)
    if p1:
        re_orig = re
        re = mpf_sub(re, fone, bc+abs(exp)+2)
        x = re, im
    if reflect:
        wp += 15
        pi = mpf_pi(wp), fzero
        pix = mpc_mul(x, pi, wp)
        t = mpc_sin_pi(x, wp)
        u = mpc_sub(mpc_one, x, wp)
        g = mpc_gamma_old(u, wp)
        w = mpc_mul(t, g, wp)
        return mpc_div(pix, w, wp)
    if iexp+ibc < -wp:
        a = mpf_gamma_old(re_orig, wp)
        b = mpf_psi0(re_orig, wp)
        gamma_diff = mpf_div(a, b, wp)
        return mpf_pos(a, prec, rounding), mpf_mul(gamma_diff, im, prec, rounding)
    sprec, a, c = get_spouge_coefficients(wp)
    s = spouge_sum_complex(re, im, sprec, a, c)
    repa = mpf_add(re, from_int(a), wp)
    logxpa = mpc_log((repa, im), wp)
    reph = mpf_add(re, fhalf, wp)
    t = mpc_sub(mpc_mul(logxpa, (reph, im), wp), (repa, im), wp)
    t = mpc_mul(mpc_exp(t, wp), s, prec, rounding)
    return t


def gram_index(ctx, t):
    if t > 10**13:
        wp = 3*ctx.log(t, 10)
    else:
        wp = 0
    prec = ctx.prec
    try:
        ctx.prec += wp
        x0 = (t/(2*mp.pi))*ctx.log(t/(2*mp.pi))
        h = ctx.findroot(lambda x:ctx.siegeltheta(t)-mp.pi*x, x0)
        h = int(h)
    finally:
        ctx.prec = prec
    return(h)

def count_to(ctx, t, T, V):
    count = 0
    vold = V[0]
    told = T[0]
    tnew = T[1]
    k = 1
    while tnew < t:
        vnew = V[k]
        if vold*vnew < 0:
            count += 1
        vold = vnew
        k += 1
        tnew = T[k]
    a = ctx.siegelz(t)
    if a*vold < 0:
        count += 1
    return count

def comp_fp_tolerance(ctx, n):
    wpz = wpzeros(n*ctx.log(n))
    if n < 15*10**8:
        fp_tolerance = 0.0005
    elif n <= 10**14:
        fp_tolerance = 0.1
    else:
        fp_tolerance = 100
    return wpz, fp_tolerance

@defun
def nzeros(ctx, t):
    if t < 14.1347251417347:
        return 0
    x = gram_index(ctx, t)
    k = int(ctx.floor(x))
    wpinitial = ctx.prec
    wpz, fp_tolerance = comp_fp_tolerance(ctx, k)
    ctx.prec = wpz
    a = ctx.siegelz(t)
    if k == -1 and a < 0:
        return 0
    elif k == -1 and a > 0:
        return 1
    if k+2 < 400000000:
        Rblock = find_rosser_block_zero(ctx, k+2)
    else:
        Rblock = search_supergood_block(ctx, k+2, fp_tolerance)
    n1, n2 = Rblock[1]
    if n2-n1 == 1:
        b = Rblock[3][0]
        if a*b > 0:
            ctx.prec = wpinitial
            return k+1
        else:
            ctx.prec = wpinitial
            return k+2
    my_zero_number,block, T, V = Rblock
    zero_number_block = n2-n1
    T, V, separated = separate_zeros_in_block(ctx, zero_number_block, T, V,\
                                              limitloop=ctx.inf,\
                                            fp_tolerance=fp_tolerance)
    n = count_to(ctx, t, T, V)
    ctx.prec = wpinitial
    return n+n1+1

@defun_wrapped
def backlunds(ctx, t):
    return ctx.nzeros(t)-1-ctx.siegeltheta(t)/mp.pi

def mpf_cos(x, prec, rnd=round_fast): return mpf_cos_sin(x, prec, rnd, 1)
def mpf_sin(x, prec, rnd=round_fast): return mpf_cos_sin(x, prec, rnd, 2)
def mpf_tan(x, prec, rnd=round_fast): return mpf_cos_sin(x, prec, rnd, 3)
def mpf_cos_sin_pi(x, prec, rnd=round_fast): return mpf_cos_sin(x, prec, rnd, 0, 1)
def mpf_cos_pi(x, prec, rnd=round_fast): return mpf_cos_sin(x, prec, rnd, 1, 1)
def mpf_sin_pi(x, prec, rnd=round_fast): return mpf_cos_sin(x, prec, rnd, 2, 1)
def mpf_cosh(x, prec, rnd=round_fast): return mpf_cosh_sinh(x, prec, rnd)[0]
def mpf_sinh(x, prec, rnd=round_fast): return mpf_cosh_sinh(x, prec, rnd)[1]
def mpf_tanh(x, prec, rnd=round_fast): return mpf_cosh_sinh(x, prec, rnd, tanh=1)


@property
def _mpf_(self):
    prec, rounding = self.context._prec_rounding
    return self.func(prec, rounding)

def __repr__(self):
    return "<%s: %s~>" % (self.name, self.context.nstr(self(dps=15)))
    


class mpnumeric(object):
    __slots__ = []
    def __new__(cls, val):
        raise NotImplementedError

class _mpc(mpnumeric):
    __slots__ = ['_mpc_']
    def __new__(cls, real=0, imag=0):
        s = object.__new__(cls)
        if isinstance(real, complex_types):
            real, imag = real.real, real.imag
        elif hasattr(real, '_mpc_'):
            s._mpc_ = real._mpc_
            return s
        real = cls.context.mpf(real)
        imag = cls.context.mpf(imag)
        s._mpc_ = (real._mpf_, imag._mpf_)
        return s

    real = property(lambda self: self.context.make_mpf(self._mpc_[0]))
    imag = property(lambda self: self.context.make_mpf(self._mpc_[1]))

    def __getstate__(self):
        return to_pickable(self._mpc_[0]), to_pickable(self._mpc_[1])

    def __setstate__(self, val):
        self._mpc_ = from_pickable(val[0]), from_pickable(val[1])

    def __repr__(s):
        if s.context.pretty:
            return str(s)
        r = repr(s.real)[4:-1]
        i = repr(s.imag)[4:-1]
        return "%s(real=%s, imag=%s)" % (type(s).__name__, r, i)

    def __str__(s):
        return "(%s)" % mpc_to_str(s._mpc_, s.context._str_digits)

    def __complex__(s):
        return mpc_to_complex(s._mpc_, rnd=s.context._prec_rounding[1])

    def __pos__(s):
        cls, new, (prec, rounding) = s._ctxdata
        v = new(cls)
        v._mpc_ = mpc_pos(s._mpc_, prec, rounding)
        return v

    def __abs__(s):
        prec, rounding = s.context._prec_rounding
        v = new(s.context.mpf)
        v._mpf_ = mpc_abs(s._mpc_, prec, rounding)
        return v

    def __neg__(s):
        cls, new, (prec, rounding) = s._ctxdata
        v = new(cls)
        v._mpc_ = mpc_neg(s._mpc_, prec, rounding)
        return v

    def conjugate(s):
        cls, new, (prec, rounding) = s._ctxdata
        v = new(cls)
        v._mpc_ = mpc_conjugate(s._mpc_, prec, rounding)
        return v

    def __nonzero__(s):
        return mpc_is_nonzero(s._mpc_)

    __bool__ = __nonzero__

    def __hash__(s):
        return mpc_hash(s._mpc_)

    @classmethod
    def mpc_convert_lhs(cls, x):
        try:
            y = cls.context.convert(x)
            return y
        except TypeError:
            return NotImplemented

    def __eq__(s, t):
        if not hasattr(t, '_mpc_'):
            if isinstance(t, str):
                return False
            t = s.mpc_convert_lhs(t)
            if t is NotImplemented:
                return t
        return s.real == t.real and s.imag == t.imag

    def __ne__(s, t):
        b = s.__eq__(t)
        if b is NotImplemented:
            return b
        return not b

    def _compare(*args):
        raise TypeError("no ordering relation is defined for complex numbers")

    __gt__ = _compare
    __le__ = _compare
    __gt__ = _compare
    __ge__ = _compare

    def __add__(s, t):
        cls, new, (prec, rounding) = s._ctxdata
        if not hasattr(t, '_mpc_'):
            t = s.mpc_convert_lhs(t)
            if t is NotImplemented:
                return t
            if hasattr(t, '_mpf_'):
                v = new(cls)
                v._mpc_ = mpc_add_mpf(s._mpc_, t._mpf_, prec, rounding)
                return v
        v = new(cls)
        v._mpc_ = mpc_add(s._mpc_, t._mpc_, prec, rounding)
        return v

    def __sub__(s, t):
        cls, new, (prec, rounding) = s._ctxdata
        if not hasattr(t, '_mpc_'):
            t = s.mpc_convert_lhs(t)
            if t is NotImplemented:
                return t
            if hasattr(t, '_mpf_'):
                v = new(cls)
                v._mpc_ = mpc_sub_mpf(s._mpc_, t._mpf_, prec, rounding)
                return v
        v = new(cls)
        v._mpc_ = mpc_sub(s._mpc_, t._mpc_, prec, rounding)
        return v

    def __mul__(s, t):
        cls, new, (prec, rounding) = s._ctxdata
        if not hasattr(t, '_mpc_'):
            if isinstance(t, int_types):
                v = new(cls)
                v._mpc_ = mpc_mul_int(s._mpc_, t, prec, rounding)
                return v
            t = s.mpc_convert_lhs(t)
            if t is NotImplemented:
                return t
            if hasattr(t, '_mpf_'):
                v = new(cls)
                v._mpc_ = mpc_mul_mpf(s._mpc_, t._mpf_, prec, rounding)
                return v
            t = s.mpc_convert_lhs(t)
        v = new(cls)
        v._mpc_ = mpc_mul(s._mpc_, t._mpc_, prec, rounding)
        return v

    def __div__(s, t):
        cls, new, (prec, rounding) = s._ctxdata
        if not hasattr(t, '_mpc_'):
            t = s.mpc_convert_lhs(t)
            if t is NotImplemented:
                return t
            if hasattr(t, '_mpf_'):
                v = new(cls)
                v._mpc_ = mpc_div_mpf(s._mpc_, t._mpf_, prec, rounding)
                return v
        v = new(cls)
        v._mpc_ = mpc_div(s._mpc_, t._mpc_, prec, rounding)
        return v

    def __pow__(s, t):
        cls, new, (prec, rounding) = s._ctxdata
        if isinstance(t, int_types):
            v = new(cls)
            v._mpc_ = mpc_pow_int(s._mpc_, t, prec, rounding)
            return v
        t = s.mpc_convert_lhs(t)
        if t is NotImplemented:
            return t
        v = new(cls)
        if hasattr(t, '_mpf_'):
            v._mpc_ = mpc_pow_mpf(s._mpc_, t._mpf_, prec, rounding)
        else:
            v._mpc_ = mpc_pow(s._mpc_, t._mpc_, prec, rounding)
        return v

    __radd__ = __add__

    def __rsub__(s, t):
        t = s.mpc_convert_lhs(t)
        if t is NotImplemented:
            return t
        return t - s

    def __rmul__(s, t):
        cls, new, (prec, rounding) = s._ctxdata
        if isinstance(t, int_types):
            v = new(cls)
            v._mpc_ = mpc_mul_int(s._mpc_, t, prec, rounding)
            return v
        t = s.mpc_convert_lhs(t)
        if t is NotImplemented:
            return t
        return t*s

    def __rdiv__(s, t):
        t = s.mpc_convert_lhs(t)
        if t is NotImplemented:
            return t
        return t / s

    def __rpow__(s, t):
        t = s.mpc_convert_lhs(t)
        if t is NotImplemented:
            return t
        return t ** s

    __truediv__ = __div__
    __rtruediv__ = __rdiv__

    def ae(s, t, rel_eps=None, abs_eps=None):
        return s.context.almosteq(s, t, rel_eps, abs_eps)

    def sqrt(s):
        return s.context.sqrt(s)

def convert_mpf_(x, prec, rounding):
    if hasattr(x, "_mpf_"): return x._mpf_
    if isinstance(x, int_types): return from_int(x, prec, rounding)
    if isinstance(x, float): return from_float(x, prec, rounding)
    if isinstance(x, basestring): return from_str(x, prec, rounding)

class ivmpf(object):
    def __new__(cls, x=0):
        return cls.ctx.convert(x)

    def __int__(self):
        a, b = self._mpi_
        if a == b:
            return int(libmp.to_int(a))
        raise ValueError

    def __hash__(self):
        a, b = self._mpi_
        if a == b:
            return mpf_hash(a)
        else:
            return hash(self._mpi_)

    @property
    def real(self): return self

    @property
    def imag(self): return self.ctx.zero

    def conjugate(self): return self

    @property
    def a(self):
        a, b = self._mpi_
        return self.ctx.make_mpf((a, a))

    @property
    def b(self):
        a, b = self._mpi_
        return self.ctx.make_mpf((b, b))

    @property
    def mid(self):
        ctx = self.ctx
        v = mpi_mid(self._mpi_, ctx.prec)
        return ctx.make_mpf((v, v))

    @property
    def delta(self):
        ctx = self.ctx
        v = mpi_delta(self._mpi_, ctx.prec)
        return ctx.make_mpf((v,v))

    @property
    def _mpci_(self):
        return self._mpi_, mpi_zero

    def _compare(*args):
        raise TypeError("no ordering relation is defined for intervals")

    __gt__ = _compare
    __le__ = _compare
    __gt__ = _compare
    __ge__ = _compare

    def __contains__(self, t):
        t = self.ctx.mpf(t)
        return (self.a <= t.a) and (t.b <= self.b)

    def __str__(self):
        return mpi_str(self._mpi_, self.ctx.prec)

    def __repr__(self):
        if self.ctx.pretty:
            return str(self)
        a, b = self._mpi_
        n = repr_dps(self.ctx.prec)
        a = libmp.to_str(a, n)
        b = libmp.to_str(b, n)
        return "mpi(%r, %r)" % (a, b)

    def _compare(s, t, cmpfun):
        if not hasattr(t, "_mpi_"):
            try:
                t = s.ctx.convert(t)
            except:
                return NotImplemented
        return cmpfun(s._mpi_, t._mpi_)

    def __eq__(s, t): return s._compare(t, libmp.mpi_eq)
    def __ne__(s, t): return s._compare(t, libmp.mpi_ne)
    def __lt__(s, t): return s._compare(t, libmp.mpi_lt)
    def __le__(s, t): return s._compare(t, libmp.mpi_le)
    def __gt__(s, t): return s._compare(t, libmp.mpi_gt)
    def __ge__(s, t): return s._compare(t, libmp.mpi_ge)

    def __abs__(self):
        return self.ctx.make_mpf(mpi_abs(self._mpi_, self.ctx.prec))
    def __pos__(self):
        return self.ctx.make_mpf(mpi_pos(self._mpi_, self.ctx.prec))
    def __neg__(self):
        return self.ctx.make_mpf(mpi_neg(self._mpi_, self.ctx.prec))

    def ae(s, t, rel_eps=None, abs_eps=None):
        return s.ctx.almosteq(s, t, rel_eps, abs_eps)

class ivmpc(object):

    def __new__(cls, re=0, im=0):
        re = cls.ctx.convert(re)
        im = cls.ctx.convert(im)
        y = new(cls)
        y._mpci_ = re._mpi_, im._mpi_
        return y

    def __hash__(self):
        (a, b), (c,d) = self._mpci_
        if a == b and c == d:
            return mpc_hash((a, c))
        else:
            return hash(self._mpci_)

    def __repr__(s):
        if s.ctx.pretty:
            return str(s)
        return "iv.mpc(%s, %s)" % (repr(s.real), repr(s.imag))

    def __str__(s):
        return "(%s + %s*j)" % (str(s.real), str(s.imag))

    @property
    def a(self):
        (a, b), (c,d) = self._mpci_
        return self.ctx.make_mpf((a, a))

    @property
    def b(self):
        (a, b), (c,d) = self._mpci_
        return self.ctx.make_mpf((b, b))

    @property
    def c(self):
        (a, b), (c,d) = self._mpci_
        return self.ctx.make_mpf((c, c))

    @property
    def d(self):
        (a, b), (c,d) = self._mpci_
        return self.ctx.make_mpf((d, d))

    @property
    def real(s):
        return s.ctx.make_mpf(s._mpci_[0])

    @property
    def imag(s):
        return s.ctx.make_mpf(s._mpci_[1])

    def conjugate(s):
        a, b = s._mpci_
        return s.ctx.make_mpc((a, mpf_neg(b)))

    def overlap(s, t):
        t = s.ctx.convert(t)
        real_overlap = (s.a <= t.a <= s.b) or (s.a <= t.b <= s.b) or (t.a <= s.a <= t.b) or (t.a <= s.b <= t.b)
        imag_overlap = (s.c <= t.c <= s.d) or (s.c <= t.d <= s.d) or (t.c <= s.c <= t.d) or (t.c <= s.d <= t.d)
        return real_overlap and imag_overlap

    def __contains__(s, t):
        t = s.ctx.convert(t)
        return t.real in s.real and t.imag in s.imag

    def _compare(s, t, ne=False):
        if not isinstance(t, s.ctx._types):
            try:
                t = s.ctx.convert(t)
            except:
                return NotImplemented
        if hasattr(t, '_mpi_'):
            tval = t._mpi_, mpi_zero
        elif hasattr(t, '_mpci_'):
            tval = t._mpci_
        if ne:
            return s._mpci_ != tval
        return s._mpci_ == tval

    def __eq__(s, t): return s._compare(t)
    def __ne__(s, t): return s._compare(t, True)

    def __lt__(s, t): raise TypeError("complex intervals cannot be ordered")
    __le__ = __gt__ = __ge__ = __lt__

    def __neg__(s): return s.ctx.make_mpc(mpci_neg(s._mpci_, s.ctx.prec))
    def __pos__(s): return s.ctx.make_mpc(mpci_pos(s._mpci_, s.ctx.prec))
    def __abs__(s): return s.ctx.make_mpf(mpci_abs(s._mpci_, s.ctx.prec))

    def ae(s, t, rel_eps=None, abs_eps=None):
        return s.ctx.almosteq(s, t, rel_eps, abs_eps)

def _binary_op(f_real, f_complex):
    def g_complex(ctx, sval, tval):
        return ctx.make_mpc(f_complex(sval, tval, ctx.prec))
    def g_real(ctx, sval, tval):
        try:
            return ctx.make_mpf(f_real(sval, tval, ctx.prec))
        except ComplexResult:
            sval = (sval, mpi_zero)
            tval = (tval, mpi_zero)
            return g_complex(ctx, sval, tval)
    def lop_real(s, t):
        ctx = s.ctx
        if not isinstance(t, ctx._types): t = ctx.convert(t)
        if hasattr(t, "_mpi_"): return g_real(ctx, s._mpi_, t._mpi_)
        if hasattr(t, "_mpci_"): return g_complex(ctx, (s._mpi_, mpi_zero), t._mpci_)
        return NotImplemented
    def rop_real(s, t):
        ctx = s.ctx
        if not isinstance(t, ctx._types): t = ctx.convert(t)
        if hasattr(t, "_mpi_"): return g_real(ctx, t._mpi_, s._mpi_)
        if hasattr(t, "_mpci_"): return g_complex(ctx, t._mpci_, (s._mpi_, mpi_zero))
        return NotImplemented
    def lop_complex(s, t):
        ctx = s.ctx
        if not isinstance(t, s.ctx._types):
            try:
                t = s.ctx.convert(t)
            except (ValueError, TypeError):
                return NotImplemented
        return g_complex(ctx, s._mpci_, t._mpci_)
    def rop_complex(s, t):
        ctx = s.ctx
        if not isinstance(t, s.ctx._types):
            t = s.ctx.convert(t)
        return g_complex(ctx, t._mpci_, s._mpci_)
    return lop_real, rop_real, lop_complex, rop_complex

ivmpf.__add__, ivmpf.__radd__, ivmpc.__add__, ivmpc.__radd__ = _binary_op(mpi_add, mpci_add)
ivmpf.__sub__, ivmpf.__rsub__, ivmpc.__sub__, ivmpc.__rsub__ = _binary_op(mpi_sub, mpci_sub)
ivmpf.__mul__, ivmpf.__rmul__, ivmpc.__mul__, ivmpc.__rmul__ = _binary_op(mpi_mul, mpci_mul)
ivmpf.__div__, ivmpf.__rdiv__, ivmpc.__div__, ivmpc.__rdiv__ = _binary_op(mpi_div, mpci_div)
ivmpf.__pow__, ivmpf.__rpow__, ivmpc.__pow__, ivmpc.__rpow__ = _binary_op(mpi_pow, mpci_pow)

ivmpf.__truediv__ = ivmpf.__div__; ivmpf.__rtruediv__ = ivmpf.__rdiv__
ivmpc.__truediv__ = ivmpc.__div__; ivmpc.__rtruediv__ = ivmpc.__rdiv__


try:
    import numbers
    numbers.Complex.register(ivmpc)
    numbers.Real.register(ivmpf)
except ImportError:
    pass

class ivmpf_constant(ivmpf):
    def __new__(cls, f):
        self = new(cls)
        self._f = f
        return self
    def _get_mpi_(self):
        prec = self.ctx._prec[0]
        a = self._f(prec, round_floor)
        b = self._f(prec, round_ceiling)
        return a, b
    _mpi_ = property(_get_mpi_)

class MPIntervalContext(StandardBaseContext):
    def __init__(ctx):
        ctx.mpf = type('ivmpf', (ivmpf,), {})
        ctx.mpc = type('ivmpc', (ivmpc,), {})
        ctx._types = (ctx.mpf, ctx.mpc)
        ctx._constant = type('ivmpf_constant', (ivmpf_constant,), {})
        ctx._prec = [53]
        ctx._set_prec(53)
        ctx._constant._ctxdata = ctx.mpf._ctxdata = ctx.mpc._ctxdata = [ctx.mpf, new, ctx._prec]
        ctx._constant.ctx = ctx.mpf.ctx = ctx.mpc.ctx = ctx
        ctx.pretty = False
        StandardBaseContext.__init__(ctx)
        ctx._init_builtins()

    def _mpi(ctx, a, b=None):
        if b is None:
            return ctx.mpf(a)
        return ctx.mpf((a,b))

    def _init_builtins(ctx):
        ctx.one = ctx.mpf(1)
        ctx.zero = ctx.mpf(0)
        ctx.inf = ctx.mpf('inf')
        ctx.ninf = -ctx.inf
        ctx.nan = ctx.mpf('nan')
        ctx.j = ctx.mpc(0,1)
        ctx.exp = ctx._wrap_mpi_function(libmp.mpi_exp, libmp.mpci_exp)
        ctx.sqrt = ctx._wrap_mpi_function(libmp.mpi_sqrt)
        ctx.ln = ctx._wrap_mpi_function(libmp.mpi_log, libmp.mpci_log)
        ctx.cos = ctx._wrap_mpi_function(libmp.mpi_cos, libmp.mpci_cos)
        ctx.sin = ctx._wrap_mpi_function(libmp.mpi_sin, libmp.mpci_sin)
        ctx.tan = ctx._wrap_mpi_function(libmp.mpi_tan)
        ctx.gamma = ctx._wrap_mpi_function(libmp.mpi_gamma, libmp.mpci_gamma)
        ctx.loggamma = ctx._wrap_mpi_function(libmp.mpi_loggamma, libmp.mpci_loggamma)
        ctx.rgamma = ctx._wrap_mpi_function(libmp.mpi_rgamma, libmp.mpci_rgamma)
        ctx.factorial = ctx._wrap_mpi_function(libmp.mpi_factorial, libmp.mpci_factorial)
        ctx.fac = ctx.factorial

        mp.eps = ctx._constant(lambda prec, rnd: (0, MPZ_ONE, 1-prec, 1))
        mp.pi = ctx._constant(libmp.mpf_pi)
        ctx.e = ctx._constant(libmp.mpf_e)
        ctx.ln2 = ctx._constant(libmp.mpf_ln2)
        ctx.ln10 = ctx._constant(libmp.mpf_ln10)
        ctx.phi = ctx._constant(libmp.mpf_phi)
        ctx.euler = ctx._constant(libmp.mpf_euler)
        ctx.catalan = ctx._constant(libmp.mpf_catalan)
        ctx.glaisher = ctx._constant(libmp.mpf_glaisher)
        ctx.khinchin = ctx._constant(libmp.mpf_khinchin)
        ctx.twinprime = ctx._constant(libmp.mpf_twinprime)

    def _wrap_mpi_function(ctx, f_real, f_complex=None):
        def g(x, **kwargs):
            if kwargs:
                prec = kwargs.get('prec', ctx._prec[0])
            else:
                prec = ctx._prec[0]
            x = ctx.convert(x)
            if hasattr(x, "_mpi_"):
                return ctx.make_mpf(f_real(x._mpi_, prec))
            if hasattr(x, "_mpci_"):
                return ctx.make_mpc(f_complex(x._mpci_, prec))
            raise ValueError
        return g

    @classmethod
    def _wrap_specfun(cls, name, f, wrap):
        if wrap:
            def f_wrapped(ctx, *args, **kwargs):
                convert = ctx.convert
                args = [convert(a) for a in args]
                prec = ctx.prec
                try:
                    ctx.prec += 10
                    retval = f(ctx, *args, **kwargs)
                finally:
                    ctx.prec = prec
                return +retval
        else:
            f_wrapped = f
        setattr(cls, name, f_wrapped)

    def _set_prec(ctx, n):
        ctx._prec[0] = max(1, int(n))
        ctx._dps = prec_to_dps(n)

    def _set_dps(ctx, n):
        ctx._prec[0] = dps_to_prec(n)
        ctx._dps = max(1, int(n))

    prec = property(lambda ctx: ctx._prec[0], _set_prec)
    dps = property(lambda ctx: ctx._dps, _set_dps)

    def make_mpf(ctx, v):
        a = new(ctx.mpf)
        a._mpi_ = v
        return a

    def make_mpc(ctx, v):
        a = new(ctx.mpc)
        a._mpci_ = v
        return a

    def _mpq(ctx, pq):
        p, q = pq
        a = libmp.from_rational(p, q, ctx.prec, round_floor)
        b = libmp.from_rational(p, q, ctx.prec, round_ceiling)
        return ctx.make_mpf((a, b))

    def convert(ctx, x):
        if isinstance(x, (ctx.mpf, ctx.mpc)):
            return x
        if isinstance(x, ctx._constant):
            return +x
        if isinstance(x, complex) or hasattr(x, "_mpc_"):
            re = ctx.convert(x.real)
            im = ctx.convert(x.imag)
            return ctx.mpc(re,im)
        if isinstance(x, basestring):
            v = mpi_from_str(x, ctx.prec)
            return ctx.make_mpf(v)
        if hasattr(x, "_mpi_"):
            a, b = x._mpi_
        else:
            try:
                a, b = x
            except (TypeError, ValueError):
                a = b = x
            if hasattr(a, "_mpi_"):
                a = a._mpi_[0]
            else:
                a = convert_mpf_(a, ctx.prec, round_floor)
            if hasattr(b, "_mpi_"):
                b = b._mpi_[1]
            else:
                b = convert_mpf_(b, ctx.prec, round_ceiling)
        if a == fnan or b == fnan:
            a = fninf
            b = finf
        assert mpf_le(a, b), "endpoints must be properly ordered"
        return ctx.make_mpf((a, b))

    def nstr(ctx, x, n=5, **kwargs):
        x = ctx.convert(x)
        if hasattr(x, "_mpi_"):
            return libmp.mpi_to_str(x._mpi_, n, **kwargs)
        if hasattr(x, "_mpci_"):
            re = libmp.mpi_to_str(x._mpci_[0], n, **kwargs)
            im = libmp.mpi_to_str(x._mpci_[1], n, **kwargs)
            return "(%s + %s*j)" % (re, im)

    def mag(ctx, x):
        x = ctx.convert(x)
        if isinstance(x, ctx.mpc):
            return max(ctx.mag(x.real), ctx.mag(x.imag)) + 1
        a, b = libmp.mpi_abs(x._mpi_)
        sign, man, exp, bc = b
        if man:
            return exp+bc
        if b == fzero:
            return ctx.ninf
        if b == fnan:
            return ctx.nan
        return ctx.inf

    def isnan(ctx, x):
        return False

    def isinf(ctx, x):
        return x == ctx.inf

    def isint(ctx, x):
        x = ctx.convert(x)
        a, b = x._mpi_
        if a == b:
            sign, man, exp, bc = a
            if man:
                return exp >= 0
            return a == fzero
        return None

    def ldexp(ctx, x, n):
        a, b = ctx.convert(x)._mpi_
        a = libmp.mpf_shift(a, n)
        b = libmp.mpf_shift(b, n)
        return ctx.make_mpf((a,b))

    def absmin(ctx, x):
        return abs(ctx.convert(x)).a

    def absmax(ctx, x):
        return abs(ctx.convert(x)).b

    def atan2(ctx, y, x):
        y = ctx.convert(y)._mpi_
        x = ctx.convert(x)._mpi_
        return ctx.make_mpf(libmp.mpi_atan2(y,x,ctx.prec))

    def _convert_param(ctx, x):
        if isinstance(x, libmp.int_types):
            return x, 'Z'
        if isinstance(x, tuple):
            p, q = x
            return (ctx.mpf(p) / ctx.mpf(q), 'R')
        x = ctx.convert(x)
        if isinstance(x, ctx.mpf):
            return x, 'R'
        if isinstance(x, ctx.mpc):
            return x, 'C'
        raise ValueError

    def _is_real_type(ctx, z):
        return isinstance(z, ctx.mpf) or isinstance(z, int_types)

    def _is_complex_type(ctx, z):
        return isinstance(z, ctx.mpc)

    def hypsum(ctx, p, q, types, coeffs, z, maxterms=6000, **kwargs):
        coeffs = list(coeffs)
        num = range(p)
        den = range(p,p+q)
        s = t = ctx.one
        k = 0
        while 1:
            for i in num: t *= (coeffs[i]+k)
            for i in den: t /= (coeffs[i]+k)
            k += 1; t /= k; t *= z; s += t
            if t == 0:
                return s
            if k > maxterms:
                raise ctx.NoConvergence

    def log(ctx, x, b=None):
        if b is None:
            return ctx.ln(x)
        wp = ctx.prec + 20
        return ctx.ln(x, prec=wp) / ctx.ln(b, prec=wp)

    def log10(ctx, x):
        return ctx.log(x, 10)

    def fmod(ctx, x, y):
        return ctx.convert(x) % ctx.convert(y)

    def degrees(ctx, x):
        return x / ctx.degree

    def radians(ctx, x):
            return x*ctx.degree

class MPIntervalContext(StandardBaseContext):
    def __init__(ctx):
        ctx.mpf = type('ivmpf', (ivmpf,), {})
        ctx.mpc = type('ivmpc', (ivmpc,), {})
        ctx._types = (ctx.mpf, ctx.mpc)
        ctx._constant = type('ivmpf_constant', (ivmpf_constant,), {})
        ctx._prec = [53]
        ctx._set_prec(53)
        ctx._constant._ctxdata = ctx.mpf._ctxdata = ctx.mpc._ctxdata = [ctx.mpf, new, ctx._prec]
        ctx._constant.ctx = ctx.mpf.ctx = ctx.mpc.ctx = ctx
        ctx.pretty = False
        StandardBaseContext.__init__(ctx)
        ctx._init_builtins()

    def _mpi(ctx, a, b=None):
        if b is None:
            return ctx.mpf(a)
        return ctx.mpf((a,b))

    def _init_builtins(ctx):
        ctx.one = ctx.mpf(1)
        ctx.zero = ctx.mpf(0)
        ctx.inf = ctx.mpf('inf')
        ctx.ninf = -ctx.inf
        ctx.nan = ctx.mpf('nan')
        ctx.j = ctx.mpc(0,1)
        ctx.exp = ctx._wrap_mpi_function(mpi_exp, mpci_exp)
        ctx.sqrt = ctx._wrap_mpi_function(mpi_sqrt)
        ctx.ln = ctx._wrap_mpi_function(mpi_log, mpci_log)
        ctx.cos = ctx._wrap_mpi_function(mpi_cos, mpci_cos)
        ctx.sin = ctx._wrap_mpi_function(mpi_sin, mpci_sin)
        ctx.tan = ctx._wrap_mpi_function(mpi_tan)
        ctx.gamma = ctx._wrap_mpi_function(mpi_gamma, mpci_gamma)
        ctx.loggamma = ctx._wrap_mpi_function(mpi_loggamma, mpci_loggamma)
        ctx.rgamma = ctx._wrap_mpi_function(mpi_rgamma, mpci_rgamma)
        ctx.factorial = ctx._wrap_mpi_function(mpi_factorial, mpci_factorial)
        ctx.fac = ctx.factorial
        mp.eps = ctx._constant(lambda prec, rnd: (0, MPZ_ONE, 1-prec, 1))
        mp.pi = ctx._constant(mpf_pi)
        ctx.e = ctx._constant(mpf_e)
        ctx.ln2 = ctx._constant(mpf_ln2)
        ctx.ln10 = ctx._constant(mpf_ln10)
        ctx.phi = ctx._constant(mpf_phi)
        ctx.euler = ctx._constant(mpf_euler)
        ctx.catalan = ctx._constant(mpf_catalan)
        ctx.glaisher = ctx._constant(mpf_glaisher)
        ctx.khinchin = ctx._constant(mpf_khinchin)
        ctx.twinprime = ctx._constant(mpf_twinprime)

    def _wrap_mpi_function(ctx, f_real, f_complex=None):
        def g(x, **kwargs):
            if kwargs:
                prec = kwargs.get('prec', ctx._prec[0])
            else:
                prec = ctx._prec[0]
            x = ctx.convert(x)
            if hasattr(x, "_mpi_"):
                return ctx.make_mpf(f_real(x._mpi_, prec))
            if hasattr(x, "_mpci_"):
                return ctx.make_mpc(f_complex(x._mpci_, prec))
            raise ValueError
        return g

    @classmethod
    def _wrap_specfun(cls, name, f, wrap):
        if wrap:
            def f_wrapped(ctx, *args, **kwargs):
                convert = ctx.convert
                args = [convert(a) for a in args]
                prec = ctx.prec
                try:
                    ctx.prec += 10
                    retval = f(ctx, *args, **kwargs)
                finally:
                    ctx.prec = prec
                return +retval
        else:
            f_wrapped = f
        setattr(cls, name, f_wrapped)

    def _set_prec(ctx, n):
        ctx._prec[0] = max(1, int(n))
        ctx._dps = prec_to_dps(n)

    def _set_dps(ctx, n):
        ctx._prec[0] = dps_to_prec(n)
        ctx._dps = max(1, int(n))

    prec = property(lambda ctx: ctx._prec[0], _set_prec)
    dps = property(lambda ctx: ctx._dps, _set_dps)

    def make_mpf(ctx, v):
        a = new(ctx.mpf)
        a._mpi_ = v
        return a

    def make_mpc(ctx, v):
        a = new(ctx.mpc)
        a._mpci_ = v
        return a

    def _mpq(ctx, pq):
        p, q = pq
        a = from_rational(p, q, ctx.prec, round_floor)
        b = from_rational(p, q, ctx.prec, round_ceiling)
        return ctx.make_mpf((a, b))

    def convert(ctx, x):
        if isinstance(x, (ctx.mpf, ctx.mpc)):
            return x
        if isinstance(x, ctx._constant):
            return +x
        if isinstance(x, complex) or hasattr(x, "_mpc_"):
            re = ctx.convert(x.real)
            im = ctx.convert(x.imag)
            return ctx.mpc(re,im)
        if isinstance(x, basestring):
            v = mpi_from_str(x, ctx.prec)
            return ctx.make_mpf(v)
        if hasattr(x, "_mpi_"):
            a, b = x._mpi_
        else:
            try:
                a, b = x
            except (TypeError, ValueError):
                a = b = x
            if hasattr(a, "_mpi_"):
                a = a._mpi_[0]
            else:
                a = convert_mpf_(a, ctx.prec, round_floor)
            if hasattr(b, "_mpi_"):
                b = b._mpi_[1]
            else:
                b = convert_mpf_(b, ctx.prec, round_ceiling)
        if a == fnan or b == fnan:
            a = fninf
            b = finf
        assert mpf_le(a, b), "endpoints must be properly ordered"
        return ctx.make_mpf((a, b))

    def nstr(ctx, x, n=5, **kwargs):
        x = ctx.convert(x)
        if hasattr(x, "_mpi_"):
            return mpi_to_str(x._mpi_, n, **kwargs)
        if hasattr(x, "_mpci_"):
            re = mpi_to_str(x._mpci_[0], n, **kwargs)
            im = mpi_to_str(x._mpci_[1], n, **kwargs)
            return "(%s + %s*j)" % (re, im)

    def mag(ctx, x):
        x = ctx.convert(x)
        if isinstance(x, ctx.mpc):
            return max(ctx.mag(x.real), ctx.mag(x.imag)) + 1
        a, b = mpi_abs(x._mpi_)
        sign, man, exp, bc = b
        if man:
            return exp+bc
        if b == fzero:
            return ctx.ninf
        if b == fnan:
            return ctx.nan
        return ctx.inf

    def isnan(ctx, x):
        return False

    def isinf(ctx, x):
        return x == ctx.inf

    def isint(ctx, x):
        x = ctx.convert(x)
        a, b = x._mpi_
        if a == b:
            sign, man, exp, bc = a
            if man:
                return exp >= 0
            return a == fzero
        return None

    def ldexp(ctx, x, n):
        a, b = ctx.convert(x)._mpi_
        a = mpf_shift(a, n)
        b = mpf_shift(b, n)
        return ctx.make_mpf((a,b))

    def absmin(ctx, x):
        return abs(ctx.convert(x)).a

    def absmax(ctx, x):
        return abs(ctx.convert(x)).b

    def atan2(ctx, y, x):
        y = ctx.convert(y)._mpi_
        x = ctx.convert(x)._mpi_
        return ctx.make_mpf(mpi_atan2(y,x,ctx.prec))

    def _convert_param(ctx, x):
        if isinstance(x, int_types):
            return x, 'Z'
        if isinstance(x, tuple):
            p, q = x
            return (ctx.mpf(p) / ctx.mpf(q), 'R')
        x = ctx.convert(x)
        if isinstance(x, ctx.mpf):
            return x, 'R'
        if isinstance(x, ctx.mpc):
            return x, 'C'
        raise ValueError

    def _is_real_type(ctx, z):
        return isinstance(z, ctx.mpf) or isinstance(z, int_types)

    def _is_complex_type(ctx, z):
        return isinstance(z, ctx.mpc)

    def hypsum(ctx, p, q, types, coeffs, z, maxterms=6000, **kwargs):
        coeffs = list(coeffs)
        num = range(p)
        den = range(p,p+q)
        s = t = ctx.one
        k = 0
        while 1:
            for i in num: t *= (coeffs[i]+k)
            for i in den: t /= (coeffs[i]+k)
            k += 1; t /= k; t *= z; s += t
            if t == 0:
                return s
            if k > maxterms:
                raise ctx.NoConvergence

complex_types = (complex, _mpc)

class mpc(mpnumeric):
    __slots__ = ['_mpc_']
    def __new__(cls, real=0, imag=0):
        s = object.__new__(cls)
        if isinstance(real, complex_types):
            real, imag = real.real, real.imag
        elif hasattr(real, '_mpc_'):
            s._mpc_ = real._mpc_
            return s
        real = cls.context.mpf(real)
        imag = cls.context.mpf(imag)
        s._mpc_ = (real._mpf_, imag._mpf_)
        return s

    real = property(lambda self: self.context.make_mpf(self._mpc_[0]))
    imag = property(lambda self: self.context.make_mpf(self._mpc_[1]))

    def __getstate__(self):
        return to_pickable(self._mpc_[0]), to_pickable(self._mpc_[1])

    def __setstate__(self, val):
        self._mpc_ = from_pickable(val[0]), from_pickable(val[1])

    def __repr__(s):
        if s.context.pretty:
            return str(s)
        r = repr(s.real)[4:-1]
        i = repr(s.imag)[4:-1]
        return "%s(real=%s, imag=%s)" % (type(s).__name__, r, i)

    def __str__(s):
        return "(%s)" % mpc_to_str(s._mpc_, s.context._str_digits)

    def __complex__(s):
        return mpc_to_complex(s._mpc_, rnd=s.context._prec_rounding[1])

    def __pos__(s):
        cls, new, (prec, rounding) = s._ctxdata
        v = new(cls)
        v._mpc_ = mpc_pos(s._mpc_, prec, rounding)
        return v

    def __abs__(s):
        prec, rounding = s.context._prec_rounding
        v = new(s.context.mpf)
        v._mpf_ = mpc_abs(s._mpc_, prec, rounding)
        return v

    def __neg__(s):
        cls, new, (prec, rounding) = s._ctxdata
        v = new(cls)
        v._mpc_ = mpc_neg(s._mpc_, prec, rounding)
        return v

    def conjugate(s):
        cls, new, (prec, rounding) = s._ctxdata
        v = new(cls)
        v._mpc_ = mpc_conjugate(s._mpc_, prec, rounding)
        return v

    def __nonzero__(s):
        return mpc_is_nonzero(s._mpc_)

    __bool__ = __nonzero__

    def __hash__(s):
        return mpc_hash(s._mpc_)

    @classmethod
    def mpc_convert_lhs(cls, x):
        try:
            y = cls.context.convert(x)
            return y
        except TypeError:
            return NotImplemented

    def __eq__(s, t):
        if not hasattr(t, '_mpc_'):
            if isinstance(t, str):
                return False
            t = s.mpc_convert_lhs(t)
            if t is NotImplemented:
                return t
        return s.real == t.real and s.imag == t.imag

    def __ne__(s, t):
        b = s.__eq__(t)
        if b is NotImplemented:
            return b
        return not b

    def _compare(*args):
        raise TypeError("no ordering relation is defined for complex numbers")

    __gt__ = _compare
    __le__ = _compare
    __gt__ = _compare
    __ge__ = _compare

    def __add__(s, t):
        cls, new, (prec, rounding) = s._ctxdata
        if not hasattr(t, '_mpc_'):
            t = s.mpc_convert_lhs(t)
            if t is NotImplemented:
                return t
            if hasattr(t, '_mpf_'):
                v = new(cls)
                v._mpc_ = mpc_add_mpf(s._mpc_, t._mpf_, prec, rounding)
                return v
        v = new(cls)
        v._mpc_ = mpc_add(s._mpc_, t._mpc_, prec, rounding)
        return v

    def __sub__(s, t):
        cls, new, (prec, rounding) = s._ctxdata
        if not hasattr(t, '_mpc_'):
            t = s.mpc_convert_lhs(t)
            if t is NotImplemented:
                return t
            if hasattr(t, '_mpf_'):
                v = new(cls)
                v._mpc_ = mpc_sub_mpf(s._mpc_, t._mpf_, prec, rounding)
                return v
        v = new(cls)
        v._mpc_ = mpc_sub(s._mpc_, t._mpc_, prec, rounding)
        return v

    def __mul__(s, t):
        cls, new, (prec, rounding) = s._ctxdata
        if not hasattr(t, '_mpc_'):
            if isinstance(t, int_types):
                v = new(cls)
                v._mpc_ = mpc_mul_int(s._mpc_, t, prec, rounding)
                return v
            t = s.mpc_convert_lhs(t)
            if t is NotImplemented:
                return t
            if hasattr(t, '_mpf_'):
                v = new(cls)
                v._mpc_ = mpc_mul_mpf(s._mpc_, t._mpf_, prec, rounding)
                return v
            t = s.mpc_convert_lhs(t)
        v = new(cls)
        v._mpc_ = mpc_mul(s._mpc_, t._mpc_, prec, rounding)
        return v

    def __div__(s, t):
        cls, new, (prec, rounding) = s._ctxdata
        if not hasattr(t, '_mpc_'):
            t = s.mpc_convert_lhs(t)
            if t is NotImplemented:
                return t
            if hasattr(t, '_mpf_'):
                v = new(cls)
                v._mpc_ = mpc_div_mpf(s._mpc_, t._mpf_, prec, rounding)
                return v
        v = new(cls)
        v._mpc_ = mpc_div(s._mpc_, t._mpc_, prec, rounding)
        return v

    def __pow__(s, t):
        cls, new, (prec, rounding) = s._ctxdata
        if isinstance(t, int_types):
            v = new(cls)
            v._mpc_ = mpc_pow_int(s._mpc_, t, prec, rounding)
            return v
        t = s.mpc_convert_lhs(t)
        if t is NotImplemented:
            return t
        v = new(cls)
        if hasattr(t, '_mpf_'):
            v._mpc_ = mpc_pow_mpf(s._mpc_, t._mpf_, prec, rounding)
        else:
            v._mpc_ = mpc_pow(s._mpc_, t._mpc_, prec, rounding)
        return v

    __radd__ = __add__

    def __rsub__(s, t):
        t = s.mpc_convert_lhs(t)
        if t is NotImplemented:
            return t
        return t - s

    def __rmul__(s, t):
        cls, new, (prec, rounding) = s._ctxdata
        if isinstance(t, int_types):
            v = new(cls)
            v._mpc_ = mpc_mul_int(s._mpc_, t, prec, rounding)
            return v
        t = s.mpc_convert_lhs(t)
        if t is NotImplemented:
            return t
        return t * s

    def __rdiv__(s, t):
        t = s.mpc_convert_lhs(t)
        if t is NotImplemented:
            return t
        return t / s

    def __rpow__(s, t):
        t = s.mpc_convert_lhs(t)
        if t is NotImplemented:
            return t
        return t ** s

    __truediv__ = __div__
    __rtruediv__ = __rdiv__

    def ae(s, t, rel_eps=None, abs_eps=None):
        return s.context.almosteq(s, t, rel_eps, abs_eps)

complex_types = (complex, _mpc)

class _mpf(mpnumeric):
    __slots__ = ['_mpf_']

    def __new__(cls, val=fzero, **kwargs):
        prec, rounding = cls.context._prec_rounding
        if kwargs:
            prec = kwargs.get('prec', prec)
            if 'dps' in kwargs:
                prec = dps_to_prec(kwargs['dps'])
            rounding = kwargs.get('rounding', rounding)
        if type(val) is cls:
            sign, man, exp, bc = val._mpf_
            if (not man) and exp:
                return val
            v = new(cls)
            v._mpf_ = normalize(sign, man, exp, bc, prec, rounding)
            return v
        elif type(val) is tuple:
            if len(val) == 2:
                v = new(cls)
                v._mpf_ = from_man_exp(val[0], val[1], prec, rounding)
                return v
            if len(val) == 4:
                sign, man, exp, bc = val
                v = new(cls)
                v._mpf_ = normalize(sign, MPZ(man), exp, bc, prec, rounding)
                return v
            raise ValueError
        else:
            v = new(cls)
            v._mpf_ = mpf_pos(cls.mpf_convert_arg(val, prec, rounding), prec, rounding)
            return v

    @classmethod
    def mpf_convert_arg(cls, x, prec, rounding):
        if isinstance(x, int_types): return from_int(x)
        if isinstance(x, float): return from_float(x)
        if isinstance(x, basestring): return from_str(x, prec, rounding)
        if isinstance(x, cls.context.constant): return x.func(prec, rounding)
        if hasattr(x, '_mpf_'): return x._mpf_
        if hasattr(x, '_mpmath_'):
            t = cls.context.convert(x._mpmath_(prec, rounding))
            if hasattr(t, '_mpf_'):
                return t._mpf_
        if hasattr(x, '_mpi_'):
            a, b = x._mpi_
            if a == b:
                return a
            raise ValueError("can only create mpf from zero-width interval")
        raise TypeError("cannot create mpf from " + repr(x))

    @classmethod
    def mpf_convert_rhs(cls, x):
        if isinstance(x, int_types): return from_int(x)
        if isinstance(x, float): return from_float(x)
        if isinstance(x, complex_types): return cls.context.mpc(x)
        if isinstance(x, mpq):
            p, q = x._mpq_
            return from_rational(p, q, cls.context.prec)
        if hasattr(x, '_mpf_'): return x._mpf_
        if hasattr(x, '_mpmath_'):
            t = cls.context.convert(x._mpmath_(*cls.context._prec_rounding))
            if hasattr(t, '_mpf_'):
                return t._mpf_
            return t
        return NotImplemented

    @classmethod
    def mpf_convert_lhs(cls, x):
        x = cls.mpf_convert_rhs(x)
        if type(x) is tuple:
            return cls.context.make_mpf(x)
        return x

    man_exp = property(lambda self: self._mpf_[1:3])
    man = property(lambda self: self._mpf_[1])
    exp = property(lambda self: self._mpf_[2])
    bc = property(lambda self: self._mpf_[3])

    real = property(lambda self: self)
    imag = property(lambda self: self.context.zero)
    conjugate = lambda self: self
    def __getstate__(self): return to_pickable(self._mpf_)
    def __setstate__(self, val): self._mpf_ = from_pickable(val)

    def __repr__(s):
        if s.context.pretty:
            return str(s)
        return "mpf('%s')" % to_str(s._mpf_, s.context._repr_digits)

    def __str__(s): return to_str(s._mpf_, s.context._str_digits)
    def __hash__(s): return mpf_hash(s._mpf_)
    def __int__(s): return int(to_int(s._mpf_))
    def __long__(s): return long(to_int(s._mpf_))
    def __float__(s): return to_float(s._mpf_, rnd=s.context._prec_rounding[1])
    def __complex__(s): return complex(float(s))
    def __nonzero__(s): return s._mpf_ != fzero
    __bool__ = __nonzero__

    def __abs__(s):
        cls, new, (prec, rounding) = s._ctxdata
        v = new(cls)
        v._mpf_ = mpf_abs(s._mpf_, prec, rounding)
        return v

    def __pos__(s):
        cls, new, (prec, rounding) = s._ctxdata
        v = new(cls)
        v._mpf_ = mpf_pos(s._mpf_, prec, rounding)
        return v

    def __neg__(s):
        cls, new, (prec, rounding) = s._ctxdata
        v = new(cls)
        v._mpf_ = mpf_neg(s._mpf_, prec, rounding)
        return v

    def _cmp(s, t, func):
        if hasattr(t, '_mpf_'):
            t = t._mpf_
        else:
            t = s.mpf_convert_rhs(t)
            if t is NotImplemented:
                return t
        return func(s._mpf_, t)

    def __cmp__(s, t): return s._cmp(t, mpf_cmp)
    def __lt__(s, t): return s._cmp(t, mpf_lt)
    def __gt__(s, t): return s._cmp(t, mpf_gt)
    def __le__(s, t): return s._cmp(t, mpf_le)
    def __ge__(s, t): return s._cmp(t, mpf_ge)

    def __ne__(s, t):
        v = s.__eq__(t)
        if v is NotImplemented:
            return v
        return not v

    def __rsub__(s, t):
        cls, new, (prec, rounding) = s._ctxdata
        if type(t) in int_types:
            v = new(cls)
            v._mpf_ = mpf_sub(from_int(t), s._mpf_, prec, rounding)
            return v
        t = s.mpf_convert_lhs(t)
        if t is NotImplemented:
            return t
        return t - s

    def __rdiv__(s, t):
        cls, new, (prec, rounding) = s._ctxdata
        if isinstance(t, int_types):
            v = new(cls)
            v._mpf_ = mpf_rdiv_int(t, s._mpf_, prec, rounding)
            return v
        t = s.mpf_convert_lhs(t)
        if t is NotImplemented:
            return t
        return t / s

    def __rpow__(s, t):
        t = s.mpf_convert_lhs(t)
        if t is NotImplemented:
            return t
        return t ** s

    def __rmod__(s, t):
        t = s.mpf_convert_lhs(t)
        if t is NotImplemented:
            return t
        return t % s

    def ae(s, t, rel_eps=None, abs_eps=None):
        return s.context.almosteq(s, t, rel_eps, abs_eps)

    def to_fixed(self, prec):
        return to_fixed(self._mpf_, prec)

    def __round__(self, *args):
        return round(float(self), *args)


class _constant(_mpf):
    def __new__(cls, func, name, docname=''):
        a = object.__new__(cls)
        a.name = name
        a.func = func
        return a

    def __call__(self, prec=None, dps=None, rounding=None):
        prec2, rounding2 = self.context._prec_rounding
        if not prec: prec = prec2
        if not rounding: rounding = rounding2
        if dps: prec = dps_to_prec(dps)
        return self.context.make_mpf(self.func(prec, rounding))

class mpf(mpnumeric):
    __slots__ = ['_mpf_']

    def __new__(cls, val=fzero, **kwargs):
        prec, rounding = cls.context._prec_rounding
        if kwargs:
            prec = kwargs.get('prec', prec)
            if 'dps' in kwargs:
                prec = dps_to_prec(kwargs['dps'])
            rounding = kwargs.get('rounding', rounding)
        if type(val) is cls:
            sign, man, exp, bc = val._mpf_
            if (not man) and exp:
                return val
            v = new(cls)
            v._mpf_ = normalize(sign, man, exp, bc, prec, rounding)
            return v
        elif type(val) is tuple:
            if len(val) == 2:
                v = new(cls)
                v._mpf_ = from_man_exp(val[0], val[1], prec, rounding)
                return v
            if len(val) == 4:
                sign, man, exp, bc = val
                v = new(cls)
                v._mpf_ = normalize(sign, MPZ(man), exp, bc, prec, rounding)
                return v
            raise ValueError
        else:
            v = new(cls)
            v._mpf_ = mpf_pos(cls.mpf_convert_arg(val, prec, rounding), prec, rounding)
            return v

    @classmethod
    def mpf_convert_arg(cls, x, prec, rounding):
        if isinstance(x, int_types): return from_int(x)
        if isinstance(x, float): return from_float(x)
        if isinstance(x, basestring): return from_str(x, prec, rounding)
        if isinstance(x, cls.context.constant): return x.func(prec, rounding)
        if hasattr(x, '_mpf_'): return x._mpf_
        if hasattr(x, '_mpmath_'):
            t = cls.context.convert(x._mpmath_(prec, rounding))
            if hasattr(t, '_mpf_'):
                return t._mpf_
        if hasattr(x, '_mpi_'):
            a, b = x._mpi_
            if a == b:
                return a
            raise ValueError("can only create mpf from zero-width interval")
        raise TypeError("cannot create mpf from " + repr(x))

    @classmethod
    def mpf_convert_rhs(cls, x):
        if isinstance(x, int_types): return from_int(x)
        if isinstance(x, float): return from_float(x)
        if isinstance(x, complex_types): return cls.context.mpc(x)
        if isinstance(x, mpq):
            p, q = x._mpq_
            return from_rational(p, q, cls.context.prec)
        if hasattr(x, '_mpf_'): return x._mpf_
        if hasattr(x, '_mpmath_'):
            t = cls.context.convert(x._mpmath_(*cls.context._prec_rounding))
            if hasattr(t, '_mpf_'):
                return t._mpf_
            return t
        return NotImplemented

    @classmethod
    def mpf_convert_lhs(cls, x):
        x = cls.mpf_convert_rhs(x)
        if type(x) is tuple:
            return cls.context.make_mpf(x)
        return x

    man_exp = property(lambda self: self._mpf_[1:3])
    man = property(lambda self: self._mpf_[1])
    exp = property(lambda self: self._mpf_[2])
    bc = property(lambda self: self._mpf_[3])

    real = property(lambda self: self)
    imag = property(lambda self: self.context.zero)
    conjugate = lambda self: self
    def __getstate__(self): return to_pickable(self._mpf_)
    def __setstate__(self, val): self._mpf_ = from_pickable(val)

    def __repr__(s):
        if s.context.pretty:
            return str(s)
        return "mpf('%s')" % to_str(s._mpf_, s.context._repr_digits)

    def __str__(s): return to_str(s._mpf_, s.context._str_digits)
    def __hash__(s): return mpf_hash(s._mpf_)
    def __int__(s): return int(to_int(s._mpf_))
    def __long__(s): return long(to_int(s._mpf_))
    def __float__(s): return to_float(s._mpf_, rnd=s.context._prec_rounding[1])
    def __complex__(s): return complex(float(s))
    def __nonzero__(s): return s._mpf_ != fzero
    __bool__ = __nonzero__

    def __abs__(s):
        cls, new, (prec, rounding) = s._ctxdata
        v = new(cls)
        v._mpf_ = mpf_abs(s._mpf_, prec, rounding)
        return v

    def __pos__(s):
        cls, new, (prec, rounding) = s._ctxdata
        v = new(cls)
        v._mpf_ = mpf_pos(s._mpf_, prec, rounding)
        return v

    def __neg__(s):
        cls, new, (prec, rounding) = s._ctxdata
        v = new(cls)
        v._mpf_ = mpf_neg(s._mpf_, prec, rounding)
        return v

    def _cmp(s, t, func):
        if hasattr(t, '_mpf_'):
            t = t._mpf_
        else:
            t = s.mpf_convert_rhs(t)
            if t is NotImplemented:
                return t
        return func(s._mpf_, t)

    def __cmp__(s, t): return s._cmp(t, mpf_cmp)
    def __lt__(s, t): return s._cmp(t, mpf_lt)
    def __gt__(s, t): return s._cmp(t, mpf_gt)
    def __le__(s, t): return s._cmp(t, mpf_le)
    def __ge__(s, t): return s._cmp(t, mpf_ge)

    def __ne__(s, t):
        v = s.__eq__(t)
        if v is NotImplemented:
            return v
        return not v

    def __rsub__(s, t):
        cls, new, (prec, rounding) = s._ctxdata
        if type(t) in int_types:
            v = new(cls)
            v._mpf_ = mpf_sub(from_int(t), s._mpf_, prec, rounding)
            return v
        t = s.mpf_convert_lhs(t)
        if t is NotImplemented:
            return t
        return t - s

    def __rdiv__(s, t):
        cls, new, (prec, rounding) = s._ctxdata
        if isinstance(t, int_types):
            v = new(cls)
            v._mpf_ = mpf_rdiv_int(t, s._mpf_, prec, rounding)
            return v
        t = s.mpf_convert_lhs(t)
        if t is NotImplemented:
            return t
        return t / s

    def __rpow__(s, t):
        t = s.mpf_convert_lhs(t)
        if t is NotImplemented:
            return t
        return t ** s

    def __rmod__(s, t):
        t = s.mpf_convert_lhs(t)
        if t is NotImplemented:
            return t
        return t % s

    def ae(s, t, rel_eps=None, abs_eps=None):
        return s.context.almosteq(s, t, rel_eps, abs_eps)

    def to_fixed(self, prec):
        return to_fixed(self._mpf_, prec)

    def __round__(self, *args):
        return round(float(self), *args)


try:
    import numbers
    numbers.Complex.register(_mpc)
    numbers.Real.register(_mpf)
except ImportError:
    pass

def chebcoeff(ctx,f,a,b,j,N):
    s = ctx.mpf(0)
    h = ctx.mpf(0.5)
    for k in range(1, N+1):
        t = ctx.cospi((k-h)/N)
        s += f(t*(b-a)*h + (b+a)*h)*ctx.cospi(j*(k-h)/N)
    return 2*s/N

def chebT(ctx, a=1, b=0):
    Tb = [1]
    yield Tb
    Ta = [b, a]
    while 1:
        yield Ta
        Tmp = [0] + [2*a*t for t in Ta]
        for i, c in enumerate(Ta): Tmp[i] += 2*b*c
        for i, c in enumerate(Tb): Tmp[i] -= c
        Ta, Tb = Tmp, Ta

@defun
def chebyfit(ctx, f, interval, N, error=False):

    a, b = ctx._as_points(interval)
    orig = ctx.prec
    try:
        ctx.prec = orig + int(N**0.5) + 20
        c = [chebcoeff(ctx,f,a,b,k,N) for k in range(N)]
        d = [ctx.zero]*N
        d[0] = -c[0]/2
        h = ctx.mpf(0.5)
        T = chebT(ctx, ctx.mpf(2)/(b-a), ctx.mpf(-1)*(b+a)/(b-a))
        for (k, Tk) in zip(range(N), T):
            for i in range(len(Tk)):
                d[i] += c[k]*Tk[i]
        d = d[::-1]
        err = ctx.zero
        for k in range(N):
            x = ctx.cos(mp.pi*k/N)*(b-a)*h + (b+a)*h
            err = max(err, abs(f(x) - ctx.polyval(d, x)))
    finally:
        ctx.prec = orig
    if error:
        return d, +err
    else:
        return d

@defun
def fourier(ctx, f, interval, N):
    interval = ctx._as_points(interval)
    a = interval[0]
    b = interval[-1]
    L = b-a
    cos_series = []
    sin_series = []
    cutoff = mp.eps*10
    for n in range(N+1):
        m = 2*n*mp.pi/L
        an = 2*ctx.quadgl(lambda t: f(t)*ctx.cos(m*t), interval)/L
        bn = 2*ctx.quadgl(lambda t: f(t)*ctx.sin(m*t), interval)/L
        if n == 0:
            an /= 2
        if abs(an) < cutoff: an = ctx.zero
        if abs(bn) < cutoff: bn = ctx.zero
        cos_series.append(an)
        sin_series.append(bn)
    return cos_series, sin_series

@defun
def fourierval(ctx, series, interval, x):
    cs, ss = series
    ab = ctx._as_points(interval)
    a = interval[0]
    b = interval[-1]
    m = 2*mp.pi/(ab[-1]-ab[0])
    s = ctx.zero
    s += ctx.fsum(cs[n]*ctx.cos(m*n*x) for n in range(len(cs)) if cs[n])
    s += ctx.fsum(ss[n]*ctx.sin(m*n*x) for n in range(len(ss)) if ss[n])
    return s

def defun(f):
    setattr(CalculusMethods, f.__name__, f)
try:
    iteritems = dict.iteritems
except AttributeError:
    iteritems = dict.items

@defun
def difference(ctx, s, n):
    n = int(n)
    d = ctx.zero
    b = (-1) ** (n & 1)
    for k in range(n+1):
        d += b*s[k]
        b = (b*(k-n)) // (k+1)
    return d

def hsteps(ctx, f, x, n, prec, **options):
    singular = options.get('singular')
    addprec = options.get('addprec', 10)
    direction = options.get('direction', 0)
    workprec = (prec+2*addprec)*(n+1)
    orig = ctx.prec
    try:
        ctx.prec = workprec
        h = options.get('h')
        if h is None:
            if options.get('relative'):
                hextramag = int(ctx.mag(x))
            else:
                hextramag = 0
            h = ctx.ldexp(1, -prec-addprec-hextramag)
        else:
            h = ctx.convert(h)
        direction = options.get('direction', 0)
        if direction:
            h *= ctx.sign(direction)
            steps = range(n+1)
            norm = h
        else:
            steps = range(-n, n+1, 2)
            norm = (2*h)
        if singular:
            x += 0.5*h
        values = [f(x+k*h) for k in steps]
        return values, norm, workprec
    finally:
        ctx.prec = orig

@defun
def diff(ctx, f, x, n=1, **options):
    partial = False
    try:
        orders = list(n)
        x = list(x)
        partial = True
    except TypeError:
        pass
    if partial:
        x = [ctx.convert(_) for _ in x]
        return _partial_diff(ctx, f, x, orders, options)
    method = options.get('method', 'step')
    if n == 0 and method != 'quad' and not options.get('singular'):
        return f(ctx.convert(x))
    prec = ctx.prec
    try:
        if method == 'step':
            values, norm, workprec = hsteps(ctx, f, x, n, prec, **options)
            ctx.prec = workprec
            v = ctx.difference(values, n) / norm**n
        elif method == 'quad':
            ctx.prec += 10
            radius = ctx.convert(options.get('radius', 0.25))
            def g(t):
                rei = radius*ctx.expj(t)
                z = x + rei
                return f(z) / rei**n
            d = ctx.quadts(g, [0, 2*mp.pi])
            v = d*ctx.factorial(n) / (2*mp.pi)
        else:
            raise ValueError("unknown method: %r" % method)
    finally:
        ctx.prec = prec
    return +v

def _partial_diff(ctx, f, xs, orders, options):
    if not orders:
        return f()
    if not sum(orders):
        return f(*xs)
    i = 0
    for i in range(len(orders)):
        if orders[i]:
            break
    order = orders[i]
    def fdiff_inner(*f_args):
        def inner(t):
            return f(*(f_args[:i] + (t,) + f_args[i+1:]))
        return ctx.diff(inner, f_args[i], order, **options)
    orders[i] = 0
    return _partial_diff(ctx, fdiff_inner, xs, orders, options)

@defun
def diffs(ctx, f, x, n=None, **options):
    if n is None:
        n = ctx.inf
    else:
        n = int(n)
    if options.get('method', 'step') != 'step':
        k = 0
        while k < n + 1:
            yield ctx.diff(f, x, k, **options)
            k += 1
        return
    singular = options.get('singular')
    if singular:
        yield ctx.diff(f, x, 0, singular=True)
    else:
        yield f(ctx.convert(x))
    if n < 1:
        return
    if n == ctx.inf:
        A, B = 1, 2
    else:
        A, B = 1, n+1
    while 1:
        callprec = ctx.prec
        y, norm, workprec = hsteps(ctx, f, x, B, callprec, **options)
        for k in range(A, B):
            try:
                ctx.prec = workprec
                d = ctx.difference(y, k) / norm**k
            finally:
                ctx.prec = callprec
            yield +d
            if k >= n:
                return
        A, B = B, int(A*1.4+1)
        B = min(B, n)

def iterable_to_function(gen):
    gen = iter(gen)
    data = []
    def f(k):
        for i in range(len(data), k+1):
            data.append(next(gen))
        return data[k]
    return f

@defun
def diffs_prod(ctx, factors):
    N = len(factors)
    if N == 1:
        for c in factors[0]:
            yield c
    else:
        u = iterable_to_function(ctx.diffs_prod(factors[:N//2]))
        v = iterable_to_function(ctx.diffs_prod(factors[N//2:]))
        n = 0
        while 1:
            s = u(n)*v(0)
            a = 1
            for k in range(1,n+1):
                a = a*(n-k+1) // k
                s += a*u(n-k)*v(k)
            yield s
            n += 1

def dpoly(n, _cache={}):
    if n in _cache:
        return _cache[n]
    if not _cache:
        _cache[0] = {(0,):1}
    R = dpoly(n-1)
    R = dict((c+(0,),v) for (c,v) in iteritems(R))
    Ra = {}
    for powers, count in iteritems(R):
        powers1 = (powers[0]+1,) + powers[1:]
        if powers1 in Ra:
            Ra[powers1] += count
        else:
            Ra[powers1] = count
    for powers, count in iteritems(R):
        if not sum(powers):
            continue
        for k,p in enumerate(powers):
            if p:
                powers2 = powers[:k] + (p-1,powers[k+1]+1) + powers[k+2:]
                if powers2 in Ra:
                    Ra[powers2] += p*count
                else:
                    Ra[powers2] = p*count
    _cache[n] = Ra
    return _cache[n]

@defun
def diffs_exp(ctx, fdiffs):

    fn = iterable_to_function(fdiffs)
    f0 = ctx.exp(fn(0))
    yield f0
    i = 1
    while 1:
        s = ctx.mpf(0)
        for powers, c in iteritems(dpoly(i)):
            s += c*ctx.fprod(fn(k+1)**p for (k,p) in enumerate(powers) if p)
        yield s*f0
        i += 1

@defun
def differint(ctx, f, x, n=1, x0=0):

    m = max(int(ctx.ceil(ctx.re(n)))+1, 1)
    r = m-n-1
    g = lambda x: ctx.quad(lambda t: (x-t)**r*f(t), [x0, x])
    return ctx.diff(g, x, m) / ctx.gamma(m-n)

@defun
def diffun(ctx, f, n=1, **options):
    if n == 0:
        return f
    def g(x):
        return ctx.diff(f, x, n, **options)
    return g

@defun
def taylor(ctx, f, x, n, **options):
    gen = enumerate(ctx.diffs(f, x, n, **options))
    if options.get("chop", True):
        return [ctx.chop(d)/ctx.factorial(i) for i, d in gen]
    else:
        return [d/ctx.factorial(i) for i, d in gen]

@defun
def pade(ctx, a, L, M):
    if len(a) < L+M+1:
        raise ValueError("L+M+1 Coefficients should be provided")
    if M == 0:
        if L == 0:
            return [ctx.one], [ctx.one]
        else:
            return a[:L+1], [ctx.one]
    A = ctx.matrix(M)
    for j in range(M):
        for i in range(min(M, L+j+1)):
            A[j, i] = a[L+j-i]
    v = -ctx.matrix(a[(L+1):(L+M+1)])
    x = ctx.lu_solve(A, v)
    q = [ctx.one] + list(x)
    p = [0]*(L+1)
    for i in range(L+1):
        s = a[i]
        for j in range(1, min(M,i) + 1):
            s += q[j]*a[i-j]
        p[i] = s
    return p, q

izip = zip
try:
    next = next
except NameError:
    next = lambda _: _.next()

@defun
def richardson(ctx, seq):
    if len(seq) < 3:
        raise ValueError("seq should be of minimum length 3")
    if ctx.sign(seq[-1]-seq[-2]) != ctx.sign(seq[-2]-seq[-3]):
        seq = seq[::2]
    N = len(seq)//2-1
    s = ctx.zero
    c = (-1)**N*N**N / ctx.mpf(ctx._ifac(N))
    maxc = 1
    for k in range(N+1):
        s += c*seq[N+k]
        maxc = max(abs(c), maxc)
        c *= (k-N)*ctx.mpf(k+N+1)**N
        c /= ((1+k)*ctx.mpf(k+N)**N)
    return s, maxc

@defun
def shanks(ctx, seq, table=None, randomized=False):
    if len(seq) < 2:
        raise ValueError("seq should be of minimum length 2")
    if table:
        START = len(table)
    else:
        START = 0
        table = []
    STOP = len(seq) - 1
    if STOP & 1:
        STOP -= 1
    one = ctx.one
    eps = +mp.eps
    if randomized:
        rnd = Random()
        rnd.seed(START)
    for i in range(START, STOP):
        row = []
        for j in range(i+1):
            if j == 0:
                a, b = 0, seq[i+1]-seq[i]
            else:
                if j == 1:
                    a = seq[i]
                else:
                    a = table[i-1][j-2]
                b = row[j-1] - table[i-1][j-1]
            if not b:
                if randomized:
                    b = rnd.getrandbits(10)*eps
                elif i & 1:
                    return table[:-1]
                else:
                    return table
            row.append(a + one/b)
        table.append(row)
    return table


class levin_class:
    def __init__(self, method = "levin", variant = "u"):
        self.variant = variant
        self.n = 0
        self.a0 = 0
        self.theta = 1
        self.A = []
        self.B = []
        self.last = 0
        self.last_s = False

        if method == "levin":
            self.factor = self.factor_levin
        elif method == "sidi":
            self.factor = self.factor_sidi
        else:
            raise ValueError("levin: unknown method \"%s\"" % method)

    def factor_levin(self, i):
        return (self.theta + i)*(self.theta + self.n - 1) ** (self.n - i - 2) / self.ctx.mpf(self.theta + self.n) ** (self.n - i - 1)

    def factor_sidi(self, i):
        return (self.theta + self.n - 1)*(self.theta + self.n - 2) / self.ctx.mpf((self.theta + 2*self.n - i - 2)*(self.theta + 2*self.n - i - 3))

    def run(self, s, a0, a1 = 0):
        if self.variant=="t":
            w=a0
        elif self.variant=="u":
            w=a0*(self.theta+self.n)
        elif self.variant=="v":
            w=a0*a1/(a0-a1)
        else:
            assert False, "unknown variant"
        if w==0:
            raise ValueError("levin: zero weight")
        self.A.append(s/w)
        self.B.append(1/w)
        for i in range(self.n-1,-1,-1):
            if i==self.n-1:
                f=1
            else:
                f=self.factor(i)
            self.A[i]=self.A[i+1]-f*self.A[i]
            self.B[i]=self.B[i+1]-f*self.B[i]
        self.n+=1


    def update_psum(self,S):
        if self.variant!="v":
            if self.n==0:
                self.run(S[0],S[0])
            while self.n<len(S):
                self.run(S[self.n],S[self.n]-S[self.n-1])
        else:
            if len(S)==1:
                self.last=0
                return S[0],abs(S[0])

            if self.n==0:
                self.a1=S[1]-S[0]
                self.run(S[0],S[0],self.a1)

            while self.n<len(S)-1:
                na1=S[self.n+1]-S[self.n]
                self.run(S[self.n],self.a1,na1)
                self.a1=na1
        value=self.A[0]/self.B[0]
        err=abs(value-self.last)
        self.last=value
        return value,err

    def update(self,X):

        if self.variant!="v":
            if self.n==0:
                self.s=X[0]
                self.run(self.s,X[0])
            while self.n<len(X):
                self.s+=X[self.n]
                self.run(self.s,X[self.n])
        else:
            if len(X)==1:
                self.last=0
                return X[0],abs(X[0])
            if self.n==0:
                self.s=X[0]
                self.run(self.s,X[0],X[1])
            while self.n<len(X)-1:
                self.s+=X[self.n]
                self.run(self.s,X[self.n],X[self.n+1])
        value=self.A[0]/self.B[0]
        err=abs(value-self.last)
        self.last=value
        return value,err

    def step_psum(self,s):
        if self.variant!="v":
            if self.n==0:
                self.last_s=s
                self.run(s,s)
            else:
                self.run(s,s-self.last_s)
                self.last_s=s
        else:
            if isinstance(self.last_s,bool):
                self.last_s=s
                self.last_w=s
                self.last=0
                return s,abs(s)

            na1=s-self.last_s
            self.run(self.last_s,self.last_w,na1)
            self.last_w=na1
            self.last_s=s

        value=self.A[0]/self.B[0]
        err=abs(value-self.last)
        self.last=value
        return value,err

    def step(self,x):
        if self.variant!="v":
            if self.n==0:
                self.s=x
                self.run(self.s,x)
            else:
                self.s+=x
                self.run(self.s,x)
        else:
            if isinstance(self.last_s,bool):
                self.last_s=x
                self.s=0
                self.last=0
                return x,abs(x)

            self.s+=self.last_s
            self.run(self.s,self.last_s,x)
            self.last_s=x

        value=self.A[0]/self.B[0]
        err=abs(value-self.last)
        self.last=value

        return value,err

def levin(ctx, method = "levin", variant = "u"):
    L = levin_class(method = method, variant = variant)
    L.ctx = ctx
    return L

class cohen_alt_class:
    def __init__(self):
        self.last=0

    def update(self, A):
        n = len(A)
        d = (3 + self.ctx.sqrt(8)) ** n
        d = (d + 1 / d) / 2
        b = -self.ctx.one
        c = -d
        s = 0
        for k in range(n):
            c = b - c
            if k % 2 == 0:
                s = s + c*A[k]
            else:
                s = s - c*A[k]
            b = 2*(k + n)*(k - n)*b / ((2*k + 1)*(k + self.ctx.one))
        value = s / d
        err = abs(value - self.last)
        self.last = value
        return value, err

    def update_psum(self, S):
        n = len(S)
        d = (3 + self.ctx.sqrt(8)) ** n
        d = (d + 1 / d) / 2
        b = self.ctx.one
        s = 0
        for k in range(n):
            b = 2*(n + k)*(n - k)*b / ((2*k + 1)*(k + self.ctx.one))
            s += b*S[k]

        value = s / d

        err = abs(value - self.last)
        self.last = value

        return value, err

def cohen_alt(ctx):
    L = cohen_alt_class()
    L.ctx = ctx
    return L

@defun
def sumap(ctx, f, interval, integral=None, error=False):
    prec = ctx.prec
    try:
        ctx.prec += 10
        a, b = interval
        if  b != ctx.inf:
            raise ValueError("b should be equal to ctx.inf")
        g = lambda x: f(x+a)
        if integral is None:
            i1, err1 = ctx.quad(g, [0,ctx.inf], error=True)
        else:
            i1, err1 = integral, 0
        j = ctx.j
        p = mp.pi*2
        if ctx._is_real_type(i1):
            h = lambda t: -2*ctx.im(g(j*t)) / ctx.expm1(p*t)
        else:
            h = lambda t: j*(g(j*t)-g(-j*t)) / ctx.expm1(p*t)
        i2, err2 = ctx.quad(h, [0,ctx.inf], error=True)
        err = err1+err2
        v = i1+i2+0.5*g(ctx.mpf(0))
    finally:
        ctx.prec = prec
    if error:
        return +v, err
    return +v

@defun
def sumem(ctx, f, interval, tol=None, reject=10, integral=None,
    adiffs=None, bdiffs=None, verbose=False, error=False,
    _fast_abort=False):
    tol = tol or +mp.eps
    interval = ctx._as_points(interval)
    a = ctx.convert(interval[0])
    b = ctx.convert(interval[-1])
    err = ctx.zero
    prev = 0
    M = 10000
    if a == ctx.ninf: adiffs = (0 for n in range(M))
    else:             adiffs = adiffs or ctx.diffs(f, a)
    if b == ctx.inf:  bdiffs = (0 for n in range(M))
    else:             bdiffs = bdiffs or ctx.diffs(f, b)
    orig = ctx.prec
    try:
        ctx.prec += 10
        s = ctx.zero
        for k, (da, db) in enumerate(izip(adiffs, bdiffs)):
            if k & 1:
                term = (db-da)*ctx.bernoulli(k+1) / ctx.factorial(k+1)
                mag = abs(term)
                if verbose:
                    print("term", k, "magnitude =", ctx.nstr(mag))
                if k > 4 and mag < tol:
                    s += term
                    break
                elif k > 4 and abs(prev) / mag < reject:
                    err += mag
                    if _fast_abort:
                        return [s, (s, err)][error]
                    if verbose:
                        print("Failed to converge")
                    break
                else:
                    s += term
                prev = term
        if a != ctx.ninf: s += f(a)/2
        if b != ctx.inf: s += f(b)/2
        if verbose:
            print("Integrating f(x) from x = %s to %s" % (ctx.nstr(a), ctx.nstr(b)))
        if integral:
            s += integral
        else:
            integral, ierr = ctx.quad(f, interval, error=True)
            if verbose:
                print("Integration error:", ierr)
            s += integral
            err += ierr
    finally:
        ctx.prec = orig
    if error:
        return s, err
    else:
        return s

@defun
def adaptive_extrapolation(ctx, update, emfun, kwargs):
    option = kwargs.get
    if ctx._fixed_precision:
        tol = option('tol', mp.eps*2**10)
    else:
        tol = option('tol', mp.eps/2**10)
    verbose = option('verbose', False)
    maxterms = option('maxterms', ctx.dps*10)
    method = set(option('method', 'r+s').split('+'))
    skip = option('skip', 0)
    steps = iter(option('steps', range(10, 10**9, 10)))
    strict = option('strict')
    summer=[]
    if 'd' in method or 'direct' in method:
        TRY_RICHARDSON = TRY_SHANKS = TRY_EULER_MACLAURIN = False
    else:
        TRY_RICHARDSON = ('r' in method) or ('richardson' in method)
        TRY_SHANKS = ('s' in method) or ('shanks' in method)
        TRY_EULER_MACLAURIN = ('e' in method) or \
            ('euler-maclaurin' in method)

        def init_levin(m):
            variant = kwargs.get("levin_variant", "u")
            if isinstance(variant, str):
                if variant == "all":
                    variant = ["u", "v", "t"]
                else:
                    variant = [variant]
            for s in variant:
                L = levin_class(method = m, variant = s)
                L.ctx = ctx
                L.name = m + "(" + s + ")"
                summer.append(L)

        if ('l' in method) or ('levin' in method):
            init_levin("levin")

        if ('sidi' in method):
            init_levin("sidi")

        if ('a' in method) or ('alternating' in method):
            L = cohen_alt_class()
            L.ctx = ctx
            L.name = "alternating"
            summer.append(L)

    last_richardson_value = 0
    shanks_table = []
    index = 0
    step = 10
    partial = []
    best = ctx.zero
    orig = ctx.prec
    try:
        if 'workprec' in kwargs:
            ctx.prec = kwargs['workprec']
        elif TRY_RICHARDSON or TRY_SHANKS or len(summer)!=0:
            ctx.prec = (ctx.prec+10)*4
        else:
            ctx.prec += 30
        while 1:
            if index >= maxterms:
                break
            try:
                step = next(steps)
            except StopIteration:
                pass
            if verbose:
                print("-"*70)
                print("Adding terms #%i-#%i" % (index, index+step))
            update(partial, range(index, index+step))
            index += step
            best = partial[-1]
            error = abs(best - partial[-2])
            if verbose:
                print("Direct error: %s" % ctx.nstr(error))
            if error <= tol:
                return best
            if TRY_RICHARDSON:
                value, maxc = ctx.richardson(partial)
                richardson_error = abs(value - last_richardson_value)
                if verbose:
                    print("Richardson error: %s" % ctx.nstr(richardson_error))
                if richardson_error <= tol:
                    return value
                last_richardson_value = value
                if mp.eps*maxc > tol:
                    if verbose:
                        print("Ran out of precision for Richardson")
                    TRY_RICHARDSON = False
                if richardson_error < error:
                    error = richardson_error
                    best = value
            if TRY_SHANKS:
                shanks_table = ctx.shanks(partial, shanks_table, randomized=True)
                row = shanks_table[-1]
                if len(row) == 2:
                    est1 = row[-1]
                    shanks_error = 0
                else:
                    est1, maxc, est2 = row[-1], abs(row[-2]), row[-3]
                    shanks_error = abs(est1-est2)
                if verbose:
                    print("Shanks error: %s" % ctx.nstr(shanks_error))
                if shanks_error <= tol:
                    return est1
                if mp.eps*maxc > tol:
                    if verbose:
                        print("Ran out of precision for Shanks")
                    TRY_SHANKS = False
                if shanks_error < error:
                    error = shanks_error
                    best = est1
            for L in summer:
                est, lerror = L.update_psum(partial)
                if verbose:
                    print("%s error: %s" % (L.name, ctx.nstr(lerror)))
                if lerror <= tol:
                    return est
                if lerror < error:
                    error = lerror
                    best = est
            if TRY_EULER_MACLAURIN:
                if ctx.mpc(ctx.sign(partial[-1]) / ctx.sign(partial[-2])).ae(-1):
                    if verbose:
                        print ("NOT using Euler-Maclaurin: the series appears"
                            " to be alternating, so numerical\n quadrature"
                            " will most likely fail")
                    TRY_EULER_MACLAURIN = False
                else:
                    value, em_error = emfun(index, tol)
                    value += partial[-1]
                    if verbose:
                        print("Euler-Maclaurin error: %s" % ctx.nstr(em_error))
                    if em_error <= tol:
                        return value
                    if em_error < error:
                        best = value
    finally:
        ctx.prec = orig
    if strict:
        raise ctx.NoConvergence
    if verbose:
        print("Warning: failed to converge to target accuracy")
    return best

@defun
def nsum(ctx, f, *intervals, **options):
    infinite, g = standardize(ctx, f, intervals, options)
    if not infinite:
        return +g()
    def update(partial_sums, indices):
        if partial_sums:
            psum = partial_sums[-1]
        else:
            psum = ctx.zero
        for k in indices:
            psum = psum + g(ctx.mpf(k))
            partial_sums.append(psum)
    prec = ctx.prec
    def emfun(point, tol):
        workprec = ctx.prec
        ctx.prec = prec + 10
        v = ctx.sumem(g, [point, ctx.inf], tol, error=1)
        ctx.prec = workprec
        return v
    return +ctx.adaptive_extrapolation(update, emfun, options)


def wrapsafe(f):
    def g(*args):
        try:
            return f(*args)
        except (ArithmeticError, ValueError):
            return 0
    return g

def standardize(ctx, f, intervals, options):
    if options.get("ignore"):
        f = wrapsafe(f)
    finite = []
    infinite = []
    for k, points in enumerate(intervals):
        a, b = ctx._as_points(points)
        if b < a:
            return False, (lambda: ctx.zero)
        if a == ctx.ninf or b == ctx.inf:
            infinite.append((k, (a,b)))
        else:
            finite.append((k, (int(a), int(b))))
    if finite:
        f = fold_finite(ctx, f, finite)
        if not infinite:
            return False, lambda: f(*([0]*len(intervals)))
    if infinite:
        f = standardize_infinite(ctx, f, infinite)
        f = fold_infinite(ctx, f, infinite)
        args = [0]*len(intervals)
        d = infinite[0][0]
        def g(k):
            args[d] = k
            return f(*args)
        return True, g

def cartesian_product(args):
    pools = map(tuple, args)
    result = [[]]
    for pool in pools:
        result = [x+[y] for x in result for y in pool]
    for prod in result:
        yield tuple(prod)

def fold_finite(ctx, f, intervals):
    if not intervals:
        return f
    indices = [v[0] for v in intervals]
    points = [v[1] for v in intervals]
    ranges = [range(a, b+1) for (a,b) in points]
    def g(*args):
        args = list(args)
        s = ctx.zero
        for xs in cartesian_product(ranges):
            for dim, x in zip(indices, xs):
                args[dim] = ctx.mpf(x)
            s += f(*args)
        return s
    return g

def standardize_infinite(ctx, f, intervals):
    if not intervals:
        return f
    dim, [a,b] = intervals[-1]
    if a == ctx.ninf:
        if b == ctx.inf:
            def g(*args):
                args = list(args)
                k = args[dim]
                if k:
                    s = f(*args)
                    args[dim] = -k
                    s += f(*args)
                    return s
                else:
                    return f(*args)
        else:
            def g(*args):
                args = list(args)
                args[dim] = b - args[dim]
                return f(*args)
    else:
        def g(*args):
            args = list(args)
            args[dim] += a
            return f(*args)
    return standardize_infinite(ctx, g, intervals[:-1])

def fold_infinite(ctx, f, intervals):
    if len(intervals) < 2:
        return f
    dim1 = intervals[-2][0]
    dim2 = intervals[-1][0]
    def g(*args):
        args = list(args)
        n = int(args[dim1])
        s = ctx.zero
        args[dim2] = ctx.mpf(n)
        for x in range(n+1):
            args[dim1] = ctx.mpf(x)
            s += f(*args)
        args[dim1] = ctx.mpf(n)
        for y in range(n):
            args[dim2] = ctx.mpf(y)
            s += f(*args)
        return s
    return fold_infinite(ctx, g, intervals[:-1])

@defun
def nprod(ctx, f, interval, nsum=False, **kwargs):
    if nsum or ('e' in kwargs.get('method', '')):
        orig = ctx.prec
        try:
            ctx.prec += 10
            v = ctx.nsum(lambda n: ctx.ln(f(n)), interval, **kwargs)
        finally:
            ctx.prec = orig
        return +ctx.exp(v)

    a, b = ctx._as_points(interval)
    if a == ctx.ninf:
        if b == ctx.inf:
            return f(0)*ctx.nprod(lambda k: f(-k)*f(k), [1, ctx.inf], **kwargs)
        return ctx.nprod(f, [-b, ctx.inf], **kwargs)
    elif b != ctx.inf:
        return ctx.fprod(f(ctx.mpf(k)) for k in range(int(a), int(b)+1))
    a = int(a)
    def update(partial_products, indices):
        if partial_products:
            pprod = partial_products[-1]
        else:
            pprod = ctx.one
        for k in indices:
            pprod = pprod*f(a + ctx.mpf(k))
            partial_products.append(pprod)
    return +ctx.adaptive_extrapolation(update, None, kwargs)

@defun
def limit(ctx, f, x, direction=1, exp=False, **kwargs):
    if ctx.isinf(x):
        direction = ctx.sign(x)
        g = lambda k: f(ctx.mpf(k+1)*direction)
    else:
        direction *= ctx.one
        g = lambda k: f(x + direction/(k+1))
    if exp:
        h = g
        g = lambda k: h(2**k)

    def update(values, indices):
        for k in indices:
            values.append(g(k+1))
    if not 'steps' in kwargs:
        kwargs['steps'] = [10]

    return +ctx.adaptive_extrapolation(update, None, kwargs)

def ode_taylor(ctx, derivs, x0, y0, tol_prec, n):
    h = tol = ctx.ldexp(1, -tol_prec)
    dim = len(y0)
    xs = [x0]
    ys = [y0]
    x = x0
    y = y0
    orig = ctx.prec
    try:
        ctx.prec = orig*(1+n)
        for i in range(n):
            fxy = derivs(x, y)
            y = [y[i]+h*fxy[i] for i in range(len(y))]
            x += h
            xs.append(x)
            ys.append(y)
        ser = [[] for d in range(dim)]
        for j in range(n+1):
            s = [0]*dim
            b = (-1) ** (j & 1)
            k = 1
            for i in range(j+1):
                for d in range(dim):
                    s[d] += b*ys[i][d]
                b = (b*(j-k+1)) // (-k)
                k += 1
            scale = h**(-j) / ctx.fac(j)
            for d in range(dim):
                s[d] = s[d]*scale
                ser[d].append(s[d])
    finally:
        ctx.prec = orig
    radius = ctx.one
    for ts in ser:
        if ts[-1]:
            radius = min(radius, ctx.nthroot(tol/abs(ts[-1]), n))
    radius /= 2  # XXX
    return ser, x0+radius

def odefun(ctx, F, x0, y0, tol=None, degree=None, method='taylor', verbose=False):
    if tol:
        tol_prec = int(-ctx.log(tol, 2))+10
    else:
        tol_prec = ctx.prec+10
    degree = degree or (3 + int(3*ctx.dps/2.))
    workprec = ctx.prec + 40
    try:
        len(y0)
        return_vector = True
    except TypeError:
        F_ = F
        F = lambda x, y: [F_(x, y[0])]
        y0 = [y0]
        return_vector = False
    ser, xb = ode_taylor(ctx, F, x0, y0, tol_prec, degree)
    series_boundaries = [x0, xb]
    series_data = [(ser, x0, xb)]
    def mpolyval(ser, a):
        return [ctx.polyval(s[::-1], a) for s in ser]
    def get_series(x):
        if x < x0:
            raise ValueError
        n = bisect(series_boundaries, x)
        if n < len(series_boundaries):
            return series_data[n-1]
        while 1:
            ser, xa, xb = series_data[-1]
            if verbose:
                print("Computing Taylor series for [%f, %f]" % (xa, xb))
            y = mpolyval(ser, xb-xa)
            xa = xb
            ser, xb = ode_taylor(ctx, F, xb, y, tol_prec, degree)
            series_boundaries.append(xb)
            series_data.append((ser, xa, xb))
            if x <= xb:
                return series_data[-1]
    def interpolant(x):
        x = ctx.convert(x)
        orig = ctx.prec
        try:
            ctx.prec = workprec
            ser, xa, xb = get_series(x)
            y = mpolyval(ser, x-xa)
        finally:
            ctx.prec = orig
        if return_vector:
            return [+yk for yk in y]
        else:
            return +y[0]
    return interpolant
ODEMethods.odefun = odefun

class Newton:
    maxsteps = 20
    def __init__(self, ctx, f, x0, **kwargs):
        self.ctx = ctx
        if len(x0) == 1:
            self.x0 = x0[0]
        else:
            raise ValueError('expected 1 starting point, got %i' % len(x0))
        self.f = f
        if not 'df' in kwargs:
            def df(x):
                return self.ctx.diff(f, x)
        else:
            df = kwargs['df']
        self.df = df

    def __iter__(self):
        f = self.f
        df = self.df
        x0 = self.x0
        while True:
            x1 = x0 - f(x0) / df(x0)
            error = abs(x1 - x0)
            x0 = x1
            yield (x1, error)

class Secant:
    maxsteps = 30
    def __init__(self, ctx, f, x0, **kwargs):
        self.ctx = ctx
        if len(x0) == 1:
            self.x0 = x0[0]
            self.x1 = self.x0 + 0.25
        elif len(x0) == 2:
            self.x0 = x0[0]
            self.x1 = x0[1]
        else:
            raise ValueError('expected 1 or 2 starting points, got %i' % len(x0))
        self.f = f
    def __iter__(self):
        f = self.f
        x0 = self.x0
        x1 = self.x1
        f0 = f(x0)
        while True:
            f1 = f(x1)
            l = x1 - x0
            if not l:
                break
            s = (f1 - f0) / l
            if not s:
                break
            x0, x1 = x1, x1 - f1/s
            f0 = f1
            yield x1, abs(l)

class MNewton:
    maxsteps = 20
    def __init__(self, ctx, f, x0, **kwargs):
        self.ctx = ctx
        if not len(x0) == 1:
            raise ValueError('expected 1 starting point, got %i' % len(x0))
        self.x0 = x0[0]
        self.f = f
        if not 'df' in kwargs:
            def df(x):
                return self.ctx.diff(f, x)
        else:
            df = kwargs['df']
        self.df = df
        if not 'd2f' in kwargs:
            def d2f(x):
                return self.ctx.diff(df, x)
        else:
            d2f = kwargs['df']
        self.d2f = d2f

    def __iter__(self):
        x = self.x0
        f = self.f
        df = self.df
        d2f = self.d2f
        while True:
            prevx = x
            fx = f(x)
            if fx == 0:
                break
            dfx = df(x)
            d2fx = d2f(x)
            x -= fx / (dfx - fx*d2fx / dfx)
            error = abs(x - prevx)
            yield x, error

class Halley:
    maxsteps = 20
    def __init__(self, ctx, f, x0, **kwargs):
        self.ctx = ctx
        if not len(x0) == 1:
            raise ValueError('expected 1 starting point, got %i' % len(x0))
        self.x0 = x0[0]
        self.f = f
        if not 'df' in kwargs:
            def df(x):
                return self.ctx.diff(f, x)
        else:
            df = kwargs['df']
        self.df = df
        if not 'd2f' in kwargs:
            def d2f(x):
                return self.ctx.diff(df, x)
        else:
            d2f = kwargs['df']
        self.d2f = d2f

    def __iter__(self):
        x = self.x0
        f = self.f
        df = self.df
        d2f = self.d2f
        while True:
            prevx = x
            fx = f(x)
            dfx = df(x)
            d2fx = d2f(x)
            x -=  2*fx*dfx / (2*dfx**2 - fx*d2fx)
            error = abs(x - prevx)
            yield x, error

class Muller:
    maxsteps = 30
    def __init__(self, ctx, f, x0, **kwargs):
        self.ctx = ctx
        if len(x0) == 1:
            self.x0 = x0[0]
            self.x1 = self.x0 + 0.25
            self.x2 = self.x1 + 0.25
        elif len(x0) == 2:
            self.x0 = x0[0]
            self.x1 = x0[1]
            self.x2 = self.x1 + 0.25
        elif len(x0) == 3:
            self.x0 = x0[0]
            self.x1 = x0[1]
            self.x2 = x0[2]
        else:
            raise ValueError('expected 1, 2 or 3 starting points, got %i'
                             % len(x0))
        self.f = f
        self.verbose = kwargs['verbose']

    def __iter__(self):
        f = self.f
        x0 = self.x0
        x1 = self.x1
        x2 = self.x2
        fx0 = f(x0)
        fx1 = f(x1)
        fx2 = f(x2)
        while True:
            fx2x1 = (fx1 - fx2) / (x1 - x2)
            fx2x0 = (fx0 - fx2) / (x0 - x2)
            fx1x0 = (fx0 - fx1) / (x0 - x1)
            w = fx2x1 + fx2x0 - fx1x0
            fx2x1x0 = (fx1x0 - fx2x1) / (x0 - x2)
            if w == 0 and fx2x1x0 == 0:
                if self.verbose:
                    print_('canceled with')
                    print_('x0 =', x0, ', x1 =', x1, 'and x2 =', x2)
                break
            x0 = x1
            fx0 = fx1
            x1 = x2
            fx1 = fx2
            r = self.ctx.sqrt(w**2 - 4*fx2*fx2x1x0)
            if abs(w - r) > abs(w + r):
                r = -r
            x2 -= 2*fx2 / (w + r)
            fx2 = f(x2)
            error = abs(x2 - x1)
            yield x2, error

class Bisection:
    maxsteps = 100
    def __init__(self, ctx, f, x0, **kwargs):
        self.ctx = ctx
        if len(x0) != 2:
            raise ValueError('expected interval of 2 points, got %i' % len(x0))
        self.f = f
        self.a = x0[0]
        self.b = x0[1]

    def __iter__(self):
        f = self.f
        a = self.a
        b = self.b
        l = b - a
        fb = f(b)
        while True:
            m = self.ctx.ldexp(a + b, -1)
            fm = f(m)
            sign = fm*fb
            if sign < 0:
                a = m
            elif sign > 0:
                b = m
                fb = fm
            else:
                yield m, self.ctx.zero
            l /= 2
            yield (a + b)/2, abs(l)

def _getm(method):
    if method == 'illinois':
        def getm(fz, fb):
            return 0.5
    elif method == 'pegasus':
        def getm(fz, fb):
            return fb/(fb + fz)
    elif method == 'anderson':
        def getm(fz, fb):
            m = 1 - fz/fb
            if m > 0:
                return m
            else:
                return 0.5
    else:
        raise ValueError("method '%s' not recognized" % method)
    return getm

class Illinois:
    maxsteps = 30
    def __init__(self, ctx, f, x0, **kwargs):
        self.ctx = ctx
        if len(x0) != 2:
            raise ValueError('expected interval of 2 points, got %i' % len(x0))
        self.a = x0[0]
        self.b = x0[1]
        self.f = f
        self.tol = kwargs['tol']
        self.verbose = kwargs['verbose']
        self.method = kwargs.get('method', 'illinois')
        self.getm = _getm(self.method)
        if self.verbose:
            print_('using %s method' % self.method)

    def __iter__(self):
        method = self.method
        f = self.f
        a = self.a
        b = self.b
        fa = f(a)
        fb = f(b)
        m = None
        while True:
            l = b - a
            if l == 0:
                break
            s = (fb - fa) / l
            z = a - fa/s
            fz = f(z)
            if abs(fz) < self.tol:
                if self.verbose:
                    print_('canceled with z =', z)
                yield z, l
                break
            if fz*fb < 0:
                a = b
                fa = fb
                b = z
                fb = fz
            else:
                m = self.getm(fz, fb)
                b = z
                fb = fz
                fa = m*fa
            if self.verbose and m and not method == 'illinois':
                print_('m:', m)
            yield (a + b)/2, abs(l)

def Pegasus(*args, **kwargs):
    kwargs['method'] = 'pegasus'
    return Illinois(*args, **kwargs)

def Anderson(*args, **kwargs):
    kwargs['method'] = 'anderson'
    return Illinois(*args, **kwargs)

class Ridder:
    maxsteps = 30
    def __init__(self, ctx, f, x0, **kwargs):
        self.ctx = ctx
        self.f = f
        if len(x0) != 2:
            raise ValueError('expected interval of 2 points, got %i' % len(x0))
        self.x1 = x0[0]
        self.x2 = x0[1]
        self.verbose = kwargs['verbose']
        self.tol = kwargs['tol']

    def __iter__(self):
        ctx = self.ctx
        f = self.f
        x1 = self.x1
        fx1 = f(x1)
        x2 = self.x2
        fx2 = f(x2)
        while True:
            x3 = 0.5*(x1 + x2)
            fx3 = f(x3)
            x4 = x3 + (x3 - x1)*ctx.sign(fx1 - fx2)*fx3 / ctx.sqrt(fx3**2 - fx1*fx2)
            fx4 = f(x4)
            if abs(fx4) < self.tol:
                if self.verbose:
                    print_('canceled with f(x4) =', fx4)
                yield x4, abs(x1 - x2)
                break
            if fx4*fx2 < 0:
                x1 = x4
                fx1 = fx4
            else:
                x2 = x4
                fx2 = fx4
            error = abs(x1 - x2)
            yield (x1 + x2)/2, error

class ANewton:
    maxsteps = 20
    def __init__(self, ctx, f, x0, **kwargs):
        self.ctx = ctx
        if not len(x0) == 1:
            raise ValueError('expected 1 starting point, got %i' % len(x0))
        self.x0 = x0[0]
        self.f = f
        if not 'df' in kwargs:
            def df(x):
                return self.ctx.diff(f, x)
        else:
            df = kwargs['df']
        self.df = df
        def phi(x):
            return x - f(x) / df(x)
        self.phi = phi
        self.verbose = kwargs['verbose']

    def __iter__(self):
        x0 = self.x0
        f = self.f
        df = self.df
        phi = self.phi
        error = 0
        counter = 0
        while True:
            prevx = x0
            try:
                x0 = phi(x0)
            except ZeroDivisionError:
                if self.verbose:
                    print_('ZeroDivisionError: canceled with x =', x0)
                break
            preverror = error
            error = abs(prevx - x0)
            if error and abs(error - preverror) / error < 1:
                if self.verbose:
                    print_('converging slowly')
                counter += 1
            if counter >= 3:
                phi = steffensen(phi)
                counter = 0
                if self.verbose:
                    print_('accelerating convergence')
            yield x0, error

def jacobian(ctx, f, x):
    x = ctx.matrix(x)
    h = ctx.sqrt(mp.eps)
    fx = ctx.matrix(f(*x))
    m = len(fx)
    n = len(x)
    J = ctx.matrix(m, n)
    for j in range(n):
        xj = x.copy()
        xj[j] += h
        Jj = (ctx.matrix(f(*xj)) - fx) / h
        for i in range(m):
            J[i,j] = Jj[i]
    return J

class MDNewton:
    maxsteps = 10
    def __init__(self, ctx, f, x0, **kwargs):
        self.ctx = ctx
        self.f = f
        if isinstance(x0, (tuple, list)):
            x0 = ctx.matrix(x0)
        assert x0.cols == 1, 'need a vector'
        self.x0 = x0
        if 'J' in kwargs:
            self.J = kwargs['J']
        else:
            def J(*x):
                return ctx.jacobian(f, x)
            self.J = J
        self.norm = kwargs['norm']
        self.verbose = kwargs['verbose']

    def __iter__(self):
        f = self.f
        x0 = self.x0
        norm = self.norm
        J = self.J
        fx = self.ctx.matrix(f(*x0))
        fxnorm = norm(fx)
        cancel = False
        while not cancel:
            fxn = -fx
            Jx = J(*x0)
            s = self.ctx.lu_solve(Jx, fxn)
            if self.verbose:
                print_('Jx:')
                print_(Jx)
                print_('s:', s)
            l = self.ctx.one
            x1 = x0 + s
            while True:
                if x1 == x0:
                    if self.verbose:
                        print_("canceled, won't get more excact")
                    cancel = True
                    break
                fx = self.ctx.matrix(f(*x1))
                newnorm = norm(fx)
                if newnorm < fxnorm:
                    fxnorm = newnorm
                    x0 = x1
                    break
                l /= 2
                x1 = x0 + l*s
            yield (x0, fxnorm)

str2solver = {'newton':Newton, 'secant':Secant, 'mnewton':MNewton,
              'halley':Halley, 'muller':Muller, 'bisect':Bisection,
              'illinois':Illinois, 'pegasus':Pegasus, 'anderson':Anderson,
              'ridder':Ridder, 'anewton':ANewton, 'mdnewton':MDNewton}

def findroot(ctx, f, x0, solver='secant', tol=None, verbose=False, verify=True, **kwargs):
    prec = ctx.prec
    try:
        ctx.prec += 20
        if tol is None:
            tol = mp.eps*2**10
        kwargs['verbose'] = kwargs.get('verbose', verbose)
        if 'd1f' in kwargs:
            kwargs['df'] = kwargs['d1f']
        kwargs['tol'] = tol
        if isinstance(x0, (list, tuple)):
            x0 = [ctx.convert(x) for x in x0]
        else:
            x0 = [ctx.convert(x0)]
        if isinstance(solver, str):
            try:
                solver = str2solver[solver]
            except KeyError:
                raise ValueError('could not recognize solver')
        if isinstance(f, (list, tuple)):
            f2 = copy(f)
            def tmp(*args):
                return [fn(*args) for fn in f2]
            f = tmp
        try:
            fx = f(*x0)
            multidimensional = isinstance(fx, (list, tuple, ctx.matrix))
        except TypeError:
            fx = f(x0[0])
            multidimensional = False
        if 'multidimensional' in kwargs:
            multidimensional = kwargs['multidimensional']
        if multidimensional:
            solver = MDNewton
            if not 'norm' in kwargs:
                norm = lambda x: ctx.norm(x, 'inf')
                kwargs['norm'] = norm
            else:
                norm = kwargs['norm']
        else:
            norm = abs
        if norm(fx) == 0:
            if multidimensional:
                return ctx.matrix(x0)
            else:
                return x0[0]
        iterations = solver(ctx, f, x0, **kwargs)
        if 'maxsteps' in kwargs:
            maxsteps = kwargs['maxsteps']
        else:
            maxsteps = iterations.maxsteps
        i = 0
        for x, error in iterations:
            if verbose:
                print_('x:    ', x)
                print_('error:', error)
            i += 1
            if error < tol*max(1, norm(x)) or i >= maxsteps:
                break
        if not isinstance(x, (list, tuple, ctx.matrix)):
            xl = [x]
        else:
            xl = x
        if verify and norm(f(*xl))**2 > tol:
            raise ValueError('Could not find root within given tolerance. '
                             '(%s > %s)\n'
                             'Try another starting point or tweak arguments.'
                             % (norm(f(*xl))**2, tol))
        return x
    finally:
        ctx.prec = prec


def multiplicity(ctx, f, root, tol=None, maxsteps=10, **kwargs):
    if tol is None:
        tol = mp.eps ** 0.8
    kwargs['d0f'] = f
    for i in range(maxsteps):
        dfstr = 'd' + str(i) + 'f'
        if dfstr in kwargs:
            df = kwargs[dfstr]
        else:
            df = lambda x: ctx.diff(f, x, i)
        if not abs(df(root)) < tol:
            break
    return i

def steffensen(f):
    def F(x):
        fx = f(x)
        ffx = f(fx)
        return (x*ffx - fx**2) / (ffx - 2*fx + x)
    return F

OptimizationMethods.jacobian = jacobian
OptimizationMethods.findroot = findroot
OptimizationMethods.multiplicity = multiplicity

@defun
def polyval(ctx, coeffs, x, derivative=False):
    if not coeffs:
        return ctx.zero
    p = ctx.convert(coeffs[0])
    q = ctx.zero
    for c in coeffs[1:]:
        if derivative:
            q = p + x*q
        p = c + x*p
    if derivative:
        return p, q
    else:
        return p

@defun
def polyroots(ctx, coeffs, maxsteps=50, cleanup=True, extraprec=10, error=False, roots_init=None):
    if len(coeffs) <= 1:
        if not coeffs or not coeffs[0]:
            raise ValueError("Input to polyroots must not be the zero polynomial")
        return []
    orig = ctx.prec
    tol = +mp.eps
    with ctx.extraprec(extraprec):
        deg = len(coeffs) - 1
        lead = ctx.convert(coeffs[0])
        if lead == 1:
            coeffs = [ctx.convert(c) for c in coeffs]
        else:
            coeffs = [c/lead for c in coeffs]
        f = lambda x: ctx.polyval(coeffs, x)
        if roots_init is None:
            roots = [ctx.mpc((0.4+0.9j)**n) for n in range(deg)]
        else:
            roots = [None]*deg
            deg_init = min(deg, len(roots_init))
            roots[:deg_init] = list(roots_init[:deg_init])
            roots[deg_init:] = [ctx.mpc((0.4+0.9j)**n) for n
                                in range(deg_init,deg)]
        err = [ctx.one for n in range(deg)]
        for step in range(maxsteps):
            if abs(max(err)) < tol:
                break
            for i in range(deg):
                p = roots[i]
                x = f(p)
                for j in range(deg):
                    if i != j:
                        try:
                            x /= (p-roots[j])
                        except ZeroDivisionError:
                            continue
                roots[i] = p - x
                err[i] = abs(x)
        if abs(max(err)) >= tol:
            raise ctx.NoConvergence("Didn't converge in maxsteps=%d steps." \
                    % maxsteps)
        if cleanup:
            for i in range(deg):
                if abs(roots[i]) < tol:
                    roots[i] = ctx.zero
                elif abs(ctx._im(roots[i])) < tol:
                    roots[i] = roots[i].real
                elif abs(ctx._re(roots[i])) < tol:
                    roots[i] = roots[i].imag*1j
        roots.sort(key=lambda x: (abs(ctx._im(x)), ctx._re(x)))
    if error:
        err = max(err)
        err = max(err, ctx.ldexp(1, -orig+1))
        return [+r for r in roots], +err
    else:
        return [+r for r in roots]



mpf_binary_op = """
def %NAME%(self, other):
    mpf, new, (prec, rounding) = self._ctxdata
    sval = self._mpf_
    if hasattr(other, '_mpf_'):
        tval = other._mpf_
        %WITH_MPF%
    ttype = type(other)
    if ttype in int_types:
        %WITH_INT%
    elif ttype is float:
        tval = from_float(other)
        %WITH_MPF%
    elif hasattr(other, '_mpc_'):
        tval = other._mpc_
        mpc = type(other)
        %WITH_MPC%
    elif ttype is complex:
        tval = from_float(other.real), from_float(other.imag)
        mpc = self.context.mpc
        %WITH_MPC%
    if isinstance(other, mpnumeric):
        return NotImplemented
    try:
        other = mpf.context.convert(other, strings=False)
    except TypeError:
        return NotImplemented
    return self.%NAME%(other)
"""

return_mpf = "; obj = new(mpf); obj._mpf_ = val; return obj"
return_mpc = "; obj = new(mpc); obj._mpc_ = val; return obj"

mpf_pow_same = """
        try:
            val = mpf_pow(sval, tval, prec, rounding) {}
        except ComplexResult:
            if mpf.context.trap_complex:
                raise
            mpc = mpf.context.mpc
            val = mpc_pow((sval, fzero), (tval, fzero), prec, rounding) {}
""".format(return_mpf, return_mpc)

_mpf.__eq__ = binary_op('__eq__',
    'return mpf_eq(sval, tval)',
    'return mpf_eq(sval, from_int(other))',
    'return (tval[1] == fzero) and mpf_eq(tval[0], sval)')

_mpf.__add__ = binary_op('__add__',
    'val = mpf_add(sval, tval, prec, rounding)' + return_mpf,
    'val = mpf_add(sval, from_int(other), prec, rounding)' + return_mpf,
    'val = mpc_add_mpf(tval, sval, prec, rounding)' + return_mpc)

_mpf.__sub__ = binary_op('__sub__',
    'val = mpf_sub(sval, tval, prec, rounding)' + return_mpf,
    'val = mpf_sub(sval, from_int(other), prec, rounding)' + return_mpf,
    'val = mpc_sub((sval, fzero), tval, prec, rounding)' + return_mpc)

_mpf.__mul__ = binary_op('__mul__',
    'val = mpf_mul(sval, tval, prec, rounding)' + return_mpf,
    'val = mpf_mul_int(sval, other, prec, rounding)' + return_mpf,
    'val = mpc_mul_mpf(tval, sval, prec, rounding)' + return_mpc)

_mpf.__div__ = binary_op('__div__',
    'val = mpf_div(sval, tval, prec, rounding)' + return_mpf,
    'val = mpf_div(sval, from_int(other), prec, rounding)' + return_mpf,
    'val = mpc_mpf_div(sval, tval, prec, rounding)' + return_mpc)

_mpf.__mod__ = binary_op('__mod__',
    'val = mpf_mod(sval, tval, prec, rounding)' + return_mpf,
    'val = mpf_mod(sval, from_int(other), prec, rounding)' + return_mpf,
    'raise NotImplementedError("complex modulo")')

_mpf.__pow__ = binary_op('__pow__',
    mpf_pow_same,
    'val = mpf_pow_int(sval, other, prec, rounding)' + return_mpf,
    'val = mpc_pow((sval, fzero), tval, prec, rounding)' + return_mpc)

_mpf.__radd__ = _mpf.__add__
_mpf.__rmul__ = _mpf.__mul__
_mpf.__truediv__ = _mpf.__div__
_mpf.__rtruediv__ = _mpf.__rdiv__
__docformat__ = 'plaintext'
new = object.__new__

__version__ = '1.0.0'
fp = FPContext()
mp = MPContext()
iv = MPIntervalContext()
fp._mp = mp
mp._mp = mp
iv._mp = mp
mp._fp = fp
fp._fp = fp
mp._iv = iv
fp._iv = iv
iv._iv = iv
BaseMPContext = PythonMPContext
_mpf_module = mp.mpf
new = object.__new__

# if BACKEND == 'sage':
#     from sage.libs.mpmath.ext_main import Context as BaseMPContext
#     import sage.libs.mpmath.ext_main as _mpf_module
# else:
#     BaseMPContext = PythonMPContext
#     _mpf_module = mp.mpf
# new = object.__new__

zetazero = mp.zetazero
riemannr = mp.riemannr
primepi = mp.primepi
primepi2 = mp.primepi2
primezeta = mp.primezeta
bell = mp.bell
polyexp = mp.polyexp
expm1 = mp.expm1
powm1 = mp.powm1
unitroots = mp.unitroots
cyclotomic = mp.cyclotomic
mangoldt = mp.mangoldt
secondzeta = mp.secondzeta
nzeros = mp.nzeros
backlunds = mp.backlunds
lerchphi = mp.lerchphi
stirling1 = mp.stirling1
stirling2 = mp.stirling2
# _ctx_mp._mpf_module.mpf = mp.mpf
# _ctx_mp._mpf_module.mpc = mp.mpc
make_mpf = mp.make_mpf
make_mpc = mp.make_mpc
extraprec = mp.extraprec
extradps = mp.extradps
workprec = mp.workprec
workdps = mp.workdps
autoprec = mp.autoprec
maxcalls = mp.maxcalls
memoize = mp.memoize
mag = mp.mag
bernfrac = mp.bernfrac
qfrom = mp.qfrom
mfrom = mp.mfrom
kfrom = mp.kfrom
taufrom = mp.taufrom
qbarfrom = mp.qbarfrom
ellipfun = mp.ellipfun
jtheta = mp.jtheta
kleinj = mp.kleinj
qp = mp.qp
qhyper = mp.qhyper
qgamma = mp.qgamma
qfac = mp.qfac
nint_distance = mp.nint_distance
# plot = mp.plot
# cplot = mp.cplot
# splot = mp.splot
odefun = mp.odefun
jacobian = mp.jacobian
findroot = mp.findroot
multiplicity = mp.multiplicity
isinf = mp.isinf
isnan = mp.isnan
isnormal = mp.isnormal
isint = mp.isint
isfinite = mp.isfinite
almosteq = mp.almosteq
nan = mp.nan
rand = mp.rand
absmin = mp.absmin
absmax = mp.absmax
fraction = mp.fraction
linspace = mp.linspace
arange = mp.arange
mpc = mp.mpc
mpi = iv._mpi
nstr = mp.nstr
nprint = mp.nprint
chop = mp.chop
fneg = mp.fneg
fadd = mp.fadd
fsub = mp.fsub
fmul = mp.fmul
fdiv = mp.fdiv
fprod = mp.fprod
quad = mp.quad
quadgl = mp.quadgl
quadts = mp.quadts
quadosc = mp.quadosc
invertlaplace = mp.invertlaplace
invlaptalbot = mp.invlaptalbot
invlapstehfest = mp.invlapstehfest
invlapdehoog = mp.invlapdehoog
pslq = mp.pslq
identify = mp.identify
findpoly = mp.findpoly
richardson = mp.richardson
shanks = mp.shanks
levin = mp.levin
cohen_alt = mp.cohen_alt
nsum = mp.nsum
nprod = mp.nprod
difference = mp.difference
diff = mp.diff
diffs = mp.diffs
diffs_prod = mp.diffs_prod
diffs_exp = mp.diffs_exp
diffun = mp.diffun
differint = mp.differint
taylor = mp.taylor
pade = mp.pade
polyval = mp.polyval
polyroots = mp.polyroots
fourier = mp.fourier
fourierval = mp.fourierval
sumem = mp.sumem
sumap = mp.sumap
chebyfit = mp.chebyfit
limit = mp.limit
matrix = mp.matrix
eye = mp.eye
diag = mp.diag
zeros = mp.zeros
ones = mp.ones
hilbert = mp.hilbert
randmatrix = mp.randmatrix
swap_row = mp.swap_row
extend = mp.extend
norm = mp.norm
mnorm = mp.mnorm
lu_solve = mp.lu_solve
lu = mp.lu
qr = mp.qr
unitvector = mp.unitvector
inverse = mp.inverse
residual = mp.residual
qr_solve = mp.qr_solve
cholesky = mp.cholesky
cholesky_solve = mp.cholesky_solve
det = mp.det
cond = mp.cond
hessenberg = mp.hessenberg
schur = mp.schur
eig = mp.eig
eig_sort = mp.eig_sort
eigsy = mp.eigsy
eighe = mp.eighe
eigh = mp.eigh
svd_r = mp.svd_r
svd_c = mp.svd_c
svd = mp.svd
gauss_quadrature = mp.gauss_quadrature
expm = mp.expm
sqrtm = mp.sqrtm
powm = mp.powm
logm = mp.logm
sinm = mp.sinm
cosm = mp.cosm
mpf = mp.mpf
j = mp.j
exp = mp.exp
expj = mp.expj
expjpi = mp.expjpi
ln = mp.ln
im = mp.im
re = mp.re
inf = mp.inf
ninf = mp.ninf
sign = mp.sign
eps = mp.eps
pi = mp.pi
ln2 = mp.ln2
ln10 = mp.ln10
phi = mp.phi
e = mp.e
euler = mp.euler
catalan = mp.catalan
khinchin = mp.khinchin
glaisher = mp.glaisher
apery = mp.apery
degree = mp.degree
twinprime = mp.twinprime
mertens = mp.mertens
ldexp = mp.ldexp
frexp = mp.frexp
fsum = mp.fsum
fdot = mp.fdot
sqrt = mp.sqrt
cbrt = mp.cbrt
exp = mp.exp
ln = mp.ln
log = mp.log
log10 = mp.log10
power = mp.power
cos = mp.cos
sin = mp.sin
tan = mp.tan
cosh = mp.cosh
sinh = mp.sinh
tanh = mp.tanh
acos = mp.acos
asin = mp.asin
atan = mp.atan
asinh = mp.asinh
acosh = mp.acosh
atanh = mp.atanh
sec = mp.sec
csc = mp.csc
cot = mp.cot
sech = mp.sech
csch = mp.csch
coth = mp.coth
asec = mp.asec
acsc = mp.acsc
acot = mp.acot
asech = mp.asech
acsch = mp.acsch
acoth = mp.acoth
cospi = mp.cospi
sinpi = mp.sinpi
sinc = mp.sinc
sincpi = mp.sincpi
cos_sin = mp.cos_sin
cospi_sinpi = mp.cospi_sinpi
fabs = mp.fabs
re = mp.re
im = mp.im
conj = mp.conj
floor = mp.floor
ceil = mp.ceil
nint = mp.nint
frac = mp.frac
root = mp.root
nthroot = mp.nthroot
hypot = mp.hypot
fmod = mp.fmod
ldexp = mp.ldexp
frexp = mp.frexp
sign = mp.sign
arg = mp.arg
phase = mp.phase
polar = mp.polar
rect = mp.rect
degrees = mp.degrees
radians = mp.radians
atan2 = mp.atan2
fib = mp.fib
fibonacci = mp.fibonacci
lambertw = mp.lambertw
zeta = mp.zeta
altzeta = mp.altzeta
gamma = mp.gamma
rgamma = mp.rgamma
factorial = mp.factorial
fac = mp.fac
fac2 = mp.fac2
beta = mp.beta
betainc = mp.betainc
psi = mp.psi
polygamma = mp.polygamma
digamma = mp.digamma
harmonic = mp.harmonic
bernoulli = mp.bernoulli
bernfrac = mp.bernfrac
stieltjes = mp.stieltjes
hurwitz = mp.hurwitz
dirichlet = mp.dirichlet
bernpoly = mp.bernpoly
eulerpoly = mp.eulerpoly
eulernum = mp.eulernum
polylog = mp.polylog
clsin = mp.clsin
clcos = mp.clcos
gammainc = mp.gammainc
gammaprod = mp.gammaprod
binomial = mp.binomial
rf = mp.rf
ff = mp.ff
hyper = mp.hyper
hyp0f1 = mp.hyp0f1
hyp1f1 = mp.hyp1f1
hyp1f2 = mp.hyp1f2
hyp2f1 = mp.hyp2f1
hyp2f2 = mp.hyp2f2
hyp2f0 = mp.hyp2f0
hyp2f3 = mp.hyp2f3
hyp3f2 = mp.hyp3f2
hyperu = mp.hyperu
hypercomb = mp.hypercomb
meijerg = mp.meijerg
appellf1 = mp.appellf1
appellf2 = mp.appellf2
appellf3 = mp.appellf3
appellf4 = mp.appellf4
hyper2d = mp.hyper2d
bihyper = mp.bihyper
erf = mp.erf
erfc = mp.erfc
erfi = mp.erfi
erfinv = mp.erfinv
npdf = mp.npdf
ncdf = mp.ncdf
expint = mp.expint
e1 = mp.e1
ei = mp.ei
li = mp.li
ci = mp.ci
si = mp.si
chi = mp.chi
shi = mp.shi
fresnels = mp.fresnels
fresnelc = mp.fresnelc
airyai = mp.airyai
airybi = mp.airybi
airyaizero = mp.airyaizero
airybizero = mp.airybizero
scorergi = mp.scorergi
scorerhi = mp.scorerhi
ellipk = mp.ellipk
ellipe = mp.ellipe
ellipf = mp.ellipf
ellippi = mp.ellippi
elliprc = mp.elliprc
elliprj = mp.elliprj
elliprf = mp.elliprf
elliprd = mp.elliprd
elliprg = mp.elliprg
agm = mp.agm
jacobi = mp.jacobi
chebyt = mp.chebyt
chebyu = mp.chebyu
legendre = mp.legendre
legenp = mp.legenp
legenq = mp.legenq
hermite = mp.hermite
pcfd = mp.pcfd
pcfu = mp.pcfu
pcfv = mp.pcfv
pcfw = mp.pcfw
gegenbauer = mp.gegenbauer
laguerre = mp.laguerre
spherharm = mp.spherharm
besselj = mp.besselj
j0 = mp.j0
j1 = mp.j1
besseli = mp.besseli
bessely = mp.bessely
besselk = mp.besselk
besseljzero = mp.besseljzero
besselyzero = mp.besselyzero
hankel1 = mp.hankel1
hankel2 = mp.hankel2
struveh = mp.struveh
struvel = mp.struvel
angerj = mp.angerj
webere = mp.webere
lommels1 = mp.lommels1
lommels2 = mp.lommels2
whitm = mp.whitm
whitw = mp.whitw
ber = mp.ber
bei = mp.bei
ker = mp.ker
kei = mp.kei
coulombc = mp.coulombc
coulombf = mp.coulombf
coulombg = mp.coulombg
barnesg = mp.barnesg
superfac = mp.superfac
hyperfac = mp.hyperfac
loggamma = mp.loggamma
siegeltheta = mp.siegeltheta
siegelz = mp.siegelz
grampoint = mp.grampoint

mpq_1 = mpq((1,1))
mpq_0 = mpq((0,1))
mpq_1_2 = mpq((1,2))
mpq_3_2 = mpq((3,2))
mpq_1_4 = mpq((1,4))
mpq_1_16 = mpq((1,16))
mpq_3_16 = mpq((3,16))
mpq_5_2 = mpq((5,2))
mpq_3_4 = mpq((3,4))
mpq_7_4 = mpq((7,4))
mpq_5_4 = mpq((5,4))
# get_complex = re.compile(r'^\(?(?P<re>[\+\-]?\d*\.?\d*(e[\+\-]?\d+)?)??'
                        #  r'(?P<im>[\+\-]?\d*\.?\d*(e[\+\-]?\d+)?j)?\)?$')
