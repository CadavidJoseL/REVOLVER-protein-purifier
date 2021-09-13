/*
Arduino code for running the sample collection from a chromatography process with a single collector.
Instructions set via serial connection and sent to distributors
----------------
Master device
*/

// V1 - I2C scanner based on code from https://playground.arduino.cc/Main/I2cScanner/

#include <Wire.h>
#include <Stepper.h>

// Define pins - distributor only has a stepper motor and pumps
const byte pump1 = 6; // Pump#1
const byte pump2 = 5; // Pump #2
const byte motPins[] = {9, 10, 11, 12}; // Pins for stepper motor

/* Constant properties of the hardware */
#define STEPS  32   // Number of steps per revolution of internal shaft of a 28byj-48 stepper
const int steps2take = 2048; // number of steps of motor - 1 revolution (motor is geared)

// Variables for parsing serial communication
const byte numChars = 32; // this value could be smaller or larger, as long as it fits the largest expected message
char receivedChars[numChars];
char tempChars[numChars];        // temporary array for use when parsing
// variables to hold the parsed data
char messageFromPC[numChars] = {0};
int ledInstruction = 0;
boolean newData = false;

// Variables for I2C scanner and I2C communication
byte error, address; // Variables for I2C scanner
int nI2C; // number of connected devices via I2C
const byte nI2CMax = 10; // Maximum number of I2C devices we will allow to be connected
const byte nTaskMax = 20; // Maximum number of tasks to be stored - TODO: This could be tuned
const byte nArgs = 3; // Maximum number of arguments to be used in functions
byte listI2C[nI2CMax]; // array that holds the addresses of the I2C devices connected to the bus
int locationsI2C[nI2CMax]; // array that contains the locations (in n steps) of the connected I2C devices - TO DO merge into a single 2D array
int angularPos = 0; // angular position of the motor when we start...arbitrary location
byte taskIdx[nI2CMax]; // array for storing the task being currently executed by each revolver
char taskName[nTaskMax]; // array for storing the tasks to be executed by each revolver (e.g. "H", "P", "F")
unsigned int taskArgs[nTaskMax][nArgs]; // array for storing arguments (up to three) for each of the tasks
boolean taskStored = false; // variable indicating whether the full protocol has been parsed and stored
byte nTask = 0; // Number of tasks received. Works as a index. Byte should allow for 255 instructions which is plenty
byte messageFromRevolver = 0; // Temporary storage for message from revolvers
boolean allDone; // variable that stores whether all devices are finished. We can use this to allow more instructions without reseting the devices
byte requestedAddress; // variable to store an I2C address if there is a manual request (e.g. rotate a plate)


// Define objects
// Setup of proper sequencing for Motor Driver Pins: In1, In2, In3, In4 in the sequence 1-3-2-4
Stepper mainStepper(STEPS, motPins[0], motPins[2], motPins[1], motPins[3]);

/* Run this program once*/
void setup() {

  // ========== Setup pins and monitor ========================================
  // Start serial monitor
  Serial.begin(9600);
  // Join I2C bus as master (no address)
  Wire.begin();
  //Serial.println("Serial collection of chromatography fractions");

  // Define pin modes
  pinMode(pump1, OUTPUT);
  pinMode(pump2, OUTPUT);

  // Turn off motor coils
  for (int i; i < 4; i++){digitalWrite(motPins[i], LOW);}
  mainStepper.setSpeed(700);

  // Step 1: Only execute once - find number and addresses of connected I2C devices
  scanI2C();

  // Step 2: Rotate to get the location of the devices
  //locateI2C();
  Serial.print("ready!");
}

/* Loop function - listen to serial monitor */
void loop() {

  // Listen to serial monitor for instructions as long as they haven't been all stored
  // Note - maybe this should get its own loop and go to setup
  if (!taskStored){
    // Monitor the serial port - the loop will scan the characters as they come, even if the full
    // message doesn't arrive during a single iteration of the loop
    recvWithStartEndMarkers();
      if (newData == true) {
          newData = false;
          // This temporary copy is necessary to protect the original data
          // because strtok() used in parseData() replaces the commas with \0
          strcpy(tempChars, receivedChars);
          // Parse the message: Split into its components and store for I2C
          parseCommand();
          // Execute the data: Called the necessary function - USE this for now to pass instructions independently to each revolver
          // for rotating
          executeCommand();
          // Announce to port that we are done so that next instruction can come in
          //Serial.println("done");
      }

  }

  // Communicate with revolvers via I2C
  else {

    // Reset boolean to check if all are done (any false will make it false, so it starts as true)
    allDone = true;
    // Loop through all revolvers, read their status and pass instructions if needed
    for (int idx = 0; idx < nI2C; idx++){
      //delay(3000); // temporary delay for debugging - it seems that if we call I2C while the stepper is running, it interrupts
      // Request status update of revolver
      Wire.requestFrom(listI2C[idx], 1); // request 1 byte
      // Read message - for now we only indicate whether the step is done, but
      // this message can be a request for a pump
      messageFromRevolver = Wire.read();
      // Check if all devices have finished (by checking if each device has finished and is currently free)
      allDone = allDone && (taskIdx[idx] == nTask && messageFromRevolver == 1);

      // Parse message - TODO:Complete cases and maybe wrap in function called parseI2C
      if (messageFromRevolver == 1 && taskIdx[idx] < nTask){
        // Task is finished, so move on to next task for this revolver (as long as there is another task)
        // by default all revolvers start as "done" and wait for the first instruction,
        // which is why we update the index after passing the instruction
        Wire.beginTransmission(listI2C[idx]);
        // Write the name of the command to be executed
        Wire.write(taskName[taskIdx[idx]]);
        // Write the arguments
        for (int i = 0; i < nArgs; i++){
          Wire.write(taskArgs[taskIdx[idx]][i]);
        }
        // End transmission
        Wire.endTransmission();
        // Update task idx for next iteration
        taskIdx[idx]++;
      }

    }

    // If all devices are done, reset and start listenting for more
    if (allDone){
      Serial.println("All devices finished! Input more");
      delay(1000);
      // Reset counters
      taskStored = false;
      nTask = 0;
      for (int i = 0; i < nI2CMax; i++){
        taskIdx[i] = 0;
      }
    }

  }


} // end of loop

// ====================================================
// Functions for serial communication

void recvWithStartEndMarkers() {
    static boolean recvInProgress = false;
    static byte idx = 0;
    char startMarker = '<';
    char endMarker = '>';
    char rc;

    while (Serial.available() > 0 && newData == false) {
        rc = Serial.read();

        if (recvInProgress == true) {
            if (rc != endMarker) {
                receivedChars[idx] = rc;
                idx++;
                if (idx >= numChars) { // this if clause makes sure we don't exceed the max size of message, and starts overwriting the last character
                    idx = numChars - 1;
                }
            }
            else {
                receivedChars[idx] = '\0'; // terminate the string
                recvInProgress = false;
                idx = 0;
                newData = true;
            }
        }

        else if (rc == startMarker) {
            recvInProgress = true;
        }
    }
}

//============

void parseCommand() {      // split the command into its parts
    char * strtokIndx; // this is used by strtok() as an index

    strtokIndx = strtok(tempChars,",");      // get the first part
    strcpy(messageFromPC, strtokIndx); // copy it to ledAddress as a string
    //Serial.println(messageFromPC);

    // If the firt letter of the message is X, we don't store the instruction and
    // stop listening to serial (the stop instruction is <X>)

    if (messageFromPC[0] != 'X'){
      // Scan and store the other arguments - Assuming all are integers
      taskName[nTask] = messageFromPC[0];

      // If the task is to rotate, we need to handle it a bit better becase
      // the number of steps requested might be larger than 255 and won't fit in a single byte for I2C.
      // To solve this, we don't pass the number of steps as the first argument, but we pass mod(n,255)
      // and as a third argument we pass the whole part of n/255. That way we tell the slaves how many time to
      // rotate 255 steps, plus a bit more
      if (messageFromPC[0] == 'R'){
        // This task accepts a fourth argument that is an I2C address, and it's not executed as part of the protocol
        // since it's used for manual rotation (this could be changed with another argument)

        strtokIndx = strtok(NULL, ",");
        unsigned int nSteps = atoi(strtokIndx);
        taskArgs[nTask][0] = nSteps % 255;
        taskArgs[nTask][2] = nSteps/255;
        // Get direction
        strtokIndx = strtok(NULL, ",");
        taskArgs[nTask][1] = atoi(strtokIndx);
        // Get I2C device to address
        strtokIndx = strtok(NULL, ",");
        requestedAddress = atoi(strtokIndx);
      }
      else {
        for (int i = 0; i < nArgs; i++){
          strtokIndx = strtok(NULL, ","); // this continues where the previous call left off
          taskArgs[nTask][i] = atoi(strtokIndx);     // convert this part to an integer
        }
        // Increase task counter
        nTask++;
      }

    }
    else {
      taskStored = true;
      Serial.println("done receiving");
    }
}

// Function for executing commands. Note: We only include commands directly executed
// by the distributor, since everything else is passed via I2C

void executeCommand(){ // TO DO - clean execute function and parse commands
  char firstLetter = messageFromPC[0];
  switch (firstLetter){
    case 'R': // Rotate manually
      if (requestedAddress == 0){ 
        // Rotate distributor with the arguments parsed - convert back to total steps as done 
        // by the distributors
        rotatePlate(taskArgs[nTask][0] + 255*taskArgs[nTask][2], taskArgs[nTask][1]);
      }
      else {
        // Pass arguments to slave via I2C
        Wire.beginTransmission(requestedAddress);
        // Write the name of the command to be executed
        Wire.write('R');
        // Write the arguments
        for (int i = 0; i < nArgs; i++){
          Wire.write(taskArgs[nTask][i]);
        }
        // End transmission
        Wire.endTransmission();
      }
      break;
    case 'S': // Store location of I2C device
      // THIS CHUNK NEEDS WORK to store the angularPos variable in the locationsI2C array
      Serial.print("Location: ");
      Serial.println(angularPos);
      locationsI2C[0] = angularPos; // TO DO: Store the location for the corresponding device
      break;
    case 'V': // Visit a device - TO DO: Make function for this
      // The distributor should call it itself before a pump function
      // it should take as arguments an I2C address for which a location has been stored
      // We might take the longest path, but we want the distriutor to never complete more than a full rotation

      // If both the current position and the stored position are in the same half of the rotation, we rotate normally. 
      // Else, we rotate the other direction to avoid getting tangled in the center
      // TO DO create a local variable called targetPos for readability
      int targetPos = locationsI2C[0];

      
      // Check if both start and end position are on the same half of the rotation
      if ((targetPos>steps2take/2 && angularPos>steps2take/2) || (targetPos<steps2take/2 && angularPos<steps2take/2)){
        // Starting and end position in the same half of the rotation
        mainStepper.step(targetPos - angularPos);
      }
      else{
        // Go all the way around to not cross the mid point (to avoid tangles)
        // Direction will depend on position of start and end point
        if (angularPos > targetPos){
          // Go counter clockwise
          mainStepper.step(targetPos - angularPos + steps2take);
        }
        else {
          // Go clockwise
          mainStepper.step(targetPos - angularPos - steps2take);
        }
      }
      // Update position
      angularPos = targetPos;
      break;
  }
}

// ====================================================
// Functions for I2C

// Scan I2C bus to see what's connected and what addresses they have
void scanI2C() {
  Serial.println("Scanning...");
  nI2C = 0;
  for(address = 1; address < 127; address++ )
  {
    // The i2c_scanner uses the return value of
    // the Write.endTransmisstion to see if
    // a device did acknowledge to the address.
    Wire.beginTransmission(address);
    error = Wire.endTransmission(); // see https://www.arduino.cc/en/Reference/WireEndTransmission

    if (error == 0) // success
    {
      Serial.print("I2C device found at address: ");
      Serial.println(address);
      // Store address
      listI2C[nI2C] = address;
      // Increase count
      nI2C++;
    }
   /* else if (error==4) // other error
    {
      Serial.print("Unknown error at address: ");
      Serial.println(address);
    }    */

    // Make sure we don't exceed the number of devices we can store
    if (nI2C > nI2CMax) {nI2C = nI2CMax;}
  }
  Serial.print("Found ");
  Serial.print(nI2C);
  Serial.println(" devices");

}

// Rotate stepper motor to get locations of I2C devices
/* if the devices are in pre-defined locations (fixed angles) we can
 *  home the distributor and get it to only check some pre-determined positions
 */


void locateI2C(){

  Serial.println("Finding locations of devices...");

  int docked = 1; // not docked by default

  // Rotate the stepper a bit at a time and scan the I2C addresses to see if we docked a device
  while (angularPos < steps2take){ // complete a single rotation
    mainStepper.step(16); // arbitrary rotation, but should be small - can change
    angularPos = angularPos + 16;

    // Loop for the I2C devices
    for (int idx = 0; idx < nI2C; idx++){
      // Ask each device if their sensor has been triggered (i.e. docking happened)
      Wire.requestFrom(listI2C[idx], 1); // request 1 byte
      //Serial.print("Status for sensor ");
      //Serial.print(listI2C[idx]);
      //Serial.print(" is: ");
      while (Wire.available()){
        docked = Wire.read();
        //Serial.println(docked);
      }
      //Serial.println(docked);
      if (docked == 0){

        // Hall effect sensor triggered - store the initial position where the sensor was triggered
        locationsI2C[idx] = angularPos;
        // Advance more until the sensor is reset, indicating we passed the docking position
        while (docked == 0){
          // Advance a bit
          mainStepper.step(4);
          angularPos = angularPos + 4;
          // Interrogate slave
          Wire.requestFrom(listI2C[idx], 1); // request 1 byte
          docked = Wire.read();
        }
        // Sensor was reset. The location of the I2C device is the average of the initial
        // position and the current position
        locationsI2C[idx] = round((locationsI2C[idx] + angularPos)/2);
        Serial.print("Location of I2C #");
        Serial.print(listI2C[idx]);
        Serial.print(" found at n = ");
        Serial.println(locationsI2C[idx]);

      }
    }
  }


}

// ==============================================
// Functions for running the protocol

void rotatePlate(int steps, int dir){ // mainStepper has a different name from plateStepper in the revolver. Unify functions?
  // d must be either 0 or 1.
  dir = dir*2 - 1; // this converts it to -1 or 1
  mainStepper.step(dir*steps);
  // Update position - calculate the mod to make sure abs(position) < steps2take
  angularPos = angularPos + dir*steps;
  angularPos = angularPos % steps2take;
  // If position is negative, add steps2take to make it positive
  if (angularPos < 0 ){
    angularPos = angularPos + steps2take;
  }
  Serial.println(angularPos);
}

// // Loop through the connected I2C devices and visit them
// for (int idx = 0; idx < nI2C; idx++){
//   Wire.requestFrom(listI2C[idx], 1); // request 1 byte
//   while (Wire.available()){
//     int c = Wire.read();
//     if (c==0){
//       digitalWrite(LED, HIGH);
//       stepper.step(locationsI2C[idx] - angularPos);
//       angularPos = locationsI2C[idx];
//       delay(2000);
//       stepper.step(-angularPos);
//       angularPos = 0;
//       }
//     else {digitalWrite(LED, LOW);}
//   }
//   delay(200);
// }