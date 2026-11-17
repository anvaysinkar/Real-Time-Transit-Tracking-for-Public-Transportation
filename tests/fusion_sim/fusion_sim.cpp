// fusion_sim.cpp -- runs the real rttt_sensor_fusion.ino on a PC with mocked ESP32 APIs and scripted
// IR / mat inputs, to check the state machine against the cases in the paper.
// Build and run:  make   (from tests/fusion_sim)
#include "Arduino.h"
uint32_t g_ms=0; int g_pin[64]; uint16_t g_adc=2500;
#include "WiFi.h"
HardwareSerial Serial(0); WiFiClass WiFi;
#include "rttt_sensor_fusion.ino"
void run(uint32_t ms){ for(uint32_t i=0;i<ms;i+=10){ g_ms+=10; loop(); } }
void outer(bool b){ g_pin[PIN_IR_OUTER]= b?LOW:HIGH; } void inner(bool b){ g_pin[PIN_IR_INNER]= b?LOW:HIGH; }
void cross(bool board, uint16_t matLevel, bool turnBack=false){
  if(board) outer(1); else inner(1); run(150); if(board) outer(0); else inner(0);
  g_adc=matLevel; run(300);
  if(!turnBack){ if(board) inner(1); else outer(1); run(150); if(board) inner(0); else outer(0); }
  else { if(board) outer(1); else inner(1); run(150); if(board) outer(0); else inner(0); }
  g_adc=2500; run(1500);
}
int main(){ for(int i=0;i<64;i++) g_pin[i]=HIGH; setup(); run(500);
  ::printf("-- board x2\n"); cross(true,500); cross(true,500);
  ::printf("-- alight\n"); cross(false,500);
  ::printf("-- crowded board (mat 100)\n"); cross(true,100);
  ::printf("-- luggage: mat only\n"); g_adc=400; run(800); g_adc=2500; run(1500);
  ::printf("-- turn back (outer, mat, outer)\n"); cross(true,500,true);
  ::printf("-- lingering: board then stand 5 s, inner beam re-broken\n");
  outer(1); run(150); outer(0); g_adc=500; run(300); inner(1); run(150); inner(0); run(3000); inner(1); run(150); inner(0); run(2000); g_adc=2500; run(1500);
  ::printf("-- too slow (2 s gap)\n"); outer(1); run(100); outer(0); run(2000); g_adc=500; run(300); inner(1); run(100); inner(0); g_adc=2500; run(1500);
  ::printf("occ=%ld boardings=%lu alightings=%lu crowded=%lu rejected=%lu\n",(long)occupancy,(unsigned long)boardings,(unsigned long)alightings,(unsigned long)crowdedEvents,(unsigned long)rejectedSequences);
  // expected: 4 boardings (2 normal, 1 crowded, 1 lingering), 1 alighting; luggage, turn-back and the
  // too-slow sequence are not counted; only the 2-3 person crossing is flagged crowded
  bool pass = occupancy == 3 && boardings == 4 && alightings == 1 && crowdedEvents == 1;
  ::printf("%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}
