# from flask import Flask, jsonify
# import cv2
# # from qreader import QReader
# from pyzbar.pyzbar import decode
# import threading

# app = Flask(__name__)

# IMAGE_WIDTH = 640//2
# IMAGE_HEIGHT = 480//2
# # qreader = QReader(model_size='n')


# cap = cv2.VideoCapture(2)
# cap.set(cv2.CAP_PROP_FRAME_WIDTH, IMAGE_WIDTH)
# cap.set(cv2.CAP_PROP_FRAME_HEIGHT, IMAGE_HEIGHT)

# last_frame = None


# def camera_loop():
#     global last_frame
#     while True:
#         ret, frame = cap.read()
#         if ret:
#             last_frame = frame

# threading.Thread(target=camera_loop, daemon=True).start()

# # def read_qr_code(frame):
# #     decoded_text = qreader.detect_and_decode(frame)
# #     return str(decoded_text[0]) if decoded_text else None

# def read_qr_code(frame):
#     decoded = decode(frame)
#     return decoded[0].data.decode("utf-8") if decoded else None

# @app.route("/readQR", methods=["POST"])
# def read_qr():

#     if not cap.isOpened():
#         return jsonify({"error": "Cannot open camera"}), 500

#     if last_frame is None:
#         return jsonify({"error": "Failed to capture image"}), 500

#     qr_text = read_qr_code(last_frame) or "No QR code detected"

#     return jsonify({
#         "qr_text": qr_text
#     }), 200

# if __name__ == "__main__":
#     app.run(host="0.0.0.0", port=5000)


import cv2
import threading
import serial
import json
import psycopg2
from qreader import QReader

# === QR Reader setup ===
qreader = QReader(model_size='n')
IMAGE_WIDTH = 640 // 2
IMAGE_HEIGHT = 480 // 2
cap = cv2.VideoCapture(0)
cap.set(cv2.CAP_PROP_FRAME_WIDTH, IMAGE_WIDTH)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, IMAGE_HEIGHT)
last_frame = None

# === Serial setup ===
ser = serial.Serial('/dev/ttyS0', 115200, timeout=1)

# === Database setup ===
DB_HOST = "db"        # Docker service name for Node.js DB
DB_PORT = 5432
DB_NAME = "smart_dorm_parcel"
DB_USER = "athip"
DB_PASSWORD = "123456"

conn = psycopg2.connect(
    host=DB_HOST,
    port=DB_PORT,
    database=DB_NAME,
    user=DB_USER,
    password=DB_PASSWORD
)
conn.autocommit = True  # for updates

def camera_loop():
    global last_frame
    while True:
        ret, frame = cap.read()
        if ret:
            last_frame = frame

threading.Thread(target=camera_loop, daemon=True).start()

def read_qr_code(frame):
    decoded_text = qreader.detect_and_decode(frame)
    return str(decoded_text[0]) if decoded_text else None

def process_qr(qr_text: str) -> int:
    """
    Returns mapped_label
    -1: QR not in tracking_numbers or user not found
    -2: QR not detected
    >=0: dorm_number of user
    """
    if not qr_text:
        return -2

    with conn.cursor() as cur:
        # 1️⃣ Check if tracking_number exists
        cur.execute("SELECT user_id, status FROM tracking_numbers WHERE tracking_number = %s", (qr_text,))
        row = cur.fetchone()
        if not row:
            return -1

        user_id, status = row
        # 2️⃣ Update status to 'scanned' if not already
        cur.execute("UPDATE tracking_numbers SET status='scanned', updated_at=NOW() WHERE tracking_number=%s", (qr_text,))

        # 3️⃣ Get dorm_number from users table
        cur.execute("SELECT dorm_number FROM users WHERE id=%s", (user_id,))
        dorm_row = cur.fetchone()
        if not dorm_row:
            return -1

        dorm_number = dorm_row[0]
        try:
            return int(dorm_number)
        except ValueError:
            return -1

print("✅ Raspberry Pi QR Serial Server with DB started...")

while True:
    if ser.in_waiting:
        cmd = ser.readline().decode().strip()
        if cmd == "READ_QR":
            if not cap.isOpened():
                response = {"error": "Cannot open camera"}
            elif last_frame is None:
                response = {"error": "Failed to capture image"}
            else:
                qr_text = read_qr_code(last_frame)
                mapped_label = process_qr(qr_text)

                if mapped_label == -2:
                    # Save debug image if QR not detected
                    cv2.imwrite("/app/output/debug_last_frame.jpg", last_frame)
                    print("⚠️ No QR detected → saved debug_last_frame.jpg")

                response = {"qr_text": qr_text or "No QR code detected", "mapped_label": mapped_label}
                print(f"📡 QR: {qr_text}, mapped_label: {mapped_label}")

            ser.write((json.dumps(response) + "\n").encode())
