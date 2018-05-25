import os.path, glob, os, sqlite3, warnings, json, io, imghdr, socket, base64, pickle, platform, hashlib, shutil, re, subprocess, random, fnmatch, sys, sqlite3
warnings.simplefilter(action='ignore', category=FutureWarning)
warnings.filterwarnings('ignore',category=DeprecationWarning)
import requests
import rsa
import zstd
import shortuuid
import numpy as np
import pandas as pd
import tensorflow as tf
import lxml, lxml.html
from keras.preprocessing import image
from keras.applications.imagenet_utils import preprocess_input
from keras import applications
from sklearn import decomposition, manifold, pipeline
from PIL import Image
from resizeimage import resizeimage
from datetime import datetime, timedelta
from subprocess import check_output
from bitcoinrpc.authproxy import AuthServiceProxy, JSONRPCException
from tqdm import tqdm
os.environ['TF_CPP_MIN_LOG_LEVEL'] = '3'
sys.setrecursionlimit(3000)

#Requirements: pip install numpy, requests, rsa, bitcoinrpc, zstandard, shortuuid, PIL, python-resize-image
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

def get_sha256_hash_of_input_data_func(input_data_or_string):
    if isinstance(input_data_or_string, str):
        input_data_or_string = input_data_or_string.encode('utf-8')
    sha256_hash_of_input_data = hashlib.sha256(input_data_or_string).hexdigest()         
    return sha256_hash_of_input_data

def get_image_hash_from_image_file_path_func(path_to_art_image_file):
    try:
        with open(path_to_art_image_file,'rb') as f:
            art_image_file_binary_data = f.read()
        sha256_hash_of_art_image_file = get_sha256_hash_of_input_data_func(art_image_file_binary_data)
        return sha256_hash_of_art_image_file
    except Exception as e:
        print('Error: '+ str(e))

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
    temp_files_folder_path = os.path.join(root_animecoin_folder_path,'temp_files' + os.sep)
    prepared_final_art_zipfiles_folder_path = os.path.join(root_animecoin_folder_path,'prepared_final_art_zipfiles' + os.sep) 
    chunk_db_file_path = os.path.join(misc_masternode_files_folder_path,'anime_chunkdb.sqlite')
    file_storage_log_file_path = os.path.join(misc_masternode_files_folder_path,'anime_storage_log.txt')
    nginx_allowed_ip_whitelist_file_path = '/var/www/masternode_file_server/html/masternode_ip_whitelist.conf'
    path_to_animecoin_trade_ticket_template = os.path.join(trade_ticket_files_folder_path,'animecoin_trade_ticket_template.html')
    artist_final_signature_files_folder_path = os.path.join(root_animecoin_folder_path,'art_signature_files' + os.sep)
    dupe_detection_image_fingerprint_database_file_path = os.path.join(misc_masternode_files_folder_path,'dupe_detection_image_fingerprint_database.sqlite')
    path_to_animecoin_artwork_metadata_template = os.path.join(misc_masternode_files_folder_path,'animecoin_metadata_template.html')
    path_where_animecoin_html_ticket_templates_are_stored = 'C:\\Users\\jeffr\\Cointel Dropbox\\Animecoin_Code\\'
    path_to_nsfw_detection_model_file = os.path.join(misc_masternode_files_folder_path,'nsfw_trained_model.pb')
    return root_animecoin_folder_path, folder_path_of_art_folders_to_encode, block_storage_folder_path, folder_path_of_remote_node_sqlite_files, reconstructed_file_destination_folder_path, \
            misc_masternode_files_folder_path, masternode_keypair_db_file_path, trade_ticket_files_folder_path, completed_trade_ticket_files_folder_path, pending_trade_ticket_files_folder_path, \
            temp_files_folder_path, prepared_final_art_zipfiles_folder_path, chunk_db_file_path, file_storage_log_file_path, nginx_allowed_ip_whitelist_file_path, path_to_animecoin_trade_ticket_template, \
            artist_final_signature_files_folder_path, dupe_detection_image_fingerprint_database_file_path, path_to_animecoin_artwork_metadata_template, path_where_animecoin_html_ticket_templates_are_stored, \
            path_to_nsfw_detection_model_file

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
    nsfw_score_threshold = 0.97
    duplicate_image_threshold = 0.08 #Any image which has another image in our image fingerprint database with a "distance" of less than this threshold will be considered a duplicate. 
    dupe_detection_model_1_name = 'VGG19'
    dupe_detection_model_2_name = 'Xception'
    dupe_detection_model_3_name = 'ResNet50'
    return anime_metadata_format_version_number, minimum_total_number_of_unique_copies_of_artwork, maximum_total_number_of_unique_copies_of_artwork, target_number_of_nodes_per_unique_block_hash, target_block_redundancy_factor, desired_block_size_in_bytes, \
            remote_node_chunkdb_refresh_time_in_minutes, remote_node_image_fingerprintdb_refresh_time_in_minutes, percentage_of_block_files_to_randomly_delete, percentage_of_block_files_to_randomly_corrupt, percentage_of_each_selected_file_to_be_randomly_corrupted, \
            registration_fee_anime_per_megabyte_of_images_pre_difficulty_adjustment, example_list_of_valid_masternode_ip_addresses, forfeitable_deposit_to_initiate_registration_as_percentage_of_adjusted_registration_fee, nginx_ip_whitelist_override_addresses, \
            example_animecoin_masternode_blockchain_address, example_trader_blockchain_address, example_artists_receiving_blockchain_address, rpc_connection_string, \
            max_number_of_blocks_to_download_before_checking, earliest_possible_artwork_signing_date, maximum_length_in_characters_of_text_field, maximum_combined_image_size_in_megabytes_for_single_artwork, \
            nsfw_score_threshold, duplicate_image_threshold, dupe_detection_model_1_name, dupe_detection_model_2_name, dupe_detection_model_3_name

#Get various settings:
anime_metadata_format_version_number, minimum_total_number_of_unique_copies_of_artwork, maximum_total_number_of_unique_copies_of_artwork, target_number_of_nodes_per_unique_block_hash, target_block_redundancy_factor, desired_block_size_in_bytes, \
remote_node_chunkdb_refresh_time_in_minutes, remote_node_image_fingerprintdb_refresh_time_in_minutes, percentage_of_block_files_to_randomly_delete, percentage_of_block_files_to_randomly_corrupt, percentage_of_each_selected_file_to_be_randomly_corrupted, \
registration_fee_anime_per_megabyte_of_images_pre_difficulty_adjustment, example_list_of_valid_masternode_ip_addresses, forfeitable_deposit_to_initiate_registration_as_percentage_of_adjusted_registration_fee, nginx_ip_whitelist_override_addresses, \
example_animecoin_masternode_blockchain_address, example_trader_blockchain_address, example_artists_receiving_blockchain_address, rpc_connection_string, max_number_of_blocks_to_download_before_checking, earliest_possible_artwork_signing_date, \
maximum_length_in_characters_of_text_field, maximum_combined_image_size_in_megabytes_for_single_artwork, nsfw_score_threshold, duplicate_image_threshold, dupe_detection_model_1_name, dupe_detection_model_2_name, \
dupe_detection_model_3_name = get_all_animecoin_parameters_func()
#Get various directories:
root_animecoin_folder_path, folder_path_of_art_folders_to_encode, block_storage_folder_path, folder_path_of_remote_node_sqlite_files, reconstructed_file_destination_folder_path, \
misc_masternode_files_folder_path, masternode_keypair_db_file_path, trade_ticket_files_folder_path, completed_trade_ticket_files_folder_path, pending_trade_ticket_files_folder_path, \
temp_files_folder_path, prepared_final_art_zipfiles_folder_path, chunk_db_file_path, file_storage_log_file_path, nginx_allowed_ip_whitelist_file_path, path_to_animecoin_trade_ticket_template, \
artist_final_signature_files_folder_path, dupe_detection_image_fingerprint_database_file_path, path_to_animecoin_artwork_metadata_template, path_where_animecoin_html_ticket_templates_are_stored, \
path_to_nsfw_detection_model_file = get_all_animecoin_directories_func()

def get_various_directories_for_testing_func():
    path_to_art_folder = os.path.join(folder_path_of_art_folders_to_encode,'Arturo_Lopez__Number_02' + os.sep)
    path_to_art_image_file = os.path.join(path_to_art_folder,'Arturo_Lopez__Number_02.png')
    path_to_another_similar_art_image_file = os.path.join(path_to_art_folder,'Arturo_Lopez__Number_02__With_Background.png')
    path_to_another_different_art_image_file = 'C:\\animecoin\\art_folders_to_encode\\Arturo_Lopez__Number_04\\Arturo_Lopez__Number_04.png'
    #path_to_artwork_metadata_file = os.path.join(path_to_art_folder,'artwork_metadata_file.db')
    path_to_all_registered_works_for_dupe_detection = 'C:\\Users\\jeffr\Cointel Dropbox\\Jeffrey Emanuel\\Animecoin_All_Finished_Works\\'
    sha256_hash_of_art_image_file = get_image_hash_from_image_file_path_func(path_to_art_image_file)
    return path_to_art_folder, path_to_art_image_file, path_to_another_similar_art_image_file, path_to_another_different_art_image_file, path_to_all_registered_works_for_dupe_detection, sha256_hash_of_art_image_file

#Various paths/data for testing purposes:
path_to_art_folder, path_to_art_image_file, path_to_another_similar_art_image_file, path_to_another_different_art_image_file, path_to_all_registered_works_for_dupe_detection, sha256_hash_of_art_image_file = get_various_directories_for_testing_func()

def get_current_datetime_string_func():
    current_datetime_string = datetime.now().strftime('%Y-%m-%d %H:%M:%S:%f')
    return current_datetime_string

def get_current_datetime_string_filepath_safe_func():
    current_datetime_string = datetime.now().strftime('%Y-%m-%d %H:%M:%S:%f')
    current_datetime_string_filtered = current_datetime_string.replace('-','_').replace(' ','__').replace(':','_')
    return current_datetime_string_filtered

def generate_recent_random_datetime_string_func():
    random_datetime = datetime.now() + timedelta(minutes=-random.randint(0,200))
    recent_random_datetime_string = random_datetime.strftime('%Y-%m-%d %H:%M:%S:%f')
    return recent_random_datetime_string
    
def parse_datetime_string_func(datetime_string):
    current_datetime = datetime.strptime(datetime_string,'%Y-%m-%d %H:%M:%S:%f')
    return current_datetime

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
        output = subprocess.check_output('ping -{} 1 {}'.format('n' if platform.system().lower()=='windows' else 'c', ip_address_to_ping), shell=True)
        print('Results of Ping:' + os.linesep + output.decode('utf-8'))
    except Exception as e:
        return False
    return True

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
    animecoin_zstd_compression_dictionary = zstd.ZstdCompressionDict(combined_dictionary_input_data)
    if use_verbose:
        print('Done!')
    return animecoin_zstd_compression_dictionary

def compress_data_with_animecoin_zstd_func(input_data, animecoin_zstd_compression_dictionary):
    global zstd_compression_level
    zstd_compression_level = 22 #Highest (best) compression level is 22
    zstandard_animecoin_compressor = zstd.ZstdCompressor(dict_data=animecoin_zstd_compression_dictionary, level=zstd_compression_level, write_content_size=True)
    if isinstance(input_data,str):
        input_data = input_data.encode('utf-8')
    animecoin_zstd_compressed_data = zstandard_animecoin_compressor.compress(input_data)
    return animecoin_zstd_compressed_data

def decompress_data_with_animecoin_zstd_func(animecoin_zstd_compressed_data, animecoin_zstd_compression_dictionary):
    zstandard_animecoin_decompressor = zstd.ZstdDecompressor(dict_data=animecoin_zstd_compression_dictionary)
    uncompressed_data = zstandard_animecoin_decompressor.decompress(animecoin_zstd_compressed_data)
    return uncompressed_data

def convert_list_of_strings_into_list_of_utf_encoded_byte_objects_func(list_of_strings):
    list_of_utf_encoded_byte_objects = []
    for current_string in list_of_strings:
        if isinstance(current_string,str):
            list_of_utf_encoded_byte_objects.append(current_string.encode('utf-8'))
        else:
            print('Error! Invalid string given as input.')
    return list_of_utf_encoded_byte_objects     

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

def check_current_animecoin_blockchain_stats_func(): #Todo: make this a real function using the RPC interface
    latest_block_number = '2512'
    latest_block_hash = '00000000000000000020cf882b78a089514eb19583b36171e62b16994f94e6b7'
    return latest_block_number, latest_block_hash

def get_nsfw_model_file_hash_func():
    global path_to_nsfw_detection_model_file
    nsfw_detection_model_file_hash = get_sha256_hash_of_input_data_func(path_to_nsfw_detection_model_file)
    return nsfw_detection_model_file_hash

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

def generate_new_animecoin_blockchain_address_func(): #Todo: replace this with a real function that uses the RPC interface
    shortuuid.set_alphabet('AWMCtF2FFgCYhDjCwHT69hG2JcwE8Ctuj7ARsGF51zdsmySHBiJDz6Si7zwXNA1gkJB3ALQRNX7yYLrYTmp7zA4uw8CtjRKhedenZCJ')
    generated_animecoin_blockchain_address = 'A' + shortuuid.uuid()[0:16] + shortuuid.uuid()[0:17]
    return generated_animecoin_blockchain_address

def get_my_masternode_collateral_address_and_key_pair_func(): #Todo: replace this with a real function that uses the RPC interface
    list_of_example_masternode_collateral_addresses = ['AWMCtF2FFgCYhDjCwHT69hG2JcwE8Ctuj7', 'ARsGF51zdsmySHBiJDz6Si7zwXNA1gkJB3', 'ALQRNX7yYLrYTmp7zA4uw8CtjRKhedenZCJ']
    my_masternode_collateral_address = random.choice(list_of_example_masternode_collateral_addresses)
    my_masternode_collateral_public_key, my_masternode_collateral_private_key, _, _, _, _ = get_example_keypair_of_rsa_public_and_private_keys_func()
    return my_masternode_collateral_address, my_masternode_collateral_public_key, my_masternode_collateral_private_key

def get_current_animecoin_metadata_format_version_number_func():
    current_animecoin_metadata_format_version_number = '1.00'
    return current_animecoin_metadata_format_version_number

def get_concatenated_art_image_file_hashes_and_total_size_in_mb_func(path_to_art_folder):
    art_input_file_paths = get_all_valid_image_file_paths_in_folder_func(path_to_art_folder)
    list_of_art_file_hashes = []
    combined_image_file_size_in_mb = 0
    for current_file_path in art_input_file_paths:
        with open(current_file_path,'rb') as f:
            current_art_file_data = f.read()
        sha256_hash_of_current_art_file = get_sha256_hash_of_input_data_func(current_art_file_data)               
        list_of_art_file_hashes.append(sha256_hash_of_current_art_file)
        current_file_size_in_mb = os.path.getsize(current_file_path)/1000000
        combined_image_file_size_in_mb = combined_image_file_size_in_mb + current_file_size_in_mb
    combined_image_file_size_in_mb = str(combined_image_file_size_in_mb)
    if len(list_of_art_file_hashes) > 0:
        try:
            concatenated_file_hashes = ''.join(list_of_art_file_hashes).encode('utf-8')
            return concatenated_file_hashes, combined_image_file_size_in_mb
        except Exception as e:
            print('Error: '+ str(e))

def unique_list_func(input_list):
    unique_input_list = []
    [unique_input_list.append(x) for x in input_list if x not in unique_input_list]
    return unique_input_list

def generate_fake_artwork_title_func():
    artist_name_fake_title_word_list = ['Vampire', 'Nurse', 'Monster', 'Chibi', 'Sword', 'Magnificent', 'Hero', 'Evil', 'Pure', 'Unreal', 'Amazing', 'Insanity', 'Power']
    fake_artwork_title_string = ''
    if random.random() > 0.5:
        fake_artwork_title_string = fake_artwork_title_string + 'The ' 
    fake_artwork_title_string = fake_artwork_title_string + random.choice(artist_name_fake_title_word_list) + ' '
    if random.random() > 0.7:
        fake_artwork_title_string = fake_artwork_title_string + ': ' 
        first_colon_used = 1
    else:
        first_colon_used = 0
    fake_artwork_title_string = fake_artwork_title_string + random.choice(artist_name_fake_title_word_list) + ' '
    if (random.random() > 0.7) and (not first_colon_used):
        fake_artwork_title_string = fake_artwork_title_string + ': ' 
    fake_artwork_title_string = fake_artwork_title_string + random.choice(artist_name_fake_title_word_list) + ' '
    fake_artwork_title_string = ' '.join(unique_list_func(fake_artwork_title_string.split()))
    fake_artwork_title_string = fake_artwork_title_string.replace(' :',':')
    if random.random() > 0.7:
        fake_artwork_title_string = fake_artwork_title_string + ' ' + str(random.randint(2,6))
    return fake_artwork_title_string

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
    current_platform = platform.platform()
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
    conn.close()
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

def get_local_masternode_animecoin_id_keypair_func():
    global masternode_keypair_db_file_path
    if not os.path.exists(masternode_keypair_db_file_path):
        print('Error, could not find local masternode identification keypair file! Generating new one now.')
        generate_and_save_local_masternode_identification_keypair_func()
        get_local_masternode_animecoin_id_keypair_func()
    try:
        conn = sqlite3.connect(masternode_keypair_db_file_path)
        c = conn.cursor()
        masternode_identification_keypair_query_results = c.execute("""SELECT * FROM masternode_identification_keypair_table""").fetchall()
        masternode_public_key_export_format = masternode_identification_keypair_query_results[0][0]
        masternode_private_key_export_format = masternode_identification_keypair_query_results[0][1]
        masternode_public_key = rsa.PublicKey.load_pkcs1(masternode_public_key_export_format,format='PEM')
        masternode_private_key = rsa.PrivateKey.load_pkcs1(masternode_private_key_export_format,format='PEM')
        conn.close()
        masternode_public_key_pem_format = rsa.PublicKey.save_pkcs1(masternode_public_key,format='PEM').decode('utf-8')
        return masternode_public_key, masternode_private_key, masternode_public_key_pem_format
    except Exception as e:
        print('Error: '+ str(e))

def get_named_model_func(model_name):
    if model_name == 'Xception':
        return applications.xception.Xception(weights='imagenet', include_top=False, pooling='avg')
    if model_name == 'VGG16':
        return applications.vgg16.VGG16(weights='imagenet', include_top=False, pooling='avg')
    if model_name == 'VGG19':
        return applications.vgg19.VGG19(weights='imagenet', include_top=False, pooling='avg')
    if model_name == 'InceptionV3':
        return applications.inception_v3.InceptionV3(weights='imagenet', include_top=False, pooling='avg')
    if model_name == 'MobileNet':
        return applications.mobilenet.MobileNet(weights='imagenet', include_top=False, pooling='avg')
    if model_name == 'ResNet50':
        return applications.resnet50.ResNet50(weights='imagenet', include_top=False, pooling='avg')
    if model_name == 'DenseNet201':
        return applications.DenseNet201(weights='imagenet', include_top=False, pooling='avg')
    if model_name == 'TSNE':
        return manifold.TSNE(random_state=0)
    if model_name == 'PCA-TSNE':
        tsne = manifold.TSNE(random_state=0, perplexity=50, early_exaggeration=6.0)
        pca = decomposition.PCA(n_components=48)
        return pipeline.Pipeline([('reduce_dims', pca), ('tsne', tsne)])
    if model_name == 'PCA':
        return decomposition.PCA(n_components=48)
    raise ValueError('Unknown model')

def prepare_image_fingerprint_data_for_export_func(image_feature_data):
    image_feature_data_arr = np.char.mod('%f', image_feature_data) # convert from Numpy to a list of values
    x_data = np.asarray(image_feature_data_arr).astype('float64') # convert image data to float64 matrix. float64 is need for bh_sne
    image_fingerprint_vector = x_data.reshape((x_data.shape[0], -1))
    return image_fingerprint_vector

def get_image_deep_learning_features_func(path_to_art_image_file):
    global dupe_detection_model_1, dupe_detection_model_1_name
    global dupe_detection_model_2, dupe_detection_model_2_name
    global dupe_detection_model_3, dupe_detection_model_3_name
    try:
        if os.path.isfile(path_to_art_image_file):
            with open(path_to_art_image_file,'rb') as f:
                image_file_binary_data = f.read()
                sha256_hash_of_art_image_file = get_sha256_hash_of_input_data_func(image_file_binary_data)
            img = image.load_img(path_to_art_image_file, target_size=(224, 224)) # load image setting the image size to 224 x 224
            x = image.img_to_array(img) # convert image to numpy array
            x = np.expand_dims(x, axis=0) # the image is now in an array of shape (3, 224, 224) but we need to expand it to (1, 2, 224, 224) as Keras is expecting a list of images
            x = preprocess_input(x)
            dupe_detection_model_1_loaded_already = 'dupe_detection_model_1' in globals()
            if not dupe_detection_model_1_loaded_already:
                print('Loading deep learning model 1 ('+dupe_detection_model_1_name+')...')
                dupe_detection_model_1 = get_named_model_func(dupe_detection_model_1_name)
            dupe_detection_model_2_loaded_already = 'dupe_detection_model_2' in globals()
            if not dupe_detection_model_2_loaded_already:
                print('Loading deep learning model 2 ('+dupe_detection_model_2_name+')...')
                dupe_detection_model_2 = get_named_model_func(dupe_detection_model_2_name)
            dupe_detection_model_3_loaded_already = 'dupe_detection_model_3' in globals()
            if not dupe_detection_model_3_loaded_already:
                print('Loading deep learning model 3 ('+dupe_detection_model_3_name+')...')
                dupe_detection_model_3 = get_named_model_func(dupe_detection_model_3_name)
            model_1_features = dupe_detection_model_1.predict(x)[0] # extract the features
            model_2_features = dupe_detection_model_2.predict(x)[0] 
            model_3_features = dupe_detection_model_3.predict(x)[0] 
            model_1_image_fingerprint_vector = prepare_image_fingerprint_data_for_export_func(model_1_features)
            model_2_image_fingerprint_vector = prepare_image_fingerprint_data_for_export_func(model_2_features)
            model_3_image_fingerprint_vector = prepare_image_fingerprint_data_for_export_func(model_3_features)
            return model_1_image_fingerprint_vector,model_2_image_fingerprint_vector,model_3_image_fingerprint_vector, sha256_hash_of_art_image_file, dupe_detection_model_1, dupe_detection_model_2, dupe_detection_model_3
    except Exception as e:
        print('Error: '+ str(e))

#Dupe detection helper functions:
def regenerate_dupe_detection_image_fingerprint_database_func():
    global dupe_detection_image_fingerprint_database_file_path
    try:
        conn = sqlite3.connect(dupe_detection_image_fingerprint_database_file_path, detect_types=sqlite3.PARSE_DECLTYPES)
        c = conn.cursor()
        dupe_detection_image_fingerprint_database_deletion_string = """DROP TABLE image_hash_to_image_fingerprint_table"""
        c.execute(dupe_detection_image_fingerprint_database_deletion_string)
        dupe_detection_image_fingerprint_database_deletion_string = """DROP TABLE tsne_coordinates_table_model_1"""
        c.execute(dupe_detection_image_fingerprint_database_deletion_string)
        dupe_detection_image_fingerprint_database_deletion_string = """DROP TABLE tsne_coordinates_table_model_2"""
        c.execute(dupe_detection_image_fingerprint_database_deletion_string)
        dupe_detection_image_fingerprint_database_deletion_string = """DROP TABLE tsne_coordinates_table_model_3"""
        c.execute(dupe_detection_image_fingerprint_database_deletion_string)
        conn.commit()
    except Exception as e:
        print('Error: '+ str(e))    
    try:
        dupe_detection_image_fingerprint_database_creation_string = """CREATE TABLE image_hash_to_image_fingerprint_table (sha256_hash_of_art_image_file text, model_1_image_fingerprint_vector array, model_2_image_fingerprint_vector array, model_3_image_fingerprint_vector array, datetime_fingerprint_added_to_database TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, PRIMARY KEY (sha256_hash_of_art_image_file));"""
        c.execute(dupe_detection_image_fingerprint_database_creation_string)
        conn.commit()
        model_1_tsne_table_creation_string= """CREATE TABLE tsne_coordinates_table_model_1 (sha256_hash_of_art_image_file text, tsne_x_coordinate real, tsne_y_coordinate real, datetime_fingerprint_added_to_database TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, PRIMARY KEY (sha256_hash_of_art_image_file, tsne_x_coordinate, tsne_y_coordinate));"""
        c.execute(model_1_tsne_table_creation_string)
        conn.commit()
        model_2_tsne_table_creation_string= """CREATE TABLE tsne_coordinates_table_model_2 (sha256_hash_of_art_image_file text, tsne_x_coordinate real, tsne_y_coordinate real, datetime_fingerprint_added_to_database TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, PRIMARY KEY (sha256_hash_of_art_image_file, tsne_x_coordinate, tsne_y_coordinate));"""
        c.execute(model_2_tsne_table_creation_string)
        conn.commit()
        model_3_tsne_table_creation_string= """CREATE TABLE tsne_coordinates_table_model_3 (sha256_hash_of_art_image_file text, tsne_x_coordinate real, tsne_y_coordinate real, datetime_fingerprint_added_to_database TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, PRIMARY KEY (sha256_hash_of_art_image_file, tsne_x_coordinate, tsne_y_coordinate));"""
        c.execute(model_3_tsne_table_creation_string)
        conn.commit()
        conn.commit()
        conn.close()
    except Exception as e:
        print('Error: '+ str(e))
        
def add_image_fingerprints_to_dupe_detection_database_func(path_to_art_image_file):
    global dupe_detection_image_fingerprint_database_file_path
    global dupe_detection_model_1, dupe_detection_model_2, dupe_detection_model_3
    model_1_image_fingerprint_vector, model_2_image_fingerprint_vector, model_3_image_fingerprint_vector, sha256_hash_of_art_image_file, \
    dupe_detection_model_1, dupe_detection_model_2, dupe_detection_model_3 = get_image_deep_learning_features_func(path_to_art_image_file)
    conn = sqlite3.connect(dupe_detection_image_fingerprint_database_file_path,detect_types=sqlite3.PARSE_DECLTYPES)
    c = conn.cursor()
    data_insertion_query_string = """INSERT OR REPLACE INTO image_hash_to_image_fingerprint_table (sha256_hash_of_art_image_file, model_1_image_fingerprint_vector, model_2_image_fingerprint_vector, model_3_image_fingerprint_vector) VALUES (?,?,?,?);"""
    c.execute(data_insertion_query_string,[sha256_hash_of_art_image_file, model_1_image_fingerprint_vector, model_2_image_fingerprint_vector, model_3_image_fingerprint_vector])
    conn.commit()
    conn.close()
    return model_1_image_fingerprint_vector, model_2_image_fingerprint_vector, model_3_image_fingerprint_vector, sha256_hash_of_art_image_file, dupe_detection_model_1, dupe_detection_model_2, dupe_detection_model_3

def add_all_images_in_folder_to_image_fingerprint_database_func(path_to_art_folder):
    valid_image_file_paths = get_all_valid_image_file_paths_in_folder_func(path_to_art_folder)
    for current_image_file_path in valid_image_file_paths:
        print('\nNow adding image file '+ current_image_file_path+' to image fingerprint database.')
        model_1_image_fingerprint_vector, model_2_image_fingerprint_vector, model_3_image_fingerprint_vector, sha256_hash_of_art_image_file,  \
        dupe_detection_model_1, dupe_detection_model_2, dupe_detection_model_3 = add_image_fingerprints_to_dupe_detection_database_func(current_image_file_path)
    return dupe_detection_model_1, dupe_detection_model_2, dupe_detection_model_3

def get_image_fingerprints_from_dupe_detection_database_func(sha256_hash_of_art_image_file):
    global dupe_detection_image_fingerprint_database_file_path
    conn = sqlite3.connect(dupe_detection_image_fingerprint_database_file_path,detect_types=sqlite3.PARSE_DECLTYPES)
    c = conn.cursor()
    dupe_detection_fingerprint_query_results = c.execute("""SELECT model_1_image_fingerprint_vector, model_2_image_fingerprint_vector, model_3_image_fingerprint_vector FROM image_hash_to_image_fingerprint_table where sha256_hash_of_art_image_file = ? ORDER BY datetime_fingerprint_added_to_database DESC""",[sha256_hash_of_art_image_file,]).fetchall()
    if len(dupe_detection_fingerprint_query_results) == 0:
        print('Fingerprints for this image could not be found, try adding it to the system!')
    model_1_image_fingerprint_vector = dupe_detection_fingerprint_query_results[0][0]
    model_2_image_fingerprint_vector = dupe_detection_fingerprint_query_results[0][1]
    model_3_image_fingerprint_vector = dupe_detection_fingerprint_query_results[0][2]
    conn.close()
    return model_1_image_fingerprint_vector, model_2_image_fingerprint_vector, model_3_image_fingerprint_vector

def construct_image_fingerprint_matrix_from_database_func(list_of_image_fingerprint_vectors):
    combined_fingerprint_matrix = np.vstack(list_of_image_fingerprint_vectors).T[0]
    combined_fingerprint_matrix = combined_fingerprint_matrix.reshape((len(list_of_image_fingerprint_vectors), -1))
    combined_fingerprint_matrix = combined_fingerprint_matrix.reshape((combined_fingerprint_matrix.shape[0], -1))
    return combined_fingerprint_matrix

def apply_tsne_to_image_fingerprint_matrix_func(combined_fingerprint_matrix):
    vis_data = tsne_model.fit_transform(combined_fingerprint_matrix) # perform t-SNE
    tsne_x_coordinates = vis_data[:,0]
    tsne_y_coordinates = vis_data[:,1]
    return tsne_x_coordinates, tsne_y_coordinates

def apply_tsne_to_image_fingerprint_database_func():
    global dupe_detection_image_fingerprint_database_file_path
    global tsne_model
    print('Now applying tSNE to image fingerprint database...')
    sqlite3.register_adapter(np.ndarray, convert_numpy_array_to_sqlite_func) # Converts np.array to TEXT when inserting
    sqlite3.register_converter('array', convert_sqlite_data_to_numpy_array_func) # Converts TEXT to np.array when selecting
    conn = sqlite3.connect(dupe_detection_image_fingerprint_database_file_path,detect_types=sqlite3.PARSE_DECLTYPES)
    c = conn.cursor()
    dupe_detection_fingerprint_query_results = c.execute("""SELECT sha256_hash_of_art_image_file, model_1_image_fingerprint_vector, model_2_image_fingerprint_vector, model_3_image_fingerprint_vector FROM image_hash_to_image_fingerprint_table ORDER BY datetime_fingerprint_added_to_database DESC""").fetchall()
    list_of_image_sha256_hashes = [x[0] for x in dupe_detection_fingerprint_query_results]
    list_of_model_1_image_fingerprint_vectors =  [x[1] for x in dupe_detection_fingerprint_query_results]
    list_of_model_2_image_fingerprint_vectors =  [x[2] for x in dupe_detection_fingerprint_query_results]
    list_of_model_3_image_fingerprint_vectors =  [x[3] for x in dupe_detection_fingerprint_query_results]
    combined_model_1_fingerprint_matrix = construct_image_fingerprint_matrix_from_database_func(list_of_model_1_image_fingerprint_vectors)
    combined_model_2_fingerprint_matrix = construct_image_fingerprint_matrix_from_database_func(list_of_model_2_image_fingerprint_vectors)
    combined_model_3_fingerprint_matrix = construct_image_fingerprint_matrix_from_database_func(list_of_model_3_image_fingerprint_vectors)
    tsne_model_loaded_already = 'tsne_model' in globals()
    if not tsne_model_loaded_already:
        print('Loading tSNE model...')
        tsne_model = manifold.TSNE(random_state=0)
    model_1_tsne_x_coordinates, model_1_tsne_y_coordinates = apply_tsne_to_image_fingerprint_matrix_func(combined_model_1_fingerprint_matrix)
    model_2_tsne_x_coordinates, model_2_tsne_y_coordinates = apply_tsne_to_image_fingerprint_matrix_func(combined_model_2_fingerprint_matrix)
    model_3_tsne_x_coordinates, model_3_tsne_y_coordinates = apply_tsne_to_image_fingerprint_matrix_func(combined_model_3_fingerprint_matrix)
    pbar = tqdm(total=len(list_of_image_sha256_hashes))
    for file_cnt, current_image_hash in enumerate(list_of_image_sha256_hashes):
        current_model_1_tsne_x_coordinate = float(model_1_tsne_x_coordinates[file_cnt])
        current_model_1_tsne_y_coordinate = float(model_1_tsne_y_coordinates[file_cnt])
        current_model_2_tsne_x_coordinate = float(model_2_tsne_x_coordinates[file_cnt])
        current_model_2_tsne_y_coordinate = float(model_2_tsne_y_coordinates[file_cnt])
        current_model_3_tsne_x_coordinate = float(model_3_tsne_x_coordinates[file_cnt])
        current_model_3_tsne_y_coordinate = float(model_3_tsne_y_coordinates[file_cnt])            
        model_1_data_insertion_query_string = """INSERT OR REPLACE INTO tsne_coordinates_table_model_1 (sha256_hash_of_art_image_file, tsne_x_coordinate, tsne_y_coordinate) VALUES (?,?,?);"""
        c.execute(model_1_data_insertion_query_string,[current_image_hash, current_model_1_tsne_x_coordinate, current_model_1_tsne_y_coordinate])
        model_2_data_insertion_query_string = """INSERT OR REPLACE INTO tsne_coordinates_table_model_2 (sha256_hash_of_art_image_file, tsne_x_coordinate, tsne_y_coordinate) VALUES (?,?,?);"""
        c.execute(model_2_data_insertion_query_string,[current_image_hash, current_model_2_tsne_x_coordinate, current_model_2_tsne_y_coordinate])
        model_3_data_insertion_query_string = """INSERT OR REPLACE INTO tsne_coordinates_table_model_3 (sha256_hash_of_art_image_file, tsne_x_coordinate, tsne_y_coordinate) VALUES (?,?,?);"""
        c.execute(model_3_data_insertion_query_string,[current_image_hash, current_model_3_tsne_x_coordinate, current_model_3_tsne_y_coordinate])
        conn.commit()
        pbar.update(1)
    conn.close()
    print('Done!\n')
    return list_of_image_sha256_hashes, model_1_tsne_x_coordinates, model_1_tsne_y_coordinates, model_2_tsne_x_coordinates, model_2_tsne_y_coordinates, model_3_tsne_x_coordinates, model_3_tsne_y_coordinates

def get_tsne_coordinates_for_desired_image_file_hash_func(sha256_hash_of_art_image_file):
    global dupe_detection_image_fingerprint_database_file_path
    sqlite3.register_adapter(np.ndarray, convert_numpy_array_to_sqlite_func) # Converts np.array to TEXT when inserting
    sqlite3.register_converter('array', convert_sqlite_data_to_numpy_array_func) # Converts TEXT to np.array when selecting
    conn = sqlite3.connect(dupe_detection_image_fingerprint_database_file_path,detect_types=sqlite3.PARSE_DECLTYPES)
    c = conn.cursor()
    model_1_tsne_coordinates_query_results = c.execute("""SELECT tsne_x_coordinate, tsne_y_coordinate FROM tsne_coordinates_table_model_1 where sha256_hash_of_art_image_file = ? ORDER BY datetime_fingerprint_added_to_database DESC""",[sha256_hash_of_art_image_file,]).fetchall()
    model_2_tsne_coordinates_query_results = c.execute("""SELECT tsne_x_coordinate, tsne_y_coordinate FROM tsne_coordinates_table_model_2 where sha256_hash_of_art_image_file = ? ORDER BY datetime_fingerprint_added_to_database DESC""",[sha256_hash_of_art_image_file,]).fetchall()
    model_3_tsne_coordinates_query_results = c.execute("""SELECT tsne_x_coordinate, tsne_y_coordinate FROM tsne_coordinates_table_model_3 where sha256_hash_of_art_image_file = ? ORDER BY datetime_fingerprint_added_to_database DESC""",[sha256_hash_of_art_image_file,]).fetchall()
    conn.close()
    model_1_tsne_x_coordinate = model_1_tsne_coordinates_query_results[0][0]
    model_1_tsne_y_coordinate = model_1_tsne_coordinates_query_results[0][1]
    model_2_tsne_x_coordinate = model_2_tsne_coordinates_query_results[0][0]
    model_2_tsne_y_coordinate = model_2_tsne_coordinates_query_results[0][1]        
    model_3_tsne_x_coordinate = model_3_tsne_coordinates_query_results[0][0]
    model_3_tsne_y_coordinate = model_3_tsne_coordinates_query_results[0][1]        
    return model_1_tsne_x_coordinate, model_1_tsne_y_coordinate, model_2_tsne_x_coordinate, model_2_tsne_y_coordinate,model_3_tsne_x_coordinate, model_3_tsne_y_coordinate

def calculate_image_similarity_between_two_image_hashes_func(image1_sha256_hash, image2_sha256_hash):
    image1_model_1_tsne_x_coordinate, image1_model_1_tsne_y_coordinate, image1_model_2_tsne_x_coordinate, image1_model_2_tsne_y_coordinate, image1_model_3_tsne_x_coordinate, image1_model_3_tsne_y_coordinate = get_tsne_coordinates_for_desired_image_file_hash_func(image1_sha256_hash)
    image2_model_1_tsne_x_coordinate, image2_model_1_tsne_y_coordinate, image2_model_2_tsne_x_coordinate, image2_model_2_tsne_y_coordinate, image2_model_3_tsne_x_coordinate, image2_model_3_tsne_y_coordinate = get_tsne_coordinates_for_desired_image_file_hash_func(image2_sha256_hash)
    model_1_image_similarity_metric = np.linalg.norm(np.array([image1_model_1_tsne_x_coordinate, image1_model_1_tsne_y_coordinate]) - np.array([image2_model_1_tsne_x_coordinate, image2_model_1_tsne_y_coordinate]))
    model_2_image_similarity_metric = np.linalg.norm(np.array([image1_model_2_tsne_x_coordinate, image1_model_2_tsne_y_coordinate]) - np.array([image2_model_2_tsne_x_coordinate, image2_model_2_tsne_y_coordinate]))
    model_3_image_similarity_metric = np.linalg.norm(np.array([image1_model_3_tsne_x_coordinate, image1_model_3_tsne_y_coordinate]) - np.array([image2_model_3_tsne_x_coordinate, image2_model_3_tsne_y_coordinate]))
    return model_1_image_similarity_metric, model_2_image_similarity_metric, model_3_image_similarity_metric

def find_most_similar_images_to_given_image_from_fingerprint_data_func(sha256_hash_of_art_image_file):
    list_of_model_1_similarity_metrics = []
    list_of_model_2_similarity_metrics = []
    list_of_model_3_similarity_metrics = []
    conn = sqlite3.connect(dupe_detection_image_fingerprint_database_file_path,detect_types=sqlite3.PARSE_DECLTYPES)
    c = conn.cursor()
    dupe_detection_fingerprint_query_results = c.execute("""SELECT sha256_hash_of_art_image_file, model_1_image_fingerprint_vector, model_2_image_fingerprint_vector, model_3_image_fingerprint_vector FROM image_hash_to_image_fingerprint_table ORDER BY datetime_fingerprint_added_to_database DESC""").fetchall()
    conn.close()
    list_of_image_sha256_hashes = [x[0] for x in dupe_detection_fingerprint_query_results]
    pbar = tqdm(total=(len(list_of_image_sha256_hashes)-1))
    print('\nScanning specified image against all image fingerprints in database...')
    for current_image_sha256_hash in list_of_image_sha256_hashes:
        if (current_image_sha256_hash != sha256_hash_of_art_image_file):
            model_1_image_similarity_metric, model_2_image_similarity_metric, model_3_image_similarity_metric = calculate_image_similarity_between_two_image_hashes_func(sha256_hash_of_art_image_file, current_image_sha256_hash)
            list_of_model_1_similarity_metrics.append(model_1_image_similarity_metric)
            list_of_model_2_similarity_metrics.append(model_2_image_similarity_metric)
            list_of_model_3_similarity_metrics.append(model_3_image_similarity_metric)
            pbar.update(1)
    image_similarity_df = pd.DataFrame([list_of_image_sha256_hashes, list_of_model_1_similarity_metrics, list_of_model_2_similarity_metrics, list_of_model_3_similarity_metrics]).T
    image_similarity_df.columns = ['image_sha_256_hash', 'model_1_image_similarity_metric', 'model_2_image_similarity_metric', 'model_3_image_similarity_metric',]
    image_similarity_df_rescaled = image_similarity_df
    image_similarity_df_rescaled['model_1_image_similarity_metric'] = 1 / (image_similarity_df['model_1_image_similarity_metric']/ image_similarity_df['model_1_image_similarity_metric'].max())
    image_similarity_df_rescaled['model_2_image_similarity_metric'] = 1 / (image_similarity_df['model_2_image_similarity_metric']/ image_similarity_df['model_2_image_similarity_metric'].max())
    image_similarity_df_rescaled['model_3_image_similarity_metric'] = 1 / (image_similarity_df['model_3_image_similarity_metric']/ image_similarity_df['model_3_image_similarity_metric'].max())
    image_similarity_df_rescaled['overall_image_similarity_metric'] = (1/3)*(image_similarity_df_rescaled['model_1_image_similarity_metric'] + image_similarity_df_rescaled['model_2_image_similarity_metric'] + image_similarity_df_rescaled['model_3_image_similarity_metric'])
    image_similarity_df_rescaled = image_similarity_df_rescaled.sort_values('overall_image_similarity_metric')
    return image_similarity_df_rescaled

def check_if_image_is_likely_dupe_func(sha256_hash_of_art_image_file):
    global duplicate_image_threshold
    image_similarity_df = find_most_similar_images_to_given_image_from_fingerprint_data_func(sha256_hash_of_art_image_file)
    possible_dupes = image_similarity_df[image_similarity_df['overall_image_similarity_metric'] <= duplicate_image_threshold ]
    if len(possible_dupes) > 0:
        is_dupe = 1
        print('\nWARNING! Art image file appears to be a dupe! Hash of suspected duplicate image file: '+sha256_hash_of_art_image_file)
    else:
        print('\nArt image file appears to be original! (i.e., not a duplicate of an existing image in our image fingerprint database)')
        is_dupe = 0
    return is_dupe

def calculate_artwork_registration_fee_func(combined_size_of_artwork_image_files_in_megabytes):
    global registration_fee_anime_per_megabyte_of_images_pre_difficulty_adjustment
    global forfeitable_deposit_to_initiate_registration_as_percentage_of_adjusted_registration_fee
    global target_number_of_nodes_per_unique_block_hash
    global target_block_redundancy_factor
    avg_anime_mining_difficulty_rate_first_20k_blocks = get_avg_anime_mining_difficulty_rate_first_20k_blocks_func()
    current_anime_mining_difficulty_rate = get_current_anime_mining_difficulty_func()
    anime_mining_difficulty_adjustment_ratio = float(current_anime_mining_difficulty_rate) / float(avg_anime_mining_difficulty_rate_first_20k_blocks)
    registration_fee_in_anime_pre_difficulty_adjustment = round(float(registration_fee_anime_per_megabyte_of_images_pre_difficulty_adjustment)*float(combined_size_of_artwork_image_files_in_megabytes)*float(target_number_of_nodes_per_unique_block_hash)*float(target_block_redundancy_factor))
    registration_fee_in_anime_post_difficulty_adjustment = round(float(registration_fee_in_anime_pre_difficulty_adjustment) / float(anime_mining_difficulty_adjustment_ratio))
    artists_forfeitable_deposit_in_animecoin_terms_to_initiate_registration = round(float(registration_fee_in_anime_post_difficulty_adjustment)*float(forfeitable_deposit_to_initiate_registration_as_percentage_of_adjusted_registration_fee))
    return registration_fee_in_anime_post_difficulty_adjustment, registration_fee_in_anime_pre_difficulty_adjustment, anime_mining_difficulty_adjustment_ratio, artists_forfeitable_deposit_in_animecoin_terms_to_initiate_registration
        
def resize_image_file_func(path_to_image_file, width_in_pixels, height_in_pixals):
    global temp_files_folder_path
    with open(path_to_image_file, 'rb') as f:
        with Image.open(f) as image_data:
            resized_image_data_pil_format = resizeimage.resize_cover(image_data, [width_in_pixels, height_in_pixals])
            resized_image_output_file_path = temp_files_folder_path + 'TempImage__'+get_current_datetime_string_filepath_safe_func()+'.png'
            resized_image_data_pil_format.save(resized_image_output_file_path, image_data.format)
    with open(resized_image_output_file_path, 'rb') as f:
        resized_image_data = f.read()
    return resized_image_data, resized_image_output_file_path

def check_image_file_nsfw_score_func(path_to_image_file):
    global path_to_nsfw_detection_model_file, nsfw_graph_def
    nsfw_model_loaded_already = 'nsfw_graph_def' in globals()
    if not nsfw_model_loaded_already:    
        with tf.gfile.FastGFile(path_to_nsfw_detection_model_file,'rb') as f:# Unpersists graph from file
            nsfw_graph_def = tf.GraphDef()
            nsfw_graph_def.ParseFromString(f.read())
    tf.import_graph_def(nsfw_graph_def, name='')
    image_data = tf.gfile.FastGFile(path_to_image_file, 'rb').read()
    with tf.Session() as sess:
        softmax_tensor = sess.graph.get_tensor_by_name('final_result:0')    # Feed the image_data as input to the graph and get first prediction
        predictions = sess.run(softmax_tensor,  {'DecodeJpeg/contents:0': image_data})
        top_k = predictions[0].argsort()[-len(predictions[0]):][::-1]  # Sort to show labels of first prediction in order of confidence
        for graph_node_id in top_k:
            image_sfw_score = predictions[0][graph_node_id]
    image_nsfw_score = 1 - image_sfw_score
    return image_nsfw_score, nsfw_graph_def

def generate_combined_artwork_image_and_metadata_string_and_hash_func(animecoin_artwork_metadata_ticket_format_version_number,
                                                  date_time_artwork_prepared_by_artist,
                                                  current_bitcoin_block_number,
                                                  current_bitcoin_block_hash,
                                                  current_animecoin_block_number,
                                                  current_animecoin_block_hash,
                                                  artist_animecoin_id_public_key,
                                                  total_number_of_unique_copies_of_artwork_to_create,
                                                  artwork_title,
                                                  artist_name,
                                                  artwork_series_name,
                                                  artist_website,
                                                  artist_statement,
                                                  combined_image_size_in_mb,
                                                  artwork_concatenated_image_hashes,
                                                  current_animecoin_mining_difficulty_rate,
                                                  average_animecoin_mining_difficulty_rate_first_20k_blocks,
                                                  animecoin_mining_difficulty_adjustment_ratio,
                                                  required_registration_fee_in_animecoin_terms_pre_difficulty_adjustment,
                                                  required_registration_fee_in_animecoin_terms_post_difficulty_adjustment,
                                                  artists_forfeitable_deposit_in_animecoin_terms_to_initiate_registration):
    if not isinstance(artwork_concatenated_image_hashes, str):
        artwork_concatenated_image_hashes = artwork_concatenated_image_hashes.decode('utf-8')
    list_of_metadata_fields = [animecoin_artwork_metadata_ticket_format_version_number, date_time_artwork_prepared_by_artist, current_bitcoin_block_number, current_bitcoin_block_hash, current_animecoin_block_number, current_animecoin_block_hash, \
                                artist_animecoin_id_public_key, str(total_number_of_unique_copies_of_artwork_to_create), artwork_title, artist_name, artwork_series_name, artist_website, artist_statement, combined_image_size_in_mb,  \
                                artwork_concatenated_image_hashes, str(current_animecoin_mining_difficulty_rate), str(average_animecoin_mining_difficulty_rate_first_20k_blocks), str(animecoin_mining_difficulty_adjustment_ratio), \
                                str(required_registration_fee_in_animecoin_terms_pre_difficulty_adjustment), str(required_registration_fee_in_animecoin_terms_post_difficulty_adjustment), str(artists_forfeitable_deposit_in_animecoin_terms_to_initiate_registration)]
    combined_artwork_image_and_metadata_string_list = []
    for current_metadata_field in list_of_metadata_fields:
        combined_artwork_image_and_metadata_string_list.append(current_metadata_field + ' || ')
    combined_artwork_image_and_metadata_string = concatenate_list_of_strings_func(combined_artwork_image_and_metadata_string_list)
    combined_artwork_image_and_metadata_string = ' '.join(combined_artwork_image_and_metadata_string.split())
    combined_artwork_image_and_metadata_hash = get_sha256_hash_of_input_data_func(combined_artwork_image_and_metadata_string)      
    return combined_artwork_image_and_metadata_string, combined_artwork_image_and_metadata_hash

class fake_artwork_metadata_class(object):
       def __init__(self):
        global dupe_detection_model_1_name, dupe_detection_model_2_name, dupe_detection_model_3_name, path_where_animecoin_html_ticket_templates_are_stored
        _, _, _, _, example_animecoin_public_id_public_key, example_animecoin_public_id_private_key = get_example_keypair_of_rsa_public_and_private_keys_func()
        _, _, _, _, example_animecoin_public_id_public_key_pem_format, animecoin_public_id_private_key_pem_format = get_example_keypair_of_rsa_public_and_private_keys_pem_format_func()
        root_animecoin_folder_path = get_animecoin_root_directory_func()
        folder_path_of_art_folders_to_encode = os.path.join(root_animecoin_folder_path,'art_folders_to_encode' + os.sep)
        example_list_of_art_folder_paths = glob.glob(folder_path_of_art_folders_to_encode+'*/')
        selected_art_folder_path = random.choice(example_list_of_art_folder_paths)
        animecoin_artwork_metadata_ticket_format_version_number = get_current_animecoin_metadata_format_version_number_func()
        date_time_artwork_prepared_by_artist = generate_recent_random_datetime_string_func()
        current_bitcoin_block_number, current_bitcoin_block_hash = check_current_bitcoin_blockchain_stats_func()
        current_animecoin_block_number, current_animecoin_block_hash = check_current_animecoin_blockchain_stats_func()
        artist_animecoin_id_public_key = example_animecoin_public_id_public_key_pem_format
        artist_receiving_blockchain_address = generate_new_animecoin_blockchain_address_func()
        total_number_of_unique_copies_of_artwork_to_create = random.choice([25,50,50,75,100,100,100,150,200,250,300,500,500,1000,1000,1000])
        artwork_title = generate_fake_artwork_title_func()
        artist_name = random.choice(['Arturo Lopez', 'Iris Rose Madulid', 'Irvin Ryan Tiu', 'Midori Matsui', 'Tiffany Lei', 'Willdhelyn Radaza', 'Mark Kong', 'Chyi Ming Lee', 'Hayo Sena', 'Dexter Buenaluz', 'Hikaru Matsui'])
        artwork_series_name = random.choice(['Real Life', 'The Dawning of an Era', 'Great Masters Series', 'In the Hospital', 'What Dreams May Come', '', '', '', '', '', '', '', '', '', '', '', '', '', '', ''])
        artist_website = random.choice(['http://www.coolanime.com', 'www.animeawesome.com', 'heroanime.io', 'https://villainanime.org', 'https://engelszorn.deviantart.com/', 'https://foxykuro.deviantart.com/', '', '', '', '', ''])
        artist_statement = random.choice(['This series is about how Vampires could exist all around us in our daily lives, without us even knowing it! I have also been exploring the themes of healthcare and sickness as I deal with my own personal battle with Groat\'s Disease.', 'My art is a reflection of my life: filled with passion, but never satisfied!', 'lol idk what 2 write here', 'For more details on my work, please visit my instagram page or follow me on twitter! #ANIMECOINROCKS','I have been drawing since I was young! I hope everyone likes my newest works, I have been learning how to use a tablet! Hit me up on discord, username is AnimeFreak15! Thx!!!'])
        artwork_creation_video_youtube_url = random.choice(['https://www.youtube.com/watch?v=IfFVZdeQEoc', 'https://www.youtube.com/watch?v=5pxpwP_TJuA', 'https://www.youtube.com/watch?v=TEAzw3OK9H8', 'https://www.youtube.com/watch?v=kF--9cyKjhc'])
        artwork_concatenated_image_hashes, combined_image_size_in_mb = get_concatenated_art_image_file_hashes_and_total_size_in_mb_func(selected_art_folder_path)
        artwork_concatenated_image_hashes = artwork_concatenated_image_hashes.decode('utf-8')
        current_animecoin_mining_difficulty_rate = get_current_anime_mining_difficulty_func()
        average_animecoin_mining_difficulty_rate_first_20k_blocks = get_avg_anime_mining_difficulty_rate_first_20k_blocks_func()
        required_registration_fee_in_animecoin_terms_post_difficulty_adjustment, required_registration_fee_in_animecoin_terms_pre_difficulty_adjustment, animecoin_mining_difficulty_adjustment_ratio, artists_forfeitable_deposit_in_animecoin_terms_to_initiate_registration = calculate_artwork_registration_fee_func(combined_image_size_in_mb)
        combined_artwork_image_and_metadata_string, combined_artwork_image_and_metadata_hash = generate_combined_artwork_image_and_metadata_string_and_hash_func(animecoin_artwork_metadata_ticket_format_version_number, date_time_artwork_prepared_by_artist, current_bitcoin_block_number, current_bitcoin_block_hash, current_animecoin_block_number, current_animecoin_block_hash, artist_animecoin_id_public_key, total_number_of_unique_copies_of_artwork_to_create, artwork_title, artist_name, artwork_series_name, artist_website, artist_statement, combined_image_size_in_mb, artwork_concatenated_image_hashes, current_animecoin_mining_difficulty_rate, average_animecoin_mining_difficulty_rate_first_20k_blocks, animecoin_mining_difficulty_adjustment_ratio, required_registration_fee_in_animecoin_terms_pre_difficulty_adjustment, required_registration_fee_in_animecoin_terms_post_difficulty_adjustment, artists_forfeitable_deposit_in_animecoin_terms_to_initiate_registration)
        artist_signature_on_combined_artwork_image_and_metadata_string = sign_data_with_private_key_func(combined_artwork_image_and_metadata_string, example_animecoin_public_id_private_key)
        registering_masternode_animecoin_collateral_blockchain_address, registering_masternode_animecoin_collateral_public_key, registering_masternode_animecoin_collateral_private_key = get_my_masternode_collateral_address_and_key_pair_func()
        registering_masternode_collateral_signature_on_combined_artwork_image_and_metadata_string = sign_data_with_private_key_func(combined_artwork_image_and_metadata_string, registering_masternode_animecoin_collateral_private_key)
        _, registering_masternode_animecoin_id_private_key, _ = get_local_masternode_animecoin_id_keypair_func()
        registering_masternode_animecoin_id_signature_on_combined_artwork_image_and_metadata_string = sign_data_with_private_key_func(combined_artwork_image_and_metadata_string, registering_masternode_animecoin_id_private_key)
        registering_masternode_animecoin_blockchain_address_for_receiving_registration_fee = generate_new_animecoin_blockchain_address_func()
        date_time_registering_masternode_processed_artwork = get_current_datetime_string_func()
        path_to_folder_containing_files_for_compression_dictionary = path_where_animecoin_html_ticket_templates_are_stored
        animecoin_zstd_compression_dictionary = generate_zstd_dictionary_from_folder_path_and_file_matching_string_func(path_to_folder_containing_files_for_compression_dictionary, '*_template.html')
        animecoin_zstd_compression_dictionary_raw_data = animecoin_zstd_compression_dictionary.as_bytes()
        artwork_metadata_ticket_animecoin_blockchain_storage_address = generate_new_animecoin_blockchain_address_func()
        animecoin_blockchain_file_storage_compression_dictionary_animecoin_blockchain_storage_address = generate_new_animecoin_blockchain_address_func()
        animecoin_blockchain_file_storage_compression_dictionary_file_hash = get_sha256_hash_of_input_data_func(animecoin_zstd_compression_dictionary_raw_data)
        _, _, registering_masternode_animecoin_id_public_key_pem_format = get_local_masternode_animecoin_id_keypair_func()
        registering_masternode_ip_address = get_my_local_ip_func()
        nsfw_detection_model_file_hash = get_nsfw_model_file_hash_func()
        self.animecoin_ticket_data_object_type = 'artwork_metadata_ticket'
        self.animecoin_artwork_metadata_ticket_format_version_number = animecoin_artwork_metadata_ticket_format_version_number
        self.date_time_artwork_prepared_by_artist = date_time_artwork_prepared_by_artist
        self.current_bitcoin_block_number = current_bitcoin_block_number
        self.current_bitcoin_block_hash = current_bitcoin_block_hash
        self.current_animecoin_block_number = current_animecoin_block_number
        self.current_animecoin_block_hash = current_animecoin_block_hash
        self.artist_animecoin_id_public_key = artist_animecoin_id_public_key
        self.artist_receiving_blockchain_address = artist_receiving_blockchain_address
        self.total_number_of_unique_copies_of_artwork_to_create = total_number_of_unique_copies_of_artwork_to_create
        self.artwork_title = artwork_title
        self.artist_name = artist_name
        self.artwork_series_name = artwork_series_name
        self.artwork_creation_video_youtube_url = artwork_creation_video_youtube_url
        self.artist_website = artist_website
        self.artist_statement = artist_statement
        self.combined_image_size_in_mb = combined_image_size_in_mb
        self.artwork_concatenated_image_hashes = artwork_concatenated_image_hashes
        self.nsfw_detection_model_file_hash = nsfw_detection_model_file_hash
        self.dupe_detection_image_fingerprinting_model_name_1_of_3 = dupe_detection_model_1_name
        self.dupe_detection_image_fingerprinting_model_name_2_of_3 = dupe_detection_model_2_name
        self.dupe_detection_image_fingerprinting_model_name_3_of_3 = dupe_detection_model_3_name
        self.current_animecoin_mining_difficulty_rate = current_animecoin_mining_difficulty_rate
        self.average_animecoin_mining_difficulty_rate_first_20k_blocks = average_animecoin_mining_difficulty_rate_first_20k_blocks
        self.animecoin_mining_difficulty_adjustment_ratio = animecoin_mining_difficulty_adjustment_ratio
        self.required_registration_fee_in_animecoin_terms_pre_difficulty_adjustment = required_registration_fee_in_animecoin_terms_pre_difficulty_adjustment
        self.required_registration_fee_in_animecoin_terms_post_difficulty_adjustment = required_registration_fee_in_animecoin_terms_post_difficulty_adjustment
        self.artists_forfeitable_deposit_in_animecoin_terms_to_initiate_registration = artists_forfeitable_deposit_in_animecoin_terms_to_initiate_registration
        self.combined_artwork_image_and_metadata_string = combined_artwork_image_and_metadata_string
        self.combined_artwork_image_and_metadata_hash = combined_artwork_image_and_metadata_hash
        self.artist_signature_on_combined_artwork_image_and_metadata_string = artist_signature_on_combined_artwork_image_and_metadata_string
        self.registering_masternode_animecoin_collateral_blockchain_address = registering_masternode_animecoin_collateral_blockchain_address
        self.registering_masternode_animecoin_id_public_key = registering_masternode_animecoin_id_public_key_pem_format
        self.registering_masternode_ip_address = registering_masternode_ip_address
        self.registering_masternode_collateral_signature_on_combined_artwork_image_and_metadata_string = registering_masternode_collateral_signature_on_combined_artwork_image_and_metadata_string
        self.registering_masternode_animecoin_id_signature_on_combined_artwork_image_and_metadata_string = registering_masternode_animecoin_id_signature_on_combined_artwork_image_and_metadata_string
        self.registering_masternode_animecoin_blockchain_address_for_receiving_registration_fee = registering_masternode_animecoin_blockchain_address_for_receiving_registration_fee
        self.date_time_registering_masternode_processed_artwork = date_time_registering_masternode_processed_artwork
        self.artwork_metadata_ticket_animecoin_blockchain_storage_address = artwork_metadata_ticket_animecoin_blockchain_storage_address
        self.animecoin_blockchain_file_storage_compression_dictionary_animecoin_blockchain_storage_address = animecoin_blockchain_file_storage_compression_dictionary_animecoin_blockchain_storage_address
        self.animecoin_blockchain_file_storage_compression_dictionary_file_hash = animecoin_blockchain_file_storage_compression_dictionary_file_hash
        self.selected_art_folder_path = selected_art_folder_path

def get_animecoin_html_ticket_template_string_func(animecoin_html_ticket_type_string):
    global path_where_animecoin_html_ticket_templates_are_stored
    glob_pattern_matching_string = path_where_animecoin_html_ticket_templates_are_stored+'*'+animecoin_html_ticket_type_string+'*.html'
    glob_result = glob.glob(glob_pattern_matching_string)
    if len(glob_result) == 1:
        path_to_animecoin_html_ticket_file = glob_result[0]
        with open(path_to_animecoin_html_ticket_file,'r') as f:
            animecoin_html_ticket_template_string = f.read()
        return animecoin_html_ticket_template_string
    else:
        print('Error, can\'nt find specified template file!')
        
def load_animecoin_html_ticket_file_func(path_to_animecoin_html_ticket_file):
    with open(path_to_animecoin_html_ticket_file,'r') as f:
       animecoin_ticket_html_string = f.read()
    return animecoin_ticket_html_string

def generate_new_animecoin_html_ticket_data_object_func(animecoin_html_ticket_type_string):
    if animecoin_html_ticket_type_string == 'artwork_metadata_ticket':
        animecoin_ticket_data_object = fake_artwork_metadata_class()
    elif animecoin_html_ticket_type_string == 'trade_ticket':
        animecoin_ticket_data_object = fake_artwork_metadata_class() #Todo: write similar classes for these tickets
    elif animecoin_html_ticket_type_string == 'treasury_fund_payment_request_voting_ticket':
        animecoin_ticket_data_object = fake_artwork_metadata_class() #Todo: write similar classes for these tickets        
    elif animecoin_html_ticket_type_string == 'offending_content_takedown_request_voting_ticket':
        animecoin_ticket_data_object = fake_artwork_metadata_class() #Todo: write similar classes for these tickets       
    return animecoin_ticket_data_object

def filter_list_of_string_for_matching_string_pattern_func(input_list_of_strings, text_pattern_matching_string):
    if (text_pattern_matching_string[0] == '!'):
        pattern = text_pattern_matching_string[1:]
        filtered_list_of_strings = []
        for current_input_string in input_list_of_strings:
            if not fnmatch.fnmatch(current_input_string, pattern):
                filtered_list_of_strings.append(current_input_string)
    else:
        filtered_list_of_strings = fnmatch.filter(input_list_of_strings, text_pattern_matching_string)
    return filtered_list_of_strings

def fill_in_new_animecoin_html_ticket_data_object_func(animecoin_ticket_data_object):
    animecoin_html_ticket_type_string = getattr(animecoin_ticket_data_object, 'animecoin_ticket_data_object_type')
    selected_art_folder_path = getattr(animecoin_ticket_data_object, 'selected_art_folder_path')
    animecoin_html_ticket_template_html_string = get_animecoin_html_ticket_template_string_func(animecoin_html_ticket_type_string)
    animecoin_html_ticket_template_html_string_parsed = lxml.html.fromstring(animecoin_html_ticket_template_html_string)
    parsed_ticket_data = animecoin_html_ticket_template_html_string_parsed.xpath('//td')
    parsed_ticket_field_names_list = [x.text for x in parsed_ticket_data]
    text_pattern_matching_string_1 = '!validation_type__*'
    parsed_ticket_field_names_list_filtered = filter_list_of_string_for_matching_string_pattern_func(parsed_ticket_field_names_list, text_pattern_matching_string_1)
    text_pattern_matching_string_2 = '!*confirming_mn_*'
    parsed_ticket_field_names_list_filtered_again = filter_list_of_string_for_matching_string_pattern_func(parsed_ticket_field_names_list_filtered, text_pattern_matching_string_2)
    parsed_ticket_field_names_list_filtered_again = parsed_ticket_field_names_list_filtered_again[:-5] #Leave out the image related fields.
    text_pattern_matching_string_3 = 'VALIDATION_TYPE__*'
    validation_type_fields_original = filter_list_of_string_for_matching_string_pattern_func(parsed_ticket_field_names_list, text_pattern_matching_string_3)
    validation_type_fields_clean = [x.replace('VALIDATION_TYPE__','').lower() for x in validation_type_fields_original]
    parsed_ticket_field_names_list_filtered_again_processed = [x.replace('PLACEHOLDER_','').lower() for x in parsed_ticket_field_names_list_filtered_again]
    animecoin_html_ticket_data_field_values = [getattr(animecoin_ticket_data_object, ticket_field_name) for ticket_field_name in parsed_ticket_field_names_list_filtered_again_processed]
    if len(parsed_ticket_field_names_list_filtered_again) == len(animecoin_html_ticket_data_field_values):
        filled_in_animecoin_html_ticket_html_string = animecoin_html_ticket_template_html_string
        for field_count, current_template_field_data_placeholder_string in enumerate(parsed_ticket_field_names_list_filtered_again):
            current_template_field_data_value = animecoin_html_ticket_data_field_values[field_count]
            filled_in_animecoin_html_ticket_html_string = filled_in_animecoin_html_ticket_html_string.replace(current_template_field_data_placeholder_string, str(current_template_field_data_value))
    if len(validation_type_fields_original) == len(validation_type_fields_clean):
        for field_count, current_validation_type_field_clean in enumerate(validation_type_fields_clean):
            current_validation_type_field_original = validation_type_fields_original[field_count]
            filled_in_animecoin_html_ticket_html_string = filled_in_animecoin_html_ticket_html_string.replace(current_validation_type_field_original, current_validation_type_field_clean)
        animecoin_ticket_output_file_path = os.path.join(selected_art_folder_path, animecoin_html_ticket_type_string + '__'+ get_current_datetime_string_filepath_safe_func() + '.html')
        try:
            existing_ticket_file_paths = glob.glob(selected_art_folder_path + animecoin_html_ticket_type_string + '*.html')
            if len(existing_ticket_file_paths):
                print('Found existing html ticket files of the same type! Removing these first...')
                for current_existing_ticket_file_path in existing_ticket_file_paths:
                    try:
                        os.remove(current_existing_ticket_file_path)
                        print('Removed existing ticket file: ' + current_existing_ticket_file_path)
                    except Exception as e:
                        print('Error: '+ str(e))
            with open(animecoin_ticket_output_file_path,'w') as f:
                f.write(filled_in_animecoin_html_ticket_html_string)
                print('Successfully wrote filled-in html ticket file to ' + animecoin_ticket_output_file_path)
        except Exception as e:
            print('Error: '+ str(e))
        return filled_in_animecoin_html_ticket_html_string, selected_art_folder_path, animecoin_ticket_output_file_path
    else:
        print('Error! The number of placeholder fields does NOT match the number of available field values!')

def add_image_files_to_metadata_html_table_string_func(animecoin_metadata_html_string, animecoin_ticket_data_object, animecoin_ticket_output_file_path=0):
    global nsfw_score_threshold
    max_image_preview_pixel_dimension = 600
    image_file_html_template_string = '<tr> <th valign="top"; align="left";><img src="ANIME_IMAGE_FILENAME" alt="ANIME_IMAGE_HASH" style="width:ANIME_IMAGE_PIXEL_WIDTHpx; height:ANIME_IMAGE_PIXEL_HEIGHTpx;"></th> <td valign="top";>ANIME_IMAGE_HASH</td> <td valign="top";>ANIME_IMAGE_NSFW_SCORE</td>  <td valign="top"; style="font-size: 1.5px;"> ANIME_IMAGE_DUPE_FINGERPRINT_MODEL_1 </td> <td valign="top"; style="font-size: 1.5px;"> ANIME_IMAGE_DUPE_FINGERPRINT_MODEL_2</td> <td valign="top"; style="font-size: 1.5px;"> ANIME_IMAGE_DUPE_FINGERPRINT_MODEL_3</td> </tr>'
    new_html_lines = []
    selected_art_folder = animecoin_ticket_data_object.selected_art_folder_path
    list_of_paths_to_art_image_files = get_all_valid_image_file_paths_in_folder_func(selected_art_folder)
    for current_image_file_path in list_of_paths_to_art_image_files:
        current_image_file_name = current_image_file_path.split(os.sep)[-1]
        current_loaded_image_data = Image.open(current_image_file_path)
        current_image_pixel_width, current_image_image_pixel_height = current_loaded_image_data.size
        rescaling_factor = max_image_preview_pixel_dimension / max([current_image_pixel_width, current_image_image_pixel_height])
        preview_image_pixel_width = round(current_image_pixel_width*rescaling_factor)
        preview_image_pixel_height = round(current_image_image_pixel_height*rescaling_factor)
        print('Current Image File: '+current_image_file_name)
        print('Checking image NSFW score...')
        resized_image_data, resized_image_file_path = resize_image_file_func(current_image_file_path, 4*preview_image_pixel_width, 4*preview_image_pixel_height)
        current_image_nsfw_score, nsfw_graph_def = check_image_file_nsfw_score_func(resized_image_file_path)
        if current_image_nsfw_score > nsfw_score_threshold:
            print('Warning: Current image is NSFW, renaming file to end in ".NSFW" and skipping to next image file.')
            old_file_extension = os.path.split(current_image_file_path)[-1].split('.')[-1]
            new_file_path = current_image_file_path.replace('.'+old_file_extension,'.NSFW')
            try:
                os.rename(current_image_file_path, new_file_path)
            except Exception as e:
                print('Error: '+ str(e))
            continue
        print('Checking if image is a duplicate...')
        current_model_1_image_fingerprint_vector, current_model_2_image_fingerprint_vector, current_model_3_image_fingerprint_vector, \
        sha256_hash_of_current_art_image_file, dupe_detection_model_1, dupe_detection_model_2, dupe_detection_model_3 = add_image_fingerprints_to_dupe_detection_database_func(current_image_file_path)
        current_model_1_image_fingerprint_vector_string = '['+ ', '.join([str(x) for x in current_model_1_image_fingerprint_vector]) + ']'
        current_model_2_image_fingerprint_vector_string = '['+ ', '.join([str(x) for x in current_model_2_image_fingerprint_vector]) + ']'
        current_model_3_image_fingerprint_vector_string = '['+ ', '.join([str(x) for x in current_model_3_image_fingerprint_vector]) + ']'
        current_image_file_sha256hash = get_image_hash_from_image_file_path_func(current_image_file_path)
        print('Got image fingerprints. Now computing similarity to images in fingerprint database...')
        list_of_image_sha256_hashes, model_1_tsne_x_coordinates, model_1_tsne_y_coordinates, model_2_tsne_x_coordinates, \
        model_2_tsne_y_coordinates, model_3_tsne_x_coordinates, model_3_tsne_y_coordinates = apply_tsne_to_image_fingerprint_database_func()
        print('Done!')
        current_image_file_sha256hash in list_of_image_sha256_hashes
        image_is_likely_dupe = check_if_image_is_likely_dupe_func(current_image_file_sha256hash)
        if image_is_likely_dupe:
            print('Warning: Current image is a likely dupe, renaming file to end in ".DUPE" and skipping to next image file.')
            old_file_extension = os.path.split(current_image_file_path)[-1].split('.')[-1]
            new_file_path = current_image_file_path.replace('.'+old_file_extension,'.DUPE')
            try:
                os.rename(current_image_file_path, new_file_path)
            except Exception as e:
                print('Error: '+ str(e))
            continue
        x = image_file_html_template_string
        x = x.replace('ANIME_IMAGE_FILENAME', './'+current_image_file_name)
        x = x.replace('ANIME_IMAGE_HASH', sha256_hash_of_current_art_image_file)
        x = x.replace('ANIME_IMAGE_PIXEL_WIDTH', str(preview_image_pixel_width))
        x = x.replace('ANIME_IMAGE_PIXEL_HEIGHT', str(preview_image_pixel_height))
        x = x.replace('ANIME_IMAGE_NSFW_SCORE', str(current_image_nsfw_score))
        x = x.replace('ANIME_IMAGE_DUPE_FINGERPRINT_MODEL_1', current_model_1_image_fingerprint_vector_string)
        x = x.replace('ANIME_IMAGE_DUPE_FINGERPRINT_MODEL_2', current_model_2_image_fingerprint_vector_string)
        x = x.replace('ANIME_IMAGE_DUPE_FINGERPRINT_MODEL_3', current_model_3_image_fingerprint_vector_string)
        modified_image_file_html_template_string = x
        new_html_lines.append(modified_image_file_html_template_string)
    final_replacement_string = concatenate_list_of_strings_func(new_html_lines)
    updated_animecoin_metadata_html_string = animecoin_metadata_html_string.replace('IMAGE_FILEPATHS_AND_HASH_DATA_AND_MORE', final_replacement_string)
    if not animecoin_ticket_output_file_path == 0:
        try:
            with open(animecoin_ticket_output_file_path,'w') as f:
                f.write(updated_animecoin_metadata_html_string)
                print('Successfully added image data to filled-in html ticket file: ' + animecoin_ticket_output_file_path)
        except Exception as e:
            print('Error: '+ str(e))
    return updated_animecoin_metadata_html_string, dupe_detection_model_1, dupe_detection_model_2, dupe_detection_model_3, nsfw_graph_def

#Script: (required to store numpy arrays in SQlite)
sqlite3.register_adapter(np.ndarray, convert_numpy_array_to_sqlite_func) # Converts np.array to TEXT when inserting
sqlite3.register_converter('array', convert_sqlite_data_to_numpy_array_func) # Converts TEXT to np.array when selecting

if 0:
    artwork_metadata_object = generate_new_animecoin_html_ticket_data_object_func('artwork_metadata_ticket')
    filled_in_animecoin_html_ticket_html_string, selected_art_folder_path, animecoin_ticket_output_file_path = fill_in_new_animecoin_html_ticket_data_object_func(artwork_metadata_object)
    updated_animecoin_metadata_html_string, dupe_detection_model_1, dupe_detection_model_2, dupe_detection_model_3, nsfw_graph_def = add_image_files_to_metadata_html_table_string_func(filled_in_animecoin_html_ticket_html_string, artwork_metadata_object, animecoin_ticket_output_file_path )


if 0: #For debugging metadata functions:
    selected_art_folder = 'C:\\animecoin\\art_folders_to_encode\\Arturo_Lopez__Number_02\\'
    animecoin_ticket_data_object = artwork_metadata_object
    animecoin_metadata_html_string = filled_in_animecoin_html_ticket_html_string
    selected_art_folder = animecoin_ticket_data_object.selected_art_folder_path
    list_of_paths_to_art_image_files = get_all_valid_image_file_paths_in_folder_func(selected_art_folder)
    current_image_file_path = list_of_paths_to_art_image_files[0]
    sha256_hash_of_art_image_file = get_image_hash_from_image_file_path_func(path_to_art_image_file)
    
if 0: #Don't use unless the Dupe Detection database has problems:
    generate_and_save_example_rsa_keypair_files_func()
    regenerate_dupe_detection_image_fingerprint_database_func()
    anime_image_database_folder = 'C:\\Users\\jeffr\\Cointel Dropbox\\Animecoin_Code\\dupe_detection_image_files_for_db\\'
    dupe_detection_model_1, dupe_detection_model_2, dupe_detection_model_3 = add_all_images_in_folder_to_image_fingerprint_database_func(anime_image_database_folder)
