from keras.models import Sequential
from keras.optimizers import SGD
from keras.layers import Input, Dense, Conv2D, MaxPooling2D, AveragePooling2D, ZeroPadding2D, Dropout, Flatten, merge, Reshape, Activation

def vgg16_model(height, width, channels=3, num_classes=None):
    model = Sequential([
        Conv2D(64, (3, 3), input_shape=(channels, width, height), padding='same',
               activation='relu'),
        Conv2D(64, (3, 3), activation='relu', padding='same'),
        MaxPooling2D(pool_size=(2, 2), strides=(2, 2)),
        Conv2D(128, (3, 3), activation='relu', padding='same'),
        Conv2D(128, (3, 3), activation='relu', padding='same', ),
        MaxPooling2D(pool_size=(2, 2), strides=(2, 2)),
        Conv2D(256, (3, 3), activation='relu', padding='same', ),
        Conv2D(256, (3, 3), activation='relu', padding='same', ),
        Conv2D(256, (3, 3), activation='relu', padding='same', ),
        MaxPooling2D(pool_size=(2, 2), strides=(2, 2)),
        Conv2D(512, (3, 3), activation='relu', padding='same', ),
        Conv2D(512, (3, 3), activation='relu', padding='same', ),
        Conv2D(512, (3, 3), activation='relu', padding='same', ),
        MaxPooling2D(pool_size=(2, 2), strides=(2, 2)),
        Conv2D(512, (3, 3), activation='relu', padding='same', ),
        Conv2D(512, (3, 3), activation='relu', padding='same', ),
        Conv2D(512, (3, 3), activation='relu', padding='same', ),
        MaxPooling2D(pool_size=(2, 2), strides=(2, 2)),
        # Flatten(),
        # Dense(4096, activation='relu'),
        # Dense(4096, activation='relu'),
        # Dense(1000, activation='softmax')
    ])

    print(model.summary())

    for layer in model.layers[:15]:
        layer.trainable = False

    model.load_weights('pretrained_weights/vgg16_weights_notop.h5')

    model.add(Flatten())
    model.add(Dense(4096, activation='relu'))
    model.add(Dropout(0.5))
    model.add(Dense(4096, activation='relu'))
    model.add(Dropout(0.5))
    model.add(Dense(num_classes, activation='softmax'))

    sgd = SGD(lr=1e-3, decay=1e-6, momentum=0.9, nesterov=True)
    model.compile(optimizer=sgd, loss='sparse_categorical_crossentropy', metrics=['accuracy'])

    return model
