.global _entry
.global main
.global _sim_end
.global write_done
.global miserable_failure

.text
_entry:
  nop
main:
  li sp, ((1 << 9) | (1 << 13)) // mstatus.VS = 1 (initial), mstatus.FS = 1
  csrs  mstatus, sp
  fcvt.s.w ft0,  sp
  li t1, 0x7f
  la t2, data_store
  sb t1, (t2)
  li t1, 0x7fff
  sh t1, 8(t2)
  li t1, 0x7fffffff
  sw t1, 16(t2)
  li t1, 0x7fffffffffffffff
  sd t1, 24(t2)
  la t2, data_load
  li t1, 0xda
  sb t1, (t2)
  lb t0, (t2)
  li t1, 0xbaba
  sh t1, 8(t2)
  lh t0, 8(t2)
  li t1, 0xcaccaccc
  sw t1, 16(t2)
  lw t0, 16(t2)
  li t1, 0xdeadbeefdeadbeef
  sd t1, 24(t2)
  ld t0, 24(t2)
  li t1, 0x1111111111111111
  sd t1, (t2)
  ld t0, (t2)
  amoadd.w.aqrl t0, t0, (t2)

_sim_end:
write_done:
  j write_done

miserable_failure:
  j miserable_failure

.balign 8
data_load:
.8byte 0x123456789abcdeff
data_store:
.8byte 0

