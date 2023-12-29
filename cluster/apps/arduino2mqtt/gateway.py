import serial
import time
import paho.mqtt.client as mqtt
import logging

print('Starting...')

# Indique si le client MQTT est connecté ou non
connected_flag = False

# Configuration de l'adresse IP et du port du broker MQTT
mqtt_broker_ip = "192.168.1.21"
mqtt_broker_port = 1883

# Configuration du port serie de l'Arduino Uno
ser = serial.Serial('/dev/ttyACM1', 9800, timeout=10)
time.sleep(2)

mqtt_topic_sonnette = "homeassistant/switch/sonnette/state"
mqtt_topic_ouverture_portail_totale = "homeassistant/switch/portail-ouverture-totale/state"
mqtt_topic_ouverture_portail_pieton = "homeassistant/switch/portail-ouverture-pieton/state"

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        connected_flag = True
        print("MQTT client connected")
        mqtt_client.on_message=on_message
        mqtt_client.subscribe(mqtt_topic_ouverture_portail_totale)
        mqtt_client.subscribe(mqtt_topic_ouverture_portail_pieton)
    else:
        print("Bad connection with eturned code ", rc)

def on_disconnect(client, userdata, rc):
    logging.info("MQTT client disconnected with code " + str(rc))
    connected_flag = False

def on_message(client, userdata, message):
    state = str(message.payload.decode("utf-8"))
    if message.topic == mqtt_topic_ouverture_portail_totale and state == "ON":
        print("Commande ouverture portail totale ", state)
        ser.write(bytearray([2]))
        mqtt_client.publish(mqtt_topic_ouverture_portail_totale, "OFF", retain=True)
    elif message.topic == mqtt_topic_ouverture_portail_pieton and state == "ON":
        print("Commande ouverture portail pieton ", state)
        ser.write(bytearray([3]))
        mqtt_client.publish(mqtt_topic_ouverture_portail_pieton, "OFF", retain=True)

mqtt_client = mqtt.Client(client_id="piGatway")
mqtt_client.on_connect = on_connect
mqtt_client.on_disconnect = on_disconnect
mqtt_client.loop_start()
mqtt_client.connect(mqtt_broker_ip, mqtt_broker_port)

while True:
    incomingByte = ser.read()
    if incomingByte == b'1':
        print("Publication du message de déclenchement de sonnette dans MQTT")
        mqtt_client.publish(mqtt_topic_sonnette, "ON", retain=True)
        time.sleep(5)
        mqtt_client.publish(mqtt_topic_sonnette, "OFF", retain=True)
