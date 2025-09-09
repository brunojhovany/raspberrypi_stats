#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Minimal SSD1306 I2C driver for 128x32 displays (addr 0x3C by default)
class SSD1306 {
public:
    SSD1306(const std::string& i2cDev = "/dev/i2c-1", uint8_t addr = 0x3C);
    ~SSD1306();

    bool init();
    void clear();
    void display();

    // Framebuffer is 128x32 mono, pages of 8 rows => 4 pages * 128 cols
    static constexpr int WIDTH = 128;
    static constexpr int HEIGHT = 32;
    static constexpr int PAGES = HEIGHT / 8;

    // Pixel operations
    void setPixel(int x, int y, bool on);
    void drawHLine(int x, int y, int w, bool on = true);
    void drawText(int x, int y, const std::string& text); // 5x7 font
    // Note: For portrait (32x128) rendering, rotate coordinates in caller
    // mapping (xp,yp) -> (xd,yd) before calling setPixel.

private:
    int fd_ = -1;
    uint8_t addr_ = 0x3C;
    std::vector<uint8_t> buf_; // 128 * 4 bytes

    bool writeCmd(uint8_t c);
    bool writeData(const uint8_t* data, size_t len);
    void initSeq();
};
