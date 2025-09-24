from flask import Flask, jsonify
import cv2
# from qreader import QReader
from pyzbar.pyzbar import decode
import threading

app = Flask(__name__)

IMAGE_WIDTH = 640//2
IMAGE_HEIGHT = 480//2
# qreader = QReader(model_size='n')


cap = cv2.VideoCapture(2)
cap.set(cv2.CAP_PROP_FRAME_WIDTH, IMAGE_WIDTH)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, IMAGE_HEIGHT)

last_frame = None


def camera_loop():
    global last_frame
    while True:
        ret, frame = cap.read()
        if ret:
            last_frame = frame

threading.Thread(target=camera_loop, daemon=True).start()

# def read_qr_code(frame):
#     decoded_text = qreader.detect_and_decode(frame)
#     return str(decoded_text[0]) if decoded_text else None

def read_qr_code(frame):
    decoded = decode(frame)
    return decoded[0].data.decode("utf-8") if decoded else None

@app.route("/readQR", methods=["POST"])
def read_qr():

    if not cap.isOpened():
        return jsonify({"error": "Cannot open camera"}), 500

    if last_frame is None:
        return jsonify({"error": "Failed to capture image"}), 500

    qr_text = read_qr_code(last_frame) or "No QR code detected"

    return jsonify({
        "qr_text": qr_text
    }), 200

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000)
