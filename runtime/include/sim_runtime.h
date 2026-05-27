#pragma once
#ifdef __cplusplus
extern "C" {
#endif

// Lifecycle. Called from sim_main.cpp; tests don't link sim_main.
void sim_runtime_init(int argc, char** argv);
void sim_runtime_shutdown(void);

// Event pump. Called once per loop() iteration in sim_main.
void sim_pump_events(void);

// Quit signal (window close, Ctrl-C). Drives the main loop.
int  sim_should_quit(void);

// Diagnostic logging — goes to stderr to keep stdout clean for Serial.
void sim_log(const char* msg);

#ifdef __cplusplus
}
#endif
