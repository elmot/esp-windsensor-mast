#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

#define UART_BAUD 9600
#define bufferSize 8192
#define CONNECTING_LED 2
#define WARNING_LED 0

#define NO_WARNINGS "$PEWWT,NONE"
#define NO_WARNINGS_PREFIX_LEN 7
#define NO_WARNINGS_LEN 11

const char *ssid = "yanus-wind";  // Your ROUTER SSID
const char *pw = "EglaisEglais"; // and WiFi PASSWORD
const int port = 9000;

WiFiUDP udp;


uint8_t buffer[bufferSize];
uint16_t i1=0;

void setup() {
    pinMode(WARNING_LED, OUTPUT);
    pinMode(CONNECTING_LED, OUTPUT);
    digitalWrite(CONNECTING_LED, 1);

    delay(500);

    Serial.begin(UART_BAUD);

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, pw);
    Serial.println("Starting UDP Server");
    udp.begin(port); // start UDP server
}


void loop() {

    if (WiFi.status() == WL_CONNECTED) {
        digitalWrite(CONNECTING_LED, 0);
    } else {
        digitalWrite(CONNECTING_LED, 1);
    }
    int packetSize = udp.parsePacket();
    if(packetSize>0) {
        udp.read(buffer, bufferSize);
        // now send to UART:
        Serial.write(buffer, packetSize);
        if(memcmp(buffer,NO_WARNINGS,NO_WARNINGS_PREFIX_LEN) == 0) {
            if(memcmp(buffer,NO_WARNINGS,NO_WARNINGS_LEN) == 0) {
                digitalWrite(WARNING_LED, 0);
            } else {
                digitalWrite(WARNING_LED, 1);
            }
        }
    }
}
