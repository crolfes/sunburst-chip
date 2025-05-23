// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#include "sw/device/lib/testing/test_framework/ottf_console.h"

#include <stdbool.h>
#include <stdint.h>

#include "sw/device/lib/base/mmio.h"
#include "sw/device/lib/base/status.h"
#include "sw/device/lib/dif/dif_rv_plic.h"
#include "sw/device/lib/dif/dif_uart.h"
#include "sw/device/lib/runtime/ibex.h"
#include "sw/device/lib/runtime/irq.h"
#include "sw/device/lib/runtime/print.h"
#include "sw/device/lib/testing/test_framework/check.h"
#include "sw/device/lib/testing/test_framework/ottf_isrs.h"
#include "sw/device/lib/testing/test_framework/ottf_test_config.h"

// TODO: make this toplevel agnostic.
#include "hw/top_chip/sw/autogen/top_chip.h"

#define MODULE_ID MAKE_MODULE_ID('o', 't', 'c')

/**
 * OTTF console configuration parameters.
 */
enum {
  /**
   * Flow control parameters.
   */
  kFlowControlLowWatermark = 4,   // bytes
  kFlowControlHighWatermark = 8,  // bytes
  kFlowControlRxWatermark = kDifUartWatermarkByte8,
  /**
   * HART PLIC Target.
   */
  kPlicTarget = kTopChipPlicTargetIbex0,
};

// Potential DIF handles for OTTF console communication.
static dif_uart_t ottf_console_uart;

// The `flow_control_state` and `flow_control_irqs` variables are shared between
// the interrupt service handler and user code.
static volatile ottf_console_flow_control_t flow_control_state;
static volatile uint32_t flow_control_irqs;

void *ottf_console_get(void) {
  switch (kOttfTestConfig.console.type) {
    default:
      return &ottf_console_uart;
  }
}

void ottf_console_init(void) {
  // Initialize/Configure the console device.
  uintptr_t base_addr = kOttfTestConfig.console.base_addr;
  switch (kOttfTestConfig.console.type) {
    case kOttfConsoleUart:
      // Set a default for the console base address if the base address is not
      // configured. The default is to use UART0.
      if (base_addr == 0) {
        CHECK(kOttfTestConfig.console.type == kOttfConsoleUart);
        base_addr = TOP_CHIP_UART0_BASE_ADDR;
      }

      ottf_console_configure_uart(base_addr);
      break;
    default:
      CHECK(false, "unsupported OTTF console interface.");
      break;
  }
}

void ottf_console_configure_uart(uintptr_t base_addr) {
  CHECK_DIF_OK(
      dif_uart_init(mmio_region_from_addr(base_addr), &ottf_console_uart));
  CHECK(kUartBaudrate <= UINT32_MAX, "kUartBaudrate must fit in uint32_t");
  CHECK(kClockFreqPeripheralHz <= UINT32_MAX,
        "kClockFreqPeripheralHz must fit in uint32_t");
  CHECK_DIF_OK(dif_uart_configure(
      &ottf_console_uart, (dif_uart_config_t){
                              .baudrate = (uint32_t)kUartBaudrate,
                              .clk_freq_hz = (uint32_t)kClockFreqPeripheralHz,
                              .parity_enable = kDifToggleDisabled,
                              .parity = kDifUartParityEven,
                              .tx_enable = kDifToggleEnabled,
                              .rx_enable = kDifToggleEnabled,
                          }));
  base_uart_stdout(&ottf_console_uart);

  // Initialize/Configure console flow control (if requested).
  if (kOttfTestConfig.enable_uart_flow_control) {
    ottf_console_flow_control_enable();
  }
}

static uint32_t get_flow_control_watermark_plic_id(void) {
  switch (kOttfTestConfig.console.base_addr) {
    case TOP_CHIP_UART1_BASE_ADDR:
      return kTopChipPlicIrqIdUart1RxWatermark;
    case TOP_CHIP_UART0_BASE_ADDR:
    default:
      return kTopChipPlicIrqIdUart0RxWatermark;
  }
}

void ottf_console_flow_control_enable(void) {
  CHECK_DIF_OK(dif_rv_plic_init(
      mmio_region_from_addr(TOP_CHIP_RV_PLIC_BASE_ADDR), &ottf_plic));

  dif_uart_t *uart = (dif_uart_t *)ottf_console_get();
  CHECK_DIF_OK(dif_uart_watermark_rx_set(uart, kFlowControlRxWatermark));
  CHECK_DIF_OK(dif_uart_irq_set_enabled(uart, kDifUartIrqRxWatermark,
                                        kDifToggleEnabled));

  // Set IRQ priorities to MAX
  CHECK_DIF_OK(dif_rv_plic_irq_set_priority(
      &ottf_plic, get_flow_control_watermark_plic_id(), kDifRvPlicMaxPriority));
  // Set Ibex IRQ priority threshold level
  CHECK_DIF_OK(dif_rv_plic_target_set_threshold(&ottf_plic, kPlicTarget,
                                                kDifRvPlicMinPriority));
  // Enable IRQs in PLIC
  CHECK_DIF_OK(dif_rv_plic_irq_set_enabled(&ottf_plic,
                                           get_flow_control_watermark_plic_id(),
                                           kPlicTarget, kDifToggleEnabled));

  flow_control_state = kOttfConsoleFlowControlAuto;
  irq_global_ctrl(true);
  irq_external_ctrl(true);
  // Make sure we're in the Resume state and we emit a Resume to the UART.
  ottf_console_flow_control((dif_uart_t *)ottf_console_get(),
                            kOttfConsoleFlowControlResume);
}

// This version of the function is safe to call from within the ISR.
static status_t manage_flow_control(const dif_uart_t *uart,
                                    ottf_console_flow_control_t ctrl) {
  if (flow_control_state == kOttfConsoleFlowControlNone) {
    return OK_STATUS((int32_t)flow_control_state);
  }
  if (ctrl == kOttfConsoleFlowControlAuto) {
    uint32_t avail;
    TRY(dif_uart_rx_bytes_available(uart, &avail));
    if (avail < kFlowControlLowWatermark &&
        flow_control_state != kOttfConsoleFlowControlResume) {
      // Enable RX watermark interrupt when RX FIFO level is below the
      // watermark.
      CHECK_DIF_OK(dif_uart_irq_set_enabled(uart, kDifUartIrqRxWatermark,
                                            kDifToggleEnabled));
      ctrl = kOttfConsoleFlowControlResume;
    } else if (avail >= kFlowControlHighWatermark &&
               flow_control_state != kOttfConsoleFlowControlPause) {
      ctrl = kOttfConsoleFlowControlPause;
      // RX watermark interrupt is status type, so disable the interrupt whilst
      // RX FIFO is above the watermark to avoid an inifite loop of ISRs.
      CHECK_DIF_OK(dif_uart_irq_set_enabled(uart, kDifUartIrqRxWatermark,
                                            kDifToggleDisabled));
    } else {
      return OK_STATUS((int32_t)flow_control_state);
    }
  }
  uint8_t byte = (uint8_t)ctrl;
  CHECK_DIF_OK(dif_uart_bytes_send(uart, &byte, 1, NULL));
  flow_control_state = ctrl;
  return OK_STATUS((int32_t)flow_control_state);
}

bool ottf_console_flow_control_isr(uint32_t *exc_info) {
  dif_uart_t *uart = (dif_uart_t *)ottf_console_get();
  flow_control_irqs += 1;
  bool rx;
  CHECK_DIF_OK(dif_uart_irq_is_pending(uart, kDifUartIrqRxWatermark, &rx));
  if (rx) {
    manage_flow_control(uart, kOttfConsoleFlowControlAuto);
    CHECK_DIF_OK(dif_uart_irq_acknowledge(uart, kDifUartIrqRxWatermark));
    return true;
  }
  return false;
}

// The public API has to save and restore interrupts to avoid an
// unexpected write to the global `flow_control_state`.
status_t ottf_console_flow_control(const dif_uart_t *uart,
                                   ottf_console_flow_control_t ctrl) {
  dif_uart_irq_enable_snapshot_t snapshot;
  CHECK_DIF_OK(dif_uart_irq_disable_all(uart, &snapshot));
  status_t s = manage_flow_control(uart, ctrl);
  CHECK_DIF_OK(dif_uart_irq_restore_all(uart, &snapshot));
  return s;
}

uint32_t ottf_console_get_flow_control_irqs(void) { return flow_control_irqs; }
