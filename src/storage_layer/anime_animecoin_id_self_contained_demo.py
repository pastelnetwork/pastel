import time, base64, hashlib, random, os, io
from time import sleep
import nacl.encoding
import nacl.signing
import nacl.secret
import nacl.utils
import pyqrcode
import pyotp
from PIL import Image, ImageFont, ImageDraw, ImageOps

#Dependencies: pip install nacl pyqrcode 
#Eddsa code is from the RFC documentation: https://datatracker.ietf.org/doc/html/rfc8032

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
        
def get_blake2b_sha3_512_merged_hash_func(input_data_or_string):
    if isinstance(input_data_or_string, str):
        input_data_or_string = input_data_or_string.encode('utf-8')
    hash_of_input_data = hashlib.sha3_512(input_data_or_string).hexdigest() + hashlib.blake2b(input_data_or_string).hexdigest()
    return hash_of_input_data

def get_raw_blake2b_sha3_512_merged_hash_func(input_data):
    hash_of_input_data = hashlib.sha3_512(input_data).digest() + hashlib.blake2b(input_data).digest()
    return hash_of_input_data

def sqrt4k3(x,p): 
    return pow(x,(p + 1)//4,p)

#Compute candidate square root of x modulo p, with p = 5 (mod 8).
def sqrt8k5(x,p):
    y = pow(x,(p+3)//8,p)
    #If the square root exists, it is either y, or y*2^(p-1)/4.
    if (y * y) % p == x % p: return y
    else:
        z = pow(2,(p - 1)//4,p)
        return (y * z) % p

#Decode a hexadecimal string representation of integer.
def hexi(s): 
    return int.from_bytes(bytes.fromhex(s), byteorder="big")

#Rotate a word x by b places to the left.
def rol(x,b): 
    return ((x << b) | (x >> (64 - b))) & (2**64-1)

#From little-endian.
def from_le(s): 
    return int.from_bytes(s, byteorder="little")

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
    curve_self_check(Edwards521Point.stdbase())

#PureEdDSA scheme. Limitation: Only b mod 8 = 0 is handled.
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
        #if privkey is None: privkey= os.urandom(self.b//8) #Replaced with more secure nacl version which uses this: https://news.ycombinator.com/item?id=11562401
        if privkey is None: privkey= nacl.utils.random(self.b//8)
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
    
def Ed521_inthash(data, ctx, hflag):
    if (ctx is not None and len(ctx) > 0) or hflag:
        raise ValueError("Contexts/hashes not supported")
    return get_raw_blake2b_sha3_512_merged_hash_func(data)


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

#The base PureEdDSA schemes.
pEd521 = PureEdDSA({"B":Edwards521Point.stdbase(),"H":Ed521_inthash})

#Our signature schemes.
Ed521 = EdDSA(pEd521, None)

def eddsa_obj(name):
    if name == "Ed521": Ed521
    raise NotImplementedError("Algorithm not implemented")

def animecoin_id_keypair_generation_func():
    print('Generating Eddsa 521 keypair now...')
    with MyTimer():
        input_length = 521*2
        animecoin_id_private_key, animecoin_id_public_key = Ed521.keygen(nacl.utils.random(input_length))
        animecoin_id_private_key_b16_encoded = base64.b16encode(animecoin_id_private_key).decode('utf-8')
        animecoin_id_public_key_b16_encoded = base64.b16encode(animecoin_id_public_key).decode('utf-8')
        return animecoin_id_private_key_b16_encoded, animecoin_id_public_key_b16_encoded

def animecoin_id_write_signature_on_data_func(input_data_or_string, animecoin_id_private_key_b16_encoded, animecoin_id_public_key_b16_encoded):
   print('Generating Eddsa 521 signature now...')
   with MyTimer():
       if isinstance(input_data_or_string, str):
           input_data_or_string = input_data_or_string.encode('utf-8')
       animecoin_id_private_key = base64.b16decode(animecoin_id_private_key_b16_encoded)
       animecoin_id_public_key = base64.b16decode(animecoin_id_public_key_b16_encoded)
       sleep(0.1*random.random()) #To combat side-channel attacks
       animecoin_id_signature = Ed521.sign(animecoin_id_private_key, animecoin_id_public_key, input_data_or_string)
       animecoin_id_signature_b16_encoded = base64.b16encode(animecoin_id_signature).decode('utf-8')
       sleep(0.1*random.random())
       return animecoin_id_signature_b16_encoded

def animecoin_id_verify_signature_with_public_key_func(input_data_or_string, animecoin_id_signature_b16_encoded, animecoin_id_public_key_b16_encoded):
    print('Verifying Eddsa 521 signature now...')
    with MyTimer():
        if isinstance(input_data_or_string, str):
            input_data_or_string = input_data_or_string.encode('utf-8')
        animecoin_id_signature = base64.b16decode(animecoin_id_signature_b16_encoded)
        animecoin_id_public_key = base64.b16decode(animecoin_id_public_key_b16_encoded)
        sleep(0.1*random.random())
        verified = Ed521.verify(animecoin_id_public_key, input_data_or_string, animecoin_id_signature)
        sleep(0.1*random.random())
        if verified:
            print('Signature is valid!')
        else:
            print('Warning! Signature was NOT valid!')
        return verified

def set_up_google_authenticator_for_private_key_encryption_func():
    secret = pyotp.random_base32() # returns a 16 character base32 secret. Compatible with Google Authenticator and other OTP apps
    os.environ['ANIME_OTP_SECRET'] = secret
    with open('otp_secret.txt','w') as f:
        f.write(secret)
    totp = pyotp.totp.TOTP(secret)
    #google_auth_uri = urllib.parse.quote_plus(totp.provisioning_uri("user@domain.com", issuer_name="Animecoin"))
    google_auth_uri = totp.provisioning_uri("user@user.com", issuer_name="Animecoin")
    #current_otp = totp.now() 
    qr_error_correcting_level = 'M' # L, M, Q, H
    qr_encoding_type = 'binary'
    qr_scale_factor = 6
    google_auth_qr_code = pyqrcode.create(google_auth_uri, error=qr_error_correcting_level, version=16, mode=qr_encoding_type)    
    google_auth_qr_code_png_string = google_auth_qr_code.png_as_base64_str(scale=qr_scale_factor)
    google_auth_qr_code_png_data = base64.b64decode(google_auth_qr_code_png_string)
    pil_qr_code_image = Image.open(io.BytesIO(google_auth_qr_code_png_data))
    img_width, img_height = pil_qr_code_image.size #getting the base image's size
    if pil_qr_code_image.mode != 'RGB':
        pil_qr_code_image = pil_qr_code_image.convert("RGB")
    pil_qr_code_image = ImageOps.expand(pil_qr_code_image, border=(600,300,600,0))
    drawing_context = ImageDraw.Draw(pil_qr_code_image)
    font1 = ImageFont.truetype('FreeSans.ttf', 30)
    font2 = ImageFont.truetype('FreeSans.ttf', 20)
    warning_message = 'Warning! Once you close this window, this QR code will be lost! You should write down your Google Auth URI string (shown below) as a backup, which will allow you to regenerate the QR code image.'
    drawing_context.text((50,65), google_auth_uri,(255,255,255),font=font1) 
    drawing_context.text((50,5), warning_message,(255,255,255),font=font2) 
    pil_qr_code_image.show()

def regenerate_google_auth_qr_code_from_secret_func():
    otp_secret = input('Enter your Google Authenticator Secret in all upper case and numbers:\n')
    otp_secret_character_set = 'ABCDEF1234567890'
    assert(len(otp_secret)==16)
    assert([(x in otp_secret_character_set) for x in otp_secret])
    totp = pyotp.totp.TOTP(otp_secret)
    google_auth_uri = totp.provisioning_uri("user@user.com", issuer_name="Animecoin")
    qr_error_correcting_level = 'M' # L, M, Q, H
    qr_encoding_type = 'binary'
    qr_scale_factor = 6
    google_auth_qr_code = pyqrcode.create(google_auth_uri, error=qr_error_correcting_level, version=16, mode=qr_encoding_type)    
    google_auth_qr_code_png_string = google_auth_qr_code.png_as_base64_str(scale=qr_scale_factor)
    google_auth_qr_code_png_data = base64.b64decode(google_auth_qr_code_png_string)
    pil_qr_code_image = Image.open(io.BytesIO(google_auth_qr_code_png_data))
    pil_qr_code_image.show()
    return otp_secret

def generate_current_otp_string_from_user_input_func():
    otp_secret = input('\n\nEnter your Google Authenticator Secret in all upper case and numbers:\n\n')
    otp_secret_character_set = 'ABCDEF1234567890'
    assert(len(otp_secret)==16)
    assert([(x in otp_secret_character_set) for x in otp_secret])
    totp = pyotp.totp.TOTP(otp_secret)
    current_otp = totp.now()
    return current_otp

def generate_current_otp_string_func():
    try:
        otp_secret = os.environ['ANIME_OTP_SECRET']
    except:
        with open('otp_secret.txt','r') as f:
            otp_secret = f.read()    
    otp_secret_character_set = 'ABCDEF1234567890'
    assert(len(otp_secret)==16)
    assert([(x in otp_secret_character_set) for x in otp_secret])
    totp = pyotp.totp.TOTP(otp_secret)
    current_otp = totp.now()
    return current_otp

def generate_and_store_key_for_nacl_box_func():
    box_key = nacl.utils.random(nacl.secret.SecretBox.KEY_SIZE)  # This must be kept secret, this is the combination to your safe
    box_key_base64 = base64.b64encode(box_key).decode('utf-8')
    with open('box_key.bin','w') as f:
        f.write(box_key_base64)
    print('This is the key for encrypting the Animecoin ID private key (using NACL box) in Base64: '+ box_key_base64)
    print('The key has been stored as an environment varibale on the local machine and saved as a file in the working directory. You should also write this key down as a backup.')
    os.environ['NACL_KEY'] = box_key_base64

def get_nacl_box_key_from_user_input_func():
    box_key_base64 = input('\n\nEnter your NACL box key in Base64:\n\n')
    assert(len(box_key_base64)==44)
    box_key = base64.b64decode(box_key_base64)
    return box_key

def get_nacl_box_key_from_environment_or_file_func():
    try:
        box_key_base64 = os.environ['NACL_KEY']
    except:
        with open('box_key.bin','r') as f:
            box_key_base64 = f.read()
    assert(len(box_key_base64)==44)
    box_key = base64.b64decode(box_key_base64)
    return box_key

def write_animecoin_public_and_private_key_to_file_func(animecoin_id_public_key_b16_encoded, animecoin_id_private_key_b16_encoded):
    animecoin_id_public_key_export_format = "-----BEGIN ED521 PUBLIC KEY-----\n" + animecoin_id_public_key_b16_encoded + "\n-----END ED521 PUBLIC KEY-----"
    animecoin_id_private_key_export_format = "-----BEGIN ED521 PRIVATE KEY-----\n" + animecoin_id_private_key_b16_encoded + "\n-----END ED521 PRIVATE KEY-----"
    try:
         box_key = get_nacl_box_key_from_environment_or_file_func()
    except:
        print("\n\nCan't find OTP secret in environment variables! Enter Secret below:\n\n")
        box_key = get_nacl_box_key_from_user_input_func()
    box = nacl.secret.SecretBox(box_key) # This is your safe, you can use it to encrypt or decrypt messages
    animecoin_id_private_key_export_format__encrypted = box.encrypt(animecoin_id_private_key_export_format.encode('utf-8'))
    animecoin_id_keys_storage_folder_path = os.getcwd() + os.sep + 'animecoin_id_key_files' + os.sep
    if not os.path.isdir(animecoin_id_keys_storage_folder_path):
        try:
            os.makedirs(animecoin_id_keys_storage_folder_path)
        except:
            print('Error creating directory-- instead saving to current working directory!')
            animecoin_id_keys_storage_folder_path = os.getcwd() + os.sep
    with open(animecoin_id_keys_storage_folder_path + 'animecoin_id_ed521_public_key.pem','w') as f:
        f.write(animecoin_id_public_key_export_format)
    with open(animecoin_id_keys_storage_folder_path + 'animecoin_id_ed521_private_key.pem','wb') as f:
        f.write(animecoin_id_private_key_export_format__encrypted)
        
def import_animecoin_public_and_private_keys_from_pem_files_func(use_require_otp):
    #use_require_otp = 1
    animecoin_id_keys_storage_folder_path = os.getcwd() + os.sep + 'animecoin_id_key_files' + os.sep
    if not os.path.isdir(animecoin_id_keys_storage_folder_path):
        print("Can't find key storage directory, trying to use current working directory instead!")
        animecoin_id_keys_storage_folder_path = os.getcwd() + os.sep
    animecoin_id_public_key_pem_filepath = animecoin_id_keys_storage_folder_path + 'animecoin_id_ed521_public_key.pem'
    animecoin_id_private_key_pem_filepath = animecoin_id_keys_storage_folder_path + 'animecoin_id_ed521_private_key.pem'
    if (os.path.isfile(animecoin_id_public_key_pem_filepath) and os.path.isfile(animecoin_id_private_key_pem_filepath)):
        with open(animecoin_id_public_key_pem_filepath,'r') as f:
            animecoin_id_public_key_export_format = f.read()
        with open(animecoin_id_private_key_pem_filepath,'rb') as f:
            animecoin_id_private_key_export_format__encrypted = f.read()
        if use_require_otp:
            try:
                otp_string = generate_current_otp_string_func()
            except:
                otp_string = generate_current_otp_string_from_user_input_func()
            otp_from_user_input = input('\nPlease Enter your Animecoin Google Authenticator Code:\n\n')
            assert(len(otp_from_user_input)==6)
            otp_correct = (otp_from_user_input == otp_string)
        else:
            otp_correct = True
            
        if otp_correct:
            try:
                box_key = get_nacl_box_key_from_environment_or_file_func()
            except:
                print("\n\nCan't find OTP secret in environment variables! Enter Secret below:\n\n")
                box_key = get_nacl_box_key_from_user_input_func()
            box = nacl.secret.SecretBox(box_key)
            animecoin_id_public_key_b16_encoded = animecoin_id_public_key_export_format.replace("-----BEGIN ED521 PUBLIC KEY-----\n","").replace("\n-----END ED521 PUBLIC KEY-----","")
            animecoin_id_private_key_export_format = box.decrypt(animecoin_id_private_key_export_format__encrypted)
            animecoin_id_private_key_export_format = animecoin_id_private_key_export_format.decode('utf-8')
            animecoin_id_private_key_b16_encoded = animecoin_id_private_key_export_format.replace("-----BEGIN ED521 PRIVATE KEY-----\n","").replace("\n-----END ED521 PRIVATE KEY-----","")
    else:
        animecoin_id_public_key_b16_encoded = ''
        animecoin_id_private_key_b16_encoded = ''
    return animecoin_id_public_key_b16_encoded, animecoin_id_private_key_b16_encoded

def generate_qr_codes_from_animecoin_keypair_func(animecoin_id_public_key_b16_encoded, animecoin_id_private_key_b16_encoded):
    qr_error_correcting_level = 'M' # L, M, Q, H
    qr_encoding_type = 'alphanumeric'
    qr_scale_factor = 6
    animecoin_id_public_key_b16_encoded_qr_code = pyqrcode.create(animecoin_id_public_key_b16_encoded, error=qr_error_correcting_level, version=16, mode=qr_encoding_type)
    animecoin_id_private_key_b16_encoded_qr_code = pyqrcode.create(animecoin_id_private_key_b16_encoded, error=qr_error_correcting_level, version=31, mode=qr_encoding_type)
    animecoin_id_keys_storage_folder_path = os.getcwd() + os.sep + 'animecoin_id_key_files' + os.sep
    if not os.path.isdir(animecoin_id_keys_storage_folder_path):
        try:
            os.makedirs(animecoin_id_keys_storage_folder_path)
        except:
            print('Error creating directory-- instead saving to current working directory!')
            animecoin_id_keys_storage_folder_path = os.getcwd() + os.sep
    animecoin_id_public_key_b16_encoded_qr_code.png(file=animecoin_id_keys_storage_folder_path + 'animecoin_id_ed521_public_key_qr_code.png', scale=qr_scale_factor)
    animecoin_id_private_key_b16_encoded_qr_code.png(file=animecoin_id_keys_storage_folder_path + 'animecoin_id_ed521_private_key_qr_code.png',scale=qr_scale_factor)

#generate_and_store_key_for_nacl_box_func()

    
use_demonstrate_eddsa_crypto = 0

if use_demonstrate_eddsa_crypto:
    #Example using a fake artwork combined metadata string:
    input_data_text = "1.00 || 2018-05-20 17:01:17:110168 || 523606 || 0000000000000000002c65b5e9be26fba6e0fb2ed9e665f567260ee14bb0d390 || 2512 || 00000000000000000020cf882b78a089514eb19583b36171e62b16994f94e6b7 || -----BEGIN ED521 PRIVATE KEY-----3677B6B51908E462367834786C9E9D4D277BBD96988AD228C30787E183DCC698E59DEBC0E1DB5F636893DE484281F9B96651B3D431DF5C60050C9F799E6251092C7A72282EAD69DF7191F4C24C853CC4C8AD2A24B6ED833AD98C55BC27D4C654B6FD0010A8D832C057731C7D3A6567BB09C0FE83420D7949493FAB2129B0935BAD47BCB6C1159B66F23953DE280AD2D2BD954FB7A589C3FEF4E6056317BA9AE9BA82AB7D4C0C501893BCA3329642DBBF8C9BFE555B2AAB9500B1DAD3BC5D973234C9C734A6F9D2FDA6024F5354ED373B5981910D6DA69BECCCF6C641C00E47AD019793F33A2404D87D8E794D885C4A168B1017ECA715A42F4CD9D6BAAB5F252ECB5B1BC8402A2F6937F72CED3E4658C5AA4CF0DDA29F28A3731D2C006F836DFD05603F121E7C93625496BB942564C33A23CF8FD30BF9939D83E45C7CCF955440AF7E888024E4EBF91D12160001DDBC9CA31D045DF997F73D4A74AF3464C46BBEAE88A1F4F9290823B6994824C181E72C7FD7D084BC2E5675257BC1888E3C8A67EA1DA28DBE2D5F796B019700D4D0838CCA376426120B93733A8F6C3D5AFEEA2D869366C4CBA51A10B6838A171BCC8F2C8D079A75DC783AAB1A0E0DF66CD371316DBF9F8E4A8D3B5B9FDE1DC1C0E70259BEED1936D0B9196C9766C6A02F0EDA929166550E3E7FBD27E366FAC350D2F6D09010BA8ECD6F2139B9B45BF1FE0AFE69E49D44C3F5D76E8AA531EBBA50B55A44C41AD5B7A216595AC1A8A805236E8EDFB5D84B0B56AC7F1B376470E8B891A38A85FA3F6BBE252C8E2AA447B56C40235DA6EE7D5C7812410A301737BFCB0E461F8B0B1587DE3A6C99AFD4108D51778C4D5C42568010806E873E49565DEAA8204A17C71688148CD346BE9AD17B1A26920172A079C5322EBC931BB35EC5387223B6FEA7BE46B6D13CDE62D42E2EB64643047E7829205E36762FE061DE43D5A78270A16B46A87DAC58FC77749615033529435220A1D9BF651187DB8E1A2C2D568FA4001F6F9E8A2A242037118B0A8D89F6133A186C7ADAF6D06A5CE698D3F8466C9FF041BE42C576BD9EA1F514272D6D33E73A1D8373E88F178A18420B39C59818E1BD7B5E56CEC50AC6C668643BA80096F2D9B1D864833FD875DE6CD1E943C024CE0B729A150A59FA5D498D62A47B8E54C210BCC275EAB7541AED477C86BAE4BB8FF487D05298607E04281154E713ED86D17CB03ADCEFB923DCF6BEAF8E79A47306C1F52B770F6D4F0FF8E9FB63F8048B64FF308C0324FB47E9771C3C5E9CD2E72763C0A8E6F5B358D8BE20CE248C1258CD1B0291D3F039E1761CC86483DB417ED680C1A7494219431E76D75866E5FC2D55E4C9493D836E78FF336569CC3482264D11B14A4EDD49919E25ADF7135FF70C9BE5900E1A0D0C8F732E5E6E463EFBD325A7F3A893BD7D7A33B38AF0B8A3CBCFE2E0D45C2372B7B32D21DAB319CDB196C3EC83-----END ED521 PRIVATE KEY-----|| 500 || Magnificent Vampire: Evil 2 || Mark Kong || || https://foxykuro.deviantart.com/ || I have been drawing since I was young! I hope everyone likes my newest works, I have been learning how to use a tablet! Hit me up on discord, username is AnimeFreak15! Thx!!! || 12.415804 || 514a10f0eec73108685ff60c8481bf8343b934fb1e5ab0f6020812ec683739f2 || 350 || 100 || 3.5 || 149 || 43 || 4 ||"
    input_data = input_data_text.encode('utf-8')
    use_require_otp = 0
    animecoin_id_public_key_b16_encoded, animecoin_id_private_key_b16_encoded = import_animecoin_public_and_private_keys_from_pem_files_func(use_require_otp)
    if animecoin_id_public_key_b16_encoded == '':
        animecoin_id_private_key_b16_encoded, animecoin_id_public_key_b16_encoded = animecoin_id_keypair_generation_func()
        write_animecoin_public_and_private_key_to_file_func(animecoin_id_public_key_b16_encoded, animecoin_id_private_key_b16_encoded)
        generate_qr_codes_from_animecoin_keypair_func(animecoin_id_public_key_b16_encoded, animecoin_id_private_key_b16_encoded)
    animecoin_id_signature_b16_encoded = animecoin_id_write_signature_on_data_func(input_data, animecoin_id_private_key_b16_encoded, animecoin_id_public_key_b16_encoded)
    verified = animecoin_id_verify_signature_with_public_key_func(input_data, animecoin_id_signature_b16_encoded, animecoin_id_public_key_b16_encoded)
