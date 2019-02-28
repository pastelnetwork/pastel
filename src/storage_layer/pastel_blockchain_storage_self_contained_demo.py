import subprocess, os.path, binascii, struct, hashlib, sys, io, os, random, glob, urllib, time, json, string, re, binascii
from urllib.request import HTTPPasswordMgrWithDefaultRealm, HTTPBasicAuthHandler, build_opener, install_opener, urlopen
import zstd
from decimal import Decimal
from time import sleep
from binascii import hexlify, unhexlify
from bitcoinrpc.authproxy import AuthServiceProxy

# requirements: pip install zstandard bitcoinrpc

# Required binaries to test with Bitcoin Core software (place in working  directory):
# Windows: bitcoind.exe -- https://www.dropbox.com/s/rfespw4brcwueby/bitcoind.exe?dl=0
# Mac: bitcoind -- https://www.dropbox.com/s/hkgjs38vzlpwv82/bitcoind?dl=0

#Parameters:
root_pastel_folder_path = '/Users/jemanuel/pastel/'
path_to_zstd_compression_dictionaries = os.path.join(root_pastel_folder_path,'zstd_compression_dictionaries' + os.sep)
path_where_pastel_html_ticket_templates_are_stored = './'

#Pay to fake multi-sig params:
rpc_user = 'test' 
rpc_password ='testpw'
rpc_port = '18443' #mainnet: 8332; testnet: 18332; regtest: 18443 
rpc_connection_string = 'http://'+rpc_user+':'+rpc_password+'@127.0.0.1:'+rpc_port
base_transaction_amount = 0.000001
COIN = 100000000 #patoshis in 1 btc
FEEPERKB = Decimal(0.0001)
OP_CHECKSIG = b'\xac'
OP_CHECKMULTISIG = b'\xae'
OP_PUSHDATA1 = b'\x4c'
OP_DUP = b'\x76'
OP_HASH160 = b'\xa9'
OP_EQUALVERIFY = b'\x88'

def generate_zstd_dictionary_from_folder_path_and_file_matching_string_func(path_to_folder_containing_files_for_dictionary, file_matching_string, use_verbose=0):
    if use_verbose:
        print('Now generating compression dictionary for ZSTD:')
    list_of_matching_input_file_paths = glob.glob(path_to_folder_containing_files_for_dictionary + file_matching_string)
    for cnt, current_input_file_path in enumerate(list_of_matching_input_file_paths):
        if use_verbose:
            print('Now loading input file '+ str(cnt + 1) + ' out of ' + str(len(list_of_matching_input_file_paths)))
        with open(current_input_file_path,'rb') as f:
            if cnt == 0:
                combined_dictionary_input_data = f.read()
            else:
                new_file_data = f.read()
                combined_dictionary_input_data = combined_dictionary_input_data + new_file_data
    pastel_zstd_compression_dictionary = zstd.ZstdCompressionDict(combined_dictionary_input_data)
    if use_verbose:
        print('Done!')
    return pastel_zstd_compression_dictionary

def compress_data_with_pastel_zstd_func(input_data, pastel_zstd_compression_dictionary):
    global zstd_compression_level
    zstd_compression_level = 22 #Highest (best) compression level is 22
    zstandard_pastel_compressor = zstd.ZstdCompressor(dict_data=pastel_zstd_compression_dictionary, level=zstd_compression_level, write_content_size=True)
    if isinstance(input_data,str):
        input_data = input_data.encode('utf-8')
    pastel_zstd_compressed_data = zstandard_pastel_compressor.compress(input_data)
    return pastel_zstd_compressed_data

def decompress_data_with_pastel_zstd_func(pastel_zstd_compressed_data, pastel_zstd_compression_dictionary):
    zstandard_pastel_decompressor = zstd.ZstdDecompressor(dict_data=pastel_zstd_compression_dictionary)
    uncompressed_data = zstandard_pastel_decompressor.decompress(pastel_zstd_compressed_data)
    return uncompressed_data

def get_most_recently_modified_zstd_dictionary_file_func():
    global path_to_zstd_compression_dictionaries
    max_mtime = 0
    for dirname,subdirs,files in os.walk(path_to_zstd_compression_dictionaries):
        for fname in files:
            if '.dict' in fname:
                full_path = os.path.join(dirname, fname)
                mtime = os.stat(full_path).st_mtime
                if mtime > max_mtime:
                    max_mtime = mtime
                    max_file = fname
    return max_file

def unhexstr(str):
    return unhexlify(str.encode('utf8'))

def select_txins(value):
    unspent = list(rpc_connection.listunspent())
    random.shuffle(unspent)
    r = []
    total = 0
    for tx in unspent:
        total += tx['amount']
        r.append(tx)
        if total >= value:
            break
    if total < value:
        return None
    else:
        return (r, total)

def varint(n):
    if n < 0xfd:
        return bytes([n])
    elif n < 0xffff:
        return b'\xfd' + struct.pack('<H',n)
    else:
        assert False

def packtxin(prevout, scriptSig, seq=0xffffffff):
    return prevout[0][::-1] + struct.pack('<L',prevout[1]) + varint(len(scriptSig)) + scriptSig + struct.pack('<L', seq)

def packtxout(value, scriptPubKey):
    return struct.pack('<Q',int(value*COIN)) + varint(len(scriptPubKey)) + scriptPubKey

def packtx(txins, txouts, locktime=0):
    r = b'\x01\x00\x00\x00' # version
    r += varint(len(txins))
    for txin in txins:
        r += packtxin((unhexstr(txin['txid']),txin['vout']), b'')
    r += varint(len(txouts))
    for (value, scriptPubKey) in txouts:
        r += packtxout(value, scriptPubKey)
    r += struct.pack('<L', locktime)
    return r

def pushdata(data):
    assert len(data) < OP_PUSHDATA1[0]
    return bytes([len(data)]) + data

def pushint(n):
    assert 0 < n <= 16
    return bytes([0x51 + n-1])

def addr2bytes(s):
    digits58 = '123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz'
    n = 0
    for c in s:
        n *= 58
        if c not in digits58:
            raise ValueError
        n += digits58.index(c)
    h = '%x' % n
    if len(h) % 2:
        h = '0' + h
    for c in s:
        if c == digits58[0]:
            h = '00' + h
        else:
            break
    return unhexstr(h)[1:-4] # skip version and checksum

def checkmultisig_scriptPubKey_dump(fd):
    data = fd.read(65*3)
    if not data:
        return None
    r = pushint(1)
    n = 0
    while data:
        chunk = data[0:65]
        data = data[65:]
        if len(chunk) < 33:
            chunk += b'\x00'*(33-len(chunk))
        elif len(chunk) < 65:
            chunk += b'\x00'*(65-len(chunk))
        r += pushdata(chunk)
        n += 1
    r += pushint(n) + OP_CHECKMULTISIG
    return r

def get_sha256_hash_of_input_data_func(input_data_or_string):
    if isinstance(input_data_or_string, str):
        input_data_or_string = input_data_or_string.encode('utf-8')
    sha256_hash_of_input_data = hashlib.sha3_256(input_data_or_string).hexdigest()         
    return sha256_hash_of_input_data

def get_raw_sha256_hash_of_input_data_func(input_data_or_string):
    if isinstance(input_data_or_string, str):
        input_data_or_string = input_data_or_string.encode('utf-8')
    sha256_hash_of_input_data = hashlib.sha3_256(input_data_or_string).digest()         
    return sha256_hash_of_input_data

def get_corresponding_zstd_compression_dictionary_func(sha256_hash_of_corresponding_zstd_compression_dictionary):
    global path_to_zstd_compression_dictionaries
    list_of_available_compression_dictionary_files = glob.glob(path_to_zstd_compression_dictionaries + '*.dict')
    found_dictionary_file = 0
    if len(list_of_available_compression_dictionary_files) == 0:
        print('Error! No dictionary files were found. Try generating one or downloading one from the blockchain.')
    else:
        available_dictionary_file_hashes = [os.path.split(x)[-1].replace('.dict','') for x in list_of_available_compression_dictionary_files]
        if sha256_hash_of_corresponding_zstd_compression_dictionary not in available_dictionary_file_hashes:
            print('Error! Cannot find the compression dictionary file used! Try downloading from the blockchain the dictionary with file hash: ' + sha256_hash_of_corresponding_zstd_compression_dictionary)
        else:
            found_dictionary_file = 1
            dictionary_file_index_number = available_dictionary_file_hashes.index(sha256_hash_of_corresponding_zstd_compression_dictionary)
            dictionary_file_path = list_of_available_compression_dictionary_files[dictionary_file_index_number]
            with open(dictionary_file_path, 'rb') as f:
                reconstructed_pastel_zstd_compression_dictionary_raw_data = f.read()
                reconstructed_pastel_zstd_compression_dictionary = zstd.ZstdCompressionDict(reconstructed_pastel_zstd_compression_dictionary_raw_data)
    if not found_dictionary_file:
        print('Attempting to generate dictionary file before giving up (not guaranteed to work!)')
        reconstructed_pastel_zstd_compression_dictionary = generate_zstd_dictionary_from_folder_path_and_file_matching_string_func(path_where_pastel_html_ticket_templates_are_stored, '*ticket*.html')
        reconstructed_pastel_zstd_compression_dictionary_raw_data = reconstructed_pastel_zstd_compression_dictionary.as_bytes()
    return reconstructed_pastel_zstd_compression_dictionary
    
def store_data_in_pastel_blockchain_UTXO_func(input_data, pastel_zstd_compression_dictionary):
    global rpc_connection_string
    global path_where_pastel_html_ticket_templates_are_stored
    uncompressed_file_size_in_bytes = sys.getsizeof(input_data)
    print('Now storing preparing file for storage in blockchain. Original uncompressed file size in bytes: '  + str(uncompressed_file_size_in_bytes)+ ' bytes')
    rpc_connection = AuthServiceProxy(rpc_connection_string)
    pastel_zstd_compressed_data = compress_data_with_pastel_zstd_func(input_data, pastel_zstd_compression_dictionary)
    compressed_file_size_in_bytes = sys.getsizeof(pastel_zstd_compressed_data)
    print('Finished compressing file. Compressed file size in megabytes is '  + str(compressed_file_size_in_bytes)+ ' bytes, or ' + str(round(100*compressed_file_size_in_bytes/uncompressed_file_size_in_bytes,1))+ '% of the original size.')
    pastel_zstd_uncompressed_data = decompress_data_with_pastel_zstd_func(pastel_zstd_compressed_data, pastel_zstd_compression_dictionary)
    assert(input_data == pastel_zstd_uncompressed_data) #Check that we can decompress successfully
    pastel_zstd_compression_dictionary_raw_data = pastel_zstd_compression_dictionary.as_bytes() #Required to be able to calculate the hash of the dictionary file
    compression_dictionary_file_hash = get_raw_sha256_hash_of_input_data_func(pastel_zstd_compression_dictionary_raw_data)
    uncompressed_data_file_hash = get_raw_sha256_hash_of_input_data_func(pastel_zstd_uncompressed_data)
    compressed_data_file_hash = get_raw_sha256_hash_of_input_data_func(pastel_zstd_compressed_data)
    (txins, change) = select_txins(0)
    txouts = []
    encoded_pastel_zstd_compressed_data = hexlify(pastel_zstd_compressed_data)
    length_of_compressed_data_string = '{0:015}'.format(len(encoded_pastel_zstd_compressed_data)).encode('utf-8')
    combined_data_hex = hexlify(length_of_compressed_data_string) + hexlify(compression_dictionary_file_hash) + hexlify(uncompressed_data_file_hash) + hexlify(compressed_data_file_hash) + encoded_pastel_zstd_compressed_data + hexlify(('0'*100).encode('utf-8'))
    fd = io.BytesIO(combined_data_hex)
    while True:
        scriptPubKey = checkmultisig_scriptPubKey_dump(fd)
        if scriptPubKey is None:
            break
        value = Decimal(1/COIN)
        txouts.append((value, scriptPubKey))
        change -= value
    out_value = Decimal(base_transaction_amount) # dest output
    change -= out_value
    receiving_pastel_blockchain_address = rpc_connection.getnewaddress()
    txouts.append((out_value, OP_DUP + OP_HASH160 + pushdata(addr2bytes(receiving_pastel_blockchain_address)) + OP_EQUALVERIFY + OP_CHECKSIG))
    change_address = rpc_connection.getnewaddress() # change output
    txouts.append([change, OP_DUP + OP_HASH160 + pushdata(addr2bytes(change_address)) + OP_EQUALVERIFY + OP_CHECKSIG])
    tx = packtx(txins, txouts)
    signed_tx = rpc_connection.signrawtransaction(hexlify(tx).decode('utf-8'))
    fee = Decimal(len(signed_tx['hex'])/1000) * FEEPERKB
    change -= fee
    txouts[-1][0] = change
    final_tx = packtx(txins, txouts)
    signed_tx = rpc_connection.signrawtransaction(hexlify(final_tx).decode('utf-8'))
    assert signed_tx['complete']
    hex_signed_transaction = signed_tx['hex']
    print('Sending data transaction to address: ' + receiving_pastel_blockchain_address)
    print('Size: %d  Fee: %2.8f' % (len(hex_signed_transaction)/2, fee), file=sys.stderr)
    send_raw_transaction_result = rpc_connection.sendrawtransaction(hex_signed_transaction)
    blockchain_transaction_id = send_raw_transaction_result
    print('Transaction ID: ' + blockchain_transaction_id)
    return blockchain_transaction_id

def retrieve_data_from_pastel_blockchain_UTXO_func(blockchain_transaction_id):
    global rpc_connection_string
    global path_where_pastel_html_ticket_templates_are_stored
    global path_to_zstd_compression_dictionaries
    rpc_connection = AuthServiceProxy(rpc_connection_string)
    raw = rpc_connection.getrawtransaction(blockchain_transaction_id)
    outputs = raw.split('0100000000000000')
    encoded_hex_data = ''
    for output in outputs[1:-2]: # there are 3 65-byte parts in this that we need
        cur = 6
        encoded_hex_data += output[cur:cur+130]
        cur += 132
        encoded_hex_data += output[cur:cur+130]
        cur += 132
        encoded_hex_data += output[cur:cur+130]
    encoded_hex_data += outputs[-2][6:-4]
    reconstructed_combined_data = binascii.a2b_hex(encoded_hex_data).decode('utf-8')
    reconstructed_length_of_compressed_data_hex_string = reconstructed_combined_data[0:30] # len(hexlify('{0:015}'.format(len(encoded_pastel_zstd_compressed_data)).encode('utf-8'))) is 30
    reconstructed_length_of_compressed_data_hex_string = int(unhexstr(reconstructed_length_of_compressed_data_hex_string).decode('utf-8').lstrip('0'))
    reconstructed_combined_data__remainder_1 = reconstructed_combined_data[30:]
    length_of_standard_hash_string = len(get_sha256_hash_of_input_data_func('test'))
    reconstructed_compression_dictionary_file_hash = reconstructed_combined_data__remainder_1[0:length_of_standard_hash_string]
    reconstructed_combined_data__remainder_2 = reconstructed_combined_data__remainder_1[length_of_standard_hash_string:]
    reconstructed_uncompressed_data_file_hash = reconstructed_combined_data__remainder_2[0:length_of_standard_hash_string]
    reconstructed_combined_data__remainder_3 = reconstructed_combined_data__remainder_2[length_of_standard_hash_string:]
    reconstructed_compressed_data_file_hash = reconstructed_combined_data__remainder_3[0:length_of_standard_hash_string]
    reconstructed_combined_data__remainder_4 = reconstructed_combined_data__remainder_3[length_of_standard_hash_string:]
    reconstructed_encoded_pastel_zstd_compressed_data_padded = reconstructed_combined_data__remainder_4.replace('A','') #Note sure where this comes from; somehow it is introduced into the data (note this is "A" not "a").
    calculated_padding_length = len(reconstructed_encoded_pastel_zstd_compressed_data_padded) - reconstructed_length_of_compressed_data_hex_string 
    reconstructed_encoded_pastel_zstd_compressed_data = reconstructed_encoded_pastel_zstd_compressed_data_padded[0:-calculated_padding_length]
    reconstructed_pastel_zstd_compressed_data = unhexstr(reconstructed_encoded_pastel_zstd_compressed_data)
    hash_of_reconstructed_pastel_zstd_compressed_data = get_sha256_hash_of_input_data_func(reconstructed_pastel_zstd_compressed_data)
    assert(hash_of_reconstructed_pastel_zstd_compressed_data == reconstructed_compressed_data_file_hash)
    list_of_available_compression_dictionary_files = glob.glob(path_to_zstd_compression_dictionaries + '*.dict')
    found_dictionary_file = 0
    if len(list_of_available_compression_dictionary_files) == 0:
        print('Error! No dictionary files were found. Try generating one or downloading one from the blockchain.')
    else:
        available_dictionary_file_hashes = [os.path.split(x)[-1].replace('.dict','') for x in list_of_available_compression_dictionary_files]
        if reconstructed_compression_dictionary_file_hash not in available_dictionary_file_hashes:
            print('Error! Cannot find the compression dictionary file used! Try downloading from the blockchain the dictionary with file hash: ' + reconstructed_compression_dictionary_file_hash)
        else:
            found_dictionary_file = 1
            dictionary_file_index_number = available_dictionary_file_hashes.index(reconstructed_compression_dictionary_file_hash)
            dictionary_file_path = list_of_available_compression_dictionary_files[dictionary_file_index_number]
            with open(dictionary_file_path, 'rb') as f:
                reconstructed_pastel_zstd_compression_dictionary_raw_data = f.read()
                reconstructed_pastel_zstd_compression_dictionary = zstd.ZstdCompressionDict(reconstructed_pastel_zstd_compression_dictionary_raw_data)
    if not found_dictionary_file:
        print('Attempting to generate dictionary file before giving up (not guaranteed to work!)')
        reconstructed_pastel_zstd_compression_dictionary = generate_zstd_dictionary_from_folder_path_and_file_matching_string_func(path_where_pastel_html_ticket_templates_are_stored, '*ticket*.html')
        reconstructed_pastel_zstd_compression_dictionary_raw_data = reconstructed_pastel_zstd_compression_dictionary.as_bytes()
    hash_of_compression_dictionary = get_sha256_hash_of_input_data_func(reconstructed_pastel_zstd_compression_dictionary_raw_data)
    assert(hash_of_compression_dictionary == reconstructed_compression_dictionary_file_hash)
    reconstructed_pastel_zstd_uncompressed_data = decompress_data_with_pastel_zstd_func(reconstructed_pastel_zstd_compressed_data, reconstructed_pastel_zstd_compression_dictionary)
    hash_of_reconstructed_pastel_zstd_uncompressed_data = get_sha256_hash_of_input_data_func(reconstructed_pastel_zstd_uncompressed_data)
    assert(hash_of_reconstructed_pastel_zstd_uncompressed_data == reconstructed_uncompressed_data_file_hash)
    print('Successfully reconstructed and decompressed data!')
    return reconstructed_pastel_zstd_uncompressed_data
    
use_demonstrate_blockchain_UTXO_storage = 0

if use_demonstrate_blockchain_UTXO_storage: 
    
    if 0: #Start up bitcoind using the regtest network and generate some coins to use:
        rpc_auth_command_string = '-rpcuser=' + rpc_user + ' -rpcpassword=' + rpc_password
        bitcoind_launch_string = 'bitcoind ' + rpc_auth_command_string + ' -regtest -server -addresstype=legacy' #Without -addresstype=legacy the current bitcoind will default to using segwit addresses, which suck (you can't sign messages with them)
        proc = subprocess.Popen(bitcoind_launch_string, shell=True)
        sleep(5)
        rpc_connection = AuthServiceProxy(rpc_connection_string)
        block_generation_output = rpc_connection.generate(101) #Generate some coins to work with
    
    #Sample file to store in blockchain-- example html trade ticket:
    link_to_blockchain_storage_example_file = 'http://149.28.34.59/Pastel_Trade_Ticket__TradeID__1502588M__DateTime_Submitted__2018_05_06_02_41_03_571550.html' 
    response = urllib.request.urlopen(link_to_blockchain_storage_example_file)
    response_html_string = response.read()

    list_of_available_compression_dictionary_files = glob.glob(path_to_zstd_compression_dictionaries + '*.dict')
    if len(list_of_available_compression_dictionary_files) == 0:
        pastel_zstd_compression_dictionary = generate_zstd_dictionary_from_folder_path_and_file_matching_string_func(path_where_pastel_html_ticket_templates_are_stored, '*.html')
        pastel_zstd_compression_dictionary_raw_data = pastel_zstd_compression_dictionary.as_bytes()
        compression_dictionary_hash = get_sha256_hash_of_input_data_func(pastel_zstd_compression_dictionary_raw_data)
        if not os.path.isdir(path_to_zstd_compression_dictionaries):
            try:
                os.makedirs(path_to_zstd_compression_dictionaries)
            except:
                print('Error creating directory-- instead saving to current working directory!')
                path_to_zstd_compression_dictionaries = os.getcwd() + os.sep
        with open(path_to_zstd_compression_dictionaries + compression_dictionary_hash + '.dict','wb') as f:
            f.write(pastel_zstd_compression_dictionary_raw_data)
    else:
        #Get most recently modified file here:
        most_recently_modified_dictionary_file_name = get_most_recently_modified_zstd_dictionary_file_func()
        most_recently_modified_dictionary_file_hash = most_recently_modified_dictionary_file_name.replace('.dict','')
        pastel_zstd_compression_dictionary = get_corresponding_zstd_compression_dictionary_func(most_recently_modified_dictionary_file_hash)
        
    rpc_connection = AuthServiceProxy(rpc_connection_string)
    input_data = response_html_string
    blockchain_transaction_id = store_data_in_pastel_blockchain_UTXO_func(input_data, pastel_zstd_compression_dictionary)
    
    reconstructed_pastel_zstd_uncompressed_data = retrieve_data_from_pastel_blockchain_UTXO_func(blockchain_transaction_id)
    reconstructed_pastel_zstd_uncompressed_data_decoded = reconstructed_pastel_zstd_uncompressed_data.decode('utf-8')
    assert(reconstructed_pastel_zstd_uncompressed_data_decoded == input_data.decode('utf-8'))
    
    #_______________________________________
    
    rpc_connection = AuthServiceProxy(rpc_connection_string)
    link_to_second_blockchain_storage_example_file = 'http://149.28.34.59/artwork_metadata_ticket__2018_05_20__18_41_17_486305.html' 
    second_response = urllib.request.urlopen(link_to_second_blockchain_storage_example_file)
    second_response_html_string = second_response.read()
    second_blockchain_transaction_id = store_data_in_pastel_blockchain_UTXO_func(second_response_html_string, pastel_zstd_compression_dictionary)
    second_reconstructed_pastel_zstd_uncompressed_data = retrieve_data_from_pastel_blockchain_UTXO_func(second_blockchain_transaction_id)
    second_reconstructed_pastel_zstd_uncompressed_data_decoded = second_reconstructed_pastel_zstd_uncompressed_data.decode('utf-8')
    assert(second_reconstructed_pastel_zstd_uncompressed_data_decoded == second_response_html_string.decode('utf-8'))