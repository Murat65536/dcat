#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <glm/glm.hpp>

constexpr uint32_t DEFAULT_TERM_WIDTH = 80;
constexpr uint32_t DEFAULT_TERM_HEIGHT = 48;

void renderTerminal(const uint8_t* buffer, uint32_t width, uint32_t height);
void renderSixel(const uint8_t* buffer, uint32_t width, uint32_t height);
void renderKittyShm(const uint8_t* buffer, uint32_t width, uint32_t height);

std::pair<uint32_t, uint32_t> calculateRenderDimensions(
    int explicitWidth, int explicitHeight, bool useSixel, bool useKitty, bool reserveBottomLine = false);
std::pair<uint32_t, uint32_t> getTerminalSize();
std::pair<uint32_t, uint32_t> getTerminalSizePixels();

void drawStatusBar(float fps, float speed, const glm::vec3& pos, const std::string& animationName = "");

void enableRawMode();
void disableRawMode();
void enterAlternateScreen();
void exitAlternateScreen();
void hideCursor();
void showCursor();
void enableFocusTracking();
void disableFocusTracking();
