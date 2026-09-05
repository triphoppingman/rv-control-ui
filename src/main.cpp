#define LGFX_USE_V1

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <WiFi.h>
#include <lvgl.h>

#include "CST816D.h"
#include "config_loader.h"
#include "constants.h"
#include "display_catalog.h"
#include "mqtt_telemetry.h"
#include "ui.h"

namespace {

using namespace rv_control_ui::constants;

/**
 * @brief Configures the GC9A01 display hardware for the rotary-screen board.
 *
 * LovyanGFX keeps the SPI bus and panel configuration together in this class.
 * The settings mirror the working demonstration firmware and must be created
 * before LVGL begins sending RGB565 draw buffers through DMA.
 */
class Display : public lgfx::LGFX_Device {
 public:
	/**
	 * @brief Bind the board SPI pins and GC9A01 panel settings to this display.
	 *
	 * This records configuration only. initializeHardware() later initializes
	 * the hardware, starts DMA support, and connects the display to LVGL.
	 */
	Display() {
		auto busConfig = bus_.config();
		busConfig.spi_host = SPI2_HOST;
		busConfig.spi_mode = 0;
		busConfig.freq_write = 80000000;
		busConfig.freq_read = 20000000;
		busConfig.spi_3wire = true;
		busConfig.use_lock = true;
		busConfig.dma_channel = SPI_DMA_CH_AUTO;
		busConfig.pin_sclk = 10;
		busConfig.pin_mosi = 11;
		busConfig.pin_miso = -1;
		busConfig.pin_dc = 3;
		bus_.config(busConfig);
		panel_.setBus(&bus_);

		auto panelConfig = panel_.config();
		panelConfig.pin_cs = 9;
		panelConfig.pin_rst = 14;
		panelConfig.pin_busy = -1;
		panelConfig.memory_width = kScreenWidth;
		panelConfig.memory_height = kScreenHeight;
		panelConfig.panel_width = kScreenWidth;
		panelConfig.panel_height = kScreenHeight;
		panelConfig.offset_x = 0;
		panelConfig.offset_y = 0;
		panelConfig.offset_rotation = 0;
		panelConfig.dummy_read_pixel = 8;
		panelConfig.dummy_read_bits = 1;
		panelConfig.readable = false;
		panelConfig.invert = true;
		panelConfig.rgb_order = false;
		panelConfig.dlen_16bit = false;
		panelConfig.bus_shared = false;
		panel_.config(panelConfig);
		setPanel(&panel_);
	}

 private:
	lgfx::Panel_GC9A01 panel_;
	lgfx::Bus_SPI bus_;
};

/** @brief High-level encoder events passed safely from the input task to loop(). */
enum class EncoderActionType : int8_t { RotateClockwise, RotateCounterclockwise, Click, DoubleClick };

/** @brief Queue payload representing one encoder movement or button action. */
struct EncoderAction {
	EncoderActionType type;
};

/** @brief Visual categories rendered with existing assets or LVGL's embedded symbols. */
enum class CarouselIcon : uint8_t { Battery, Solar, Load, Temperature, Brightness, WiFi };

/**
 * @brief Describes one selectable item in the telemetry carousel.
 *
 * A catalog index selects its generic telemetry value. The brightness item
 * deliberately has no telemetry index because it controls local backlight.
 */
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
	uint8_t precision;
	bool compact;
	uint8_t fontSize;
};

CarouselItem carouselItems[kMaximumCarouselItems] = {};
size_t carouselItemCount = 0;

// The physical panel driver and touch controller shared by LVGL callbacks.
Display display;
CST816D touch(kTouchSdaPin, kTouchSclPin, kTouchResetPin, kTouchInterruptPin);

// The encoder task posts input events here; only loop() consumes them and touches LVGL.
QueueHandle_t encoderActionQueue = nullptr;

// Double full-screen RGB565 buffers reside in PSRAM for LVGL rendering and DMA transfer.
uint8_t *drawBuffer = nullptr;
uint8_t *drawBufferSecondary = nullptr;

// Interrupt-written click state, read by the encoder task after the double-click window.
volatile unsigned long lastPressTime = 0;
volatile uint8_t clickCount = 0;

// Quadrature sampling state and the current selection in the telemetry carousel.
int lastEncoderClockState = LOW;
size_t selectedItemIndex = 0;

// Settings read once from /config.ini before the future Wi-Fi and MQTT task starts.
AppConfig appConfig = {};
DisplayCatalog displayCatalog = {};

// The separate network task owns Wi-Fi/MQTT; loop() later copies its snapshots for LVGL.
MqttTelemetry mqttTelemetry;
uint32_t lastTelemetrySequence = 0;

// The UI-loop-owned snapshot copied from the network task before rendering labels or arcs.
TelemetrySnapshot latestSnapshot = {};
bool hasTelemetrySnapshot = false;

// Runtime labels overlay LVGL symbols for telemetry types missing from the SquareLine asset set.
lv_obj_t *carouselSymbolLabels[3] = {nullptr, nullptr, nullptr};
// Wi-Fi Info uses dedicated labels instead of the generated electrical-value label.
lv_obj_t *wifiSignalDetailLabel = nullptr;
lv_obj_t *wifiInfoDetailLabel = nullptr;
// Catalog-driven tick labels replace the static demo scales on telemetry detail screens.
constexpr size_t kDialTickLabelCount = 6;
lv_obj_t *electricalTickLabels[kDialTickLabelCount] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
lv_obj_t *temperatureTickLabels[kDialTickLabelCount] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

// Limits Wi-Fi Info rendering to once per second while preserving a responsive LVGL loop.
uint32_t lastWiFiInfoRefreshMilliseconds = 0;

// Detects SquareLine-initiated touch transitions so application layers stay synchronized.
lv_obj_t *lastActiveScreen = nullptr;

// Backlight state is separate from telemetry: inactivity turns it off without losing the preference.
uint8_t displayBrightnessPercent = kDefaultBrightnessPercent;
uint32_t lastUserActivityMilliseconds = 0;
bool displaySleeping = false;

/** @brief Show the selected carousel item and its visible neighboring previews. */
void updateSelection();

/** @brief Refresh the active generated detail screen using the current carousel item. */
void refreshActiveDetail();

/** @brief Record user input and restore the backlight when the display is sleeping. */
void recordUserActivity();

/** @brief Turn off the backlight after the configured period without input. */
void updateDisplaySleep();

/** @brief Build telemetry entries and append enabled device-local controls. */
void initializeCarouselItems();

/** @brief Reapply application-owned UI layers after an LVGL screen transition. */
void synchronizeActiveScreen();

/**
 * @brief Record a debounced encoder-button press with minimal interrupt work.
 *
 * The input task interprets this timestamp and count as a single or double
 * click. This ISR deliberately does not access LVGL or queue complex work.
 */
void IRAM_ATTR encoderButtonInterrupt() {
	static unsigned long lastInterruptTime = 0;
	const unsigned long now = millis();
	if (now - lastInterruptTime > kDebounceMilliseconds && !digitalRead(kEncoderButtonPin)) {
		lastPressTime = now;
		clickCount++;
	}
	lastInterruptTime = now;
}

/**
 * @brief Transfer an LVGL-rendered rectangle to the physical display via DMA.
 *
 * LVGL invokes this on the main application thread after filling a draw
 * buffer. Completion is acknowledged only after DMA finishes so LVGL can
 * safely reuse the buffer for a subsequent frame.
 */
void flushDisplay(lv_display_t *lvDisplay, const lv_area_t *area, uint8_t *pixels) {
	if (display.getStartCount() > 0) display.endWrite();
	display.pushImageDMA(area->x1, area->y1, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1,
											 reinterpret_cast<lgfx::rgb565_t *>(pixels));
	display.waitDMA();
	lv_display_flush_ready(lvDisplay);
}

/**
 * @brief Translate one CST816D sample into the LVGL pointer-input format.
 *
 * The current UI consumes coordinates; gesture data remains available for a
 * later swipe implementation without changing the driver boundary.
 */
void readTouch(lv_indev_t *, lv_indev_data_t *data) {
	uint8_t gesture = 0;
	uint16_t touchX = 0;
	uint16_t touchY = 0;
	if (!touch.getTouch(&touchX, &touchY, &gesture)) {
		data->state = LV_INDEV_STATE_REL;
		return;
	}
	recordUserActivity();
	data->state = LV_INDEV_STATE_PR;
	data->point.x = touchX;
	data->point.y = touchY;
}

/**
 * @brief Apply the selected brightness percentage to the LVGL label and PWM.
 *
 * This is intentionally the only generated control that modifies hardware.
 * It supports both touch changes to the SquareLine arc and encoder changes
 * made while the brightness detail screen is active.
 */
void brightnessChanged(lv_event_t *event) {
	if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
	const int brightness = constrain(lv_arc_get_value(lv_event_get_target_obj(event)), 0, 100);
	displayBrightnessPercent = static_cast<uint8_t>(brightness);
	char label[8];
	snprintf(label, sizeof(label), "%d%%", brightness);
	lv_label_set_text(ui_LightNum, label);
	if (!displaySleeping) ledcWrite(kBacklightPin, (brightness * 255) / 100);
}

/**
 * @brief Mark the interaction timer as active and wake the panel if necessary.
 *
 * The framebuffer and LVGL state keep running while asleep. Waking therefore
 * needs only restore PWM output; it does not rebuild a screen or reconnect MQTT.
 */
void recordUserActivity() {
	lastUserActivityMilliseconds = millis();
	if (!displaySleeping) return;
	displaySleeping = false;
	ledcWrite(kBacklightPin, (displayBrightnessPercent * 255) / 100);
	Serial.println("[INFO] Display backlight restored by user activity");
}

/**
 * @brief Blank the panel by disabling its PWM backlight after user inactivity.
 *
 * A zero `sleep_after_seconds` explicitly disables automatic sleep. The
 * timestamp subtraction is overflow-safe for the unsigned Arduino millisecond clock.
 */
void updateDisplaySleep() {
	if (displaySleeping || appConfig.sleepAfterSeconds == 0) return;
	const uint32_t sleepAfterMilliseconds = static_cast<uint32_t>(appConfig.sleepAfterSeconds) * 1000U;
	if (millis() - lastUserActivityMilliseconds < sleepAfterMilliseconds) return;
	displaySleeping = true;
	ledcWrite(kBacklightPin, 0);
	Serial.println("[INFO] Display backlight sleeping after inactivity");
}

/**
 * @brief Return the descriptor for the active telemetry or brightness item.
 *
 * selectedItemIndex is always constrained by encoder handling, but this helper
 * provides one obvious place to keep carousel data access consistent.
 */
const CarouselItem &selectedItem() {
	return carouselItems[selectedItemIndex];
}

/** @brief Translate a catalog icon name to a supported LVGL/SquareLine visual category. */
CarouselIcon catalogIcon(const char *name) {
	if (strcmp(name, "battery") == 0) return CarouselIcon::Battery;
	if (strcmp(name, "solar") == 0) return CarouselIcon::Solar;
	if (strcmp(name, "load") == 0) return CarouselIcon::Load;
	if (strcmp(name, "temperature") == 0) return CarouselIcon::Temperature;
	return CarouselIcon::Battery;
}

/**
 * @brief Build runtime carousel items from the validated SPIFFS telemetry catalog.
 *
 * Brightness and WiFi Info remain in firmware because they are local display
 * features rather than MQTT values. Each is appended only when enabled in SPIFFS.
 */
void initializeCarouselItems() {
	carouselItemCount = 0;
	for (size_t index = 0; index < displayCatalog.itemCount; ++index) {
		const TelemetryDisplayDefinition &definition = displayCatalog.items[index];
		carouselItems[carouselItemCount++] = {definition.title, definition.carouselTitle, definition.unit, index,
																				 catalogIcon(definition.icon), strcmp(definition.screen, "temperature") == 0, false, false,
																					 definition.arcMinimum, definition.arcMaximum, definition.tickLabelColor, definition.displayBackground,
																					 definition.precision, definition.compact, definition.fontSize};
	}
	if (appConfig.showBrightness) {
		carouselItems[carouselItemCount++] = {"Brightness", "Brightness", "%", kNoTelemetryIndex, CarouselIcon::Brightness, false, true, false, 0, 100, 0xFFFFFF, 0x000000, 0, false, 40};
	}
	if (appConfig.showWifi) {
		carouselItems[carouselItemCount++] = {"WiFi info", "WiFi\nInfo", "", kNoTelemetryIndex, CarouselIcon::WiFi, false, false, true, 0, 100, 0xFFFFFF, 0x000000, 0, false, 20};
	}
}

/** @brief Return true when the item needs an LVGL symbol instead of a generated image asset. */
bool usesLvglSymbol(CarouselIcon icon) {
	return icon == CarouselIcon::Battery || icon == CarouselIcon::Solar || icon == CarouselIcon::Load || icon == CarouselIcon::WiFi;
}

/** @brief Select a supported battery, charge, or power glyph for a telemetry category. */
const char *telemetrySymbol(CarouselIcon icon) {
	if (icon == CarouselIcon::Battery) return LV_SYMBOL_BATTERY_FULL;
	if (icon == CarouselIcon::Solar) return LV_SYMBOL_CHARGE;
	if (icon == CarouselIcon::WiFi) return LV_SYMBOL_WIFI;
	return LV_SYMBOL_POWER;
}

/** @brief Select an existing SquareLine image for temperature and brightness carousel items. */
const void *iconAsset(CarouselIcon icon, bool selected) {
	if (icon == CarouselIcon::Temperature) return selected ? &ui_img_icon_temp_602_png : &ui_img_icon_temp_601_png;
	return selected ? &ui_img_icon_light_602_png : &ui_img_icon_light_601_png;
}

/**
 * @brief Create the three fixed symbol positions over the generated carousel.
 *
 * The positions match the left preview, selected center, and right preview in
 * the existing SquareLine layout. These labels remain hidden for the existing
 * temperature and brightness images, which continue to use SquareLine assets.
 */
void initializeCarouselSymbols() {
	for (size_t index = 0; index < 3; ++index) {
		if (carouselSymbolLabels[index]) continue;
		carouselSymbolLabels[index] = lv_label_create(ui_Screen1);
		lv_obj_set_align(carouselSymbolLabels[index], LV_ALIGN_CENTER);
		lv_obj_set_x(carouselSymbolLabels[index], kCarouselSymbolPositions[index]);
		lv_obj_set_y(carouselSymbolLabels[index], -9);
		lv_obj_set_style_text_font(carouselSymbolLabels[index], &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_add_flag(carouselSymbolLabels[index], LV_OBJ_FLAG_HIDDEN);
	}
}

/** @brief Render an existing image or a semantically appropriate LVGL glyph in one carousel position. */
void setCarouselIcon(lv_obj_t *image, lv_obj_t *symbolLabel, const CarouselItem &item, bool selected) {
	// Each slot can alternate between a SquareLine image and an LVGL symbol as
	// the carousel moves. Clear both layers before showing the active one.
	lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(symbolLabel, LV_OBJ_FLAG_HIDDEN);
	if (usesLvglSymbol(item.icon)) {
		lv_label_set_text(symbolLabel, telemetrySymbol(item.icon));
		lv_obj_set_style_text_color(symbolLabel, selected ? lv_color_hex(0x00FFFF) : lv_color_hex(0xFFFFFF),
											 LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_remove_flag(symbolLabel, LV_OBJ_FLAG_HIDDEN);
		return;
	}
	if (selected) lv_obj_set_style_bg_image_src(image, iconAsset(item.icon, true), LV_PART_MAIN | LV_STATE_DEFAULT);
	else lv_image_set_src(image, iconAsset(item.icon, false));
}

/**
 * @brief Hide all objects in one fixed SquareLine carousel position.
 *
 * The generated layout supplies paired white preview and blue selected objects
 * for each position. Hiding both before assigning the next state prevents old
 * labels from remaining visible while the ring rotates.
 */
void hideCarouselSlot(lv_obj_t *whiteIcon, lv_obj_t *whiteLabel, lv_obj_t *blueIcon, lv_obj_t *blueLabel) {
	lv_obj_add_flag(whiteIcon, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(whiteLabel, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(blueIcon, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(blueLabel, LV_OBJ_FLAG_HIDDEN);
}

/** @brief Populate one fixed preview position with a neighboring carousel item. */
void showPreview(lv_obj_t *icon, lv_obj_t *label, lv_obj_t *symbolLabel, size_t itemIndex) {
	const CarouselItem &item = carouselItems[itemIndex];
	setCarouselIcon(icon, symbolLabel, item, false);
	lv_label_set_text(label, item.carouselTitle);
	if (!usesLvglSymbol(item.icon)) lv_obj_remove_flag(icon, LV_OBJ_FLAG_HIDDEN);
	lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
}

/** @brief Populate the central blue SquareLine position with the current carousel item. */
void showSelection(lv_obj_t *icon, lv_obj_t *label, lv_obj_t *symbolLabel, size_t itemIndex) {
	const CarouselItem &item = carouselItems[itemIndex];
	setCarouselIcon(icon, symbolLabel, item, true);
	lv_label_set_text(label, item.carouselTitle);
	if (!usesLvglSymbol(item.icon)) lv_obj_remove_flag(icon, LV_OBJ_FLAG_HIDDEN);
	lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Open the generated child screen represented by the carousel choice.
 *
 * The existing SquareLine export has three detail screens. This preserves the
 * demonstration navigation pattern while later work maps the first two to
 * selected Renogy values and retains the third for display brightness.
 */
void openSelectedScreen() {
	if (carouselItemCount == 0 || lv_screen_active() != ui_Screen1) return;
	// The generated helper keeps screens alive across transitions. Keep runtime
	// overlay references too, or returning to the carousel would create duplicate
	// LVGL symbol labels on top of the preserved SquareLine screen.
	const CarouselItem &item = selectedItem();
	if (item.controlsBrightness) {
		_ui_screen_change(&ui_Screen4, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, &ui_Screen4_screen_init);
	} else if (item.usesTemperatureScreen) {
		_ui_screen_change(&ui_Screen3, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, &ui_Screen3_screen_init);
	} else {
		_ui_screen_change(&ui_Screen2, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, &ui_Screen2_screen_init);
	}
	refreshActiveDetail();
}

/**
 * @brief Return from any generated detail screen to the main carousel.
 *
 * The fade transition and post-transition selection refresh mirror the demo,
 * ensuring the active carousel item remains visibly selected after return.
 */
void returnToCarousel() {
	if (lv_screen_active() != ui_Screen1) {
		_ui_screen_change(&ui_Screen1, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, &ui_Screen1_screen_init);
		updateSelection();
	}
}

/**
 * @brief Refresh the selected and neighboring icons in the generated carousel.
 *
 * SquareLine supplies blue objects for the centered selection and white
 * objects for available neighbors. The telemetry carousel will keep this
 * behavior while expanding the number of represented values.
 */
void updateSelection() {
	if (carouselItemCount == 0) return;
	initializeCarouselSymbols();
	// Runtime symbol labels are separate from SquareLine's generated image slots.
	// Hide every previous glyph first so index zero has no phantom left preview.
	for (lv_obj_t *symbolLabel : carouselSymbolLabels) {
		lv_obj_add_flag(symbolLabel, LV_OBJ_FLAG_HIDDEN);
	}
	hideCarouselSlot(ui_volumeWhite, ui_volumeTextWhite, ui_volumeBlue, ui_volumeTextBlue);
	hideCarouselSlot(ui_tempWhite, ui_tempTextWhite, ui_tempBlue, ui_tempTextBlue);
	hideCarouselSlot(ui_lightWhite, ui_lightTextWhite, ui_lightBlue, ui_lightTextBlue);
	if (selectedItemIndex > 0) showPreview(ui_volumeWhite, ui_volumeTextWhite, carouselSymbolLabels[0], selectedItemIndex - 1);
	showSelection(ui_tempBlue, ui_tempTextBlue, carouselSymbolLabels[1], selectedItemIndex);
	if (selectedItemIndex + 1 < carouselItemCount) {
		showPreview(ui_lightWhite, ui_lightTextWhite, carouselSymbolLabels[2], selectedItemIndex + 1);
	}
}

/**
 * @brief Apply one queued rotary movement to the active UI state.
 *
 * On the carousel the movement changes the selected item. On the brightness
 * screen it changes only the local backlight in five-percent increments.
 * Other detail screens remain read-only by design.
 */
void handleEncoderRotation(bool clockwise) {
	if (carouselItemCount == 0) return;
	recordUserActivity();
	if (lv_screen_active() == ui_Screen4) {
		const int brightness = constrain(lv_arc_get_value(ui_lightArc) + (clockwise ? 5 : -5), 0, 100);
		lv_arc_set_value(ui_lightArc, brightness);
		lv_obj_send_event(ui_lightArc, LV_EVENT_VALUE_CHANGED, nullptr);
		return;
	}
	if (lv_screen_active() != ui_Screen1) return;
	if (clockwise && selectedItemIndex + 1 < carouselItemCount) ++selectedItemIndex;
	if (!clockwise && selectedItemIndex > 0) --selectedItemIndex;
	updateSelection();
}

/**
 * @brief Format a telemetry field for the large generated value labels.
 *
 * Values are shown as `--` until a snapshot contains the field. Catalog
 * precision controls decimals; `compact` scales values of at least $1000$ with
 * a `k` unit prefix to keep the round dial readable.
 */
void formatTelemetryValue(const CarouselItem &item, char *text, size_t textSize) {
	if (!hasTelemetrySnapshot || item.telemetryIndex == kNoTelemetryIndex) {
		strlcpy(text, "--", textSize);
		return;
	}
	const TelemetryValue value = latestSnapshot.values[item.telemetryIndex];
	if (!value.available) {
		strlcpy(text, "--", textSize);
		return;
	}
	const char *unit = item.usesTemperatureScreen ? appConfig.temperatureUnit : item.unit;
	char compactUnit[12] = {};
	float displayValue = value.value;
	if (item.compact && fabsf(displayValue) >= 1000.0F) {
		displayValue /= 1000.0F;
		snprintf(compactUnit, sizeof(compactUnit), "k%s", unit);
		unit = compactUnit;
	}
	snprintf(text, textSize, "%.*f%s", item.precision, displayValue, unit);
}

/**
 * @brief Choose a configured dial font and fall back only when text exceeds its label width.
 *
 * The generated dial labels have a fixed width. Measuring real glyph widths
 * preserves the preferred large type for normal values while preventing a long
 * value or unit from wrapping onto a second line.
 */
const lv_font_t *fittingDialFont(const char *text, uint8_t preferredSize, lv_coord_t availableWidth) {
	const lv_font_t *fonts[] = {&lv_font_montserrat_40, &lv_font_montserrat_28, &lv_font_montserrat_20};
	const uint8_t sizes[] = {40, 28, 20};
	for (size_t index = 0; index < 3; ++index) {
		if (sizes[index] > preferredSize) continue;
		lv_point_t textSize = {};
		lv_text_get_size(&textSize, text, fonts[index], 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
		if (textSize.x <= availableWidth) return fonts[index];
	}
	return &lv_font_montserrat_20;
}

/** @brief Apply formatted text and a non-wrapping fitted font to one generated dial label. */
void setDialValue(lv_obj_t *label, const char *text, uint8_t preferredFontSize) {
	lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
	lv_obj_set_style_text_font(label, fittingDialFont(text, preferredFontSize, lv_obj_get_width(label)),
											LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_label_set_text(label, text);
}

/**
 * @brief Create the supplementary SSID and address label used only by Wi-Fi Info.
 *
 * The existing blue SquareLine screen supplies the large RSSI value, title,
 * arc, and return control. This small label adds the two connection details
 * that do not fit the generated screen's single large-value field.
 */
void initializeWiFiInfoLabel() {
	if (wifiInfoDetailLabel) return;
	wifiInfoDetailLabel = lv_label_create(ui_Screen2);
	lv_obj_set_width(wifiInfoDetailLabel, 180);
	lv_obj_set_align(wifiInfoDetailLabel, LV_ALIGN_CENTER);
	// Leave the lower portion of the generated screen available for its Return control.
	lv_obj_set_y(wifiInfoDetailLabel, 52);
	lv_obj_set_style_text_align(wifiInfoDetailLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(wifiInfoDetailLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_font(wifiInfoDetailLabel, &lv_font_montserrat_10, LV_PART_MAIN | LV_STATE_DEFAULT);
}

/**
 * @brief Create the dedicated RSSI label for Wi-Fi Info's center value.
 *
 * Screen 2's ui_VolNum belongs to the volume demonstration. Using a separate
 * label prevents any generated-object styling or lifecycle behavior from
 * affecting the Wi-Fi signal text.
 */
void initializeWiFiSignalLabel() {
	if (wifiSignalDetailLabel) return;
	wifiSignalDetailLabel = lv_label_create(ui_Screen2);
	lv_obj_set_width(wifiSignalDetailLabel, 150);
	lv_obj_set_height(wifiSignalDetailLabel, 56);
	lv_obj_set_align(wifiSignalDetailLabel, LV_ALIGN_CENTER);
	lv_obj_set_x(wifiSignalDetailLabel, 4);
	lv_obj_set_y(wifiSignalDetailLabel, 3);
	lv_obj_set_style_text_align(wifiSignalDetailLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(wifiSignalDetailLabel, lv_color_hex(0x33DCFF), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_font(wifiSignalDetailLabel, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
}

/**
 * @brief Populate the generated detail screen with current Wi-Fi connection data.
 *
 * WiFi status is sampled only for display. The dedicated network task remains
 * responsible for connecting Wi-Fi and MQTT, so this screen never initiates
 * network activity or logs the displayed SSID.
 */
void refreshWiFiInfo() {
	const bool connected = WiFi.status() == WL_CONNECTED;
	char signalText[16];
	int signalPercent = 0;
	if (connected) {
		const int rssi = WiFi.RSSI();
		// -100 dBm is effectively unusable and -40 dBm is a strong nearby signal.
		signalPercent = constrain(((rssi + 100) * 100) / 60, 0, 100);
		snprintf(signalText, sizeof(signalText), "%ddBm", rssi);
	} else {
		strlcpy(signalText, "Offline", sizeof(signalText));
	}

	// Keep the original blue SquareLine dial background while hiding its demo
	// speaker image and value label in favor of the dedicated Wi-Fi elements.
	lv_obj_set_style_bg_image_src(ui_Screen2, &ui_img_v2_bj_volume_100_png, LV_PART_MAIN | LV_STATE_DEFAULT);
	for (lv_obj_t *label : electricalTickLabels) {
		if (label) lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
	}
	lv_label_set_text(ui_Label4, "WiFi Info");
	lv_obj_add_flag(ui_VolNum, LV_OBJ_FLAG_HIDDEN);
	initializeWiFiSignalLabel();
	lv_label_set_text(wifiSignalDetailLabel, signalText);
	lv_arc_set_range(ui_VolumeArc, 0, 100);
	lv_arc_set_value(ui_VolumeArc, signalPercent);
	lv_obj_remove_flag(ui_VolumeArc, LV_OBJ_FLAG_CLICKABLE);
	lv_image_set_src(ui_Image3, nullptr);
	lv_obj_add_flag(ui_Image3, LV_OBJ_FLAG_HIDDEN);
	initializeWiFiInfoLabel();
	if (connected) {
		const String ipAddress = WiFi.localIP().toString();
		lv_label_set_text_fmt(wifiInfoDetailLabel, "SSID: %s\nIP: %s", WiFi.SSID().c_str(), ipAddress.c_str());
	} else {
		lv_label_set_text(wifiInfoDetailLabel, "SSID: --\nIP: --");
	}
	// Keep the original generated return button usable by touch as well as encoder double click.
	lv_obj_set_y(ui_screen2ReturnBt, 85);
	lv_obj_set_y(ui_Label3, 84);
	lv_obj_remove_flag(ui_screen2ReturnBt, LV_OBJ_FLAG_HIDDEN);
	lv_obj_remove_flag(ui_Label3, LV_OBJ_FLAG_HIDDEN);
}

/** @brief Render one catalog item's latest numeric value against its configured dial range. */
void updateTelemetryArc(lv_obj_t *arc, const CarouselItem &item) {
	lv_arc_set_range(arc, item.arcMinimum, item.arcMaximum);
	int arcValue = item.arcMinimum;
	if (hasTelemetrySnapshot && item.telemetryIndex != kNoTelemetryIndex) {
		const TelemetryValue value = latestSnapshot.values[item.telemetryIndex];
		if (value.available) {
			arcValue = static_cast<int>(lroundf(value.value));
			if (arcValue < item.arcMinimum) arcValue = item.arcMinimum;
			if (arcValue > item.arcMaximum) arcValue = item.arcMaximum;
		}
	}
	lv_arc_set_value(arc, arcValue);
}

/** @brief Draw six range-aware labels over the static demonstration dial scale. */
void updateTelemetryTickLabels(lv_obj_t *screen, lv_obj_t *labels[], const CarouselItem &item, const char *unit, lv_color_t color) {
	for (size_t index = 0; index < kDialTickLabelCount; ++index) {
		if (!labels[index]) {
			labels[index] = lv_label_create(screen);
			lv_obj_set_size(labels[index], 42, 14);
			lv_obj_set_style_text_align(labels[index], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
			lv_obj_set_style_text_font(labels[index], &lv_font_montserrat_10, LV_PART_MAIN | LV_STATE_DEFAULT);
		}
		const float fraction = static_cast<float>(index) / static_cast<float>(kDialTickLabelCount - 1);
		const int value = static_cast<int>(lroundf(item.arcMinimum + fraction * (item.arcMaximum - item.arcMinimum)));
		const float angle = kDialTickStartAngleRadians + fraction * kDialTickSweepRadians;
		const int x = static_cast<int>(lroundf(kDialCenterX + kDialTickLabelRadius * cosf(angle))) - 21;
		const int y = static_cast<int>(lroundf(kDialCenterY + kDialTickLabelRadius * sinf(angle))) - 7;
		char text[14];
		snprintf(text, sizeof(text), "%d%s", value, unit);
		lv_label_set_text(labels[index], text);
		lv_obj_set_pos(labels[index], x, y);
		lv_obj_set_style_text_color(labels[index], color, LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_remove_flag(labels[index], LV_OBJ_FLAG_HIDDEN);
	}
}

/**
 * @brief Render the active read-only telemetry detail using the demo's existing screens.
 *
 * Screen 2 retains the blue electrical background and Screen 3 retains the
 * orange temperature background. Their arcs are presentation-only; touch and
 * encoder input never change a Renogy value.
 */
void refreshActiveDetail() {
	if (carouselItemCount == 0) return;
	const CarouselItem &item = selectedItem();
	if (item.controlsBrightness) return;
	if (item.showsWiFiInfo) {
		refreshWiFiInfo();
		return;
	}
	char valueText[16];
	formatTelemetryValue(item, valueText, sizeof(valueText));
	if (item.usesTemperatureScreen) {
		lv_obj_set_style_bg_image_src(ui_Screen3, nullptr, LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_set_style_bg_color(ui_Screen3, lv_color_hex(item.displayBackground), LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_label_set_text(ui_Label8, item.title);
		setDialValue(ui_TempNum, valueText, item.fontSize);
		updateTelemetryArc(ui_TempArc, item);
		updateTelemetryTickLabels(ui_Screen3, temperatureTickLabels, item, appConfig.temperatureUnit, lv_color_hex(item.tickLabelColor));
		lv_obj_remove_flag(ui_TempArc, LV_OBJ_FLAG_CLICKABLE);
	} else {
		lv_obj_set_style_bg_image_src(ui_Screen2, nullptr, LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_set_style_bg_color(ui_Screen2, lv_color_hex(item.displayBackground), LV_PART_MAIN | LV_STATE_DEFAULT);
		if (wifiSignalDetailLabel) lv_obj_add_flag(wifiSignalDetailLabel, LV_OBJ_FLAG_HIDDEN);
		lv_obj_remove_flag(ui_VolNum, LV_OBJ_FLAG_HIDDEN);
		lv_label_set_text(ui_Label4, item.title);
		setDialValue(ui_VolNum, valueText, item.fontSize);
		updateTelemetryArc(ui_VolumeArc, item);
		updateTelemetryTickLabels(ui_Screen2, electricalTickLabels, item, item.unit, lv_color_hex(item.tickLabelColor));
		lv_obj_remove_flag(ui_VolumeArc, LV_OBJ_FLAG_CLICKABLE);
		// The original generated icon is a speaker. Hide it rather than adding a
		// runtime glyph over the dial; the carousel already identifies the value.
		lv_obj_add_flag(ui_Image3, LV_OBJ_FLAG_HIDDEN);
	}
}

/**
 * @brief Normalize application-owned layers after either encoder or SquareLine touch navigation.
 *
 * Generated SquareLine callbacks can load a screen directly, bypassing the
 * encoder-specific navigation helpers. Detecting the new active screen in the
 * LVGL owner loop ensures carousel symbols, Wi-Fi labels, and detail content
 * are refreshed exactly once regardless of how the user changed screens.
 */
void synchronizeActiveScreen() {
	lv_obj_t *activeScreen = lv_screen_active();
	if (activeScreen == lastActiveScreen) return;
	lastActiveScreen = activeScreen;
	if (activeScreen == ui_Screen1) {
		updateSelection();
	} else if (activeScreen == ui_Screen2 || activeScreen == ui_Screen3) {
		refreshActiveDetail();
	}
}

/**
 * @brief Sample the rotary encoder and enqueue debounced UI actions.
 *
 * This FreeRTOS task owns low-level quadrature sampling and click timing. It
 * does not touch LVGL because loop() is the sole LVGL owner; that rule also
 * permits the future MQTT task to update telemetry without display races.
 */
void encoderTask(void *) {
	lastEncoderClockState = digitalRead(kEncoderAPin);
	while (true) {
		const int currentClockState = digitalRead(kEncoderAPin);
		if (currentClockState != lastEncoderClockState && currentClockState == HIGH && encoderActionQueue) {
			EncoderAction action = {digitalRead(kEncoderBPin) != currentClockState ? EncoderActionType::RotateClockwise
																																							 : EncoderActionType::RotateCounterclockwise};
			xQueueSend(encoderActionQueue, &action, 0);
		}
		lastEncoderClockState = currentClockState;

		if (clickCount >= 2 && encoderActionQueue) {
			const EncoderAction action = {EncoderActionType::DoubleClick};
			clickCount = 0;
			xQueueSend(encoderActionQueue, &action, 0);
		} else if (clickCount == 1 && millis() - lastPressTime > kDoubleClickMilliseconds && encoderActionQueue) {
			const EncoderAction action = {EncoderActionType::Click};
			clickCount = 0;
			xQueueSend(encoderActionQueue, &action, 0);
		}
		vTaskDelay(pdMS_TO_TICKS(2));
	}
}

/**
 * @brief Initialize board power, display DMA, LVGL, touch, brightness, and input.
 *
 * This follows the known-good RotaryScreen demonstration sequence. It must
 * complete before any UI or MQTT code attempts to manipulate LVGL objects.
 */
void initializeHardware() {
	pinMode(kPowerLightPin, OUTPUT);
	digitalWrite(kPowerLightPin, LOW);
	pinMode(1, OUTPUT);
	pinMode(2, OUTPUT);
	digitalWrite(1, HIGH);
	digitalWrite(2, HIGH);
	pinMode(kEncoderAPin, INPUT);
	pinMode(kEncoderBPin, INPUT);
	pinMode(kEncoderButtonPin, INPUT_PULLUP);
	attachInterrupt(digitalPinToInterrupt(kEncoderButtonPin), encoderButtonInterrupt, CHANGE);

	touch.begin();
	display.init();
	display.initDMA();
	display.startWrite();
	display.fillScreen(TFT_BLACK);

	lv_init();
	lv_tick_set_cb(millis);
	const size_t bufferSize = kScreenWidth * kScreenHeight * sizeof(uint16_t);
	drawBuffer = static_cast<uint8_t *>(heap_caps_malloc(bufferSize, MALLOC_CAP_SPIRAM));
	drawBufferSecondary = static_cast<uint8_t *>(heap_caps_malloc(bufferSize, MALLOC_CAP_SPIRAM));
	if (!drawBuffer || !drawBufferSecondary) {
		Serial.println("[ERROR] LVGL buffer allocation failed");
		return;
	}

	lv_display_t *lvDisplay = lv_display_create(kScreenWidth, kScreenHeight);
	lv_display_set_color_format(lvDisplay, LV_COLOR_FORMAT_RGB565);
	lv_display_set_flush_cb(lvDisplay, flushDisplay);
	lv_display_set_buffers(lvDisplay, drawBuffer, drawBufferSecondary, bufferSize, LV_DISPLAY_RENDER_MODE_FULL);
	lv_indev_t *touchInput = lv_indev_create();
	lv_indev_set_type(touchInput, LV_INDEV_TYPE_POINTER);
	lv_indev_set_read_cb(touchInput, readTouch);
	lv_indev_set_display(touchInput, lvDisplay);

	ui_init();
	lv_obj_add_event_cb(ui_lightArc, brightnessChanged, LV_EVENT_VALUE_CHANGED, nullptr);
	updateSelection();
	ledcAttach(kBacklightPin, 5000, 8);
	lv_obj_send_event(ui_lightArc, LV_EVENT_VALUE_CHANGED, nullptr);

	encoderActionQueue = xQueueCreate(16, sizeof(EncoderAction));
	xTaskCreatePinnedToCore(encoderTask, "encoder", 2048, nullptr, 1, nullptr, 0);
}

}  // namespace

/**
 * @brief Start serial diagnostics and initialize the source-owned UI firmware.
 *
 * Arduino calls setup() once after reset. SPIFFS configuration and the MQTT
 * task will be initialized here after the display foundation is validated.
 */
void setup() {
	Serial.begin(115200);
	delay(100);
	Serial.println("[INFO] RV Control UI booting");
	char configurationError[128] = {};
	const bool configurationLoaded = loadAppConfig(appConfig, configurationError, sizeof(configurationError));
	if (configurationLoaded) {
		Serial.printf("[INFO] Configuration loaded; MQTT host=%s base topic=%s\n", appConfig.mqttHost, appConfig.mqttBaseTopic);
	} else {
		Serial.printf("[ERROR] Configuration unavailable: %s\n", configurationError);
	}
	char catalogError[128] = {};
	if (configurationLoaded && loadDisplayCatalog(displayCatalog, catalogError, sizeof(catalogError))) {
		Serial.printf("[INFO] Loaded %u telemetry display definitions\n", static_cast<unsigned>(displayCatalog.itemCount));
	} else {
		Serial.printf("[ERROR] Display catalog unavailable: %s\n", catalogError[0] ? catalogError : "configuration unavailable");
	}
	initializeCarouselItems();
	initializeHardware();
	lastUserActivityMilliseconds = millis();
	if (configurationLoaded && displayCatalog.itemCount > 0) mqttTelemetry.begin(appConfig, displayCatalog);
}

/**
 * @brief Consume queued UI input and service LVGL continuously.
 *
 * This is the only function that changes LVGL objects after initialization.
 * Future MQTT snapshot notifications are consumed here for the same reason.
 */
void loop() {
	EncoderAction action;
	while (encoderActionQueue && xQueueReceive(encoderActionQueue, &action, 0) == pdTRUE) {
		recordUserActivity();
		if (action.type == EncoderActionType::RotateClockwise) handleEncoderRotation(true);
		else if (action.type == EncoderActionType::RotateCounterclockwise) handleEncoderRotation(false);
		else if (action.type == EncoderActionType::Click) openSelectedScreen();
		else returnToCarousel();
	}
	TelemetrySnapshot snapshot;
	if (mqttTelemetry.copyLatestSnapshot(snapshot) && snapshot.sequence != lastTelemetrySequence) {
		latestSnapshot = snapshot;
		hasTelemetrySnapshot = true;
		lastTelemetrySequence = snapshot.sequence;
		Serial.printf("[INFO] Telemetry snapshot %lu received\n", static_cast<unsigned long>(snapshot.sequence));
		if (lv_screen_active() == ui_Screen2 || lv_screen_active() == ui_Screen3) refreshActiveDetail();
	}
	if (carouselItemCount > 0 && lv_screen_active() == ui_Screen2 && selectedItem().showsWiFiInfo &&
		millis() - lastWiFiInfoRefreshMilliseconds >= 1000U) {
		lastWiFiInfoRefreshMilliseconds = millis();
		refreshWiFiInfo();
	}
	updateDisplaySleep();
	lv_timer_handler();
	synchronizeActiveScreen();
	delay(5);
}