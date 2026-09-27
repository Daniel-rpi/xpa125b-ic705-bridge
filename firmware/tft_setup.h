// Eigenstaendige TFT_eSPI-Konfiguration fuer den TTGO T-Display v1.1 -
// direkt im Sketch definiert (1:1 aus der mitgelieferten
// User_Setups/Setup25_TTGO_T_Display.h uebernommen), damit KEINE
// Bibliotheksdatei angefasst werden muss (reproduzierbar, ueberlebt eine
// Neuinstallation der Bibliothek). Muss VOR jedem #include <TFT_eSPI.h>
// eingebunden werden.
#pragma once
#define USER_SETUP_LOADED 1

#define ST7789_DRIVER
#define TFT_SDA_READ

#define TFT_WIDTH  135
#define TFT_HEIGHT 240

#define CGRAM_OFFSET

#define TFT_MOSI 19
#define TFT_SCLK 18
#define TFT_CS   5
#define TFT_DC   16
#define TFT_RST  23

#define TFT_BL 4
#define TFT_BACKLIGHT_ON HIGH

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF

#define SMOOTH_FONT

#define SPI_FREQUENCY  40000000
#define SPI_READ_FREQUENCY  6000000
