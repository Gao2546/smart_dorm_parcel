from flask import Flask, jsonify
import cv2
from qreader import QReader

app = Flask(__name__)

IMAGE_WIDTH = 640
IMAGE_HEIGHT = 480
qreader = QReader(model_size='n')

def read_qr_code(frame):
    decoded_text = qreader.detect_and_decode(frame)
    return str(decoded_text[0]) if decoded_text else None

@app.route("/readQR", methods=["POST"])
def read_qr():
    cap = cv2.VideoCapture(0)
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, IMAGE_WIDTH)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, IMAGE_HEIGHT)

    if not cap.isOpened():
        return jsonify({"error": "Cannot open camera"}), 500

    ret, frame = cap.read()
    cap.release()

    if not ret:
        return jsonify({"error": "Failed to capture image"}), 500

    qr_text = read_qr_code(frame) or "No QR code detected"

    return jsonify({
        "qr_text": qr_text
    }), 200

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True)
