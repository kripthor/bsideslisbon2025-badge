#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_system.h>
#include <HTTPClient.h> 
#include <Update.h>     
#include <string.h>
#include <stdint.h>
#include <stdlib.h> // For strtoul
#include <Preferences.h>
#include "esp_wifi.h"
#include "esp_pm.h"
#include <esp_bt.h>
#include <WiFiClientSecure.h>
#include <Arduino_JSON.h>
#include <time.h>
#include <easter.h>
#include "badgepki.h"

struct Btn { 
  uint8_t pin;
  bool prev; 
  unsigned long lastPressTime; 
};

// Enum for key press buffer
enum ButtonID {
  BTN_ID_NONE,
  BTN_ID_A,
  BTN_ID_B,
  BTN_ID_UP,
  BTN_ID_DOWN,
  BTN_ID_LEFT,
  BTN_ID_RIGHT
};

void powerOff();
bool connectToWiFi();
bool checkBtnPress(Btn &b, uint8_t buttonId);
uint16_t mapJetColor(uint8_t intensity);
uint16_t intensityToGlowColor(uint8_t intensity);
void drawFrameBufferBox(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void drawCircuitBackground();
bool checkAndTriggerKonami();
void drawSetTimeScreen(bool forceRedraw); // New function prototype

// ==== 1. HARDWARE & DISPLAY DEFINES ====
#define PIN_KEEPALIVE 6
#define TFT_CS         2
#define TFT_RST        3
#define TFT_DC         4
#define PIN_DISPLAYLED 5 
#define DISPLAY_WIDTH  160
#define DISPLAY_HEIGHT 128

#define BUTTON_A    22
#define BUTTON_B    23
#define BUTTON_UP    7
#define BUTTON_DOWN 15
#define BUTTON_LEFT  9
#define BUTTON_RIGHT 18
#define DEBOUNCE_DELAY 30

// ==== 2. COLOR DEFINES ====
#define COL_BG          ST77XX_BLACK
#define COL_ACCENT      0x07E0
#define COL_ACCENT_DARK 0x03E0
#define COL_PRIMARY     ST77XX_CYAN
#define COL_STATUS_BG   0x0008
#define COL_GRID        0x10A2
#define COL_SEPARATOR   0x3186
#define COL_WARNING     ST77XX_YELLOW
#define COL_ERROR       ST77XX_RED
#define COL_NEON_PURPLE 0xE01F
#define COL_WHITE       ST77XX_WHITE
#define COL_GLITCH_ORANGE 0xFD20
#define COL_CIRCUIT_TRACE 0x4208

// ==== 3. UI & MENU DEFINES ====
static const uint8_t STATUS_H = 10; 
static const uint8_t BOTTOM_STATUS_H = 10; 
static const uint8_t PADDING = 1; 
#define STATUS_MESSAGE_DURATION_MS 2000
#define BACKLIGHT_TIMEOUT_MS 60000
#define POWER_OFF_TIMEOUT_MS 900000

enum Screen {
  SCREEN_MAIN_MENU,
  SCREEN_SCAN,
  SCREEN_DEBUG_MENU,
  SCREEN_OTA_PROCESS,
  SCREEN_EXPLOITS,
  SCREEN_REBOOT,
  SCREEN_REMOTE_FW_UPDATE,
  SCREEN_REMOTE_FACT_RESET,
  SCREEN_REMOTE_POWER_OFF,
  SCREEN_SETTINGS,       
  SCREEN_DEFENSES,
  SCREEN_ATTACK_STATUS,
  SCREEN_FORCE_REGISTER,
  SCREEN_CLEAR_PREFS,
  SCREEN_SHOW_DATA,
  SCREEN_HACKERMAN,
  SCREEN_2038,
  SCREEN_SET_TIME, // New screen
  SCREEN_LOGS,
  SCREEN_MASTER_TROLL
};

struct MenuItem {
  const char* title;
  Screen targetScreen;
};


// ==== 4. ESPNOW & GRID DEFINES ====
#define ARTIFACT '*'
#define SPONSOR '+'
#define BADGE_ACTIVE_TIMEOUT_MS 30000
#define RSSI_MIN_CAP -90
#define RSSI_MAX_CAP -35

#define BADGE_PIXEL_SIZE 4
#define BADGE_GRID_ROWS 17
static const uint8_t GRID_WIDTH_PX = DISPLAY_WIDTH;
#define GRID_WIDTH_BADGES (GRID_WIDTH_PX / BADGE_PIXEL_SIZE)
#define MAX_BADGE_TRACKING (GRID_WIDTH_BADGES * BADGE_GRID_ROWS)
static const uint8_t FULL_GRID_HEIGHT_PX = (BADGE_PIXEL_SIZE * BADGE_GRID_ROWS); 

static const uint8_t GRID_HEADER_H = 14; 
static const uint8_t GRID_Y_START = 14 + GRID_HEADER_H;

#define GLOW_UPDATE_INTERVAL_MS 120
#define FADE_RATE 1
#define HACKERMAN_SCREEN_DURATION_MS 5000

// ==== 5. OTA & SERVER DEFINES ====
#define OTA_BUFF_SIZE 4096 


/*--- Polyalphabetic Cipher Test for fun and games ---
KEY  42 Obfuscated:  gPmuzxA1343
Deobfuscated: wifi-badges
KEY  69 Obfuscated:  IgYAuQmLBHf-WRtBv1.fKKOOg
Deobfuscated: Qaz123Edc654.aaaammmm890.
KEY  31337 Obfuscated:  T2y6d5iYLQXs-syQb4A.CqiFmLDogmjocb4DkTLdeeoC3RP1sZ
Deobfuscated: https://badges2025.kripthor.xyz:33666/firmware.bin
KEY  666 Obfuscated:  tpJiuelVmnU8sp6U.aNFdOu5RTYqJeEw9.
Deobfuscated: https://badge2038.bsideslisbon.org
KEY  667 Obfuscated:  BJHYEZV3rRPmLmHRwxsGe9V71rXhp1ahg94Dk
Deobfuscated: https://badges2025.kripthor.xyz:58000
*/

char ijjijjiijijijijiij[] = "gPmuzxA1343";
char ijijjiijiiijjiijijjji[] = "IgYAuQmLBHf-WRtBv1.fKKOOg";
char ijijjiijjiijiijjjijijijj[] = "T2y6d5iYLQXs-syQb4A.CqiFmLDogmjocb4DkTLdeeoC3RP1sZ";
//const char* ijjjjiiijijiiijjijjji = "https://badges2025.kripthor.xyz:58000";
char ijjjjiiijijiiijjijjji[] = "tpJiuelVmnU8sp6U.aNFdOu5RTYqJeEw9.";
//cert_pin = bsides_cert;

const char* currentVersion = "2.53"; // Version incremented
const char* badgeType = "normal";

// ==== 6. GLOBAL OBJECTS & STATE ====
Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);
Preferences preferences;
WiFiClientSecure client; 
HTTPClient http;

// Main Menu structure
const MenuItem MAIN_MENU_ITEMS[] = {
  {"[SCAN] / TARGET SELECT", SCREEN_SCAN},         
  {"[ATTACK] / EXPLOITZ", SCREEN_EXPLOITS},
  {"[DEFEND] / FIREWALLZ", SCREEN_DEFENSES}, 
  {"[STATS] / LOGZ", SCREEN_LOGS},
  {"[SETTINGS] / TWEAKZ", SCREEN_SETTINGS}
};
const uint8_t MENU_ITEM_COUNT = sizeof(MAIN_MENU_ITEMS) / sizeof(MenuItem);

// Debug Menu Structure
#ifdef MASTERBADGE
const MenuItem DEBUG_MENU_ITEMS[] = {
  {"[1] / FIRMWARE UPDATE", SCREEN_OTA_PROCESS},
  {"[2] / FACTORY RESET", SCREEN_CLEAR_PREFS},
  {"[3] / REMOTE FW UPDATE",  SCREEN_REMOTE_FW_UPDATE},
  {"[4] / REMOTE FACT RESET", SCREEN_REMOTE_FACT_RESET},
  {"[5] / REMOTE POWER OFF", SCREEN_REMOTE_POWER_OFF},
  {"[6] / SHOW BADGE DATA", SCREEN_SHOW_DATA},
  {"[7] / MASTER TROLL", SCREEN_MASTER_TROLL}
};
#else
const MenuItem DEBUG_MENU_ITEMS[] = {
  {"[1] / FIRMWARE UPDATE", SCREEN_OTA_PROCESS},
  {"[2] / FACTORY RESET", SCREEN_CLEAR_PREFS},
  {"[3] / SHOW BADGE DATA", SCREEN_SHOW_DATA}
};
#endif

const uint8_t DEBUG_ITEM_COUNT = sizeof(DEBUG_MENU_ITEMS) / sizeof(MenuItem);

// Exploit & Defense Pairs
struct ExploitDefensePair {
  const char* exploitName;
  const char* defenseName;
};

const ExploitDefensePair CYBER_PAIRS[] = {
  {"DEEPFAKE AI CAMPAIGN",    "AI FACE/VOICE CHECK"},
  {"LIGHTSPEED PACKET FLOOD", "DDOS DARKHOLE SHIELD"},
  {"MALWARE DEPLOYMENT",      "EDR CONTINOUS SCANNING"},
  {"PHISHING CAMPAIGNS",      "NONSTOP AWARENESS TRAIN"},
  {"SCHRODINGER RANSOMWARE",  "MULTI GEO CLOUD BACKUPS"},
  {"ESS-CUE-ELL INJECTION",   "ESCAPE ALL DUMB INPUTS"},
  {"EPOCH DESYNCHRONIZATION", "SECURE NTP INFRASTRUCT"},
  {"API ABUSE EXFILTRATION",  "RATE LIMIT WAF WOF"},
  {"CREDENTIAL STUFFING UP",  "MFA ALL THE AUTH THINGZ"}
};
const uint8_t CYBER_PAIR_COUNT = sizeof(CYBER_PAIRS) / sizeof(ExploitDefensePair);
#define MAX_ACTIVE_EXPLOITS 3
#define MAX_ACTIVE_DEFENSES 5

// ESP-NOW global peer info
esp_now_peer_info_t peerInfo;
const uint8_t BROADCAST_MAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
unsigned long lastBeaconTime = 0;
uint8_t myMac[6];

// Badge grid state
struct PeerInfo {
    uint8_t mac_addr[6] = {0, 0, 0, 0, 0, 0};
    char nickname[17] = {0};
    int8_t rssi = -127; 
    unsigned long last_seen = 0; 
    uint32_t badgeIdentifier = 0;
};
PeerInfo all_peers[MAX_BADGE_TRACKING];
uint8_t badge_intensity[MAX_BADGE_TRACKING]; 
uint8_t last_hits[MAX_BADGE_TRACKING]; 

uint16_t badge_framebuffer[GRID_WIDTH_PX * FULL_GRID_HEIGHT_PX]; 
unsigned long lastGlowDraw = 0;

// Scan screen navigation state
uint16_t selected_badge_id = 0;
bool selection_active = false;

// Global UI state
Screen currentScreen = SCREEN_MAIN_MENU;
Screen parentScreen = SCREEN_MAIN_MENU;  // Track parent for hierarchical navigation
Screen screenToReturnTo = SCREEN_SCAN;
bool exploitScreenInAttackMode = false;  // True when entered from Scan with target selected
uint8_t selectedItem = 0; 
uint8_t lastSelectedItem = 0; 
uint32_t currentScore = 0;
uint32_t currentPosition = 666;
// Timekeeping
char currentTimeStr[9] = "--:--:--";
char currentDateStr[11] = "--/--/----";
long timeOffset = 0;
bool timeIsSynced = false;
extern bool snakeExit;
// Manual Time Set state
struct tm stagingTimeInfo;
uint8_t selectedTimeField = 0; // 0=day, 1=mon, 2=year, 3=hour, 4=min
static uint8_t lastSelectedTimeField = 0;

bool stressTestEnabled = false;
bool showAllExploits = false;
uint8_t selectedSettingItem = 0;
uint8_t lastSelectedSettingItem = 0;
uint8_t selectedLogItem = 0;
uint8_t lastSelectedLogItem = 0;
uint8_t selectedExploit = 0;
uint8_t selectedDefense = 0;
uint8_t lastSelectedExploit = 0;
uint8_t lastSelectedDefense = 0;
unsigned long lastButtonPressTime = 0;
bool backlightOn = true;
unsigned long currentBeaconInterval = 5000;

int knownWifiChannel = 0;

// Top Lists Data - [0]=attackers, [1]=victims
char topListNicknames[2][3][17] = {{{0}}}; // 2 lists, 3 entries each, 16 chars + null
uint8_t topListCounts[2][3] = {{0}};       // Attack counts for each entry

// Badge Server Identity
uint32_t badgeIdentifier = 0;
uint32_t secretToken = 0;
String badgeNickname = "Icepick";

// Exploit/Defense active state
bool active_exploits[CYBER_PAIR_COUNT];
bool active_defenses[CYBER_PAIR_COUNT];
bool exploitsChanged = false;  // Track if exploits were modified in current session
bool defensesChanged = false;  // Track if defenses were modified in current session

// Attack Status Screen
char statusMessageLine1[40] = "";
char statusMessageLine2[40] = "";
char statusMessageLine3[40] = "";
unsigned long statusMessageStartTime = 0;
volatile bool showStatusMessage = false;
char bottomBarMessage[40] = "";
unsigned long bottomBarMessageTime = 0;
unsigned long hackermanScreenStartTime = 0;

// ESP-NOW Packet Handling (volatile for ISRs)
volatile bool newExploitPacket = false;
volatile bool newExploitResultPacket = false;
uint8_t lastAttackerMac[6];
uint8_t lastExploitHitsMac[6];
volatile char lastAttackerNickname[17];
volatile char lastExploitList[12] = "";
volatile bool newFwUpdatePacket = false;
uint8_t lastExploitData[3];
volatile uint8_t lastExploitHits = 0;
volatile bool isIncomingExploit = false;
volatile bool isIncomingResult = false;
volatile bool newFactoryResetPacket = false;
bool wokeScreenForExploit = false; 

unsigned long lastSilentSyncTime = 0;
unsigned long currentSilentSyncInterval = 240000; 

bool pendingExploitSync = false;
uint32_t pendingTargetId = 0;
bool pendingDefenseSync = false;
bool pendingExploitPost = false;
bool pendingDefensePost = false;
bool trypoweroff = true;

// Scan screen header rotation
#define HEADER_CHANGE_INTERVAL_MS 4000
const char* SCAN_HEADERS[] = {
  "FINGERPRINTING THE GIBSON",
  "SCANNING THE AI BLOCKCHAIN",
  "PROBING POST-QUANTUM CYBER",
  "CHECKING AGENTIC FIREWALLS",
  "DEFRAGGING DATETIME DESYNC",
  "ENUMERATING ALL THE LLAMAS",
  "NMAPPING SHADOW CYBERSPACE"
};
const uint8_t SCAN_HEADER_COUNT = sizeof(SCAN_HEADERS) / sizeof(const char*);
uint8_t currentScanHeader = 0;

// OTA state
enum OtaState {
  OTA_IDLE,
  OTA_CONNECTING_WIFI,
  OTA_CHECKING,
  OTA_DOWNLOADING,
  OTA_SUCCESS,
  OTA_FAILED
};
OtaState otaState = OTA_IDLE;
String otaStatusMessage = "SYSTEM READY. CHECK FOR UPDATES.";
int otaProgress = 0; 
unsigned long otaStartTime = 0;
char bootMessage[40] = "";

// Button debounce state
Btn btnA{BUTTON_A,false,0}, btnB{BUTTON_B,false,0}, btnUp{BUTTON_UP,false,0}, 
    btnDn{BUTTON_DOWN,false,0}, btnLt{BUTTON_LEFT,false,0}, btnRt{BUTTON_RIGHT,false,0}; 

// Key press history buffer
bool upPressed;
bool downPressed;
bool ltPressed;
bool rtPressed;
bool aPressed;
bool bPressed;
#define KEY_BUFFER_SIZE 10
uint8_t keyPressBuffer[KEY_BUFFER_SIZE];
uint8_t keyPressBufferIndex = 0;

// Key repeat functionality (for holding buttons)
#define KEY_REPEAT_INITIAL_DELAY 500  // ms to wait before repeat starts
#define KEY_REPEAT_RATE 150            // ms between repeats (approx 6-7 per second)
unsigned long upHoldStartTime = 0;
unsigned long downHoldStartTime = 0;
unsigned long leftHoldStartTime = 0;
unsigned long rightHoldStartTime = 0;
unsigned long lastUpRepeatTime = 0;
unsigned long lastDownRepeatTime = 0;
unsigned long lastLeftRepeatTime = 0;
unsigned long lastRightRepeatTime = 0;

bool snakeStart = false;
bool snakeRun = false;

// ---- Anti-Flicker State Tracking ----
static uint32_t lastScore = 0xFFFFFFFF;
static uint32_t lastPosition = 0xFFFFFFFF;
static char lastTimeStr[9] = "XXXXXXXX";
static char lastDateStr[11] = "XXXXXXXXXX";
static IPAddress lastIP = IPAddress(255, 255, 255, 255);
static uint8_t lastWifiStatus = 0xFF; 
static OtaState lastOtaState = OTA_FAILED; 
static int16_t last_displayed_badge_id = -1;
static bool last_selection_active = false;  
static char lastAttackerNickDrawn[17] = "";
static char lastExploitListDrawn[12] = "";

// ==== System Task: Keep-alive ====
void TaskKeepAlive(void *pvParameters) {
  pinMode(PIN_KEEPALIVE, OUTPUT);
  digitalWrite(PIN_KEEPALIVE, HIGH);
  for (;;) {
    delay(5000);
    digitalWrite(PIN_KEEPALIVE, LOW);
    delay(100);
    digitalWrite(PIN_KEEPALIVE, HIGH);
  }
}

/**
 * @brief Sends two pulses to the keep-alive pin to signal power off.
 */
void powerOff() {
    if (!trypoweroff) {
        currentScreen = SCREEN_MAIN_MENU;
        drawCurrentScreen(true);
        return;
    }
    trypoweroff = false;
    //Serial.println("POWER OFF: Pulsing keep-alive low.");
    digitalWrite(PIN_KEEPALIVE, LOW);
    delay(100);
    digitalWrite(PIN_KEEPALIVE, HIGH);
    delay(500);
    digitalWrite(PIN_KEEPALIVE, LOW);
    delay(100);
    digitalWrite(PIN_KEEPALIVE, HIGH);
    delay(500);
    //Serial.println("POWER OFF: Pulse sequence sent. If you see this you are running on cable.");
}

// ==== Version Comparison ====
/**
 * @brief Compare two version strings (e.g., "2.65" vs "3.10").
 * @param version1 First version string
 * @param version2 Second version string
 * @return 1 if version1 > version2, -1 if version1 < version2, 0 if equal
 */
int compareVersions(const char* version1, const char* version2) {
    int major1 = 0, minor1 = 0;
    int major2 = 0, minor2 = 0;
    
    // Parse version1 (e.g., "2.65")
    sscanf(version1, "%d.%d", &major1, &minor1);
    
    // Parse version2 (e.g., "3.10")
    sscanf(version2, "%d.%d", &major2, &minor2);
    
    // Compare major version first
    if (major1 > major2) return 1;
    if (major1 < major2) return -1;
    
    // Major versions are equal, compare minor version
    if (minor1 > minor2) return 1;
    if (minor1 < minor2) return -1;
    
    // Versions are equal
    return 0;
}

// ==== Button Debounce Logic ====
/**
 * @brief Wrapper for button presses. Handles debounce and backlight wake-up.
 * The first press on a dark screen will only wake it and be consumed.
 */
bool checkBtnPress(Btn &b, uint8_t buttonId) {
  bool cur = (digitalRead(b.pin) == LOW); 
  bool edge = cur && !b.prev; 
  b.prev = cur; 

  if (edge) {
    unsigned long now = millis();
    if (now - b.lastPressTime > DEBOUNCE_DELAY) {
      b.lastPressTime = now;
      lastButtonPressTime = now;

      // Add to the key press buffer
      keyPressBuffer[keyPressBufferIndex] = buttonId;
      keyPressBufferIndex = (keyPressBufferIndex + 1) % KEY_BUFFER_SIZE; // Wrap around
      
      // Check for Konami code
      if (checkAndTriggerKonami()) {
        delay(500);
        return false; // Eat the keypress that triggered the code
      }

      if (backlightOn) {
        wokeScreenForExploit = false; // User pressed a button, so reset the flag
        return true; 
      } else {
        digitalWrite(PIN_DISPLAYLED, HIGH);
        backlightOn = true;
        return false;
      }
    }
  }
  return false; 
}

// ==== Color & Drawing Logic ====

/**
 * @brief Maps an intensity value (0-255) to the "Portland" colormap.
 * 0 -> Dark Blue, 101 -> Green, 169 -> Dark Yellow, 255 -> Red
 */
uint16_t mapJetColor(uint8_t intensity) {
    uint8_t r = 0, g = 0, b = 0;
    
    if (intensity < 1) { 
        r = 0;
        g = 0;
        b = 0; }
    else if (intensity < 102) {
        r = 0;
        g = map(intensity, 0, 101, 0, 255);
        b = map(intensity, 0, 101, 96, 0);
    } else if (intensity < 170) { 
        r = map(intensity, 102, 169, 0, 255);
        g = map(intensity, 102, 169, 255, 192);
        b = 0;
    } else { 
        r = 255;
        g = map(intensity, 170, 255, 192, 0);
        b = 0;
    }
    
    return tft.color565(constrain(r, 0, 255), constrain(g, 0, 255), constrain(b, 0, 255));
}




uint16_t mapNeonPurpleColor(uint8_t intensity) {
    // COL_NEON_PURPLE (0xE01F) is defined [cite: 6]
    // In 8-bit R,G,B, this color is (R: 230, G: 4, B: 255)

    // --- NEW: Define a minimum brightness floor ---
    // These values are ~25% of the max brightness, so it fades
    // to a dim purple instead of pure black.
    // You can increase these numbers to make the dimmest state even brighter.
    const uint8_t MIN_R = 57; // Was 0
    const uint8_t MIN_G = 1;  // Was 0
    const uint8_t MIN_B = 64; // Was 0

    // 1. Map Red from MIN_R (dim) to 230 (bright)
    uint8_t r = map(intensity, 0, 255, MIN_R, 240);
// 2. Map Green from MIN_G (dim) to 4 (bright)
    uint8_t g = map(intensity, 0, 255, MIN_G, 100);
// 3. Map Blue from MIN_B (dim) to 255 (bright)
    uint8_t b = map(intensity, 0, 255, MIN_B, 255);
// Combine the 8-bit components back into a 16-bit 5-6-5 color
    return tft.color565(r, g, b);
}

uint16_t intensityToGlowColor(uint8_t intensity) {
    return mapJetColor(intensity);
}

/**
 * @brief Draws a hollow rectangle directly into the badge_framebuffer.
 */
void drawFrameBufferBox(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (y >= 0 && y < FULL_GRID_HEIGHT_PX) {
        uint32_t line_index = y * GRID_WIDTH_PX;
        for (int16_t i = x; i < x + w; i++) {
            if (i >= 0 && i < GRID_WIDTH_PX) {
                badge_framebuffer[line_index + i] = color;
            }
        }
    }
    
    int16_t y_bottom = y + h - 1;
    if (y_bottom >= 0 && y_bottom < FULL_GRID_HEIGHT_PX && h > 1) {
        uint32_t line_index = y_bottom * GRID_WIDTH_PX;
        for (int16_t i = x; i < x + w; i++) {
            if (i >= 0 && i < GRID_WIDTH_PX) {
                badge_framebuffer[line_index + i] = color;
            }
        }
    }
    
    if (x >= 0 && x < GRID_WIDTH_PX && w > 1) {
        for (int16_t i = y + 1; i < y + h - 1; i++) {
            if (i >= 0 && i < FULL_GRID_HEIGHT_PX) {
                badge_framebuffer[i * GRID_WIDTH_PX + x] = color;
            }
        }
    }
    
    int16_t x_right = x + w - 1;
    if (x_right >= 0 && x_right < GRID_WIDTH_PX && w > 1) {
        for (int16_t i = y + 1; i < y + h - 1; i++) {
            if (i >= 0 && i < FULL_GRID_HEIGHT_PX) {
                badge_framebuffer[i * GRID_WIDTH_PX + x_right] = color;
            }
        }
    }
}


/**
 * @brief Draws a procedural circuit-like background in a very dark grey.
 * Follows the user's specific algorithm.
 */
void drawCircuitBackground() {
    uint8_t yStart = 28;
    uint8_t yEnd = DISPLAY_HEIGHT - BOTTOM_STATUS_H - 2;
    uint8_t fontHeight = 8;
    uint8_t bottomPadding = 8;
    uint8_t drawHeight = yEnd - yStart - fontHeight - bottomPadding;
    
    tft.setFont();
    tft.setTextSize(1);
    tft.setTextColor(COL_CIRCUIT_TRACE, COL_BG);

    for (int i = 0; i < 200; i++) {
        int16_t x = esp_random() % DISPLAY_WIDTH;
        int16_t y = yStart + (esp_random() % drawHeight);
        
        tft.setCursor(x, y);
        if (esp_random() % 2 == 0) {
            tft.print("1");
        } else {
            tft.print("0");
        }
    }
}


/**
 * @brief Draws the "Hackerman" meme screen.
 * */
void drawHackermanScreen() {
    if (esp_random() % 2 == 0) {
        tft.drawRGBBitmap(0, 0, easteregg1, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    } else {
        tft.drawRGBBitmap(0, 0, easteregg2, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    }
}

void typeText(String text, int d) {
   for (int i = 0; i < text.length(); i++) {
    tft.print(text[i]);
    delay(d);
  }
}
void typeText(String text) {
  typeText(text,100);
}

void draw2038Screen() {
    digitalWrite(PIN_DISPLAYLED, HIGH);
    backlightOn = true;
    tft.drawRGBBitmap(0, 0, broken, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    drawStatusBar();
    delay(3000);
    tft.setTextColor(ST77XX_GREEN);
    tft.setFont(&FreeSansBold9pt7b);
    tft.setCursor(10,DISPLAY_HEIGHT/2-30);
    typeText("Time...");
    delay(500);
    tft.setCursor(10,DISPLAY_HEIGHT/2);
    typeText("  it has been...");
    delay(500);
    tft.setCursor(10,DISPLAY_HEIGHT/2+30);
    typeText("broken!!! ");
    delay(3000);
    tft.drawRGBBitmap(0, 0, morty, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    delay(1000);
    tft.setFont();
    tft.setTextColor(ST77XX_RED,COL_BG);
    tft.setCursor(0,DISPLAY_HEIGHT-30);
    typeText("Only one thing left to do...");
    delay(200);
    tft.setCursor(0,DISPLAY_HEIGHT-22);
    typeText("Rick Ro... ");
    delay(1000);
    typeText("wait... ups...");
    delay(1500);
    tft.setCursor(0,DISPLAY_HEIGHT-14);
    typeText("I'm sorry Dave - Wrong ");
    delay(1000);
    typeText("Rick... ");
    delay(2000);
    tft.drawRGBBitmap(0, 0, rick, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    tft.setTextColor(ST77XX_GREEN,COL_BG);
    delay(1000);
    int yy = 56;
    tft.setCursor(5,yy); yy += 9;
    typeText("Never gonna give you up",50);
    tft.setCursor(5,yy); yy += 9;
    delay(200);
    typeText("never gonna let you down",50);
    tft.setCursor(5,yy); yy += 9;
    delay(200);
    typeText("Never gonna run around",50); 
    tft.setCursor(5,yy); yy += 9;
    delay(200);
    typeText("and desert you",50);
    tft.setCursor(5,yy); yy += 9;
    delay(200);
    typeText("Never gonna make you cry",50);
    tft.setCursor(5,yy); yy += 9;
    delay(200);
    typeText("never gonna say goodbye",50);
    tft.setCursor(5,yy); yy += 9;
    delay(200);
    typeText("Never gonna tell a lie",50);
    tft.setCursor(5,yy); yy += 9;
    delay(200);
    typeText("and hurt you",50);
    delay(3000);
    tft.drawRGBBitmap(0, 0, broken, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    delay(500);
    ESP.restart();
}

/**
 * @brief Draws a status animation in the bottom-right of the status bar.
 * Frame 0: <-> (for HTTP POST)
 * Frame 1: .
 * Frame 2: .o
 * Frame 3: .oO
 * Frame 4: (clear)
 */
void drawWifiAnim(int frame) {
    //const uint8_t yStart = DISPLAY_HEIGHT - BOTTOM_STATUS_H;
    //uint8_t iconX = DISPLAY_WIDTH - PADDING - (6 * 3);
    
    const uint8_t yStart = 0;
    uint8_t iconX = 75;
    
    tft.fillRect(iconX, yStart + PADDING, (6 * 3), BOTTOM_STATUS_H - 2 * PADDING, COL_STATUS_BG);
    tft.setFont();
    tft.setTextSize(1);
    tft.setCursor(iconX, yStart + PADDING); 
    
    switch(frame) {
        case 0: // <->
            tft.setTextColor(COL_WARNING, COL_STATUS_BG);
            tft.print("<->");
            break;
        case 1:
            tft.setTextColor(COL_PRIMARY, COL_STATUS_BG);
            tft.print(".  ");
            break;
        case 2:
            tft.setTextColor(COL_PRIMARY, COL_STATUS_BG);
            tft.print(".o ");
            break;
        case 3:
            tft.setTextColor(COL_PRIMARY, COL_STATUS_BG);
            tft.print(".oO");
            break;
        case 4: // Clear
        default:
            // Already cleared by fillRect
            break;
    }
}

// ==== ESPNOW Handler Functions ====

// OnDataRecv: Called from ISR. Must be fast.
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    
    // Beacon packet (from anyone)
    if (len == 23 && data[0] == 'B' && data[1] == 'C' && data[2] == 'N') {
        int8_t current_rssi = recv_info->rx_ctrl->rssi; 
        
        int8_t capped_rssi = current_rssi;
        if (capped_rssi < RSSI_MIN_CAP) capped_rssi = RSSI_MIN_CAP;
        else if (capped_rssi > RSSI_MAX_CAP) capped_rssi = RSSI_MAX_CAP;

        uint32_t peerBadgeIdentifier;
        memcpy(&peerBadgeIdentifier, &data[3], sizeof(uint32_t));
        
        uint16_t badge_id;
        if (peerBadgeIdentifier != 0) {
            badge_id = peerBadgeIdentifier % MAX_BADGE_TRACKING;
        } else {
            uint16_t mac_seed = (uint16_t)recv_info->src_addr[4] * 256 + recv_info->src_addr[5];
            badge_id = mac_seed % MAX_BADGE_TRACKING; 
        }
        
        all_peers[badge_id].badgeIdentifier = peerBadgeIdentifier;
        all_peers[badge_id].rssi = current_rssi; 
        memcpy(all_peers[badge_id].mac_addr, recv_info->src_addr, 6);
        all_peers[badge_id].last_seen = millis();
        memcpy(all_peers[badge_id].nickname, &data[7], 16);
        all_peers[badge_id].nickname[16] = '\0';

        uint8_t new_intensity = (uint8_t)map(capped_rssi, RSSI_MIN_CAP, RSSI_MAX_CAP, 0, 255);
        new_intensity = constrain(new_intensity, 0, 255); 

        if (new_intensity > badge_intensity[badge_id]) {
            badge_intensity[badge_id] = new_intensity;
        }
    }
    // Exploit packet (to me)
    else if (len == 22 && data[0] == 'E' && data[1] == 'X' && data[2] == 'P' && (memcmp(recv_info->des_addr, myMac, 6) == 0)) {
        if (!newExploitPacket) {
            memcpy(lastAttackerMac, recv_info->src_addr, 6);
            memcpy(lastExploitData, &data[3], 3);
            memcpy((void*)lastAttackerNickname, &data[6], 16);
            lastAttackerNickname[16] = '\0';
            newExploitPacket = true;
        }
    }
    // Exploit Result packet (to me)
    else if (len == 5 && data[0] == 'R' && data[1] == 'S' && data[2] == 'L' && (memcmp(recv_info->des_addr, myMac, 6) == 0)) {
        if (!newExploitResultPacket) {
            memcpy(lastExploitHitsMac, recv_info->src_addr, 6);
            printHex("newExploitResultPacket from: ", lastExploitHitsMac, 6);
            lastExploitHits = (uint8_t) data[3];
            newExploitResultPacket = true;
        }
    }
    // Remote FW Update packet (broadcast)
    else if (len == HASH_SIZE+SIG_SIZE) { // Check for 24-byte password
        if (memcmp(data, "RMTFWU", 6) == 0) {
            if (!newFwUpdatePacket) {
                //Serial.println("Remote FIRMWARE UPGRADE received. Checking signature.");
                memcpy(pkimessage, data,HASH_SIZE);
                memcpy(pkisig, data+HASH_SIZE,SIG_SIZE);
                if (pkiverify()) {
                    //Serial.println("Signature OK. Executing!");
                    if (memcmp(data+6, currentVersion, 4) != 0) {
                        //Serial.println("Version differs. Executing!");
                        newFwUpdatePacket = true;
                    } else {
                         //Serial.println("Version is the same, not upgrading.");
                    }       
                } else {
                    //Serial.println("Signature FAILED!");        
                }
            }
        }
        // Check for Remote Factory Reset password
        else if (memcmp(data, "RMTRST", 6) == 0) {
            if (!newFactoryResetPacket) {
                //Serial.println("Remote FACTORY RESET received. Checking signature.");
                memcpy(pkimessage, data,HASH_SIZE);
                memcpy(pkisig, data+HASH_SIZE,SIG_SIZE);
                if (pkiverify()) {
                    newFactoryResetPacket = true;
                    //Serial.println("Signature OK. Executing!");
                } else {
                    //Serial.println("Signature FAILED!");        
                }
            }
        }  
        else if (memcmp(data, "RMTOFF", 6) == 0) {
            //Serial.println("Remote POWEROFF received. Checking signature.");
            memcpy(pkimessage, data,HASH_SIZE);
            memcpy(pkisig, data+HASH_SIZE,SIG_SIZE);
            if (pkiverify()) {
                //Serial.println("Signature OK. Executing!");
                tft.fillScreen(COL_BG);
                tft.setFont();
                tft.setTextColor(COL_ACCENT, COL_BG);
                tft.setFont(&FreeSansBold9pt7b);
                tft.setCursor(PADDING, STATUS_H + 28);
                tft.print("REMOTE FORCED");
                tft.setCursor(PADDING, STATUS_H + 50);
                tft.print("POWER OFF");
                tft.setFont();
                delay(500);
                powerOff();
                drawCurrentScreen(true);
            } else {
                //Serial.println("Signature FAILED!");        
            }
        } 
        else if (memcmp(data, "RMTTRL", 6) == 0) {
            //Serial.println("Remote POWEROFF received. Checking signature.");
            memcpy(pkimessage, data,HASH_SIZE);
            memcpy(pkisig, data+HASH_SIZE,SIG_SIZE);
            if (pkiverify()) {
              stagingTimeInfo.tm_year = 2038 - 1900; // tm_year is years since 1900
              stagingTimeInfo.tm_mon = 0; // 10 = Nov
              stagingTimeInfo.tm_mday = 19;
              stagingTimeInfo.tm_hour = 3;
              stagingTimeInfo.tm_min = 14;
              stagingTimeInfo.tm_sec =7;
              stagingTimeInfo.tm_isdst = -1; 
              time_t newTimestamp = mktime(&stagingTimeInfo);
              long newOffset = (long)newTimestamp - (long)(millis() / 1000);
              timeOffset = newOffset;
              //timeIsSynced = true;
              saveTimeOffset(timeOffset);
            } else {
                //Serial.println("Signature FAILED!");        
            }
        }
        else {
            //Serial.println("Remote command failure.");
        }
    }
}

void initESPNOW(bool forceClearPeers) {
    if (WiFi.getMode() != WIFI_STA) {
        WiFi.mode(WIFI_STA); 
        delay(100); 
    }

    esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_max_tx_power(44);
    
    if (esp_now_init() == ESP_OK) {
        //Serial.println("ESPNOW Initialized.");
        WiFi.macAddress(myMac);
        
        memset(&peerInfo, 0, sizeof(peerInfo));
        memcpy(peerInfo.peer_addr, BROADCAST_MAC, 6);
        peerInfo.channel = 0; 
        peerInfo.encrypt = false;
        
        if (!esp_now_is_peer_exist(BROADCAST_MAC)) {
            if (esp_now_add_peer(&peerInfo) != ESP_OK) {
                //Serial.println("Failed to add broadcast peer.");
            }
        }
        
        esp_now_register_recv_cb(OnDataRecv);

        if (forceClearPeers) { 
            memset(all_peers, 0, sizeof(all_peers)); 
            selection_active = false;
            selected_badge_id = 0;
            
            memset(badge_intensity, 0, sizeof(badge_intensity)); 
            memset(badge_framebuffer, 0, sizeof(badge_framebuffer)); 
            
            last_displayed_badge_id = -1;
            last_selection_active = false;
        }
    } else {
        //Serial.println("Error initializing ESPNOW");
    }
}

void sendBeacon() {
    uint8_t payload[23];
    payload[0] = 'B';
    payload[1] = 'C';
    payload[2] = 'N';
    
    memcpy(&payload[3], &badgeIdentifier, sizeof(uint32_t));
    
    strncpy((char*)&payload[7], badgeNickname.c_str(), 16);
    if (badgeNickname.length() < 16) {
        memset(&payload[7 + badgeNickname.length()], 0, 16 - badgeNickname.length());
    }
    
    if (esp_now_is_peer_exist(BROADCAST_MAC)) {
        esp_now_send(BROADCAST_MAC, payload, sizeof(payload));
        lastBeaconTime = millis();
    } else {
        if (WiFi.getMode() == WIFI_STA) {
            initESPNOW(false); 
        }
    }
}

// ==== Preference (NVS) Logic ====

void saveCyberSettings(bool isExploit) {
    preferences.begin("badge-os", false);
    if (isExploit) {
        preferences.putBytes("exploits", active_exploits, sizeof(active_exploits));
    } else {
        preferences.putBytes("defenses", active_defenses, sizeof(active_defenses));
    }
    preferences.end();
}

void saveHits() {
    preferences.begin("badge-os", false);
    preferences.putBytes("last_hits", last_hits, sizeof(last_hits));
    preferences.end();
}

void saveAppSettings() {
    preferences.begin("badge-os", false);
    preferences.putBool("stressTest", stressTestEnabled);
    preferences.putBool("showExploits", showAllExploits);
    preferences.end();
}

void saveScore() {
    preferences.begin("badge-os", false);
    preferences.putUInt("score", currentScore);
    preferences.putUInt("currentPosition", currentPosition);
    preferences.end();
}

void saveRegistration(uint32_t id, uint32_t token, String nick) {
    preferences.begin("badge-os", false);
    preferences.putUInt("badgeId", id);
    preferences.putUInt("secretToken", token);
    preferences.putString("badgeNick", nick);
    preferences.end();
}

void saveWifiChannel(int channel) {
    preferences.begin("badge-os", false);
    preferences.putInt("wifiChannel", channel);
    preferences.end();
}

void saveTimeOffset(long offset) {
    preferences.begin("badge-os", false);
    preferences.putLong("timeOffset", offset);
    preferences.end();
}

void loadAllSettings() {
    preferences.begin("badge-os", true);
    
    if (preferences.getBytesLength("exploits") == sizeof(active_exploits)) {
        preferences.getBytes("exploits", active_exploits, sizeof(active_exploits));
    } else {
        memset(active_exploits, 0, sizeof(active_exploits));
    }

    if (preferences.getBytesLength("defenses") == sizeof(active_defenses)) {
        preferences.getBytes("defenses", active_defenses, sizeof(active_defenses));
    } else {
        memset(active_defenses, 0, sizeof(active_defenses));
    }

    if (preferences.getBytesLength("last_hits") == sizeof(last_hits)) {
        preferences.getBytes("last_hits", last_hits, sizeof(last_hits));
    } else {
        memset(last_hits, 0, sizeof(last_hits));
    }


    currentScore = preferences.getUInt("score", 0);
    currentPosition = preferences.getUInt("currentPosition", 0);
    stressTestEnabled = preferences.getBool("stressTest", false);
    showAllExploits = preferences.getBool("showExploits", true);

    badgeIdentifier = preferences.getUInt("badgeId", 0);
    secretToken = preferences.getUInt("secretToken", 0);
    badgeNickname = preferences.getString("badgeNick", "Icepick");

    timeOffset = preferences.getLong("timeOffset", 0);
    if (timeOffset != 0) {
        timeIsSynced = true;
    }
    
    knownWifiChannel = preferences.getInt("wifiChannel", 0);
    if (knownWifiChannel > 0) {
        //Serial.printf("POWER: Loaded known WiFi channel: %d\n", knownWifiChannel);
    }

    preferences.end();
}

// ==== Time Logic ====

/**
 * @brief Sets the internal time offset based on a server timestamp.
 */
void setTimeFromServer(uint32_t serverTimestamp) {
    if (serverTimestamp == 0) return;
    
    unsigned long currentMillis = millis();
    long newOffset = (long)serverTimestamp - (long)(currentMillis / 1000);
    
    timeOffset = newOffset;
    timeIsSynced = true;
    saveTimeOffset(timeOffset);
    
    //Serial.printf("TIME: Synced. Server: %u, Offset: %ld\n", serverTimestamp, timeOffset);
}

/**
 * @brief Updates the global currentTimeStr and currentDateStr.
 */
void updateCurrentTimeStr() {
    if (!timeIsSynced) {
        snprintf(currentTimeStr, sizeof(currentTimeStr), "--:--:--");
        snprintf(currentDateStr, sizeof(currentDateStr), "--/--/----");
        return;
    }
    time_t now = timeOffset + (millis() / 1000);
    const time_t Y2038_LIMIT = 2147483647;
    time_t displayTime; // This is the time we'll actually format
    if (now > Y2038_LIMIT) {
        displayTime = (time_t)((int32_t)now);
    } else {
        displayTime = now;
    }
    struct tm * timeinfo;
    // Use the (potentially wrapped) displayTime to get calendar info
    timeinfo = gmtime(&displayTime);
    snprintf(currentDateStr, sizeof(currentDateStr), "%02d/%02d/%04d", timeinfo->tm_mday, timeinfo->tm_mon + 1, timeinfo->tm_year + 1900);
    snprintf(currentTimeStr, sizeof(currentTimeStr), "%02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);

    if ((now >= Y2038_LIMIT) && (now < Y2038_LIMIT+2)) { //just in cae
        currentScreen = SCREEN_2038;
        drawCurrentScreen(true);
        return;
    } 

}


// ==== API Communication Functions ====

/**
 * @brief Converts the active_exploits array to a 32-bit bitmask.
 */
uint32_t getExploitConfigBitmask() {
    uint32_t config = 0;
    for (int i = 0; i < CYBER_PAIR_COUNT; i++) {
        if (active_exploits[i]) {
            config |= (1 << i);
        }
    }
    return config;
}

/**
 * @brief Converts the active_defenses array to a 32-bit bitmask.
 */
uint32_t getDefenseConfigBitmask() {
    uint32_t config = 0;
    for (int i = 0; i < CYBER_PAIR_COUNT; i++) {
        if (active_defenses[i]) {
            config |= (1 << i);
        }
    }
    return config;
}

/**
 * @brief Connects to WiFi. Tries a direct connection on a saved channel first.
 * If that fails, or if no channel is saved, it performs a full scan.
 * @return true on success, false on failure.
 */
bool connectToWiFi() {
    esp_now_deinit();
    WiFi.mode(WIFI_STA);
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11G);
    esp_wifi_config_80211_tx_rate(WIFI_IF_STA, WIFI_PHY_RATE_6M);
    esp_wifi_set_max_tx_power(44);
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

    unsigned long wifiStartTime = millis();
    
    unsigned long lastAnimTime = 0;
    int animFrame = 1;

    if (knownWifiChannel > 0 && knownWifiChannel <= 13) {
        //Serial.printf("WiFi: Attempting direct connect on channel %d...\n", knownWifiChannel);
        WiFi.begin(ijjijjiijijijijiij, ijijjiijiiijjiijijjji, knownWifiChannel);
        
        wifiStartTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - wifiStartTime < 10000) {
            if (millis() - lastAnimTime > 250) {
                drawWifiAnim(animFrame);
                animFrame++;
                if (animFrame > 3) animFrame = 1;
                lastAnimTime = millis();
            }
            delay(50);
        }

        if (WiFi.status() == WL_CONNECTED) {
            //Serial.println("WiFi: Direct connect SUCCESS.");
            return true;
        }

        //Serial.println("WiFi: Direct connect FAILED. Clearing channel and re-scanning.");
        knownWifiChannel = 0;
        saveWifiChannel(0);
        WiFi.disconnect(true);
        delay(100);
    }

    //Serial.println("WiFi: Scanning all channels...");
    WiFi.begin(ijjijjiijijijijiij, ijijjiijiiijjiijijjji);
    
    wifiStartTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wifiStartTime < 15000) {
        if (millis() - lastAnimTime > 250) {
            drawWifiAnim(animFrame);
            animFrame++;
            if (animFrame > 3) animFrame = 1;
            lastAnimTime = millis();
        }
        delay(50);
    }

    if (WiFi.status() == WL_CONNECTED) {
        knownWifiChannel = WiFi.channel();
        //Serial.printf("WiFi: Scan connect SUCCESS. AP is on channel %d. Saving.\n", knownWifiChannel);
        saveWifiChannel(knownWifiChannel);
        drawWifiAnim(4);
        return true;
    }

    //Serial.println("WiFi: Scan connect FAILED. No network.");
    drawWifiAnim(4);
    return false;
}


void postRegister() {
    //Serial.println("WiFi: De-init ESP-NOW for WiFi...");
    if (!connectToWiFi()) {
        //Serial.println("Register: WiFi not connected.");
        WiFi.disconnect(true);
        initESPNOW(false);
        return;
    }
    
    client.setCACert(cert_pin); 
    
    String url = String(ijjjjiiijijiiijjijjji) + "/register";
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");

    JSONVar request;
    request["badgeMac"] = WiFi.macAddress();
    request["badgeType"] = badgeType;
    String reqBody = JSON.stringify(request);

    //Serial.println("Registering...");
    drawWifiAnim(0);
    int httpCode = http.POST(reqBody);
    drawWifiAnim(4);

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        //Serial.println(payload);
        JSONVar response = JSON.parse(payload);
        
        if (JSON.typeof(response) == "undefined") {
            //Serial.println("JSON parse failed");
            http.end();
            client.stop();
            WiFi.disconnect(true);
            initESPNOW(false);
            return;
        }

        // Use JSON.stringify and strtoul to correctly parse large unsigned integers
        String badgeIdStr = JSON.stringify(response["badgeIdentifier"]);
        String secretTokenStr = JSON.stringify(response["secretToken"]);
        
        badgeIdentifier = strtoul(badgeIdStr.c_str(), NULL, 10);
        secretToken = strtoul(secretTokenStr.c_str(), NULL, 10);
        
        String nick = (const char*)response["badgeNickname"];
        badgeNickname = nick.substring(0, 16);
        if (response.hasOwnProperty("timestamp")) {
            setTimeFromServer((uint32_t)response["timestamp"]);
        }
        if (response.hasOwnProperty("defenseConfig")) {
            uint32_t defenseConfig = (uint32_t)response["defenseConfig"]; 
            for(int i = 0; i < CYBER_PAIR_COUNT; i++) {
                active_defenses[i] = (defenseConfig >> i) & 1; 
            }
            saveCyberSettings(false); 
            //Serial.println("Register: Defense config loaded from server.");
        }
        saveRegistration(badgeIdentifier, secretToken, badgeNickname);
        //Serial.printf("Registered! ID: %u, Nick: %s\n", badgeIdentifier, badgeNickname.c_str());
        
        http.end();
        client.stop();
        WiFi.disconnect(true);
        initESPNOW(false);
        // Call postSync() immediately after registration
        ////Serial.println("Registration successful. Calling postSync() to get config...");
        //postSync();
        
        return;

    } else {
        //Serial.printf("Register failed, code: %d\n", httpCode);
    }
    http.end();
    client.stop();
    WiFi.disconnect(true);
    initESPNOW(false);
    return;
}

void postSetDefenseConfig() {
    if (badgeIdentifier == 0) return; 
    //Serial.println("DEBUG: postSetDefenseConfig() CALLED.");
    //Serial.println("WiFi: De-init ESP-NOW for WiFi...");
    if (!connectToWiFi()) {
        //Serial.println("SetDefense: WiFi not connected.");
        WiFi.disconnect(true);
        initESPNOW(false);
        return;
    }

    client.setCACert(cert_pin);
    String url = String(ijjjjiiijijiiijjijjji) + "/setDefenseConfig";
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");

    JSONVar request;
    request["badgeIdentifier"] = badgeIdentifier;
    request["secretToken"] = secretToken;
    request["defenseConfig"] = getDefenseConfigBitmask();
    String reqBody = JSON.stringify(request);

    //Serial.println("Setting defense config...");
    //Serial.printf("DEBUG: Request body: %s\n", reqBody.c_str());
    drawWifiAnim(0);
    int httpCode = http.POST(reqBody);
    drawWifiAnim(4);
    //Serial.printf("DEBUG: POST complete. HTTP Code: %d\n", httpCode);

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        //Serial.println(payload);
        JSONVar response = JSON.parse(payload);
        if (JSON.typeof(response) != "undefined") {
            uint32_t defenseConfig = (uint32_t)response["defenseConfig"];
            //Serial.printf("Server accepted defense config: %u\n", defenseConfig);
            if (response.hasOwnProperty("timestamp")) {
                setTimeFromServer((uint32_t)response["timestamp"]);
            }
        }
    } else {
        //Serial.printf("Set defense config failed, code: %d\n", httpCode);
    }
    http.end();
    client.stop();
    WiFi.disconnect(true);
    initESPNOW(false);
}

// Removed postGetDefenseConfig()
// Removed postSetAttackConfig()
// Removed postGetAttackConfig()

void postExploit(uint32_t targetBadgeIdentifier) { 
    if (badgeIdentifier == 0) {
        snprintf(statusMessageLine1, 40, "EXPLOIT FAILED!");
        snprintf(statusMessageLine2, 40, "NO BADGE ID.");
        snprintf(statusMessageLine3, 40, "");
        screenToReturnTo = SCREEN_EXPLOITS;
        showStatusMessage = true;
        return;
    }

    //Serial.println("DEBUG: postExploit() CALLED.");
    //Serial.println("WiFi: De-init ESP-NOW for WiFi...");
    if (!connectToWiFi()) {
        snprintf(statusMessageLine1, 40, "EXPLOIT FAILED!");
        snprintf(statusMessageLine2, 40, "NO SERVER CONNECTION.");
        snprintf(statusMessageLine3, 40, "");
        screenToReturnTo = SCREEN_EXPLOITS;
        showStatusMessage = true;
        
        WiFi.disconnect(true);
        initESPNOW(false);
        return;
    }

    client.setCACert(cert_pin);
    String url = String(ijjjjiiijijiiijjijjji) + "/exploit";
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");

    JSONVar request;
    request["badgeIdentifier"] = badgeIdentifier;
    request["secretToken"] = secretToken;
    request["targetBadgeIdentifier"] = targetBadgeIdentifier;
    request["attackConfig"] = getExploitConfigBitmask();
    String reqBody = JSON.stringify(request);

    //Serial.printf("Posting exploit to target %u...\n", targetBadgeIdentifier);
    //Serial.printf("DEBUG: Request body: %s\n", reqBody.c_str());
    drawWifiAnim(0);
    int httpCode = http.POST(reqBody);
    drawWifiAnim(4);
    //Serial.printf("DEBUG: POST complete. HTTP Code: %d\n", httpCode);

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        //Serial.println(payload);
        JSONVar response = JSON.parse(payload);
        
        if (JSON.typeof(response) == "undefined") {
            snprintf(statusMessageLine1, 40, "EXPLOIT FAILED!");
            snprintf(statusMessageLine2, 40, "SERVER JSON ERROR");
            snprintf(statusMessageLine3, 40, "");
        } else {
            String msg = "";
            if (response.hasOwnProperty("msg")) {
                 msg = (const char*)response["msg"];
            }
            uint32_t successfulExploits = (uint32_t)response["successfulExploits"];
            int32_t pointsGained = (int32_t)response["pointsGained"];
            int32_t pointsLost = (int32_t)response["pointsLost"];

            if (response.hasOwnProperty("totalPoints")) {
                 currentScore = (int32_t)response["totalPoints"];
            } 
            else currentScore = currentScore + pointsGained - pointsLost;
            saveScore();

            if (response.hasOwnProperty("scorePosition")) {
                 currentPosition = (int32_t)response["scorePosition"];
            } 
            
            if (response.hasOwnProperty("timestamp")) {
                setTimeFromServer((uint32_t)response["timestamp"]);
            }
            
            if (msg.length() > 0) {
                snprintf(statusMessageLine1, 40, "%s", msg.substring(0, 39).c_str());
            } else {
                snprintf(statusMessageLine1, 40, "EXPLOIT SENT!");
            }
            snprintf(statusMessageLine2, 40, "%u SUCCESSFUL HITS!", successfulExploits);
            snprintf(statusMessageLine3, 40, "CRD: +%d / -%d", pointsGained, pointsLost);
        }
    } else {
        //Serial.printf("Exploit failed, code: %d\n", httpCode);
        snprintf(statusMessageLine1, 40, "EXPLOIT FAILED!");
        snprintf(statusMessageLine2, 40, "SERVER ERROR: %d", httpCode);
        snprintf(statusMessageLine3, 40, "");
    }
    http.end();
    client.stop();
    WiFi.disconnect(true);
    initESPNOW(false);
    
    screenToReturnTo = SCREEN_EXPLOITS;
    showStatusMessage = true;
}

bool postGetTime() {
    if (badgeIdentifier == 0) return false; 
    //Serial.println("WiFi: De-init ESP-NOW for WiFi...");
    if (!connectToWiFi()) {
        //Serial.println("GetTime: WiFi not connected.");
        WiFi.disconnect(true);
        initESPNOW(false);
        return false;
    }

    client.setCACert(cert_pin);
    String url = String(ijjjjiiijijiiijjijjji) + "/getTime";
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");

    JSONVar request;
    request["badgeIdentifier"] = badgeIdentifier;
    request["secretToken"] = secretToken;
    String reqBody = JSON.stringify(request);

    //Serial.println("Getting time...");
    drawWifiAnim(0);
    int httpCode = http.POST(reqBody);
    drawWifiAnim(4);

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        //Serial.println(payload);
        JSONVar response = JSON.parse(payload);
        if (JSON.typeof(response) != "undefined") {
            if (response.hasOwnProperty("timestamp")) {
                setTimeFromServer((uint32_t)response["timestamp"]);
            }
        }
        http.end();
        client.stop();
        WiFi.disconnect(true);
        initESPNOW(false);
        return true;
    } else {
        //Serial.printf("Get time failed, code: %d\n", httpCode);
    }
    http.end();
    client.stop();
    WiFi.disconnect(true);
    initESPNOW(false);
    return false;
}

void postSync() {
    if (badgeIdentifier == 0) return; 
    //Serial.println("WiFi: De-init ESP-NOW for WiFi...");
    if (!connectToWiFi()) {
        //Serial.println("Sync: WiFi not connected.");
        WiFi.disconnect(true);
        initESPNOW(false);
        return;
    }

    client.setCACert(cert_pin);
    String url = String(ijjjjiiijijiiijjijjji) + "/sync";
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");

    JSONVar request;
    request["badgeIdentifier"] = badgeIdentifier;
    request["secretToken"] = secretToken;
    String reqBody = JSON.stringify(request);

    //Serial.println("Syncing...");
    drawWifiAnim(0);
    int httpCode = http.POST(reqBody);
    drawWifiAnim(4);

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        //Serial.println(payload);
        JSONVar response = JSON.parse(payload);
        
        if (JSON.typeof(response) == "undefined") {
            //Serial.println("Sync JSON parse failed");
        } else {
            String nick = (const char*)response["badgeNickname"];
            badgeNickname = nick.substring(0, 16);
            currentScore = (int32_t)response["totalPoints"];
            if (response.hasOwnProperty("timestamp")) {
                setTimeFromServer((uint32_t)response["timestamp"]);
            }
            if (response.hasOwnProperty("scorePosition")) {
                 currentPosition = (int32_t)response["scorePosition"];
            } 
            if (response.hasOwnProperty("currentVersion")) {
                String serverVersion = (const char*)response["currentVersion"];
                // Only update if server version is HIGHER than current version
                if (compareVersions(serverVersion.c_str(), currentVersion) > 0) {
                    //Serial.printf("Server version %s is newer than %s. Triggering firmware update!\n", serverVersion.c_str(), currentVersion);
                    newFwUpdatePacket = true;
                }
            } 
            if (response.hasOwnProperty("defenseConfig")) {
                uint32_t defenseConfig = (uint32_t)response["defenseConfig"];
                for(int i = 0; i < CYBER_PAIR_COUNT; i++) {
                    active_defenses[i] = (defenseConfig >> i) & 1;
                }
                saveCyberSettings(false);
                //Serial.println("Defense config synced from server.");
            }
            
            //Serial.printf("SYNC: Total Points: %d Current Ranking %d\n", currentScore,currentPosition);
            if (response.hasOwnProperty("totalExploits")) {
                //Serial.printf("SYNC: Total Exploits: %u, Fails: %u, Defenses: %u\n",
                  //            (uint32_t)response["totalExploits"],
                    //          (uint32_t)response["totalFails"],
                      //        (uint32_t)response["totalDefenses"]);
            }
            
            saveScore();
            saveRegistration(badgeIdentifier, secretToken, badgeNickname);
        }
    } else {
        //Serial.printf("Sync failed, code: %d\n", httpCode);
    }
    http.end();
    client.stop();
    WiFi.disconnect(true);
    initESPNOW(false);
}

bool fetchTopLists() {
    if (badgeIdentifier == 0) return false;
    
    // Clear existing data
    memset(topListNicknames, 0, sizeof(topListNicknames));
    memset(topListCounts, 0, sizeof(topListCounts));
    
    //Serial.println("WiFi: De-init ESP-NOW for WiFi...");
    if (!connectToWiFi()) {
        //Serial.println("FetchTop: WiFi not connected.");
        WiFi.disconnect(true);
        initESPNOW(false);
        return false;
    }

    client.setCACert(cert_pin);
    String url = String(ijjjjiiijijiiijjijjji) + "/top";
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");

    JSONVar request;
    request["badgeIdentifier"] = badgeIdentifier;
    request["secretToken"] = secretToken;
    String reqBody = JSON.stringify(request);

    //Serial.println("Fetching top lists...");
    drawWifiAnim(0);
    int httpCode = http.POST(reqBody);
    drawWifiAnim(4);

    bool success = false;
    
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        //Serial.println(payload);
        JSONVar response = JSON.parse(payload);
        
        if (JSON.typeof(response) == "undefined") {
            //Serial.println("FetchTop JSON parse failed");
        } else {
            // Parse topAttackers
            if (response.hasOwnProperty("topAttackers")) {
                JSONVar attackers = response["topAttackers"];
                int attackerCount = attackers.length();
                for (int i = 0; i < attackerCount && i < 9; i++) {
                    String nick = (const char*)attackers[i]["nickname"];
                    strncpy(topListNicknames[0][i], nick.c_str(), 16);
                    topListNicknames[0][i][16] = '\0';
                    topListCounts[0][i] = (uint8_t)attackers[i]["attackCount"];
                }
            }
            
            // Parse topVictims
            if (response.hasOwnProperty("topVictims")) {
                JSONVar victims = response["topVictims"];
                int victimCount = victims.length();
                for (int i = 0; i < victimCount && i < 9; i++) {
                    String nick = (const char*)victims[i]["nickname"];
                    strncpy(topListNicknames[1][i], nick.c_str(), 16);
                    topListNicknames[1][i][16] = '\0';
                    topListCounts[1][i] = (uint8_t)victims[i]["attackCount"];
                }
            }
            
            success = true;
            //Serial.println("Top lists fetched successfully.");
        }
    } else {
        //Serial.printf("FetchTop failed, code: %d\n", httpCode);
    }
    
    http.end();
    client.stop();
    WiFi.disconnect(true);
    initESPNOW(false);
    
    return success;
}


// ==== UI Rendering Functions ====

uint8_t countActiveExploits() {
  uint8_t count = 0;
  for(uint8_t i = 0; i < CYBER_PAIR_COUNT; i++) {
    if (active_exploits[i]) count++;
  }
  return count;
}

uint8_t countActiveDefenses() {
  uint8_t count = 0;
  for(uint8_t i = 0; i < CYBER_PAIR_COUNT; i++) {
    if (active_defenses[i]) count++;
  }
  return count;
}

void drawStatusBar() {
  tft.drawFastHLine(0, STATUS_H, DISPLAY_WIDTH, COL_SEPARATOR);
  tft.setTextColor(COL_PRIMARY, COL_STATUS_BG);
  tft.setTextSize(1);
  tft.setFont();

  if (strcmp(currentTimeStr, lastTimeStr) != 0) {
    const uint8_t timeX = DISPLAY_WIDTH - PADDING - 48;
    tft.fillRect(timeX, PADDING, 48, STATUS_H - 2 * PADDING, COL_STATUS_BG);
    tft.setCursor(timeX, PADDING); 
    tft.print(currentTimeStr);
    strcpy(lastTimeStr, currentTimeStr);
  }

  if (strcmp(currentDateStr, lastDateStr) != 0) {
    const uint8_t dateW = 60;
    tft.fillRect(PADDING, PADDING, dateW, STATUS_H - 2 * PADDING, COL_STATUS_BG);
    tft.setCursor(PADDING, PADDING);
    tft.print(currentDateStr);
    strcpy(lastDateStr, currentDateStr);
  }
}

void drawBottomStatusBar() {
  tft.drawFastHLine(0, DISPLAY_HEIGHT - BOTTOM_STATUS_H - 1, DISPLAY_WIDTH, COL_SEPARATOR);
  const uint8_t yStart = DISPLAY_HEIGHT - BOTTOM_STATUS_H;
  tft.setFont();

  if (strlen(bootMessage) > 0) {
      tft.fillRect(0, yStart + PADDING, DISPLAY_WIDTH, BOTTOM_STATUS_H - 2 * PADDING, COL_STATUS_BG);
      tft.setTextSize(1);
      tft.setTextColor(COL_WARNING);
      tft.setCursor(PADDING, yStart + PADDING);
      tft.print(bootMessage);
      last_displayed_badge_id = -1; 
      last_selection_active = !selection_active; 
      lastWifiStatus = 0xFF; 
      return;
  }

  if (bottomBarMessageTime > 0 && millis() - bottomBarMessageTime < STATUS_MESSAGE_DURATION_MS) {
      tft.fillRect(0, yStart + PADDING, DISPLAY_WIDTH, BOTTOM_STATUS_H - 2 * PADDING, COL_STATUS_BG);
      tft.setTextSize(1);
      tft.setTextColor(COL_ERROR);
      tft.setCursor(PADDING, yStart + PADDING);
      tft.print(bottomBarMessage);
      
      last_displayed_badge_id = -1; 
      last_selection_active = !selection_active; 
      lastWifiStatus = 0xFF; 
      
      return;
  } else {
      if (bottomBarMessageTime > 0) {
           bottomBarMessageTime = 0;
           last_displayed_badge_id = -1; 
           last_selection_active = !selection_active;
           lastWifiStatus = 0xFF;
      }
  }
  
  if (currentScreen == SCREEN_ATTACK_STATUS) {
      if (strcmp(lastAttackerNickDrawn, (const char*)lastAttackerNickname) != 0 || 
          strcmp(lastExploitListDrawn, (const char*)lastExploitList) != 0) 
      {
          tft.fillRect(0, yStart + PADDING, DISPLAY_WIDTH, BOTTOM_STATUS_H - 2 * PADDING, COL_STATUS_BG);
          tft.setTextSize(1);
          
          tft.setTextColor(COL_ERROR);
          tft.setCursor(PADDING, yStart + PADDING);
          tft.print((const char*)lastAttackerNickname);
          
          tft.setTextColor(COL_WARNING);
          int16_t x1, y1;
          uint16_t w, h;
          tft.getTextBounds((const char*)lastExploitList, 0, 0, &x1, &y1, &w, &h);
          tft.setCursor(DISPLAY_WIDTH - PADDING - w, yStart + PADDING);
          tft.print((const char*)lastExploitList);

          strncpy(lastAttackerNickDrawn, (const char*)lastAttackerNickname, 16);
          lastAttackerNickDrawn[16] = '\0';
          strncpy(lastExploitListDrawn, (const char*)lastExploitList, 11);
          lastExploitListDrawn[11] = '\0';
      }
      
      last_displayed_badge_id = -1; 
      last_selection_active = !selection_active; 
      lastWifiStatus = 0xFF; 
  }
  else if (currentScreen == SCREEN_DEBUG_MENU || currentScreen == SCREEN_OTA_PROCESS || currentScreen == SCREEN_SHOW_DATA) {
      uint8_t wifiStatus = WiFi.status();
      IPAddress currentIP = (wifiStatus == WL_CONNECTED) ? WiFi.localIP() : IPAddress(0, 0, 0, 0);

      if (wifiStatus == lastWifiStatus && currentIP == lastIP && otaState == lastOtaState) {
        return;
      }

      uint16_t statusColor = COL_ERROR;
      const char* statusText = "NET: DOWN"; 

      if (wifiStatus == WL_CONNECTED) {
        statusColor = COL_ACCENT; 
        statusText = "NET: ONLINE";
      } else if (otaState == OTA_CONNECTING_WIFI || otaState == OTA_CHECKING) {
        statusColor = COL_PRIMARY;
        statusText = "NET: INIT"; 
      } else if (WiFi.getMode() == WIFI_MODE_AP) {
        statusColor = COL_PRIMARY;
        statusText = "NET: AP MODE";
      } else if (WiFi.getMode() == WIFI_STA) { 
         statusColor = COL_PRIMARY;
         statusText = "NET: ESPNOW"; 
      }

      tft.setTextSize(1);
      tft.fillRect(0, yStart + PADDING, 70, BOTTOM_STATUS_H - 2 * PADDING, COL_STATUS_BG); 
      tft.fillRect(70, yStart + PADDING, DISPLAY_WIDTH - 70 - PADDING, BOTTOM_STATUS_H - 2 * PADDING, COL_STATUS_BG); 

      tft.setCursor(PADDING, yStart + PADDING); 
      tft.setTextColor(statusColor, COL_STATUS_BG);
      tft.print(statusText);
      
      tft.setTextColor(COL_PRIMARY, COL_STATUS_BG);
      tft.setCursor(70, yStart + PADDING); 
      
      if (wifiStatus == WL_CONNECTED) {
        char ipStr[16];
        sprintf(ipStr, "%u.%u.%u.%u", currentIP[0], currentIP[1], currentIP[2], currentIP[3]);
        tft.print(ipStr);
      } else {
        tft.print("0.0.0.0");
      }

      lastWifiStatus = wifiStatus;
      lastIP = currentIP;
      lastOtaState = otaState;
  } 
  else {
      unsigned long now = millis();
      bool mustRedrawInfo = false;
      
      if (selection_active != last_selection_active) {
          mustRedrawInfo = true;
      } else if (selection_active && selected_badge_id != last_displayed_badge_id) {
          mustRedrawInfo = true;
      }
      
      if (currentScreen == SCREEN_SCAN && selection_active) {
          if (last_displayed_badge_id < 0 || last_displayed_badge_id >= MAX_BADGE_TRACKING) {
              last_displayed_badge_id = 0;
          }
          PeerInfo& peer = all_peers[last_displayed_badge_id];
          bool was_active = (peer.last_seen > 0 && (now - peer.last_seen < BADGE_ACTIVE_TIMEOUT_MS));
          bool is_active = (all_peers[selected_badge_id].last_seen > 0 && (now - all_peers[selected_badge_id].last_seen < BADGE_ACTIVE_TIMEOUT_MS));
          if (was_active != is_active) {
              mustRedrawInfo = true;
          }
      }

      if (mustRedrawInfo) {
          tft.fillRect(0, yStart + PADDING, DISPLAY_WIDTH, BOTTOM_STATUS_H - 2 * PADDING, COL_STATUS_BG);
          tft.setTextSize(1);
          
          if (selection_active) {
              PeerInfo& peer = all_peers[selected_badge_id];
              
              uint16_t targetColor = intensityToGlowColor(badge_intensity[selected_badge_id]);
              if (badge_intensity[selected_badge_id] < 10) {
                  targetColor = COL_PRIMARY;
              }

              tft.setTextColor(targetColor, COL_STATUS_BG);
              tft.setCursor(PADDING, yStart + PADDING);
              
              char nick[17];
              strncpy(nick, peer.nickname, 16);
              nick[16] = '\0';
              if (strlen(nick) == 0) {
                  strcpy(nick, "ID_UNKNOWN");
              }

              tft.printf("%s  H:%d %ddBm", nick,last_hits[selected_badge_id], peer.rssi);
          } else {
              tft.setTextColor(COL_ERROR, COL_STATUS_BG);
              tft.setCursor(PADDING, yStart + PADDING);
              if (currentScreen == SCREEN_SCAN) {
                tft.print("Cursor moves, A selects");
              } else if (currentScreen == SCREEN_SET_TIME) {
                // This is our new condition: Print nothing!
                tft.print("");
              } else {
                tft.print(" !! NO TARGET SELECTED !!");
              }
          }
          
          last_displayed_badge_id = selected_badge_id;
          last_selection_active = selection_active;
          
          lastWifiStatus = 0xFF; 
      }
  }
}

void drawOtaProcessScreen() {
  tft.setFont();
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.setFont(&FreeSansBold9pt7b);
  tft.setCursor(PADDING, STATUS_H + 20);
  tft.print("UPDATE SYSTEM");
  tft.setFont();
  
  tft.drawRect(PADDING, STATUS_H + 44, DISPLAY_WIDTH - 2 * PADDING, 50, COL_GRID);
  
  tft.setTextSize(1);
  tft.setCursor(PADDING + 5, STATUS_H + 49); 
  tft.setTextColor(COL_PRIMARY, COL_BG);
  tft.printf("VERSION: %s", currentVersion);

  tft.setCursor(PADDING + 5, STATUS_H + 59); 
  uint16_t statusColor = COL_PRIMARY;
  if (otaState == OTA_FAILED) statusColor = COL_ERROR;
  else if (otaState == OTA_DOWNLOADING) statusColor = COL_ACCENT;
  tft.setTextColor(statusColor, COL_BG);
  tft.print("STATUS:");
  tft.print(otaStatusMessage.substring(0, 16)); 
  
  if (otaState == OTA_DOWNLOADING || otaState == OTA_SUCCESS) {
    tft.setCursor(PADDING + 5, STATUS_H + 69); 
    tft.setTextColor(COL_ACCENT, COL_BG);
    tft.printf("PROGRESS: %d%%", otaProgress);
    
    uint8_t barY = STATUS_H + 79; 
    uint8_t barW = DISPLAY_WIDTH - 2 * PADDING - 10;
    tft.drawRect(PADDING + 5, barY, barW, 6, COL_ACCENT);
    uint16_t fillW = map(otaProgress, 0, 100, 0, barW - 2); 
    tft.fillRect(PADDING + 6, barY + 1, fillW, 4, COL_ACCENT);
  } else {
    tft.setTextColor(COL_PRIMARY, COL_BG);
    tft.setCursor(PADDING, DISPLAY_HEIGHT - BOTTOM_STATUS_H - 10); 
    tft.print("PRESS [B] TO RETURN.");
  }
}

void drawDebugMenu() {
  uint8_t yStart = STATUS_H + PADDING + 1; 
  
  tft.setFont();
  tft.setTextColor(COL_ERROR, COL_BG); 
  tft.setTextSize(1);
  tft.setCursor(PADDING, yStart + 2); 
  tft.print("!! DEBUG / ROOT ACCESS !!");
  tft.drawFastHLine(0, yStart + 12, DISPLAY_WIDTH, COL_ERROR); 
  
  yStart += 20;

  for (uint8_t i = 0; i < DEBUG_ITEM_COUNT; i++) {
    uint16_t color = (i == selectedItem) ? COL_ACCENT : COL_PRIMARY;
    tft.setTextColor(color, COL_BG); 
    
    tft.setCursor(PADDING, yStart + i * 11);
    tft.print((i == selectedItem) ? ">>" : "  "); 
    
    tft.setCursor(PADDING + 16, yStart + i * 11);
    tft.print(DEBUG_MENU_ITEMS[i].title);
  }

  tft.setTextColor(COL_WARNING, COL_BG);
  tft.setTextSize(1);
  tft.setCursor(PADDING, DISPLAY_HEIGHT - BOTTOM_STATUS_H - 10); 
  tft.print("DANGER: PROCEED W/ CAUTION.");
}

/**
 * @brief Helper function to draw a single line in the Settings menu.
 */
void drawSettingsMenuLine(uint8_t index, bool isSelected) {
    uint8_t yStart = 28;
    uint8_t y = yStart + index * 12;
    
    uint16_t color = isSelected ? COL_ACCENT : COL_PRIMARY;

    tft.fillRect(PADDING, y, DISPLAY_WIDTH - PADDING, 12, COL_BG);
    
    tft.setFont();
    tft.setTextSize(1);
    tft.setTextColor(color, COL_BG); 
    tft.setCursor(PADDING, y);
    tft.print(isSelected ? ">>" : "  "); 
    
    tft.setCursor(PADDING + 16, y);
    switch(index) {
        case 0:
            tft.print("SHOW EXPLOITS: ");
            tft.print(showAllExploits ? "[ON]" : "[OFF]");
            break;
        case 1:
            tft.print("[SYNC SCORE/STATE]");
            break;
        case 2:
            tft.print("[SYNC TIME]");
            break;
        case 3:
            tft.print("[SET TIME]"); // New item
            break;
        case 4:
            tft.print("[SHOW BACKSTORY]"); // Shifted down
            break;
        case 5:
            tft.print("[I'm B0R3D]");
            break;
        case 6:
            tft.print("[POWER OFF]"); // Shifted down
            break;
    }
}

void drawSettingsScreen() {
  uint8_t yStart = STATUS_H + PADDING + 1; 
  
  tft.setFont();
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.setTextSize(1);
  tft.setCursor(PADDING, yStart + 2); 
  tft.print("SYSTEM SETTINGS");
  tft.drawFastHLine(0, yStart + 12, DISPLAY_WIDTH, COL_GRID); 
  tft.drawFastHLine(0, 25, DISPLAY_WIDTH, COL_NEON_PURPLE);
  
  drawSettingsMenuLine(0, 0 == selectedSettingItem);
  drawSettingsMenuLine(1, 1 == selectedSettingItem);
  drawSettingsMenuLine(2, 2 == selectedSettingItem);
  drawSettingsMenuLine(3, 3 == selectedSettingItem);
  drawSettingsMenuLine(4, 4 == selectedSettingItem);
  drawSettingsMenuLine(5, 5 == selectedSettingItem); // New line
  drawSettingsMenuLine(6, 6 == selectedSettingItem); // Shifted down
}

void drawLogsMenuLine(uint8_t index, bool isSelected) {
    uint8_t yStart = 28;
    uint8_t y = yStart + index * 10;
    
    uint16_t color = isSelected ? COL_ACCENT : COL_PRIMARY;

    tft.fillRect(PADDING, y, DISPLAY_WIDTH - PADDING, 10, COL_BG);
    
    tft.setFont();
    tft.setTextSize(1);
    tft.setTextColor(color, COL_BG); 
    tft.setCursor(PADDING, y);
    tft.print(isSelected ? ">>" : "  "); 
    
    tft.setCursor(PADDING + 16, y);
    switch(index) {
        case 0:
            tft.print("TOP ATTACKERS");
            break;
        case 1:
            tft.print("TOP VICTIMS");
            break;
    }
}

void drawLogsScreen() {
  uint8_t yStart = STATUS_H + PADDING + 1; 
  
  tft.setFont();
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.setTextSize(1);
  tft.setCursor(PADDING, yStart + 2); 
  tft.print("STATS & LOGS");
  tft.drawFastHLine(0, yStart + 12, DISPLAY_WIDTH, COL_GRID); 
  tft.drawFastHLine(0, 25, DISPLAY_WIDTH, COL_NEON_PURPLE);
  
  drawLogsMenuLine(0, 0 == selectedLogItem);
  drawLogsMenuLine(1, 1 == selectedLogItem);
}

void drawTopListScreen(bool isAttackers, const char topList[][3][17], const uint8_t topCounts[][3]) {
  uint8_t yStart = STATUS_H + PADDING + 1;
  
  tft.fillRect(0, yStart, DISPLAY_WIDTH, DISPLAY_HEIGHT - STATUS_H - BOTTOM_STATUS_H - 2, COL_BG);
  
  tft.setFont();
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.setTextSize(1);
  tft.setCursor(PADDING, yStart + 2);
  
  if (isAttackers) {
    tft.print("TOP ATTACKERS");
  } else {
    tft.print("TOP VICTIMS");
  }
  
  tft.drawFastHLine(0, yStart + 12, DISPLAY_WIDTH, COL_GRID);
  tft.drawFastHLine(0, 25, DISPLAY_WIDTH, COL_NEON_PURPLE);
  
  uint8_t listY = 30;
  uint8_t idx = isAttackers ? 0 : 1;
  
  tft.setTextColor(COL_PRIMARY, COL_BG);
  
  for (uint8_t i = 0; i < 3; i++) {
    if (topList[idx][i][0] == '\0') break; // Empty entry
    
    // Draw rank number
    tft.setTextColor(COL_WARNING, COL_BG);
    tft.setCursor(PADDING, listY + i * 14);
    tft.printf("#%d", i + 1);
    
    // Draw nickname
    tft.setTextColor(COL_PRIMARY, COL_BG);
    tft.setCursor(PADDING + 18, listY + i * 14);
    tft.print(topList[idx][i]);
    
    // Draw attack count (right-aligned)
    tft.setTextColor(COL_ACCENT, COL_BG);
    char countStr[10];
    snprintf(countStr, 10, "x%d", topCounts[idx][i]);
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(countStr, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor(DISPLAY_WIDTH - w - PADDING, listY + i * 14);
    tft.print(countStr);
  }
  
  // Show instruction at bottom
  tft.setTextColor(COL_GRID, COL_BG);
  tft.setCursor(PADDING, DISPLAY_HEIGHT - BOTTOM_STATUS_H - 12);
  tft.print("B:BACK");
}

void drawMainMenu() {
  uint8_t yStart = STATUS_H + PADDING + 1; 
  
  tft.setFont();
  tft.setTextSize(1);


    tft.fillRect(0, yStart, DISPLAY_WIDTH, 12, COL_BG);
    tft.setTextColor(COL_WARNING, COL_BG);
    tft.setCursor(PADDING, yStart + 2); 
    tft.printf("BdgOS v%s | CRD:%u", currentVersion, currentScore);

      tft.setTextColor(COL_GLITCH_ORANGE, COL_BG);
      int16_t x1, y1;
      uint16_t w, h;
      char posText[32];
      snprintf(posText, 32, "Rank #%d", currentPosition);
      tft.getTextBounds(posText, 0, 0, &x1, &y1, &w, &h);
      uint16_t nickX = (DISPLAY_WIDTH - w) / 2;
      tft.setCursor(nickX, DISPLAY_HEIGHT - BOTTOM_STATUS_H - 11);
      tft.print(posText);

  
  yStart += 12;

  tft.drawFastHLine(0, yStart, DISPLAY_WIDTH, COL_GRID); 
  yStart += 5;
  
  for (uint8_t i = 0; i < MENU_ITEM_COUNT; i++) {
    uint16_t color = (i == selectedItem) ? COL_ACCENT : COL_PRIMARY;
    tft.setTextColor(color, COL_BG); 
    
    tft.setCursor(PADDING, yStart + i * 11);
    tft.print((i == selectedItem) ? ">>" : "  "); 
    
    tft.setCursor(PADDING + 16, yStart + i * 11);
    tft.print(MAIN_MENU_ITEMS[i].title);
  }

  if (badgeIdentifier != 0) {
      tft.setFont(&FreeSansBold9pt7b);
      tft.setTextColor(COL_GLITCH_ORANGE, COL_BG);
      int16_t x1, y1;
      uint16_t w, h;
      tft.getTextBounds(badgeNickname, 0, 0, &x1, &y1, &w, &h);
      uint16_t nickX = (DISPLAY_WIDTH - w) / 2;
      tft.setCursor(nickX, DISPLAY_HEIGHT - BOTTOM_STATUS_H - 18);
      tft.print(badgeNickname);
      tft.setFont();
  }

}

void drawMenuLines(const MenuItem* menu, uint8_t yOffset, uint8_t lastIndex, uint8_t newIndex) {
  uint8_t yStart = STATUS_H + PADDING + 1 + yOffset; 

  tft.setFont();
  tft.setTextSize(1);

  tft.setTextColor(COL_PRIMARY, COL_BG); 
  tft.setCursor(PADDING, yStart + lastIndex * 11);
  tft.print("  "); 
  tft.setCursor(PADDING + 16, yStart + lastIndex * 11);
  tft.print(menu[lastIndex].title); 

  tft.setTextColor(COL_ACCENT, COL_BG); 
  tft.setCursor(PADDING, yStart + newIndex * 11);
  tft.print(">>"); 
  tft.setCursor(PADDING + 16, yStart + newIndex * 11);
  tft.print(menu[newIndex].title); 
}

void drawMainMenuOptimized() {
    drawMenuLines(MAIN_MENU_ITEMS, 17, lastSelectedItem, selectedItem);
}  

void drawDebugMenuOptimized() {
    drawMenuLines(DEBUG_MENU_ITEMS, 20, lastSelectedItem, selectedItem);
}

/**
 * @brief Draws the color scale legend for the scan screen.
 */
void drawColorScaleLegend() {
    uint8_t y = 100;
    uint8_t bar_h = 6;
    uint16_t bar_w = 140;
    uint16_t x_start = (DISPLAY_WIDTH - bar_w) / 2;

    for (uint16_t i = 0; i < bar_w; i++) {
        uint8_t intensity = map(i, 0, bar_w - 1, 0, 255);
        uint16_t color = mapJetColor(intensity);
        tft.drawFastVLine(x_start + i, y, bar_h, color);
    }
    
    tft.setFont();
    tft.setTextSize(1);
    tft.setTextColor(COL_PRIMARY, COL_BG);
    
    const char* min_label = "-90dBm";
    tft.setCursor(x_start, y + bar_h + 2);
    tft.print(min_label);
    
    const char* max_label = "-35dBm";
    uint8_t max_label_w = 6 * strlen(max_label);
    tft.setCursor(x_start + bar_w - max_label_w, y + bar_h + 2);
    tft.print(max_label); 
}

void drawBadgeGlow() {
    static unsigned long lastGlowDraw = 0;
    unsigned long now = millis();
    
    if (now - lastGlowDraw < GLOW_UPDATE_INTERVAL_MS) {
        return;
    }
    lastGlowDraw = now;

    for (uint16_t i = 0; i < MAX_BADGE_TRACKING; i++) {
        
        uint8_t intensity = badge_intensity[i];

        if (intensity > FADE_RATE) {
            intensity -= FADE_RATE; 
        } else {
            intensity = 0;
            all_peers[i].last_seen = 0;
        }
        badge_intensity[i] = intensity;

        uint16_t color = intensityToGlowColor(intensity);
        uint16_t color2 = mapNeonPurpleColor(intensity);
        if (intensity == 0) {
            color2 = COL_BG; // COL_BG is ST77XX_BLACK
            color = COL_BG; // COL_BG is ST77XX_BLACK
        }
        color = intensityToGlowColor(intensity);
        uint8_t badge_x = i % GRID_WIDTH_BADGES;
        uint8_t badge_y = i / GRID_WIDTH_BADGES;
        
        uint16_t pixel_x_start = badge_x * BADGE_PIXEL_SIZE;
        uint16_t pixel_y_start = badge_y * BADGE_PIXEL_SIZE;
        uint32_t buffer_index_start = pixel_y_start * GRID_WIDTH_PX + pixel_x_start;
        uint32_t buffer_index = buffer_index_start; 
        
       if (all_peers[i].nickname[0] == ARTIFACT)
        {
         uint16_t color3 = tft.color565(intensity, 0, 0);
         uint16_t color4 = tft.color565(0,intensity,intensity);
         for (int y=0; y < BADGE_PIXEL_SIZE; y++){
          for (int x=0; x < BADGE_PIXEL_SIZE; x++){
             if ((buffer_index + x) < (GRID_WIDTH_PX * FULL_GRID_HEIGHT_PX)) {
                if (random() % 2 == 1) badge_framebuffer[buffer_index + x] = color3;
                else badge_framebuffer[buffer_index + x] = color4;
                if (random() % 16 == 1) badge_framebuffer[buffer_index + x] = ST77XX_WHITE;
             }
          }
          buffer_index += GRID_WIDTH_PX;
         }
        }
        else if (all_peers[i].nickname[0] == SPONSOR)
        {
         for (int y=0; y < BADGE_PIXEL_SIZE; y++){
          for (int x=0; x < BADGE_PIXEL_SIZE; x++){
             if ((buffer_index + x) < (GRID_WIDTH_PX * FULL_GRID_HEIGHT_PX)) {
                //if (random() % 8 == 1) badge_framebuffer[buffer_index + x] = ST77XX_BLACK;
                //else badge_framebuffer[buffer_index + x] = color;
                if (y >= x) badge_framebuffer[buffer_index + x] = color;
             }
          }
          buffer_index += GRID_WIDTH_PX;
         }
         }
        else {
         for (int y=0; y < BADGE_PIXEL_SIZE; y++){
          for (int x=0; x < BADGE_PIXEL_SIZE; x++){
             if ((x == 0 && y == 0) || (x == (BADGE_PIXEL_SIZE - 1) && y == 0) || (x == 0 && y == (BADGE_PIXEL_SIZE - 1)) || (x == (BADGE_PIXEL_SIZE - 1) && y == (BADGE_PIXEL_SIZE - 1))) {
                //if (last_hits[i] > 0) badge_framebuffer[buffer_index + x] = color2;
                continue;
             }
             if ((buffer_index + x) < (GRID_WIDTH_PX * FULL_GRID_HEIGHT_PX)) {
                if (last_hits[i] > 0) badge_framebuffer[buffer_index + x] = color2;
                else badge_framebuffer[buffer_index + x] = color;
             }
          }
          buffer_index += GRID_WIDTH_PX;
         }
        }

    }

    if (selection_active) {
        uint8_t badge_x = selected_badge_id % GRID_WIDTH_BADGES;
        uint8_t badge_y = selected_badge_id / GRID_WIDTH_BADGES;
        uint16_t px = badge_x * BADGE_PIXEL_SIZE;
        uint16_t py = badge_y * BADGE_PIXEL_SIZE;

        drawFrameBufferBox(px - 2, py - 2, BADGE_PIXEL_SIZE + 4, BADGE_PIXEL_SIZE + 4, COL_WHITE);
        drawFrameBufferBox(px - 1, py - 1, BADGE_PIXEL_SIZE + 2, BADGE_PIXEL_SIZE + 2, COL_STATUS_BG);
    }

    const int16_t w = GRID_WIDTH_PX;
    const int16_t h = FULL_GRID_HEIGHT_PX;
    const int16_t y_tft = GRID_Y_START + 1;
    
    tft.drawRGBBitmap(0, y_tft, badge_framebuffer, w, h);
}

void drawScanScreen() {
  
  if (currentScreen == SCREEN_SCAN) {
     drawBadgeGlow();
  }

  if (selection_active && !isBadgeActive(selected_badge_id)) {
      uint16_t next_badge = findFirstActiveBadge();
      if (next_badge != UINT16_MAX) {
          selected_badge_id = next_badge;
          last_displayed_badge_id = -1;
      } else {
          selection_active = false;
          last_selection_active = true;
      }
  }
}

/**
 * @brief Helper function to draw a single line in the Exploit menu.
 */
void drawExploitMenuLine(uint8_t index, bool isSelected) {
    uint8_t yStart = 28;
    uint8_t y = yStart + index * 10;
    
    uint16_t color = isSelected ? COL_ACCENT : COL_PRIMARY;
    
    tft.fillRect(PADDING, y, DISPLAY_WIDTH - PADDING, 10, COL_BG);
    
    tft.setFont();
    tft.setTextSize(1);
    tft.setTextColor(color, COL_BG); 
    tft.setCursor(PADDING, y);
    tft.print(isSelected ? ">>" : "  "); 
    
    tft.setCursor(PADDING + 16, y);
    if (active_exploits[index]) { 
      tft.setTextColor(COL_NEON_PURPLE, COL_BG);
      tft.print("+");
    } else { 
      tft.print(" ");
    }
    
    tft.setTextColor(color, COL_BG); 
    tft.print(CYBER_PAIRS[index].exploitName);
}

void drawExploitScreen() {
  uint8_t yStart = STATUS_H + PADDING + 1; 

  tft.setFont();
  tft.setTextColor(COL_WARNING, COL_BG);
  tft.setTextSize(1);
  tft.setCursor(PADDING, yStart + 2);
  if (exploitScreenInAttackMode) {
    tft.print("L/R:TGL A:EXPLOIT B:BACK");
  } else {
    tft.print("L/R:TGL A:SAVE B:BCK");
  }
  tft.drawFastHLine(0, yStart + 12, DISPLAY_WIDTH, COL_GRID); 
  tft.drawFastHLine(0, 25, DISPLAY_WIDTH, COL_NEON_PURPLE);
  
  for (uint8_t i = 0; i < CYBER_PAIR_COUNT; i++) {
    drawExploitMenuLine(i, i == selectedExploit);
  }
}

/**
 * @brief Helper function to draw a single line in the Defense menu.
 */
void drawDefenseMenuLine(uint8_t index, bool isSelected) {
    uint8_t yStart = 28;
    uint8_t y = yStart + index * 10;
    
    uint16_t color = isSelected ? COL_ACCENT : COL_PRIMARY;

    tft.fillRect(PADDING, y, DISPLAY_WIDTH - PADDING, 10, COL_BG);
    
    tft.setFont();
    tft.setTextSize(1);
    tft.setTextColor(color, COL_BG); 
    tft.setCursor(PADDING, y);
    tft.print(isSelected ? ">>" : "  "); 
    
    tft.setCursor(PADDING + 16, y);
    if (active_defenses[index]) { 
      tft.setTextColor(COL_NEON_PURPLE, COL_BG);
      tft.print("+");
    } else { 
      tft.print(" ");
    }
    
    tft.setTextColor(color, COL_BG);
    tft.print(CYBER_PAIRS[index].defenseName);
}

void drawDefenseScreen() {
  uint8_t yStart = STATUS_H + PADDING + 1; 

  tft.setFont();
  tft.setTextColor(COL_WARNING, COL_BG);
  tft.setTextSize(1);
  tft.setCursor(PADDING, yStart + 2); 
  tft.print("L/R:TGL A:SAVE B:BCK");
  tft.drawFastHLine(0, yStart + 12, DISPLAY_WIDTH, COL_GRID); 
  tft.drawFastHLine(0, 25, DISPLAY_WIDTH, COL_NEON_PURPLE);
  
  for (uint8_t i = 0; i < CYBER_PAIR_COUNT; i++) {
    drawDefenseMenuLine(i, i == selectedDefense);
  }
}

/**
 * @brief Draws the temporary status message screen.
 * Now draws EITHER the "Exploit Detected" artwork (if isIncomingExploit is true)
 * OR the generic statusMessageLine1-3 text (if isIncomingExploit is false).
 */
void drawStatusMessageScreen() {

  if (isIncomingExploit) {
    
    drawCircuitBackground();
  
    int16_t x0 = DISPLAY_WIDTH / 2;
    int16_t y0 = (DISPLAY_HEIGHT / 2) - 20;
    int16_t x1 = (DISPLAY_WIDTH / 2) - 40;
    int16_t y1 = (DISPLAY_HEIGHT / 2) + 30;
    int16_t x2 = (DISPLAY_WIDTH / 2) + 40;
    int16_t y2 = (DISPLAY_HEIGHT / 2) + 30;
    tft.fillTriangle(x0, y0, x1, y1, x2, y2, COL_ERROR);
  
    tft.fillCircle(x0, y1 - 8, 4, COL_WHITE);
    tft.fillRect(x0 - 3, y0 + 10, 6, 25, COL_WHITE);
  
    tft.setFont(&FreeSansBold9pt7b);
    tft.setTextColor(COL_WHITE, COL_BG);
    int16_t xb, yb;
    uint16_t wb, hb;
  
    tft.getTextBounds("Exploit", 0, 0, &xb, &yb, &wb, &hb);
    tft.setCursor((DISPLAY_WIDTH - wb) / 2, y0 - 5);
    tft.print("Exploit");
    
    tft.getTextBounds("Detected", 0, 0, &xb, &yb, &wb, &hb);
    tft.setCursor((DISPLAY_WIDTH - wb) / 2, y1 + 18);
    tft.print("Detected");
  
    tft.setFont();
  } else {
    
    // 1. Draw 0/1 background ONLY FOR RSLT PACKET
    if (isIncomingResult) {
        drawCircuitBackground();
    }
    
    // 2. Draw Text (statusMessageLine1-3)
    tft.setFont(&FreeSansBold9pt7b);
    tft.setTextColor(COL_WARNING, COL_BG);
    int16_t x1, y1;
    uint16_t w, h;
    
    if (strlen(statusMessageLine1) > 0) {
      tft.getTextBounds(statusMessageLine1, 0, 0, &x1, &y1, &w, &h);
      tft.setCursor((DISPLAY_WIDTH - w) / 2, 45);
      tft.print(statusMessageLine1);
    }

    tft.setFont();
    tft.setTextSize(1);
    tft.setTextColor(COL_PRIMARY, COL_BG);
    if (strlen(statusMessageLine2) > 0) {
      tft.getTextBounds(statusMessageLine2, 0, 0, &x1, &y1, &w, &h);
      tft.setCursor((DISPLAY_WIDTH - w) / 2, 70);
      tft.print(statusMessageLine2);
    }

    if (strlen(statusMessageLine3) > 0) {
      tft.getTextBounds(statusMessageLine3, 0, 0, &x1, &y1, &w, &h);
      tft.setCursor((DISPLAY_WIDTH - w) / 2, 85);
      tft.print(statusMessageLine3);
    }
    
    tft.setFont();
  }
}

/**
 * @brief Draws the "Show Badge Data" screen.
 */
void drawShowDataScreen() {
  uint8_t yStart = STATUS_H + PADDING + 1; 
  
  tft.setFont();
  tft.setTextColor(COL_ERROR, COL_BG); 
  tft.setTextSize(1);
  tft.setCursor(PADDING, yStart + 2); 
  tft.print("!! SENSITIVE BADGE DATA !!");
  tft.drawFastHLine(0, yStart + 12, DISPLAY_WIDTH, COL_ERROR); 
  
  yStart += 25;

  tft.setTextColor(COL_PRIMARY, COL_BG);
  tft.setCursor(PADDING, yStart);
  tft.printf("NICK: %s", badgeNickname.c_str());

  yStart += 14;
  tft.setCursor(PADDING, yStart);
  tft.printf("ID:   %u", badgeIdentifier);

  yStart += 14;
  #ifdef MASTERBADGE
  tft.setCursor(PADDING, yStart);
  tft.printf("TOKEN: %u", secretToken);
  #endif

  yStart += 14;
  tft.setCursor(PADDING, yStart);
  tft.printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X", myMac[0], myMac[1], myMac[2], myMac[3], myMac[4], myMac[5]);

  tft.setTextColor(COL_PRIMARY, COL_BG);
  tft.setCursor(PADDING, DISPLAY_HEIGHT - BOTTOM_STATUS_H - 10); 
  tft.print("PRESS [B] TO RETURN.");
}

/**
 * @brief Draws the new screen for setting the time manually.
 */
void drawSetTimeScreen(bool forceRedraw) {
  uint8_t yStart = STATUS_H + PADDING + 1; 

  if (forceRedraw) {
    tft.setFont();
    tft.setTextColor(COL_ACCENT, COL_BG);
    tft.setTextSize(1);
    tft.setCursor(PADDING, yStart + 2); 
    tft.print("MANUAL TIME SET");
    tft.drawFastHLine(0, yStart + 12, DISPLAY_WIDTH, COL_GRID); 

    tft.setTextColor(COL_WARNING, COL_BG);
    tft.setCursor(PADDING, DISPLAY_HEIGHT - BOTTOM_STATUS_H - 10); 
    tft.print("[A] SAVE [B] BACK");
    
    lastSelectedTimeField = selectedTimeField; // Force redraw of time string
  }
  
  if (selectedTimeField == lastSelectedTimeField && !forceRedraw) {
    return; // Nothing to update
  }

  yStart += 25;
  uint8_t xStart = (DISPLAY_WIDTH - (16 * 6)) / 2; // Center "DD/MM/YYYY HH:MM"
  
  tft.setFont(&FreeSansBold9pt7b);
  tft.setTextSize(1);
  tft.fillRect(0, yStart, DISPLAY_WIDTH, 50, COL_BG);
  tft.setCursor(xStart, yStart + 15);
  char buf[5];

  // DD
  tft.setTextColor(selectedTimeField == 0 ? COL_ACCENT : COL_PRIMARY, COL_BG);
  sprintf(buf, "%02d", stagingTimeInfo.tm_mday);
  tft.print(buf);

  // /
  tft.setTextColor(COL_PRIMARY, COL_BG);
  tft.print("/");

  // MM
  tft.setTextColor(selectedTimeField == 1 ? COL_ACCENT : COL_PRIMARY, COL_BG);
  sprintf(buf, "%02d", stagingTimeInfo.tm_mon + 1); // tm_mon is 0-11
  tft.print(buf);

  // /
  tft.setTextColor(COL_PRIMARY, COL_BG);
  tft.print("/");

  // YYYY
  tft.setTextColor(selectedTimeField == 2 ? COL_ACCENT : COL_PRIMARY, COL_BG);
  sprintf(buf, "%04d", stagingTimeInfo.tm_year + 1900); // tm_year is since 1900
  tft.print(buf);
  
  // (space)
  tft.setCursor(xStart + 30, yStart + 35);
  
  // HH
  tft.setTextColor(selectedTimeField == 3 ? COL_ACCENT : COL_PRIMARY, COL_BG);
  sprintf(buf, "%02d", stagingTimeInfo.tm_hour);
  tft.print(buf);
  
  // :
  tft.setTextColor(COL_PRIMARY, COL_BG);
  tft.print(":");

  // MM
  tft.setTextColor(selectedTimeField == 4 ? COL_ACCENT : COL_PRIMARY, COL_BG);
  sprintf(buf, "%02d", stagingTimeInfo.tm_min);
  tft.print(buf);

  tft.setFont();
  lastSelectedTimeField = selectedTimeField;
}


void drawPlaceholderScreen(const char* title) {
  tft.setFont();
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.setFont(&FreeSansBold9pt7b);
  tft.setCursor(PADDING, STATUS_H + 28);
  tft.print(title);
  tft.setFont();
  
  tft.setTextColor(COL_PRIMARY, COL_BG);
  tft.setTextSize(1);
  tft.setCursor(PADDING, STATUS_H + 50);
  tft.print("STATUS: OFFLINE");
  tft.setCursor(DISPLAY_HEIGHT - BOTTOM_STATUS_H - 10, DISPLAY_HEIGHT - BOTTOM_STATUS_H - 10);
  tft.print("PRESS [B] TO RETURN.");
}

void drawCurrentScreen(bool forceRedraw) {
  static Screen lastScreen = SCREEN_MAIN_MENU; 
  
  if (!forceRedraw && currentScreen == lastScreen) {
    if (currentScreen == SCREEN_OTA_PROCESS) {
      drawOtaProcessScreen();
    } else if (currentScreen == SCREEN_SCAN) {
      drawBadgeGlow(); 
      drawScanScreen();
    } else if (currentScreen == SCREEN_SET_TIME) {
      drawSetTimeScreen(false);
    }
    return;
  }

  tft.fillRect(0, STATUS_H + 1, DISPLAY_WIDTH, DISPLAY_HEIGHT - STATUS_H - BOTTOM_STATUS_H - 2, COL_BG);
  
  lastScore = 0xFFFFFFFF;
 
  strcpy(lastTimeStr, "XXXXXXXX");
  strcpy(lastDateStr, "XXXXXXXXXX");
  lastIP = IPAddress(255, 255, 255, 255);
  lastWifiStatus = 0xFF;
  last_displayed_badge_id = -1;
  last_selection_active = !selection_active;

  tft.setFont();
  tft.setTextSize(1);

  switch (currentScreen) {
    case SCREEN_MAIN_MENU:
      drawMainMenu();
      break;
    case SCREEN_DEBUG_MENU:
      drawDebugMenu();
      break;
    case SCREEN_SETTINGS:
      drawSettingsScreen();
      break;
    case SCREEN_LOGS:
      drawLogsScreen();
      break;
    case SCREEN_SCAN:
      {
        const uint8_t HEADER_Y_START = STATUS_H + 1;
        tft.fillRect(0, HEADER_Y_START, DISPLAY_WIDTH, GRID_HEADER_H, COL_BG); 
        tft.setTextColor(COL_WARNING, COL_BG);
        tft.setTextSize(1);
        tft.setCursor(PADDING, HEADER_Y_START + 4);
        tft.print(SCAN_HEADERS[currentScanHeader]);
        tft.drawFastHLine(0, HEADER_Y_START + 12, DISPLAY_WIDTH, COL_GRID);
        
        tft.drawFastHLine(0, GRID_Y_START - 1, DISPLAY_WIDTH, COL_NEON_PURPLE); 
        tft.drawFastHLine(0, GRID_Y_START + FULL_GRID_HEIGHT_PX + 1, DISPLAY_WIDTH, COL_NEON_PURPLE); 

        tft.fillRect(0, GRID_Y_START + 1, GRID_WIDTH_PX, FULL_GRID_HEIGHT_PX, COL_BG); 

        drawColorScaleLegend();
      }
      drawScanScreen();
      break;
    case SCREEN_OTA_PROCESS:
      drawOtaProcessScreen();
      break;
    case SCREEN_EXPLOITS:
      drawExploitScreen();
      break;
    case SCREEN_DEFENSES:
      drawDefenseScreen();
      break;
    case SCREEN_ATTACK_STATUS:
      drawStatusMessageScreen();
      break;
    case SCREEN_SHOW_DATA:
      drawShowDataScreen();
      break;
    case SCREEN_HACKERMAN:
      drawHackermanScreen();
      break;
    case SCREEN_2038:
      draw2038Screen();
      break;
    case SCREEN_SET_TIME:
      drawSetTimeScreen(true);
      break;
    case SCREEN_REBOOT:
    default: 
      if (lastScreen == SCREEN_DEBUG_MENU) {
        drawPlaceholderScreen("DEBUG [FUNCTION UNIMP]");
      } else if (lastScreen == SCREEN_SETTINGS) {
        drawPlaceholderScreen("SETTINGS [FUNCTION UNIMP]");
      } else {
        drawPlaceholderScreen("ERROR [SYSTEM FAULT]");
      }
      break;
  }
  
  lastScreen = currentScreen;
}
/*
// ==== OTA Handler Logic ====
size_t performOtaUpdate(const String& url) {
    //Serial.printf("OTA: Starting update check for URL: %s\n", url.c_str());
    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();
    
    size_t totalWritten = 0;

    if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK) {
            int contentLength = http.getSize();
            bool canBegin = Update.begin(contentLength); 

            if (canBegin) {
                //Serial.printf("OTA: Partition check OK. Content length: %d\n", contentLength);
                otaState = OTA_DOWNLOADING;
                otaStatusMessage = "DOWNLOADING..."; 
                otaStartTime = millis();
                
                WiFiClient& client = *http.getStreamPtr();
                uint8_t buff[OTA_BUFF_SIZE];
                int lastProgress = -1;
                int lastDrawnProgress = -1; 
                
                while (http.connected() && (contentLength == -1 || totalWritten < contentLength)) {
                    size_t size = client.readBytes(buff, OTA_BUFF_SIZE);

                    if (size > 0) {
                        size_t written = Update.write(buff, size);
                        
                        if (written != size) {
                            Update.end(false); 
                            otaState = OTA_FAILED;
                            otaStatusMessage = "FLASH WRITE ERR";
                            http.end();
                            return 0; 
                        }
                        
                        totalWritten += written;
                        
                        if (contentLength > 0) {
                            int currentProgress = (totalWritten * 100) / contentLength;
                            if (currentProgress > lastProgress) {
                                otaProgress = currentProgress;
                                if ((currentProgress % 5 == 0 || currentProgress == 100) && currentProgress != lastDrawnProgress) { 
                                    drawCurrentScreen(false); 
                                    lastDrawnProgress = currentProgress;
                                }
                                lastProgress = currentProgress;
                            }
                        }
                    } else {
                        delay(10); 
                    }
                    
                    if (contentLength > 0 && totalWritten >= (size_t)contentLength) {
                        break; 
                    }
                }
                
                otaProgress = 100; 
                
                if (Update.end(true)) {
                    otaState = OTA_SUCCESS;
                    otaStatusMessage = "UPDATE SUCCESS!"; 
                    //Serial.println("OTA: SUCCESS! Rebooting...");
                    
                    drawCurrentScreen(true); 
                    Serial.flush(); 
                    delay(2000);
                    ESP.restart();
                    
                } else {
                    otaState = OTA_FAILED;
                    int updateError = Update.getError();
                    //Serial.printf("OTA: FLASH FAILED during end(). Error: %d\n", updateError);
                    otaStatusMessage = (updateError == 7) ? "FLASH NO SPACE" : "END ERR"; 
                    Update.end(false);
                }
            } else {
                otaState = OTA_FAILED;
                //Serial.printf("OTA: FLASH FAIL: SIZE. Error: %d\n", Update.getError());
                otaStatusMessage = "FLASH SIZE ERR"; 
            }
        } else {
            otaState = OTA_FAILED;
            otaStatusMessage = "HTTP ERR: " + String(httpCode); 
        }
    } else {
        otaState = OTA_FAILED;
        otaStatusMessage = "CONN FAIL: HTTP"; 
    }

    http.end();
    return totalWritten; 
}
*/
size_t performOtaUpdate(const String& url) {
    //Serial.printf("OTA: Starting secure update check for URL: %s\n", url.c_str());

    // <<< MODIFIED: Create a WiFiClientSecure object
    WiFiClientSecure secureClient;

    // <<< MODIFIED: Set the Root CA certificate
    secureClient.setCACert(kripthor_cert);

    // Optional: If your server uses a self-signed cert (NOT recommended)
    // secureClient.setInsecure();

    HTTPClient http;

    // <<< MODIFIED: Pass the secure client and URL to http.begin()
    if (!http.begin(secureClient, url)) {
        //Serial.println("OTA: Failed to begin HTTPS connection!");
        otaState = OTA_FAILED;
        otaStatusMessage = "HTTPS BEGIN FAIL";
        return 0;
    }

    int httpCode = http.GET();
    
    size_t totalWritten = 0;

    if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK) {
            int contentLength = http.getSize();
            bool canBegin = Update.begin(contentLength); 

            if (canBegin) {
                //Serial.printf("OTA: Partition check OK. Content length: %d\n", contentLength);
                otaState = OTA_DOWNLOADING;
                otaStatusMessage = "DOWNLOADING..."; 
                otaStartTime = millis();
                
                // <<< MODIFIED: Get the stream as a base 'Stream&'
                // This works for both WiFiClient and WiFiClientSecure
                Stream& stream = http.getStream(); 
                
                uint8_t buff[OTA_BUFF_SIZE];
                int lastProgress = -1;
                int lastDrawnProgress = -1; 
                
                while (http.connected() && (contentLength == -1 || totalWritten < contentLength)) {
                    
                    // <<< MODIFIED: Read bytes from the 'stream' object
                    size_t size = stream.readBytes(buff, OTA_BUFF_SIZE);

                    if (size > 0) {
                        size_t written = Update.write(buff, size);
                        
                        if (written != size) {
                            Update.end(false); 
                            otaState = OTA_FAILED;
                            otaStatusMessage = "FLASH WRITE ERR";
                            http.end();
                            return 0; 
                        }
                        
                        totalWritten += written;
                        
                        if (contentLength > 0) {
                            int currentProgress = (totalWritten * 100) / contentLength;
                            if (currentProgress > lastProgress) {
                                otaProgress = currentProgress;
                                if ((currentProgress % 2 == 0 || currentProgress == 100) && currentProgress != lastDrawnProgress) { 
                                    drawCurrentScreen(false); 
                                    lastDrawnProgress = currentProgress;
                                }
                                lastProgress = currentProgress;
                            }
                        }
                    } else {
                        delay(10); 
                    }
                    
                    if (contentLength > 0 && totalWritten >= (size_t)contentLength) {
                        break; 
                    }
                }
                
                otaProgress = 100; 
                
                if (Update.end(true)) {
                    otaState = OTA_SUCCESS;
                    otaStatusMessage = "UPDATE SUCCESS!"; 
                    //Serial.println("OTA: SUCCESS! Rebooting...");
                    
                    drawCurrentScreen(true); 
                    Serial.flush(); 
                    delay(2000);
                    ESP.restart();
                    
                } else {
                    otaState = OTA_FAILED;
                    int updateError = Update.getError();
                    //Serial.printf("OTA: FLASH FAILED during end(). Error: %d\n", updateError);
                    otaStatusMessage = (updateError == 7) ? "FLASH NO SPACE" : "END ERR"; 
                    Update.end(false);
                }
            } else {
                otaState = OTA_FAILED;
                //Serial.printf("OTA: FLASH FAIL: SIZE. Error: %d\n", Update.getError());
                otaStatusMessage = "FLASH SIZE ERR"; 
            }
        } else {
            otaState = OTA_FAILED;
            //Serial.printf("OTA: HTTP Error: %d\n", httpCode);
        }
    } else {
        otaState = OTA_FAILED;
        // <<< MODIFIED: More descriptive error
        otaStatusMessage = "CONN FAIL: HTTP";
        //Serial.printf("OTA: Connection failed: %s\n", http.errorToString(httpCode).c_str());
    }

    http.end();
    return totalWritten; 
}

void handleOtaCheck() {
  if (currentScreen != SCREEN_OTA_PROCESS) return;

  switch (otaState) {
    case OTA_CONNECTING_WIFI:
      otaStatusMessage = "CONNECTING..."; 
      if (connectToWiFi()) {
          otaState = OTA_CHECKING;
      } else {
          otaState = OTA_FAILED;
          otaStatusMessage = "WIFI TIMEOUT"; 
      }
      drawCurrentScreen(true);
      break;

    case OTA_CHECKING:
      if (WiFi.status() == WL_CONNECTED) {
        //Serial.printf("OTA: Wi-Fi connected! IP: %s\n", WiFi.localIP().toString().c_str());
        drawBottomStatusBar();
        performOtaUpdate(ijijjiijjiijiijjjijijijj); 
        drawCurrentScreen(true);
      } else if (WiFi.status() == WL_CONNECT_FAILED || millis() - otaStartTime > 15000) {
        //Serial.println("OTA: Wi-Fi connection FAILED or TIMED OUT.");
        otaState = OTA_FAILED;
        otaStatusMessage = "WIFI TIMEOUT"; 
        drawCurrentScreen(true);
      }
      break;

    case OTA_FAILED:
      //Serial.println("OTA: Failed. Rebooting...");
      drawCurrentScreen(true);
      delay(3000);
      ESP.restart();
      break;
      
    default:
      break;
  }
}

// ==== Input and Navigation Logic ====

/**
 * @brief Helper to check if a badge is valid and recently seen.
 */
bool isBadgeActive(uint16_t id) {
    if (id >= MAX_BADGE_TRACKING) return false;
    return (all_peers[id].last_seen > 0 && (millis() - all_peers[id].last_seen < BADGE_ACTIVE_TIMEOUT_MS));
}

/**
 * @brief Finds the first active badge on the grid, starting from ID 0.
 */
uint16_t findFirstActiveBadge() {
    for (uint16_t i = 0; i < MAX_BADGE_TRACKING; i++) {
        if (isBadgeActive(i)) {
            return i;
        }
    }
    return UINT16_MAX;
}

/**
 * @brief For UP/DOWN jumps, finds the closest active badge in a target row.
 */
uint16_t findClosestActiveInRow(uint8_t row, uint8_t target_col) {
    uint16_t row_start_id = row * GRID_WIDTH_BADGES;

    for (uint8_t offset = 0; offset <= GRID_WIDTH_BADGES / 2; offset++) {
        uint16_t id_right = row_start_id + (target_col + offset) % GRID_WIDTH_BADGES;
        if (isBadgeActive(id_right)) return id_right;
        
        if (offset > 0) {
            uint16_t id_left = row_start_id + (target_col + GRID_WIDTH_BADGES - offset) % GRID_WIDTH_BADGES;
            if (isBadgeActive(id_left)) return id_left;
        }
    }
    
    return UINT16_MAX;
}

/**
 * @brief For LEFT/RIGHT moves, finds the next active badge on the same row.
 * Returns UINT16_MAX if no *other* active badges are found on this row.
 */
uint16_t findNextActiveInRow(uint16_t current_id, bool search_forward) {
    uint8_t row = current_id / GRID_WIDTH_BADGES;
    uint8_t col = current_id % GRID_WIDTH_BADGES;
    uint16_t row_start_id = row * GRID_WIDTH_BADGES;

    for (uint8_t i = 1; i < GRID_WIDTH_BADGES; i++) {
        uint16_t id_to_check;
        if (search_forward) {
            id_to_check = row_start_id + (col + i) % GRID_WIDTH_BADGES;
        } else {
            id_to_check = row_start_id + (col + GRID_WIDTH_BADGES - i) % GRID_WIDTH_BADGES;
        }
        if (isBadgeActive(id_to_check)) return id_to_check;
    }
    
    if (isBadgeActive(current_id)) return current_id;
    
    return UINT16_MAX;
}

/**
 * @brief Handles UP/DOWN/LEFT/RIGHT navigation on the scan screen.
 */
void handleScanNavigation() {
    
    if (!upPressed && !downPressed && !ltPressed && !rtPressed) {
      return;
    }

    bool changed = false;
    uint16_t new_id = selected_badge_id;
    
    if (!selection_active) {
        new_id = findFirstActiveBadge();
        if (new_id != UINT16_MAX) {
            selection_active = true;
            changed = true;
        }
    } else {
        uint8_t current_row = selected_badge_id / GRID_WIDTH_BADGES;
        uint8_t current_col = selected_badge_id % GRID_WIDTH_BADGES;

        if (ltPressed) {
            new_id = findNextActiveInRow(selected_badge_id, false);
            if (new_id == UINT16_MAX || new_id == selected_badge_id) {
                for (uint8_t i = 1; i < BADGE_GRID_ROWS; i++) {
                    uint8_t target_row = (current_row + BADGE_GRID_ROWS - i) % BADGE_GRID_ROWS;
                    uint16_t id_in_row = findClosestActiveInRow(target_row, GRID_WIDTH_BADGES - 1);
                    if (id_in_row != UINT16_MAX) {
                        new_id = id_in_row;
                        break;
                    }
                }
            }
            changed = true;
        } else if (rtPressed) {
            new_id = findNextActiveInRow(selected_badge_id, true);
            if (new_id == UINT16_MAX || new_id == selected_badge_id) {
                for (uint8_t i = 1; i < BADGE_GRID_ROWS; i++) {
                    uint8_t target_row = (current_row + i) % BADGE_GRID_ROWS;
                    uint16_t id_in_row = findClosestActiveInRow(target_row, 0);
                    if (id_in_row != UINT16_MAX) {
                        new_id = id_in_row;
                        break;
                    }
                }
            }
            changed = true;
        } else if (upPressed) {
            for (uint8_t i = 1; i < BADGE_GRID_ROWS; i++) {
                uint8_t target_row = (current_row + BADGE_GRID_ROWS - i) % BADGE_GRID_ROWS;
                uint16_t id_in_row = findClosestActiveInRow(target_row, current_col);
                if (id_in_row != UINT16_MAX) {
                    new_id = id_in_row;
                    changed = true;
                    break;
                }
            }
        } else if (downPressed) {
            for (uint8_t i = 1; i < BADGE_GRID_ROWS; i++) {
                uint8_t target_row = (current_row + i) % BADGE_GRID_ROWS;
                uint16_t id_in_row = findClosestActiveInRow(target_row, current_col);
                if (id_in_row != UINT16_MAX) {
                    new_id = id_in_row;
                    changed = true;
                    break;
                }
            }
        }
    }

    if (changed) {
        if (new_id == UINT16_MAX) {
            selection_active = false;
        } else {
            selected_badge_id = new_id;
        }
        last_displayed_badge_id = -1;
    }
}

/**
 * @brief Builds and sends the EXP packet to the selected target.
 */
void launchExploit() {
    if (!selection_active) {
        snprintf(statusMessageLine1, 40, "EXPLOIT FAILED!");
        snprintf(statusMessageLine2, 40, "NO TARGET SELECTED.");
        snprintf(statusMessageLine3, 40, "");
        screenToReturnTo = SCREEN_MAIN_MENU;  // Return to main menu
        showStatusMessage = true;
        return;
    }
    
    uint8_t* targetMac = all_peers[selected_badge_id].mac_addr;
    uint32_t targetBadgeIdentifier = all_peers[selected_badge_id].badgeIdentifier;

    if (targetBadgeIdentifier == 0) {
        snprintf(statusMessageLine1, 40, "EXPLOIT FAILED!");
        snprintf(statusMessageLine2, 40, "TARGET NOT REGISTERED.");
        snprintf(statusMessageLine3, 40, "(Cannot find ID)");
        screenToReturnTo = SCREEN_MAIN_MENU;  // Return to main menu
        showStatusMessage = true;
        return;
    }


    
    tft.setFont();
    
    uint8_t payload[22];
    payload[0] = 'E';
    payload[1] = 'X';
    payload[2] = 'P';
    payload[3] = 255;
    payload[4] = 255;
    payload[5] = 255;
    
    uint8_t exploit_count = 0;
    for(uint8_t i = 0; i < CYBER_PAIR_COUNT && exploit_count < 3; i++) {
        if (active_exploits[i]) {
            payload[3 + exploit_count] = i;
            exploit_count++;
        }
    }

    strncpy((char*)&payload[6], badgeNickname.c_str(), 16);
    if (badgeNickname.length() < 16) {
        memset(&payload[6 + badgeNickname.length()], 0, 16 - badgeNickname.length());
    }

    if (!esp_now_is_peer_exist(targetMac)) {
        memset(&peerInfo, 0, sizeof(peerInfo));
        memcpy(peerInfo.peer_addr, targetMac, 6);
        peerInfo.channel = 0; 
        peerInfo.encrypt = false;
        if (esp_now_add_peer(&peerInfo) != ESP_OK) {
            //Serial.println("Failed to add exploit target peer.");
            snprintf(statusMessageLine1, 40, "EXPLOIT FAILED!");
            snprintf(statusMessageLine2, 40, "PEER ADD FAILED.");
            snprintf(statusMessageLine3, 40, "");
            screenToReturnTo = SCREEN_MAIN_MENU;  // Return to main menu
            showStatusMessage = true;
            return;
        }
    }

    esp_now_send(targetMac, payload, sizeof(payload));

    if (memcmp(targetMac, BROADCAST_MAC, 6) != 0) {
        esp_now_del_peer(targetMac);
    }
    
    tft.fillRect(0, STATUS_H + 1, DISPLAY_WIDTH, DISPLAY_HEIGHT - STATUS_H - BOTTOM_STATUS_H - 2, COL_BG); 
    drawCircuitBackground();
    
    tft.setFont(&FreeSansBold9pt7b);
    tft.setTextColor(COL_GLITCH_ORANGE);
    
    int16_t x1, y1;
    uint16_t w, h;
    const char* msg = "Running 0-days!";
    tft.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((DISPLAY_WIDTH - w) / 2, (DISPLAY_HEIGHT - h) / 2);
    tft.print(msg);
    pendingExploitPost = true;
    pendingTargetId = targetBadgeIdentifier;

    snprintf(statusMessageLine1, 40, "HACKING!");
    snprintf(statusMessageLine2, 40, "TARGET: %s", all_peers[selected_badge_id].nickname);
    snprintf(statusMessageLine3, 40, "");
    screenToReturnTo = SCREEN_MAIN_MENU;  // Return to main menu
    showStatusMessage = true;
    
    // Clear selection after launching attack - user must choose target again
    selection_active = false;
}

void handleMenuNavigation() {
    if (currentScreen == SCREEN_HACKERMAN || currentScreen == SCREEN_2038) {
        return;
    }
    if (bPressed) {
    
    if (currentScreen == SCREEN_ATTACK_STATUS) {
        return;
    }
    
    // Special case: OTA screen goes back to debug menu
    if (currentScreen == SCREEN_OTA_PROCESS) {
      if (otaState == OTA_DOWNLOADING) return; 
      WiFi.disconnect(true);
      otaState = OTA_IDLE;
      otaStatusMessage = "SYSTEM READY. CHECK FOR UPDATES.";
      currentScreen = SCREEN_DEBUG_MENU;
      parentScreen = SCREEN_MAIN_MENU;  // Debug menu's parent is main
      drawCurrentScreen(true);
      initESPNOW(false);
      return;
    } 

    // Special case: Show data goes back to debug menu
    if (currentScreen == SCREEN_SHOW_DATA) {
        currentScreen = SCREEN_DEBUG_MENU;
        parentScreen = SCREEN_MAIN_MENU;
        drawCurrentScreen(true);
        return;
    }
    
    // Hierarchical navigation: Use parent screen
    // Main menu is the root - pressing B does nothing
    if (currentScreen == SCREEN_MAIN_MENU) {
        return;  // Already at root, nowhere to go back
    }
    
    {  // Block for other screens
    
    // For all other screens, go back to parent without saving
    Screen returnScreen = parentScreen;
    
    // Reset sub-menu selections when leaving (discard changes)
    if (currentScreen == SCREEN_SCAN) {
        selection_active = false;
        // Keep selection active when going back to main menu
        // This allows returning to exploits with target still selected
    }
    if (currentScreen == SCREEN_EXPLOITS) {
        // B button goes back without saving exploits
        selectedExploit = 0;
        lastSelectedExploit = 0;
        exploitsChanged = false;  // Discard changes
        exploitScreenInAttackMode = false;  // Reset attack mode flag
    }
    if (currentScreen == SCREEN_DEFENSES) {
        // B button goes back without saving defenses
        selectedDefense = 0;
        lastSelectedDefense = 0;
        defensesChanged = false;  // Discard changes
    }
    if (currentScreen == SCREEN_SETTINGS) {
        selectedSettingItem = 0;
        lastSelectedSettingItem = 0;
    }
    if (currentScreen == SCREEN_LOGS) {
        selectedLogItem = 0;
        lastSelectedLogItem = 0;
    }
    
    // Navigate back to parent
    currentScreen = returnScreen;
    
    // Restore parent's parent (for multi-level navigation)
    if (currentScreen == SCREEN_SCAN || currentScreen == SCREEN_EXPLOITS ||
        currentScreen == SCREEN_DEFENSES || currentScreen == SCREEN_SETTINGS ||
        currentScreen == SCREEN_LOGS) {
        parentScreen = SCREEN_MAIN_MENU;
    } else if (currentScreen == SCREEN_DEBUG_MENU) {
        parentScreen = SCREEN_MAIN_MENU;
    } else {
        parentScreen = SCREEN_MAIN_MENU;  // Default parent
    }
    
    drawCurrentScreen(true);
    return;
    }  // End of else block
  }

  if (currentScreen == SCREEN_MAIN_MENU || currentScreen == SCREEN_DEBUG_MENU) {
    
    const MenuItem* menu = (currentScreen == SCREEN_MAIN_MENU) ? MAIN_MENU_ITEMS : DEBUG_MENU_ITEMS;
    const uint8_t count = (currentScreen == SCREEN_MAIN_MENU) ? MENU_ITEM_COUNT : DEBUG_ITEM_COUNT;
    
    bool selectionChanged = false;

    if (upPressed || downPressed) {
        lastSelectedItem = selectedItem; 
        if (upPressed) {
            selectedItem = (selectedItem == 0) ? (count - 1) : (selectedItem - 1);
        }
        if (downPressed) {
            selectedItem = (selectedItem + 1) % count;
        }
        selectionChanged = true;
    }

    if (aPressed) {
      Screen target = menu[selectedItem].targetScreen;

       if (target == SCREEN_REMOTE_FW_UPDATE) {

          #ifdef MASTERBADGE
          uint8_t payload[HASH_SIZE+SIG_SIZE];
          memcpy(pkimessage,"RMTFWU",6);
          memcpy(pkimessage+6,currentVersion,4);
          memcpy(pkimessage+6+4,"3456781234567812345678",HASH_SIZE-6-4);
          memcpy(payload, pkimessage, HASH_SIZE);
          if (!pkisign()) {
            snprintf(statusMessageLine1, 40, "REMOTE UPDATE");
            snprintf(statusMessageLine2, 40, "ERROR!");
            screenToReturnTo = SCREEN_DEBUG_MENU;
            showStatusMessage = true;
            return;
          }
          memcpy(payload+HASH_SIZE, pkisig, SIG_SIZE);
          printHex("Payload sent: ",payload, HASH_SIZE+SIG_SIZE);
          esp_now_send(BROADCAST_MAC, payload, HASH_SIZE+SIG_SIZE);
          #endif

          snprintf(statusMessageLine1, 40, "REMOTE UPDATE");
          snprintf(statusMessageLine2, 40, "BROADCASTING...");
          snprintf(statusMessageLine3, 40, "");
          screenToReturnTo = SCREEN_DEBUG_MENU;
          showStatusMessage = true;
          return;
      }

      if (target == SCREEN_REMOTE_POWER_OFF) {
          // STILL NEEDS TO ADD INCREMENTAL TOKEN FOR REPLAY ATTACKS MITIGATION
          // I'LL ACCEPT THE RISK FOR A 2 DAY BADGE. HOPEFULLY THIS COMMAND WILL NOT BE USED AT ALL
          // SO, NO SNIFFING.
          #ifdef MASTERBADGE
          uint8_t payload[HASH_SIZE+SIG_SIZE];
          memcpy(pkimessage,"RMTOFF78123456781234567812345678",HASH_SIZE);
          memcpy(payload, pkimessage, HASH_SIZE);
          if (!pkisign()) {
            snprintf(statusMessageLine1, 40, "REMOTE POWEROFF");
            snprintf(statusMessageLine2, 40, "ERROR!");
            screenToReturnTo = SCREEN_DEBUG_MENU;
            showStatusMessage = true;
            return;
          }
          memcpy(payload+HASH_SIZE, pkisig, SIG_SIZE);
          printHex("Payload sent: ",payload, HASH_SIZE+SIG_SIZE);
          esp_now_send(BROADCAST_MAC, payload, HASH_SIZE+SIG_SIZE);
          #endif

          snprintf(statusMessageLine1, 40, "REMOTE POWEROFF");
          snprintf(statusMessageLine2, 40, "BROADCASTING...");
          snprintf(statusMessageLine3, 40, "");
          screenToReturnTo = SCREEN_DEBUG_MENU;
          showStatusMessage = true;
          return;
      }

      if (target == SCREEN_MASTER_TROLL) {
          // STILL NEEDS TO ADD INCREMENTAL TOKEN FOR REPLAY ATTACKS MITIGATION
          // I'LL ACCEPT THE RISK FOR A 2 DAY BADGE. HOPEFULLY THIS COMMAND WILL NOT BE USED AT ALL
          // SO, NO SNIFFING.
          #ifdef MASTERBADGE
          uint8_t payload[HASH_SIZE+SIG_SIZE];
          memcpy(pkimessage,"RMTTRL78123456781234567812345678",HASH_SIZE);
          memcpy(payload, pkimessage, HASH_SIZE);
          if (!pkisign()) {
            snprintf(statusMessageLine1, 40, "REMOTE TROLL");
            snprintf(statusMessageLine2, 40, "ERROR!");
            screenToReturnTo = SCREEN_DEBUG_MENU;
            showStatusMessage = true;
            return;
          }
          memcpy(payload+HASH_SIZE, pkisig, SIG_SIZE);
          printHex("Payload sent: ",payload, HASH_SIZE+SIG_SIZE);
          esp_now_send(BROADCAST_MAC, payload, HASH_SIZE+SIG_SIZE);
          #endif

          snprintf(statusMessageLine1, 40, "REMOTE TROLL");
          snprintf(statusMessageLine2, 40, "BROADCASTING...");
          snprintf(statusMessageLine3, 40, "MASS Y2K38!");
          screenToReturnTo = SCREEN_DEBUG_MENU;
          showStatusMessage = true;
          return;
      }

      if (target == SCREEN_REMOTE_FACT_RESET) {
          // STILL NEEDS TO ADD INCREMENTAL TOKEN FOR REPLAY ATTACKS MITIGATION
          // I'LL ACCEPT THE RISK FOR A 2 DAY BADGE. HOPEFULLY THIS COMMAND WILL NOT BE USED AT ALL
          // SO, NO SNIFFING.
          #ifdef MASTERBADGE
          uint8_t payload[HASH_SIZE+SIG_SIZE];
          memcpy(pkimessage,"RMTRST78123456781234567812345678",HASH_SIZE);
          memcpy(payload, pkimessage, HASH_SIZE);
          if (!pkisign()) {
            snprintf(statusMessageLine1, 40, "REMOTE RESET");
            snprintf(statusMessageLine2, 40, "ERROR!");
            screenToReturnTo = SCREEN_DEBUG_MENU;
            showStatusMessage = true;
            return;
          }
          memcpy(payload+HASH_SIZE, pkisig, SIG_SIZE);
          printHex("Payload sent: ",payload, HASH_SIZE+SIG_SIZE);
          esp_now_send(BROADCAST_MAC, payload, HASH_SIZE+SIG_SIZE);
          #endif

          snprintf(statusMessageLine1, 40, "REMOTE RESET");
          snprintf(statusMessageLine2, 40, "BROADCASTING...");
          snprintf(statusMessageLine3, 40, "");
          screenToReturnTo = SCREEN_DEBUG_MENU;
          showStatusMessage = true;
          return;
      }

      if (target == SCREEN_CLEAR_PREFS) {
          preferences.begin("badge-os", false);
          preferences.clear();
          preferences.end();
          
          //Serial.println("NVS CLEARED. REBOOTING.");
          tft.fillScreen(COL_BG);
          tft.setFont();
          tft.setTextColor(COL_ERROR);
          tft.setFont(&FreeSansBold9pt7b);
          tft.setCursor(20, 60);
          tft.print("NVS CLEARED");
          tft.setCursor(20, 80);
          tft.print("REBOOTING...");
          tft.setFont();
          delay(2000);
          ESP.restart();
          return;
      }
      
      // Set parent before changing screen
      parentScreen = currentScreen;
      currentScreen = target;
      
      if (currentScreen == SCREEN_OTA_PROCESS) {
        esp_now_deinit();
        otaState = OTA_CONNECTING_WIFI;
        otaStartTime = millis(); 
        if (!backlightOn) {
            digitalWrite(PIN_DISPLAYLED, HIGH);
            backlightOn = true;
        }
        lastButtonPressTime = millis();
      } else if (currentScreen == SCREEN_SCAN) {
          initESPNOW(true);
      } else if (currentScreen == SCREEN_EXPLOITS) {
          exploitScreenInAttackMode = false;  // Entering from menu in config mode
      }
      
      selectedItem = 0; 
      lastSelectedItem = 0;
      drawCurrentScreen(true); 
      return;
    }

    if (selectionChanged) {
      if (currentScreen == SCREEN_MAIN_MENU) {
          drawMainMenuOptimized(); 
      } else if (currentScreen == SCREEN_DEBUG_MENU) {
          drawDebugMenuOptimized();
      }
    }
  }
  else if (currentScreen == SCREEN_SCAN) {
      if (selection_active && aPressed) {
          parentScreen = SCREEN_SCAN;  // Set scan as parent
          currentScreen = SCREEN_EXPLOITS;
          exploitScreenInAttackMode = true;  // Entering in attack mode with target
          drawCurrentScreen(true);
          return;
      }
      handleScanNavigation();
  }
  else if (currentScreen == SCREEN_SETTINGS) {
    bool selectionChanged = false;
    bool stateChanged = false;
    const uint8_t SETTINGS_MENU_COUNT = 7;

    if (upPressed) {
      lastSelectedSettingItem = selectedSettingItem;
      selectedSettingItem = (selectedSettingItem == 0) ? (SETTINGS_MENU_COUNT - 1) : (selectedSettingItem - 1);
      selectionChanged = true;
    }
    if (downPressed) {
      lastSelectedSettingItem = selectedSettingItem;
      selectedSettingItem = (selectedSettingItem == (SETTINGS_MENU_COUNT - 1)) ? 0 : (selectedSettingItem + 1);
      selectionChanged = true;
    }
      
    // LEFT/RIGHT to toggle Show Exploits setting when on that item
    if ((ltPressed || rtPressed) && selectedSettingItem == 0) {
      showAllExploits = !showAllExploits;
      stateChanged = true;
      saveAppSettings();
      selectionChanged = true;
    }
    
    if (aPressed) {
      // A button also toggles Show Exploits for backward compatibility
      if (selectedSettingItem == 0) {
          showAllExploits = !showAllExploits;
          stateChanged = true;
          saveAppSettings();
          selectionChanged = true;
      }
    }
    
    if (aPressed) {

         if (selectedSettingItem == 1) {
            snprintf(statusMessageLine1, 40, "REQUEST SENT");
            snprintf(statusMessageLine2, 40, "Syncing state...");
            snprintf(statusMessageLine3, 40, "");
            screenToReturnTo = SCREEN_SETTINGS;
            currentScreen = SCREEN_ATTACK_STATUS;
            drawCurrentScreen(true);
            
            postSync();
            
            snprintf(statusMessageLine1, 40, "SYNC COMPLETE");
            snprintf(statusMessageLine2, 40, "Score/state updated.");
            snprintf(statusMessageLine3, 40, "");
            statusMessageStartTime = millis();
            drawCurrentScreen(true);
        } else if (selectedSettingItem == 2) {
            snprintf(statusMessageLine1, 40, "SYNCING TIME...");
            snprintf(statusMessageLine2, 40, "Please wait...");
            snprintf(statusMessageLine3, 40, "");
            screenToReturnTo = SCREEN_SETTINGS;
            currentScreen = SCREEN_ATTACK_STATUS;
            drawCurrentScreen(true);
            
            bool syncSuccess = postGetTime();
            
            if (syncSuccess) {
                snprintf(statusMessageLine1, 40, "TIME SYNC OK!");
                snprintf(statusMessageLine2, 40, "");
                snprintf(statusMessageLine3, 40, "");
            } else {
                snprintf(statusMessageLine1, 40, "SYNC FAILED!");
                snprintf(statusMessageLine2, 40, "Check WiFi connection.");
                snprintf(statusMessageLine3, 40, "");
            }
            statusMessageStartTime = millis();
            drawCurrentScreen(true);
        } else if (selectedSettingItem == 3) {
            // Navigate to SET TIME screen
            if (timeIsSynced) {
              time_t now = timeOffset + (millis() / 1000);
              stagingTimeInfo = *gmtime(&now);
            } else {
              memset(&stagingTimeInfo, 0, sizeof(stagingTimeInfo));
              stagingTimeInfo.tm_year = 2025 - 1900; // tm_year is years since 1900
              stagingTimeInfo.tm_mon = 10; // 10 = Nov
              stagingTimeInfo.tm_mday = 13;
              stagingTimeInfo.tm_hour = 13;
              stagingTimeInfo.tm_min = 13;
              stagingTimeInfo.tm_sec =0;
            }
            stagingTimeInfo.tm_isdst = -1; // Let mktime handle DST
            selectedTimeField = 0;
            lastSelectedTimeField = -1; // Force redraw
            parentScreen = SCREEN_SETTINGS;  // Set settings as parent
            currentScreen = SCREEN_SET_TIME;
            drawCurrentScreen(true);
            return; // Return to avoid re-drawing settings
        } else if (selectedSettingItem == 4) {   
            tft.fillScreen(COL_BG);
            tft.drawRGBBitmap((160-128)/2, 0, qrcode, 128, 128);
            
            // Add small "B:BACK" instruction in the corner without covering QR
            tft.setFont();
            tft.setTextSize(1);
            tft.setTextColor(COL_WARNING, COL_BG);
            // Position at top-left corner, outside QR code area
            tft.setCursor(0, 0);
            tft.print("B:BACK");
            
            // Wait for B button press instead of fixed delay
            while (true) {
                bool bPressed = checkBtnPress(btnB, BTN_ID_B);
                if (bPressed) {
                    break;
                }
                delay(100);
            }
            
            tft.fillScreen(COL_BG);
            tft.fillRect(0, 0, DISPLAY_WIDTH, STATUS_H, COL_STATUS_BG);
            drawCurrentScreen(true);
        } else if (selectedSettingItem == 5) {
            snakeStart = true;
            return;
        }
        else if (selectedSettingItem == 6) {
            // Show splash screen
            tft.fillScreen(COL_BG);
            tft.drawRGBBitmap(0, 7, splash2, DISPLAY_WIDTH, 110);
            delay(1000);
            
            // Overlay random BRACE messages on top of the image in neon purple
            tft.setFont(&FreeSansBold9pt7b);
            tft.setTextSize(1);
            tft.setTextColor(COL_NEON_PURPLE);
            
            // Display multiple "BRACE" messages at random positions for cyberpunk effect
            for (int i = 0; i < 8; i++) {
                int x = esp_random() % (DISPLAY_WIDTH - 60);  // Leave room for text
                int y = 15 + (esp_random() % (DISPLAY_HEIGHT - 30));  // Random Y position
                tft.setCursor(x, y);
                tft.print("BRACE");
            }
            
            tft.setFont();
            delay(2000);
            
            powerOff();
            
            // If powerOff() returns (cable connected), return to settings
            tft.fillScreen(COL_BG);
            tft.fillRect(0, 0, DISPLAY_WIDTH, STATUS_H, COL_STATUS_BG);
            drawCurrentScreen(true);
        }
    }

    if (selectionChanged) {
      if (stateChanged) {
        drawSettingsMenuLine(selectedSettingItem, true);
      } else {
        drawSettingsMenuLine(lastSelectedSettingItem, false);
        drawSettingsMenuLine(selectedSettingItem, true);
      }
    }
  }
  else if (currentScreen == SCREEN_LOGS) {
    bool selectionChanged = false;
    const uint8_t LOGS_MENU_COUNT = 2;

    if (upPressed) {
      lastSelectedLogItem = selectedLogItem;
      selectedLogItem = (selectedLogItem == 0) ? (LOGS_MENU_COUNT - 1) : (selectedLogItem - 1);
      selectionChanged = true;
    }
    if (downPressed) {
      lastSelectedLogItem = selectedLogItem;
      selectedLogItem = (selectedLogItem == (LOGS_MENU_COUNT - 1)) ? 0 : (selectedLogItem + 1);
      selectionChanged = true;
    }
    
    if (aPressed) {
      // Fetch top lists and display
      snprintf(statusMessageLine1, 40, "FETCHING DATA...");
      snprintf(statusMessageLine2, 40, "Please wait...");
      snprintf(statusMessageLine3, 40, "");
      screenToReturnTo = SCREEN_LOGS;
      currentScreen = SCREEN_ATTACK_STATUS;
      drawCurrentScreen(true);
      
      bool success = fetchTopLists();
      
      if (success) {
        // Display the selected list
        bool isAttackers = (selectedLogItem == 0);
        
        // Draw the top list screen directly
        tft.fillScreen(COL_BG);
        tft.fillRect(0, 0, DISPLAY_WIDTH, STATUS_H, COL_STATUS_BG);
        tft.drawFastHLine(0, STATUS_H, DISPLAY_WIDTH, COL_SEPARATOR);
        tft.fillRect(0, DISPLAY_HEIGHT - BOTTOM_STATUS_H, DISPLAY_WIDTH, BOTTOM_STATUS_H, COL_STATUS_BG);
        tft.drawFastHLine(0, DISPLAY_HEIGHT - BOTTOM_STATUS_H - 1, DISPLAY_WIDTH, COL_SEPARATOR);
        drawStatusBar();
        drawBottomStatusBar();
        drawTopListScreen(isAttackers, topListNicknames, topListCounts);
        
        // Wait for B button to go back
        while (true) {
          bool bPressedNow = (digitalRead(BUTTON_B) == LOW);
          if (bPressedNow && !btnB.prev) {
            btnB.prev = true;
            delay(DEBOUNCE_DELAY);
            break;
          } else if (!bPressedNow) {
            btnB.prev = false;
          }
          delay(50);
        }
        
        // Return to logs menu
        currentScreen = SCREEN_LOGS;
        drawCurrentScreen(true);
      } else {
        snprintf(statusMessageLine1, 40, "FETCH FAILED!");
        snprintf(statusMessageLine2, 40, "Check WiFi connection.");
        snprintf(statusMessageLine3, 40, "");
        statusMessageStartTime = millis();
        showStatusMessage = true;
      }
    }
    
    if (selectionChanged) {
      drawLogsMenuLine(lastSelectedLogItem, false);
      drawLogsMenuLine(selectedLogItem, true);
    }
  }
  else if (currentScreen == SCREEN_SET_TIME) {
    bool timeChanged = false;
    if (ltPressed) {
      selectedTimeField = (selectedTimeField == 0) ? 4 : (selectedTimeField - 1);
      drawSetTimeScreen(false);
    }
    if (rtPressed) {
      selectedTimeField = (selectedTimeField + 1) % 5;
      drawSetTimeScreen(false);
    }

    if (upPressed) {
      switch(selectedTimeField) {
        case 0: stagingTimeInfo.tm_mday++; break;
        case 1: stagingTimeInfo.tm_mon++; break;
        case 2: stagingTimeInfo.tm_year++; break;
        case 3: stagingTimeInfo.tm_hour++; break;
        case 4: stagingTimeInfo.tm_min++; break;
        stagingTimeInfo.tm_sec =0;
      }
      timeChanged = true;
    }
    if (downPressed) {
      switch(selectedTimeField) {
        case 0: stagingTimeInfo.tm_mday--; break;
        case 1: stagingTimeInfo.tm_mon--; break;
        case 2: stagingTimeInfo.tm_year--; break;
        case 3: stagingTimeInfo.tm_hour--; break;
        case 4: stagingTimeInfo.tm_min--; break;
        stagingTimeInfo.tm_sec =0;
      }
      timeChanged = true;
    }

    if (timeChanged) {
      stagingTimeInfo.tm_sec = 0;
      mktime(&stagingTimeInfo); // Normalize the date/time (e.g., handles month/day rollovers)
      drawSetTimeScreen(true); // Force redraw to show normalized date
    }

    if (aPressed) {
      stagingTimeInfo.tm_isdst = -1; // Let mktime handle DST
      stagingTimeInfo.tm_sec = 0;
      time_t newTimestamp = mktime(&stagingTimeInfo);
      
      long newOffset = (long)newTimestamp - (long)(millis() / 1000);
      timeOffset = newOffset;
      timeIsSynced = true;
      saveTimeOffset(timeOffset);
      
      snprintf(statusMessageLine1, 40, "TIME SET!");
      snprintf(statusMessageLine2, 40, "");
      snprintf(statusMessageLine3, 40, "");
      screenToReturnTo = SCREEN_SETTINGS;
      showStatusMessage = true;
    }
  }
  else if (currentScreen == SCREEN_EXPLOITS) {
    bool selectionChanged = false;
    bool stateChanged = false;
    
    // Navigation and toggling work the same in both modes
    if (upPressed) {
      lastSelectedExploit = selectedExploit;
      selectedExploit = (selectedExploit == 0) ? (CYBER_PAIR_COUNT - 1) : (selectedExploit - 1);
      selectionChanged = true;
    }
    if (downPressed) {
      lastSelectedExploit = selectedExploit;
      selectedExploit = (selectedExploit + 1) % CYBER_PAIR_COUNT;
      selectionChanged = true;
    }

    if (ltPressed || rtPressed) {
      // Toggle the selected exploit on/off (LEFT/RIGHT buttons)
      if (active_exploits[selectedExploit]) {
        active_exploits[selectedExploit] = false;
        selectionChanged = true;
        stateChanged = true;
        exploitsChanged = true;  // Track that exploits were modified
      } else {
        if (countActiveExploits() < MAX_ACTIVE_EXPLOITS) {
          active_exploits[selectedExploit] = true;
          selectionChanged = true;
          stateChanged = true;
          exploitsChanged = true;  // Track that exploits were modified
        }
      }
    }

    // A button behavior depends on mode
    if (aPressed) {
      if (exploitScreenInAttackMode) {
        // Attack mode: A launches the attack
        exploitScreenInAttackMode = false;  // Reset flag
        // Auto-save exploits before launching
        if (exploitsChanged) {
          saveCyberSettings(true);
          exploitsChanged = false;
        }
        launchExploit();
        return;
      } else {
        // Configuration mode: A saves and goes back
        if (exploitsChanged) {
          saveCyberSettings(true);
          snprintf(statusMessageLine1, 40, "EXPLOITS");
          snprintf(statusMessageLine2, 40, "Configuration saved.");
          snprintf(statusMessageLine3, 40, "%u ACTIVE", countActiveExploits());
          screenToReturnTo = SCREEN_MAIN_MENU;
          showStatusMessage = true;
          exploitsChanged = false;  // Reset flag
          selectedExploit = 0;
          lastSelectedExploit = 0;
          return;  // Don't navigate yet, status screen will handle it
        }
        // If nothing changed, just go back
        selectedExploit = 0;
        lastSelectedExploit = 0;
        currentScreen = SCREEN_MAIN_MENU;
        parentScreen = SCREEN_MAIN_MENU;
        drawCurrentScreen(true);
        return;
      }
    }
    
    if (selectionChanged) {
      if (stateChanged) {
        drawExploitMenuLine(selectedExploit, true);
      } else {
        drawExploitMenuLine(lastSelectedExploit, false);
        drawExploitMenuLine(selectedExploit, true);
      }
    }
  }
  else if (currentScreen == SCREEN_DEFENSES) {
    bool selectionChanged = false;
    bool stateChanged = false;

    if (upPressed) {
      lastSelectedDefense = selectedDefense;
      selectedDefense = (selectedDefense == 0) ? (CYBER_PAIR_COUNT - 1) : (selectedDefense - 1);
      selectionChanged = true;
    }
    if (downPressed) {
      lastSelectedDefense = selectedDefense;
      selectedDefense = (selectedDefense + 1) % CYBER_PAIR_COUNT;
      selectionChanged = true;
    }

    if (ltPressed || rtPressed) {
      // Toggle the selected defense on/off (LEFT/RIGHT buttons)
      if (active_defenses[selectedDefense]) {
        active_defenses[selectedDefense] = false;
        selectionChanged = true;
        stateChanged = true;
        defensesChanged = true;  // Track that defenses were modified
      } else {
        if (countActiveDefenses() < MAX_ACTIVE_DEFENSES) {
          active_defenses[selectedDefense] = true;
          selectionChanged = true;
          stateChanged = true;
          defensesChanged = true;  // Track that defenses were modified
        }
      }
    }

    if (aPressed) {
        uint8_t defenseCount = countActiveDefenses();

      // Check if the count is NOT equal to 5
      if (defenseCount != MAX_ACTIVE_DEFENSES) {
          // If not 5, show an error message and return to the defense screen
          snprintf(statusMessageLine1, 40, "SAVE FAILED!");
          snprintf(statusMessageLine2, 40, "Must select %d", MAX_ACTIVE_DEFENSES);
          snprintf(statusMessageLine3, 40, "You have %d selected", defenseCount);
          screenToReturnTo = SCREEN_DEFENSES; // Return to this screen
          showStatusMessage = true;
          return; // Stop here, do not save or exit
      }
      // A button saves and goes back
      if (defensesChanged) {
        saveCyberSettings(false);
        snprintf(statusMessageLine1, 40, "DEFENSES");
        snprintf(statusMessageLine2, 40, "Syncing...");
        snprintf(statusMessageLine3, 40, "%u ACTIVE", countActiveDefenses());
        screenToReturnTo = SCREEN_MAIN_MENU;
        showStatusMessage = true;
        pendingDefensePost = true;
        defensesChanged = false;  // Reset flag
        selectedDefense = 0;
        lastSelectedDefense = 0;
        return;  // Don't navigate yet, status screen will handle it
      }
      // If nothing changed, just go back
      selectedDefense = 0;
      lastSelectedDefense = 0;
      currentScreen = SCREEN_MAIN_MENU;
      parentScreen = SCREEN_MAIN_MENU;
      drawCurrentScreen(true);
      return;
    }
    
    if (selectionChanged) {
      if (stateChanged) {
        drawDefenseMenuLine(selectedDefense, true);
      } else {
        drawDefenseMenuLine(lastSelectedDefense, false);
        drawDefenseMenuLine(selectedDefense, true);
      }
    }
  }
}

// ==== Stress Test Function ====
void runStressTest() {
    if (!stressTestEnabled || currentScreen != SCREEN_SCAN) {
        return;
    }
    
    static unsigned long lastStressBadgeTime = 0;
    unsigned long now = millis();
    
    if (now - lastStressBadgeTime > 100) {
        lastStressBadgeTime = now;
        
        uint16_t badge_id = esp_random() % MAX_BADGE_TRACKING;
        
        int8_t rssi = random(RSSI_MIN_CAP, RSSI_MAX_CAP + 1);
        
        uint8_t mac[6] = {0xDE, 0xAD, 0xBE, (uint8_t)(esp_random() % 256), (uint8_t)((badge_id >> 8) & 0xFF), (uint8_t)(badge_id & 0xFF)};
        
        badge_intensity[badge_id] = map(rssi, RSSI_MIN_CAP, RSSI_MAX_CAP, 0, 255);
        all_peers[badge_id].rssi = rssi;
        all_peers[badge_id].last_seen = now;
        memcpy(all_peers[badge_id].mac_addr, mac, 6);
        snprintf(all_peers[badge_id].nickname, 17, "STRESS_%03u", badge_id);
    }
}

// ==== Packet Handler Functions (run in main loop) ====

/**
 * @brief Checks the key buffer for the Konami code.
 * UP, UP, DOWN, DOWN, LEFT, RIGHT, LEFT, RIGHT, B, A
 * If found, triggers the hackerman screen and returns true.
 * @return true if Konami code was triggered, false otherwise.
 */
bool checkAndTriggerKonami() {
    const uint8_t konamiCode[KEY_BUFFER_SIZE] = {
        BTN_ID_UP, BTN_ID_UP, BTN_ID_DOWN, BTN_ID_DOWN, 
        BTN_ID_LEFT, BTN_ID_RIGHT, BTN_ID_LEFT, BTN_ID_RIGHT, 
        BTN_ID_B, BTN_ID_A
    };

    bool match = true;
    for (int i = 0; i < KEY_BUFFER_SIZE; i++) {
        // We check from the oldest entry (keyPressBufferIndex) forward
        if (keyPressBuffer[(keyPressBufferIndex + i) % KEY_BUFFER_SIZE] != konamiCode[i]) {
            match = false;
            break;
        }
    }

    if (match) {
        //Serial.println("KONAMI CODE DETECTED!");
        hackermanScreenStartTime = millis();
        currentScreen = SCREEN_HACKERMAN;
        
        // Clear buffer to prevent re-triggering
        memset(keyPressBuffer, BTN_ID_NONE, sizeof(keyPressBuffer));
        keyPressBufferIndex = 0;
        
        drawCurrentScreen(true); // Force redraw to the hackerman screen
        return true; // Signal that we triggered
    }
    
    const uint8_t konamiCode2[KEY_BUFFER_SIZE] = {
        BTN_ID_LEFT, BTN_ID_UP, BTN_ID_RIGHT, BTN_ID_DOWN,
        BTN_ID_LEFT, BTN_ID_UP, BTN_ID_RIGHT, BTN_ID_DOWN,
        BTN_ID_B, BTN_ID_A
    };

    match = true;
    for (int i = 0; i < KEY_BUFFER_SIZE; i++) {
        // We check from the oldest entry (keyPressBufferIndex) forward
        if (keyPressBuffer[(keyPressBufferIndex + i) % KEY_BUFFER_SIZE] != konamiCode2[i]) {
            match = false;
            break;
        }
    }
    if (match) {
        //Serial.println("KONAMI CODE2 DETECTED!");
        currentScreen = SCREEN_2038;
        memset(keyPressBuffer, BTN_ID_NONE, sizeof(keyPressBuffer));
        keyPressBufferIndex = 0;
        drawCurrentScreen(true); // Force redraw to the hackerman screen
        return true; // Signal that we triggered
    }
    return false; // No match
}


void handleExploitPacket() {
    uint8_t successful_hits = 0;
    uint8_t exploit_count = 0;
    char exploit_list[12] = "";
    
    for (int i = 0; i < 3; i++) {
        uint8_t exploit_index = lastExploitData[i];
        if (exploit_index < CYBER_PAIR_COUNT) {
            exploit_count++;
            char num_str[4];
            sprintf(num_str, "%u ", exploit_index);
            strncat(exploit_list, num_str, sizeof(exploit_list) - strlen(exploit_list) - 1);

            if (active_defenses[exploit_index] == false) {
                successful_hits++;
            }
        }
    }
/*
    uint8_t points_gained = exploit_count - successful_hits;
    currentScore += points_gained;
    if (points_gained > 0) saveScore();
*/
    if (showAllExploits) {
        
        if (successful_hits > 0) { 
            if (!backlightOn) {
                digitalWrite(PIN_DISPLAYLED, HIGH);
                backlightOn = true;
                wokeScreenForExploit = true; // We woke the screen for this exploit
            }
            lastButtonPressTime = millis(); 
        }
    
        isIncomingExploit = true; 
        snprintf(statusMessageLine1, 40, "");
        snprintf(statusMessageLine2, 40, "");
        snprintf(statusMessageLine3, 40, "");
        
        memcpy((void*)lastExploitList, exploit_list, sizeof(lastExploitList));
        lastExploitList[11] = '\0';
        
        if (currentScreen != SCREEN_ATTACK_STATUS) {
          screenToReturnTo = currentScreen;
        }
        showStatusMessage = true;
    } else {
        // STEALTH NOTIFICATION (BOTTOM BAR)
        if (successful_hits > 0) {
            snprintf(bottomBarMessage, 40, "PWN3D BY: %s", lastAttackerNickname);
        } else {
            snprintf(bottomBarMessage, 40, "Blocked: %s", lastAttackerNickname);
        }
        bottomBarMessageTime = millis();
    }


    uint8_t replyPayload[5];
    replyPayload[0] = 'R';
    replyPayload[1] = 'S';
    replyPayload[2] = 'L';
    replyPayload[3] = successful_hits;
    replyPayload[4] = 0;

    if (!esp_now_is_peer_exist(lastAttackerMac)) {
        memset(&peerInfo, 0, sizeof(peerInfo));
        memcpy(peerInfo.peer_addr, lastAttackerMac, 6);
        peerInfo.channel = 0; 
        peerInfo.encrypt = false;
        if (esp_now_add_peer(&peerInfo) != ESP_OK) {
            //Serial.println("Failed to add attacker peer for reply.");
            return;
        }
    }

    esp_now_send(lastAttackerMac, replyPayload, sizeof(replyPayload));

    if (memcmp(lastAttackerMac, BROADCAST_MAC, 6) != 0) {
        esp_now_del_peer(lastAttackerMac);
    }
}

void handleExploitResultPacket() {
    for (uint16_t i = 0; i < MAX_BADGE_TRACKING; i++) {
        if (memcmp(all_peers[i].mac_addr, lastExploitHitsMac, 6) == 0) {
            if (last_hits[i] != lastExploitHits)
            {
                last_hits[i] = lastExploitHits;
                saveHits();
            }
            //Serial.print(all_peers[i].nickname);
            //Serial.print(" is at ");
            //Serial.println(i);
            printHex("Exploit result from: ", lastExploitHitsMac, 6);
            break; 
        }
    }
    /*
    snprintf(statusMessageLine1, 40, "RT-FEEDBACK:");
    snprintf(statusMessageLine2, 40, "%u SUCCESSFUL HITS!", lastExploitHits);
    snprintf(statusMessageLine3, 40, "Server will confirm score.");
    if (currentScreen != SCREEN_ATTACK_STATUS) {
      screenToReturnTo = currentScreen;
    }
    isIncomingResult = true;
    showStatusMessage = true;
    */
}


// ==== Setup / Loop ====
void setup(){
  Serial.begin(115200);
  //Serial.println("\n--- BADGE OS BOOT ---");
  
  esp_pm_config_t pmc = {
    .max_freq_mhz = 80,
    .min_freq_mhz = 40,
    .light_sleep_enable = true
  };
  esp_pm_configure(&pmc);
  
  btStop(); 
  esp_bt_controller_disable();
  //Serial.println("POWER: BT/Zigbee radios disabled.");
  
  pinMode(PIN_KEEPALIVE,OUTPUT);
  pinMode(PIN_DISPLAYLED, OUTPUT);
  digitalWrite(PIN_DISPLAYLED, HIGH);
  lastButtonPressTime = millis();
  backlightOn = true;

  for(auto p:{BUTTON_A,BUTTON_B,BUTTON_UP,BUTTON_DOWN,BUTTON_LEFT,BUTTON_RIGHT})
    pinMode(p,INPUT_PULLUP);
    
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tft.fillScreen(COL_BG);
  tft.setTextWrap(false);
  
  xTaskCreatePinnedToCore(TaskKeepAlive,"KA",2048,NULL,1,NULL,0);
  (void)esp_random();
  strstrip(ijjijjiijijijijiij, 42, true);
  strstrip(ijijjiijiiijjiijijjji, 69, true); 

  memset(badge_intensity, 0, sizeof(badge_intensity));
  memset(badge_framebuffer, 0, sizeof(badge_framebuffer));
  memset(keyPressBuffer, BTN_ID_NONE, sizeof(keyPressBuffer));
  loadAllSettings();

  WiFi.mode(WIFI_STA);
  initESPNOW(false);

  tft.fillRect(0, 0, DISPLAY_WIDTH, STATUS_H, COL_STATUS_BG);
  tft.drawFastHLine(0, STATUS_H, DISPLAY_WIDTH, COL_SEPARATOR);
  tft.fillRect(0, DISPLAY_HEIGHT - BOTTOM_STATUS_H, DISPLAY_WIDTH, BOTTOM_STATUS_H, COL_STATUS_BG);
  tft.drawFastHLine(0, DISPLAY_HEIGHT - BOTTOM_STATUS_H - 1, DISPLAY_WIDTH, COL_SEPARATOR);

  if (digitalRead(BUTTON_UP) == LOW && digitalRead(BUTTON_RIGHT) == LOW) {
    currentScreen = SCREEN_DEBUG_MENU;
    //Serial.println("DEBUG MODE ENABLED (UP+RIGHT)");
  }
  
  strstrip(ijijjiijjiijiijjjijijijj, 31337, true);
  tft.drawRGBBitmap(0, 7, splash2, DISPLAY_WIDTH, 110);
  
  #ifdef MASTERBADGE
    tft.setTextColor(COL_ERROR);
    tft.setFont(&FreeSansBold9pt7b);
    tft.setCursor(28, 65);
    tft.print("! MASTER !");
    tft.setCursor(28, 85);
    tft.print("!  BADGE  !");
    tft.setFont();
  #endif
  strstrip(ijjjjiiijijiiijjijjji, 666, true);
  if (badgeIdentifier == 0 && currentScreen != SCREEN_DEBUG_MENU) {
    //Serial.println("BOOT: No badge ID found. Attempting registration...");
    snprintf(bootMessage, 40, "Registering badge...");
    drawBottomStatusBar();
    postRegister();
    bootMessage[0] = '\0';
    //Serial.println("BOOT: Registration attempt finished.");
  }
  else if (badgeIdentifier != 0 && currentScreen != SCREEN_DEBUG_MENU) {
    //Serial.println("BOOT: Badge is registered. Attempting sync...");
    snprintf(bootMessage, 40, "Syncing badge...");
    drawBottomStatusBar();
    postSync();
    bootMessage[0] = '\0';
    //Serial.println("BOOT: Sync attempt finished.");
  }

  lastSilentSyncTime = millis();
  tft.fillRect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, COL_STATUS_BG);
  drawBottomStatusBar();
  drawCurrentScreen(true);
  
}



void loop(){
  if (snakeStart) {
    setup1();
    snakeStart = false;
    snakeRun = true;
    showAllExploits = false;
  }

  if (snakeExit) {
    snakeExit = false;
    snakeRun = false;
    currentScreen = SCREEN_SETTINGS;
    tft.fillRect(0, 0, DISPLAY_WIDTH, STATUS_H, COL_STATUS_BG);
    tft.drawFastHLine(0, STATUS_H, DISPLAY_WIDTH, COL_SEPARATOR);
    drawCurrentScreen(true);  // ← Forces full screen redraw
  }
  
  if (!stressTestEnabled && 
      currentScreen != SCREEN_DEBUG_MENU && 
      currentScreen != SCREEN_REBOOT &&
      currentScreen != SCREEN_SHOW_DATA &&
      (currentScreen != SCREEN_OTA_PROCESS || otaState == OTA_IDLE || otaState == OTA_FAILED)) {
      if (millis() - lastBeaconTime > currentBeaconInterval) {
          sendBeacon();
          currentBeaconInterval = 5000 + esp_random() % 5000;
      }
  }
  
  if (newExploitPacket) {
    newExploitPacket = false;
    handleExploitPacket();
  }
  
  if (snakeRun) {
    loop1();
    return;
  }

  if (newExploitResultPacket) {
    newExploitResultPacket = false;
    handleExploitResultPacket();
  }

  if (newFwUpdatePacket) {
    if (currentScreen != SCREEN_DEBUG_MENU && currentScreen != SCREEN_OTA_PROCESS && currentScreen != SCREEN_SHOW_DATA) {
        newFwUpdatePacket = false;
        //Serial.println("OTA: Received remote FW update command. Starting...");
        currentScreen = SCREEN_OTA_PROCESS;
        esp_now_deinit();
        uint32_t randomWait = 1000 + (esp_random() % 9000);
        delay(randomWait);
        otaState = OTA_CONNECTING_WIFI;
        otaStartTime = millis();
        if (!backlightOn) {
            digitalWrite(PIN_DISPLAYLED, HIGH);
            backlightOn = true;
        }
        lastButtonPressTime = millis();
        drawCurrentScreen(true);
    } else {
        newFwUpdatePacket = false;
    }
  }

  // Handle remote Factory Reset packet
  if (newFactoryResetPacket) {
      newFactoryResetPacket = false; // Clear flag
      //Serial.println("RESET: Received remote factory reset command.");
      
      tft.fillScreen(COL_BG);
      tft.setFont();
      tft.setTextColor(COL_ERROR);
      tft.setFont(&FreeSansBold9pt7b);
      tft.setCursor(10, 60);
      tft.print("REMOTE RESET");
      tft.setCursor(20, 80);
      tft.print("REBOOTING...");
      tft.setFont();
      
      preferences.begin("badge-os", false);
      preferences.clear();
      preferences.end();
      
      //Serial.println("NVS CLEARED. REBOOTING.");
      delay(3000); // Show message
      ESP.restart();
  }

  upPressed = checkBtnPress(btnUp, BTN_ID_UP);
  downPressed = checkBtnPress(btnDn, BTN_ID_DOWN);
  ltPressed = checkBtnPress(btnLt, BTN_ID_LEFT);
  rtPressed = checkBtnPress(btnRt, BTN_ID_RIGHT);
  aPressed = checkBtnPress(btnA, BTN_ID_A);
  bPressed = checkBtnPress(btnB, BTN_ID_B);
  
  // Key repeat handling for held buttons (UP and DOWN only)
  unsigned long now = millis();
  
  // Track when UP/DOWN buttons are pressed down
  if (upPressed) upHoldStartTime = now;
  if (downPressed) downHoldStartTime = now;
  
  // Check if UP/DOWN buttons are still being held (not just edge detection)
  bool upHeld = (digitalRead(btnUp.pin) == LOW);
  bool downHeld = (digitalRead(btnDn.pin) == LOW);
  
  // Reset hold times when buttons are released
  if (!upHeld) {
    upHoldStartTime = 0;
    lastUpRepeatTime = 0;
  }
  if (!downHeld) {
    downHoldStartTime = 0;
    lastDownRepeatTime = 0;
  }
  
  // Generate repeat events for held UP/DOWN buttons
  if (upHeld && upHoldStartTime > 0) {
    unsigned long holdDuration = now - upHoldStartTime;
    if (holdDuration > KEY_REPEAT_INITIAL_DELAY) {
      if (lastUpRepeatTime == 0 || (now - lastUpRepeatTime) > KEY_REPEAT_RATE) {
        upPressed = true;
        lastUpRepeatTime = now;
      }
    }
  }
  
  if (downHeld && downHoldStartTime > 0) {
    unsigned long holdDuration = now - downHoldStartTime;
    if (holdDuration > KEY_REPEAT_INITIAL_DELAY) {
      if (lastDownRepeatTime == 0 || (now - lastDownRepeatTime) > KEY_REPEAT_RATE) {
        downPressed = true;
        lastDownRepeatTime = now;
      }
    }
  }
  
  handleMenuNavigation();
  
  if (currentScreen == SCREEN_OTA_PROCESS) {
    handleOtaCheck();
  }
  
  if (pendingExploitPost) { 
      pendingExploitPost = false;
      uint32_t targetId = pendingTargetId;
      pendingTargetId = 0;
      postExploit(targetId);
      return;
  }
  


  if (currentScreen == SCREEN_HACKERMAN && millis() - hackermanScreenStartTime > HACKERMAN_SCREEN_DURATION_MS) {
      tft.fillRect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, COL_BG);
      currentScreen = SCREEN_MAIN_MENU;
      drawCurrentScreen(true);
  }

  if (currentScreen == SCREEN_ATTACK_STATUS && millis() - statusMessageStartTime > STATUS_MESSAGE_DURATION_MS) {
      
      isIncomingExploit = false;
      isIncomingResult = false;
      
      if (wokeScreenForExploit) {
          digitalWrite(PIN_DISPLAYLED, LOW);
          backlightOn = false;
          wokeScreenForExploit = false;
      }
      
      {
          if (screenToReturnTo == SCREEN_EXPLOITS) { 
              selection_active = false;
              currentScreen = SCREEN_MAIN_MENU;
              screenToReturnTo = SCREEN_SCAN;
          } else {
              currentScreen = screenToReturnTo;
              screenToReturnTo = SCREEN_SCAN;
          }
          
          if (pendingExploitSync || pendingExploitPost) {
              pendingExploitSync = false; 
              pendingExploitPost = false;
          }
          if (pendingDefenseSync || pendingDefensePost) { 
              pendingDefenseSync = false;
              pendingDefensePost = false;
          }       
          lastAttackerNickDrawn[0] = '\0';
          lastExploitListDrawn[0] = '\0'; 
          drawCurrentScreen(true);
      }
  }

  if (showStatusMessage) {
      showStatusMessage = false;
      statusMessageStartTime = millis();
      currentScreen = SCREEN_ATTACK_STATUS;
      drawCurrentScreen(true);
      return;
  }

  if (pendingDefensePost) { 
      pendingDefensePost = false;
      postSetDefenseConfig(); 
  }
  

  
  static uint32_t lastTimeUpdate = 0;
  now = millis(); // Update the time (now was declared earlier for key repeat)

  if (now - lastTimeUpdate > 1000) { 
    lastTimeUpdate = now;
    updateCurrentTimeStr();
    if (currentScreen != SCREEN_HACKERMAN) drawStatusBar();
  }
  if (currentScreen != SCREEN_HACKERMAN) drawBottomStatusBar();

  drawCurrentScreen(false);

  if (currentScreen == SCREEN_SCAN) {
    static unsigned long lastHeaderChange = 0;
    if (now - lastHeaderChange > HEADER_CHANGE_INTERVAL_MS) {
      lastHeaderChange = now;
      currentScanHeader = (currentScanHeader + 1) % SCAN_HEADER_COUNT;
      
      const uint8_t HEADER_Y_START = STATUS_H + 1;
      tft.fillRect(0, HEADER_Y_START, DISPLAY_WIDTH, GRID_HEADER_H, COL_BG); 
      tft.setTextColor(COL_WARNING, COL_BG);
      tft.setTextSize(1);
      tft.setFont();
      tft.setCursor(PADDING, HEADER_Y_START + 4);
      tft.print(SCAN_HEADERS[currentScanHeader]);
      tft.drawFastHLine(0, HEADER_Y_START + 12, DISPLAY_WIDTH, COL_GRID);
    }
  }
  
  runStressTest();

  if (badgeIdentifier != 0 && (now - lastSilentSyncTime > currentSilentSyncInterval)) {
      if (currentScreen != SCREEN_OTA_PROCESS && 
          currentScreen != SCREEN_ATTACK_STATUS && 
          !pendingExploitPost && !pendingDefensePost) 
      {
          //Serial.println("SILENT SYNC: Running background sync...");
          postSync();
          lastSilentSyncTime = now;
          currentSilentSyncInterval = (240 + esp_random() % 61) * 1000; 
          //Serial.printf("SILENT SYNC: Next sync in %lu ms\n", currentSilentSyncInterval);
      } else {
          lastSilentSyncTime = now - (currentSilentSyncInterval - 30000); 
          //Serial.println("SILENT SYNC: Busy, retrying in 30s...");
      }
  }

  if (backlightOn && (now - lastButtonPressTime > BACKLIGHT_TIMEOUT_MS)) {
      //Serial.println("LCD POWER SAVE...");
      digitalWrite(PIN_DISPLAYLED, LOW);
      backlightOn = false;
  }
  
  if (now - lastButtonPressTime > POWER_OFF_TIMEOUT_MS) {
      tft.fillScreen(COL_BG);
      tft.setFont();
      tft.setTextColor(COL_ACCENT, COL_BG);
      tft.setFont(&FreeSansBold9pt7b);
      tft.setCursor(PADDING, STATUS_H + 28);
      tft.print("INACTIVITY");
      tft.setCursor(PADDING, STATUS_H + 50);
      tft.print("POWER OFF");
      tft.setFont();
      delay(1000);
      powerOff();
  }
  
  if (backlightOn) delay(50);
  else delay(100);
}
