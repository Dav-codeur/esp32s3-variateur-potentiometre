#include <stdio.h>
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#define POT_CHANNEL ADC_CHANNEL_4   // GPIO5 sur ESP32-S3
#define LED_GPIO    18
#define BTN_GPIO    4
#define PWM_MAX     4095
#define VMAX_MV     3159
#define SEUIL_MV    50
#define DEBOUNCE_MS 50

static adc_oneshot_unit_handle_t adc;
static adc_cali_handle_t cali;
static TimerHandle_t debounce_timer;

static volatile int g_raw = 0;
static volatile int g_mv = 0;
static volatile int mode_corrige = 0;   // 0 = linéaire, 1 = corrigé

// Appelée 50 ms après le premier front : le rebond est fini
static void debounce_cb(TimerHandle_t t)
{
    if (gpio_get_level(BTN_GPIO) == 0) {   // toujours appuyé : appui valide
        mode_corrige = !mode_corrige;
    }
    gpio_intr_enable(BTN_GPIO);
}

// Interruption sur front descendant : on coupe l'interruption et on lance le timer
static void IRAM_ATTR button_isr(void *arg)
{
    BaseType_t woken = pdFALSE;
    gpio_intr_disable(BTN_GPIO);
    xTimerStartFromISR(debounce_timer, &woken);
    if (woken) portYIELD_FROM_ISR();
}

static void button_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BTN_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    debounce_timer = xTimerCreate("debounce", pdMS_TO_TICKS(DEBOUNCE_MS),
                                  pdFALSE, NULL, debounce_cb);   // pdFALSE = one-shot

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BTN_GPIO, button_isr, NULL));
}

static void pwm_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_12_BIT,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t ch_cfg = {
        .gpio_num = LED_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));
}

// Timer 20 ms : lecture ADC, calcul du rapport cyclique, mise à jour de la PWM
static void sample_cb(void *arg)
{
    int raw, mv;
    if (adc_oneshot_read(adc, POT_CHANNEL, &raw) != ESP_OK) return;
    adc_cali_raw_to_voltage(cali, raw, &mv);
    g_raw = raw;
    g_mv = mv;

    int duty = 0;
    if (mv > SEUIL_MV) {
        duty = (mv - SEUIL_MV) * PWM_MAX / (VMAX_MV - SEUIL_MV);
    }
    if (duty > PWM_MAX) duty = PWM_MAX;

    if (mode_corrige) {
        duty = (duty * duty) / PWM_MAX;   // carré, ramené à 0-4095
    }

    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void app_main(void)
{
    pwm_init();
    button_init();

    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, POT_CHANNEL, &chan_cfg));

    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali));

    const esp_timer_create_args_t targs = {
        .callback = sample_cb,
        .name = "adc_sample",
    };
    esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_create(&targs, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, 20 * 1000));

    while (1) {
        printf("mode=%s  raw=%d  tension=%d mV\n",
               mode_corrige ? "corrige" : "lineaire", g_raw, g_mv);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}