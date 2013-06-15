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
FILE *inst_trace_fp = NULL, *cycle_trace_fp = NULL, *dma_trace_fp = NULL;

#define BTB_SIZE 8

char btb_is_taken[BTB_SIZE] = {0}; // 1 bit array
int btb_target [BTB_SIZE] = {0};   // 16 bits array

int is_pipe_stalled = 0; //1 bit

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

  //DMA
  int dma_busy;     // Flag indicates whether DMA machine is busy
  int dma_src;      // Source address for DMA
  int dma_dst;      // Destination address for DMA
  int dma_len;      // Remaining words to copy for DMA
  int dma_reg;      // DMA register as temporary storage between reads and writes
  int dma_reg2;     // DMA register for storing second value in case stall in DMA_STATE_DO_WRITE fucks up pipelining
  int dma_do_dirty; // DMA bit indicating that SRAM_DO needs to be read
  int dma_state;    // DMA state machine control

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
#define DMA 10
#define DMP 11

static char opcode_name[32][4] = {"ADD", "SUB", "LSF", "RSF", "AND", "OR", "XOR", "LHI",
				  "LD", "ST", "DMA", "DMP", "U", "U", "U", "U",
				  "JLT", "JLE", "JEQ", "JNE", "JIN", "U", "U", "U",
				  "HLT", "U", "U", "U", "U", "U", "U", "U"};


// DMA states
#define DMA_STATE_IDLE 0
#define DMA_STATE_READ_FIRST 1
#define DMA_STATE_DO_READ 2
#define DMA_STATE_DO_WRITE 3
#define DMA_STATE_WRITE_STALLED 4
#define DMA_STATE_WRITE_LAST 5
#define DMA_STATE_DO 6


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

static int is_jump_opcode(int opcode) {
  switch(opcode) {
  case DMP:
  case JLT:
  case JLE:
  case JEQ:
  case JNE:
  case JIN:       
    return 1;
  }
  return 0;
}

static void sp_ctl(sp_t *sp)
{
  sp_registers_t *spro = sp->spro;
  sp_registers_t *sprn = sp->sprn;
  int i;
  int immediate;
  int inst;
  int is_flush_needed;

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
  
  
  fprintf(cycle_trace_fp, "nr_simulated_instructions %d\n", nr_simulated_instructions);
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
  dec1_r0_bypass_en = (spro->dec1_src0 > 1) && (spro->dec1_src0 == spro->exec1_dst) && (spro->exec1_active);
  dec1_r0_bypass = spro->exec1_aluout;
  dec1_r1_cmp = (spro->dec1_opcode == LHI) ? spro->dec1_dst : spro->dec1_src1;
  dec1_r1_bypass_en = (dec1_r1_cmp > 1) && (dec1_r1_cmp == spro->exec1_dst) && (spro->exec1_active);
  dec1_r1_bypass = spro->exec1_aluout;
  
  exec0_alu1_cmp = (spro->exec0_opcode == LHI) ? spro->exec0_dst : spro->exec0_src1;
  exec0_exec1_to_alu0_bypass = (spro->exec0_src0 > 1) && (spro->exec0_src0 == spro->exec1_dst) && (spro->exec1_opcode != ST) && (spro->exec1_active);
  exec0_exec1_to_alu1_bypass = (exec0_alu1_cmp > 1) && (exec0_alu1_cmp == spro->exec1_dst) && (spro->exec1_opcode != ST) && (spro->exec1_active);
  exec0_mem_to_alu0_bypass = (spro->exec0_src0 > 1) && spro->mem_stall && (spro->exec0_src0 == spro->mem_dst);
  exec0_mem_to_alu1_bypass = (exec0_alu1_cmp > 1) && spro->mem_stall && (exec0_alu1_cmp == spro->mem_dst);
  exec0_alu0_bypass_en = (exec0_exec1_to_alu0_bypass || exec0_mem_to_alu0_bypass);
  exec0_alu1_bypass_en = (exec0_exec1_to_alu1_bypass || exec0_mem_to_alu1_bypass);  
  exec0_alu0_bypass = (exec0_exec1_to_alu0_bypass && spro->exec1_active) ? spro->exec1_aluout : spro->mem_SRAM_DO;
  exec0_alu1_bypass = (exec0_exec1_to_alu1_bypass && spro->exec1_active) ? spro->exec1_aluout : spro->mem_SRAM_DO;

  is_pipe_stalled = (spro->exec1_active && 
		   ((spro->exec0_opcode == LD && spro->exec1_opcode == ST) || //structural
		    (spro->exec1_opcode == LD && ((spro->exec1_dst == spro->exec0_src0) || (spro->exec1_dst == exec0_alu1_cmp))))); //data Read after LD
		    
  fprintf(cycle_trace_fp, "is_pipe_stalled %d\n", is_pipe_stalled);

  fprintf(cycle_trace_fp, "exec0_exec1_to_alu0_bypass %d ",exec0_exec1_to_alu0_bypass); 
  fprintf(cycle_trace_fp, "exec0_exec1_to_alu1_bypass %d ",exec0_exec1_to_alu1_bypass);
  fprintf(cycle_trace_fp, "exec0_mem_to_alu0_bypass %d ",exec0_mem_to_alu0_bypass); 
  fprintf(cycle_trace_fp, "exec0_mem_to_alu1_bypass %d\n",exec0_mem_to_alu1_bypass);

  fprintf(cycle_trace_fp, "\n");

  //  Patch for debuggin when encounter infinite loop. Uncomment out when needed.
  //if (spro->cycle_counter > 1000) {
  //  llsim_stop();
  //}

  // fetch0
  if (spro->fetch0_active && !is_pipe_stalled) {
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
    if (is_pipe_stalled) {
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
  } else {
    sprn->dec0_active = 0;
  }
	
  // dec0
  if (spro->dec0_active && !is_pipe_stalled) {
    int opcode = (spro->dec0_inst >> 25) & 0x1F;
    sprn->dec1_active = 1;
    sprn->dec1_pc = spro->dec0_pc;
    sprn->dec1_inst = spro->dec0_inst;
    sprn->dec1_opcode = opcode;
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
    
    //if predicted jump taken, but opcode is not jump, flush pipe
    if (!is_jump_opcode(opcode) && spro->dec0_btb_is_taken) {
      sprn->fetch1_active = sprn->dec0_active = 0;
      sprn->fetch0_pc = spro->dec0_pc + 1;
    }

    sprn->dec1_btb_is_taken = spro->dec0_btb_is_taken;
    sprn->dec1_btb_target = spro->dec0_btb_target;
  }
  if (!(spro->dec0_active)) {
    sprn->dec1_active = 0;
  }

  // dec1
  if (spro->dec1_active && !is_pipe_stalled) {
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
    sprn->exec0_btb_is_taken = spro->dec1_btb_is_taken;
    sprn->exec0_btb_target = spro->dec1_btb_target;
  }
  if (!(spro->dec1_active)) {
    sprn->exec0_active = 0;
  }

  // exec0
  if (spro->exec0_active && !is_pipe_stalled) {
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
    case DMA:
      sprn->exec1_aluout = spro->exec0_alu0;
      break;
    case DMP:
      sprn->exec1_aluout = !(spro->exec0_alu0);
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
    sprn->exec1_btb_is_taken = spro->exec0_btb_is_taken;
    sprn->exec1_btb_target = spro->exec0_btb_target;

  }
  if (is_pipe_stalled) {
    sprn->exec1_active = 0;
  }
  if (!(spro->exec0_active)) {
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
      break;

    case LD:
      if (spro->exec1_dst > 1) {
        sprn->mem_SRAM_DO = llsim_mem_extract_dataout(sp->sramd, 31, 0);
	sprn->r[spro->exec1_dst] = sprn->mem_SRAM_DO;
        sprn->mem_stall = 1;
        sprn->mem_dst = spro->exec1_dst;
	fprintf(inst_trace_fp, ">>>> EXEC: R[%d] = MEM[%d] = %08x <<<<\n\n", spro->exec1_dst, spro->exec1_alu1, sprn->r[spro->exec1_dst]);
      }
      break;
	    
    case ST:
      llsim_mem_set_datain(sp->sramd, spro->exec1_alu0, 31, 0);
      llsim_mem_write(sp->sramd, spro->exec1_alu1);
      fprintf(inst_trace_fp, ">>>> EXEC: MEM[%d] = R[%d] = %08x <<<<\n\n", spro->exec1_alu1, spro->exec1_src0, spro->exec1_alu0);
      break;
    
    case DMA:
      if (spro->dma_busy) {
        // Suppress operation if DMA machine is already busy
        break;
      }
      sprn->dma_busy = 1;
      sprn->dma_src = spro->exec1_alu0;
      sprn->dma_dst = spro->exec1_aluout;
      sprn->dma_len = spro->exec1_alu1;
      break;

    case DMP:
    case JLT:
    case JLE:
    case JEQ:
    case JNE:
    case JIN:

      // Execute branch
      if (spro->exec1_aluout) {
	sprn->r[7] = spro->exec1_pc;
      }

      //update btb
      btb_is_taken[btb_addr] = spro->exec1_aluout;
      btb_target[btb_addr] = spro->exec1_immediate;
      
      is_flush_needed = ((spro->exec1_aluout != spro->exec1_btb_is_taken) || 
			     (spro->exec1_aluout && (spro->exec1_btb_target != spro->exec1_immediate)));
      
      if (is_flush_needed) {
	sprn->fetch1_active = sprn->dec0_active = sprn->dec1_active = 
	  sprn->exec0_active = sprn->exec1_active = 0;
	sprn->fetch0_pc = (spro->exec1_aluout) ? spro->exec1_immediate : spro->exec1_pc + 1;
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

static int is_dma_hazard(sp_registers_t *sp) {
  return ((!is_pipe_stalled && sp->exec0_active && sp->exec0_opcode == LD) ||
	  (sp->exec1_active && sp->exec1_opcode == ST));
}

static void dma_ctl(sp_t *sp)
{
  sp_registers_t *spro = sp->spro;
  sp_registers_t *sprn = sp->sprn;

  fprintf(dma_trace_fp, "cycle %d\n", llsim->clock);
  fprintf(dma_trace_fp, "dma_src %08x\n", spro->dma_src);
  fprintf(dma_trace_fp, "dma_dst %08x\n", spro->dma_dst);
  fprintf(dma_trace_fp, "dma_len %08x\n", spro->dma_len);
  fprintf(dma_trace_fp, "dma_reg %08x\n", spro->dma_reg);
  fprintf(dma_trace_fp, "dma_reg2 %08x\n", spro->dma_reg2);
  fprintf(dma_trace_fp, "dma_do_dirty %08x\n", spro->dma_do_dirty);
  fprintf(dma_trace_fp, "dma_state %08x\n", spro->dma_state);
  
  sprn->cycle_counter = spro->cycle_counter + 1;
  
  switch (spro->dma_state) {
    
  case DMA_STATE_IDLE:
    // Idle state for DMA machine
    if (spro->dma_busy && (spro->dma_len > 0)) {
      // DMA operation requested, start copy
      sprn->dma_state = DMA_STATE_READ_FIRST;
    } else {
      spro->dma_busy = 0;
    }
    break;
  
  case DMA_STATE_READ_FIRST:
    // Check for hazards
    if (is_dma_hazard(spro)) {
      // Stall
      sprn->dma_state = spro->dma_state;
      fprintf(dma_trace_fp, "Stalled in DMA_STATE_READ_FIRST\n");
      break;
    }
    // Read from memory
    llsim_mem_read(sp->sramd, spro->dma_src & 0xffff);
    sprn->dma_src = spro->dma_src + 1;
    sprn->dma_do_dirty = 1;

    //edge case: copy single word
    if(spro->dma_len == 1) {
      sprn->dma_state = DMA_STATE_DO;
    } else {
      sprn->dma_state = DMA_STATE_DO_READ;
    }
    break;
  
  case DMA_STATE_DO_READ:
    
    //copy data from memory bus to register
    if (spro->dma_do_dirty) {
      sprn->dma_reg = llsim_mem_extract_dataout(sp->sramd, 31, 0);
    }
    sprn->dma_do_dirty = 0;
    
    // Check for hazards
    if (is_dma_hazard(spro)) {
      // Stall
      sprn->dma_state = spro->dma_state;
      fprintf(dma_trace_fp, "Stalled in DMA_STATE_DO_READ\n");
      break;
    }
        
    llsim_mem_read(sp->sramd, spro->dma_src & 0xffff);
    sprn->dma_src = spro->dma_src + 1;
    sprn->dma_do_dirty = 1;
    sprn->dma_state = DMA_STATE_DO_WRITE;
    break;
  
  case DMA_STATE_DO_WRITE:

    // Check for hazards
    if (is_dma_hazard(spro)) {
      // Stall
      sprn->dma_state = DMA_STATE_WRITE_STALLED;
      sprn->dma_reg2 = llsim_mem_extract_dataout(sp->sramd, 31, 0);
      sprn->dma_do_dirty = 0;
      fprintf(dma_trace_fp, "Stalled in DMA_STATE_DO_WRITE\n");
      break;
    }

    //copy data from memory bus to register
    if (spro->dma_do_dirty) {
      sprn->dma_reg = llsim_mem_extract_dataout(sp->sramd, 31, 0);
    }
    sprn->dma_do_dirty = 0;
    
    // Write to memory
    llsim_mem_set_datain(sp->sramd, spro->dma_reg, 31, 0);
    llsim_mem_write(sp->sramd, spro->dma_dst & 0xffff);
    
    sprn->dma_dst = spro->dma_dst + 1;
    sprn->dma_len = spro->dma_len - 1;
    if (spro->dma_len == 2) {
      sprn->dma_state = DMA_STATE_WRITE_LAST;
    } else {
      //Next iteration
      sprn->dma_state = DMA_STATE_DO_READ;
    }
    break;

  case DMA_STATE_WRITE_STALLED:
    // Check for hazards
    if (is_dma_hazard(spro)) {
      // Stall
      sprn->dma_state = spro->dma_state;
      fprintf(dma_trace_fp, "Stalled in DMA_STATE_WRITE_LAST\n");
      break;
    }
    // Write to memory
    llsim_mem_set_datain(sp->sramd, spro->dma_reg, 31, 0);
    llsim_mem_write(sp->sramd, spro->dma_dst & 0xffff);
    
    sprn->dma_dst = spro->dma_dst + 1;
    sprn->dma_len = spro->dma_len - 1;

    sprn->dma_reg = spro->dma_reg2;
    if (spro->dma_len == 2) {
      sprn->dma_state = DMA_STATE_WRITE_LAST;
    } else {
      //Next iteration
      sprn->dma_state = DMA_STATE_DO_READ;
    }
    break;

  case DMA_STATE_WRITE_LAST:
    // Check for hazards
    if (is_dma_hazard(spro)) {
      // Stall
      sprn->dma_state = spro->dma_state;
      fprintf(dma_trace_fp, "Stalled in DMA_STATE_WRITE_LAST\n");
      break;
    }

    //Write last word
    llsim_mem_set_datain(sp->sramd, spro->dma_reg, 31, 0);
    llsim_mem_write(sp->sramd, spro->dma_dst & 0xffff);
    
    sprn->dma_busy = 0;
    sprn->dma_state = DMA_STATE_IDLE;
    break;
  
  case DMA_STATE_DO:
    sprn->dma_reg = llsim_mem_extract_dataout(sp->sramd, 31, 0);
    sprn->dma_state = DMA_STATE_WRITE_LAST;
    break;
  }

  fprintf(dma_trace_fp, "\n");
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
  dma_ctl(sp);
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

  dma_trace_fp = fopen("dma_trace.txt", "w");
  if (dma_trace_fp == NULL) {
    printf("couldn't open file dma_trace.txt\n");
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
