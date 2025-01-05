#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>
#include <SD.h>
#include <math.h>
#include "protos_and_consts.h"
#include "screen_bitmaps.h"

// User Input
int selectPos    = 1024 * (analogRead(POTENTIOMETER_PIN)) / 4095;
bool btn1Val     = true; // false when pressed (pull down btn)
bool btn2Val     = true; // false when pressed (pull down btn)
bool btn1LastVal = true; 
bool btn2LastVal = true;

//OLED SCREEN PARAMETERS
int LOADING_FRAME_COUNT = (sizeof(loading_frames) / sizeof(loading_frames[0]));

Adafruit_SSD1306 display(128, 64, &Wire, OLED_RST_PIN);

unsigned long t;
unsigned long time_checkpoint;
int loadingMenuFrame  = 0;
int textX = 0, textY  = 0;  // Text position
String text           = "";
bool select_screen    = true;
bool scrolling        = false;
bool scroll_end       = true;
bool scroll_start     = true;
int current_menu      = 0;
bool YesNoSel         = false;
int charWidth         = 6; 


// MISC
String logMsg                   = "";
bool quit                       = false;
int lastTrainedColor            = -1;
unsigned long lastLogMsgUpdate  = -1;
bool awaitingUsrSel             = false;
String notEnoughSamples         = "";
int pot_val;


// Data Containers

//Indicates wether each category has enough samples
int sampleCounts[8] = {0};

struct sample {
  //A sample of the values read by the phototransistor from the colored LEDs
  unsigned int groupValue : 3;            //We only need 3 bits to categorize 6 groups
  uint8_t colorValues[NUM_COLOR_VALUES];
  unsigned int distance : 11;             //The biggest distance can be 2046 units

  //Overloading the equal operator to set all values of one sample equal to another sample
  sample &operator=(const sample &s) {
    for (int a = 0; a < NUM_COLOR_VALUES; a++) {
      colorValues[a] = s.colorValues[a];
    }
    distance = s.distance;
    groupValue = s.groupValue;
    return *this;
  }
};

struct Group {
  unsigned int id : 3;         //There are only 8 groups, so we only need 3 bits
  unsigned int frequency : 4;  //We're limiting it to 12 samples per group, and 4 bits give us 15
};

struct {
  //Used to keep track of global memory in a tight compartment
  unsigned char trainingModeActive : 1;  //Basically a boolean
  unsigned char currentlyReading : 1;    //Debounce for button press
  unsigned char loadingRequired : 1;     //When we flip the toggle switch to sampling, we need to load the data
  unsigned int selectedGroupId : 4;      //What's the potentiometer pointing to?
  sample trainingData[8 * SAMPLES_PER_GROUP];
} colorDetector;



// Creating a process for each core
TaskHandle_t DisplayLoopHandle = NULL;
TaskHandle_t DataProcessingLoopHandle = NULL;

void setup() {
  Serial.begin(9600);
  while (!Serial) {
    delay(10);
  }
  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(POTENTIOMETER_PIN, INPUT);
  pinMode(PHOTOTRANSISTOR_PIN, INPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);

  if (!SD.begin(CS_PIN)) {
    Serial.println("Card Mount Failed");
    return;
  } else {
    getDataFromCard();
    listDir(SD, "/", 0);
  }

  pot_val = analogRead(POTENTIOMETER_PIN);
  colorDetector.trainingModeActive = 0;
  checkMinNumSamples();
  current_menu = READY;

  // Create tasks and pin them to specific cores
  xTaskCreatePinnedToCore(
    displayLoop,          // Task function
    "Display Loop Task",  // Name of the task
    12000,                // Stack size for the task in words (num bytes / 4)
    NULL,                 // Parameter to pass to the task
    2,                    // Task priority
    &DisplayLoopHandle,   // Task handle
    1                     // Core ID (1 for Core 1)
  );

  xTaskCreatePinnedToCore(processingLoop, "Data Processing Loop Task", 12000, NULL, 2, &DataProcessingLoopHandle, 0);
}

void loop() {}

void displayLoop(void *parameter) {
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_I2C_ADDR)) {
    Serial.println("SSD1306 allocation failed");
    vTaskDelete(NULL);  // End the task if display initialization fails
  }

  String t_text = "Training:";
  int offset;
  
  while (true) {
    display.clearDisplay();
    t = millis();
    pot_val = analogRead(POTENTIOMETER_PIN);

    if (colorDetector.trainingModeActive && !awaitingUsrSel && current_menu != LOADING) {
      if (t - lastLogMsgUpdate < 2000 && logMsg.length() > 0) {
        drawTextAtPosition(0, 0, logMsg, true, 1.5);
      }
      else if (colorDetector.trainingModeActive && current_menu != LOADING) {
        drawTextAtPosition(0, 0, t_text, true, 1.5);
      }
    }
    switch (current_menu) {
      case DET_BLACK:
      case DET_WHITE: 
      case DET_RED:
      case DET_ORANGE:
      case DET_YELLOW:
      case DET_GREEN:
      case DET_BLUE:
      case DET_PURPLE:
        displayDetColorText();
        break;
      case READY:
        displayReady();
        break;
      case ENABLE_TRAINING:
      case DISABLE_TRAINING:
      case DEL_TRAINING:
        displayYesNoOpt();
        break;
      case COLOR_LOGGED:
      case COLOR_SEL: 
        offset = getOffset(selectPos);
        drawSelMenuSlice(offset);
        break;
      case LOADING:
        displayLoading();
        break;
      default: break;
    }

    display.display();
    // Delay to yield to the OS and avoid watchdog issues
    vTaskDelay(15 / portTICK_PERIOD_MS);
  }
}

void processingLoop(void *parameter) {
  bool btn1LastState = true;
  bool btn2LastState = true;

  while(true) {
    btn1Val = digitalRead(BTN1);
    btn2Val = digitalRead(BTN2);

    if(awaitingUsrSel) {
      if(!btn1Val && btn1LastState) {
        if (YesNoSel) {
          switch(current_menu) {
            case ENABLE_TRAINING:
            case DISABLE_TRAINING:
              checkMinNumSamples();
              colorDetector.trainingModeActive = !colorDetector.trainingModeActive;
              current_menu = colorDetector.trainingModeActive ? COLOR_SEL : READY;
              break;
            case DEL_TRAINING: 
              deleteTraining(SD);
              colorDetector.trainingModeActive = 1;
              current_menu = COLOR_SEL;
              break;
            default: 
              current_menu = COLOR_SEL;
              break;
          }
          
        }
        else current_menu = colorDetector.trainingModeActive ? COLOR_SEL : READY;
        awaitingUsrSel = false;
      }
      else if (!btn2Val && btn2LastState) {
        current_menu = colorDetector.trainingModeActive ? COLOR_SEL : READY;
        awaitingUsrSel = false;
      }
      btn1LastState = btn1Val;
      btn2LastState = btn2Val;
      continue;
    }
    
    // If one button is pressed wait a moment to see if the user is
    // trying to press both buttons at once
    if(btn1Val && !btn2Val) {
      vTaskDelay(100 / portTICK_PERIOD_MS);
      btn1Val = digitalRead(BTN1);
    }
    else if (!btn1Val && btn2Val) {
      vTaskDelay(100 / portTICK_PERIOD_MS);
      btn2Val = digitalRead(BTN2);
    }
    
    if (current_menu != LOADING && btn1Val && !btn2Val && btn2LastState) {
      //change bring up toggle training mode menu
      current_menu = (colorDetector.trainingModeActive || current_menu == DISABLE_TRAINING) ? DISABLE_TRAINING : ENABLE_TRAINING;
      awaitingUsrSel = true;
      btn1LastState = btn1Val;
      btn2LastState = btn2Val;
      //add delay between checks
      vTaskDelay(15 / portTICK_PERIOD_MS);
      continue;
    }
    else if (current_menu != LOADING && colorDetector.trainingModeActive && !btn1Val && !btn2Val) {
      //change bring up toggle training mode menu
      current_menu = DEL_TRAINING;
      awaitingUsrSel = true;
      btn1LastState = btn1Val;
      btn2LastState = btn2Val;
      //add delay between checks
      vTaskDelay(15 / portTICK_PERIOD_MS);
      continue;
    }

    if (!btn1Val && btn1LastState && !colorDetector.currentlyReading && !scrolling) {
      if (colorDetector.trainingModeActive) {
        colorDetector.currentlyReading = 1;
        current_menu = LOADING;
        //When we click the button we will store the data into the selected group's file on the SD card
        const String path = String("/") + String(colorDetector.selectedGroupId) + String(".txt");
        //Since we added to the training data, we need to load that new data next time we sample
        colorDetector.loadingRequired = 1;
        //Write color data to SD card in RGBY order
        writeColor(RED_LED, SD);
        writeColor(GREEN_LED, SD);
        writeColor(BLUE_LED, SD);
        writeColor(YELLOW_LED, SD);
        appendFile(SD, path, "\n");

        sampleCounts[colorDetector.selectedGroupId]++;

        // Sleep to let our nice loading animation play for a moment :')
        vTaskDelay(LOADING_DELAY / portTICK_PERIOD_MS);
        current_menu = COLOR_SEL;
        lastLogMsgUpdate = millis();
        logMsg = color2Txt(colorDetector.selectedGroupId) + " Sample Logged!";
      } 
      else if (notEnoughSamples == "") {
        //When we click the button we will compare the sample data to the training data
        current_menu = LOADING;

        sample newSample;
        Serial.printf("Red ");
        newSample.colorValues[0] = readColor(RED_LED);
        Serial.printf("Green ");
        newSample.colorValues[1] = readColor(GREEN_LED);
        Serial.printf("Blue ");
        newSample.colorValues[2] = readColor(BLUE_LED);
        Serial.printf("Yellow ");
        newSample.colorValues[3] = readColor(YELLOW_LED);
        //If we haven't added anything new to the training data, we don't need to read from the SD card again
        if (colorDetector.loadingRequired) {
          //Take the training data from the SD card and put it into the trainingData array
          getDataFromCard();
          colorDetector.loadingRequired = 0;
        }
        //Now we use the classify() function to determine what color newSample is... but how many neighbors should we look at?
        //Typically you can get away with setting k to the square root of the total number of datapoints. In our case k=sqrt(8*12)
        int newSample_groupId = classify(colorDetector.trainingData, 8 * SAMPLES_PER_GROUP, 9, newSample);

        // Sleep to let our nice loading animation play for a moment :')
        vTaskDelay(LOADING_DELAY / portTICK_PERIOD_MS);
        current_menu = newSample_groupId;
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        current_menu = READY;
      }
      colorDetector.currentlyReading = 0;
    }
    btn1LastState = btn1Val;
    btn2LastState = btn2Val;

    //loop delay
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void checkMinNumSamples() {
  String lastColor = "";
  notEnoughSamples = "";
  int notEnoughCount = 0;

  for (int i = 0; i < 8; i++) {
    if (sampleCounts[i] < 12) {
      // If there are already elements in notEnoughSamples, handle commas and spaces
      if (notEnoughCount > 0) {
        if (notEnoughCount == 1) {
          notEnoughSamples += lastColor; // Add the first color without a comma
        } else {
          notEnoughSamples += ", " + lastColor; // Add comma for subsequent colors
        }
      }
      lastColor = color2Txt(i); // Update lastColor to the current color
      notEnoughCount++;
    }
  }

  // Handle final addition with "and"
  if (notEnoughCount > 1) {
     if (notEnoughCount == 2) {
      // No comma for exactly two entries
      notEnoughSamples += " and " + lastColor;
    } else {
      notEnoughSamples += ", and " + lastColor;
    }
  } else if (notEnoughCount == 1) {
    notEnoughSamples = lastColor; // Only one entry
  }
}



void displayReady() {
  if (notEnoughSamples != "") {
    drawTextAtPosition(0, 0, "Low Sample Count!", true, 1.5);
    drawTextAtPosition(0, 16, "The following need\nmore samples:\n\n" + notEnoughSamples, false, 1.5);
  } else {
    drawTextAtPosition(0, 0, "Press the blue button", true, 1.5);
    drawTextAtPosition(0, 8, "to scan.", true, 1.5);
    drawTextAtPosition(14, 26, "Ready!", false, 3.5);
  }
}

void displayLoading() {
  display.drawBitmap(40, 16, loading_frames[loadingMenuFrame], FRAME_WIDTH, FRAME_HEIGHT, 1);
  drawTextAtPosition(0, 0, getProcessingString(loadingMenuFrame), false, 1.5);

  // Update loadingMenuFrame
  loadingMenuFrame = (loadingMenuFrame + 1) % LOADING_FRAME_COUNT;
  delay(FRAME_DELAY);
}

void displayYesNoOpt() {
  //logMsg = "";
  // Draw the Yes and No text
  display.drawBitmap(0, 0, epd_bitmap_YesNoBitmap, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  // Display prompt text
  String prompt = "";
  switch (current_menu) {
    case ENABLE_TRAINING: 
      prompt = "Enable Training Mode?";
      break;
    case DISABLE_TRAINING:
      prompt = "Disable Training\nMode?";
      break;
    case DEL_TRAINING:
      prompt = "Delete All Training\nData?";
      break;
    default:
      prompt = "Unknown Prompt";
      break;
  }
  drawTextAtPosition(0, 0, prompt, false, 1.5);

  // Set the highlight
  YesNoSel = pot_val % ((ANALOG_RES+1)/2) <= ANALOG_RES/4;
  int rectWidth = 36;
  int rectHeight = 23;
  int radius = 10;
  if (YesNoSel) display.drawRoundRect(14, 27, rectWidth, rectHeight, radius, SSD1306_WHITE);
  else display.drawRoundRect(78, 27, rectWidth, rectHeight, radius, SSD1306_WHITE);
}

void displayDetColorText() {
  // Set these to true to disable blinky arrows when we slice the bitmap
  scroll_start = true;
  scroll_end = true;
  // Enable rounded rectangle boarder around text
  scrolling = false;

  int dispNum = (0 <= current_menu && current_menu < 8) ? current_menu : 0;
  // Get the bit map offset for slicing the color text out of the map
  int offset = SCREEN_WIDTH * dispNum;
  // Finally draw the text
  drawSelMenuSlice(offset);
  drawTextAtPosition(0, 0, "Color Detected:", true, 1.5);
}

String color2Txt(int color) {
  switch(color) {
    case black:   return "Black";
    case white:   return "White";
    case red:     return "Red";
    case orange:  return "Orange";
    case yellow:  return "Yellow";
    case green:   return "Green";
    case blue:    return "Blue";
    case purple:  return "Purple";
  }
}

void changeColor(enum colors c) {
  colorDetector.selectedGroupId = c;
}

// Function to draw text at a specified position
void drawTextAtPosition(int x, int y, String txt, bool center = false, float size = 1.5) {
  if (center) x = (SCREEN_WIDTH-charWidth*(size /1.5)*txt.length())/2;
  display.setTextSize(size);
  display.setCursor(x, y);
  display.setTextColor(SSD1306_WHITE);
  display.print(txt);
}

// returns the processing loading text with an appropriate number of dots
String getProcessingString(int num_frames) {
  //TO DO: Change to use current millis() instead of loadingMenuFrame
  String loading_text = "Processing";
  for (int i = 0; i < ((int)(num_frames / 7)) % 4; i++) {
    loading_text += ".";
  }
  return loading_text;
}

int getOffset(int &selectPos) {
  int val = pot_val * SEL_MAP_WIDTH / 4095;
  scroll_start = scroll_end = false;
  scrolling = true;
  if (val > 7 * SCREEN_WIDTH) {
    changeColor(purple);
    if (selectPos < 7 * SCREEN_WIDTH) selectPos = min(selectPos + SEL_SCROLL_SPD, 7 * SCREEN_WIDTH);
    else if (selectPos > 7 * SCREEN_WIDTH) selectPos = max(selectPos - SEL_SCROLL_SPD, 7 * SCREEN_WIDTH);
    if (selectPos == 7 * SCREEN_WIDTH) {
      time_checkpoint = millis();
      scrolling = false;
    }
    scroll_end = true;
    return selectPos;
  }
  if (val > 6 * SCREEN_WIDTH) {
    changeColor(blue);
    if (selectPos < 6 * SCREEN_WIDTH) selectPos = min(selectPos + SEL_SCROLL_SPD, 6 * SCREEN_WIDTH);
    else if (selectPos > 6 * SCREEN_WIDTH) selectPos = max(selectPos - SEL_SCROLL_SPD, 6 * SCREEN_WIDTH);
    if (selectPos == 6 * SCREEN_WIDTH) {
      time_checkpoint = millis();
      scrolling = false;
    }
    return selectPos;
  }
  if (val > 5 * SCREEN_WIDTH) {
    changeColor(green);
    if (selectPos < 5 * SCREEN_WIDTH) selectPos = min(selectPos + SEL_SCROLL_SPD, 5 * SCREEN_WIDTH);
    else if (selectPos > 5 * SCREEN_WIDTH) selectPos = max(selectPos - SEL_SCROLL_SPD, 5 * SCREEN_WIDTH);
    if (selectPos == 5 * SCREEN_WIDTH) {
      time_checkpoint = millis();
      scrolling = false;
    }
    return selectPos;
  }
  if (val > 4 * SCREEN_WIDTH) {
    changeColor(yellow);
    if (selectPos < 4 * SCREEN_WIDTH) selectPos = min(selectPos + SEL_SCROLL_SPD, 4 * SCREEN_WIDTH);
    else if (selectPos > 4 * SCREEN_WIDTH) selectPos = max(selectPos - SEL_SCROLL_SPD, 4 * SCREEN_WIDTH);
    if (selectPos == 4 * SCREEN_WIDTH) {
      time_checkpoint = millis();
      scrolling = false;
    }
    return selectPos;
  }
  if (val > 3 * SCREEN_WIDTH) {
    changeColor(orange);
    if (selectPos < 3 * SCREEN_WIDTH) selectPos = min(selectPos + SEL_SCROLL_SPD, 3 * SCREEN_WIDTH);
    else if (selectPos > 3 * SCREEN_WIDTH) selectPos = max(selectPos - SEL_SCROLL_SPD, 3 * SCREEN_WIDTH);
    if (selectPos == 3 * SCREEN_WIDTH) {
      time_checkpoint = millis();
      scrolling = false;
    }
    return selectPos;
  }
  if (val > 2 * SCREEN_WIDTH) {
    changeColor(red);
    if (selectPos < 2 * SCREEN_WIDTH) selectPos = min(selectPos + SEL_SCROLL_SPD, 2 * SCREEN_WIDTH);
    else if (selectPos > 2 * SCREEN_WIDTH) selectPos = max(selectPos - SEL_SCROLL_SPD, 2 * SCREEN_WIDTH);
    if (selectPos == 2 * SCREEN_WIDTH) {
      time_checkpoint = millis();
      scrolling = false;
    }
    return selectPos;
  }
  if (val > SCREEN_WIDTH) {
    changeColor(white);
    if (selectPos < SCREEN_WIDTH) selectPos = min(selectPos + SEL_SCROLL_SPD, SCREEN_WIDTH);
    else if (selectPos > SCREEN_WIDTH) selectPos = max(selectPos - SEL_SCROLL_SPD, SCREEN_WIDTH);
    if (selectPos == SCREEN_WIDTH) {
      time_checkpoint = millis();
      scrolling = false;
    }
    return selectPos;
  } else {
    changeColor(black);
    if (selectPos < 0) selectPos = min(selectPos + SEL_SCROLL_SPD, 0);
    else if (selectPos > 0) selectPos = max(selectPos - SEL_SCROLL_SPD, 0);
    if (selectPos == 0) {
      time_checkpoint = millis();
      scrolling = false;
    }
    scroll_start = true;
    return selectPos;
  }
}

void drawSelMenuSlice(int xOffset) {
  // Ensure xOffset is within bounds of the larger bitmap
  xOffset = min(max(xOffset, 0), SEL_MAP_WIDTH - SCREEN_WIDTH);

  // Buffer for the sliced 128x48 image
  uint8_t slicedBitmap[SCREEN_WIDTH * SCREEN_HEIGHT / 8];

  // Copy the relevant section from the larger bitmap to the sliced bitmap
  for (int y = 0; y < SCREEN_HEIGHT; y++) {
    int sourceIndex = y * (SCREEN_WIDTH) + (xOffset / 8);  // Start byte of the row in the larger bitmap
    int targetIndex = y * (SCREEN_WIDTH / 8);          // Start byte of the row in the sliced bitmap

    // Copy exactly 16 bytes (128 pixels) for each row
    memcpy(&slicedBitmap[targetIndex], &epd_bitmap_color_sel_screen_bitmap[sourceIndex], SCREEN_WIDTH / 8);
  }

  // Draw the sliced bitmap on the display
  display.drawBitmap(0, 0, slicedBitmap, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);

  if (!scrolling) {
    // Define dimensions for the rectangle loadingMenuFrame
    int rectWidth = 94;   // Width of the rectangle
    int rectHeight = 40;  // Height of the rectangle
    int radius = 10;

    // Calculate the top-left corner to center the rectangle
    int x = (SCREEN_WIDTH - rectWidth) / 2;
    int y = (SCREEN_HEIGHT - rectHeight + 9) / 2;

    // Draw the rounded rectangle
    display.drawRoundRect(x, y, rectWidth, rectHeight, radius, SSD1306_WHITE);

    if (time_checkpoint % 1500 > 700) {
      int arrowSize = 6;                    // Size of the arrow head
      int yCenter = SCREEN_HEIGHT / 2 + 4;  // Center of the screen vertically

      if (!scroll_start) {
        // Draw < symbol on the left side
        display.drawLine(0, yCenter, 0 + arrowSize, yCenter - arrowSize, SSD1306_WHITE);  // Left top line
        display.drawLine(0, yCenter, 0 + arrowSize, yCenter + arrowSize, SSD1306_WHITE);  // Left bottom line
      }

      if (!scroll_end) {
        // Draw > symbol on the right side
        display.drawLine(SCREEN_WIDTH - 1, yCenter, SCREEN_WIDTH - 1 - arrowSize, yCenter - arrowSize, SSD1306_WHITE);  // Right top line
        display.drawLine(SCREEN_WIDTH - 1, yCenter, SCREEN_WIDTH - 1 - arrowSize, yCenter + arrowSize, SSD1306_WHITE);  // Right bottom line
      }
    }
  }
}

void insertionSort(sample arr[], int n) {
  // Copied and modified from GeeksForGeeks.com
  int i, key, j;
  for (i = 1; i < n; i++) {
    key = arr[i].distance;
    j = i - 1;

    while (j >= 0 && arr[j].distance > key) {
      sample temp = arr[j + 1];
      arr[j + 1] = arr[j];
      arr[j] = temp;
      j = j - 1;
    }
    arr[j + 1].distance = key;
  }
}

// Gets the biggest difference in color value
// Was found to work the best in my use case but suming all the differences in RGBY values
// will likely work better in most cases
unsigned int maxColorValueDistance(const sample &s1, const sample &s2) {
  unsigned int maxDifference = 0;
  for (int i = 0; i < 4; ++i) { // Assuming there are 4 color values
    unsigned int diff = std::abs(static_cast<int>(s1.colorValues[i]) - static_cast<int>(s2.colorValues[i]));
    if (diff > maxDifference) {
      maxDifference = diff;
    }
  }
  return maxDifference;
}


/*
The classify() function utilizes the KNN algorithm
k is the number of nearby neighbors
*/
int classify(sample arr[], int numTrainingSamples, int k, const sample node) {
  Group g0{ 0 };
  Group g1{ 1 };
  Group g2{ 2 };
  Group g3{ 3 };
  Group g4{ 4 };
  Group g5{ 5 };
  Group g6{ 6 };
  Group g7{ 7 };
  Group allGroups[8] = { g0, g1, g2, g3, g4, g5, g6, g7 };

  //For each neighbor of the sample (node) we use the distance formula and set it's "distance" member variable
  for (int a = 0; a < numTrainingSamples; a++) {
    arr[a].distance = maxColorValueDistance(node, arr[a]);//colorValueDistance(node, arr[a]);
  }
  //Then we sort the array of training data based on the what's closest to the sample node
  insertionSort(arr, numTrainingSamples - 1);
  //Now we count how many times each group is in the nearest k number of neighbors
  for (int a = 0; a < k; a++) {
    Serial.printf("Sample: %s. Red: %d, Green: %d, Blue: %d, Yellow: %d. Distance: %d\n",  color2Txt(arr[a].groupValue), arr[a].colorValues[0], arr[a].colorValues[1], arr[a].colorValues[2], arr[a].colorValues[3], arr[a].distance);
    switch (arr[a].groupValue) {
      case 0:
        allGroups[0].frequency++;
        break;
      case 1:
        allGroups[1].frequency++;
        break;
      case 2:
        allGroups[2].frequency++;
        break;
      case 3:
        allGroups[3].frequency++;
        break;
      case 4:
        allGroups[4].frequency++;
        break;
      case 5:
        allGroups[5].frequency++;
        break;
      case 6:
        allGroups[6].frequency++;
        break;
      case 7:
        allGroups[7].frequency++;
        break;
    }
  }

  //Finally, we look at the most common group in the neighboring samples
  int highestFrequency = 0;
  int groupNumber;
  for (int a = 0; a < 8; a++)  //8 because there are 8 groups
  {
    if (allGroups[a].frequency >= highestFrequency) {
      highestFrequency = allGroups[a].frequency;
      groupNumber = allGroups[a].id;
    }
  }

  return groupNumber;
}

void getDataFromCard() {
  //Read each file on the SD card line by line, storing the data in each sample in the "trainingData" array (arr)
  File myFile;
  String value;  //Number we read from each file
  int row = 0;
  int nodeCount = 0;
  int counter = 0;
  for (int groupNumber = 0; groupNumber < 8; groupNumber++) {
    const String path = String("/") + String(groupNumber) + String(".txt");
    myFile = SD.open(path, FILE_READ);
    while (1) {
      value = myFile.readStringUntil('.');
      
      // a value of 4095 means the sensor is being maxed out
      //Serial.println(value);
      if (value == NULL || value == (String)'\n' || counter == SAMPLES_PER_GROUP) {
        sampleCounts[groupNumber] = counter;
        row = 0;
        counter = 0;
        break;
      }
      //Store the value from the file into the sample
      colorDetector.trainingData[nodeCount].colorValues[row] = value.toInt();
      colorDetector.trainingData[nodeCount].groupValue = groupNumber;
      
      //a value of 255 means the sensor is being maxed out
      row++;
      if (row == 4) {
        row = 0;
        nodeCount++;
        counter++;
      }
    }
    myFile.close();
  }
}

int getNumDigits(int num) {
  //Returns the number of digits in an integer
  return (int)log10((double)num) + 1;
}

void writeColor(int pin, fs::FS &SD) {
  const String path = String("/") + String(colorDetector.selectedGroupId) + String(".txt");
  //Turns on an LED at the specified pin, reads the value on the transistor, and stores it in 4-char format on the SD card
  digitalWrite(pin, HIGH);
  delay(100);
  int reading = analogRead(PHOTOTRANSISTOR_PIN);
  Serial.printf("saving val: %s\n", String(reading));
  int numDigits = getNumDigits(reading);
  switch (numDigits) {
    case 0:
      appendFile(SD, path, String("0000"));
      break;
    case 1:
      appendFile(SD, path, String("000") + String(reading));
      break;
    case 2:
      appendFile(SD, path, String("00") + String(reading));
      break;
    case 3:
      appendFile(SD, path, String("0") + String(reading));
      break;
    default:
      appendFile(SD, path, String(reading));
  }
  appendFile(SD, path, ".");
  digitalWrite(pin, LOW);
  delay(100);
}

int readColor(int pin) {
  //Very similar to writeColor() but we don't write to the SD card
  digitalWrite(pin, HIGH);
  delay(100);
  int reading = analogRead(PHOTOTRANSISTOR_PIN);
  Serial.printf("read Value: %d\n", reading);
  digitalWrite(pin, LOW);
  delay(100);
  return reading;
}

void listDir(fs::FS &fs, const char *dirname, uint8_t levels) {
  Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
    Serial.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels) {
        listDir(fs, file.path(), levels - 1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}

void readFile(fs::FS &fs, const char *path) {
  Serial.printf("Reading file: %s\n", path);

  File file = fs.open(path);
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }
    Serial.print("Read from file: ");
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
}

// Adapted from SD.h example
void writeFile(fs::FS &fs, const char *path, const char *message) {
  Serial.printf("Writing file: %s\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
  file.close();
}

// Adapted from SD.h example
void appendFile(fs::FS &fs, const String &path, const String &message) {
  //Serial.printf("Appending to file: %s\n", path);
  //Serial.printf("saving val: %s\n", message);
  File file = fs.open(path, FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open file for appending");
    return;
  }
  if (file.print(message)) {
    Serial.println("Message appended");
  } else {
    Serial.println("Append failed");
  }
  file.close();
}

// Adapted from SD.h example
void deleteFile(fs::FS &fs, const String &path) {
  Serial.printf("Deleting file: %s\n", path);
  if (fs.remove(path)) {
    Serial.println("File deleted");
  } else {
    Serial.println("Delete failed");
  }
}

void deleteTraining(fs::FS &fs) {
  for (int i = 0; i < 8; i++) {
    const String path = String("/") + String(i) + (".txt");
    deleteFile(fs, path);
  }
  // Reset sampleCounts to 0 for each entry
  for (int i = 0; i < 8; ++i) {
    sampleCounts[i] = 0;
  }
  logMsg = "Data Deleted!";
  lastLogMsgUpdate = millis();
  checkMinNumSamples();
}