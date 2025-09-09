#include "ssd1306.h"
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>

// 5x7 ASCII font (32..127), adapted from public domain sources
static const uint8_t font5x7[] = {
#include "tiny5x7.inc"
};

SSD1306::SSD1306(const std::string& i2cDev, uint8_t addr) : addr_(addr) {
    buf_.assign(WIDTH * PAGES, 0);
    fd_ = ::open(i2cDev.c_str(), O_RDWR);
    if (fd_ >= 0) {
        if (ioctl(fd_, I2C_SLAVE, addr_) < 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }
}

SSD1306::~SSD1306() {
    if (fd_ >= 0) ::close(fd_);
}

bool SSD1306::init() {
    if (fd_ < 0) return false;
    initSeq();
    clear();
    display();
    return true;
}

void SSD1306::initSeq() {
    // Minimal init for 128x32
    uint8_t cmds[] = {
        0xAE,       // Display off
        0xD5, 0x80, // Set display clock divide ratio/oscillator frequency
        0xA8, 0x1F, // Multiplex ratio (0x1F = 32-1)
        0xD3, 0x00, // Display offset
        0x40,       // Start line 0
        0x8D, 0x14, // Charge pump on
        0x20, 0x00, // Memory addressing mode: horizontal
        0xA1,       // Segment remap
        0xC8,       // COM output scan direction remapped
        0xDA, 0x02, // COM pins hardware configuration for 128x32
        0x81, 0x8F, // Contrast
        0xD9, 0xF1, // Pre-charge period
        0xDB, 0x40, // VCOMH deselect level
        0xA4,       // Entire display ON from RAM
        0xA6,       // Normal display
        0x2E,       // Deactivate scroll
        0xAF        // Display ON
    };
    for (uint8_t c : cmds) writeCmd(c);
}

bool SSD1306::writeCmd(uint8_t c) {
    if (fd_ < 0) return false;
    uint8_t buf[2] = {0x00, c};
    return ::write(fd_, buf, 2) == 2;
}

bool SSD1306::writeData(const uint8_t* data, size_t len) {
    if (fd_ < 0) return false;
    // We need to prefix with 0x40 control byte; send in chunks
    uint8_t packet[17]; // 1 ctrl + 16 data
    packet[0] = 0x40;
    size_t i = 0;
    while (i < len) {
        size_t n = (len - i > 16) ? 16 : (len - i);
        memcpy(packet + 1, data + i, n);
        if (::write(fd_, packet, n + 1) != static_cast<ssize_t>(n + 1)) return false;
        i += n;
    }
    return true;
}

void SSD1306::clear() {
    std::fill(buf_.begin(), buf_.end(), 0);
}

void SSD1306::display() {
    writeCmd(0x21); // set column address
    writeCmd(0);
    writeCmd(WIDTH - 1);

    writeCmd(0x22); // set page address
    writeCmd(0);
    writeCmd(PAGES - 1);

    writeData(buf_.data(), buf_.size());
}

void SSD1306::setPixel(int x, int y, bool on) {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;
    int page = y / 8;
    int bit = y % 8;
    size_t idx = page * WIDTH + x;
    if (on)
        buf_[idx] |= (1u << bit);
    else
        buf_[idx] &= ~(1u << bit);
}

void SSD1306::drawHLine(int x, int y, int w, bool on) {
    for (int i = 0; i < w; ++i) setPixel(x + i, y, on);
}

void SSD1306::drawText(int x, int y, const std::string& text) {
    // Draw glyphs 5x7 with 1px space
    for (char ch : text) {
        uint8_t c = static_cast<uint8_t>(ch);
        if (c < 32 || c > 127) c = '?';
        const uint8_t* glyph = &font5x7[(c - 32) * 5];
        for (int col = 0; col < 5; ++col) {
            uint8_t bits = glyph[col];
            for (int row = 0; row < 7; ++row) {
                bool on = bits & (1u << row);
                setPixel(x + col, y + row, on);
            }
        }
        x += 6; // 5 + 1 spacing
    }
}
