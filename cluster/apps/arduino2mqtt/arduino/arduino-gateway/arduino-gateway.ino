
const int analogInPin = A0;
const int pinSwitch1 = 2;
const int pinSwitch2 = 3;
const int pinSwitch3 = 4;
const int pinSwitch4 = 5;

const int ringInputTriggerLevel = 30;

const int idSwitch1 = 2;
const int idSwitch2 = 3;
const int idSwitch3 = 4;
const int idSwitch4 = 5;

int sensorValue = 0;

void setup() {
  Serial.begin(9600);
  
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(pinSwitch1, OUTPUT);
  pinMode(pinSwitch2, OUTPUT);
  pinMode(pinSwitch3, OUTPUT);
  pinMode(pinSwitch4, OUTPUT);
  
  digitalWrite(pinSwitch1, LOW);
  digitalWrite(pinSwitch2, LOW);
  digitalWrite(pinSwitch3, LOW);
  digitalWrite(pinSwitch4, LOW);
}

void loop() {
  
  sensorValue = analogRead(analogInPin);

  if (sensorValue >= ringInputTriggerLevel) {
    triggerRing();
  } else {
    delay(10);
  }
  
  if (Serial.available() > 0) {
    
    int incomingByteFromSerial = Serial.read();
    
    switch (incomingByteFromSerial) {
      case idSwitch1:
        toggleSwitch(pinSwitch1);
        break;
      case idSwitch2:
        toggleSwitch(pinSwitch2);
        break;
      case idSwitch3:
        toggleSwitch(pinSwitch3);
        break;
      case idSwitch4:
        toggleSwitch(pinSwitch4);
        break;
    }
  }

}

void triggerRing() {
  digitalWrite(LED_BUILTIN, HIGH);
  Serial.print("1");
  delay(5000);
}

void toggleSwitch(int pinSwitch) {
  digitalWrite(pinSwitch, HIGH);
  delay(500);
  digitalWrite(pinSwitch, LOW);
}
