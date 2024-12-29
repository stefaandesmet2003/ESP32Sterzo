/*
 * Hook up 2 TTP223 touch pads to a ESP32 board, on pins 25 (left) & 27 (right)
 * Standard settings for ESP32 are used for compile & download
 * Each key-press gives +-10° on the steering angle
 * The steering angle is notified to the BLE client (Zwift) every second, and then reset.

 * char 30  : angle notifications
 * char 31  : ZWIFT -> STERZO
 * char 32 : STERZO -> ZWIFT
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

BLEServer *pServer;
BLEService *pSvcSterzo;
BLECharacteristic *pChar14;
BLECharacteristic *pChar30;
BLECharacteristic *pChar31;
BLECharacteristic *pChar32;
BLE2902 *p2902Char14;
BLE2902 *p2902Char30;
BLE2902 *p2902Char32;

bool deviceConnected = false;
bool challengeOK = false;
bool ind32On = false;
float steeringAngle = 0.0;

static uint32_t rotate_left32 (uint32_t value, uint32_t count) {
    const uint32_t mask = (CHAR_BIT * sizeof (value)) - 1;
    count &= mask;
    return (value << count) | (value >> (-count & mask));
}

static uint32_t hashed(uint64_t seed) {
    uint32_t ret = (seed + 0x16fa5717);
    uint64_t rax = seed * 0xba2e8ba3;
    uint64_t eax = (rax >> 35) * 0xb;
    uint64_t ecx = seed - eax;
    uint32_t edx = rotate_left32(seed, ecx & 0x0F);
    ret ^= edx;
    return ret;
}

static void bleNotifySteeringAngle() {
  // steeringAngle = float cast in 4 bytes
  if (1 /*char30.subscribed()*/) { //-> dit bestaat precies niet voor esp32??
    pChar30->setValue((uint8_t *)&steeringAngle,4);
    pChar30->notify();
    Serial.print("ntf angle ");Serial.println(steeringAngle);
    steeringAngle = 0.0;
  }
}

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("a device connected!");
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("lost connection!");
    }
};

// if Zwift writes 0x310, we send a challenge on char32
class char31Callbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    String val = pCharacteristic->getValue();
    Serial.print("char31EventHandler : ");
    for (int i=0;i<val.length();i++){
      Serial.print(val[i],HEX);
      Serial.print('-');
    }
    Serial.println();
    
    if ((val[0] == 0x03) && (val[1] == 0x10)) {
      uint8_t challenge[] = {0x03,0x10,0x12,0x34}; //<2024 {0x03,0x10,0x4a,0x89}
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

		else if ((val[0] == 0x03) && (val[1] == 0x12)) {
			uint32_t seed = val[5] << 24;
			seed |= val[4] << 16;
			seed |= val[3] << 8;
			seed |= val[2];
			uint32_t password = hashed(seed);
      Serial.print("char31EventHandler : got 0x312! seed = ");Serial.println(seed,HEX);

			uint8_t response[6];
			response[0] = 0x03;
			response[1] = 0x12;
			response[5] = (password & 0xFF000000 ) >> 24;
			response[4] = (password & 0x00FF0000 ) >> 16;
			response[3] = (password & 0x0000FF00 ) >> 8;
			response[2] = (password & 0x000000FF );
      pChar32->setValue(response,6);
      pChar32->indicate();

		} 
    else if ((val[0] == 0x03) && (val[1] == 0x13)) {
      Serial.println("char31EventHandler : got 0x313!");
			uint8_t response[3];
			response[0] = 0x03;
			response[1] = 0x13;
			response[2] = 0xFF;
      pChar32->setValue(response,3);
      pChar32->indicate();
      challengeOK = true; // BLE_CUS_START_SENDING_STEERING_DATA
    }
  } // onWrite

  void onRead(BLECharacteristic* pCharacteristic) {
    Serial.println("char31EventHandler onRead, doing nothing!");
  }
}; // char31Callbacks

// dummy for test to see if zwift comes here
class char32Callbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    Serial.println("char32EventHandler onWrite, doing nothing!");
  }

  void onRead(BLECharacteristic* pCharacteristic) {
    Serial.println("char32EventHandler onRead, doing nothing!");
  }
}; // char32Callbacks

class char32Desc2902Callbacks:public BLEDescriptorCallbacks {
  void onWrite(BLEDescriptor* pDescriptor) {
    uint8_t *value = pDescriptor->getValue();
    //size_t descLength = Descriptor->getLength();
    Serial.print("char32 desc 2902 written : ");Serial.println(value[0]);
    ind32On = true;
  }
};

void setup() {
  Serial.begin(115200);
  Serial.println("Starting Sterzo!");
  pinMode(27,INPUT);
  pinMode(25,INPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  BLEDevice::init("STERZO");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  //pSvcSterzo = pServer->createService("347b0001-7635-408b-8918-8ff3949ce592");
  pSvcSterzo = pServer->createService(BLEUUID(STERZO_SERVICE_UUID));
  pChar14 = pSvcSterzo->createCharacteristic(CHAR14_UUID,BLECharacteristic::PROPERTY_NOTIFY); // 1 byte 0xFF, doet niets
  pChar30 = pSvcSterzo->createCharacteristic(CHAR30_UUID,BLECharacteristic::PROPERTY_NOTIFY); // 4 bytes steering angle
  pChar31 = pSvcSterzo->createCharacteristic(CHAR31_UUID,BLECharacteristic::PROPERTY_WRITE); // zwift writes 4 bytes
  pChar32 = pSvcSterzo->createCharacteristic(CHAR32_UUID,BLECharacteristic::PROPERTY_INDICATE); // 4 bytes challenge

  p2902Char14 = new BLE2902();
  p2902Char30 = new BLE2902();
  p2902Char30->setNotifications(true);
  p2902Char32 = new BLE2902();
  p2902Char32->setCallbacks(new char32Desc2902Callbacks());
  pChar14->addDescriptor(p2902Char14);
  pChar30->addDescriptor(p2902Char30);
  pChar32->addDescriptor(p2902Char32);

  // initial values
  uint8_t defaultValue[4] = {0x0,0x0,0x0,0x0};
  pChar30->setValue(defaultValue,4); // default angle = 0
  defaultValue[0] = 0xFF; // fill other characteristics with a default 0xFF
  pChar14->setValue(defaultValue,1);
  pChar31->setValue(defaultValue,4);
  uint8_t challenge[] = {0x03,0x10,0x12, 0x34}; //<2024 {0x03,0x10,0x4a, 0x89};
  pChar32->setValue(challenge,4);

  pChar31->setCallbacks(new char31Callbacks());
  pChar32->setCallbacks(new char32Callbacks()); // 2024 for test only TODO REMOVE

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
uint32_t challengeMillis;
uint32_t blinkMillis;
bool buttonTaken = false;

void loop() {

  uint32_t blinkInterval;
  bool buttonLeft;
  bool buttonRight;

  // blink
  blinkInterval = 1000;
  if (deviceConnected) {
    blinkInterval = 500;
  }
  if ((millis() - blinkMillis) > blinkInterval) {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    blinkMillis = millis();
  }

  buttonLeft = digitalRead(27);
  buttonRight = digitalRead(25);
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

  /*
  // alt : manually activate char32 indications, not waiting for zwift to do this
  if (deviceConnected && (!p2902Char32->getIndications())) {
    Serial.println("manually enabling char32 indications");
    p2902Char32->setIndications(true);
    challengeMillis = millis();
    ind32On = true;
  }
  */

  // if Zwift subscribes to the notifications, we send the initial challenge on char32
  // 2024 : put in a retry every 5 seconds until challengeOK, but zwift doesn't seem to send anything on char 31
  if (deviceConnected && ind32On && !challengeOK && ((millis() - challengeMillis) > 5000)) { 
    challengeMillis = millis();
    Serial.println("sending initial challenge on char32");
    uint8_t challenge[] = {0x03,0x10,0x12,0x34}; //<2024 {0x03,0x10,0x4a, 0x89};
    pChar32->setValue(challenge,4);
    pChar32->indicate();
  }

  // challengeOK remains false because zwift doesn't seem to send anything on char 31
  // so for workaround we start sending steering angles as soon as zwift connects, and strangely enough that works
  //if (deviceConnected && challengeOK) { 
  if (deviceConnected) {
    if (millis() - bleNotifyMillis > 1000) { // notify steering angle every second
      bleNotifySteeringAngle ();
      bleNotifyMillis = millis();
    }
  }
} // loop
