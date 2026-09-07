#pragma once

#include <lvgl.h>

#include "config_loader.h"
#include "detail_renderer_factory.h"
#include "elecrow_crowpanel_display.h"
#include "network_controller.h"

/** @brief Visual categories rendered with existing assets or LVGL embedded symbols. */
enum class CarouselIcon : uint8_t { Battery, Solar, Load, Temperature, Brightness, WiFi };

/** @brief One selectable telemetry or device-local item in the carousel. */
struct CarouselItem {
	const char *title;
	const char *carouselTitle;
	const char *unit;
	size_t telemetryIndex;
	CarouselIcon icon;
	bool usesTemperatureScreen;
	bool controlsBrightness;
	bool showsWiFiInfo;
	int arcMinimum;
	int arcMaximum;
	uint32_t tickLabelColor;
	uint32_t displayBackground;
	DetailBackgroundImage backgroundImage;
	uint8_t precision;
	bool compact;
	uint8_t fontSize;
	TelemetryDisplayMode displayMode;
	int thresholdLow;
	int thresholdHigh;
	char flowSourceKey[40];
	char flowBatteryKey[40];
	char flowLoadKey[40];
};

/**
 * @brief Owns UI-level display brightness and inactivity sleep behavior.
 *
 * The controller receives all calls on Arduino's LVGL loop thread. It keeps
 * presentation-level backlight state separate from the board display's PWM
 * implementation and does not access networking or input-task state.
 */
class UiController {
 public:
	/** @brief Return the single UI and carousel owner used by the LVGL loop. */
	static UiController &instance();

	/** @brief Bind the controller to the initialized singleton board display. */
	void begin();

	/** @brief Read the singleton display settings used by the UI sleep policy. */
	void configure();

	/** @brief Build carousel items from the singleton telemetry catalog and local settings. */
	void initializeCarousel();

	/** @brief Register the generated brightness control callback after SquareLine initialization. */
	void registerGeneratedCallbacks();

	/** @brief Create generated screens and initialize application-owned UI layers. */
	void initializeGeneratedUi();

	/** @brief Render the selected carousel item and its visible neighbors. */
	void refreshCarousel();

	/** @brief Apply one rotary movement to the active carousel or brightness screen. */
	void handleRotation(bool clockwise);

	/** @brief Open the selected carousel item's generated detail screen. */
	void openSelectedScreen();

	/** @brief Return from a generated detail screen to the carousel. */
	void returnToCarousel();

	/** @brief Service queued input, copied telemetry, LVGL, and screen synchronization. */
	void update();

	/** @brief Return the number of configured telemetry and local carousel items. */
	size_t carouselItemCount() const;

	/** @brief Return one carousel item at a caller-validated index. */
	const CarouselItem &carouselItem(size_t index) const;

	/** @brief Return the currently selected carousel item. */
	const CarouselItem &selectedItem() const;

	/** @brief Return the current selection index. */
	size_t selectedItemIndex() const;

	/** @brief Move the selection one position when a neighboring item exists. */
	void moveSelection(bool clockwise);

	/** @brief Store a newly copied network snapshot and report whether its sequence changed. */
	bool acceptTelemetrySnapshot(const TelemetrySnapshot &snapshot);

	/** @brief Return whether the active UI has a copied telemetry snapshot. */
	bool hasTelemetrySnapshot() const;

	/** @brief Return the copied telemetry snapshot owned by the UI loop. */
	const TelemetrySnapshot &latestSnapshot() const;

	/** @brief Return true once per second while the Wi-Fi detail view needs refreshing. */
	bool shouldRefreshWiFiInfo();

	/** @brief Render the current board Wi-Fi state on the generated detail screen. */
	void refreshWiFiInfo();

	/** @brief Update the active electrical detail dial's catalog-driven tick labels. */
	void updateElectricalTickLabels();

	/** @brief Update the active temperature detail dial's catalog-driven tick labels. */
	void updateTemperatureTickLabels();

	/** @brief Record interaction and restore the backlight when it is sleeping. */
	void recordUserActivity();

	/** @brief Blank the backlight once the configured idle period has elapsed. */
	void updateDisplaySleep();

	/** @brief Apply the current SquareLine brightness-arc value to the display. */
	void handleBrightnessChanged(lv_event_t *event);

 private:
	/** @brief Construct the singleton; startup supplies its hardware dependency through begin(). */
	UiController() = default;

	ElecrowCrowPanelDisplay *display_ = nullptr;
	CarouselItem carouselItems_[rv_control_ui::constants::kMaximumCarouselItems] = {};
	size_t carouselItemCount_ = 0;
	size_t selectedItemIndex_ = 0;
	uint16_t sleepAfterSeconds_ = 0;
	uint8_t brightnessPercent_ = 50;
	uint32_t lastUserActivityMilliseconds_ = 0;
	bool displaySleeping_ = false;
	TelemetrySnapshot latestSnapshot_ = {};
	uint32_t lastTelemetrySequence_ = 0;
	bool hasTelemetrySnapshot_ = false;
	uint32_t lastWiFiInfoRefreshMilliseconds_ = 0;
	lv_obj_t *wifiSignalDetailLabel_ = nullptr;
	lv_obj_t *wifiInfoDetailLabel_ = nullptr;
	lv_obj_t *electricalTickLabels_[rv_control_ui::constants::kDialTickLabelCount] = {};
	lv_obj_t *temperatureTickLabels_[rv_control_ui::constants::kDialTickLabelCount] = {};
	DetailRendererFactory rendererFactory_;
	DetailRenderer *activeRenderer_ = nullptr;

	/** @brief Create the supplementary SSID and address label used only by Wi-Fi Info. */
	void initializeWiFiInfoLabel();

	/** @brief Create the dedicated RSSI label for Wi-Fi Info's center value. */
	void initializeWiFiSignalLabel();

	/** @brief Draw six range-aware labels over one static demonstration dial scale. */
	void updateTelemetryTickLabels(lv_obj_t *screen, lv_obj_t *labels[], const CarouselItem &item, const char *unit,
										 lv_color_t color);

	/** @brief Forward the generated brightness-arc callback to the singleton controller. */
	static void brightnessChangedCallback(lv_event_t *event);
	/** @brief Refresh the active telemetry detail screen from the copied snapshot. */
	void refreshActiveDetail();
	/** @brief Synchronize application overlays after a SquareLine screen transition. */
	void synchronizeActiveScreen();
	/** @brief Format one selected telemetry value for a generated dial label. */
	void formatTelemetryValue(const CarouselItem &item, char *text, size_t textSize) const;
	/** @brief Choose a dial font that fits the generated label width. */
	static const lv_font_t *fittingDialFont(const char *text, uint8_t preferredSize, lv_coord_t availableWidth);
	/** @brief Apply fitted non-wrapping text to a generated dial label. */
	static void setDialValue(lv_obj_t *label, const char *text, uint8_t preferredFontSize);
	/** @brief Render one telemetry value against its configured arc range. */
	void updateTelemetryArc(lv_obj_t *arc, const CarouselItem &item) const;

	/** @brief Return true when an icon is rendered by LVGL rather than SquareLine image assets. */
	static bool usesLvglSymbol(CarouselIcon icon);
	/** @brief Return the LVGL symbol matching a telemetry icon category. */
	static const char *telemetrySymbol(CarouselIcon icon);
	/** @brief Return the SquareLine asset for a temperature or brightness carousel icon. */
	static const void *iconAsset(CarouselIcon icon, bool selected);
	/** @brief Create the three dynamic symbol labels over the generated carousel. */
	void initializeCarouselSymbols();
	/** @brief Configure one generated image slot or dynamic symbol label. */
	void setCarouselIcon(lv_obj_t *image, lv_obj_t *symbolLabel, const CarouselItem &item, bool selected);
	/** @brief Hide all generated objects belonging to one carousel slot. */
	static void hideCarouselSlot(lv_obj_t *whiteIcon, lv_obj_t *whiteLabel, lv_obj_t *blueIcon, lv_obj_t *blueLabel);
	/** @brief Render one neighboring carousel preview. */
	void showPreview(lv_obj_t *icon, lv_obj_t *label, lv_obj_t *symbolLabel, size_t itemIndex);
	/** @brief Render the selected center carousel item. */
	void showSelection(lv_obj_t *icon, lv_obj_t *label, lv_obj_t *symbolLabel, size_t itemIndex);

	lv_obj_t *carouselSymbolLabels_[3] = {};
	lv_obj_t *lastActiveScreen_ = nullptr;
};