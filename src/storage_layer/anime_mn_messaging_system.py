import time, sys, random, base64, hashlib, glob, os
import nacl
import zstd
from anime_animecoin_id_self_contained_demo import import_animecoin_public_and_private_keys_from_pem_files_func, animecoin_id_keypair_generation_func, \
    write_animecoin_public_and_private_key_to_file_func, animecoin_id_write_signature_on_data_func, animecoin_id_verify_signature_with_public_key_func

def sleep_rand():
     time.sleep(0.05*random.random())

def get_sha3_512_func(input_data_or_string):
    if isinstance(input_data_or_string, str):
        input_data_or_string = input_data_or_string.encode('utf-8')
    hash_of_input_data = hashlib.sha3_512(input_data_or_string).hexdigest()
    return hash_of_input_data
    
def compress_data_with_zstd_func(input_data):
    zstd_compression_level = 22 #Highest (best) compression level is 22
    zstandard_compressor = zstd.ZstdCompressor(level=zstd_compression_level, write_content_size=True)
    if isinstance(input_data, str):
        input_data = input_data.encode('utf-8')
    zstd_compressed_data = zstandard_compressor.compress(input_data)
    return zstd_compressed_data

def decompress_data_with_zstd_func(zstd_compressed_data):
    zstandard_decompressor = zstd.ZstdDecompressor()
    uncompressed_data = zstandard_decompressor.decompress(zstd_compressed_data)
    return uncompressed_data

class messengerObject(object):
    sender_id = ''
    receiver_id = ''
    timestamp = ''
    random_nonce = ''
    message_body = ''
    
    def __init__(self, receiver_id, message_body):
        message_format_version = '1.00'
        sleep_rand()
        animecoin_id_public_key_b16_encoded, animecoin_id_private_key_b16_encoded = import_animecoin_public_and_private_keys_from_pem_files_func()
        if animecoin_id_public_key_b16_encoded == '':
            animecoin_id_private_key_b16_encoded, animecoin_id_public_key_b16_encoded = animecoin_id_keypair_generation_func()
            write_animecoin_public_and_private_key_to_file_func(animecoin_id_public_key_b16_encoded, animecoin_id_private_key_b16_encoded)
        self.animecoin_id_public_key_b16_encoded = animecoin_id_public_key_b16_encoded
        self.animecoin_id_private_key_b16_encoded = animecoin_id_private_key_b16_encoded
        self.sender_id = animecoin_id_public_key_b16_encoded
        self.receiver_id = receiver_id
        self.timestamp = time.time()
        self.message_format_version = message_format_version
        min_nonce_length = 500
        max_nonce_length = 1200
        max_message_size = 1000
        nonce_length = random.randint(min_nonce_length, max_nonce_length)
        self.random_nonce = base64.b64encode(nacl.utils.random(nonce_length)).decode('utf-8')
        if isinstance(message_body, str):
            print('Nonce Length:' + str(len(self.random_nonce)))
            message_size = len(message_body)
            if (message_size - nonce_length) < max_message_size:
                self.message_size = message_size
                self.message_body = message_body
            else:
                print('Error, message body is too large!')
                return
        else:
            return
            print('Error, message body must be a valid string.')
        
    def generate_combined_message(self):
        linebreak = '\n______________________\n'
        x = linebreak + '\nSTART_OF_MESSAGE:\n'
        x = x + linebreak + '\nsender_id:\n' + self.sender_id + linebreak
        x = x + '\nreceiver_id:\n' + self.receiver_id + linebreak
        x = x + '\ntimestamp:\n' + str(self.timestamp) + linebreak
        x = x + '\nmessage_format_version:\n' + self.message_format_version + linebreak
        x = x + '\nmessage_size:\n' + str(self.message_size) + linebreak
        x = x + '\nmessage_body:\n' + self.message_body + linebreak
        x = x + '\nrandom_nonce:\n' + self.random_nonce + linebreak
        x = x + '\nEND_OF_MESSAGE\n' + linebreak
        self.combined_message_string = x
        self.hash_of_combined_message_string = get_sha3_512_func(self.combined_message_string)
        sleep_rand()
        return self

    def sign_combined_raw_message(self):
        sleep_rand()
        self.signature_on_raw_message = animecoin_id_write_signature_on_data_func(self.hash_of_combined_message_string, self.animecoin_id_private_key_b16_encoded, self.animecoin_id_public_key_b16_encoded)
        signature_string = '\n\ndigital_signature: \n'
        self.signed_combined_message = self.combined_message_string + signature_string + self.signature_on_raw_message
        self.hash_of_signed_combined_message = get_sha3_512_func(self.signed_combined_message)
        sleep_rand()
        return self
            
    def compress_signed_raw_message(self):
        sleep_rand()
        self.compressed_signed_combined_message = compress_data_with_zstd_func(self.signed_combined_message)
        self.hash_of_compressed_signed_combined_message = get_sha3_512_func(self.compressed_signed_combined_message)
        sleep_rand()
        return self
    
    def sign_compressed_message(self):
        sleep_rand()
        self.signature_on_compressed_message = animecoin_id_write_signature_on_data_func(self.hash_of_compressed_signed_combined_message, self.animecoin_id_private_key_b16_encoded, self.animecoin_id_public_key_b16_encoded)
        sleep_rand()
        return self

    def write_raw_message_string_to_disk(self):
        if isinstance(self.hash_of_combined_message_string, str): 
            output_file_path = 'Raw_Message__ID__' + self.hash_of_combined_message_string + '.txt'
            self.last_exported_raw_message_file_path = output_file_path
            with open(output_file_path,'w') as f:
                f.write(self.signed_combined_message )
        return self
    
    def verify_raw_message_file(self, raw_message_contents):
        if isinstance(raw_message_contents, str):
            if len(raw_message_contents) < 5000:
                signature_string = '\n\ndigital_signature: \n'
                start_string = 'START_OF_MESSAGE'
                end_string = 'END_OF_MESSAGE'
                id_character_set = 'ABCDEF1234567890'
                nonce_character_set = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890+/='
                version_character_set = '1234567890.'
                assert(raw_message_contents[0]=='\n')
                assert(raw_message_contents.split(start_string)[0]=='\n______________________\n\n')
                assert(len(raw_message_contents.split('\n'))==42)
                assert(len(raw_message_contents.split(signature_string))==2)
                message_contents = raw_message_contents.split(signature_string)[0]
                assert(message_contents.split(end_string)[-1]=='\n\n______________________\n')
                assert(message_contents.replace('\n','')[-22:] == '______________________')
                hash_of_combined_message_string = get_sha3_512_func(message_contents)
                signature_line = raw_message_contents.split(signature_string)[-1].replace('\n','')
                assert(isinstance(signature_line, str))
                assert(len(signature_line)==264)
                assert([(x in id_character_set) for x in signature_line])
                message_contents_fields = message_contents.split('______________________')
                assert(len(message_contents_fields)==11)
                for current_field in message_contents_fields[1:-1]:
                    sender_string = '\n\nsender_id:\n'
                    assert(len(message_contents.split(sender_string))==2)
                    if current_field[:len(sender_string)] == sender_string:
                        senders_animecoin_id = current_field.replace(sender_string,'').replace('\n','')
                        assert(len(senders_animecoin_id)==132)
                        assert([(x in id_character_set) for x in senders_animecoin_id])
                    receiver_string = '\n\nreceiver_id:\n'
                    assert(len(message_contents.split(receiver_string))==2)
                    if current_field[:len(receiver_string)]==receiver_string:
                        receivers_animecoin_id = current_field.replace(receiver_string,'').replace('\n','')
                        assert(len(receivers_animecoin_id)==132)
                        assert([(x in id_character_set) for x in receivers_animecoin_id])
                    timestamp_string = '\n\ntimestamp:\n'
                    assert(len(message_contents.split(timestamp_string))==2)
                    if current_field[:len(timestamp_string)]==timestamp_string:
                        timestamp_of_message = float(current_field.replace(timestamp_string,'').replace('\n',''))
                        assert(timestamp_of_message > time.time() - 60)
                        assert(timestamp_of_message < time.time() + 60)
                    message_format_version_string = '\n\nmessage_format_version:\n'
                    assert(len(message_contents.split(message_format_version_string))==2)
                    if current_field[:len(message_format_version_string)]==message_format_version_string:
                        message_format_version = current_field.replace(message_format_version_string,'').replace('\n','')
                        assert(len(message_format_version) < 6)
                        assert([(x in version_character_set) for x in message_format_version])
                        assert('.' in message_format_version)
                    message_size_string = '\n\nmessage_size:\n'
                    assert(len(message_contents.split(message_size_string))==2)
                    if current_field[:len(message_size_string)]==message_size_string:
                        message_size = int(current_field.replace(message_size_string,'').replace('\n',''))
                        assert(message_size >= 10)
                        assert(message_size <= 1000)
                    message_body_string = '\n\nmessage_body:\n'
                    assert(len(message_contents.split(message_body_string))==2)
                    if current_field[:len(message_body_string)]==message_body_string:
                        message_body = current_field.replace(message_body_string,'').replace('\n','')
                        assert(len(message_body) == message_size)
                    random_nonce_string = '\n\nrandom_nonce:\n'
                    assert(len(message_contents.split(random_nonce_string))==2)
                    if current_field[:len(random_nonce_string)]==random_nonce_string:
                        random_nonce = current_field.replace(random_nonce_string,'').replace('\n','')
                        assert(len(random_nonce) >= 500)
                        assert(len(random_nonce) <= 2500)
                        assert([(x in nonce_character_set) for x in random_nonce])
                    sleep_rand()
                verified = animecoin_id_verify_signature_with_public_key_func(hash_of_combined_message_string, signature_line, senders_animecoin_id)
                sleep_rand()
                return verified, senders_animecoin_id, receivers_animecoin_id, timestamp_of_message, message_size, message_body, random_nonce, message_contents, signature_line
            
    def write_compressed_message_and_signature_to_disk(self):
        if isinstance(self.hash_of_compressed_signed_combined_message, str): 
            output_file_path = 'Compressed_Message__ID__' + self.hash_of_compressed_signed_combined_message + '.bin'
            self.last_exported_compressed_message_file_path = output_file_path
            with open(output_file_path,'wb') as f:
                f.write(self.compressed_signed_combined_message)
            output_file_path = 'Signature_for_Compressed_Message__ID__' + self.hash_of_compressed_signed_combined_message + '.txt'
            self.last_exported_compressed_signature_file_path = output_file_path
            with open(output_file_path,'w') as f:
                f.write(self.signature_on_compressed_message)
        return self
    
    def verify_compressed_message_file(self, senders_animecoin_id, compressed_binary_data, signature_on_compressed_file):
        if isinstance(compressed_binary_data, bytes):
            if sys.getsizeof(compressed_binary_data) < 2800:
                hash_of_compressed_message = get_sha3_512_func(compressed_binary_data)
                if len(senders_animecoin_id) == 132:
                    sleep_rand()
                    verified = animecoin_id_verify_signature_with_public_key_func(hash_of_compressed_message, signature_on_compressed_file, senders_animecoin_id)
                    sleep_rand()
                    if verified:
                        try:
                            decompressed_message_data = decompress_data_with_zstd_func(compressed_binary_data)
                            return decompressed_message_data.decode('utf-8')
                        except Exception as e:
                            print('Error: '+ str(e))
                            
    def read_unverified_compressed_message_file(self, compressed_binary_data):
        if isinstance(compressed_binary_data, bytes):
            if sys.getsizeof(compressed_binary_data) < 2800:
                try:
                    decompressed_message_data = decompress_data_with_zstd_func(compressed_binary_data)
                    return decompressed_message_data.decode('utf-8')
                except Exception as e:
                    print('Error: '+ str(e))
                    
def make_messenger_object_func(receiver_id, message_body):
    msgr = messengerObject(receiver_id, message_body)
    return msgr

def generate_message_func(messenger_object):
    x = messenger_object
    x = x.generate_combined_message()
    x = x.sign_combined_raw_message()
    x = x.compress_signed_raw_message()
    x = x.sign_compressed_message()
    x = x.write_raw_message_string_to_disk()
    x = x.write_compressed_message_and_signature_to_disk()
    messenger_object = x 
    return messenger_object

#For testing:
message_body = ""
message_body = "1234"
message_body = "Crud far in far oh immoral and more caribou hiccupped tyrannically tortoise rode sheepishly where gorilla metric radical the badger a and gosh smugly manatee devilishly that."
message_body = "Above occasional as sang however jeepers vengeful pounded dashingly smugly far studied anteater darn yet unbound more reprehensively and watchful hello ingenuously nightingale between less the much gloated then and less."
message_body = "Coasted more dipped ouch in hey stupid one monumental more so suddenly precisely and a far audible leniently ocelot thanks changed goodness toward well next jeez."
message_body = "Since grimaced modest rode unwound notwithstanding expressly one devilish that decided off alas as goodness wow wayward robin that a one customarily cassowary within spoiled."
message_body = "Beseechingly from much well reindeer glib erect nobly opossum and abject darn lemur so a in neutrally more indescribably meticulous that more after wow while jeez this roadrunner ouch for reasonable less unwittingly."
message_body = "This is a test of our new messaging system. We hope that it's secure from attacks. Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum."
if 'test_receiver__animecoin_id_private_key_b16_encoded' not in globals():
    test_receiver__animecoin_id_private_key_b16_encoded, test_receiver__animecoin_id_public_key_b16_encoded = animecoin_id_keypair_generation_func()
receiver_id = test_receiver__animecoin_id_public_key_b16_encoded

messenger_object = make_messenger_object_func(receiver_id, message_body)
messenger_object = generate_message_func(messenger_object)

path_of_most_recent_raw_message_file = max(glob.glob('Raw_Message__ID__*.txt'), key=os.path.getctime)
path_of_most_recent_compressed_file = max(glob.glob('Compressed_Message__ID__*.bin'), key=os.path.getctime)
path_of_most_recent_signature_file = max(glob.glob('Signature_for_Compressed_Message__ID__*.txt'), key=os.path.getctime)

print('Now verifying raw message data...')
with open(path_of_most_recent_raw_message_file, 'r') as f:
    try:
        print('Now reading raw message file: '+ path_of_most_recent_raw_message_file)
        raw_message_contents = f.read()
    except Exception as e:
        print('Error: '+ str(e))
verified, senders_animecoin_id, receivers_animecoin_id, timestamp_of_message, message_size, message_body, random_nonce, message_contents, signature_line = messenger_object.verify_raw_message_file(raw_message_contents)

if verified:
    print('Done! Message has been validated by confirming the digital signature matches the combined message hash.')
else:
    print('Error! Message is NOT valid!')
   
    
print('Now verifying compressed message data...')
with open(path_of_most_recent_compressed_file, 'rb') as f:
    try:
        print('Now reading compressed message file: '+ path_of_most_recent_compressed_file)
        compressed_message_contents = f.read()
    except Exception as e:
        print('Error: '+ str(e))
with open(path_of_most_recent_signature_file, 'r') as f:
    try:
        print('Now reading compressed signature file: '+ path_of_most_recent_signature_file)
        compressed_signature = f.read()
    except Exception as e:
        print('Error: '+ str(e))
senders_animecoin_id, _ = import_animecoin_public_and_private_keys_from_pem_files_func()
decompressed_message_data = messenger_object.verify_compressed_message_file(senders_animecoin_id, compressed_message_contents, compressed_signature)
verified, senders_animecoin_id, receivers_animecoin_id, timestamp_of_message, message_size, message_body, random_nonce, message_contents, signature_line = messenger_object.verify_raw_message_file(decompressed_message_data)
if verified:
    print('Done! Compressed message has been validated by confirming the digital signature matches the combined message hash.')
else:
    print('Error! Message is NOT valid!')
   
print('Now testing unverified compressed file read functionality...')
decompressed_message_data = messenger_object.read_unverified_compressed_message_file(compressed_message_contents)
if isinstance(decompressed_message_data, str):
    print('Successfully read unverified compressed file!')
else:
    print('Error!')














