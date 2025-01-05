# Microcontroller-Color-Detector
Detects the color of physical objects using a phototransistor and simple machine learning.

## Pre-trained Example
Once the color detector has a full set of training data the user can scan a color using the blue button. The scan data is then compared against the training data to find the closest color match.

https://github.com/user-attachments/assets/66921391-efe7-4fe2-914a-f40178192495

## Training menu
The prompt to swap to training mode can be entered by pressing the yellow button. Once in training mode, additional samples may be taken the same way you would scan a color with the blue button. While in training mode, both buttons may be pressed at once to prompt deleting the stored training data. Each color requires 12 samples to function properly. If scanning/ready mode is entered when there is not sufficient training data, a message will display the colors that need more samples. 

https://github.com/user-attachments/assets/d95f9c35-6fea-4ae8-9513-e3b5f32a4a7d

## Circuit Diagram
The following circuit diagram is the one used in the example videos. Most microcontrollers that can run arduino code and have sufficient memory as well as two processing cores should have no problem replacing the [Adafruit ESP32 Feather V2]([url](https://learn.adafruit.com/adafruit-esp32-feather-v2)) that was used in the example. 

**Some things to note if using a different microcontroller:**
- Your analog resolution may be different so make sure to set the [ANALOG_RES]([url](https://github.com/jvandag/Microcontroller-Color-Detector/blob/ead716868bf5c02b5b45c210d8dfff97584aa8df/protos_and_consts.h#L31)) macro to your microcontroller's analog resolution.
- The example microcontroller has an internal pull up and pull down buttons that don't have resistors on the ground path. This is fine in this case but make sure that this wont fry your microcontroller or use pull down resistors.
- You may also have a similar problem with the LEDs. The out put on the example microcontroller output 3.3V to each LED which is a little high for some of them. You definitely should use resistors here if you have a higher voltage microcontroller.

Lastly, the 4 mega-ohm pull down on the photo transistor should be adjusted to fit your particular 3D print and microcontroller. If you find your phototransistor readings are too high, use a smaller pull down resistance. __I would highly recommend design your own case instead of using the stl files in this repository.__ The model is a bit small making it difficult to fit all of the components into the case and uncomfortable to hold the device as the grip area is too small. Additionally, bottom section that shields the transistor from external light is to long. In the example videos this section was 3mm from the paper to where the phototransistor was mounted, using a smaller distance would provide better readings, especially if you're using regular LEDs like the example.

![circuit_diagram](https://github.com/user-attachments/assets/a45955fe-6d7b-463b-b62e-5d7ab6ff9300)
