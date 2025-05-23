// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
//
// Register Top module auto-generated by `reggen`

`include "prim_assert.sv"

module rev_ctl_reg_top (
  input clk_i,
  input rst_ni,
  // To HW
  output rev_ctl_reg_pkg::rev_ctl_reg2hw_t reg2hw, // Write
  input  rev_ctl_reg_pkg::rev_ctl_hw2reg_t hw2reg, // Read

  input  tlul_pkg::tl_h2d_t tl_i,
  output tlul_pkg::tl_d2h_t tl_o
);

  import rev_ctl_reg_pkg::* ;

  localparam int AW = 5;
  localparam int DW = 32;
  localparam int DBW = DW/8;                    // Byte Width

  // register signals
  logic           reg_we;
  logic           reg_re;
  logic [AW-1:0]  reg_addr;
  logic [DW-1:0]  reg_wdata;
  logic [DBW-1:0] reg_be;
  logic [DW-1:0]  reg_rdata;
  logic           reg_error;

  logic          addrmiss, wr_err;

  logic [DW-1:0] reg_rdata_next;
  logic reg_busy;

  tlul_pkg::tl_h2d_t tl_reg_h2d;
  tlul_pkg::tl_d2h_t tl_reg_d2h;


  // outgoing integrity generation
  tlul_pkg::tl_d2h_t tl_o_pre;
  tlul_rsp_intg_gen #(
    .EnableRspIntgGen(0),
    .EnableDataIntgGen(0)
  ) u_rsp_intg_gen (
    .tl_i(tl_o_pre),
    .tl_o(tl_o)
  );

  assign tl_reg_h2d = tl_i;
  assign tl_o_pre   = tl_reg_d2h;

  tlul_adapter_reg #(
    .RegAw(AW),
    .RegDw(DW),
    .EnableDataIntgGen(0)
  ) u_reg_if (
    .clk_i  (clk_i),
    .rst_ni (rst_ni),

    .tl_i (tl_reg_h2d),
    .tl_o (tl_reg_d2h),

    .en_ifetch_i(prim_mubi_pkg::MuBi4False),
    .intg_error_o(),

    .we_o    (reg_we),
    .re_o    (reg_re),
    .addr_o  (reg_addr),
    .wdata_o (reg_wdata),
    .be_o    (reg_be),
    .busy_i  (reg_busy),
    .rdata_i (reg_rdata),
    .error_i (reg_error)
  );

  // cdc oversampling signals

  assign reg_rdata = reg_rdata_next ;
  assign reg_error = addrmiss | wr_err;

  // Define SW related signals
  // Format: <reg>_<field>_{wd|we|qs}
  //        or <reg>_{wd|we|qs} if field == 1 or 0
  logic start_addr_we;
  logic [31:0] start_addr_qs;
  logic [31:0] start_addr_wd;
  logic end_addr_we;
  logic [31:0] end_addr_qs;
  logic [31:0] end_addr_wd;
  logic go_re;
  logic go_we;
  logic [31:0] go_qs;
  logic [31:0] go_wd;
  logic epoch_running_qs;
  logic [30:0] epoch_epoch_qs;
  logic interrupt_status_re;
  logic interrupt_status_we;
  logic interrupt_status_qs;
  logic interrupt_status_wd;
  logic interrupt_enable_we;
  logic interrupt_enable_qs;
  logic interrupt_enable_wd;

  // Register instances
  // R[start_addr]: V(False)
  prim_subreg #(
    .DW      (32),
    .SwAccess(prim_subreg_pkg::SwAccessRW),
    .RESVAL  (32'h0),
    .Mubi    (1'b0)
  ) u_start_addr (
    .clk_i   (clk_i),
    .rst_ni  (rst_ni),

    // from register interface
    .we     (start_addr_we),
    .wd     (start_addr_wd),

    // from internal hardware
    .de     (1'b0),
    .d      ('0),

    // to internal hardware
    .qe     (),
    .q      (reg2hw.start_addr.q),
    .ds     (),

    // to register interface (read)
    .qs     (start_addr_qs)
  );


  // R[end_addr]: V(False)
  prim_subreg #(
    .DW      (32),
    .SwAccess(prim_subreg_pkg::SwAccessRW),
    .RESVAL  (32'h0),
    .Mubi    (1'b0)
  ) u_end_addr (
    .clk_i   (clk_i),
    .rst_ni  (rst_ni),

    // from register interface
    .we     (end_addr_we),
    .wd     (end_addr_wd),

    // from internal hardware
    .de     (1'b0),
    .d      ('0),

    // to internal hardware
    .qe     (),
    .q      (reg2hw.end_addr.q),
    .ds     (),

    // to register interface (read)
    .qs     (end_addr_qs)
  );


  // R[go]: V(True)
  logic go_qe;
  logic [0:0] go_flds_we;
  assign go_qe = &go_flds_we;
  prim_subreg_ext #(
    .DW    (32)
  ) u_go (
    .re     (go_re),
    .we     (go_we),
    .wd     (go_wd),
    .d      (hw2reg.go.d),
    .qre    (),
    .qe     (go_flds_we[0]),
    .q      (reg2hw.go.q),
    .ds     (),
    .qs     (go_qs)
  );
  assign reg2hw.go.qe = go_qe;


  // R[epoch]: V(False)
  //   F[running]: 0:0
  prim_subreg #(
    .DW      (1),
    .SwAccess(prim_subreg_pkg::SwAccessRO),
    .RESVAL  (1'h0),
    .Mubi    (1'b0)
  ) u_epoch_running (
    .clk_i   (clk_i),
    .rst_ni  (rst_ni),

    // from register interface
    .we     (1'b0),
    .wd     ('0),

    // from internal hardware
    .de     (hw2reg.epoch.running.de),
    .d      (hw2reg.epoch.running.d),

    // to internal hardware
    .qe     (),
    .q      (reg2hw.epoch.running.q),
    .ds     (),

    // to register interface (read)
    .qs     (epoch_running_qs)
  );

  //   F[epoch]: 31:1
  prim_subreg #(
    .DW      (31),
    .SwAccess(prim_subreg_pkg::SwAccessRO),
    .RESVAL  (31'h0),
    .Mubi    (1'b0)
  ) u_epoch_epoch (
    .clk_i   (clk_i),
    .rst_ni  (rst_ni),

    // from register interface
    .we     (1'b0),
    .wd     ('0),

    // from internal hardware
    .de     (hw2reg.epoch.epoch.de),
    .d      (hw2reg.epoch.epoch.d),

    // to internal hardware
    .qe     (),
    .q      (reg2hw.epoch.epoch.q),
    .ds     (),

    // to register interface (read)
    .qs     (epoch_epoch_qs)
  );


  // R[interrupt_status]: V(True)
  logic interrupt_status_qe;
  logic [0:0] interrupt_status_flds_we;
  assign interrupt_status_qe = &interrupt_status_flds_we;
  prim_subreg_ext #(
    .DW    (1)
  ) u_interrupt_status (
    .re     (interrupt_status_re),
    .we     (interrupt_status_we),
    .wd     (interrupt_status_wd),
    .d      (hw2reg.interrupt_status.d),
    .qre    (),
    .qe     (interrupt_status_flds_we[0]),
    .q      (reg2hw.interrupt_status.q),
    .ds     (),
    .qs     (interrupt_status_qs)
  );
  assign reg2hw.interrupt_status.qe = interrupt_status_qe;


  // R[interrupt_enable]: V(False)
  prim_subreg #(
    .DW      (1),
    .SwAccess(prim_subreg_pkg::SwAccessRW),
    .RESVAL  (1'h0),
    .Mubi    (1'b0)
  ) u_interrupt_enable (
    .clk_i   (clk_i),
    .rst_ni  (rst_ni),

    // from register interface
    .we     (interrupt_enable_we),
    .wd     (interrupt_enable_wd),

    // from internal hardware
    .de     (1'b0),
    .d      ('0),

    // to internal hardware
    .qe     (),
    .q      (reg2hw.interrupt_enable.q),
    .ds     (),

    // to register interface (read)
    .qs     (interrupt_enable_qs)
  );



  logic [5:0] addr_hit;
  always_comb begin
    addr_hit = '0;
    addr_hit[0] = (reg_addr == REV_CTL_START_ADDR_OFFSET);
    addr_hit[1] = (reg_addr == REV_CTL_END_ADDR_OFFSET);
    addr_hit[2] = (reg_addr == REV_CTL_GO_OFFSET);
    addr_hit[3] = (reg_addr == REV_CTL_EPOCH_OFFSET);
    addr_hit[4] = (reg_addr == REV_CTL_INTERRUPT_STATUS_OFFSET);
    addr_hit[5] = (reg_addr == REV_CTL_INTERRUPT_ENABLE_OFFSET);
  end

  assign addrmiss = (reg_re || reg_we) ? ~|addr_hit : 1'b0 ;

  // Check sub-word write is permitted
  always_comb begin
    wr_err = (reg_we &
              ((addr_hit[0] & (|(REV_CTL_PERMIT[0] & ~reg_be))) |
               (addr_hit[1] & (|(REV_CTL_PERMIT[1] & ~reg_be))) |
               (addr_hit[2] & (|(REV_CTL_PERMIT[2] & ~reg_be))) |
               (addr_hit[3] & (|(REV_CTL_PERMIT[3] & ~reg_be))) |
               (addr_hit[4] & (|(REV_CTL_PERMIT[4] & ~reg_be))) |
               (addr_hit[5] & (|(REV_CTL_PERMIT[5] & ~reg_be)))));
  end

  // Generate write-enables
  assign start_addr_we = addr_hit[0] & reg_we & !reg_error;

  assign start_addr_wd = reg_wdata[31:0];
  assign end_addr_we = addr_hit[1] & reg_we & !reg_error;

  assign end_addr_wd = reg_wdata[31:0];
  assign go_re = addr_hit[2] & reg_re & !reg_error;
  assign go_we = addr_hit[2] & reg_we & !reg_error;

  assign go_wd = reg_wdata[31:0];
  assign interrupt_status_re = addr_hit[4] & reg_re & !reg_error;
  assign interrupt_status_we = addr_hit[4] & reg_we & !reg_error;

  assign interrupt_status_wd = reg_wdata[0];
  assign interrupt_enable_we = addr_hit[5] & reg_we & !reg_error;

  assign interrupt_enable_wd = reg_wdata[0];

  // Read data return
  always_comb begin
    reg_rdata_next = '0;
    unique case (1'b1)
      addr_hit[0]: begin
        reg_rdata_next[31:0] = start_addr_qs;
      end

      addr_hit[1]: begin
        reg_rdata_next[31:0] = end_addr_qs;
      end

      addr_hit[2]: begin
        reg_rdata_next[31:0] = go_qs;
      end

      addr_hit[3]: begin
        reg_rdata_next[0] = epoch_running_qs;
        reg_rdata_next[31:1] = epoch_epoch_qs;
      end

      addr_hit[4]: begin
        reg_rdata_next[0] = interrupt_status_qs;
      end

      addr_hit[5]: begin
        reg_rdata_next[0] = interrupt_enable_qs;
      end

      default: begin
        reg_rdata_next = '1;
      end
    endcase
  end

  // shadow busy
  logic shadow_busy;
  assign shadow_busy = 1'b0;

  // register busy
  assign reg_busy = shadow_busy;

  // Unused signal tieoff

  // wdata / byte enable are not always fully used
  // add a blanket unused statement to handle lint waivers
  logic unused_wdata;
  logic unused_be;
  assign unused_wdata = ^reg_wdata;
  assign unused_be = ^reg_be;

  // Assertions for Register Interface
  `ASSERT_PULSE(wePulse, reg_we, clk_i, !rst_ni)
  `ASSERT_PULSE(rePulse, reg_re, clk_i, !rst_ni)

  `ASSERT(reAfterRv, $rose(reg_re || reg_we) |=> tl_o_pre.d_valid, clk_i, !rst_ni)

  `ASSERT(en2addrHit, (reg_we || reg_re) |-> $onehot0(addr_hit), clk_i, !rst_ni)

  // this is formulated as an assumption such that the FPV testbenches do disprove this
  // property by mistake
  //`ASSUME(reqParity, tl_reg_h2d.a_valid |-> tl_reg_h2d.a_user.chk_en == tlul_pkg::CheckDis)

endmodule
