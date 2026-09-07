#include "application.h"

/** @brief Arduino setup entry point delegating to the firmware application singleton. */
void setup() { Application::instance().begin(); }

/** @brief Arduino loop entry point delegating to the firmware application singleton. */
void loop() { Application::instance().update(); }
