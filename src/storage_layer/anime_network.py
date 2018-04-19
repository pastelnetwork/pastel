 
import sys, time, os.path, io, glob, hashlib, imghdr, random, os, sqlite3, socket, warnings, base64, json, hmac, logging, asyncio
#import bencode, crc32c, dht, krpc, tracker, utils #TinyBT
from shutil import copyfile
#from fs.memoryfs import MemoryFS
from random import randint
from math import ceil, log, floor, sqrt
from collections import defaultdict
from zipfile import ZipFile
from subprocess import check_output
from datetime import datetime
from kademlia.network import Server
from uuid import getnode as get_mac
with warnings.catch_warnings():
    warnings.filterwarnings("ignore",category=DeprecationWarning)
    from fs.copy import copy_fs
    from fs.osfs import OSFS
try:
    from tqdm import tqdm
    import yenc
    import zstandard as zstd
except:
    pass
    
folder_path_of_art_folders_to_encode = 'C:\\animecoin\\art_folders_to_encode\\' #Each subfolder contains the various art files pertaining to a given art asset.
block_storage_folder_path = 'C:\\animecoin\\art_block_storage\\'
chunk_db_file_path = 'C:\\animecoin\\anime_chunkdb.sqlite'
use_integrity_scan_of_block_files_on_startup = 0

if use_integrity_scan_of_block_files_on_startup:
    if not os.path.exists(chunk_db_file_path):
        conn = sqlite3.connect(chunk_db_file_path)
        c = conn.cursor()
        local_hash_table_creation_string= """CREATE TABLE potential_local_hashes (block_hash text PRIMARY KEY, file_hash);"""
        global_hash_table_creation_string= """CREATE TABLE potential_global_hashes (
                                                block_hash text,
                                                file_hash text,
                                                peer_ip_and_port text,
                                                datetime_peer_last_seen text,
                                                PRIMARY KEY (block_hash,peer_ip_and_port)
                                                );"""
        c.execute(local_hash_table_creation_string)
        c.execute(global_hash_table_creation_string)
        conn.commit()
        
    potential_local_block_hashes_list = []
    potential_local_file_hashes_list = []
    list_of_block_file_paths = glob.glob(block_storage_folder_path+'*.block')
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
    table_name= 'potential_local_hashes'
    id_column = 'block_hash'
    column_name = 'file_hash'
    conn = sqlite3.connect(chunk_db_file_path)
    c = conn.cursor()
    c.execute("""DELETE FROM potential_local_hashes""")
    for hash_cnt, current_block_hash in enumerate(potential_local_block_hashes_list):
        current_file_hash = potential_local_file_hashes_list[hash_cnt]
        sql_string = """INSERT OR IGNORE INTO potential_local_hashes (block_hash, file_hash) VALUES (\"{blockhash}\", \"{filehash}\")""".format(blockhash=current_block_hash, filehash=current_file_hash)
        c.execute(sql_string)
    conn.commit()
    print('Done writing file hash data to SQLite file!\n')
    #list_of_local_potential_block_hashes = c.execute('SELECT block_hash FROM potential_local_hashes').fetchall()
    #set_of_local_potential_file_hashes = c.execute('SELECT DISTINCT file_hash FROM potential_local_hashes').fetchall()
    #conn.close()
    #list_of_block_file_paths = glob.glob(block_storage_folder_path+'*.block')
    #current_block_file_path = list_of_block_file_paths[0]


def generate_node_mac_hash_id():
    return hashlib.sha256(str(int(get_mac())).encode('utf-8')).hexdigest()

def get_local_matching_blocks_from_file_hash_func(block_storage_folder_path,sha256_hash_of_desired_file):
    list_of_block_file_paths = glob.glob(os.path.join(block_storage_folder_path,'*'+sha256_hash_of_desired_file+'*.block'))
    list_of_block_hashes = []
    list_of_file_hashes = []
    for current_block_file_path in list_of_block_file_paths:
        reported_file_sha256_hash = current_block_file_path.split('\\')[-1].split('__')[1]
        list_of_file_hashes.append(reported_file_sha256_hash)
        reported_block_file_sha256_hash = current_block_file_path.split('__')[-1].replace('.block','').replace('BlockHash_','')
        list_of_block_hashes.append(reported_block_file_sha256_hash)
    return list_of_block_file_paths, list_of_block_hashes, list_of_file_hashes


try:
    list_of_block_file_paths, list_of_block_hashes, list_of_file_hashes = get_local_matching_blocks_from_file_hash_func(block_storage_folder_path,example_file_hash)
    example_block_hash_1 = list_of_block_hashes[0]
    example_block_hash_2 = list_of_block_hashes[1]
    example_file_hash_1 = list_of_file_hashes[0]
    example_file_hash_2 = list_of_file_hashes[1]
    example_block_file_path_hash_1 = list_of_block_file_paths[0]
    example_block_file_path_hash_2 = list_of_block_file_paths[0]
except:
    example_block_hash_1 = 'bb739b16f1e8bb3fe51f32c11319248649c18423c0c75bcaaae68bf4cb09cde0'
    example_block_hash_2 = 'f1126b528c10e97af98e1af71894e5a7a2d1dbf9ba30c94fff1f4d494e669ced'
    example_file_hash_1 = '7eb4b448d346b5bee36bba8b2a1e4983d64ac1a57c3064e39e93110c4704ef1b'
    example_file_hash_2 = '586e0ad859248b54e7c055028126bf22bc8a4850440c9829505ba9cf8164bba1'
    
default_port = 14142
alternative_port = 2718
bootstrap_node1 = ('108.61.86.243', int(default_port))
bootstrap_node2 = ('108.61.86.243', int(alternative_port))
bootstrap_node3 = ('140.82.14.38', int(default_port))
bootstrap_node4 = ('140.82.14.38', int(alternative_port))
bootstrap_node5 = ('140.82.2.58', int(default_port))
bootstrap_node6 = ('140.82.2.58', int(alternative_port))

bootstrap_node_list = [bootstrap_node1,bootstrap_node2,bootstrap_node3,bootstrap_node4,bootstrap_node5,bootstrap_node6]
handler = logging.StreamHandler()
formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
handler.setFormatter(formatter)
log = logging.getLogger('kademlia')
log.addHandler(handler)
log.setLevel(logging.DEBUG)   
my_node_id = generate_node_mac_hash_id()

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.connect(("8.8.8.8", 80))
my_node_ip_address = s.getsockname()[0]
s.close()

bootstrap_node_list_filtered = [x for x in bootstrap_node_list if x[0] != my_node_ip_address]

conn = sqlite3.connect(chunk_db_file_path)
c = conn.cursor()
list_of_local_potential_block_hashes = c.execute('SELECT block_hash FROM potential_local_hashes').fetchall()
list_of_local_potential_block_hashes = [x[0] for x in list_of_local_potential_block_hashes]
list_of_local_potential_file_hashes = c.execute('SELECT file_hash FROM potential_local_hashes').fetchall()
list_of_local_potential_file_hashes = [x[0] for x in list_of_local_potential_file_hashes]
list_of_local_potential_block_hashes_json = json.dumps(list_of_local_potential_block_hashes)
list_of_local_potential_file_hashes_json = json.dumps(list_of_local_potential_file_hashes)
list_of_local_potential_file_hashes_unique = c.execute('SELECT DISTINCT file_hash FROM potential_local_hashes').fetchall()
list_of_local_potential_file_hashes_unique = [x[0] for x in list_of_local_potential_file_hashes_unique]
list_of_local_potential_file_hashes_unique_json = json.dumps(list_of_local_potential_file_hashes_unique)



def get_blocks_for_given_file_hash_func(file_hash):
    global chunk_db_file_path
    conn = sqlite3.connect(chunk_db_file_path)
    c = conn.cursor()
    blocks_for_this_file_hash = c.execute("""SELECT block_hash FROM potential_local_hashes where file_hash = ?""",[file_hash]).fetchall()
    list_of_block_hashes = []
    for current_block in blocks_for_this_file_hash:
        list_of_block_hashes.append(current_block[0])
    list_of_block_hashes_json = json.dumps(list_of_block_hashes)
    return list_of_block_hashes, list_of_block_hashes_json

def advertise_my_local_blocks_func(server,list_of_local_potential_block_hashes):
    global chunk_db_file_path
    global default_port
    global alternative_port
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        my_node_ip_address = s.getsockname()[0]
        s.close()
    except Exception as e:
        print('Error: '+ str(e)) 
    my_node_ip_and_port = my_node_ip_address+ ':' + str(default_port)
    list_of_local_potential_block_hashes.insert(0,my_node_ip_and_port)
    my_node_id = generate_node_mac_hash_id()
    try:
        print('\nAdvertising to the network the '+str(len(list_of_local_potential_block_hashes))+' blocks held by the local machine, along with the local IP:port\n')
        asyncio.get_event_loop().run_until_complete(server.set(my_node_id,list_of_local_potential_block_hashes))
        time.sleep(1)
    except:
        pass
    loop.run_until_complete(server.set(my_node_id,my_node_ip_address+':'+str(default_port))) #Announce who you are

    
def update_blockhash_to_zipfilehash_table_func(server,list_of_local_potential_block_hashes,list_of_local_potential_file_hashes,list_of_local_potential_file_hashes_unique):
    for cnt, current_file_hash in enumerate(list_of_local_potential_file_hashes_unique):
        list_of_block_hashes, list_of_block_hashes_json = get_blocks_for_given_file_hash_func(current_file_hash)
        try:
            print('Broadcasting to the network the list of blocks for the zip file with hash: '+current_file_hash)
            asyncio.get_event_loop().run_until_complete(server.set(current_file_hash,list_of_block_hashes_json))
            time.sleep(1)
        except:
            pass
        
def ask_network_for_blocks_corresponding_to_hash_of_given_zipfile_func(server,sha256_hash_of_desired_zipfile):
        try:
            print('\nAsking network for any block hashes that correspond to the zip file with the SHA256 hash: '+sha256_hash_of_desired_zipfile+'\n')
            block_list = asyncio.get_event_loop().run_until_complete(server.get(sha256_hash_of_desired_zipfile))
            time.sleep(1)
            block_list = json.loads(block_list)
        except:
            pass
        return block_list
    
def ask_network_for_nodeids_corresponding_to_hash_of_block_hash_func(server,sha256_hash_of_desired_block):
        try:
            print('\nAsking network for any nodeIDs that correspond to the block file with the SHA256 hash: '+sha256_hash_of_desired_block+'\n')
            node_list = asyncio.get_event_loop().run_until_complete(server.get(sha256_hash_of_desired_block))
            node_list = json.loads(node_list)
        except:
            pass
        return node_list
 
def ask_network_for_node_information_func(desired_nodeid):
    try:
        print('\nAsking network for the ip/port and the block list of the node with node_id: '+desired_nodeid+'\n')
        node_info = asyncio.get_event_loop().run_until_complete(server.get(desired_nodeid))
        time.sleep(1)
        node_info = json.loads(node_info)
    except:
        pass
    return node_info

use_start_new_network = 0
use_connect_to_existing_network = 0
use_register_local_blocks_on_dht = 1 
use_demonstrate_asking_network_for_file_blocks = 1

if use_start_new_network:
    server = Server()
    try:
        server.listen(default_port)
        loop = asyncio.get_event_loop()
        loop.set_debug(True)
        try:
            loop.run_forever()
        except KeyboardInterrupt:
            pass
        finally:
            server.stop()
            loop.close()
    except:
        server.listen(alternative_port)
        loop = asyncio.get_event_loop()
        loop.set_debug(True)
        try:
            loop.run_forever()
        except KeyboardInterrupt:
            pass
        finally:
            server.stop()
            loop.close()

if use_connect_to_existing_network:
    loop = asyncio.get_event_loop()
    loop.set_debug(True)
    server = Server()
    
    try:
        server.listen(default_port)
    except:
        server.listen(alternative_port)
        print('Problem!')
    
    print('Bootstrapping network now!\n')
    loop.run_until_complete(server.bootstrap(bootstrap_node_list_filtered))
    time.sleep(10)
    
    if use_register_local_blocks_on_dht:
        update_blockhash_to_zipfilehash_table_func(server,list_of_local_potential_block_hashes,list_of_local_potential_file_hashes,list_of_local_potential_file_hashes_unique)
        time.sleep(10)
        advertise_my_local_blocks_func(server,list_of_local_potential_block_hashes)
        time.sleep(10)

        
    if use_demonstrate_asking_network_for_file_blocks:
        test_file_hash = 'a75a1757d54ad0876f9b2cf9b64b1017b6293ceed61e80cd64aae5abfdc8514e' #Arturo_number_04
        print('Now asking network for hashes of blocks that correspond to the zipfile with a SHA256 hash of: '+test_file_hash)
        block_hash_list = ask_network_for_blocks_corresponding_to_hash_of_given_zipfile_func(server,test_file_hash)
        print('Sleeping...\n')
        time.sleep(10)
        list_of_node_ids = []
        list_of_node_ids_lengths = []
        for current_block_hash in block_hash_list:
            current_node_id = ask_network_for_nodeids_corresponding_to_hash_of_block_hash_func(server,current_block_hash)
            print('Sleeping...\n')
            time.sleep(10)
            if type(current_node_id) == 'str':
                try:
                    print('Node_ID: '+current_node_id+' has block with hash: '+current_block_hash)
                    if current_node_id not in list_of_node_ids:
                        list_of_node_ids.append(current_node_id)
                        list_of_node_ids_lengths.append(len(current_node_id))
                        if len(list_of_node_ids_lengths) > 4:
                            if list_of_node_ids_lengths[-1] == list_of_node_ids_lengths[-2]:
                                break
                except Exception as e:
                    print('Error: '+ str(e)) 
                
        list_of_node_ids = list(set(list_of_node_ids))
        
        list_of_ip_port_strings = []
        
        for current_node_id in list_of_node_ids:
            node_info = ask_network_for_node_information_func(current_node_id)
            ip_port_string = node_info[0]
            if ip_port_string not in list_of_ip_port_strings:
                list_of_ip_port_strings.append(ip_port_string)
        

    if 0:
        test_block_hash = '2755c37e3a3c9808d027348f48f889df7a5bac61cd0add15fa2d0a92da1ffb29'
        result = loop.run_until_complete(server.get(test_block_hash))
        print(result)
    
    if 0:
        loop.call_soon_threadsafe(loop.stop)
        server.stop()
        loop.close()

