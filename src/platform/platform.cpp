#include "platform/platform.h"

#include <SDL.h>

#include <cstdio>

namespace rf {

Platform::~Platform() { shutdown(); }

bool Platform::init(int fbWidth, int fbHeight, const char* title, std::string* error) {
    fbWidth_ = fbWidth;
    fbHeight_ = fbHeight;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        if (error) *error = std::string("SDL_Init failed: ") + SDL_GetError();
        return false;
    }

    window_ = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               fbWidth * 2, fbHeight * 2,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window_) {
        if (error) *error = std::string("SDL_CreateWindow failed: ") + SDL_GetError();
        return false;
    }

    // The renderer is used only to scale and present the CPU framebuffer.
    // Requesting the software backend keeps all pixel production on the CPU.
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer_) {
        renderer_ = SDL_CreateRenderer(window_, -1, 0);
    }
    if (!renderer_) {
        if (error) *error = std::string("SDL_CreateRenderer failed: ") + SDL_GetError();
        return false;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");  // nearest neighbour
    SDL_RenderSetLogicalSize(renderer_, fbWidth_, fbHeight_);

    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888,
                                 SDL_TEXTUREACCESS_STREAMING, fbWidth_, fbHeight_);
    if (!texture_) {
        if (error) *error = std::string("SDL_CreateTexture failed: ") + SDL_GetError();
        return false;
    }

    setMouseCaptured(true);
    return true;
}

void Platform::shutdown() {
    if (texture_) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    SDL_Quit();
}

void Platform::setMouseCaptured(bool captured) {
    mouseCaptured_ = captured;
    SDL_SetRelativeMouseMode(captured ? SDL_TRUE : SDL_FALSE);
    if (SDL_ShowCursor(captured ? SDL_DISABLE : SDL_ENABLE) < 0) {
        // Non-fatal; some drivers reject cursor toggling.
    }
}

bool Platform::pump(InputState& in) {
    in = InputState{};

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
            case SDL_QUIT:
                in.quit = true;
                break;
            case SDL_KEYDOWN:
                if (ev.key.repeat) break;
                switch (ev.key.keysym.scancode) {
                    case SDL_SCANCODE_ESCAPE:
                        in.quit = true;
                        break;
                    case SDL_SCANCODE_E:
                        in.usePressed = true;
                        break;
                    case SDL_SCANCODE_F1:
                        in.toggleStats = true;
                        break;
                    case SDL_SCANCODE_F2:
                        in.toggleBsp = true;
                        break;
                    case SDL_SCANCODE_F3:
                        in.toggleMinimap = true;
                        break;
                    case SDL_SCANCODE_F4:
                        in.toggleBlockmap = true;
                        break;
                    case SDL_SCANCODE_F5:
                        in.toggleFire = true;
                        break;
                    case SDL_SCANCODE_F11:
                        toggleFullscreen();
                        break;
                    case SDL_SCANCODE_F12:
                        in.screenshot = true;
                        break;
                    default:
                        break;
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (ev.button.button == SDL_BUTTON_LEFT) in.firePressed = true;
                break;
            case SDL_WINDOWEVENT:
                if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                    setMouseCaptured(false);
                } else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                    setMouseCaptured(true);
                }
                break;
            default:
                break;
        }
    }
    if (in.quit) return false;

    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    in.forward = keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP] != 0;
    in.back = keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN] != 0;
    in.strafeLeft = keys[SDL_SCANCODE_A] != 0;
    in.strafeRight = keys[SDL_SCANCODE_D] != 0;
    in.sprint = (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) != 0;
    in.use = keys[SDL_SCANCODE_E] != 0;

    if (mouseCaptured_) {
        int dx = 0;
        int dy = 0;
        SDL_GetRelativeMouseState(&dx, &dy);
        in.mouseDX = dx;
        in.mouseDY = dy;

        const Uint32 buttons = SDL_GetMouseState(nullptr, nullptr);
        in.fire = (buttons & SDL_BUTTON_LMASK) != 0;
    }
    return true;
}

void Platform::present(const Framebuffer& fb) {
    if (!renderer_ || !texture_) return;
    SDL_UpdateTexture(texture_, nullptr, fb.data(), fb.width() * 4);
    SDL_RenderClear(renderer_);
    SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
}

bool Platform::saveScreenshot(const Framebuffer& fb, const std::string& path) {
    return fb.saveBMP(path);
}

void Platform::toggleFullscreen() {
    fullscreen_ = !fullscreen_;
    SDL_SetWindowFullscreen(window_, fullscreen_ ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    SDL_RenderSetLogicalSize(renderer_, fbWidth_, fbHeight_);
}

}  // namespace rf
