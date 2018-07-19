import hashlib, sqlite3, imghdr, glob, os, io
import tensorflow as tf
import numpy as np
import pandas as pd
from sklearn import decomposition, manifold, pipeline
from tqdm import tqdm
from time import time
from PIL import Image
from keras.preprocessing import image
from keras.applications.imagenet_utils import preprocess_input
from keras import applications

#requirements: pip install numpy keras tensorflow tqdm pillow sklearn pandas
root_animecoin_folder_path = '/Users/jemanuel/animecoin/'
misc_masternode_files_folder_path = os.path.join(root_animecoin_folder_path,'misc_masternode_files' + os.sep) #Where we store some of the SQlite databases
dupe_detection_image_fingerprint_database_file_path = os.path.join(misc_masternode_files_folder_path,'dupe_detection_image_fingerprint_database.sqlite')
path_to_all_registered_works_for_dupe_detection = '/Users/jemanuel/Cointel Dropbox/Animecoin_Code/Animecoin_All_Finished_Works/'
dupe_detection_test_images_base_folder_path = '/Users/jemanuel/Cointel Dropbox/Animecoin_Code/dupe_detector_test_images/' #Stress testing with sophisticated "modified" duplicates:

duplicate_image_threshold = 0.08 #Any image which has another image in our image fingerprint database with a "distance" of less than this threshold will be considered a duplicate.

max_image_preview_pixel_dimension = 800

def get_sha256_hash_of_input_data_func(input_data_or_string):
    if isinstance(input_data_or_string, str):
        input_data_or_string = input_data_or_string.encode('utf-8')
    sha256_hash_of_input_data = hashlib.sha3_256(input_data_or_string).hexdigest()
    return sha256_hash_of_input_data

def get_image_hash_from_image_file_path_func(path_to_art_image_file):
    try:
        with open(path_to_art_image_file,'rb') as f:
            art_image_file_binary_data = f.read()
        sha256_hash_of_art_image_file = get_sha256_hash_of_input_data_func(art_image_file_binary_data)
        return sha256_hash_of_art_image_file
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
        sha256_hash_of_current_art_file = get_sha256_hash_of_input_data_func(image_file_binary_data)                
        image_sha256_hash_to_image_file_path_dict[sha256_hash_of_current_art_file] = current_image_file_path
    return image_sha256_hash_to_image_file_path_dict[sha256_hash_of_art_image_file]

def get_image_deep_learning_features_func(path_to_art_image_file):
    dupe_detection_model_1_name = 'VGG19'
    dupe_detection_model_2_name = 'Xception'
    dupe_detection_model_3_name = 'ResNet50'
    global dupe_detection_model_1
    global dupe_detection_model_2
    global dupe_detection_model_3
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

def add_image_fingerprints_to_dupe_detection_database_func(path_to_art_image_file):
    #path_to_art_image_file = current_image_file_path
    global dupe_detection_image_fingerprint_database_file_path
    model_1_image_fingerprint_vector,model_2_image_fingerprint_vector, model_3_image_fingerprint_vector, sha256_hash_of_art_image_file, dupe_detection_model_1, dupe_detection_model_2, dupe_detection_model_3 = get_image_deep_learning_features_func(path_to_art_image_file)
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
    #path_to_art_folder = path_to_all_registered_works_for_dupe_detection
    valid_image_file_paths = get_all_valid_image_file_paths_in_folder_func(path_to_art_folder)
    #current_image_file_path = valid_image_file_paths[0]
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

def check_if_image_is_likely_dupe_func(sha256_hash_of_art_image_file):
    global duplicate_image_threshold
    image_similarity_df = find_most_similar_images_to_given_image_from_fingerprint_data_func(sha256_hash_of_art_image_file)
    possible_dupes = image_similarity_df[image_similarity_df['overall_image_similarity_metric'] <= duplicate_image_threshold ]
    if len(possible_dupes) > 0:
        is_dupe = 1
        print('WARNING! Art image file appears to be a dupe! Hash of suspected duplicate image file: '+sha256_hash_of_art_image_file)
    else:
        print('Art image file appears to be original! (i.e., not a duplicate of an existing image in our image fingerprint database)')
        is_dupe = 0
    return is_dupe


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


use_demonstrate_duplicate_detection = 0
sqlite3.register_adapter(np.ndarray, convert_numpy_array_to_sqlite_func) # Converts np.array to TEXT when inserting
sqlite3.register_converter('array', convert_sqlite_data_to_numpy_array_func) # Converts TEXT to np.array when selecting

if use_demonstrate_duplicate_detection:
    add_all_images_in_folder_to_image_fingerprint_database_func(path_to_all_registered_works_for_dupe_detection)
    list_of_image_sha256_hashes, _, _, _, _, _, _ = apply_tsne_to_image_fingerprint_database_func()
    path_to_original_dupe_test_image = os.path.join(dupe_detection_test_images_base_folder_path,'Arturo_Lopez__Number_02.png')
    hash_of_original_dupe_test_image = get_image_hash_from_image_file_path_func(path_to_original_dupe_test_image)
    image_similarity_df = find_most_similar_images_to_given_image_from_fingerprint_data_func(hash_of_original_dupe_test_image)
    model_1_tsne_x_coordinate, model_1_tsne_y_coordinate, model_2_tsne_x_coordinate, model_2_tsne_y_coordinate,model_3_tsne_x_coordinate, model_3_tsne_y_coordinate,art_image_file_name = get_tsne_coordinates_for_desired_image_file_hash_func(hash_of_original_dupe_test_image)
    sha256_hash_of_art_image_file = hash_of_original_dupe_test_image
    is_dupe = check_if_image_is_likely_dupe_func(sha256_hash_of_art_image_file)
    
    
    
    
    
    
    
    
    
