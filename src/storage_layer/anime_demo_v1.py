import os, os.path, glob, warnings
from datetime import datetime
warnings.simplefilter(action='ignore', category=FutureWarning)
warnings.filterwarnings('ignore',category=DeprecationWarning)
from anime_utility_functions_v1 import get_all_animecoin_directories_func, get_all_animecoin_parameters_func, get_example_keypair_of_rsa_public_and_private_keys_func, get_example_keypair_of_rsa_public_and_private_keys_pem_format_func, \
    generate_required_directories_func, regenerate_sqlite_chunk_database_func, get_local_masternode_identification_keypair_func, get_my_local_ip_func, generate_and_save_local_masternode_identification_keypair_func, get_various_directories_for_testing_func, \
    delete_all_blocks_and_zip_files_to_reset_system_func, get_image_hash_from_image_file_path_func, generate_example_artwork_metadata_func, sign_data_with_private_key_func, generate_and_save_example_rsa_keypair_files_func, \
    get_avg_anime_mining_difficulty_rate_first_20k_blocks_func, get_current_datetime_string_func, parse_datetime_string_func
from anime_image_processing_v1 import check_if_image_is_likely_dupe_func, add_all_images_in_folder_to_image_fingerprint_database_func, apply_tsne_to_image_fingerprint_database_func, \
    find_most_similar_images_to_given_image_from_fingerprint_data_func, get_tsne_coordinates_for_desired_image_file_hash_func, regenerate_dupe_detection_image_fingerprint_database_func
from anime_fountain_coding_v1 import refresh_block_storage_folder_and_check_block_integrity_func, decode_block_files_into_art_zipfile_func, randomly_delete_percentage_of_local_block_files_func, \
    randomly_corrupt_a_percentage_of_bytes_in_a_random_sample_of_block_files_func
from anime_art_registration_v1 import register_new_artwork_folder_func, generate_combined_image_and_metadata_hash_func, get_concatenated_art_image_file_hashes_and_total_size_in_mb_func, create_metadata_html_table_for_given_art_folder_func, \
    perform_superficial_validation_of_html_metadata_table_func
from anime_trading_v1 import sign_trade_ticket_hash_func, verify_trade_ticket_hash_signature_func, generate_trade_ticket_func
###############################################################################################################
# Parameters:
###############################################################################################################
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
#Various paths/data for testing purposes:
path_to_art_folder, path_to_art_image_file, path_to_another_similar_art_image_file, path_to_another_different_art_image_file, path_to_all_registered_works_for_dupe_detection, sha256_hash_of_art_image_file = get_various_directories_for_testing_func()

###############################################################################################################
# Demo:
###############################################################################################################
#Demonstration toggles:
use_demo_mode = 0
use_reset_system_for_demo = 0 
use_stress_test = 0
use_random_corruption = 0
use_demonstrate_file_reconstruction = 0
use_verify_integrity = 0
use_generate_new_sqlite_chunk_database = 0
use_generate_new_demonstration_artist_key_pair = 0
use_generate_local_masternode_anime_public_id_keypair = 0
use_demonstrate_duplicate_detection = 0
use_demonstrate_combined_metadata_hash_generation = 0
use_demonstrate_metadata_html_table_file_generation = 0
use_demonstrate_metadata_validation = 0
use_demonstrate_nginx_file_transfers = 0
use_demonstrate_trade_ticket_generation = 0
use_demonstrate_trade_ticket_parsing = 0

if use_reset_system_for_demo:
    delete_all_blocks_and_zip_files_to_reset_system_func()
list_of_required_folder_paths = [root_animecoin_folder_path, folder_path_of_remote_node_sqlite_files, reconstructed_file_destination_folder_path, artist_final_signature_files_folder_path, misc_masternode_files_folder_path, folder_path_of_art_folders_to_encode, prepared_final_art_zipfiles_folder_path, block_storage_folder_path]
generate_required_directories_func(list_of_required_folder_paths)
if not os.path.exists(dupe_detection_image_fingerprint_database_file_path): #only generate a new db file if it can't find an existing one.
    regenerate_dupe_detection_image_fingerprint_database_func()
if use_generate_local_masternode_anime_public_id_keypair:
    generate_and_save_local_masternode_identification_keypair_func() #We don't have a masternode keypair so we first need to generate one. 
if use_generate_new_sqlite_chunk_database:
    regenerate_sqlite_chunk_database_func()
if use_generate_new_demonstration_artist_key_pair:
    generate_and_save_example_rsa_keypair_files_func()
_, _, _, _, example_artist_public_key, example_artist_private_key = get_example_keypair_of_rsa_public_and_private_keys_func()
_, _, _, _, example_artist_public_key_pem_format, example_artist_private_key_pem_format = get_example_keypair_of_rsa_public_and_private_keys_pem_format_func()

if use_demo_mode:
    print('\nWelcome! This is a demo of the file storage system that is being used for the Animecoin project to store art files in a decentralized, robust way.')    
    print('\nFirst, we begin by taking a bunch of folders; each one contains one or images representing a single digital artwork to be registered.')
    print('\nWe will now perform a series of steps on these files. First we verify they are valid images and that they are not NSFW or dupes.')
    print('\nThen we will have the artist sign all the art files with his digital signature, create the art metadata file, and finally have the artist sign the final art zipfile with metadata.')
    list_of_art_sub_folder_paths = glob.glob(folder_path_of_art_folders_to_encode + '*' + os.sep)
    for current_art_folder in list_of_art_sub_folder_paths:
        successfully_registered_artwork = register_new_artwork_folder_func(current_art_folder)
    potential_local_block_hashes_list, potential_local_file_hashes_list = refresh_block_storage_folder_and_check_block_integrity_func()
    if use_stress_test: #Check how robust system is to lost/corrupted blocks:
        if use_demo_mode:
            print('\n\nGreat, we just finished turning the file into a bunch of "fungible" blocks! If you check the output folder now, you will see a collection of the resulting files.')
            print('Now we can see the purpose of all this. Suppose something really bad happens, and that most of the master nodes hosting these files disappear.')
            print('On top of that, suppose that many of the remaining nodes also lose some of the file chunks to corruption or disk failure. Still, we will be able to reconstruct the file.')
        number_of_deleted_blocks = randomly_delete_percentage_of_local_block_files_func(percentage_of_block_files_to_randomly_delete)
        if use_demo_mode:
            print('\n\nJust deleted '+str(number_of_deleted_blocks) +' of the generated blocks, or '+ str(round(100*number_of_deleted_blocks/len(potential_local_block_hashes_list),2))+'% of the blocks')
        if use_random_corruption:
           number_of_corrupted_blocks = randomly_corrupt_a_percentage_of_bytes_in_a_random_sample_of_block_files_func(percentage_of_block_files_to_randomly_corrupt,percentage_of_each_selected_file_to_be_randomly_corrupted)
           print('\n\nJust Corrupted '+str(number_of_corrupted_blocks) +' of the generated blocks, or '+ str(round(100*number_of_corrupted_blocks/len(potential_local_block_hashes_list),2))+'% of the blocks')
    
    if use_demonstrate_file_reconstruction:
        print('\n\nNow let\'s try to reconstruct the original file despite this *random* loss of most of the block files...')
        list_of_block_file_paths = glob.glob(block_storage_folder_path+'*.block')
        available_art_file_hashes = [p.split(os.sep)[-1].split('__')[1] for p in list_of_block_file_paths]
        available_art_file_hashes = list(set(available_art_file_hashes))
        failed_file_hash_list = []
        for current_file_hash in available_art_file_hashes:
            print('Now reconstructing file with SHA256 Hash of: ' + current_file_hash)
            completed_successfully = decode_block_files_into_art_zipfile_func(current_file_hash)
            if not completed_successfully:
                failed_file_hash_list.append(current_file_hash)
            if completed_successfully:
                print('\nBoom, we\'ve done it!')
        number_of_failed_files = len(failed_file_hash_list)
        if number_of_failed_files == 0:
            print('All files reconstructed successfully!')
        else:
            print('Some files were NOT successfully reconstructed! '+str(number_of_failed_files)+' Files had errors:\n ')
            for current_hash in failed_file_hash_list:
                print(current_hash+'\n')

if use_demonstrate_duplicate_detection:
    regenerate_dupe_detection_image_fingerprint_database_func() #only generates a new db file if it can't find an existing one.
    add_all_images_in_folder_to_image_fingerprint_database_func(path_to_all_registered_works_for_dupe_detection)    
    add_all_images_in_folder_to_image_fingerprint_database_func('C:\\anime_image_database\\')
    list_of_image_sha256_hashes, _, _, _, _, _, _ = apply_tsne_to_image_fingerprint_database_func()
    dupe_detection_test_images_base_folder_path = 'C:\\Users\\jeffr\\Cointel Dropbox\\Animecoin_Code\\dupe_detector_test_images\\' #Stress testing with sophisticated "modified" duplicates:
    path_to_original_dupe_test_image = os.path.join(dupe_detection_test_images_base_folder_path,'Arturo_Lopez__Number_02.png')
    hash_of_original_dupe_test_image = get_image_hash_from_image_file_path_func(path_to_original_dupe_test_image)
    image_similarity_df = find_most_similar_images_to_given_image_from_fingerprint_data_func(hash_of_original_dupe_test_image)
    model_1_tsne_x_coordinate, model_1_tsne_y_coordinate, model_2_tsne_x_coordinate, model_2_tsne_y_coordinate,model_3_tsne_x_coordinate, model_3_tsne_y_coordinate,art_image_file_name = get_tsne_coordinates_for_desired_image_file_hash_func(hash_of_original_dupe_test_image)
    is_dupe = check_if_image_is_likely_dupe_func(sha256_hash_of_art_image_file)

if use_demonstrate_combined_metadata_hash_generation:
    artists_name, artwork_title, total_number_of_unique_copies_of_artwork, artwork_series_name, artists_website, artists_statement_about_artwork = generate_example_artwork_metadata_func()
    concatenated_image_file_hashes, combined_size_of_artwork_image_files_in_megabytes = get_concatenated_art_image_file_hashes_and_total_size_in_mb_func(path_to_art_folder)
    artists_animecoin_id_public_key_pem_format = example_artist_public_key_pem_format
    artists_receiving_anime_blockchain_address = example_artists_receiving_blockchain_address
    computed_combined_image_and_metadata_hash, combined_image_and_metadata = generate_combined_image_and_metadata_hash_func(concatenated_image_file_hashes, combined_size_of_artwork_image_files_in_megabytes, artists_name, artists_animecoin_id_public_key_pem_format, artists_receiving_anime_blockchain_address, artwork_title, total_number_of_unique_copies_of_artwork, artwork_series_name, artists_website, artists_statement_about_artwork)

if use_demonstrate_metadata_html_table_file_generation:
    artist_reported_combined_image_and_metadata_hash = computed_combined_image_and_metadata_hash
    artists_digital_signature_on_the_combined_image_and_metadata_hash_base64_encoded = sign_data_with_private_key_func(artist_reported_combined_image_and_metadata_hash, example_artist_private_key)
    date_time_artwork_was_signed_by_artist = get_current_datetime_string_func()
    metadata_html_table_output_file_path = create_metadata_html_table_for_given_art_folder_func(path_to_art_folder, artists_name, artists_animecoin_id_public_key_pem_format, artists_receiving_anime_blockchain_address, artwork_title, total_number_of_unique_copies_of_artwork, artwork_series_name, artists_website, artists_statement_about_artwork, date_time_artwork_was_signed_by_artist, artist_reported_combined_image_and_metadata_hash, artists_digital_signature_on_the_combined_image_and_metadata_hash_base64_encoded)

if use_demonstrate_metadata_validation:
    list_of_validation_steps = perform_superficial_validation_of_html_metadata_table_func(path_to_art_folder)

if use_demonstrate_trade_ticket_generation:
    submitting_trader_animecoin_blockchain_address = example_trader_blockchain_address
    desired_trade_type = 'BUY'
    desired_art_asset_hash = '570f4b835404a83e9870b9e98a0e2ca3b58b43c775cf1275a6690ab618f0e73c'
    desired_quantity = 1
    specified_price_in_anime = 75000
    submitting_trader_animecoin_identity_public_key = example_artist_public_key
    submitting_trader_animecoin_identity_private_key = example_artist_private_key
    assigned_masternode_broker_ip_address = get_my_local_ip_func()
    assigned_masternode_broker_animecoin_identity_public_key, assigned_masternode_broker_animecoin_identity_private_key = get_local_masternode_identification_keypair_func()
    trade_ticket_details_sha256_hash, submitting_traders_digital_signature_on_trade_ticket_details_hash = sign_trade_ticket_hash_func(submitting_trader_animecoin_identity_private_key, submitting_trader_animecoin_identity_public_key, submitting_trader_animecoin_blockchain_address, assigned_masternode_broker_ip_address, assigned_masternode_broker_animecoin_identity_public_key, desired_trade_type, desired_art_asset_hash, desired_quantity, specified_price_in_anime)#This would happen on the artist/trader's machine.
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