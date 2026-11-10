// Entry point. Supports normal windowed play and a headless screenshot mode
// used by the test/verification tooling.
#define SDL_MAIN_HANDLED

#include <SDL.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "core/game.h"
#include "core/input.h"
#include "math/trig.h"
#include "platform/platform.h"
#include "render/framebuffer.h"

#ifndef RF_ASSET_DIR
#define RF_ASSET_DIR "assets"
#endif

namespace {

constexpr int kFbWidth = 640;
constexpr int kFbHeight = 400;
constexpr double kStepSeconds = 1.0 / 60.0;

struct Options {
    bool headless = false;
    int frames = 1;
    std::string shot;
    std::string mapPath = std::string(RF_ASSET_DIR) + "/sample.rmap";
    bool haveCamera = false;
    float camX = 0;
    float camY = 0;
    float camAngle = 0;
    bool minimap = false;
    bool wireframe = false;
};

bool parseArgs(int argc, char** argv, Options& opt) {
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "--headless") == 0) {
            opt.headless = true;
        } else if (std::strcmp(a, "--frames") == 0 && i + 1 < argc) {
            opt.frames = std::atoi(argv[++i]);
            if (opt.frames < 1) opt.frames = 1;
        } else if (std::strcmp(a, "--shot") == 0 && i + 1 < argc) {
            opt.shot = argv[++i];
        } else if (std::strcmp(a, "--map") == 0 && i + 1 < argc) {
            opt.mapPath = argv[++i];
        } else if (std::strcmp(a, "--cam") == 0 && i + 3 < argc) {
            opt.haveCamera = true;
            opt.camX = static_cast<float>(std::atof(argv[++i]));
            opt.camY = static_cast<float>(std::atof(argv[++i]));
            opt.camAngle = static_cast<float>(std::atof(argv[++i]));
        } else if (std::strcmp(a, "--minimap") == 0) {
            opt.minimap = true;
        } else if (std::strcmp(a, "--wireframe") == 0) {
            opt.wireframe = true;
        } else if (std::strcmp(a, "--debug") == 0) {
            opt.minimap = true;
            opt.wireframe = true;
        } else if (std::strcmp(a, "--help") == 0) {
            std::printf(
                "retrofps [--headless --frames N --shot out.bmp] [--map path] "
                "[--cam X Y ANGLE_DEG] [--minimap] [--wireframe] [--debug]\n");
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    SDL_SetMainReady();

    Options opt;
    if (!parseArgs(argc, argv, opt)) return 0;

    rf::initTrigTables();

    rf::Game game;
    std::string error;
    if (!game.init(opt.mapPath, &error)) {
        std::fprintf(stderr, "Failed to initialise game: %s\n", error.c_str());
        return 1;
    }
    if (opt.haveCamera) game.setCamera(opt.camX, opt.camY, opt.camAngle);
    if (opt.minimap || opt.wireframe) game.setDebugOverlays(opt.minimap, opt.wireframe);

    rf::Framebuffer fb(kFbWidth, kFbHeight);

    if (opt.headless) {
        rf::InputState idle;
        for (int frame = 0; frame < opt.frames; ++frame) {
            game.update(idle, kStepSeconds);
            game.render(fb);
        }
        if (!opt.shot.empty()) {
            if (!fb.saveBMP(opt.shot)) {
                std::fprintf(stderr, "Could not write %s\n", opt.shot.c_str());
                return 1;
            }
            std::printf("Wrote %s (%dx%d)\n", opt.shot.c_str(), fb.width(), fb.height());
        }
        return 0;
    }

    rf::Platform platform;
    if (!platform.init(kFbWidth, kFbHeight, "Retro FPS Engine", &error)) {
        std::fprintf(stderr, "Failed to initialise SDL platform: %s\n", error.c_str());
        return 1;
    }

    using Clock = std::chrono::steady_clock;
    auto last = Clock::now();
    double smoothedMs = 16.7;
    unsigned long frameCounter = 0;

    bool running = true;
    while (running) {
        const auto frameStart = Clock::now();
        double dt = std::chrono::duration<double>(frameStart - last).count();
        last = frameStart;
        if (dt > 0.1) dt = 0.1;
        if (dt <= 0.0) dt = kStepSeconds;

        rf::InputState in;
        running = platform.pump(in);
        if (!running) break;

        game.update(in, dt);
        game.render(fb);
        platform.present(fb);

        if (in.screenshot) {
            const std::string path = "screenshot_" + std::to_string(frameCounter++) + ".bmp";
            if (platform.saveScreenshot(fb, path)) {
                std::printf("Wrote %s\n", path.c_str());
            }
        }

        const auto frameEnd = Clock::now();
        const double ms = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
        smoothedMs = smoothedMs * 0.9 + ms * 0.1;
        game.stats().frameMs = ms;
        game.stats().fps = smoothedMs > 0.0 ? static_cast<double>(1000.0 / smoothedMs) : 0.0;

        // The software renderer may ignore vsync; cap presentation at 60 Hz.
        if (ms < 16.0) {
            SDL_Delay(static_cast<Uint32>((16.0 - ms)));
        }
    }

    platform.shutdown();
    return 0;
}
