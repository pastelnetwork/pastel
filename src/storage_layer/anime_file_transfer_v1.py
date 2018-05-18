import time, os.path, glob, hashlib, os, sqlite3, stat, warnings
warnings.simplefilter(action='ignore', category=FutureWarning)
warnings.filterwarnings('ignore',category=DeprecationWarning)
import requests
import lxml.html
from collections import OrderedDict
from itertools import compress
from anime_utility_functions_v1 import filter_list_with_boolean_vector_func, get_all_remote_node_ips_func, get_local_matching_blocks_from_zipfile_hash_func, download_remote_file_func, get_all_animecoin_parameters_func
from anime_fountain_coding_v1 import get_local_block_file_header_data_func, refresh_block_storage_folder_and_check_block_integrity_func, decode_block_files_into_art_zipfile_func
#Get various settings:
anime_metadata_format_version_number, minimum_total_number_of_unique_copies_of_artwork, maximum_total_number_of_unique_copies_of_artwork, target_number_of_nodes_per_unique_block_hash, target_block_redundancy_factor, desired_block_size_in_bytes, \
remote_node_chunkdb_refresh_time_in_minutes, remote_node_image_fingerprintdb_refresh_time_in_minutes, percentage_of_block_files_to_randomly_delete, percentage_of_block_files_to_randomly_corrupt, percentage_of_each_selected_file_to_be_randomly_corrupted, \
registration_fee_anime_per_megabyte_of_images_pre_difficulty_adjustment, example_list_of_valid_masternode_ip_addresses, forfeitable_deposit_to_initiate_registration_as_percentage_of_adjusted_registration_fee, nginx_ip_whitelist_override_addresses, \
example_animecoin_masternode_blockchain_address, example_trader_blockchain_address, example_artists_receiving_blockchain_address, rpc_connection_string, max_number_of_blocks_to_download_before_checking, earliest_possible_artwork_signing_date, \
maximum_length_in_characters_of_text_field, maximum_combined_image_size_in_megabytes_for_single_artwork, nsfw_score_threshold, duplicate_image_threshold = get_all_animecoin_parameters_func()
###############################################################################################################
# Functions:
###############################################################################################################

def get_list_of_available_files_on_remote_node_file_server_func(remote_node_ip, type_of_file_to_retrieve='blocks'):
    global chunk_db_file_path
    try:
        conn = sqlite3.connect(chunk_db_file_path)
        c = conn.cursor()
        r = requests.get('http://'+remote_node_ip+'/')
        html_file_listing = r.text
        parsed_file_listing = lxml.html.fromstring(html_file_listing)
        list_of_links = parsed_file_listing.xpath('//a/@href')
        list_of_block_filenames = []
        list_of_signature_filenames = []
        list_of_zipfile_filenames = []
        list_of_trade_ticket_filenames = []
        list_of_sqlite_filenames = []
        for link in list_of_links:
            if '.block' in link:
                list_of_block_filenames.append(link)
            if '.sig' in link:
                list_of_signature_filenames.append(link)
            if '.zip' in link:
                list_of_zipfile_filenames.append(link)
            if '.sqlite' in link:
                list_of_sqlite_filenames.append(link)
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
        if (len(list_of_sqlite_filenames) > 0) and (type_of_file_to_retrieve == 'sqlite_files'):
            return list_of_sqlite_filenames
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
    global max_number_of_blocks_to_download_before_checking
    download_count = 0
    list_of_downloaded_files = []
    list_of_node_ip_addresses = get_all_remote_node_ips_func()
    list_of_local_block_file_paths, _ , _ = get_local_matching_blocks_from_zipfile_hash_func(sha256_hash_of_desired_zipfile)
    list_of_local_block_file_names = [os.path.split(x)[-1] for x in list_of_local_block_file_paths]
    for current_remote_node_ip in list_of_node_ip_addresses:
        list_of_remote_block_filenames, list_of_remote_block_hashes, list_of_remote_file_hashes = get_list_of_available_files_on_remote_node_file_server_func(current_remote_node_ip, type_of_file_to_retrieve='blocks') 
        boolean_download_vector = [ (x==sha256_hash_of_desired_zipfile) for x in list_of_remote_file_hashes]
        filtered_list_of_remote_block_filenames = filter_list_with_boolean_vector_func(list_of_remote_block_filenames, boolean_download_vector)
        filtered_list_of_remote_block_hashes = filter_list_with_boolean_vector_func(list_of_remote_block_hashes, boolean_download_vector)
        filtered_list_of_remote_file_hashes = filter_list_with_boolean_vector_func(list_of_remote_file_hashes, boolean_download_vector)
        file_listing_url = 'http://'+current_remote_node_ip+'/'
        for cnt, current_block_file_name in enumerate(filtered_list_of_remote_block_filenames):
            if current_block_file_name not in list_of_local_block_file_names:
                if download_count <= max_number_of_blocks_to_download_before_checking:
                    url_of_file_to_download = file_listing_url+current_block_file_name
                    current_block_hash = filtered_list_of_remote_block_hashes[cnt]
                    current_file_hash = filtered_list_of_remote_file_hashes[cnt]
                    try:
                        print('Downloading new block file with hash '+ current_block_hash + ' for art zipfile with hash '+current_file_hash+' from node at ' + current_remote_node_ip)
                        path_to_downloaded_file = download_remote_file_func(url_of_file_to_download, block_storage_folder_path)
                        list_of_downloaded_files.append(path_to_downloaded_file)
                        download_count = download_count + 1
                        print('Done!')
                    except Exception as e:
                        print('Error: '+ str(e))
    return list_of_downloaded_files

def download_remote_block_files_for_given_zipfile_hash_func(remote_node_ip, sha256_hash_of_desired_zipfile):
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
    list_of_downloaded_files = []
    list_of_node_ip_addresses = get_all_remote_node_ips_func()
    list_of_existing_local_trade_ticket_file_paths = glob.glob(os.path.join(pending_trade_ticket_files_folder_path,'*.html'))
    list_of_existing_local_trade_ticket_file_names = [os.path.split(x)[-1] for x in list_of_existing_local_trade_ticket_file_paths]
    for current_remote_node_ip in list_of_node_ip_addresses:
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

def download_sqlite_files_from_remote_node(remote_node_ip, sqlite_file_type='chunkdb'): 
    global folder_path_of_remote_node_sqlite_files
    global remote_node_chunkdb_refresh_time_in_minutes
    global remote_node_image_fingerprintdb_refresh_time_in_minutes
    if sqlite_file_type == 'chunkdb':
        required_refresh_time_in_minutes = remote_node_chunkdb_refresh_time_in_minutes
    else:
        required_refresh_time_in_minutes = remote_node_image_fingerprintdb_refresh_time_in_minutes
    list_of_downloaded_files = []
    list_of_sqlite_filenames = get_list_of_available_files_on_remote_node_file_server_func(remote_node_ip, 'sqlite_files')
    file_listing_url = 'http://'+remote_node_ip
    output_filename_prefix = 'Remote_Node__'+remote_node_ip.replace('.','_')+'__'
    existing_file_path = glob.glob(folder_path_of_remote_node_sqlite_files+output_filename_prefix+'*'+sqlite_file_type+'*')
    minutes_since_file_last_updated = (time.time() - os.stat(existing_file_path[0])[stat.ST_MTIME])/60
    if minutes_since_file_last_updated >= required_refresh_time_in_minutes:    
        for current_sqlite_file_name in list_of_sqlite_filenames:
            if sqlite_file_type in current_sqlite_file_name:
                url_of_file_to_download = file_listing_url+current_sqlite_file_name[1:]
                try:
                    print('Downloading sqlite file '+ current_sqlite_file_name + ' from node at ' + remote_node_ip)
                    path_to_downloaded_file = download_remote_file_func(url_of_file_to_download, folder_path_of_remote_node_sqlite_files, output_filename_prefix)
                    list_of_downloaded_files.append(path_to_downloaded_file)
                    print('Done!')
                except Exception as e:
                    print('Error: '+ str(e))
  
def check_sqlitedb_for_remote_blocks_func(sha256_hash_of_desired_zipfile):
    global chunk_db_file_path
    conn = sqlite3.connect(chunk_db_file_path)
    c = conn.cursor()
    query_results_table = c.execute("""SELECT * FROM potential_global_hashes where file_hash = ? ORDER BY datetime_inserted""",[sha256_hash_of_desired_zipfile]).fetchall()
    list_of_block_hashes = [x[0] for x in query_results_table]
    list_of_file_hashes = [x[1] for x in query_results_table]
    list_of_ip_addresses = [x[2] for x in query_results_table]
    potential_local_block_hashes_list, potential_local_file_hashes_list = refresh_block_storage_folder_and_check_block_integrity_func()
    return list_of_block_hashes, list_of_file_hashes, list_of_ip_addresses

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
    
def count_number_of_nodes_reporting_having_block_hash_for_all_known_blocks_in_network_func(): #Todo: Get this to work
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

def run_self_healing_loop_func(): #Todo: Get this to work
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
             
def wrapper_reconstruct_zipfile_from_hash_func(sha256_hash_of_desired_zipfile): #Todo: Get this to work
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
