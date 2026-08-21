.global _entry
.global _sim_end

.option norvc

.text
_entry:
  nop
main:
  la x1, exc_handler
  csrw mtvec, x1
  csrr a0, vxsat
  j failure

.balign 16
exc_handler:
  csrr x1, mcause
  li x2, 2
  beq x1, x2, _sim_end
  j failure
_sim_end:
  j _sim_end

failure:
  j failure
