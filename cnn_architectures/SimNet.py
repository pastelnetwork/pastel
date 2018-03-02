import keras
import numpy as np
from keras.applications import vgg16
from keras.optimizers import SGD
# custom imports
import finetune_vgg16 as vgg16
class SimNet:
    def __init__(self, im_wdt, im_hdt, epochs, pretrained_model='vgg16', finetune=True):
        if pretrained_model == 'vgg16':
            self.pretrained_model = vgg16.VGG16(weights='imagenet', include_top=finetune)
        else:
            print('Not available yet')
        self.im_wdt = im_wdt
        self.im_hdt = im_hdt
        self.epochs = epochs
        self.weights_id = 0

    def build_full_model(self):
        self.model = keras.Sequential()
        # add layers here
        self.model.compile(loss='categorical_crossentropy', optimizer='sgd', metrics=['accuracy'])

        self.optimizer_sgd = SGD(lr=1e-3, decay=1e-6, momentum=0.9, nesterov=True)
        self.pretrained_model.compile(optimizer=self.optimizer_sgd, loss='categorical_crossentropy', metrics=['accuracy'])
        model = vgg_std16_model(img_rows, img_cols, channel, num_class)

    def generate_data(self, batch_size=10):
        from keras.preprocessing.image import ImageDataGenerator
        train_generator = ImageDataGenerator(
                rescale=1./255,
                shear_range=0.2,
                zoom_range=0.2,
                horizontal_flip=True)

        test_generator = ImageDataGenerator(rescale=1./255)

        self.train_data = train_generator.flow_from_directory(
                'images/train',
                target_size=(150, 150),  # all images will be resized to 150x150
                batch_size=batch_size,
                class_mode='sparse')  # since we use binary_crossentropy loss, we need binary labels

        self.validation_data = test_generator.flow_from_directory(
                'images/validation',
                target_size=(150, 150),
                batch_size=batch_size,
                class_mode='sparse')

    # find out if any heuristics exist or bruteforce it
    def train_model(self, n_train_batches, n_val_batches):
        self.model.fit_generator(
            generator=self.train_data,
            steps_per_epoch=n_train_batches,
            validation_data=self.validation_data,
            validation_steps=n_val_batches,
            epochs=self.epochs,
        )
        self.model.save_weights('wee=ights/w{}.h5'.format(self.weights_id))
        self.weights_id+=1

    # GPU only computations
    # def train_w_CV(self, data, labels, splits=5):
    #     kfold = StratifiedKFold(n_splits=splits, shuffle=True, random_state=seed)
    #     cvscores = []
    #     for train, test in kfold.split(X, Y):
    #         self.model.fit(data[train], labels[train], self.epochs, self.batch_size)
    #         scores = model.evaluate(X[test], Y[test], verbose=0)
    #         print("%s: %.2f%%" % (model.metrics_names[1], scores[1] * 100))
    #         cvscores.append(scores[1] * 100)
    #     print("%.2f%% (+/- %.2f%%)" % (numpy.mean(cvscores), numpy.std(cvscores)))

if __name__ == '__main__':

    vgg_model = vgg16.VGG16(weights='imagenet', include_top=False)

    from keras.preprocessing.image import load_img
    from keras.preprocessing.image import img_to_array
    from keras.applications.imagenet_utils import decode_predictions

    filename = 'images/cat.jpg'
    # load an image in PIL format
    original = load_img(filename, target_size=(224, 224))
    numpy_image = img_to_array(original)
    image_batch = np.expand_dims(numpy_image, axis=0) # reshape by adding extra dim
    image = vgg16.preprocess_input(image_batch)

    pred = vgg_model.predict(image)
    print(decode_predictions(pred))