//
// Arduino sketch for controlling PTT 
//
// This sketch receives the mqtt data from 'api_controller.sh' script. 
// 
// The topic followed is:  'tx/active'
//
// When clicking the PTT button in SDRConsole this program will switch the defined PTT_PORT.
//
//
// Initial version.
//
// sketch running @arduino-pro-micro
// 
// Enjoy, Johan, PA3GSB


#define PTT_PORT 9  

String receivedData = "";  

void setup() {
  Serial.begin(115200);
  pinMode(PTT_PORT, OUTPUT);
  digitalWrite(PTT_PORT, LOW);  
}

void loop() {
  while (Serial.available() > 0) {
    char incomingChar = Serial.read();  
    receivedData += incomingChar;  // Add the incoming character to the receivedData string

    // Check if the incoming data contains a complete message (e.g., key-value pair)
    if (incomingChar == '\n' || incomingChar == '\r') { // Check for end of line
      // Trim any trailing whitespace
      receivedData.trim();

      // Process the key-value pair when complete
      if (receivedData.indexOf("tx/active") != -1) {  
        // Find the position where the value starts
        int startPos = receivedData.indexOf("tx/active") + 9; 

        // Ensure startPos is valid and within bounds
        if (startPos >= 0 && startPos < receivedData.length()) {
          // Extract the value from startPos onward
          String value = receivedData.substring(startPos); 

          // Trim any extra spaces
          value.trim();

          // Debugging output to check what is being received
          // Serial.println("Extracted value: " + value);

          // Check if value is '1' or '0' and set PTT accordingly
          if (value == "1") {
            Serial.println("PTT/pin LOW");
            digitalWrite(PTT_PORT, LOW);  // Set PTT LED LOW 
          }
          else if (value == "0") {
            Serial.println("PTT/pin HIGH");
            digitalWrite(PTT_PORT, HIGH); // Set PTT LED HIGH
          }
        } 
      }

      // Clear receivedData for the next line
      receivedData = "";  
    }
  }
}