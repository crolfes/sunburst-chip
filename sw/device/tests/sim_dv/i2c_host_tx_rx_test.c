// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#include "sw/device/lib/arch/device.h"
#include "sw/device/lib/base/bitfield.h"
#include "sw/device/lib/base/mmio.h"
#include "sw/device/lib/dif/dif_base.h"
#include "sw/device/lib/dif/dif_i2c.h"
#include "sw/device/lib/dif/dif_pinmux.h"
#include "sw/device/lib/dif/dif_rv_plic.h"
#include "sw/device/lib/runtime/hart.h"
#include "sw/device/lib/runtime/irq.h"
#include "sw/device/lib/runtime/log.h"
#include "sw/device/lib/testing/i2c_testutils.h"
#include "sw/device/lib/testing/rand_testutils.h"
#include "sw/device/lib/testing/test_framework/check.h"
#include "sw/device/lib/testing/test_framework/ottf_main.h"
#include "sw/device/lib/testing/test_framework/status.h"

#include "hw/top_chip/sw/autogen/top_chip.h"
#include "sw/device/lib/testing/autogen/isr_testutils.h"

static dif_i2c_t i2c;
static dif_pinmux_t pinmux;
static dif_rv_plic_t plic;

OTTF_DEFINE_TEST_CONFIG();

/**
 * This symbol is meant to be backdoor loaded by the testbench.
 * The testbench will inform the test the rough speed of the clock
 * used by the I2C modules.
 */
static volatile const uint8_t kClockPeriodNanos = 0;
static volatile const uint8_t kI2cRiseFallNanos = 0;
static volatile const uint32_t kI2cClockPeriodNanos = 0;
static volatile const uint8_t kI2cCdcInstrumentationEnabled = 0;

/**
 * This symbol is meant to be backdoor loaded by the testbench.
 * to indicate which I2c is actually under test. It is not used
 * at the moment, will connect it later.
 */
static volatile const uint8_t kI2cIdx = 0;

/**
 * Provides external irq handling for this test.
 *
 * This function overrides the default OTTF external ISR.
 */
static volatile bool fmt_irq_seen = false;
static volatile bool rx_irq_seen = false;
static volatile bool done_irq_seen = false;
/**
 * these variables store values based on kI2cIdx
 */
static uint32_t i2c_base_addr;
static top_chip_plic_irq_id_t plic_irq;
static top_chip_plic_peripheral_t peripheral_id;

void ottf_external_isr(uint32_t *exc_info) {
  // Find which interrupt fired at PLIC by claiming it.
  dif_rv_plic_irq_id_t plic_irq_id;
  CHECK_DIF_OK(
      dif_rv_plic_irq_claim(&plic, kTopChipPlicTargetIbex0, &plic_irq_id));

  // Check if it is the right peripheral.
  top_chip_plic_peripheral_t peripheral = (top_chip_plic_peripheral_t)
      top_chip_plic_interrupt_for_peripheral[plic_irq_id];
  CHECK(peripheral == peripheral_id,
        "Interrupt from unexpected peripheral: %d", peripheral);

  // Sunburst - Determine interrupt cause by reading the interrupt registers of
  //            the peripheral itself rather than the reduced-precision PLIC.
  dif_i2c_irq_state_snapshot_t state_snapshot;
  dif_i2c_irq_enable_snapshot_t enable_snapshot;
  dif_i2c_irq_state_snapshot_t pending_enabled;
  // Note - peripheral interrupt state (INTR_STATE) is not enable-masked.
  CHECK_DIF_OK(dif_i2c_irq_get_state(&i2c, &state_snapshot));
  // Note - at present the only way to get all interrupt enables (INTR_ENABLE)
  //        from a peripheral is to use the ...irq_disable_all function.
  CHECK_DIF_OK(dif_i2c_irq_disable_all(&i2c, &enable_snapshot));
  CHECK_DIF_OK(dif_i2c_irq_restore_all(&i2c, &enable_snapshot));
  // Combine peripheral interrupt state bits with interrupt enable mask
  // for an approximation of interrupts fired.
  pending_enabled = state_snapshot & enable_snapshot;

  dif_i2c_irq_t i2c_irq = 0;
  bool disable = false;
  if (bitfield_bit32_read(pending_enabled, kDifI2cIrqFmtThreshold)) {
    fmt_irq_seen = true;
    i2c_irq = kDifI2cIrqFmtThreshold;
    disable = true;
  } else if (bitfield_bit32_read(pending_enabled, kDifI2cIrqRxThreshold)) {
    rx_irq_seen = true;
    i2c_irq = kDifI2cIrqRxThreshold;
    disable = true;
  } else if (bitfield_bit32_read(pending_enabled, kDifI2cIrqCmdComplete)) {
    done_irq_seen = true;
    i2c_irq = kDifI2cIrqCmdComplete;
  } else {
    LOG_ERROR("Unexpected interrupts (at I2C): %x (%x & %x)",
              pending_enabled, state_snapshot, enable_snapshot);
    if (bitfield_bit32_read(pending_enabled, kDifI2cIrqControllerHalt)) {
      LOG_ERROR("ControllerHalt!");
      dif_i2c_controller_halt_events_t halt_events = {0};
      CHECK_DIF_OK(dif_i2c_get_controller_halt_events(&i2c, &halt_events));
      LOG_ERROR("CONTROLLER_EVENTS:\n  nack received: %d\n  "
                 "unhandled nack timeout: %d\n  bus timeout: %d\n  "
                 "arbitration lost: %d",
                halt_events.nack_received, halt_events.unhandled_nack_timeout,
                halt_events.bus_timeout, halt_events.arbitration_lost);
    }
    test_status_set(kTestStatusFailed);
  }

  if (disable) {
    // Status type interrupt must be disabled since it cannot be cleared
    CHECK_DIF_OK(dif_i2c_irq_set_enabled(&i2c, i2c_irq, kDifToggleDisabled));
  }

  // Clear the interrupt at I^2C block.
  CHECK_DIF_OK(dif_i2c_irq_acknowledge(&i2c, i2c_irq));

  // Complete the IRQ at PLIC.
  CHECK_DIF_OK(dif_rv_plic_irq_complete(&plic, kTopChipPlicTargetIbex0,
                                        plic_irq_id));
}

static void en_plic_irqs(dif_rv_plic_t *plic) {
  // Enable interrupt for the chosen i2c block at the PLIC
  CHECK_DIF_OK(dif_rv_plic_irq_set_enabled(
      plic, plic_irq, kTopChipPlicTargetIbex0, kDifToggleEnabled));

  // Assign a default priority
  CHECK_DIF_OK(dif_rv_plic_irq_set_priority(plic, plic_irq,
                                            kDifRvPlicMaxPriority));
}

static void en_i2c_irqs(dif_i2c_t *i2c) {
  dif_i2c_irq_t i2c_irqs[] = {
      kDifI2cIrqRxThreshold, kDifI2cIrqRxOverflow, kDifI2cIrqControllerHalt,
      kDifI2cIrqSclInterference, kDifI2cIrqSdaInterference,
      kDifI2cIrqStretchTimeout,
      // kDifI2cIrqSdaUnstable removed for now, instability is intentionally
      // introduced in testing that can interfere with it (TODO: check this).
      // kDifI2cIrqSdaUnstable,
      kDifI2cIrqCmdComplete};

  for (uint32_t i = 0; i < ARRAYSIZE(i2c_irqs); ++i) {

    CHECK_DIF_OK(dif_i2c_irq_set_enabled(i2c, i2c_irqs[i], kDifToggleEnabled));
  }
}

static void en_i2c_status_irqs(dif_i2c_t *i2c) {
  // Enable the FmtThreshold IRQ now that there is something in the FMT FIFO
  CHECK_DIF_OK(
      dif_i2c_irq_set_enabled(i2c, kDifI2cIrqFmtThreshold, kDifToggleEnabled));
  // Enable the RxThreshold IRQ in anticipation of receiving data into the
  // RX FIFO
  CHECK_DIF_OK(
      dif_i2c_irq_set_enabled(i2c, kDifI2cIrqRxThreshold, kDifToggleEnabled));
}

// handle i2c index related configure
void config_i2c_with_index(void) {
  switch (kI2cIdx) {
    case 0:
      i2c_base_addr = TOP_CHIP_I2C0_BASE_ADDR;
      plic_irq = kTopChipPlicIrqIdI2c0;
      peripheral_id = kTopChipPlicPeripheralI2c0;

      // Sunburst - pinmux code kept as placeholder
      CHECK_DIF_OK(dif_pinmux_input_select(
          &pinmux, kTopChipPinmuxPeripheralInI2c0Scl,
          kTopChipPinmuxInselIoa8));
      CHECK_DIF_OK(dif_pinmux_input_select(
          &pinmux, kTopChipPinmuxPeripheralInI2c0Sda,
          kTopChipPinmuxInselIoa7));
      CHECK_DIF_OK(dif_pinmux_output_select(&pinmux,
                                            kTopChipPinmuxMioOutIoa8,
                                            kTopChipPinmuxOutselI2c0Scl));
      CHECK_DIF_OK(dif_pinmux_output_select(&pinmux,
                                            kTopChipPinmuxMioOutIoa7,
                                            kTopChipPinmuxOutselI2c0Sda));
      break;
    case 1:
      i2c_base_addr = TOP_CHIP_I2C1_BASE_ADDR;
      plic_irq = kTopChipPlicIrqIdI2c1;
      peripheral_id = kTopChipPlicPeripheralI2c1;

      // Sunburst - pinmux code kept as placeholder
      CHECK_DIF_OK(dif_pinmux_input_select(
          &pinmux, kTopChipPinmuxPeripheralInI2c1Scl,
          kTopChipPinmuxInselIob9));
      CHECK_DIF_OK(dif_pinmux_input_select(
          &pinmux, kTopChipPinmuxPeripheralInI2c1Sda,
          kTopChipPinmuxInselIob10));
      CHECK_DIF_OK(dif_pinmux_output_select(&pinmux,
                                            kTopChipPinmuxMioOutIob9,
                                            kTopChipPinmuxOutselI2c1Scl));
      CHECK_DIF_OK(dif_pinmux_output_select(&pinmux,
                                            kTopChipPinmuxMioOutIob10,
                                            kTopChipPinmuxOutselI2c1Sda));
      break;
    default:
      LOG_FATAL("Unsupported i2c index %d", kI2cIdx);
  }
}

/**
 * Send a write transaction with random data, followed by a read transaction of
 * the same length. Check that the read data matches the written data. Ensure
 * the appropriate IRQs have been triggered.
 *
 * @param skip_stop True to use a repeated start to end the write transaction
 *                  and start the read. False to use a STOP condition to end the
 *                  write transaction and start the read.
 */
void issue_test_transactions(bool skip_stop) {
  // Randomize variables.
  uint8_t byte_count = (uint8_t)rand_testutils_gen32_range(30, 64);
  uint8_t device_addr = (uint8_t)rand_testutils_gen32_range(0, 16);
  uint8_t expected_data[byte_count];
  LOG_INFO("Loopback %d bytes with device %d", byte_count, device_addr);

  // Controlling the randomization from C side is a bit slow, but might be
  // easier for portability to a different setup later.
  for (uint32_t i = 0; i < byte_count; ++i) {
    expected_data[i] = (uint8_t)rand_testutils_gen32_range(0, 0xff);
  };

  // Write expected data to i2c device.
  CHECK_STATUS_OK(i2c_testutils_write(&i2c, device_addr, byte_count,
                                      expected_data, skip_stop));
  CHECK(!fmt_irq_seen);
  en_i2c_status_irqs(&i2c);

  dif_i2c_level_t fmt_fifo_lvl, rx_fifo_lvl;

  // Make sure all fifo entries have been drained.
  do {
    CHECK_DIF_OK(
        dif_i2c_get_fifo_levels(&i2c, &fmt_fifo_lvl, &rx_fifo_lvl, NULL, NULL));
  } while (fmt_fifo_lvl > 0);
  CHECK(fmt_irq_seen);
  fmt_irq_seen = false;

  // Read data from i2c device.
  CHECK(!rx_irq_seen);
  CHECK_STATUS_OK(i2c_testutils_issue_read(&i2c, device_addr, byte_count));

  // Make sure all data has been read back.
  do {
    CHECK_DIF_OK(
        dif_i2c_get_fifo_levels(&i2c, &fmt_fifo_lvl, &rx_fifo_lvl, NULL, NULL));
  } while (rx_fifo_lvl < byte_count);
  CHECK(rx_irq_seen);

  // Make sure every read is the same.
  for (uint32_t i = 0; i < byte_count; ++i) {
    uint8_t byte;
    CHECK_DIF_OK(dif_i2c_read_byte(&i2c, &byte));
    if (expected_data[i] != byte) {
      LOG_ERROR("Byte %d, Expected data 0x%x, read data 0x%x", i,
                expected_data[i], byte);
    }
  };
  CHECK(done_irq_seen);
}

bool test_main(void) {
  LOG_INFO("Testing I2C index %d", kI2cIdx);
  CHECK_DIF_OK(dif_pinmux_init(
      mmio_region_from_addr(TOP_CHIP_PINMUX_AON_BASE_ADDR), &pinmux));

  config_i2c_with_index();

  CHECK_DIF_OK(dif_i2c_init(mmio_region_from_addr(i2c_base_addr), &i2c));
  CHECK_DIF_OK(dif_rv_plic_init(
      mmio_region_from_addr(TOP_CHIP_RV_PLIC_BASE_ADDR), &plic));

  en_plic_irqs(&plic);

  // Sunburst - Moved irq en here to avoid irq-disabling side-effects of
  //            function returns compiled by CHERIoT toolchain.
  irq_global_ctrl(true);
  // Enable the external IRQ at Ibex.
  irq_external_ctrl(true);

  // I2C speed parameters.
  dif_i2c_timing_config_t timing_config = {
      .lowest_target_device_speed = kDifI2cSpeedFastPlus,
      .clock_period_nanos = kClockPeriodNanos,
      .sda_rise_nanos = kI2cRiseFallNanos,
      .sda_fall_nanos = kI2cRiseFallNanos,
      .scl_period_nanos = kI2cClockPeriodNanos};

  dif_i2c_config_t config;
  CHECK_DIF_OK(dif_i2c_compute_timing(timing_config, &config));
  if (kI2cCdcInstrumentationEnabled) {
    // Increase rise cycles to accommodate CDC incorrectly delaying the data
    // change. Without this, the SDA interference interrupt will be triggered
    // when the prim_flop_2sync randomly adds an extra cycle of delay.
    config.rise_cycles++;
  }
  CHECK_DIF_OK(dif_i2c_configure(&i2c, config));
  CHECK_DIF_OK(dif_i2c_host_set_enabled(&i2c, kDifToggleEnabled));
  CHECK_DIF_OK(
      dif_i2c_set_host_watermarks(&i2c, /*rx_level=*/29u, /*fmt_level=*/5u));

  en_i2c_irqs(&i2c);

  // Round 1: Test with a STOP between the write and read transactions.
  CHECK(!fmt_irq_seen);
  CHECK(!rx_irq_seen);
  CHECK(!done_irq_seen);
  issue_test_transactions(/*skip_stop=*/false);

  // Round 2: Test without a STOP between the write and read transactions. Use a
  // repeated start instead.
  fmt_irq_seen = false;
  rx_irq_seen = false;
  done_irq_seen = false;
  issue_test_transactions(/*skip_stop=*/true);

  return true;
}
