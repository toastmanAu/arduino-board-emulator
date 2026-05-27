#include <gtest/gtest.h>
#include "sim_runtime.h"
#include <SDL.h>

TEST(EventPump, InitDoesNotCrashWithDummyDriver) {
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    sim_runtime_init(0, nullptr);
    EXPECT_EQ(sim_should_quit(), 0);
    sim_pump_events();
    EXPECT_EQ(sim_should_quit(), 0);
    sim_runtime_shutdown();
}

TEST(EventPump, QuitEventSetsFlag) {
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    sim_runtime_init(0, nullptr);
    SDL_Event ev;
    ev.type = SDL_QUIT;
    SDL_PushEvent(&ev);
    sim_pump_events();
    EXPECT_EQ(sim_should_quit(), 1);
    sim_runtime_shutdown();
}
