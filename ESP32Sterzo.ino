/*
 * Hook up 2 TTP223 touch pads to a ESP32 board, on pins 25 (left) & 27 (right)
 * Standard settings for ESP32 are used for compile & download
 * Each key-press gives +-10° on the steering angle
 * The steering angle is notified to the BLE client (Zwift) every second, and then reset.
 */

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>


#define STERZO_SERVICE_UUID "347b0001-7635-408b-8918-8ff3949ce592"
//#define CHAR12_UUID         "347b0012-7635-408b-8918-8ff3949ce592" // not needed for Zwift
//#define CHAR13_UUID         "347b0013-7635-408b-8918-8ff3949ce592" // not needed for Zwift
#define CHAR14_UUID         "347b0014-7635-408b-8918-8ff3949ce592"
//#define CHAR19_UUID         "347b0019-7635-408b-8918-8ff3949ce592" // not needed for Zwift
#define CHAR30_UUID         "347b0030-7635-408b-8918-8ff3949ce592"
#define CHAR31_UUID         "347b0031-7635-408b-8918-8ff3949ce592"
#define CHAR32_UUID         "347b0032-7635-408b-8918-8ff3949ce592"

bool deviceConnected = false;
BLECharacteristic *pChar32;
BLECharacteristic *pChar30;
BLE2902 *p2902Char14;
BLE2902 *p2902Char30;
BLE2902 *p2902Char32;
float steeringAngle = 0.0;

bool challengeOK = false;
bool ntf32On = false;

static void bleNotifySteeringAngle() {
  // steeringAngle = float cast in 4 bytes
  if ((deviceConnected) && (challengeOK)) { // ) && (char30.subscribed() -> dit bestaat precies niet voor esp32??
    pChar30->setValue((uint8_t *)&steeringAngle,4);
    pChar30->notify();
    steeringAngle = 0.0;
  }
}

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};

// not needed for Zwift
class char12Callbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    Serial.print("char12EventHandler : ");
    Serial.println(pCharacteristic->getValue().c_str());
  }
};

// if Zwift writes 0x310, we send a challenge on char32
class char31Callbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    std::string val = pCharacteristic->getValue();
    Serial.print("char31EventHandler : ");
    for (int i=0;i<4;i++){
      Serial.print(val[i],HEX);
      Serial.print('-');
    }
    Serial.println();
    
    if ((val[0] == 0x03) && (val[1] == 0x10)) {
      uint8_t challenge[] = {0x03,0x10,0x4a,0x89};//{0x03,0x10,0x12,0x34};
      Serial.println("char31EventHandler : got 0x310!");
      pChar32->setValue(challenge,4);
      pChar32->indicate();
    }
    else if ((val[0] == 0x03) && (val[1] == 0x11)) {
      unsigned char fakeData[] = {0x03,0x11,0xff, 0xff};
      Serial.println("char31EventHandler : got 0x311!");
      challengeOK = true;
      pChar32->setValue(fakeData,4);
      pChar32->indicate();
    }
  } // onWrite
};

class char32NtfCallbacks:public BLEDescriptorCallbacks {
  void onWrite(BLEDescriptor* pDescriptor) {
    ntf32On = true;
  }
};

void setup() {
  Serial.begin(115200);
  Serial.println("Starting Sterzo!");
  pinMode(27,INPUT);
  pinMode(25,INPUT);

  BLEDevice::init("stefaan-sterzo");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pSvcSterzo = pServer->createService("347b0001-7635-408b-8918-8ff3949ce592");
  BLECharacteristic *pChar14 = pSvcSterzo->createCharacteristic(CHAR14_UUID,BLECharacteristic::PROPERTY_NOTIFY); // 1 byte 0xFF, doet niets
  pChar30 = pSvcSterzo->createCharacteristic(CHAR30_UUID,BLECharacteristic::PROPERTY_NOTIFY); // 4 bytes steering angle
  BLECharacteristic *pChar31 = pSvcSterzo->createCharacteristic(CHAR31_UUID,BLECharacteristic::PROPERTY_WRITE); // zwift writes 4 bytes
  pChar32 = pSvcSterzo->createCharacteristic(CHAR32_UUID,BLECharacteristic::PROPERTY_INDICATE); // 4 bytes challenge

  p2902Char14 = new BLE2902();
  p2902Char30 = new BLE2902();
  p2902Char32 = new BLE2902();
  p2902Char32->setCallbacks(new char32NtfCallbacks());
  pChar14->addDescriptor(p2902Char14);
  pChar30->addDescriptor(p2902Char30);
  pChar32->addDescriptor(p2902Char32);

  // initial values
  uint8_t defaultValue[4] = {0x0,0x0,0x0,0x0};
  pChar30->setValue(defaultValue,4); // default angle = 0
  defaultValue[0] = 0xFF; // fill other characteristics with a default 0xFF
  pChar14->setValue(defaultValue,1);
  pChar31->setValue(defaultValue,1);
  uint8_t challenge[] = {0x03,0x10,0x4a, 0x89};
  pChar32->setValue(challenge,4);

  pChar31->setCallbacks(new char31Callbacks());
  pChar32->setCallbacks(new char31Callbacks());

  pSvcSterzo->start();
  // BLEAdvertising *pAdvertising = pServer->getAdvertising();  // this still is working for backward compatibility
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(STERZO_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println("Sterzo ready!");
}

uint32_t bleNotifyMillis;
bool buttonTaken = false;

void loop() {
  bool buttonLeft = digitalRead(27);
  bool buttonRight = digitalRead(25);
  // TTP223 doesn't need debouncing

  if (!buttonLeft && !buttonRight) buttonTaken = false; // listen for new key
  if (buttonLeft && !buttonTaken) {
    steeringAngle -= 10;
    if (steeringAngle < -40) steeringAngle = -40;
    buttonTaken = true;
    Serial.print("steering angle: ");Serial.println(steeringAngle);
  }
  if( buttonRight && !buttonTaken) {
    steeringAngle += 10;
    if (steeringAngle > 40) steeringAngle = 40;
    buttonTaken = true;
    Serial.print("steering angle: ");Serial.println(steeringAngle);
  }
  // steering angle will be reset after each BLE-notify

  // if Zwift subscribes to the notifications, we send the initial challenge on char32
  if ((ntf32On) && (deviceConnected)) {
    ntf32On = false;
    uint8_t challenge[] = {0x03,0x10,0x4a, 0x89};
    pChar32->setValue(challenge,4);
    pChar32->indicate();
  }
  
  if (deviceConnected) {
    // notify steering angle every second
    if (millis() - bleNotifyMillis > 1000) {
      bleNotifySteeringAngle ();
      bleNotifyMillis = millis();
    }
  }
} // loop
