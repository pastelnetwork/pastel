import keras
from keras.layers import Input, Lambda, Dense, Conv2D, Flatten, MaxPooling2D
from keras.models import Model
from vgg16 import vgg16_model


def downsample_cnn_model(shape1, shape2, channels):
    # get im w
    # subsample im1 as 4:1, im2 as 8:1 (no scaling!)
    # im1 goes thru CNN3 w max pooling
    # im2 goes thru CNN2 w max pooling
    # at this point no weights are shared
    import keras.backend as K

    input1 = Input(shape=(shape1[0], shape1[1], channels), name='input1')
    image1 = Conv2D(64, (3,3), activation='relu', padding='same')(input1)
    image1 = Conv2D(64, (3,3), activation='relu', padding='same')(image1)
    image1 = Conv2D(64, (3,3), activation='relu', padding='same')(image1)
    image1 = MaxPooling2D(pool_size=(2, 2), strides=(2, 2))(image1)
    image1 = Flatten()(image1)
    image1 = Dense(1024, activation='sigmoid')(image1)
    l2_norm1 = Lambda(lambda x: K.l2_normalize(x, axis=1))(image1)

    input2 = Input(shape=(shape2[0], shape2[1], channels), name='input2')
    image2 = Conv2D(64, (3,3), activation='relu', padding='same')(input2)
    image2 = Conv2D(64, (3,3), activation='relu', padding='same')(image2)
    image2 = MaxPooling2D(pool_size=(2, 2), strides=(2, 2))(image2)
    image2 = Dense(512, activation='sigmoid')(image2)
    # normalize embedding
    l2_norm2 = Lambda(lambda x: K.l2_normalize(x, axis=1))(image2)

    model = Model(inputs=[input1, input2], outputs=[image1, image2])
    return model

def simnet_model(original_shape, scaled_shape1, scaled_shape2, channels, num_classes):

    def simnet_unit():
        vgg = vgg16_model(original_shape[0], original_shape[1], channels, num_classes)
        ds = downsample_cnn_model(scaled_shape1, scaled_shape2, channels)

        x = keras.layers.concatenate([vgg.outputs, ds.outputs])
        # linear embedding
        x = Dense(4096)(x)
        return Model(inputs=[vgg.inputs, ds.inputs], outputs=x)

    shared_unit = simnet_unit()

    #im1, im2
    im1 = [
               Input(shape=(original_shape[0], original_shape[1], channels)),
               Input(shape=(scaled_shape1[0], scaled_shape1[1], channels)),
               Input(shape=(scaled_shape2[0], scaled_shape2[1], channels)),
            ]

    im2 = [
               Input(shape=(original_shape[0], original_shape[1], channels)),
               Input(shape=(scaled_shape1[0], scaled_shape1[1], channels)),
               Input(shape=(scaled_shape2[0], scaled_shape2[1], channels)),
           ]

    embedded1 = shared_unit(im1)
    embedded2 = shared_unit(im2)
    model = Model(inputs=[im1, im2], outputs=[embedded1, embedded2])

    from keras.optimizers import SGD
    sgd = SGD(lr=1e-3, decay=1e-6, momentum=0.9, nesterov=True)
    # loss =
    model.compile(optimizer=sgd, loss='sparse_categorical_crossentropy', metrics=['accuracy'])
