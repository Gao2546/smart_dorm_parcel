import cv2
import threading
import serial
import json
import psycopg2
from pyzbar.pyzbar import decode
from qreader import QReader
import time

# ===================================
# QR Reader และ Camera Setup
# ===================================
# สร้าง QReader instance สำหรับ YOLOv7-based QR detection
qreader = QReader(model_size='n')  # model_size 'n' = nano model (เล็กและเร็ว)

# กำหนดขนาดของภาพจากกล้อง
IMAGE_WIDTH = 1280
IMAGE_HEIGHT = 720

# วนลูปเพื่อเชื่อมต่อกล้อง (retry จนกว่าจะสำเร็จ)
while True:
    try:
        cap = cv2.VideoCapture(0)  # เปิดกล้องตัวที่ 0 (default camera)
        break
    except Exception as e:
        print(f"⚠️ Camera init error: {e}, retrying...")
        time.sleep(2)  # รอ 2 วินาทีก่อน retry
        continue

# ตั้งค่าความละเอียดของกล้อง
cap.set(cv2.CAP_PROP_FRAME_WIDTH, IMAGE_WIDTH)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, IMAGE_HEIGHT)

# ตัวแปรเก็บ frame ล่าสุดที่ capture ได้
last_frame = None

# ===================================
# Serial Communication Setup
# ===================================
# วนลูปเพื่อเชื่อมต่อ Serial Port (retry จนกว่าจะสำเร็จ)
while True:
    try:
        # เปิดการเชื่อมต่อ Serial ที่ /dev/ttyS0 ด้วย baud rate 115200
        ser = serial.Serial('/dev/ttyS0', 115200, timeout=1)
        break
    except Exception as e:
        print(f"⚠️ Serial init error: {e}, retrying...")
        time.sleep(2)  # รอ 2 วินาทีก่อน retry
        continue
    
# ===================================
# Database Connection Setup
# ===================================
# ข้อมูลการเชื่อมต่อฐานข้อมูล PostgreSQL
DB_HOST = "db"        # ชื่อ service ใน Docker สำหรับ Node.js DB
DB_PORT = 5432
DB_NAME = "smart_dorm_parcel"
DB_USER = "athip"
DB_PASSWORD = "123456"

# สร้างการเชื่อมต่อฐานข้อมูล
conn = psycopg2.connect(
    host=DB_HOST,
    port=DB_PORT,
    database=DB_NAME,
    user=DB_USER,
    password=DB_PASSWORD
)
conn.autocommit = True  # เปิด autocommit สำหรับ updates

# ===================================
# Camera Thread Function
# ===================================
def camera_loop():
    """
    ฟังก์ชันที่ทำงานใน thread แยก เพื่อ capture ภาพจากกล้องอย่างต่อเนื่อง
    และเก็บ frame ล่าสุดไว้ในตัวแปร global last_frame
    """
    global last_frame
    while True:
        ret, frame = cap.read()  # อ่าน frame จากกล้อง
        if ret:  # ถ้าอ่านสำเร็จ
            # ตัด frame ให้เหลือแค่ส่วนซ้าย (crop จากขวา)
            # frame[:, 0:1050] หมายถึง เอาทุก row แต่ column 0-1050
            last_frame = frame[:, 0:1050]

# เริ่ม thread สำหรับ camera loop
threading.Thread(target=camera_loop, daemon=True).start()

# ===================================
# QR Code Reading Functions
# ===================================
def read_qr_code2(frame):
    """
    ฟังก์ชันอ่าน QR Code ด้วย YOLOv7 + QReader
    Args:
        frame: ภาพที่จะอ่าน QR Code
    Returns:
        str: ข้อความใน QR Code ที่อยู่ขวาสุด หรือ None ถ้าไม่พบ
    """
    # detect และ decode QR codes พร้อมกับ return detection bounding boxes
    decoded_qrs, detections = qreader.detect_and_decode(frame, return_detections=True, is_bgr=True)
    
    # ถ้าไม่พบ QR code หรือไม่มี detection
    if not decoded_qrs or not detections:
        return None

    # จับคู่ข้อความที่ decode ได้กับ bounding box coordinates
    results = [
        (decoded_qrs[i], detections[i]["bbox_xyxy"][0:2])  # (text, (x1, y1))
        for i in range(len(decoded_qrs))
        if decoded_qrs[i] is not None  # เฉพาะที่ decode ได้
    ]

    if not results:
        return None

    # หา QR code ที่อยู่ขวาสุด โดยดูจาก x coordinate (bbox[0])
    rightmost = max(results, key=lambda item: item[1][0])
    return rightmost[0]  # return ข้อความ

def read_qr_code1(frame):
    """
    ฟังก์ชันอ่าน QR Code ด้วย pyzbar (วิธีแบบดั้งเดิม)
    Args:
        frame: ภาพที่จะอ่าน QR Code  
    Returns:
        str: ข้อความใน QR Code ที่อยู่ขวาสุด หรือ None ถ้าไม่พบ
    """
    decoded = decode(frame)  # decode QR codes จาก frame
    if not decoded:
        return None

    # หา QR code ที่อยู่ขวาสุด โดยดูจาก rect.left
    rightmost = max(decoded, key=lambda r: r.rect.left)
    return rightmost.data.decode("utf-8")  # แปลง bytes เป็น string

# ===================================
# QR Code Processing Function
# ===================================
def process_qr(qr_text: str) -> int:
    """
    ประมวลผล QR code text และคืนค่า mapped_label
    Args:
        qr_text: ข้อความใน QR Code
    Returns:
        int: 
        -1: QR ไม่อยู่ใน tracking_numbers หรือไม่พบ user
        -2: ไม่พบ QR code
        >=0: หมายเลขห้องพักของ user
    """
    if not qr_text:
        return -2  # ไม่พบ QR code

    with conn.cursor() as cur:
        # 1️⃣ ตรวจสอบว่า tracking_number นี้มีในฐานข้อมูลหรือไม่
        cur.execute("SELECT user_id, status FROM tracking_numbers WHERE tracking_number = %s", (qr_text,))
        row = cur.fetchone()
        if not row:
            return -1  # ไม่พบ tracking_number

        user_id, status = row
        
        # 2️⃣ อัปเดต status เป็น 'scanned' พร้อมเวลาปัจจุบัน
        cur.execute("UPDATE tracking_numbers SET status='scanned', updated_at=NOW() WHERE tracking_number=%s", (qr_text,))

        # 3️⃣ ดึงหมายเลขห้องพักจากตาราง users
        cur.execute("SELECT dorm_number FROM users WHERE id=%s", (user_id,))
        dorm_row = cur.fetchone()
        if not dorm_row:
            return -1  # ไม่พบข้อมูล user

        dorm_number = dorm_row[0]
        try:
            return int(dorm_number)  # แปลงเป็น integer
        except ValueError:
            return -1  # แปลงไม่ได้

# ===================================
# Main Loop
# ===================================
print("✅ Raspberry Pi QR Serial Server with DB started...")

while True:
    # ตรวจสอบว่ามีข้อมูลเข้ามาทาง Serial หรือไม่
    if ser.in_waiting:
        try:
            # อ่านข้อมูลจาก Serial และแปลงเป็น string
            cmd = ser.readline().decode().strip()
        except Exception as e:
            print(f"⚠️ Serial read error: {e}")
            continue

        # === คำสั่ง READ_QR ===
        if cmd == "READ_QR":
            # ตรวจสอบสถานะกล้อง
            if not cap.isOpened():
                response = {"error": "Cannot open camera"}
            elif last_frame is None:
                response = {"error": "Failed to capture image"}
            else:
                # ลองอ่าน QR ด้วยวิธีที่ 1 (pyzbar) ก่อน
                qr_text = read_qr_code1(last_frame)
                
                # ถ้าอ่านไม่ได้ ลองด้วยวิธีที่ 2 (YOLOv7)
                if not qr_text:
                    qr_text = read_qr_code2(last_frame)
                
                # ประมวลผล QR code และได้ mapped_label
                mapped_label = process_qr(qr_text)

                # ถ้าไม่พบ QR code (-2) ให้บันทึกภาพไว้ debug
                if mapped_label == -2:
                    cv2.imwrite("/app/output/debug_last_frame.jpg", last_frame)
                    print("⚠️ No QR detected → saved debug_last_frame.jpg")

                # สร้าง response object
                response = {"qr_text": qr_text or "No QR code detected", "mapped_label": mapped_label}
                print(f"📡 QR: {qr_text}, mapped_label: {mapped_label}")

            # ส่ง response กลับทาง Serial ในรูปแบบ JSON
            ser.write((json.dumps(response) + "\n").encode())
        
        # === ลองแปลงคำสั่งเป็น JSON ===
        try:
            data = json.loads(cmd)
        except json.JSONDecodeError:
            print(f"⚠️ Invalid JSON from ESP32: {cmd}")
            continue

        # === คำสั่ง Update Status ===
        if data.get("type") == "update_status":
            tracking_number = data.get("trackingNumber")
            status = data.get("status")

            # ตรวจสอบว่ามีข้อมูลครบถ้วน
            if tracking_number and status:
                try:
                    with conn.cursor() as cur:
                        # อัปเดต status ในฐานข้อมูล
                        cur.execute("""
                            UPDATE tracking_numbers 
                            SET status=%s, updated_at=NOW() 
                            WHERE tracking_number=%s
                        """, (status, tracking_number))
                        print(f"✅ Updated {tracking_number} → {status}")
                except Exception as e:
                    print(f"❌ DB update error: {e}")
            else:
                print("⚠️ Missing fields in update_status request")