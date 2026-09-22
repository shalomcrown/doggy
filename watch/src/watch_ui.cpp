#include "watch_ui.h"

#include "board_hal.h"
#include "time_offset.h"
#include "watch_network.h"
#include "watch_settings.h"
#include "watch_sleep_policy.h"
#include "watch_status.h"

#include <lvgl.h>

#include <cstdio>
#include <cstring>

namespace {

const char *kHourOptions =
        "-12\n-11\n-10\n-9\n-8\n-7\n-6\n-5\n-4\n-3\n-2\n-1\n0\n1\n2\n3\n"
        "4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14";
const char *kMinuteOptions = "00\n15\n30\n45";

inline constexpr int kStatusBarHeight = 22;
inline constexpr int kSsidCharacterBudget = 12;
inline constexpr int kSignalBarWidth = 4;
inline constexpr int kSignalBarGap = 2;
inline constexpr int kSignalBarBaseHeight = 4;
inline constexpr int kSignalBarStep = 3;

lv_obj_t *tile_view = nullptr;
lv_obj_t *clock_tile = nullptr;
lv_obj_t *settings_tile = nullptr;
lv_obj_t *clock_label = nullptr;
lv_obj_t *date_label = nullptr;
lv_obj_t *sync_label = nullptr;
lv_obj_t *status_label = nullptr;
lv_obj_t *ssid_label = nullptr;
lv_obj_t *battery_label = nullptr;
lv_obj_t *signal_bars[kWatchSignalBarCount] = {nullptr};
lv_obj_t *offset_label = nullptr;
lv_obj_t *hour_roller = nullptr;
lv_obj_t *minute_roller = nullptr;
lv_obj_t *idle_roller = nullptr;
// The settings page edits this copy; nothing reaches storage until Save.
int draft_offset_hours = kWatchDefaultOffsetHours;
int draft_offset_minutes = kWatchDefaultOffsetMinutes;
int draft_idle_timeout_seconds = kWatchDefaultIdleTimeoutSeconds;
std::time_t last_rendered_time = 0;
long last_rendered_sync_age = -2;
bool last_time_valid = false;

}

// ================================================================================

static lv_color_t background_color() {
    return lv_color_black();
}

// ================================================================================

static lv_color_t foreground_color() {
    return lv_color_white();
}

// ================================================================================

static lv_color_t muted_color() {
    return lv_color_hex(0x404040);
}

// ================================================================================

static void apply_dark_surface(lv_obj_t *object) {
    lv_obj_set_style_bg_color(object, background_color(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(object, foreground_color(), LV_PART_MAIN);
    lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
}

// ================================================================================

static void refresh_offset_label() {
    char text[24];
    std::snprintf(
            text,
            sizeof(text),
            "UTC%+d:%02d",
            draft_offset_hours,
            draft_offset_minutes);
    lv_label_set_text(offset_label, text);
}

// ================================================================================

// Pulls the rollers back to what is actually stored, which is how both Cancel
// and leaving the page throw an edit away.
static void discard_draft() {
    if (hour_roller == nullptr) {
        return;
    }
    draft_offset_hours = watch_settings_offset_hours();
    draft_offset_minutes = watch_settings_offset_minutes();
    draft_idle_timeout_seconds = watch_settings_idle_timeout_seconds();
    lv_roller_set_selected(
            hour_roller,
            draft_offset_hours - kWatchMinimumOffsetHours,
            LV_ANIM_OFF);
    lv_roller_set_selected(
            minute_roller,
            draft_offset_minutes / 15,
            LV_ANIM_OFF);
    lv_roller_set_selected(
            idle_roller,
            watch_idle_timeout_option(draft_idle_timeout_seconds),
            LV_ANIM_OFF);
    refresh_offset_label();
}

// ================================================================================

static void offset_changed(lv_event_t *) {
    draft_offset_hours = static_cast<int>(lv_roller_get_selected(hour_roller))
            + kWatchMinimumOffsetHours;
    draft_offset_minutes =
            static_cast<int>(lv_roller_get_selected(minute_roller)) * 15;
    refresh_offset_label();
}

// ================================================================================

static void idle_timeout_changed(lv_event_t *) {
    draft_idle_timeout_seconds =
            watch_idle_timeout_seconds(lv_roller_get_selected(idle_roller));
}

// ================================================================================

static void save_settings(lv_event_t *) {
    watch_settings_set_offset(draft_offset_hours, draft_offset_minutes);
    watch_settings_set_idle_timeout_seconds(draft_idle_timeout_seconds);
    // The stored offset moved, so the clock has to redraw even on the same
    // second.
    last_rendered_time = 0;
    discard_draft();
}

// ================================================================================

static void cancel_settings(lv_event_t *) {
    discard_draft();
}

// ================================================================================

static void start_provisioning(lv_event_t *) {
    watch_network_start_provisioning();
}

// ================================================================================

static void show_settings(lv_event_t *) {
    lv_tileview_set_tile(tile_view, settings_tile, LV_ANIM_ON);
}

// ================================================================================

static void show_clock(lv_event_t *) {
    lv_tileview_set_tile(tile_view, clock_tile, LV_ANIM_ON);
}

// ================================================================================

// Swiping back to the clock counts as walking away from the edit, same as Cancel.
static void tile_changed(lv_event_t *) {
    if (lv_tileview_get_tile_active(tile_view) != settings_tile) {
        discard_draft();
    }
}

// ================================================================================

static lv_obj_t *make_button(
        lv_obj_t *parent,
        const char *text,
        lv_event_cb_t callback) {
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, 120, 44);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_style_bg_color(button, background_color(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, foreground_color(), LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_text_color(button, foreground_color(), LV_PART_MAIN);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}

// ================================================================================

static void build_status_bar() {
    lv_obj_t *bar = lv_obj_create(clock_tile);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, lv_pct(100), kStatusBarHeight);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 2);
    apply_dark_surface(bar);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    ssid_label = lv_label_create(bar);
    lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(ssid_label, foreground_color(), LV_PART_MAIN);
    lv_label_set_text(ssid_label, "No Wi-Fi");
    lv_obj_align(ssid_label, LV_ALIGN_LEFT_MID, 6, 0);

    for (int index = 0; index < kWatchSignalBarCount; index += 1) {
        lv_obj_t *bar_piece = lv_obj_create(bar);
        lv_obj_remove_style_all(bar_piece);
        const int height = kSignalBarBaseHeight + index * kSignalBarStep;
        lv_obj_set_size(bar_piece, kSignalBarWidth, height);
        lv_obj_set_style_bg_color(bar_piece, muted_color(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar_piece, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(bar_piece, 1, LV_PART_MAIN);
        const int offset_from_right =
                -6 - (kWatchSignalBarCount - 1 - index)
                        * (kSignalBarWidth + kSignalBarGap);
        lv_obj_align(bar_piece, LV_ALIGN_BOTTOM_RIGHT, offset_from_right, -4);
        signal_bars[index] = bar_piece;
    }

    battery_label = lv_label_create(bar);
    lv_obj_set_style_text_font(
            battery_label,
            &lv_font_montserrat_12,
            LV_PART_MAIN);
    lv_obj_set_style_text_color(battery_label, foreground_color(), LV_PART_MAIN);
    lv_label_set_text(battery_label, LV_SYMBOL_BATTERY_EMPTY " --");
    const int battery_offset =
            -6 - kWatchSignalBarCount * (kSignalBarWidth + kSignalBarGap) - 6;
    lv_obj_align(battery_label, LV_ALIGN_RIGHT_MID, battery_offset, 0);
}

// ================================================================================

static void build_clock_page() {
    build_status_bar();

    clock_label = lv_label_create(clock_tile);
    lv_obj_set_style_text_font(
            clock_label,
            &lv_font_montserrat_48,
            LV_PART_MAIN);
    lv_label_set_text(clock_label, "--:--:--");
    lv_obj_align(clock_label, LV_ALIGN_CENTER, 0, -30);

    date_label = lv_label_create(clock_tile);
    lv_obj_set_style_text_font(
            date_label,
            &lv_font_montserrat_16,
            LV_PART_MAIN);
    lv_label_set_text(date_label, "Waiting for NTP");
    lv_obj_align(date_label, LV_ALIGN_CENTER, 0, 6);

    sync_label = lv_label_create(clock_tile);
    lv_obj_set_style_text_font(sync_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_label_set_text(sync_label, "Time not synchronized");
    lv_obj_align(sync_label, LV_ALIGN_CENTER, 0, 30);

    status_label = lv_label_create(clock_tile);
    lv_obj_set_style_text_font(
            status_label,
            &lv_font_montserrat_12,
            LV_PART_MAIN);
    lv_obj_set_width(status_label, lv_pct(90));
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(status_label, "Starting Wi-Fi");
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -58);

    lv_obj_t *settings_button =
            make_button(clock_tile, "Settings >", show_settings);
    lv_obj_align(settings_button, LV_ALIGN_BOTTOM_MID, 0, -8);
}

// ================================================================================

static lv_obj_t *make_row(lv_obj_t *parent) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
            row,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, LV_PART_MAIN);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    apply_dark_surface(row);
    return row;
}

// ================================================================================

static lv_obj_t *make_section_label(const char *text) {
    lv_obj_t *label = lv_label_create(settings_tile);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, LV_PART_MAIN);
    return label;
}

// ================================================================================

static lv_obj_t *make_roller(
        lv_obj_t *parent,
        const char *options,
        lv_event_cb_t callback) {
    lv_obj_t *roller = lv_roller_create(parent);
    lv_roller_set_options(roller, options, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller, 3);
    lv_obj_set_width(roller, 90);
    apply_dark_surface(roller);
    lv_obj_set_style_border_color(roller, foreground_color(), LV_PART_MAIN);
    lv_obj_set_style_border_width(roller, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_color(roller, muted_color(), LV_PART_SELECTED);
    lv_obj_add_event_cb(roller, callback, LV_EVENT_VALUE_CHANGED, nullptr);
    return roller;
}

// ================================================================================

static void build_settings_page() {
    // The page is taller than either panel, so it stacks and scrolls instead of
    // aligning against fixed offsets.
    lv_obj_set_flex_flow(settings_tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
            settings_tile,
            LV_FLEX_ALIGN_START,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(settings_tile, 8, LV_PART_MAIN);
    lv_obj_set_scroll_dir(settings_tile, LV_DIR_VER);

    lv_obj_t *title = make_section_label("UTC offset");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);

    offset_label = make_section_label("UTC+0:00");
    lv_obj_set_style_text_font(
            offset_label,
            &lv_font_montserrat_24,
            LV_PART_MAIN);

    lv_obj_t *offset_row = make_row(settings_tile);
    hour_roller = make_roller(offset_row, kHourOptions, offset_changed);
    minute_roller = make_roller(offset_row, kMinuteOptions, offset_changed);

    make_section_label("Sleep after");
    idle_roller = make_roller(
            settings_tile,
            watch_idle_timeout_labels(),
            idle_timeout_changed);

    make_button(settings_tile, "Pair Wi-Fi", start_provisioning);

    lv_obj_t *commit_row = make_row(settings_tile);
    lv_obj_t *save_button = make_button(commit_row, "Save", save_settings);
    lv_obj_set_width(save_button, 100);
    lv_obj_t *cancel_button =
            make_button(commit_row, "Cancel", cancel_settings);
    lv_obj_set_width(cancel_button, 100);

    make_button(settings_tile, "< Clock", show_clock);
    discard_draft();
}

// ================================================================================

static const char *battery_symbol(WatchBatteryLevel level) {
    switch (level) {
        case WatchBatteryLevel::Full:
            return LV_SYMBOL_BATTERY_FULL;
        case WatchBatteryLevel::High:
            return LV_SYMBOL_BATTERY_3;
        case WatchBatteryLevel::Half:
            return LV_SYMBOL_BATTERY_2;
        case WatchBatteryLevel::Low:
            return LV_SYMBOL_BATTERY_1;
        case WatchBatteryLevel::Empty:
            return LV_SYMBOL_BATTERY_EMPTY;
        case WatchBatteryLevel::Unknown:
        default:
            return LV_SYMBOL_BATTERY_EMPTY;
    }
}

// ================================================================================

static void refresh_battery(const WatchUiState &state) {
    const WatchBatteryLevel level =
            watch_battery_level(state.battery_percent, state.battery_valid);
    char text[24];
    if (level == WatchBatteryLevel::Unknown) {
        std::snprintf(text, sizeof(text), "%s --", battery_symbol(level));
    } else if (state.battery_charging) {
        std::snprintf(
                text,
                sizeof(text),
                "%s%s %d%%",
                LV_SYMBOL_CHARGE,
                battery_symbol(level),
                state.battery_percent);
    } else {
        std::snprintf(
                text,
                sizeof(text),
                "%s %d%%",
                battery_symbol(level),
                state.battery_percent);
    }
    lv_label_set_text(battery_label, text);
}

// ================================================================================

static void refresh_signal(const WatchUiState &state) {
    const int bars = watch_signal_bars(state.rssi, state.wifi_connected);
    for (int index = 0; index < kWatchSignalBarCount; index += 1) {
        const lv_color_t color =
                index < bars ? foreground_color() : muted_color();
        lv_obj_set_style_bg_color(signal_bars[index], color, LV_PART_MAIN);
    }

    char ssid_text[40];
    watch_format_ssid(
            state.ssid,
            state.wifi_connected,
            kSsidCharacterBudget,
            ssid_text,
            sizeof(ssid_text));
    lv_label_set_text(ssid_label, ssid_text);
}

// ================================================================================

static void refresh_sync_age(long seconds) {
    if (seconds == last_rendered_sync_age) {
        return;
    }
    char text[48];
    watch_format_sync_age(seconds, text, sizeof(text));
    lv_label_set_text(sync_label, text);
    last_rendered_sync_age = seconds;
}

// ================================================================================

void watch_ui_begin() {
    lv_obj_t *screen = lv_screen_active();
    apply_dark_surface(screen);

    tile_view = lv_tileview_create(screen);
    lv_obj_set_size(tile_view, lv_pct(100), lv_pct(100));
    apply_dark_surface(tile_view);
    lv_obj_set_style_bg_color(tile_view, background_color(), LV_PART_SCROLLBAR);

    clock_tile = lv_tileview_add_tile(tile_view, 0, 0, LV_DIR_RIGHT);
    settings_tile = lv_tileview_add_tile(tile_view, 1, 0, LV_DIR_LEFT);
    apply_dark_surface(clock_tile);
    apply_dark_surface(settings_tile);
    // Every child aligns against the tile content area, so padding the tiles
    // keeps the whole page clear of a rounded panel mask.
    const int inset = watch_board_safe_inset();
    lv_obj_set_style_pad_all(clock_tile, inset, LV_PART_MAIN);
    lv_obj_set_style_pad_all(settings_tile, inset, LV_PART_MAIN);

    build_clock_page();
    build_settings_page();
    lv_obj_add_event_cb(
            tile_view,
            tile_changed,
            LV_EVENT_VALUE_CHANGED,
            nullptr);
}

// ================================================================================

void watch_ui_sleep() {
    if (tile_view == nullptr) {
        return;
    }
    lv_tileview_set_tile(tile_view, clock_tile, LV_ANIM_OFF);
    discard_draft();
}

// ================================================================================

void watch_ui_update(const WatchUiState &state) {
    lv_label_set_text(status_label, state.network_status);
    refresh_signal(state);
    refresh_battery(state);
    refresh_sync_age(state.sync_age_seconds);

    if (state.time_valid == false) {
        if (last_time_valid) {
            lv_label_set_text(clock_label, "--:--:--");
            lv_label_set_text(date_label, "Waiting for NTP");
        }
        last_time_valid = false;
        return;
    }

    const std::time_t local_time = state.utc_time + watch_utc_offset_seconds(
            watch_settings_offset_hours(),
            watch_settings_offset_minutes());
    if (local_time == last_rendered_time && last_time_valid) {
        return;
    }
    std::tm broken_down{};
    gmtime_r(&local_time, &broken_down);
    char clock_text[16];
    char date_text[32];
    std::strftime(clock_text, sizeof(clock_text), "%H:%M:%S", &broken_down);
    std::strftime(date_text, sizeof(date_text), "%a %Y-%m-%d", &broken_down);
    lv_label_set_text(clock_label, clock_text);
    lv_label_set_text(date_label, date_text);
    last_rendered_time = local_time;
    last_time_valid = true;
}
