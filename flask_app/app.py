import numpy as np
import tensorflow.lite as tflite
from flask import Flask, request, jsonify
import json
import os

app = Flask(__name__)

# Constants
MODEL_PATH = "cnn_lstm_model.tflite"  # Your trained model
MEAN_STD_PATH = "mean_std.npy"        # Normalization parameters
LABEL_ENCODER_PATH = "label_encoder.pkl"  # For decoding predictions

# Load assets once at startup
try:
    print("🟢 Initializing server...")

    # 1. Load TFLite model
    print(f"🔍 Loading model from: {MODEL_PATH}")
    interpreter = tflite.Interpreter(model_path=MODEL_PATH)
    interpreter.allocate_tensors()

    # Get input/output details
    input_details = interpreter.get_input_details()
    output_details = interpreter.get_output_details()
    expected_shape = input_details[0]['shape']  # [1, 100, 6]

    print(f"✅ Model loaded. Expected input shape: {expected_shape}")

    # 2. Load normalization parameters
    mean_std = np.load(MEAN_STD_PATH)
    mean, std = mean_std[0], mean_std[1]
    print(f"📊 Normalization - Mean: {mean}, Std: {std}")

    # 3. Load label encoder
    import joblib
    label_encoder = joblib.load(LABEL_ENCODER_PATH)
    class_names = label_encoder.classes_
    print(f"🏷️ Label classes: {class_names}")

except Exception as e:
    print(f"❌ Initialization failed: {str(e)}")
    exit()


@app.route('/predict', methods=['POST'])
def predict():
    try:
        # 1. Get and validate input
        data = request.get_json()
        if not data or "sensor_data" not in data:
            return jsonify({"error": "Expected 'sensor_data' array in JSON"}), 400

        raw_data = np.array(data["sensor_data"], dtype=np.float32)


        # 2. Check input shape (ESP32 should send [100,6] data)
        if raw_data.shape != (100, 6):
            return jsonify({
                "error": f"Invalid shape. Expected (100, 6), got {raw_data.shape}",
                "hint": "Send exactly 100 timesteps of [acc_x,acc_y,acc_z,gyro_x,gyro_y,gyro_z]"
            }), 400

        # 3. Normalize data (using saved mean/std)
        normalized_data = ((raw_data - mean) / std).astype(np.float32)
        input_data = normalized_data.reshape(1, 100, 6)



        # 5. Run inference
        interpreter.set_tensor(input_details[0]['index'], input_data)
        interpreter.invoke()
        probabilities = interpreter.get_tensor(output_details[0]['index'])[0]

        # 6. Get predicted class and confidence
        predicted_class_idx = np.argmax(probabilities)
        predicted_class = class_names[predicted_class_idx]
        confidence = float(probabilities[predicted_class_idx])

        # 🔊 Print prediction to console
        print(f"📢 Predicted Activity: {predicted_class}, Confidence: {confidence:.2f}")

        return jsonify({
            "prediction": predicted_class,
            "confidence": confidence,
            "all_probabilities": {cls: float(prob) for cls, prob in zip(class_names, probabilities)}
        })

    except Exception as e:
        return jsonify({"error": str(e)}), 500


if __name__ == "__main__":
    print("🔥 Server ready at http://0.0.0.0:5000")
    app.run(host="0.0.0.0", port=5000)
