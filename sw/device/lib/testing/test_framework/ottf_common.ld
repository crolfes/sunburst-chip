/* Copyright lowRISC contributors (OpenTitan project). */
/* Licensed under the Apache License, Version 2.0, see LICENSE for details. */
/* SPDX-License-Identifier: Apache-2.0 */

/**
 * NOTE:
 * This is an incomplete common portion of OTTF linker file, and should not
 * be used directly. Instead it should be included by a top level OTTF slot
 * linker file.
 */

OUTPUT_ARCH(riscv)

/**
 * Indicate that there are no dynamic libraries, whatsoever.
 */
__DYNAMIC = 0;

/**
 * Marking the entry point correctly for the ELF file. The signer tool will
 * transfer this value to the `entry_point` field of the manifest, which will
 * then be used by `rom_boot` or `rom_ext_boot` to handover execution to
 * the OTTF.
 */
ENTRY(_ottf_start);

/**
 * DV Log offset.
 * TODO: this will need to be different depending on the boot stage the OTTF is
 * launched at. See lowrisc/opentitan:#10712.
 */
_dv_log_offset = 0x10000;

/**
 * The start of the .text section relative to the beginning of the associated
 * slot for use in the manifest.
 */
_manifest_code_start = _text_start - _ottf_start_address;

/**
 * The end of the .text section relative to the beginning of the associated slot
 * for use in the manifest.
 */
_manifest_code_end = _text_end - _ottf_start_address;

/**
 * The location of the entry point relative to the beginning of the associated
 * slot for use in the manifest.
 */
_manifest_entry_point = _ottf_start - _ottf_start_address;
/*
 * The value kHardenedBoolFalse (0x1d4) is selected if the code is linked in
 * the flash region.  Otherwise, the value kHardenedBoolTrue (0x739) is
 * selected.
 */
_manifest_address_translation = (_text_start >= ORIGIN(eflash) &&
                                 _text_start < (ORIGIN(eflash)
                                              + LENGTH(eflash))) ? 0x1d4 : 0x739;

PHDRS {
  static_critical_segment PT_LOAD;
  manifest_segment PT_LOAD;
  data_segment PT_LOAD;
  freertos_segment PT_LOAD;
  scratch_segment PT_LOAD;
}

/**
 * NOTE: We have to align each section to word boundaries as our current
 * s19->slm conversion scripts are not able to handle non-word aligned sections.
 */
SECTIONS {
  /**
   * Critical static data that is accessible by both the ROM and the ROM
   * extension.
   */
  INCLUDE sw/device/silicon_creator/lib/base/static_critical.ld

  .manifest _ottf_start_address : {
    KEEP(*(.manifest))
    . = ALIGN(4);
  } > ottf_flash : manifest_segment

  /**
   * Immutable ROM_EXT section.
   *
   * Resides in the .text section so it is covered by the ePMP configuration
   * that enables ROM_EXT execution.
   */
  .rom_ext_immutable : ALIGN(4) {
    _text_start = .;
    _rom_ext_immutable_start = .;
    KEEP(*(.rom_ext_immutable))
    /* Ensure section end is 256-byte aligned for interrupt vector after. */
    . = ALIGN(256);
    _rom_ext_immutable_end = .;
  } > ottf_flash


  /**
   * Ibex interrupt vector. See 'ottf_start.S' for more information.
   *
   * This has to be set up at a 256-byte offset, so that we can use it with
   * Ibex.
   */
  .vectors : ALIGN (256){
    *(.vectors)
  } > ottf_flash

  /**
   * C runtime (CRT) section, containing program initialization code.
   */
  .crt : ALIGN(4) {
    KEEP(*(.crt))
  } > ottf_flash

  /**
   * For LLVM profiling. This contains a pointer to the runtime initialization
   * function that is generated by the compiler. See
   * 'InstrProfiling::emitInitialization()' in 'InstrProfiling.cpp' and
   * 'getInstrProfInitFuncName()' in 'InstrProf.h'.
   */
  .init_array : ALIGN(4) {
      _init_array_start = .;
      KEEP(*(.init_array))
      KEEP(*(.init_array.*))
      . = ALIGN(4);
      _init_array_end = .;
  } > ottf_flash

  /**
   * Standard text section, containing program code.
   */
  .text : ALIGN(4) {
    *(.text)
    *(.text.*)

    /* Ensure section end is word-aligned. */
    . = ALIGN(4);
  } > ottf_flash

  /**
   * Shutdown text section, containing shutdown function(s).
   *
   * This must be the last executable section in the OTTF flash image.
   */
  .shutdown : ALIGN(4) {
    *(.shutdown)
    *(.shutdown.*)

    /* Ensure section end is word-aligned. */
    . = ALIGN(4);
    _text_end = .;
  } > ottf_flash

  /**
   * Read-only data section, containing all large compile-time constants, like
   * strings.
   */
  .rodata : ALIGN(4) {
    /**
     * Small read-only data comes before regular read-only data for the same
     * reasons as in the data section.
     */
    *(.srodata)
    *(.srodata.*)
    *(.rodata)
    *(.rodata.*)

    /* Read-only sections for LLVM profiling. */
    KEEP(*(__llvm_covfun))
    KEEP(*(__llvm_covmap))
    KEEP(*(__llvm_prf_names))
  } > ottf_flash

  /**
   * Standard mutable data section, at the bottom of RAM. This will be
   * initialized from the .idata section at runtime by the CRT.
   */
  .data : ALIGN(4) {
    _data_start = .;
    _data_init_start = LOADADDR(.data);

    /**
     * This will get loaded into `gp`, and the linker will use that register for
     * accessing data within [-2048,2047] of `__global_pointer$`.
     *
     * This is much cheaper (for small data) than materializing the
     * address and loading from that (which will take one extra instruction).
     */
    __global_pointer$ = . + 2048;

    /* SRAM programs embedded in functional tests must come first. */
    *(.data.sram_program)

    /**
     * Small data should come before larger data. This helps to ensure small
     * globals are within 2048 bytes of the value of `gp`, making their accesses
     * hopefully only take one instruction.
     */
    *(.sdata)
    *(.sdata.*)

    /**
     * Other data will likely need multiple instructions to load, so we're less
     * concerned about address materialisation taking more than one instruction.
     */
    *(.data)
    *(.data.*)

    /* Sections for LLVM profiling. */
    KEEP(*(__llvm_prf_cnts))
    KEEP(*(__llvm_prf_data))

    /* Ensure section end is word-aligned. */
    . = ALIGN(4);
    _data_end = .;
    _data_init_end = LOADADDR(.data) + SIZEOF(.data);

    /**
     * This puts it in ram_main at runtime (for the VMA), but puts the section
     * into flash for load time (for the LMA). This is why `_data_init_*` uses
     * `LOADADDR`.
     *
     * Using `AT>` means we don't have to keep track of the next free part of
     * flash, as we do in our other linker scripts.
     */
  } > ram_main AT> ottf_flash :data_segment

  /**
   * Standard BSS section. This will be zeroed at runtime by the CRT.
   */
  .bss : ALIGN(4) {
    _bss_start = .;

    /**
     * Small BSS comes before regular BSS for the same reasons as in the data
     * section.
     */
    *(.sbss)
    *(.sbss.*)
    *(.bss)
    *(.bss.*)

    /* Ensure section end is word-aligned. */
    . = ALIGN(4);
    _bss_end = .;
  } > ram_main

  /**
   * FreeRTOS heap (for OTTF).
   *
   * This is a separate NOLOAD section so that it does not end up in the `.bss`
   * section which gets zeroed during the second boot stage initialization,
   * causing wasted simulation cycles.
   */
  .freertos.heap (NOLOAD): ALIGN(4) {
    _freertos_heap_start = .;
    *(.freertos.heap)
  } > ram_main :freertos_segment

  /**
   * Non-volatile scratch and counter areas for tests.
   *
   * These sections are meant to be used by tests to store data that should
   * persist across resets. Since `opentitantool` assumes that the signed
   * region of an image extends until the end, these sections are placed after
   * all other sections and are NOLOAD so that they don't appear in the binary
   * and therefore don't end up being covered by the signature.
   */
   _non_volatile_scratch_size = DEFINED(no_ottf_nv_scratch) ? 0 : 20480;
   _non_volatile_scratch_end = ORIGIN(ottf_flash) + _ottf_size;
   _non_volatile_scratch_start = _non_volatile_scratch_end - _non_volatile_scratch_size;

   _non_volatile_counter_size = DEFINED(no_ottf_nv_counter) ? 0 : 2048;
   _non_volatile_counter_flash_words = _non_volatile_counter_size / 8;

   _non_volatile_counter_0_end = _non_volatile_scratch_start;
   _non_volatile_counter_0_start = _non_volatile_counter_0_end - _non_volatile_counter_size;

   _non_volatile_counter_1_end = _non_volatile_counter_0_start;
   _non_volatile_counter_1_start = _non_volatile_counter_1_end - _non_volatile_counter_size;

   _non_volatile_counter_2_end = _non_volatile_counter_1_start;
   _non_volatile_counter_2_start = _non_volatile_counter_2_end - _non_volatile_counter_size;

   _non_volatile_counter_3_end = _non_volatile_counter_2_start;
   _non_volatile_counter_3_start = _non_volatile_counter_3_end - _non_volatile_counter_size;

  /**
   * Non-volatile counter area in flash.
   *
   * Similar to the `.non_volatile_scratch` section, this section is meant to
   * be used by tests. Because we don't want to write to the same flash word
   * twice, tests that require non-volatile counters can use this section by
   * striking flash words to count up to `_non_volatile_counter_max_count`.
   *
   * The sections are ordered by ascending memory address, and thus by
   * descending volatile counter number. That prevents linker problems.
   */
  .non_volatile_counter_3 _non_volatile_counter_3_start (NOLOAD) : ALIGN(2048) {
    KEEP(*(.non_volatile_counter_3))
    KEEP(*(.non_volatile_counter_3.*))
    . = ABSOLUTE(_non_volatile_counter_3_end);
  } > ottf_flash :scratch_segment

  .non_volatile_counter_2 _non_volatile_counter_2_start (NOLOAD) : ALIGN(2048) {
    KEEP(*(.non_volatile_counter_2))
    KEEP(*(.non_volatile_counter_2.*))
    . = ABSOLUTE(_non_volatile_counter_2_end);
  } > ottf_flash

  .non_volatile_counter_1 _non_volatile_counter_1_start (NOLOAD) : ALIGN(2048) {
    KEEP(*(.non_volatile_counter_1))
    KEEP(*(.non_volatile_counter_1.*))
    . = ABSOLUTE(_non_volatile_counter_1_end);
  } > ottf_flash

  .non_volatile_counter_0 _non_volatile_counter_0_start (NOLOAD) : ALIGN(2048) {
    KEEP(*(.non_volatile_counter_0))
    KEEP(*(.non_volatile_counter_0.*))
    . = ABSOLUTE(_non_volatile_counter_0_end);
  } > ottf_flash

  /**
   * Non-volatile scratch area in flash.
   */
  .non_volatile_scratch _non_volatile_scratch_start (NOLOAD) : ALIGN(2048) {
    begin_of_non_volatile_scratch = . ;
    KEEP(*(.non_volatile_scratch))
    KEEP(*(.non_volatile_scratch.*))
    . = ABSOLUTE(_non_volatile_scratch_end);
    end_of_non_volatile_scratch = . ;
  } > ottf_flash

  INCLUDE sw/device/info_sections.ld
}
