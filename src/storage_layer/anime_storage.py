import time, sys, os.path, io, glob, hashlib, platform, imghdr, random, os, sqlite3, warnings, base64, json, pickle
warnings.simplefilter(action='ignore', category=FutureWarning)
warnings.filterwarnings("ignore",category=DeprecationWarning)
import rsa
import tensorflow as tf
import numpy as np
import h5py
from struct import pack, unpack
from shutil import copyfile
from fs.memoryfs import MemoryFS
from fs.copy import copy_fs
from fs.osfs import OSFS
from random import randint
from math import ceil, floor, sqrt, log
from collections import defaultdict
from zipfile import ZipFile
from tqdm import tqdm
from subprocess import check_output
from PIL import Image
from keras.models import Sequential
from keras.optimizers import SGD
from keras.layers.core import Layer
from keras.layers import merge, Input, Dense, Convolution2D, Conv2D, MaxPooling2D, AveragePooling2D, ZeroPadding2D, Dropout, Flatten, merge, Reshape, Activation
from keras.layers.normalization import BatchNormalization
from keras.models import Model
from keras.engine import InputSpec
from keras import initializers
from keras import backend as K
os.environ['TF_CPP_MIN_LOG_LEVEL'] = '2'
sys.setrecursionlimit(3000)

#Requirements: pip install tqdm, fs, rsa, numpy, tensorflow, keras, h5py==2.8.0rc1
warnings.resetwarnings()

def gen_tau(S, K, delta):
    """The Robust part of the RSD, we precompute an array for speed"""
    pivot = floor(K/S)
    return [S/K * 1/d for d in range(1, pivot)] \
            + [S/K * log(S/delta)] \
            + [0 for d in range(pivot, K)] 

def gen_rho(K):
    """The Ideal Soliton Distribution, we precompute an array for speed"""
    return [1/K] + [1/(d*(d-1)) for d in range(2, K+1)]

def gen_mu(K, delta, c):
    """The Robust Soliton Distribution on the degree of transmitted blocks"""
    S = c * log(K/delta) * sqrt(K) 
    tau = gen_tau(S, K, delta)
    rho = gen_rho(K)
    normalizer = sum(rho) + sum(tau)
    return [(rho[d] + tau[d])/normalizer for d in range(K)]

def gen_rsd_cdf(K, delta, c):
    """The CDF of the RSD on block degree, precomputed for sampling speed"""
    mu = gen_mu(K, delta, c)
    return [sum(mu[:d+1]) for d in range(K)]

class PRNG(object):
    """A Pseudorandom Number Generator that yields samples from the set of source blocks using the RSD degree distribution described above. """
    def __init__(self, params):
        """Provide RSD parameters on construction """
        self.state = None  # Seed is set by interfacing code using set_seed
        K, delta, c = params
        self.K = K
        self.cdf = gen_rsd_cdf(K, delta, c)

    def _get_next(self):
        """Executes the next iteration of the PRNG evolution process, and returns the result"""
        PRNG_A = 16807
        PRNG_M = (1 << 31) - 1
        self.state = PRNG_A * self.state % PRNG_M
        return self.state

    def _sample_d(self):
        """Samples degree given the precomputed distributions above and the linear PRNG output """
        PRNG_M = (1 << 31) - 1
        PRNG_MAX_RAND = PRNG_M - 1
        p = self._get_next() / PRNG_MAX_RAND
        for ix, v in enumerate(self.cdf):
            if v > p:
                return ix + 1
        return ix + 1

    def set_seed(self, seed):
        """Reset the state of the PRNG to the given seed"""
        self.state = seed

    def get_src_blocks(self, seed=None):
        """Returns the indices of a set of `d` source blocks sampled from indices i = 1, ..., K-1 uniformly, where `d` is sampled from the RSD described above.     """
        if seed:
            self.state = seed
        blockseed = self.state
        d = self._sample_d()
        have = 0
        nums = set()
        while have < d:
            num = self._get_next() % self.K
            if num not in nums:
                nums.add(num)
                have += 1
        return blockseed, d, nums
  
def _split_file(f, blocksize):
    """Block file byte contents into blocksize chunks, padding last one if necessary
    """
    f_bytes = f.read()
    blocks = [int.from_bytes(f_bytes[i:i+blocksize].ljust(blocksize, b'0'), sys.byteorder) 
            for i in range(0, len(f_bytes), blocksize)]
    return len(f_bytes), blocks

class CheckNode(object):
     def __init__(self, src_nodes, check):
        self.check = check
        self.src_nodes = src_nodes

class BlockGraph(object):
    """Graph on which we run Belief Propagation to resolve source node data"""
    def __init__(self, num_blocks):
        self.checks = defaultdict(list)
        self.num_blocks = num_blocks
        self.eliminated = {}

    def add_block(self, nodes, data):
        """Adds a new check node and edges between that node and all source nodes it connects, resolving all message passes that become possible as a result. """
        # We can eliminate this source node
        if len(nodes) == 1:
            to_eliminate = list(self.eliminate(next(iter(nodes)), data))
            # Recursively eliminate all nodes that can now be resolved
            while len(to_eliminate):
                other, check = to_eliminate.pop()
                to_eliminate.extend(self.eliminate(other, check))
        else:
            # Pass messages from already-resolved source nodes
            for node in list(nodes):
                if node in self.eliminated:
                    nodes.remove(node)
                    data ^= self.eliminated[node]

            # Resolve if we are left with a single non-resolved source node
            if len(nodes) == 1:
                return self.add_block(nodes, data)
            else:
                # Add edges for all remaining nodes to this check
                check = CheckNode(nodes, data)
                for node in nodes:
                    self.checks[node].append(check)
        # Are we done yet?
        return len(self.eliminated) >= self.num_blocks

    def eliminate(self, node, data):
        """Resolves a source node, passing the message to all associated checks """
        # Cache resolved value
        self.eliminated[node] = data
        others = self.checks[node]
        del self.checks[node]
        # Pass messages to all associated checks
        for check in others:
            check.check ^= data
            check.src_nodes.remove(node)
            # Yield all nodes that can now be resolved
            if len(check.src_nodes) == 1:
                yield (next(iter(check.src_nodes)), check.check)

#Various Utility Functions:
def regenerate_sqlite_chunk_database_func():
    global chunk_db_file_path
    try:
        conn = sqlite3.connect(chunk_db_file_path)
        c = conn.cursor()
        local_hash_table_creation_string= """CREATE TABLE potential_local_hashes (block_hash text PRIMARY KEY, file_hash);"""
        global_hash_table_creation_string= """CREATE TABLE potential_global_hashes (block_hash text, file_hash text, remote_node_ip text, remote_node_id text, datetime_peer_last_seen TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, PRIMARY KEY (block_hash, remote_node_id));"""
        node_ip_to_id_table_creation_string= """CREATE TABLE node_ip_to_id_table (remote_node_id text PRIMARY KEY, remote_node_ip text, datetime_peer_last_seen TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL);"""
        c.execute(local_hash_table_creation_string)
        c.execute(global_hash_table_creation_string)
        c.execute(node_ip_to_id_table_creation_string)
        conn.commit()
    except Exception as e:
        print('Error: '+ str(e))

def make_decoded_file_destination_directory_func():
    global decoded_file_destination_folder_path
    success = 0
    if not os.path.exists(decoded_file_destination_folder_path):
        try:
            os.makedirs(decoded_file_destination_folder_path)
            success = 1
            return success
        except Exception as e:
            print('Error: '+ str(e)) 
            return success

def randomly_delete_percentage_of_local_block_files_func(percentage_of_block_files_to_randomly_delete):
    global block_storage_folder_path
    list_of_block_file_paths = glob.glob(block_storage_folder_path+'*.block')
    number_of_deleted_blocks = 0
    pbar = tqdm(total=ceil(percentage_of_block_files_to_randomly_delete*len(list_of_block_file_paths)))
    print('\nNow deleting random block files!\n')
    for current_file_path in list_of_block_file_paths:
        if random.random() <= percentage_of_block_files_to_randomly_delete:
            try:
                os.remove(current_file_path)
                number_of_deleted_blocks = number_of_deleted_blocks + 1
                pbar.update(1)
            except OSError:
                pass
    return number_of_deleted_blocks
            
def randomly_corrupt_a_percentage_of_bytes_in_a_random_sample_of_block_files_func(percentage_of_block_files_to_randomly_corrupt,percentage_of_each_selected_file_to_be_randomly_corrupted):
    global block_storage_folder_path
    list_of_block_file_paths = glob.glob(block_storage_folder_path+'*.block')
    number_of_corrupted_blocks = 0
    for current_file_path in list_of_block_file_paths:
        if random.random() <= percentage_of_block_files_to_randomly_corrupt:
            number_of_corrupted_blocks = number_of_corrupted_blocks + 1
            total_bytes_of_data_in_chunk = os.path.getsize(current_file_path)
            random_bytes_to_write = 5
            number_of_bytes_to_corrupt = ceil(total_bytes_of_data_in_chunk*percentage_of_affected_file_to_be_randomly_corrupted)
            specific_bytes_to_corrupt = random.sample(range(1, total_bytes_of_data_in_chunk), ceil(number_of_bytes_to_corrupt/random_bytes_to_write))
            print('\n\nNow intentionally corrupting block file: ' + current_file_path.split('\\')[-1] )
            with open(current_file_path,'wb') as f:
                try:
                    for byte_index in specific_bytes_to_corrupt:
                        f.seek(byte_index)
                        f.write(os.urandom(random_bytes_to_write))
                except OSError:
                    pass
    return number_of_corrupted_blocks

def delete_all_blocks_and_zip_files_to_reset_system_func():
    global block_storage_folder_path
    global decoded_file_destination_folder_path
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
    previous_reconstructed_zip_file_paths = glob.glob(decoded_file_destination_folder_path+'*.zip')
    for current_existing_zip_file_path in previous_reconstructed_zip_file_paths:
        try:
            os.remove(current_existing_zip_file_path)
        except:
            pass

#Artist digital signature functions:
def generate_artist_public_and_private_keys_func():
    (artist_public_key, artist_private_key) = rsa.newkeys(512)
    return artist_public_key, artist_private_key

def sign_art_file_with_artist_signature_func(sha256_hash_of_art_file, artist_private_key):
    sha256_hash_of_art_file_utf8_encoded = sha256_hash_of_art_file.encode('utf-8')
    artist_signature_for_art_file = rsa.sign(sha256_hash_of_art_file_utf8_encoded, artist_private_key, 'SHA-256')
    return artist_signature_for_art_file

def verify_artist_signature_on_art_file_func(sha256_hash_of_art_file, artist_public_key, artist_signature_for_art_file):
    verified = 0
    sha256_hash_of_art_file_utf8_encoded = sha256_hash_of_art_file.encode('utf-8')
    try:
        rsa.verify(sha256_hash_of_art_file_utf8_encoded, artist_signature_for_art_file, artist_public_key)
        verified = 1
        return verified
    except Exception as e:
        print('Error: '+ str(e))
        return verified
    
def sign_all_art_files_in_folder_with_artists_digital_signature_func(path_to_art_folder,artist_public_key,artist_private_key):
    sqlite_file_path = path_to_art_folder+'artists_signatures_file.sig'
    if os.path.exists(sqlite_file_path):
        try:
            os.remove(sqlite_file_path)
        except:
            print('Could not remove existing signature file! Please remove manually.')
    art_input_file_paths =  glob.glob(path_to_art_folder+'*.jpg') + glob.glob(path_to_art_folder+'*.png') + glob.glob(path_to_art_folder+'*.bmp') + glob.glob(path_to_art_folder+'*.gif')
    list_of_art_file_hashes = []
    list_of_art_file_paths = []
    list_of_artist_signatures_for_each_art_file_hash = []
    for current_file_path in art_input_file_paths:
        if (imghdr.what(current_file_path) == 'gif') or (imghdr.what(current_file_path) == 'jpeg') or (imghdr.what(current_file_path) == 'png') or (imghdr.what(current_file_path) == 'bmp'):
           with open(current_file_path,'rb') as f:
               current_art_file = f.read()
           sha256_hash_of_current_art_file = hashlib.sha256(current_art_file).hexdigest()                    
           artist_signature_for_art_file = sign_art_file_with_artist_signature_func(sha256_hash_of_current_art_file,artist_private_key)
           list_of_art_file_hashes.append(sha256_hash_of_current_art_file)
           list_of_art_file_paths.append(current_file_path)
           list_of_artist_signatures_for_each_art_file_hash.append(artist_signature_for_art_file)
    if len(list_of_art_file_hashes) > 0:
        try:
            conn = sqlite3.connect(sqlite_file_path)
            c = conn.cursor()
            artist_signature_table_creation_string= """CREATE TABLE artist_signatures_table (artist_signature BLOB, sha256_hash_of_art_file text, datetime_art_was_signed TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, PRIMARY KEY (artist_signature, sha256_hash_of_art_file));"""
            c.execute(artist_signature_table_creation_string)
            for cnt, current_art_file_hash in enumerate(list_of_art_file_hashes):
                current_artist_signature = list_of_artist_signatures_for_each_art_file_hash[cnt]
                update_table_data_query_string = """INSERT OR REPLACE INTO artist_signatures_table (artist_signature, sha256_hash_of_art_file) VALUES (?,?);"""
                c.execute(update_table_data_query_string,[sqlite3.Binary(current_artist_signature), current_art_file_hash]) 
            concatenated_file_hashes = ''.join(list_of_art_file_hashes).encode('utf-8')
            hash_of_the_concatenated_file_hashes = hashlib.sha256(concatenated_file_hashes).hexdigest()
            artist_signature_for_hash_of_the_concatenated_hashes = sign_art_file_with_artist_signature_func(hash_of_the_concatenated_file_hashes,artist_private_key)
            concatenated_hash_signature_table_creation_string= """CREATE TABLE concatenated_hash_signature (artist_signature BLOB, hash_of_the_concatenated_hashes text, datetime_art_was_signed TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, PRIMARY KEY (artist_signature, hash_of_the_concatenated_hashes));"""
            c.execute(concatenated_hash_signature_table_creation_string)
            update_concatenated_hash_query_string = """INSERT OR REPLACE INTO concatenated_hash_signature (artist_signature, hash_of_the_concatenated_hashes) VALUES (?,?);"""
            c.execute(update_concatenated_hash_query_string,[sqlite3.Binary(artist_signature_for_hash_of_the_concatenated_hashes), hash_of_the_concatenated_file_hashes]) 
            artist_public_key_base64encoded = rsa.PublicKey.save_pkcs1(artist_public_key,format='PEM')
            artist_public_key_base64encoded_table_creation_string = """CREATE TABLE artist_public_key_table (artist_public_key text);"""
            c.execute(artist_public_key_base64encoded_table_creation_string)
            artist_public_key_insert_string = """INSERT OR REPLACE INTO artist_public_key_table (artist_public_key) VALUES (?);"""
            c.execute(artist_public_key_insert_string,[artist_public_key_base64encoded]) 
            conn.commit()
            print('Successfully wrote signature file to disk!')
        except Exception as e:
            print('Error: '+ str(e))
    return list_of_artist_signatures_for_each_art_file_hash, list_of_art_file_hashes, list_of_art_file_paths 

def verify_artist_signatures_in_art_folder_func(path_to_art_folder,artist_public_key):
    sqlite_file_path = path_to_art_folder+'artists_signatures_file.sig'
    if not os.path.exists(sqlite_file_path):
        print('Error, could not find signature file!')
        return
    try:
        conn = sqlite3.connect(sqlite_file_path)
        c = conn.cursor()
        artist_signature_query_string = c.execute("""SELECT artist_signature, sha256_hash_of_art_file, datetime_art_was_signed FROM artist_signatures_table ORDER BY datetime_art_was_signed DESC""").fetchall()
        list_of_artist_signatures_for_each_art_file_hash = [x[0] for x in artist_signature_query_string]
        list_of_sha256_hashes_of_art_files = [x[1] for x in artist_signature_query_string]
    except Exception as e:
        print('Error: '+ str(e))
    verified_counter = 0
    for cnt, current_art_file_hash in enumerate(list_of_sha256_hashes_of_art_files):
        current_digital_signature = list_of_artist_signatures_for_each_art_file_hash[cnt]
        print('\nArt File Hash: '+current_art_file_hash)
        try:
            verified = verify_artist_signature_on_art_file_func(current_art_file_hash,artist_public_key,current_digital_signature)
            if verified == 1:
                verified_counter = verified_counter + 1
                print('Successfully verified the artist signature for art file with SHA256 has of '+current_art_file_hash +'!\n')
        except Exception as e:
            print('Unable to verify signature!')
            print('Error: '+ str(e))
    if verified_counter == len(list_of_sha256_hashes_of_art_files):
        all_verified = 1
        print('Finished verifying art folder '+path_to_art_folder)
    else:
        all_verified = 0
        print('One or more art files could NOT be verified as being correctly signed with the correct digital signature!')
    return all_verified

#Artwork Metadata Functions:
def create_metadata_file_for_given_art_folder_func(path_to_art_folder,
                                                  artist_name,
                                                  artwork_title,
                                                  artwork_max_quantity,
                                                  artwork_series_name='',
                                                  artist_website='',
                                                  artwork_artist_statement=''):
    success = 0
    artist_signatures_sqlite_file_path = path_to_art_folder+'artists_signatures_file.sig'
    try:#First get the digital signature and file hash data:
        conn = sqlite3.connect(artist_signatures_sqlite_file_path)
        c = conn.cursor()
        artist_signature_query_results= c.execute("""SELECT artist_signature, sha256_hash_of_art_file, datetime_art_was_signed FROM artist_signatures_table ORDER BY datetime_art_was_signed DESC""").fetchall()
        list_of_artist_signatures_for_each_art_file_hash = [base64.b64encode(x[0]).decode('utf-8') for x in artist_signature_query_results]
        list_of_sha256_hashes_of_art_files = [x[1] for x in artist_signature_query_results]
        list_of_artist_signatures_for_each_art_file_hash_json = json.dumps(list_of_artist_signatures_for_each_art_file_hash)
        list_of_sha256_hashes_of_art_files_json = json.dumps(list_of_sha256_hashes_of_art_files)
        artist_public_key_query_results= c.execute("""SELECT artist_public_key FROM artist_public_key_table""").fetchall()
        artist_public_key_query_results = artist_public_key_query_results[0][0]
        artist_public_key = artist_public_key_query_results.decode('utf-8')
        artist_concatenated_hash_signature_table_results = c.execute("""SELECT artist_signature, hash_of_the_concatenated_hashes, datetime_art_was_signed FROM concatenated_hash_signature ORDER BY datetime_art_was_signed DESC""").fetchall()[0]
        artist_concatenated_hash_signature = artist_concatenated_hash_signature_table_results[0]
        artist_concatenated_hash_signature_base64_encoded = base64.b64encode(artist_concatenated_hash_signature).decode('utf-8')
        hash_of_the_concatenated_file_hashes = artist_concatenated_hash_signature_table_results[1]
        datetime_art_was_signed = artist_concatenated_hash_signature_table_results[2]
        c.close()
    except Exception as e:
        print('Error: '+ str(e))
    artwork_metadata_sqlite_file_path = path_to_art_folder+'artwork_metadata_file__hash_ '+hash_of_the_concatenated_file_hashes+'.db'
    try:
        conn = sqlite3.connect(artwork_metadata_sqlite_file_path)
        c = conn.cursor()
        artwork_metadata__table_creation_string= """
        CREATE TABLE artwork_metadata_table (
        artist_public_key TEXT,
        artist_concatenated_hash_signature_base64_encoded TEXT, 
        hash_of_the_concatenated_file_hashes TEXT,
        datetime_art_was_signed TIMESTAMP, 
        artist_name TEXT,
        artwork_title TEXT, 
        artwork_max_quantity INTEGER,
        artwork_series_name TEXT,
        artist_website TEXT,
        artwork_artist_statement TEXT,
        PRIMARY KEY (artist_concatenated_hash_signature_base64_encoded, hash_of_the_concatenated_file_hashes));"""
        try:
            c.execute(artwork_metadata__table_creation_string)
        except Exception as e:
                print('Error: '+ str(e))
        update_table_data_query_string = """
        INSERT OR REPLACE INTO artwork_metadata_table (
        artist_concatenated_hash_signature_base64_encoded, 
        hash_of_the_concatenated_file_hashes,
        datetime_art_was_signed, 
        artist_name,
        artwork_title, 
        artwork_max_quantity,
        artwork_series_name,
        artist_website,
        artwork_artist_statement) VALUES (?,?,?,?,?,?,?,?,?);"""
        c.execute(update_table_data_query_string,[artist_concatenated_hash_signature_base64_encoded, 
                                                  hash_of_the_concatenated_file_hashes, 
                                                  datetime_art_was_signed, 
                                                  artist_name, 
                                                  artwork_title,
                                                  int(artwork_max_quantity),
                                                  artwork_series_name,
                                                  artist_website,
                                                  artwork_artist_statement])
        conn.commit()
        print('Successfully wrote metadata file to disk!')
        success = 1
        return success
    except Exception as e:
        print('Error: '+ str(e))
        return success
    
def read_artwork_metadata_from_metadata_file(path_to_artwork_metadata_file):
    try:
        conn = sqlite3.connect(path_to_artwork_metadata_file)
        c = conn.cursor()
        metadata_query_results_table = c.execute("""SELECT * FROM artwork_metadata_table ORDER BY datetime_art_was_signed DESC""").fetchall()
        metadata_query_results_table = metadata_query_results_table[0]
        artist_concatenated_hash_signature_base64_encoded = metadata_query_results_table[1]
        hash_of_the_concatenated_file_hashes = metadata_query_results_table[2]
        datetime_art_was_signed = metadata_query_results_table[3]
        artist_name = metadata_query_results_table[4]
        artwork_title = metadata_query_results_table[5] 
        artwork_max_quantity = metadata_query_results_table[6]
        artwork_series_name = metadata_query_results_table[7]
        artist_website = metadata_query_results_table[8]
        artwork_artist_statement = metadata_query_results_table[9]
        return artist_concatenated_hash_signature_base64_encoded, hash_of_the_concatenated_file_hashes, datetime_art_was_signed, artist_name, artwork_title, artwork_max_quantity, artwork_series_name, artist_website, artwork_artist_statement
    except Exception as e:
        print('Error: '+ str(e))
        
def read_artwork_metadata_from_art_folder_func(path_to_art_folder): #Convenience function
    artwork_metadata_sqlite_file_path = path_to_art_folder+'artwork_metadata_file__hash_*.db'
    if os.path.exists(artwork_metadata_sqlite_file_path):
        artist_concatenated_hash_signature_base64_encoded, hash_of_the_concatenated_file_hashes, datetime_art_was_signed, artist_name, artwork_title, artwork_max_quantity, artwork_series_name, artist_website, artwork_artist_statement = read_artwork_metadata_from_metadata_file(path_to_artwork_metadata_file)
        return artist_concatenated_hash_signature_base64_encoded, hash_of_the_concatenated_file_hashes, datetime_art_was_signed, artist_name, artwork_title, artwork_max_quantity, artwork_series_name, artist_website, artwork_artist_statement

#NSFW Function:
def check_art_folder_for_nsfw_content(path_to_art_folder):
   global nsfw_score_threshold
   start_time = time.time()
   art_input_file_paths =  glob.glob(path_to_art_folder+'*.jpg') + glob.glob(path_to_art_folder+'*.png') + glob.glob(path_to_art_folder+'*.bmp') + glob.glob(path_to_art_folder+'*.gif')
   list_of_art_file_hashes = []
   list_of_nsfw_scores = []
   label_lines = [line.rstrip() for line in tf.gfile.GFile('retrained_labels.txt')] # Loads label file, strips off carriage return
   print('Now loading NSFW detection TensorFlow Library...')
   with tf.gfile.FastGFile('retrained_graph.pb', 'rb') as f:# Unpersists graph from file
       graph_def = tf.GraphDef()
       graph_def.ParseFromString(f.read())
       tf.import_graph_def(graph_def, name='')
       print('Done!')
   for current_file_path in art_input_file_paths:
      file_base_name = current_file_path.split('\\')[-1].replace(' ','_').lower()
      if (imghdr.what(current_file_path) == 'gif') or (imghdr.what(current_file_path) == 'jpeg') or (imghdr.what(current_file_path) == 'png') or (imghdr.what(current_file_path) == 'bmp'):
         with open(current_file_path,'rb') as f:
            current_art_file = f.read()
      sha256_hash_of_current_art_file = hashlib.sha256(current_art_file).hexdigest()                    
      print('\nCurrent Art Image File Name: '+file_base_name+' \nImage SHA256 Hash: '+sha256_hash_of_current_art_file+'\n')            
      list_of_art_file_hashes.append(sha256_hash_of_current_art_file)
      image_data = tf.gfile.FastGFile(current_file_path, 'rb').read()
      with tf.Session() as sess:
          softmax_tensor = sess.graph.get_tensor_by_name('final_result:0')    # Feed the image_data as input to the graph and get first prediction
          predictions = sess.run(softmax_tensor,  {'DecodeJpeg/contents:0': image_data})
          top_k = predictions[0].argsort()[-len(predictions[0]):][::-1]  # Sort to show labels of first prediction in order of confidence
          for node_id in top_k:
              human_string = label_lines[node_id]
              nsfw_score = predictions[0][node_id]
              list_of_nsfw_scores.append(nsfw_score)
              print('%s (score = %.5f)' % (human_string, nsfw_score))
              if nsfw_score > nsfw_score_threshold:
                  print('\n\n***Warning, current image is NFSW!***\n\n')
   return list_of_art_file_hashes, list_of_nsfw_scores
   duration_in_seconds = round(time.time() - start_time, 1)
   print('\n\nFinished processing in '+str(duration_in_seconds) + ' seconds!')

#Dupe detection helper functions:
class Scale(Layer):
    '''Learns a set of weights and biases used for scaling the input data. '''
    def __init__(self, weights=None, axis=-1, momentum = 0.9, beta_init='zero', gamma_init='one', **kwargs):
        self.momentum = momentum
        self.axis = axis 
        self.beta_init = initializers.get(beta_init)
        self.gamma_init = initializers.get(gamma_init)
        self.initial_weights = weights
        super(Scale, self).__init__(**kwargs)

    def build(self, input_shape):
        self.input_spec = [InputSpec(shape=input_shape)]
        shape = (int(input_shape[self.axis]),)
        self.gamma = K.variable(self.gamma_init(shape), name='{}_gamma'.format(self.name))
        self.beta = K.variable(self.beta_init(shape), name='{}_beta'.format(self.name))
        self.trainable_weights = [self.gamma, self.beta]
        if self.initial_weights is not None:
            self.set_weights(self.initial_weights)
            del self.initial_weights

    def call(self, x, mask=None):
        input_shape = self.input_spec[0].shape
        broadcast_shape = [1] * len(input_shape)
        broadcast_shape[self.axis] = input_shape[self.axis]
        out = K.reshape(self.gamma, broadcast_shape) * x + K.reshape(self.beta, broadcast_shape)
        return out

    def get_config(self):
        config = {"momentum": self.momentum, "axis": self.axis}
        base_config = super(Scale, self).get_config()
        return dict(list(base_config.items()) + list(config.items()))

def identity_block(input_tensor, kernel_size, filters, stage, block):
    '''The identity_block is the block that has no conv layer at shortcut'''
    eps = 1.1e-5
    nb_filter1, nb_filter2, nb_filter3 = filters
    conv_name_base = 'res' + str(stage) + block + '_branch'
    bn_name_base = 'bn' + str(stage) + block + '_branch'
    scale_name_base = 'scale' + str(stage) + block + '_branch'
    x = Conv2D(64, (1, 1), name=conv_name_base + '2a', use_bias=False)(input_tensor)
    x = BatchNormalization(epsilon=eps, axis=bn_axis, name=bn_name_base + '2a')(x)
    x = Scale(axis=bn_axis, name=scale_name_base + '2a')(x)
    x = Activation('relu', name=conv_name_base + '2a_relu')(x)
    x = ZeroPadding2D((1, 1), name=conv_name_base + '2b_zeropadding')(x)
    x = Conv2D(64, (3, 3), name=conv_name_base + '2b', use_bias=False)(x)
    x = BatchNormalization(epsilon=eps, axis=bn_axis, name=bn_name_base + '2b')(x)
    x = Scale(axis=bn_axis, name=scale_name_base + '2b')(x)
    x = Activation('relu', name=conv_name_base + '2b_relu')(x)
    x = Conv2D(256, (1, 1), name=conv_name_base + '2c', use_bias=False)(x)
    x = BatchNormalization(epsilon=eps, axis=bn_axis, name=bn_name_base + '2c')(x)
    x = Scale(axis=bn_axis, name=scale_name_base + '2c')(x)
    x = merge([x, input_tensor], mode='sum', name='res' + str(stage) + block)
    x = Activation('relu', name='res' + str(stage) + block + '_relu')(x)
    return x

def conv_block(input_tensor, kernel_size, filters, stage, block, strides=(2, 2)):
    '''conv_block is the block that has a conv layer at shortcut'''
    eps = 1.1e-5
    nb_filter1, nb_filter2, nb_filter3 = filters
    conv_name_base = 'res2' + str(stage) + block + '_branch'
    bn_name_base = 'bn2' + str(stage) + block + '_branch'
    scale_name_base = 'scale2' + str(stage) + block + '_branch'
    x = Conv2D(64, (1, 1), name=conv_name_base + '2a', use_bias=False)(input_tensor)
    x = BatchNormalization(epsilon=eps, axis=bn_axis, name=bn_name_base + '2a')(x)
    x = Scale(axis=bn_axis, name=scale_name_base + '2a')(x)
    x = Activation('relu', name=conv_name_base + '2a_relu')(x)
    x = ZeroPadding2D((1, 1), name=conv_name_base + '2b_zeropadding')(x)
    x = Conv2D(64, (3, 3), name=conv_name_base + '2b', use_bias=False)(x)
    x = BatchNormalization(epsilon=eps, axis=bn_axis, name=bn_name_base + '2b')(x)
    x = Scale(axis=bn_axis, name=scale_name_base + '2b')(x)
    x = Activation('relu', name=conv_name_base + '2b_relu')(x)
    x = Conv2D(256, (1, 1), name=conv_name_base + '2c', use_bias=False)(x)
    x = BatchNormalization(epsilon=eps, axis=bn_axis, name=bn_name_base + '2c')(x)
    x = Scale(axis=bn_axis, name=scale_name_base + '2c')(x)
    shortcut = Conv2D(256, (1, 1), name=conv_name_base + '1', strides=(1, 1), use_bias=False)(input_tensor)
    shortcut = BatchNormalization(epsilon=eps, axis=bn_axis, name=bn_name_base + '1')(shortcut)
    shortcut = Scale(axis=bn_axis, name=scale_name_base + '1')(shortcut)
    x = merge([x, shortcut], mode='sum', name='res' + str(stage) + block)
    x = merge([x, shortcut], mode='sum', name='res' + str(stage) + block)
    x = Activation('relu', name='res' + str(stage) + block + '_relu')(x)
    return x
    
def resnet152_model(img_rows, img_cols, color_type=3, num_classes=None):
    """Resnet 152 Model for Keras  Parameters: img_rows, img_cols - resolution of inputs; color_type 3 for color (1 for gs); num_classes - number of class labels for our classification task"""
    global bn_axis
    bn_axis = 3
    eps = 1.1e-5
    img_input = Input(shape=(img_rows, img_cols, color_type), name='data')
    x = ZeroPadding2D((3, 3), name='conv1_zeropadding')(img_input)
    #x = Convolution2D(64, 7, 7, subsample=(2, 2), name='conv1', bias=False)(x)
    x = Conv2D(64, (7, 7), name="conv1", strides=(2, 2), use_bias=False)(x)
    x = BatchNormalization(epsilon=eps, axis=bn_axis, name='bn_conv1')(x)
    x = Scale(axis=bn_axis, name='scale_conv1')(x)
    x = Activation('relu', name='conv1_relu')(x)
    x = MaxPooling2D((3, 3), strides=(2, 2), name='pool1')(x)
    x = conv_block(x, 3, [64, 64, 256], stage=2, block='a', strides=(1, 1))
    x = identity_block(x, 3, [64, 64, 256], stage=2, block='b')
    x = identity_block(x, 3, [64, 64, 256], stage=2, block='c')
    x = conv_block(x, 3, [128, 128, 512], stage=3, block='a')
    for i in range(1,8):
      x = identity_block(x, 3, [128, 128, 512], stage=3, block='b'+str(i))
    x = conv_block(x, 3, [256, 256, 1024], stage=4, block='a')
    for i in range(1,36):
      x = identity_block(x, 3, [256, 256, 1024], stage=4, block='b'+str(i))
    x = conv_block(x, 3, [512, 512, 2048], stage=5, block='a')
    x = identity_block(x, 3, [512, 512, 2048], stage=5, block='b')
    x = identity_block(x, 3, [512, 512, 2048], stage=5, block='c')
    x_fc = AveragePooling2D((7, 7), name='avg_pool')(x)
    x_fc = Flatten()(x_fc)
    x_fc = Dense(1000, activation='softmax', name='fc1000')(x_fc)
    model = Model(img_input, x_fc)
    weights_path = 'resnet152_weights_tf.h5'# Use pre-trained weights for Tensorflow backend
    model.load_weights(weights_path, by_name=True)
    x_newfc = AveragePooling2D((7, 7), name='avg_pool')(x) # Truncate and replace softmax layer for transfer learning;  Cannot use model.layers.pop() since model is not of Sequential() type;The method below works since pre-trained weights are stored in layers but not in the model 
    x_newfc = Flatten()(x_newfc)
    x_newfc = Dense(num_classes, activation='softmax', name='fc8')(x_newfc)
    model = Model(img_input, x_newfc)
    sgd = SGD(lr=1e-3, decay=1e-6, momentum=0.9, nesterov=True)    # Learning rate is changed to 0.001
    model.compile(optimizer=sgd, loss='categorical_crossentropy', metrics=['accuracy'])
    return model

def add_to_duplicate_detection_model_func(path_to_art_image_file):
    global path_to_all_registered_works_for_dupe_detection
    img = Image.open(path_to_art_image_file)
    resized_width = 5000
    resized_height = 5000
    original_height, original_width = img.size
    resized_image_matrix = img.resize((resized_width,resized_height), Image.ANTIALIAS)
    #img.save('resized.png') 
    number_of_channels = 3
    number_of_classes = 1000 
    dummy_label = np.ones(number_of_classes)
    batch_size = 1
    nb_epoch = 1
    model = resnet152_model(resized_height, resized_width, number_of_channels, number_of_classes)
    model.fit(resized_image_matrix, dummy_label, batch_size=batch_size, nb_epoch=nb_epoch, verbose=1)# Start Fine-tuning
    predictions_valid = model.predict(resized_image_matrix, batch_size=batch_size, verbose=1)# Make predictions


def turn_art_image_folder_into_encoded_block_files_func(folder_path_of_art_folders_to_encode, block_storage_folder_path, desired_block_size_in_bytes, block_redundancy_factor):
    global nsfw_score_threshold
    #First, scan the input folder and add each valid image file to a single zip file:\
    overall_start_time = time.time()
    list_of_art_sub_folder_paths = glob.glob(folder_path_of_art_folders_to_encode + '*\\')
    if not os.path.exists(block_storage_folder_path):
        try:
            os.makedirs(block_storage_folder_path)
        except Exception as e:
                print('Error: '+ str(e)) 
    ramdisk_object = MemoryFS()
    print('\nNow removing any existing remaining zip files...')
    zip_file_paths = glob.glob(folder_path_of_art_folders_to_encode+'\\**\\*.zip')
    for current_zip_file_path in zip_file_paths:
        try:
            os.remove(current_zip_file_path)
        except Exception as e:
            print('Error: '+ str(e))
    print('Done removing old zip files!\n')
    for current_art_folder_path in list_of_art_sub_folder_paths:
        start_time = time.time()
        file_base_name = current_art_folder_path.split('\\')[-2].replace(' ','_').lower()
        zip_file_path = os.path.join(current_art_folder_path,file_base_name+'.zip')
        art_input_file_paths =  glob.glob(current_art_folder_path+'*.jpg') + glob.glob(current_art_folder_path+'*.png') + glob.glob(current_art_folder_path+'*.bmp') + glob.glob(current_art_folder_path+'*.gif')
        
        list_of_art_file_hashes, list_of_nsfw_scores = check_art_folder_for_nsfw_content(path_to_art_folder)
        art_file_hash_to_nsfw_dict = dict(zip(list_of_art_file_hashes,list_of_nsfw_scores))
        list_of_accepted_art_file_hashes = []
        with ZipFile(zip_file_path,'w') as myzip:
            for current_file_path in art_input_file_paths:
                if (imghdr.what(current_file_path) == 'gif') or (imghdr.what(current_file_path) == 'jpeg') or (imghdr.what(current_file_path) == 'png') or (imghdr.what(current_file_path) == 'bmp'):
                   with open(current_file_path,'rb') as f:
                       current_art_file = f.read()
                   sha256_hash_of_current_art_file = hashlib.sha256(current_art_file).hexdigest()                    
                   current_image_nsfw_score = art_file_hash_to_nsfw_dict[sha256_hash_of_current_art_file]
                   if current_image_nsfw_score <= nsfw_score_threshold:
                       myzip.write(current_file_path,arcname=current_file_path.split('\\')[-1])
                       list_of_accepted_art_file_hashes.append(sha256_hash_of_current_art_file)
        metadata_sqlitedb_file_path =  glob.glob(current_art_folder_path+'*.db')[0]
        with ZipFile(zip_file_path,'w') as myzip:
            myzip.write(metadata_sqlitedb_file_path, arcname=metadata_sqlitedb_file_path.split('\\')[-1])
        with open(zip_file_path,'rb') as f:
            final_art_file = f.read()
            art_zipfile_hash = hashlib.sha256(final_art_file).hexdigest()
        new_zip_file_path = os.path.join(current_art_folder_path,art_zipfile_hash + '.zip')
        copyfile(zip_file_path,new_zip_file_path)
        zip_file_path = new_zip_file_path
        final_art_file__original_size_in_bytes = os.path.getsize(zip_file_path)
        output_blocks_list = []
        DEFAULT_C = 0.1
        DEFAULT_DELTA = 0.5
        seed = randint(0, 1 << 31 - 1)
        #Process ZIP file into a stream of encoded blocks, and save those blocks as separate files in the output folder:
        print('Now encoding file ' + zip_file_path + ', (' + str(round(final_art_file__original_size_in_bytes/1000000)) + 'mb);\nFile Hash is: '+ art_zipfile_hash+'\n\n')
        total_number_of_blocks_required = ceil((1.00*block_redundancy_factor*final_art_file__original_size_in_bytes) / desired_block_size_in_bytes)
        pbar = tqdm(total=total_number_of_blocks_required)
        with open(zip_file_path,'rb') as f:
            f_bytes = f.read()
        filesize = len(f_bytes)
        #Convert file byte contents into blocksize chunks, padding last one if necessary
        blocks = [int.from_bytes(f_bytes[ii:ii+desired_block_size_in_bytes].ljust(desired_block_size_in_bytes, b'0'), sys.byteorder) for ii in range(0, len(f_bytes), desired_block_size_in_bytes)]
        K = len(blocks) # init stream vars
        prng = PRNG(params=(K, DEFAULT_DELTA, DEFAULT_C))
        prng.set_seed(seed)
        number_of_blocks_generated = 0# block generation loop
        while number_of_blocks_generated <= total_number_of_blocks_required:
            update_skip = 1
            if (number_of_blocks_generated % update_skip) == 0:
                pbar.update(update_skip)
            blockseed, d, ix_samples = prng.get_src_blocks()
            block_data = 0
            for ix in ix_samples:
                block_data ^= blocks[ix]
            block = (filesize, desired_block_size_in_bytes, blockseed, int.to_bytes(block_data, desired_block_size_in_bytes, sys.byteorder)) # Generate blocks of XORed data in network byte order
            number_of_blocks_generated = number_of_blocks_generated + 1
            packed_block_data = pack('!III%ss'%desired_block_size_in_bytes, *block)
            output_blocks_list.append(packed_block_data)
            hash_of_block = hashlib.sha256(packed_block_data).hexdigest()
            output_block_file_path = 'FileHash__'+art_zipfile_hash + '__Block__' + '{0:09}'.format(number_of_blocks_generated) + '__BlockHash_' + hash_of_block +'.block'
            try:
                with ramdisk_object.open(output_block_file_path,'wb') as f:
                    f.write(packed_block_data)
            except Exception as e:
                print('Error: '+ str(e))
        duration_in_seconds = round(time.time() - start_time, 1)
        print('\n\nFinished processing in '+str(duration_in_seconds) + ' seconds! \nOriginal zip file was encoded into ' + str(number_of_blocks_generated) + ' blocks of ' + str(ceil(desired_block_size_in_bytes/1000)) + ' kilobytes each. Total size of all blocks is ~' + str(ceil((number_of_blocks_generated*desired_block_size_in_bytes)/1000000)) + ' megabytes\n')
    print('Now copying encoded files from ram disk to local storage...')
    if not os.path.exists(block_storage_folder_path):
        try:
            os.makedirs(block_storage_folder_path)
        except Exception as e:
            print('Error: '+ str(e))
    filesystem_object = OSFS(block_storage_folder_path)     
    copy_fs(ramdisk_object,filesystem_object)
    total_duration_in_seconds = round(time.time() - overall_start_time, 1)
    print('Now removing zip files...')
    zip_file_paths = glob.glob(folder_path_of_art_folders_to_encode+'\\**\\*.zip')
    for current_zip_file_path in zip_file_paths:
        try:
            os.remove(current_zip_file_path)
        except Exception as e:
            print('Error: '+ str(e))
    print('Completed deleting zip files!')
    print('All Done! The entire process took '+ str(round(total_duration_in_seconds/60,2))+' minutes!\n')
    ramdisk_object.close()
    return total_duration_in_seconds

def decode_folder_of_block_files_into_original_zip_file_func(sha256_hash_of_desired_file, block_storage_folder_path, decoded_file_destination_folder_path):
    #First, scan the blocks folder:
    start_time = time.time()
    list_of_block_file_paths = glob.glob(os.path.join(block_storage_folder_path,'*'+sha256_hash_of_desired_file+'*.block'))
    reported_file_sha256_hash = list_of_block_file_paths[0].split('\\')[-1].split('__')[1]
    print('\nFound '+str(len(list_of_block_file_paths))+' block files in folder! The SHA256 hash of the original zip file is reported to be: '+reported_file_sha256_hash+'\n')
    decoded_file_destination_file_path = decoded_file_destination_folder_path + 'Reconstructed_File_with_SHA256_Hash_of__' + reported_file_sha256_hash + '.zip'
    c = 0.1
    delta = 0.5
    block_graph = BlockGraph(len(list_of_block_file_paths))
    for block_count, current_block_file_path in enumerate(list_of_block_file_paths):
        with open(current_block_file_path,'rb') as f:
            packed_block_data = f.read()
        hash_of_block = hashlib.sha256(packed_block_data).hexdigest()
        reported_hash_of_block = current_block_file_path.split('__')[-1].replace('.block','').replace('BlockHash_','')
        if hash_of_block == reported_hash_of_block:
            pass#print('Block hash matches reported hash, so block is not corrupted!')
        else:
            print('\nError, the block hash does NOT match the reported hash, so this block is corrupted! Skipping to next block...\n')
            continue
        input_stream = io.BufferedReader(io.BytesIO(packed_block_data),buffer_size=1000000)
        header = unpack('!III', input_stream.read(12))
        filesize = header[0]
        blocksize = header[1]
        blockseed = header[2]
        block = int.from_bytes(input_stream.read(blocksize), 'big')
        number_of_blocks_required = ceil(filesize/blocksize)
        if (block_count % 1) == 0:
            name_parts_list = current_block_file_path.split('\\')[-1].split('_')
            parsed_block_hash = name_parts_list[-1].replace('.block','')
            parsed_block_number = name_parts_list[6].replace('.block','')
            parsed_file_hash = name_parts_list[3].replace('.block','')
            print('\nNow decoding:\nBlock Number: ' + parsed_block_number + '\nFile Hash: ' + parsed_file_hash + '\nBlock Hash: '+ parsed_block_hash)
        prng = PRNG(params=(number_of_blocks_required, delta, c))
        _, _, src_blocks = prng.get_src_blocks(seed = blockseed)
        file_reconstruction_complete = block_graph.add_block(src_blocks, block)
        if file_reconstruction_complete:
            print('\nDone building file! Processed a total of '+str(block_count)+' blocks\n')
            break
    if os.path.isfile(decoded_file_destination_file_path):
        print('\nA file with that name exists already; deleting this first before attempting to write new file!\n')
        try:
            os.remove(decoded_file_destination_file_path)
        except:
            print('Error removing file!')
    else:
        with open(decoded_file_destination_file_path,'wb') as f: 
            for ix, block_bytes in enumerate(map(lambda p: int.to_bytes(p[1], blocksize, 'big'), sorted(block_graph.eliminated.items(), key = lambda p:p[0]))):
                if ix < number_of_blocks_required - 1 or filesize % blocksize == 0:
                    f.write(block_bytes)
                else:
                    f.write(block_bytes[:filesize%blocksize])
    try:
        with open(decoded_file_destination_file_path,'rb') as f:
            reconstructed_file = f.read()
            reconstructed_file_hash = hashlib.sha256(reconstructed_file).hexdigest()
            if reported_file_sha256_hash == reconstructed_file_hash:
                completed_successfully = 1
                print('\nThe SHA256 hash of the reconstructed file matches the reported file hash-- file is valid!\n')
            else:
                completed_successfully = 0
                print('\nProblem! The SHA256 hash of the reconstructed file does NOT match the expected hash! File is not valid.\n')
    except Exception as e:
        print('Error: '+ str(e))
    duration_in_seconds = round(time.time() - start_time, 1)
    print('\n\nFinished processing in '+str(duration_in_seconds) + ' seconds!')
    return completed_successfully

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
    #print('Done writing file hash data to SQLite file!\n')  
    return potential_local_block_hashes_list, potential_local_file_hashes_list

########################################################################################################################################
#Input Parameters:
use_demo_mode = 1
use_stress_test = 1
use_reconstruct_files = 1
use_reset_system_for_demo = 0 
block_redundancy_factor = 10 #How many times more blocks should we store than are required to regenerate the file?
desired_block_size_in_bytes = 1024*1000*2
percentage_of_block_files_to_randomly_delete = 0.75
use_random_corruption = 0
use_generate_new_sqlite_chunk_database = 0
use_digital_signature_sanity_check = 0
use_demonstrate_signing_art_folder_with_artists_private_key = 1
use_demonstrate_meta_data_construction = 0
use_generate_new_key_pair = 0
percentage_of_block_files_to_randomly_corrupt = 0.05
percentage_of_each_selected_file_to_be_randomly_corrupted = 0.01
nsfw_score_threshold = 0.95 #Most actual porn will come up over 99% confident.
folder_path_of_art_folders_to_encode = 'C:\\animecoin\\art_folders_to_encode\\' #Each subfolder contains the various art files pertaining to a given art asset.
block_storage_folder_path = 'C:\\animecoin\\art_block_storage\\'
chunk_db_file_path = 'C:\\animecoin\\anime_chunkdb.sqlite'
decoded_file_destination_folder_path = 'C:\\animecoin\\reconstructed_files\\'
path_to_art_folder = 'C:\\animecoin\\art_folders_to_encode\\Arturo_Lopez__Number_02\\'
path_to_art_image_file = os.path.join(path_to_art_folder,'Arturo_Lopez__Number_02.png')
path_to_all_registered_works_for_dupe_detection = 'C:\\Users\\jeffr\Cointel Dropbox\\Jeffrey Emanuel\\Animecoin_All_Finished_Works\\'
path_to_artwork_metadata_file = os.path.join(path_to_art_folder,'artwork_metadata_file.db')
current_platform = platform.platform()

if use_generate_new_sqlite_chunk_database:
    regenerate_sqlite_chunk_database_func()
    
if use_demo_mode:
    print('\nWelcome! This is a demo of the file storage system that is being used for the Animecoin project to store art files in a decentralized, robust way.')    
    print('\n\nFirst, we begin by taking a bunch of folders; each one contains one or images representing a single digital artwork to be registered.')
    print('\nWe will now iterate through these files, adding each to a zip file, so that every artwork has a corresponding zip file. Then we will encode each of these zip files into a collection of chunks as follows:\n\n')
    sys.exit(0)

list_of_block_file_paths = glob.glob(block_storage_folder_path+'*.block')
run_time = turn_art_image_folder_into_encoded_block_files_func(folder_path_of_art_folders_to_encode, block_storage_folder_path, desired_block_size_in_bytes, block_redundancy_factor)
print('Total Run Time: '+str(run_time)+' seconds')

potential_local_block_hashes_list, potential_local_file_hashes_list = refresh_block_storage_folder_and_check_block_integrity_func()

if use_stress_test: #Check how robust system is to lost/corrupted blocks:
    if use_demo_mode:
        print('\n\nGreat, we just finished turning the file into a bunch of "fungible" blocks! If you check the output folder now, you will see a collection of the resulting files.')
        print('Now we can see the purpose of all this. Suppose something really bad happens, and that most of the master nodes hosting these files disappear.')
        print('On top of that, suppose that many of the remaining nodes also lose some of the file chunks to corruption or disk failure. Still, we will be able to reconstruct the file.')
        #sys.exit(0)
    number_of_deleted_blocks = randomly_delete_percentage_of_local_block_files_func(percentage_of_block_files_to_randomly_delete)
    if use_demo_mode:
        print('\n\nJust deleted '+str(number_of_deleted_blocks) +' of the generated blocks, or '+ str(round(100*number_of_deleted_blocks/len(list_of_block_file_paths),2))+'% of the blocks')
    if use_random_corruption:
       number_of_corrupted_blocks = randomly_corrupt_a_percentage_of_bytes_in_a_random_sample_of_block_files_func(percentage_of_block_files_to_randomly_corrupt,percentage_of_each_selected_file_to_be_randomly_corrupted)
       print('\n\nJust Corrupted '+str(number_of_corrupted_blocks) +' of the generated blocks, or '+ str(round(100*number_of_corrupted_blocks/len(list_of_block_file_paths),2))+'% of the blocks')

if use_reset_system_for_demo:
    delete_all_blocks_and_zip_files_to_reset_system_func()

if use_reconstruct_files:
    print('\n\nNow let\'s try to reconstruct the original file despite this *random* loss of most of the block files...')
    list_of_block_file_paths = glob.glob(block_storage_folder_path+'*.block')
    available_art_file_hashes = [p.split('\\')[-1].split('__')[1] for p in list_of_block_file_paths]
    made_decoded_folder_successfully = make_decoded_file_destination_directory_func()
    for current_file_path in list_of_block_file_paths:
        reported_file_sha256_hash = current_file_path.split('\\')[-1].split('__')[1]
        available_art_file_hashes.append(reported_file_sha256_hash)
    available_art_file_hashes = list(set(available_art_file_hashes))
    failed_file_hash_list = []
    for current_file_hash in available_art_file_hashes:
        print('Now reconstructing file with SHA256 Hash of: ' + current_file_hash)
        completed_successfully = decode_folder_of_block_files_into_original_zip_file_func(current_file_hash,block_storage_folder_path,decoded_file_destination_folder_path)
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

if use_demonstrate_signing_art_folder_with_artists_private_key:
    if use_generate_new_key_pair:
        artist_public_key, artist_private_key = generate_artist_public_and_private_keys_func()
        pickle.dump(artist_public_key, open(os.path.join(path_to_art_folder,'artist_public_key.p'),'wb'))
        pickle.dump(artist_private_key, open(os.path.join(path_to_art_folder,'artist_private_key.p'),'wb'))
        artist_public_key_base64encoded = rsa.PublicKey.save_pkcs1(artist_public_key,format='PEM').decode('utf-8')
        artist_private_key_base64encoded = rsa.PrivateKey.save_pkcs1(artist_private_key,format='PEM').decode('utf-8')
    else:
        artist_public_key = pickle.load(open(os.path.join(path_to_art_folder,'artist_public_key.p'),'rb'))
        artist_private_key = pickle.load(open(os.path.join(path_to_art_folder,'artist_private_key.p'),'rb'))
    #sys.exit(0)#Sign all of the files in the folder, saving data in an SQlite database:
    list_of_artist_signatures_for_each_art_file_hash, list_of_art_file_hashes, list_of_art_file_paths  = sign_all_art_files_in_folder_with_artists_digital_signature_func(path_to_art_folder, artist_public_key, artist_private_key)
    #Connect to the SQlite database and verify that all of the art file hashes are correctly signed using the artist's private key:
    all_verified = verify_artist_signatures_in_art_folder_func(path_to_art_folder,artist_public_key)

if use_digital_signature_sanity_check: #Verify we can verify a specific file hash:
    example_art_file_sha256_hash = 'f01ba6f4d5630f5da3003f541c765da74cf43134b2e514f36434d6eb209'
    artist_signature_for_art_file = sign_art_file_with_artist_signature_func(example_art_file_sha256_hash, artist_private_key)
    rsa.verify(example_art_file_sha256_hash.encode('utf-8'), artist_signature_for_art_file, artist_public_key)
    verified = verify_artist_signature_on_art_file_func(example_art_file_sha256_hash, artist_public_key, artist_signature_for_art_file)
    
if use_demonstrate_meta_data_construction:
    #Example metadata:
    artist_name = 'Arturo Lopez'
    artwork_title = 'Girl with a Bitcoin Earring'
    artwork_max_quantity = 100
    artwork_series_name = 'Animecoin Initial Release Crew: The Great Works Collection'
    artist_website='http://www.anime-coin.com'
    artwork_artist_statement = 'This work is a reference to astronomers in ancient times, using tools like astrolabes.'
    success = create_metadata_file_for_given_art_folder_func( path_to_art_folder,
                                                              artist_name,
                                                              artwork_title,
                                                              artwork_max_quantity,
                                                              artwork_series_name,
                                                              artist_website,
                                                              artwork_artist_statement)
    if success: #Now we show that we can retrieve the metadata fields from the sqlite file:
        artist_concatenated_hash_signature_base64_encoded, hash_of_the_concatenated_file_hashes, datetime_art_was_signed, _, _, _, _, _, _= read_artwork_metadata_from_art_folder_func(path_to_art_folder)

