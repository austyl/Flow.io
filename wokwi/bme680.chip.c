#include "wokwi-api.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
extern double chip_read_control_value(const char *id);

#define BME680_I2C_ADDR 0x77
#define CHIP_ID_REG 0xD0
#define CHIP_ID 0x61
#define MEAS_BASE 0xF7
#define MEAS_LEN 6

typedef struct {
  uint8_t last_reg;
  float t_c;
  float p_hpa;
  float h_rh;
  float gas_rkohm;
  uint32_t tick;

} bme680_state;

static bme680_state state;  // global persistent state

static void float_to_16be(int16_t val, uint8_t *buf) {
  buf[0] = (val >> 8) & 0xFF;
  buf[1] = val & 0xFF;
}

void chip_init(void) {
  memset(&state, 0, sizeof(state));
  state.t_c = 24.5f;
  state.p_hpa = 1013.25f;
  state.h_rh = 44.7f;
  state.gas_rkohm = 120.0f;
  state.last_reg = 0xFF;
  state.tick = 0;
}

/* === Called periodically (every few ms) === */
void chip_tick(void) {
  state.tick++;

  // Read user controls
  state.t_c = chip_read_control_value("temperature");
  state.p_hpa = chip_read_control_value("pressure");
  state.h_rh = chip_read_control_value("humidity");
  state.gas_rkohm = chip_read_control_value("gas");
}


/* === I2C write handler === */
void i2c_write(uint8_t addr, uint8_t *data, uint32_t len) {
  if (addr != BME680_I2C_ADDR || len == 0) return;
  state.last_reg = data[0];
}

/* === I2C read handler === */
uint32_t i2c_read(uint8_t addr, uint8_t *data, uint32_t len) {
  if (addr != BME680_I2C_ADDR) return 0;
  if (len == 0) return 0;

  if (state.last_reg == CHIP_ID_REG) {
    data[0] = CHIP_ID;
    for (uint32_t i = 1; i < len; ++i) data[i] = 0;
    return len;
  }

  if (state.last_reg >= MEAS_BASE && state.last_reg < (MEAS_BASE + MEAS_LEN)) {
    uint8_t buf[MEAS_LEN];
    int16_t tval = (int16_t)roundf(state.t_c * 100.0f);
    int16_t pval = (int16_t)roundf(state.p_hpa * 100.0f);
    int16_t hval = (int16_t)roundf(state.h_rh * 100.0f);

    float_to_16be(tval, &buf[0]);
    float_to_16be(pval, &buf[2]);
    float_to_16be(hval, &buf[4]);

    uint32_t start = (uint32_t)(state.last_reg - MEAS_BASE);
    uint32_t remaining = MEAS_LEN - start;
    uint32_t to_copy = (len < remaining) ? len : remaining;
    memcpy(data, &buf[start], to_copy);
    for (uint32_t i = to_copy; i < len; ++i) data[i] = 0;
    return len;
  }

  for (uint32_t i = 0; i < len; ++i) data[i] = 0;
  return len;
}
