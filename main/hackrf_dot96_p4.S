/* SPDX-License-Identifier: GPL-2.0-or-later
 * Fixed-size planar I/Q Q15 FIR dot products using ESP32-P4 PIE.
 * Instruction reference: Espressif esp-dsp v1.6.4 dsps_dotprod_s16_arp4.S
 * (Apache-2.0). No esp-dsp code is linked or copied.
 * 96 coefficients, two contiguous 96-element signed-16 histories. No padding
 * reads, no shifts/rounding until C quantization; |sum| fits signed 32 bits.
 * q0/q1 and xacc are caller-clobbered, as in Espressif's dot-product ABI.
 */
.text
.align 4
.global hackrf_dot96_p4
.type hackrf_dot96_p4,@function
hackrf_dot96_p4:
    /* a0 coeff, a1 I, a2 Q, a3 int32_t sums[2]. */
    esp.movx.r.cfg t6
    ori a4,t6,2
    esp.movx.w.cfg a4
    mv a5,a0
    esp.zero.xacc
    .rept 12
        esp.vld.128.ip q0,a5,16
        esp.vld.128.ip q1,a1,16
        esp.vmulas.s16.xacc q0,q1
    .endr
    li t4,0
    esp.srs.s.xacc t5,t4
    sw t5,0(a3)
    esp.zero.xacc
    .rept 12
        esp.vld.128.ip q0,a0,16
        esp.vld.128.ip q1,a2,16
        esp.vmulas.s16.xacc q0,q1
    .endr
    esp.srs.s.xacc t5,t4
    sw t5,4(a3)
    /* Match esp-dsp: CFG is not task-local in IDF; leave unaligned access
     * enabled instead of restoring a stale pre-preemption global value. */
    ret
.size hackrf_dot96_p4,.-hackrf_dot96_p4
