from data_loader import load_data, split_data
from vgg16 import vgg16_model
from keras.layers import Input

def foo(X_train):
    input_shape = X_train[0,:,:,:].shape
    model_input = Input(shape=input_shape)


if __name__ == '__main__':

    img_rows, img_cols = 32, 32 # Resolution of inputs
    channel = 3
    num_classes = 10
    batch_size = 16
    nb_epoch = 10

    from sklearn.metrics import log_loss
    X, y = load_data('CIFAR10')
    X_train, X_valid, Y_train, Y_valid = split_data(X, y)

    model = vgg16_model(img_rows, img_cols, channel, num_classes)

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
