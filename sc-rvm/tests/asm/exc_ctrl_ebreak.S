.global _entry
.global _sim_end

.text
_entry:
  la x1, exc_handler
  csrw mtvec, x1
  ebreak
  wfi

.balign 16
exc_handler:
  csrr x1, mcause
  li x2, 3
  beq x1, x2, _sim_end
  j failure
_sim_end:
  j _sim_end

failure:
  j failure
