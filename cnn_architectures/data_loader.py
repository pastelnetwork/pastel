import numpy as np

def downsample_image(img, ratio=4.):
    from skimage import transform as tf
    return tf.rescale(img, 1./ratio)

def downsample_data(images):
    data4 = []
    data8 = []
    images = images.astype('uint8')
    for img in images:
        data4.append(downsample_image(img, 4))
        data8.append(downsample_image(img, 8))
    return np.asarray(data4), np.asarray(data8)

def get_augmented_data(x, y, n_samples):
    path='datasets/CIFAR10/data_batch_1_augm'
    images = []
    labels = []
    # files = glob.glob(path+'*.png')
    # if len(files) != 0:
    #     from PIL import Image
    #     for f in files:
    #         img = Image.open(f)
    #         images.append(img)
    #     return np.asarray(images)
    # else:
    from keras.preprocessing.image import ImageDataGenerator
    print('Generating augmented dataset.')
    datagen = ImageDataGenerator()
    datagen.fit(x)
    for new_x, new_y in datagen.flow(x, y, batch_size=n_samples): #,
                                     # save_to_dir=path,
                                     # save_prefix='aug',
                                     # save_format='png'):
        images.append(new_x)
        labels.append(new_y)
        break
    return np.asarray(images)[0], np.asarray(labels)[0]

def add_augmented_data(X, y):
    # TODO make generation for each class of images
    # instead of training similar images with itself
    X_aug, y_aug = get_augmented_data(X, y, len(X))

    X_full = np.concatenate((X, X_aug))
    y_full = np.concatenate((y, y_aug))

    return X_full, y_full


def load_data(name='cifar10', file='data_batch_1'):
    import _pickle
    f = open('datasets/' + name + '/' + file, 'rb')
    dict = _pickle.load(f, encoding='latin1')
    images = dict['data']
    images = np.reshape(images, (10000, 3, 32, 32))
    images = images.transpose([0, 2, 3, 1])
    # debugging
    n = 2000
    images = images[:n, :, :, :]
    labels = dict['labels']
    X = np.array(images)  # (10000, 3072)
    y = np.array(labels)  # (10000,)
    # debugging
    y = y[:n]
    return X, y

def split_data(X, y, test_size=0.33, seed=0):
    from sklearn.model_selection import train_test_split
    return train_test_split(X, y, test_size=test_size, random_state=seed)