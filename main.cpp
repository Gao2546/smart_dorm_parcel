/**
 * ระบบสายพานลำเลียงพัสดุอัตโนมัติพร้อม QR Code Reader
 * สำหรับส่งพัสดุไปยังหอพักต่างๆ โดยใช้ ESP32 เป็นตัวควบคุม
 * 
 * ฟีเจอร์หลัก:
 * - อ่าน QR Code ผ่าน Raspberry Pi
 * - ระบบ FIFO Queue สำหรับจัดคิวพัสดุ
 * - ควบคุมมอเตอร์สายพานและมอเตอร์ผลัก
 * - อัพเดทสถานะการส่งผ่าน UART
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>   // ✅ ใช้สำหรับ parse JSON

// === กำหนดขา GPIO สำหรับ IR Sensor ===
#define IR_DIGITAL_PIN 34      // IR sensor หลักตรวจจับพัสดุ
#define IR_DIGITAL_PING1 35    // IR sensor ประตู 1 (หอพัก 10)
#define IR_DIGITAL_PING2 36    // IR sensor ประตู 2 (หอพัก 2)
#define IR_DIGITAL_PING3 39    // IR sensor ประตู 3 (หอพัก 6)

// === กำหนดขา L298N Motor Driver สำหรับสายพาน ===
#define IN1 26                 // ควบคุมทิศทางมอเตอร์ (ขา 1)
#define IN2 18                 // ควบคุมทิศทางมอเตอร์ (ขา 2)
#define ENA 25                 // ควบคุมความเร็วมอเตอร์ (PWM)

// === กำหนดขา Motor สำหรับระบบผลักพัสดุ ===
#define MOTOR0_IN_A 12         // มอเตอร์ผลัก 1 - ขา A
#define MOTOR0_IN_B 23         // มอเตอร์ผลัก 1 - ขา B

#define MOTOR1_IN_A 13         // มอเตอร์ผลัก 2 - ขา A
#define MOTOR1_IN_B 27         // มอเตอร์ผลัก 2 - ขา B

#define MOTOR2_IN_A 33         // มอเตอร์ผลัก 3 - ขา A
#define MOTOR2_IN_B 32         // มอเตอร์ผลัก 3 - ขา B

#define MAX_DORM 10            // จำนวนสูงสุดของพัสดุในคิว

// === ตัวแปร Flag สำหรับตรวจจับสัญญาณ IR ===
bool flag = false;             // Flag สำหรับ IR sensor หลัก
bool flagG1 = false;           // Flag สำหรับ IR sensor ประตู 1
bool flagG2 = false;           // Flag สำหรับ IR sensor ประตู 2
bool flagG3 = false;           // Flag สำหรับ IR sensor ประตู 3

// === ระบบ FIFO Queue สำหรับเก็บข้อมูลพัสดุ ===
int dorm[MAX_DORM];            // Array เก็บหมายเลขหอพัก
String tracking_number[MAX_DORM]; // Array เก็บหมายเลขติดตาม
int size = 0;                  // ขนาดปัจจุบันของคิว

// === เก็บจำนวนพัสดุที่ผ่านแต่ละประตู ===
int gate_count[3] = {0,0,0};   // [ประตู1, ประตู2, ประตู3]

int camera_gate_count = 0;     // นับจำนวนการถ่ายภาพ

/**
 * ฟังก์ชันเพิ่มพัสดุเข้าคิว (FIFO)
 * @param value หมายเลขหอพัก
 * @param trackingNum หมายเลขติดตามพัสดุ
 */
void enqueue(int value, String trackingNum) {
  if (size < MAX_DORM) {
    dorm[size] = value;                    // เพิ่มหมายเลขหอพัก
    tracking_number[size] = trackingNum;   // เพิ่มหมายเลขติดตาม
    size++;                                // เพิ่มขนาดคิว
  } else {
    Serial.println("Queue full, cannot enqueue");
  }
}

/**
 * ฟังก์ชันนำพัสดุออกจากคิวตำแหน่งแรก (FIFO)
 * @return หมายเลขหอพักที่นำออก หรือ -1 หากคิวว่าง
 */
int dequeue() {
  if (size > 0) {
    int first = dorm[0];                         // เก็บค่าแรก
    String firstTrackingNumber = tracking_number[0];
    
    // เลื่อนข้อมูลทั้งหมดไปข้างหน้า
    for (int i = 1; i < size; i++) {
      dorm[i - 1] = dorm[i];
      tracking_number[i - 1] = tracking_number[i];
    }
    size--;                                      // ลดขนาดคิว
    return first;
  } else {
    Serial.println("Queue empty");
    return -1;
  }
}

/**
 * ฟังก์ชันนำพัสดุออกจากตำแหน่งที่กำหนด
 * @param index ตำแหน่งที่ต้องการนำออก
 * @return หมายเลขหอพักที่นำออก หรือ -1 หากผิดพลาด
 */
int dequeueAt(int index) {
  if (index < 0 || index >= size) {
    Serial.println("Invalid index");
    return -1;  // error
  }

  int removed = dorm[index];                     // เก็บค่าที่จะลบ

  // เลื่อนข้อมูลหลังจากตำแหน่งที่ลบมาข้างหน้า
  for (int i = index + 1; i < size + 1; i++) {
    dorm[i - 1] = dorm[i];
    tracking_number[i - 1] = tracking_number[i];
  }

  size--;                                        // ลดขนาดคิว
  return removed;
}

/**
 * ฟังก์ชันจัดเรียงคิวใหม่เมื่อมีการซ้ำ
 * ใช้สำหรับกรณีที่อ่าน QR Code ซ้ำ
 */
int requeue_lastvalue() {
  if (size > 0) {
    // ตรวจสอบว่าหมายเลขติดตามตัวสุดท้ายกับตัวก่อนหน้าเหมือนกันหรือไม่
    if (tracking_number[size - 2] == tracking_number[size - 1]) {
      dorm[size] = dorm[size - 2];                           // คัดลอกข้อมูล
      tracking_number[size] = tracking_number[size - 2];
      dorm[size - 2] = -2;                                   // ทำเครื่องหมายว่าลบแล้ว
      tracking_number[size - 2] = "No tracking number";
    } else {
      Serial.println("Tracking number mismatch");
      return -1;
    }
  }
}

/**
 * ฟังก์ชันส่งคำสั่งอ่าน QR Code ไป Raspberry Pi ผ่าน UART
 * แทนการใช้ HTTP request
 * @return ข้อมูล JSON ที่ได้รับจาก Raspberry Pi
 */
String requestQRFromPi() {
  Serial2.println("READ_QR");                    // ส่งคำสั่งไป Pi
  unsigned long start = millis();
  String response = "";

  // รอรับข้อมูลจาก Pi สูงสุด 5 วินาที
  while (millis() - start < 5000) {
    if (Serial2.available()) {
      char c = Serial2.read();
      if (c == '\n') break;                      // จบข้อความ
      response += c;
    }
  }

  return response;
}

/**
 * ฟังก์ชันอัพเดทสถานะการส่งพัสดุไปยังเซิร์ฟเวอร์ผ่าน UART
 * @param trackingNumber หมายเลขติดตามพัสดุ
 * @param status สถานะใหม่
 */
void updateTrackingStatusOnServer(String trackingNumber, const char *status) {
  // สร้าง JSON message
  String payload = "{";
  payload += "\"type\":\"update_status\",";
  payload += "\"trackingNumber\":\"" + trackingNumber + "\",";
  payload += "\"status\":\"" + String(status) + "\"";
  payload += "}\n";   // newline = end of message

  // ส่งไป Raspberry Pi ผ่าน UART
  Serial2.print(payload);

  Serial.println("📡 Sent update via UART: " + payload);
}

/**
 * ฟังก์ชันควบคุมการเคลื่อนไหวของสายพาน
 * @param fb_move ทิศทางการเคลื่อนไหว (-1=ถอยหลัง, 0=หยุด, 1=เดินหน้า)
 */
void convayer_move(int fb_move) {
  switch (fb_move) {
    case -1: // ถอยหลัง
      digitalWrite(IN1, LOW);
      digitalWrite(IN2, HIGH);
      ledcWrite(0, 255);                         // ความเร็วเต็มที่
      Serial.println("backward");
      break;

    case 0: // หยุด
      digitalWrite(IN1, LOW);
      digitalWrite(IN2, LOW);
      ledcWrite(0, 0);                           // ความเร็ว 0
      Serial.println("stop");
      break;

    case 1: // เดินหน้า
      digitalWrite(IN1, HIGH);
      digitalWrite(IN2, LOW);
      ledcWrite(0, 255);                         // ความเร็วเต็มที่
      Serial.println("forward");
      break;
  }
}

/**
 * ฟังก์ชันควบคุมมอเตอร์ผลักพัสดุ
 * @param fb_move ทิศทางการเคลื่อนไหว (1=ผลัก, -1=ดึงกลับ, 0=หยุด)
 * @param time_ms ระยะเวลาการเคลื่อนไหว (มิลลิวินาที)
 * @param motor_id หมายเลขมอเตอร์ (1-3)
 */
void motor_move(int fb_move, int time_ms, int motor_id) {
  int IN_A, IN_B;

  // เลือกขา GPIO ตามหมายเลขมอเตอร์
  switch (motor_id) {
    case 1: IN_A = MOTOR0_IN_A; IN_B = MOTOR0_IN_B; break;
    case 2: IN_A = MOTOR1_IN_A; IN_B = MOTOR1_IN_B; break;
    case 3: IN_A = MOTOR2_IN_A; IN_B = MOTOR2_IN_B; break;
    default: return;  // หมายเลขมอเตอร์ไม่ถูกต้อง
  }

  // ควบคุมทิศทางการหมุน
  switch (fb_move) {
    case 1: // หมุนไปข้างหน้า / ตามเข็มนาฬิกา
      digitalWrite(IN_A, HIGH);
      digitalWrite(IN_B, LOW);
      break;
    case -1: // หมุนถอยหลัง / ทวนเข็มนาฬิกา
      digitalWrite(IN_A, LOW);
      digitalWrite(IN_B, HIGH);
      break;
    case 0: // หยุด
      digitalWrite(IN_A, LOW);
      digitalWrite(IN_B, LOW);
      break;
    default: return;
  }

  delay(time_ms);                                // รอตามเวลาที่กำหนด

  // หยุดมอเตอร์หลังจากเคลื่อนไหวเสร็จ
  digitalWrite(IN_A, LOW);
  digitalWrite(IN_B, LOW);
}

/**
 * ฟังก์ชันแสดงสถานะของคิว (สำหรับ Debug)
 */
void show_queue() {
  Serial.print("Dorm contents: ");
  for (int i = 0; i < size + 1; i++) {
    Serial.println(dorm[i]);
    Serial.println(" ");
  }
  Serial.println();
}

/**
 * ฟังก์ชันผลักพัสดุออกจากสายพานไปยังหอพักที่กำหนด
 * @param dorm_box หมายเลขประตู/หอพัก (1, 2, 3)
 */
void push_box(int dorm_box){
  Serial.println("push Box-------------------");
  Serial.println(gate_count[dorm_box - 1]);
  Serial.println(dorm[gate_count[dorm_box - 1] - 1]);
  show_queue();
  
  switch (dorm_box)
  {
  case 1: // ประตู 1 - หอพัก 10
    if (dorm[gate_count[dorm_box - 1] - 1] == 10){
      convayer_move(0);                          // หยุดสายพาน
      
      // สร้างข้อความสถานะ
      char * status = (char *)malloc(20 * sizeof(char));
      sprintf(status, "delivered to dorm %d", dorm[gate_count[dorm_box - 1] - 1]);
      
      // อัพเดทสถานะ
      updateTrackingStatusOnServer(tracking_number[gate_count[dorm_box - 1] - 1], status);
      
      Serial.print("push dorm: ");
      Serial.println(dequeueAt(gate_count[dorm_box - 1] - 1));
      gate_count[dorm_box - 1] = gate_count[dorm_box - 1] - 1;
      
      show_queue();                              // แสดงสถานะคิว
      
      // ผลักพัสดุด้วยมอเตอร์
      motor_move(1,1800,dorm_box);               // ผลักออก 1.8 วินาที
      motor_move(-1,1700,dorm_box);              // ดึงกลับ 1.7 วินาที  
      motor_move(0,50,dorm_box);                 // หยุด 0.05 วินาที
      convayer_move(1);                          // เริ่มสายพานอีกครั้ง
    }
    break;

  case 2: // ประตู 2 - หอพัก 2
    if (dorm[gate_count[dorm_box-1] - 1] == 2){
      convayer_move(0);                          // หยุดสายพาน
      
      // สร้างข้อความสถานะ
      char * status = (char *)malloc(20 * sizeof(char));
      sprintf(status, "delivered to dorm %d", dorm[gate_count[dorm_box - 1] - 1]);
      
      // อัพเดทสถานะ
      updateTrackingStatusOnServer(tracking_number[gate_count[dorm_box - 1] - 1], status);
      
      Serial.print("push dorm: ");
      Serial.println(dequeueAt(gate_count[dorm_box - 1] - 1));
      
      // อัพเดทตัวนับของทุกประตูที่อยู่ก่อนหน้า
      gate_count[dorm_box - 1] = gate_count[dorm_box - 1] - 1;
      gate_count[dorm_box - 2] = gate_count[dorm_box - 2] - 1;
      
      show_queue();                              // แสดงสถานะคิว
      
      // ผลักพัสดุด้วยมอเตอร์
      motor_move(1,1800,dorm_box);               // ผลักออก 1.8 วินาที
      motor_move(-1,1700,dorm_box);              // ดึงกลับ 1.7 วินาที
      motor_move(0,50,dorm_box);                 // หยุด 0.05 วินาที
      convayer_move(1);                          // เริ่มสายพานอีกครั้ง
      Serial.println(dorm[0]);
    }
    break;

  case 3: // ประตู 3 - หอพัก 6
    if (dorm[gate_count[dorm_box-1] - 1] == 6){
      convayer_move(0);                          // หยุดสายพาน
      
      // สร้างข้อความสถานะ
      char * status = (char *)malloc(20 * sizeof(char));
      sprintf(status, "delivered to dorm %d", dorm[gate_count[dorm_box - 1] - 1]);
      
      // อัพเดทสถานะ
      updateTrackingStatusOnServer(tracking_number[gate_count[dorm_box - 1] - 1], status);
      
      Serial.print("push dorm: ");
      Serial.println(dequeueAt(gate_count[dorm_box - 1] - 1));
      
      // อัพเดทตัวนับของทุกประตูที่อยู่ก่อนหน้า
      gate_count[dorm_box - 1] = gate_count[dorm_box - 1] - 1;
      gate_count[dorm_box - 2] = gate_count[dorm_box - 2] - 1;
      gate_count[dorm_box - 3] = gate_count[dorm_box - 3] - 1;
      
      show_queue();                              // แสดงสถานะคิว
      
      // ผลักพัสดุด้วยมอเตอร์
      motor_move(1,1800,dorm_box);               // ผลักออก 1.8 วินาที
      motor_move(-1,1650,dorm_box);              // ดึงกลับ 1.65 วินาที
      motor_move(0,50,dorm_box);                 // หยุด 0.05 วินาที
      convayer_move(1);                          // เริ่มสายพานอีกครั้ง
    }
    else {
      // กรณีพัสดุไม่ใช่ของหอพักนี้ - ส่งต่อไป
      Serial.print("push box no form: ");
      Serial.println(dequeueAt(gate_count[dorm_box - 1] - 1));
      
      // อัพเดทตัวนับของทุกประตูที่อยู่ก่อนหน้า
      gate_count[dorm_box - 1] = gate_count[dorm_box - 1] - 1;
      gate_count[dorm_box - 2] = gate_count[dorm_box - 2] - 1;
      gate_count[dorm_box - 3] = gate_count[dorm_box - 3] - 1;
      
      show_queue();                              // แสดงสถานะคิว
      Serial.println();
      Serial.println("No box for this dorm");
    }
    break;
  
  default:
    break;
  }
}

/**
 * ฟังก์ชัน Setup - ทำงานครั้งเดียวเมื่อเริ่มต้น
 */
void setup() {
  // เริ่มต้น Serial communication
  Serial.begin(115200);                         // Serial หลักสำหรับ debug
  Serial2.begin(115200, SERIAL_8N1, 16, 17);   // Serial2 สำหรับติดต่อ Raspberry Pi (RX=16, TX=17)

  // ตั้งค่าขา IR Sensor เป็น INPUT
  pinMode(IR_DIGITAL_PIN, INPUT);               // IR sensor หลัก
  pinMode(IR_DIGITAL_PING1, INPUT);             // IR sensor ประตู 1
  pinMode(IR_DIGITAL_PING2, INPUT);             // IR sensor ประตู 2
  pinMode(IR_DIGITAL_PING3, INPUT);             // IR sensor ประตู 3

  // ตั้งค่าขามอเตอร์สายพานเป็น OUTPUT
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);

  // ตั้งค่าขามอเตอร์ผลัก 1 เป็น OUTPUT
  pinMode(MOTOR0_IN_A, OUTPUT);
  pinMode(MOTOR0_IN_B, OUTPUT);
  digitalWrite(MOTOR0_IN_A, LOW);               // ตั้งค่าเริ่มต้นเป็น LOW
  digitalWrite(MOTOR0_IN_B, LOW);

  // ตั้งค่าขามอเตอร์ผลัก 2 เป็น OUTPUT
  pinMode(MOTOR1_IN_A, OUTPUT);
  pinMode(MOTOR1_IN_B, OUTPUT);
  digitalWrite(MOTOR1_IN_A, LOW);               // ตั้งค่าเริ่มต้นเป็น LOW
  digitalWrite(MOTOR1_IN_B, LOW);

  // ตั้งค่าขามอเตอร์ผลัก 3 เป็น OUTPUT
  pinMode(MOTOR2_IN_A, OUTPUT);
  pinMode(MOTOR2_IN_B, OUTPUT);
  digitalWrite(MOTOR2_IN_A, LOW);               // ตั้งค่าเริ่มต้นเป็น LOW
  digitalWrite(MOTOR2_IN_B, LOW);

  // ตั้งค่า PWM สำหรับควบคุมความเร็วมอเตอร์สายพาน
  ledcSetup(0, 5000, 8);                       // Channel 0, ความถี่ 5kHz, ความละเอียด 8 บิต
  ledcAttachPin(ENA, 0);                       // เชื่อมต่อขา ENA กับ PWM channel 0

  convayer_move(0);                            // หยุดสายพานเริ่มต้น
  convayer_move(1);                            // เริ่มการทำงานสายพาน
}

/**
 * ฟังก์ชัน Loop หลัก - ทำงานแบบวนซ้ำ
 */
void loop() {
  // อ่านค่าจาก IR Sensor ทั้งหมด
  int IRCamera = digitalRead(IR_DIGITAL_PIN) ^ HIGH;    // IR หลัก (invert signal)
  int IRCameraG1 = (digitalRead(IR_DIGITAL_PING1) ^ HIGH) * 1;  // IR ประตู 1
  int IRCameraG2 = (digitalRead(IR_DIGITAL_PING2) ^ HIGH) * 2;  // IR ประตู 2  
  int IRCameraG3 = (digitalRead(IR_DIGITAL_PING3) ^ HIGH) * 3;  // IR ประตู 3
  int IRCameraBB = IRCameraG1 + IRCameraG2 + IRCameraG3;        // รวมค่า IR ทั้งหมด

  // ตรวจสอบว่ามีพัสดุในคิวหรือไม่
  if (dorm[0] != 0){

    // === ตรวจสอบ IR Sensor ประตู 1 ===
    if ((IRCameraG1 == 1) && flagG1 && (gate_count[0] + 1 > gate_count[1]) && (gate_count[0] + 1 > gate_count[2]) && (gate_count[0] + 1 <= size)){
      flagG1 = false;                           // รีเซ็ต flag
      gate_count[0] = gate_count[0] + 1;        // เพิ่มตัวนับประตู 1
      
      // แสดงสถานะตัวนับทุกประตู
      Serial.print("Gate 1: ");
      Serial.println(gate_count[0]);
      Serial.print("Gate 2: ");
      Serial.println(gate_count[1]);
      Serial.print("Gate 3: ");
      Serial.println(gate_count[2]);
      
      push_box(1);                              // ผลักพัสดุที่ประตู 1
    }
    else if ((IRCameraG1 == 0) && (flagG1 == false)){
      flagG1 = true;                            // ตั้ง flag เมื่อไม่มีสัญญาณ
    }
  
    // === ตรวจสอบ IR Sensor ประตู 2 ===
    if (IRCameraG2 == 2 && flagG2 && (gate_count[1] + 1 <= gate_count[0]) && (gate_count[1] + 1 > gate_count[2])){
      flagG2 = false;                           // รีเซ็ต flag
      gate_count[1] = gate_count[1] + 1;        // เพิ่มตัวนับประตู 2
      
      // แสดงสถานะตัวนับทุกประตู
      Serial.print("Gate 1: ");
      Serial.println(gate_count[0]);
      Serial.print("Gate 2: ");
      Serial.println(gate_count[1]);
      Serial.print("Gate 3: ");
      Serial.println(gate_count[2]);
      
      push_box(2);                              // ผลักพัสดุที่ประตู 2
    }
    else if ((IRCameraG2 == 0) && (flagG2 == false)){
      flagG2 = true;                            // ตั้ง flag เมื่อไม่มีสัญญาณ
    }
  
    // === ตรวจสอบ IR Sensor ประตู 3 ===
    if (IRCameraG3 == 3 && flagG3 && (gate_count[2] + 1 <= gate_count[0]) && (gate_count[2] + 1 <= gate_count[1])){
      flagG3 = false;                           // รีเซ็ต flag
      gate_count[2] = gate_count[2] + 1;        // เพิ่มตัวนับประตู 3
      
      // แสดงสถานะตัวนับทุกประตู
      Serial.print("Gate 1: ");
      Serial.println(gate_count[0]);
      Serial.print("Gate 2: ");
      Serial.println(gate_count[1]);
      Serial.print("Gate 3: ");
      Serial.println(gate_count[2]);
      
      push_box(3);                              // ผลักพัสดุที่ประตู 3
    }
    else if ((IRCameraG3 == 0) && (flagG3 == false)){
      flagG3 = true;                            // ตั้ง flag เมื่อไม่มีสัญญาณ
    }
  }

  // === ตรวจสอบ IR Sensor หลักสำหรับตรวจจับพัสดุใหม่ ===
  if (IRCamera == HIGH && flag) {
    convayer_move(0);                           // หยุดสายพานทันที
    
    // รีเซ็ตระบบหากไม่มี IR ที่ประตูใดๆ ทำงานและคิวว่าง
    if ((IR_DIGITAL_PING1 == LOW) && (IR_DIGITAL_PING2 == LOW) && (IR_DIGITAL_PING3 == LOW) && (size == 0)) {
      // ล้างข้อมูลในคิวทั้งหมด
      for (int i = 0; i < MAX_DORM; i++) {
        dorm[i] = 0;
        tracking_number[i] = "";
      }
      // รีเซ็ตตัวนับประตูทั้งหมด
      gate_count[0] = 0;
      gate_count[1] = 0;
      gate_count[2] = 0;
    }
    
    flag = false;                               // รีเซ็ต flag
    Serial.println("IR Triggered! Sending request to Raspberry Pi...");
    delay(300);                                 // รอให้พัสดุอยู่ในตำแหน่งที่เสถียร
    
    // ส่งคำสั่งอ่าน QR Code ไป Raspberry Pi ผ่าน UART
    String response = requestQRFromPi();        // 👈 ขอข้อมูลจาก Pi ผ่าน Serial
    Serial.println("Response from Pi: " + response);

    // ตรวจสอบว่าได้รับข้อมูลหรือไม่
    if (response.length() > 0) {
      // สร้าง JSON document สำหรับ parse ข้อมูล
      StaticJsonDocument<256> doc;
      DeserializationError error = deserializeJson(doc, response);

      // ตรวจสอบว่า parse JSON สำเร็จหรือไม่
      if (!error) {
        // ดึงข้อมูลจาก JSON
        int mappedLabel = doc["mapped_label"] | -2;         // หมายเลขหอพัก (-2 หากไม่มีข้อมูล)
        String trackingNumber = doc["qr_text"] | "No tracking number";  // หมายเลขติดตาม

        // แสดงข้อมูลที่ได้รับ
        Serial.print("Tracking Number: ");
        Serial.println(trackingNumber);
        Serial.print("Mapped Label: ");
        Serial.println(mappedLabel);

        // เพิ่มข้อมูลเข้าคิว (ไม่ว่าจะเป็นค่าใดก็ตาม)
        enqueue(mappedLabel, trackingNumber);
        requeue_lastvalue();                    // จัดการกรณีที่อ่านซ้ำ
        
        // แสดงสถานะคิว
        Serial.print("Queue size: ");
        Serial.println(size);
        show_queue();
      } else {
        Serial.println("JSON parse failed!");  // แสดงข้อผิดพลาดหาก parse ไม่สำเร็จ
      }
    }
    convayer_move(1);                           // เริ่มสายพานอีกครั้ง
  } 
  else if ((IRCamera == LOW) && (flag == false)) {
    flag = true;                                // ตั้ง flag เมื่อไม่มีพัสดุ
  }

  delay(50);                                    // หน่วงเวลา 50ms ก่อนวนซ้ำ
}