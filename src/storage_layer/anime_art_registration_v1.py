import os.path, glob, hashlib, os, sqlite3, base64, shutil
import lxml, lxml.html
from zipfile import ZipFile
from shutil import copyfile
from datetime import datetime, timedelta
from PIL import Image
from anime_utility_functions_v1 import sign_data_with_private_key_func, verify_signature_on_data_func, generate_and_save_example_rsa_keypair_files_func, clean_up_art_folder_after_registration_func, generate_example_artwork_metadata_func, \
    get_local_masternode_identification_keypair_func, get_all_valid_image_file_paths_in_folder_func, check_current_bitcoin_blockchain_stats_func, get_my_local_ip_func, remove_existing_remaining_zip_files_func, get_all_animecoin_parameters_func, \
    get_all_animecoin_directories_func, get_pem_format_of_rsa_key_func, get_example_keypair_of_rsa_public_and_private_keys_func, get_example_keypair_of_rsa_public_and_private_keys_pem_format_func, get_current_anime_mining_difficulty_func, \
    get_avg_anime_mining_difficulty_rate_first_20k_blocks_func, get_image_hash_from_image_file_path_func, concatenate_list_of_strings_func, get_metadata_file_path_from_art_folder_func, get_current_datetime_string_func, parse_datetime_string_func, \
    validate_list_of_text_fields_for_html_metadata_table_func, check_if_anime_blockchain_address_is_valid_func, validate_list_of_anime_blockchain_addresses, validate_url_func, check_if_ip_address_responds_to_pings_func
from anime_image_processing_v1 import check_art_folder_for_nsfw_content_func, check_if_image_is_likely_dupe_func
from anime_fountain_coding_v1 import encode_final_art_zipfile_into_luby_transform_blocks_func
#Get various settings:
anime_metadata_format_version_number, minimum_total_number_of_unique_copies_of_artwork, maximum_total_number_of_unique_copies_of_artwork, target_number_of_nodes_per_unique_block_hash, target_block_redundancy_factor, desired_block_size_in_bytes, \
remote_node_chunkdb_refresh_time_in_minutes, remote_node_image_fingerprintdb_refresh_time_in_minutes, percentage_of_block_files_to_randomly_delete, percentage_of_block_files_to_randomly_corrupt, percentage_of_each_selected_file_to_be_randomly_corrupted, \
registration_fee_anime_per_megabyte_of_images_pre_difficulty_adjustment, example_list_of_valid_masternode_ip_addresses, forfeitable_deposit_to_initiate_registration_as_percentage_of_adjusted_registration_fee, nginx_ip_whitelist_override_addresses, \
example_animecoin_masternode_blockchain_address, example_trader_blockchain_address, example_artists_receiving_blockchain_address, rpc_connection_string, max_number_of_blocks_to_download_before_checking, earliest_possible_artwork_signing_date, \
maximum_length_in_characters_of_text_field, maximum_combined_image_size_in_megabytes_for_single_artwork, nsfw_score_threshold, duplicate_image_threshold = get_all_animecoin_parameters_func()
#Get various directories:
root_animecoin_folder_path, folder_path_of_art_folders_to_encode, block_storage_folder_path, folder_path_of_remote_node_sqlite_files, reconstructed_file_destination_folder_path, \
misc_masternode_files_folder_path, masternode_keypair_db_file_path, trade_ticket_files_folder_path, completed_trade_ticket_files_folder_path, pending_trade_ticket_files_folder_path, \
prepared_final_art_zipfiles_folder_path, chunk_db_file_path, file_storage_log_file_path, nginx_allowed_ip_whitelist_file_path, path_to_animecoin_trade_ticket_template, \
artist_final_signature_files_folder_path, dupe_detection_image_fingerprint_database_file_path, path_to_animecoin_artwork_metadata_template = get_all_animecoin_directories_func()

###############################################################################################################
# Functions:
###############################################################################################################
#Digital signature functions:

def get_concatenated_art_image_file_hashes_and_total_size_in_mb_func(path_to_art_folder):
    art_input_file_paths = get_all_valid_image_file_paths_in_folder_func(path_to_art_folder)
    list_of_art_file_hashes = []
    combined_image_file_size_in_mb = 0
    for current_file_path in art_input_file_paths:
        with open(current_file_path,'rb') as f:
            current_art_file_data = f.read()
        sha256_hash_of_current_art_file = hashlib.sha256(current_art_file_data).hexdigest()                    
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

def calculate_artwork_registration_fee_func(combined_size_of_artwork_image_files_in_megabytes):
    global registration_fee_anime_per_megabyte_of_images_pre_difficulty_adjustment
    global forfeitable_deposit_to_initiate_registration_as_percentage_of_adjusted_registration_fee
    avg_anime_mining_difficulty_rate_first_20k_blocks = get_avg_anime_mining_difficulty_rate_first_20k_blocks_func()
    current_anime_mining_difficulty_rate = get_current_anime_mining_difficulty_func()
    anime_mining_difficulty_adjustment_ratio = float(current_anime_mining_difficulty_rate) / float(avg_anime_mining_difficulty_rate_first_20k_blocks)
    registration_fee_in_anime_pre_difficulty_adjustment = round(float(registration_fee_anime_per_megabyte_of_images_pre_difficulty_adjustment)*float(combined_size_of_artwork_image_files_in_megabytes))
    registration_fee_in_anime_post_difficulty_adjustment = round(float(registration_fee_in_anime_pre_difficulty_adjustment) / float(anime_mining_difficulty_adjustment_ratio))
    forfeitable_deposit_in_anime_to_initiate_registration = round(float(registration_fee_in_anime_post_difficulty_adjustment)*float(forfeitable_deposit_to_initiate_registration_as_percentage_of_adjusted_registration_fee))
    return registration_fee_in_anime_post_difficulty_adjustment, registration_fee_in_anime_pre_difficulty_adjustment, anime_mining_difficulty_adjustment_ratio, forfeitable_deposit_in_anime_to_initiate_registration

def generate_combined_image_and_metadata_hash_func(concatenated_image_file_hashes,
                                                  combined_size_of_artwork_image_files_in_megabytes,
                                                  artists_name,
                                                  artists_animecoin_id_public_key_pem_format,
                                                  artists_receiving_anime_blockchain_address,
                                                  artwork_title,
                                                  total_number_of_unique_copies_of_artwork,
                                                  artwork_series_name,
                                                  artists_website,
                                                  artists_statement_about_artwork):
    global anime_metadata_format_version_number
    if not isinstance(concatenated_image_file_hashes, str):
        concatenated_image_file_hashes = concatenated_image_file_hashes.decode('utf-8') 
    latest_bitcoin_blockchain_block_number, latest_bitcoin_blockchain_block_hash = check_current_bitcoin_blockchain_stats_func()
    registration_fee_in_anime_post_difficulty_adjustment, registration_fee_in_anime_pre_difficulty_adjustment, anime_mining_difficulty_adjustment_ratio, forfeitable_deposit_in_anime_to_initiate_registration = calculate_artwork_registration_fee_func(combined_size_of_artwork_image_files_in_megabytes)
    combined_image_and_metadata = concatenated_image_file_hashes + anime_metadata_format_version_number + artists_name + artists_animecoin_id_public_key_pem_format + artists_receiving_anime_blockchain_address + artwork_title + str(total_number_of_unique_copies_of_artwork) + artwork_series_name + artists_website + artists_statement_about_artwork + str(registration_fee_in_anime_post_difficulty_adjustment) + str(anime_mining_difficulty_adjustment_ratio) + str(forfeitable_deposit_in_anime_to_initiate_registration) + latest_bitcoin_blockchain_block_number + latest_bitcoin_blockchain_block_hash
    combined_image_and_metadata_hash = hashlib.sha256(combined_image_and_metadata.encode('utf-8')).hexdigest()         
    return combined_image_and_metadata_hash, combined_image_and_metadata
    
#Artwork Metadata Functions:
#list_of_paths_to_art_image_files = get_all_valid_image_file_paths_in_folder_func(path_to_art_folder)
def add_image_files_to_metadata_html_table_string_func(animecoin_metadata_html_string, list_of_paths_to_art_image_files):
    max_image_preview_pixel_dimension = 1000
    image_file_html_template_string = '#<tr> <th align="left"><img src="ANIME_IMAGE_FILENAME" alt="ANIME_IMAGE_HASH" style="width:ANIME_IMAGE_PIXEL_WIDTHpx;height:ANIME_IMAGE_PIXEL_HEIGHTpx;"></th> <td>ANIME_IMAGE_HASH</td> </tr>'
    new_html_lines = []
    for cnt, current_image_file_path in enumerate(list_of_paths_to_art_image_files):
        modified_image_file_html_template_string = image_file_html_template_string
        current_image_file_name = current_image_file_path.split(os.sep)[-1]
        sha256_hash_of_current_art_image_file = get_image_hash_from_image_file_path_func(current_image_file_path)
        current_loaded_image_data = Image.open(current_image_file_path)
        current_image_pixel_width, current_image_image_pixel_height = current_loaded_image_data.size
        rescaling_factor = max_image_preview_pixel_dimension / max([current_image_pixel_width, current_image_image_pixel_height])
        preview_image_pixel_width = round(current_image_pixel_width*rescaling_factor)
        preview_image_pixel_height = round(current_image_image_pixel_height*rescaling_factor)
        modified_image_file_html_template_string = modified_image_file_html_template_string.replace('ANIME_IMAGE_FILENAME', './'+current_image_file_name)
        modified_image_file_html_template_string = modified_image_file_html_template_string.replace('ANIME_IMAGE_HASH', sha256_hash_of_current_art_image_file)
        modified_image_file_html_template_string = modified_image_file_html_template_string.replace('ANIME_IMAGE_PIXEL_WIDTH', str(preview_image_pixel_width))
        modified_image_file_html_template_string = modified_image_file_html_template_string.replace('ANIME_IMAGE_PIXEL_HEIGHT', str(preview_image_pixel_height))
        new_html_lines.append(modified_image_file_html_template_string)
    replacement_string = concatenate_list_of_strings_func(new_html_lines)
    final_replacement_string = replacement_string.replace('#','')
    updated_animecoin_metadata_html_string = animecoin_metadata_html_string.replace('ANIME_IMAGE_FILEPATHS_AND_HASH_DATA', final_replacement_string)
    return updated_animecoin_metadata_html_string
    
def create_metadata_html_table_for_given_art_folder_func(path_to_art_folder,
                                                  artists_name,
                                                  artists_animecoin_id_public_key_pem_format,
                                                  artists_receiving_anime_blockchain_address,
                                                  artwork_title,
                                                  total_number_of_unique_copies_of_artwork,
                                                  artwork_series_name,
                                                  artists_website,
                                                  artists_statement_about_artwork,
                                                  date_time_artwork_was_signed_by_artist,
                                                  artist_reported_combined_image_and_metadata_hash,
                                                  artists_digital_signature_on_the_combined_image_and_metadata_hash_base64_encoded):
    global path_to_animecoin_artwork_metadata_template
    global anime_metadata_format_version_number
    global forfeitable_deposit_to_initiate_registration_as_percentage_of_adjusted_registration_fee
    global example_animecoin_masternode_blockchain_address #Todo: Replace this with real address from RPC interface
    current_anime_mining_difficulty_rate = get_current_anime_mining_difficulty_func()
    avg_anime_mining_difficulty_rate_first_20k_blocks = get_avg_anime_mining_difficulty_rate_first_20k_blocks_func()
    try:
      with open(path_to_animecoin_artwork_metadata_template,'r') as f:
        animecoin_metadata_template_html_string = f.read()
    except Exception as e:
        print('Error: '+ str(e))
    current_bitcoin_blockchain_block_number, current_bitcoin_blockchain_block_hash = check_current_bitcoin_blockchain_stats_func()
    date_time_masternode_registered_artwork = get_current_datetime_string_func()
    registering_masternode_animecoin_id_public_key, registering_masternode_animecoin_id_private_key = get_local_masternode_identification_keypair_func()
    registering_masternode_collateral_anime_blockchain_address = example_animecoin_masternode_blockchain_address
    concatenated_image_file_hashes, combined_size_of_artwork_image_files_in_megabytes = get_concatenated_art_image_file_hashes_and_total_size_in_mb_func(path_to_art_folder)
    registration_fee_in_anime_post_difficulty_adjustment, registration_fee_in_anime_pre_difficulty_adjustment, anime_mining_difficulty_adjustment_ratio, forfeitable_deposit_in_anime_to_initiate_registration = calculate_artwork_registration_fee_func(combined_size_of_artwork_image_files_in_megabytes)
    computed_combined_image_and_metadata_hash, computed_combined_image_and_metadata_string = generate_combined_image_and_metadata_hash_func(concatenated_image_file_hashes, combined_size_of_artwork_image_files_in_megabytes, artists_name, artists_animecoin_id_public_key_pem_format, artists_receiving_anime_blockchain_address, artwork_title, total_number_of_unique_copies_of_artwork, artwork_series_name, artists_website, artists_statement_about_artwork)
    example_collateral_address_public_key, example_collateral_address_private_key, example_animecoin_public_id_public_key, animecoin_public_id_private_key, _, _ = get_example_keypair_of_rsa_public_and_private_keys_func()
    example_collateral_address_public_key_pem_format, example_collateral_address_private_key_pem_format, example_animecoin_public_id_public_key_pem_format, animecoin_public_id_private_key_pem_format, _, _ = get_example_keypair_of_rsa_public_and_private_keys_pem_format_func()
    registering_masternode_animecoin_id_public_key_pem_format = example_animecoin_public_id_public_key_pem_format
    registering_masternode_ip_address = get_my_local_ip_func()
    if artist_reported_combined_image_and_metadata_hash != computed_combined_image_and_metadata_hash:
        print('Error! Registering artist has provided a combined image and metadata hash that does NOT match the locally computed hash! Submission is invalid!')
        metadata_html_table_output_file_path = 0
        return metadata_html_table_output_file_path
    else:
        registering_masternodes_collateral_address_digital_signature_on_the_hash_of_the_concatenated_hashes_base64_encoded = sign_data_with_private_key_func(computed_combined_image_and_metadata_hash, example_collateral_address_private_key)
        registering_masternodes_animecoin_public_id_digital_signature_on_the_hash_of_the_concatenated_hashes_base64_encoded = sign_data_with_private_key_func(computed_combined_image_and_metadata_hash, animecoin_public_id_private_key)
        x = animecoin_metadata_template_html_string
        x = x.replace('ANIME_METADATA_FORMAT_VERSION_NUMBER', anime_metadata_format_version_number)
        x = x.replace('ANIME_ARTIST_ID_PUBLIC_KEY', artists_animecoin_id_public_key_pem_format)
        x = x.replace('ANIME_ARTIST_RECEIVING_BLOCKCHAIN_ADDRESS', artists_receiving_anime_blockchain_address)
        x = x.replace('ANIME_COMBINED_IMAGE_AND_METADATA_STRING', computed_combined_image_and_metadata_string)
        x = x.replace('ANIME_COMBINED_IMAGE_AND_METADATA_HASH', computed_combined_image_and_metadata_hash)
        x = x.replace('ANIME_ARTIST_SIGNATURE_ON_COMBINED_IMAGE_AND_METADATA_HASH', artists_digital_signature_on_the_combined_image_and_metadata_hash_base64_encoded)
        x = x.replace('ANIME_TOTAL_NUMBER_OF_UNIQUE_COPIES_OF_ARTWORK', str(total_number_of_unique_copies_of_artwork))
        x = x.replace('ANIME_DATE_TIME_ARTWORK_SIGNED', date_time_artwork_was_signed_by_artist)
        x = x.replace('ANIME_ARTIST_NAME', artists_name)
        x = x.replace('ANIME_ARTWORK_TITLE', artwork_title)
        x = x.replace('ANIME_SERIES_NAME', artwork_series_name)
        x = x.replace('ANIME_ARTIST_WEBSITE', artists_website)
        x = x.replace('ANIME_ARTIST_STATEMENT', artists_statement_about_artwork)
        x = x.replace('ANIME_COMBINED_IMAGE_SIZE_IN_MB', combined_size_of_artwork_image_files_in_megabytes)
        x = x.replace('ANIME_MN_CURRENT_BITCOIN_BLOCK_NUMBER', current_bitcoin_blockchain_block_number)
        x = x.replace('ANIME_MN_CURRENT_BITCOIN_BLOCK_HASH', current_bitcoin_blockchain_block_hash)
        x = x.replace('ANIME_MN_CURRENT_ANIME_MINING_DIFFICULTY_RATE', str(current_anime_mining_difficulty_rate))
        x = x.replace('ANIME_MN_AVG_ANIME_MINING_DIFFICULTY_RATE_FIRST_20K_BLOCKS', str(avg_anime_mining_difficulty_rate_first_20k_blocks))
        x = x.replace('ANIME_MN_MINING_DIFFICULTY_ADJUSTMENT_RATIO', str(anime_mining_difficulty_adjustment_ratio))
        x = x.replace('ANIME_MN_REGISTRATION_FEE_IN_ANIME_PRE_ADJUSTMENT', str(registration_fee_in_anime_pre_difficulty_adjustment))
        x = x.replace('ANIME_MN_REGISTRATION_FEE_IN_ANIME_POST_ADJUSTMENT', str(registration_fee_in_anime_post_difficulty_adjustment))
        x = x.replace('ANIME_MN_FORFEITABLE_DEPOSIT_IN_ANIME_TO_INITIATE_REGISTRATION', str(forfeitable_deposit_in_anime_to_initiate_registration))
        x = x.replace('ANIME_MN_COLLATERAL_BLOCKCHAIN_ADDRESS', registering_masternode_collateral_anime_blockchain_address)
        x = x.replace('ANIME_MN_ANIMECOIN_ID_PUBLIC_KEY', registering_masternode_animecoin_id_public_key_pem_format)
        x = x.replace('ANIME_MN_IP_ADDRESS', registering_masternode_ip_address)
        x = x.replace('ANIME_MN_COLLATERAL_SIGNATURE_ON_COMBINED_IMAGE_AND_METADATA_HASH', registering_masternodes_collateral_address_digital_signature_on_the_hash_of_the_concatenated_hashes_base64_encoded)
        x = x.replace('ANIME_MN_ANIMECOIN_ID_SIGNATURE_ON_COMBINED_IMAGE_AND_METADATA_HASH', registering_masternodes_animecoin_public_id_digital_signature_on_the_hash_of_the_concatenated_hashes_base64_encoded)
        x = x.replace('ANIME_DATE_TIME_MN_REGISTERED_ARTWORK', date_time_masternode_registered_artwork)
        new_animecoin_metadata_html_string = x
        valid_image_file_paths = get_all_valid_image_file_paths_in_folder_func(path_to_art_folder)
        final_animecoin_metadata_html_string = add_image_files_to_metadata_html_table_string_func(new_animecoin_metadata_html_string, valid_image_file_paths)
        metadata_html_table_output_file_path = os.path.join(path_to_art_folder,'Animecoin_Metadata_File__Combined_Hash__' + computed_combined_image_and_metadata_hash + '.html')
        with open(metadata_html_table_output_file_path,'w') as f:
            f.write(final_animecoin_metadata_html_string)
        print('Successfully generated HTML metadata table file for Artwork with Combined Image and Metadata Hash:'+computed_combined_image_and_metadata_hash)
        return metadata_html_table_output_file_path

#path_to_metadata_html_table_file = get_metadata_file_path_from_art_folder_func(path_to_art_folder)

def parse_metadata_html_table_func(path_to_metadata_html_table_file):
   with open(path_to_metadata_html_table_file,'r') as f:
       metadata_html_string = f.read()
   metadata_html_string_parsed = lxml.html.fromstring(metadata_html_string)
   parsed_metadata = metadata_html_string_parsed.xpath('//td')
   parsed_metadata_list = [x.text for x in parsed_metadata]
   anime_metadata_format_version_number = parsed_metadata_list[0]
   artists_animecoin_id_public_key_pem_format = parsed_metadata_list[1]
   artists_receiving_anime_blockchain_address = parsed_metadata_list[2]
   computed_combined_image_and_metadata_string = parsed_metadata_list[3]
   computed_combined_image_and_metadata_hash = parsed_metadata_list[4]
   artists_digital_signature_on_the_combined_image_and_metadata_hash_base64_encoded = parsed_metadata_list[5]
   total_number_of_unique_copies_of_artwork = int(parsed_metadata_list[6])
   date_time_artwork_was_signed_by_artist = parse_datetime_string_func(parsed_metadata_list[7])
   artists_name = parsed_metadata_list[8]
   artwork_title = parsed_metadata_list[9]
   artwork_series_name = parsed_metadata_list[10]
   artists_website = parsed_metadata_list[11]
   artists_statement_about_artwork = parsed_metadata_list[12]
   combined_size_of_artwork_image_files_in_megabytes = float(parsed_metadata_list[13])
   current_bitcoin_blockchain_block_number = parsed_metadata_list[14]
   current_bitcoin_blockchain_block_hash = parsed_metadata_list[15]
   current_anime_mining_difficulty_rate = float(parsed_metadata_list[16])
   avg_anime_mining_difficulty_rate_first_20k_blocks = float(parsed_metadata_list[17])
   anime_mining_difficulty_adjustment_ratio = float(parsed_metadata_list[18])
   registration_fee_in_anime_pre_difficulty_adjustment = float(parsed_metadata_list[19])
   registration_fee_in_anime_post_difficulty_adjustment = float(parsed_metadata_list[20])
   forfeitable_deposit_in_anime_to_initiate_registration = float(parsed_metadata_list[21])
   registering_masternode_collateral_anime_blockchain_address = parsed_metadata_list[22]
   registering_masternode_animecoin_id_public_key_pem_format = parsed_metadata_list[23]
   registering_masternode_ip_address = parsed_metadata_list[24]
   registering_masternodes_collateral_address_digital_signature_on_the_hash_of_the_concatenated_hashes_base64_encoded = parsed_metadata_list[25]
   registering_masternodes_animecoin_public_id_digital_signature_on_the_hash_of_the_concatenated_hashes_base64_encoded = parsed_metadata_list[26]
   date_time_masternode_registered_artwork = parse_datetime_string_func(parsed_metadata_list[27])
   confirming_masternode_1_collateral_anime_blockchain_address = parsed_metadata_list[28]
   confirming_masternode_1_animecoin_id_public_key_pem_format = parsed_metadata_list[29]
   confirming_masternode_1_ip_address = parsed_metadata_list[30]
   confirming_masternode_1_collateral_address_digital_signature_on_the_hash_of_the_concatenated_hashes_base64_encoded = parsed_metadata_list[31]
   confirming_masternode_1_animecoin_public_id_digital_signature_on_the_hash_of_the_concatenated_hashes_base64_encoded = parsed_metadata_list[32]
   date_time_confirming_masternode_1_attested_to_artwork = parse_datetime_string_func(parsed_metadata_list[33])
   confirming_masternode_2_collateral_anime_blockchain_address = parsed_metadata_list[34]
   confirming_masternode_2_animecoin_id_public_key_pem_format = parsed_metadata_list[35]
   confirming_masternode_2_ip_address = parsed_metadata_list[36]
   confirming_masternode_2_collateral_address_digital_signature_on_the_hash_of_the_concatenated_hashes_base64_encoded = parsed_metadata_list[37]
   confirming_masternode_2_animecoin_public_id_digital_signature_on_the_hash_of_the_concatenated_hashes_base64_encoded = parsed_metadata_list[38]
   date_time_confirming_masternode_2_attested_to_artwork = parse_datetime_string_func(parsed_metadata_list[39])
   list_of_image_file_hashes = []
   for cnt in range(40,len(parsed_metadata_list)):
       current_image_file_hash = parsed_metadata_list[cnt]
       list_of_image_file_hashes.append(current_image_file_hash)
   concatenated_image_file_hashes = concatenate_list_of_strings_func(list_of_image_file_hashes).replace(os.linesep,'')
   return anime_metadata_format_version_number, artists_animecoin_id_public_key_pem_format, artists_receiving_anime_blockchain_address, artists_receiving_anime_blockchain_address, computed_combined_image_and_metadata_string, computed_combined_image_and_metadata_hash, \
        artists_digital_signature_on_the_combined_image_and_metadata_hash_base64_encoded, total_number_of_unique_copies_of_artwork, date_time_artwork_was_signed_by_artist, artists_name, artwork_title, artwork_series_name, \
        artists_website, artists_statement_about_artwork, combined_size_of_artwork_image_files_in_megabytes, current_bitcoin_blockchain_block_number, current_bitcoin_blockchain_block_hash, current_anime_mining_difficulty_rate, \
        avg_anime_mining_difficulty_rate_first_20k_blocks, anime_mining_difficulty_adjustment_ratio, registration_fee_in_anime_pre_difficulty_adjustment, registration_fee_in_anime_post_difficulty_adjustment, \
        forfeitable_deposit_in_anime_to_initiate_registration, registering_masternode_collateral_anime_blockchain_address, registering_masternode_animecoin_id_public_key_pem_format, registering_masternode_ip_address, \
        registering_masternodes_collateral_address_digital_signature_on_the_hash_of_the_concatenated_hashes_base64_encoded, registering_masternodes_animecoin_public_id_digital_signature_on_the_hash_of_the_concatenated_hashes_base64_encoded, date_time_masternode_registered_artwork, \
        confirming_masternode_1_collateral_anime_blockchain_address, confirming_masternode_1_animecoin_id_public_key_pem_format, confirming_masternode_1_ip_address, confirming_masternode_1_collateral_address_digital_signature_on_the_hash_of_the_concatenated_hashes_base64_encoded, \
        confirming_masternode_1_animecoin_public_id_digital_signature_on_the_hash_of_the_concatenated_hashes_base64_encoded, date_time_confirming_masternode_1_attested_to_artwork, confirming_masternode_2_collateral_anime_blockchain_address, confirming_masternode_2_animecoin_id_public_key_pem_format, \
        confirming_masternode_2_ip_address, confirming_masternode_2_collateral_address_digital_signature_on_the_hash_of_the_concatenated_hashes_base64_encoded, confirming_masternode_2_animecoin_public_id_digital_signature_on_the_hash_of_the_concatenated_hashes_base64_encoded, \
        date_time_confirming_masternode_2_attested_to_artwork, concatenated_image_file_hashes, list_of_image_file_hashes, parsed_metadata_list

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

def validate_folder_for_nsfw_and_dupe_content_func(path_to_art_folder):
    global nsfw_score_threshold
    art_input_file_paths = get_all_valid_image_file_paths_in_folder_func(path_to_art_folder)
    list_of_art_file_hashes, list_of_nsfw_scores = check_art_folder_for_nsfw_content_func(path_to_art_folder)
    art_file_hash_to_nsfw_dict = dict(zip(list_of_art_file_hashes, list_of_nsfw_scores))
    list_of_accepted_art_file_hashes = []
    list_of_accepted_art_file_paths = []
    for current_file_path in art_input_file_paths:
        with open(current_file_path,'rb') as f:
            current_art_file = f.read()
        current_file_name_without_extension = current_file_path.split(os.sep)[-1].split('.')[0]
        sha256_hash_of_current_art_file = hashlib.sha256(current_art_file).hexdigest()                    
        current_image_nsfw_score = art_file_hash_to_nsfw_dict[sha256_hash_of_current_art_file]
        if current_image_nsfw_score > nsfw_score_threshold:
            print('Renaming offending file to end with NSFW: '+current_file_name_without_extension)
            modified_file_path = path_to_art_folder + current_file_name_without_extension + '.nsfw'
            os.rename(current_file_path,modified_file_path)
        else:
            print('\nNow checking for duplicates...')
            is_dupe = check_if_image_is_likely_dupe_func(sha256_hash_of_current_art_file)
            if is_dupe:
                print('Renaming offending file to end with DUPE: '+current_file_name_without_extension)
                modified_file_path = path_to_art_folder + current_file_name_without_extension + '.dupe'
                os.rename(current_file_path,modified_file_path)
            else:
                list_of_accepted_art_file_hashes.append(sha256_hash_of_current_art_file)
                list_of_accepted_art_file_paths.append(current_file_path)
    return list_of_accepted_art_file_hashes, list_of_accepted_art_file_paths


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
   
def register_new_artwork_folder_func(path_to_art_folder):
    global artist_final_signature_files_folder_path
    global prepared_final_art_zipfiles_folder_path
    successfully_registered_artwork = 0
    final_signature_file_path, path_to_final_artwork_zipfile_including_metadata = prepare_artwork_folder_for_registration_func(path_to_art_folder)
    parsed_final_art_zipfile_hash = path_to_final_artwork_zipfile_including_metadata.split(os.sep)[-1].split('__')[-1].replace('.zip','')
    duration_in_seconds = encode_final_art_zipfile_into_luby_transform_blocks_func(parsed_final_art_zipfile_hash)
    if duration_in_seconds > 0:
        shutil.copy(final_signature_file_path, artist_final_signature_files_folder_path)
    clean_up_art_folder_after_registration_func(path_to_art_folder)
    successfully_registered_artwork = 1
    return successfully_registered_artwork
