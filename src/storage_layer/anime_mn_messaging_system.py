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
    zstd_compression_level = 18 #Highest (best) compression level is 22
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
        min_nonce_length = 200
        max_nonce_length = 600
        nonce_length = random.randint(min_nonce_length, max_nonce_length)
        self.random_nonce = base64.b64encode(nacl.utils.random(nonce_length)).decode('utf-8')
        if isinstance(message_body, str):
            max_message_size = 500
            message_size = sys.getsizeof(message_body)
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
        self.signed_combined_message = self.combined_message_string + '\n\ndigital_signature: \n' + self.signature_on_raw_message
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
            if sys.getsizeof(raw_message_contents) < 2000:
                message_contents = raw_message_contents.split('\n\ndigital_signature: \n')[0]
                hash_of_combined_message_string = get_sha3_512_func(message_contents)
                signature_line = raw_message_contents.split('\n\ndigital_signature: \n')[-1].replace('\n','')
                message_contents_fields = message_contents.split('______________________')
                for current_field in message_contents_fields[1:-1]:
                    sender_string = '\n\nsender_id:\n'
                    if current_field[:len(sender_string)] == sender_string:
                        senders_animecoin_id = current_field.replace(sender_string,'').replace('\n','')
                    receiver_string = '\n\nreceiver_id:\n'
                    if current_field[:len(receiver_string)] == receiver_string:
                        receivers_animecoin_id = current_field.replace(receiver_string,'').replace('\n','')   
                    timestamp_string = '\n\ntimestamp:\n'
                    if current_field[:len(timestamp_string)] == timestamp_string:
                        timestamp_of_message = float(current_field.replace(timestamp_string,'').replace('\n',''))          
                    message_size_string = '\n\nmessage_size:\n'
                    if current_field[:len(message_size_string)] == message_size_string:
                        message_size = int(current_field.replace(message_size_string,'').replace('\n',''))                                  
                    message_body_string = '\n\nmessage_body:\n'
                    if current_field[:len(message_body_string)] == message_body_string:
                        message_body = current_field.replace(message_body_string,'').replace('\n','')
                assert(timestamp_of_message > 1533673000)
                assert(len(senders_animecoin_id) == 132)
                assert(len(receivers_animecoin_id) == 132)
                assert(sys.getsizeof(message_body) == message_size)
                assert(isinstance(message_body, str))
                assert(isinstance(signature_line, str))
                assert(isinstance(senders_animecoin_id, str))
                sleep_rand()
                verified = animecoin_id_verify_signature_with_public_key_func(hash_of_combined_message_string, signature_line, senders_animecoin_id)
                sleep_rand()
                return verified, senders_animecoin_id, receivers_animecoin_id, timestamp_of_message, message_size, message_body, message_contents, signature_line
                   
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
            if sys.getsizeof(compressed_binary_data) < 2000:
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
            if sys.getsizeof(compressed_binary_data) < 2000:
                try:
                    decompressed_message_data = decompress_data_with_zstd_func(compressed_binary_data)
                    return decompressed_message_data.decode('utf-8')
                except Exception as e:
                    print('Error: '+ str(e))
                    
def make_messenger_object_func(receiver_id, message_body):
    msgr = messengerObject(receiver_id, message_body)
    return msgr

if 'test_receiver__animecoin_id_private_key_b16_encoded' not in globals():
    test_receiver__animecoin_id_private_key_b16_encoded, test_receiver__animecoin_id_public_key_b16_encoded = animecoin_id_keypair_generation_func()

message_body = "This is a test of our new messaging system. We hope that it's secure from attacks."
receiver_id = test_receiver__animecoin_id_public_key_b16_encoded
x = make_messenger_object_func(receiver_id, message_body)
x = x.generate_combined_message()
x = x.sign_combined_raw_message()
x = x.compress_signed_raw_message()
x = x.sign_compressed_message()
x = x.write_raw_message_string_to_disk()
x = x.write_compressed_message_and_signature_to_disk()
messenger_object = x 

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
verified, senders_animecoin_id, receivers_animecoin_id, timestamp_of_message, message_size, message_body, message_contents, signature_line = messenger_object.verify_raw_message_file(raw_message_contents)
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
verified, senders_animecoin_id, receivers_animecoin_id, timestamp_of_message, message_size, message_body, message_contents, signature_line = messenger_object.verify_raw_message_file(decompressed_message_data)
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














