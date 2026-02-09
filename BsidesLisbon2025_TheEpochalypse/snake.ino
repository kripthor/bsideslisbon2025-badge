// =====================
//      Snake Game
// =====================

// Grid & rendering
static const uint8_t CELL = 8;                     // 8x8 pixels per cell
static const uint8_t COLS = DISPLAY_WIDTH / CELL;  // 160/8 = 20
static const uint8_t ROWS = DISPLAY_HEIGHT / CELL; // 128/8 = 16

// Colors (very dark grid)
#define RGB565(r,g,b) ( ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b) >> 3) )
#define COL_BG      ST77XX_BLACK
#define COL_GRID    RGB565(16,16,16)   // near-black grid
#define COL_SNAKE   ST77XX_GREEN
#define COL_HEAD    ST77XX_YELLOW
#define COL_FOOD    ST77XX_RED
#define COL_TEXT    ST77XX_WHITE

// ---- Buttons: declare struct BEFORE any function uses it ----
struct BtnS {
  uint8_t pin;
  bool prev;
};

// Snake state
struct Cell { int8_t x; int8_t y; };

static const uint16_t MAX_SNAKE = (uint16_t)COLS * (uint16_t)ROWS;
Cell snake[MAX_SNAKE];
uint16_t snakeLen = 0;
uint16_t headIndex = 0; // circular buffer head (index of current head)
int8_t dx = 1, dy = 0;
int8_t pendingDx = 1, pendingDy = 0;

// Food
Cell food{ -1, -1 };

// Timing
uint32_t lastStepMs = 0;
uint16_t stepIntervalMs = 150; // speed

// Score & game state
uint16_t score = 0;
bool paused = false;
bool gameOver = false;
bool snakeExit = false;
bool showMenu = false;  // New: controls pause/game-over menu display

// ====== COWSAY SPLASH STATE ======
bool splashActive = false;
uint32_t splashStartMs = 0;
static const uint16_t SPLASH_DURATION_MS = 2000;

// After splash or init, force a total repaint of all cells
bool forceFullRender = false;

// Some short, funny cowsay lines
const char* COW_LINES[] = {
  "Moo or never!",
  "Got milk… of bytes?",
  "404: Grass not found",
  "Beef? Only with bugs.",
  "Cowngratulations!",
  "I herd you like points.",
  "Udderly unstoppable!",
  "Mooooo-ve fast!",
  "Don’t have a cow, man.",
  "Holy cow, +50!"
};
static const uint8_t COW_LINES_COUNT = sizeof(COW_LINES)/sizeof(COW_LINES[0]);

// ===== Utilities =====
bool snakeOccupies(int8_t x, int8_t y) {
  uint16_t idx = headIndex;
  for (uint16_t i = 0; i < snakeLen; ++i) {
    const Cell &c = snake[idx];
    if (c.x == x && c.y == y) return true;
    idx = (idx == 0) ? (MAX_SNAKE - 1) : (idx - 1);
  }
  return false;
}

void placeFood() {
  for (uint16_t tries = 0; tries < 2000; ++tries) {
    int8_t x = (int8_t)(esp_random() % COLS);
    int8_t y = (int8_t)(esp_random() % (ROWS-1))+1;
    if (!snakeOccupies(x, y)) { food = {x, y}; return; }
  }
  for (uint8_t y = 0; y < ROWS; ++y)
    for (uint8_t x = 0; x < COLS; ++x)
      if (!snakeOccupies(x, y)) { food = { (int8_t)x, (int8_t)y }; return; }
}

// ===== Drawing (cell-level) =====
void drawCell(int8_t x, int8_t y, uint16_t color) {
  tft.fillRect(x * CELL, y * CELL, CELL, CELL, color);
}

void clearCell(int8_t x, int8_t y) {
  int16_t px = x * CELL;
  int16_t py = y * CELL;
  // repaint cell background
  tft.fillRect(px, py, CELL, CELL, COL_BG);
  // repaint the grid lines crossing this cell
  tft.drawFastVLine(px,              py, CELL, COL_GRID);        // left
  tft.drawFastHLine(px,              py, CELL, COL_GRID);        // top
  tft.drawFastVLine(px + CELL - 1,   py, CELL, COL_GRID);        // right
  tft.drawFastHLine(px,        py + CELL - 1, CELL, COL_GRID);   // bottom
}

void drawBoard() {
  tft.fillScreen(COL_BG);
  for (uint8_t x = 0; x < COLS; ++x)
    tft.drawFastVLine(x * CELL, 0, DISPLAY_HEIGHT, COL_GRID);
  for (uint8_t y = 0; y < ROWS; ++y)
    tft.drawFastHLine(0, y * CELL, DISPLAY_WIDTH, COL_GRID);
}

void drawHUD() {
  tft.setTextColor(COL_TEXT);
  tft.setTextSize(1);
  char buf[24];
  snprintf(buf, sizeof(buf), "Score:%u", score);
  // clear HUD strip and redraw top grid line to avoid gaps
  tft.fillRect(0, 0, 64, 8, COL_BG);
  tft.drawFastHLine(0, 0, DISPLAY_WIDTH, COL_GRID);
  tft.setCursor(2, 2);
  tft.print(buf);
  if (paused && !splashActive) {
    const char* p = "PAUSED";
    int16_t x1, y1; uint16_t w, h;
    tft.getTextBounds(p, 0, 0, &x1, &y1, &w, &h);
    tft.fillRect((DISPLAY_WIDTH - w)/2 - 1, 0, w+2, 8, COL_BG);
    tft.drawFastHLine(0, 0, DISPLAY_WIDTH, COL_GRID);
    tft.setCursor((DISPLAY_WIDTH - w)/2, 2);
    tft.print(p);
  }
}

void drawPauseMenu() {
  tft.fillScreen(COL_BG);
  tft.setTextColor(COL_TEXT);
  int16_t x1,y1; uint16_t w,h;
  
  if (gameOver) {
    // Game Over Menu
    tft.setTextSize(2);
    const char* over = "GAME OVER";
    tft.getTextBounds(over, 0,0, &x1,&y1,&w,&h);
    tft.setCursor((DISPLAY_WIDTH - w)/2, 25);
    tft.print(over);

    tft.setTextSize(1);
    char s[32];
    snprintf(s, sizeof(s), "Score: %u", score);
    tft.getTextBounds(s, 0,0, &x1,&y1,&w,&h);
    tft.setCursor((DISPLAY_WIDTH - w)/2, 50);
    tft.print(s);

    const char* optionA = "[A] New Game";
    tft.getTextBounds(optionA, 0,0, &x1,&y1,&w,&h);
    tft.setCursor((DISPLAY_WIDTH - w)/2, 75);
    tft.print(optionA);
    
    const char* optionB = "[B] Return to Settings";
    tft.getTextBounds(optionB, 0,0, &x1,&y1,&w,&h);
    tft.setCursor((DISPLAY_WIDTH - w)/2, 95);
    tft.print(optionB);
  } else {
    // Pause Menu
    tft.setTextSize(2);
    const char* pauseTitle = "PAUSED";
    tft.getTextBounds(pauseTitle, 0,0, &x1,&y1,&w,&h);
    tft.setCursor((DISPLAY_WIDTH - w)/2, 30);
    tft.print(pauseTitle);

    tft.setTextSize(1);
    char s[32];
    snprintf(s, sizeof(s), "Score: %u", score);
    tft.getTextBounds(s, 0,0, &x1,&y1,&w,&h);
    tft.setCursor((DISPLAY_WIDTH - w)/2, 60);
    tft.print(s);

    const char* optionA = "[A] Resume";
    tft.getTextBounds(optionA, 0,0, &x1,&y1,&w,&h);
    tft.setCursor((DISPLAY_WIDTH - w)/2, 80);
    tft.print(optionA);
    
    const char* optionB = "[B] Return to Settings";
    tft.getTextBounds(optionB, 0,0, &x1,&y1,&w,&h);
    tft.setCursor((DISPLAY_WIDTH - w)/2, 100);
    tft.print(optionB);
  }
}

void drawGameOver() {
  gameOver = true;
  showMenu = true;
  drawPauseMenu();
}

// ===== COWSAY SPLASH =====
void drawCowsayBubble(const char* text) {
  tft.fillScreen(COL_BG);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setTextSize(1);

  int16_t x1,y1; uint16_t w,h;
  tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  uint16_t pad = 6;
  uint16_t bw = w + pad*2;
  if (bw < 80) bw = 80; // minimum width
  uint16_t bh = 16 + pad*2; // two lines tall to look nicer

  int16_t bx = (DISPLAY_WIDTH - bw)/2;
  int16_t by = 10;
  // Bubble rectangle & border
  tft.fillRoundRect(bx, by, bw, bh, 6, ST77XX_BLACK);
  tft.drawRoundRect(bx, by, bw, bh, 6, ST77XX_WHITE);

  // Bubble "tail"
  tft.drawLine(bx + bw/2, by + bh, bx + bw/2 - 6, by + bh + 8, ST77XX_WHITE);
  tft.drawLine(bx + bw/2, by + bh, bx + bw/2 + 6, by + bh + 8, ST77XX_WHITE);

  // Text inside bubble (centered)
  tft.setCursor(bx + (bw - w)/2, by + pad + 3);
  tft.print(text);

  // Tiny cow under the bubble
  int16_t cx = 28;
  int16_t cy = 48;
  const char* cow[] = {
    "    ^__^",
    "    (oo)\\_______",
    "    (__)\\       )\\/\\",
    "        ||----w |",
    "        ||     ||"
  };
  for (uint8_t i = 0; i < 5; ++i) {
    tft.setCursor(cx, cy + i*10);
    tft.print(cow[i]);
  }

  // Footer: prompt
  const char* footer = "[+50 pts] Keep going!";
  tft.getTextBounds(footer, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((DISPLAY_WIDTH - w)/2, DISPLAY_HEIGHT - 16);
  tft.print(footer);
}

void showCowsaySplash() {
  uint8_t idx = esp_random() % COW_LINES_COUNT;
  drawCowsayBubble(COW_LINES[idx]);
  splashActive = true;
  splashStartMs = millis();
  forceFullRender = true;  // after splash, repaint every cell
}

// ===== Full repaint to nuke artifacts =====
void repaintAllCellsFromState() {
  // Pass 1: clear all cells (with grid)
  for (uint8_t y = 0; y < ROWS; ++y)
    for (uint8_t x = 0; x < COLS; ++x)
      clearCell(x, y);

  // Pass 2: draw food
  if (food.x >= 0) drawCell(food.x, food.y, COL_FOOD);

  // Pass 3: draw snake (body then head)
  uint16_t idx = headIndex;
  for (uint16_t i = 0; i < snakeLen; ++i) {
    const Cell &c = snake[idx];
    drawCell(c.x, c.y, (i == 0) ? COL_HEAD : COL_SNAKE);
    idx = (idx == 0) ? (MAX_SNAKE - 1) : (idx - 1);
  }

  // HUD last (over top grid line)
  drawHUD();
}

// ===== Game Logic =====
void resetGame() {
  score = 0;
  paused = false;
  gameOver = false;
  showMenu = false;  // Clear menu state
  dx = 1; dy = 0;
  pendingDx = 1; pendingDy = 0;
  splashActive = false;

  // init snake in center
  int8_t sx = COLS / 2;
  int8_t sy = ROWS / 2;
  snakeLen = 4;

  headIndex = 0;
  snake[headIndex] = {sx, sy};
  snake[(MAX_SNAKE + headIndex - 1) % MAX_SNAKE] = { (int8_t)(sx-1), sy };
  snake[(MAX_SNAKE + headIndex - 2) % MAX_SNAKE] = { (int8_t)(sx-2), sy };
  snake[(MAX_SNAKE + headIndex - 3) % MAX_SNAKE] = { (int8_t)(sx-3), sy };

  placeFood();

  // Paint everything from scratch — eliminates any stray pixels
  drawBoard();
  repaintAllCellsFromState();

  lastStepMs = millis();
  stepIntervalMs = 150;
}

void handleInput() {

  upPressed = checkBtnPress(btnUp, BTN_ID_UP);
  downPressed = checkBtnPress(btnDn, BTN_ID_DOWN);
  ltPressed = checkBtnPress(btnLt, BTN_ID_LEFT);
  rtPressed = checkBtnPress(btnRt, BTN_ID_RIGHT);
  aPressed = checkBtnPress(btnA, BTN_ID_A);
  bPressed = checkBtnPress(btnB, BTN_ID_B);
  
  // During splash, allow B to skip
  if (splashActive) {
    if (bPressed) splashActive = false;
    return;
  }

  // Handle menu input (pause or game over)
  if (showMenu) {
    if (aPressed) {
      if (gameOver) {
        // A pressed on game over menu = new game
        resetGame();
        showMenu = false;
      } else {
        // A pressed on pause menu = resume
        showMenu = false;
        paused = false;
        forceFullRender = true;  // Redraw the game board
      }
    }
    if (bPressed) {
      // B = return to settings (both menus)
      snakeExit = true;
    }
    return;  // Don't process other inputs while menu is shown
  }

  // Normal gameplay input
  if (upPressed)   { pendingDx = 0;  pendingDy = -1; }
  if (downPressed)   { pendingDx = 0;  pendingDy =  1; }
  if (ltPressed)   { pendingDx = -1; pendingDy =  0; }
  if (rtPressed)   { pendingDx = 1;  pendingDy =  0; }

  // A button shows the pause menu
  if (aPressed) {
    showMenu = true;
    paused = true;
    drawPauseMenu();
  }
}

void tryApplyDirection() {
  if ((pendingDx == -dx && pendingDy == -dy)) return; // no 180°
  dx = pendingDx; dy = pendingDy;
}

void gameStep() {
  // old head reference before move
  Cell head = snake[headIndex];
  tryApplyDirection();

  // compute new head position
  int8_t nx = head.x + dx;
  int8_t ny = head.y + dy;

  // Wrap-around
  if (nx < 0) nx = COLS - 1;
  if (nx >= COLS) nx = 0;
  if (ny < 0) ny = ROWS - 1;
  if (ny >= ROWS) ny = 0;

  // Collision with self
  if (snakeOccupies(nx, ny)) {
    gameOver = true;
    drawGameOver();
    return;
  }

  // Advance head (circular buffer)
  headIndex = (headIndex + 1) % MAX_SNAKE;
  snake[headIndex] = {nx, ny};

  bool grew = (nx == food.x && ny == food.y);
  if (grew) {
    uint16_t oldScore = score;
    snakeLen++;               // grow: DO NOT remove tail
    score += 10;

    // Every +50 points, show cowsay splash
    if (((score / 50) > (oldScore / 50)) && (score % 50 == 0)) {
      showCowsaySplash();
    }

    if ((score % 50) == 0 && stepIntervalMs > 70) stepIntervalMs -= 10;
    placeFood();
  } else {
    // move without growth: remove the OLDEST cell (the one that fell off)
    // IMPORTANT: index to clear is (headIndex - snakeLen) mod MAX_SNAKE
    uint16_t removeIndex = (uint16_t)((MAX_SNAKE + headIndex - snakeLen) % MAX_SNAKE);
    Cell oldTail = snake[removeIndex];
    clearCell(oldTail.x, oldTail.y);
    // snakeLen stays the same (we added one head and removed one tail)
  }

  // Draw: previous head becomes body, new head highlighted
  Cell prevHead = snake[(MAX_SNAKE + headIndex - 1) % MAX_SNAKE];
  drawCell(prevHead.x, prevHead.y, COL_SNAKE);
  drawCell(nx, ny, COL_HEAD);

  // Draw food (it can move on eat)
  if (food.x >= 0) drawCell(food.x, food.y, COL_FOOD);

  drawHUD();
}

// ===== Setup & Loop =====
void setup1() {
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1); // landscape
  tft.fillScreen(COL_BG);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setTextSize(1);
  digitalWrite(PIN_DISPLAYLED, HIGH);
  (void)esp_random();
  resetGame();
}

void loop1() {
  handleInput();
  
  // Check for exit request
  if (snakeExit) {
    return;  // This will exit loop1() and return control to main loop
  }
  
  // Handle cowsay splash timing
  if (splashActive) {
    if (millis() - splashStartMs >= SPLASH_DURATION_MS) {
      splashActive = false;
      forceFullRender = true;           // repaint everything when splash ends
    } else {
      delay(5);
      return; // keep splash up, pause game
    }
  }
  if (forceFullRender) {
    // repaint ALL cells from the current game state — this nukes any artifact
    drawBoard();
    repaintAllCellsFromState();
    forceFullRender = false;
  }
  if (!gameOver && !paused) {
    uint32_t now = millis();
    if (now - lastStepMs >= stepIntervalMs) {
      lastStepMs = now;
      gameStep();
    }
  }

  delay(10);
}
