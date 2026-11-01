/* Horodyne: Unica One is the only font. The font data lives in the sketch
   (font_*.c); this only declares the default for LVGL's built-in widgets. */
#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(font_label)
#undef  LV_FONT_DEFAULT
#define LV_FONT_DEFAULT        &font_label
