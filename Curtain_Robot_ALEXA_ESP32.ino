/* DesignBuildDestroy.com - Curtain Robot 0.9
   Written by DesignBuildDestroy (DBD) 2021
   
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
   
   Required Libraries:
    *  FauxmoESP: https://github.com/vintlabs/fauxmoESP
    *  AsyncTCP: https://github.com/me-no-dev/AsyncTCP
    
    Fauxmo Requires AsyncTCP to be installed for it's own library use so you will not see it included below but it is required
    NOTE: AsyncTCP is for ESP32 if you are using ESP2866 use ESPAsyncTCP instead: https://github.com/me-no-dev/ESPAsyncTCP
*/

//**Alexa Definitions
#include <Arduino.h>
#include <WiFi.h>
#include "fauxmoESP.h"

#define WIFI_SSID "YOUR_SSID"
#define WIFI_PASS "YOUR_WIFI_PASSWORD"
#define CURTAIN "curtain"   //This is the name to call to Alexa

fauxmoESP fauxmo;

bool alexaTrigger = false;
byte alexaDir = 0; // Open assume open on first power up.
byte alexaAmount = 100; //full open/close
//**End Alexa Defs

// Encoder
const byte encoderA = 25;
const byte encoderB = 26;
const byte encoderBTN = 27;

//Program mode - FUTURE IMPLEMENTATION
//const byte progModeLED = 8;      // FUTURE IMPLEMENTATION
//const byte progModeTimeout = 3000; // FUTURE IMPLEMENTATION
//const byte EEPROMreserve = 4; // Reserve space for stored setting 2 bytes for encoder count, 2 for last pos FUTURE IMPLEMENTATION

//Other Vars
int currentPositionCount = 0;
int encoderState;
int lastEncoderState;

//Stepper vars
const byte stepRelay = 18;
const byte stepDirection = 19;
const byte stepActionPin = 21;
const byte stepEnable = 22; //This is actually REST & SLEEP
const byte stepsPerRevolution = 200;
bool stepperEngaged = false;

String encdir = "";


void setup() {
  // Set up Pins
  pinMode (encoderA, INPUT_PULLUP);
  pinMode (encoderB, INPUT_PULLUP);
  pinMode (encoderBTN, INPUT_PULLUP);

  //Debug LED
  pinMode(2, OUTPUT);
  //Stepper Pins
  pinMode (stepRelay, OUTPUT);
  pinMode(stepActionPin, OUTPUT);
  pinMode(stepDirection, OUTPUT);
  pinMode(stepEnable, OUTPUT);

  //Fully disable stepper by cutting lines via relay
  // and put driver into Sleep mode
  digitalWrite(stepRelay, LOW);
  digitalWrite(stepEnable, HIGH);

  // Setup Serial Monitor
  Serial.begin (115200);

  // Get current Encoder state on startup
  lastEncoderState = digitalRead(encoderA);


  // ALEXA SETUP from FauxMo example
  wifiSetup();  //Join WIFI
  // The TCP port must be 80 for gen3 devices (default is 1901)
  // This has to be done before the call to enable()
  fauxmo.createServer(true); // not needed, this is the default value
  fauxmo.setPort(80); // This is required for gen3 devices
  fauxmo.enable(true);
  
  // Add virtual device for Alexa
  fauxmo.addDevice(CURTAIN);

  // Callback when a command from Alexa is received
  // You can use device_id or device_name to choose the element to perform an action onto (relay, LED,...)
  // State is a boolean (ON/OFF) and value a number from 0 to 255 (if you say "set kitchen light to 50%" you will receive a 128 here).
  // Just remember not to delay too much here, this is a callback, exit as soon as possible.
  // If you have to do something more involved here set a flag and process it in your main loop.
  fauxmo.onSetState([](unsigned char device_id, const char * device_name, bool state, unsigned char value) {
    Serial.printf("[MAIN] Device #%d (%s) state: %s value: %d\n", device_id, device_name, state ? "ON" : "OFF", value);
    if ( (strcmp(device_name, CURTAIN) == 0) ) {
      // this just sets a variable that the main loop() does something about
      //Serial.println("ALEXA TRIGGERED");  //debug
      alexaTrigger = true;
      if (state) {
        //Serial.println("Curtain Close");  //debug
        alexaDir = 1;
        alexaAmount = 100;
      } else {
        //Serial.println("Curtain Open"); //debug
        alexaDir = 0;
        alexaAmount = 100;
      }
    }
  });

}

void wifiSetup() {
  // Set WIFI module to STA mode
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);

  // Connect
  Serial.printf("[WIFI] Connecting to %s ", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  // Wait
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(100);
  }
  Serial.println();

  // Connected!
  Serial.printf("[WIFI] STATION Mode, SSID: %s, IP address: %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}


// Move the motor in the right direction checking for binding (assume binding is hitting the end stop)
// In this version we are not using POS position amount, this was intended for Alexa to be able to
// say something like "Alexa, Open Curtain 50%" but not implemented yet
void spinMotor(int stepDir, byte pos)
{
  // Serial.println("MOTOR START");  //Debug
  stepperEngaged = true;

  // Enable driver and relay
  // Relay makes the physical motor winding connections to driver board
  digitalWrite(stepRelay, HIGH);
  digitalWrite(stepEnable, HIGH);
  digitalWrite(2, HIGH);  // Light up Blue onboard LED for Active Status
  delay(100);

  // Set the spinning direction on stepper driver
  digitalWrite(stepDirection, stepDir);

  // debug
  // Serial.print("Direction: ");
  // Serial.println(encdir);

  //Grab timestamp to use for checking when we bind up (hit end stop or bind up)
  unsigned long encoderCount = millis();

  bool notRotating = false;

  //Start moving while checking for binding to know when to stop
  while (notRotating == false) {
    byte rollencoderState = digitalRead(encoderA); //Get current Encoder state

    // These four lines result in 1/8th step - the actual motor movement
    digitalWrite(stepActionPin, HIGH);
    delayMicroseconds(300);
    digitalWrite(stepActionPin, LOW);
    delayMicroseconds(300);

    if (digitalRead(encoderA) != rollencoderState) {
      currentPositionCount++;
      encoderCount = millis(); //Reset timer
      //digitalWrite(2, digitalRead(encoderA));
    }
    else {
      if (millis() - encoderCount > 50) {
        //We may have gotten stuck or reached the end point, stop trying and disable motor
        Serial.println("JAM!");
        notRotating = true;

      }
    }
  } //end while move loop

  //Disable stepper driver and switch off Relay for motor windings for protection
  digitalWrite(stepRelay, LOW);
  digitalWrite(stepEnable, LOW);
  digitalWrite(2, LOW);
  stepperEngaged = false;

  // Let the physical device settle after motor disabled
  // spring arms may shift the device slightly when motor
  // force is dsabled possibly triggering the encoder again
  // so wait for it to settle out then read encoder to reset
  delay(1000);
  encoderState = digitalRead(encoderA);
  Serial.println("MOTOR DONE");
}

int encVerify = 0;
unsigned long encTimeStamp = 0;
bool encEngaged = false;

void loop() {

  // Check for Alexa Command, if triggered call to move device
  // NOTE: This version alexaAmout is ignored in spinMotor not yet implemented
  fauxmo.handle();
  if (alexaTrigger) {
    alexaTrigger = false; //Reset the trigger
    spinMotor(alexaDir, alexaAmount);
  }

  // Check if Button Pressed
  if (!digitalRead(encoderBTN)) {
    // Button Pressed just turn on the LED for debugging right now
    // Building something with this button later. This could be used as a setup on long press
    // to allow user to set start and end point manually, counting pulses from encoder
    // and SET them in with a short press so Alexa command can do 50% or whatever open/close
    // and know how many steps it should move to reach that, current hard binding open/close
    // Detection does not keep track of where the device is to be able to do partial open/close.
    digitalWrite(2, HIGH);
    delay(300);
    digitalWrite(2, LOW);
  }

  // Check if Encoder changed if so we have movement by a person
  // Check a few pulses to confirm the direction and intention - cancel out wind shaking the curtain and device
  encoderState = digitalRead(encoderA);
  if (encoderState != lastEncoderState) {
    // If we are already counting do not update timestamp
    if (!encEngaged) {
      encEngaged = true; //We are now counting to verify intention
      encTimeStamp = millis(); //Grab current timestamp
    }

    // Figure out Encoder Rotation by comparing pin states
    // we compile a few fast counts to make sure movement is intentional
    // and not just the wind blowing the curtain or a wiggle back and forth
    if (digitalRead(encoderB) != encoderState) {
      //CW - User is pulling to OPEN the curtain
      encVerify += 1;
      encdir = "CW";
      //spinMotor(0, 1);
    } else {
      //CCW - User is pulling to CLOSE the curtain
      encVerify -= 1;
      encdir = "CCW";
      //spinMotor(1, 0);
    }
  }

  //Check we met minimum movement by user and engage motor takeover
  if (encEngaged && encVerify >= 5) {
    //Open the curtain
    spinMotor(0, 1);
  }
  if (encEngaged && encVerify <= -5) {
    //close the curtain
    spinMotor(1, 1);
  }

  // Check if time has elapsed to reset encoder counter 1.5s window
  // This is basically an escape if wind or child shook the curtain that may
  // have triggered it to start counting pulses thinking a user might be pulling the curtain
  // but then stops, reset the counters as a "false alarm" situation
  if (encEngaged && (millis() - encTimeStamp >= 2000)) {
    //Reset the count and wait again
    encVerify = 0;
    encEngaged = false;
    Serial.println("ENC TIMEOUT");  //Debug
  }

  // Update last state with current
  lastEncoderState = encoderState;
}
