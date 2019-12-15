#include <Adafruit_GFX.h>
#include <gfxfont.h>
#include <Fonts/Picopixel.h>
#include <Adafruit_SSD1305.h>
#include <Wire.h>
#include <ESP8266WiFi.h>
extern "C" {
#include "gpio.h"
}
extern "C" {
  #include "user_interface.h"
}
WiFiClient client;
#include <time.h>
#include "autoupdate.h"

#define QUEUE_SIZE 32*1024
#define IDLE_TIME 180

#include <SoftwareSerial.h>
#include <jmy6xx.h>
#define RFID_RX 12
#define RFID_TX 14
SoftwareSerial SSerial(RFID_RX,RFID_TX);
JMY6xx jmy622(&SSerial);
//JMY6xx jmy622(0x50); // 0xA0/0xA1 => 0x50 in arduino world

Adafruit_SSD1305 display(2);

#define BUZZER_PIN 15
#define CHECKIN_PIN 13

#include "sounds.h"
#include "secrets.h"

// IDLE
long idle_after = 1000*IDLE_TIME;

// GLOBAL VARS
int start_block = 0;
int read_blocks = 9;
byte* send_buffer;
byte* queue;
int q_size = QUEUE_SIZE;
int q_pos = 0;
int record_size = 8+2+read_blocks*4; // align
char shelf[32];
byte wrt[64];

// DISPLAY MESSAGES
int display_prio = 0;
long display_expire = 0;

int count_total = 0;
int count_from_idle = 0;
int count_shelf = 0;

inline void shelf_reset() {
  memset(shelf, 0, 32);

  byte* record = queue+q_pos;

  memset(record, 0, 8);
  record[8] = 0; // flags
  record[9] = 32; // size
  memcpy(record+10, shelf, 32);
  q_pos += 10 + 32;
}

// CURRENT STATE
int state = 1; // 0 idle, 1 Inventory, 2 Checkin
long epoch = 0;

void ICACHE_RAM_ATTR checkin () { // interrupts need to reside in IRAM since esp8266 v2.6.0
  long now = millis();
  idle_after = now+1000*IDLE_TIME; // extends the idle time
  display_expire = 0; // clear the display

  if (state==0) {
    state = -1;
    return;
  }
  
  int prev = state;
  if (digitalRead(CHECKIN_PIN)==LOW) { // PRESS
    state = 2;
  } else {
    state = 1;
  }
}


////////////////////////////////////////////
// UPDATE DISPLAY
////////////////////////////////////////////

int wifi_gfx[] = {
  0,0,
  3,0,  3,1,  2,2,
  6,0,  6,1,  6,2,  5,3,  5,4,
  9,0,  9,1,  9,2,  8,3,  8,4,  7,5,
};

int display_cycle = 0;
void update_display() {
  if (millis() > display_expire) full_update_display();
  display_cycle++;

  yield();
  int wx = 127;
  int wy = 0;
  display.fillRect(wx-10, wy, 11, 7, BLACK);
  if (client.connected()) {
    display_cycle %=4;
    if (display_cycle<2) {
      display.drawLine(wx-4,wy, wx-4,wy+4, WHITE);
      display.drawLine(wx-5,wy+1,wx-3, wy+1, WHITE);
    } 
    if (display_cycle==1 or display_cycle==2) {
      display.drawLine(wx-1,wy, wx-1,wy+4, WHITE);
      display.drawLine(wx-2,wy+3,wx-0, wy+3, WHITE);
    }
  } else if (WiFi.status() == WL_CONNECTED) {
    int wifi = wifi_connect()*14/100;
    if (wifi>13) wifi=13;
    if (wifi<0) wifi=0;  
    display_cycle %= wifi+2;
    for (int i=0; i<display_cycle; i++) {
      display.drawPixel(wx-wifi_gfx[i*2], wy+wifi_gfx[i*2+1], WHITE);
    }
  } else {
    display_cycle %= 2;
    if (display_cycle==0) {
      display.drawLine(wx-2, wy, wx, wy+2, WHITE);
      display.drawLine(wx, wy, wx-2, wy+2, WHITE);
    } 
  }
  yield();
  display.display();
}

void full_update_display() {
  display_prio = 0;
  yield();
  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setFont();
  display.setCursor(0,0);
  if (state==1) {
    display.println("Inventory");
  } else if (state==2) {
    display.println("Check IN");
  } else {
    display.println(String("State: ")+state);
  }

  yield();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setFont(&Picopixel);
  display.setCursor(0,14);
  display.print(count_total);
  display.print(" tags scanned");
  if (shelf[0]) {
    display.print(", ");
    display.print(count_shelf);
    display.print(" tags on shelf");
  }
  else if (count_from_idle != count_total) {
    display.print(", ");
    display.print(count_from_idle);
    display.print(" since idle");
  }

  yield();

  if (shelf[0]) { // first byte is not \0
    display.setFont(&Picopixel);
    display.setCursor(0,22);
    display.println(String("")+"shelf: "+shelf);
  }

  if (q_pos) {
    yield();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setFont(&Picopixel);
    display.setCursor(0,30);
    //int queue_full = q_pos *100 /q_size;
    display.print("buffer: ");
    display.print((q_pos+record_size/2)/record_size);
    display.print("/");
    display.print(q_size/record_size);
//    display.print(" (");
//    display.print(queue_full);
//    display.print("%)");
  }

  if (epoch) {
    display.setFont(&Picopixel);
    display.setCursor(110,30);
    long e = millis()/1000+epoch;
    struct tm* now = localtime(&e);
    display.print(now->tm_hour);
    display.print(now->tm_sec%2 ? " ":":");
    display.print(now->tm_min<10?"0":"");
    display.print(now->tm_min);
  }

/*
  if (wifi>99) wifi=99;
  display.setCursor(100,30);
  display.println(String("")+"WiFi:"+wifi+"%");
  */
  yield();
}

void img() {
  display.clearDisplay();
  for (int i=0; i<6; i++) {
    delay(1);
    String line = client.readStringUntil('\n');
    for (int x=0; x<128; x++) {
      char c = line[x];
      byte b = 0;
      if (c>='A' && c<='Z') b = c-'A';
      else if (c>='a' && c<='z') b = c-'a'+26;
      else if (c>='0' && c<='9') b = c-'0'+52;
      else if (c=='+') b = 62;
      else b = 63;
      delay(0);
      //Serial.print(b<16 ? ":0":":");
      //Serial.print(b, HEX);
      for (int y=0; y<6; y++) {
        if (b&(1<<y)) display.drawPixel(x, y+i*6, WHITE);
        else  display.drawPixel(x, y+i*6, BLACK);
      }
    }
  }
  display.display();
}

void error(String msg) {
  if (display_prio > 9) return;
  display_prio = 9;
  display_expire = millis()+1000*20;
  toneKO();
  Serial.println(msg);

  yield();
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setFont();
  display.setCursor(0,0);
  display.println(msg);
  display.setFont(&Picopixel);
  yield();
  display.display();
}


void info(String s) {
  if (display_prio > 1) return;
  display_prio = 1;
  display_expire = millis()+1000*20;

  yield();
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setFont();
  display.setCursor(0,0);
  display.println(s);
  yield();
  display.display();
}

void welcome() {
  display_prio = 1;
  display_expire = millis()+1000*5;

  yield();
  display.clearDisplay();
  display.setTextSize(1);
  //display.setFont(&Picopixel);
  //display.setCursor(0,4);
  display.setFont(NULL);
  display.setCursor(0,0);
  display.println("I'm Ready to scan");
  display.setCursor(0,14);
  display.println("Hold button down to");
  display.setCursor(0,24);
  display.println("Check-IN while scan");
  yield();
  display.display();
}


byte* queue_find(const byte* uid) {
  //Serial.print("queue search "); jmy622.hexprint(uid, 8); Serial.println();
  
  for(int p=0; p<q_pos;) {
    yield();
    byte* record = queue+p;

//    Serial.print("pos: 0x");
//    if (p<16) Serial.print("000");
//    else if (p<256) Serial.print("00");
//    else if (p<16*256) Serial.print("0");
//    Serial.print(p, HEX);
//    Serial.print(": ");
//    jmy622.hexprint(record, 8);
    
    if (memcmp(uid, record, 8)) {
      //Serial.println(" NO");
      p += record_size;
    } else {
      //Serial.println(" FOUND");
      return record;
    }
  }
//  Serial.println("Not in queue");
  return NULL;
}

int scan() {
  byte buf[read_blocks*4];
  if (q_pos >= q_size - record_size) {
    error("Queue full");
    return 0;
  }
  int ct = 0;
  for(; ct<100; ct++) {
    yield();
    const byte* uid = jmy622.iso15693_scan();
    
    if (!uid) break;

    if (memcmp(uid, wrt, 8)==0) { // WRT id match
      Serial.print("should write: ");
      jmy622.hexprint(uid, 8);
      Serial.println();
      jmy622.hexdump(wrt+8, read_blocks*4);
      jmy622.iso15693_write(start_block,read_blocks, wrt+8);
      toneOK();
      memset(wrt, 0, 8);
      delay(10);
    }

    yield();
    const byte* data = jmy622.iso15693_read(start_block,read_blocks);
    if (!data) break;
    yield();
    memcpy(buf, data, read_blocks*4);
    // iso_quiet() will overwrite the data, so let's have a safe copy

    Serial.print("ISO15693 ");jmy622.hexprint(uid, 8); Serial.println();
    jmy622.hexdump(buf, read_blocks*4);
    Serial.println("END");
    
    if (!jmy622.iso15693_quiet()) break;

    byte* record = queue_find(uid);
    if (record) {
    } else {
      if (q_pos + record_size > q_size) {
        error("NO MORE SPACE");
        break;
      }
      record = queue + q_pos;
      memcpy(record, uid, 8);
      q_pos += record_size;
//      Serial.print("New tag: ");
//      jmy622.hexprint(uid, 8);
//      Serial.print(", queue is ");
//      Serial.print(q_pos);
//      Serial.print("/");
//      Serial.println(q_size);
      count_total++;
      count_from_idle++;
      count_shelf++;
    }

    record[8] = (state == 2) ? 1 : 0; // 1 if checkin, 0 otherwise
    record[9] = read_blocks*4; // align

    memcpy(record+10, buf, read_blocks*4);

    if (state==2) {
       toneTock();
    } else {
       toneTick();
    }
    
//    Serial.println("record: ");
//    jmy622.hexdump(record, record_size);

    for (int i=10; i<record_size-10; i++) {
      if (memcmp((char*)(record+i), "SHELF#", strlen("SHELF#"))) continue;
      i+= strlen("SHELF#");
      strncpy(shelf, (char*)(record+i), 32);
      //Serial.println(shelf);
      count_shelf = 0;
    }
  }
  return ct;
}

void setup() {
  //Serial.begin(9600);
  Serial.begin(115200);
  SSerial.begin(19200);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(CHECKIN_PIN, INPUT_PULLUP);
  pinMode(A0, INPUT);
//  jmy622.debug = 3;

  delay(100);
  display.begin();
  display.clearDisplay();
  display.setRotation(2);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setFont(&Picopixel);
  display.setCursor(0,4);
  display.print("INIT... built ");
  display.print(__DATE__);
  display.print(" - ");
  display.println(__TIME__);
  display.display();

  toneOK();
  delay(200);


  attachInterrupt(digitalPinToInterrupt(CHECKIN_PIN), checkin, CHANGE);

  WiFi.mode(WIFI_STA);
//  WiFi.setSleepMode(WIFI_LIGHT_SLEEP);
  wifi_set_sleep_type(LIGHT_SLEEP_T);
  gpio_pin_wakeup_enable(GPIO_ID_PIN(CHECKIN_PIN), GPIO_PIN_INTR_ANYEDGE);
//  WiFi.macAddress(mac);

  yield();
  delay(100);


  yield();
  Serial.println();
  Serial.println(String("INIT... build ")+__DATE__+" "+__TIME__);

  send_buffer = (byte*)malloc(q_size+8+32);
  if (!send_buffer) {
    error("Can't allocate minumum queue memory");
    while(1) delay(1000);
  }

  send_buffer[0] = 0x42;
  send_buffer[1] = 0x42;
  WiFi.macAddress(send_buffer+2);
  send_buffer[8] = '\0'; // shelf
  queue = send_buffer+8+32;
  shelf_reset();
  Serial.println(String("queue ")+(q_size/1024)+"Kb ("+(q_size/record_size)+" records)");


  jmy622.info();

  if (digitalRead(CHECKIN_PIN)==LOW) { // PRESS
    toneWAIT();
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setFont();
    display.setCursor(0,0);
    display.println("Software update");
    display.display();
    while(WiFi.status() != WL_CONNECTED) {
      delay(200);
    }
    software_update(&client, AUTOUPDATE_HOST, AUTOUPDATE_PORT, AUTOUPDATE_PATH);
  }

  delay(200);
  toneOK();
  welcome();
}

void sleep() {
  Serial.println("Zzzzz...");
  state = 0;
  shelf_reset();
  //WiFi.mode(WIFI_OFF);
  yield();
  jmy622.idle();
  yield();
  display.clearDisplay();
  display.display();
  yield();
  display.command(SSD1305_DISPLAYOFF);
  yield();
}

void wake_up() {
  Serial.println("Waking up...");
  toneOK();
  yield();
  display.command(SSD1305_DISPLAYON);
  display.clearDisplay();
  display.display();
  
  //yield();
  //WiFi.mode(WIFI_STA);
  yield();
  idle_after = millis()+1000*IDLE_TIME;
  count_from_idle = 0;
}

int prev_state = 0;
long prev_press = 0;
void loop() {
  if (state==-1) {
    wake_up(); 
    state = 1;
  }
  
  // IDLE LOOP?
  if (state==0) {
    delay(1000);
    return;
  }

  if (millis() > idle_after) {
    sleep();
    return;
  }

  long now = millis();
  if (prev_state != state) {
//    Serial.printf("change %d - %d = %d\n", now, prev_press, now - prev_press);
    if (state == 1) { // release
      if (now - prev_press < 1000) {
        shelf_reset();
      }
      prev_press = now;
    }
  }
  prev_state = state;
  
  if (scan()) {
    //Serial.println("-------------");
    idle_after = now+1000*IDLE_TIME;
  } else {
    update_display();
    wifi_recv();
    wifi_send();
    wifi_init();
  }
}

long wifi_timeout = 0;
int wifi_connect() {
  if (WiFi.status() == WL_CONNECTED) {
      int rssi = WiFi.RSSI();
      int rssi_min = -80;
      int rssi_max = -40;
      rssi = rssi < rssi_min ? 0 : rssi > rssi_max ? 100 : (rssi-rssi_min)*100/(rssi_max-rssi_min);
      return rssi;
  }

  if (millis()<wifi_timeout) {
    return 0;
  }
  yield();

#ifdef WIFI_SSID
#ifdef WIFI_PASS
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("connecting to: "); Serial.println(WIFI_SSID);
  wifi_timeout = millis() + 1000*20; // wait 20 seconds before trying another network
  return 0;
#endif
#endif

  Serial.println("scanning WiFi networks...");
  int ct = WiFi.scanNetworks();
  for (int i=0; i<ct; i++) {
    yield();
    String ssid = WiFi.SSID(i);
    const char* pwd = wifi_pass(ssid.c_str());
    if (!pwd) {
      Serial.printf("SSID '%s' unknown\n", ssid.c_str());
      continue;
    }
    int rssi = WiFi.RSSI(i);
    rssi = rssi < -90 ? 0 : rssi > -50 ? 100 : (rssi+90)*100/(90-50);
    if (rssi<10) {
      Serial.printf("SSID '%s' is weak\n", ssid.c_str());
      continue;
    }

    WiFi.begin(ssid.c_str(), pwd);
    Serial.print("connecting to: "); Serial.println(ssid.c_str());
    wifi_timeout = millis() + 1000*20; // wait 20 seconds before trying another network
    return 0;
  }
  wifi_timeout = millis() + 1000*20; // try again in 20 seconds
  return 0;
}

long wifi_wait = 0;
long next_ping = 0;
int wifi_send() {
  if (!q_pos && millis() < next_ping) return 0; // nothing to do
  if (client.connected()) { // already waiting for an answer
    return 0;
  }
  if (wifi_connect()<30) { // poor wifi
    return 0;
  }
  if (millis()<wifi_wait) { // I should wait
    return 0;
  }
  wifi_wait = millis()+1000*1; // antiflood

  int len = q_pos+8+32;
  if (!client.connect(host(), port())) {
    Serial.println(String("client.connect(")+host()+":"+port()+") failed");
    error("server connection failed");
    return 0;
  }
  next_ping = millis()+1000*60;
  //Serial.println(String("Sending data ")+millis());
  client.print(String("POST ") + url() + " HTTP/1.0\r\n" +
               "Host: " + host() + "\r\n" + 
               "X-Scanner-Ver: "+__DATE__+" "+__TIME__+"\r\n" +
               "Content-Length: "+ (len) +"\r\n" +
               "Connection: close\r\n\r\n");

//  Serial.println("SENDING:");
//  jmy622.hexdump(send_buffer, len);
  int sent = 0;
  while(sent < len) {
    int w = client.write((const byte*)(send_buffer+sent), len-sent);
    if (w<1) {
      error("server send failed");
      wifi_wait = millis()+1000*15; // 15 seconds wait after errors
      return 0;
    }
    sent += w;
  }
//  Serial.print("sent "); Serial.print(sent); Serial.println(" bytes");
  wifi_timeout = millis()+1000*15; // read timeout
  q_pos = 0;
  memcpy(send_buffer+8, shelf, 32); // mark for the next data
}

long wifi_init_wait = 0;
int wifi_init() {
  if (client.connected()) { // already waiting for an answer
    return 0;
  }
  if (wifi_connect()<30) { // poor wifi
    return 0;
  }
  return 1;
  if (millis()<wifi_init_wait) { // I should wait
    return 0;
  }
  wifi_init_wait = millis()+1000*300; // antiflood
  if (!client.connect(host(), port())) {
    Serial.println(String("client.connect(")+host()+":"+port()+") failed");
    error("server connection failed");
    return 0;
  }
  client.print(String("GET ") + url() + " HTTP/1.0\r\n" +
               "Host: " + host() + "\r\n" + 
               "X-Scanner-Ver: "+__DATE__+" "+__TIME__+"\r\n" +
               "Connection: close\r\n\r\n");
  wifi_timeout = millis()+1000*15; 
}

int wifi_recv() {
  if (!client.connected()) return 0;
  if (millis() > wifi_timeout) {
    error("timeout waiting from server");
    client.stop();
    return 0;
  }
  if (!client.available()) return 0;

//  Serial.println("RECV");

  String cmd = client.readStringUntil('\n');
  
//  Serial.println(cmd);
  while(client.available()) {
    yield();
    String line = client.readStringUntil('\n');
    if (line.length()<2) break;
//    Serial.println(line);
  }
//  Serial.println("..");

  for(;;) {
    String msg = client.readStringUntil('\n');
    if (!msg) break;
    Serial.println("received: "); Serial.println(msg);

    if (msg.equals("")) {
      // ignore empty strings
    }
    else if (msg.equals("WRT")) {
      toneWAIT();
      int len = client.readStringUntil('\n').toInt();
      if (len>64) {
        error("WRT buffer overflow");
      } else {
        memset(wrt, 0, 64);
        client.read(wrt, len);
        Serial.println("WRT");
        jmy622.hexdump(wrt, len);
        jmy622.iso15693_ready(wrt);
      }
    }
    else if (msg.equals("LOCAL_EPOCH")) {
      String e = client.readStringUntil('\n');
      epoch = e.toInt()-millis()/1000;
    }
    else if (msg.equals("IMG")) {
//      Serial.print("IMG current display prio is: "); Serial.println(display_prio);
      if (display_prio <= 1) {
        display_prio = 1;
        display_expire = millis()+1000*15;
        img();
      } else {
        client.readStringUntil('\n');
        client.readStringUntil('\n');
        client.readStringUntil('\n');
        client.readStringUntil('\n');
        client.readStringUntil('\n');
        client.readStringUntil('\n');
      }
    }
    else if (msg.equals("PIMG")) {
      Serial.print("IMG current display prio is: "); Serial.println(display_prio);
      if (display_prio <= 5) {
        tonePICK();
        display_prio = 5;
        display_expire = millis()+1000*30;
        img();
      } else {
        client.readStringUntil('\n');
        client.readStringUntil('\n');
        client.readStringUntil('\n');
        client.readStringUntil('\n');
        client.readStringUntil('\n');
        client.readStringUntil('\n');
      }
    }
    else if (msg.equals("VIMG")) {
      Serial.print("IMG current display prio is: "); Serial.println(display_prio);
      if (display_prio <= 5) {
        toneVERIFY();
        display_prio = 5;
        display_expire = millis()+1000*30;
        img();
      } else {
        client.readStringUntil('\n');
        client.readStringUntil('\n');
        client.readStringUntil('\n');
        client.readStringUntil('\n');
        client.readStringUntil('\n');
        client.readStringUntil('\n');
      }
    }
    else if (msg.equals("SOFTWARE_UPDATE")) {
      String path = client.readStringUntil('\n');
      client.stop(); // software update take priority
      Serial.print("SOFTWARE UPDATE: "); Serial.println(path);
      error("Automatic Update\nPlase wait\n(several minutes)");
      software_update(&client, AUTOUPDATE_HOST, AUTOUPDATE_PORT, path.c_str());
      error("Update failed");
      delay(2000);
      ESP.restart();
    }
    else if (msg.equals("END")) {
      break;
    }
    else {
      Serial.println(String("UNKNOWN CMD: '")+msg+"'");
      wifi_wait = millis()+1000*10;
      break;
    }
  }
  client.stop();
  
  return 1;
}

