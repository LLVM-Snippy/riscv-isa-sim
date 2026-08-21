.global _entry

.text
_entry:
  la x1, exc_handler
  csrw mtvec, x1
  ebreak
  wfi

.balign 16
exc_handler:
  li x1, 0
loop:
  wfi
  j loop
