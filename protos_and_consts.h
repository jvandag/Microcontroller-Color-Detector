#ifndef _PROTOS_AND_CONSTS_H
#define _PROTOS_AND_CONSTS_H

// PINs
#define BTN1 14 // K10 on board
#define BTN2 15 // M10 on board
#define POTENTIOMETER_PIN 13 // A12, Q1 on board
#define PHOTOTRANSISTOR_PIN A3
//#define BUTTON_PIN 2
#define RED_LED 33 
#define YELLOW_LED 12
#define GREEN_LED 27
#define BLUE_LED 25
//#define TOGGLE_SWITCH_PIN 19 //A5
#define OLED_RST_PIN -1  // Reset pin (-1 if not available)
#define CS_PIN 4 // SD card chip select


// OLED SCREEN PARAMETERS
#define SCREEN_I2C_ADDR 0x3C // or 0x3C
#define SCREEN_WIDTH 128     // OLED display width, in pixels
#define SCREEN_HEIGHT 64     // OLED display height, in pixels
#define FRAME_DELAY 42
#define FRAME_WIDTH 48
#define FRAME_HEIGHT 48
#define SEL_SCROLL_SPD 12
#define SEL_MAP_WIDTH 1024
#define SEL_SCR_COUNT 8

// MISC
#define ANALOG_RES 4095
#define LOADING_DELAY 1500
#define SAMPLES_PER_GROUP 12
#define NUM_COLOR_VALUES 4 //Red, blue, green, and yellow values


enum menus {
  DET_BLACK,
  DET_WHITE,
  DET_RED,
  DET_ORANGE,
  DET_YELLOW,
  DET_GREEN,
  DET_BLUE,
  DET_PURPLE,
  READY,
  ENABLE_TRAINING,
  DISABLE_TRAINING,
  DEL_TRAINING,
  COLOR_SEL,
  LOADING,
  COLOR_LOGGED,
};

enum colors {
  black,
  white,
  red,
  orange,
  yellow,
  green,
  blue,
  purple
};

// Checks if each color has atleast the minimum number of samples needed.
// Sets the string notEnoughSamples to a list of the groups that don't ahve enough samples
void checkMinNumSamples();

// Displays the loading screen
void displayLoading();

// displays the ready to scan screen
void displayReady();

// Returns the string name of the enum color
String color2Txt(int color);

// Sets the display to be a yes no option menu with prompt text based on the current_menu variable
void displayYesNoOpt();

// Sets the display to show the appropriate slice of the select menu bitmap when detecting a color
void displayDetColorText();

// Deletes a file at the given path
void deleteFile(fs::FS &fs, const String &path);

// Appends to the file at the given path, creates a new file if the file doesn't exist
void appendFile(fs::FS &fs, const String &path, const String &message);

// Overwrites a file at the given path, creates a new file if the file doesn't exist
void writeFile(fs::FS &fs, const char *path, const char *message);

// Lists the files in a directory on the SD card
void listDir(fs::FS &fs, const char *dirname, uint8_t levels);

// Gathers and writes color training data to the SD card
void writeColor(int pin, fs::FS &SD);

// Similar to writeColor() but doesn't write to the SD card
int readColor(int pin);

// Returns the number of digits in an integer
int getNumDigits(int num);

// Updates the current menu to prompt displayLoop to change the display to the color
void displayColor(colors color);

// Retrieves training data from the SD card and loads it into colorDetector.trainingData
void getDataFromCard();

/*
The classify() function utilizes the KNN algorithm
k is the number of nearby neighbors
*/
int classify(struct sample arr[], int numTrainingSamples, int k, const sample node);

//Takes two colorValue arrays and finds the distance between each color value.
//We're using the formula d=sqrt(sum((pixel_1 - pixel_2)^2))
unsigned int colorValueDistance(const sample s1, sample s2);

// inserstion sort for the passed in sample array
void insertionSort(sample arr[], int n);

// Changes the color of the display elements based on the provided enum value
void changeColor(enum colors c);

// Draws text at a specified position on the display
void drawTextAtPosition(int x, int y, String txt, bool center, float size);

// Generates a processing string with dynamic ellipsis based on frame count
String getProcessingString(int num_frames);

// Calculates the offset based on POTENTIOMETER_PIN value and adjusts the selection position
int getOffset(int &selectPos);

// The display loop that runs on a separate core from the processing loop.
void displayLoop(void* parameter);

// The processing loop that runs on a separate core from the display loop.
void processingLoop(void* parameter);

// Subsections the bitmap used for the training menu select options
void drawSelMenuSlice(int xOffset);

#endif //_PROTOS_AND_CONSTS_H