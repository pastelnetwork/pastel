import subprocess, json, os.path, binascii, struct, re, hashlib, sys
from anime_utility_functions_v1 import generate_zstd_dictionary_from_folder_path_and_file_matching_string_func, compress_data_with_animecoin_zstd_func, \
        decompress_data_with_animecoin_zstd_func, get_all_animecoin_directories_func, get_all_animecoin_parameters_func
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
temp_files_folder_path, prepared_final_art_zipfiles_folder_path, chunk_db_file_path, file_storage_log_file_path, nginx_allowed_ip_whitelist_file_path, path_to_animecoin_trade_ticket_template, \
artist_final_signature_files_folder_path, dupe_detection_image_fingerprint_database_file_path, path_to_animecoin_artwork_metadata_template, path_where_animecoin_html_ticket_templates_are_stored, \
path_to_nsfw_detection_model_file = get_all_animecoin_directories_func()

#Parameters:
OP_RETURN_BITCOIN_IP='127.0.0.1' # IP address of your bitcoin node
OP_RETURN_BITCOIN_PORT = '' # leave empty to use default port for mainnet/testnet
OP_RETURN_BITCOIN_USER = 'test' # leave empty to read from ~/.bitcoin/bitcoin.conf (Unix only)
OP_RETURN_BITCOIN_PASSWORD ='testpw' # leave empty to read from ~/.bitcoin/bitcoin.conf (Unix only)
OP_RETURN_BTC_FEE = 0.0001 # BTC fee to pay per transaction
OP_RETURN_BTC_DUST = 0.00001 # omit BTC outputs smaller than this
OP_RETURN_MAX_BYTES = 80 # maximum bytes in an OP_RETURN (80 as of Bitcoin 0.11)
OP_RETURN_MAX_BLOCKS = 10 # maximum number of blocks to try when retrieving data
OP_RETURN_NET_TIMEOUT = 10 # how long to time out (in seconds) when communicating with bitcoin node
OP_RETURN_BITCOIN_PATH = 'bitcoin-cli.exe'

def OP_RETURN_send(send_address, send_amount, metadata, testnet=False):
    if not OP_RETURN_bitcoin_check(testnet): # Validate some parameters
        return {'error': 'Please check Bitcoin Core is running and OP_RETURN_BITCOIN_* constants are set correctly'}
    result = OP_RETURN_bitcoin_cmd('validateaddress', testnet, send_address)
    if not ('isvalid' in result and result['isvalid']):
        return {'error': 'Send address could not be validated: '+send_address}
    metadata_len = len(metadata)
    if metadata_len > 65536:
        return {'error': 'This library only supports metadata up to 65536 bytes in size'}
    if metadata_len > OP_RETURN_MAX_BYTES:
        return {'error': 'Metadata has ' + str(metadata_len)+' bytes but is limited to '+ str(OP_RETURN_MAX_BYTES)+' (see OP_RETURN_MAX_BYTES)'}
    output_amount = send_amount+OP_RETURN_BTC_FEE     # Calculate amounts and choose inputs
    inputs_spend = OP_RETURN_select_inputs(output_amount, testnet)
    if 'error' in inputs_spend:
        return {'error': inputs_spend['error']}
    change_amount = inputs_spend['total'] - output_amount
    change_address = OP_RETURN_bitcoin_cmd('getrawchangeaddress', testnet) # Build the raw transaction
    outputs = {send_address: send_amount}
    if change_amount >= OP_RETURN_BTC_DUST:
        outputs[change_address] = change_amount
    raw_txn = OP_RETURN_create_txn(inputs_spend['inputs'], outputs, metadata, len(outputs), testnet)    # Sign and send the transaction, return result
    return OP_RETURN_sign_send_txn(raw_txn, testnet)

def OP_RETURN_store(data, testnet=False):
    if not OP_RETURN_bitcoin_check(testnet): # Data is stored in OP_RETURNs within a series of chained transactions. If the OP_RETURN is followed by another output, the data continues in the transaction spending that output. When the OP_RETURN is the last output, this also signifies the end of the data.
        return {'error': 'Please check Bitcoin Core is running and OP_RETURN_BITCOIN_* constants are set correctly'}
    data_len = len(data)
    if data_len == 0:
        return {'error': 'Some data is required to be stored'}
    change_address = OP_RETURN_bitcoin_cmd('getrawchangeaddress', testnet)# Calculate amounts and choose first inputs to use:
    output_amount = OP_RETURN_BTC_FEE*int((data_len+OP_RETURN_MAX_BYTES - 1)/OP_RETURN_MAX_BYTES) # number of transactions required
    inputs_spend=OP_RETURN_select_inputs(output_amount, testnet)
    if 'error' in inputs_spend:
        return {'error': inputs_spend['error']}
    inputs = inputs_spend['inputs']
    input_amount = inputs_spend['total']
    height = int(OP_RETURN_bitcoin_cmd('getblockcount', testnet))    # Find the current blockchain height and mempool txids
    avoid_txids = OP_RETURN_bitcoin_cmd('getrawmempool', testnet)
    result = {'txids':[]} # Loop to build and send transactions
    for data_ptr in range(0, data_len, OP_RETURN_MAX_BYTES): # Some preparation for this iteration
        last_txn = ((data_ptr+OP_RETURN_MAX_BYTES)>=data_len) # is this the last tx in the chain?
        change_amount = input_amount-OP_RETURN_BTC_FEE
        metadata = data[data_ptr:data_ptr+OP_RETURN_MAX_BYTES]
        outputs = {} # Build and send this transaction
        if change_amount >= OP_RETURN_BTC_DUST: # might be skipped for last transaction
            outputs[change_address] = change_amount
        raw_txn = OP_RETURN_create_txn(inputs, outputs, metadata, len(outputs) if last_txn else 0, testnet)
        send_result=OP_RETURN_sign_send_txn(raw_txn, testnet)
        if 'error' in send_result: # Check for errors and collect the txid
            result['error']=send_result['error']
            break
    result['txids'].append(send_result['txid'])
    if data_ptr == 0:
        result['ref'] = OP_RETURN_calc_ref(height, send_result['txid'], avoid_txids) # Prepare inputs for next iteration
    inputs = [{'txid': send_result['txid'], 'vout': 1,}]
    input_amount = change_amount
    return result

def OP_RETURN_retrieve(ref, max_results=1, testnet=False):
    if not OP_RETURN_bitcoin_check(testnet): # Validate parameters and get status of Bitcoin Core
        return {'error': 'Please check Bitcoin Core is running and OP_RETURN_BITCOIN_* constants are set correctly'}
    max_height = int(OP_RETURN_bitcoin_cmd('getblockcount', testnet))
    heights = OP_RETURN_get_ref_heights(ref, max_height)
    if not isinstance(heights, list):
        return {'error': 'Ref is not valid'}
    results=[]
    for height in heights:
        if height == 0:
            txids = OP_RETURN_list_mempool_txns(testnet) # if mempool, only get list for now (to save RPC calls)
            txns = None
        else:
            txns = OP_RETURN_get_block_txns(height, testnet) # if block, get all fully unpacked
            txids = txns.keys()
    for txid in txids:
        if OP_RETURN_match_ref_txid(ref, txid):
            if height == 0:
                txn_unpacked = OP_RETURN_get_mempool_txn(txid, testnet)
            else:
                txn_unpacked = txns[txid]
            found = OP_RETURN_find_txn_data(txn_unpacked)
        if found: # Collect data from txid which matches ref and contains an OP_RETURN        
            result = {'txids': [str(txid)], 'data': found['op_return'],}
            key_heights = {height: True}
            if height == 0: # Work out which other block heights / mempool we should try
                try_heights = [] # nowhere else to look if first still in mempool
            else:
                result['ref'] = OP_RETURN_calc_ref(height, txid, txns.keys())
                try_heights = OP_RETURN_get_try_heights(height+1, max_height, False)
            if height == 0: # Collect the rest of the data, if appropriate
                this_txns = OP_RETURN_get_mempool_txns(testnet) # now retrieve all to follow chain
            else:
                this_txns = txns
            last_txid = txid
            this_height = height
            while found['index'] < (len(txn_unpacked['vout']) - 1): # this means more data to come
                next_txid = OP_RETURN_find_spent_txid(this_txns, last_txid, found['index']+1) # If we found the next txid in the data chain
                if next_txid:
                    result['txids'].append(str(next_txid))
                    txn_unpacked = this_txns[next_txid]
                    found = OP_RETURN_find_txn_data(txn_unpacked)
                    if found:
                        result['data'] += found['op_return']
                        key_heights[this_height] = True
                    else:
                        result['error'] = 'Data incomplete - missing OP_RETURN'
                        break
                    last_txid = next_txid
            else: # Otherwise move on to the next height to keep looking
                if len(try_heights):
                    this_height = try_heights.pop(0)
                    if this_height == 0:
                        this_txns = OP_RETURN_get_mempool_txns(testnet)
                    else:
                        this_txns = OP_RETURN_get_block_txns(this_height, testnet)    
                else:
                    result['error'] = 'Data incomplete - could not find next transaction'
                    break
            result['heights'] = list(key_heights.keys()) # Finish up the information about this result             
            results.append(result)
            if len(results) >= max_results:
                break # stop if we have collected enough
    return results

# Utility functions
def OP_RETURN_select_inputs(total_amount, testnet):
    unspent_inputs = OP_RETURN_bitcoin_cmd('listunspent', testnet, 0) # List and sort unspent inputs by priority
    if not isinstance(unspent_inputs, list):
        return {'error': 'Could not retrieve list of unspent inputs'}
    unspent_inputs.sort(key = lambda unspent_input: unspent_input['amount']*unspent_input['confirmations'], reverse=True)
    inputs_spend=[] # Identify which inputs should be spent
    input_amount=0
    for unspent_input in unspent_inputs:
        inputs_spend.append(unspent_input)
        input_amount += unspent_input['amount']
        if input_amount >= total_amount:
            break # stop when we have enough
    if input_amount<total_amount:
        return {'error': 'Not enough funds are available to cover the amount and fee'}
    return {'inputs': inputs_spend, 'total': input_amount,}

def OP_RETURN_create_txn(inputs, outputs, metadata, metadata_pos, testnet):
    raw_txn = OP_RETURN_bitcoin_cmd('createrawtransaction', testnet, inputs, outputs)
    txn_unpacked = OP_RETURN_unpack_txn(OP_RETURN_hex_to_bin(raw_txn))
    metadata_len = len(metadata)
    if metadata_len <= 75:
        payload = bytearray((metadata_len,))+metadata # length byte + data (https://en.bitcoin.it/wiki/Script)
    elif metadata_len <= 256:
        payload = "\x4c"+bytearray((metadata_len,))+metadata # OP_PUSHDATA1 format
    else:
        payload = "\x4d"+bytearray((metadata_len%256,))+bytearray((int(metadata_len/256),))+metadata # OP_PUSHDATA2 format
    metadata_pos = min(max(0, metadata_pos), len(txn_unpacked['vout'])) # constrain to valid values
    txn_unpacked['vout'][metadata_pos:metadata_pos] = [{'value': 0, 'scriptPubKey': '6a' + OP_RETURN_bin_to_hex(payload)}] # here's the OP_RETURN
    return OP_RETURN_bin_to_hex(OP_RETURN_pack_txn(txn_unpacked))

def OP_RETURN_sign_send_txn(raw_txn, testnet):
    signed_txn = OP_RETURN_bitcoin_cmd('signrawtransaction', testnet, raw_txn)
    if not ('complete' in signed_txn and signed_txn['complete']):
        return {'error': 'Could not sign the transaction'}
    send_txid = OP_RETURN_bitcoin_cmd('sendrawtransaction', testnet, signed_txn['hex'])
    if not (isinstance(send_txid, str) and len(send_txid)==64):
        return {'error': 'Could not send the transaction'}
    return {'txid': str(send_txid)}

def OP_RETURN_list_mempool_txns(testnet):
    return OP_RETURN_bitcoin_cmd('getrawmempool', testnet)

def OP_RETURN_get_mempool_txn(txid, testnet):
    raw_txn = OP_RETURN_bitcoin_cmd('getrawtransaction', testnet, txid)
    return OP_RETURN_unpack_txn(OP_RETURN_hex_to_bin(raw_txn))

def OP_RETURN_get_mempool_txns(testnet):
    txids = OP_RETURN_list_mempool_txns(testnet)
    txns = {}
    for txid in txids:
        txns[txid] = OP_RETURN_get_mempool_txn(txid, testnet)
    return txns
    
def OP_RETURN_get_raw_block(height, testnet):
    block_hash = OP_RETURN_bitcoin_cmd('getblockhash', testnet, height)
    if not (isinstance(block_hash, str) and len(block_hash) == 64):
        return {'error': 'Block at height '+str(height)+' not found'}
    return {'block': OP_RETURN_hex_to_bin(OP_RETURN_bitcoin_cmd('getblock', testnet, block_hash, False))}

def OP_RETURN_get_block_txns(height, testnet):
    raw_block = OP_RETURN_get_raw_block(height, testnet)
    if 'error' in raw_block:
        return {'error': raw_block['error']}
    block = OP_RETURN_unpack_block(raw_block['block'])
    return block['txs']

# Talking to bitcoin-cli
def OP_RETURN_bitcoin_check(testnet):
    info = OP_RETURN_bitcoin_cmd('getwalletinfo', testnet)
    return isinstance(info, dict) and 'balance' in info
    
def OP_RETURN_bitcoin_cmd(command, testnet, *args): # more params are read from here
    sub_args = [OP_RETURN_BITCOIN_PATH]
    sub_args.append('-rpcuser=' + OP_RETURN_BITCOIN_USER)
    sub_args.append('-rpcpassword=' + OP_RETURN_BITCOIN_PASSWORD)
    sub_args.append('-regtest') #ToDo: Remove this when done testing.
    if testnet:
        sub_args.append('-testnet')
    sub_args.append(command)
    for arg in args:
        sub_args.append(json.dumps(arg) if isinstance(arg, (dict, list, tuple)) else str(arg))
    raw_result = subprocess.check_output(sub_args).decode('utf-8').rstrip("\n")
    try: # decode JSON if possible
        result = json.loads(raw_result)
    except ValueError:
        result = raw_result
    return result
    
def OP_RETURN_calc_ref(next_height, txid, avoid_txids):
    txid_binary = OP_RETURN_hex_to_bin(txid)
    for txid_offset in range(15):
        sub_txid = txid_binary[2*txid_offset:2*txid_offset+2]
    clashed = False
    for avoid_txid in avoid_txids:
        avoid_txid_binary = OP_RETURN_hex_to_bin(avoid_txid)
        if ((avoid_txid_binary[2*txid_offset:2*txid_offset+2] == sub_txid) and (txid_binary != avoid_txid_binary)):
            clashed = True
            break
        if not clashed:
            break
        if clashed: # could not find a good reference
            return None
    tx_ref = ord(txid_binary[2*txid_offset:1+2*txid_offset])+256*ord(txid_binary[1+2*txid_offset:2+2*txid_offset])+65536*txid_offset
    return '%06d-%06d' % (next_height, tx_ref)

def OP_RETURN_get_ref_parts(ref):
    if not re.search('^[0-9]+\-[0-9A-Fa-f]+$', ref): # also support partial txid for second half
        return None
    parts = ref.split('-')
    if re.search('[A-Fa-f]', parts[1]):
        if len(parts[1]) >= 4:
            txid_binary = OP_RETURN_hex_to_bin(parts[1][0:4])
            parts[1] = ord(txid_binary[0:1])+256*ord(txid_binary[1:2])+65536*0
        else:
            return None
    parts = list(map(int, parts))
    if parts[1] > 983039: # 14*65536+65535
        return None
    return parts

def OP_RETURN_get_ref_heights(ref, max_height):
    parts = OP_RETURN_get_ref_parts(ref)
    if not parts:
        return None
    return OP_RETURN_get_try_heights(parts[0], max_height, True)

def OP_RETURN_get_try_heights(est_height, max_height, also_back):
    forward_height = est_height
    back_height = min(forward_height - 1, max_height)
    heights = []
    mempool = False
    try_height = 0
    while True:
        if also_back and ((try_height%3) == 2): # step back every 3 tries
            heights.append(back_height)
            back_height -= 1
        else:
            if forward_height > max_height:
                if not mempool:
                    heights.append(0) # indicates to try mempool
                    mempool = True
                elif not also_back:
                    break # nothing more to do here
            else:
                heights.append(forward_height)
            forward_height += 1
        if len(heights) >= OP_RETURN_MAX_BLOCKS:
            break
    try_height += 1
    return heights

def OP_RETURN_match_ref_txid(ref, txid):
    parts = OP_RETURN_get_ref_parts(ref)
    if not parts:
        return None
    txid_offset = int(parts[1]/65536)
    txid_binary = OP_RETURN_hex_to_bin(txid)
    txid_part = txid_binary[2*txid_offset:2*txid_offset+2]
    txid_match = bytearray([parts[1]%256, int((parts[1]%65536)/256)])
    return txid_part == txid_match # exact binary comparison

def OP_RETURN_unpack_block(binary): # Unpacking and packing bitcoin blocks and transactions    
    buffer = OP_RETURN_buffer(binary)
    block = {}
    block['version'] = buffer.shift_unpack(4, '<L')
    block['hashPrevBlock'] = OP_RETURN_bin_to_hex(buffer.shift(32)[::-1])
    block['hashMerkleRoot'] = OP_RETURN_bin_to_hex(buffer.shift(32)[::-1])
    block['time'] = buffer.shift_unpack(4, '<L')
    block['bits'] = buffer.shift_unpack(4, '<L')
    block['nonce'] = buffer.shift_unpack(4, '<L')
    block['tx_count'] = buffer.shift_varint()
    block['txs'] = {}
    old_ptr = buffer.used()
    while buffer.remaining():
        transaction = OP_RETURN_unpack_txn_buffer(buffer)
        new_ptr = buffer.used()
        size = new_ptr-old_ptr
        raw_txn_binary = binary[old_ptr:old_ptr+size]
        txid = OP_RETURN_bin_to_hex(hashlib.sha256(hashlib.sha256(raw_txn_binary).digest()).digest()[::-1])
        old_ptr = new_ptr
        transaction['size'] = size
        block['txs'][txid] = transaction
    return block

def OP_RETURN_unpack_txn(binary):
    return OP_RETURN_unpack_txn_buffer(OP_RETURN_buffer(binary))

def OP_RETURN_unpack_txn_buffer(buffer): # see: https://en.bitcoin.it/wiki/Transactions
    txn = {'vin': [], 'vout': [],}
    txn['version'] = buffer.shift_unpack(4, '<L') # small-endian 32-bits
    inputs = buffer.shift_varint()
    if inputs > 100000: # sanity check
        return None
    for _ in range(inputs):
        input = {}
        input['txid'] = OP_RETURN_bin_to_hex(buffer.shift(32)[::-1])
        input['vout'] = buffer.shift_unpack(4, '<L')
        length = buffer.shift_varint()
        input['scriptSig'] = OP_RETURN_bin_to_hex(buffer.shift(length))
        input['sequence'] = buffer.shift_unpack(4, '<L')
        txn['vin'].append(input)
    outputs = buffer.shift_varint()
    if outputs > 100000: # sanity check
        return None
    for _ in range(outputs):
        output = {}
        output['value'] = float(buffer.shift_uint64())/100000000
        length = buffer.shift_varint()
        output['scriptPubKey'] = OP_RETURN_bin_to_hex(buffer.shift(length))
        txn['vout'].append(output)
    txn['locktime'] = buffer.shift_unpack(4, '<L')
    return txn

def OP_RETURN_find_spent_txid(txns, spent_txid, spent_vout):
    for txid, txn_unpacked in txns.items():
        for input in txn_unpacked['vin']:
            if (input['txid'] == spent_txid) and (input['vout']==spent_vout):
                return txid
        return None

def OP_RETURN_find_txn_data(txn_unpacked):
    for index, output in enumerate(txn_unpacked['vout']):
        op_return = OP_RETURN_get_script_data(OP_RETURN_hex_to_bin(output['scriptPubKey']))
    if op_return:
        return {'index': index, 'op_return': op_return,}
    return None
    
def OP_RETURN_get_script_data(scriptPubKeyBinary):
    op_return = None
    if scriptPubKeyBinary[0:1] == b'\x6a':
        first_ord = ord(scriptPubKeyBinary[1:2])
    if first_ord <= 75:
        op_return = scriptPubKeyBinary[2:2+first_ord]
    elif first_ord == 0x4c:
        op_return = scriptPubKeyBinary[3:3+ord(scriptPubKeyBinary[2:3])]
    elif first_ord == 0x4d:
        op_return = scriptPubKeyBinary[4:4+ord(scriptPubKeyBinary[2:3])+256*ord(scriptPubKeyBinary[3:4])]
    return op_return    

def OP_RETURN_pack_txn(txn):
    binary = b''
    binary += struct.pack('<L', txn['version'])
    binary += OP_RETURN_pack_varint(len(txn['vin']))
    for input in txn['vin']:
        binary += OP_RETURN_hex_to_bin(input['txid'])[::-1]
        binary += struct.pack('<L', input['vout'])
        binary += OP_RETURN_pack_varint(int(len(input['scriptSig'])/2)) # divide by 2 because it is currently in hex
        binary += OP_RETURN_hex_to_bin(input['scriptSig'])
        binary += struct.pack('<L', input['sequence'])
    binary += OP_RETURN_pack_varint(len(txn['vout']))
    for output in txn['vout']:
        binary += OP_RETURN_pack_uint64(int(round(output['value']*100000000)))
        binary += OP_RETURN_pack_varint(int(len(output['scriptPubKey'])/2)) # divide by 2 because it is currently in hex
        binary += OP_RETURN_hex_to_bin(output['scriptPubKey'])
    binary += struct.pack('<L', txn['locktime'])
    return binary

def OP_RETURN_pack_varint(integer):
    if integer > 0xFFFFFFFF:
        packed = "\xFF"+OP_RETURN_pack_uint64(integer)
    elif integer>0xFFFF:
        packed = "\xFE"+struct.pack('<L', integer)
    elif integer>0xFC:
        packed = "\xFD".struct.pack('<H', integer)
    else:
        packed = struct.pack('B', integer)
    return packed

def OP_RETURN_pack_uint64(integer):
    upper = int(integer/4294967296)
    lower = integer-upper*4294967296
    return struct.pack('<L', lower)+struct.pack('<L', upper)

class OP_RETURN_buffer(): # Helper class for unpacking bitcoin binary data
    def __init__(self, data, ptr=0):
        self.data = data
        self.len = len(data)
        self.ptr = ptr
    def shift(self, chars):
        prefix = self.data[self.ptr:self.ptr+chars]
        self.ptr += chars
        return prefix
    def shift_unpack(self, chars, format):
        unpack = struct.unpack(format, self.shift(chars))
        return unpack[0]
    def shift_varint(self):
        value = self.shift_unpack(1, 'B')
        if value == 0xFF:
            value = self.shift_uint64()
        elif value == 0xFE:
            value=self.shift_unpack(4, '<L')
        elif value == 0xFD:
            value = self.shift_unpack(2, '<H')
        return value
    def shift_uint64(self):
        return self.shift_unpack(4, '<L')+4294967296*self.shift_unpack(4, '<L')
    def used(self):
        return min(self.ptr, self.len)
    def remaining(self):
        return max(self.len-self.ptr, 0)

def OP_RETURN_hex_to_bin(hex): # Converting binary <-> hexadecimal
    try:
        raw = binascii.a2b_hex(hex)
    except Exception:
        return None
    return raw
    
def OP_RETURN_bin_to_hex(string):
    return binascii.b2a_hex(string).decode('utf-8')

if 0: #Demo:
    rpc_auth_command_string = '-rpcuser=' + OP_RETURN_BITCOIN_USER + ' -rpcpassword=' + OP_RETURN_BITCOIN_PASSWORD
    bitcoind_launch_string = 'bitcoind.exe' + rpc_auth_command_string + ' -regtest -server'
    proc = subprocess.Popen(bitcoind_launch_string, shell=True)
    block_generation_test_command_string = 'bitcoin-cli.exe ' + rpc_auth_command_string + ' -regtest generate 101'
    block_generation_result = subprocess.check_output(block_generation_test_command_string)
    get_new_address_command_string = 'bitcoin-cli.exe ' + rpc_auth_command_string + ' -regtest getnewaddress'
    get_new_address_result = subprocess.check_output(get_new_address_command_string)
    send_btc_to_new_address_command_string = 'bitcoin-cli.exe ' + rpc_auth_command_string + ' -regtest sendtoaddress ' + get_new_address_result.decode('utf-8').rstrip() + ' 10.5'
    send_btc_to_new_address_result = subprocess.check_output(send_btc_to_new_address_command_string)
    list_unspent_command_string = 'bitcoin-cli.exe ' + rpc_auth_command_string + ' -regtest listunspent 0'
    list_unspent_result = subprocess.check_output(list_unspent_command_string)
    
if 0:
    path_to_animecoin_html_ticket_file = 'C:\\animecoin\\art_folders_to_encode\\Midori_Matsui__Number_05\\artwork_metadata_ticket__2018_05_25__18_37_51_580510.html'
    with open(path_to_animecoin_html_ticket_file,'r') as f:
       animecoin_ticket_html_string = f.read()
    animecoin_zstd_compression_dictionary = generate_zstd_dictionary_from_folder_path_and_file_matching_string_func(path_where_animecoin_html_ticket_templates_are_stored, '*ticket*.html')
    animecoin_zstd_compressed_data = compress_data_with_animecoin_zstd_func(animecoin_ticket_html_string, animecoin_zstd_compression_dictionary)
    animecoin_zstd_uncompressed_data = decompress_data_with_animecoin_zstd_func(animecoin_zstd_compressed_data, animecoin_zstd_compression_dictionary)
    assert(animecoin_ticket_html_string == animecoin_zstd_uncompressed_data.decode('utf-8'))
    animecoin_ticket_uncompressed_data_size_in_bytes = sys.getsizeof(animecoin_ticket_html_string)
    animecoin_ticket_compressed_data_size_in_bytes = sys.getsizeof(animecoin_zstd_compressed_data)
    result = OP_RETURN_store(animecoin_zstd_compressed_data)
    data = OP_RETURN_retrieve(ref, max_results=1, testnet=False)
    
     

    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    