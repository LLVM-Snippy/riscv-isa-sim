.altmacro

.global _entry
.global main
.global _sim_end
.global write_done
.global miserable_failure

.text
_entry:
  nop
main:
  li sp, (1 << 9) // mstatus.VS = 1 (initial)
  csrs  mstatus, sp

  // vtype: 11000'b { vlmul = 0, SEW = 64 }
  li s0, 0x18
  csrr    s1, vl
  vsetvl  zero, s1, s0

  csrr s1, vl
  srli s1, s1, 63
  bne zero, s1, miserable_failure

  // vl = 0x2
  li s0, 0x2
  csrr    s1, vtype
  vsetvl  zero, s0, s1

  csrr s1, vl
  srli s1, s1, 63

  bne zero, s1, miserable_failure

  li t2, 31

.macro slide regn
  li t1, 1
  slli t2, t1, \regn
  add s0, zero, t2
  sub s1, zero, t2
  vslide1down.vx v\regn, v\regn, s0
  vslide1down.vx v\regn, v\regn, s1
.endm

.set i,0
.rept 32
  slide %i
  .set i,i+1
.endr

  # several vector registers.
  # expect vlen=128.
  vsetivli t0, 0x4, e64, m2

.macro vadd_proc regn
  vadd.vi v\regn, v\regn, 1
.endm

.set i,0
# as lmul=m2
.rept 16
  vadd_proc %i
  .set i,i+2
.endr
 
  

.macro add_zero regn
  addi x\regn, zero, \regn
.endm

.set i,0
.rept 32
  add_zero %i
  .set i,i+1
.endr

_sim_end:
write_done:
  j write_done

miserable_failure:
  j miserable_failure

