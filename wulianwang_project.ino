#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <MQUnifiedsensor.h>


// WiFi 配置信息
const char* ssid = "esp32";   
const char* password = "12345678"; 

// LeanCloud 数据库表 URL
const char* serverName = "https://fyr9doty.lc-cn-n1-shared.com/1.1/classes/tem_and_hum";  
const char* serverName_LED = "https://YOUR_APP_ID.lc-cn-n1-shared.com/1.1/classes/LED_Control";

// LeanCloud App ID 和 App Key
const char* appID = "FYr9DoTYGGf1jB1zyj2eiaf5-gzGzoHsz";
const char* appKey = "cwskAtC4QndeILToat9a2Qti";

// 定义 DHT11 传感器连接的 GPIO 引脚
#define DHTPIN 23  
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// MQ-135宏定义
#define placa "ESP32"
#define Voltage_Resolution 3.3      // ESP32 ADC参考电压为3.3V
#define ADC_Bit_Resolution 12       // ESP32 ADC分辨率（0～4095）
#define sensorPin 34                // MQ-135模块模拟输出连接到ESP32的GPIO34
#define type "MQ-135"               // 传感器型号
#define RatioMQ135CleanAir 3.6      // 在洁净空气条件下，MQ-135的RS/R0典型值约为3.6

// 声明MQ-135传感器对象
MQUnifiedsensor MQ135(placa, Voltage_Resolution, ADC_Bit_Resolution, sensorPin, type);

//定义板载LED引脚
#define LEDPIN 2  

// 创建本地Web 服务器，监听端口 80
AsyncWebServer server(80);

// 计时器变量
unsigned long lastLocalPrintTime = 0;
unsigned long lastUploadTime = 0;
unsigned long lastDeleteTime = 0;
unsigned long lastprintTime_smoke = 0;

//初始化函数
void setup() {
  Serial.begin(115200);  // 启动串口通讯
  Serial.println("DHT11 温湿度传感器初始化...");
  dht.begin();  // 初始化 DHT11 传感器

  //板载led初始化
  pinMode(LEDPIN, OUTPUT);
  digitalWrite(LEDPIN, LOW); // 初始状态关闭 LED

  WiFi.begin(ssid, password);// 连接 WiFi
  while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("正在连接 WiFi...");
    }
  Serial.println("\n✅ WiFi 连接成功！");
  Serial.print("IP 地址: ");
  Serial.println(WiFi.localIP());

   // 封装的传感器初始化与校准
  initMQ135();

//后端执行
  readAndPrintData();
  uploadDataToLeanCloud();
  int totalcount=checkDatabase();
  if(totalcount>50){ deleteExtraData();}
  else  {Serial.println("【数据库数据正常】:已保留最新数据。");}
  
  // 启动 Web 服务器
  startWebServer();
}

void loop() {
  
    ledControl();//远程控制LED

    // 每 1 分钟读取并打印温湿度数据
    if (isTimeToRun(lastLocalPrintTime, 60000)) {
        readAndPrintData();
    }

    // 每 5 分钟上传一次数据到 LeanCloud
    if (isTimeToRun(lastUploadTime, 60000)) {
        uploadDataToLeanCloud();
    }

    //每30分钟清除旧数据
    if (isTimeToRun(lastDeleteTime, 600000)) {
        int totalcount=checkDatabase();
        if(totalcount>50){ deleteExtraData();}
        else  {Serial.println("【数据库数据正常】:已保留最新数据。");}
    }

    // 每 30s 上传一次数据到 LeanCloud
    if (isTimeToRun(lastprintTime_smoke, 30000)) {
        readSmoke();
    }

}

//查看数据库的温湿度数据条
int checkDatabase() {
  HTTPClient http;
  int totalCount;
  // 拼接查询URL，使用 ?count=1&limit=0 直接获取数据库记录总数
  String queryUrl = String(serverName) + "?count=1&limit=0";
  http.begin(queryUrl);
  http.addHeader("X-LC-Id", appID);
  http.addHeader("X-LC-Key", appKey);

  int httpResponseCode = http.GET();
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("【响应数据】：" + response);
    
    // 从响应中解析 count 字段
    int countIndex = response.indexOf("\"count\":");
    if (countIndex != -1) {
      int startCount = countIndex + 8;  // 跳过 "count": 这8个字符
      int endCount = response.indexOf(",", startCount);
      if (endCount == -1) {
        endCount = response.indexOf("}", startCount);
      }
      String countStr = response.substring(startCount, endCount);
      countStr.trim();  // 去除空格
      totalCount = countStr.toInt();
      Serial.print("【总记录数】：");
      Serial.println(totalCount);
    }
  }
  return totalCount;
}

// 上传数据到 LeanCloud
void uploadDataToLeanCloud() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("❌ WiFi 未连接，无法上传数据！");
        return;
    }

    float humidity = dht.readHumidity();
    float temperature = dht.readTemperature();

    if (isnan(humidity) || isnan(temperature)) {
        Serial.println("❌ 读取 DHT 传感器失败，数据不上传！");
        return;
    }

    HTTPClient http;
    http.begin(serverName);
    http.addHeader("X-LC-Id", appID);
    http.addHeader("X-LC-Key", appKey);
    http.addHeader("Content-Type", "application/json");

    String postData = "{\"temperature\": " + String(temperature) + ", \"humidity\": " + String(humidity) + "}";
    int httpResponseCode = http.POST(postData);

    if (httpResponseCode > 0) {
        Serial.print("✅ 数据上传成功 (HTTP ");
        Serial.print(httpResponseCode);
        Serial.println(")");

        String payload = http.getString();
        Serial.println("📄 返回数据: " + payload);

    } else {
        Serial.print("❌ 上传失败，错误码: ");
        Serial.println(httpResponseCode);
    }

    http.end();
}
// 判断是否到达执行时间
bool isTimeToRun(unsigned long &lastTime, unsigned long interval) {
    unsigned long currentMillis = millis();
    if (currentMillis - lastTime >= interval) {
        lastTime = currentMillis; // 更新上次执行时间
        return true;
    }
    return false;
}
//串口你显示传感器信息
void readAndPrintData() {
    float humidity = dht.readHumidity();
    float temperature = dht.readTemperature();

    if (isnan(humidity) || isnan(temperature)) {
        Serial.println("❌ 读取 DHT 传感器失败，请检查连接！");
        return;
    }

    Serial.print("🌡 温度: ");
    Serial.print(temperature);
    Serial.print(" °C  ");
    
    Serial.print("💧 湿度: ");
    Serial.print(humidity);
    Serial.println(" %");
}
//启动 Web 服务器
void startWebServer() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        float temperature = dht.readTemperature(); // 获取温度
        float humidity = dht.readHumidity();      // 获取湿度

        // 检查传感器数据是否有效
        if (isnan(temperature) || isnan(humidity)) {
            request->send(500, "text/plain", "读取传感器失败!");
            return;
        }

        // 生成 HTML 页面
        String html = "<html><head><meta charset='UTF-8'><meta http-equiv='refresh' content='5'></head><body>";
        html += "<h2>🌡 ESP32 温湿度监测</h2>";
        html += "<p>温度: <b>" + String(temperature) + " °C</b></p>";
        html += "<p>湿度: <b>" + String(humidity) + " %</b></p>";
        html += "</body></html>";

        request->send(200, "text/html", html);
    });

    // 启动 Web 服务器
    server.begin();
}
// 删除多余数据，仅保留最新的50条数据
void deleteExtraData() {
  HTTPClient http;
  // 查询多余数据：按创建时间降序排列，跳过前50条数据
  String url = String(serverName) + "?order=-createdAt&skip=50";
  http.begin(url);
  http.addHeader("X-LC-Id", appID);
  http.addHeader("X-LC-Key", appKey);
  
  int httpResponseCode = http.GET();
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("【多余记录JSON】: " + response);
    
    // 解析返回的 JSON 中所有 objectId 字段
    int idStart = 0;
    int idEnd = 0;
    while ((idStart = response.indexOf("\"objectId\":\"", idStart)) != -1) {
      idStart += 12; // 跳过 "objectId":" 这12个字符，指向实际的 objectId 开始位置
      idEnd = response.indexOf("\"", idStart);  // 查找 objectId 结束位置（下一个双引号）
      if (idEnd != -1) {
        String extraId = response.substring(idStart, idEnd);  // 提取 objectId
        Serial.println("【待删除记录的objectId】: " + extraId);
        // 调用删除函数删除此记录
        deleteRecord(extraId);
      }
      // 更新 idStart 为 idEnd + 1，继续查找下一个 objectId
      idStart = idEnd + 1;
    }
  } 
  else {
    Serial.print("【错误】：获取多余记录失败，错误码：");
    Serial.println(httpResponseCode);
  }
  http.end();
}

// 删除单条记录的函数，传入记录的 objectId
void deleteRecord(String objectId) {
  HTTPClient httpDel;
  String deleteUrl = String(serverName) + "/" + objectId;
  httpDel.begin(deleteUrl);
  httpDel.addHeader("X-LC-Id", appID);
  httpDel.addHeader("X-LC-Key", appKey);
  
  int delCode = httpDel.sendRequest("DELETE");
  if (delCode == 200) {
    Serial.println("【删除成功】： " + objectId);
  } else {
    Serial.print("【删除失败】： 错误码 ");
    Serial.println(delCode);
  }
  httpDel.end();
}

//远程控制板载led
void ledControl(){
   if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(serverName_LED);
        http.addHeader("X-LC-Id", appID);
        http.addHeader("X-LC-Key", appKey);
        http.addHeader("Content-Type", "application/json");

        int httpResponseCode = http.GET();
        if (httpResponseCode > 0) {
            String response = http.getString();
            Serial.println("服务器返回数据: " + response);

            DynamicJsonDocument doc(512);
            deserializeJson(doc, response);

            if (doc.containsKey("results") && doc["results"].size() > 0) {
                String ledStatus = doc["results"][0]["ledStatus"].as<String>();
                Serial.println("当前 LED 状态: " + ledStatus);

                if (ledStatus == "ON") {
                    digitalWrite(LEDPIN, HIGH);
                } 
                else {
                    digitalWrite(LEDPIN, LOW);
                }
            }
        } 
        else {
            Serial.print("HTTP 请求失败, 错误码: ");
            Serial.println(httpResponseCode);
        }
        http.end();
    } 
    else {
        Serial.println("WiFi 连接已断开, 正在重连...");
        WiFi.begin(ssid, password);
    }
    delay(5000); // 每 5 秒检查一次 LED 状态
}

//烟雾传感器初始化与校准的函数
void initMQ135() {
  // 配置模拟引脚为输入，并设置ADC衰减模式，扩展测量范围（确保传感器输出在安全范围内）
  pinMode(sensorPin, INPUT);
  analogSetPinAttenuation(sensorPin, ADC_11db);

  // 设置回归模型： _PPM = a * (RS/R0)^b
  MQ135.setRegressionMethod(1);
  // 设置典型回归参数：a = 102.2, b = -2.473（这些数值为参考值，实际使用时建议校准）
  MQ135.setA(102.2);
  MQ135.setB(-2.473);

  // 初始化传感器
  MQ135.init();

  // 校准过程：在洁净空气条件下采样并计算R0
  Serial.print("Calibrating MQ-135, please wait");
  float calcR0 = 0;
  for (int i = 1; i <= 10; i++) {
    MQ135.update();  // 更新采样数据
    calcR0 += MQ135.calibrate(RatioMQ135CleanAir);  // 根据洁净空气RS/R0=3.6计算当前R0
    Serial.print(".");
    delay(500);  // 采样间隔，根据实际情况调整
  }
  // 设置平均R0值
  MQ135.setR0(calcR0 / 10);
  Serial.println(" done!");

  // 检查校准结果是否异常
  if (isinf(calcR0)) {
    Serial.println("Warning: R0 is infinite (open circuit detected). Check wiring and supply.");
    while (1);
  }
  if (calcR0 == 0) {
    Serial.println("Warning: R0 is zero (analog pin shorted to ground). Check wiring and supply.");
    while (1);
  }
  
  // 开启详细调试输出（便于查看内部数据）
  MQ135.serialDebug(true);
}

//烟雾浓度读取函数
float readSmoke() {
  MQ135.update();                      // 更新传感器数据
  float smokePPM = MQ135.readSensor(); // 读取烟雾浓度（单位 ppm）
  Serial.print("MQ-135 PPM: ");
  Serial.print(smokePPM);
  Serial.println(" ppm");
  // 打印详细调试信息（包含原始ADC值、电压、RS、R0以及计算出的浓度）
  MQ135.serialDebug();
  return smokePPM;
}
