#include "furi_hal_subghz.h"
#include "furi_hal_subghz_config.h"
#include "furi_hal_version.h"
#include "furi_hal_rtc.h"
#include "furi_hal_delay.h"

#include <furi_hal_gpio.h>
#include <furi_hal_spi.h>
#include <furi_hal_interrupt.h>
#include <furi_hal_resources.h>

#include <stm32wbxx_ll_dma.h>

#include <furi.h>
#include <si446x.h>
#include <stdio.h>

#define TAG "FuriHalSubGhz"

//https://www.silabs.com/documents/public/application-notes/AN633.pdf

static volatile SubGhzState furi_hal_subghz_state = SubGhzStateInit;
static volatile SubGhzRegulation furi_hal_subghz_regulation = SubGhzRegulationTxRx;
static volatile FuriHalSubGhzPreset furi_hal_subghz_preset = FuriHalSubGhzPresetIDLE;

// Apply the radio configuration
void furi_hal_subghz_load_config(const uint8_t config[]) {
    uint8_t buff[17];
    uint8_t buff_tx[2] = {SI446X_CMD_READ_CMD_BUFF, 0xFF};
    uint8_t buff_rx[2] = {0};
    uint16_t i = 0;
    while(config[i]) {
        memcpy(buff, &config[i], sizeof(buff));

        furi_hal_spi_acquire(&furi_hal_spi_bus_handle_subghz);
        furi_hal_spi_bus_tx(&furi_hal_spi_bus_handle_subghz, &buff[1], buff[0], SI446X_TIMEOUT);
        furi_hal_spi_release(&furi_hal_spi_bus_handle_subghz);

        buff_rx[1] = 0;
        while(buff_rx[1] != SI446X_CTS_OK) {
            furi_hal_spi_acquire(&furi_hal_spi_bus_handle_subghz);
            furi_hal_spi_bus_trx(
                &furi_hal_spi_bus_handle_subghz, buff_tx, (uint8_t*)buff_rx, 2, SI446X_TIMEOUT);
            furi_hal_spi_release(&furi_hal_spi_bus_handle_subghz);
        }

        i += buff[0];
        i++;
    }
    si446x_clear_interrupt_status(&furi_hal_spi_bus_handle_subghz);
}

static void furi_hal_subghz_mod_gpio_for_async(SI446X_Prop_Modem_Mod_Type_t modulation) {
    //ASYNC	1	Direct mode operates in asynchronous mode, applies to TX only. GFSK is not supported.
    uint8_t modem_mod[1] = {0};

    switch(modulation) {
    case SI446X_MODEM_MOD_TYPE_MOD_TYPE_CW:
        modem_mod[0] =
            (SI446X_MODEM_MOD_TYPE_TX_DIRECT_MODE_TYPE_ASYNCHRONOUS |
             SI446X_MODEM_MOD_TYPE_TX_DIRECT_MODE_GPIO1 |
             SI446X_MODEM_MOD_TYPE_MOD_SOURCE_DIRECT_MODE | SI446X_MODEM_MOD_TYPE_MOD_TYPE_CW);
        break;
    case SI446X_MODEM_MOD_TYPE_MOD_TYPE_OOK:
        modem_mod[0] =
            (SI446X_MODEM_MOD_TYPE_TX_DIRECT_MODE_TYPE_ASYNCHRONOUS |
             SI446X_MODEM_MOD_TYPE_TX_DIRECT_MODE_GPIO1 |
             SI446X_MODEM_MOD_TYPE_MOD_SOURCE_DIRECT_MODE | SI446X_MODEM_MOD_TYPE_MOD_TYPE_OOK);
        break;
    case SI446X_MODEM_MOD_TYPE_MOD_TYPE_2FSK:
        modem_mod[0] =
            (SI446X_MODEM_MOD_TYPE_TX_DIRECT_MODE_TYPE_ASYNCHRONOUS |
             SI446X_MODEM_MOD_TYPE_TX_DIRECT_MODE_GPIO1 |
             SI446X_MODEM_MOD_TYPE_MOD_SOURCE_DIRECT_MODE | SI446X_MODEM_MOD_TYPE_MOD_TYPE_2FSK);
        break;
        break;
    case SI446X_MODEM_MOD_TYPE_MOD_TYPE_4FSK:
        modem_mod[0] =
            (SI446X_MODEM_MOD_TYPE_TX_DIRECT_MODE_TYPE_ASYNCHRONOUS |
             SI446X_MODEM_MOD_TYPE_TX_DIRECT_MODE_GPIO1 |
             SI446X_MODEM_MOD_TYPE_MOD_SOURCE_DIRECT_MODE | SI446X_MODEM_MOD_TYPE_MOD_TYPE_4FSK);
        break;
    default:
        furi_crash(NULL);
        break;
    }
    si446x_set_properties(
        &furi_hal_spi_bus_handle_subghz,
        SI446X_PROP_MODEM_MOD_TYPE,
        &modem_mod[0],
        sizeof(modem_mod));
}

void furi_hal_subghz_init() {
    furi_assert(furi_hal_subghz_state == SubGhzStateInit);
    furi_hal_subghz_state = SubGhzStateIdle;
    furi_hal_subghz_preset = FuriHalSubGhzPresetIDLE;

    furi_hal_subghz_reset();

#ifdef FURI_HAL_SUBGHZ_TX_GPIO
    furi_hal_gpio_init(&FURI_HAL_SUBGHZ_TX_GPIO, GpioModeOutputPushPull, GpioPullNo, GpioSpeedLow);
#endif

    furi_hal_subghz_load_config(furi_hal_subghz_preset_ook_650khz_async_regs);
    furi_hal_subghz_dump_state();
    uint8_t buff_tx[] = {SI446X_CMD_FUNC_INFO};
    uint8_t buff_rx[6] = {0};
    si446x_write_data(&furi_hal_spi_bus_handle_subghz, &buff_tx[0], sizeof(buff_tx));
    si446x_read_data(&furi_hal_spi_bus_handle_subghz, &buff_rx[0], sizeof(buff_rx));

    furi_hal_gpio_init(&gpio_cc1101_g0, GpioModeAnalog, GpioPullNo, GpioSpeedLow);

    //ToDo think about where to tie
    si446x_set_pa(&furi_hal_spi_bus_handle_subghz, SI446X_SET_MAX_PA);
    furi_hal_subghz_mod_gpio_for_async(SI446X_MODEM_MOD_TYPE_MOD_TYPE_OOK);

    uint8_t pa_mode[1] = {0x88};
    si446x_set_properties(
        &furi_hal_spi_bus_handle_subghz, SI446X_PROP_PA_MODE, &pa_mode[0], sizeof(pa_mode));
    si446x_write_gpio(&furi_hal_spi_bus_handle_subghz, SI446X_GPIO0, SI446X_GPIO_MODE_TX_STATE);

    furi_hal_subghz_sleep();

    FURI_LOG_I(TAG, "Init OK");
}

void furi_hal_subghz_sleep() {
    //furi_assert(furi_hal_subghz_state == SubGhzStateIdle);
    //ToDo sometimes freezes when exiting sleep mode
    // si446x_write_gpio(&furi_hal_spi_bus_handle_subghz, SI446X_GPIO1, SI446X_GPIO_MODE_INPUT);
    // si446x_clear_interrupt_status(&furi_hal_spi_bus_handle_subghz);
    // si446x_set_state(&furi_hal_spi_bus_handle_subghz, SI446X_STATE_SLEEP);
    // furi_hal_gpio_init(&gpio_cc1101_g0, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
    //furi_hal_subghz_preset = FuriHalSubGhzPresetIDLE;
    furi_hal_subghz_shutdown();
}

void furi_hal_subghz_dump_state() {
    printf(
        "[furi_hal_subghz] si446x chip %X, version %X\r\n",
        si446x_get_partnumber(&furi_hal_spi_bus_handle_subghz),
        si446x_get_version(&furi_hal_spi_bus_handle_subghz));
    si446x_clear_interrupt_status(&furi_hal_spi_bus_handle_subghz);
}

void furi_hal_subghz_load_preset(FuriHalSubGhzPreset preset) {
    //ToDo need Reset?
    //download with evaluation takes 200ms without calibration 20ms
    switch(preset) {
    case FuriHalSubGhzPresetOok650Async:
        furi_hal_subghz_load_config(furi_hal_subghz_preset_ook_650khz_async_regs);
        furi_hal_subghz_mod_gpio_for_async(SI446X_MODEM_MOD_TYPE_MOD_TYPE_OOK);
        break;
    case FuriHalSubGhzPresetOok270Async:
        furi_hal_subghz_load_config(furi_hal_subghz_preset_ook_270khz_async_regs);
        furi_hal_subghz_mod_gpio_for_async(SI446X_MODEM_MOD_TYPE_MOD_TYPE_OOK);
        break;
    case FuriHalSubGhzPreset2FSKDev238Async:
        furi_hal_subghz_load_config(furi_hal_subghz_preset_2fsk_dev2_38khz_async_regs);
        furi_hal_subghz_mod_gpio_for_async(SI446X_MODEM_MOD_TYPE_MOD_TYPE_2FSK);
        break;
    case FuriHalSubGhzPreset2FSKDev476Async:
        furi_hal_subghz_load_config(furi_hal_subghz_preset_2fsk_dev4_76khz_async_regs);
        furi_hal_subghz_mod_gpio_for_async(SI446X_MODEM_MOD_TYPE_MOD_TYPE_2FSK);
        break;
    case FuriHalSubGhzPresetOok650AsyncFreq:
        furi_hal_subghz_load_config(furi_hal_subghz_preset_ook_650khz_async_for_freq_regs);
        furi_hal_subghz_mod_gpio_for_async(SI446X_MODEM_MOD_TYPE_MOD_TYPE_OOK);
        break;
    // case FuriHalSubGhzPresetMSK99_97KbAsync:
    //     furi_hal_subghz_load_config(furi_hal_subghz_preset_msk_99_97kb_async_regs);
    //     //furi_hal_subghz_mod_gpio_for_async(SI446X_MODEM_MOD_TYPE_MOD_TYPE_OOK);
    //     break;
    // case FuriHalSubGhzPresetGFSK9_99KbAsync:
    //     furi_hal_subghz_load_config(furi_hal_subghz_preset_gfsk_9_99kb_async_regs);
    //     //furi_hal_subghz_mod_gpio_for_async(SI446X_MODEM_MOD_TYPE_MOD_TYPE_OOK);
    //     break;
    default:
        furi_crash(NULL);
        break;
    }

    si446x_set_pa(&furi_hal_spi_bus_handle_subghz, SI446X_SET_MAX_PA);

    furi_hal_subghz_preset = preset;
    furi_hal_subghz_state = SubGhzStateIdle;
}

void furi_hal_subghz_load_registers(const uint8_t data[][2]) {
    // furi_hal_spi_acquire(&furi_hal_spi_bus_handle_subghz);
    // cc1101_reset(&furi_hal_spi_bus_handle_subghz);
    // uint32_t i = 0;
    // while(data[i][0]) {
    //     cc1101_write_reg(&furi_hal_spi_bus_handle_subghz, data[i][0], data[i][1]);
    //     i++;
    // }
    // furi_hal_spi_release(&furi_hal_spi_bus_handle_subghz);
}

void furi_hal_subghz_load_patable(const uint8_t data[8]) {
    // furi_hal_spi_acquire(&furi_hal_spi_bus_handle_subghz);
    // cc1101_set_pa_table(&furi_hal_spi_bus_handle_subghz, data);
    // furi_hal_spi_release(&furi_hal_spi_bus_handle_subghz);
}

void furi_hal_subghz_write_packet(const uint8_t* data, uint8_t size) {
    // furi_hal_spi_acquire(&furi_hal_spi_bus_handle_subghz);
    // cc1101_flush_tx(&furi_hal_spi_bus_handle_subghz);
    // cc1101_write_reg(&furi_hal_spi_bus_handle_subghz, CC1101_FIFO, size);
    // cc1101_write_fifo(&furi_hal_spi_bus_handle_subghz, data, size);
    // furi_hal_spi_release(&furi_hal_spi_bus_handle_subghz);
}

void furi_hal_subghz_flush_rx() {
    // furi_hal_spi_acquire(&furi_hal_spi_bus_handle_subghz);
    // cc1101_flush_rx(&furi_hal_spi_bus_handle_subghz);
    // furi_hal_spi_release(&furi_hal_spi_bus_handle_subghz);
}

void furi_hal_subghz_flush_tx() {
    // furi_hal_spi_acquire(&furi_hal_spi_bus_handle_subghz);
    // cc1101_flush_tx(&furi_hal_spi_bus_handle_subghz);
    // furi_hal_spi_release(&furi_hal_spi_bus_handle_subghz);
}

bool furi_hal_subghz_rx_pipe_not_empty() {
    // CC1101RxBytes status[1];
    // furi_hal_spi_acquire(&furi_hal_spi_bus_handle_subghz);
    // cc1101_read_reg(
    //     &furi_hal_spi_bus_handle_subghz, (CC1101_STATUS_RXBYTES) | CC1101_BURST, (uint8_t*)status);
    // furi_hal_spi_release(&furi_hal_spi_bus_handle_subghz);
    // // TODO: you can add a buffer overflow flag if needed
    // if(status->NUM_RXBYTES > 0) {
    //     return true;
    // } else {
    //     return false;
    // }

    return true;
}

bool furi_hal_subghz_is_rx_data_crc_valid() {
    // furi_hal_spi_acquire(&furi_hal_spi_bus_handle_subghz);
    // uint8_t data[1];
    // cc1101_read_reg(&furi_hal_spi_bus_handle_subghz, CC1101_STATUS_LQI | CC1101_BURST, data);
    // furi_hal_spi_release(&furi_hal_spi_bus_handle_subghz);
    // if(((data[0] >> 7) & 0x01)) {
    //     return true;
    // } else {
    //     return false;
    // }
    return true;
}

void furi_hal_subghz_read_packet(uint8_t* data, uint8_t* size) {
    // furi_hal_spi_acquire(&furi_hal_spi_bus_handle_subghz);
    // cc1101_read_fifo(&furi_hal_spi_bus_handle_subghz, data, size);
    // furi_hal_spi_release(&furi_hal_spi_bus_handle_subghz);
}

void furi_hal_subghz_shutdown() {
    furi_hal_gpio_write(&gpio_rf_sw_0, true); //nSDN UP
    furi_hal_subghz_preset = FuriHalSubGhzPresetIDLE;
    furi_hal_subghz_state = SubGhzStateInit;
}

void furi_hal_subghz_reset() {
    furi_hal_gpio_init(&gpio_rf_sw_0, GpioModeOutputPushPull, GpioPullDown, GpioSpeedLow);
    furi_hal_gpio_init(&gpio_cc1101_g0, GpioModeInput, GpioPullDown, GpioSpeedLow); //active go0
    // Reset
    furi_hal_gpio_write(&gpio_rf_sw_0, true); //nSDN UP
    furi_hal_delay_us(SI446X_TIMEOUT_NSDN);
    furi_hal_gpio_write(&gpio_rf_sw_0, false); //nSDN DOWN

    //wait CTS
    while(furi_hal_gpio_read(&gpio_cc1101_g0) == false)
        ;
    furi_hal_subghz_state = SubGhzStateInit;
    furi_hal_subghz_preset = FuriHalSubGhzPresetIDLE;
}

void furi_hal_subghz_idle() {
    //ToDo crutch, GO0 should be low at the time of disengagement
    furi_hal_gpio_init(&gpio_cc1101_g0, GpioModeOutputPushPull, GpioPullDown, GpioSpeedLow);
    furi_hal_gpio_write(&gpio_cc1101_g0, false); //DOWN
    si446x_switch_to_idle(&furi_hal_spi_bus_handle_subghz);
    si446x_clear_interrupt_status(&furi_hal_spi_bus_handle_subghz);
    //si446x_write_gpio(&furi_hal_spi_bus_handle_subghz, SI446X_GPIO1, SI446X_GPIO_MODE_INPUT);
}

void furi_hal_subghz_rx() {
    si446x_write_gpio(&furi_hal_spi_bus_handle_subghz, SI446X_GPIO1, SI446X_GPIO_MODE_RX_DATA);
    si446x_clear_interrupt_status(&furi_hal_spi_bus_handle_subghz);
    uint8_t channel = 0;
    si446x_switch_to_start_rx(&furi_hal_spi_bus_handle_subghz, channel, SI446X_STATE_NOCHANGE, 0);
}

bool furi_hal_subghz_tx() {
    if(furi_hal_subghz_regulation != SubGhzRegulationTxRx) return false;
    si446x_write_gpio(&furi_hal_spi_bus_handle_subghz, SI446X_GPIO1, SI446X_GPIO_MODE_INPUT);

    si446x_clear_interrupt_status(&furi_hal_spi_bus_handle_subghz);
    uint8_t channel = 0;
    return si446x_switch_to_start_tx(
        &furi_hal_spi_bus_handle_subghz, channel, SI446X_STATE_NOCHANGE, 0);
}

float furi_hal_subghz_get_rssi() {
    float rssi = (float)si446x_get_get_rssi(&furi_hal_spi_bus_handle_subghz);
    rssi = (rssi / 2.0f) - 134.0f;
    return rssi;
}

uint8_t furi_hal_subghz_get_lqi() {
    return si446x_get_get_lqi(&furi_hal_spi_bus_handle_subghz);
}

bool furi_hal_subghz_is_frequency_valid(uint32_t value) {
    if(!(value >= 299999755 && value <= 348000335) &&
       !(value >= 386999938 && value <= 464000000) &&
       !(value >= 778999847 && value <= 928000000)) {
        return false;
    }

    return true;
}

uint32_t furi_hal_subghz_set_frequency_and_path(uint32_t value) {
    value = furi_hal_subghz_set_frequency(value);
    if(value >= 299999755 && value <= 348000335) {
        furi_hal_subghz_set_path(FuriHalSubGhzPath315);
    } else if(value >= 386999938 && value <= 464000000) {
        furi_hal_subghz_set_path(FuriHalSubGhzPath433);
    } else if(value >= 778999847 && value <= 928000000) {
        furi_hal_subghz_set_path(FuriHalSubGhzPath868);
    } else {
        furi_crash(NULL);
    }
    return value;
}

bool furi_hal_subghz_is_tx_allowed(uint32_t value) {
    //checking regional settings
    bool is_allowed = false;
    switch(furi_hal_version_get_hw_region()) {
    case FuriHalVersionRegionEuRu:
        //433,05..434,79; 868,15..868,55
        if(!(value >= 433050000 && value <= 434790000) &&
           !(value >= 868150000 && value <= 868550000)) {
        } else {
            is_allowed = true;
        }
        break;
    case FuriHalVersionRegionUsCaAu:
        //304,10..321,95; 433,05..434,79; 915,00..928,00
        if(!(value >= 304100000 && value <= 321950000) &&
           !(value >= 433050000 && value <= 434790000) &&
           !(value >= 915000000 && value <= 928000000)) {
        } else {
            if(furi_hal_rtc_is_flag_set(FuriHalRtcFlagDebug)) {
                if((value >= 304100000 && value <= 321950000) &&
                   ((furi_hal_subghz_preset == FuriHalSubGhzPresetOok270Async) ||
                    (furi_hal_subghz_preset == FuriHalSubGhzPresetOok650Async))) {
                    //ToDo set maximum transmit power for australia
                    //si446x_set_pa(&furi_hal_spi_bus_handle_subghz, SI446X_SET_MAX_PA);
                }
            }
            is_allowed = true;
        }
        break;
    case FuriHalVersionRegionJp:
        //312,00..315,25; 920,50..923,50
        if(!(value >= 312000000 && value <= 315250000) &&
           !(value >= 920500000 && value <= 923500000)) {
        } else {
            is_allowed = true;
        }
        break;

    default:
        is_allowed = true;
        break;
    }
    return is_allowed;
}

uint32_t furi_hal_subghz_set_frequency(uint32_t value) {
    if(furi_hal_subghz_is_tx_allowed(value)) {
        furi_hal_subghz_regulation = SubGhzRegulationTxRx;
    } else {
        furi_hal_subghz_regulation = SubGhzRegulationOnlyRx;
    }
    uint32_t real_frequency =
        si446x_set_frequency_and_step_channel(&furi_hal_spi_bus_handle_subghz, value, 250000);
    return real_frequency;
}

void furi_hal_subghz_set_path(FuriHalSubGhzPath path) {
    //Path_433      sw_0-0 sw_1-1
    //Path_315      sw_0-1 sw_1-0
    //Path_868      sw_0-1 sw_1-1
    //Path_Isolate  sw_0-0 sw_1-0
    uint8_t pa_mode[1] = {0x88};
    si446x_set_properties(
        &furi_hal_spi_bus_handle_subghz, SI446X_PROP_PA_MODE, &pa_mode[0], sizeof(pa_mode));
    si446x_write_gpio(&furi_hal_spi_bus_handle_subghz, SI446X_GPIO0, SI446X_GPIO_MODE_TX_STATE);

    if(path == FuriHalSubGhzPath433) {
        si446x_write_sw(
            &furi_hal_spi_bus_handle_subghz,
            SI446X_GPIO2,
            SI446X_GPIO_MODE_DRIVE0,
            SI446X_GPIO3,
            SI446X_GPIO_MODE_DRIVE1);
    } else if(path == FuriHalSubGhzPath315) {
        si446x_write_sw(
            &furi_hal_spi_bus_handle_subghz,
            SI446X_GPIO2,
            SI446X_GPIO_MODE_DRIVE1,
            SI446X_GPIO3,
            SI446X_GPIO_MODE_DRIVE0);
    } else if(path == FuriHalSubGhzPath868) {
        si446x_write_sw(
            &furi_hal_spi_bus_handle_subghz,
            SI446X_GPIO2,
            SI446X_GPIO_MODE_DRIVE1,
            SI446X_GPIO3,
            SI446X_GPIO_MODE_DRIVE1);
    } else if(path == FuriHalSubGhzPathIsolate) {
        si446x_write_sw(
            &furi_hal_spi_bus_handle_subghz,
            SI446X_GPIO2,
            SI446X_GPIO_MODE_DRIVE0,
            SI446X_GPIO3,
            SI446X_GPIO_MODE_DRIVE0);
    } else {
        furi_crash(NULL);
    }
}

volatile uint32_t furi_hal_subghz_capture_delta_duration = 0;
volatile FuriHalSubGhzCaptureCallback furi_hal_subghz_capture_callback = NULL;
volatile void* furi_hal_subghz_capture_callback_context = NULL;

static void furi_hal_subghz_capture_ISR() {
    // Channel 1
    if(LL_TIM_IsActiveFlag_CC1(TIM2)) {
        LL_TIM_ClearFlag_CC1(TIM2);
        furi_hal_subghz_capture_delta_duration = LL_TIM_IC_GetCaptureCH1(TIM2);
        if(furi_hal_subghz_capture_callback) {
            furi_hal_subghz_capture_callback(
                true,
                furi_hal_subghz_capture_delta_duration,
                (void*)furi_hal_subghz_capture_callback_context);
        }
    }
    // Channel 2
    if(LL_TIM_IsActiveFlag_CC2(TIM2)) {
        LL_TIM_ClearFlag_CC2(TIM2);
        if(furi_hal_subghz_capture_callback) {
            furi_hal_subghz_capture_callback(
                false,
                LL_TIM_IC_GetCaptureCH2(TIM2) - furi_hal_subghz_capture_delta_duration,
                (void*)furi_hal_subghz_capture_callback_context);
        }
    }
}

void furi_hal_subghz_start_async_rx(FuriHalSubGhzCaptureCallback callback, void* context) {
    furi_assert(furi_hal_subghz_state == SubGhzStateIdle);
    furi_hal_subghz_state = SubGhzStateAsyncRx;

    furi_hal_subghz_capture_callback = callback;
    furi_hal_subghz_capture_callback_context = context;

    furi_hal_gpio_init_ex(
        &gpio_cc1101_g0, GpioModeAltFunctionPushPull, GpioPullNo, GpioSpeedLow, GpioAltFn1TIM2);

    // Timer: base
    LL_TIM_InitTypeDef TIM_InitStruct = {0};
    TIM_InitStruct.Prescaler = 64 - 1;
    TIM_InitStruct.CounterMode = LL_TIM_COUNTERMODE_UP;
    TIM_InitStruct.Autoreload = 0x7FFFFFFE;
    TIM_InitStruct.ClockDivision = LL_TIM_CLOCKDIVISION_DIV4;
    LL_TIM_Init(TIM2, &TIM_InitStruct);

    // Timer: advanced
    LL_TIM_SetClockSource(TIM2, LL_TIM_CLOCKSOURCE_INTERNAL);
    LL_TIM_DisableARRPreload(TIM2);
    LL_TIM_SetTriggerInput(TIM2, LL_TIM_TS_TI2FP2);
    LL_TIM_SetSlaveMode(TIM2, LL_TIM_SLAVEMODE_RESET);
    LL_TIM_SetTriggerOutput(TIM2, LL_TIM_TRGO_RESET);
    LL_TIM_EnableMasterSlaveMode(TIM2);
    LL_TIM_DisableDMAReq_TRIG(TIM2);
    LL_TIM_DisableIT_TRIG(TIM2);

    // Timer: channel 1 indirect
    LL_TIM_IC_SetActiveInput(TIM2, LL_TIM_CHANNEL_CH1, LL_TIM_ACTIVEINPUT_INDIRECTTI);
    LL_TIM_IC_SetPrescaler(TIM2, LL_TIM_CHANNEL_CH1, LL_TIM_ICPSC_DIV1);
    LL_TIM_IC_SetPolarity(TIM2, LL_TIM_CHANNEL_CH1, LL_TIM_IC_POLARITY_FALLING);
    LL_TIM_IC_SetFilter(TIM2, LL_TIM_CHANNEL_CH1, LL_TIM_IC_FILTER_FDIV1);

    // Timer: channel 2 direct
    LL_TIM_IC_SetActiveInput(TIM2, LL_TIM_CHANNEL_CH2, LL_TIM_ACTIVEINPUT_DIRECTTI);
    LL_TIM_IC_SetPrescaler(TIM2, LL_TIM_CHANNEL_CH2, LL_TIM_ICPSC_DIV1);
    LL_TIM_IC_SetPolarity(TIM2, LL_TIM_CHANNEL_CH2, LL_TIM_IC_POLARITY_RISING);
    LL_TIM_IC_SetFilter(TIM2, LL_TIM_CHANNEL_CH2, LL_TIM_IC_FILTER_FDIV32_N8);

    // ISR setup
    furi_hal_interrupt_set_isr(FuriHalInterruptIdTIM2, furi_hal_subghz_capture_ISR, NULL);

    // Interrupts and channels
    LL_TIM_EnableIT_CC1(TIM2);
    LL_TIM_EnableIT_CC2(TIM2);
    LL_TIM_CC_EnableChannel(TIM2, LL_TIM_CHANNEL_CH1);
    LL_TIM_CC_EnableChannel(TIM2, LL_TIM_CHANNEL_CH2);

    // Start timer
    LL_TIM_SetCounter(TIM2, 0);
    LL_TIM_EnableCounter(TIM2);

    // Switch to RX
    furi_hal_subghz_rx();
}

void furi_hal_subghz_stop_async_rx() {
    furi_assert(furi_hal_subghz_state == SubGhzStateAsyncRx);
    furi_hal_subghz_state = SubGhzStateIdle;

    // Shutdown radio
    furi_hal_subghz_idle();

    FURI_CRITICAL_ENTER();
    LL_TIM_DeInit(TIM2);
    FURI_CRITICAL_EXIT();
    furi_hal_interrupt_set_isr(FuriHalInterruptIdTIM2, NULL, NULL);

    furi_hal_gpio_init(&gpio_cc1101_g0, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
}

#define API_HAL_SUBGHZ_ASYNC_TX_BUFFER_FULL (256)
#define API_HAL_SUBGHZ_ASYNC_TX_BUFFER_HALF (API_HAL_SUBGHZ_ASYNC_TX_BUFFER_FULL / 2)
#define API_HAL_SUBGHZ_ASYNC_TX_GUARD_TIME 333

typedef struct {
    uint32_t* buffer;
    bool flip_flop;
    FuriHalSubGhzAsyncTxCallback callback;
    void* callback_context;
    uint64_t duty_high;
    uint64_t duty_low;
} FuriHalSubGhzAsyncTx;

static FuriHalSubGhzAsyncTx furi_hal_subghz_async_tx = {0};

static void furi_hal_subghz_async_tx_refill(uint32_t* buffer, size_t samples) {
    while(samples > 0) {
        bool is_odd = samples % 2;
        LevelDuration ld =
            furi_hal_subghz_async_tx.callback(furi_hal_subghz_async_tx.callback_context);

        if(level_duration_is_wait(ld)) {
            return;
        } else if(level_duration_is_reset(ld)) {
            // One more even sample required to end at low level
            if(is_odd) {
                *buffer = API_HAL_SUBGHZ_ASYNC_TX_GUARD_TIME;
                buffer++;
                samples--;
                furi_hal_subghz_async_tx.duty_low += API_HAL_SUBGHZ_ASYNC_TX_GUARD_TIME;
            }
            break;
        } else {
            // Inject guard time if level is incorrect
            bool level = level_duration_get_level(ld);
            if(is_odd == level) {
                *buffer = API_HAL_SUBGHZ_ASYNC_TX_GUARD_TIME;
                buffer++;
                samples--;
                if(!level) {
                    furi_hal_subghz_async_tx.duty_high += API_HAL_SUBGHZ_ASYNC_TX_GUARD_TIME;
                } else {
                    furi_hal_subghz_async_tx.duty_low += API_HAL_SUBGHZ_ASYNC_TX_GUARD_TIME;
                }
            }

            uint32_t duration = level_duration_get_duration(ld);
            furi_assert(duration > 0);
            *buffer = duration;
            buffer++;
            samples--;

            if(level) {
                furi_hal_subghz_async_tx.duty_high += duration;
            } else {
                furi_hal_subghz_async_tx.duty_low += duration;
            }
        }
    }

    memset(buffer, 0, samples * sizeof(uint32_t));
}

static void furi_hal_subghz_async_tx_dma_isr() {
    furi_assert(furi_hal_subghz_state == SubGhzStateAsyncTx);
    if(LL_DMA_IsActiveFlag_HT1(DMA1)) {
        LL_DMA_ClearFlag_HT1(DMA1);
        furi_hal_subghz_async_tx_refill(
            furi_hal_subghz_async_tx.buffer, API_HAL_SUBGHZ_ASYNC_TX_BUFFER_HALF);
    }
    if(LL_DMA_IsActiveFlag_TC1(DMA1)) {
        LL_DMA_ClearFlag_TC1(DMA1);
        furi_hal_subghz_async_tx_refill(
            furi_hal_subghz_async_tx.buffer + API_HAL_SUBGHZ_ASYNC_TX_BUFFER_HALF,
            API_HAL_SUBGHZ_ASYNC_TX_BUFFER_HALF);
    }
}

static void furi_hal_subghz_async_tx_timer_isr() {
    if(LL_TIM_IsActiveFlag_UPDATE(TIM2)) {
        LL_TIM_ClearFlag_UPDATE(TIM2);
        if(LL_TIM_GetAutoReload(TIM2) == 0) {
            if(furi_hal_subghz_state == SubGhzStateAsyncTx) {
                furi_hal_subghz_state = SubGhzStateAsyncTxLast;
                //forcibly pulls the pin to the ground so that there is no carrier
                furi_hal_gpio_init(&gpio_cc1101_g0, GpioModeInput, GpioPullDown, GpioSpeedLow);
            } else {
                furi_hal_subghz_state = SubGhzStateAsyncTxEnd;
                LL_TIM_DisableCounter(TIM2);
            }
        }
    }
}

bool furi_hal_subghz_start_async_tx(FuriHalSubGhzAsyncTxCallback callback, void* context) {
    furi_assert(furi_hal_subghz_state == SubGhzStateIdle);
    furi_assert(callback);

    //If transmission is prohibited by regional settings
    if(furi_hal_subghz_regulation != SubGhzRegulationTxRx) return false;

    furi_hal_subghz_async_tx.callback = callback;
    furi_hal_subghz_async_tx.callback_context = context;

    furi_hal_subghz_state = SubGhzStateAsyncTx;

    furi_hal_subghz_async_tx.duty_low = 0;
    furi_hal_subghz_async_tx.duty_high = 0;

    furi_hal_subghz_async_tx.buffer =
        malloc(API_HAL_SUBGHZ_ASYNC_TX_BUFFER_FULL * sizeof(uint32_t));
    furi_hal_subghz_async_tx_refill(
        furi_hal_subghz_async_tx.buffer, API_HAL_SUBGHZ_ASYNC_TX_BUFFER_FULL);

    // Connect CC1101_GD0 to TIM2 as output
    furi_hal_gpio_init_ex(
        &gpio_cc1101_g0, GpioModeAltFunctionPushPull, GpioPullDown, GpioSpeedLow, GpioAltFn1TIM2);

    // Configure DMA
    LL_DMA_InitTypeDef dma_config = {0};
    dma_config.PeriphOrM2MSrcAddress = (uint32_t) & (TIM2->ARR);
    dma_config.MemoryOrM2MDstAddress = (uint32_t)furi_hal_subghz_async_tx.buffer;
    dma_config.Direction = LL_DMA_DIRECTION_MEMORY_TO_PERIPH;
    dma_config.Mode = LL_DMA_MODE_CIRCULAR;
    dma_config.PeriphOrM2MSrcIncMode = LL_DMA_PERIPH_NOINCREMENT;
    dma_config.MemoryOrM2MDstIncMode = LL_DMA_MEMORY_INCREMENT;
    dma_config.PeriphOrM2MSrcDataSize = LL_DMA_PDATAALIGN_WORD;
    dma_config.MemoryOrM2MDstDataSize = LL_DMA_MDATAALIGN_WORD;
    dma_config.NbData = API_HAL_SUBGHZ_ASYNC_TX_BUFFER_FULL;
    dma_config.PeriphRequest = LL_DMAMUX_REQ_TIM2_UP;
    dma_config.Priority = LL_DMA_MODE_NORMAL;
    LL_DMA_Init(DMA1, LL_DMA_CHANNEL_1, &dma_config);
    furi_hal_interrupt_set_isr(FuriHalInterruptIdDma1Ch1, furi_hal_subghz_async_tx_dma_isr, NULL);
    LL_DMA_EnableIT_TC(DMA1, LL_DMA_CHANNEL_1);
    LL_DMA_EnableIT_HT(DMA1, LL_DMA_CHANNEL_1);
    LL_DMA_EnableChannel(DMA1, LL_DMA_CHANNEL_1);

    // Configure TIM2
    LL_TIM_InitTypeDef TIM_InitStruct = {0};
    TIM_InitStruct.Prescaler = 64 - 1;
    TIM_InitStruct.CounterMode = LL_TIM_COUNTERMODE_UP;
    TIM_InitStruct.Autoreload = 1000;
    TIM_InitStruct.ClockDivision = LL_TIM_CLOCKDIVISION_DIV1;
    LL_TIM_Init(TIM2, &TIM_InitStruct);
    LL_TIM_SetClockSource(TIM2, LL_TIM_CLOCKSOURCE_INTERNAL);
    LL_TIM_EnableARRPreload(TIM2);

    // Configure TIM2 CH2
    LL_TIM_OC_InitTypeDef TIM_OC_InitStruct = {0};
    TIM_OC_InitStruct.OCMode = LL_TIM_OCMODE_TOGGLE;
    TIM_OC_InitStruct.OCState = LL_TIM_OCSTATE_DISABLE;
    TIM_OC_InitStruct.OCNState = LL_TIM_OCSTATE_DISABLE;
    TIM_OC_InitStruct.CompareValue = 0;
    TIM_OC_InitStruct.OCPolarity = LL_TIM_OCPOLARITY_HIGH;
    LL_TIM_OC_Init(TIM2, LL_TIM_CHANNEL_CH2, &TIM_OC_InitStruct);
    LL_TIM_OC_DisableFast(TIM2, LL_TIM_CHANNEL_CH2);
    LL_TIM_DisableMasterSlaveMode(TIM2);

    furi_hal_interrupt_set_isr(FuriHalInterruptIdTIM2, furi_hal_subghz_async_tx_timer_isr, NULL);

    LL_TIM_EnableIT_UPDATE(TIM2);
    LL_TIM_EnableDMAReq_UPDATE(TIM2);
    LL_TIM_CC_EnableChannel(TIM2, LL_TIM_CHANNEL_CH2);

    // Start counter
    LL_TIM_GenerateEvent_UPDATE(TIM2);
#ifdef FURI_HAL_SUBGHZ_TX_GPIO
    furi_hal_gpio_write(&FURI_HAL_SUBGHZ_TX_GPIO, true);
#endif
    furi_hal_subghz_tx();

    LL_TIM_SetCounter(TIM2, 0);
    LL_TIM_EnableCounter(TIM2);
    return true;
}

bool furi_hal_subghz_is_async_tx_complete() {
    return furi_hal_subghz_state == SubGhzStateAsyncTxEnd;
}

void furi_hal_subghz_stop_async_tx() {
    furi_assert(
        furi_hal_subghz_state == SubGhzStateAsyncTx ||
        furi_hal_subghz_state == SubGhzStateAsyncTxLast ||
        furi_hal_subghz_state == SubGhzStateAsyncTxEnd);

    // Shutdown radio
    furi_hal_subghz_idle();
#ifdef FURI_HAL_SUBGHZ_TX_GPIO
    furi_hal_gpio_write(&FURI_HAL_SUBGHZ_TX_GPIO, false);
#endif

    // Deinitialize Timer
    FURI_CRITICAL_ENTER();
    LL_TIM_DeInit(TIM2);
    furi_hal_interrupt_set_isr(FuriHalInterruptIdTIM2, NULL, NULL);

    // Deinitialize DMA
    LL_DMA_DeInit(DMA1, LL_DMA_CHANNEL_1);

    furi_hal_interrupt_set_isr(FuriHalInterruptIdDma1Ch1, NULL, NULL);

    // Deinitialize GPIO
    furi_hal_gpio_init(&gpio_cc1101_g0, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
    FURI_CRITICAL_EXIT();

    free(furi_hal_subghz_async_tx.buffer);

    float duty_cycle =
        100.0f * (float)furi_hal_subghz_async_tx.duty_high /
        ((float)furi_hal_subghz_async_tx.duty_low + (float)furi_hal_subghz_async_tx.duty_high);
    FURI_LOG_D(
        TAG,
        "Async TX Radio stats: on %0.0fus, off %0.0fus, DutyCycle: %0.0f%%",
        (float)furi_hal_subghz_async_tx.duty_high,
        (float)furi_hal_subghz_async_tx.duty_low,
        duty_cycle);

    furi_hal_subghz_state = SubGhzStateIdle;
}