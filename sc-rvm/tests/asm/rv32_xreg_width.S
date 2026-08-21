.global _entry
.global main
.global _sim_end
.global write_done
.global miserable_failure

.text
_entry:
  nop
main:
  lui s2, 0x1
  lui s3, 0x2
  sub s2, s2, s3
  lui s3, 0x88888

_sim_end:
write_done:
  j write_done

miserable_failure:
  j miserable_failure
