#pragma once

class Adafruit_SSD1306;

namespace DinoGame {

using CollisionCallback = void (*)();
using TickCallback = void (*)();
using GameOverCallback = void (*)();

void Init(Adafruit_SSD1306* display);
void Run();
void SetCollisionCallback(CollisionCallback callback);
void SetTickCallback(TickCallback callback);
void SetGameOverCallback(GameOverCallback callback);

}  // namespace DinoGame
