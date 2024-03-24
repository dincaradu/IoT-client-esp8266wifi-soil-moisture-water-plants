# IoT ESP8266 Client - Water the flowers

Example sketch built for ESP8266 board, with soil moisture sensor and water pump. It also connects to a server through WebSockets and sends the soil moisture measurements, as well as an update everytime the pump is turned on.


## Functionalities

- You can upload a new sketch Over The Air
- Acts as a WiFi Access Point for setting up WiFi credentials through a webpage accessible at 10.1.1.1
- Connects to the WiFi network
- Connects through WebSockets and emits a keep_alive event to a server
  (Please note that it uses WebSockets v2 and is not compatible with the more recent versions 3 or 4. Sample server code here: https://github.com/dincaradu/IoT-server-nestjs)
- Reads the data from the soil moisture sensor
- Turns on the water pump everytime the soil moisture drops below 50%

## Board, Sensors, and other attachments

- ESP8266WiFi
- MH Soil Moisture sensor
- 3-6v water pump


## Other required components

- 2N2222 transistor
- a bunch of wires :))

Hope it helps! Have fun!
