#pragma once
#include <cstdint>
struct TinyGPSDate { bool isValid(){return false;} uint16_t year(){return 0;} uint8_t month(){return 0;} uint8_t day(){return 0;} };
struct TinyGPSTime { bool isValid(){return false;} uint32_t age(){return 0;} uint8_t hour(){return 0;} uint8_t minute(){return 0;} uint8_t second(){return 0;} };
struct TinyGPSLocation { bool isValid(){return false;} uint32_t age(){return 0;} double lat(){return 0;} double lng(){return 0;} };
struct TinyGPSInteger { uint32_t value(){return 0;} };
struct TinyGPSPlus { TinyGPSDate date; TinyGPSTime time; TinyGPSLocation location; TinyGPSInteger satellites; bool encode(char){return false;} };
