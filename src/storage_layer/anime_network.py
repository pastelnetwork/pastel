import sys, time, os.path, io, string, glob, hashlib, imghdr, random, os, zlib, pickle, sqlite3, uuid, socket, warnings, base64, json, hmac, logging, asyncio, binascii
from shutil import copyfile
from struct import pack, unpack, error
from multiprocessing import Process, Queue
from random import randint
from math import ceil, floor, sqrt, log
from collections import defaultdict
from collections import OrderedDict
from zipfile import ZipFile
from subprocess import check_output
from datetime import datetime
from itertools import compress
from kademlia.network import Server
from pyftpdlib.handlers import FTPHandler, ThrottledDTPHandler
from pyftpdlib.authorizers import DummyAuthorizer
from pyftpdlib.servers import FTPServer
from ftplib import FTP
from fs.memoryfs import MemoryFS
with warnings.catch_warnings():
    warnings.filterwarnings("ignore",category=DeprecationWarning)
    from fs.copy import copy_fs
    from fs.osfs import OSFS

try:
    from tqdm import tqdm
    import yenc
except:
    pass
#Requirements: pip install kademlia, yenc, tqdm, fs, pyftpdlib
        
###############################################################################################################
# Parameters:
###############################################################################################################

use_start_new_dht_network = 1
use_connect_to_existing_dht_network = 1
use_verify_integrity = 1
use_generate_new_sqlite_chunk_database = 1
use_advertise_local_blocks_on_dht = 1 
use_demonstrate_asking_dht_network_for_file_blocks = 1
use_demonstrate_ftp_based_block_file_transfer = 0

folder_path_of_art_folders_to_encode = 'C:\\animecoin\\art_folders_to_encode\\' #Each subfolder contains the various art files pertaining to a given art asset.
block_storage_folder_path = 'C:\\animecoin\\art_block_storage\\'
chunk_db_file_path = 'C:\\animecoin\\anime_chunkdb.sqlite'
file_storage_log_file_path = 'C:\\animecoin\\anime_storage_log.txt'
reconstructed_file_destination_folder_path = 'C:\\animecoin\\reconstructed_files\\'
default_port = 14142
alternative_port = 2718
list_of_ips = ['108.61.86.243','140.82.14.38','140.82.2.58']
target_number_of_nodes_per_unique_block_hash = 10
target_block_redundancy_factor = 10 #How many times more blocks should we store than are required to regenerate the file?
desired_block_size_in_bytes = 1024*1000*2


###############################################################################################################
# Functions:
###############################################################################################################

#Utility functions:
def generate_node_mac_hash_id():
    return hashlib.sha256(str(int(uuid.getnode())).encode('utf-8')).hexdigest()
        
def get_my_local_ip_func():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.connect(("8.8.8.8", 80))
    my_node_ip_address = s.getsockname()[0]
    s.close()
    return my_node_ip_address 

def regenerate_sqlite_chunk_database_func():
    global chunk_db_file_path
    try:
        conn = sqlite3.connect(chunk_db_file_path)
        c = conn.cursor()
        local_hash_table_creation_string= """CREATE TABLE potential_local_hashes (block_hash text PRIMARY KEY, file_hash);"""
        global_hash_table_creation_string= """CREATE TABLE potential_global_hashes (block_hash text, file_hash text, remote_node_ip text, remote_node_id text, datetime_peer_last_seen TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, PRIMARY KEY (block_hash, remote_node_id));"""
        node_ip_to_id_table_creation_string= """CREATE TABLE node_ip_to_id_table (remote_node_id text PRIMARY KEY, remote_node_ip text, datetime_peer_last_seen TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL);"""
        c.execute(local_hash_table_creation_string)
        c.execute(global_hash_table_creation_string)
        c.execute(node_ip_to_id_table_creation_string)
        conn.commit()
    except Exception as e:
        print('Error: '+ str(e))

    
def get_node_ip_address_from_node_id_func(remote_node_id):
    try:
        global node_ip_to_node_id_dict
        list_of_node_ids_from_dict = node_ip_to_node_id_dict.values()
        list_of_node_ips_from_dict = node_ip_to_node_id_dict.keys()
        for node_count, current_node_id in enumerate(list_of_node_ids_from_dict):
            current_node_ip_address = list_of_node_ips_from_dict[node_count]
            sql_string = """INSERT OR REPLACE INTO node_ip_to_id_table (remote_node_id, remote_node_ip) VALUES (\"{remotenodeid}\", \"{remotenodeip}\")""".format(remotenodeid=current_node_id, remotenodeip=current_node_ip_address)
        c.execute(sql_string)
        conn.commit()
        node_ip_query_results = c.execute("""SELECT DISTINCT remote_node_ip FROM potential_global_hashes WHERE remote_node_id=? ORDER BY datetime_peer_last_seen DESC""",[remote_node_id]).fetchall()
        list_of_matching_remote_node_ips = [x[0] for x in node_ip_query_results]
        return list_of_matching_remote_node_ips[0]
    except Exception as e:
        print('Error: '+ str(e))
        return 0

def get_node_id_from_node_ip_address_func(remote_node_ip):
    try:
        global node_ip_to_node_id_dict
        list_of_node_ids_from_dict = node_ip_to_node_id_dict.values()
        list_of_node_ips_from_dict = node_ip_to_node_id_dict.keys()
        for node_count, current_node_id in enumerate(list_of_node_ids_from_dict):
            current_node_ip_address = list_of_node_ips_from_dict[node_count]
            sql_string = """INSERT OR REPLACE INTO node_ip_to_id_table (remote_node_id, remote_node_ip) VALUES (\"{remotenodeid}\", \"{remotenodeip}\")""".format(remotenodeid=current_node_id, remotenodeip=current_node_ip_address)
        c.execute(sql_string)
        conn.commit()
        node_id_query_results = c.execute("""SELECT DISTINCT remote_node_id FROM potential_global_hashes WHERE remote_node_ip=? ORDER BY datetime_peer_last_seen DESC""",[remote_node_ip]).fetchall()
        list_of_matching_remote_node_ids = [x[0] for x in node_id_query_results]
        return list_of_matching_remote_node_ids[0]
    except Exception as e:
        print('Error: '+ str(e))
        return 0
    
def refresh_block_storage_folder_and_check_block_integrity_func(use_verify_integrity=0):
    global chunk_db_file_path
    global block_storage_folder_path
    if not os.path.exists(chunk_db_file_path):
       regenerate_sqlite_chunk_database_func()
    potential_local_block_hashes_list = []
    potential_local_file_hashes_list = []
    list_of_block_file_paths = glob.glob(block_storage_folder_path+'*.block')
    if use_verify_integrity:
        print('Now Verifying Block Files ('+str(len(list_of_block_file_paths))+' files found)\n')
        try:
            pbar = tqdm(total=len(list_of_block_file_paths))
        except:
            print('.')
        for current_block_file_path in list_of_block_file_paths:
            with open(current_block_file_path, 'rb') as f:
                try:
                    current_block_binary_data = f.read()
                except:
                    print('\nProblem reading block file!\n')
                    continue
            try:
                pbar.update(1)
            except:
                pass
            hash_of_block = hashlib.sha256(current_block_binary_data).hexdigest()
            reported_block_sha256_hash = current_block_file_path.split('\\')[-1].split('__')[-1].replace('BlockHash_','').replace('.block','')
            if hash_of_block == reported_block_sha256_hash:
                reported_file_sha256_hash = current_block_file_path.split('\\')[-1].split('__')[1]
                if reported_block_sha256_hash not in potential_local_block_hashes_list:
                    potential_local_block_hashes_list.append(reported_block_sha256_hash)
                    potential_local_file_hashes_list.append(reported_file_sha256_hash)
            else:
                print('\nBlock '+reported_block_sha256_hash+' did not hash to the correct value-- file is corrupt! Skipping to next file...\n')
        print('\n\nDone verifying block files!\nNow writing local block metadata to SQLite database...\n')
    else:
        for current_block_file_path in list_of_block_file_paths:
            reported_block_sha256_hash = current_block_file_path.split('\\')[-1].split('__')[-1].replace('BlockHash_','').replace('.block','')
            reported_file_sha256_hash = current_block_file_path.split('\\')[-1].split('__')[1]
            if reported_block_sha256_hash not in potential_local_block_hashes_list:
                potential_local_block_hashes_list.append(reported_block_sha256_hash)
                potential_local_file_hashes_list.append(reported_file_sha256_hash)
    conn = sqlite3.connect(chunk_db_file_path)
    c = conn.cursor()
    c.execute("""DELETE FROM potential_local_hashes""")
    for hash_cnt, current_block_hash in enumerate(potential_local_block_hashes_list):
        current_file_hash = potential_local_file_hashes_list[hash_cnt]
        sql_string = """INSERT OR IGNORE INTO potential_local_hashes (block_hash, file_hash) VALUES (\"{blockhash}\", \"{filehash}\")""".format(blockhash=current_block_hash, filehash=current_file_hash)
        c.execute(sql_string)
    conn.commit()
    #print('Done writing file hash data to SQLite file!\n')  
    return potential_local_block_hashes_list, potential_local_file_hashes_list

def get_local_matching_blocks_from_zipfile_hash_func(sha256_hash_of_desired_zipfile):
    global block_storage_folder_path
    list_of_block_file_paths = glob.glob(os.path.join(block_storage_folder_path,'*'+sha256_hash_of_desired_zipfile+'*.block'))
    list_of_block_hashes = []
    list_of_file_hashes = []
    for current_block_file_path in list_of_block_file_paths:
        reported_file_sha256_hash = current_block_file_path.split('\\')[-1].split('__')[1]
        list_of_file_hashes.append(reported_file_sha256_hash)
        reported_block_file_sha256_hash = current_block_file_path.split('__')[-1].replace('.block','').replace('BlockHash_','')
        list_of_block_hashes.append(reported_block_file_sha256_hash)
    return list_of_block_file_paths, list_of_block_hashes, list_of_file_hashes

def get_local_block_list_from_sqlite_for_given_file_hash_func(file_hash):
    global chunk_db_file_path
    conn = sqlite3.connect(chunk_db_file_path)
    c = conn.cursor()
    blocks_for_this_file_hash = c.execute("""SELECT block_hash FROM potential_local_hashes where file_hash = ?""",[file_hash]).fetchall()
    list_of_block_hashes = []
    for current_block in blocks_for_this_file_hash:
        list_of_block_hashes.append(current_block[0])
    return list_of_block_hashes

def get_local_block_file_binary_data_func(sha256_hash_of_desired_block):
    global block_storage_folder_path
    list_of_block_file_paths = glob.glob(os.path.join(block_storage_folder_path,'*'+sha256_hash_of_desired_block+'*.block'))
    try:
        with open(list_of_block_file_paths[0],'rb') as f:
            block_binary_data= f.read()
            return block_binary_data
    except Exception as e:
        print('Error: '+ str(e))     

def get_local_block_file_header_data_func(sha256_hash_of_desired_block):
    global block_storage_folder_path
    list_of_block_file_paths = glob.glob(os.path.join(block_storage_folder_path,'*'+sha256_hash_of_desired_block+'*.block'))
    if len(list_of_block_file_paths) > 0:
        try:
            with open(list_of_block_file_paths[0],'rb') as f:
                block_binary_data= f.read()
            input_stream = io.BufferedReader(io.BytesIO(block_binary_data),buffer_size=1000000)
            header = unpack('!III', input_stream.read(12))
            filesize = header[0]
            blocksize = header[1]
            number_of_blocks_required = ceil(filesize/blocksize)
            return filesize, blocksize, number_of_blocks_required
        except Exception as e:
            print('Error: '+ str(e))     
    else:
        print('Don\'t have that file locally!')
        filesize = 0
        blocksize = 0
        number_of_blocks_required = 0
        return filesize, blocksize, number_of_blocks_required
    
def package_dht_hash_table_entry_func(data_as_python_list):
    global default_port
    my_node_id = generate_node_mac_hash_id()
    data_as_python_list.insert(0,my_node_id)
    my_node_ip_address =  get_my_local_ip_func()
    my_node_ip_and_port = my_node_ip_address+ ':' + str(default_port)
    data_as_python_list.insert(0,my_node_ip_and_port)
    data_as_json = json.dumps(data_as_python_list)
    data_as_json_zipped = zlib.compress(data_as_json.encode('utf-8'))
    _, _, data_as_json_zipped_yenc_encoded = yenc.encode(data_as_json_zipped)
    return data_as_json_zipped_yenc_encoded
    use_demonstrate_size_of_encodings = 0
    if use_demonstrate_size_of_encodings:
        data_as_json_zipped_base64_encoded = base64.b64encode(data_as_json_zipped)
        size_of_data_as_json = sys.getsizeof(data_as_json)
        size_of_data_as_json_zipped = sys.getsizeof(data_as_json_zipped)
        size_of_data_as_json_zipped_yenc_encoded = sys.getsizeof(data_as_json_zipped_yenc_encoded)
        size_of_data_as_json_zipped_base64_encoded = sys.getsizeof(data_as_json_zipped_base64_encoded)
        with open('data_as_json','wb') as f:
            f.write(data_as_json.encode('utf-8'))
        with open('data_as_json_zipped','wb') as f:
            f.write(data_as_json_zipped)   
        with open('data_as_json_zipped_yenc_encoded','wb') as f:
            f.write(data_as_json_zipped_yenc_encoded)
        with open('data_as_json_zipped_base64_encoded','wb') as f:
            f.write(data_as_json_zipped_base64_encoded)    
        print('Comparison of the size of different representations of the data:\n')
        print('data_as_json: '+str(size_of_data_as_json))
        print('data_as_json_zipped: '+str(size_of_data_as_json_zipped))
        print('data_as_json_zipped_yenc_encoded: '+str(size_of_data_as_json_zipped_yenc_encoded))
        print('data_as_json_zipped_base64_encoded: '+str(size_of_data_as_json_zipped_base64_encoded))
        print('Now verifying that we can decode the Yenc text and then unzip the data to get the original json:')
        _, _, yenc_decoded = yenc.decode(data_as_json_zipped_yenc_encoded)
        data_as_json_unzipped = zlib.decompress(yenc_decoded)
        if yenc_decoded == data_as_json_zipped:
            print('Decoded the Yenc successfully!')
        if data_as_json_unzipped == data_as_json.encode('utf-8'):
            print('Unzipped the data successfully!')

def unpackage_dht_hash_table_entry_func(data_as_yenc_encoded_zipped_data):
    global node_ip_to_node_id_dict
    global node_id_to_node_ip_dict
    _, _, yenc_decoded = yenc.decode(data_as_yenc_encoded_zipped_data)
    data_as_json_unzipped = zlib.decompress(yenc_decoded)
    data_as_list_with_headers = json.loads(data_as_json_unzipped)
    node_ip_and_port = data_as_list_with_headers[0]
    node_ip = node_ip_and_port.split(':')[0]
    node_port = int(node_ip_and_port.split(':')[1])
    node_id = data_as_list_with_headers[1]
    data_as_list = data_as_list_with_headers[2:]
    node_ip_to_node_id_dict[node_ip] = node_id
    node_id_to_node_ip_dict[node_id] = node_ip
    return data_as_list, node_ip, node_port, node_id

def advertise_local_blocks_to_dht_network_func():
    global server
    global default_port
    global alternative_port
    global list_of_local_potential_block_hashes
    global list_of_local_potential_file_hashes
    global list_of_local_potential_file_hashes_unique
    for cnt, current_file_hash in enumerate(list_of_local_potential_file_hashes_unique):
        list_of_block_hashes = get_local_block_list_from_sqlite_for_given_file_hash_func(current_file_hash)
        yencoded_zipped_json = package_dht_hash_table_entry_func(list_of_block_hashes)
        try:
            print('Broadcasting to the DHT network the list of blocks for the zip file with hash: '+current_file_hash)
            asyncio.get_event_loop().run_until_complete(server.set(current_file_hash,yencoded_zipped_json))
            time.sleep(1)
        except:
            pass
        
def ask_dht_network_for_blocks_corresponding_to_hash_of_given_zipfile_func(sha256_hash_of_desired_zipfile):
    global server
    try:
        print('\nAsking DHT network for any block hashes that correspond to the zip file with the SHA256 hash: '+sha256_hash_of_desired_zipfile+'\n')
        yencoded_zipped_json = asyncio.get_event_loop().run_until_complete(server.get(sha256_hash_of_desired_zipfile))
        time.sleep(1)
        data_as_list, node_ip, node_port, node_id = unpackage_dht_hash_table_entry_func(yencoded_zipped_json)
        return data_as_list, node_ip, node_port, node_id
    except:
        pass

def start_new_dht_network_func(port=default_port):
    try:
        server = Server()
        server.listen(port)
        loop = asyncio.get_event_loop()
        loop.set_debug(True)
        try:
            loop.run_forever()
        except KeyboardInterrupt:
            pass
        finally:
            server.stop()
            loop.close()
    except Exception as e:
        print('Error: '+ str(e))

def connect_to_existing_dht_network(port=default_port):
    try:
        loop = asyncio.get_event_loop()
        loop.set_debug(True)
        server = Server()
        server.listen(port)
        return server, loop
    except Exception as e:
        print('Error: '+ str(e))

def get_peer_node_ips(server):
    peers = server.bootstrappableNeighbors()
    return peers

def start_file_server_func():
    global my_node_id
    global block_storage_folder_path
    global file_storage_log_file_path
    global storage_key_cert_file_path
    authorizer = DummyAuthorizer()
    file_server_user_name = 'masternode_user'
    file_server_password = my_node_id
    authorizer.add_user(file_server_user_name, file_server_password, block_storage_folder_path, perm='lr')
    handler = FTPHandler
    handler.authorizer = authorizer
    handler.banner = 'You are now connected to Node_ID: '+my_node_id
    file_server_port = 21
    address = ('0.0.0.0', file_server_port) #Listen for requests from any IP. Should restrict this just to MN IPs later for security against DoS attacks.
    dtp_handler = ThrottledDTPHandler
    dtp_handler.read_limit = 30720*250  # 250*30 Kb/sec (250* 30 * 1024)
    dtp_handler.write_limit = 30720*250  # 250*30 Kb/sec (250* 30 * 1024)
    handler.dtp_handler = dtp_handler 
    file_server = FTPServer(address, handler)
    file_server.max_cons_per_ip = 5
    file_server.max_cons = 256 
    logging.basicConfig(filename=file_storage_log_file_path, level=logging.DEBUG)
    file_server.serve_forever() #handler.masquerade_address = '151.25.42.11' #handler.passive_ports = range(60000, 65535)

def get_list_of_available_blocks_on_remote_node_file_server_func(remote_note_id):
    global block_storage_folder_path
    global file_storage_log_file_path
    remote_node_ip = get_node_ip_address_from_node_id_func(remote_node_id)
    if remote_node_ip != 0:
        print('\n\nNow connecting to remote node: '+ remote_node_ip + ' at IP: '+remote_node_ip)
        ftp = FTP(remote_node_ip)
        file_server_user_name = 'masternode_user'
        file_server_password = remote_note_id
        ftp.login(file_server_user_name, file_server_password)# password is the remote node's node_id
        print('Connected Successfully! Now getting list of available block files...')
        block_file_names_on_remote_node = []
        ftp.retrlines('LIST',lambda x: block_file_names_on_remote_node.append(x))
        block_file_names_on_remote_node_clean = []
        for current_block_file_name in block_file_names_on_remote_node:
            current_block_file_name_clean = current_block_file_name.split()[-1]
            block_file_names_on_remote_node_clean.append(current_block_file_name_clean)
        print('Done getting list of block files!\n')
        ftp.quit()
        return block_file_names_on_remote_node_clean
    else:
        return 0
    
def get_block_file_lists_from_all_nodes_func():
    list_of_unique_node_ip_addresses = get_all_remote_node_ips_func()
    for current_node_ip in list_of_unique_node_ip_addresses:
        get_list_of_available_blocks_on_remote_node_file_server_func(remote_note_id)
    #FINISH THIS
    
def check_sqlitedb_for_remote_blocks_func(sha256_hash_of_desired_zipfile):
    global chunk_db_file_path
    conn = sqlite3.connect(chunk_db_file_path)
    c = conn.cursor()
    query_results_table = c.execute("""SELECT * FROM potential_global_hashes where file_hash = ? ORDER BY datetime_peer_last_seen""",[sha256_hash_of_desired_zipfile]).fetchall()
    list_of_block_hashes = [x[0] for x in query_results_table]
    list_of_file_hashes = [x[1] for x in query_results_table]
    list_of_ip_addresses = [x[2] for x in query_results_table]
    list_of_node_ids = [x[3] for x in query_results_table]
    #list_of_datetimes_peer_last_seen = [x[4] for x in query_results_table]
    potential_local_block_hashes_list, potential_local_file_hashes_list = refresh_block_storage_folder_and_check_block_integrity_func()
    return list_of_block_hashes, list_of_file_hashes, list_of_ip_addresses, list_of_node_ids

def get_all_remote_node_ips_func():
    global chunk_db_file_path
    global bootstrap_node_list
    conn = sqlite3.connect(chunk_db_file_path)
    c = conn.cursor()
    list_of_unique_node_ip_addresses = c.execute("""SELECT DISTINCT remote_node_ip FROM potential_global_hashes""").fetchall()
    list_of_unique_node_ip_addresses =  [x[0] for x in list_of_unique_node_ip_addresses]
    list_of_bootstrap_node_ip_addresses = list(set([x[0] for x in bootstrap_node_list]))
    list_of_unique_node_ip_addresses = list(set(list_of_unique_node_ip_addresses + list_of_bootstrap_node_ip_addresses))
    return list_of_unique_node_ip_addresses
    

def verify_locally_reconstructed_zipfile_func(sha256_hash_of_desired_zipfile):
    global reconstructed_file_destination_folder_path
    successful_reconstruction = 0
    file_path_of_reconstructed_zip_file = glob.glob(os.path.join(reconstructed_file_destination_folder_path,'*'+sha256_hash_of_desired_zipfile+'*.zip'))
    if len(file_path_of_reconstructed_zip_file) > 0:
        with open(file_path_of_reconstructed_zip_file[0],'rb') as f:
            zip_file_data = f.read()
            calculated_hash_of_zip_file = hashlib.sha256(zip_file_data).hexdigest()
        reported_hash_of_zip_file = file_path_of_reconstructed_zip_file.split('__')[-1].replace('.zip','')
        if sha256_hash_of_desired_zipfile == calculated_hash_of_zip_file:
            print('Success! We have reconstructed the file!')
            successful_reconstruction = 1
        if reported_hash_of_zip_file != calculated_hash_of_zip_file:
            print('Reconstructed hash in the file name doesn\'t match the calculated hash!')
    return successful_reconstruction

def retrieve_block_files_from_remote_node_using_file_hash_func(sha256_hash_of_desired_zipfile,remote_node_ip,remote_note_id):
    global block_storage_folder_path
    global file_storage_log_file_path
    global chunk_db_file_path
    remote_block_file_hash_list = []
    remote_zipfile_hash_list = []
    block_file_names_on_remote_node = []
    block_file_names_on_remote_node_clean = []
    print('\n\nNow connecting to remote node: '+ remote_node_ip + ' at IP: '+remote_node_ip)
    ftp = FTP(remote_node_ip)
    file_server_user_name = 'masternode_user'
    file_server_password = remote_note_id
    ftp.login(file_server_user_name, file_server_password)# password is the remote node's node_id
    print('Connected Successfully! Now getting list of available block files...')
    ftp.retrlines('LIST',lambda x: block_file_names_on_remote_node.append(x))
    for current_block_file_name in block_file_names_on_remote_node:
        current_block_file_name_clean = current_block_file_name.split()[-1]
        block_file_names_on_remote_node_clean.append(current_block_file_name_clean)
        
    for current_file_name in block_file_names_on_remote_node_clean:
        reported_block_sha256_hash = current_file_name.split('\\')[-1].split('__')[-1].replace('BlockHash_','').replace('.block','')
        reported_file_sha256_hash = current_file_name.split('\\')[-1].split('__')[1]
        remote_block_file_hash_list.append(reported_block_sha256_hash)
        remote_zipfile_hash_list.append(reported_file_sha256_hash)
    print('Done!')
    potential_local_block_hashes_list, potential_local_file_hashes_list = refresh_block_storage_folder_and_check_block_integrity_func()

    if len(block_file_names_on_remote_node_clean) > 0:
        for block_file_counter,current_block_file_name in enumerate(block_file_names_on_remote_node_clean):
            current_block_file_hash = remote_block_file_hash_list[block_file_counter]
            current_zipfile_hash = remote_zipfile_hash_list[block_file_counter]
            if sha256_hash_of_desired_zipfile in current_block_file_name:
                if current_block_file_hash not in potential_local_block_hashes_list:
                    try:
                        print('\nNow retrieving file: '+current_block_file_name)
                        ftp.retrbinary('RETR '+current_block_file_name, open(os.path.join(block_storage_folder_path,current_block_file_name),'wb').write)
                        print('Done!')
                    except Exception as e:
                        print('Error: '+ str(e))    
    ftp.quit()
    datetime_peer_last_seen = datetime.now().time()
    print('Now writing remote block metadata to SQLite database...')
    conn = sqlite3.connect(chunk_db_file_path)
    datetime_peer_last_seen = datetime.now()
    c = conn.cursor()
    for hash_cnt, current_block_hash in enumerate(remote_block_file_hash_list):
        current_file_hash = remote_zipfile_hash_list[hash_cnt]
        c.execute('INSERT OR IGNORE INTO potential_global_hashes (block_hash, file_hash, remote_node_ip, remote_node_id) VALUES (?,?,?,?)',[current_block_hash, current_file_hash, remote_node_ip, remote_note_id]) 
    conn.commit()
    print('Done!')

def count_number_of_nodes_reporting_having_block_hash_for_all_known_blocks_in_network_func():
    global chunk_db_file_path
    global target_number_of_nodes_per_unique_block_hash
    global target_block_redundancy_factor
    conn = sqlite3.connect(chunk_db_file_path)
    c = conn.cursor()
    list_of_all_unique_block_hashes_on_network = c.execute("""SELECT DISTINCT block_hash FROM potential_global_hashes""").fetchall() + c.execute("""SELECT DISTINCT block_hash FROM potential_local_hashes""").fetchall()
    list_of_all_unique_block_hashes_on_network =  [x[0] for x in list_of_all_unique_block_hashes_on_network]
    list_of_all_unique_block_hashes_on_network = list(set(list_of_all_unique_block_hashes_on_network))
    number_of_total_unique_block_hashes_on_network = len(list_of_all_unique_block_hashes_on_network)
    print('A total of '+ str(number_of_total_unique_block_hashes_on_network)+' unique block file hashes were found both locally and remotely in the SQlite database!')
    node_host_file_count_query_results = c.execute("""SELECT file_hash, count(block_hash), count(remote_node_ip) FROM potential_global_hashes GROUP BY file_hash""").fetchall()
    list_of_zipfile_hashes = [x[0] for x in node_host_file_count_query_results]
    list_of_block_file_counts = [x[1] for x in node_host_file_count_query_results]
    list_of_node_host_counts = [x[2] for x in node_host_file_count_query_results]
    required_replication_boolean_vector = [x < target_number_of_nodes_per_unique_block_hash for x in list_of_node_host_counts]
#   list_of_minimum_required_number_of_blocks = []
#   for current_zipfile_hash in list_of_zipfile_hashes:
#        try:
#            filesize, blocksize, number_of_blocks_required = get_local_block_file_header_data_func(current_zipfile_hash)
#            minimum_block_files_for_current_zipfile_required_in_network = ceil(number_of_blocks_required*target_block_redundancy_factor)
#            list_of_minimum_required_number_of_blocks.append(minimum_block_files_for_current_zipfile_required_in_network)
#        except:
#            list_of_minimum_required_number_of_blocks.append(1)
#            print('Error opening block file locally, perhaps it does not exist locally?')
#    required_replication_boolean_vector = [list_of_block_file_counts < list_of_minimum_required_number_of_blocks]
    list_of_zipfile_hashes_that_require_additional_replication = list(compress(list_of_zipfile_hashes, required_replication_boolean_vector))
    return list_of_zipfile_hashes_that_require_additional_replication, number_of_total_unique_block_hashes_on_network

def run_self_healing_loop_func():
    list_of_zipfile_hashes_that_require_additional_replication, _ = count_number_of_nodes_reporting_having_block_hash_for_all_known_blocks_in_network_func()
    for current_zipfile_hash in list_of_zipfile_hashes_that_require_additional_replication:
        _, _, list_of_zipfile_hashes__local = get_local_matching_blocks_from_zipfile_hash_func(current_zipfile_hash)
        if current_zipfile_hash not in list_of_zipfile_hashes__local:
            try:
                completed_successfully = wrapper_reconstruct_zipfile_from_hash_func(current_zipfile_hash)
                if completed_successfully:
                    try:
                        turn_art_zip_file_into_encoded_block_files_func(current_zipfile_hash)
                    except Exception as e:
                        print('Error: '+ str(e))
            except Exception as e:
                print('Error: '+ str(e))
                
def wrapper_reconstruct_zipfile_from_hash_func(sha256_hash_of_desired_zipfile):
    global reconstructed_file_destination_folder_path
    completed_successfully = 0
    print('Now attempting to locate and reconstruct the original zip file using its SHA256 hash...')
    #First, check this machine:
    list_of_block_file_paths__local, list_of_block_hashes__local, list_of_file_hashes__local = get_local_matching_blocks_from_zipfile_hash_func(sha256_hash_of_desired_zipfile)
    filesize, blocksize, number_of_blocks_required = get_local_block_file_header_data_func(sha256_hash_of_desired_zipfile)
    number_of_relevant_blocks_found_locally = len(list_of_block_hashes__local)
    if number_of_blocks_required == 0:
        number_of_blocks_required = 1 #Dummy value 
    
    if number_of_relevant_blocks_found_locally >= number_of_blocks_required:
        print('We appear to have enough blocks locally to reconstruct the file! Attempting to do that now...')
        try:
            completed_successfully = reconstruct_block_files_into_original_zip_file_func(sha256_hash_of_desired_zipfile)
        except Exception as e:
            print('Error: '+ str(e))  
    else: #Next, we check remote machines that we already know about from our local SQlite chunk database:
        print('Checking with the local SQlite database for block files on remote nodes...')
        list_of_block_hashes__remote, list_of_file_hashes__remote, list_of_ip_addresses, list_of_node_ids = check_sqlitedb_for_remote_blocks_func(sha256_hash_of_desired_zipfile)
        unique_block_hashes__remote = list(OrderedDict.fromkeys(list_of_block_hashes__remote)) 
        unique_node_ips = list(OrderedDict.fromkeys(list_of_ip_addresses)) 
        unique_node_ids = list(OrderedDict.fromkeys(list_of_node_ids))
        number_of_relevant_blocks_found_remotely = len(unique_block_hashes__remote)
        if number_of_relevant_blocks_found_remotely >= number_of_blocks_required:
            print('There appear to be enough blocks remotely to reconstruct the file! Attempting to do that now...')
            for remote_node_counter, current_remote_node_ip in enumerate(unique_node_ips):
                current_remote_node_id = unique_node_ids[remote_node_counter]
                try:
                    print('Now attempting to download block files from Node at IP address: '+current_remote_node_ip + ' (node_id: '+current_remote_node_id+')')
                    retrieve_block_files_from_remote_node_using_file_hash_func(sha256_hash_of_desired_zipfile,current_remote_node_ip,current_remote_node_id)
                except Exception as e:
                    print('Error: '+ str(e))  
                print('Checking if if we now have enough blocks locally to reconstruct file...')
                try:
                    completed_successfully = reconstruct_block_files_into_original_zip_file_func(sha256_hash_of_desired_zipfile)
                except Exception as e:
                    completed_successfully = 0
                    print('Error: '+ str(e))  
                if completed_successfully:
                    break
            if completed_successfully:
                successful_reconstruction = verify_locally_reconstructed_zipfile_func(sha256_hash_of_desired_zipfile)
            else:
                try:#As a last resort, we will now attempt to query the DHT to find more nodes with blocks that we are interested in.
                    list_of_remote_block_hashes, remote_node_ip, remote_node_port, remote_node_id = ask_dht_network_for_blocks_corresponding_to_hash_of_given_zipfile_func(sha256_hash_of_desired_zipfile)
                    time.sleep(10)
                    retrieve_block_files_from_remote_node_using_file_hash_func(sha256_hash_of_desired_zipfile,remote_node_ip,remote_node_id)
                    print('Checking if if we now have enough blocks locally to reconstruct file...')
                    try:
                        completed_successfully = reconstruct_block_files_into_original_zip_file_func(sha256_hash_of_desired_zipfile)
                    except Exception as e:
                        completed_successfully = 0
                        print('Error: '+ str(e))  
                    if completed_successfully:
                        successful_reconstruction = verify_locally_reconstructed_zipfile_func(sha256_hash_of_desired_zipfile)
                        if successful_reconstruction:
                            print('\n\nAll done!\n\n')
                except Exception as e:
                    completed_successfully = 0
                    print('Error: '+ str(e))
    if not completed_successfully:
        print('\n\nError! We were unable to successfully reconstruct the desired zip file!\n')
    return completed_successfully

#LT-Code helper functions:
def gen_tau(S, K, delta):
    """The Robust part of the RSD, we precompute an array for speed"""
    pivot = floor(K/S)
    return [S/K * 1/d for d in range(1, pivot)] \
            + [S/K * log(S/delta)] \
            + [0 for d in range(pivot, K)] 

def gen_rho(K):
    """The Ideal Soliton Distribution, we precompute an array for speed"""
    return [1/K] + [1/(d*(d-1)) for d in range(2, K+1)]

def gen_mu(K, delta, c):
    """The Robust Soliton Distribution on the degree of transmitted blocks"""
    S = c * log(K/delta) * sqrt(K) 
    tau = gen_tau(S, K, delta)
    rho = gen_rho(K)
    normalizer = sum(rho) + sum(tau)
    return [(rho[d] + tau[d])/normalizer for d in range(K)]

def gen_rsd_cdf(K, delta, c):
    """The CDF of the RSD on block degree, precomputed for sampling speed"""
    mu = gen_mu(K, delta, c)
    return [sum(mu[:d+1]) for d in range(K)]

class PRNG(object):
    """A Pseudorandom Number Generator that yields samples from the set of source blocks using the RSD degree distribution described above. """
    def __init__(self, params):
        """Provide RSD parameters on construction """
        self.state = None  # Seed is set by interfacing code using set_seed
        K, delta, c = params
        self.K = K
        self.cdf = gen_rsd_cdf(K, delta, c)

    def _get_next(self):
        """Executes the next iteration of the PRNG evolution process, and returns the result"""
        PRNG_A = 16807
        PRNG_M = (1 << 31) - 1
        self.state = PRNG_A * self.state % PRNG_M
        return self.state

    def _sample_d(self):
        """Samples degree given the precomputed distributions above and the linear PRNG output """
        PRNG_M = (1 << 31) - 1
        PRNG_MAX_RAND = PRNG_M - 1
        p = self._get_next() / PRNG_MAX_RAND
        for ix, v in enumerate(self.cdf):
            if v > p:
                return ix + 1
        return ix + 1

    def set_seed(self, seed):
        """Reset the state of the PRNG to the given seed"""
        self.state = seed

    def get_src_blocks(self, seed=None):
        """Returns the indices of a set of `d` source blocks sampled from indices i = 1, ..., K-1 uniformly, where `d` is sampled from the RSD described above.     """
        if seed:
            self.state = seed
        blockseed = self.state
        d = self._sample_d()
        have = 0
        nums = set()
        while have < d:
            num = self._get_next() % self.K
            if num not in nums:
                nums.add(num)
                have += 1
        return blockseed, d, nums
  
def _split_file(f, blocksize):
    """Block file byte contents into blocksize chunks, padding last one if necessary
    """
    f_bytes = f.read()
    blocks = [int.from_bytes(f_bytes[i:i+blocksize].ljust(blocksize, b'0'), sys.byteorder) 
            for i in range(0, len(f_bytes), blocksize)]
    return len(f_bytes), blocks

class CheckNode(object):
     def __init__(self, src_nodes, check):
        self.check = check
        self.src_nodes = src_nodes

class BlockGraph(object):
    """Graph on which we run Belief Propagation to resolve source node data"""
    def __init__(self, num_blocks):
        self.checks = defaultdict(list)
        self.num_blocks = num_blocks
        self.eliminated = {}

    def add_block(self, nodes, data):
        """Adds a new check node and edges between that node and all source nodes it connects, resolving all message passes that become possible as a result. """
        # We can eliminate this source node
        if len(nodes) == 1:
            to_eliminate = list(self.eliminate(next(iter(nodes)), data))
            # Recursively eliminate all nodes that can now be resolved
            while len(to_eliminate):
                other, check = to_eliminate.pop()
                to_eliminate.extend(self.eliminate(other, check))
        else:
            # Pass messages from already-resolved source nodes
            for node in list(nodes):
                if node in self.eliminated:
                    nodes.remove(node)
                    data ^= self.eliminated[node]

            # Resolve if we are left with a single non-resolved source node
            if len(nodes) == 1:
                return self.add_block(nodes, data)
            else:
                # Add edges for all remaining nodes to this check
                check = CheckNode(nodes, data)
                for node in nodes:
                    self.checks[node].append(check)
        # Are we done yet?
        return len(self.eliminated) >= self.num_blocks

    def eliminate(self, node, data):
        """Resolves a source node, passing the message to all associated checks """
        # Cache resolved value
        self.eliminated[node] = data
        others = self.checks[node]
        del self.checks[node]
        # Pass messages to all associated checks
        for check in others:
            check.check ^= data
            check.src_nodes.remove(node)
            # Yield all nodes that can now be resolved
            if len(check.src_nodes) == 1:
                yield (next(iter(check.src_nodes)), check.check)

def turn_art_zip_file_into_encoded_block_files_func(sha256_hash_of_desired_zipfile):
    global block_storage_folder_path
    global desired_block_size_in_bytes
    global target_block_redundancy_factor
    global reconstructed_file_destination_folder_path
    start_time = time.time()
    zip_file_path = glob.glob(reconstructed_file_destination_folder_path+'*'+sha256_hash_of_desired_zipfile+'*.zip')
    if len(zip_file_path) > 0:
        ramdisk_object = MemoryFS()
        with open(zip_file_path,'rb') as f:
            final_art_file = f.read()
            art_zipfile_hash = hashlib.sha256(final_art_file).hexdigest()
        final_art_file__original_size_in_bytes = os.path.getsize(zip_file_path)
        output_blocks_list = []
        DEFAULT_C = 0.1
        DEFAULT_DELTA = 0.5
        seed = randint(0, 1 << 31 - 1)
        #Process ZIP file into a stream of encoded blocks, and save those blocks as separate files in the output folder:
        print('Now encoding file ' + zip_file_path + ', (' + str(round(final_art_file__original_size_in_bytes/1000000)) + 'mb);\nFile Hash is: '+ art_zipfile_hash+'\n\n')
        total_number_of_blocks_required = ceil((1.00*target_block_redundancy_factor*final_art_file__original_size_in_bytes) / desired_block_size_in_bytes)
        pbar = tqdm(total=total_number_of_blocks_required)
        with open(zip_file_path,'rb') as f:
            f_bytes = f.read()
        filesize = len(f_bytes)
        #Convert file byte contents into blocksize chunks, padding last one if necessary
        blocks = [int.from_bytes(f_bytes[ii:ii+desired_block_size_in_bytes].ljust(desired_block_size_in_bytes, b'0'), sys.byteorder) for ii in range(0, len(f_bytes), desired_block_size_in_bytes)]
        K = len(blocks) # init stream vars
        prng = PRNG(params=(K, DEFAULT_DELTA, DEFAULT_C))
        prng.set_seed(seed)
        number_of_blocks_generated = 0# block generation loop
        while number_of_blocks_generated <= total_number_of_blocks_required:
            update_skip = 1
            if (number_of_blocks_generated % update_skip) == 0:
                pbar.update(update_skip)
            blockseed, d, ix_samples = prng.get_src_blocks()
            block_data = 0
            for ix in ix_samples:
                block_data ^= blocks[ix]
            block = (filesize, desired_block_size_in_bytes, blockseed, int.to_bytes(block_data, desired_block_size_in_bytes, sys.byteorder)) # Generate blocks of XORed data in network byte order
            number_of_blocks_generated = number_of_blocks_generated + 1
            packed_block_data = pack('!III%ss'%desired_block_size_in_bytes, *block)
            output_blocks_list.append(packed_block_data)
            hash_of_block = hashlib.sha256(packed_block_data).hexdigest()
            output_block_file_path = 'FileHash__'+art_zipfile_hash + '__Block__' + '{0:09}'.format(number_of_blocks_generated) + '__BlockHash_' + hash_of_block +'.block'
            try:
                with ramdisk_object.open(output_block_file_path,'wb') as f:
                    f.write(packed_block_data)
            except Exception as e:
                print('Error: '+ str(e))
        duration_in_seconds = round(time.time() - start_time, 1)
        print('\n\nFinished processing in '+str(duration_in_seconds) + ' seconds! \nOriginal zip file was encoded into ' + str(number_of_blocks_generated) + ' blocks of ' + str(ceil(desired_block_size_in_bytes/1000)) + ' kilobytes each. Total size of all blocks is ~' + str(ceil((number_of_blocks_generated*desired_block_size_in_bytes)/1000000)) + ' megabytes\n')
        if not os.path.exists(block_storage_folder_path):
            try:
                os.makedirs(block_storage_folder_path)
            except Exception as e:
                print('Error: '+ str(e))
        filesystem_object = OSFS(block_storage_folder_path)   
        print('Now copying encoded files from ram disk to local storage...')
        try:
            copy_fs(ramdisk_object,filesystem_object)
        except Exception as e:
            print('Error: '+ str(e))
        #ramdisk_object.close()

def reconstruct_block_files_into_original_zip_file_func(sha256_hash_of_desired_zipfile):
    global block_storage_folder_path
    global reconstructed_file_destination_folder_path
    #First, scan the blocks folder:
    start_time = time.time()
    list_of_block_file_paths = glob.glob(os.path.join(block_storage_folder_path,'*'+sha256_hash_of_desired_zipfile+'*.block'))
    reported_file_sha256_hash = list_of_block_file_paths[0].split('\\')[-1].split('__')[1]
    print('\nFound '+str(len(list_of_block_file_paths))+' block files in folder! The SHA256 hash of the original zip file is reported to be: '+reported_file_sha256_hash+'\n')
    reconstructed_file_destination_file_path = reconstructed_file_destination_folder_path + 'Reconstructed_File_with_SHA256_Hash_of__' + reported_file_sha256_hash + '.zip'
    c = 0.1
    delta = 0.5
    block_graph = BlockGraph(len(list_of_block_file_paths))
    for block_count, current_block_file_path in enumerate(list_of_block_file_paths):
        with open(current_block_file_path,'rb') as f:
            packed_block_data = f.read()
        hash_of_block = hashlib.sha256(packed_block_data).hexdigest()
        reported_hash_of_block = current_block_file_path.split('__')[-1].replace('.block','').replace('BlockHash_','')
        if hash_of_block == reported_hash_of_block:
            pass
            #print('Block hash matches reported hash, so block is not corrupted!')
        else:
            print('\nError, the block hash does NOT match the reported hash, so this block is corrupted! Skipping to next block...\n')
            continue
        input_stream = io.BufferedReader(io.BytesIO(packed_block_data),buffer_size=1000000)
        header = unpack('!III', input_stream.read(12))
        filesize = header[0]
        blocksize = header[1]
        blockseed = header[2]
        block = int.from_bytes(input_stream.read(blocksize), 'big')
        number_of_blocks_required = ceil(filesize/blocksize)
        if (block_count % 1) == 0:
            name_parts_list = current_block_file_path.split('\\')[-1].split('_')
            parsed_block_hash = name_parts_list[-1].replace('.block','')
            parsed_block_number = name_parts_list[6].replace('.block','')
            parsed_file_hash = name_parts_list[3].replace('.block','')
            print('\nNow decoding:\nBlock Number: ' + parsed_block_number + '\nFile Hash: ' + parsed_file_hash + '\nBlock Hash: '+ parsed_block_hash)
        prng = PRNG(params=(number_of_blocks_required, delta, c))
        _, _, src_blocks = prng.get_src_blocks(seed = blockseed)
        file_reconstruction_complete = block_graph.add_block(src_blocks, block)
        if file_reconstruction_complete:
            print('\nDone building file! Processed a total of '+str(block_count)+' blocks\n')
            break
    if os.path.isfile(reconstructed_file_destination_folder_path):
        print('\nA file with that name exists already; deleting this first before attempting to write new file!\n')
        try:
            os.remove(reconstructed_file_destination_folder_path)
        except:
            print('Error removing file!')
    else:
        with open(reconstructed_file_destination_folder_path,'wb') as f: 
            for ix, block_bytes in enumerate(map(lambda p: int.to_bytes(p[1], blocksize, 'big'), sorted(block_graph.eliminated.items(), key = lambda p:p[0]))):
                if ix < number_of_blocks_required - 1 or filesize % blocksize == 0:
                    f.write(block_bytes)
                else:
                    f.write(block_bytes[:filesize%blocksize])
    try:
        with open(reconstructed_file_destination_folder_path,'rb') as f:
            reconstructed_file = f.read()
            reconstructed_file_hash = hashlib.sha256(reconstructed_file).hexdigest()
            if reported_file_sha256_hash == reconstructed_file_hash:
                completed_successfully = 1
                print('\nThe SHA256 hash of the reconstructed file matches the reported file hash-- file is valid!\n')
            else:
                completed_successfully = 0
                print('\nProblem! The SHA256 hash of the reconstructed file does NOT match the expected hash! File is not valid.\n')
    except Exception as e:
        print('Error: '+ str(e))
    duration_in_seconds = round(time.time() - start_time, 1)
    print('\n\nFinished processing in '+str(duration_in_seconds) + ' seconds!')
    return completed_successfully

#query_results_table = c.execute("""SELECT * FROM potential_global_hashes where file_hash = ?""",[sha256_hash_of_desired_zipfile]).fetchall()
#query_results_table = c.execute("""SELECT * FROM potential_global_hashes""").fetchall()

###############################################################################################################
# Script:
###############################################################################################################
my_node_ip_address = get_my_local_ip_func()
node_ip_to_node_id_dict = {}
node_id_to_node_ip_dict = {}
my_node_ip_address = get_my_local_ip_func()
my_node_id = generate_node_mac_hash_id()
node_ip_to_node_id_dict[my_node_ip_address] = my_node_id
node_id_to_node_ip_dict[my_node_id] = my_node_ip_address

bootstrap_node_list = [(x,int(default_port)) for x in list_of_ips if x[0] != my_node_ip_address] +  [(x,int(alternative_port)) for x in list_of_ips if x[0] != my_node_ip_address]
handler = logging.StreamHandler()
formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
handler.setFormatter(formatter)
dht_log = logging.getLogger('kademlia')
dht_log.addHandler(handler)
dht_log.setLevel(logging.DEBUG)   
conn = sqlite3.connect(chunk_db_file_path)
c = conn.cursor()
if use_generate_new_sqlite_chunk_database:
    regenerate_sqlite_chunk_database_func()
potential_local_block_hashes_list, potential_local_file_hashes_list = refresh_block_storage_folder_and_check_block_integrity_func()
list_of_local_potential_block_hashes = c.execute('SELECT block_hash FROM potential_local_hashes').fetchall()
list_of_local_potential_block_hashes = [x[0] for x in list_of_local_potential_block_hashes]
list_of_local_potential_file_hashes = c.execute('SELECT file_hash FROM potential_local_hashes').fetchall()
list_of_local_potential_file_hashes = [x[0] for x in list_of_local_potential_file_hashes]
list_of_local_potential_block_hashes_json = json.dumps(list_of_local_potential_block_hashes)
list_of_local_potential_file_hashes_json = json.dumps(list_of_local_potential_file_hashes)
list_of_local_potential_file_hashes_unique = c.execute('SELECT DISTINCT file_hash FROM potential_local_hashes').fetchall()
list_of_local_potential_file_hashes_unique = [x[0] for x in list_of_local_potential_file_hashes_unique]
list_of_local_potential_file_hashes_unique_json = json.dumps(list_of_local_potential_file_hashes_unique)
_, my_node_ip, my_node_port, my_node_id = unpackage_dht_hash_table_entry_func(package_dht_hash_table_entry_func([]))
file_server_process = Process(target=start_file_server_func, args=())
try:
    file_server_process.start()
except Exception as e:
    print('Error: '+ str(e))
    
if use_start_new_dht_network:
    try:
     dht_process = Process(target=start_new_dht_network_func, args=(alternative_port))
     dht_process.start()
    except:
        print('Some kind of error, loop is probably running already!')

if use_connect_to_existing_dht_network:
    try:
        server, loop = connect_to_existing_dht_network(default_port)
    except:
        print('Some kind of error, loop is probably running already!')
        try:
            loop = asyncio.get_event_loop()
            server = Server()
            server.listen(default_port)
        except:
            pass
    print('Bootstrapping network now!\n')
    try:
        loop.run_until_complete(server.bootstrap(bootstrap_node_list))
    except:
        print('Error bootstrapping network! Probably alrady using socket, try closing and restarting system!')
    
    if use_advertise_local_blocks_on_dht:
        advertise_local_blocks_to_dht_network_func()
        
    if use_demonstrate_asking_dht_network_for_file_blocks:
        #test_file_hash = 'a75a1757d54ad0876f9b2cf9b64b1017b6293ceed61e80cd64aae5abfdc8514e' #Arturo_number_04
        #test_file_hash = '734ad8214cb20deab6474f19397d012678de4acc61c4c24de35ce3b8cc466d5f' #Mark_Kong_number_02
        test_file_hash = '105c4c9b9d79ed544c36df792f7bbbddb6700209f51e4b9e1073fa41653d2aa8' #Iris Madula Number 11
        sha256hashsize = sys.getsizeof(test_file_hash)
        print('Now asking network for hashes of blocks that correspond to the zipfile with a SHA256 hash of: '+test_file_hash)
        try:
            list_of_remote_block_hashes, remote_node_ip, remote_node_port, remote_node_id = ask_dht_network_for_blocks_corresponding_to_hash_of_given_zipfile_func(test_file_hash)
            print('\n\nDone! Got back '+str(len(list_of_remote_block_hashes))+' Block Hashes:\n')
            print('\nNode IP:port is '+remote_node_ip+':'+str(remote_node_port))
            print('\nNode ID is '+remote_node_id+'\n\n')
            for current_block_hash in list_of_remote_block_hashes:
                print(current_block_hash+', ',end='')
        except:
            print('Problem requesting block files from DHT network!')

    sha256_hash_of_desired_zipfile = 'a75a1757d54ad0876f9b2cf9b64b1017b6293ceed61e80cd64aae5abfdc8514e'
    remote_node_ip = '140.82.14.38'#'108.61.86.243' #'140.82.2.58'
    remote_node_id = 'ab5476e2a797c669ae7fdc5f2a8684301db812061357b703aa4e990457c86179'
    sha256_hash_of_desired_block = '1e6864df358abd54b3ebcdb8da05c16149f208ef10aab4ac86e12cb1b0074ac9'
    wrapper_reconstruct_zipfile_from_hash_func(sha256_hash_of_desired_zipfile)

    if use_demonstrate_ftp_based_block_file_transfer:
        retrieve_block_files_from_remote_node_using_file_hash_func(sha256_hash_of_desired_zipfile,remote_node_ip,remote_node_id)
        list_of_block_hashes, list_of_file_hashes, list_of_ip_addresses, list_of_node_ids = check_sqlitedb_for_remote_blocks_func(sha256_hash_of_desired_zipfile)
        filesize, blocksize, number_of_blocks_required = get_local_block_file_header_data_func(sha256_hash_of_desired_zipfile)
