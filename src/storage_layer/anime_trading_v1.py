import os.path, glob, hashlib, os, sqlite3, warnings, base64, locale
warnings.simplefilter(action='ignore', category=FutureWarning)
warnings.filterwarnings('ignore',category=DeprecationWarning)
import lxml.html
import shortuuid
import rsa
from anime_utility_functions_v1 import download_remote_file_func, get_all_remote_node_ips_func, get_my_local_ip_func, get_local_masternode_identification_keypair_func
from anime_file_transfer_v1 import get_list_of_available_files_on_remote_node_file_server_func
from datetime import datetime
#Requirements: pip install lxml, shortuuid, rsa

###############################################################################################################
# Functions:
###############################################################################################################
    
def generate_trade_id_func():
    shortuuid.set_alphabet('ANME0123456789')
    trade_id = shortuuid.uuid()[0:8]
    return trade_id

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