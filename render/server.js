const express = require("express");
const app = express();
const cors = require("cors");

app.use(express.json());
app.use(cors());

let sensorData = {}; // Store the latest sensor data

// Endpoint to receive sensor data from ESP32
app.post("/update-sensor-data", (req, res) => {
    sensorData = req.body; // Store the received sensor data
    console.log("Received Sensor Data:", sensorData);
    res.json({ message: "Data received successfully!" });
});

// Endpoint to serve sensor data to the frontend
app.get("/get-sensor-data", (req, res) => {
    res.json(sensorData);
});

// Serve static files (optional: for hosting HTML frontend)
app.use(express.static("public"));

// Start the server
const PORT = process.env.PORT || 3000;
app.listen(PORT, () => console.log(`Server running on port ${PORT}`));
