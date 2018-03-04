import numpy as np

def downsample_image(img, ratio=4.):
    from skimage import transform as tf
    return tf.rescale(img, 1./ratio)

def downsample_data(images):
    new_data = []
    for img in images:
        new_data.append(downsample_image(img))


def load_data(name='cifar10', file='data_batch_1'):
    import _pickle
    f = open('datasets/' + name + '/' + file, 'rb')
    dict = _pickle.load(f, encoding='latin1')
    images = dict['data']
    images = np.reshape(images, (10000, 32, 32, 3))
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