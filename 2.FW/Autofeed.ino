// Arduino cloud 的物联网函数，有 key 及 WiFi 用户名密码等
#include "arduino_secrets.h"
#include "thingProperties.h"
// 定义引脚
#define pwm1Pin 12
#define pwm2Pin 14
#define keyPin 13

bool motorRunning = false;

void setup() {
  // Initialize serial and wait for port to open:
  Serial.begin(9600);
  // This delay gives the chance to wait for a Serial Monitor without blocking if none is found
  delay(1500); 
  pinMode(pwm1Pin, OUTPUT);
  pinMode(pwm2Pin, OUTPUT);
  pinMode(keyPin, INPUT_PULLUP);
  analogWrite(pwm1Pin, 0);
  digitalWrite(pwm2Pin, LOW);
  
  // Defined in thingProperties.h
  initProperties();

  // Connect to Arduino IoT Cloud
  ArduinoCloud.begin(ArduinoIoTPreferredConnection);
  
  /*
     The following function allows you to obtain more information
     related to the state of network and IoT Cloud connection and errors
     the higher number the more granular information you’ll get.
     The default is 0 (only errors).
     Maximum is 4
 */
  setDebugMessageLevel(2);
  ArduinoCloud.printDebugInfo();
}

void loop() {
  ArduinoCloud.update();
  // 按键喂食，按一下电机开始转动
  // 读取 key 状态并进行去抖动处理
  int keyState = digitalRead(keyPin);
  delay(50);
  keyState = digitalRead(keyPin);

  //检测到 key 信号为低电平并且电机未转动时，驱动电机转动 10s
  if (keyState == LOW && motorRunning == false) {
    analogWrite(pwm1Pin, 200); // 设置电机顺时针转动
    digitalWrite(pwm2Pin, LOW);
    
    //记录当前时间电机状态为运行
    motorRunning = true;

    delay(10000);
  }else {
    analogWrite(pwm1Pin, 0); // 停止电机
    digitalWrite(pwm2Pin, LOW);
    // 设置电机状态为停止
    motorRunning = false;
  }
}

/*
  Since Pushbutton is READ_WRITE variable, onPushbuttonChange() is
  executed every time a new value is received from IoT Cloud.
*/
void onPushbuttonChange()  {
  // 当物联网按键触发时，电机转动，speed 速度可以在 app 上调整 0-255
    if (pushbutton == true && motorRunning == false) {
    analogWrite(pwm1Pin, speed); // 设置电机顺时针转动
    digitalWrite(pwm2Pin, LOW);

    //记录当前时间电机状态为运行
    motorRunning = true;

    delay(10000);
  } else {
    analogWrite(pwm1Pin, 0); // 停止电机
    digitalWrite(pwm2Pin, LOW);
    // 设置电机状态为停止
    motorRunning = false;
  }
}

/*
  Since Speed is READ_WRITE variable, onSpeedChange() is
  executed every time a new value is received from IoT Cloud.
*/
void onSpeedChange()  {
  // Add your code here to act upon Speed change
}