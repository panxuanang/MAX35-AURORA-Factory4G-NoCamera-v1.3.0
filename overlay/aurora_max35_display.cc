#include "aurora_max35_display.h"

#include "application.h"
#include "board.h"
#include "lvgl_theme.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

#define TAG "AuroraMAX35"

namespace {
constexpr uint32_t kBg = 0x07101F;
constexpr uint32_t kPanel = 0x0D1B2E;
constexpr uint32_t kPanelAlt = 0x10243A;
constexpr uint32_t kAccent = 0x5BD9FF;
constexpr uint32_t kAccent2 = 0x8A7CFF;
constexpr uint32_t kText = 0xF2F7FF;
constexpr uint32_t kMuted = 0x92A6BF;
constexpr uint32_t kUser = 0x143B52;
constexpr uint32_t kAssistant = 0x13243A;
constexpr int kAnimPeriodMs = 40;
constexpr int kInitialScrollDelayTicks = 40;   // 1.6 s
constexpr int kResumeScrollDelayTicks = 12;    // 0.48 s when another TTS sentence arrives
constexpr int kReturnHomeDelayTicks = 30;      // 1.2 s after the last line is readable
constexpr int kScrollPixelsPerTick = 2;        // ~50 px/s
}

AuroraMax35Display::AuroraMax35Display(esp_lcd_panel_io_handle_t panel_io,
                                       esp_lcd_panel_handle_t panel,
                                       int width,
                                       int height,
                                       int offset_x,
                                       int offset_y,
                                       bool mirror_x,
                                       bool mirror_y,
                                       bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy) {
    SetupAuroraUi();
    ESP_LOGI(TAG, "AURORA UI attached to SpotPear MAX35 factory 4G display");
}

AuroraMax35Display::~AuroraMax35Display() {
    DisplayLockGuard lock(this);
    if (animation_timer_ != nullptr) {
        lv_timer_delete(animation_timer_);
        animation_timer_ = nullptr;
    }
    if (aurora_root_ != nullptr) {
        lv_obj_delete(aurora_root_);
        aurora_root_ = nullptr;
    }
}

void AuroraMax35Display::SetupAuroraUi() {
    DisplayLockGuard lock(this);

    auto* theme = static_cast<LvglTheme*>(current_theme_);
    const lv_font_t* text_font = theme != nullptr && theme->text_font() ? theme->text_font()->font() : LV_FONT_DEFAULT;

    lv_obj_t* screen = lv_screen_active();
    aurora_root_ = lv_obj_create(screen);
    lv_obj_set_size(aurora_root_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(aurora_root_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(aurora_root_, 0, 0);
    lv_obj_set_style_border_width(aurora_root_, 0, 0);
    lv_obj_set_style_pad_all(aurora_root_, 0, 0);
    lv_obj_set_style_bg_color(aurora_root_, lv_color_hex(kBg), 0);
    lv_obj_set_style_bg_opa(aurora_root_, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(aurora_root_, text_font, 0);
    lv_obj_set_style_text_color(aurora_root_, lv_color_hex(kText), 0);
    lv_obj_clear_flag(aurora_root_, LV_OBJ_FLAG_SCROLLABLE);

    // ---------------- Home page ----------------
    home_page_ = lv_obj_create(aurora_root_);
    lv_obj_set_size(home_page_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_radius(home_page_, 0, 0);
    lv_obj_set_style_border_width(home_page_, 0, 0);
    lv_obj_set_style_bg_opa(home_page_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_left(home_page_, 18, 0);
    lv_obj_set_style_pad_right(home_page_, 18, 0);
    lv_obj_set_style_pad_top(home_page_, 12, 0);
    lv_obj_set_style_pad_bottom(home_page_, 10, 0);
    lv_obj_set_flex_flow(home_page_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(home_page_, 6, 0);
    lv_obj_clear_flag(home_page_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* home_header = lv_obj_create(home_page_);
    lv_obj_set_width(home_header, LV_PCT(100));
    lv_obj_set_height(home_header, 28);
    lv_obj_set_style_bg_opa(home_header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(home_header, 0, 0);
    lv_obj_set_style_pad_all(home_header, 0, 0);
    lv_obj_set_flex_flow(home_header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(home_header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(home_header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* brand = lv_label_create(home_header);
    lv_label_set_text(brand, "AURORA  /  MAX35");
    lv_obj_set_style_text_color(brand, lv_color_hex(kAccent), 0);
    lv_obj_set_style_text_letter_space(brand, 1, 0);

    home_battery_label_ = lv_label_create(home_header);
    lv_label_set_text(home_battery_label_, "ML307  4G");
    lv_obj_set_style_text_color(home_battery_label_, lv_color_hex(kMuted), 0);

    lv_obj_t* home_center = lv_obj_create(home_page_);
    lv_obj_set_width(home_center, LV_PCT(100));
    lv_obj_set_flex_grow(home_center, 1);
    lv_obj_set_style_bg_opa(home_center, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(home_center, 0, 0);
    lv_obj_set_style_pad_all(home_center, 0, 0);
    lv_obj_set_flex_flow(home_center, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(home_center, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(home_center, 5, 0);
    lv_obj_clear_flag(home_center, LV_OBJ_FLAG_SCROLLABLE);

    core_outer_ = lv_obj_create(home_center);
    lv_obj_set_size(core_outer_, 108, 108);
    lv_obj_set_style_radius(core_outer_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(core_outer_, lv_color_hex(0x091728), 0);
    lv_obj_set_style_bg_opa(core_outer_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(core_outer_, 2, 0);
    lv_obj_set_style_border_color(core_outer_, lv_color_hex(kAccent), 0);
    lv_obj_set_style_border_opa(core_outer_, pulse_opa_, 0);
    lv_obj_set_style_pad_all(core_outer_, 0, 0);
    lv_obj_clear_flag(core_outer_, LV_OBJ_FLAG_SCROLLABLE);

    core_inner_ = lv_obj_create(core_outer_);
    lv_obj_set_size(core_inner_, 76, 76);
    lv_obj_center(core_inner_);
    lv_obj_set_style_radius(core_inner_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(core_inner_, lv_color_hex(0x102D43), 0);
    lv_obj_set_style_bg_opa(core_inner_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(core_inner_, 2, 0);
    lv_obj_set_style_border_color(core_inner_, lv_color_hex(kAccent2), 0);
    lv_obj_clear_flag(core_inner_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* core_text = lv_label_create(core_inner_);
    lv_label_set_text(core_text, "AI");
    lv_obj_set_style_text_color(core_text, lv_color_hex(kText), 0);
    lv_obj_set_style_text_letter_space(core_text, 2, 0);
    lv_obj_center(core_text);

    lv_obj_t* name = lv_label_create(home_center);
    lv_label_set_text(name, "AURORA");
    lv_obj_set_style_text_color(name, lv_color_hex(kText), 0);
    lv_obj_set_style_text_letter_space(name, 3, 0);

    home_state_label_ = lv_label_create(home_center);
    lv_label_set_text(home_state_label_, "正在启动");
    lv_obj_set_width(home_state_label_, LV_PCT(88));
    lv_obj_set_style_text_align(home_state_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(home_state_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(home_state_label_, lv_color_hex(kAccent), 0);

    home_emotion_label_ = lv_label_create(home_center);
    lv_label_set_text(home_emotion_label_, "neutral");
    lv_obj_set_style_text_color(home_emotion_label_, lv_color_hex(kMuted), 0);

    lv_obj_t* home_footer = lv_label_create(home_page_);
    lv_label_set_text(home_footer, "ML307 4G  ·  按键开始对话");
    lv_obj_set_width(home_footer, LV_PCT(100));
    lv_obj_set_style_text_align(home_footer, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(home_footer, lv_color_hex(kMuted), 0);

    // ---------------- Dialog page ----------------
    dialog_page_ = lv_obj_create(aurora_root_);
    lv_obj_set_size(dialog_page_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_radius(dialog_page_, 0, 0);
    lv_obj_set_style_border_width(dialog_page_, 0, 0);
    lv_obj_set_style_bg_color(dialog_page_, lv_color_hex(kBg), 0);
    lv_obj_set_style_bg_opa(dialog_page_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(dialog_page_, 14, 0);
    lv_obj_set_style_pad_right(dialog_page_, 14, 0);
    lv_obj_set_style_pad_top(dialog_page_, 10, 0);
    lv_obj_set_style_pad_bottom(dialog_page_, 10, 0);
    lv_obj_set_style_pad_row(dialog_page_, 8, 0);
    lv_obj_set_flex_flow(dialog_page_, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(dialog_page_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* dialog_header = lv_obj_create(dialog_page_);
    lv_obj_set_width(dialog_header, LV_PCT(100));
    lv_obj_set_height(dialog_header, 27);
    lv_obj_set_style_bg_opa(dialog_header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dialog_header, 0, 0);
    lv_obj_set_style_pad_all(dialog_header, 0, 0);
    lv_obj_set_flex_flow(dialog_header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dialog_header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(dialog_header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* dialog_title = lv_label_create(dialog_header);
    lv_label_set_text(dialog_title, "DIALOG");
    lv_obj_set_style_text_color(dialog_title, lv_color_hex(kAccent), 0);
    lv_obj_set_style_text_letter_space(dialog_title, 2, 0);

    dialog_state_label_ = lv_label_create(dialog_header);
    lv_label_set_text(dialog_state_label_, "正在聆听");
    lv_obj_set_style_text_color(dialog_state_label_, lv_color_hex(kMuted), 0);

    dialog_battery_label_ = lv_label_create(dialog_header);
    lv_label_set_text(dialog_battery_label_, "4G");
    lv_obj_set_style_text_color(dialog_battery_label_, lv_color_hex(kMuted), 0);

    lv_obj_t* user_card = lv_obj_create(dialog_page_);
    lv_obj_set_width(user_card, LV_PCT(100));
    lv_obj_set_height(user_card, 74);
    lv_obj_set_style_radius(user_card, 12, 0);
    lv_obj_set_style_border_width(user_card, 1, 0);
    lv_obj_set_style_border_color(user_card, lv_color_hex(0x24506A), 0);
    lv_obj_set_style_bg_color(user_card, lv_color_hex(kUser), 0);
    lv_obj_set_style_bg_opa(user_card, LV_OPA_80, 0);
    lv_obj_set_style_pad_left(user_card, 12, 0);
    lv_obj_set_style_pad_right(user_card, 12, 0);
    lv_obj_set_style_pad_top(user_card, 7, 0);
    lv_obj_set_style_pad_bottom(user_card, 7, 0);
    lv_obj_set_flex_flow(user_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(user_card, 3, 0);
    lv_obj_clear_flag(user_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* user_tag = lv_label_create(user_card);
    lv_label_set_text(user_tag, "YOU");
    lv_obj_set_style_text_color(user_tag, lv_color_hex(kAccent), 0);

    user_text_label_ = lv_label_create(user_card);
    lv_obj_set_width(user_text_label_, LV_PCT(100));
    lv_label_set_long_mode(user_text_label_, LV_LABEL_LONG_WRAP);
    lv_label_set_text(user_text_label_, "…");
    lv_obj_set_style_text_color(user_text_label_, lv_color_hex(kText), 0);

    lv_obj_t* assistant_card = lv_obj_create(dialog_page_);
    lv_obj_set_width(assistant_card, LV_PCT(100));
    lv_obj_set_flex_grow(assistant_card, 1);
    lv_obj_set_style_radius(assistant_card, 12, 0);
    lv_obj_set_style_border_width(assistant_card, 1, 0);
    lv_obj_set_style_border_color(assistant_card, lv_color_hex(0x243B62), 0);
    lv_obj_set_style_bg_color(assistant_card, lv_color_hex(kAssistant), 0);
    lv_obj_set_style_bg_opa(assistant_card, LV_OPA_90, 0);
    lv_obj_set_style_pad_left(assistant_card, 12, 0);
    lv_obj_set_style_pad_right(assistant_card, 12, 0);
    lv_obj_set_style_pad_top(assistant_card, 7, 0);
    lv_obj_set_style_pad_bottom(assistant_card, 8, 0);
    lv_obj_set_style_pad_row(assistant_card, 4, 0);
    lv_obj_set_flex_flow(assistant_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(assistant_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* assistant_tag = lv_label_create(assistant_card);
    lv_label_set_text(assistant_tag, "AURORA");
    lv_obj_set_style_text_color(assistant_tag, lv_color_hex(kAccent2), 0);

    assistant_view_ = lv_obj_create(assistant_card);
    lv_obj_set_width(assistant_view_, LV_PCT(100));
    lv_obj_set_flex_grow(assistant_view_, 1);
    lv_obj_set_style_radius(assistant_view_, 0, 0);
    lv_obj_set_style_border_width(assistant_view_, 0, 0);
    lv_obj_set_style_bg_opa(assistant_view_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(assistant_view_, 0, 0);
    lv_obj_set_scroll_dir(assistant_view_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(assistant_view_, LV_SCROLLBAR_MODE_ACTIVE);

    assistant_text_label_ = lv_label_create(assistant_view_);
    lv_obj_set_width(assistant_text_label_, LV_PCT(100));
    lv_obj_set_height(assistant_text_label_, LV_SIZE_CONTENT);
    lv_label_set_long_mode(assistant_text_label_, LV_LABEL_LONG_WRAP);
    lv_label_set_text(assistant_text_label_, "等待回答…");
    lv_obj_set_style_text_color(assistant_text_label_, lv_color_hex(kText), 0);
    lv_obj_align(assistant_text_label_, LV_ALIGN_TOP_LEFT, 0, 0);

    // ---------------- Toast ----------------
    toast_ = lv_obj_create(aurora_root_);
    lv_obj_set_width(toast_, LV_PCT(86));
    lv_obj_set_height(toast_, LV_SIZE_CONTENT);
    lv_obj_align(toast_, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_radius(toast_, 10, 0);
    lv_obj_set_style_border_width(toast_, 1, 0);
    lv_obj_set_style_border_color(toast_, lv_color_hex(kAccent), 0);
    lv_obj_set_style_bg_color(toast_, lv_color_hex(kPanelAlt), 0);
    lv_obj_set_style_bg_opa(toast_, LV_OPA_90, 0);
    lv_obj_set_style_pad_all(toast_, 8, 0);
    lv_obj_clear_flag(toast_, LV_OBJ_FLAG_SCROLLABLE);
    toast_label_ = lv_label_create(toast_);
    lv_obj_set_width(toast_label_, LV_PCT(100));
    lv_label_set_long_mode(toast_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(toast_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(toast_label_, "");
    lv_obj_add_flag(toast_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_flag(dialog_page_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(aurora_root_);

    animation_timer_ = lv_timer_create(AnimationTimerCallback, kAnimPeriodMs, this);
}

void AuroraMax35Display::ShowHomeLocked() {
    dialog_visible_ = false;
    pending_home_ = false;
    home_delay_ticks_ = 0;
    lv_obj_add_flag(dialog_page_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(home_page_, LV_OBJ_FLAG_HIDDEN);
}

void AuroraMax35Display::ShowDialogLocked() {
    dialog_visible_ = true;
    pending_home_ = false;
    home_delay_ticks_ = 0;
    lv_obj_add_flag(home_page_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(dialog_page_, LV_OBJ_FLAG_HIDDEN);
}

void AuroraMax35Display::ResetConversationLocked() {
    user_text_.clear();
    assistant_text_.clear();
    scroll_y_ = 0;
    max_scroll_y_ = 0;
    scroll_delay_ticks_ = kInitialScrollDelayTicks;
    scroll_complete_ = true;
    pending_home_ = false;
    home_delay_ticks_ = 0;
    lv_label_set_text(user_text_label_, "…");
    lv_label_set_text(assistant_text_label_, "等待回答…");
    lv_obj_scroll_to_y(assistant_view_, 0, LV_ANIM_OFF);
}

void AuroraMax35Display::AppendAssistantLocked(const char* content) {
    if (content == nullptr || content[0] == '\0') {
        return;
    }
    if (!assistant_text_.empty()) {
        assistant_text_ += "\n";
    }
    assistant_text_ += content;
    lv_label_set_text(assistant_text_label_, assistant_text_.c_str());
    RefreshScrollMetricsLocked(true);
}

void AuroraMax35Display::RefreshScrollMetricsLocked(bool text_was_appended) {
    if (assistant_view_ == nullptr || assistant_text_label_ == nullptr) {
        return;
    }
    lv_obj_update_layout(assistant_view_);
    lv_obj_update_layout(assistant_text_label_);

    const int viewport_h = std::max<int>(1, lv_obj_get_height(assistant_view_));
    const int text_h = std::max<int>(0, lv_obj_get_height(assistant_text_label_));
    const int old_max = max_scroll_y_;
    max_scroll_y_ = std::max(0, text_h - viewport_h + 4);

    if (max_scroll_y_ <= 0) {
        scroll_y_ = 0;
        scroll_complete_ = true;
        lv_obj_scroll_to_y(assistant_view_, 0, LV_ANIM_OFF);
        return;
    }

    if (scroll_y_ > max_scroll_y_) {
        scroll_y_ = max_scroll_y_;
    }

    if (text_was_appended && old_max == 0 && max_scroll_y_ > 0) {
        scroll_delay_ticks_ = kInitialScrollDelayTicks;
    } else if (text_was_appended && scroll_y_ >= old_max && max_scroll_y_ > old_max) {
        scroll_delay_ticks_ = kResumeScrollDelayTicks;
    }
    scroll_complete_ = (scroll_y_ >= max_scroll_y_);
}

void AuroraMax35Display::SetStatus(const char* status) {
    DisplayLockGuard lock(this);
    if (status == nullptr) {
        status = "";
    }
    last_status_ = status;
    last_status_update_time_ = std::chrono::system_clock::now();
    lv_label_set_text(home_state_label_, status);
    lv_label_set_text(dialog_state_label_, status);

    const DeviceState state = Application::GetInstance().GetDeviceState();
    if (state == kDeviceStateListening || state == kDeviceStateSpeaking) {
        if (!dialog_visible_) {
            ShowDialogLocked();
        }
        pending_home_ = false;
    } else if (state == kDeviceStateIdle || state == kDeviceStateUnknown) {
        if (dialog_visible_ && (!user_text_.empty() || !assistant_text_.empty())) {
            pending_home_ = true;
            if (scroll_complete_) {
                home_delay_ticks_ = kReturnHomeDelayTicks;
            }
        } else {
            ShowHomeLocked();
        }
    }
}

void AuroraMax35Display::ShowNotification(const std::string& notification, int duration_ms) {
    ShowNotification(notification.c_str(), duration_ms);
}

void AuroraMax35Display::ShowNotification(const char* notification, int duration_ms) {
    DisplayLockGuard lock(this);
    if (notification == nullptr || notification[0] == '\0') {
        lv_obj_add_flag(toast_, LV_OBJ_FLAG_HIDDEN);
        toast_ticks_ = 0;
        return;
    }
    lv_label_set_text(toast_label_, notification);
    lv_obj_remove_flag(toast_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(toast_);
    toast_ticks_ = std::max(1, duration_ms / kAnimPeriodMs);
}

void AuroraMax35Display::SetEmotion(const char* emotion) {
    DisplayLockGuard lock(this);
    last_emotion_ = (emotion == nullptr || emotion[0] == '\0') ? "neutral" : emotion;
    lv_label_set_text(home_emotion_label_, last_emotion_.c_str());
}

void AuroraMax35Display::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (role == nullptr) {
        role = "system";
    }
    if (content == nullptr) {
        content = "";
    }

    if (std::strcmp(role, "user") == 0) {
        ShowDialogLocked();
        user_text_ = content;
        assistant_text_.clear();
        lv_label_set_text(user_text_label_, user_text_.empty() ? "…" : user_text_.c_str());
        lv_label_set_text(assistant_text_label_, "等待回答…");
        scroll_y_ = 0;
        max_scroll_y_ = 0;
        scroll_delay_ticks_ = kInitialScrollDelayTicks;
        scroll_complete_ = true;
        pending_home_ = false;
        home_delay_ticks_ = 0;
        lv_obj_scroll_to_y(assistant_view_, 0, LV_ANIM_OFF);
        return;
    }

    if (std::strcmp(role, "assistant") == 0) {
        ShowDialogLocked();
        AppendAssistantLocked(content);
        pending_home_ = false;
        home_delay_ticks_ = 0;
        return;
    }

    // System messages are useful during boot/network/activation. Keep them on the home page
    // without creating hidden chat bubbles in the factory UI behind AURORA.
    if (content[0] != '\0' && !dialog_visible_) {
        lv_label_set_text(home_state_label_, content);
    }
}

void AuroraMax35Display::UpdateBatteryLocked(bool available, int level, bool charging) {
    char text[48];
    if (available) {
        std::snprintf(text, sizeof(text), "ML307  4G  ·  %d%%%s", level, charging ? "+" : "");
        lv_label_set_text(home_battery_label_, text);
        std::snprintf(text, sizeof(text), "4G  %d%%%s", level, charging ? "+" : "");
        lv_label_set_text(dialog_battery_label_, text);
    } else {
        lv_label_set_text(home_battery_label_, "ML307  4G");
        lv_label_set_text(dialog_battery_label_, "4G");
    }
}

void AuroraMax35Display::UpdateStatusBar(bool update_all) {
    // Keep factory status-bar side effects (network polling, low-battery warning sound, etc.)
    // even though the factory widgets themselves are hidden behind the AURORA root.
    LvglDisplay::UpdateStatusBar(update_all);

    int level = 0;
    bool charging = false;
    bool discharging = false;
    const bool available = Board::GetInstance().GetBatteryLevel(level, charging, discharging);

    DisplayLockGuard lock(this);
    UpdateBatteryLocked(available, level, charging);
}

void AuroraMax35Display::AnimationTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<AuroraMax35Display*>(lv_timer_get_user_data(timer));
    if (self != nullptr) {
        self->OnAnimationTick();
    }
}

void AuroraMax35Display::OnAnimationTick() {
    // Soft pulse on the home-page core.
    pulse_opa_ += pulse_step_;
    if (pulse_opa_ >= 245) {
        pulse_opa_ = 245;
        pulse_step_ = -5;
    } else if (pulse_opa_ <= 100) {
        pulse_opa_ = 100;
        pulse_step_ = 5;
    }
    if (core_outer_ != nullptr) {
        lv_obj_set_style_border_opa(core_outer_, static_cast<lv_opa_t>(pulse_opa_), 0);
    }

    if (toast_ticks_ > 0) {
        --toast_ticks_;
        if (toast_ticks_ == 0 && toast_ != nullptr) {
            lv_obj_add_flag(toast_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (!dialog_visible_ || assistant_text_.empty()) {
        return;
    }

    RefreshScrollMetricsLocked(false);
    if (max_scroll_y_ > 0 && scroll_y_ < max_scroll_y_) {
        if (scroll_delay_ticks_ > 0) {
            --scroll_delay_ticks_;
        } else {
            scroll_y_ = std::min(max_scroll_y_, scroll_y_ + kScrollPixelsPerTick);
            lv_obj_scroll_to_y(assistant_view_, scroll_y_, LV_ANIM_OFF);
            scroll_complete_ = (scroll_y_ >= max_scroll_y_);
        }
    } else {
        scroll_complete_ = true;
    }

    if (pending_home_ && scroll_complete_) {
        if (home_delay_ticks_ <= 0) {
            home_delay_ticks_ = kReturnHomeDelayTicks;
        } else {
            --home_delay_ticks_;
            if (home_delay_ticks_ == 0) {
                ShowHomeLocked();
                ResetConversationLocked();
            }
        }
    }
}
