import subprocess, json, os.path, binascii, struct, re, hashlib, sys, io, os, random, base64, glob
import zstd
from math import ceil, floor, sqrt
from decimal import Decimal
from time import sleep
from binascii import crc32, hexlify, unhexlify
from bitcoinrpc.authproxy import AuthServiceProxy, JSONRPCException
from anime_utility_functions_v1 import generate_zstd_dictionary_from_folder_path_and_file_matching_string_func, compress_data_with_animecoin_zstd_func, \
        decompress_data_with_animecoin_zstd_func, get_all_animecoin_directories_func, get_all_animecoin_parameters_func
# pip install jsonrpc
#Get various settings:
anime_metadata_format_version_number, minimum_total_number_of_unique_copies_of_artwork, maximum_total_number_of_unique_copies_of_artwork, target_number_of_nodes_per_unique_block_hash, target_block_redundancy_factor, desired_block_size_in_bytes, \
remote_node_chunkdb_refresh_time_in_minutes, remote_node_image_fingerprintdb_refresh_time_in_minutes, percentage_of_block_files_to_randomly_delete, percentage_of_block_files_to_randomly_corrupt, percentage_of_each_selected_file_to_be_randomly_corrupted, \
registration_fee_anime_per_megabyte_of_images_pre_difficulty_adjustment, example_list_of_valid_masternode_ip_addresses, forfeitable_deposit_to_initiate_registration_as_percentage_of_adjusted_registration_fee, nginx_ip_whitelist_override_addresses, \
example_animecoin_masternode_blockchain_address, example_trader_blockchain_address, example_artists_receiving_blockchain_address, rpc_connection_string, max_number_of_blocks_to_download_before_checking, earliest_possible_artwork_signing_date, \
maximum_length_in_characters_of_text_field, maximum_combined_image_size_in_megabytes_for_single_artwork, nsfw_score_threshold, duplicate_image_threshold, dupe_detection_model_1_name, dupe_detection_model_2_name, \
dupe_detection_model_3_name, max_image_preview_pixel_dimension = get_all_animecoin_parameters_func()
#Get various directories:
root_animecoin_folder_path, folder_path_of_art_folders_to_encode, block_storage_folder_path, folder_path_of_remote_node_sqlite_files, reconstructed_file_destination_folder_path, \
misc_masternode_files_folder_path, masternode_keypair_db_file_path, trade_ticket_files_folder_path, completed_trade_ticket_files_folder_path, pending_trade_ticket_files_folder_path, \
temp_files_folder_path, path_to_zstd_compression_dictionaries, prepared_final_art_zipfiles_folder_path, chunk_db_file_path, file_storage_log_file_path, nginx_allowed_ip_whitelist_file_path, path_to_animecoin_trade_ticket_template, \
artist_final_signature_files_folder_path, dupe_detection_image_fingerprint_database_file_path, path_to_animecoin_artwork_metadata_template, path_where_animecoin_html_ticket_templates_are_stored, \
path_to_nsfw_detection_model_file = get_all_animecoin_directories_func()

rpc_user = 'test' 
rpc_password ='testpw'
rpc_port = '18443' #mainnet: 8332; testnet: 18332; regtest: 18443 
rpc_connection_string = 'http://'+rpc_user+':'+rpc_password+'@127.0.0.1:'+rpc_port
base_transaction_amount = 0.0001
COIN = 100000000 #satoshis in 1 btc
FEEPERKB = Decimal(0.0001)
OP_CHECKSIG = b'\xac'
OP_CHECKMULTISIG = b'\xae'
OP_PUSHDATA1 = b'\x4c'
OP_DUP = b'\x76'
OP_HASH160 = b'\xa9'
OP_EQUALVERIFY = b'\x88'

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
    sha256_hash_of_input_data = hashlib.sha256(input_data_or_string).hexdigest()         
    return sha256_hash_of_input_data

def get_raw_sha256_hash_of_input_data_func(input_data_or_string):
    if isinstance(input_data_or_string, str):
        input_data_or_string = input_data_or_string.encode('utf-8')
    sha256_hash_of_input_data = hashlib.sha256(input_data_or_string).digest()         
    return sha256_hash_of_input_data

def store_data_in_animecoin_blockchain_func(input_data, animecoin_zstd_compression_dictionary, receiving_animecoin_blockchain_address):
    global rpc_connection_string, path_where_animecoin_html_ticket_templates_are_stored
    rpc_connection = AuthServiceProxy(rpc_connection_string)
    animecoin_zstd_compressed_data = compress_data_with_animecoin_zstd_func(input_data, animecoin_zstd_compression_dictionary)
    animecoin_zstd_uncompressed_data = decompress_data_with_animecoin_zstd_func(animecoin_zstd_compressed_data, animecoin_zstd_compression_dictionary)
    assert(animecoin_ticket_html_string == animecoin_zstd_uncompressed_data.decode('utf-8')) #Check that we can decompress successfully
    animecoin_zstd_compression_dictionary_raw_data = animecoin_zstd_compression_dictionary.as_bytes() #Required to be able to calculate the hash of the dictionary file
    compression_dictionary_file_hash =  get_raw_sha256_hash_of_input_data_func(animecoin_zstd_compression_dictionary_raw_data)
    uncompressed_data_file_hash = get_raw_sha256_hash_of_input_data_func(animecoin_zstd_uncompressed_data)
    compressed_data_file_hash = get_raw_sha256_hash_of_input_data_func(animecoin_zstd_compressed_data)
    (txins, change) = select_txins(0)
    txouts = []
    encoded_animecoin_zstd_compressed_data = hexlify(animecoin_zstd_compressed_data)
    length_of_compressed_data_string = '{0:015}'.format(len(encoded_animecoin_zstd_compressed_data)).encode('utf-8')
    combined_data_hex = hexlify(length_of_compressed_data_string) + hexlify(compression_dictionary_file_hash) + hexlify(uncompressed_data_file_hash) + hexlify(compressed_data_file_hash) + encoded_animecoin_zstd_compressed_data + hexlify(('0'*100).encode('utf-8'))
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
    txouts.append((out_value, OP_DUP + OP_HASH160 + pushdata(addr2bytes(receiving_animecoin_blockchain_address)) + OP_EQUALVERIFY + OP_CHECKSIG))
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
    print('Sending data transaction to address: ' + receiving_animecoin_blockchain_address)
    print('Size: %d  Fee: %2.8f' % (len(hex_signed_transaction)/2, fee), file=sys.stderr)
    send_raw_transaction_result = rpc_connection.sendrawtransaction(hex_signed_transaction)
    blockchain_transaction_id = send_raw_transaction_result
    print('Transaction ID: ' + blockchain_transaction_id)
    return blockchain_transaction_id

def retrieve_data_from_animecoin_blockchain_func(blockchain_transaction_id):
    global rpc_connection_string, path_where_animecoin_html_ticket_templates_are_stored
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
    reconstructed_length_of_compressed_data_hex_string = reconstructed_combined_data[0:30] # len(hexlify('{0:015}'.format(len(encoded_animecoin_zstd_compressed_data)).encode('utf-8'))) is 30
    reconstructed_length_of_compressed_data_hex_string = int(unhexstr(reconstructed_length_of_compressed_data_hex_string).decode('utf-8').lstrip('0'))
    reconstructed_combined_data__remainder_1 = reconstructed_combined_data[30:]
    length_of_standard_hash_string = len(get_sha256_hash_of_input_data_func('test'))
    reconstructed_compression_dictionary_file_hash = reconstructed_combined_data__remainder_1[0:length_of_standard_hash_string]
    reconstructed_combined_data__remainder_2 = reconstructed_combined_data__remainder_1[length_of_standard_hash_string:]
    reconstructed_uncompressed_data_file_hash = reconstructed_combined_data__remainder_2[0:length_of_standard_hash_string]
    reconstructed_combined_data__remainder_3 = reconstructed_combined_data__remainder_2[length_of_standard_hash_string:]
    reconstructed_compressed_data_file_hash = reconstructed_combined_data__remainder_3[0:length_of_standard_hash_string]
    reconstructed_combined_data__remainder_4 = reconstructed_combined_data__remainder_3[length_of_standard_hash_string:]
    reconstructed_encoded_animecoin_zstd_compressed_data_padded = reconstructed_combined_data__remainder_4.replace('A','') #Note sure where this comes from; somehow it is introduced into the data (note this is "A" not "a").
    calculated_padding_length = len(reconstructed_encoded_animecoin_zstd_compressed_data_padded) - reconstructed_length_of_compressed_data_hex_string 
    reconstructed_encoded_animecoin_zstd_compressed_data = reconstructed_encoded_animecoin_zstd_compressed_data_padded[0:-calculated_padding_length]
    reconstructed_animecoin_zstd_compressed_data = unhexstr(reconstructed_encoded_animecoin_zstd_compressed_data)
    hash_of_reconstructed_animecoin_zstd_compressed_data = get_sha256_hash_of_input_data_func(reconstructed_animecoin_zstd_compressed_data)
    assert(hash_of_reconstructed_animecoin_zstd_compressed_data == reconstructed_compressed_data_file_hash)
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
                reconstructed_animecoin_zstd_compression_dictionary_raw_data = f.read()
                reconstructed_animecoin_zstd_compression_dictionary = zstd.ZstdCompressionDict(reconstructed_animecoin_zstd_compression_dictionary_raw_data)
    if not found_dictionary_file:
        print('Attempting to generate dictionary file before giving up (not guaranteed to work!)')
        reconstructed_animecoin_zstd_compression_dictionary = generate_zstd_dictionary_from_folder_path_and_file_matching_string_func(path_where_animecoin_html_ticket_templates_are_stored, '*ticket*.html')
        reconstructed_animecoin_zstd_compression_dictionary_raw_data = reconstructed_animecoin_zstd_compression_dictionary.as_bytes()
    hash_of_compression_dictionary = get_sha256_hash_of_input_data_func(reconstructed_animecoin_zstd_compression_dictionary_raw_data)
    assert(hash_of_compression_dictionary == reconstructed_compression_dictionary_file_hash)
    reconstructed_animecoin_zstd_uncompressed_data = decompress_data_with_animecoin_zstd_func(reconstructed_animecoin_zstd_compressed_data, reconstructed_animecoin_zstd_compression_dictionary)
    hash_of_reconstructed_animecoin_zstd_uncompressed_data = get_sha256_hash_of_input_data_func(reconstructed_animecoin_zstd_uncompressed_data)
    assert(hash_of_reconstructed_animecoin_zstd_uncompressed_data == reconstructed_uncompressed_data_file_hash)
    print('Successfully reconstructed and decompressed data!')
    return reconstructed_animecoin_zstd_uncompressed_data
    

if 0: #Demo:
    if 0: #Start up bitcoind using the regtest network and generate some coins to use:
        rpc_auth_command_string = '-rpcuser=' + rpc_user + ' -rpcpassword=' + rpc_password
        bitcoind_launch_string = 'bitcoind.exe ' + rpc_auth_command_string + ' -regtest -server -addresstype=legacy' #Without -addresstype=legacy the current bitcoind will default to using segwit addresses, which suck (you can't sign messages with them)
        proc = subprocess.Popen(bitcoind_launch_string, shell=True)
        sleep(5)
        rpc_connection = AuthServiceProxy(rpc_connection_string)
        block_generation_output = rpc_connection.generate(101) #Generate some coins to work with
        
    path_to_animecoin_html_ticket_file = 'C:\\animecoin\\art_folders_to_encode\\Carlo_Angelo__Felipe_Number_02\\artwork_metadata_ticket__2018_05_27__23_42_33_964953.html'
    with open(path_to_animecoin_html_ticket_file,'r') as f:
       animecoin_ticket_html_string = f.read()
       
    use_generate_new_compression_dictionary = 1
    if use_generate_new_compression_dictionary:
        animecoin_zstd_compression_dictionary = generate_zstd_dictionary_from_folder_path_and_file_matching_string_func(path_where_animecoin_html_ticket_templates_are_stored, '*ticket*.html')
        animecoin_zstd_compression_dictionary_raw_data = animecoin_zstd_compression_dictionary.as_bytes()
        compression_dictionary_hash = get_sha256_hash_of_input_data_func(animecoin_zstd_compression_dictionary_raw_data)
        with open(path_to_zstd_compression_dictionaries + compression_dictionary_hash + '.dict','wb') as f:
            f.write(animecoin_zstd_compression_dictionary_raw_data)
    
    rpc_connection = AuthServiceProxy(rpc_connection_string)
    receiving_animecoin_blockchain_address = rpc_connection.getnewaddress()
    
    input_data = animecoin_ticket_html_string
    blockchain_transaction_id = store_data_in_animecoin_blockchain_func(input_data, animecoin_zstd_compression_dictionary, receiving_animecoin_blockchain_address)
    
    reconstructed_animecoin_zstd_uncompressed_data = retrieve_data_from_animecoin_blockchain_func(blockchain_transaction_id)
    reconstructed_animecoin_zstd_uncompressed_data_decoded = reconstructed_animecoin_zstd_uncompressed_data.decode('utf-8')
    
    assert(reconstructed_animecoin_zstd_uncompressed_data_decoded == input_data)
