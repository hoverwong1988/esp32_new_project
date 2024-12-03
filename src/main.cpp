#include "Web_Scr_set.h"


// 定义引脚
#define key 47    // 语音开关按键 
#define led 13  // LED3引脚21蓝色
#define led2 9   // LED2引脚14红色
#define led1 8   // LED1引脚19绿色
#define light 17 // 台灯引脚
// #define awaken 16 // 语音唤醒信号引脚
#define blk 18 // 背光

// 定义音频放大模块的I2S引脚定义
#define I2S_DOUT 7 // DIN引脚
#define I2S_BCLK 6 // BCLK引脚
#define I2S_LRC 5  // LRC引脚
HTTPClient http;  // 将HTTPClient对象移到全局范围


// int llm = 3;    // 大模型选择参数:0:coze智能体，1：科大讯飞星火，2：通义千问

String APPID = "7a55521c";                             // App ID,必填
String APISecret = "YTY4MzJjOWNmNTVkOTA5MTFiOTUyMDA4"; // API Secret，必填
String APIKey = "cc78d5339317e0c4cd518f54d6ce88c9";    // API Key，必填

String appId1 = APPID;
String websockets_server1 = "ws://iat-api.xfyun.cn/v2/iat";
String language = "zh_cn";     //zh_cn：中文（支持简单的英文识别）en_us：English


// // 定义一些全局变量
bool ledstatus = true;          // 控制led闪烁
// bool startPlay = false;
unsigned long urlTime = 0;
int noise = 20;                 // 噪声门限值
// int volume = 80;               // 初始音量大小（最小0，最大100）
// //音乐播放
// int mainStatus = 0;
// int conStatus = 0;
// int musicnum = 0;   //音乐位置下标
// int musicplay = 0;  // 是否进入连续播放音乐状态
// int cursorY = 0;
// //语音唤醒
int awake_flag = 1;

// // 使用动态JSON文档存储历史对话信息占用的内存过多，故改用c++中的vector向量
// std::vector<String> text;

// // 使用。
// String url = "";
String url1 = "";
String Date = "";

String askquestion = "";        //存储stt语音转文字信息，即用户的提问信息
String Answer = "";             //存储llm回答，用于语音合成（较短的回答）

int conflag = 0;        //用于连续对话
int await_flag = 1;     //待机标识
int start_con = 0;      //标识是否开启了一轮对话

using namespace websockets; // 使用WebSocket命名空间

WebsocketsClient webSocketClient1;  //与stt通信
// // 创建音频对象
Audio1 audio1;

void processResponse(int status);
void getText(String role, String content);

float calculateRMS(uint8_t *buffer, int bufferSize);
void ConnServer1();
int wifiConnect();
void getTimeFromServer();
String getUrl(String server, String host, String path, String date);


// 接收stt返回的语音识别文本并做相应的逻辑处理
void onMessageCallback1(WebsocketsMessage message)
{
    // 创建一个动态JSON文档对象，用于存储解析后的JSON数据，最大容量为4096字节
    // Serial.println("message:");
    DynamicJsonDocument jsonDocument(4096);

    // 解析收到的JSON数据
    DeserializationError error = deserializeJson(jsonDocument, message.data());
    // Serial.println(message.data());还是88。

    if (error)
    {
        // 如果解析出错，输出错误信息和收到的消息数据
        Serial.println("error:");
        Serial.println(error.c_str());
        Serial.println(message.data());
        return;
    }

    // 如果解析没有错误，从JSON数据中获取返回码，如果返回码不为0，表示出错
    if (jsonDocument["code"] != 0)
    {
        // 输出完整的JSON数据
        // Serial.println(message.data());  //中间值
        // 关闭WebSocket客户端
        webSocketClient1.close();
    }
    else
    {
        // 获取JSON数据中的结果部分
        JsonArray ws = jsonDocument["data"]["result"]["ws"].as<JsonArray>();
        
        // 检查是否为最终句子(所有bg不为0)
        bool isFinalSentence = true;
        for (JsonVariant i : ws)
        {
            if(i["bg"].as<int>() == 0)
            {
                isFinalSentence = false;
                break;
            }
        }

        if (jsonDocument["data"]["status"] != 2)    //处理流式返回的内容
        {
            if(isFinalSentence)  // 只有是最终句子时才更新askquestion
            {
                
                for (JsonVariant i : ws)
                {
                    for (JsonVariant w : i["cw"].as<JsonArray>())
                    {
                        askquestion += w["w"].as<String>();
                    }
                }
            }
        }
        else  // status == 2 时是最后一次返回(标点符号)
        {
            for (JsonVariant i : ws)
            {
                for (JsonVariant w : i["cw"].as<JsonArray>())
                {
                    askquestion += w["w"].as<String>();
                }
            }
        }
    }

    // 获取状态码，等于2表示文本已经转换完成
    if (jsonDocument["data"]["status"] == 2)
    {
        // 如果状态码为2，表示消息处理完成
        Serial.println("status == 2");
        webSocketClient1.close();
        Serial.println(askquestion);
        http.begin("http://192.168.1.119:5002/typing");  // 初始化 HTTP 请求
        http.addHeader("Content-Type", "application/json");

        // 创建 JSON 数据
        String jsonData = String("{\"message\":\"") + askquestion + "\"}";

        int httpResponseCode = http.POST(jsonData);

        if (httpResponseCode > 0) {
            String response = http.getString();
            Serial.println("响应代码: " + String(httpResponseCode));
            Serial.println("响应内容: " + response);
        } else {
            Serial.println("错误代码: " + String(httpResponseCode));
        }

        http.end();  

    }
    
}


// 录音
void onEventsCallback1(WebsocketsEvent event, String data)
{
    // 当WebSocket连接打开时触发
    if (event == WebsocketsEvent::ConnectionOpened)
    {

        // 初始化变量
        int silence = 0;
        int firstframe = 1;
        int voicebegin = 0;
        int voice = 0;
        int null_voice = 0;

        // 创建一个静态JSON文档对象，2000一般够了，不够可以再加（最多不能超过4096），但是可能会发生内存溢出
        StaticJsonDocument<2000> doc;

        conflag = 0;

        Serial.println("ConnectionOpened 开始录音");
        // 无限循环，用于录制和发送音频数据
        while (1)
        {
            // 待机状态（语音唤醒状态）也可通过boot键启动
            if (digitalRead(key) == 0 && await_flag == 1)
            {
                start_con = 1;      //对话开始标识
                await_flag = 0;
                webSocketClient1.close();
                break;
            }
            // 清空JSON文档
            doc.clear();

            // 创建data对象
            JsonObject data = doc.createNestedObject("data");

            // 录制音频数据
            audio1.Record();

            // 计算音频数据的RMS值
            float rms = calculateRMS((uint8_t *)audio1.wavData[0], 1280);
            if (null_voice < 10 && rms > 1000) // 抑制录音初期奇奇怪怪的噪声
            {
                rms = 8.6;
            }
            printf("%d %f\n", 0, rms);

            if(null_voice >= 80)    // 如果从录音开始过了8秒才说话，讯飞stt识别会超时，所以直接结束本次录音，重新开始录音如果从录音开始过了8秒才说话。讯飞TT识别会超时。所以直接结束本次录音。这个字我不认识啊。
            {
                if (start_con == 1)     // 表示正处于对话中，才回复退下，没有进入对话则继续待机
                {
                    start_con = 0;      // 退出对话
                    Answer = "主人，我先退下了，有事再找我";
                }
                // 标识正处于待机状态
                await_flag = 1;
                // 将awake_flag置为0,继续进行唤醒词识别
                awake_flag = 0;
                // 录音超时，断开本次连接
                webSocketClient1.close();
                Serial.println("录音结束");
                return;
            }

            // 。如果从录音开始过了8秒才说话。，识别会超时。所以直接结束本次录音。如果从录音开始过了8秒。
            if (rms < noise)
            {
                null_voice++;
                if (voicebegin == 1)
                {
                    silence++;
                }
            }
            else
            {
                if (null_voice > 0)
                    null_voice--;
                voice++;
                if (voice >= 5)
                {
                    voicebegin = 1;
                }
                else
                {
                    voicebegin = 0;
                }
                silence = 0;
            }

            // 如果静音达到8个周期，发送结束标志的音频数据
            if (silence == 10)
            {
                data["status"] = 2;
                data["format"] = "audio/L16;rate=8000";
                data["audio"] = base64::encode((byte *)audio1.wavData[0], 1280); // 只发送当前这一块数据
                data["encoding"] = "raw";

                String jsonString;
                serializeJson(doc, jsonString);
                webSocketClient1.send(jsonString);
                
                Serial.println("status2");
                break; // 代表发送已经结束
            }

            // 处理第一帧音频数据
            if (firstframe == 1)
            {
                data["status"] = 0;
                data["format"] = "audio/L16;rate=8000";
                data["audio"] = base64::encode((byte *)audio1.wavData[0], 1280);
                data["encoding"] = "raw";
                Serial.println("status0");
                JsonObject common = doc.createNestedObject("common");
                common["app_id"] = appId1.c_str();

                JsonObject business = doc.createNestedObject("business");
                business["domain"] = "iat";
                business["language"] = language.c_str();
                business["accent"] = "mandarin";
                // 不使用动态修正
                business["vinfo"] = 1;
                // 使用动态修正
                business["dwa"] = "wpgs";
                business["vad_eos"] = 1000;

                String jsonString;
                serializeJson(doc, jsonString);

                webSocketClient1.send(jsonString);
                firstframe = 0;
                delay(40);
            }
            else
            {
                // 处理后续帧音频数据
                data["status"] = 1;
                data["format"] = "audio/L16;rate=8000";
                data["audio"] = base64::encode((byte *)audio1.wavData[0], 1280);
                data["encoding"] = "raw";

                String jsonString;
                serializeJson(doc, jsonString);

                webSocketClient1.send(jsonString);
                delay(40);
            }
        }
    }
    // 当WebSocket连接关闭时触发
    else if (event == WebsocketsEvent::ConnectionClosed)
    {
        // 向串口输出提示信息
        Serial.println("Connnection1 Closed");
    }
    // 当收到Ping消息时触发
    else if (event == WebsocketsEvent::GotPing)
    {
        // 向串口输出提示信息
        Serial.println("Got a Ping!");
    }
    // 当收到Pong消息时触发
    else if (event == WebsocketsEvent::GotPong)
    {
        // 向串口输出提示信息
        Serial.println("Got a Pong!");
    }
}

void setup()
{
    // 初始化串口通信，波特率为115200
    Serial.begin(115200);

    // // 初始化音频模块audio1
    audio1.init();

    // // 初始化Preferences
    preferences.begin("wifi_store");

    int result = wifiConnect();
    
    webSocketClient1.onMessage(onMessageCallback1);

    webSocketClient1.onEvent(onEventsCallback1);

    // 从百度服务器获取当前时间
    getTimeFromServer();
    // 使用当前时间生成WebSocket连接的URL
    // url = getUrl(websockets_server, "spark-api.xf-yun.com", websockets_server.substring(25), Date);
    url1 = getUrl(websockets_server1, "iat-api.xfyun.cn", "/v2/iat", Date);

    if (result == 1)
    {
        awake_flag = 0;
    }
    else
    {
        openWeb();
    }
    // // 记录当前时间，用于后续时间戳比较
    urlTime = millis();
    // // 延迟1000毫秒，便于用户查看屏幕显示的信息，同时使设备充分初始化
    delay(1000);
}


void loop()
{
    while (webSocketClient1.available()) {
        webSocketClient1.poll();
        // 可以在这里添加其他需要持续执行的代码
    }
    askquestion = "";
    Serial.printf("Start recognition\r\n\r\n");
    // 如果距离上次时间同步超过4分钟  
    if (urlTime + 240000 < millis()) // 超过4分钟，重新做一次鉴权，感觉买正股前排肯定进不去，后排也不想去。
    {
        // 更新时间戳嗯，挺好。
        urlTime = millis();
        // 从服务器获取当前时间
        getTimeFromServer();
        // 更新WebSocket连接9 20
        url1 = getUrl(websockets_server1, "iat-api.xfyun.cn", "/v2/iat", Date);
    }
    // Connect to WebSocket
    Serial.println("Begin connect to server1......");
    if (webSocketClient1.connect(url1.c_str()))
    {
        Serial.println("Connected to server1!");
    }
    else
    {
        Serial.println("Failed to connect to server1!");
    }

}

int wifiConnect()
{
    // 断开当前WiFi连接
    WiFi.disconnect(true);

    preferences.begin("wifi_store");
    int numNetworks = preferences.getInt("numNetworks", 0);
    if (numNetworks == 0)
    {
        preferences.end();
        return 0;
    }

    // 获取存储的 WiFi 配置
    for (int i = 0; i < numNetworks; ++i)
    {
        String ssid = preferences.getString(("ssid" + String(i)).c_str(), "");
        String password = preferences.getString(("password" + String(i)).c_str(), "");

        // 尝试连接到存储的 WiFi 网络
        if (ssid.length() > 0 && password.length() > 0)
        {
            Serial.print("Connecting to ");
            Serial.println(ssid);
            Serial.print("password:");
            Serial.println(password);
            uint8_t count = 0;

            // 检查SSID并配置IP和网关
            if (ssid == "TP-LINK_7228FA") {  
                IPAddress localIP(192, 168, 1, 166);
                IPAddress gateway(192, 168, 1, 1);
                IPAddress subnet(255, 255, 255, 0); // 子网掩码
                IPAddress dns(192, 168, 1, 1); // DNS服务器
                // 设置静态IP
            if (!WiFi.config(localIP, gateway, subnet, dns)) {
                Serial.println("WiFi.config failed");
            }
            
            }
            WiFi.begin(ssid.c_str(), password.c_str());
            // 等待WiFi连接成功
            while (WiFi.status() != WL_CONNECTED)
            {
                // 闪烁板载LED以指示连接状态
                digitalWrite(led, ledstatus);
                ledstatus = !ledstatus;
                count++;

                // 如果尝试连接超过30次，则认为连接失败
                if (count >= 30)
                {
                    Serial.printf("\r\n-- wifi connect fail! --\r\n");

                    break;
                }

                // 等待100毫秒
                vTaskDelay(100);
            }

            if (WiFi.status() == WL_CONNECTED)
            {
                // 向串口输出连接成功信息和IP地址
                Serial.printf("\r\n-- wifi connect success! --\r\n");
                Serial.print("IP address: ");
                Serial.println(WiFi.localIP());

                // 输出当前空闲堆内存大小
                Serial.println("Free Heap: " + String(ESP.getFreeHeap()));

                preferences.end();
                return 1;
            }
        }
    }

    preferences.end();
    return 0;
}

void getTimeFromServer()
{
    String timeurl = "https://www.baidu.com";   // 定义用于获取时间的URL
    HTTPClient http;                // 创建HTTPClient对象
    http.begin(timeurl);            // 初始化HTTP连接
    const char *headerKeys[] = {"Date"};        // 定义需要收集的HTTP头字段
    http.collectHeaders(headerKeys, sizeof(headerKeys) / sizeof(headerKeys[0]));    // 设置要收集的HTTP头字段
    int httpCode = http.GET();      // 发送HTTP GET请求
    Date = http.header("Date");     // 从HTTP响应头中获取Date字段
    Serial.println(Date);           // 输出获取到的Date字段到串口
    http.end();                     // 结束HTTP连接

    // delay(50); // 根据实际情况可以添加延时，以便避免频繁请求
}

String getUrl(String Spark_url, String host, String path, String Date)
{
    // 拼接签名原始字符串
    String signature_origin = "host: " + host + "\n";
    signature_origin += "date: " + Date + "\n";
    signature_origin += "GET " + path + " HTTP/1.1";
    // 示例：signature_origin="host: spark-api.xf-yun.com\ndate: Mon, 04 Mar 2024 19:23:20 GMT\nGET /v3.5/chat HTTP/1.1";

    // 使用 HMAC-SHA256 进行加密
    unsigned char hmac[32];                                 // 存储HMAC结果
    mbedtls_md_context_t ctx;                               // HMAC上下文
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;          // 使用SHA256哈希算法
    const size_t messageLength = signature_origin.length(); // 签名原始字符串的长度
    const size_t keyLength = APISecret.length();            // 密钥的长度

    // 初始化HMAC上下文
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
    // 设置HMAC密钥
    mbedtls_md_hmac_starts(&ctx, (const unsigned char *)APISecret.c_str(), keyLength);
    // 更新HMAC上下文
    mbedtls_md_hmac_update(&ctx, (const unsigned char *)signature_origin.c_str(), messageLength);
    // 完成HMAC计算
    mbedtls_md_hmac_finish(&ctx, hmac);
    // 释放HMAC上下文
    mbedtls_md_free(&ctx);

    // 将HMAC结果进行Base64编码
    String signature_sha_base64 = base64::encode(hmac, sizeof(hmac) / sizeof(hmac[0]));

    // 替换Date字符串中的特殊字符
    Date.replace(",", "%2C");
    Date.replace(" ", "+");
    Date.replace(":", "%3A");

    // 构建Authorization原始字符串
    String authorization_origin = "api_key=\"" + APIKey + "\", algorithm=\"hmac-sha256\", headers=\"host date request-line\", signature=\"" + signature_sha_base64 + "\"";

    // 将Authorization原始字符串进行Base64编码
    String authorization = base64::encode(authorization_origin);

    // 构建最终的URL
    String url = Spark_url + '?' + "authorization=" + authorization + "&date=" + Date + "&host=" + host;

    // 向串口输出生成的URL
    Serial.println(url);

    // 返回生成的URL
    return url;
}

// 计算录音数据的均方根值
float calculateRMS(uint8_t *buffer, int bufferSize)
{
    float sum = 0;  // 初始化总和变量
    int16_t sample; // 定义16位整数类型的样本变量

    // 遍历音频数据缓冲区，每次处理两个字节（16位）
    for (int i = 0; i < bufferSize; i += 2)
    {
        // 将两个字节组合成一个16位的样本值
        sample = (buffer[i + 1] << 8) | buffer[i];

        // 将样本值平方后累加到总和中
        sum += sample * sample;
    }

    // 计算平均值（样本总数为bufferSize / 2）
    sum /= (bufferSize / 2);

    // 返回总和的平方根，即RMS值
    return sqrt(sum);
}

