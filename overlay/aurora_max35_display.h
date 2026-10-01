#pragma once

#include "lcd_display.h"

#include <lvgl.h>
#include <string>

class AuroraMax35Display : public SpiLcdDisplay {
public:
    AuroraMax35Display(esp_lcd_panel_io_handle_t panel_io,
                       esp_lcd_panel_handle_t panel,
                       int width,
                       int height,
                       int offset_x,
                       int offset_y,
                       bool mirror_x,
                       bool mirror_y,
                       bool swap_xy);
    ~AuroraMax35Display() override;

    void SetStatus(const char* status) override;
    void ShowNotification(const char* notification, int duration_ms = 3000) override;
    void ShowNotification(const std::string& notification, int duration_ms = 3000) override;
    void SetEmotion(const char* emotion) override;
    void SetChatMessage(const char* role, const char* content) override;
    void UpdateStatusBar(bool update_all = false) override;

private:
    static void AnimationTimerCallback(lv_timer_t* timer);

    void SetupAuroraUi();
    void ShowHomeLocked();
    void ShowDialogLocked();
    void ResetConversationLocked();
    void AppendAssistantLocked(const char* content);
    void RefreshScrollMetricsLocked(bool text_was_appended);
    void OnAnimationTick();
    void UpdateBatteryLocked(bool available, int level, bool charging);

    lv_obj_t* aurora_root_ = nullptr;
    lv_obj_t* home_page_ = nullptr;
    lv_obj_t* dialog_page_ = nullptr;

    lv_obj_t* home_state_label_ = nullptr;
    lv_obj_t* home_emotion_label_ = nullptr;
    lv_obj_t* home_battery_label_ = nullptr;
    lv_obj_t* core_outer_ = nullptr;
    lv_obj_t* core_inner_ = nullptr;

    lv_obj_t* dialog_state_label_ = nullptr;
    lv_obj_t* dialog_battery_label_ = nullptr;
    lv_obj_t* user_text_label_ = nullptr;
    lv_obj_t* assistant_view_ = nullptr;
    lv_obj_t* assistant_text_label_ = nullptr;

    lv_obj_t* toast_ = nullptr;
    lv_obj_t* toast_label_ = nullptr;

    lv_timer_t* animation_timer_ = nullptr;

    std::string user_text_;
    std::string assistant_text_;
    std::string last_status_;
    std::string last_emotion_;

    bool dialog_visible_ = false;
    bool pending_home_ = false;
    bool scroll_complete_ = true;
    int scroll_y_ = 0;
    int max_scroll_y_ = 0;
    int scroll_delay_ticks_ = 0;
    int home_delay_ticks_ = 0;
    int toast_ticks_ = 0;
    int pulse_opa_ = 150;
    int pulse_step_ = 5;
};
