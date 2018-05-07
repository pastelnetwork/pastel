import sys, time, os.path, io, string, glob, hashlib, imghdr, random, os, zlib, pickle, sqlite3, uuid, socket, warnings, base64, json, hmac, logging, asyncio, binascii, subprocess, locale
warnings.simplefilter(action='ignore', category=FutureWarning)
warnings.filterwarnings('ignore',category=DeprecationWarning)
import requests
import lxml.html
import shortuuid
import rsa
from struct import pack, unpack
from multiprocessing import Process, Queue
from collections import OrderedDict
from datetime import datetime
from itertools import compress
from math import ceil
try:
    from tqdm import tqdm
except:
    pass
#from anime_storage import *
#Requirements: pip install tqdm, fs, lxml, requests, shortuuid, rsa

#Add to crontab:
#* * * * * 
# (first install tree: apt install tree)
    
###############################################################################################################
# Parameters:
###############################################################################################################
use_verify_integrity = 1
use_generate_new_sqlite_chunk_database = 1
use_demonstrate_nginx_file_transfers = 0
use_demonstrate_trade_ticket_generation = 1
use_demonstrate_trade_ticket_parsing = 1
root_animecoin_folder_path = 'C:\\animecoin\\'
folder_path_of_art_folders_to_encode = os.path.join(root_animecoin_folder_path,'art_folders_to_encode' + os.sep)#Each subfolder contains the various art files pertaining to a given art asset.
block_storage_folder_path = os.path.join(root_animecoin_folder_path,'art_block_storage' + os.sep)
folder_path_of_remote_node_sqlite_files = os.path.join(root_animecoin_folder_path,'remote_node_sqlite_files' + os.sep)
reconstructed_file_destination_folder_path = os.path.join(root_animecoin_folder_path,'reconstructed_files' + os.sep)
misc_masternode_files_folder_path = os.path.join(root_animecoin_folder_path,'misc_masternode_files' + os.sep) #Where we store some of the SQlite databases
masternode_keypair_db_file_path = os.path.join(misc_masternode_files_folder_path,'masternode_keypair_db.sqlite')
trade_ticket_files_folder_path = os.path.join(root_animecoin_folder_path,'trade_ticket_files' + os.sep) #Where we store the html trade tickets for the masternode DEX
completed_trade_ticket_files_folder_path = os.path.join(trade_ticket_files_folder_path,'completed' + os.sep) 
pending_trade_ticket_files_folder_path = os.path.join(trade_ticket_files_folder_path,'pending' + os.sep) 
prepared_final_art_zipfiles_folder_path = os.path.join(root_animecoin_folder_path,'prepared_final_art_zipfiles' + os.sep) 
chunk_db_file_path = os.path.join(misc_masternode_files_folder_path,'anime_chunkdb.sqlite')
file_storage_log_file_path = os.path.join(misc_masternode_files_folder_path,'anime_storage_log.txt')
nginx_allowed_ip_whitelist_file_path = os.path.join(misc_masternode_files_folder_path,'masternode_ip_whitelist.conf')
path_to_animecoin_trade_ticket_template = os.path.join(trade_ticket_files_folder_path,'animecoin_trade_ticket_template.html')
default_port = 14142
alternative_port = 2718
list_of_ips = ['108.61.86.243','140.82.14.38','140.82.2.58']
target_number_of_nodes_per_unique_block_hash = 10
target_block_redundancy_factor = 10 #How many times more blocks should we store than are required to regenerate the file?
desired_block_size_in_bytes = 1024*1000*2
example_list_of_valid_masternode_ip_addresses = ['207.246.93.232','149.28.41.105','149.28.34.59']
nginx_ip_whitelist_override_addresses = ['65.200.165.210'] #For debugging purposes
example_animecoin_masternode_blockchain_address = 'AdaAvFegJbBdH4fB8AiWw8Z4Ek75Xsja6i'
example_trader_blockchain_address = 'ANQTCifwMdh1Lsda9DsMcXuXBwtkyHRrZe'
locale.setlocale(locale.LC_ALL, '')
if 0: #NAT traversal experiments
    import stun
    nat_type, external_ip, external_port = stun.get_ip_info()
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

def generate_trade_id_func():
    shortuuid.set_alphabet('ANME0123456789')
    trade_id = shortuuid.uuid()[0:8]
    return trade_id

def generate_nginx_ip_whitelist_file_func(python_list_of_masternode_ip_addresses):
    global nginx_allowed_ip_whitelist_file_path
    global example_animecoin_masternode_blockchain_address
    try:
        combined_list_of_whitelisted_ips = python_list_of_masternode_ip_addresses + example_animecoin_masternode_blockchain_address
        with open(nginx_allowed_ip_whitelist_file_path,'w') as f:
            for current_ip_address in combined_list_of_whitelisted_ips:
                allow_string = 'allow ' +current_ip_address+';\n'
                f.write(allow_string)
    except Exception as e:
        print('Error: '+ str(e))    

def regenerate_sqlite_chunk_database_func():
    global chunk_db_file_path
    try:
        conn = sqlite3.connect(chunk_db_file_path)
        c = conn.cursor()
        local_hash_table_creation_string = """CREATE TABLE potential_local_hashes (block_hash text PRIMARY KEY, file_hash text);"""
        c.execute(local_hash_table_creation_string)
        global_hash_table_creation_string = """CREATE TABLE potential_global_hashes (block_hash text, file_hash text, remote_node_ip text, datetime_inserted TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, PRIMARY KEY (block_hash, remote_node_ip));"""
        c.execute(global_hash_table_creation_string)
        final_art_zipfile_table_creation_string = """CREATE TABLE final_art_zipfile_table (file_hash text, remote_node_ip text, datetime_inserted TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, PRIMARY KEY (file_hash, remote_node_ip));"""
        c.execute(final_art_zipfile_table_creation_string)
        final_art_signatures_table_creation_string = """CREATE TABLE final_art_signatures_table (file_hash text, remote_node_ip text, final_signature_base64_encoded text, datetime_inserted TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, PRIMARY KEY (file_hash, remote_node_ip));"""
        c.execute(final_art_signatures_table_creation_string)
        pending_trade_tickets_table_creation_string = """CREATE TABLE pending_trade_tickets_table (desired_art_asset_hash text, trade_ticket_details_sha256_hash text, submitting_trader_animecoin_identity_public_key text, submitting_traders_digital_signature_on_trade_ticket_details_hash text, submitting_trader_animecoin_blockchain_address text,  assigned_masternode_broker_ip_address text, assigned_masternode_broker_animecoin_identity_public_key text, assigned_masternode_broker_animecoin_blockchain_address text, assigned_masternode_broker_digital_signature_on_trade_ticket_details_hash text, desired_trade_type text, desired_quantity integer, specified_price_in_anime real, tradeid text, datetime_trade_submitted text, datetime_trade_executed text, datetime_inserted TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, PRIMARY KEY (trade_ticket_details_sha256_hash));"""
        c.execute(pending_trade_tickets_table_creation_string)
        completed_trade_tickets_table_creation_string = """CREATE TABLE completed_trade_tickets_table (desired_art_asset_hash text, trade_ticket_details_sha256_hash text, submitting_trader_animecoin_identity_public_key text, submitting_traders_digital_signature_on_trade_ticket_details_hash text, submitting_trader_animecoin_blockchain_address text,  assigned_masternode_broker_ip_address text, assigned_masternode_broker_animecoin_identity_public_key text, assigned_masternode_broker_animecoin_blockchain_address text, assigned_masternode_broker_digital_signature_on_trade_ticket_details_hash text, desired_trade_type text, desired_quantity integer, specified_price_in_anime real, tradeid text, datetime_trade_submitted text, datetime_trade_executed text, datetime_inserted TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, PRIMARY KEY (trade_ticket_details_sha256_hash));"""
        c.execute(completed_trade_tickets_table_creation_string)        
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

def get_list_of_available_files_on_remote_node_file_server_func(remote_node_ip, type_of_file_to_retrieve='blocks'):
    global chunk_db_file_path
    try:
        conn = sqlite3.connect(chunk_db_file_path)
        c = conn.cursor()
        r = requests.get('http://'+remote_node_ip+'/')
        html_file_listing = r.text
        parsed_file_listing = lxml.html.fromstring(html_file_listing)
        list_of_links = parsed_file_listing.xpath('//a/@href')
        list_of_links = list_of_links[1:]
        list_of_block_filenames = []
        list_of_signature_filenames = []
        list_of_zipfile_filenames = []
        list_of_trade_ticket_filenames = []
        for link in list_of_links:
            if '.block' in link:
                list_of_block_filenames.append(link)
            if '.sig' in link:
                list_of_signature_filenames.append(link)
            if '.zip' in link:
                list_of_zipfile_filenames.append(link)
            if 'TradeID' in link:
                list_of_trade_ticket_filenames.append(link)            
        list_of_block_hashes = []
        list_of_file_hashes = []
        list_of_trade_ids = []
        if (len(list_of_block_filenames) > 0) and (type_of_file_to_retrieve == 'blocks'):
            for current_block_file_name in list_of_block_filenames:
                reported_file_sha256_hash = current_block_file_name.split('__')[1]
                list_of_file_hashes.append(reported_file_sha256_hash)
                reported_block_file_sha256_hash = current_block_file_name.split('__')[-1].replace('.block','').replace('BlockHash_','')
                list_of_block_hashes.append(reported_block_file_sha256_hash)
            for hash_cnt, current_block_hash in enumerate(list_of_block_hashes):
                current_file_hash = list_of_file_hashes[hash_cnt]
                c.execute('INSERT OR IGNORE INTO potential_global_hashes (block_hash, file_hash, remote_node_ip) VALUES (?,?,?)',[current_block_hash, current_file_hash, remote_node_ip]) 
            conn.commit()
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
            for current_zipfile_hash in list_of_file_hashes:
                c.execute('INSERT OR IGNORE INTO final_art_zipfile_table (file_hash, remote_node_ip) VALUES (?,?)',[current_zipfile_hash, remote_node_ip])
            return list_of_zipfile_filenames, list_of_file_hashes        
        if (len(list_of_trade_ticket_filenames) > 0) and (type_of_file_to_retrieve == 'trade_tickets'):
            for current_trade_ticket_file_name in list_of_trade_ticket_filenames:
                reported_trade_id = current_trade_ticket_file_name.split('__')[2]
                list_of_trade_ids.append(reported_trade_id)
            return list_of_trade_ticket_filenames, list_of_trade_ids  
    except Exception as e:
        print('Error: '+ str(e))

def query_all_masternodes_for_relevant_block_files_and_download_them_func(sha256_hash_of_desired_zipfile):
    global block_storage_folder_path
    list_of_unique_node_ip_addresses = get_all_remote_node_ips_func()
    list_of_local_block_file_paths, _ , _ = get_local_matching_blocks_from_zipfile_hash_func(sha256_hash_of_desired_zipfile)
    list_of_local_block_file_names = [os.path.split(x)[-1] for x in list_of_local_block_file_paths]
    for current_remote_node_ip in list_of_unique_node_ip_addresses:
        list_of_remote_block_filenames, list_of_remote_block_hashes, list_of_remote_file_hashess = get_list_of_available_files_on_remote_node_file_server_func(current_remote_node_ip, type_of_file_to_retrieve='blocks') 
        file_listing_url = 'http://'+current_remote_node_ip+'/'
        for cnt, current_block_file_name in enumerate(list_of_remote_block_filenames):
            if current_block_file_name not in list_of_local_block_file_names:
                url_of_file_to_download = file_listing_url+current_block_file_name
                current_block_hash = list_of_remote_block_hashes[cnt]
                current_file_hash = list_of_remote_file_hashess[cnt]
                try:
                    print('Downloading new block file with hash '+ current_block_hash + ' for art zipfile with hash '+current_file_hash+' from node at ' + current_remote_node_ip)
                    path_to_downloaded_file = download_remote_file_func(url_of_file_to_download, block_storage_folder_path)
                    list_of_downloaded_files.append(path_to_downloaded_file)
                    print('Done!')
                except Exception as e:
                    print('Error: '+ str(e))
    return list_of_downloaded_files

def download_remote_file_func(url_of_file_to_download, folder_path_for_saved_file):
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
                    path_to_downloaded_file = download_remote_file_func(url_of_file_to_download, block_storage_folder_path)
                    list_of_downloaded_files.append(path_to_downloaded_file)
                    print('Done!')
                except Exception as e:
                    print('Error: '+ str(e))
        return list_of_downloaded_files
   except Exception as e:
       print('Error: '+ str(e))

def download_all_new_pending_trade_tickets_from_masternodes_func():
    global pending_trade_ticket_files_folder_path
    list_of_unique_node_ip_addresses = get_all_remote_node_ips_func()
    list_of_existing_local_trade_ticket_file_paths = glob.glob(os.path.join(pending_trade_ticket_files_folder_path,'*.html'))
    list_of_existing_local_trade_ticket_file_names = [os.path.split(x)[-1] for x in list_of_existing_local_trade_ticket_file_paths]
    for current_remote_node_ip in list_of_unique_node_ip_addresses:
        list_of_trade_ticket_filenames, list_of_trade_ids = get_list_of_available_files_on_remote_node_file_server_func(current_remote_node_ip, type_of_file_to_retrieve='trade_tickets') 
        file_listing_url = 'http://'+current_remote_node_ip+'/'
        for cnt, current_trade_ticket_file_name in enumerate(list_of_trade_ticket_filenames):
            if current_trade_ticket_file_name not in list_of_existing_local_trade_ticket_file_names:
                url_of_file_to_download = file_listing_url+current_trade_ticket_file_name
                current_trade_id = list_of_trade_ids[cnt]
                try:
                    print('Downloading new pending trade ticket for TradeID '+ current_trade_id + ' from node at ' + current_remote_node_ip)
                    path_to_downloaded_file = download_remote_file_func(url_of_file_to_download, pending_trade_ticket_files_folder_path)
                    list_of_downloaded_files.append(path_to_downloaded_file)
                    print('Done!')
                except Exception as e:
                    print('Error: '+ str(e))
    return list_of_downloaded_files
     
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

def check_if_finished_art_zipfile_is_stored_locally_func(sha256_hash_of_desired_zipfile):
    global prepared_final_art_zipfiles_folder_path
    zipfile_stored_locally = 0
    list_of_matching_zipfile_paths = glob.glob(os.path.join(prepared_final_art_zipfiles_folder_path,'*'+sha256_hash_of_desired_zipfile+'.zip'))
    if len(list_of_matching_zipfile_paths) > 0:
        zipfile_stored_locally = 1
    return zipfile_stored_locally
    
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
                    list_of_downloaded_files = download_remote_block_files_for_given_zipfile_hash_func(current_remote_node_ip, sha256_hash_of_desired_zipfile)
                    print('Successfully downloaded '+str(len(list_of_downloaded_files))+' block files!')
                except Exception as e:
                    print('Error: '+ str(e))  
                print('Checking if we now have enough blocks locally to reconstruct file...')
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
                try:#As a last resort, we will now query all masternodes to look for more block files:
                    list_of_downloaded_files = query_all_masternodes_for_relevant_block_files_and_download_them_func(sha256_hash_of_desired_zipfile)
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

def get_local_masternode_identification_keypair_func():
    global masternode_keypair_db_file_path
    if not os.path.exists(masternode_keypair_db_file_path):
        print('Error, could not find local masternode identification keypair file--try generating a new one!')
        return
    try:
        conn = sqlite3.connect(masternode_keypair_db_file_path)
        c = conn.cursor()
        masternode_identification_keypair_query_results = c.execute("""SELECT * FROM masternode_identification_keypair_table""").fetchall()
        masternode_public_key_export_format = masternode_identification_keypair_query_results[0][0]
        masternode_private_key_export_format = masternode_identification_keypair_query_results[0][1]
        masternode_public_key = rsa.PublicKey.load_pkcs1(masternode_public_key_export_format,format='PEM')
        masternode_private_key = rsa.PrivateKey.load_pkcs1(masternode_private_key_export_format,format='PEM')
        conn.close()
        print('\nGot local masternode identification keypair successfully!')
        return masternode_public_key, masternode_private_key
    except Exception as e:
        print('Error: '+ str(e))
        
def generate_trade_ticket_func(submitting_trader_animecoin_identity_public_key, submitting_trader_animecoin_blockchain_address, desired_trade_type, desired_art_asset_hash, desired_quantity, specified_price_in_anime, submitting_traders_digital_signature_on_trade_ticket_details_hash):
    global example_animecoin_masternode_blockchain_address
    global path_to_animecoin_trade_ticket_template
    global example_masternode_public_key
    global example_masternode_private_key
    global trade_ticket_files_folder_path
    global pending_trade_ticket_files_folder_path
    with open(path_to_animecoin_trade_ticket_template,'r') as f:
        trade_ticket_template_html_string = f.read()
    datetime_trade_submitted = datetime.now().strftime('%Y-%m-%d %H:%M:%S:%f')
    new_trade_id = generate_trade_id_func()
    desired_quantity_formatted = locale.format('%d', desired_quantity, grouping=True)
    specified_price_in_anime_formatted = locale.format('%f', specified_price_in_anime, grouping=True)
    submitting_trader_animecoin_identity_public_key_pem_format = submitting_trader_animecoin_identity_public_key.save_pkcs1(format='PEM').decode('utf-8')
    new_trade_ticket_html_string = trade_ticket_template_html_string
    new_trade_ticket_html_string = new_trade_ticket_html_string.replace('TRADE_ID_VAR', new_trade_id)
    new_trade_ticket_html_string = new_trade_ticket_html_string.replace('TRADE_DATETIME_VAR', datetime_trade_submitted)
    new_trade_ticket_html_string = new_trade_ticket_html_string.replace('TRADE_TRADER_PUBLIC_KEY_ID_VAR', submitting_trader_animecoin_identity_public_key_pem_format)
    new_trade_ticket_html_string = new_trade_ticket_html_string.replace('TRADE_TRADER_ANIME_ADDRESS_VAR', submitting_trader_animecoin_blockchain_address)
    new_trade_ticket_html_string = new_trade_ticket_html_string.replace('TRADE_TYPE_VAR', desired_trade_type)
    new_trade_ticket_html_string = new_trade_ticket_html_string.replace('TRADE_ART_ASSET_HASH_VAR', desired_art_asset_hash)
    new_trade_ticket_html_string = new_trade_ticket_html_string.replace('TRADE_QUANTITY_VAR', desired_quantity_formatted)
    new_trade_ticket_html_string = new_trade_ticket_html_string.replace('TRADE_PRICE_VAR', specified_price_in_anime_formatted)
    assigned_masternode_broker_ip_address = get_my_local_ip_func()
    assigned_masternode_broker_animecoin_identity_public_key, assigned_masternode_broker_animecoin_identity_private_key = get_local_masternode_identification_keypair_func()
    assigned_masternode_broker_animecoin_identity_public_key_pem_format = assigned_masternode_broker_animecoin_identity_public_key.save_pkcs1(format='PEM')
    assigned_masternode_broker_animecoin_blockchain_address = example_animecoin_masternode_blockchain_address
    trade_ticket_details_sha256_hash, assigned_masternode_broker_digital_signature_on_trade_ticket_details_hash_base64_encoded = sign_trade_ticket_hash_func(assigned_masternode_broker_animecoin_identity_private_key, assigned_masternode_broker_animecoin_identity_public_key_pem_format, submitting_trader_animecoin_blockchain_address, assigned_masternode_broker_ip_address, assigned_masternode_broker_animecoin_identity_public_key, desired_trade_type, desired_art_asset_hash, desired_quantity, specified_price_in_anime)#This would happen on the artist/trader's machine.
    new_trade_ticket_html_string = new_trade_ticket_html_string.replace('TRADE_TICKET_HASH_VAR', trade_ticket_details_sha256_hash)
    new_trade_ticket_html_string = new_trade_ticket_html_string.replace('TRADE_TRADER_SIGNATURE_VAR', submitting_traders_digital_signature_on_trade_ticket_details_hash)
    new_trade_ticket_html_string = new_trade_ticket_html_string.replace('TRADE_ASSIGNED_BROKER_IP_ADDRESS_VAR', assigned_masternode_broker_ip_address)
    new_trade_ticket_html_string = new_trade_ticket_html_string.replace('TRADE_ASSIGNED_BROKER_ANIME_ADDRESS_VAR', assigned_masternode_broker_animecoin_blockchain_address)
    new_trade_ticket_html_string = new_trade_ticket_html_string.replace('TRADE_ASSIGNED_BROKER_PUBLIC_KEY_ID_VAR', assigned_masternode_broker_animecoin_identity_public_key_pem_format.decode('utf-8'))
    new_trade_ticket_html_string = new_trade_ticket_html_string.replace('TRADE_ASSIGNED_BROKER_SIGNATURE_VAR', assigned_masternode_broker_digital_signature_on_trade_ticket_details_hash_base64_encoded)
    trade_ticket_output_file_path = os.path.join(pending_trade_ticket_files_folder_path,'Animecoin_Trade_Ticket__TradeID__'+new_trade_id+'__DateTime_Submitted__'+datetime_trade_submitted.replace(':','_').replace('-','_').replace(' ','_')+'.html')
    with open(trade_ticket_output_file_path,'w') as f:
        f.write(new_trade_ticket_html_string)
    print('Successfully generated trade ticket for TradeID '+new_trade_id)
    return trade_ticket_output_file_path

def sign_trade_ticket_hash_func(signer_private_key, submitting_trader_animecoin_identity_public_key, submitting_trader_animecoin_blockchain_address, assigned_masternode_broker_ip_address, assigned_masternode_broker_animecoin_identity_public_key, desired_trade_type, desired_art_asset_hash, desired_quantity, specified_price_in_anime):
    desired_quantity_formatted = locale.format('%d', desired_quantity, grouping=True)
    specified_price_in_anime_formatted = locale.format('%f', specified_price_in_anime, grouping=True)
    submitting_trader_animecoin_identity_public_key_pem_format = artist_public_key.save_pkcs1(format='PEM').decode('utf-8')
    assigned_masternode_broker_animecoin_identity_public_key_pem_format = assigned_masternode_broker_animecoin_identity_public_key.save_pkcs1(format='PEM').decode('utf-8')
    data_for_trade_ticket_hash = submitting_trader_animecoin_identity_public_key_pem_format + submitting_trader_animecoin_blockchain_address +  assigned_masternode_broker_ip_address + assigned_masternode_broker_animecoin_identity_public_key_pem_format + desired_trade_type + desired_art_asset_hash + desired_quantity_formatted + specified_price_in_anime_formatted
    data_for_trade_ticket_hash = data_for_trade_ticket_hash.encode('utf-8')
    trade_ticket_details_sha256_hash = hashlib.sha256(data_for_trade_ticket_hash).hexdigest()
    signed_trade_ticket_details_sha256_hash = rsa.sign(trade_ticket_details_sha256_hash.encode('utf-8'), signer_private_key,'SHA-256') #This would happen on the artist/trader's machine.
    signed_trade_ticket_details_sha256_hash_base64_encoded = base64.b64encode(signed_trade_ticket_details_sha256_hash).decode('utf-8')
    return trade_ticket_details_sha256_hash, signed_trade_ticket_details_sha256_hash_base64_encoded

def verify_trade_ticket_hash_signature_func(trade_ticket_details_sha256_hash, signed_trade_ticket_details_sha256_hash_base64_encoded, signer_public_key):
    verified = 0
    trade_ticket_details_sha256_hash_utf8_encoded = trade_ticket_details_sha256_hash.encode('utf-8')
    if isinstance(signed_trade_ticket_details_sha256_hash_base64_encoded,str):
        signed_trade_ticket_details_sha256_hash = base64.b64decode(signed_trade_ticket_details_sha256_hash_base64_encoded)
    else:
        signed_trade_ticket_details_sha256_hash = signed_trade_ticket_details_sha256_hash_base64_encoded
    try:
        rsa.verify(trade_ticket_details_sha256_hash_utf8_encoded, signed_trade_ticket_details_sha256_hash, signer_public_key)
        verified = 1
        return verified
    except Exception as e:
        print('Error: '+ str(e))
        return verified
    
def parse_trade_ticket_func(path_to_trade_ticket_html_file):
    with open(path_to_trade_ticket_html_file,'r') as f:
        trade_ticket_html_string = f.read()    
    trade_ticket_html_string_parsed = lxml.html.fromstring(trade_ticket_html_string)
    parsed_trade_data = trade_ticket_html_string_parsed.xpath('//td')
    parsed_trade_data_list = [x.text for x in parsed_trade_data]
    trade_id = parsed_trade_data_list[0]
    datetime_trade_submitted = parsed_trade_data_list[1]
    submitting_trader_animecoin_blockchain_address = parsed_trade_data_list[2]
    submitting_trader_animecoin_identity_public_key_pem_format = parsed_trade_data_list[3]
    submitting_trader_animecoin_identity_public_key = rsa.PublicKey.load_pkcs1(submitting_trader_animecoin_identity_public_key_pem_format,format='PEM')
    desired_trade_type = parsed_trade_data_list[4]
    desired_art_asset_hash = parsed_trade_data_list[5]
    desired_quantity = int(parsed_trade_data_list[6])
    specified_price_in_anime = locale.atof(parsed_trade_data_list[7])
    trade_ticket_details_sha256_hash = parsed_trade_data_list[8]
    submitting_traders_digital_signature_on_trade_ticket_details_hash_base64_encoded = parsed_trade_data_list[9]
    submitting_traders_digital_signature_on_trade_ticket_details_hash = base64.b64decode(submitting_traders_digital_signature_on_trade_ticket_details_hash_base64_encoded)
    assigned_masternode_broker_ip_address = parsed_trade_data_list[10]
    assigned_masternode_broker_animecoin_blockchain_address = parsed_trade_data_list[11]
    assigned_masternode_broker_animecoin_identity_public_key_pem_format = parsed_trade_data_list[12]
    assigned_masternode_broker_animecoin_identity_public_key = rsa.PublicKey.load_pkcs1(assigned_masternode_broker_animecoin_identity_public_key_pem_format,format='PEM')
    assigned_masternode_broker_digital_signature_on_trade_ticket_details_hash_base64_encoded = parsed_trade_data_list[13]
    assigned_masternode_broker_digital_signature_on_trade_ticket_details_hash = base64.b64decode(assigned_masternode_broker_digital_signature_on_trade_ticket_details_hash_base64_encoded)
    return trade_id, datetime_trade_submitted, submitting_trader_animecoin_blockchain_address, submitting_trader_animecoin_identity_public_key, desired_trade_type, desired_art_asset_hash, desired_quantity, specified_price_in_anime, trade_ticket_details_sha256_hash, submitting_traders_digital_signature_on_trade_ticket_details_hash, assigned_masternode_broker_ip_address, assigned_masternode_broker_animecoin_blockchain_address, assigned_masternode_broker_animecoin_identity_public_key, assigned_masternode_broker_digital_signature_on_trade_ticket_details_hash

def verify_signatures_on_trade_ticket_func(path_to_trade_ticket_html_file):
    all_verified = 0
    trade_id, datetime_trade_submitted, submitting_trader_animecoin_blockchain_address, submitting_trader_animecoin_identity_public_key, desired_trade_type, desired_art_asset_hash, desired_quantity, specified_price_in_anime, trade_ticket_details_sha256_hash, submitting_traders_digital_signature_on_trade_ticket_details_hash, assigned_masternode_broker_ip_address, assigned_masternode_broker_animecoin_blockchain_address, assigned_masternode_broker_animecoin_identity_public_key, assigned_masternode_broker_digital_signature_on_trade_ticket_details_hash = parse_trade_ticket_func(path_to_trade_ticket_html_file)
    desired_quantity_formatted = locale.format('%d', desired_quantity, grouping=True)
    specified_price_in_anime_formatted = locale.format('%f', specified_price_in_anime, grouping=True)
    submitting_trader_animecoin_identity_public_key_pem_format = submitting_trader_animecoin_identity_public_key.save_pkcs1(format='PEM').decode('utf-8')    
    assigned_masternode_broker_animecoin_identity_public_key_pem_format = assigned_masternode_broker_animecoin_identity_public_key.save_pkcs1(format='PEM').decode('utf-8')
    data_for_trade_ticket_hash = submitting_trader_animecoin_identity_public_key_pem_format + submitting_trader_animecoin_blockchain_address +  assigned_masternode_broker_ip_address + assigned_masternode_broker_animecoin_identity_public_key_pem_format + desired_trade_type + desired_art_asset_hash + desired_quantity_formatted + specified_price_in_anime_formatted
    data_for_trade_ticket_hash = data_for_trade_ticket_hash.encode('utf-8')
    computed_trade_ticket_details_sha256_hash = hashlib.sha256(data_for_trade_ticket_hash).hexdigest()
    if computed_trade_ticket_details_sha256_hash == trade_ticket_details_sha256_hash:
        print('Computed trade ticket hash matches the reported hash in the trade ticket! Now checking that digital signatures are valid...')
        submitting_trader_signatured_was_verified = verify_trade_ticket_hash_signature_func(trade_ticket_details_sha256_hash, submitting_traders_digital_signature_on_trade_ticket_details_hash, submitting_trader_animecoin_identity_public_key)
        if submitting_trader_signatured_was_verified:
            print('Submitting trader\'s digital signature on trade ticket hash was successfully verified as being authentic!' )
        else:
            print('Error! Submitting trader\'s digital signature on trade ticket was NOT valid!')
        assigned_masternode_broker_signatured_was_verified = verify_trade_ticket_hash_signature_func(trade_ticket_details_sha256_hash, assigned_masternode_broker_digital_signature_on_trade_ticket_details_hash, assigned_masternode_broker_animecoin_identity_public_key)
        if assigned_masternode_broker_signatured_was_verified:
            print('Assigned masternode\'s digital signature on trade ticket hash was successfully verified as being authentic!' )
        else:
            print('Error!Assigned masternode\'s digital signature on trade ticket was NOT valid!')
        all_verified = (computed_trade_ticket_details_sha256_hash == trade_ticket_details_sha256_hash) and submitting_trader_signatured_was_verified and assigned_masternode_broker_signatured_was_verified
        if all_verified:
            print('Everything was verified successfully! Trade ticket is valid!')
            return all_verified
        else:
            print('There was a problem verifying the trade ticket!')
            return all_verified
    else:
        print('Error! Computed trade ticket hash does NOT match the reported hash in the trade ticket! Ticket has been corrupted and is invalid!')
        return all_verified
    
def write_pending_trade_ticket_to_chunkdb_func(path_to_trade_ticket_html_file):
    global chunk_db_file_path
    imported_trade_ticket_successfully = 0
    trade_id, datetime_trade_submitted, submitting_trader_animecoin_blockchain_address, submitting_trader_animecoin_identity_public_key, desired_trade_type, desired_art_asset_hash, desired_quantity, specified_price_in_anime, trade_ticket_details_sha256_hash, submitting_traders_digital_signature_on_trade_ticket_details_hash, assigned_masternode_broker_ip_address, assigned_masternode_broker_animecoin_blockchain_address, assigned_masternode_broker_animecoin_identity_public_key, assigned_masternode_broker_digital_signature_on_trade_ticket_details_hash = parse_trade_ticket_func(path_to_trade_ticket_html_file)
    try:
        conn = sqlite3.connect(chunk_db_file_path)
        c = conn.cursor()
        update_table_data_query_string = """
        INSERT OR REPLACE INTO pending_trade_tickets_table (
        desired_art_asset_hash,
        trade_ticket_details_sha256_hash, 
        submitting_trader_animecoin_identity_public_key,
        submitting_traders_digital_signature_on_trade_ticket_details_hash, 
        submitting_trader_animecoin_blockchain_address,
        assigned_masternode_broker_ip_address,
        assigned_masternode_broker_animecoin_identity_public_key,
        assigned_masternode_broker_animecoin_blockchain_address,
        assigned_masternode_broker_digital_signature_on_trade_ticket_details_hash,
        desired_trade_type,
        desired_quantity,
        specified_price_in_anime,
        tradeid,
        datetime_trade_submitted,
        datetime_trade_executed) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);"""
        submitting_trader_animecoin_identity_public_key_pem_format = submitting_trader_animecoin_identity_public_key.save_pkcs1(format='PEM').decode('utf-8')   
        submitting_traders_digital_signature_on_trade_ticket_details_hash_base64_encoded = base64.b64encode(submitting_traders_digital_signature_on_trade_ticket_details_hash).decode('utf-8')
        assigned_masternode_broker_animecoin_identity_public_key_pem_format = assigned_masternode_broker_animecoin_identity_public_key.save_pkcs1(format='PEM').decode('utf-8')   
        assigned_masternode_broker_digital_signature_on_trade_ticket_details_hash_base64_encoded = base64.b64encode(assigned_masternode_broker_digital_signature_on_trade_ticket_details_hash).decode('utf-8')
        c.execute(update_table_data_query_string, [desired_art_asset_hash,
                                                  trade_ticket_details_sha256_hash, 
                                                  submitting_trader_animecoin_identity_public_key_pem_format,
                                                  submitting_traders_digital_signature_on_trade_ticket_details_hash_base64_encoded,
                                                  submitting_trader_animecoin_blockchain_address,
                                                  assigned_masternode_broker_ip_address,
                                                  assigned_masternode_broker_animecoin_identity_public_key_pem_format,
                                                  assigned_masternode_broker_animecoin_blockchain_address,
                                                  assigned_masternode_broker_digital_signature_on_trade_ticket_details_hash_base64_encoded,
                                                  desired_trade_type,
                                                  desired_quantity,
                                                  specified_price_in_anime,
                                                  trade_id,
                                                  datetime_trade_submitted])
        conn.commit()
        conn.close()
        print('Successfully imported trade ticket file to SQlite database!')
        imported_trade_ticket_successfully = 1
        return imported_trade_ticket_successfully
    except Exception as e:
        print('Error: '+ str(e))
        return imported_trade_ticket_successfully
    
    
###############################################################################################################
# Script:
###############################################################################################################
my_node_ip_address = get_my_local_ip_func()
list_of_required_folder_paths = [root_animecoin_folder_path,folder_path_of_remote_node_sqlite_files,misc_masternode_files_folder_path,folder_path_of_art_folders_to_encode,block_storage_folder_path,trade_ticket_files_folder_path, completed_trade_ticket_files_folder_path, pending_trade_ticket_files_folder_path]
for current_required_folder_path in list_of_required_folder_paths:
    if not os.path.exists(current_required_folder_path):
        try:
            os.makedirs(current_required_folder_path)
        except Exception as e:
                print('Error: '+ str(e)) 
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
sys.exit(0)

if use_demonstrate_nginx_file_transfers:
    list_of_block_filenames, list_of_block_hashes, list_of_file_hashes = get_list_of_available_files_on_remote_node_file_server_func(remote_node_ip, type_of_file_to_retrieve='blocks')
    list_of_signature_filenames, list_of_file_hashes = get_list_of_available_files_on_remote_node_file_server_func(remote_node_ip, type_of_file_to_retrieve='signatures')
    list_of_zipfile_filenames, list_of_file_hashes = get_list_of_available_files_on_remote_node_file_server_func(remote_node_ip, type_of_file_to_retrieve='zipfiles')
    list_of_downloaded_files = download_remote_block_files_for_given_zipfile_hash_func(remote_node_ip, sha256_hash_of_desired_zipfile)

if use_demonstrate_trade_ticket_generation:
    sys.exit(0)
    try:
        artist_public_key = pickle.load(open(os.path.join(root_animecoin_folder_path,'artist_public_key.p'),'rb'))
        artist_private_key = pickle.load(open(os.path.join(root_animecoin_folder_path,'artist_private_key.p'),'rb'))
    except:
        print('Couldn\'t load the demonstration artist pub/priv keys!')
    submitting_trader_animecoin_blockchain_address = example_trader_blockchain_address
    desired_trade_type = 'BUY'
    desired_art_asset_hash = '570f4b835404a83e9870b9e98a0e2ca3b58b43c775cf1275a6690ab618f0e73c'
    desired_quantity = 1
    specified_price_in_anime = 75000
    submitting_trader_animecoin_identity_public_key = artist_public_key
    submitting_trader_animecoin_identity_private_key = artist_private_key
    assigned_masternode_broker_ip_address = get_my_local_ip_func()
    assigned_masternode_broker_animecoin_identity_public_key, assigned_masternode_broker_animecoin_identity_private_key = get_local_masternode_identification_keypair_func()

    trade_ticket_details_sha256_hash, submitting_traders_digital_signature_on_trade_ticket_details_hash = sign_trade_ticket_hash_func(submitting_trader_animecoin_identity_private_key, submitting_trader_animecoin_identity_public_key, submitting_trader_animecoin_blockchain_address,  assigned_masternode_broker_ip_address, assigned_masternode_broker_animecoin_identity_public_key, desired_trade_type, desired_art_asset_hash, desired_quantity, specified_price_in_anime)#This would happen on the artist/trader's machine.
    
    trade_ticket_signature_is_valid = verify_trade_ticket_hash_signature_func(trade_ticket_details_sha256_hash, submitting_traders_digital_signature_on_trade_ticket_details_hash, submitting_trader_animecoin_identity_public_key)

    trade_ticket_output_file_path = generate_trade_ticket_func(submitting_trader_animecoin_identity_public_key,
                                                               submitting_trader_animecoin_blockchain_address,
                                                               desired_trade_type,
                                                               desired_art_asset_hash,
                                                               desired_quantity,
                                                               specified_price_in_anime,
                                                               submitting_traders_digital_signature_on_trade_ticket_details_hash)

if use_demonstrate_trade_ticket_parsing:
    list_of_pending_trade_ticket_filepaths = glob.glob(os.path.join(pending_trade_ticket_files_folder_path,'*.html'))
    path_to_trade_ticket_html_file = list_of_pending_trade_ticket_filepaths[0]
    imported_trade_ticket_successfully = write_pending_trade_ticket_to_chunkdb_func(path_to_trade_ticket_html_file)
    trade_id, datetime_trade_submitted, submitting_trader_animecoin_blockchain_address, submitting_trader_animecoin_identity_public_key, desired_trade_type, desired_art_asset_hash, desired_quantity, specified_price_in_anime, trade_ticket_details_sha256_hash, submitting_traders_digital_signature_on_trade_ticket_details_hash, assigned_masternode_broker_ip_address, assigned_masternode_broker_animecoin_blockchain_address, assigned_masternode_broker_animecoin_identity_public_key, assigned_masternode_broker_digital_signature_on_trade_ticket_details_hash = parse_trade_ticket_func(path_to_trade_ticket_html_file)
    all_verified = verify_signatures_on_trade_ticket_func(path_to_trade_ticket_html_file)
