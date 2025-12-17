#pragma once

#include <stddef.h>

class Adafruit_SSD1306;

struct OledMenu {
  const char* title = nullptr;
  const char* const* items = nullptr;
  size_t item_count = 0;
  size_t selected_index = 0;
};

class OledDisplay {
 public:
  using CustomRenderCallback = void (*)(Adafruit_SSD1306& display,
                                        void* user_context);

  void begin();
  void loop();
  void scanBus();
  void forceRender();

  void setMenu(const OledMenu& menu);
  void clearMenu();
  void setCustomRenderer(CustomRenderCallback renderer, void* user_context);
  void clearCustomRenderer();
  bool customRendererActive() const;

 private:
  void render();
  void drawSplash();
  void drawMenu();
  void configureBus();

  CustomRenderCallback custom_renderer_ = nullptr;
  void* custom_renderer_context_ = nullptr;
  bool initialized_ = false;
  bool init_attempted_ = false;
  unsigned long last_render_ms_ = 0;
  OledMenu menu_;
  size_t menu_scroll_offset_ = 0;
};
