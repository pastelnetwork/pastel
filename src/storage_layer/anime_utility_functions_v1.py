import os.path, glob, os, sqlite3, warnings, json, io, imghdr, socket, base64, pickle, platform, hashlib, shutil, re, subprocess
warnings.simplefilter(action='ignore', category=FutureWarning)
warnings.filterwarnings('ignore',category=DeprecationWarning)
import requests
import rsa
import numpy as np
from datetime import datetime
from bitcoinrpc.authproxy import AuthServiceProxy, JSONRPCException
#Requirements: pip install numpy, requests, rsa, bitcoinrpc
###############################################################################################################
# Functions:
###############################################################################################################

def get_animecoin_root_directory_func():
    current_platform = platform.platform()
    if 'Windows' in current_platform:
        root_animecoin_folder_path = 'C:\\animecoin\\'
    else:
        root_animecoin_folder_path = '/home/animecoinuser/animecoin/'
    return root_animecoin_folder_path

def get_all_animecoin_directories_func():
    root_animecoin_folder_path = get_animecoin_root_directory_func()
    folder_path_of_art_folders_to_encode = os.path.join(root_animecoin_folder_path,'art_folders_to_encode' + os.sep)#Each subfolder contains the various art files pertaining to a given art asset.
    block_storage_folder_path = os.path.join(root_animecoin_folder_path,'art_block_storage' + os.sep)
    folder_path_of_remote_node_sqlite_files = os.path.join(root_animecoin_folder_path,'remote_node_sqlite_files' + os.sep)
    reconstructed_file_destination_folder_path = os.path.join(root_animecoin_folder_path,'reconstructed_files' + os.sep)
    misc_masternode_files_folder_path = os.path.join(root_animecoin_folder_path,'misc_masternode_files' + os.sep) #Where we store some of the SQlite databases
    masternode_keypair_db_file_path = os.path.join(misc_masternode_files_folder_path,'masternode_keypair_db.secret')
    trade_ticket_files_folder_path = os.path.join(root_animecoin_folder_path,'trade_ticket_files' + os.sep) #Where we store the html trade tickets for the masternode DEX
    completed_trade_ticket_files_folder_path = os.path.join(trade_ticket_files_folder_path,'completed' + os.sep) 
    pending_trade_ticket_files_folder_path = os.path.join(trade_ticket_files_folder_path,'pending' + os.sep) 
    prepared_final_art_zipfiles_folder_path = os.path.join(root_animecoin_folder_path,'prepared_final_art_zipfiles' + os.sep) 
    chunk_db_file_path = os.path.join(misc_masternode_files_folder_path,'anime_chunkdb.sqlite')
    file_storage_log_file_path = os.path.join(misc_masternode_files_folder_path,'anime_storage_log.txt')
    nginx_allowed_ip_whitelist_file_path = '/var/www/masternode_file_server/html/masternode_ip_whitelist.conf'
    path_to_animecoin_trade_ticket_template = os.path.join(trade_ticket_files_folder_path,'animecoin_trade_ticket_template.html')
    artist_final_signature_files_folder_path = os.path.join(root_animecoin_folder_path,'art_signature_files' + os.sep)
    dupe_detection_image_fingerprint_database_file_path = os.path.join(misc_masternode_files_folder_path,'dupe_detection_image_fingerprint_database.sqlite')
    path_to_animecoin_artwork_metadata_template = os.path.join(misc_masternode_files_folder_path,'animecoin_metadata_template.html')
    return root_animecoin_folder_path, folder_path_of_art_folders_to_encode, block_storage_folder_path, folder_path_of_remote_node_sqlite_files, reconstructed_file_destination_folder_path, \
            misc_masternode_files_folder_path, masternode_keypair_db_file_path, trade_ticket_files_folder_path, completed_trade_ticket_files_folder_path, pending_trade_ticket_files_folder_path, \
            prepared_final_art_zipfiles_folder_path, chunk_db_file_path, file_storage_log_file_path, nginx_allowed_ip_whitelist_file_path, path_to_animecoin_trade_ticket_template, \
            artist_final_signature_files_folder_path, dupe_detection_image_fingerprint_database_file_path, path_to_animecoin_artwork_metadata_template

def get_all_animecoin_parameters_func():
    anime_metadata_format_version_number = '1.00'
    minimum_total_number_of_unique_copies_of_artwork = 5
    maximum_total_number_of_unique_copies_of_artwork = 10000
    target_number_of_nodes_per_unique_block_hash = 10
    target_block_redundancy_factor = 10 #How many times more blocks should we store than are required to regenerate the file?
    desired_block_size_in_bytes = 1024*1000*2
    remote_node_chunkdb_refresh_time_in_minutes = 5
    remote_node_image_fingerprintdb_refresh_time_in_minutes = 12*60
    percentage_of_block_files_to_randomly_delete = 0.55
    percentage_of_block_files_to_randomly_corrupt = 0.05
    percentage_of_each_selected_file_to_be_randomly_corrupted = 0.01
    registration_fee_anime_per_megabyte_of_images_pre_difficulty_adjustment = 12
    forfeitable_deposit_to_initiate_registration_as_percentage_of_adjusted_registration_fee = 0.1
    example_list_of_valid_masternode_ip_addresses = ['207.246.93.232','149.28.41.105','149.28.34.59','173.52.208.74']
    nginx_ip_whitelist_override_addresses = ['65.200.165.210'] #For debugging purposes
    example_animecoin_masternode_blockchain_address = 'AdaAvFegJbBdH4fB8AiWw8Z4Ek75Xsja6i'
    example_trader_blockchain_address = 'ANQTCifwMdh1Lsda9DsMcXuXBwtkyHRrZe'
    example_artists_receiving_blockchain_address = 'AfyXTrtHaimN6YDFCnWqMssRU3QPJjhT77'
    rpc_user = 'test'
    rpc_password = 'testpw'
    rpc_port = '19932'
    rpc_connection_string = 'http://'+rpc_user+':'+rpc_password+'@127.0.0.1:'+rpc_port
    max_number_of_blocks_to_download_before_checking = 10
    earliest_possible_artwork_signing_date = datetime(2018, 5, 15)
    maximum_length_in_characters_of_text_field = 5000
    maximum_combined_image_size_in_megabytes_for_single_artwork = 1000
    return anime_metadata_format_version_number, minimum_total_number_of_unique_copies_of_artwork, maximum_total_number_of_unique_copies_of_artwork, target_number_of_nodes_per_unique_block_hash, target_block_redundancy_factor, desired_block_size_in_bytes, \
            remote_node_chunkdb_refresh_time_in_minutes, remote_node_image_fingerprintdb_refresh_time_in_minutes, percentage_of_block_files_to_randomly_delete, percentage_of_block_files_to_randomly_corrupt, percentage_of_each_selected_file_to_be_randomly_corrupted, \
            registration_fee_anime_per_megabyte_of_images_pre_difficulty_adjustment, example_list_of_valid_masternode_ip_addresses, forfeitable_deposit_to_initiate_registration_as_percentage_of_adjusted_registration_fee, nginx_ip_whitelist_override_addresses, \
            example_animecoin_masternode_blockchain_address, example_trader_blockchain_address, example_artists_receiving_blockchain_address, rpc_connection_string, \
            max_number_of_blocks_to_download_before_checking, earliest_possible_artwork_signing_date, maximum_length_in_characters_of_text_field, maximum_combined_image_size_in_megabytes_for_single_artwork

#Get various settings:
anime_metadata_format_version_number, minimum_total_number_of_unique_copies_of_artwork, maximum_total_number_of_unique_copies_of_artwork, target_number_of_nodes_per_unique_block_hash, target_block_redundancy_factor, desired_block_size_in_bytes, \
remote_node_chunkdb_refresh_time_in_minutes, remote_node_image_fingerprintdb_refresh_time_in_minutes, percentage_of_block_files_to_randomly_delete, percentage_of_block_files_to_randomly_corrupt, percentage_of_each_selected_file_to_be_randomly_corrupted, \
registration_fee_anime_per_megabyte_of_images_pre_difficulty_adjustment, example_list_of_valid_masternode_ip_addresses, forfeitable_deposit_to_initiate_registration_as_percentage_of_adjusted_registration_fee, nginx_ip_whitelist_override_addresses, \
example_animecoin_masternode_blockchain_address, example_trader_blockchain_address, example_artists_receiving_blockchain_address, rpc_connection_string, max_number_of_blocks_to_download_before_checking, \
earliest_possible_artwork_signing_date, maximum_length_in_characters_of_text_field, maximum_combined_image_size_in_megabytes_for_single_artwork = get_all_animecoin_parameters_func()
#Get various directories:
root_animecoin_folder_path, folder_path_of_art_folders_to_encode, block_storage_folder_path, folder_path_of_remote_node_sqlite_files, reconstructed_file_destination_folder_path, \
misc_masternode_files_folder_path, masternode_keypair_db_file_path, trade_ticket_files_folder_path, completed_trade_ticket_files_folder_path, pending_trade_ticket_files_folder_path, \
prepared_final_art_zipfiles_folder_path, chunk_db_file_path, file_storage_log_file_path, nginx_allowed_ip_whitelist_file_path, path_to_animecoin_trade_ticket_template, \
artist_final_signature_files_folder_path, dupe_detection_image_fingerprint_database_file_path, path_to_animecoin_artwork_metadata_template = get_all_animecoin_directories_func()

def get_various_directories_for_testing_func():
    root_animecoin_folder_path, folder_path_of_art_folders_to_encode, block_storage_folder_path, folder_path_of_remote_node_sqlite_files, reconstructed_file_destination_folder_path, \
    misc_masternode_files_folder_path, masternode_keypair_db_file_path, trade_ticket_files_folder_path, completed_trade_ticket_files_folder_path, pending_trade_ticket_files_folder_path, \
    prepared_final_art_zipfiles_folder_path, chunk_db_file_path, file_storage_log_file_path, nginx_allowed_ip_whitelist_file_path, path_to_animecoin_trade_ticket_template, \
    artist_final_signature_files_folder_path, dupe_detection_image_fingerprint_database_file_path, path_to_animecoin_artwork_metadata_template= get_all_animecoin_directories_func()
    path_to_art_folder = os.path.join(folder_path_of_art_folders_to_encode,'Arturo_Lopez__Number_02' + os.sep)
    path_to_art_image_file = os.path.join(path_to_art_folder,'Arturo_Lopez__Number_02.png')
    path_to_another_similar_art_image_file = os.path.join(path_to_art_folder,'Arturo_Lopez__Number_02__With_Background.png')
    path_to_another_different_art_image_file = 'C:\\animecoin\\art_folders_to_encode\\Arturo_Lopez__Number_04\\Arturo_Lopez__Number_04.png'
    #path_to_artwork_metadata_file = os.path.join(path_to_art_folder,'artwork_metadata_file.db')
    path_to_all_registered_works_for_dupe_detection = 'C:\\Users\\jeffr\Cointel Dropbox\\Jeffrey Emanuel\\Animecoin_All_Finished_Works\\'
    sha256_hash_of_art_image_file = get_image_hash_from_image_file_path_func(path_to_art_image_file)
    return path_to_art_folder, path_to_art_image_file, path_to_another_similar_art_image_file, path_to_another_different_art_image_file, path_to_all_registered_works_for_dupe_detection, sha256_hash_of_art_image_file

def generate_rsa_public_and_private_keys_func():
    (public_key, private_key) = rsa.newkeys(512)
    return public_key, private_key

def get_pem_format_of_rsa_key_func(public_key, private_key):
    public_key_pem_format = rsa.PublicKey.save_pkcs1(public_key,format='PEM').decode('utf-8')
    private_key_pem_format = rsa.PrivateKey.save_pkcs1(private_key,format='PEM').decode('utf-8')
    return public_key_pem_format, private_key_pem_format

def generate_and_save_example_rsa_keypair_files_func():
    root_animecoin_folder_path = get_animecoin_root_directory_func()
    example_public_key_1, example_private_key_1 = generate_rsa_public_and_private_keys_func()
    example_public_key_2, example_private_key_2 = generate_rsa_public_and_private_keys_func()
    example_public_key_3, example_private_key_3 = generate_rsa_public_and_private_keys_func()
    output_file_path = os.path.join(root_animecoin_folder_path,'example_rsa_keypairs_pickle_file.p')
    try:
        with open(output_file_path,'wb') as f:
            pickle.dump([example_public_key_1, example_private_key_1, example_public_key_2, example_private_key_2, example_public_key_3, example_private_key_3], f)
    except Exception as e:
        print('Error: '+ str(e))

def get_example_keypair_of_rsa_public_and_private_keys_func():
    root_animecoin_folder_path = get_animecoin_root_directory_func()
    example_rsa_keypairs_pickle_file_path = os.path.join(root_animecoin_folder_path,'example_rsa_keypairs_pickle_file.p')
    [example_public_key_1, example_private_key_1, example_public_key_2, example_private_key_2, example_public_key_3, example_private_key_3] = pickle.load(open(example_rsa_keypairs_pickle_file_path,'rb'))
    return example_public_key_1, example_private_key_1, example_public_key_2, example_private_key_2, example_public_key_3, example_private_key_3

def get_example_keypair_of_rsa_public_and_private_keys_pem_format_func():
    root_animecoin_folder_path = get_animecoin_root_directory_func()
    example_rsa_keypairs_pickle_file_path = os.path.join(root_animecoin_folder_path,'example_rsa_keypairs_pickle_file.p')
    [example_public_key_1, example_private_key_1, example_public_key_2, example_private_key_2, example_public_key_3, example_private_key_3] = pickle.load(open(example_rsa_keypairs_pickle_file_path,'rb'))
    example_public_key_1_pem_format, example_private_key_1_pem_format = get_pem_format_of_rsa_key_func(example_public_key_1, example_private_key_1)
    example_public_key_2_pem_format, example_private_key_2_pem_format = get_pem_format_of_rsa_key_func(example_public_key_2, example_private_key_2)
    example_public_key_3_pem_format, example_private_key_3_pem_format = get_pem_format_of_rsa_key_func(example_public_key_3, example_private_key_3)
    return example_public_key_1_pem_format, example_private_key_1_pem_format, example_public_key_2_pem_format, example_private_key_2_pem_format, example_public_key_3_pem_format, example_private_key_3_pem_format

def sign_data_with_private_key_func(sha256_hash_of_data, private_key):
    sha256_hash_of_data_utf8_encoded = sha256_hash_of_data.encode('utf-8')
    signature_on_data = rsa.sign(sha256_hash_of_data_utf8_encoded, private_key, 'SHA-256')
    signature_on_data_base64_encoded = base64.b64encode(signature_on_data).decode('utf-8')
    return signature_on_data_base64_encoded

def verify_signature_on_data_func(sha256_hash_of_data, public_key, signature_on_data):
    verified = 0
    sha256_hash_of_data_utf8_encoded = sha256_hash_of_data.encode('utf-8')
    if isinstance(signature_on_data,str): #This way we can use one function for base64 encoded signatures and for bytes like objects
        signature_on_data = base64.b64decode(signature_on_data)
    try:
        rsa.verify(sha256_hash_of_data_utf8_encoded, signature_on_data, public_key)
        verified = 1
        return verified
    except Exception as e:
        print('Error: '+ str(e))
        return verified

def validate_list_of_text_fields_for_html_metadata_table_func(list_of_input_fields):
    global maximum_length_in_characters_of_text_field
    list_of_valid_text_fields = []
    list_of_invalid_text_fields = []
    for current_input_field_text in list_of_input_fields:
        if isinstance(current_input_field_text, str):
            field_text_is_too_long = len(current_input_field_text) > maximum_length_in_characters_of_text_field
            if field_text_is_too_long:
                list_of_invalid_text_fields.append(current_input_field_text)
            else:
                list_of_valid_text_fields.append(current_input_field_text)
        else:
            print('Error! Field is not a valid string!')
            list_of_invalid_text_fields.append(current_input_field_text)
    return list_of_invalid_text_fields, list_of_valid_text_fields

def get_current_datetime_string_func():
    current_datetime_string = datetime.now().strftime('%Y-%m-%d %H:%M:%S:%f')
    return current_datetime_string
    
def parse_datetime_string_func(datetime_string):
    current_datetime = datetime.strptime(datetime_string,'%Y-%m-%d %H:%M:%S:%f')
    return current_datetime

def get_current_anime_mining_difficulty_func(): #Todo: replace this with a real function that uses the RPC interface
    current_anime_mining_difficulty_rate = 350
    return current_anime_mining_difficulty_rate

def get_avg_anime_mining_difficulty_rate_first_20k_blocks_func(): #Todo: replace this with a real function that uses the RPC interface
    avg_anime_mining_difficulty_rate_first_20k_blocks = 100
    return avg_anime_mining_difficulty_rate_first_20k_blocks

def concatenate_list_of_strings_func(list_of_strings):
    concatenated_string = ''
    for current_string in list_of_strings:
        concatenated_string = concatenated_string + current_string + os.linesep
    return concatenated_string

def check_if_anime_blockchain_address_is_valid_func(potential_anime_address_string): #Todo: make this a real function using the RPC interface
    length_of_address = len(potential_anime_address_string)
    if length_of_address == 34:
        address_is_valid = 1
    else:
        address_is_valid = 0
    return address_is_valid

def validate_list_of_anime_blockchain_addresses(list_of_anime_blockchain_addresses_to_verify):
    list_of_valid_anime_blockchain_addresses = []
    list_of_invalid_anime_blockchain_addresses = []
    for current_blockchain_address in list_of_anime_blockchain_addresses_to_verify:
        address_is_valid = check_if_anime_blockchain_address_is_valid_func(current_blockchain_address)
        if address_is_valid == 1:
            list_of_valid_anime_blockchain_addresses.append(current_blockchain_address)
        else:
            list_of_invalid_anime_blockchain_addresses.append(current_blockchain_address)
    return list_of_valid_anime_blockchain_addresses, list_of_invalid_anime_blockchain_addresses

def get_my_local_ip_func():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.connect(("8.8.8.8", 80))
    my_node_ip_address = s.getsockname()[0]
    s.close()
    return my_node_ip_address 

def get_all_remote_node_ips_func(): #For now we are just using the static example list of IPs; later we will use the RPC interface to get the list of masternode IPs
    global example_list_of_valid_masternode_ip_addresses
    global rpc_connection_string
    if 0:
        rpc_connection = AuthServiceProxy(rpc_connection_string)
        print(rpc_connection)
    return example_list_of_valid_masternode_ip_addresses

def generate_required_directories_func(list_of_required_folder_paths):
    for current_required_folder_path in list_of_required_folder_paths:
        if not os.path.exists(current_required_folder_path):
            try:
                os.makedirs(current_required_folder_path)
            except Exception as e:
                    print('Error: '+ str(e)) 

def filter_list_with_boolean_vector_func(python_list, boolean_vector):
    if len(python_list) == len(boolean_vector):
        filtered_list = []
        for cnt, current_element in enumerate(python_list):
            current_boolean = boolean_vector[cnt]
            if (current_boolean == True) or (current_boolean==1):
                filtered_list.append(current_element)
        return filtered_list
    else:    
        print('Error! Boolean vector must be the same length as python list!')

def check_current_bitcoin_blockchain_stats_func():
    successfully_retrieved_data = 0
    try:
        latest_block_number_url = 'https://blockchain.info/q/getblockcount'
        r = requests.get(latest_block_number_url)
        latest_block_number = r.text
        latest_block_hash_url = 'https://blockchain.info/q/latesthash'
        r = requests.get(latest_block_hash_url)
        latest_block_hash = r.text
        if (len(latest_block_number) > 0) and (len(latest_block_hash) > 0):
            successfully_retrieved_data = 1
    except Exception as e:
        print('Error: '+ str(e))    
    if successfully_retrieved_data == 0:        
        try:
            blockcypher_api_url = 'https://api.blockcypher.com/v1/btc/main'
            r = requests.get(blockcypher_api_url)
            blockcypher_api_response = r.text
            blockcypher_api_response_parsed = json.loads(blockcypher_api_response)
            latest_block_number = blockcypher_api_response_parsed['height']
            latest_block_hash = blockcypher_api_response_parsed['hash']
            if (len(latest_block_number) > 0) and (len(latest_block_hash) > 0):
                successfully_retrieved_data = 1
        except Exception as e:
            print('Error: '+ str(e))
    if successfully_retrieved_data == 0:        
        try:
            btcdotcom_api_url = 'https://chain.api.btc.com/v3/block/latest/'
            r = requests.get(btcdotcom_api_url)
            btcdotcom_api_response = r.text
            btcdotcom_api_response_parsed = json.loads(btcdotcom_api_response)
            btcdotcom_api_response_parsed = btcdotcom_api_response_parsed['data']
            latest_block_number = btcdotcom_api_response_parsed['height']
            latest_block_hash = btcdotcom_api_response_parsed['hash']
            if (len(latest_block_number) > 0) and (len(latest_block_hash) > 0):
                successfully_retrieved_data = 1
        except Exception as e:
            print('Error: '+ str(e))
    if successfully_retrieved_data == 1:
        return latest_block_number, latest_block_hash
    else:
        print('Error! Unable to retrieve latest bitcoin blockchain statistics!')
        latest_block_number = ''
        latest_block_hash = ''
        return latest_block_number, latest_block_hash

def clean_up_art_folder_after_registration_func(path_to_art_folder):
    deletion_count = 0
    list_of_file_paths_to_clean_up = glob.glob(path_to_art_folder+'*.zip')+glob.glob(path_to_art_folder+'*.db') + glob.glob(path_to_art_folder+'*.sig')
    for current_file_path in list_of_file_paths_to_clean_up:
        try:
            os.remove(current_file_path)
            deletion_count = deletion_count + 1
        except Exception as e:
            print('Error: '+ str(e))
    if deletion_count > 0:
        print('Finished cleaning up art folder! A total of '+str(deletion_count)+' files were deleted.')
        
def remove_existing_remaining_zip_files_func(path_to_art_folder):
    print('\nNow removing any existing remaining zip files...')
    zip_file_paths = glob.glob(path_to_art_folder+'\\*.zip')
    for current_zip_file_path in zip_file_paths:
        try:
            os.remove(current_zip_file_path)
        except Exception as e:
            print('Error: '+ str(e))
    print('Done removing old zip files!\n')

def delete_all_blocks_and_zip_files_to_reset_system_func():
    global block_storage_folder_path
    global reconstructed_files_destination_folder_path
    list_of_block_file_paths = glob.glob(block_storage_folder_path+'*.block')
    if len(list_of_block_file_paths) > 0:
        print('\nDeleting all the previously generated block files...')
        try:
            if 'Windows' in current_platform:
                check_output('rmdir /S /Q '+ block_storage_folder_path, shell=True)
                print('Done!\n')
            else:
                shutil.rmtree(block_storage_folder_path)
        except:
            print('.')
    previous_reconstructed_zip_file_paths = glob.glob(reconstructed_files_destination_folder_path+'*.zip')
    for current_existing_zip_file_path in previous_reconstructed_zip_file_paths:
        try:
            os.remove(current_existing_zip_file_path)
        except:
            pass

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

def download_remote_file_func(url_of_file_to_download, folder_path_for_saved_file, output_filename_prefix=''):
    path_to_downloaded_file = os.path.join(folder_path_for_saved_file, output_filename_prefix + url_of_file_to_download.split('/')[-1])
    r = requests.get(url_of_file_to_download, stream=True)
    with open(path_to_downloaded_file, 'wb') as f:
        for chunk in r.iter_content(chunk_size=1024): 
            if chunk: # filter out keep-alive new chunks
                f.write(chunk)
    return path_to_downloaded_file

def import_remote_chunkdb_file_func(path_to_remote_chunkdb_file): #Todo: Get this to work
    conn = sqlite3.connect(path_to_remote_chunkdb_file)
    c = conn.cursor()
    try:
        potential_global_hashes_table_results = c.execute("""SELECT * FROM potential_global_hashes""").fetchall()
    except Exception as e:
        print('Error: '+ str(e))
    try:
        final_art_zipfile_table_results = c.execute("""SELECT * FROM final_art_zipfile_table""").fetchall()
    except Exception as e:
        print('Error: '+ str(e))
    try:
        final_art_signatures_table_results = c.execute("""SELECT * FROM final_art_signatures_table""").fetchall()
    except Exception as e:
        print('Error: '+ str(e))
    try:
        pending_trade_tickets_table_results = c.execute("""SELECT * FROM pending_trade_tickets_table""").fetchall()
    except Exception as e:
        print('Error: '+ str(e))
    try:
        completed_trade_tickets_table_results = c.execute("""SELECT * FROM completed_trade_tickets_table""").fetchall()
    except Exception as e:
        print('Error: '+ str(e))        
    conn.commit()
    conn.close()
 
def import_downloaded_signature_files_func(): #Todo: Get this to work
    global chunk_db_file_path
    conn = sqlite3.connect(chunk_db_file_path)
    c = conn.cursor()
    # for current_file_hash in list_of_file_hashes:
    # c.execute('INSERT OR IGNORE INTO final_art_signatures_table (file_hash, final_signature_base64_encoded, remote_node_ip) VALUES (?,?,?)',[current_file_hash, current_file_hash, remote_node_ip]) 

def check_if_finished_art_zipfile_is_stored_locally_func(sha256_hash_of_desired_zipfile):
    global prepared_final_art_zipfiles_folder_path
    zipfile_stored_locally = 0
    list_of_matching_zipfile_paths = glob.glob(os.path.join(prepared_final_art_zipfiles_folder_path,'*'+sha256_hash_of_desired_zipfile+'.zip'))
    if len(list_of_matching_zipfile_paths) > 0:
        zipfile_stored_locally = 1
    return zipfile_stored_locally

def generate_and_save_local_masternode_identification_keypair_func():
    global masternode_keypair_db_file_path
    generated_id_keys_successfully = 0
    (masternode_public_key, masternode_private_key) = rsa.newkeys(512)
    masternode_public_key_export_format = rsa.PublicKey.save_pkcs1(masternode_public_key,format='PEM').decode('utf-8')
    masternode_private_key_export_format = rsa.PrivateKey.save_pkcs1(masternode_private_key,format='PEM').decode('utf-8')
    try:
        conn = sqlite3.connect(masternode_keypair_db_file_path)
        c = conn.cursor()
        concatenated_hash_signature_table_creation_string= """CREATE TABLE masternode_identification_keypair_table (masternode_public_key text, masternode_private_key text, datetime_masternode_keypair_was_generated TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, PRIMARY KEY (masternode_public_key));"""
        c.execute(concatenated_hash_signature_table_creation_string)
        data_insertion_query_string = """INSERT OR REPLACE INTO masternode_identification_keypair_table (masternode_public_key, masternode_private_key) VALUES (?,?);"""
        c.execute(data_insertion_query_string,[masternode_public_key_export_format, masternode_private_key_export_format])
        generated_id_keys_successfully = 1
        conn.commit()
        conn.close()
        print('Just generated a new public/private keypair for identifying the local masternode on the network!')
        return generated_id_keys_successfully
    except Exception as e:
        print('Error: '+ str(e))
        
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

def get_image_hash_from_image_file_path_func(path_to_art_image_file):
    try:
        with open(path_to_art_image_file,'rb') as f:
            art_image_file_binary_data = f.read()
        sha256_hash_of_art_image_file = hashlib.sha256(art_image_file_binary_data).hexdigest()        
        return sha256_hash_of_art_image_file
    except Exception as e:
        print('Error: '+ str(e))

def get_all_valid_image_file_paths_in_folder_func(path_to_art_folder):
    valid_image_file_paths = []
    try:
        art_input_file_paths =  glob.glob(path_to_art_folder + os.sep + '*.jpg') + glob.glob(path_to_art_folder + os.sep + '*.png') + glob.glob(path_to_art_folder + os.sep + '*.bmp') + glob.glob(path_to_art_folder + os.sep + '*.gif')
        for current_art_file_path in art_input_file_paths:
            if check_if_file_path_is_a_valid_image_func(current_art_file_path):
                valid_image_file_paths.append(current_art_file_path)
        return valid_image_file_paths
    except Exception as e:
        print('Error: '+ str(e))

def convert_numpy_array_to_sqlite_func(input_numpy_array):
    """ Store Numpy array natively in SQlite (see: http://stackoverflow.com/a/31312102/190597"""
    output_data = io.BytesIO()
    np.save(output_data, input_numpy_array)
    output_data.seek(0)
    return sqlite3.Binary(output_data.read())

def convert_sqlite_data_to_numpy_array_func(sqlite_data_in_text_format):
    output_data = io.BytesIO(sqlite_data_in_text_format)
    output_data.seek(0)
    return np.load(output_data)

def check_if_file_path_is_a_valid_image_func(path_to_file):
    is_image = 0
    try:
        if (imghdr.what(path_to_file) == 'gif') or (imghdr.what(path_to_file) == 'jpeg') or (imghdr.what(path_to_file) == 'png') or (imghdr.what(path_to_file) == 'bmp'):
            is_image = 1
            return is_image
    except Exception as e:
        print('Error: '+ str(e))

def generate_example_artwork_metadata_func():
    #This is a dummy function to just supply some data; in reality this will run on the artists computer and will be supplied along with the art image files to the masternode for registration
    artists_name = 'Arturo Lopez'
    artwork_title = 'Girl with a Bitcoin Earring'
    total_number_of_unique_copies_of_artwork = 100
    artwork_series_name = 'Animecoin Initial Release Crew: The Great Works Collection'
    artists_website='http://www.anime-coin.com'
    artists_statement_about_artwork = 'This work is a reference to astronomers in ancient times, using tools like astrolabes.'
    return artists_name, artwork_title, total_number_of_unique_copies_of_artwork, artwork_series_name, artists_website, artists_statement_about_artwork

def get_metadata_file_path_from_art_folder_func(path_to_art_folder):
    path_to_metadata_html_table_file = glob.glob(path_to_art_folder + 'Animecoin_Metadata_File__Combined_Hash__*.html')[0]
    return path_to_metadata_html_table_file

def validate_url_func(url_string_to_check):
    url_is_valid = 0
    regex = re.compile(
        r'^(?:http|ftp)s?://' # http:// or https://
        r'(?:(?:[A-Z0-9](?:[A-Z0-9-]{0,61}[A-Z0-9])?\.)+(?:[A-Z]{2,6}\.?|[A-Z0-9-]{2,}\.?)|' #domain...
        r'localhost|' #localhost...
        r'\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})' # ...or ip
        r'(?::\d+)?' # optional port
        r'(?:/?|[/?]\S+)$', re.IGNORECASE)
    url_match = re.match(regex, url_string_to_check) 
    if url_match is not None:
        url_is_valid = 1
    return url_is_valid

def check_if_ip_address_responds_to_pings_func(ip_address_to_ping):
    try:
        output = subprocess.check_output("ping -{} 1 {}".format('n' if platform.system().lower()=="windows" else 'c', ip_address_to_ping), shell=True)
        print('Results of Ping:' + os.linesep + output.decode('utf-8'))
    except Exception as e:
        return False
    return True
   
