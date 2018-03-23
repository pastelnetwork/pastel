from data_loader import load_data, split_data, downsample_data, add_augmented_data
from vgg16 import vgg16_model
from SimNet import simnet_model
from keras.layers import Input
import numpy as np

if __name__ == '__main__':

    num_classes = 10
    batch_size = 16
    nb_epoch = 10
    from sklearn.metrics import log_loss
    X, y = load_data('CIFAR10')
    # from PIL import Image
    # Image.fromarray(X[220], 'RGB').show()

    def test_vgg():
        X_train, X_valid, Y_train, Y_valid = split_data(X, y)
        img_rows, img_cols, channels = X_train[0, :, :, :].shape

        model = vgg16_model(img_rows, img_cols, channels, num_classes)

        model.fit(X_train, Y_train,
                  batch_size=batch_size,
                  epochs=nb_epoch,
                  shuffle=True,
                  verbose=1,
                  validation_data=(X_valid, Y_valid),
                  )

        predictions_valid = model.predict(X_valid, batch_size=batch_size, verbose=1)

        score = log_loss(Y_valid, predictions_valid)
        print(score)

    def test_simnet():
        # make augmented data
        X_train, y_train = add_augmented_data(X, y)

        X_train4, X_train8 = downsample_data(X_train)  # reuse labels

        mdl = simnet_model(X_train[0,:,:,0].shape,
                           X_train4[0, :, :, 0].shape,
                           X_train8[0, :, :, 0].shape,
                           3, 10)
        # model requires 1 normal img, 2 downsampled + label (1 if similar, 0 if dissimilar)
        # TODO prepare dataset and make one pass randomized training
        # train on similar
        mdl.fit(
            {'img1' : X_train, 'img1_sc1' : X_train4, 'img1_sc2' : X_train8,
            'img2' : X_train, 'img2_sc1' : X_train4, 'img2_sc2' : X_train8},
            {'distance' : np.zeros(len(y_train))},
            epochs=10,
            batch_size=100,
            validation_split=0.33
        )
        # train on different
        n_samples = len(y_train)
        # just a trick to avoid reverting
        distances = [1 if y_train[i] != y_train[n_samples-i-1] else 0 for i in range(n_samples)]
        mdl.fit(
            {'img1': X_train, 'img1_sc1': X_train4, 'img1_sc2': X_train8,
             'img2': np.flip(X_train, 0), 'img2_sc1': np.flip(X_train4, 0), 'img2_sc2': np.flip(X_train8, 0)},
            {'distance': np.zeros(len(y_train))},
            epochs=10,
            batch_size=100,
            validation_split=0.33
        )

    test_simnet()