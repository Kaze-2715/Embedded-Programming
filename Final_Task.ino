#include <MsTimer2.h>
#include <Adafruit_GFX.h>//申明OLED 12864的函数库
#include <Adafruit_SSD1306.h>//申明OLED 12864的函数库
#include <IRremote.h>
#include "dht11.h"

#define Fan_Pin       10                  // 风扇PWM管脚
#define DHT11_Pin     2                  // 温湿度传感器管脚
#define GM_Pin       A0                 //光敏管脚
#define LED_Pin      12                 //双色LED管脚
#define OLED_RESET   8
#define RECV_PIN     11                   // 红外接收引脚

int GetTempToPWM(float v_fTemp); //将温度转换为风扇转速的PWM值
int serial_putc( char c, struct __file * ); //重定向输出函数
void printf_begin(void); //初始化重定向输出函数
void UploadTemp(); //向串口监视器返回温湿度信息
void DisplaySpeed(int v_iNum);   //板载LED显示当前风扇转速档位
void runtimeDHT(); //运行时对DHT11的调用，读取温湿度值并转换为风扇挡位，同时输出数值
void runtimePhoto(); //运行时对光敏电阻的调用，读取环境亮度并修改LED亮度
void displayEnv(); //在OLED上显示温湿度、光照数据
void warning(); //报警
void displayWaringT(); //显示温度报警信息
void displayWaringH(); //显示湿度报警信息

// 解析串口通信
int incomingByte = 0;                    
char inputString[20] = {0};
boolean newLineReceived = false;
boolean startBit  = false;
String returntemp = "";

// OLED
Adafruit_SSD1306 display(OLED_RESET);

// 蜂鸣器
int BEEP_Pin = 7;

//dht11温湿度传感器、风扇模块
dht11 DHT11;
float fTempCtr[5] = { 5.0f, 25.0f, 28.0f, 30.0f, 32.0f};       //存储温度阀值
const int iSpeedPwm[5] = {0, 80, 80, 160, 240};  //存储不同阀值对应不同速度
float g_fTemp= 0.0;
float g_humidity = 0.0;
float g_limitTfloor = 20.0;
float g_limitTceil = 30.0;
float g_limitHfloor = 40.0;
float g_limitHceil = 70.0;
unsigned int g_count = 10;

//光敏传感器
int g_GM = 0;
int g_LT = 0;
int g_LIMIT = 25;

//红外遥控器
IRrecv irrecv(RECV_PIN);
decode_results results;//结构声明
boolean signalReceived = false;
long IRcode = 0;


void setup() {
  //初始化温湿度传感器管脚、风扇、板载LEDIO口、双色LED IO口为输出方式
  pinMode(DHT11_Pin, OUTPUT);
  pinMode(Fan_Pin, OUTPUT);   
  pinMode(BEEP_Pin, OUTPUT);
  pinMode(LED_Pin, OUTPUT);
  pinMode(RECV_PIN, INPUT);   //端口模式，输入
  pinMode(13, OUTPUT);      //端口模式，输出  

  digitalWrite(BEEP_Pin, HIGH);
  printf_begin(); 
  Serial.begin(9600);	          //波特率9600 （WIFI通讯设定波特率）  
  MsTimer2::set(1000, UploadTemp);        // 中断设置函数，每 1000ms 进入一次中断
  MsTimer2::start();                //开始计时
  irrecv.enableIRIn(); // Start the receiver

  display.begin(SSD1306_SWITCHCAPVCC, 0x3c);  // initialize with the I2C addr 0x3D (for the 128x64)
  display.clearDisplay();   // 清屏    
  display.setTextSize(2);   //字体尺寸大小2
  display.setTextColor(WHITE);//字体颜色为白色
  display.setCursor(0,8); //把光标定位在第16行，第0列
  display.print("Hi there!");//显示问候语
  display.display();//显示
  delay(1000);

  //******************************************************

}

void loop() 
{
  Serial.println(inputString);
  runtimeDHT();
  runtimePhoto();
  displayEnv();
  if (((g_fTemp < g_limitTfloor) || (g_fTemp > g_limitTceil)) || ((g_humidity < g_limitHfloor) || (g_humidity > g_limitHceil)))
  {
    warning();//这里是怕warning()被重复调用影响时序逻辑
    Serial.println("Warning!");
  }
  if ((g_fTemp < g_limitTfloor) || (g_fTemp > g_limitTceil))
  {
    displayWaringT();
  }
  if ((g_humidity < g_limitHfloor) || (g_humidity > g_limitHceil))
  {
    displayWaringH();
  }
  if (irrecv.decode(&results))
   {
      IRcode = results.value;
      Serial.println(results.value); 
      irrecv.resume();
      signalReceived = true;
   }
   if (signalReceived)
  {
    protocol();
  }
  if (newLineReceived)
  {
    displayLine();
    newLineReceived = false;
    memset(inputString, sizeof(inputString), 0);
  }
}

/**
* Function       serialEvent
* @author        zhulin
* @date          2017.11.14
* @brief         串口接收中断   serialEvent()是IDE1.0及以后版本新增的功能，不清楚为什么大部份人不愿意用，这个可是相当于中断功能一样的啊! 
* @param[in]     void
* @retval        void
* @par History   无
*/
void serialEvent()
{
  int i = 0;
  while (Serial.available())
  {    
    incomingByte = Serial.read();
    if(incomingByte == '$')
    {
      startBit= true;
    }
    if(startBit == true)
    {
       inputString[i++] = (char) incomingByte;
    }  
    if (incomingByte == '^')
    {
       newLineReceived = true; 
       startBit = false;
    }
  }
}

int GetTempToPWM(float v_fTemp)
{
  /*小于2档以下都算是1档*/
  if(v_fTemp < fTempCtr[1])    //如果当前温度小于温度阈值20.0f
  {
    DisplaySpeed(1);          //风扇1档
    delay(1000);              //延时1000ms
    return iSpeedPwm[0];      //返回速度0
  }
  else if((v_fTemp >= fTempCtr[1]) && (v_fTemp < fTempCtr[2])) //如果当前温度大于等于温度阈值20.0f且小于温度阈值25.0f
  {
    DisplaySpeed(2);        //风扇2档
    delay(1000);            //延时1000ms
    return iSpeedPwm[1];    //返回速度120
  }
  else if((v_fTemp >= fTempCtr[2]) && (v_fTemp < fTempCtr[3]))  //如果当前温度大于等于温度阈值25.0f且小于温度阈值28.0f
  {
    DisplaySpeed(3);        //风扇3档
    delay(1000);            //延时1000ms
    return iSpeedPwm[2];    //返回速度160
  }
  else if((v_fTemp >= fTempCtr[3]) && (v_fTemp < fTempCtr[4]))  //如果当前温度大于等于温度阈值28.0f且小于温度阈值32.0f
  {
    DisplaySpeed(4);        //风扇4档
    delay(1000);            //延时1000ms
    return iSpeedPwm[3];    //返回速度160
  }
  else if(v_fTemp > fTempCtr[4])                                //如果当前温度大于等于温度阈值32.0f
  {
    DisplaySpeed(5);       //风扇5档
    delay(1000);           //延时1000ms
    return iSpeedPwm[4];   //返回速度240
  }
}

void DisplaySpeed(int v_iNum)   //板载LED显示当前档位
{
  for(int i=0; i < v_iNum; i++)  //当前档位为多少，风扇就亮几次
  {
    digitalWrite(13, HIGH);
    delay(200);
    digitalWrite(13, LOW);
    delay(200);
  }
  
}

void UploadTemp()
{
    char temp[50] = {0};
    snprintf(temp, sizeof(temp), "Temp:%dC, Hum:%d%%, Light:%d%%", (int)g_fTemp, (int)g_humidity, (int)g_LT);
    Serial.println(temp);
    Serial.print("Light_limit:");
    Serial.println(g_LIMIT);  
   
}

void runtimeDHT() //运行时对DHT11的调用
{
  int chk = DHT11.read(DHT11_Pin);           //读取温湿度传感器管脚的数值
  g_fTemp = (float)DHT11.temperature;
  g_humidity = (float)DHT11.humidity;
  int voltage = GetTempToPWM(g_fTemp);        //g_fTemp赋值为浮点型读取到的温度值
  analogWrite(Fan_Pin, voltage);
  UploadTemp();
  Serial.print("Fan Speed:");
  Serial.println(voltage); //根据现在温度输出不同PWM控制风扇转速
}

void runtimePhoto() //运行时对光敏电阻的调用
{
  int val = analogRead(GM_Pin); //读取A0口的电压值并赋值到val
  g_GM = -10 * val + 10000;
  g_LT = map(g_GM, 10000, 0, 255, 0);   //10000-0 划分到 0-255
  Serial.print("Current_light:");
  Serial.println(g_LT);
  if (g_LT < ((float)g_LIMIT / 100 * 255))
  {
    analogWrite(LED_Pin, 255 - g_LT);
  }
  else
  {
    analogWrite(LED_Pin, 0);
  }
  //analogWrite(LED_Pin, g_LT);           //将划分好后的值模拟写入LED管脚
  g_LT = map(g_LT, 255, 0, 100, 0);   //0-255 划分到 0-100
  printf("Lx:%d, lightMode:%d\n", g_GM, g_LT); //打印光强和亮度
}

/*printf格式化字符串初始化*/
int serial_putc( char c, struct __file * )
{
  Serial.write( c );
  return c;
}
void printf_begin(void)
{
  fdevopen( &serial_putc, 0 );
}

// @attention 还是不能显示浮点数
void displayEnv()
{
    char temp[20] = {0};
    display.clearDisplay();   // 清屏    
    display.setTextSize(1);   //字体尺寸大小1
    display.setTextColor(WHITE);//字体颜色为白色
    display.setCursor(0,4); //把光标定位在第16行，第0列
    snprintf(temp, sizeof(temp), "Temp:%dC", (int)g_fTemp);
    display.print(temp);//显示时间
    display.setCursor(0,12); //把光标定位在第16行，第0列
    snprintf(temp, sizeof(temp), "Hum:%d%%", (int)g_humidity);
    display.print(temp);//显示时间
    display.setCursor(0,20); //把光标定位在第16行，第0列
    snprintf(temp, sizeof(temp), "Light:%d%%", (int)g_LT);
    display.print(temp);//显示时间
    display.display();//显示   
}

void protocol()
{
  switch (IRcode)
    {
      case 16754775:
        Serial.print("Before:");
        Serial.print(g_LIMIT);
        g_LIMIT += 5;
        Serial.print(" After:");
        Serial.println(g_LIMIT);
        break;
      case 16769055:
        //光敏阈值-5
        Serial.print("Before:");
        Serial.print(g_LIMIT);
        g_LIMIT -= 5;
        Serial.print(" After:");
        Serial.println(g_LIMIT);
        break;
      case 16712445:
        //温度上限+1
        Serial.print("Before:");
        Serial.print(g_limitTceil);
        g_limitTceil += 1;
        Serial.print(" After:");
        Serial.println(g_limitTceil);
        break;
      case 16720605:
        //温度上限-1
        Serial.print("Before:");
        Serial.print(g_limitTceil);
        g_limitTceil -= 1;
        Serial.print(" After:");
        Serial.println(g_limitTceil);
        break;
      case 16769565:
        //温度下限+1
        Serial.print("Before:");
        Serial.print(g_limitTfloor);
        g_limitTfloor += 1;
        Serial.print(" After:");
        Serial.println(++g_limitTfloor);
        break;
      case 16753245:
        //温度下限-1
        Serial.print("Before:");
        Serial.print(g_limitTfloor);
        g_limitTfloor -= 1;
        Serial.print(" After:");
        Serial.println(--g_limitTfloor);
        break;
       case 16750695:
      //   //湿度上限+1
         Serial.print("Before:");
         Serial.print(g_limitHceil);
         Serial.print(" After:");
         Serial.println(++g_limitHceil);
         break;
       case 16756815:
        //湿度上限-1
        Serial.print("Before:");
        Serial.print(g_limitHceil);
        Serial.print(" After:");
        Serial.println(--g_limitHceil);
        break;
      case 16724175:
        Serial.print("Before:");
        Serial.print(g_limitHfloor);
        Serial.print(" After:");
        Serial.println(g_limitHfloor);
        break;
      case 16738455:
        Serial.print("Before:");
        Serial.print(g_limitHfloor);
        Serial.print("After:");
        Serial.println(g_limitHfloor);
        break;
      default:
        break;
    }
    IRcode = 0;
    signalReceived = false;
}

void warning()
{
  // TODO 超标的时候报警
  digitalWrite(BEEP_Pin,LOW);
  delay(500);
  digitalWrite(BEEP_Pin,HIGH);
}

void displayWaringT()
{
    // TODO 显示温度报警信息
    char temp[30] = {0};
    snprintf(temp, sizeof(temp), "Warning!\nTemprature: %dC", (int)g_fTemp);
    display.clearDisplay();   // 清屏    
    display.setTextSize(1);   //字体尺寸大小1
    display.setTextColor(WHITE);//字体颜色为白色
    display.setCursor(0,8); //把光标定位在第8行，第0列
    display.print(temp);//显示时间
    display.display();//显示  
    delay(500); 
}

void displayWaringH()
{
  // TODO 显示湿度报警信息
  char temp[30] = {0};
    snprintf(temp, sizeof(temp), "Warning!\nCurrent Humidity:%d%%", (int)g_humidity);
    display.clearDisplay();   // 清屏    
    display.setTextSize(1);   //字体尺寸大小2
    display.setTextColor(WHITE);//字体颜色为白色
    // display.setCursor(0,0); //把光标定位在第0行，第0列
    // display.print("ChuangLeBo");//显示字符
    display.setCursor(0,8); //把光标定位在第16行，第0列
    display.print(temp);//显示时间
    // display.setCursor(0,48); //把光标定位在第48行，第0列
    // display.print(" Wellcom!"); //显示字符
    display.display();//显示 
}

void displayLine()
{
  display.clearDisplay();   // 清屏    
  display.setTextSize(1);   //字体尺寸大小1
  display.setTextColor(WHITE);//字体颜色为白色
  for (int i = 0; inputString[i] != '\0'; i++)
  {
    display.setCursor(i*6,4); //把光标定位在第16行，第0列
    display.print(inputString[i]);//显示
  }
  display.display();//显示
}
