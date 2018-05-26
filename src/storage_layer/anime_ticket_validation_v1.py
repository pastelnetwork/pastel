import os.path, glob, hashlib, os, sqlite3, base64, shutil
import lxml, lxml.html
from zipfile import ZipFile
from shutil import copyfile
from datetime import datetime, timedelta
from PIL import Image
import pandas as pd
from bs4 import BeautifulSoup
from anime_utility_functions_v1 import get_sha256_hash_of_input_data_func, sign_data_with_private_key_func, verify_signature_on_data_func, generate_and_save_example_rsa_keypair_files_func, clean_up_art_folder_after_registration_func, \
    get_local_masternode_animecoin_id_keypair_func, get_all_valid_image_file_paths_in_folder_func, check_current_bitcoin_blockchain_stats_func, get_my_local_ip_func, remove_existing_remaining_zip_files_func, get_all_animecoin_parameters_func, \
    get_all_animecoin_directories_func, get_pem_format_of_rsa_key_func, get_example_keypair_of_rsa_public_and_private_keys_func, get_example_keypair_of_rsa_public_and_private_keys_pem_format_func, get_current_anime_mining_difficulty_func, \
    get_avg_anime_mining_difficulty_rate_first_20k_blocks_func, get_image_hash_from_image_file_path_func, concatenate_list_of_strings_func, get_metadata_file_path_from_art_folder_func, get_current_datetime_string_func, parse_datetime_string_func, \
    validate_list_of_text_fields_for_html_metadata_table_func, check_if_anime_blockchain_address_is_valid_func, validate_list_of_anime_blockchain_addresses, validate_url_func, check_if_ip_address_responds_to_pings_func, \
    calculate_artwork_registration_fee_func, generate_fake_artwork_title_func, generate_new_animecoin_blockchain_address_func, fake_artwork_metadata_class, \
    get_concatenated_art_image_file_hashes_and_total_size_in_mb_func, get_image_deep_learning_features_func, parse_html_table_func, get_animecoin_html_ticket_template_string_func
from anime_fountain_coding_v1 import encode_final_art_zipfile_into_luby_transform_blocks_func
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




###############################################################################################################
# Functions:
###############################################################################################################

def is_number_func(s):
    try:
        float(s)
        return True
    except ValueError:
        return False
    
class generic_animecoin_html_ticket_data_object_class(object):
      def __init__(self):
          self.animecoin_ticket_data_object_type = ''

def get_animecoin_html_ticket_type_func(animecoin_ticket_df):
    ticket_format_string = animecoin_ticket_df.iloc[0,0]
    if 'Animecoin Metadata Ticket' in ticket_format_string:
        ticket_type_string = 'artwork_metadata_ticket'
    elif 'Animecoin Trade Ticket' in ticket_format_string:
        ticket_type_string = 'trade_ticket'   
    elif 'Animecoin Treasury Fund Payment Request Voting Ticket' in ticket_format_string:
        ticket_type_string = 'treasury_fund_payment_request_voting_ticket'
    elif 'Animecoin Offending Content Takedown Request Voting Ticket' in ticket_format_string:
        ticket_type_string = 'offending_content_takedown_request_voting_ticket'
    return ticket_type_string

def validate_ticket_data_func(field_validation_type_string, field_value):
    global earliest_possible_artwork_signing_date
    latest_possible_artwork_signing_date = datetime.now() + timedelta(hours=24)
    is_valid = 0
    if field_validation_type_string == 'animecoin_artwork_metadata_ticket_format_version_number':
        if (len(field_value) == 4) and ('.' in field_value):
            is_valid = 1
    elif field_validation_type_string == 'datetime_string__%y-%m-%d %h:%m:%s:%f__artwork_prepared_by_artist':
        if (field_value > earliest_possible_artwork_signing_date) and (field_value < latest_possible_artwork_signing_date):
           is_valid = 1
    elif field_validation_type_string == 'bitcoin_block_number':
        if is_number_func(field_value):
            if float(field_value) >= 524412:
                is_valid = 1
    elif field_validation_type_string ==  'bitcoin_block_hash':
        if (len(field_value) == 64) and (field_value[:9]=='0000000000'):
            is_valid = 1
    elif field_validation_type_string ==  'animecoin_block_number':
        if is_number_func(field_value):
            is_valid = 1
    elif field_validation_type_string ==  'animecoin_block_hash':
        if (len(field_value) == 64) and (field_value[:2]=='00'):
            is_valid = 1    
    return is_valid
#    elif field_validation_type_string ==  'animecoin_id_public_key':
#    elif field_validation_type_string ==  'animecoin_blockchain_address':
#    elif field_validation_type_string ==  'animecoin_total_number_of_unique_copies_of_artwork_to_create':
#    elif field_validation_type_string ==  'artwork_title':
#    elif field_validation_type_string ==  'artist_name':
#    elif field_validation_type_string ==  'artwork_series_name':
#    elif field_validation_type_string ==  'artwork_creation_video_youtube_url':
#    elif field_validation_type_string ==  'url':
#    elif field_validation_type_string ==  'artist_statement':
#    elif field_validation_type_string ==  'file_size__mb':
#    elif field_validation_type_string ==  'artwork_concatenated_image_hashes':
#    elif field_validation_type_string ==  'sha256_file_hash':
#    elif field_validation_type_string ==  'dupe_detection_image_fingerprinting_model_name':
#    elif field_validation_type_string ==  'animecoin_mining_difficulty_rate':
#    elif field_validation_type_string ==  'numerical_ratio':
#    elif field_validation_type_string ==  'registration_fee':
#    elif field_validation_type_string ==  'forfeitable_deposit':
#    elif field_validation_type_string ==  'combined_artwork_image_and_metadata_string':
#    elif field_validation_type_string ==  'artist_signature_on_combined_artwork_image_and_metadata_string':
#    elif field_validation_type_string ==  'masternode_collateral_blockchain_address':
#    elif field_validation_type_string ==  'masternode_ip_address':
#    elif field_validation_type_string ==  'masternode_collateral_signature_on_combined_artwork_image_and_metadata_string':
#    elif field_validation_type_string ==  'bitcoin_block_hash':
#    elif field_validation_type_string ==  'animecoin_block_number':
#    elif field_validation_type_string ==  'animecoin_block_hash':
#    elif field_validation_type_string ==  'animecoin_id_public_key':
#    elif field_validation_type_string ==  'animecoin_blockchain_address':
#    elif field_validation_type_string ==  'animecoin_total_number_of_unique_copies_of_artwork_to_create':
#    elif field_validation_type_string ==  'artwork_title':
#    elif field_validation_type_string ==  'artist_name':
#    elif field_validation_type_string ==  'artwork_series_name':
#    elif field_validation_type_string ==  'artwork_creation_video_youtube_url':
#    elif field_validation_type_string ==  'url':
#    elif field_validation_type_string ==  'artist_statement':
#    elif field_validation_type_string ==  'file_size__mb':
#    elif field_validation_type_string ==  'artwork_concatenated_image_hashes':
#    elif field_validation_type_string ==  'sha256_file_hash':
#    elif field_validation_type_string ==  'dupe_detection_image_fingerprinting_model_name':
#    elif field_validation_type_string ==  'animecoin_mining_difficulty_rate':
#    elif field_validation_type_string ==  'numerical_ratio':
#    elif field_validation_type_string ==  'registration_fee':
#    elif field_validation_type_string ==  'forfeitable_deposit':
#    elif field_validation_type_string ==  'combined_artwork_image_and_metadata_string':
#    elif field_validation_type_string ==  'artist_signature_on_combined_artwork_image_and_metadata_string':
#    elif field_validation_type_string ==  'masternode_collateral_blockchain_address':
#    elif field_validation_type_string ==  'masternode_ip_address':
#    elif field_validation_type_string ==  'masternode_collateral_signature_on_combined_artwork_image_and_metadata_string':
#    elif field_validation_type_string ==  'animecoin_id_signature_on_combined_artwork_image_and_metadata_string':
#    elif field_validation_type_string ==  'datetime_string__%y-%m-%d %h:%m:%s:%f__registering_masternode_processed_artwork':
#    elif field_validation_type_string ==  'datetime_string__%y-%m-%d %h:%m:%s:%f__confirming_mn_1_processed_artwork':
#    elif field_validation_type_string ==  'datetime_string__%y-%m-%d %h:%m:%s:%f__confirming_mn_2_processed_artwork':
#    elif field_validation_type_string ==  'animecoin_blockchain_storage_address':
#    elif field_validation_type_string ==  'animecoin_blockchain_file_storage_compression_dictionary_file_hash':
            

path_to_animecoin_html_ticket_file = 'C:\\animecoin\\art_folders_to_encode\\Midori_Matsui__Number_05\\artwork_metadata_ticket__2018_05_25__18_37_51_580510.html'

def parse_animecoin_html_ticket_func(path_to_animecoin_html_ticket_file):
   with open(path_to_animecoin_html_ticket_file,'r') as f:
       animecoin_ticket_html_string = f.read()
   soup = BeautifulSoup(animecoin_ticket_html_string, 'lxml')
   tables = soup.find_all('table')
   animecoin_ticket_df = parse_html_table_func(tables[0])
   animecoin_ticket_addendum_df = parse_html_table_func(tables[1])
   addendum_column_names = animecoin_ticket_addendum_df.columns
   animecoin_ticket_addendum_df = animecoin_ticket_addendum_df.iloc[:,:-1]
   animecoin_ticket_addendum_df.columns = addendum_column_names[1:]
   ticket_type_string = get_animecoin_html_ticket_type_func(animecoin_ticket_df)
   animecoin_html_ticket_template_html_string = get_animecoin_html_ticket_template_string_func(ticket_type_string)
   soup_template = BeautifulSoup(animecoin_html_ticket_template_html_string, 'lxml')
   tables_template = soup_template.find_all('table')
   animecoin_ticket_template_df = parse_html_table_func(tables_template[0])
   list_of_ticket_object_field_names = animecoin_ticket_template_df['Field Value'].tolist()
   list_of_ticket_object_field_names_clean = [x.replace('PLACEHOLDER_','').lower() for x in list_of_ticket_object_field_names]
   list_of_ticket_object_field_values = animecoin_ticket_df['Field Value'].tolist()
   ticket_data_object = generic_animecoin_html_ticket_data_object_class()
   list_of_ticket_field_validation_types = animecoin_ticket_df.iloc[:,-1].tolist()
   for cnt, current_field_name in enumerate(list_of_ticket_object_field_names_clean):
       current_field_value = list_of_ticket_object_field_values[cnt]
       current_field_validation_type = list_of_ticket_field_validation_types[cnt]
       setattr(ticket_data_object, current_field_name, current_field_value)
   return ticket_data_object

ticket_data_object = parse_animecoin_html_ticket_func(path_to_animecoin_html_ticket_file)

if 0: #Old code to replace:
    def perform_superficial_validation_of_html_metadata_table_func(path_to_art_folder):
        global minimum_total_number_of_unique_copies_of_artwork
        global maximum_total_number_of_unique_copies_of_artwork
        global earliest_possible_artwork_signing_date
        global maximum_length_in_characters_of_text_field
        global maximum_combined_image_size_in_megabytes_for_single_artwork
        list_of_validation_steps = []
        latest_possible_artwork_signing_date = datetime.now() + timedelta(hours=24)
        path_to_metadata_html_table_file = get_metadata_file_path_from_art_folder_func(path_to_art_folder)    
        anime_metadata_format_version_number, artists_animecoin_id_public_key_pem_format, artists_receiving_anime_blockchain_address, artists_receiving_anime_blockchain_address, computed_combined_image_and_metadata_string, computed_combined_image_and_metadata_hash, \
            artists_digital_signature_on_the_combined_image_and_metadata_hash_base64_encoded, total_number_of_unique_copies_of_artwork, date_time_artwork_was_signed_by_artist, artists_name, artwork_title, artwork_series_name, \
            artists_website, artists_statement_about_artwork, combined_size_of_artwork_image_files_in_megabytes, current_bitcoin_blockchain_block_number, current_bitcoin_blockchain_block_hash, current_anime_mining_difficulty_rate, \
            avg_anime_mining_difficulty_rate_first_20k_blocks, anime_mining_difficulty_adjustment_ratio, registration_fee_in_anime_pre_difficulty_adjustment, registration_fee_in_anime_post_difficulty_adjustment, \
            forfeitable_deposit_in_anime_to_initiate_registration, registering_masternode_collateral_anime_blockchain_address, registering_masternode_animecoin_id_public_key_pem_format, registering_masternode_ip_address, \
            registering_masternodes_collateral_address_digital_signature_on_the_hash_of_the_concatenated_hashes_base64_encoded, registering_masternodes_animecoin_public_id_digital_signature_on_the_hash_of_the_concatenated_hashes_base64_encoded, date_time_masternode_registered_artwork, \
            confirming_masternode_1_collateral_anime_blockchain_address, confirming_masternode_1_animecoin_id_public_key_pem_format, confirming_masternode_1_ip_address, confirming_masternode_1_collateral_address_digital_signature_on_the_hash_of_the_concatenated_hashes_base64_encoded, \
            confirming_masternode_1_animecoin_public_id_digital_signature_on_the_hash_of_the_concatenated_hashes_base64_encoded, date_time_confirming_masternode_1_attested_to_artwork, confirming_masternode_2_collateral_anime_blockchain_address, confirming_masternode_2_animecoin_id_public_key_pem_format, \
            confirming_masternode_2_ip_address, confirming_masternode_2_collateral_address_digital_signature_on_the_hash_of_the_concatenated_hashes_base64_encoded, confirming_masternode_2_animecoin_public_id_digital_signature_on_the_hash_of_the_concatenated_hashes_base64_encoded, \
            date_time_confirming_masternode_2_attested_to_artwork, concatenated_image_file_hashes, list_of_image_file_hashes, parsed_metadata_list = parse_metadata_html_table_func(path_to_metadata_html_table_file)
        verification_combined_image_and_metadata_hash, verification_combined_image_and_metadata_string = generate_combined_image_and_metadata_hash_func(concatenated_image_file_hashes, combined_size_of_artwork_image_files_in_megabytes, artists_name, artists_animecoin_id_public_key_pem_format, artists_receiving_anime_blockchain_address, artwork_title, total_number_of_unique_copies_of_artwork, artwork_series_name, artists_website, artists_statement_about_artwork)
        if (verification_combined_image_and_metadata_hash != computed_combined_image_and_metadata_hash):
            print('Error! The reported combined image and metadata hash did NOT match the computed hash.')
        else:
            list_of_validation_steps.append(1)
        if (total_number_of_unique_copies_of_artwork < minimum_total_number_of_unique_copies_of_artwork) or (total_number_of_unique_copies_of_artwork > maximum_total_number_of_unique_copies_of_artwork):
            print('Error! The metadata table specifies an invalid total number of unique copies of the artwork.')
        else:
            list_of_validation_steps.append(1)
        if (date_time_artwork_was_signed_by_artist < earliest_possible_artwork_signing_date) or (date_time_artwork_was_signed_by_artist > latest_possible_artwork_signing_date):
            print('Error! The metadata table specifies an invalid artwork signing date-time (either too early or from the future!)')
        else:
            list_of_validation_steps.append(1)
        if (date_time_masternode_registered_artwork < earliest_possible_artwork_signing_date) or (date_time_masternode_registered_artwork > latest_possible_artwork_signing_date):
            print('Error! The metadata table specifies an invalid masternode artwork registration date-time (either too early or from the future!)')
        else:
            list_of_validation_steps.append(1)
        list_of_invalid_text_fields, list_of_valid_text_fields = validate_list_of_text_fields_for_html_metadata_table_func(parsed_metadata_list)
        if len(list_of_invalid_text_fields) > 0:
            print('Error! One or more text fields in the metadata table were invalid! List of invalid fields:')
            for current_invalid_field in list_of_invalid_text_fields:
                print(current_invalid_field)
        else:
            list_of_validation_steps.append(1)
        list_of_anime_blockchain_addresses_to_verify = [artists_receiving_anime_blockchain_address, registering_masternode_collateral_anime_blockchain_address]
        list_of_valid_anime_blockchain_addresses, list_of_invalid_anime_blockchain_addresses = validate_list_of_anime_blockchain_addresses(list_of_anime_blockchain_addresses_to_verify)
        if len(list_of_invalid_anime_blockchain_addresses) > 0:
            print('Error! One or more ANIME blockchain addresses in the metadata table were invalid! List of invalid ANIME blockchain addresses:')
            for current_invalid_address in list_of_invalid_anime_blockchain_addresses:
                print(current_invalid_address)
        else:
            list_of_validation_steps.append(1)
        url_is_valid = validate_url_func(artists_website)
        if (len(artists_website) > 0) and (url_is_valid==0):
            print('Error! The metadata table specifies an invalid artist website (' + artists_website + ').')
        else:
            list_of_validation_steps.append(1)
        if combined_size_of_artwork_image_files_in_megabytes > maximum_combined_image_size_in_megabytes_for_single_artwork:
            print('Error! The metadata table specifies image files with a combined size in megabytes (' + str(combined_size_of_artwork_image_files_in_megabytes) + 'mb) above the limit (' + str(maximum_combined_image_size_in_megabytes_for_single_artwork) + 'mb). Cannot Validate artwork.')
        else:
            list_of_validation_steps.append(1)
        verification_bitcoin_blockchain_block_number_string, current_bitcoin_blockchain_block_hash = check_current_bitcoin_blockchain_stats_func()
        verification_bitcoin_blockchain_block_number = float(verification_bitcoin_blockchain_block_number_string) + 1
        current_bitcoin_blockchain_block_number = float(current_bitcoin_blockchain_block_number)
        if current_bitcoin_blockchain_block_number > verification_bitcoin_blockchain_block_number:
            print('Error! The metadata table specifies a bitcoin block number(' + str(current_bitcoin_blockchain_block_number) + ')  that is impossible (current bitcoin block number is ' + str(verification_bitcoin_blockchain_block_number) + '). Cannot Validate artwork.')
        else:
            list_of_validation_steps.append(1)
        if not forfeitable_deposit_in_anime_to_initiate_registration > 0:
            print('Error! The metadata table does NOT specify a positive forfeitable deposit in ANIME to initiate registration.')
        else:
            list_of_validation_steps.append(1)
        if not registration_fee_in_anime_post_difficulty_adjustment > 0:
            print('Error! The metadata table does NOT specify a positive registration fee in ANIME.')
        else:
            list_of_validation_steps.append(1)
        verification_registration_fee_in_anime_post_difficulty_adjustment, verification_registration_fee_in_anime_pre_difficulty_adjustment, verification_anime_mining_difficulty_adjustment_ratio, verification_forfeitable_deposit_in_anime_to_initiate_registration = calculate_artwork_registration_fee_func(combined_size_of_artwork_image_files_in_megabytes)
        if (verification_registration_fee_in_anime_post_difficulty_adjustment != registration_fee_in_anime_post_difficulty_adjustment):
            print('Error! The metadata table specifies a registration fee that is different from the computed fee.')
        else:
            list_of_validation_steps.append(1)    
        if (verification_anime_mining_difficulty_adjustment_ratio != anime_mining_difficulty_adjustment_ratio):
            print('Error! The metadata table specifies a mining difficulty adjustment ratio that is different from the computed ratio.')
        else:
            list_of_validation_steps.append(1)    
        if (verification_forfeitable_deposit_in_anime_to_initiate_registration != forfeitable_deposit_in_anime_to_initiate_registration):
            print('Error! The metadata table specifies a forfeitable deposit that is different from the computed deposit.')
        else:
            list_of_validation_steps.append(1)    
        ip_responded_to_ping = check_if_ip_address_responds_to_pings_func(registering_masternode_ip_address)
        if not ip_responded_to_ping:
            print('Error! The metadata table specifies a registering masternode IP address (' + registering_masternode_ip_address + ') that is not responding to pings!')
        else:
            list_of_validation_steps.append(1)    
        artist_signature_verified = verify_signature_on_data_func(computed_combined_image_and_metadata_hash, artists_animecoin_id_public_key_pem_format, artists_digital_signature_on_the_combined_image_and_metadata_hash_base64_encoded)
        masternode_anime_public_id_signature_verified = verify_signature_on_data_func(computed_combined_image_and_metadata_hash, registering_masternode_animecoin_id_public_key_pem_format, registering_masternodes_animecoin_public_id_digital_signature_on_the_hash_of_the_concatenated_hashes_base64_encoded)
        if not artist_signature_verified:
            print('Error! The metadata table specifies an invalid artist signature!')
        else:
            list_of_validation_steps.append(1)
        if not masternode_anime_public_id_signature_verified:
            print('Error! The metadata table specifies an invalid registering masternode Animecoin ID signature!')
        else:
            list_of_validation_steps.append(1)
        #masternode_anime_public_id_signature_verified = verify_signature_on_data_func(computed_combined_image_and_metadata_hash, registering_masternode_collateral_address_public_key_pem_format, registering_masternodes_collateral_address_digital_signature_on_the_hash_of_the_concatenated_hashes_base64_encoded)
        return list_of_validation_steps
    
    
    
    def confirm_and_attest_to_artwork_registered_by_another_masternode_func(path_to_art_folder):
        path_to_metadata_html_table_file = get_metadata_file_path_from_art_folder_func(path_to_art_folder)
        with open(path_to_metadata_html_table_file,'r') as f:
            metadata_html_string = f.read()
        metadata_html_string_parsed = lxml.html.fromstring(metadata_html_string)
        parsed_metadata = metadata_html_string_parsed.xpath('//td')
        parsed_metadata_list = [x.text for x in parsed_metadata]
        computed_combined_image_and_metadata_hash = parsed_metadata_list[3]
        confirming_masternode_1_collateral_anime_blockchain_address = parsed_metadata_list[26]
        metadata_html_table_output_file_path = os.path.join(path_to_art_folder,'Animecoin_Metadata_File__Combined_Hash__'+computed_combined_image_and_metadata_hash+'.html')
        
        concatenated_image_file_hashes, combined_size_of_artwork_image_files_in_megabytes = get_concatenated_art_image_file_hashes_and_total_size_in_mb_func(path_to_art_folder)
        
        computed_combined_image_and_metadata_hash, concatenated_core_metadata = generate_combined_image_and_metadata_hash_func(concatenated_image_file_hashes,
                                                                                  combined_size_of_artwork_image_files_in_megabytes,
                                                                                  latest_anime_mining_difficulty_rate,
                                                                                  artists_name,
                                                                                  artists_animecoin_id_public_key_pem_format,
                                                                                  artists_receiving_anime_blockchain_address,
                                                                                  artwork_title,
                                                                                  total_number_of_unique_copies_of_artwork,
                                                                                  artwork_series_name='',
                                                                                  artists_website='',
                                                                                  artists_statement_about_artwork='')
        x = metadata_html_string
        if confirming_masternode_1_collateral_anime_blockchain_address == 'ANIME_CONFIRMING_MN_1_COLLATERAL_BLOCKCHAIN_ADDRESS':
            print('Acting as Confirming Masternode 1 in verifying the registration of the art assets with combined image/metadata hash of '+computed_combined_image_and_metadata_hash)
            x = x.replace('ANIME_CONFIRMING_MN_1_COLLATERAL_BLOCKCHAIN_ADDRESS', confirming_masternode_1_collateral_anime_blockchain_address)
            x = x.replace('ANIME_CONFIRMING_MN_1_ANIMECOIN_ID_PUBLIC_KEY', confirming_masternode_1_animecoin_id_public_key_pem_format)
            x = x.replace('ANIME_CONFIRMING_MN_1_IP_ADDRESS', confirming_masternode_1_ip_address)
            x = x.replace('ANIME_CONFIRMING_MN_1_COLLATERAL_SIGNATURE_ON_HASH_OF_HASHES', confirming_masternode_1_collateral_address_digital_signature_on_the_hash_of_the_concatenated_hashes_base64_encoded)
            x = x.replace('ANIME_CONFIRMING_MN_1_ANIMECOIN_ID_SIGNATURE_ON_HASH_OF_HASHES', confirming_masternode_1_animecoin_public_id_digital_signature_on_the_hash_of_the_concatenated_hashes_base64_encoded)
            x = x.replace('ANIME_DATE_TIME_CONFIRMING_MN_1_ATTESTED_TO_ARTWORK', date_time_confirming_masternode_1_attested_to_artwork)
        else:
            print('Acting as Confirming Masternode 2 in verifying the registration of the art assets with combined image/metadata hash of '+computed_combined_image_and_metadata_hash)
            x = x.replace('ANIME_CONFIRMING_MN_2_COLLATERAL_BLOCKCHAIN_ADDRESS', confirming_masternode_2_collateral_anime_blockchain_address)
            x = x.replace('ANIME_CONFIRMING_MN_2_ANIMECOIN_ID_PUBLIC_KEY', confirming_masternode_2_animecoin_id_public_key_pem_format)
            x = x.replace('ANIME_CONFIRMING_MN_2_IP_ADDRESS', confirming_masternode_2_ip_address)
            x = x.replace('ANIME_CONFIRMING_MN_2_COLLATERAL_SIGNATURE_ON_HASH_OF_HASHES', confirming_masternode_2_collateral_address_digital_signature_on_the_hash_of_the_concatenated_hashes_base64_encoded)
            x = x.replace('ANIME_CONFIRMING_MN_2_ANIMECOIN_ID_SIGNATURE_ON_HASH_OF_HASHES', confirming_masternode_2_animecoin_public_id_digital_signature_on_the_hash_of_the_concatenated_hashes_base64_encoded)
            x = x.replace('ANIME_DATE_TIME_CONFIRMING_MN_2_ATTESTED_TO_ARTWORK', date_time_confirming_masternode_2_attested_to_artwork)
       