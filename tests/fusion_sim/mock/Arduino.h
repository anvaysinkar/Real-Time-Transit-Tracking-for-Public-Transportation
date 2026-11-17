#pragma once
#include <string>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <ctime>
#define HIGH 1
#define LOW 0
#define INPUT_PULLUP 2
#define SERIAL_8N1 0
#define PROGMEM
typedef int adc_attenuation_t; enum { ADC_11db = 3 };
extern uint32_t g_ms; inline uint32_t millis(){return g_ms;} inline void delay(uint32_t){}
extern int g_pin[64]; inline int digitalRead(uint8_t p){return g_pin[p];} inline void pinMode(uint8_t,int){}
extern uint16_t g_adc; inline uint16_t analogRead(uint8_t){return g_adc;} inline void analogReadResolution(int){} inline void analogSetPinAttenuation(uint8_t,int){}
struct String { std::string s; String(const char*c=""):s(c){} String& operator+=(const char*c){s+=c;return *this;} const char* c_str() const {return s.c_str();} };

struct HardwareSerial { HardwareSerial(int){} void begin(long,int=0,int=0,int=0){} int available(){return 0;} int read(){return -1;}
  void println(const char* c){ if(c[0]=='{') ::printf("%s\n",c);}  size_t printf(const char*,...){return 0;} };
extern HardwareSerial Serial;
typedef void* QueueHandle_t; typedef int BaseType_t; typedef uint32_t TickType_t;
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
inline QueueHandle_t xQueueCreate(int,size_t){return nullptr;} inline BaseType_t xQueueSend(QueueHandle_t,const void*,TickType_t){return 1;}
inline BaseType_t xQueueReceive(QueueHandle_t,void*,TickType_t){return 1;} inline BaseType_t xQueuePeek(QueueHandle_t,void*,TickType_t){return 1;}
inline unsigned uxQueueMessagesWaiting(QueueHandle_t){return 0;} inline void vTaskDelay(TickType_t){}
inline BaseType_t xTaskCreatePinnedToCore(void(*)(void*),const char*,uint32_t,void*,int,void*,int){return 1;}
inline void configTime(long,int,const char*){}
