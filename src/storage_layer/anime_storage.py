import sys, os.path, io, glob, hashlib, platform, imghdr, random, os, sqlite3, warnings, base64, json, pickle, shutil, math
warnings.simplefilter(action='ignore', category=FutureWarning)
warnings.filterwarnings('ignore',category=DeprecationWarning)
import rsa
import tensorflow as tf
import numpy as np
import pandas as pd
import cv2
import keras
import matplotlib.pyplot
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
from time import time
from keras.models import Sequential
from keras.models import Model
from keras.optimizers import SGD
from keras.layers import merge, Input, Dense, Convolution2D, Conv2D, MaxPooling2D, AveragePooling2D, ZeroPadding2D, Dropout, Flatten, merge, Reshape, Activation
from keras.layers.normalization import BatchNormalization
from keras.layers.core import Layer
from keras.engine import InputSpec
from keras import initializers
from keras import layers
from keras import applications
from keras.applications.resnet50 import preprocess_input
from matplotlib.pyplot import imshow
from keras.preprocessing import image
from keras.applications.imagenet_utils import decode_predictions, preprocess_input
from sklearn import decomposition, manifold, pipeline
from scipy.spatial import distance
os.environ['TF_CPP_MIN_LOG_LEVEL'] = '3'
sys.setrecursionlimit(3000)
#Requirements: pip install tqdm, fs, rsa, numpy, tensorflow, keras, h5py==2.8.0rc1
        
###############################################################################################################
# Parameters:
###############################################################################################################
use_demo_mode = 0
use_reset_system_for_demo = 0 
use_stress_test = 0
use_random_corruption = 0
use_reconstruct_files = 1
use_generate_new_sqlite_chunk_database = 1
use_generate_new_demonstration_artist_key_pair = 0
use_demonstrate_duplicate_detection = 0 
block_redundancy_factor = 10 #How many times more blocks should we store than are required to regenerate the file?
desired_block_size_in_bytes = 1024*1000*10
percentage_of_block_files_to_randomly_delete = 0.55
percentage_of_block_files_to_randomly_corrupt = 0.05
percentage_of_each_selected_file_to_be_randomly_corrupted = 0.01
nsfw_score_threshold = 0.95 #Most actual porn will come up over 99% confident.
duplicate_image_threshold = 0.08 #Any image which has another image in our image fingerprint database with a "distance" of less than this threshold will be considered a duplicate. 
root_animecoin_folder_path = 'C:\\animecoin\\'
block_storage_folder_path = os.path.join(root_animecoin_folder_path,'art_block_storage' + os.sep)
folder_path_of_remote_node_sqlite_files = os.path.join(root_animecoin_folder_path,'remote_node_sqlite_files' + os.sep)
reconstructed_files_destination_folder_path = os.path.join(root_animecoin_folder_path,'reconstructed_files' + os.sep)
artist_final_signature_files_folder_path = os.path.join(root_animecoin_folder_path,'art_signature_files' + os.sep)
misc_masternode_files_folder_path = os.path.join(root_animecoin_folder_path,'misc_masternode_files' + os.sep) #Where we store some of the SQlite databases
prepared_final_art_zipfiles_folder_path = os.path.join(root_animecoin_folder_path,'prepared_final_art_zipfiles' + os.sep) 
folder_path_of_art_folders_to_encode = os.path.join(root_animecoin_folder_path,'art_folders_to_encode' + os.sep) #Each subfolder contains the various art files pertaining to a given art asset.
chunk_db_file_path = os.path.join(misc_masternode_files_folder_path,'anime_chunkdb.sqlite')
masternode_keypair_db_file_path = os.path.join(misc_masternode_files_folder_path,'masternode_keypair_db.sqlite')
dupe_detection_image_fingerprint_database_file_path = os.path.join(misc_masternode_files_folder_path,'dupe_detection_image_fingerprint_database.sqlite')

#For testing purposes:
path_to_art_folder = os.path.join(folder_path_of_art_folders_to_encode,'Arturo_Lopez__Number_02' + os.sep)
path_to_art_image_file = os.path.join(path_to_art_folder,'Arturo_Lopez__Number_02.png')
path_to_another_similar_art_image_file = os.path.join(path_to_art_folder,'Arturo_Lopez__Number_02__With_Background.png')
path_to_another_different_art_image_file = 'C:\\animecoin\\art_folders_to_encode\\Arturo_Lopez__Number_04\\Arturo_Lopez__Number_04.png'
path_to_artwork_metadata_file = os.path.join(path_to_art_folder,'artwork_metadata_file.db')
path_to_all_registered_works_for_dupe_detection = 'C:\\Users\\jeffr\Cointel Dropbox\\Jeffrey Emanuel\\Animecoin_All_Finished_Works\\'
current_platform = platform.platform()

###############################################################################################################
# Functions:
###############################################################################################################

#LT Coding Helper fucntions:
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
    """Block file byte contents into blocksize chunks, padding last one if necessary"""
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
        if len(nodes) == 1:# We can eliminate this source node
            to_eliminate = list(self.eliminate(next(iter(nodes)), data))
            while len(to_eliminate):# Recursively eliminate all nodes that can now be resolved
                other, check = to_eliminate.pop()
                to_eliminate.extend(self.eliminate(other, check))
        else:
            for node in list(nodes): # Pass messages from already-resolved source nodes
                if node in self.eliminated:
                    nodes.remove(node)
                    data ^= self.eliminated[node]
            if len(nodes) == 1: # Resolve if we are left with a single non-resolved source node
                return self.add_block(nodes, data)
            else: # Add edges for all remaining nodes to this check
                check = CheckNode(nodes, data)
                for node in nodes:
                    self.checks[node].append(check)
        return len(self.eliminated) >= self.num_blocks # Are we done yet?

    def eliminate(self, node, data):
        """Resolves a source node, passing the message to all associated checks """
        self.eliminated[node] = data # Cache resolved value
        others = self.checks[node]
        del self.checks[node]
        for check in others: # Pass messages to all associated checks
            check.check ^= data
            check.src_nodes.remove(node)
            if len(check.src_nodes) == 1: # Yield all nodes that can now be resolved
                yield (next(iter(check.src_nodes)), check.check)

#Dupe detection helper functions:
def get_image_filename_from_image_hash_func(sha256_hash_of_art_image_file,path_to_images_folder=path_to_all_registered_works_for_dupe_detection):
    global image_sha256_hash_to_image_file_path_dict
    if 'image_sha256_hash_to_image_file_path_dict' not in globals():
        image_sha256_hash_to_image_file_path_dict = {}
    else:
        try:
            image_file_path = image_sha256_hash_to_image_file_path_dict[sha256_hash_of_art_image_file]
            return image_file_path
        except:
            pass
    valid_image_file_paths = get_all_valid_image_file_paths_in_folder_func(path_to_images_folder)
    for current_image_file_path in valid_image_file_paths:
        with open(current_image_file_path,'rb') as f:
            image_file_binary_data = f.read()
        sha256_hash_of_current_art_file = hashlib.sha256(image_file_binary_data).hexdigest()                    
        image_sha256_hash_to_image_file_path_dict[sha256_hash_of_current_art_file] = current_image_file_path
    return image_sha256_hash_to_image_file_path_dict[sha256_hash_of_art_image_file]

def get_image_hash_from_image_file_path_func(path_to_art_image_file):
    try:
        with open(path_to_art_image_file,'rb') as f:
            art_image_file_binary_data = f.read()
        sha256_hash_of_art_image_file = hashlib.sha256(art_image_file_binary_data).hexdigest()        
        return sha256_hash_of_art_image_file
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

def regenerate_dupe_detection_image_fingerprint_database_func():
    global dupe_detection_image_fingerprint_database_file_path
    try:
        conn = sqlite3.connect(dupe_detection_image_fingerprint_database_file_path, detect_types=sqlite3.PARSE_DECLTYPES)
        c = conn.cursor()
        dupe_detection_image_fingerprint_database_creation_string= """CREATE TABLE image_hash_to_image_fingerprint_table (sha256_hash_of_art_image_file text, model_1_image_fingerprint_vector array, model_2_image_fingerprint_vector array, model_3_image_fingerprint_vector array, datetime_fingerprint_added_to_database TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, PRIMARY KEY (sha256_hash_of_art_image_file));"""
        c.execute(dupe_detection_image_fingerprint_database_creation_string)
        model_1_tsne_table_creation_string= """CREATE TABLE tsne_coordinates_table_model_1 (sha256_hash_of_art_image_file text, tsne_x_coordinate real, tsne_y_coordinate real, datetime_fingerprint_added_to_database TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, PRIMARY KEY (sha256_hash_of_art_image_file, tsne_x_coordinate, tsne_y_coordinate));"""
        c.execute(model_1_tsne_table_creation_string)
        model_2_tsne_table_creation_string= """CREATE TABLE tsne_coordinates_table_model_2 (sha256_hash_of_art_image_file text, tsne_x_coordinate real, tsne_y_coordinate real, datetime_fingerprint_added_to_database TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, PRIMARY KEY (sha256_hash_of_art_image_file, tsne_x_coordinate, tsne_y_coordinate));"""
        c.execute(model_2_tsne_table_creation_string)
        model_3_tsne_table_creation_string= """CREATE TABLE tsne_coordinates_table_model_3 (sha256_hash_of_art_image_file text, tsne_x_coordinate real, tsne_y_coordinate real, datetime_fingerprint_added_to_database TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, PRIMARY KEY (sha256_hash_of_art_image_file, tsne_x_coordinate, tsne_y_coordinate));"""
        c.execute(model_3_tsne_table_creation_string)
        conn.commit()
        conn.close()
    except Exception as e:
        print('Error: '+ str(e))
            
def get_image_deep_learning_features_func(path_to_art_image_file):
    global dupe_detection_model_1
    global dupe_detection_model_2
    global dupe_detection_model_3
    try:
        if os.path.isfile(path_to_art_image_file):
            with open(path_to_art_image_file,'rb') as f:
                image_file_binary_data = f.read()
                sha256_hash_of_art_image_file = hashlib.sha256(image_file_binary_data).hexdigest()
            img = image.load_img(path_to_art_image_file, target_size=(224, 224)) # load image setting the image size to 224 x 224
            x = image.img_to_array(img) # convert image to numpy array
            x = np.expand_dims(x, axis=0) # the image is now in an array of shape (3, 224, 224) but we need to expand it to (1, 2, 224, 224) as Keras is expecting a list of images
            x = preprocess_input(x)
            dupe_detection_model_1_loaded_already = 'dupe_detection_model_1' in globals()
            if not dupe_detection_model_1_loaded_already:
                print('Loading deep learning model 1 (VGG19)...')
                dupe_detection_model_1 = get_named_model_func('VGG19')
            dupe_detection_model_2_loaded_already = 'dupe_detection_model_2' in globals()
            if not dupe_detection_model_2_loaded_already:
                print('Loading deep learning model 2 (Xception)...')
                dupe_detection_model_2 = get_named_model_func('Xception')
            dupe_detection_model_3_loaded_already = 'dupe_detection_model_3' in globals()
            if not dupe_detection_model_3_loaded_already:
                print('Loading deep learning model 3 (Resnet50)...')
                dupe_detection_model_3 = get_named_model_func('ResNet50')
            model_1_features = dupe_detection_model_1.predict(x)[0] # extract the features
            model_2_features = dupe_detection_model_2.predict(x)[0] 
            model_3_features = dupe_detection_model_3.predict(x)[0] 
            model_1_image_fingerprint_vector = prepare_image_fingerprint_data_for_export_func(model_1_features)
            model_2_image_fingerprint_vector = prepare_image_fingerprint_data_for_export_func(model_2_features)
            model_3_image_fingerprint_vector = prepare_image_fingerprint_data_for_export_func(model_3_features)
            return model_1_image_fingerprint_vector,model_2_image_fingerprint_vector,model_3_image_fingerprint_vector, sha256_hash_of_art_image_file
    except Exception as e:
        print('Error: '+ str(e))

def add_image_fingerprints_to_dupe_detection_database_func(path_to_art_image_file):
    global dupe_detection_image_fingerprint_database_file_path
    model_1_image_fingerprint_vector,model_2_image_fingerprint_vector,model_3_image_fingerprint_vector, sha256_hash_of_art_image_file = get_image_deep_learning_features_func(path_to_art_image_file)
    try:
        conn = sqlite3.connect(dupe_detection_image_fingerprint_database_file_path)
        c = conn.cursor()
        data_insertion_query_string = """INSERT OR REPLACE INTO image_hash_to_image_fingerprint_table (sha256_hash_of_art_image_file, model_1_image_fingerprint_vector, model_2_image_fingerprint_vector, model_3_image_fingerprint_vector) VALUES (?,?,?,?);"""
        try:
            c.execute(data_insertion_query_string,[sha256_hash_of_art_image_file, model_1_image_fingerprint_vector, model_2_image_fingerprint_vector, model_3_image_fingerprint_vector])
        except:
            regenerate_dupe_detection_image_fingerprint_database_func()
            c.execute(data_insertion_query_string,[sha256_hash_of_art_image_file, model_1_image_fingerprint_vector, model_2_image_fingerprint_vector, model_3_image_fingerprint_vector])
        conn.commit()
        conn.close()
    except Exception as e:
        print('Error: '+ str(e))
    return  model_1_image_fingerprint_vector, model_2_image_fingerprint_vector, model_3_image_fingerprint_vector

def add_all_images_in_folder_to_image_fingerprint_database_func(path_to_art_folder):
    valid_image_file_paths = get_all_valid_image_file_paths_in_folder_func(path_to_art_folder)
    for current_image_file_path in valid_image_file_paths:
        print('\nNow adding image file '+ current_image_file_path+' to image fingerprint database.')
        add_image_fingerprints_to_dupe_detection_database_func(current_image_file_path)
     
def get_image_fingerprints_from_dupe_detection_database_func(sha256_hash_of_art_image_file):
    global dupe_detection_image_fingerprint_database_file_path
    try:
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
    except Exception as e:
        print('Error: '+ str(e))

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
    try:
        print('Now applying tSNE to image fingerprint database...')
        conn = sqlite3.connect(dupe_detection_image_fingerprint_database_file_path,detect_types=sqlite3.PARSE_DECLTYPES)
        c = conn.cursor()
        dupe_detection_fingerprint_query_results = c.execute("""SELECT sha256_hash_of_art_image_file, model_1_image_fingerprint_vector, model_2_image_fingerprint_vector, model_3_image_fingerprint_vector FROM image_hash_to_image_fingerprint_table ORDER BY datetime_fingerprint_added_to_database DESC""").fetchall()
        conn.close()
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
        for file_cnt,current_image_hash in enumerate(list_of_image_sha256_hashes):
            current_model_1_tsne_x_coordinate = float(model_1_tsne_x_coordinates[file_cnt])
            current_model_1_tsne_y_coordinate = float(model_1_tsne_y_coordinates[file_cnt])
            current_model_2_tsne_x_coordinate = float(model_2_tsne_x_coordinates[file_cnt])
            current_model_2_tsne_y_coordinate = float(model_2_tsne_y_coordinates[file_cnt])
            current_model_3_tsne_x_coordinate = float(model_3_tsne_x_coordinates[file_cnt])
            current_model_3_tsne_y_coordinate = float(model_3_tsne_y_coordinates[file_cnt])            
            try:
                conn = sqlite3.connect(dupe_detection_image_fingerprint_database_file_path)
                c = conn.cursor()
                model_1_data_insertion_query_string = """INSERT OR REPLACE INTO tsne_coordinates_table_model_1 (sha256_hash_of_art_image_file, tsne_x_coordinate, tsne_y_coordinate) VALUES (?,?,?);"""
                c.execute(model_1_data_insertion_query_string,[current_image_hash, current_model_1_tsne_x_coordinate, current_model_1_tsne_y_coordinate])
                model_2_data_insertion_query_string = """INSERT OR REPLACE INTO tsne_coordinates_table_model_2 (sha256_hash_of_art_image_file, tsne_x_coordinate, tsne_y_coordinate) VALUES (?,?,?);"""
                c.execute(model_2_data_insertion_query_string,[current_image_hash, current_model_2_tsne_x_coordinate, current_model_2_tsne_y_coordinate])
                model_3_data_insertion_query_string = """INSERT OR REPLACE INTO tsne_coordinates_table_model_3 (sha256_hash_of_art_image_file, tsne_x_coordinate, tsne_y_coordinate) VALUES (?,?,?);"""
                c.execute(model_3_data_insertion_query_string,[current_image_hash, current_model_3_tsne_x_coordinate, current_model_3_tsne_y_coordinate])
                conn.commit()
                conn.close()
                pbar.update(1)
            except Exception as e:
                print('Error: '+ str(e))
        print('Done!\n')
        return list_of_image_sha256_hashes, model_1_tsne_x_coordinates, model_1_tsne_y_coordinates, model_2_tsne_x_coordinates, model_2_tsne_y_coordinates, model_3_tsne_x_coordinates, model_3_tsne_y_coordinates
    except Exception as e:
        print('Error: '+ str(e))
        
def get_tsne_coordinates_for_desired_image_file_hash_func(sha256_hash_of_art_image_file):
    global dupe_detection_image_fingerprint_database_file_path
    try:
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
        art_image_file_path = get_image_filename_from_image_hash_func(sha256_hash_of_art_image_file)
        art_image_file_name = os.path.split(art_image_file_path)[-1]
        return model_1_tsne_x_coordinate, model_1_tsne_y_coordinate, model_2_tsne_x_coordinate, model_2_tsne_y_coordinate,model_3_tsne_x_coordinate, model_3_tsne_y_coordinate,art_image_file_name
    except Exception as e:
        print('Error: '+ str(e))

def calculate_image_similarity_between_two_image_hashes_func(image1_sha256_hash,image2_sha256_hash):
    image1_model_1_tsne_x_coordinate, image1_model_1_tsne_y_coordinate, image1_model_2_tsne_x_coordinate, image1_model_2_tsne_y_coordinate, image1_model_3_tsne_x_coordinate, image1_model_3_tsne_y_coordinate, _ = get_tsne_coordinates_for_desired_image_file_hash_func(image1_sha256_hash)
    image2_model_1_tsne_x_coordinate, image2_model_1_tsne_y_coordinate, image2_model_2_tsne_x_coordinate, image2_model_2_tsne_y_coordinate, image2_model_3_tsne_x_coordinate, image2_model_3_tsne_y_coordinate, _ = get_tsne_coordinates_for_desired_image_file_hash_func(image2_sha256_hash)
    model_1_image_similarity_metric = np.linalg.norm(np.array([image1_model_1_tsne_x_coordinate, image1_model_1_tsne_y_coordinate]) - np.array([image2_model_1_tsne_x_coordinate, image2_model_1_tsne_y_coordinate]))
    model_2_image_similarity_metric = np.linalg.norm(np.array([image1_model_2_tsne_x_coordinate, image1_model_2_tsne_y_coordinate]) - np.array([image2_model_2_tsne_x_coordinate, image2_model_2_tsne_y_coordinate]))
    model_3_image_similarity_metric = np.linalg.norm(np.array([image1_model_3_tsne_x_coordinate, image1_model_3_tsne_y_coordinate]) - np.array([image2_model_3_tsne_x_coordinate, image2_model_3_tsne_y_coordinate]))
    return model_1_image_similarity_metric, model_2_image_similarity_metric, model_3_image_similarity_metric

def find_most_similar_images_to_given_image_from_fingerprint_data_func(sha256_hash_of_art_image_file):
    global image_sha256_hash_to_image_file_path_dict
    if 'image_sha256_hash_to_image_file_path_dict' not in globals():
        get_image_filename_from_image_hash_func(sha256_hash_of_art_image_file)
    list_of_image_hashes_from_folder = image_sha256_hash_to_image_file_path_dict.keys()
    total_number_of_images_hashes = len(list_of_image_hashes_from_folder)
    list_of_image_file_names = []
    list_of_model_1_similarity_metrics = []
    list_of_model_2_similarity_metrics = []
    list_of_model_3_similarity_metrics = []
    pbar = tqdm(total=(total_number_of_images_hashes-1))
    print('\nScanning specified image against all image fingerprints in database...')
    for current_image_sha256_hash in list_of_image_hashes_from_folder:
        if (current_image_sha256_hash != sha256_hash_of_art_image_file):
            current_image_file_path = get_image_filename_from_image_hash_func(current_image_sha256_hash)
            list_of_image_file_names.append(os.path.split(current_image_file_path)[-1])
            model_1_image_similarity_metric, model_2_image_similarity_metric, model_3_image_similarity_metric = calculate_image_similarity_between_two_image_hashes_func(sha256_hash_of_art_image_file, current_image_sha256_hash)
            list_of_model_1_similarity_metrics.append(model_1_image_similarity_metric)
            list_of_model_2_similarity_metrics.append(model_2_image_similarity_metric)
            list_of_model_3_similarity_metrics.append(model_3_image_similarity_metric)
            pbar.update(1)
    image_similarity_df = pd.DataFrame([list_of_image_hashes_from_folder,list_of_image_file_names,list_of_model_1_similarity_metrics,list_of_model_2_similarity_metrics,list_of_model_3_similarity_metrics]).T
    image_similarity_df.columns = ['image_sha_256_hash','image_file_name', 'model_1_image_similarity_metric', 'model_2_image_similarity_metric', 'model_3_image_similarity_metric',]
    image_similarity_df_rescaled = image_similarity_df
    image_similarity_df_rescaled['model_1_image_similarity_metric'] = 1 / (image_similarity_df['model_1_image_similarity_metric']/ image_similarity_df['model_1_image_similarity_metric'].max())
    image_similarity_df_rescaled['model_2_image_similarity_metric'] = 1 / (image_similarity_df['model_2_image_similarity_metric']/ image_similarity_df['model_2_image_similarity_metric'].max())
    image_similarity_df_rescaled['model_3_image_similarity_metric'] = 1 / (image_similarity_df['model_3_image_similarity_metric']/ image_similarity_df['model_3_image_similarity_metric'].max())
    image_similarity_df_rescaled['overall_image_similarity_metric'] = (1/3)*(image_similarity_df_rescaled['model_1_image_similarity_metric'] + image_similarity_df_rescaled['model_2_image_similarity_metric'] + image_similarity_df_rescaled['model_3_image_similarity_metric'])
    return image_similarity_df.sort_values('model_1_image_similarity_metric')

def check_if_image_is_likely_dupe(sha256_hash_of_art_image_file):
    global duplicate_image_threshold
    image_similarity_df = find_most_similar_images_to_given_image_from_fingerprint_data_func(sha256_hash_of_art_image_file)
    possible_dupes = image_similarity_df[image_similarity_df['image_similarity_metric'] <= duplicate_image_threshold ]
    if len(possible_dupes) > 0:
        is_dupe = 1
    else:
        is_dupe = 0
    return is_dupe

#Various Utility Functions:
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

def regenerate_sqlite_chunk_database_func():
    global chunk_db_file_path
    try:
        conn = sqlite3.connect(chunk_db_file_path)
        c = conn.cursor()
        local_hash_table_creation_string= """CREATE TABLE potential_local_hashes (block_hash text PRIMARY KEY, file_hash text);"""
        global_hash_table_creation_string= """CREATE TABLE potential_global_hashes (block_hash text, file_hash text, remote_node_ip text, datetime_peer_last_seen TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, PRIMARY KEY (block_hash, remote_node_ip));"""
        c.execute(local_hash_table_creation_string)
        c.execute(global_hash_table_creation_string)
        conn.commit()
        conn.close()
    except Exception as e:
        print('Error: '+ str(e))

def check_if_file_path_is_a_valid_image_func(path_to_file):
    is_image = 0
    try:
        if (imghdr.what(path_to_file) == 'gif') or (imghdr.what(path_to_file) == 'jpeg') or (imghdr.what(path_to_file) == 'png') or (imghdr.what(path_to_file) == 'bmp'):
            is_image = 1
            return is_image
    except Exception as e:
        print('Error: '+ str(e))

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
        
def subdivide_image_into_array_of_equally_sized_image_tiles_func(path_to_art_image_file,number_of_horiz_and_vert_segments):
    im = cv2.imread(path_to_art_image_file)#Note: number_of_horiz_and_vert_segments = 2 will cut the image into quarters;  number_of_horiz_and_vert_segments = 3 will make 9 equal sized tiles. 
    M = im.shape[0]//number_of_horiz_and_vert_segments
    N = im.shape[1]//number_of_horiz_and_vert_segments
    image_tiles = [im[x:x+M,y:y+N] for x in range(0,im.shape[0],M) for y in range(0,im.shape[1],N)]
    return image_tiles

#Stress testing functions:
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

#Digital signature functions:
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
        
def generate_artist_public_and_private_keys_func():
    (artist_public_key, artist_private_key) = rsa.newkeys(512)
    return artist_public_key, artist_private_key

def sign_art_file_with_artist_signature_func(sha256_hash_of_art_file, artist_private_key):
    sha256_hash_of_art_file_utf8_encoded = sha256_hash_of_art_file.encode('utf-8')
    artist_signature_for_art_file = rsa.sign(sha256_hash_of_art_file_utf8_encoded, artist_private_key, 'SHA-256')
    artist_signature_for_art_file_base64_encoded = base64.b64encode(artist_signature_for_art_file).decode('utf-8')
    return artist_signature_for_art_file_base64_encoded

def sign_art_files_in_folder_with_artist_digital_signature_func(path_to_art_folder,artist_public_key,artist_private_key):
    sqlite_file_path = path_to_art_folder+os.sep+'artist_signature_file.sig'
    if os.path.exists(sqlite_file_path):
        try:
            os.remove(sqlite_file_path)
        except:
            print('Could not remove existing signature file! Please remove manually.')
    art_input_file_paths = get_all_valid_image_file_paths_in_folder_func(path_to_art_folder)
    list_of_art_file_hashes = []
    list_of_artist_signatures_for_each_art_file_hash = []
    for current_file_path in art_input_file_paths:
        with open(current_file_path,'rb') as f:
            current_art_file_data = f.read()
        sha256_hash_of_current_art_file = hashlib.sha256(current_art_file_data).hexdigest()                    
        artist_signature_for_art_file = sign_art_file_with_artist_signature_func(sha256_hash_of_current_art_file,artist_private_key)
        list_of_art_file_hashes.append(sha256_hash_of_current_art_file)
        list_of_artist_signatures_for_each_art_file_hash.append(artist_signature_for_art_file)
    if len(list_of_art_file_hashes) > 0:
        try:
            conn = sqlite3.connect(sqlite_file_path)
            c = conn.cursor()
            concatenated_file_hashes = ''.join(list_of_art_file_hashes).encode('utf-8')
            hash_of_the_concatenated_file_hashes = hashlib.sha256(concatenated_file_hashes).hexdigest()
            artist_signature_for_hash_of_the_concatenated_hashes_base64_encoded = sign_art_file_with_artist_signature_func(hash_of_the_concatenated_file_hashes,artist_private_key)
            concatenated_hash_signature_table_creation_string= """CREATE TABLE concatenated_hash_artist_signature_table (artist_signature text, hash_of_the_concatenated_hashes text, datetime_art_was_signed TIMESTAMP DEFAULT CURRENT_TIMESTAMP NOT NULL, PRIMARY KEY (artist_signature, hash_of_the_concatenated_hashes));"""
            c.execute(concatenated_hash_signature_table_creation_string)
            update_concatenated_hash_query_string = """INSERT OR REPLACE INTO concatenated_hash_artist_signature_table (artist_signature, hash_of_the_concatenated_hashes) VALUES (?,?);"""
            c.execute(update_concatenated_hash_query_string,[artist_signature_for_hash_of_the_concatenated_hashes_base64_encoded, hash_of_the_concatenated_file_hashes]) 
            artist_public_key_export_format = rsa.PublicKey.save_pkcs1(artist_public_key,format='PEM')
            artist_public_key_export_format_table_creation_string = """CREATE TABLE artist_public_key_table (artist_public_key text);"""
            c.execute(artist_public_key_export_format_table_creation_string)
            artist_public_key_insert_string = """INSERT OR REPLACE INTO artist_public_key_table (artist_public_key) VALUES (?);"""
            c.execute(artist_public_key_insert_string,[artist_public_key_export_format]) 
            conn.commit()
            conn.close()
            print('Successfully wrote signature file to disk!')
            return artist_signature_for_hash_of_the_concatenated_hashes_base64_encoded, hash_of_the_concatenated_file_hashes 
        except Exception as e:
            print('Error: '+ str(e))
            
def sign_final_artwork_zipfile_including_metadata_with_artist_signature_func(path_to_final_artwork_zipfile_including_metadata, artist_private_key):
    try:
        with open(path_to_final_artwork_zipfile_including_metadata,'rb') as f:
            final_zipfile_binary_data = f.read()
        sha256_hash_of_final_artwork_zipfile_including_metadata = hashlib.sha256(final_zipfile_binary_data).hexdigest()                    
        sha256_hash_of_final_artwork_zipfile_including_metadata_utf8_encoded = sha256_hash_of_final_artwork_zipfile_including_metadata.encode('utf-8')
        artist_signature_for_final_artwork_zipfile_including_metadata = rsa.sign(sha256_hash_of_final_artwork_zipfile_including_metadata_utf8_encoded, artist_private_key, 'SHA-256')
        return artist_signature_for_final_artwork_zipfile_including_metadata
    except Exception as e:
        print('Error: '+ str(e))

def verify_artist_signature_on_art_file_func(sha256_hash_of_art_file, artist_public_key, artist_signature_for_art_file):
    verified = 0
    sha256_hash_of_art_file_utf8_encoded = sha256_hash_of_art_file.encode('utf-8')
    if isinstance(artist_signature_for_art_file,str): #This way we can use one function for base64 encoded signatures and for bytes like objects
        artist_signature_for_art_file = base64.b64decode(artist_signature_for_art_file)
    try:
        rsa.verify(sha256_hash_of_art_file_utf8_encoded, artist_signature_for_art_file, artist_public_key)
        verified = 1
        return verified
    except Exception as e:
        print('Error: '+ str(e))
        return verified
    
def verify_artist_signature_in_art_folder_func(path_to_art_folder,artist_public_key):
    sqlite_file_path = path_to_art_folder+os.sep+'artist_signature_file.sig'
    if not os.path.exists(sqlite_file_path):
        print('Error, could not find signature file!')
        return
    try:
        conn = sqlite3.connect(sqlite_file_path)
        c = conn.cursor()
        artist_signature_query_results = c.execute("""SELECT artist_signature, hash_of_the_concatenated_hashes, datetime_art_was_signed FROM concatenated_hash_artist_signature_table ORDER BY datetime_art_was_signed DESC""").fetchall()
        artist_digital_signature = artist_signature_query_results[0][0]
        hash_of_the_concatenated_hashes = artist_signature_query_results[0][1]
        conn.close()
    except Exception as e:
        print('Error: '+ str(e))
    verified = verify_artist_signature_on_art_file_func(hash_of_the_concatenated_hashes,artist_public_key,artist_digital_signature)
    if verified == 1:
        print('Successfully verified the artist signature for art folder: '+ path_to_art_folder)
    else:
        print('Unable to verify signature!')
    return verified

#Artwork Metadata Functions:
def create_metadata_file_for_given_art_folder_func(path_to_art_folder,
                                                  artist_name,
                                                  artwork_title,
                                                  artwork_max_quantity,
                                                  artwork_series_name='',
                                                  artist_website='',
                                                  artwork_artist_statement=''):
    created_metadata_file_successfully = 0
    artist_signatures_sqlite_file_path = path_to_art_folder+'artist_signature_file.sig'
    try:#First get the digital signature and file hash data:
        conn = sqlite3.connect(artist_signatures_sqlite_file_path)
        c = conn.cursor()
        artist_public_key_query_results = c.execute("""SELECT artist_public_key FROM artist_public_key_table""").fetchall()
        artist_public_key_query_results = artist_public_key_query_results[0][0]
        artist_public_key = artist_public_key_query_results.decode('utf-8')
        artist_concatenated_hash_signature_table_results = c.execute("""SELECT artist_signature, hash_of_the_concatenated_hashes, datetime_art_was_signed FROM concatenated_hash_artist_signature_table ORDER BY datetime_art_was_signed DESC""").fetchall()[0]
        artist_concatenated_hash_signature_base64_encoded = artist_concatenated_hash_signature_table_results[0]
        hash_of_the_concatenated_file_hashes = artist_concatenated_hash_signature_table_results[1]
        datetime_art_was_signed = artist_concatenated_hash_signature_table_results[2]
        conn.close()
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
        conn.close()
        print('Successfully wrote metadata file to disk!')
        created_metadata_file_successfully = 1
        return created_metadata_file_successfully
    except Exception as e:
        print('Error: '+ str(e))
        return created_metadata_file_successfully
    
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
        conn.close()
        return artist_concatenated_hash_signature_base64_encoded, hash_of_the_concatenated_file_hashes, datetime_art_was_signed, artist_name, artwork_title, artwork_max_quantity, artwork_series_name, artist_website, artwork_artist_statement
    except Exception as e:
        print('Error: '+ str(e))

def read_artwork_metadata_from_art_folder_func(path_to_art_folder): #Convenience function
    artwork_metadata_sqlite_file_path = path_to_art_folder+'artwork_metadata_file__hash_*.db'
    if os.path.exists(artwork_metadata_sqlite_file_path):
        artist_concatenated_hash_signature_base64_encoded, hash_of_the_concatenated_file_hashes, datetime_art_was_signed, artist_name, artwork_title, artwork_max_quantity, artwork_series_name, artist_website, artwork_artist_statement = read_artwork_metadata_from_metadata_file(path_to_artwork_metadata_file)
        return artist_concatenated_hash_signature_base64_encoded, hash_of_the_concatenated_file_hashes, datetime_art_was_signed, artist_name, artwork_title, artwork_max_quantity, artwork_series_name, artist_website, artwork_artist_statement

def get_artwork_metadata_from_artist_func(hash_of_the_concatenated_file_hashes):
    #This is a dummy function to just supply some data; in reality this will run on the artists computer and will be supplied along with the art image files to the masternode for registration
    artist_name = 'Arturo Lopez'
    artwork_title = 'Girl with a Bitcoin Earring'
    artwork_max_quantity = 100
    artwork_series_name = 'Animecoin Initial Release Crew: The Great Works Collection'
    artist_website='http://www.anime-coin.com'
    artwork_artist_statement = 'This work is a reference to astronomers in ancient times, using tools like astrolabes.'
    return artist_name, artwork_title, artwork_max_quantity, artwork_series_name, artist_website, artwork_artist_statement

#NSFW Function:
def check_art_folder_for_nsfw_content(path_to_art_folder):
   global nsfw_score_threshold
   start_time = time()
   art_input_file_paths = get_all_valid_image_file_paths_in_folder_func(path_to_art_folder)
   list_of_art_file_hashes = []
   list_of_nsfw_scores = []
   label_text = ['sfw', 'nsfw']
   print('Now loading NSFW detection TensorFlow Library...')
   with tf.gfile.FastGFile('nsfw_trained_model.pb', 'rb') as f:# Unpersists graph from file
       graph_def = tf.GraphDef()
       graph_def.ParseFromString(f.read())
       tf.import_graph_def(graph_def, name='')
       print('Done!')
   for current_file_path in art_input_file_paths:
      file_base_name = current_file_path.split('\\')[-1].replace(' ','_').lower()
      with open(current_file_path,'rb') as f:
          current_art_file = f.read()
      sha256_hash_of_current_art_file = hashlib.sha256(current_art_file).hexdigest()                    
      print('\nCurrent Art Image File Name: '+file_base_name+' \nImage SHA256 Hash: '+sha256_hash_of_current_art_file)            
      list_of_art_file_hashes.append(sha256_hash_of_current_art_file)
      image_data = tf.gfile.FastGFile(current_file_path, 'rb').read()
      with tf.Session() as sess:
          softmax_tensor = sess.graph.get_tensor_by_name('final_result:0')    # Feed the image_data as input to the graph and get first prediction
          predictions = sess.run(softmax_tensor,  {'DecodeJpeg/contents:0': image_data})
          top_k = predictions[0].argsort()[-len(predictions[0]):][::-1]  # Sort to show labels of first prediction in order of confidence
          for graph_node_id in top_k:
              human_string = label_text[graph_node_id]
              nsfw_score = predictions[0][graph_node_id]
              list_of_nsfw_scores.append(nsfw_score)
              print('%s (score = %.5f)' % (human_string, nsfw_score))
              if nsfw_score > nsfw_score_threshold:
                  print('\n\n***Warning, current image is NFSW!***\n\n')
   return list_of_art_file_hashes, list_of_nsfw_scores
   duration_in_seconds = round(time() - start_time, 1)
   print('\nFinished checking for NSFW images in '+str(duration_in_seconds) + ' seconds!')
   
def prepare_artwork_folder_for_registration_func(path_to_art_folder):
    global nsfw_score_threshold
    global artist_public_key # These two variables won't exist in the real version; they will instead be 
    global artist_private_key# supplied by the artist at the time of submission for the art registration process.
    remove_existing_remaining_zip_files_func(path_to_art_folder)
    file_base_name = path_to_art_folder.split(os.sep)[-2].replace(' ','_').lower()
    art_input_file_paths = get_all_valid_image_file_paths_in_folder_func(path_to_art_folder)
    #These next two lines would actually be run on the artists machine and the resulting files would be included in the folder of art files already:
    artist_signature_for_hash_of_the_concatenated_hashes_base64_encoded, hash_of_the_concatenated_file_hashes = sign_art_files_in_folder_with_artist_digital_signature_func(path_to_art_folder,artist_public_key,artist_private_key)
    artist_name, artwork_title, artwork_max_quantity, artwork_series_name, artist_website, artwork_artist_statement = get_artwork_metadata_from_artist_func(hash_of_the_concatenated_file_hashes)
    list_of_art_file_hashes, list_of_nsfw_scores = check_art_folder_for_nsfw_content(path_to_art_folder)
    art_file_hash_to_nsfw_dict = dict(zip(list_of_art_file_hashes,list_of_nsfw_scores))
    list_of_accepted_art_file_hashes = []
    zip_file_path = os.path.join(path_to_art_folder,file_base_name+'.zip')
    with ZipFile(zip_file_path,'w') as myzip:
        for current_file_path in art_input_file_paths:
            with open(current_file_path,'rb') as f:
                current_art_file = f.read()
                sha256_hash_of_current_art_file = hashlib.sha256(current_art_file).hexdigest()                    
                current_image_nsfw_score = art_file_hash_to_nsfw_dict[sha256_hash_of_current_art_file]
                if current_image_nsfw_score <= nsfw_score_threshold:
                    myzip.write(current_file_path,arcname=current_file_path.split(os.sep)[-1])
                    list_of_accepted_art_file_hashes.append(sha256_hash_of_current_art_file)
    if len(list_of_accepted_art_file_hashes) > 0:
        print('\nSuccessfully added '+str(len(list_of_accepted_art_file_hashes))+ ' art image files that passed the NSFW test!\n')
    successfully_created_metadata_file = create_metadata_file_for_given_art_folder_func(path_to_art_folder, artist_name, artwork_title, artwork_max_quantity, artwork_series_name, artist_website,  artwork_artist_statement)#Now we use the artist's digital signature on the hash of the concatenated hashes for all image files included in the art folder, together with the various pieces of metadata, to construct the final metadata file that will be included in the art zip file.
    if successfully_created_metadata_file:
        try:
            print('Now adding metadata file to zip file...')
            metadata_sqlitedb_file_path = glob.glob(path_to_art_folder+'*.db')[0]
            with ZipFile(zip_file_path,'a') as myzip:
                myzip.write(metadata_sqlitedb_file_path, arcname=metadata_sqlitedb_file_path.split(os.sep)[-1])
            print('Done!')
            with open(zip_file_path,'rb') as f:
                final_art_zipfile_binary_data = f.read()
                art_zipfile_hash = hashlib.sha256(final_art_zipfile_binary_data).hexdigest()
            final_artwork_zipfile_base_name = 'Final_Art_Zipfile_Hash__' + art_zipfile_hash + '.zip'
            path_to_final_artwork_zipfile_including_metadata = os.path.join(prepared_final_art_zipfiles_folder_path,final_artwork_zipfile_base_name)
            artist_signature_for_final_artwork_zipfile_including_metadata = sign_final_artwork_zipfile_including_metadata_with_artist_signature_func(zip_file_path, artist_private_key) #Again, this part will need to be done on the artists machin so the artist never has to reveal his private key.
            artist_signature_for_final_artwork_zipfile_including_metadata_base64_encoded = base64.b64encode(artist_signature_for_final_artwork_zipfile_including_metadata).decode('utf-8')
            final_signature_file_path = os.path.join(path_to_art_folder,'Final_Artist_Signature_for_Zipfile_with_Hash__' + art_zipfile_hash + '.sig')
            with open(final_signature_file_path,'w') as f:
                f.write(artist_signature_for_final_artwork_zipfile_including_metadata_base64_encoded)
            copyfile(zip_file_path, path_to_final_artwork_zipfile_including_metadata)
            return final_signature_file_path, path_to_final_artwork_zipfile_including_metadata
        except Exception as e:
            print('Error: '+ str(e))
            
def encode_final_art_zipfile_into_luby_transform_blocks_func(sha256_hash_of_art_file):
    global block_storage_folder_path
    global block_redundancy_factor
    global desired_block_size_in_bytes
    global prepared_final_art_zipfiles_folder_path
    start_time = time()
    ramdisk_object = MemoryFS()
    filesystem_object = OSFS(block_storage_folder_path)     
    c_constant = 0.1
    delta_constant = 0.5
    seed = randint(0, 1 << 31 - 1)
    path_to_final_artwork_zipfile_including_metadata = glob.glob(prepared_final_art_zipfiles_folder_path+'*'+sha256_hash_of_art_file+'*')[0]    
    final_art_file__original_size_in_bytes = os.path.getsize(path_to_final_artwork_zipfile_including_metadata)
    output_blocks_list = [] #Process ZIP file into a stream of encoded blocks, and save those blocks as separate files in the output folder:
    print('Now encoding file ' + os.path.split(path_to_final_artwork_zipfile_including_metadata)[-1] + ' (' + str(round(final_art_file__original_size_in_bytes/1000000)) + 'mb)\n\n')
    total_number_of_blocks_to_generate = ceil((1.00*block_redundancy_factor*final_art_file__original_size_in_bytes) / desired_block_size_in_bytes)
    print('Total number of blocks to generate for target level of redundancy: '+str(total_number_of_blocks_to_generate))
    pbar = tqdm(total=total_number_of_blocks_to_generate)
    with open(path_to_final_artwork_zipfile_including_metadata,'rb') as f:
        f_bytes = f.read()
    filesize = len(f_bytes) 
    art_zipfile_hash = hashlib.sha256(f_bytes).hexdigest()
    if art_zipfile_hash == sha256_hash_of_art_file: #Convert file byte contents into blocksize chunks, padding last one if necessary:
        blocks = [int.from_bytes(f_bytes[ii:ii+desired_block_size_in_bytes].ljust(desired_block_size_in_bytes, b'0'), sys.byteorder) for ii in range(0, len(f_bytes), desired_block_size_in_bytes)]
        number_of_blocks = len(blocks)
        print('The length of the blocks list: '+str(number_of_blocks))
        prng = PRNG(params=(number_of_blocks, delta_constant, c_constant))
        prng.set_seed(seed)
        number_of_blocks_generated = 0# block generation loop
        while number_of_blocks_generated <= total_number_of_blocks_to_generate:
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
        duration_in_seconds = round(time() - start_time, 1)
        print('\n\nFinished processing in '+str(duration_in_seconds) + ' seconds! \nOriginal zip file was encoded into ' + str(number_of_blocks_generated) + ' blocks of ' + str(ceil(desired_block_size_in_bytes/1000)) + ' kilobytes each. Total size of all blocks is ~' + str(ceil((number_of_blocks_generated*desired_block_size_in_bytes)/1000000)) + ' megabytes\n')
        print('Now copying encoded files from ram disk to local storage...')
        copy_fs(ramdisk_object,filesystem_object)
        print('Done!\n')
        ramdisk_object.close()
        return duration_in_seconds

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

def decode_block_files_into_art_zipfile_func(sha256_hash_of_art_file):
    global block_storage_folder_path
    global reconstructed_files_destination_folder_path
    global prepared_final_art_zipfiles_folder_path
    start_time = time()
    reconstructed_file_destination_file_path = os.path.join(reconstructed_files_destination_folder_path,'Final_Art_Zipfile_Hash__' + sha256_hash_of_art_file + '.zip')
    list_of_block_file_paths = glob.glob(os.path.join(block_storage_folder_path,'*'+sha256_hash_of_art_file+'*.block'))
    reported_file_sha256_hash = list_of_block_file_paths[0].split(os.sep)[-1].split('__')[1]
    print('\nFound '+str(len(list_of_block_file_paths))+' block files in folder! The SHA256 hash of the original zip file is reported to be: '+reported_file_sha256_hash+'\n')
    c_constant = 0.1
    delta_constant = 0.5
    block_graph = BlockGraph(len(list_of_block_file_paths))
    for block_count, current_block_file_path in enumerate(list_of_block_file_paths):
        with open(current_block_file_path,'rb') as f:
            packed_block_data = f.read()
        hash_of_block = hashlib.sha256(packed_block_data).hexdigest()
        reported_hash_of_block = current_block_file_path.split('__')[-1].replace('.block','').replace('BlockHash_','')
        if hash_of_block == reported_hash_of_block:
            pass #Block hash matches reported hash, so block is not corrupted
        else:
            print('\nError, the block hash does NOT match the reported hash, so this block is corrupted! Skipping to next block...\n')
            continue
        input_stream = io.BufferedReader(io.BytesIO(packed_block_data)) #,buffer_size=1000000
        header = unpack('!III', input_stream.read(12))
        filesize = header[0]
        blocksize = header[1]
        blockseed = header[2]
        number_of_blocks_required = ceil(filesize/blocksize)
        if block_count == 0:
            print('\nA total of '+str(number_of_blocks_required)+' blocks are required!\n')
        block = int.from_bytes(input_stream.read(blocksize),'big')
        input_stream.close
        if (block_count % 1) == 0:
            name_parts_list = current_block_file_path.split(os.sep)[-1].split('_')
            parsed_block_hash = name_parts_list[-1].replace('.block','')
            parsed_block_number = name_parts_list[6]
            parsed_file_hash = name_parts_list[2]
            print('\nNow decoding:\nBlock Number: ' + parsed_block_number + '\nFile Hash: ' + parsed_file_hash + '\nBlock Hash: '+ parsed_block_hash)
        prng = PRNG(params=(number_of_blocks_required, delta_constant, c_constant))
        _, _, src_blocks = prng.get_src_blocks(seed = blockseed)
        file_reconstruction_complete = block_graph.add_block(src_blocks, block)
        if file_reconstruction_complete:
            print('\nDone building file! Processed a total of '+str(block_count)+' blocks\n')
            break
        with open(reconstructed_file_destination_file_path,'wb') as f: 
            for ix, block_bytes in enumerate(map(lambda p: int.to_bytes(p[1], blocksize, 'big'), sorted(block_graph.eliminated.items(), key = lambda p:p[0]))):
                if ix < number_of_blocks_required - 1 or filesize % blocksize == 0:
                    f.write(block_bytes)
                else:
                    f.write(block_bytes[:filesize%blocksize])
    try:
        with open(reconstructed_file_destination_file_path,'rb') as f:
            reconstructed_file = f.read()
            reconstructed_file_hash = hashlib.sha256(reconstructed_file).hexdigest()
            if reported_file_sha256_hash == reconstructed_file_hash:
                completed_successfully = 1
                print('\nThe SHA256 hash of the reconstructed file matches the reported file hash-- file is valid! Now copying to prepared final art zipfiles folder...\n')
                shutil.copy(reconstructed_file,prepared_final_art_zipfiles_folder_path)
                print('Done!')
            else:
                completed_successfully = 0
                print('\nProblem! The SHA256 hash of the reconstructed file does NOT match the expected hash! File is not valid.\n')
    except Exception as e:
        print('Error: '+ str(e))
    duration_in_seconds = round(time() - start_time, 1)
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
            reported_block_sha256_hash = current_block_file_path.split(os.sep)[-1].split('__')[-1].replace('BlockHash_','').replace('.block','')
            if hash_of_block == reported_block_sha256_hash:
                reported_file_sha256_hash = current_block_file_path.split(os.sep)[-1].split('__')[1]
                if reported_block_sha256_hash not in potential_local_block_hashes_list:
                    potential_local_block_hashes_list.append(reported_block_sha256_hash)
                    potential_local_file_hashes_list.append(reported_file_sha256_hash)
            else:
                print('\nBlock '+reported_block_sha256_hash+' did not hash to the correct value-- file is corrupt! Skipping to next file...\n')
        print('\nDone verifying block files!\nNow writing local block metadata to SQLite database...\n')
    else:
        for current_block_file_path in list_of_block_file_paths:
            reported_block_sha256_hash = current_block_file_path.split(os.sep)[-1].split('__')[-1].replace('BlockHash_','').replace('.block','')
            reported_file_sha256_hash = current_block_file_path.split(os.sep)[-1].split('__')[1]
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
    conn.close()
    #print('Done writing file hash data to SQLite file!\n')  
    return potential_local_block_hashes_list, potential_local_file_hashes_list

###############################################################################################################
# Script:
###############################################################################################################
sqlite3.register_adapter(np.ndarray, convert_numpy_array_to_sqlite_func) # Converts np.array to TEXT when inserting
sqlite3.register_converter('array', convert_sqlite_data_to_numpy_array_func) # Converts TEXT to np.array when selecting
if use_demo_mode:
    if use_reset_system_for_demo:
        delete_all_blocks_and_zip_files_to_reset_system_func()
    list_of_required_folder_paths = [root_animecoin_folder_path,folder_path_of_remote_node_sqlite_files,reconstructed_files_destination_folder_path,artist_final_signature_files_folder_path,misc_masternode_files_folder_path,folder_path_of_art_folders_to_encode,prepared_final_art_zipfiles_folder_path,block_storage_folder_path]
    for current_required_folder_path in list_of_required_folder_paths:
        if not os.path.exists(current_required_folder_path):
            try:
                os.makedirs(current_required_folder_path)
            except Exception as e:
                    print('Error: '+ str(e)) 
    regenerate_dupe_detection_image_fingerprint_database_func() #only generates a new db file if it can't find an existing one.
    try:
        artist_public_key = pickle.load(open(os.path.join(root_animecoin_folder_path,'artist_public_key.p'),'rb'))
        artist_private_key = pickle.load(open(os.path.join(root_animecoin_folder_path,'artist_private_key.p'),'rb'))
    except:
        print('Couldn\'t load the demonstration artist pub/priv keys!')
        
    try:
        masternode_public_key, masternode_private_key = get_local_masternode_identification_keypair_func()
    except:
        print('Generating a new  local masternode identification keypair now...')
        generate_and_save_local_masternode_identification_keypair_func()#We don't have a masternode keypair so we first need to generate one. 
    if use_generate_new_sqlite_chunk_database:
        regenerate_sqlite_chunk_database_func()
    if use_generate_new_demonstration_artist_key_pair:
        artist_public_key, artist_private_key = generate_artist_public_and_private_keys_func()
        pickle.dump(artist_public_key, open(os.path.join(root_animecoin_folder_path,'artist_public_key.p'),'wb'))
        pickle.dump(artist_private_key, open(os.path.join(root_animecoin_folder_path,'artist_private_key.p'),'wb'))
        artist_public_key_export_format = rsa.PublicKey.save_pkcs1(artist_public_key,format='PEM').decode('utf-8')
        artist_private_key_export_format = rsa.PrivateKey.save_pkcs1(artist_private_key,format='PEM').decode('utf-8')
    print('\nWelcome! This is a demo of the file storage system that is being used for the Animecoin project to store art files in a decentralized, robust way.')    
    print('\nFirst, we begin by taking a bunch of folders; each one contains one or images representing a single digital artwork to be registered.')
    print('\nWe will now perform a series of steps on these files. First we verify they are valid images and that they are not NSFW or dupes.')
    print('\nThen we will have the artist sign all the art files with his digital signature, create the art metadata file, and finally have the artist sign the final art zipfile with metadata.')
    #sys.exit(0)
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
    if use_reconstruct_files:
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
    add_all_images_in_folder_to_image_fingerprint_database_func(path_to_all_registered_works_for_dupe_detection)    
    add_all_images_in_folder_to_image_fingerprint_database_func('C:\\anime_image_database\\')
    
    list_of_image_sha256_hashes, _, _, _, _, _, _ = apply_tsne_to_image_fingerprint_database_func()
    
    dupe_detection_test_images_base_folder_path = 'C:\\Users\\jeffr\\Cointel Dropbox\\Animecoin_Code\\dupe_detector_test_images\\' #Stress testing with sophisticated "modified" duplicates:
    path_to_original_dupe_test_image = os.path.join(dupe_detection_test_images_base_folder_path,'Arturo_Lopez__Number_02.png')
    hash_of_original_dupe_test_image = get_image_hash_from_image_file_path_func(path_to_original_dupe_test_image)
    image_similarity_df = find_most_similar_images_to_given_image_from_fingerprint_data_func(hash_of_original_dupe_test_image)
    
    #model_1_tsne_x_coordinate, model_1_tsne_y_coordinate, model_2_tsne_x_coordinate, model_2_tsne_y_coordinate,model_3_tsne_x_coordinate, model_3_tsne_y_coordinate,art_image_file_name = get_tsne_coordinates_for_desired_image_file_hash_func(hash_of_original_dupe_test_image)
    #is_dupe = check_if_image_is_likely_dupe(sha256_hash_of_art_image_file)
   
