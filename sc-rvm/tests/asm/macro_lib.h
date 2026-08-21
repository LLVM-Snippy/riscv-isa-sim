// NOTE: init_d stands for "initialize destination"
.macro pext_extended_check insn init_d
  la a0, \insn\()_rs1_end
  la a1, \insn\()_rs1_start
  la a2, \insn\()_rs2_start
  la a3, \insn\()_rd_exp_start
  la a4, \insn\()_rd_ov_start
.ifc init_d,\init_d
  la a5, \insn\()_rd_start
.endif
1:
  csrci   vxsat, 0x1
.ifc init_d,\init_d
  // t1 is destination
  lw t1, 0(a5)
.endif
  // t2 is rs1
  lw t2, 0(a1)
  // t3 is rs2
  lw t3, 0(a2)
  // do operation
  \insn t1, t2, t3
  // t4 is expected destination
  lw t4, 0(a3)
  // t5 is expected OV
  lw t5, 0(a4)
  bne t1, t4, _sim_fail
  csrr t1, vxsat
  bne t1, t5, _sim_fail
  addi a1, a1, 4
  addi a2, a2, 4
  addi a3, a3, 4
  addi a4, a4, 4
.ifc init_d,\init_d
  addi a5, a5, 4
.endif
  blt a1, a0, 1b
.endm

.macro basic_check insn in1 in2 result
  li t5, \in1
  li gp, \in2
  \insn a4, t5, gp
  li a0, \result
  bne a0, a4, _sim_fail
.endm

.macro basic_check_pair insn in1 in2 result1 result2
  li t5, \in1
  li gp, \in2
  \insn a4, t5, gp
  li a0, \result1
  li a1, \result2
  bne a0, a4, _sim_fail
  bne a1, a5, _sim_fail
.endm

.macro basic_check_imm_op insn in1 in2 result ov_exp
  csrci   vxsat, 0x1
  li t5, \in1
  \insn a4, t5, \in2
  li a0, \result
  bne a0, a4, _sim_fail
  li a0, \ov_exp
  csrr a4, vxsat
  bne a0, a4, _sim_fail
.endm
