#include <gtest/gtest.h>
#include "sim_runtime.h"
#include "sim_lvgl.h"
#include <lvgl.h>
#include <cstdlib>

TEST(LVGLInit, AttachAndCreateLabelDoesNotCrash) {
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    sim_runtime_init(0, nullptr);
    lv_init();
    sim_lvgl_attach_sdl(480, 320);

    // lv_screen_active() is the v9 API; lv_scr_act() is a compat alias defined
    // in lv_api_map_v8.h — use the canonical v9 name.
    lv_obj_t* label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "hello");
    lv_obj_center(label);

    lv_timer_handler();
    EXPECT_EQ(sim_should_quit(), 0);

    lv_deinit();
    sim_runtime_shutdown();
}
