import sys, time, os.path, io, string, glob, hashlib, imghdr, random, os, zlib, pickle, sqlite3, uuid, socket, warnings, base64, json, hmac, logging, asyncio, binascii, subprocess
warnings.simplefilter(action='ignore', category=FutureWarning)
warnings.filterwarnings('ignore',category=DeprecationWarning)
import requests
import lxml.html
from struct import pack, unpack
from multiprocessing import Process, Queue
from collections import OrderedDict
from datetime import datetime
from itertools import compress
from kademlia.network import Server
from math import ceil
try:
    from tqdm import tqdm
    import yenc
except:
    pass
#from anime_storage import *
#Requirements: pip install kademlia, yenc, tqdm, fs, lxml, requests

#Add to crontab:
#* * * * * 
# (first install tree: apt install tree)
    
###############################################################################################################
# Parameters:
###############################################################################################################
use_verify_integrity = 1
use_generate_new_sqlite_chunk_database = 1
use_advertise_local_blocks_on_dht = 1 
use_demonstrate_asking_dht_network_for_file_blocks = 0
root_animecoin_folder_path = 'C:\\animecoin\\'
folder_path_of_art_folders_to_encode = os.path.join(root_animecoin_folder_path,'art_folders_to_encode' + os.sep)#Each subfolder contains the various art files pertaining to a given art asset.
block_storage_folder_path = os.path.join(root_animecoin_folder_path,'art_block_storage' + os.sep)
folder_path_of_remote_node_sqlite_files = os.path.join(root_animecoin_folder_path,'remote_node_sqlite_files' + os.sep)
reconstructed_file_destination_folder_path = os.path.join(root_animecoin_folder_path,'reconstructed_files' + os.sep)
misc_masternode_files_folder_path = os.path.join(root_animecoin_folder_path,'misc_masternode_files' + os.sep) #Where we store some of the SQlite databases
chunk_db_file_path = os.path.join(misc_masternode_files_folder_path,'anime_chunkdb.sqlite')
file_storage_log_file_path = os.path.join(misc_masternode_files_folder_path,'anime_storage_log.txt')
nginx_allowed_ip_whitelist_file_path = os.path.join(misc_masternode_files_folder_path,'masternode_ip_whitelist.conf')

default_port = 14142
alternative_port = 2718
list_of_ips = ['108.61.86.243','140.82.14.38','140.82.2.58']
target_number_of_nodes_per_unique_block_hash = 10
target_block_redundancy_factor = 10 #How many times more blocks should we store than are required to regenerate the file?
desired_block_size_in_bytes = 1024*1000*2
if 0: #NAT traversal experiments
    import stun
    nat_type, external_ip, external_port = stun.get_ip_info()

example_list_of_valid_masternode_ip_addresses = ['65.200.165.210','207.246.93.232','140.82.14.38','140.82.2.58']
###############################################################################################################
# Functions:
###############################################################################################################

#Utility functions:
def get_my_local_ip_func():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.connect(("8.8.8.8", 80))
    my_node_ip_address = s.getsockname()[0]
    s.close()
    return my_node_ip_address 

def generate_nginx_ip_whitelist_file_func(python_list_of_masternode_ip_addresses):
    global nginx_allowed_ip_whitelist_file_path
    try:
        with open(nginx_allowed_ip_whitelist_file_path,'w') as f:
            for current_ip_address in python_list_of_masternode_ip_addresses:
                allow_string = 'allow ' +current_ip_address+';\n'
                f.write(allow_string)
    except Exception as e:
        print('Error: '+ str(e))    

def regenerate_sqlite_chunk_database_func():
    global chunk_db_file_path
    try:
        conn = sqlite3.connect(chunk_db_file_path)
        c = conn.cursor()
        local_hash_table_creation_string= """CREATE TABLE potential_local_hashes (block_hash text PRIMARY KEY, file_hash text);"""
        global_hash_table_creation_string= """CREATE TABLE potential_global_hashes (block_hash text, file_hash text, remote_node_ip text, datetime_peer_last_seen TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, PRIMARY KEY (block_hash, remote_node_ip));"""
        c.execute(local_hash_table_creation_string)
        c.execute(global_hash_table_creation_string)
        conn.commit()
        conn.close()
    except Exception as e:
        print('Error: '+ str(e))
        
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

def package_binary_dht_hash_table_entry_func(binary_data):
    zipped_data = zlib.compress(binary_data)
    _, _, data_as_zipped_yenc_encoded = yenc.encode(zipped_data)
    return data_as_zipped_yenc_encoded

def unpackage_dht_hash_table_entry_func(data_as_yenc_encoded_zipped_data):
    _, _, yenc_decoded = yenc.decode(data_as_yenc_encoded_zipped_data)
    data_as_json_unzipped = zlib.decompress(yenc_decoded)
    data_as_list_with_headers = json.loads(data_as_json_unzipped)
    node_ip_and_port = data_as_list_with_headers[0]
    node_ip = node_ip_and_port.split(':')[0]
    node_port = int(node_ip_and_port.split(':')[1])
    data_as_list = data_as_list_with_headers[1:]
    return data_as_list, node_ip, node_port

def broadcast_sqlite_chunkdb_to_dht_network_func():
    global chunk_db_file_path
    global server
    global default_port
    global alternative_port
    my_node_ip_address =  get_my_local_ip_func()
    try:
        with open(chunk_db_file_path,'rb') as f:
            sqlitedb_chunkdb_binary_data = f.read()
        data_as_zipped_yenc_encoded = package_binary_dht_hash_table_entry_func(sqlitedb_chunkdb_binary_data)
        print('Broadcasting SQlite chunkdb file to the DHT network, using this node\'s IP address ('+str(my_node_ip_address)+') as the key!')
        asyncio.get_event_loop().run_until_complete(server.set(my_node_ip_address,data_as_zipped_yenc_encoded))
    except Exception as e:
        print('Error: '+ str(e))

def get_remote_node_chunkdb_file_from_dht_network_func(remote_node_ip):
    global folder_path_of_remote_node_sqlite_files
    remote_node_ip = str(remote_node_ip)
    try:
        print('\nAsking DHT network for the chunkdb file from the remote node at IP address '+remote_node_ip)
        yencoded_zipped_data = asyncio.get_event_loop().run_until_complete(server.get(remote_node_ip))
        _, _, yenc_decoded = yenc.decode(yencoded_zipped_data)
        data_unzipped = zlib.decompress(yenc_decoded)
        output_file_name = 'Remote_Node__'+ remote_node_ip.replace('.','_') + '__chunkdb.sqlite'
        remote_node_chunkd_output_filepath = os.path.join(folder_path_of_remote_node_sqlite_files,output_file_name)
        with open(remote_node_chunkd_output_filepath,'wb') as f:
            f.write(data_unzipped)
    except Exception as e:
        print('Error: '+ str(e))
        
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
        if len(yencoded_zipped_json) > 0:
            data_as_list, node_ip, node_port = unpackage_dht_hash_table_entry_func(yencoded_zipped_json)
            return data_as_list, node_ip, node_port
        else:
            print('Did not get back a valid response!')
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

def get_list_of_available_blocks_on_remote_node_file_server_func(remote_node_ip,type_of_file_to_retrieve='blocks'):
    try:
        r = requests.get('http://'+remote_node_ip+'/')
        html_file_listing = r.text
        parsed_file_listing = lxml.html.fromstring(html_file_listing)
        list_of_links = parsed_file_listing.xpath('//a/@href')
        list_of_links = list_of_links[1:]
        list_of_block_filenames = []
        list_of_signature_filenames = []
        list_of_zipfile_filenames = []
        for link in list_of_links:
            if '.block' in link:
                list_of_block_filenames.append(link)
            if '.sig' in link:
                list_of_signature_filenames.append(link)
            if '.zip' in link:
                list_of_zipfile_filenames.append(link)
        list_of_block_hashes = []
        list_of_file_hashes = []
        if (len(list_of_block_filenames) > 0) and (type_of_file_to_retrieve == 'blocks'):
            for current_block_file_name in list_of_block_filenames:
                reported_file_sha256_hash = current_block_file_name.split('__')[1]
                list_of_file_hashes.append(reported_file_sha256_hash)
                reported_block_file_sha256_hash = current_block_file_name.split('__')[-1].replace('.block','').replace('BlockHash_','')
                list_of_block_hashes.append(reported_block_file_sha256_hash)
            return list_of_block_filenames, list_of_block_hashes, list_of_file_hashes
        if (len(list_of_signature_filenames) > 0) and (type_of_file_to_retrieve == 'signatures'):
            for current_signature_file_name in list_of_signature_filenames:
                reported_file_sha256_hash = current_signature_file_name.split('__')[1].replace('.sig','')
                list_of_file_hashes.append(reported_file_sha256_hash)
            return list_of_signature_filenames, list_of_file_hashes
        if (len(list_of_zipfile_filenames) > 0) and (type_of_file_to_retrieve == 'zipfiles'):
            for current_zipfile_file_name in list_of_zipfile_filenames:
                reported_file_sha256_hash = current_zipfile_file_name.split('__')[1].replace('.zip','')
                list_of_file_hashes.append(reported_file_sha256_hash)
            return list_of_zipfile_filenames, list_of_file_hashes        
    except Exception as e:
        print('Error: '+ str(e))

def download_file_func(url_of_file_to_download, folder_path_for_saved_file):
    path_to_downloaded_file = os.path.join(folder_path_for_saved_file, url_of_file_to_download.split('/')[-1])
    r = requests.get(url_of_file_to_download, stream=True)
    with open(path_to_downloaded_file, 'wb') as f:
        for chunk in r.iter_content(chunk_size=1024): 
            if chunk: # filter out keep-alive new chunks
                f.write(chunk)
    return path_to_downloaded_file

def download_remote_block_files_for_given_zipfile_hash_func(remote_node_ip,sha256_hash_of_desired_zipfile):
   global block_storage_folder_path
   try:
        _, list_of_local_block_hashes, _ = get_local_matching_blocks_from_zipfile_hash_func(sha256_hash_of_desired_zipfile)
        file_listing_url = 'http://'+remote_node_ip+'/'
        r = requests.get(file_listing_url)
        html_file_listing = r.text
        parsed_file_listing = lxml.html.fromstring(html_file_listing)
        list_of_links = parsed_file_listing.xpath('//a/@href')
        list_of_links = list_of_links[1:]
        list_of_filenames = [x.split('FileHash__')[1] for x in list_of_links]
        list_of_downloaded_files = []
        for cnt, current_block_file_name in enumerate(list_of_filenames):
            reported_file_sha256_hash = current_block_file_name.split('__')[0]
            reported_block_file_sha256_hash = current_block_file_name.split('__')[-1].replace('.block','').replace('BlockHash_','')
            if (reported_file_sha256_hash == sha256_hash_of_desired_zipfile) and (reported_block_file_sha256_hash not in list_of_local_block_hashes):
                url_of_file_to_download = file_listing_url+list_of_links[cnt]
                try:
                    print('Downloading new block file with hash '+reported_block_file_sha256_hash+' from node at '+remote_node_ip)
                    path_to_downloaded_file = download_file_func(url_of_file_to_download, block_storage_folder_path)
                    list_of_downloaded_files.append(path_to_downloaded_file)
                    print('Done!')
                except Exception as e:
                    print('Error: '+ str(e))
        return list_of_downloaded_files
   except Exception as e:
       print('Error: '+ str(e))
       

def get_block_file_lists_from_all_nodes_func():
    list_of_unique_node_ip_addresses = get_all_remote_node_ips_func()
    for current_node_ip in list_of_unique_node_ip_addresses:
        get_list_of_available_blocks_on_remote_node_file_server_func(remote_node_ip)
    #FINISH THIS
    
def check_sqlitedb_for_remote_blocks_func(sha256_hash_of_desired_zipfile):
    global chunk_db_file_path
    conn = sqlite3.connect(chunk_db_file_path)
    c = conn.cursor()
    query_results_table = c.execute("""SELECT * FROM potential_global_hashes where file_hash = ? ORDER BY datetime_peer_last_seen""",[sha256_hash_of_desired_zipfile]).fetchall()
    list_of_block_hashes = [x[0] for x in query_results_table]
    list_of_file_hashes = [x[1] for x in query_results_table]
    list_of_ip_addresses = [x[2] for x in query_results_table]
    potential_local_block_hashes_list, potential_local_file_hashes_list = refresh_block_storage_folder_and_check_block_integrity_func()
    return list_of_block_hashes, list_of_file_hashes, list_of_ip_addresses

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
                        encode_final_art_zipfile_into_luby_transform_blocks_func(current_zipfile_hash)
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
            completed_successfully = decode_block_files_into_art_zipfile_func(sha256_hash_of_desired_zipfile)
        except Exception as e:
            print('Error: '+ str(e))  
    else: #Next, we check remote machines that we already know about from our local SQlite chunk database:
        print('Checking with the local SQlite database for block files on remote nodes...')
        list_of_block_hashes__remote, list_of_file_hashes__remote, list_of_ip_addresses = check_sqlitedb_for_remote_blocks_func(sha256_hash_of_desired_zipfile)
        unique_block_hashes__remote = list(OrderedDict.fromkeys(list_of_block_hashes__remote)) 
        unique_node_ips = list(OrderedDict.fromkeys(list_of_ip_addresses)) 
        number_of_relevant_blocks_found_remotely = len(unique_block_hashes__remote)
        if number_of_relevant_blocks_found_remotely >= number_of_blocks_required:
            print('There appear to be enough blocks remotely to reconstruct the file! Attempting to do that now...')
            for remote_node_counter, current_remote_node_ip in enumerate(unique_node_ips):
                try:
                    print('Now attempting to download block files from Node at IP address: '+current_remote_node_ip)
                    retrieve_block_files_from_remote_node_using_file_hash_func(sha256_hash_of_desired_zipfile,current_remote_node_ip)
                except Exception as e:
                    print('Error: '+ str(e))  
                print('Checking if if we now have enough blocks locally to reconstruct file...')
                try:
                    completed_successfully = decode_block_files_into_art_zipfile_func(sha256_hash_of_desired_zipfile)
                except Exception as e:
                    completed_successfully = 0
                    print('Error: '+ str(e))  
                if completed_successfully:
                    break
            if completed_successfully:
                successful_reconstruction = verify_locally_reconstructed_zipfile_func(sha256_hash_of_desired_zipfile)
            else:
                try:#As a last resort, we will now attempt to query the DHT to find more nodes with blocks that we are interested in.
                    list_of_remote_block_hashes, remote_node_ip, remote_node_port = ask_dht_network_for_blocks_corresponding_to_hash_of_given_zipfile_func(sha256_hash_of_desired_zipfile)
                    time.sleep(10)
                    retrieve_block_files_from_remote_node_using_file_hash_func(sha256_hash_of_desired_zipfile,remote_node_ip)
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

#query_results_table = c.execute("""SELECT * FROM potential_global_hashes where file_hash = ?""",[sha256_hash_of_desired_zipfile]).fetchall()
#query_results_table = c.execute("""SELECT * FROM potential_global_hashes""").fetchall()

###############################################################################################################
# Script:
###############################################################################################################
my_node_ip_address = get_my_local_ip_func()
bootstrap_node_list = [(x,int(default_port)) for x in list_of_ips if x[0] != my_node_ip_address] +  [(x,int(alternative_port)) for x in list_of_ips if x[0] != my_node_ip_address]
handler = logging.StreamHandler()
formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
handler.setFormatter(formatter)
dht_log = logging.getLogger('kademlia')
dht_log.addHandler(handler)
dht_log.setLevel(logging.DEBUG)   
if use_generate_new_sqlite_chunk_database:
    regenerate_sqlite_chunk_database_func()
    
potential_local_block_hashes_list, potential_local_file_hashes_list = refresh_block_storage_folder_and_check_block_integrity_func()
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
conn.close()
_, my_node_ip, my_node_port = unpackage_dht_hash_table_entry_func(package_dht_hash_table_entry_func([]))
sys.exit(0)

try:
    subprocess.Popen(['python','run_animecoin_file_server.py'])
    #file_server_process = Process(target=start_file_server_func, args=())
    #file_server_process.start()
except Exception as e:
    print('Error: '+ str(e))

try:
 dht_process = Process(target=start_new_dht_network_func, args=(alternative_port,))
 dht_process.start()
except:
    print('Some kind of error, loop is probably running already!')

try:
    server, loop = connect_to_existing_dht_network(default_port)
    time.sleep(5)
except:
    print('Some kind of error, loop is probably running already!')

try:
    print('Bootstrapping network now!\n')
    loop.run_until_complete(server.bootstrap(bootstrap_node_list))
except:
    print('Error bootstrapping network! Probably already using socket, try closing and restarting system!')

advertise_local_blocks_to_dht_network_func()
    
if use_demonstrate_asking_dht_network_for_file_blocks:
    sys.exit(0)
    #test_file_hash = 'a75a1757d54ad0876f9b2cf9b64b1017b6293ceed61e80cd64aae5abfdc8514e' #Arturo_number_04
    test_file_hash = '405801baada7151188942c23f66768987c114ff5d950bc70bb2cfcdcbc5d4679'
    #test_file_hash = '105c4c9b9d79ed544c36df792f7bbbddb6700209f51e4b9e1073fa41653d2aa8' #Iris Madula Number 11
    sha256hashsize = sys.getsizeof(test_file_hash)
    print('Now asking network for hashes of blocks that correspond to the zipfile with a SHA256 hash of: '+test_file_hash)
    try:
        list_of_remote_block_hashes, remote_node_ip, remote_node_port = ask_dht_network_for_blocks_corresponding_to_hash_of_given_zipfile_func(test_file_hash)
        print('\n\nDone! Got back '+str(len(list_of_remote_block_hashes))+' Block Hashes:\n')
        print('\nNode IP:port is '+remote_node_ip+':'+str(remote_node_port))
        for current_block_hash in list_of_remote_block_hashes:
            print(current_block_hash+', ',end='')
    except:
        print('Problem requesting block files from DHT network!')

    sha256_hash_of_desired_zipfile = '405801baada7151188942c23f66768987c114ff5d950bc70bb2cfcdcbc5d4679'
    remote_node_ip = '140.82.14.38'#'108.61.86.243' #'140.82.2.58'
    sha256_hash_of_desired_block = '1e6864df358abd54b3ebcdb8da05c16149f208ef10aab4ac86e12cb1b0074ac9'
    #wrapper_reconstruct_zipfile_from_hash_func(sha256_hash_of_desired_zipfile)

    list_of_block_filenames, list_of_block_hashes, list_of_file_hashes = get_list_of_available_blocks_on_remote_node_file_server_func(remote_node_ip,type_of_file_to_retrieve='blocks')
    list_of_signature_filenames, list_of_file_hashes = get_list_of_available_blocks_on_remote_node_file_server_func(remote_node_ip,type_of_file_to_retrieve='signatures')
    list_of_zipfile_filenames, list_of_file_hashes = get_list_of_available_blocks_on_remote_node_file_server_func(remote_node_ip,type_of_file_to_retrieve='zipfiles')
    list_of_downloaded_files = download_remote_block_files_for_given_zipfile_hash_func(remote_node_ip,sha256_hash_of_desired_zipfile)
