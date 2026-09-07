#pragma once

/**
 * @brief Coordinates startup and loop-thread service of the singleton firmware components.
 *
 * Arduino invokes setup() and loop() as free functions, while Application keeps
 * the firmware lifecycle behind one explicit composition-root singleton.
 */
class Application {
 public:
	/** @brief Return the one application coordinator. */
	static Application &instance();

	/** @brief Start configuration, hardware, UI, and networking in board-safe order. */
	void begin();

	/** @brief Service queued input, telemetry presentation, LVGL, and display sleep. */
	void update();

 private:
	/** @brief Construct the singleton; use instance() to access the coordinator. */
	Application() = default;

	bool initialized_ = false;
};