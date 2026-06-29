/*
 * S800 network clock course project
 * Student: JIANGSJ (Jiang Shengji)
 * Student ID: 523170910011
 *
 * UART0 protocol (115200, 8-N-1):
 *   GET
 *   SET TIME HH:MM:SS
 *   SET ALARM HH:MM
 *   ALARM ON
 *   ALARM OFF
 *   STOP
 */

#include <stdint.h>
#include <stdbool.h>
#include "hw_memmap.h"
#include "hw_ints.h"
#include "gpio.h"
#include "i2c.h"
#include "interrupt.h"
#include "pin_map.h"
#include "pwm.h"
#include "sysctl.h"
#include "systick.h"
#include "timer.h"
#include "uart.h"

#define SYSTICK_FREQUENCY       1000U
#define UART_RX_BUFFER_SIZE     64U
#define SCROLL_BUFFER_SIZE      32U

#define LED_HEARTBEAT           0x01U
#define LED_ALARM_ENABLED       0x02U
#define LED_ALARM_RINGING       0x04U
#define LED_DISPLAY_DATE        0x08U
#define LED_UART_ACTIVITY       0x10U
#define LED_FORMAT_RIGHT        0x20U
#define LED_DISPLAY_OFF         0x40U
#define LED_SCROLL_ACTIVE       0x80U

#define WEATHER_NONE            0U
#define WEATHER_SUN             1U
#define WEATHER_RAIN            2U
#define WEATHER_CLOUD           3U
#define WEATHER_HOT             4U

#define FOCUS_BEEP_MS           1500U

#define EDIT_NONE               0U
#define EDIT_DATE               1U
#define EDIT_TIME               2U
#define EDIT_ALARM              3U
#define KEY_LONG_MS             1000U
#define KEY_ADD_REPEAT_START_MS 600U
#define KEY_ADD_REPEAT_MS       200U

#define TCA6424_I2CADDR         0x22U
#define TCA6424_INPUT_PORT0     0x00U
#define TCA6424_OUTPUT_PORT1    0x05U
#define TCA6424_OUTPUT_PORT2    0x06U
#define TCA6424_CONFIG_PORT0    0x0cU
#define TCA6424_CONFIG_PORT1    0x0dU
#define TCA6424_CONFIG_PORT2    0x0eU

#define PCA9557_I2CADDR         0x18U
#define PCA9557_OUTPUT          0x01U
#define PCA9557_CONFIG          0x03U

/*
 * S800 schematic labels the buzzer control net PWM7. The common LaunchPad
 * mapping for TM4C1294NCPDT is PK5/M0PWM7. Change these four definitions if
 * the board jumper
 * routes PWM7 to another MCU pin.
 */
#define BUZZER_GPIO_PERIPH      SYSCTL_PERIPH_GPIOK
#define BUZZER_GPIO_BASE        GPIO_PORTK_BASE
#define BUZZER_GPIO_PIN         GPIO_PIN_5
#define BUZZER_GPIO_CONFIG      GPIO_PK5_M0PWM7
#define BUZZER_PWM_PERIPH       SYSCTL_PERIPH_PWM0
#define BUZZER_PWM_BASE         PWM0_BASE
#define BUZZER_PWM_GEN          PWM_GEN_3
#define BUZZER_PWM_OUT          PWM_OUT_7
#define BUZZER_PWM_OUT_BIT      PWM_OUT_7_BIT
#define BUZZER_PWM_CLOCK_HZ     (20000000U / 64U)
#define BUZZER_DEFAULT_FREQ_HZ  1000U
#define BUZZER_ALARM_FREQ_HZ    600U
#define BUZZER_NORMAL_DUTY_DIV  2U
#define BUZZER_MUSIC_DUTY_DIV   2U

#define NOTE_REST               0U

typedef struct {
    uint16_t freq;
    uint16_t duration_ms;
} MusicNote;

static const uint8_t g_segment_code[11] = {
    0x3fU, 0x06U, 0x5bU, 0x4fU, 0x66U, 0x6dU,
    0x7dU, 0x07U, 0x7fU, 0x6fU, 0x00U
};

static const uint8_t g_startup_student_id[8] = {
    0x07U, 0x3fU, 0x6fU, 0x06U, 0x3fU, 0x3fU, 0x06U, 0x06U
};

static const uint8_t g_startup_name[8] = {
    0x00U, 0x1eU, 0x06U, 0x77U, 0x54U, 0x3dU, 0x6dU, 0x1eU
};

static const uint8_t g_startup_version[8] = {
    0x00U, 0x00U, 0x3eU, 0x86U, 0x3fU, 0x00U, 0x00U, 0x00U
};

static const MusicNote g_music_wind[] = {
    {NOTE_REST, 750U}, {370U, 188U}, {392U, 188U}, {440U, 188U},
    {494U, 188U}, {247U, 375U}, {588U, 188U}, {494U, 188U},
    {NOTE_REST, 22U}, {494U, 1500U}, {370U, 188U}, {392U, 188U},
    {440U, 188U}, {494U, 188U}, {220U, 375U}, {588U, 188U},
    {494U, 188U}, {440U, 188U}, {494U, 188U}, {392U, 188U},
    {440U, 188U}, {370U, 188U}, {392U, 188U}, {294U, 375U},
    {370U, 188U}, {392U, 188U}, {440U, 188U}, {494U, 188U},
    {247U, 375U}, {588U, 188U}, {494U, 188U}, {NOTE_REST, 11U},
    {494U, 1500U}, {370U, 188U}, {392U, 188U}, {440U, 188U},
    {494U, 188U}, {220U, 375U}, {588U, 188U}, {494U, 188U},
    {440U, 188U}, {494U, 188U}, {392U, 188U}, {440U, 188U},
    {370U, 188U}, {392U, 188U}, {294U, 750U}, {NOTE_REST, 188U},
    {220U, 375U}, {220U, 188U}, {196U, 188U}, {220U, 375U},
    {220U, 188U}, {196U, 188U}, {220U, 375U}, {247U, 375U},
    {294U, 375U}, {247U, 375U}, {220U, 375U}, {220U, 188U},
    {196U, 188U}, {220U, 375U}, {220U, 188U}, {196U, 188U},
    {220U, 188U}, {247U, 188U}, {220U, 188U}, {196U, 188U},
    {147U, 750U}, {220U, 375U}, {220U, 188U}, {196U, 188U},
    {220U, 375U}, {220U, 188U}, {196U, 188U}, {220U, 375U},
    {247U, 375U}, {294U, 375U}, {247U, 375U}, {220U, 375U},
    {220U, 188U}, {247U, 188U}, {220U, 375U}, {196U, 188U},
    {220U, 188U}, {NOTE_REST, 22U}, {220U, 750U}, {NOTE_REST, 750U},
    {NOTE_REST, 375U}, {220U, 375U}, {220U, 188U}, {196U, 188U},
    {220U, 375U}, {220U, 188U}, {196U, 188U}, {220U, 375U},
    {247U, 375U}, {294U, 375U}, {247U, 375U}, {220U, 375U},
    {220U, 188U}, {247U, 188U}, {220U, 375U}, {196U, 188U},
    {165U, 188U}, {NOTE_REST, 22U}, {165U, 750U}, {247U, 188U},
    {220U, 188U}, {196U, 188U}, {220U, 188U}, {196U, 750U},
    {247U, 188U}, {220U, 188U}, {196U, 188U}, {220U, 188U},
    {196U, 750U}, {247U, 188U}, {220U, 188U}, {196U, 188U},
    {220U, 188U}, {196U, 750U}, {NOTE_REST, 750U}, {NOTE_REST, 375U},
    {196U, 375U}, {220U, 375U}, {247U, 375U}, {196U, 375U},
    {330U, 375U}, {294U, 188U}, {330U, 188U}, {NOTE_REST, 22U},
    {330U, 375U}, {330U, 188U}, {196U, 188U}, {370U, 375U},
    {330U, 188U}, {370U, 188U}, {NOTE_REST, 22U}, {370U, 750U},
    {NOTE_REST, 22U}, {370U, 375U}, {330U, 188U}, {370U, 188U},
    {NOTE_REST, 22U}, {370U, 188U}, {247U, 188U}, {NOTE_REST, 22U},
    {247U, 375U}, {392U, 188U}, {440U, 188U}, {392U, 188U},
    {370U, 188U}, {330U, 375U}, {294U, 375U}, {330U, 375U},
    {294U, 188U}, {330U, 188U}, {NOTE_REST, 22U}, {330U, 188U},
    {294U, 188U}, {330U, 188U}, {294U, 188U}, {330U, 375U},
    {294U, 188U}, {220U, 188U}, {NOTE_REST, 22U}, {220U, 188U},
    {294U, 375U}, {294U, 188U}, {247U, 750U}, {NOTE_REST, 750U},
    {NOTE_REST, 188U}, {196U, 375U}, {220U, 375U}, {247U, 375U},
    {NOTE_REST, 22U}, {196U, 375U}, {330U, 375U}, {294U, 188U},
    {330U, 188U}, {NOTE_REST, 22U}, {330U, 375U}, {330U, 188U},
    {196U, 188U}, {370U, 375U}, {330U, 188U}, {370U, 188U},
    {NOTE_REST, 22U}, {370U, 750U}, {NOTE_REST, 22U}, {370U, 375U},
    {330U, 188U}, {370U, 188U}, {NOTE_REST, 22U}, {370U, 188U},
    {247U, 188U}, {NOTE_REST, 22U}, {247U, 375U}, {392U, 188U},
    {440U, 188U}, {392U, 188U}, {370U, 188U}, {330U, 375U},
    {294U, 375U}, {330U, 375U}, {494U, 188U}, {NOTE_REST, 22U},
    {494U, 188U}, {NOTE_REST, 22U}, {494U, 375U}, {294U, 375U},
    {330U, 375U}, {494U, 188U}, {NOTE_REST, 22U}, {494U, 188U},
    {NOTE_REST, 22U}, {494U, 188U}, {294U, 375U}, {330U, 188U},
    {NOTE_REST, 22U}, {330U, 1500U}, {NOTE_REST, 750U}, {392U, 375U},
    {440U, 375U}, {494U, 375U}, {660U, 188U}, {588U, 188U},
    {NOTE_REST, 22U}, {588U, 375U}, {660U, 188U}, {588U, 188U},
    {NOTE_REST, 22U}, {588U, 375U}, {660U, 188U}, {588U, 188U},
    {NOTE_REST, 22U}, {588U, 375U}, {440U, 188U}, {494U, 188U},
    {NOTE_REST, 22U}, {494U, 375U}, {660U, 188U}, {588U, 188U},
    {NOTE_REST, 22U}, {588U, 375U}, {660U, 188U}, {588U, 188U},
    {NOTE_REST, 22U}, {588U, 375U}, {660U, 188U}, {588U, 188U},
    {NOTE_REST, 22U}, {588U, 188U}, {494U, 375U}, {494U, 188U},
    {440U, 375U}, {392U, 188U}, {330U, 188U}, {NOTE_REST, 22U},
    {330U, 188U}, {392U, 375U}, {NOTE_REST, 22U}, {392U, 188U},
    {440U, 375U}, {392U, 188U}, {330U, 188U}, {NOTE_REST, 22U},
    {330U, 188U}, {392U, 375U}, {494U, 750U}, {NOTE_REST, 22U},
    {494U, 188U}, {524U, 188U}, {494U, 188U}, {440U, 188U},
    {494U, 188U}, {440U, 375U}, {440U, 188U}, {392U, 375U},
    {440U, 375U}, {494U, 375U}, {660U, 188U}, {588U, 188U},
    {NOTE_REST, 22U}, {588U, 375U}, {660U, 188U}, {588U, 188U},
    {NOTE_REST, 22U}, {588U, 375U}, {660U, 188U}, {588U, 188U},
    {NOTE_REST, 22U}, {588U, 375U}, {440U, 375U}, {494U, 375U},
    {660U, 188U}, {588U, 188U}, {NOTE_REST, 22U}, {588U, 375U},
    {660U, 188U}, {588U, 188U}, {NOTE_REST, 22U}, {588U, 375U},
    {660U, 188U}, {588U, 188U}, {NOTE_REST, 22U}, {588U, 188U},
    {494U, 375U}, {494U, 188U}, {440U, 375U}, {392U, 188U},
    {330U, 188U}, {NOTE_REST, 22U}, {330U, 188U}, {494U, 375U},
    {494U, 188U}, {440U, 375U}, {392U, 188U}, {330U, 188U},
    {NOTE_REST, 22U}, {330U, 188U}, {NOTE_REST, 22U}, {330U, 188U},
    {392U, 375U}, {NOTE_REST, 22U}, {392U, 3000U}, {392U, 1500U},
    {330U, 188U}, {494U, 375U}, {494U, 188U}, {440U, 375U},
    {392U, 188U}, {330U, 188U}, {NOTE_REST, 22U}, {330U, 188U},
    {494U, 375U}, {494U, 188U}, {440U, 375U}, {392U, 188U},
    {330U, 188U}, {NOTE_REST, 22U}, {330U, 188U}, {392U, 375U},
    {392U, 188U}, {NOTE_REST, 22U}, {392U, 3000U}, {392U, 1500U},
    {NOTE_REST, 750U}
};

static volatile uint32_t g_milliseconds;
static volatile bool g_keys_due;
static volatile bool g_second_due;
static volatile char g_uart_rx[UART_RX_BUFFER_SIZE];
static volatile uint8_t g_uart_rx_length;
static volatile bool g_command_ready;
static volatile bool g_uart_rx_overflow;

static uint32_t g_system_clock;
static uint8_t g_hour = 12U;
static uint8_t g_minute;
static uint8_t g_second;
static uint16_t g_year = 2026U;
static uint8_t g_month = 6U;
static uint8_t g_day = 4U;
static uint8_t g_display_mode;
static bool g_display_enabled = true;
static uint8_t g_alarm_hour = 12U;
static uint8_t g_alarm_minute = 1U;
static uint8_t g_alarm_second;
static bool g_alarm_enabled;
static bool g_alarm_ringing;
static bool g_alarm_match_latched;
static bool g_equal_time_latched;
static volatile bool g_buzzer_enabled;
static volatile bool g_buzzer_level;
static uint16_t g_buzzer_current_freq = BUZZER_DEFAULT_FREQ_HZ;
static uint32_t g_alarm_started_ms;
static uint32_t g_manual_beep_until_ms;
static uint32_t g_uart_activity_until_ms;
static bool g_time_sync_ok;
static uint32_t g_time_sync_until_ms;
static uint8_t g_display_position;
static bool g_focus_active;
static uint8_t g_focus_duration_minutes = 40U;
static uint32_t g_focus_remaining_seconds;
static uint16_t g_focus_completed_count;
static uint8_t g_stable_keys = 0xffU;
static uint8_t g_raw_keys = 0xffU;
static uint8_t g_key_stable_count;
static uint8_t g_stable_user_keys = 0x03U;
static uint8_t g_raw_user_keys = 0x03U;
static uint8_t g_user_key_stable_count;
static bool g_startup_complete;
static uint8_t g_startup_phase = 0xffU;
static bool g_format_right;
static bool g_display_flip;
static bool g_night_mode;
static bool g_scroll_fast;
static uint8_t g_scroll_codes[SCROLL_BUFFER_SIZE];
static char g_scroll_chars[SCROLL_BUFFER_SIZE];
static uint8_t g_scroll_length;
static uint8_t g_scroll_offset;
static uint32_t g_scroll_next_ms;
static uint8_t g_weather_codes[8];
static char g_weather_chars[8];
static uint8_t g_weather_length;
static uint8_t g_weather_condition;
static uint32_t g_weather_show_until_ms;
static uint8_t g_led_last_output = 0xffU;
static bool g_led_override;
static uint8_t g_led_override_value;
static uint8_t g_edit_mode;
static uint8_t g_edit_field;
static uint32_t g_edit_last_key_ms;
static uint16_t g_edit_year;
static uint8_t g_edit_month;
static uint8_t g_edit_day;
static uint8_t g_edit_hour;
static uint8_t g_edit_minute;
static uint8_t g_edit_second;
static uint8_t g_edit_alarm_hour;
static uint8_t g_edit_alarm_minute;
static uint8_t g_edit_alarm_second;
static bool g_edit_alarm_touched;
static uint8_t g_edit_display_phase = 0xffU;
static uint32_t g_key_down_ms[8];
static uint32_t g_add_next_repeat_ms;
static uint8_t g_key_long_sent;
static bool g_save_key_latched;
static const MusicNote *g_music_notes;
static uint16_t g_music_length;
static uint16_t g_music_index;
static uint16_t g_music_note_elapsed_ms;
static uint16_t g_music_note_duration_ms;
static bool g_music_active;
static bool g_music_finished_event;

static void ClockTick(void);
static void FocusUpdate(void);
static void FocusStart(void);
static void FocusStop(bool completed);
static void AlarmUpdate(void);
static void AlarmCheckTrigger(void);
static void AlarmLatchCurrentMatch(void);
static void EqualTimeCheckTrigger(void);
static void MusicStart(const MusicNote *notes, uint16_t length);
static void MusicStop(void);
static void MusicUpdate(void);
static void MusicLoadNextNote(void);
static void LEDUpdate(void);
static void ScrollUpdate(void);
static void ScrollSetMessage(const volatile char *text);
static void WeatherSetMessage(uint8_t temp, uint8_t condition);
static uint8_t CharacterToSegment(char value);
static uint8_t RotateSegment180(uint8_t code);
static bool IsLeapYear(uint16_t year);
static uint8_t DaysInMonth(uint16_t year, uint8_t month);
static void DateIncrement(void);
static void StartupUpdate(void);
static void ProcessKeys(void);
static void HandleKeyPress(uint8_t pressed, uint8_t user_pressed);
static void KeyOnPressed(uint8_t key_index, bool long_press);
static void EditBegin(void);
static void EditCancel(void);
static void EditSave(void);
static void EditAdd(void);
static void ProcessCommand(void);
static void SendState(void);
static void SendDisplayEvent(void);
static void SendLedEvent(void);
static void SendKeyEvent(const char *name);
static void SendModeEvent(void);
static void SendFlipEvent(void);
static void SendFocusEvent(const char *state);
static void DisplayScan(void);
static void BuzzerDuty(uint8_t duty_div);
static void BuzzerTone(uint16_t freq);
static void BuzzerSet(bool enabled);
static void BuzzerInit(void);
static void MusicTimerInit(void);
static void S800GPIOInit(void);
static void S800I2CInit(void);
static void S800UARTInit(void);
static uint8_t I2C0ReadByte(uint8_t device, uint8_t reg);
static uint8_t I2C0WriteByte(uint8_t device, uint8_t reg, uint8_t data);
static void UARTPutString(const char *text);
static void UARTPut2(uint8_t value);
static void UARTPutTimeText(uint8_t hour, uint8_t minute, uint8_t second);
static void UARTPutDateText(uint16_t year, uint8_t month, uint8_t day);
static void UARTPut4(uint16_t value);
static void UARTPutHex(uint8_t value);
static void UARTPutUInt(uint32_t value);
static bool Parse2(const volatile char *text, uint8_t *value);
static bool Parse4(const volatile char *text, uint16_t *value);
static bool ParseUInt(const volatile char *text, uint16_t *value);
static bool ParseHex2(const volatile char *text, uint8_t *value);
static bool ProcessDateSet(void);
static bool ProcessTimeSet(void);
static bool ProcessAlarmSet(void);
static bool ProcessWeatherSet(void);
static bool CommandEquals(const char *text);

int main(void)
{
    g_system_clock = SysCtlClockFreqSet(
        SYSCTL_XTAL_16MHZ | SYSCTL_OSC_INT | SYSCTL_USE_PLL |
        SYSCTL_CFG_VCO_480, 20000000U);

    S800GPIOInit();
    S800I2CInit();
    S800UARTInit();
    BuzzerInit();
    MusicTimerInit();

    SysTickPeriodSet(g_system_clock / SYSTICK_FREQUENCY);
    SysTickIntEnable();
    SysTickEnable();
    IntMasterEnable();

    UARTPutString("\r\nS800 Network Clock - JIANGSJ 523170910011\r\n");
    UARTPutString("Type GET for current state.\r\n");
    SendState();

    while (1) {
        StartupUpdate();
        if (g_edit_mode != EDIT_NONE &&
            (g_milliseconds - g_edit_last_key_ms) >= 5000U) {
            EditCancel();
        }
        AlarmUpdate();
        MusicUpdate();
        ScrollUpdate();
        LEDUpdate();
        DisplayScan();
        if (g_edit_mode != EDIT_NONE) {
            uint8_t phase = (uint8_t)((g_milliseconds / 250U) & 1U);
            if (phase != g_edit_display_phase) {
                g_edit_display_phase = phase;
                SendDisplayEvent();
            }
        } else {
            g_edit_display_phase = 0xffU;
        }
        if (g_keys_due) {
            g_keys_due = false;
            ProcessKeys();
        }
        if (g_second_due) {
            g_second_due = false;
            ClockTick();
            SendDisplayEvent();
            if (g_focus_active) {
                SendFocusEvent("ON");
            }
        }
        if (g_command_ready) {
            ProcessCommand();
        }
    }
}

static void StartupUpdate(void)
{
    uint8_t phase;
    uint8_t led_output;

    if (g_startup_complete) {
        return;
    }

    if (g_milliseconds < 500U) {
        phase = 0U;
        led_output = 0x00U;
    } else if (g_milliseconds < 1000U) {
        phase = 1U;
        led_output = 0xffU;
    } else if (g_milliseconds < 1500U) {
        phase = 2U;
        led_output = 0x00U;
    } else if (g_milliseconds < 2000U) {
        phase = 3U;
        led_output = 0xffU;
    } else if (g_milliseconds < 3000U) {
        phase = 4U;
        led_output = 0x00U;
    } else if (g_milliseconds < 3500U) {
        phase = 5U;
        led_output = 0xffU;
    } else if (g_milliseconds < 4500U) {
        phase = 6U;
        led_output = 0x00U;
    } else if (g_milliseconds < 5000U) {
        phase = 7U;
        led_output = 0xffU;
    } else if (g_milliseconds < 6000U) {
        phase = 8U;
        led_output = 0xffU;
    } else {
        g_startup_complete = true;
        g_led_last_output = 0x00U;
        SendDisplayEvent();
        return;
    }

    if (phase != g_startup_phase) {
        g_startup_phase = phase;
        g_led_last_output = led_output;
        I2C0WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, led_output);
        SendDisplayEvent();
        SendLedEvent();
    }
}

static void ClockTick(void)
{
    g_second++;
    if (g_second >= 60U) {
        g_second = 0U;
        g_minute++;
        if (g_minute >= 60U) {
            g_minute = 0U;
            g_hour++;
            if (g_hour >= 24U) {
                g_hour = 0U;
                DateIncrement();
            }
        }
    }

    FocusUpdate();
    AlarmCheckTrigger();
    EqualTimeCheckTrigger();
}

static void FocusStart(void)
{
    g_focus_active = true;
    g_focus_remaining_seconds = (uint32_t)g_focus_duration_minutes * 60U;
    SendDisplayEvent();
    SendFocusEvent("ON");
}

static void FocusStop(bool completed)
{
    g_focus_active = false;
    g_focus_remaining_seconds = 0U;
    if (completed) {
        g_focus_completed_count++;
        MusicStop();
        g_manual_beep_until_ms = g_milliseconds + FOCUS_BEEP_MS;
        BuzzerTone(BUZZER_ALARM_FREQ_HZ);
        BuzzerSet(true);
        SendDisplayEvent();
        SendFocusEvent("DONE");
    } else {
        SendDisplayEvent();
        SendFocusEvent("OFF");
    }
}

static void FocusUpdate(void)
{
    if (!g_focus_active) {
        return;
    }
    if (g_focus_remaining_seconds > 0U) {
        g_focus_remaining_seconds--;
    }
    if (g_focus_remaining_seconds == 0U) {
        FocusStop(true);
    }
}

static void AlarmCheckTrigger(void)
{
    bool matched = g_alarm_enabled &&
                   g_hour == g_alarm_hour &&
                   g_minute == g_alarm_minute &&
                   g_second == g_alarm_second;

    if (!matched) {
        g_alarm_match_latched = false;
        return;
    }
    if (!g_alarm_ringing && !g_alarm_match_latched) {
        g_alarm_match_latched = true;
        MusicStop();
        g_alarm_ringing = true;
        g_alarm_started_ms = g_milliseconds;
        UARTPutString("#EVT:ALARM\r\n");
    }
}

static void AlarmLatchCurrentMatch(void)
{
    g_alarm_match_latched = g_alarm_enabled &&
                            g_hour == g_alarm_hour &&
                            g_minute == g_alarm_minute &&
                            g_second == g_alarm_second;
}

static void EqualTimeCheckTrigger(void)
{
    bool matched = (g_hour == g_minute) && (g_minute == g_second);

    if (!matched) {
        g_equal_time_latched = false;
        return;
    }
    if (g_equal_time_latched) {
        return;
    }
    g_equal_time_latched = true;
    if (g_music_active) {
        return;
    }
    if (!g_alarm_ringing) {
        MusicStop();
        g_alarm_ringing = true;
        g_alarm_started_ms = g_milliseconds;
        UARTPutString("#EVT:ALARM\r\n");
        UARTPutString("#EVT:EQUAL ");
        UARTPutTimeText(g_hour, g_minute, g_second);
        UARTPutString("\r\n");
    }
}

static void MusicStart(const MusicNote *notes, uint16_t length)
{
    if (g_alarm_ringing || length == 0U || notes == 0) {
        return;
    }
    MusicStop();
    g_manual_beep_until_ms = 0U;
    g_music_notes = notes;
    g_music_length = length;
    g_music_index = 0U;
    g_music_note_elapsed_ms = 0U;
    g_music_note_duration_ms = 0U;
    g_music_finished_event = false;
    g_music_active = true;
    MusicLoadNextNote();
    TimerLoadSet(TIMER1_BASE, TIMER_A, (g_system_clock / 1000U) - 1U);
    TimerEnable(TIMER1_BASE, TIMER_A);
    UARTPutString("#EVT:MUSIC ON\r\n");
}

static void MusicStop(void)
{
    if (g_music_active) {
        UARTPutString("#EVT:MUSIC OFF\r\n");
    }
    g_music_active = false;
    g_music_notes = 0;
    g_music_length = 0U;
    g_music_index = 0U;
    g_music_note_elapsed_ms = 0U;
    g_music_note_duration_ms = 0U;
    TimerDisable(TIMER1_BASE, TIMER_A);
    BuzzerSet(false);
    BuzzerDuty(BUZZER_NORMAL_DUTY_DIV);
}

static void MusicUpdate(void)
{
    if (g_music_active && g_alarm_ringing) {
        MusicStop();
        return;
    }
    if (g_music_finished_event) {
        g_music_finished_event = false;
        UARTPutString("#EVT:MUSIC OFF\r\n");
    }
}

static void MusicLoadNextNote(void)
{
    MusicNote note;

    if (!g_music_active || g_music_notes == 0 ||
        g_music_index >= g_music_length) {
        g_music_active = false;
        TimerDisable(TIMER1_BASE, TIMER_A);
        BuzzerSet(false);
        g_music_finished_event = true;
        return;
    }
    note = g_music_notes[g_music_index++];
    g_music_note_elapsed_ms = 0U;
    g_music_note_duration_ms = note.duration_ms;
    if (note.freq == NOTE_REST) {
        BuzzerSet(false);
    } else {
        BuzzerTone(note.freq);
        BuzzerSet(true);
    }
}

static void AlarmUpdate(void)
{
    uint32_t elapsed;

    if (g_manual_beep_until_ms != 0U &&
        (int32_t)(g_milliseconds - g_manual_beep_until_ms) >= 0) {
        g_manual_beep_until_ms = 0U;
        if (!g_alarm_ringing) {
            BuzzerSet(false);
        }
    }
    if (!g_alarm_ringing) {
        return;
    }
    elapsed = g_milliseconds - g_alarm_started_ms;
    if (elapsed >= 10000U) {
        g_alarm_ringing = false;
        g_alarm_match_latched = true;
        BuzzerSet(false);
        UARTPutString("#EVT:ALARM_OFF\r\n");
        return;
    }
    BuzzerTone(BUZZER_ALARM_FREQ_HZ);
    BuzzerSet((elapsed % 1000U) < 500U);
}

static void LEDUpdate(void)
{
    uint8_t value = g_led_override ? g_led_override_value : 0U;
    uint8_t output;

    if (!g_startup_complete) {
        return;
    }
    if (((g_milliseconds / 500U) & 1U) != 0U) {
        value |= LED_HEARTBEAT;
    }
    if (g_alarm_enabled) {
        value |= LED_ALARM_ENABLED;
    }
    if (g_alarm_ringing) {
        value |= LED_ALARM_RINGING;
    }
    if ((g_time_sync_ok &&
         (int32_t)(g_time_sync_until_ms - g_milliseconds) > 0) ||
        g_edit_mode != EDIT_NONE) {
        value |= LED_DISPLAY_DATE;
    }
    if ((int32_t)(g_uart_activity_until_ms - g_milliseconds) > 0) {
        value |= LED_UART_ACTIVITY;
    }
    if (g_format_right) {
        value |= LED_FORMAT_RIGHT;
    }
    if (!g_display_enabled) {
        value |= LED_DISPLAY_OFF;
    }
    if (g_scroll_length > 8U) {
        value |= LED_SCROLL_ACTIVE;
    }
    if (g_weather_condition == WEATHER_SUN) {
        value |= LED_FORMAT_RIGHT;
    } else if (g_weather_condition == WEATHER_RAIN) {
        value |= LED_DISPLAY_OFF;
    } else if (g_weather_condition == WEATHER_HOT) {
        value |= LED_SCROLL_ACTIVE;
    }
    if (g_night_mode) {
        value &= (LED_HEARTBEAT | LED_ALARM_ENABLED | LED_ALARM_RINGING);
    }
    output = (uint8_t)~value;
    if (output != g_led_last_output) {
        g_led_last_output = output;
        I2C0WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, output);
        SendLedEvent();
    }
}

static void ScrollUpdate(void)
{
    uint32_t interval = g_scroll_fast ? 200U : 500U;

    if (g_scroll_length <= 8U || g_milliseconds < g_scroll_next_ms) {
        return;
    }
    g_scroll_next_ms = g_milliseconds + interval;
    if (g_format_right) {
        if (g_scroll_offset == 0U) {
            g_scroll_offset = (uint8_t)(g_scroll_length - 1U);
        } else {
            g_scroll_offset--;
        }
    } else {
        g_scroll_offset = (uint8_t)((g_scroll_offset + 1U) % g_scroll_length);
    }
    SendDisplayEvent();
}

static uint8_t CharacterToSegment(char value)
{
    if (value >= 'a' && value <= 'z') {
        value = (char)(value - 'a' + 'A');
    }
    if (value >= '0' && value <= '9') {
        return g_segment_code[(uint8_t)(value - '0')];
    }
    switch (value) {
    case 'A': return 0x77U;
    case 'B': return 0x7cU;
    case 'C': return 0x39U;
    case 'D': return 0x5eU;
    case 'E': return 0x79U;
    case 'F': return 0x71U;
    case 'H': return 0x76U;
    case 'I': return 0x06U;
    case 'J': return 0x1eU;
    case 'L': return 0x38U;
    case 'N': return 0x54U;
    case 'O': return 0x3fU;
    case 'P': return 0x73U;
    case 'R': return 0x50U;
    case 'S': return 0x6dU;
    case 'T': return 0x78U;
    case 'U': return 0x3eU;
    case 'Y': return 0x6eU;
    case '-': return 0x40U;
    default: return 0x00U;
    }
}

static uint8_t RotateSegment180(uint8_t code)
{
    uint8_t rotated = (uint8_t)(code & 0x80U);

    if ((code & 0x01U) != 0U) {
        rotated |= 0x08U;
    }
    if ((code & 0x08U) != 0U) {
        rotated |= 0x01U;
    }
    if ((code & 0x02U) != 0U) {
        rotated |= 0x10U;
    }
    if ((code & 0x10U) != 0U) {
        rotated |= 0x02U;
    }
    if ((code & 0x04U) != 0U) {
        rotated |= 0x20U;
    }
    if ((code & 0x20U) != 0U) {
        rotated |= 0x04U;
    }
    if ((code & 0x40U) != 0U) {
        rotated |= 0x40U;
    }
    return rotated;
}

static void ScrollSetMessage(const volatile char *text)
{
    uint8_t length = 0U;

    while (*text != '\0' && length < SCROLL_BUFFER_SIZE) {
        if (*text == '.' && length != 0U) {
            g_scroll_codes[length - 1U] |= 0x80U;
        } else {
            g_scroll_chars[length] = *text;
            g_scroll_codes[length++] = CharacterToSegment(*text);
        }
        text++;
    }
    g_scroll_length = length;
    g_scroll_offset = g_format_right && length != 0U ? (uint8_t)(length - 1U) : 0U;
    g_scroll_next_ms = g_milliseconds + (g_scroll_fast ? 200U : 500U);
}

static void WeatherSetMessage(uint8_t temp, uint8_t condition)
{
    uint8_t index = 0U;
    const char *label = "CLOUD";

    if (condition == WEATHER_SUN) {
        label = "SUN";
    } else if (condition == WEATHER_RAIN) {
        label = "RAIN";
    } else if (condition == WEATHER_HOT) {
        label = "HOT";
    }

    if (temp > 99U) {
        temp = 99U;
    }
    g_weather_chars[index] = (char)('0' + temp / 10U);
    g_weather_codes[index] = CharacterToSegment(g_weather_chars[index]);
    index++;
    g_weather_chars[index] = (char)('0' + temp % 10U);
    g_weather_codes[index] = CharacterToSegment(g_weather_chars[index]);
    index++;
    g_weather_chars[index] = 'C';
    g_weather_codes[index] = CharacterToSegment('C');
    index++;
    g_weather_chars[index] = '_';
    g_weather_codes[index] = 0x00U;
    index++;
    while (*label != '\0' && index < 8U) {
        g_weather_chars[index] = *label;
        g_weather_codes[index] = CharacterToSegment(*label);
        label++;
        index++;
    }
    while (index < 8U) {
        g_weather_chars[index] = '_';
        g_weather_codes[index] = 0x00U;
        index++;
    }
    g_weather_length = 8U;
    g_weather_condition = condition;
}

static bool IsLeapYear(uint16_t year)
{
    return ((year % 400U) == 0U) ||
           (((year % 4U) == 0U) && ((year % 100U) != 0U));
}

static uint8_t DaysInMonth(uint16_t year, uint8_t month)
{
    static const uint8_t days[12] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U
    };

    if (month == 0U || month > 12U) {
        return 31U;
    }
    if (month == 2U && IsLeapYear(year)) {
        return 29U;
    }
    return days[month - 1U];
}

static void DateIncrement(void)
{
    g_day++;
    if (g_day > DaysInMonth(g_year, g_month)) {
        g_day = 1U;
        g_month++;
        if (g_month > 12U) {
            g_month = 1U;
            g_year++;
            if (g_year > 9999U) {
                g_year = 1U;
            }
        }
    }
}

static void ProcessKeys(void)
{
    uint8_t keys = I2C0ReadByte(TCA6424_I2CADDR, TCA6424_INPUT_PORT0);
    uint8_t confirm_keys = I2C0ReadByte(TCA6424_I2CADDR,
                                        TCA6424_INPUT_PORT0);
    uint8_t user_keys = (uint8_t)GPIOPinRead(GPIO_PORTJ_BASE,
                                             GPIO_PIN_0 | GPIO_PIN_1);
    uint8_t pressed;
    uint8_t released;
    uint8_t user_pressed;
    uint8_t index;
    static uint8_t save_release_count;
    static const char *names[8] = {
        "FUNC", "SHIFT", "ADD", "SAVE", "DISP", "SPEED", "FORMAT", "EXT"
    };

    /*
     * SAVE uses a fast falling-edge detector before the normal debounce path.
     * This keeps a very short SW4 press from being lost.
     */
    if (((keys & 0x08U) == 0U) || ((confirm_keys & 0x08U) == 0U)) {
        save_release_count = 0U;
        if (!g_save_key_latched) {
            g_save_key_latched = true;
            KeyOnPressed(3U, false);
            SendKeyEvent("SAVE");
        }
    } else if (save_release_count < 3U) {
        save_release_count++;
    } else {
        g_save_key_latched = false;
    }

    /* Other keys still use two consecutive I2C reads to reject noise. */
    if (keys != confirm_keys) {
        return;
    }

    if (keys != g_raw_keys) {
        g_raw_keys = keys;
        g_key_stable_count = 0U;
    } else if (g_key_stable_count < 1U) {
        g_key_stable_count++;
    } else if (keys != g_stable_keys) {
        pressed = (uint8_t)(g_stable_keys & (uint8_t)~keys);
        released = (uint8_t)((uint8_t)~g_stable_keys & keys);
        g_stable_keys = keys;

        for (index = 0U; index < 8U; index++) {
            uint8_t mask = (uint8_t)(1U << index);
            if ((pressed & mask) != 0U) {
                g_key_down_ms[index] = g_milliseconds;
                g_key_long_sent &= (uint8_t)~mask;
                if (index == 0U && g_alarm_ringing) {
                    KeyOnPressed(index, false);
                    g_key_long_sent |= mask;
                } else if (index != 0U && index != 3U) {
                    KeyOnPressed(index, false);
                }
                if (index != 3U) {
                    SendKeyEvent(names[index]);
                }
                if (index == 2U) {
                    g_add_next_repeat_ms = g_milliseconds +
                                           KEY_ADD_REPEAT_START_MS;
                }
            }
            if ((released & mask) != 0U && index == 0U &&
                (g_key_long_sent & mask) == 0U) {
                KeyOnPressed(0U, false);
            }
        }
    }

    for (index = 0U; index < 8U; index++) {
        uint8_t mask = (uint8_t)(1U << index);
        if ((g_stable_keys & mask) == 0U && index == 0U &&
            (g_key_long_sent & mask) == 0U &&
            (g_milliseconds - g_key_down_ms[index]) >= KEY_LONG_MS) {
            g_key_long_sent |= mask;
            KeyOnPressed(index, true);
        }
        if ((g_stable_keys & mask) == 0U && index == 6U &&
            (g_key_long_sent & mask) == 0U &&
            (g_milliseconds - g_key_down_ms[index]) >= KEY_LONG_MS) {
            g_key_long_sent |= mask;
            KeyOnPressed(index, true);
            SendKeyEvent("FORMAT_LONG");
        }
    }
    if ((g_stable_keys & 0x04U) == 0U && g_edit_mode != EDIT_NONE &&
        g_milliseconds >= g_add_next_repeat_ms) {
        g_add_next_repeat_ms = g_milliseconds + KEY_ADD_REPEAT_MS;
        KeyOnPressed(2U, true);
    }

    if (user_keys != g_raw_user_keys) {
        g_raw_user_keys = user_keys;
        g_user_key_stable_count = 0U;
    } else if (g_user_key_stable_count < 1U) {
        g_user_key_stable_count++;
    } else if (user_keys != g_stable_user_keys) {
        user_pressed = (uint8_t)(g_stable_user_keys & (uint8_t)~user_keys);
        g_stable_user_keys = user_keys;
        if (user_pressed != 0U) {
            HandleKeyPress(0U, user_pressed);
        }
    }
}

static void HandleKeyPress(uint8_t pressed, uint8_t user_pressed)
{
    uint8_t index;
    static const char *names[8] = {
        "FUNC", "SHIFT", "ADD", "SAVE", "DISP", "SPEED", "FORMAT", "EXT"
    };

    for (index = 0U; index < 8U; index++) {
        if ((pressed & (uint8_t)(1U << index)) != 0U) {
            KeyOnPressed(index, false);
            SendKeyEvent(names[index]);
        }
    }
    if ((user_pressed & GPIO_PIN_0) != 0U) {
        SendKeyEvent("USER1");
    }
    if ((user_pressed & GPIO_PIN_1) != 0U) {
        if (g_weather_length != 0U) {
            g_weather_show_until_ms = g_milliseconds + 5000U;
            SendDisplayEvent();
        }
        SendKeyEvent("USER2");
    }
}

static void KeyOnPressed(uint8_t key_index, bool long_press)
{
    if (key_index > 7U) {
        return;
    }

    if (key_index == 0U && g_alarm_ringing) {
        g_alarm_ringing = false;
        g_alarm_match_latched = true;
        BuzzerSet(false);
        UARTPutString("#EVT:ALARM_OFF\r\n");
        return;
    }

    if (g_edit_mode != EDIT_NONE) {
        g_edit_last_key_ms = g_milliseconds;
    }

    if (key_index == 0U) {
        if (long_press) {
            if (g_edit_mode != EDIT_NONE) {
                EditSave();
            }
        } else if (g_edit_mode == EDIT_NONE) {
            EditBegin();
        } else if (g_edit_mode < EDIT_ALARM) {
            g_edit_mode++;
            g_edit_field = 0U;
        } else {
            EditCancel();
        }
    } else if (key_index == 1U && g_edit_mode != EDIT_NONE) {
        g_edit_field = (uint8_t)((g_edit_field + 1U) % 3U);
    } else if (key_index == 2U && g_edit_mode != EDIT_NONE) {
        EditAdd();
    } else if (key_index == 3U && g_edit_mode != EDIT_NONE) {
        EditSave();
    } else if (key_index == 4U && g_edit_mode == EDIT_NONE) {
        g_display_mode = (uint8_t)((g_display_mode + 1U) % 2U);
    } else if (key_index == 5U && g_edit_mode == EDIT_NONE) {
        g_scroll_fast = !g_scroll_fast;
        g_scroll_next_ms = g_milliseconds;
    } else if (key_index == 6U && g_edit_mode == EDIT_NONE) {
        if (long_press) {
            g_format_right = !g_format_right;
            g_display_flip = !g_display_flip;
            SendFlipEvent();
            SendDisplayEvent();
        } else {
            g_format_right = !g_format_right;
            g_scroll_offset = g_format_right && g_scroll_length != 0U ?
                              (uint8_t)(g_scroll_length - 1U) : 0U;
        }
    } else if (key_index == 7U && g_edit_mode == EDIT_NONE) {
        if (g_focus_active) {
            FocusStop(false);
        } else {
            FocusStart();
        }
    }
}

static void EditBegin(void)
{
    g_edit_year = g_year;
    g_edit_month = g_month;
    g_edit_day = g_day;
    g_edit_hour = g_hour;
    g_edit_minute = g_minute;
    g_edit_second = g_second;
    g_edit_alarm_hour = g_alarm_hour;
    g_edit_alarm_minute = g_alarm_minute;
    g_edit_alarm_second = g_alarm_second;
    g_edit_alarm_touched = false;
    g_edit_mode = EDIT_DATE;
    g_edit_field = 0U;
    g_edit_last_key_ms = g_milliseconds;
}

static void EditCancel(void)
{
    g_edit_mode = EDIT_NONE;
    g_edit_field = 0U;
}

static void EditSave(void)
{
    uint8_t saved_mode = g_edit_mode;

    g_year = g_edit_year;
    g_month = g_edit_month;
    g_day = g_edit_day;
    g_hour = g_edit_hour;
    g_minute = g_edit_minute;
    g_second = g_edit_second;
    g_alarm_hour = g_edit_alarm_hour;
    g_alarm_minute = g_edit_alarm_minute;
    g_alarm_second = g_edit_alarm_second;
    if (g_edit_alarm_touched) {
        g_alarm_enabled = true;
        UARTPutString("#EVT:ALARM_ON\r\n");
    }
    if (saved_mode == EDIT_DATE) {
        UARTPutString("#EVT:EDIT DATE ");
        UARTPutDateText(g_year, g_month, g_day);
        UARTPutString("\r\n");
    } else if (saved_mode == EDIT_TIME) {
        UARTPutString("#EVT:EDIT TIME ");
        UARTPutTimeText(g_hour, g_minute, g_second);
        UARTPutString("\r\n");
    } else if (saved_mode == EDIT_ALARM) {
        UARTPutString("#EVT:EDIT ALARM ");
        UARTPutTimeText(g_alarm_hour, g_alarm_minute, g_alarm_second);
        UARTPutString(g_alarm_enabled ? " ON\r\n" : " OFF\r\n");
    }
    EditCancel();
    if (saved_mode == EDIT_ALARM) {
        AlarmLatchCurrentMatch();
    } else {
        g_alarm_match_latched = false;
    }
    if (saved_mode == EDIT_TIME || saved_mode == EDIT_DATE) {
        g_equal_time_latched = false;
    }
}

static void EditAdd(void)
{
    if (g_edit_mode == EDIT_DATE) {
        if (g_edit_field == 0U) {
            g_edit_year = g_edit_year >= 9999U ? 1U :
                          (uint16_t)(g_edit_year + 1U);
        } else if (g_edit_field == 1U) {
            g_edit_month++;
            if (g_edit_month > 12U) {
                g_edit_month = 1U;
                g_edit_year = g_edit_year >= 9999U ? 1U :
                              (uint16_t)(g_edit_year + 1U);
            }
        } else {
            g_edit_day++;
            if (g_edit_day > DaysInMonth(g_edit_year, g_edit_month)) {
                g_edit_day = 1U;
                g_edit_month++;
                if (g_edit_month > 12U) {
                    g_edit_month = 1U;
                    g_edit_year = g_edit_year >= 9999U ? 1U :
                                  (uint16_t)(g_edit_year + 1U);
                }
            }
        }
        if (g_edit_day > DaysInMonth(g_edit_year, g_edit_month)) {
            g_edit_day = DaysInMonth(g_edit_year, g_edit_month);
        }
    } else if (g_edit_mode == EDIT_TIME) {
        if (g_edit_field == 0U) {
            g_edit_hour = (uint8_t)((g_edit_hour + 1U) % 24U);
        } else if (g_edit_field == 1U) {
            g_edit_minute++;
            if (g_edit_minute >= 60U) {
                g_edit_minute = 0U;
                g_edit_hour = (uint8_t)((g_edit_hour + 1U) % 24U);
            }
        } else {
            g_edit_second++;
            if (g_edit_second >= 60U) {
                g_edit_second = 0U;
                g_edit_minute++;
                if (g_edit_minute >= 60U) {
                    g_edit_minute = 0U;
                    g_edit_hour = (uint8_t)((g_edit_hour + 1U) % 24U);
                }
            }
        }
    } else if (g_edit_mode == EDIT_ALARM) {
        if (g_edit_field == 0U) {
            g_edit_alarm_hour = (uint8_t)((g_edit_alarm_hour + 1U) % 24U);
        } else if (g_edit_field == 1U) {
            g_edit_alarm_minute++;
            if (g_edit_alarm_minute >= 60U) {
                g_edit_alarm_minute = 0U;
                g_edit_alarm_hour = (uint8_t)((g_edit_alarm_hour + 1U) % 24U);
            }
        } else {
            g_edit_alarm_second++;
            if (g_edit_alarm_second >= 60U) {
                g_edit_alarm_second = 0U;
                g_edit_alarm_minute++;
                if (g_edit_alarm_minute >= 60U) {
                    g_edit_alarm_minute = 0U;
                    g_edit_alarm_hour =
                        (uint8_t)((g_edit_alarm_hour + 1U) % 24U);
                }
            }
        }
        g_edit_alarm_touched = true;
    }
}

static void DisplayScan(void)
{
    uint8_t digits[8];
    uint8_t code;
    uint8_t display_index;
    const uint8_t *startup_text = 0;
    static uint32_t next_scan_ms;

    if (g_milliseconds == next_scan_ms) {
        return;
    }
    next_scan_ms = g_milliseconds;
    display_index = g_display_flip ? (uint8_t)(7U - g_display_position) :
                    g_display_position;

    if (!g_startup_complete) {
        if (g_startup_phase == 0U || g_startup_phase == 2U) {
            code = 0xffU;
        } else if (g_startup_phase == 4U) {
            startup_text = g_startup_student_id;
            code = startup_text[display_index];
        } else if (g_startup_phase == 6U) {
            startup_text = g_startup_name;
            code = startup_text[display_index];
        } else if (g_startup_phase == 8U) {
            startup_text = g_startup_version;
            code = startup_text[display_index];
        } else {
            code = 0x00U;
        }
    } else if (!g_display_enabled) {
        code = 0x00U;
    } else if (g_weather_length != 0U &&
               (int32_t)(g_weather_show_until_ms - g_milliseconds) > 0) {
        code = g_weather_codes[display_index];
    } else if (g_night_mode) {
        digits[0] = (uint8_t)(g_hour / 10U);
        digits[1] = (uint8_t)(g_hour % 10U);
        digits[2] = (uint8_t)(g_minute / 10U);
        digits[3] = (uint8_t)(g_minute % 10U);
        code = display_index < 4U ?
               g_segment_code[digits[display_index]] : 0x00U;
        if ((!g_display_flip && display_index == 1U) ||
            (g_display_flip && display_index == 2U)) {
            code |= 0x80U;
        }
    } else if (g_edit_mode != EDIT_NONE) {
        bool selected = false;

        if (g_edit_mode == EDIT_DATE) {
            digits[0] = (uint8_t)(g_edit_year / 1000U);
            digits[1] = (uint8_t)((g_edit_year / 100U) % 10U);
            digits[2] = (uint8_t)((g_edit_year / 10U) % 10U);
            digits[3] = (uint8_t)(g_edit_year % 10U);
            digits[4] = (uint8_t)(g_edit_month / 10U);
            digits[5] = (uint8_t)(g_edit_month % 10U);
            digits[6] = (uint8_t)(g_edit_day / 10U);
            digits[7] = (uint8_t)(g_edit_day % 10U);
            selected = (g_edit_field == 0U && display_index <= 3U) ||
                       (g_edit_field == 1U && display_index >= 4U &&
                        display_index <= 5U) ||
                       (g_edit_field == 2U && display_index >= 6U);
            code = g_segment_code[digits[display_index]];
            if ((!g_display_flip && display_index == 3U) ||
                (g_display_flip && display_index == 4U)) {
                code |= 0x80U;
            }
        } else {
            uint8_t edit_h = g_edit_mode == EDIT_TIME ? g_edit_hour :
                             g_edit_alarm_hour;
            uint8_t edit_m = g_edit_mode == EDIT_TIME ? g_edit_minute :
                             g_edit_alarm_minute;
            uint8_t edit_s = g_edit_mode == EDIT_TIME ? g_edit_second :
                             g_edit_alarm_second;

            digits[0] = (uint8_t)(edit_h / 10U);
            digits[1] = (uint8_t)(edit_h % 10U);
            digits[2] = 10U;
            digits[3] = (uint8_t)(edit_m / 10U);
            digits[4] = (uint8_t)(edit_m % 10U);
            digits[5] = 10U;
            digits[6] = (uint8_t)(edit_s / 10U);
            digits[7] = (uint8_t)(edit_s % 10U);
            selected = (g_edit_field == 0U && display_index <= 1U) ||
                       (g_edit_field == 1U && display_index >= 3U &&
                        display_index <= 4U) ||
                       (g_edit_field == 2U && display_index >= 6U);
            code = g_segment_code[digits[display_index]];
            if ((!g_display_flip &&
                 (display_index == 1U || display_index == 4U)) ||
                (g_display_flip &&
                 (display_index == 2U || display_index == 5U))) {
                code |= 0x80U;
            }
        }
        if (selected && ((g_milliseconds / 250U) & 1U) == 0U) {
            code = 0x00U;
        }
    } else if (g_scroll_length != 0U) {
        uint8_t source;
        uint8_t previous;

        if (g_scroll_length <= 8U) {
            source = display_index;
            code = source < g_scroll_length ? g_scroll_codes[source] : 0x00U;
        } else if (g_format_right) {
            source = (uint8_t)((g_scroll_offset + g_scroll_length -
                               display_index) % g_scroll_length);
            code = (uint8_t)(g_scroll_codes[source] & 0x7fU);
            previous = (uint8_t)((source + 1U) % g_scroll_length);
            if ((g_scroll_codes[previous] & 0x80U) != 0U &&
                display_index != 0U) {
                code |= 0x80U;
            }
        } else {
            source = (uint8_t)((g_scroll_offset + display_index) %
                               g_scroll_length);
            code = g_scroll_codes[source];
        }
    } else if (g_display_mode == 0U) {
        digits[0] = (uint8_t)(g_hour / 10U);
        digits[1] = (uint8_t)(g_hour % 10U);
        digits[2] = 10U;
        digits[3] = (uint8_t)(g_minute / 10U);
        digits[4] = (uint8_t)(g_minute % 10U);
        digits[5] = 10U;
        digits[6] = (uint8_t)(g_second / 10U);
        digits[7] = (uint8_t)(g_second % 10U);

        code = g_segment_code[digits[display_index]];
        if ((!g_display_flip &&
             (display_index == 1U || display_index == 4U)) ||
            (g_display_flip &&
             (display_index == 2U || display_index == 5U))) {
            code |= 0x80U;
        }
    } else {
        digits[0] = (uint8_t)(g_year / 1000U);
        digits[1] = (uint8_t)((g_year / 100U) % 10U);
        digits[2] = (uint8_t)((g_year / 10U) % 10U);
        digits[3] = (uint8_t)(g_year % 10U);
        digits[4] = (uint8_t)(g_month / 10U);
        digits[5] = (uint8_t)(g_month % 10U);
        digits[6] = (uint8_t)(g_day / 10U);
        digits[7] = (uint8_t)(g_day % 10U);
        code = g_segment_code[digits[display_index]];
        if ((!g_display_flip && display_index == 3U) ||
            (g_display_flip && display_index == 4U)) {
            code |= 0x80U;
        }
    }

    /* Disable all digits before changing segments to avoid visible ghosting. */
    I2C0WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2, 0x00U);
    if (g_display_flip) {
        code = RotateSegment180(code);
    }
    I2C0WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT1, code);
    I2C0WriteByte(TCA6424_I2CADDR, TCA6424_OUTPUT_PORT2,
                  (uint8_t)(1U << g_display_position));
    g_display_position = (uint8_t)((g_display_position + 1U) & 0x07U);
}

static void ProcessCommand(void)
{
    bool accepted = false;
    uint8_t a;
    uint8_t b;
    uint8_t c;
    uint16_t year;

    g_uart_activity_until_ms = g_milliseconds + 100U;
    if (g_uart_rx_overflow) {
        UARTPutString("ERROR LEN\r\n");
        accepted = true;
    } else if (CommandEquals("*GET:DATE")) {
        UARTPutString("OK ");
        UARTPutDateText(g_year, g_month, g_day);
        UARTPutString("\r\n");
        accepted = true;
    } else if (CommandEquals("*PING")) {
        UARTPutString("#PONG ");
        UARTPutUInt(g_milliseconds / 1000U);
        UARTPutString("\r\n");
        accepted = true;
    } else if (CommandEquals("*RST")) {
        g_hour = 12U;
        g_minute = 0U;
        g_second = 0U;
        g_year = 2026U;
        g_month = 6U;
        g_day = 4U;
        g_alarm_hour = 12U;
        g_alarm_minute = 1U;
        g_alarm_second = 0U;
        g_alarm_enabled = false;
        g_alarm_ringing = false;
        g_alarm_match_latched = false;
        g_equal_time_latched = false;
        g_led_override = false;
        g_display_enabled = true;
        g_format_right = false;
        g_display_flip = false;
        g_night_mode = false;
        g_led_override = false;
        g_time_sync_ok = false;
        g_weather_condition = WEATHER_NONE;
        g_weather_length = 0U;
        g_focus_active = false;
        g_focus_duration_minutes = 40U;
        g_focus_remaining_seconds = 0U;
        g_focus_completed_count = 0U;
        BuzzerSet(false);
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (CommandEquals("*GET:TIME")) {
        UARTPutString("OK ");
        UARTPutTimeText(g_hour, g_minute, g_second);
        UARTPutString("\r\n");
        accepted = true;
    } else if (CommandEquals("*GET:ALARM")) {
        UARTPutString("OK ");
        UARTPutTimeText(g_alarm_hour, g_alarm_minute, g_alarm_second);
        UARTPutString(g_alarm_enabled ? " ON\r\n" : " OFF\r\n");
        accepted = true;
    } else if (CommandEquals("*GET:MODE")) {
        UARTPutString(g_night_mode ? "OK NIGHT\r\n" : "OK DAY\r\n");
        accepted = true;
    } else if (CommandEquals("*GET:FORMAT")) {
        UARTPutString(g_format_right ? "OK RIGHT\r\n" : "OK LEFT\r\n");
        accepted = true;
    } else if (CommandEquals("*GET:FLIP")) {
        UARTPutString(g_display_flip ? "OK FLIP ON\r\n" : "OK FLIP OFF\r\n");
        accepted = true;
    } else if (CommandEquals("*GET:FOCUS")) {
        UARTPutString("OK FOCUS ");
        UARTPutString(g_focus_active ? "ON " : "OFF ");
        UARTPut2(g_focus_duration_minutes);
        UARTPutString(" ");
        UARTPutUInt(g_focus_remaining_seconds);
        UARTPutString(" ");
        UARTPutUInt(g_focus_completed_count);
        UARTPutString("\r\n");
        accepted = true;
    } else if (CommandEquals("*SET:SYNC OK")) {
        g_time_sync_ok = true;
        g_time_sync_until_ms = g_milliseconds + 10000U;
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (ProcessWeatherSet()) {
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (ProcessDateSet()) {
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (ProcessTimeSet()) {
        UARTPutString("OK\r\n");
        g_alarm_match_latched = false;
        g_equal_time_latched = false;
        accepted = true;
    } else if (ProcessAlarmSet()) {
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (CommandEquals("*SET:FORMAT LEFT")) {
        g_format_right = false;
        g_scroll_offset = 0U;
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (CommandEquals("*SET:FORMAT RIGHT")) {
        g_format_right = true;
        g_scroll_offset = g_scroll_length != 0U ?
                          (uint8_t)(g_scroll_length - 1U) : 0U;
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (CommandEquals("*SET:FLIP ON")) {
        g_display_flip = true;
        UARTPutString("OK\r\n");
        SendFlipEvent();
        SendDisplayEvent();
        accepted = true;
    } else if (CommandEquals("*SET:FLIP OFF")) {
        g_display_flip = false;
        UARTPutString("OK\r\n");
        SendFlipEvent();
        SendDisplayEvent();
        accepted = true;
    } else if (CommandEquals("*SET:MODE DAY")) {
        g_night_mode = false;
        UARTPutString("OK\r\n");
        SendModeEvent();
        accepted = true;
    } else if (CommandEquals("*SET:MODE NIGHT")) {
        g_night_mode = true;
        UARTPutString("OK\r\n");
        SendModeEvent();
        accepted = true;
    } else if (CommandEquals("*SET:MUSIC WIND")) {
        MusicStart(g_music_wind,
                   (uint16_t)(sizeof(g_music_wind) / sizeof(g_music_wind[0])));
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (CommandEquals("*SET:MUSIC STOP")) {
        MusicStop();
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (CommandEquals("*SET:KEY FUNC")) {
        KeyOnPressed(0U, false);
        SendKeyEvent("FUNC");
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (CommandEquals("*SET:KEY SHIFT")) {
        KeyOnPressed(1U, false);
        SendKeyEvent("SHIFT");
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (CommandEquals("*SET:KEY ADD")) {
        KeyOnPressed(2U, false);
        SendKeyEvent("ADD");
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (CommandEquals("*SET:KEY SAVE")) {
        KeyOnPressed(3U, false);
        SendKeyEvent("SAVE");
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (CommandEquals("*SET:KEY DISP")) {
        KeyOnPressed(4U, false);
        SendKeyEvent("DISP");
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (CommandEquals("*SET:KEY SPEED")) {
        KeyOnPressed(5U, false);
        SendKeyEvent("SPEED");
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (CommandEquals("*SET:KEY FORMAT")) {
        KeyOnPressed(6U, false);
        SendKeyEvent("FORMAT");
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (CommandEquals("*SET:KEY FORMAT_LONG") ||
               CommandEquals("*SET:KEY FLIP")) {
        g_display_flip = !g_display_flip;
        SendFlipEvent();
        SendDisplayEvent();
        SendKeyEvent("FORMAT_LONG");
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (CommandEquals("*SET:KEY EXT")) {
        if (g_focus_active) {
            FocusStop(false);
        } else {
            FocusStart();
        }
        SendKeyEvent("EXT");
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (CommandEquals("*SET:KEY USER1")) {
        HandleKeyPress(0U, GPIO_PIN_0);
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (CommandEquals("*SET:KEY USER2")) {
        HandleKeyPress(0U, GPIO_PIN_1);
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (g_uart_rx[0] == '*' && g_uart_rx[1] == 'S' &&
               g_uart_rx[2] == 'E' && g_uart_rx[3] == 'T' &&
               g_uart_rx[4] == ':' && g_uart_rx[5] == 'L' &&
               g_uart_rx[6] == 'E' && g_uart_rx[7] == 'D' &&
               g_uart_rx[8] == ' ' && ParseHex2(&g_uart_rx[9], &a) &&
               g_uart_rx[11] == '\0') {
        g_led_override = true;
        g_led_override_value = a;
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (g_uart_rx[0] == '*' && g_uart_rx[1] == 'S' &&
               g_uart_rx[2] == 'E' && g_uart_rx[3] == 'T' &&
               g_uart_rx[4] == ':' && g_uart_rx[5] == 'T' &&
               g_uart_rx[6] == 'I' && g_uart_rx[7] == 'M' &&
               g_uart_rx[8] == 'E' && g_uart_rx[9] == ' ' &&
               g_uart_rx[10] == 'H' && g_uart_rx[11] == 'O' &&
               g_uart_rx[12] == 'U' && g_uart_rx[13] == 'R' &&
               g_uart_rx[14] == ' ' && g_uart_rx[15] == 'M' &&
               g_uart_rx[16] == 'I' && g_uart_rx[17] == 'N' &&
               g_uart_rx[18] == ' ' && g_uart_rx[19] == 'S' &&
               g_uart_rx[20] == 'E' && g_uart_rx[21] == 'C' &&
               g_uart_rx[22] == ' ' && Parse2(&g_uart_rx[23], &a) &&
               g_uart_rx[25] == ' ' && Parse2(&g_uart_rx[26], &b) &&
               g_uart_rx[28] == ' ' && Parse2(&g_uart_rx[29], &c) &&
               g_uart_rx[31] == '\0' && a < 24U && b < 60U && c < 60U) {
        g_hour = a;
        g_minute = b;
        g_second = c;
        g_alarm_match_latched = false;
        g_equal_time_latched = false;
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (g_uart_rx[0] == '*' && g_uart_rx[1] == 'S' &&
               g_uart_rx[2] == 'E' && g_uart_rx[3] == 'T' &&
               g_uart_rx[4] == ':' && g_uart_rx[5] == 'B' &&
               g_uart_rx[6] == 'E' && g_uart_rx[7] == 'E' &&
               g_uart_rx[8] == 'P' && g_uart_rx[9] == ' ' &&
               ParseUInt(&g_uart_rx[10], &year) &&
               year >= 10U && year <= 5000U) {
        MusicStop();
        g_manual_beep_until_ms = g_milliseconds + year;
        BuzzerTone(BUZZER_ALARM_FREQ_HZ);
        BuzzerSet(true);
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (g_uart_rx[0] == '*' && g_uart_rx[1] == 'S' &&
               g_uart_rx[2] == 'E' && g_uart_rx[3] == 'T' &&
               g_uart_rx[4] == ':' && g_uart_rx[5] == 'F' &&
               g_uart_rx[6] == 'O' && g_uart_rx[7] == 'C' &&
               g_uart_rx[8] == 'U' && g_uart_rx[9] == 'S' &&
               g_uart_rx[10] == ' ' && ParseUInt(&g_uart_rx[11], &year) &&
               year >= 1U && year <= 99U) {
        g_focus_duration_minutes = (uint8_t)year;
        if (g_focus_active) {
            g_focus_remaining_seconds = (uint32_t)g_focus_duration_minutes *
                                        60U;
            SendDisplayEvent();
            SendFocusEvent("ON");
        }
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (CommandEquals("*FOCUS:ON") || CommandEquals("*FOCUS:START")) {
        FocusStart();
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (CommandEquals("*FOCUS:OFF") || CommandEquals("*FOCUS:STOP")) {
        FocusStop(false);
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (g_uart_rx[0] == '*' && g_uart_rx[1] == 'S' &&
               g_uart_rx[2] == 'E' && g_uart_rx[3] == 'T' &&
               g_uart_rx[4] == ':' && g_uart_rx[5] == 'M' &&
               g_uart_rx[6] == 'S' && g_uart_rx[7] == 'G' &&
               g_uart_rx[8] == ' ') {
        uint8_t msg_length = 0U;
        while (g_uart_rx[9U + msg_length] != '\0') {
            msg_length++;
        }
        if (msg_length > 32U) {
            UARTPutString("ERROR LEN\r\n");
            accepted = true;
        } else {
        ScrollSetMessage(&g_uart_rx[9]);
        UARTPutString("OK\r\n");
        accepted = true;
        }
    } else if (CommandEquals("*SET:MSG")) {
        g_scroll_length = 0U;
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (CommandEquals("*GET:DISPLAY") || CommandEquals("*GET:DISP") ||
               CommandEquals("*GET:DISPL") || CommandEquals("*GET:DISPLA")) {
        UARTPutString(g_display_enabled ? "OK ON\r\n" : "OK OFF\r\n");
        accepted = true;
    } else if (CommandEquals("*SET:DISPLAY ON") ||
               CommandEquals("*SET:DISP ON") ||
               CommandEquals("*SET:DISPL ON") ||
               CommandEquals("*SET:DISPLA ON")) {
        g_display_enabled = true;
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (CommandEquals("*SET:DISPLAY OFF") ||
               CommandEquals("*SET:DISP OFF") ||
               CommandEquals("*SET:DISPL OFF") ||
               CommandEquals("*SET:DISPLA OFF")) {
        g_display_enabled = false;
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (g_uart_rx[0] == '*' && g_uart_rx[1] == 'S' &&
               g_uart_rx[2] == 'E' && g_uart_rx[3] == 'T' &&
               g_uart_rx[4] == ':' && g_uart_rx[5] == 'A' &&
               g_uart_rx[6] == 'L' && g_uart_rx[7] == 'A' &&
               g_uart_rx[8] == 'R' && g_uart_rx[9] == 'M' &&
               g_uart_rx[10] == ' ' && g_uart_rx[11] == 'H' &&
               g_uart_rx[12] == 'O' && g_uart_rx[13] == 'U' &&
               g_uart_rx[14] == 'R' && g_uart_rx[15] == ' ' &&
               g_uart_rx[16] == 'M' && g_uart_rx[17] == 'I' &&
               g_uart_rx[18] == 'N' && g_uart_rx[19] == ' ' &&
               g_uart_rx[20] == 'S' && g_uart_rx[21] == 'E' &&
               g_uart_rx[22] == 'C' && g_uart_rx[23] == ' ' &&
               Parse2(&g_uart_rx[24], &a) && g_uart_rx[26] == ' ' &&
               Parse2(&g_uart_rx[27], &b) && g_uart_rx[29] == ' ' &&
               Parse2(&g_uart_rx[30], &c) && g_uart_rx[32] == '\0' &&
               a < 24U && b < 60U && c < 60U) {
        g_alarm_hour = a;
        g_alarm_minute = b;
        g_alarm_second = c;
        g_alarm_enabled = true;
        UARTPutString("OK\r\n");
        UARTPutString("#EVT:ALARM_ON\r\n");
        AlarmLatchCurrentMatch();
        accepted = true;
    } else if (CommandEquals("*SET:ALARM OFF")) {
        g_alarm_enabled = false;
        g_alarm_ringing = false;
        g_alarm_match_latched = false;
        BuzzerSet(false);
        UARTPutString("OK\r\n");
        UARTPutString("#EVT:ALARM_OFF\r\n");
        accepted = true;
    } else if (g_uart_rx[0] == '*' && g_uart_rx[1] == 'S' &&
               g_uart_rx[2] == 'E' && g_uart_rx[3] == 'T' &&
               g_uart_rx[4] == ':' && g_uart_rx[5] == 'D' &&
               g_uart_rx[6] == 'A' && g_uart_rx[7] == 'T' &&
               g_uart_rx[8] == 'E' && g_uart_rx[9] == ' ' &&
               g_uart_rx[10] == 'Y' && g_uart_rx[11] == 'E' &&
               g_uart_rx[12] == 'A' && g_uart_rx[13] == 'R' &&
               g_uart_rx[14] == ' ' && g_uart_rx[15] == 'M' &&
               g_uart_rx[16] == 'O' && g_uart_rx[17] == 'N' &&
               g_uart_rx[18] == 'T' && g_uart_rx[19] == 'H' &&
               g_uart_rx[20] == ' ' && g_uart_rx[21] == 'D' &&
               g_uart_rx[22] == 'A' && g_uart_rx[23] == 'T' &&
               g_uart_rx[24] == 'E' && g_uart_rx[25] == ' ' &&
               Parse4(&g_uart_rx[26], &year) && g_uart_rx[30] == ' ' &&
               Parse2(&g_uart_rx[31], &a) && g_uart_rx[33] == ' ' &&
               Parse2(&g_uart_rx[34], &b) && g_uart_rx[36] == '\0' &&
               year >= 1U && a >= 1U && a <= 12U &&
               b >= 1U && b <= DaysInMonth(year, a)) {
        g_year = year;
        g_month = a;
        g_day = b;
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (g_uart_rx[0] == 'G' && g_uart_rx[1] == 'E' && g_uart_rx[2] == 'T' &&
        g_uart_rx[3] == '\0') {
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (g_uart_rx[0] == 'S' && g_uart_rx[1] == 'T' &&
               g_uart_rx[2] == 'O' && g_uart_rx[3] == 'P' &&
               g_uart_rx[4] == '\0') {
        g_alarm_ringing = false;
        g_alarm_match_latched = true;
        MusicStop();
        BuzzerSet(false);
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (g_uart_rx[0] == 'A' && g_uart_rx[1] == 'L' &&
               g_uart_rx[2] == 'A' && g_uart_rx[3] == 'R' &&
               g_uart_rx[4] == 'M' && g_uart_rx[5] == ' ') {
        if (g_uart_rx[6] == 'O' && g_uart_rx[7] == 'N' &&
            g_uart_rx[8] == '\0') {
            g_alarm_enabled = true;
            AlarmLatchCurrentMatch();
            UARTPutString("OK\r\n");
            accepted = true;
        } else if (g_uart_rx[6] == 'O' && g_uart_rx[7] == 'F' &&
                   g_uart_rx[8] == 'F' && g_uart_rx[9] == '\0') {
            g_alarm_enabled = false;
            g_alarm_ringing = false;
            g_alarm_match_latched = false;
            BuzzerSet(false);
            UARTPutString("OK\r\n");
            accepted = true;
        }
    } else if (g_uart_rx[0] == 'B' && g_uart_rx[1] == 'E' &&
               g_uart_rx[2] == 'E' && g_uart_rx[3] == 'P' &&
               g_uart_rx[4] == ' ') {
        if (g_uart_rx[5] == 'O' && g_uart_rx[6] == 'N' &&
            g_uart_rx[7] == '\0') {
            BuzzerSet(true);
            UARTPutString("OK\r\n");
            accepted = true;
        } else if (g_uart_rx[5] == 'O' && g_uart_rx[6] == 'F' &&
                   g_uart_rx[7] == 'F' && g_uart_rx[8] == '\0') {
            BuzzerSet(false);
            UARTPutString("OK\r\n");
            accepted = true;
        }
    } else if (g_uart_rx[0] == 'K' && g_uart_rx[1] == 'E' &&
               g_uart_rx[2] == 'Y' && g_uart_rx[3] == ' ' &&
               g_uart_rx[4] >= '1' && g_uart_rx[4] <= '8' &&
               g_uart_rx[5] == '\0') {
        HandleKeyPress((uint8_t)(1U << (g_uart_rx[4] - '1')), 0U);
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (g_uart_rx[0] == 'U' && g_uart_rx[1] == 'S' &&
               g_uart_rx[2] == 'E' && g_uart_rx[3] == 'R' &&
               g_uart_rx[4] == ' ' &&
               (g_uart_rx[5] == '1' || g_uart_rx[5] == '2') &&
               g_uart_rx[6] == '\0') {
        HandleKeyPress(0U, g_uart_rx[5] == '1' ? GPIO_PIN_0 : GPIO_PIN_1);
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (g_uart_rx[0] == 'S' && g_uart_rx[1] == 'E' &&
               g_uart_rx[2] == 'T' && g_uart_rx[3] == ' ' &&
               g_uart_rx[4] == 'T' && g_uart_rx[5] == 'I' &&
               g_uart_rx[6] == 'M' && g_uart_rx[7] == 'E' &&
               g_uart_rx[8] == ' ' &&
               Parse2(&g_uart_rx[9], &a) && g_uart_rx[11] == ':' &&
               Parse2(&g_uart_rx[12], &b) && g_uart_rx[14] == ':' &&
               Parse2(&g_uart_rx[15], &c) && g_uart_rx[17] == '\0' &&
               a < 24U && b < 60U && c < 60U) {
        g_hour = a;
        g_minute = b;
        g_second = c;
        g_alarm_match_latched = false;
        g_equal_time_latched = false;
        UARTPutString("OK\r\n");
        accepted = true;
    } else if (g_uart_rx[0] == 'S' && g_uart_rx[1] == 'E' &&
               g_uart_rx[2] == 'T' && g_uart_rx[3] == ' ' &&
               g_uart_rx[4] == 'A' && g_uart_rx[5] == 'L' &&
               g_uart_rx[6] == 'A' && g_uart_rx[7] == 'R' &&
               g_uart_rx[8] == 'M' && g_uart_rx[9] == ' ' &&
               Parse2(&g_uart_rx[10], &a) && g_uart_rx[12] == ':' &&
               Parse2(&g_uart_rx[13], &b) && g_uart_rx[15] == '\0' &&
               a < 24U && b < 60U) {
        g_alarm_hour = a;
        g_alarm_minute = b;
        g_alarm_second = 0U;
        g_alarm_enabled = true;
        UARTPutString("#EVT:ALARM_ON\r\n");
        AlarmLatchCurrentMatch();
        UARTPutString("OK\r\n");
        accepted = true;
    }

    if (!accepted) {
        UARTPutString("ERROR SYNTAX\r\n");
    }
    g_uart_rx_length = 0U;
    g_uart_rx_overflow = false;
    g_command_ready = false;
    SendState();
}

static bool Parse2(const volatile char *text, uint8_t *value)
{
    if (text[0] < '0' || text[0] > '9' ||
        text[1] < '0' || text[1] > '9') {
        return false;
    }
    *value = (uint8_t)((text[0] - '0') * 10 + (text[1] - '0'));
    return true;
}

static bool Parse4(const volatile char *text, uint16_t *value)
{
    uint8_t index;
    uint16_t result = 0U;

    for (index = 0U; index < 4U; index++) {
        if (text[index] < '0' || text[index] > '9') {
            return false;
        }
        result = (uint16_t)(result * 10U + (uint16_t)(text[index] - '0'));
    }
    *value = result;
    return true;
}

static bool ParseUInt(const volatile char *text, uint16_t *value)
{
    uint8_t count = 0U;
    uint16_t result = 0U;

    while (text[count] != '\0' && count < 5U) {
        if (text[count] < '0' || text[count] > '9') {
            return false;
        }
        result = (uint16_t)(result * 10U + (uint16_t)(text[count] - '0'));
        count++;
    }
    if (count == 0U || text[count] != '\0') {
        return false;
    }
    *value = result;
    return true;
}

static bool ParseHex2(const volatile char *text, uint8_t *value)
{
    uint8_t index;
    uint8_t result = 0U;

    for (index = 0U; index < 2U; index++) {
        uint8_t digit;
        if (text[index] >= '0' && text[index] <= '9') {
            digit = (uint8_t)(text[index] - '0');
        } else if (text[index] >= 'A' && text[index] <= 'F') {
            digit = (uint8_t)(text[index] - 'A' + 10U);
        } else {
            return false;
        }
        result = (uint8_t)((result << 4) | digit);
    }
    *value = result;
    return true;
}

static bool ReadToken(const volatile char **cursor, char *token, uint8_t max_len)
{
    uint8_t count = 0U;

    while (**cursor == ' ') {
        (*cursor)++;
    }
    while (**cursor != '\0' && **cursor != ' ' && count < (max_len - 1U)) {
        token[count++] = **cursor;
        (*cursor)++;
    }
    if (**cursor != '\0' && **cursor != ' ') {
        return false;
    }
    token[count] = '\0';
    return count != 0U;
}

static bool ReadValue(const volatile char **cursor, uint16_t *value)
{
    uint8_t count = 0U;
    uint16_t result = 0U;

    while (**cursor == ' ') {
        (*cursor)++;
    }
    while (**cursor >= '0' && **cursor <= '9' && count < 4U) {
        result = (uint16_t)(result * 10U + (uint16_t)(**cursor - '0'));
        (*cursor)++;
        count++;
    }
    if (count == 0U || (**cursor != '\0' && **cursor != ' ')) {
        return false;
    }
    *value = result;
    return true;
}

static bool NameEquals(const char *a, const char *b)
{
    uint8_t index = 0U;
    while (a[index] != '\0' && a[index] == b[index]) {
        index++;
    }
    return a[index] == '\0' && b[index] == '\0';
}

static bool ProcessDateSet(void)
{
    const volatile char *cursor;
    const volatile char *value_cursor;
    char names[3][8];
    uint16_t values[3];
    uint16_t year = g_year;
    uint8_t month = g_month;
    uint8_t day = g_day;
    uint8_t count = 0U;
    uint8_t index;

    if (!(g_uart_rx[0] == '*' && g_uart_rx[1] == 'S' &&
          g_uart_rx[2] == 'E' && g_uart_rx[3] == 'T' &&
          g_uart_rx[4] == ':' && g_uart_rx[5] == 'D' &&
          g_uart_rx[6] == 'A' && g_uart_rx[7] == 'T' &&
          g_uart_rx[8] == 'E' && g_uart_rx[9] == ' ')) {
        return false;
    }
    cursor = &g_uart_rx[10];
    while (*cursor != '\0' && count < 3U) {
        while (*cursor == ' ') {
            cursor++;
        }
        if (*cursor >= '0' && *cursor <= '9') {
            break;
        }
        if (!ReadToken(&cursor, names[count], sizeof(names[count]))) {
            return false;
        }
        count++;
    }
    if (count == 0U) {
        return false;
    }
    value_cursor = cursor;
    for (index = 0U; index < count; index++) {
        if (!ReadValue(&value_cursor, &values[index])) {
            return false;
        }
    }
    while (*value_cursor == ' ') {
        value_cursor++;
    }
    if (*value_cursor != '\0') {
        return false;
    }
    for (index = 0U; index < count; index++) {
        if (NameEquals(names[index], "YEAR") && values[index] >= 1U &&
            values[index] <= 9999U) {
            year = values[index];
        } else if (NameEquals(names[index], "MONTH") &&
                   values[index] >= 1U && values[index] <= 12U) {
            month = (uint8_t)values[index];
        } else if (NameEquals(names[index], "DATE") &&
                   values[index] >= 1U && values[index] <= 31U) {
            day = (uint8_t)values[index];
        } else {
            return false;
        }
    }
    if (day > DaysInMonth(year, month)) {
        return false;
    }
    g_year = year;
    g_month = month;
    g_day = day;
    return true;
}

static bool ProcessTimeSet(void)
{
    const volatile char *cursor;
    const volatile char *value_cursor;
    char names[3][8];
    uint16_t values[3];
    uint8_t hour = g_hour;
    uint8_t minute = g_minute;
    uint8_t second = g_second;
    uint8_t count = 0U;
    uint8_t index;

    if (!(g_uart_rx[0] == '*' && g_uart_rx[1] == 'S' &&
          g_uart_rx[2] == 'E' && g_uart_rx[3] == 'T' &&
          g_uart_rx[4] == ':' && g_uart_rx[5] == 'T' &&
          g_uart_rx[6] == 'I' && g_uart_rx[7] == 'M' &&
          g_uart_rx[8] == 'E' && g_uart_rx[9] == ' ')) {
        return false;
    }
    cursor = &g_uart_rx[10];
    while (*cursor != '\0' && count < 3U) {
        while (*cursor == ' ') {
            cursor++;
        }
        if (*cursor >= '0' && *cursor <= '9') {
            break;
        }
        if (!ReadToken(&cursor, names[count], sizeof(names[count]))) {
            return false;
        }
        count++;
    }
    if (count == 0U) {
        return false;
    }
    value_cursor = cursor;
    for (index = 0U; index < count; index++) {
        if (!ReadValue(&value_cursor, &values[index])) {
            return false;
        }
    }
    while (*value_cursor == ' ') {
        value_cursor++;
    }
    if (*value_cursor != '\0') {
        return false;
    }
    for (index = 0U; index < count; index++) {
        if (NameEquals(names[index], "HOUR") && values[index] < 24U) {
            hour = (uint8_t)values[index];
        } else if ((NameEquals(names[index], "MIN") ||
                    NameEquals(names[index], "MINU") ||
                    NameEquals(names[index], "MINUT") ||
                    NameEquals(names[index], "MINUTE")) &&
                   values[index] < 60U) {
            minute = (uint8_t)values[index];
        } else if ((NameEquals(names[index], "SEC") ||
                    NameEquals(names[index], "SECO") ||
                    NameEquals(names[index], "SECON") ||
                    NameEquals(names[index], "SECOND")) &&
                   values[index] < 60U) {
            second = (uint8_t)values[index];
        } else {
            return false;
        }
    }
    g_hour = hour;
    g_minute = minute;
    g_second = second;
    return true;
}

static bool ProcessAlarmSet(void)
{
    const volatile char *cursor;
    const volatile char *value_cursor;
    char names[3][8];
    uint16_t values[3];
    uint8_t hour = g_alarm_hour;
    uint8_t minute = g_alarm_minute;
    uint8_t second = g_alarm_second;
    uint8_t count = 0U;
    uint8_t index;

    if (!(g_uart_rx[0] == '*' && g_uart_rx[1] == 'S' &&
          g_uart_rx[2] == 'E' && g_uart_rx[3] == 'T' &&
          g_uart_rx[4] == ':' && g_uart_rx[5] == 'A' &&
          g_uart_rx[6] == 'L' && g_uart_rx[7] == 'A' &&
          g_uart_rx[8] == 'R' && g_uart_rx[9] == 'M' &&
          g_uart_rx[10] == ' ')) {
        return false;
    }
    if (CommandEquals("*SET:ALARM OFF")) {
        return false;
    }
    cursor = &g_uart_rx[11];
    while (*cursor != '\0' && count < 3U) {
        while (*cursor == ' ') {
            cursor++;
        }
        if (*cursor >= '0' && *cursor <= '9') {
            break;
        }
        if (!ReadToken(&cursor, names[count], sizeof(names[count]))) {
            return false;
        }
        count++;
    }
    if (count == 0U) {
        return false;
    }
    value_cursor = cursor;
    for (index = 0U; index < count; index++) {
        if (!ReadValue(&value_cursor, &values[index])) {
            return false;
        }
    }
    while (*value_cursor == ' ') {
        value_cursor++;
    }
    if (*value_cursor != '\0') {
        return false;
    }
    for (index = 0U; index < count; index++) {
        if (NameEquals(names[index], "HOUR") && values[index] < 24U) {
            hour = (uint8_t)values[index];
        } else if ((NameEquals(names[index], "MIN") ||
                    NameEquals(names[index], "MINU") ||
                    NameEquals(names[index], "MINUT") ||
                    NameEquals(names[index], "MINUTE")) &&
                   values[index] < 60U) {
            minute = (uint8_t)values[index];
        } else if ((NameEquals(names[index], "SEC") ||
                    NameEquals(names[index], "SECO") ||
                    NameEquals(names[index], "SECON") ||
                    NameEquals(names[index], "SECOND")) &&
                   values[index] < 60U) {
            second = (uint8_t)values[index];
        } else {
            return false;
        }
    }
    g_alarm_hour = hour;
    g_alarm_minute = minute;
    g_alarm_second = second;
    g_alarm_enabled = true;
    UARTPutString("#EVT:ALARM_ON\r\n");
    AlarmLatchCurrentMatch();
    return true;
}

static bool ProcessWeatherSet(void)
{
    uint8_t temp;
    uint8_t condition = WEATHER_CLOUD;

    if (!(g_uart_rx[0] == '*' && g_uart_rx[1] == 'S' &&
          g_uart_rx[2] == 'E' && g_uart_rx[3] == 'T' &&
          g_uart_rx[4] == ':' && g_uart_rx[5] == 'W' &&
          g_uart_rx[6] == 'E' && g_uart_rx[7] == 'A' &&
          g_uart_rx[8] == 'T' && g_uart_rx[9] == 'H' &&
          g_uart_rx[10] == 'E' && g_uart_rx[11] == 'R' &&
          g_uart_rx[12] == ' ')) {
        return false;
    }
    if (!Parse2(&g_uart_rx[13], &temp)) {
        return false;
    }
    if (g_uart_rx[15] == ' ') {
        if (g_uart_rx[16] == 'S' && g_uart_rx[17] == 'U' &&
            g_uart_rx[18] == 'N' && g_uart_rx[19] == '\0') {
            condition = WEATHER_SUN;
        } else if (g_uart_rx[16] == 'R' && g_uart_rx[17] == 'A' &&
                   g_uart_rx[18] == 'I' && g_uart_rx[19] == 'N' &&
                   g_uart_rx[20] == '\0') {
            condition = WEATHER_RAIN;
        } else if (g_uart_rx[16] == 'H' && g_uart_rx[17] == 'O' &&
                   g_uart_rx[18] == 'T' && g_uart_rx[19] == '\0') {
            condition = WEATHER_HOT;
        } else if (g_uart_rx[16] == 'C' && g_uart_rx[17] == 'L' &&
                   g_uart_rx[18] == 'O' && g_uart_rx[19] == 'U' &&
                   g_uart_rx[20] == 'D' && g_uart_rx[21] == '\0') {
            condition = WEATHER_CLOUD;
        } else {
            return false;
        }
    } else {
        return false;
    }
    WeatherSetMessage(temp, condition);
    return true;
}

static bool CommandEquals(const char *text)
{
    uint8_t index = 0U;

    while (text[index] != '\0' && g_uart_rx[index] == text[index]) {
        index++;
    }
    return text[index] == '\0' && g_uart_rx[index] == '\0';
}

static void SendState(void)
{
    g_uart_activity_until_ms = g_milliseconds + 50U;
    SendDisplayEvent();
    SendLedEvent();
    SendFlipEvent();
    SendFocusEvent(g_focus_active ? "ON" : "OFF");
}

static void SendDisplayEvent(void)
{
    uint8_t index;
    uint8_t dp = 0U;
    char chars[8];

    UARTPutString("#EVT:DISP ");
    if (!g_startup_complete) {
        if (g_startup_phase == 0U || g_startup_phase == 2U) {
            UARTPutString("88888888 FF\r\n");
        } else if (g_startup_phase == 4U) {
            UARTPutString("70910011 00\r\n");
        } else if (g_startup_phase == 6U) {
            UARTPutString("_JIANGSJ 00\r\n");
        } else if (g_startup_phase == 8U) {
            UARTPutString("__U1_0__ 08\r\n");
        } else {
            UARTPutString("________ 00\r\n");
        }
    } else if (!g_display_enabled) {
        UARTPutString("________ 00\r\n");
    } else if (g_edit_mode != EDIT_NONE) {
        bool blank = ((g_milliseconds / 250U) & 1U) == 0U;

        if (g_edit_mode == EDIT_DATE) {
            chars[0] = (char)('0' + (g_edit_year / 1000U) % 10U);
            chars[1] = (char)('0' + (g_edit_year / 100U) % 10U);
            chars[2] = (char)('0' + (g_edit_year / 10U) % 10U);
            chars[3] = (char)('0' + g_edit_year % 10U);
            chars[4] = (char)('0' + g_edit_month / 10U);
            chars[5] = (char)('0' + g_edit_month % 10U);
            chars[6] = (char)('0' + g_edit_day / 10U);
            chars[7] = (char)('0' + g_edit_day % 10U);
            dp = 0x08U;
            if (blank) {
                for (index = 0U; index < 8U; index++) {
                    if ((g_edit_field == 0U && index <= 3U) ||
                        (g_edit_field == 1U && index >= 4U && index <= 5U) ||
                        (g_edit_field == 2U && index >= 6U)) {
                        chars[index] = '_';
                    }
                }
            }
        } else {
            uint8_t edit_h = g_edit_mode == EDIT_TIME ? g_edit_hour :
                             g_edit_alarm_hour;
            uint8_t edit_m = g_edit_mode == EDIT_TIME ? g_edit_minute :
                             g_edit_alarm_minute;
            uint8_t edit_s = g_edit_mode == EDIT_TIME ? g_edit_second :
                             g_edit_alarm_second;

            chars[0] = (char)('0' + edit_h / 10U);
            chars[1] = (char)('0' + edit_h % 10U);
            chars[2] = '_';
            chars[3] = (char)('0' + edit_m / 10U);
            chars[4] = (char)('0' + edit_m % 10U);
            chars[5] = '_';
            chars[6] = (char)('0' + edit_s / 10U);
            chars[7] = (char)('0' + edit_s % 10U);
            dp = 0x12U;
            if (blank) {
                for (index = 0U; index < 8U; index++) {
                    if ((g_edit_field == 0U && index <= 1U) ||
                        (g_edit_field == 1U && index >= 3U && index <= 4U) ||
                        (g_edit_field == 2U && index >= 6U)) {
                        chars[index] = '_';
                    }
                }
            }
        }
        for (index = 0U; index < 8U; index++) {
            UARTCharPut(UART0_BASE, chars[index]);
        }
        UARTPutString(" ");
        UARTPutHex(dp);
        UARTPutString("\r\n");
    } else if (g_weather_length != 0U &&
               (int32_t)(g_weather_show_until_ms - g_milliseconds) > 0) {
        for (index = 0U; index < 8U; index++) {
            UARTCharPut(UART0_BASE, g_weather_chars[index]);
        }
        UARTPutString(" 00\r\n");
    } else if (g_night_mode) {
        UARTPut2(g_hour);
        UARTPut2(g_minute);
        UARTPutString("____ 02\r\n");
    } else if (g_scroll_length != 0U) {
        for (index = 0U; index < 8U; index++) {
            uint8_t source;
            if (g_scroll_length <= 8U) {
                source = index;
                chars[index] = source < g_scroll_length ?
                               g_scroll_chars[source] : '_';
                if (source < g_scroll_length &&
                    (g_scroll_codes[source] & 0x80U) != 0U) {
                    dp |= (uint8_t)(1U << index);
                }
            } else if (g_format_right) {
                uint8_t previous;
                source = (uint8_t)((g_scroll_offset + g_scroll_length -
                                   index) % g_scroll_length);
                chars[index] = g_scroll_chars[source];
                previous = (uint8_t)((source + 1U) % g_scroll_length);
                if (index != 0U &&
                    (g_scroll_codes[previous] & 0x80U) != 0U) {
                    dp |= (uint8_t)(1U << index);
                }
            } else {
                source = (uint8_t)((g_scroll_offset + index) %
                                   g_scroll_length);
                chars[index] = g_scroll_chars[source];
                if ((g_scroll_codes[source] & 0x80U) != 0U) {
                    dp |= (uint8_t)(1U << index);
                }
            }
            UARTCharPut(UART0_BASE, chars[index]);
        }
        UARTPutString(" ");
        UARTPutHex(dp);
        UARTPutString("\r\n");
    } else if (g_display_mode == 0U) {
        UARTPut2(g_hour);
        UARTPutString("_");
        UARTPut2(g_minute);
        UARTPutString("_");
        UARTPut2(g_second);
        UARTPutString(" 12\r\n");
    } else {
        UARTPut4(g_year);
        UARTPut2(g_month);
        UARTPut2(g_day);
        UARTPutString(" 08\r\n");
    }
}

static void SendLedEvent(void)
{
    UARTPutString("#EVT:LED ");
    UARTPutHex((uint8_t)~g_led_last_output);
    UARTPutString("\r\n");
}

static void SendKeyEvent(const char *name)
{
    UARTPutString("#EVT:KEY ");
    UARTPutString(name);
    UARTPutString("\r\n");
}

static void SendModeEvent(void)
{
    UARTPutString(g_night_mode ? "#EVT:MODE NIGHT\r\n" :
                                "#EVT:MODE DAY\r\n");
}

static void SendFlipEvent(void)
{
    UARTPutString(g_display_flip ? "#EVT:FLIP ON\r\n" :
                                  "#EVT:FLIP OFF\r\n");
}

static void SendFocusEvent(const char *state)
{
    UARTPutString("#EVT:FOCUS ");
    UARTPutString(state);
    UARTPutString(" ");
    UARTPut2(g_focus_duration_minutes);
    UARTPutString(" ");
    UARTPutUInt(g_focus_remaining_seconds);
    UARTPutString(" ");
    UARTPutUInt(g_focus_completed_count);
    UARTPutString("\r\n");
}

static void UARTPutString(const char *text)
{
    while (*text != '\0') {
        UARTCharPut(UART0_BASE, *text++);
        DisplayScan();
    }
}

static void UARTPut2(uint8_t value)
{
    UARTCharPut(UART0_BASE, (char)('0' + value / 10U));
    UARTCharPut(UART0_BASE, (char)('0' + value % 10U));
}

static void UARTPutTimeText(uint8_t hour, uint8_t minute, uint8_t second)
{
    if (g_format_right) {
        UARTPut2((uint8_t)(second % 10U * 10U + second / 10U));
        UARTPutString(".");
        UARTPut2((uint8_t)(minute % 10U * 10U + minute / 10U));
        UARTPutString(".");
        UARTPut2((uint8_t)(hour % 10U * 10U + hour / 10U));
    } else {
        UARTPut2(hour);
        UARTPutString(".");
        UARTPut2(minute);
        UARTPutString(".");
        UARTPut2(second);
    }
}

static void UARTPutDateText(uint16_t year, uint8_t month, uint8_t day)
{
    if (g_format_right) {
        UARTPut2((uint8_t)(day % 10U * 10U + day / 10U));
        UARTPutString(".");
        UARTPut2((uint8_t)(month % 10U * 10U + month / 10U));
        UARTPutString(".");
        UARTPut2((uint8_t)(year % 10U * 10U + (year / 10U) % 10U));
    } else {
        UARTPut2((uint8_t)(year % 100U));
        UARTPutString(".");
        UARTPut2(month);
        UARTPutString(".");
        UARTPut2(day);
    }
}

static void UARTPut4(uint16_t value)
{
    UARTCharPut(UART0_BASE, (char)('0' + (value / 1000U) % 10U));
    UARTCharPut(UART0_BASE, (char)('0' + (value / 100U) % 10U));
    UARTCharPut(UART0_BASE, (char)('0' + (value / 10U) % 10U));
    UARTCharPut(UART0_BASE, (char)('0' + value % 10U));
}

static void UARTPutHex(uint8_t value)
{
    static const char hex[] = "0123456789ABCDEF";
    UARTCharPut(UART0_BASE, hex[value >> 4]);
    UARTCharPut(UART0_BASE, hex[value & 0x0fU]);
}

static void UARTPutUInt(uint32_t value)
{
    char digits[10];
    uint8_t count = 0U;

    do {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    while (count != 0U) {
        UARTCharPut(UART0_BASE, digits[--count]);
    }
}

static void S800GPIOInit(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOJ);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOJ)) {
    }
    GPIOPinTypeGPIOInput(GPIO_PORTJ_BASE, GPIO_PIN_0 | GPIO_PIN_1);
    GPIOPadConfigSet(GPIO_PORTJ_BASE, GPIO_PIN_0 | GPIO_PIN_1,
                     GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);
}

static void S800I2CInit(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_I2C0);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_I2C0) ||
           !SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOB)) {
    }
    GPIOPinConfigure(GPIO_PB2_I2C0SCL);
    GPIOPinConfigure(GPIO_PB3_I2C0SDA);
    GPIOPinTypeI2CSCL(GPIO_PORTB_BASE, GPIO_PIN_2);
    GPIOPinTypeI2C(GPIO_PORTB_BASE, GPIO_PIN_3);
    I2CMasterInitExpClk(I2C0_BASE, g_system_clock, true);

    I2C0WriteByte(TCA6424_I2CADDR, TCA6424_CONFIG_PORT0, 0xffU);
    I2C0WriteByte(TCA6424_I2CADDR, TCA6424_CONFIG_PORT1, 0x00U);
    I2C0WriteByte(TCA6424_I2CADDR, TCA6424_CONFIG_PORT2, 0x00U);
    I2C0WriteByte(PCA9557_I2CADDR, PCA9557_CONFIG, 0x00U);
    I2C0WriteByte(PCA9557_I2CADDR, PCA9557_OUTPUT, 0xffU);
}

static void S800UARTInit(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_UART0) ||
           !SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOA)) {
    }
    GPIOPinConfigure(GPIO_PA0_U0RX);
    GPIOPinConfigure(GPIO_PA1_U0TX);
    GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);
    UARTConfigSetExpClk(UART0_BASE, g_system_clock, 115200U,
                       UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE |
                       UART_CONFIG_PAR_NONE);
    UARTIntEnable(UART0_BASE, UART_INT_RX | UART_INT_RT);
    IntEnable(INT_UART0);
}

static void BuzzerInit(void)
{
    SysCtlPeripheralEnable(BUZZER_GPIO_PERIPH);
    while (!SysCtlPeripheralReady(BUZZER_GPIO_PERIPH)) {
    }
    GPIOPinTypeGPIOOutput(BUZZER_GPIO_BASE, BUZZER_GPIO_PIN);
    BuzzerSet(false);
}

static void BuzzerDuty(uint8_t duty_div)
{
    (void)duty_div;
}

static void BuzzerTone(uint16_t freq)
{
    uint32_t load;

    if (freq == 0U) {
        BuzzerSet(false);
        return;
    }
    g_buzzer_current_freq = freq;
    load = g_system_clock / (2U * (uint32_t)freq);
    if (load < 2U) {
        load = 2U;
    }
    TimerLoadSet(TIMER0_BASE, TIMER_A, load - 1U);
}

static void BuzzerSet(bool enabled)
{
    if (g_buzzer_enabled == enabled) {
        return;
    }
    g_buzzer_enabled = enabled;
    if (enabled) {
        BuzzerTone(g_buzzer_current_freq);
        TimerEnable(TIMER0_BASE, TIMER_A);
    } else {
        TimerDisable(TIMER0_BASE, TIMER_A);
        g_buzzer_level = false;
        GPIOPinWrite(BUZZER_GPIO_BASE, BUZZER_GPIO_PIN, 0U);
    }
}

static void MusicTimerInit(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER1);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_TIMER0) ||
           !SysCtlPeripheralReady(SYSCTL_PERIPH_TIMER1)) {
    }

    TimerConfigure(TIMER0_BASE, TIMER_CFG_PERIODIC);
    TimerLoadSet(TIMER0_BASE, TIMER_A,
                 (g_system_clock / (2U * BUZZER_DEFAULT_FREQ_HZ)) - 1U);
    TimerIntClear(TIMER0_BASE, TIMER_TIMA_TIMEOUT);
    TimerIntEnable(TIMER0_BASE, TIMER_TIMA_TIMEOUT);
    IntPrioritySet(INT_TIMER0A, 0x20U);
    IntEnable(INT_TIMER0A);

    TimerConfigure(TIMER1_BASE, TIMER_CFG_PERIODIC);
    TimerLoadSet(TIMER1_BASE, TIMER_A, (g_system_clock / 1000U) - 1U);
    TimerIntClear(TIMER1_BASE, TIMER_TIMA_TIMEOUT);
    TimerIntEnable(TIMER1_BASE, TIMER_TIMA_TIMEOUT);
    IntPrioritySet(INT_TIMER1A, 0x40U);
    IntEnable(INT_TIMER1A);
}

static uint8_t I2C0WriteByte(uint8_t device, uint8_t reg, uint8_t data)
{
    while (I2CMasterBusy(I2C0_BASE)) {
    }
    I2CMasterSlaveAddrSet(I2C0_BASE, device, false);
    I2CMasterDataPut(I2C0_BASE, reg);
    I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_BURST_SEND_START);
    while (I2CMasterBusy(I2C0_BASE)) {
    }
    I2CMasterDataPut(I2C0_BASE, data);
    I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_BURST_SEND_FINISH);
    while (I2CMasterBusy(I2C0_BASE)) {
    }
    return (uint8_t)I2CMasterErr(I2C0_BASE);
}

static uint8_t I2C0ReadByte(uint8_t device, uint8_t reg)
{
    while (I2CMasterBusy(I2C0_BASE)) {
    }
    I2CMasterSlaveAddrSet(I2C0_BASE, device, false);
    I2CMasterDataPut(I2C0_BASE, reg);
    I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_SINGLE_SEND);
    while (I2CMasterBusy(I2C0_BASE)) {
    }
    I2CMasterSlaveAddrSet(I2C0_BASE, device, true);
    I2CMasterControl(I2C0_BASE, I2C_MASTER_CMD_SINGLE_RECEIVE);
    while (I2CMasterBusy(I2C0_BASE)) {
    }
    return (uint8_t)I2CMasterDataGet(I2C0_BASE);
}

void SysTick_Handler(void)
{
    g_milliseconds++;
    if ((g_milliseconds % 5U) == 0U) {
        g_keys_due = true;
    }
    if ((g_milliseconds % 1000U) == 0U) {
        g_second_due = true;
    }
}

void TIMER0A_Handler(void)
{
    TimerIntClear(TIMER0_BASE, TIMER_TIMA_TIMEOUT);
    if (g_buzzer_enabled) {
        g_buzzer_level = !g_buzzer_level;
        GPIOPinWrite(BUZZER_GPIO_BASE, BUZZER_GPIO_PIN,
                     g_buzzer_level ? BUZZER_GPIO_PIN : 0U);
    } else {
        GPIOPinWrite(BUZZER_GPIO_BASE, BUZZER_GPIO_PIN, 0U);
    }
}

void TIMER1A_Handler(void)
{
    TimerIntClear(TIMER1_BASE, TIMER_TIMA_TIMEOUT);
    if (!g_music_active) {
        TimerDisable(TIMER1_BASE, TIMER_A);
        return;
    }
    if (g_music_note_duration_ms == 0U) {
        MusicLoadNextNote();
        return;
    }
    g_music_note_elapsed_ms++;
    if (g_music_note_elapsed_ms >= g_music_note_duration_ms) {
        MusicLoadNextNote();
    }
}

void UART0_Handler(void)
{
    uint32_t status = UARTIntStatus(UART0_BASE, true);
    char received;

    UARTIntClear(UART0_BASE, status);
    while (UARTCharsAvail(UART0_BASE)) {
        received = (char)UARTCharGetNonBlocking(UART0_BASE);
        if ((received == '\r' || received == '\n') &&
            g_uart_rx_length != 0U) {
            g_uart_rx[g_uart_rx_length] = '\0';
            g_command_ready = true;
        } else if (!g_command_ready &&
                   (received >= ' ' || received == '\t') &&
                   g_uart_rx_length < (UART_RX_BUFFER_SIZE - 1U)) {
            bool preserve_message =
                g_uart_rx_length >= 9U &&
                g_uart_rx[0] == '*' && g_uart_rx[1] == 'S' &&
                g_uart_rx[2] == 'E' && g_uart_rx[3] == 'T' &&
                g_uart_rx[4] == ':' && g_uart_rx[5] == 'M' &&
                g_uart_rx[6] == 'S' && g_uart_rx[7] == 'G' &&
                g_uart_rx[8] == ' ';
            if (received == '\t') {
                received = ' ';
            }
            if (!preserve_message && received >= 'a' && received <= 'z') {
                received = (char)(received - 'a' + 'A');
            }
            if (received != ' ' || g_uart_rx_length == 0U ||
                g_uart_rx[g_uart_rx_length - 1U] != ' ' ||
                preserve_message) {
                g_uart_rx[g_uart_rx_length++] = received;
            }
            g_uart_activity_until_ms = g_milliseconds + 100U;
        } else if (!g_command_ready && (received >= ' ' || received == '\t')) {
            g_uart_rx_overflow = true;
        }
    }
}
