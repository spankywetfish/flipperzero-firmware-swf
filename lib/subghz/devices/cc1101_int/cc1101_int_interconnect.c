#include "cc1101_int_interconnect.h"
#include <furi_hal.h>

#define TAG "SubGhzDeviceCC1101Int"

static bool subghz_device_cc1101_int_interconnect_is_frequency_valid(uint32_t frequency) {
    bool ret = furi_hal_subghz_is_frequency_valid(frequency);
    if(!ret) {
        furi_crash("SubGhz: Incorrect frequency.");
    }
    return ret;
}

static uint32_t subghz_device_cc1101_int_interconnect_set_frequency(uint32_t frequency) {
    subghz_device_cc1101_int_interconnect_is_frequency_valid(frequency);
    return furi_hal_subghz_set_frequency_and_path(frequency);
}

static bool subghz_device_cc1101_int_interconnect_start_async_tx(void* callback, void* context) {
    return furi_hal_subghz_start_async_tx(
        (FuriHalSubGhzAsyncTxCallback)callback, context);
}

static void subghz_device_cc1101_int_interconnect_start_async_rx(void* callback, void* context) {
    furi_hal_subghz_start_async_rx(
        (FuriHalSubGhzCaptureCallback)callback, context);
}

static void subghz_device_cc1101_int_interconnect_load_preset(
    FuriHalSubGhzPreset preset,
    uint8_t* preset_data) {
    if(preset != FuriHalSubGhzPresetCustom) {
        furi_hal_subghz_load_preset(preset);
    } else {
        furi_hal_subghz_load_custom_preset(preset_data);
    }
}

const SubGhzDeviceInterconnect subghz_device_cc1101_int_interconnect = {
    .begin = NULL,
    .end = furi_hal_subghz_shutdown,
    //.check = subghz_device_cc1101_int_check,
    .reset = furi_hal_subghz_reset,
    .sleep = furi_hal_subghz_sleep,
    .idle = furi_hal_subghz_idle,
    .load_preset = subghz_device_cc1101_int_interconnect_load_preset,
    .set_frequency = subghz_device_cc1101_int_interconnect_set_frequency,
    .set_async_mirror_pin = furi_hal_subghz_set_async_mirror_pin,
    .get_data_gpio = furi_hal_subghz_get_data_gpio,

    .set_tx = furi_hal_subghz_tx,
    .flush_tx = furi_hal_subghz_flush_tx,
    .start_async_tx = subghz_device_cc1101_int_interconnect_start_async_tx,
    .is_async_complete_tx = furi_hal_subghz_is_async_tx_complete,
    .stop_async_tx = furi_hal_subghz_stop_async_tx,

    .set_rx = furi_hal_subghz_rx,
    .flush_rx = furi_hal_subghz_flush_rx,
    .start_async_rx = subghz_device_cc1101_int_interconnect_start_async_rx,
    .stop_async_rx = furi_hal_subghz_stop_async_rx,

    .get_rssi = furi_hal_subghz_get_rssi,
    .get_lqi = furi_hal_subghz_get_lqi,

    .rx_pipe_not_empty = furi_hal_subghz_rx_pipe_not_empty,
    .is_rx_data_crc_valid = furi_hal_subghz_is_rx_data_crc_valid,
    .read_packet = furi_hal_subghz_read_packet,
    .write_packet = furi_hal_subghz_write_packet,
};

const SubGhzDevice subghz_device_cc1101_int = {
    .name = SUBGHZ_DEVICE_CC1101_INT_NAME,
    .interconnect = &subghz_device_cc1101_int_interconnect,
};