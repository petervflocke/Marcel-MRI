#pragma once

#include <stddef.h>

struct OledMenu {
  const char* title = nullptr;
  const char* const* items = nullptr;
  size_t item_count = 0;
  size_t selected_index = 0;
};

class OledDisplay {
 public:
  void begin();
  void loop();
  void scanBus();

  void setMenu(const OledMenu& menu);
  void clearMenu();

 private:
  void render();
  void drawSplash();
  void drawMenu();
  void configureBus();

  bool initialized_ = false;
  bool init_attempted_ = false;
  unsigned long last_render_ms_ = 0;
  OledMenu menu_;
};
