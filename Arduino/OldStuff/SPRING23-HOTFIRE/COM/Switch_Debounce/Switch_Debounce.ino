/*
 * This ESP32 code is created by esp32io.com
 *
 * This ESP32 code is released in the public domain
 *
 * For more detail (instruction and wiring diagram), visit https://esp32io.com/tutorials/esp32-button-debounce
 */

#include <ezButton.h>

#define DEBOUNCE_TIME 50 // the debounce time in millisecond, increase this time if it still chatters

ezButton button1(22); // create ezButton object that attach to pin GIOP21;
ezButton button2(23); // create ezButton object that attach to pin GIOP22;
ezButton button3(21); // create ezButton object that attach to pin GIOP23;

void setup() {
  Serial.begin(115200);
  button1.setDebounceTime(DEBOUNCE_TIME); // set debounce time to 50 milliseconds
  button2.setDebounceTime(DEBOUNCE_TIME); // set debounce time to 50 milliseconds
  button3.setDebounceTime(DEBOUNCE_TIME); // set debounce time to 50 milliseconds
}

void loop() {
  Serial.println(button1.getState());
  button1.loop(); // MUST call the loop() function first
  button2.loop(); // MUST call the loop() function first
  button3.loop(); // MUST call the loop() function first

  if (button1.getState())
    Serial.println("The button 1 is pressed");

  if (!button1.getState())
    Serial.println("The button 1 is released");

  if (button2.getState())
    Serial.println("The button 2 is pressed");

  if (!button2.getState())
    Serial.println("The button 2 is released");

  if (button3.getState())
    Serial.println("The button 3 is pressed");

  if (!button3.getState())
    Serial.println("The button 3 is released");
}
