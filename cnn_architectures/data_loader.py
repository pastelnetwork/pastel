from keras import backend
import numpy as np

def load_data(name='cifar10', file='data_batch_1'):
    import _pickle
    f = open('datasets/' + name + '/' + file, 'rb')
    dict = _pickle.load(f, encoding='latin1')
    images = dict['data']
    backend.set_image_data_format('channels_first')
    images = np.reshape(images, (10000, 3, 32, 32))
    images = images[:2000]
    labels = dict['labels']
    X = np.array(images)  # (10000, 3072)
    y = np.array(labels)  # (10000,)
    return X, y

def split_data(X, y, test_size=0.33, seed=0):
    from sklearn.model_selection import train_test_split
    return train_test_split(X, y, test_size=test_size, random_state=seed)