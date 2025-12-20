#include "DinoGame.h"

#include <Arduino.h>
#include <Adafruit_SSD1306.h>

#define LCD_HEIGHT 64U
#define LCD_WIDTH 128U
#define LCD_BYTE_SZIE (LCD_WIDTH * LCD_HEIGHT / 8)
#define LCD_IF_VIRTUAL_WIDTH(TRUE_COND, FALSE_COND) FALSE_COND
#define LCD_PART_BUFF_WIDTH LCD_WIDTH
#define LCD_PART_BUFF_HEIGHT LCD_HEIGHT
#define LCD_PART_BUFF_SZ ((LCD_PART_BUFF_HEIGHT / 8) * LCD_PART_BUFF_WIDTH)

#include "array.h"
#include "assets.h"
#include "GameConfig.h"
#include "Cactus.h"
#include "engine.h"
#include "Ground.h"
#include "HeartLive.h"
#include "Pterodactyl.h"
#include "TrexPlayer.h"

extern const uint8_t ButEnter;
extern const uint8_t ButDown;

namespace DinoGame {

namespace {

Adafruit_SSD1306* g_display = nullptr;
CollisionCallback g_collisionCallback = nullptr;
TickCallback g_tickCallback = nullptr;
GameOverCallback g_gameOverCallback = nullptr;

class DisplayAdapter {
 public:
  void begin(Adafruit_SSD1306* display) { display_ = display; }

  void setInverse(bool inverse) {
    if (display_) display_->invertDisplay(inverse);
  }

  void setAddressingMode(uint8_t) {}

  void fillScreen(const uint8_t* buffer, uint16_t size, uint8_t stride = 0) {
    (void)size;
    (void)stride;
    if (!display_) return;
    display_->clearDisplay();
    uint8_t* const dest = display_->getBuffer();
    if (dest && buffer) {
      const uint16_t copy =
          min<uint16_t>(LCD_BYTE_SZIE, size ? size : LCD_BYTE_SZIE);
      memcpy(dest, buffer, copy);
      if (copy < LCD_BYTE_SZIE) {
        memset(dest + copy, 0, LCD_BYTE_SZIE - copy);
      }
    }
    display_->display();
  }

 private:
  Adafruit_SSD1306* display_ = nullptr;
};

DisplayAdapter lcd;

bool isPressedJump() {
  return digitalRead(ButEnter) == LOW;
}

bool isPressedDuck() {
  return digitalRead(ButDown) == LOW;
}

uint8_t randByte() {
  return static_cast<uint8_t>(random(0, 256));
}

uint16_t hiScore = 0;
bool firstStart = true;

void renderNumber(BitCanvas& canvas, Point2Di8 point, uint16_t number) {
  uint16_t base = 10000;
  while (base) {
    const uint8_t digit = (number / base) % 10;
    canvas.render(numbers.getSprite(digit, point));
    base /= 10;
    point.x += numbers.getWidth() + 1;
  }
}

void gameLoop() {
  uint8_t lcdBuff[LCD_PART_BUFF_SZ];
  VirtualBitCanvas bitCanvas(
      VirtualBitCanvas::VIRTUAL_HEIGHT, lcdBuff, LCD_PART_BUFF_HEIGHT,
      LCD_PART_BUFF_WIDTH, LCD_HEIGHT);

  SpawnHold spawnHolder;

  TrexPlayer trex;
  Ground ground1(-1);
  Ground ground2(63);
  Ground ground3(127);
  Cactus cactus1(spawnHolder);
  Cactus cactus2(spawnHolder);
  Pterodactyl pterodactyl1(spawnHolder);
  HeartLive heartLive;
  const array<SpriteAnimated*, 8> sprites{{&ground1, &ground2, &ground3,
                                           &cactus1, &cactus2, &pterodactyl1,
                                           &heartLive, &trex}};
  const array<SpriteAnimated*, 3> enemies{
      {&cactus1, &cactus2, &pterodactyl1}};

  const Sprite gameOverSprite(&game_overver_bm, {15, 12});
  const Sprite restartIconSprite(&restart_icon_bm, {55, 25});
  const Sprite hiSprite(&hi_score, {44, 0});
  Sprite heartsSprite(&hearts_5x_bm, {95, 8});

  uint32_t prvT = millis();
  bool gameOver = false;
  uint16_t score = 0;
  uint8_t targetFPS = TARGET_FPS_START;
  uint8_t lives = LIVES_START;
  bool night = false;
  lcd.setInverse(night);

  while (true) {
    while (true) {
      bitCanvas.render(hiSprite);
      renderNumber(bitCanvas, {60, 0}, hiScore);
      renderNumber(bitCanvas, {95, 0}, score);
      bitCanvas.render(heartsSprite);
      for (uint8_t i = 0; i < sprites.size(); ++i) bitCanvas.render(*sprites[i]);
      if (gameOver) {
        bitCanvas.render(gameOverSprite);
        bitCanvas.render(restartIconSprite);
      }
      lcd.fillScreen(lcdBuff, LCD_PART_BUFF_SZ,
                     LCD_IF_VIRTUAL_WIDTH(LCD_PART_BUFF_WIDTH, 0));
      if (bitCanvas.nextPart()) break;
    }

    if (gameOver) {
      if (score > hiScore) hiScore = score;
      return;
    }

    if (!trex.isBlinking() &&
        CollisionDetector::check(trex, enemies.data, enemies.size())) {
      if (g_collisionCallback) {
        g_collisionCallback();
      }
      if (lives) {
        trex.blink();
        --lives;
      } else {
        trex.die();
        gameOver = true;
        continue;
      }
    }
    if (lives < LIVES_MAX && CollisionDetector::check(trex, heartLive)) {
      ++lives;
      heartLive.eat();
    }

    if (isPressedJump()) trex.jump();
    trex.duck(isPressedDuck());

    for (uint8_t i = 0; i < sprites.size(); ++i) sprites[i]->step();
    if (score < 0xFFFE) ++score;
    if (!(score % INCREASE_FPS_EVERY_N_SCORE_POINTS) &&
        targetFPS < TARGET_FPS_MAX)
      ++targetFPS;
    heartsSprite.limitRenderWidthTo = 6 * lives + 1;
    if (!(score % DAY_NIGHT_SWITCH_CYCLES))
      lcd.setInverse(night = !night);

    const uint8_t frameTime = 1000 / targetFPS;
    while (millis() - prvT < frameTime) {
    }
    prvT = millis();
    if (g_tickCallback) {
      g_tickCallback();
    }
  }
}

void splashScreen() {
  if (!g_display) return;
  g_display->clearDisplay();
  g_display->setTextSize(2);
  g_display->setTextColor(SSD1306_WHITE);
  g_display->setCursor(10, 20);
  g_display->print(F("MRI-Lab"));
  g_display->setCursor(10, 40);
  g_display->print(F("Ready"));
  g_display->display();
  for (uint8_t i = 50; i && !isPressedJump(); --i) delay(100);
}

}  // namespace

void Init(Adafruit_SSD1306* display) {
  if (!display) return;
  g_display = display;
  lcd.begin(display);
  splashScreen();
  randomSeed(micros());
}

void Run() {
  if (!g_display) return;
  firstStart = false;
  gameLoop();
  if (g_gameOverCallback) {
    g_gameOverCallback();
  }
  lcd.setInverse(false);
  while (!isPressedJump()) delay(50);
  while (isPressedJump()) delay(50);
}

void SetCollisionCallback(CollisionCallback callback) {
  g_collisionCallback = callback;
}

void SetTickCallback(TickCallback callback) {
  g_tickCallback = callback;
}

void SetGameOverCallback(GameOverCallback callback) {
  g_gameOverCallback = callback;
}

}  // namespace DinoGame
