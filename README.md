# Real-Time Transit Tracking (RTTT)

An end-to-end IoT + AI system for real-time passenger counting and demand forecasting in public transportation.

## 🚀 Overview
RTTT is a low-cost, privacy-preserving transit analytics system designed to solve overcrowding and inefficiency in urban bus networks. It combines a custom hardware prototype with intelligent data processing and a scalable predictive modeling pipeline.

The system uses sensor fusion (pressure mat + IR sensors) powered by an ESP32 microcontroller to accurately detect passenger boarding and alighting events in real time.

## 🧠 Key Features
- 📊 Real-time passenger counting using sensor fusion (Pressure Mat + IR)
- 🔒 Privacy-preserving (no cameras, no personal data)
- ⚡ Lightweight, low-cost hardware (~₹2500 per unit)
- 🌐 Live data transmission via ESP32 Wi-Fi
- 📍 GPS-based location tagging
- 📈 Future-ready hybrid forecasting model (SARIMA + LSTM)

## 🏗️ System Architecture
1. **Sensor Layer**
   - Velostat-based pressure mat
   - IR break-beam sensors for direction detection

2. **Edge Processing**
   - ESP32 microcontroller
   - Sensor fusion algorithm + state machine
   - Noise filtering and debouncing

3. **Data Pipeline**
   - JSON-based event transmission
   - Cloud ingestion (Firebase / backend)
   - Feature engineering (time, weather, events)

4. **Predictive Engine (Planned)**
   - SARIMA for seasonal patterns
   - LSTM for nonlinear residuals
   - 24-hour occupancy forecasting

## 📊 Performance
- ✅ Overall accuracy: **90.4%**
- 🚶 Single-file accuracy: **~98%**
- ⚠️ Crowded scenarios: **~70.8%** (known trade-off)

## 🧪 Validation
Tested across multiple real-world scenarios:
- Single passenger flow
- High traffic streams
- Crowded entries
- Luggage interference
- Direction changes

## 📡 Example Data Payload
```json
{
  "timestamp": "2025-05-07T13:45:08Z",
  "bus_id": "PMPML_45",
  "latitude": 18.5000,
  "longitude": 73.8591,
  "event_type": "boarding",
  "current_occupancy": 34,
  "is_crowded_event": false
}
