#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>   // ✅ ใช้สำหรับ parse JSON

// === WiFi credentials ===
const char* ssid     = "POCO F7 Pro";
const char* password = "1233477815";

// === Flask server endpoint ===
const char* serverURL = "http://10.128.44.58:3000/readQR";//"http://10.128.44.72:3000/readQR";

// === IR Sensor Pins ===
#define IR_DIGITAL_PIN 34
#define IR_DIGITAL_PING1 35
#define IR_DIGITAL_PING2 36
#define IR_DIGITAL_PING3 39
// === L298N Motor Driver Pins ===
#define IN1 26
#define IN2 18
#define ENA 25

// === Motor pins for H-bridge control ===
#define MOTOR0_IN_A 12
#define MOTOR0_IN_B 23

#define MOTOR1_IN_A 13
#define MOTOR1_IN_B 27

#define MOTOR2_IN_A 33
#define MOTOR2_IN_B 32


#define MAX_DORM 10

bool flag = false;
bool flagG1 = false;
bool flagG2 = false;
bool flagG3 = false;

// FIFO queue buffer
int dorm[MAX_DORM];
String tracking_number[MAX_DORM];
int size = 0;

int gate_count[3] = {0,0,0};

int camera_gate_count = 0;

// Append new data into dorm (FIFO)
void enqueue(int value, String trackingNum) {
  if (size < MAX_DORM) {
    dorm[size] = value;
    tracking_number[size] = trackingNum;
    size++;
  } else {
    Serial.println("Queue full, cannot enqueue");
  }
}

// Remove first element (FIFO)
int dequeue() {
  if (size > 0) {
    int first = dorm[0];
    String firstTrackingNumber = tracking_number[0];
    for (int i = 1; i < size; i++) {
      dorm[i - 1] = dorm[i];
      tracking_number[i - 1] = tracking_number[i];
    }
    size--;
    return first;
  } else {
    Serial.println("Queue empty");
    return -1;
  }
}

// Remove element at a specific index
int dequeueAt(int index) {
  if (index < 0 || index >= size) {
    Serial.println("Invalid index");
    return -1;  // error
  }

  int removed = dorm[index];

  // Shift everything after index to the left
  for (int i = index + 1; i < size + 1; i++) {
    dorm[i - 1] = dorm[i];
    tracking_number[i - 1] = tracking_number[i];
  }

  size--;
  return removed;
}

int requeue_lastvalue() {
  if (size > 0) {
    if (tracking_number[size - 2] == tracking_number[size - 1]) {
      dorm[size] = dorm[size - 2];
      tracking_number[size] = tracking_number[size - 2];
      dorm[size - 2] = -2;
      tracking_number[size - 2] = "No tracking number";
    } else {
      Serial.println("Tracking number mismatch");
      return -1;
    }
  }
}

// === Replace HTTP call with Serial request ===
String requestQRFromPi() {
  Serial2.println("READ_QR");  // Send request to Raspberry Pi
  unsigned long start = millis();
  String response = "";

  while (millis() - start < 5000) {  // wait up to 5 seconds
    if (Serial2.available()) {
      char c = Serial2.read();
      if (c == '\n') break;  // End of message
      response += c;
    }
  }

  return response;
}

// void updateTrackingStatusOnServer(String trackingNumber, char * status) {
//   if (WiFi.status() != WL_CONNECTED) {
//     Serial.println("WiFi not connected!");
//     return;
//   }

//   HTTPClient http;
//   http.begin("http://10.128.44.58:3000/tracking/status/update");  // Express server endpoint http://10.128.44.72:3000/tracking/status/update

//   http.addHeader("Content-Type", "application/json");

//   // Build JSON payload
//   String payload = "{";
//   payload += "\"trackingNumber\":\"" + String(trackingNumber) + "\",";
//   payload += "\"status\":\"" + String(status) + "\"";
//   payload += "}";

//   int httpResponseCode = http.POST(payload);

//   if (httpResponseCode > 0) {
//     String response = http.getString();
//     Serial.println("Update response: " + response);
//   } else {
//     Serial.print("Error updating status: ");
//     Serial.println(httpResponseCode);
//   }

//   http.end();
// }

// === Replace HTTP update with UART update ===
void updateTrackingStatusOnServer(String trackingNumber, const char *status) {
  // Build JSON message
  String payload = "{";
  payload += "\"type\":\"update_status\",";
  payload += "\"trackingNumber\":\"" + trackingNumber + "\",";
  payload += "\"status\":\"" + String(status) + "\"";
  payload += "}\n";   // newline = end of message

  // Send to Raspberry Pi over UART
  Serial2.print(payload);

  Serial.println("📡 Sent update via UART: " + payload);
}


// Motor control function
void convayer_move(int fb_move) {
  switch (fb_move) {
    case -1: // Backward
      digitalWrite(IN1, LOW);
      digitalWrite(IN2, HIGH);
      ledcWrite(0, 255);
      Serial.println("backward");
      break;

    case 0: // Stop
      digitalWrite(IN1, LOW);
      digitalWrite(IN2, LOW);
      ledcWrite(0, 0);
      Serial.println("stop");
      break;

    case 1: // Forward
      digitalWrite(IN1, HIGH);
      digitalWrite(IN2, LOW);
      ledcWrite(0, 255);
      Serial.println("forward");
      break;
  }
}

void motor_move(int fb_move, int time_ms, int motor_id) {
  int IN_A, IN_B;

  switch (motor_id) {
    case 1: IN_A = MOTOR0_IN_A; IN_B = MOTOR0_IN_B; break;
    case 2: IN_A = MOTOR1_IN_A; IN_B = MOTOR1_IN_B; break;
    case 3: IN_A = MOTOR2_IN_A; IN_B = MOTOR2_IN_B; break;
    default: return;  // invalid motor_id
  }

  // Motor action
  switch (fb_move) {
    case 1: // Forward / CW
      digitalWrite(IN_A, HIGH);
      digitalWrite(IN_B, LOW);
      break;
    case -1: // Backward / CCW
      digitalWrite(IN_A, LOW);
      digitalWrite(IN_B, HIGH);
      break;
    case 0: // Stop
      digitalWrite(IN_A, LOW);
      digitalWrite(IN_B, LOW);
      break;
    default: return;
  }

  delay(time_ms);

  // Stop motor after movement
  digitalWrite(IN_A, LOW);
  digitalWrite(IN_B, LOW);
}


void show_queue() {
  Serial.print("Dorm contents: ");
  for (int i = 0; i < size + 1; i++) {
    Serial.println(dorm[i]);
    Serial.println(" ");
  }
  Serial.println();
}


void push_box(int dorm_box){
  Serial.println("push Box-------------------");
  Serial.println(gate_count[dorm_box - 1]);
  Serial.println(dorm[gate_count[dorm_box - 1] - 1]);
  show_queue();
  switch (dorm_box)
  {
  case 1:
    /* code */
    if (dorm[gate_count[dorm_box - 1] - 1] == 10){
      convayer_move(0);
      char * status = (char *)malloc(20 * sizeof(char));
      sprintf(status, "delivered to dorm %d", dorm[gate_count[dorm_box - 1] - 1]);
      updateTrackingStatusOnServer(tracking_number[gate_count[dorm_box - 1] - 1], status);
      Serial.print("push dorm: ");
      Serial.println(dequeueAt(gate_count[dorm_box - 1] - 1));
      gate_count[dorm_box - 1] = gate_count[dorm_box - 1] - 1;
      // Print queue contents
      show_queue();
      /*push box using motor*/
      motor_move(1,1800,dorm_box);
      motor_move(-1,1700,dorm_box);
      motor_move(0,50,dorm_box);
      convayer_move(1);

    }
    break;

  case 2:
    /* code */
    if (dorm[gate_count[dorm_box-1] - 1] == 2){
      convayer_move(0);
      char * status = (char *)malloc(20 * sizeof(char));
      sprintf(status, "delivered to dorm %d", dorm[gate_count[dorm_box - 1] - 1]);
      updateTrackingStatusOnServer(tracking_number[gate_count[dorm_box - 1] - 1], status);
      Serial.print("push dorm: ");
      Serial.println(dequeueAt(gate_count[dorm_box - 1] - 1));
      gate_count[dorm_box - 1] = gate_count[dorm_box - 1] - 1;
      gate_count[dorm_box - 2] = gate_count[dorm_box - 2] - 1;
      // Print queue contents
      show_queue();
      /*push box using motor*/
      motor_move(1,1800,dorm_box);
      motor_move(-1,1700,dorm_box);
      motor_move(0,50,dorm_box);
      convayer_move(1);
      Serial.println(dorm[0]);
    }
    break;

  case 3:
    /* code */
    if (dorm[gate_count[dorm_box-1] - 1] == 6){
      convayer_move(0);
      char * status = (char *)malloc(20 * sizeof(char));
      sprintf(status, "delivered to dorm %d", dorm[gate_count[dorm_box - 1] - 1]);
      updateTrackingStatusOnServer(tracking_number[gate_count[dorm_box - 1] - 1], status);
      Serial.print("push dorm: ");
      Serial.println(dequeueAt(gate_count[dorm_box - 1] - 1));
      gate_count[dorm_box - 1] = gate_count[dorm_box - 1] - 1;
      gate_count[dorm_box - 2] = gate_count[dorm_box - 2] - 1;
      gate_count[dorm_box - 3] = gate_count[dorm_box - 3] - 1;
      // Print queue contents
      show_queue();
      /*push box using motor*/
      motor_move(1,1800,dorm_box);
      motor_move(-1,1650,dorm_box);
      motor_move(0,50,dorm_box);
      convayer_move(1);
    }
    else {
      Serial.print("push box no form: ");
      Serial.println(dequeueAt(gate_count[dorm_box - 1] - 1));
      gate_count[dorm_box - 1] = gate_count[dorm_box - 1] - 1;
      gate_count[dorm_box - 2] = gate_count[dorm_box - 2] - 1;
      gate_count[dorm_box - 3] = gate_count[dorm_box - 3] - 1;
      // Print queue contents
      show_queue();
      Serial.println();
      Serial.println("No box for this dorm");
    }
    break;
  
  default:
    break;
  }
}


void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, 16, 17); // RX=16, TX=17 (change pins if needed)
  // If using USB cable directly, just use Serial instead of Serial2

  pinMode(IR_DIGITAL_PIN, INPUT);
  pinMode(IR_DIGITAL_PING1, INPUT);
  pinMode(IR_DIGITAL_PING2, INPUT);
  pinMode(IR_DIGITAL_PING3, INPUT);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);

  pinMode(MOTOR0_IN_A, OUTPUT);
  pinMode(MOTOR0_IN_B, OUTPUT);
  digitalWrite(MOTOR0_IN_A, LOW);
  digitalWrite(MOTOR0_IN_B, LOW);

  pinMode(MOTOR1_IN_A, OUTPUT);
  pinMode(MOTOR1_IN_B, OUTPUT);
  digitalWrite(MOTOR1_IN_A, LOW);
  digitalWrite(MOTOR1_IN_B, LOW);

  pinMode(MOTOR2_IN_A, OUTPUT);
  pinMode(MOTOR2_IN_B, OUTPUT);
  digitalWrite(MOTOR2_IN_A, LOW);
  digitalWrite(MOTOR2_IN_B, LOW);


  ledcSetup(0, 5000, 8);
  ledcAttachPin(ENA, 0);

  convayer_move(0);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  convayer_move(1);
}

void loop() {
  int IRCamera = digitalRead(IR_DIGITAL_PIN) ^ HIGH;
  int IRCameraG1 = (digitalRead(IR_DIGITAL_PING1) ^ HIGH) * 1;
  int IRCameraG2 = (digitalRead(IR_DIGITAL_PING2) ^ HIGH) * 2;
  int IRCameraG3 = (digitalRead(IR_DIGITAL_PING3) ^ HIGH) * 3;
  int IRCameraBB = IRCameraG1 + IRCameraG2 + IRCameraG3;

  if (dorm[0] != 0){

    if ((IRCameraG1 == 1) && flagG1 && (gate_count[0] + 1 > gate_count[1]) && (gate_count[0] + 1 > gate_count[2]) && (gate_count[0] + 1 <= size)){
      flagG1 = false;
      gate_count[0] = gate_count[0] + 1;
      Serial.print("Gate 1: ");
      Serial.println(gate_count[0]);
      Serial.print("Gate 2: ");
      Serial.println(gate_count[1]);
      Serial.print("Gate 3: ");
      Serial.println(gate_count[2]);
      push_box(1);
    }
    else if ((IRCameraG1 == 0) && (flagG1 == false)){
      flagG1 = true;
    }
  
    if (IRCameraG2 == 2 && flagG2 && (gate_count[1] + 1 <= gate_count[0]) && (gate_count[1] + 1 > gate_count[2])){
      flagG2 = false;
      gate_count[1] = gate_count[1] + 1;
      Serial.print("Gate 1: ");
      Serial.println(gate_count[0]);
      Serial.print("Gate 2: ");
      Serial.println(gate_count[1]);
      Serial.print("Gate 3: ");
      Serial.println(gate_count[2]);
      push_box(2);
    }
    else if ((IRCameraG2 == 0) && (flagG2 == false)){
      flagG2 = true;
    }
  
    if (IRCameraG3 == 3 && flagG3 && (gate_count[2] + 1 <= gate_count[0]) && (gate_count[2] + 1 <= gate_count[1])){
      flagG3 = false;
      gate_count[2] = gate_count[2] + 1;
      Serial.print("Gate 1: ");
      Serial.println(gate_count[0]);
      Serial.print("Gate 2: ");
      Serial.println(gate_count[1]);
      Serial.print("Gate 3: ");
      Serial.println(gate_count[2]);
      push_box(3);
    }
    else if ((IRCameraG3 == 0) && (flagG3 == false)){
      flagG3 = true;
    }
  }

  

  if (IRCamera == HIGH && flag) {
    convayer_move(0);
    if ((IR_DIGITAL_PING1 == LOW) && (IR_DIGITAL_PING2 == LOW) && (IR_DIGITAL_PING3 == LOW) && (size == 0)) {
      for (int i = 0; i < MAX_DORM; i++) {
        dorm[i] = 0;
        tracking_number[i] = "";
      }
      gate_count[0] = 0;
      gate_count[1] = 0;
      gate_count[2] = 0;
    }
    flag = false;
    Serial.println("IR Triggered! Sending request to Raspberry Pi...");
    delay(300);  // Wait for the box to stabilize
    String response = requestQRFromPi();  // 👈 Now request from Pi over Serial
    Serial.println("Response from Pi: " + response);

    if (response.length() > 0) {
      StaticJsonDocument<256> doc;
      DeserializationError error = deserializeJson(doc, response);

      if (!error) {
        int mappedLabel = doc["mapped_label"] | -2;
        String trackingNumber = doc["qr_text"] | "No tracking number";

        Serial.print("Tracking Number: ");
        Serial.println(trackingNumber);
        Serial.print("Mapped Label: ");
        Serial.println(mappedLabel);

        // if (mappedLabel > 0) {
        enqueue(mappedLabel, trackingNumber);
        requeue_lastvalue();
        // }
        Serial.print("Queue size: ");
        Serial.println(size);
        show_queue();
      } else {
        Serial.println("JSON parse failed!");
      }
    }
    convayer_move(1);
  } 
  else if ((IRCamera == LOW) && (flag == false)) {
    flag = true;
  }

  delay(50);
}




















// #include <Arduino.h>
// #include <WiFi.h>
// #include <HTTPClient.h>
// #include <ArduinoJson.h>   // ✅ ใช้สำหรับ parse JSON

// // === WiFi credentials ===
// const char* ssid     = "POCO F7 Pro";
// const char* password = "1233477815";

// // === Flask server endpoint ===
// const char* serverURL = "http://10.253.71.72:5000/readQR";

// // === IR Sensor Pins ===
// #define IR_DIGITAL_PIN 34
// #define IR_DIGITAL_PING1 35
// #define IR_DIGITAL_PING2 36
// #define IR_DIGITAL_PING3 37
// // === L298N Motor Driver Pins ===
// #define IN1 26
// #define IN2 27
// #define ENA 25

// #define MAX_DORM 10

// bool flag = false;
// bool flagG1 = false;
// bool flagG2 = false;
// bool flagG3 = false;

// // FIFO queue buffer
// int dorm[MAX_DORM]; 
// String tracking_number[MAX_DORM];
// int size = 0;

// int gate_count[3] = {0,0,0};

// // Append new data into dorm (FIFO)
// void enqueue(const int &value, const String &trackingNum) {
//   if (size < MAX_DORM) {
//     dorm[size++] = value;
//     tracking_number[size - 1] = trackingNum;
//   } else {
//     Serial.println("Queue full, cannot enqueue");
//   }
// }

// // Remove first element (FIFO)
// int dequeue() {
//   if (size > 0) {
//     int first = dorm[0];
//     String firstTrackingNumber = tracking_number[0];
//     for (int i = 1; i < size; i++) {
//       dorm[i - 1] = dorm[i];
//       tracking_number[i - 1] = tracking_number[i];
//     }
//     size--;
//     return first;
//   } else {
//     Serial.println("Queue empty");
//     return -1;
//   }
// }

// // Remove element at a specific index
// int dequeueAt(int index) {
//   if (index < 0 || index >= size) {
//     Serial.println("Invalid index");
//     return -1;  // error
//   }

//   int removed = dorm[index];

//   // Shift everything after index to the left
//   for (int i = index + 1; i < size; i++) {
//     dorm[i - 1] = dorm[i];
//     tracking_number[i - 1] = tracking_number[i];
//   }

//   size--;
//   return removed;
// }


// // Motor control function
// void convayer_move(int fb_move) {
//   switch (fb_move) {
//     case -1: // Backward
//       digitalWrite(IN1, LOW);
//       digitalWrite(IN2, HIGH);
//       ledcWrite(0, 255);
//       Serial.println("backward");
//       break;

//     case 0: // Stop
//       digitalWrite(IN1, LOW);
//       digitalWrite(IN2, LOW);
//       ledcWrite(0, 0);
//       Serial.println("stop");
//       break;

//     case 1: // Forward
//       digitalWrite(IN1, HIGH);
//       digitalWrite(IN2, LOW);
//       ledcWrite(0, 255);
//       Serial.println("forward");
//       break;
//   }
// }

// void motor_move(int fb_move, int time){
//   switch (fb_move)
//   {
//   case 1:
//     /* code */
//     break;
  
//   case -1:
//     /* code */
//     break;
  
//   case 0:
//     /* code */
//     break;
  
//   default:
//     break;
//   }

// }

// void push_box(int dorm_box){
//   Serial.println("push Box-------------------");
//   Serial.println(gate_count[dorm_box - 1]);
//   Serial.println(dorm[gate_count[dorm_box - 1] - 1]);
//   switch (dorm_box)
//   {
//   case 1:
//     /* code */
//     if (dorm[gate_count[dorm_box - 1] - 1] == 10){
//       convayer_move(0);
//       Serial.print("push dorm: ");
//       Serial.println(dequeueAt(gate_count[dorm_box - 1]));
//       gate_count[dorm_box - 1] = gate_count[dorm_box - 1] - 1;
//       /*push box using motor*/
//       motor_move(1,100);
//       motor_move(-1,100);
//       motor_move(0,100);
//       convayer_move(1);
//     }
//     break;

//   case 2:
//     /* code */
//     if (dorm[gate_count[dorm_box-1] - 1] == 2){
//       convayer_move(0);
//       Serial.print("push dorm: ");
//       Serial.println(dequeueAt(gate_count[dorm_box - 1] - 1));
//       gate_count[dorm_box - 1] = gate_count[dorm_box - 1] - 1;;
//       /*push box using motor*/
//       motor_move(1,100);
//       motor_move(-1,100);
//       motor_move(0,100);
//       convayer_move(1);
//     }
//     break;

//   case 3:
//     /* code */
//     if (dorm[gate_count[dorm_box-1] - 1] == 6){
//       convayer_move(0);
//       Serial.print("push dorm: ");
//       Serial.println(dequeueAt(gate_count[dorm_box - 1] - 1));
//       gate_count[dorm_box - 1] = gate_count[dorm_box - 1] - 1;
//       /*push box using motor*/
//       motor_move(1,100);
//       motor_move(-1,100);
//       motor_move(0,100);
//       convayer_move(1);
//     }
//     break;
  
//   default:
//     break;
//   }
// }

// void setup() {
//   Serial.begin(115200);

//   pinMode(IR_DIGITAL_PIN, INPUT);
//   pinMode(IR_DIGITAL_PING1, INPUT);
//   pinMode(IR_DIGITAL_PING2, INPUT);
//   pinMode(IR_DIGITAL_PING3, INPUT);

//   pinMode(IN1, OUTPUT);
//   pinMode(IN2, OUTPUT);

//   ledcSetup(0, 5000, 8);
//   ledcAttachPin(ENA, 0);

//   convayer_move(1);

//   WiFi.begin(ssid, password);
//   Serial.print("Connecting to WiFi");
//   while (WiFi.status() != WL_CONNECTED) {
//     delay(500);
//     Serial.print(".");
//   }
//   Serial.println("\nWiFi connected");
//   Serial.print("IP address: ");
//   Serial.println(WiFi.localIP());
// }

// void loop() {
//   int IRCamera = digitalRead(IR_DIGITAL_PIN);
//   int IRCameraG1 = digitalRead(IR_DIGITAL_PING1) * 1;
//   int IRCameraG2 = digitalRead(IR_DIGITAL_PING2) * 2;
//   int IRCameraG3 = digitalRead(IR_DIGITAL_PING3) * 3;
//   int IRCameraBB = IRCameraG1 + IRCameraG2 + IRCameraG3;

//   if (dorm[0] != 0){

//     if ((IRCameraG1 == 1) && flagG1){
//       flagG1 = false;
//       gate_count[0] = gate_count[0] + 1;
//       // Serial.print(gate_count[0]);
//       push_box(1);
//     }
//     else if ((IRCameraG1 == 0) && (flagG1 == false)){
//       flagG1 = true;
//     }
  
//     if (IRCameraG2 == 2 && flagG2){
//       flagG2 = false;
//       gate_count[1] = gate_count[1] + 1;
//       // Serial.print(gate_count[1]);
//       push_box(2);
//     }
//     else if ((IRCameraG2 == 0) && (flagG2 == false)){
//       flagG2 = true;
//     }
  
//     if (IRCameraG3 == 3 && flagG3){
//       flagG3 = false;
//       gate_count[2] = gate_count[2] + 1;
//       // Serial.print(gate_count[2]);
//       push_box(3);
//     }
//     else if ((IRCameraG3 == 0) && (flagG3 == false)){
//       flagG3 = true;
//     }
//   }

  

//   if (IRCamera == HIGH && flag) {
//     convayer_move(0);
//     flag = false;
//     Serial.println("IR Triggered! Sending request to server...");

//     if (WiFi.status() == WL_CONNECTED) {
//       HTTPClient http;
//       http.begin(serverURL);

//       int httpResponseCode = http.POST("");   // ✅ Flask ใช้ POST
//       if (httpResponseCode > 0) {
//         String response = http.getString();
//         Serial.println("Response body: " + response);

//         // ✅ Parse JSON
//         StaticJsonDocument<512> doc;
//         DeserializationError error = deserializeJson(doc, response);

//         if (!error) {
//           int mappedLabel = doc["mapped_label"] | -2;
//           String trackingNumber = doc["tracking_number"] | "No tracking number";
//           Serial.print("Tracking Number: ");
//           Serial.println(trackingNumber);
//           Serial.print("Mapped Label: ");
//           Serial.println(mappedLabel);

//           if (mappedLabel) {
//             enqueue(mappedLabel, trackingNumber);  // Enqueue both label and tracking number
//             Serial.print("Enqueued dorm: ");
//             Serial.println(dorm[0]);
//             Serial.print("Appended to dorm: ");
//             Serial.println(mappedLabel);

//             // Print queue contents
//             Serial.print("Dorm contents: ");
//             for (int i = 0; i < size; i++) {
//               Serial.print(dorm[i]);
//               Serial.print(" ");
//             }
//             Serial.println();
//           }
//         } else {
//           Serial.println("JSON parse failed!");
//         }

//         convayer_move(1);
//       } else {
//         Serial.print("Error on sending request: ");
//         Serial.println(httpResponseCode);
//       }
//       http.end();
//     }
//   } 
//   else if ((IRCamera == LOW) && (flag == false)) {
//     flag = true;
//   }

//   delay(50);
// }

