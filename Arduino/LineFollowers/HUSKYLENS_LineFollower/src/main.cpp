/***************************************************
 HUSKYLENS An Easy-to-use AI Machine Vision Sensor
 <https://www.dfrobot.com/product-1922.html>

 ***************************************************
 This example shows the basic function of library for HUSKYLENS via Serial.

 Created 2020-03-13
 By [Angelo qiao](Angelo.qiao@dfrobot.com)

 GNU Lesser General Public License.
 See <http://www.gnu.org/licenses/> for details.
 All above must be included in any redistribution
 ****************************************************/

/***********Notice and Trouble shooting***************
 1.Connection and Diagram can be found here
 <https://wiki.dfrobot.com/HUSKYLENS_V1.0_SKU_SEN0305_SEN0336#target_23>
 2.This code is tested on Arduino Uno, Leonardo, Mega boards.
 ****************************************************/
#include <Arduino.h>
#include "HUSKYLENS.h"
#include "SoftwareSerial.h"

HUSKYLENS huskylens;
SoftwareSerial mySerial(10, 11); // RX, TX      Inland RX:10 TX:11
// HUSKYLENS green line >> Pin 10; blue line >> Pin 11

// TODO move these declarations to .hpp file
void printResult(HUSKYLENSResult result);
boolean isHuskylensReady();
void loopLineFollow(HUSKYLENSResult result);

void setup()
{
  Serial.begin(115200);
  mySerial.begin(9600);
  while (!huskylens.begin(mySerial))
  {
    Serial.println(F("Begin failed!"));
    Serial.println(F("1.Please recheck the \"Protocol Type\" in HUSKYLENS (General Settings>>Protocol Type>>Serial 9600)"));
    Serial.println(F("2.Please recheck the connection."));
    delay(1000);
  }

  HUSKYLENSResult result = huskylens.read();
}

bool raceEnded = false;

void loop()
{
  // Serial.println(F("###########"));
  while (isHuskylensReady() && !raceEnded)
  {
    HUSKYLENSResult result = huskylens.read();

    // printResult(result);
    loopLineFollow(result);
    delay(1000);
  }

  Serial.println("Exiting loop()");
  delay(500);
  exit(200);
}

boolean isEndOfRace()
{
  if (!huskylens.available())
  {
    Serial.print(raceEnded);
    Serial.print(" - ");
    Serial.println(F("No more line to follow. End of Race."));
    raceEnded = true;
  }

  return raceEnded;
}

void loopLineFollow(HUSKYLENSResult result)
{
  // if(isEndOfRace()){
  //     // Visual feedback
  //     // Audible feedback
  //     // Stop Motors
  //     exit(3);
  // }

  int oX = result.xOrigin;
  int oY = result.yOrigin;

  int tX = result.xTarget;
  int tY = result.yTarget;
  if ((tY > 100) && (tX > 100) && (tX < 220))
  {
    Serial.print("End of race condition found. tY:");
    Serial.println(tY);
    raceEnded = true;
    delay(250);
    return;
  }

  Serial.println(String() + F("Arrow:xOrigin=") + oX + F(",yOrigin=") + oY + F(",xTarget=") + tY + F(",yTarget=") + tY + F(",ID=") + result.ID);
}

boolean isHuskylensReady()
{
  HUSKYLENSResult result = huskylens.read();
  boolean ready = true;

  if (!huskylens.request())
  {
    Serial.println(F("Fail to request data from HUSKYLENS, recheck the connection!"));
    ready = false;
  }
  else if (!huskylens.isLearned())
  {
    Serial.println(F("Nothing learned, press learn button on HUSKYLENS to learn one!"));
    ready = false;
  }
  else if (!huskylens.available())
  {
    Serial.println(F("No block or arrow appears on the screen!"));
    ready = false;
  }
  else if (result.command == COMMAND_RETURN_BLOCK)
  {
    Serial.println("Wrong mode for line following. Set to 'Line Tracking'");
  }

  return ready;
}

void printResult(HUSKYLENSResult result)
{
  if (result.command == COMMAND_RETURN_BLOCK)
  {
    Serial.println(String() + F("Block:xCenter=") + result.xCenter + F(",yCenter=") + result.yCenter + F(",width=") + result.width + F(",height=") + result.height + F(",ID=") + result.ID);
  }
  else if (result.command == COMMAND_RETURN_ARROW)
  {
    Serial.println(String() + F("Arrow:xOrigin=") + result.xOrigin + F(",yOrigin=") + result.yOrigin + F(",xTarget=") + result.xTarget + F(",yTarget=") + result.yTarget + F(",ID=") + result.ID);
  }
  else
  {
    Serial.println("Object unknown!");
  }
}