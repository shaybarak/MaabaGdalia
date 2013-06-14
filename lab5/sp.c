#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>

#include "llsim.h"

#define sp_printf(a...)					\
  do {							\
    llsim_printf("sp: clock %d: ", llsim->clock);	\
    llsim_printf(a);					\
  } while (0)

int nr_simulated_instructions = 0;
FILE *inst_trace_fp = NULL, *cycle_trace_fp = NULL;

#define BTB_SIZE 64

char btb_is_taken[BTB_SIZE] = {0}; // 1 bit array
int btb_target [BTB_SIZE] = {0};   // 16 bits array

typedef struct sp_registers_s {
  // 6 32 bit registers (r[0], r[1] don't exist)
  int r[8];

  // 32 bit cycle counter
  int cycle_counter;

  // fetch0
  int fetch0_active; // 1 bit
  int fetch0_pc; // 16 bits

  // fetch1
  int fetch1_active; // 1 bit
  int fetch1_pc; // 16 bits
  int fetch1_saved_inst; // 32 bits
  int fetch1_use_saved; // 1 bit
  int fetch1_btb_target; //16 bits
  int fetch1_btb_is_taken; //1 bit
  
  // dec0
  int dec0_active; // 1 bit
  int dec0_pc; // 16 bits
  int dec0_inst; // 32 bits
  int dec0_btb_target; //16 bits
  int dec0_btb_is_taken; //1 bit

  // dec1
  int dec1_active; // 1 bit
  int dec1_pc; // 16 bits
  int dec1_inst; // 32 bits
  int dec1_opcode; // 5 bits
  int dec1_src0; // 3 bits
  int dec1_src1; // 3 bits
  int dec1_dst; // 3 bits
  int dec1_immediate; // 32 bits
  int dec1_btb_target; //16 bits
  int dec1_btb_is_taken; //1 bit

  // exec0
  int exec0_active; // 1 bit
  int exec0_pc; // 16 bits
  int exec0_inst; // 32 bits
  int exec0_opcode; // 5 bits
  int exec0_src0; // 3 bits
  int exec0_src1; // 3 bits
  int exec0_dst; // 3 bits
  int exec0_immediate; // 32 bits
  int exec0_alu0; // 32 bits
  int exec0_alu1; // 32 bits
  int exec0_btb_target; //16 bits
  int exec0_btb_is_taken; //1 bit

  // exec1
  int exec1_active; // 1 bit
  int exec1_pc; // 16 bits
  int exec1_inst; // 32 bits
  int exec1_opcode; // 5 bits
  int exec1_src0; // 3 bits
  int exec1_src1; // 3 bits
  int exec1_dst; // 3 bits
  int exec1_immediate; // 32 bits
  int exec1_alu0; // 32 bits
  int exec1_alu1; // 32 bits
  int exec1_aluout; //32 bits
  int exec1_btb_target; //16 bits
  int exec1_btb_is_taken; //1 bit

  // MEM stall
  int mem_stall; // 1 bit
  int mem_SRAM_DO; // 32 bits
  int mem_dst; // 3 bits
} sp_registers_t;

/*
 * Master structure
 */
typedef struct sp_s {
  // local srams
#define SP_SRAM_HEIGHT	64 * 1024
  llsim_memory_t *srami, *sramd;

  unsigned int memory_image[SP_SRAM_HEIGHT];
  int memory_image_size;

  int start;

  sp_registers_t *spro, *sprn;
} sp_t;

static void sp_reset(sp_t *sp)
{
  sp_registers_t *sprn = sp->sprn;

  memset(sprn, 0, sizeof(*sprn));
}

/*
 * opcodes
 */
#define ADD 0
#define SUB 1
#define LSF 2
#define RSF 3
#define AND 4
#define OR  5
#define XOR 6
#define LHI 7
#define LD 8
#define ST 9
#define JLT 16
#define JLE 17
#define JEQ 18
#define JNE 19
#define JIN 20
#define HLT 24

static char opcode_name[32][4] = {"ADD", "SUB", "LSF", "RSF", "AND", "OR", "XOR", "LHI",
				  "LD", "ST", "U", "U", "U", "U", "U", "U",
				  "JLT", "JLE", "JEQ", "JNE", "JIN", "U", "U", "U",
				  "HLT", "U", "U", "U", "U", "U", "U", "U"};

static void dump_sram(sp_t *sp, char *name, llsim_memory_t *sram)
{
  FILE *fp;
  int i;

  fp = fopen(name, "w");
  if (fp == NULL) {
    printf("couldn't open file %s\n", name);
    exit(1);
  }
  for (i = 0; i < SP_SRAM_HEIGHT; i++)
    fprintf(fp, "%08x\n", llsim_mem_extract(sram, i, 31, 0));
  fclose(fp);
}

static void printInstruction(sp_registers_t *sp) {
  fprintf(inst_trace_fp, "--- instruction %d (%04x) @ PC %d (%04x) -----------------------------------------------------------\n",
	  nr_simulated_instructions, nr_simulated_instructions, sp->exec1_pc, sp->exec1_pc);
  
  fprintf(inst_trace_fp, "pc = %04x, inst = %08x, opcode = %d (%s), dst = %d, src0 = %d, src1 = %d, immediate = %08x\n",
	  sp->exec1_pc, sp->exec1_inst, sp->exec1_opcode, opcode_name[sp->exec1_opcode], sp->exec1_dst, sp->exec1_src0, sp->exec1_src1, sp->exec1_immediate);
  
  fprintf(inst_trace_fp, "r[0] = 00000000 r[1] = %08x r[2] = %08x r[3] = %08x \n", sp->exec1_immediate, sp->r[2], sp->r[3]);
  fprintf(inst_trace_fp, "r[4] = %08x r[5] = %08x r[6] = %08x r[7] = %08x \n\n", sp->r[4], sp->r[5], sp->r[6], sp->r[7]);
  nr_simulated_instructions += 1;
}

static void printExecution(sp_registers_t *sp) {
  switch(sp->exec1_opcode) {
  case ADD:
    fprintf(inst_trace_fp, ">>>> EXEC: R[%d] = %d ADD %d <<<<\n\n", sp->exec1_dst, sp->exec1_alu0, sp->exec1_alu1);
    break;
  case SUB:
    fprintf(inst_trace_fp, ">>>> EXEC: R[%d] = %d SUB %d <<<<\n\n", sp->exec1_dst, sp->exec1_alu0, sp->exec1_alu1);
    break;
  case LSF:
    fprintf(inst_trace_fp, ">>>> EXEC: R[%d] = %d LSF %d <<<<\n\n", sp->exec1_dst, sp->exec1_alu0, sp->exec1_alu1);
    break;
  case RSF:
    fprintf(inst_trace_fp, ">>>> EXEC: R[%d] = %d RSF %d <<<<\n\n", sp->exec1_dst, sp->exec1_alu0, sp->exec1_alu1);
    break;
  case AND:
    fprintf(inst_trace_fp, ">>>> EXEC: R[%d] = %d AND %d <<<<\n\n", sp->exec1_dst, sp->exec1_alu0, sp->exec1_alu1);
    break;
  case OR:
    fprintf(inst_trace_fp, ">>>> EXEC: R[%d] = %d OR %d <<<<\n\n", sp->exec1_dst, sp->exec1_alu0, sp->exec1_alu1);
    break;
  case XOR:
    fprintf(inst_trace_fp,">>>> EXEC: R[%d] = %d XOR %d <<<<\n\n", sp->exec1_dst, sp->exec1_alu0, sp->exec1_alu1);
    break;
  case LHI:
    fprintf(inst_trace_fp,">>>> EXEC: R[%d][31:16] = %d <<<<\n\n", sp->exec1_dst, sp->exec1_alu1);
    break;
  case JLT:
    fprintf(inst_trace_fp, ">>>> EXEC: JLT %d, %d, %d <<<<\n\n", sp->exec1_alu0, sp->exec1_alu1, (sp->exec1_aluout ? sp->exec1_immediate : sp->exec1_pc + 1));
    break;
  case JLE:
    fprintf(inst_trace_fp, ">>>> EXEC: JLE %d, %d, %d <<<<\n\n", sp->exec1_alu0, sp->exec1_alu1, (sp->exec1_aluout ? sp->exec1_immediate : sp->exec1_pc + 1));
    break;
  case JEQ:
    fprintf(inst_trace_fp, ">>>> EXEC: JEQ %d, %d, %d <<<<\n\n", sp->exec1_alu0, sp->exec1_alu1, (sp->exec1_aluout ? sp->exec1_immediate : sp->exec1_pc + 1));
    break;
  case JNE:
    fprintf(inst_trace_fp, ">>>> EXEC: JNE %d, %d, %d <<<<\n\n", sp->exec1_alu0, sp->exec1_alu1, (sp->exec1_aluout ? sp->exec1_immediate : sp->exec1_pc + 1));
    break;
  case JIN:
    fprintf(inst_trace_fp, ">>>> EXEC: JIN R[%d] = %08x <<<<\n\n", sp->exec1_src0, sp->exec1_alu0);
    break;
  }
}

static void sp_ctl(sp_t *sp)
{
  sp_registers_t *spro = sp->spro;
  sp_registers_t *sprn = sp->sprn;
  int i;
  int immediate;
  int inst;

  //Stall
  int fetch0_stalled = 0;
  int fetch1_stalled = 0;
  int dec0_stalled = 0;
  int dec1_stalled = 0;
  int exec0_stalled = 0;

  // Bypasses
  int dec1_r0_bypass = 0;
  int dec1_r0_bypass_en = 0;
  int dec1_r0_final = 0;
  int dec1_r1_bypass = 0;
  int dec1_r1_bypass_en = 0;
  int dec1_r1_final = 0;
  int dec1_r1_cmp = 0;
  int exec0_alu0_bypass = 0;
  int exec0_alu0_bypass_en = 0;
  int exec0_alu1_bypass = 0;
  int exec0_alu1_bypass_en = 0;
  int exec0_mem_to_alu0_bypass = 0;
  int exec0_mem_to_alu1_bypass = 0;
  int exec0_exec1_to_alu0_bypass = 0;
  int exec0_exec1_to_alu1_bypass = 0;
  int exec0_alu0_final = 0;
  int exec0_alu1_final = 0;
  int exec0_alu1_cmp = 0;
  
  

  fprintf(cycle_trace_fp, "cycle %d\n", spro->cycle_counter);
  fprintf(cycle_trace_fp, "cycle_counter %08x\n", spro->cycle_counter);
  for (i = 2; i <= 7; i++)
    fprintf(cycle_trace_fp, "r%d %08x\n", i, spro->r[i]);

  fprintf(cycle_trace_fp, "fetch0_active %d\n", spro->fetch0_active);
  fprintf(cycle_trace_fp, "fetch0_pc %08x\n", spro->fetch0_pc);

  fprintf(cycle_trace_fp, "fetch1_active %d\n", spro->fetch1_active);
  fprintf(cycle_trace_fp, "fetch1_pc %08x\n", spro->fetch1_pc);
  fprintf(cycle_trace_fp, "fetch1_btb_is_taken %d\n", spro->fetch1_btb_is_taken);
  fprintf(cycle_trace_fp, "fetch1_btb_target %08x\n", spro->fetch1_btb_target);

  fprintf(cycle_trace_fp, "dec0_active %d\n", spro->dec0_active);
  fprintf(cycle_trace_fp, "dec0_pc %08x\n", spro->dec0_pc);
  fprintf(cycle_trace_fp, "dec0_inst %08x\n", spro->dec0_inst); // 32 bits
  fprintf(cycle_trace_fp, "dec0_btb_is_taken %d\n", spro->dec0_btb_is_taken);
  fprintf(cycle_trace_fp, "dec0_btb_target %08x\n", spro->dec0_btb_target);

  fprintf(cycle_trace_fp, "dec1_active %d\n", spro->dec1_active);
  fprintf(cycle_trace_fp, "dec1_pc %08x\n", spro->dec1_pc); // 16 bits
  fprintf(cycle_trace_fp, "dec1_inst %08x\n", spro->dec1_inst); // 32 bits
  fprintf(cycle_trace_fp, "dec1_opcode %08x\n", spro->dec1_opcode); // 5 bits
  fprintf(cycle_trace_fp, "dec1_src0 %08x\n", spro->dec1_src0); // 3 bits
  fprintf(cycle_trace_fp, "dec1_src1 %08x\n", spro->dec1_src1); // 3 bits
  fprintf(cycle_trace_fp, "dec1_dst %08x\n", spro->dec1_dst); // 3 bits
  fprintf(cycle_trace_fp, "dec1_immediate %08x\n", spro->dec1_immediate); // 32 bits
  fprintf(cycle_trace_fp, "dec1_btb_is_taken %d\n", spro->dec1_btb_is_taken);
  fprintf(cycle_trace_fp, "dec1_btb_target %08x\n", spro->dec1_btb_target);

  fprintf(cycle_trace_fp, "exec0_active %d\n", spro->exec0_active);
  fprintf(cycle_trace_fp, "exec0_pc %08x\n", spro->exec0_pc); // 16 bits
  fprintf(cycle_trace_fp, "exec0_inst %08x\n", spro->exec0_inst); // 32 bits
  fprintf(cycle_trace_fp, "exec0_opcode %08x\n", spro->exec0_opcode); // 5 bits
  fprintf(cycle_trace_fp, "exec0_src0 %08x\n", spro->exec0_src0); // 3 bits
  fprintf(cycle_trace_fp, "exec0_src1 %08x\n", spro->exec0_src1); // 3 bits
  fprintf(cycle_trace_fp, "exec0_dst %08x\n", spro->exec0_dst); // 3 bits
  fprintf(cycle_trace_fp, "exec0_immediate %08x\n", spro->exec0_immediate); // 32 bits
  fprintf(cycle_trace_fp, "exec0_alu0 %08x\n", spro->exec0_alu0); // 32 bits
  fprintf(cycle_trace_fp, "exec0_alu1 %08x\n", spro->exec0_alu1); // 32 bits
  fprintf(cycle_trace_fp, "exec0_btb_is_taken %d\n", spro->exec0_btb_is_taken);
  fprintf(cycle_trace_fp, "exec0_btb_target %08x\n", spro->exec0_btb_target);

  fprintf(cycle_trace_fp, "exec1_active %d\n", spro->exec1_active);
  fprintf(cycle_trace_fp, "exec1_pc %08x\n", spro->exec1_pc); // 16 bits
  fprintf(cycle_trace_fp, "exec1_inst %08x\n", spro->exec1_inst); // 32 bits
  fprintf(cycle_trace_fp, "exec1_opcode %08x\n", spro->exec1_opcode); // 5 bits
  fprintf(cycle_trace_fp, "exec1_src0 %08x\n", spro->exec1_src0); // 3 bits
  fprintf(cycle_trace_fp, "exec1_src1 %08x\n", spro->exec1_src1); // 3 bits
  fprintf(cycle_trace_fp, "exec1_dst %08x\n", spro->exec1_dst); // 3 bits
  fprintf(cycle_trace_fp, "exec1_immediate %08x\n", spro->exec1_immediate); // 32 bits
  fprintf(cycle_trace_fp, "exec1_alu0 %08x\n", spro->exec1_alu0); // 32 bits
  fprintf(cycle_trace_fp, "exec1_alu1 %08x\n", spro->exec1_alu1); // 32 bits
  fprintf(cycle_trace_fp, "exec1_aluout %08x\n", spro->exec1_aluout);
  fprintf(cycle_trace_fp, "exec1_btb_is_taken %d\n", spro->exec1_btb_is_taken);
  fprintf(cycle_trace_fp, "exec1_btb_target %08x\n", spro->exec1_btb_target);
  
  sp_printf("cycle_counter %08x\n", spro->cycle_counter);
  sp_printf("r2 %08x, r3 %08x\n", spro->r[2], spro->r[3]);
  sp_printf("r4 %08x, r5 %08x, r6 %08x, r7 %08x\n", spro->r[4], spro->r[5], spro->r[6], spro->r[7]);
  sp_printf("fetch0_active %d, fetch1_active %d, dec0_active %d, dec1_active %d, exec0_active %d, exec1_active %d\n",
	    spro->fetch0_active, spro->fetch1_active, spro->dec0_active, spro->dec1_active, spro->exec0_active, spro->exec1_active);
  sp_printf("fetch0_pc %d, fetch1_pc %d, dec0_pc %d, dec1_pc %d, exec0_pc %d, exec1_pc %d\n",
	    spro->fetch0_pc, spro->fetch1_pc, spro->dec0_pc, spro->dec1_pc, spro->exec0_pc, spro->exec1_pc);

  sprn->cycle_counter = spro->cycle_counter + 1;

  if (sp->start)
    sprn->fetch0_active = 1;

  // Establish bypass signals
  dec1_r0_bypass_en = (spro->dec1_src0 == spro->exec1_dst) && (spro->exec1_active);
  dec1_r0_bypass = spro->exec1_aluout;
  dec1_r1_cmp = (spro->dec1_opcode == LHI) ? spro->dec1_dst : spro->dec1_src1;
  dec1_r1_bypass_en = (dec1_r1_cmp == spro->exec1_dst) && (spro->exec1_active);
  dec1_r1_bypass = spro->exec1_aluout;
  
  exec0_alu1_cmp = (spro->exec0_opcode == LHI) ? spro->exec0_dst : spro->exec0_src1;
  exec0_exec1_to_alu0_bypass = (spro->exec0_src0 == spro->exec1_dst) && (spro->exec1_opcode != ST) && (spro->exec1_active);
  exec0_exec1_to_alu1_bypass = (exec0_alu1_cmp == spro->exec1_dst) && (spro->exec1_opcode != ST) && (spro->exec1_active);
  exec0_mem_to_alu0_bypass = spro->mem_stall && (spro->exec0_src0 == spro->mem_dst) && (spro->exec1_active);
  exec0_mem_to_alu1_bypass = spro->mem_stall && (exec0_alu1_cmp == spro->mem_dst) && (spro->exec1_active);
  exec0_alu0_bypass_en = (exec0_exec1_to_alu0_bypass || exec0_mem_to_alu0_bypass);
  exec0_alu1_bypass_en = (exec0_exec1_to_alu1_bypass || exec0_mem_to_alu1_bypass);  
  exec0_alu0_bypass = (exec0_exec1_to_alu0_bypass && spro->exec1_active) ? spro->exec1_aluout : spro->mem_SRAM_DO;
  exec0_alu1_bypass = (exec0_exec1_to_alu1_bypass && spro->exec1_active) ? spro->exec1_aluout : spro->mem_SRAM_DO;

  exec0_stalled = (spro->exec1_active && 
		   ((spro->exec0_opcode == LD && spro->exec1_opcode == ST) || //structural
		    (spro->exec1_opcode == LD && ((spro->exec1_dst == spro->exec0_src0) || (spro->exec1_dst == exec0_alu1_cmp))))); //data Read after LD
		    
  dec1_stalled = exec0_stalled;
  dec0_stalled = dec1_stalled;
  fetch1_stalled = dec0_stalled;
  fetch0_stalled = fetch1_stalled;

  fprintf(cycle_trace_fp, "fetch0_stalled %d ", fetch0_stalled);
  fprintf(cycle_trace_fp, "fetch1_stalled %d ", fetch1_stalled);
  fprintf(cycle_trace_fp, "dec0_stalled %d ", dec0_stalled);
  fprintf(cycle_trace_fp, "dec1_stalled %d ", dec1_stalled);
  fprintf(cycle_trace_fp, "exec0_stalled %d\n", exec0_stalled);

  fprintf(cycle_trace_fp, "exec0_exec1_to_alu0_bypass %d ",exec0_exec1_to_alu0_bypass); 
  fprintf(cycle_trace_fp, "exec0_exec1_to_alu1_bypass %d ",exec0_exec1_to_alu1_bypass);
  fprintf(cycle_trace_fp, "exec0_mem_to_alu0_bypass %d ",exec0_mem_to_alu0_bypass); 
  fprintf(cycle_trace_fp, "exec0_mem_to_alu1_bypass %d\n",exec0_mem_to_alu1_bypass);

  fprintf(cycle_trace_fp, "\n");

  //TODO remove
  //if (spro->cycle_counter > 400) {
  //  llsim_stop();
  //}

  // fetch0
  if (spro->fetch0_active && !fetch0_stalled) {
    int btb_addr = spro->fetch0_pc % BTB_SIZE;
    llsim_mem_read(sp->srami, spro->fetch0_pc);
    if (btb_is_taken[btb_addr]) {
      sprn->fetch0_pc = btb_target[btb_addr];
    } else {
      sprn->fetch0_pc = spro->fetch0_pc + 1;
    }
    
    sprn->fetch1_active = 1;
    sprn->fetch1_pc = spro->fetch0_pc;
    sprn->fetch1_btb_is_taken = btb_is_taken[btb_addr];
    sprn->fetch1_btb_target = btb_target[btb_addr];
  }
	
  // fetch1
  if (spro->fetch1_active) {
    inst = llsim_mem_extract_dataout(sp->srami, 31, 0);
    if (fetch1_stalled) {
      sprn->fetch1_saved_inst = inst;
      sprn->fetch1_use_saved = 1;
    } else {
      sprn->fetch1_use_saved = 0;
      sprn->dec0_active = 1;
      sprn->dec0_inst = spro->fetch1_use_saved ? spro->fetch1_saved_inst : inst;
      sprn->dec0_pc = spro->fetch1_pc;
      sprn->dec0_btb_is_taken = spro->fetch1_btb_is_taken;
      sprn->dec0_btb_target = spro->fetch1_btb_target;
    }
  }
	
  // dec0
  if (spro->dec0_active && !dec0_stalled) {
    sprn->dec1_active = 1;
    sprn->dec1_pc = spro->dec0_pc;
    sprn->dec1_inst = spro->dec0_inst;
    sprn->dec1_opcode = (spro->dec0_inst >> 25) & 0x1F;
    sprn->dec1_src0 = (spro->dec0_inst >> 19) & 0x7;
    sprn->dec1_src1 = (spro->dec0_inst >> 16) & 0x7;
    sprn->dec1_dst = (spro->dec0_inst >> 22) & 0x7;
    immediate = (spro->dec0_inst) & 0xffff;
    // Sign-extend immediate
    if (immediate & 0x8000) {
      sprn->dec1_immediate = immediate | 0xffff0000;
    } else {
      sprn->dec1_immediate = immediate;
    }
    sprn->dec1_btb_is_taken = sprn->dec0_btb_is_taken;
    sprn->dec1_btb_target = sprn->dec0_btb_target;
  }

  // dec1
  if (spro->dec1_active && !dec1_stalled) {
    sprn->exec0_active = 1;
    sprn->exec0_pc = spro->dec1_pc;
    sprn->exec0_inst = spro->dec1_inst;
    sprn->exec0_opcode = spro->dec1_opcode;
    sprn->exec0_src0 = spro->dec1_src0;
    sprn->exec0_src1 = spro->dec1_src1;
    sprn->exec0_dst = spro->dec1_dst;
    sprn->exec0_immediate = spro->dec1_immediate;
    dec1_r0_final = dec1_r0_bypass_en ? dec1_r0_bypass : spro->r[spro->dec1_src0];
    sprn->exec0_alu0 = (spro->dec1_src0 == 1 || spro->dec1_opcode == LHI) ? spro->dec1_immediate : dec1_r0_final;
    if (spro->dec1_opcode == LHI) {
      dec1_r1_final = dec1_r1_bypass_en ? dec1_r1_bypass : spro->r[spro->dec1_dst];
      sprn->exec0_alu1 = dec1_r1_final;
    } else {
      dec1_r1_final = dec1_r1_bypass_en ? dec1_r1_bypass : spro->r[spro->dec1_src1];
      sprn->exec0_alu1 = (spro->dec1_src1 == 1) ? spro->dec1_immediate : dec1_r1_final;
    }
    sprn->exec0_btb_is_taken = sprn->dec1_btb_is_taken;
    sprn->exec0_btb_target = sprn->dec1_btb_target;
  }

  // exec0
  if (spro->exec0_active && !exec0_stalled) {
    sprn->exec1_active = 1;
    sprn->exec1_pc = spro->exec0_pc;
    sprn->exec1_inst = spro->exec0_inst;
    sprn->exec1_opcode = spro->exec0_opcode;
    sprn->exec1_src0 = spro->exec0_src0;
    sprn->exec1_src1 = spro->exec0_src1;
    sprn->exec1_dst = spro->exec0_dst;
    sprn->exec1_immediate = spro->exec0_immediate;

    // Establish MUX outputs for bypasses
    exec0_alu0_final = exec0_alu0_bypass_en ? exec0_alu0_bypass : spro->exec0_alu0;
    exec0_alu1_final = exec0_alu1_bypass_en ? exec0_alu1_bypass : spro->exec0_alu1;
    sprn->exec1_alu0 = exec0_alu0_final;
    sprn->exec1_alu1 = exec0_alu1_final;    
    
    switch(spro->exec0_opcode) {
    case ADD:
      sprn->exec1_aluout = exec0_alu0_final + exec0_alu1_final;
      break;
    case SUB:
      sprn->exec1_aluout = exec0_alu0_final - exec0_alu1_final;
      break;
    case LSF:
      sprn->exec1_aluout = exec0_alu0_final << exec0_alu1_final;
      break;
    case RSF:
      sprn->exec1_aluout = exec0_alu0_final >> exec0_alu1_final;
      break;
    case AND:
      sprn->exec1_aluout = exec0_alu0_final & exec0_alu1_final;
      break;
    case OR:
      sprn->exec1_aluout = exec0_alu0_final | exec0_alu1_final;
      break;
    case XOR:
      sprn->exec1_aluout = exec0_alu0_final ^ exec0_alu1_final;
      break;
    case LHI:
      sprn->exec1_aluout = (exec0_alu1_final << 16) | (exec0_alu0_final & 0xffff);
      break;
    case LD:
      llsim_mem_read(sp->sramd, exec0_alu1_final & 0xffff);
      break;
    case ST:
      break;
    case JLT:
      sprn->exec1_aluout = exec0_alu0_final < exec0_alu1_final;
      break;
    case JLE:
      sprn->exec1_aluout = exec0_alu0_final <= exec0_alu1_final;
      break;
    case JEQ:
      sprn->exec1_aluout = exec0_alu0_final == exec0_alu1_final;
      break;
    case JNE:
      sprn->exec1_aluout = exec0_alu0_final != exec0_alu1_final;
      break;
    case JIN:
      sprn->exec1_aluout = 1;
      break;
    }
    sprn->exec1_btb_is_taken = sprn->exec0_btb_is_taken;
    sprn->exec1_btb_target = sprn->exec0_btb_target;

  }
  if (exec0_stalled) {
    sprn->exec1_active = 0;
  }
	
  // exec1
  if (spro->exec1_active) {
    printInstruction(spro);
    printExecution(spro);

    sprn->mem_stall = 0;
    int btb_addr = spro->exec1_pc % BTB_SIZE;
    btb_is_taken[btb_addr] = 0;

    switch (spro->exec1_opcode) {
    case ADD:
    case SUB:
    case LSF:
    case RSF:
    case AND:
    case OR:
    case XOR:
    case LHI:
      if (spro->exec1_dst > 1) {
	sprn->r[spro->exec1_dst] = spro->exec1_aluout;
      }
      sprn->exec1_pc = spro->exec1_pc + 1;
      break;

    case LD:
      if (spro->exec1_dst > 1) {
        sprn->mem_SRAM_DO = llsim_mem_extract_dataout(sp->sramd, 31, 0);
	sprn->r[spro->exec1_dst] = sprn->mem_SRAM_DO;
        sprn->mem_stall = 1;
        sprn->mem_dst = spro->exec1_dst;
	fprintf(inst_trace_fp, ">>>> EXEC: R[%d] = MEM[%d] = %08x <<<<\n\n", spro->exec1_dst, spro->exec1_alu1, sprn->r[spro->exec1_dst]);
      }
      sprn->exec1_pc = spro->exec1_pc + 1;
      break;
	    
    case ST:
      llsim_mem_set_datain(sp->sramd, spro->exec1_alu0, 31, 0);
      llsim_mem_write(sp->sramd, spro->exec1_alu1);
      fprintf(inst_trace_fp, ">>>> EXEC: MEM[%d] = R[%d] = %08x <<<<\n\n", spro->exec1_alu1, spro->exec1_src0, spro->exec1_alu0);
      sprn->exec1_pc = spro->exec1_pc + 1;
      break;

    case JLT:
    case JLE:
    case JEQ:
    case JNE:
    case JIN:     

      //update btb       
      btb_is_taken[btb_addr] = spro->exec1_aluout;
      btb_target[btb_addr] = spro->exec1_immediate;
      
      //flush pipeline if needed
      if ((spro->exec1_aluout != spro->exec1_btb_is_taken) || (spro->exec1_btb_target != spro->exec1_immediate)) {
	sprn->fetch1_active = sprn->dec0_active = sprn->dec1_active = 
	  sprn->exec0_active = sprn->exec1_active = 0;
	sprn->exec1_pc = spro->exec1_immediate;
	sprn->fetch0_pc = spro->exec1_immediate;
	sprn->r[7] = spro->exec1_pc;
      } else {
	sprn->exec1_pc = spro->exec1_pc + 1;       
      }
      break;

    case HLT:
      fprintf(inst_trace_fp, ">>>> EXEC: HALT at PC %04x<<<<\n", spro->exec1_pc);
      fprintf(inst_trace_fp, "sim finished at pc %d, %d instructions\n", spro->exec1_pc, nr_simulated_instructions);
      llsim_stop();
      dump_sram(sp, "srami_out.txt", sp->srami);
      dump_sram(sp, "sramd_out.txt", sp->sramd);
      break;
    }
    
  }
}

static void sp_run(llsim_unit_t *unit)
{
  sp_t *sp = (sp_t *) unit->private;
  //	sp_registers_t *spro = sp->spro;
  //	sp_registers_t *sprn = sp->sprn;

  //	llsim_printf("-------------------------\n");

  if (llsim->reset) {
    sp_reset(sp);
    return;
  }

  sp->srami->read = 0;
  sp->srami->write = 0;
  sp->sramd->read = 0;
  sp->sramd->write = 0;

  sp_ctl(sp);
}

static void sp_generate_sram_memory_image(sp_t *sp, char *program_name)
{
  FILE *fp;
  int addr, i;

  fp = fopen(program_name, "r");
  if (fp == NULL) {
    printf("couldn't open file %s\n", program_name);
    exit(1);
  }
  addr = 0;
  while (addr < SP_SRAM_HEIGHT) {
    fscanf(fp, "%08x\n", &sp->memory_image[addr]);
    //              printf("addr %x: %08x\n", addr, sp->memory_image[addr]);
    addr++;
    if (feof(fp))
      break;
  }
  sp->memory_image_size = addr;

  fprintf(inst_trace_fp, "program %s loaded, %d lines\n\n", program_name, addr);

  for (i = 0; i < sp->memory_image_size; i++) {
    llsim_mem_inject(sp->srami, i, sp->memory_image[i], 31, 0);
    llsim_mem_inject(sp->sramd, i, sp->memory_image[i], 31, 0);
  }
}

void sp_init(char *program_name)
{
  llsim_unit_t *llsim_sp_unit;
  llsim_unit_registers_t *llsim_ur;
  sp_t *sp;

  llsim_printf("initializing sp unit\n");

  inst_trace_fp = fopen("inst_trace.txt", "w");
  if (inst_trace_fp == NULL) {
    printf("couldn't open file inst_trace.txt\n");
    exit(1);
  }

  cycle_trace_fp = fopen("cycle_trace.txt", "w");
  if (cycle_trace_fp == NULL) {
    printf("couldn't open file cycle_trace.txt\n");
    exit(1);
  }

  llsim_sp_unit = llsim_register_unit("sp", sp_run);
  llsim_ur = llsim_allocate_registers(llsim_sp_unit, "sp_registers", sizeof(sp_registers_t));
  sp = llsim_malloc(sizeof(sp_t));
  llsim_sp_unit->private = sp;
  sp->spro = llsim_ur->old;
  sp->sprn = llsim_ur->new;

  sp->srami = llsim_allocate_memory(llsim_sp_unit, "srami", 32, SP_SRAM_HEIGHT, 0);
  sp->sramd = llsim_allocate_memory(llsim_sp_unit, "sramd", 32, SP_SRAM_HEIGHT, 0);
  sp_generate_sram_memory_image(sp, program_name);

  sp->start = 1;
	
  // c2v_translate_end
} 
