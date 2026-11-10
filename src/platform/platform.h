// SDL2 presentation layer. Owns the window, the software renderer used only
// to blit the finished CPU framebuffer, and input translation.
#pragma once

#include <string>

#include "core/input.h"
#include "render/framebuffer.h"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace rf {

class Platform {
public:
    Platform() = default;
    ~Platform();

    bool init(int fbWidth, int fbHeight, const char* title, std::string* error);
    void shutdown();

    // Fills a fresh InputState for this frame. Returns false when the user
    // asked to quit.
    bool pump(InputState& input);

    void present(const Framebuffer& fb);
    bool saveScreenshot(const Framebuffer& fb, const std::string& path);

    void setMouseCaptured(bool captured);
    void toggleFullscreen();

private:
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    int fbWidth_ = 0;
    int fbHeight_ = 0;
    bool fullscreen_ = false;
    bool mouseCaptured_ = false;
};

}  // namespace rf
