#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>

#include "llsim.h"

#define sp_printf(a...)        					\
	do {							\
		llsim_printf("sp: clock %d: ", llsim->clock);	\
		llsim_printf(a);				\
	} while (0)

int nr_simulated_instructions = 0;
FILE *inst_trace_fp = NULL, *cycle_trace_fp = NULL, *dma_trace_fp = NULL;

typedef struct sp_registers_s {
	// 6 32 bit registers (r[0], r[1] don't exist)
	int r[8];

	// 16 bit program counter
	int pc;

	// 32 bit instruction
	int inst;

	// 5 bit opcode
	int opcode;

	// 3 bit destination register index
	int dst;

	// 3 bit source #0 register index
	int src0;

	// 3 bit source #1 register index
	int src1;

	// 32 bit alu #0 operand
	int alu0;

	// 32 bit alu #1 operand
	int alu1;

	// 32 bit alu output
	int aluout;

	// 32 bit immediate field (original 16 bit sign extended)
	int immediate;

	// 32 bit cycle counter
	int cycle_counter;

	// 3 bit control state machine state register
	int ctl_state;

	// control states
	#define CTL_STATE_IDLE		0
	#define CTL_STATE_FETCH0	1
	#define CTL_STATE_FETCH1	2
	#define CTL_STATE_DEC0		3
	#define CTL_STATE_DEC1		4
	#define CTL_STATE_EXEC0		5
	#define CTL_STATE_EXEC1		6

  // Flag indicates whether DMA machine is busy
  int dma_busy;

  // Source address for DMA
  int dma_src;

  // Destination address for DMA
  int dma_dst;

  // Remaining words to copy for DMA
  int dma_len;

  // DMA register as temporary storage between reads and writes
  int dma_reg;

  // DMA register for storing second value in case stall in DMA_STATE_DO_WRITE fucks up pipelining
  int dma_reg2;

  // DMA bit indicating that SRAM_DO needs to be read
  int dma_do_dirty;

  // DMA state machine control
  int dma_state;

  // DMA states
  #define DMA_STATE_IDLE          0
  #define DMA_STATE_READ_FIRST    1
  #define DMA_STATE_DO_READ       2
  #define DMA_STATE_DO_WRITE      3
  #define DMA_STATE_WRITE_STALLED 4
  #define DMA_STATE_WRITE_LAST    5
  #define DMA_STATE_DO            6

} sp_registers_t;

/*
 * Master structure
 */
typedef struct sp_s {
	// local sram
#define SP_SRAM_HEIGHT	64 * 1024
	llsim_memory_t *sram;

	unsigned int memory_image[SP_SRAM_HEIGHT];
	int memory_image_size;

	sp_registers_t *spro, *sprn;
	
	int start;
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
#define LD  8
#define ST  9
#define DMA 10
#define DMP 11
#define JLT 16
#define JLE 17
#define JEQ 18
#define JNE 19
#define JIN 20
#define HLT 24

static char opcode_name[32][4] = {"ADD", "SUB", "LSF", "RSF", "AND", "OR", "XOR", "LHI",
				 "LD", "ST", "DMA", "DMP", "U", "U", "U", "U",
				 "JLT", "JLE", "JEQ", "JNE", "JIN", "U", "U", "U",
				 "HLT", "U", "U", "U", "U", "U", "U", "U"};

static void dump_sram(sp_t *sp)
{
	FILE *fp;
	int i;

	fp = fopen("sram_out.txt", "w");
	if (fp == NULL) {
                printf("couldn't open file sram_out.txt\n");
                exit(1);
	}
  for (i = 0; i < SP_SRAM_HEIGHT; i++)
		fprintf(fp, "%08x\n", llsim_mem_extract(sp->sram, i, 31, 0));
	fclose(fp);
}

static void printInstruction(sp_registers_t *sp) {
  fprintf(inst_trace_fp, "--- instruction %d (%04x) @ PC %d (%04x) -----------------------------------------------------------\n",
	  nr_simulated_instructions, nr_simulated_instructions, sp->pc, sp->pc);
  
  fprintf(inst_trace_fp, "pc = %04x, inst = %08x, opcode = %d (%s), dst = %d, src0 = %d, src1 = %d, immediate = %08x\n",
	  sp->pc, sp->inst, sp->opcode, opcode_name[sp->opcode], sp->dst, sp->src0, sp->src1, sp->immediate);
  
  fprintf(inst_trace_fp, "r[0] = 00000000 r[1] = %08x r[2] = %08x r[3] = %08x \n", sp->immediate, sp->r[2], sp->r[3]);
  fprintf(inst_trace_fp, "r[4] = %08x r[5] = %08x r[6] = %08x r[7] = %08x \n\n", sp->r[4], sp->r[5], sp->r[6], sp->r[7]);
}



static void sp_ctl(sp_t *sp)
{
  sp_registers_t *spro = sp->spro;
  sp_registers_t *sprn = sp->sprn;
  int i;

  // sp_ctl

  fprintf(cycle_trace_fp, "cycle %d\n", spro->cycle_counter);
  for (i = 2; i <= 7; i++)
    fprintf(cycle_trace_fp, "r%d %08x\n", i, spro->r[i]);
  fprintf(cycle_trace_fp, "pc %08x\n", spro->pc);
  fprintf(cycle_trace_fp, "inst %08x\n", spro->inst);
  fprintf(cycle_trace_fp, "opcode %08x\n", spro->opcode);
  fprintf(cycle_trace_fp, "dst %08x\n", spro->dst);
  fprintf(cycle_trace_fp, "src0 %08x\n", spro->src0);
  fprintf(cycle_trace_fp, "src1 %08x\n", spro->src1);
  fprintf(cycle_trace_fp, "immediate %08x\n", spro->immediate);
  fprintf(cycle_trace_fp, "alu0 %08x\n", spro->alu0);
  fprintf(cycle_trace_fp, "alu1 %08x\n", spro->alu1);
  fprintf(cycle_trace_fp, "aluout %08x\n", spro->aluout);
  fprintf(cycle_trace_fp, "cycle_counter %08x\n", spro->cycle_counter);
  fprintf(cycle_trace_fp, "ctl_state %08x\n\n", spro->ctl_state);

  sprn->cycle_counter = spro->cycle_counter + 1;

  switch (spro->ctl_state) {
  case CTL_STATE_IDLE:
    sprn->pc = 0;
    if (sp->start)
      sprn->ctl_state = CTL_STATE_FETCH0;
    break;

  case CTL_STATE_FETCH0:
    llsim_mem_read(sp->sram, spro->pc);
    sprn->ctl_state = CTL_STATE_FETCH1;		
    break;

  case CTL_STATE_FETCH1:
    sprn->inst = llsim_mem_extract_dataout(sp->sram, 31, 0);
    sprn->ctl_state = CTL_STATE_DEC0;		
    break;

  case CTL_STATE_DEC0:
    sprn->opcode = (spro->inst >> 25) & 0x1F;
    sprn->dst = (spro->inst >> 22) & 0x7;
    sprn->src0 = (spro->inst >> 19) & 0x7;
    sprn->src1 = (spro->inst >> 16) & 0x7;
    sprn->immediate = (spro->inst) & 0xffff;	
    // Sign-extend immediate
    if (sprn->immediate & 0x8000) {
      sprn->immediate |= 0xffff0000;
    }
    sprn->ctl_state = CTL_STATE_DEC1;
    break;

  case CTL_STATE_DEC1:
    sprn->alu0 = (spro->src0 == 1 || spro->opcode == LHI) ? spro->immediate : spro->r[spro->src0];
    sprn->alu1 = (spro->src1 == 1) ? spro->immediate : spro->r[spro->src1];
    sprn->ctl_state = CTL_STATE_EXEC0;
    printInstruction(sprn);
    break;

  case CTL_STATE_EXEC0:
    switch(spro->opcode) {
    case ADD:
      sprn->aluout = spro->alu0 + spro->alu1;
      fprintf(inst_trace_fp, ">>>> EXEC: R[%d] = %d ADD %d <<<<\n\n", spro->dst, spro->alu0, spro->alu1);
      break;
    case SUB:
      sprn->aluout = spro->alu0 - spro->alu1;
      fprintf(inst_trace_fp, ">>>> EXEC: R[%d] = %d SUB %d <<<<\n\n", spro->dst, spro->alu0, spro->alu1);
      break;
    case LSF:
      sprn->aluout = spro->alu0 << spro->alu1;
      fprintf(inst_trace_fp, ">>>> EXEC: R[%d] = %d LSF %d <<<<\n\n", spro->dst, spro->alu0, spro->alu1);
      break;
    case RSF:
      sprn->aluout = spro->alu0 >> spro->alu1;
      fprintf(inst_trace_fp, ">>>> EXEC: R[%d] = %d RSF %d <<<<\n\n", spro->dst, spro->alu0, spro->alu1);
      break;
    case AND:
      sprn->aluout = spro->alu0 & spro->alu1;
      fprintf(inst_trace_fp, ">>>> EXEC: R[%d] = %d AND %d <<<<\n\n", spro->dst, spro->alu0, spro->alu1);
      break;
    case OR:
      sprn->aluout = spro->alu0 | spro->alu1;
      fprintf(inst_trace_fp, ">>>> EXEC: R[%d] = %d OR %d <<<<\n\n", spro->dst, spro->alu0, spro->alu1);
      break;
    case XOR:
      sprn->aluout = spro->alu0 ^ spro->alu1;
      fprintf(inst_trace_fp,">>>> EXEC: R[%d] = %d XOR %d <<<<\n\n", spro->dst, spro->alu0, spro->alu1);
      break;
    case LHI:
      sprn->aluout = (spro->alu0 << 16) | (sprn->aluout & 0xffff);
      fprintf(inst_trace_fp,">>>> EXEC: R[%d][31:16] = %d <<<<\n\n", spro->dst, spro->alu0);
      break;
    case LD:
      llsim_mem_read(sp->sram, spro->alu1 & 0xffff);
      break;
    case ST:      
      break;
    case DMA:
      sprn->aluout = spro->r[spro->dst];
      break;
    case DMP:
      sprn->aluout = !(spro->dma_busy);
      break;
    case JLT:
      sprn->aluout = spro->alu0 < spro->alu1;
      fprintf(inst_trace_fp, ">>>> EXEC: JLT %d, %d, %d <<<<\n\n", spro->alu0, spro->alu1, (sprn->aluout ? spro->immediate : spro->pc + 1));
      break;
    case JLE:
      sprn->aluout = spro->alu0 <= spro->alu1;
      fprintf(inst_trace_fp, ">>>> EXEC: JLE %d, %d, %d <<<<\n\n", spro->alu0, spro->alu1, (sprn->aluout ? spro->immediate : spro->pc + 1));
      break;
    case JEQ:
      sprn->aluout = spro->alu0 == spro->alu1;
      fprintf(inst_trace_fp, ">>>> EXEC: JEQ %d, %d, %d <<<<\n\n", spro->alu0, spro->alu1, (sprn->aluout ? spro->immediate : spro->pc + 1));
      break;
    case JNE:
      sprn->aluout = spro->alu0 != spro->alu1;
      fprintf(inst_trace_fp, ">>>> EXEC: JNE %d, %d, %d <<<<\n\n", spro->alu0, spro->alu1, (sprn->aluout ? spro->immediate : spro->pc + 1));
      break;
    case JIN:
      sprn->aluout = 1;
      fprintf(inst_trace_fp, ">>>> EXEC: JIN R[%d] = %08x <<<<\n\n", spro->src0, spro->alu0);
      break;
    }

    sprn->ctl_state = CTL_STATE_EXEC1;
    break;

  case CTL_STATE_EXEC1:
    nr_simulated_instructions += 1;
    switch (spro->opcode) {
    case ADD:
    case SUB:
    case LSF:
    case RSF:
    case AND:
    case OR: 
    case XOR:
    case LHI:
      if (spro->dst > 1) {
      	sprn->r[spro->dst] = spro->aluout;
      }
      sprn->pc = spro->pc + 1;
      break;
    
    case LD:
      if (spro->dst > 1) {
      	sprn->r[spro->dst] = llsim_mem_extract_dataout(sp->sram, 31, 0);    
      	fprintf(inst_trace_fp, ">>>> EXEC: R[%d] = MEM[%d] = %08x <<<<\n\n", spro->dst, spro->alu1, sprn->r[spro->dst]);
      }
      sprn->pc = spro->pc + 1;
      break;
    
    case ST:
      llsim_mem_set_datain(sp->sram, spro->alu0, 31, 0);
      llsim_mem_write(sp->sram, spro->alu1);
      fprintf(inst_trace_fp, ">>>> EXEC: MEM[%d] = R[%d] = %08x <<<<\n\n", spro->alu1, spro->src0, spro->alu0);
      sprn->pc = spro->pc + 1;
      break;

    case DMA:
      if (spro->dma_busy) {
        // Suppress operation if DMA machine is already busy
        break;
      }
      sprn->dma_busy = 1;
      sprn->dma_src = spro->alu0;
      sprn->dma_dst = spro->aluout;
      sprn->dma_len = spro->alu1;
      sprn->pc = spro->pc + 1;
      break;
    
    case DMP:
    case JLT:
    case JLE:
    case JEQ:
    case JNE:
    case JIN:
      sprn->pc = spro->aluout ? spro->immediate : spro->pc + 1;
      sprn->r[7] = spro->aluout ? spro->pc : spro->r[7];      
      break;
  
    case HLT:
      // Intentionally print one line break
      fprintf(inst_trace_fp, ">>>> EXEC: HALT at PC %04x<<<<\n", spro->pc);
      fprintf(inst_trace_fp, "sim finished at pc %d, %d instructions\n", spro->pc, nr_simulated_instructions);
      dump_sram(sp);
      llsim_stop();
      break;
    }

    sprn->ctl_state = (spro->inst == HLT) ? CTL_STATE_IDLE : CTL_STATE_FETCH0;
    break;
  }
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
    if (spro->ctl_state == CTL_STATE_FETCH0 ||
        (spro->ctl_state == CTL_STATE_EXEC0 && spro->opcode == LD) ||
        (spro->ctl_state == CTL_STATE_EXEC1 && spro->opcode == ST)) {
      // Stall
      sprn->dma_state = spro->dma_state;
      fprintf(dma_trace_fp, "Stalled in DMA_STATE_READ_FIRST\n");
      break;
    }
    // Read from memory
    llsim_mem_read(sp->sram, spro->dma_src & 0xffff);
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
      sprn->dma_reg = llsim_mem_extract_dataout(sp->sram, 31, 0);
    }
    sprn->dma_do_dirty = 0;
    
    // Check for hazards
    if (spro->ctl_state == CTL_STATE_FETCH0 ||
        (spro->ctl_state == CTL_STATE_EXEC0 && spro->opcode == LD) ||
        (spro->ctl_state == CTL_STATE_EXEC1 && spro->opcode == ST)) {
      // Stall
      sprn->dma_state = spro->dma_state;
      fprintf(dma_trace_fp, "Stalled in DMA_STATE_DO_READ\n");
      break;
    }
        
    llsim_mem_read(sp->sram, spro->dma_src & 0xffff);
    sprn->dma_src = spro->dma_src + 1;
    sprn->dma_do_dirty = 1;
    sprn->dma_state = DMA_STATE_DO_WRITE;
    break;
  
  case DMA_STATE_DO_WRITE:

    // Check for hazards
    if (spro->ctl_state == CTL_STATE_FETCH0 ||
        (spro->ctl_state == CTL_STATE_EXEC0 && spro->opcode == LD) ||
        (spro->ctl_state == CTL_STATE_EXEC1 && spro->opcode == ST)) {
      // Stall      
      sprn->dma_state = DMA_STATE_WRITE_STALLED;
      sprn->dma_reg2 = llsim_mem_extract_dataout(sp->sram, 31, 0);
      sprn->dma_do_dirty = 0;
      fprintf(dma_trace_fp, "Stalled in DMA_STATE_DO_WRITE\n");
      break;
    }

    //copy data from memory bus to register
    if (spro->dma_do_dirty) {
      sprn->dma_reg = llsim_mem_extract_dataout(sp->sram, 31, 0);
    }
    sprn->dma_do_dirty = 0;
    
    // Write to memory
    llsim_mem_set_datain(sp->sram, spro->dma_reg, 31, 0);
    llsim_mem_write(sp->sram, spro->dma_dst & 0xffff);
    
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
    if (spro->ctl_state == CTL_STATE_FETCH0 ||
        (spro->ctl_state == CTL_STATE_EXEC0 && spro->opcode == LD) ||
        (spro->ctl_state == CTL_STATE_EXEC1 && spro->opcode == ST)) {
      // Stall      
      sprn->dma_state = spro->dma_state;
      fprintf(dma_trace_fp, "Stalled in DMA_STATE_WRITE_LAST\n");
      break;
    }
    // Write to memory
    llsim_mem_set_datain(sp->sram, spro->dma_reg, 31, 0);
    llsim_mem_write(sp->sram, spro->dma_dst & 0xffff);
    
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
    if (spro->ctl_state == CTL_STATE_FETCH0 ||
        (spro->ctl_state == CTL_STATE_EXEC0 && spro->opcode == LD) ||
        (spro->ctl_state == CTL_STATE_EXEC1 && spro->opcode == ST)) {
      // Stall      
      sprn->dma_state = spro->dma_state;
      fprintf(dma_trace_fp, "Stalled in DMA_STATE_WRITE_LAST\n");
      break;
    }

    //Write last word
    llsim_mem_set_datain(sp->sram, spro->dma_reg, 31, 0);
    llsim_mem_write(sp->sram, spro->dma_dst & 0xffff);
    
    sprn->dma_busy = 0;
    sprn->dma_state = DMA_STATE_IDLE;
    break;
  
  case DMA_STATE_DO:
    sprn->dma_reg = llsim_mem_extract_dataout(sp->sram, 31, 0);
    sprn->dma_state = DMA_STATE_WRITE_LAST;
    break;
  }

  fprintf(dma_trace_fp, "\n");
}

static void sp_run(llsim_unit_t *unit)
{
	sp_t *sp = (sp_t *) unit->private;

	if (llsim->reset) {
		sp_reset(sp);
		return;
	}

	sp->sram->read = 0;
	sp->sram->write = 0;

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
                addr++;
                if (feof(fp))
                        break;
        }
	sp->memory_image_size = addr;

        fprintf(inst_trace_fp, "program %s loaded, %d lines\n\n", program_name, addr);

	for (i = 0; i < sp->memory_image_size; i++)
		llsim_mem_inject(sp->sram, i, sp->memory_image[i], 31, 0);
}

static void sp_register_all_registers(sp_t *sp)
{
	sp_registers_t *spro = sp->spro, *sprn = sp->sprn;

	// registers
	llsim_register_register("sp", "r_0", 32, 0, &spro->r[0], &sprn->r[0]);
	llsim_register_register("sp", "r_1", 32, 0, &spro->r[1], &sprn->r[1]);
	llsim_register_register("sp", "r_2", 32, 0, &spro->r[2], &sprn->r[2]);
	llsim_register_register("sp", "r_3", 32, 0, &spro->r[3], &sprn->r[3]);
	llsim_register_register("sp", "r_4", 32, 0, &spro->r[4], &sprn->r[4]);
	llsim_register_register("sp", "r_5", 32, 0, &spro->r[5], &sprn->r[5]);
	llsim_register_register("sp", "r_6", 32, 0, &spro->r[6], &sprn->r[6]);
	llsim_register_register("sp", "r_7", 32, 0, &spro->r[7], &sprn->r[7]);

	llsim_register_register("sp", "pc", 16, 0, &spro->pc, &sprn->pc);
	llsim_register_register("sp", "inst", 32, 0, &spro->inst, &sprn->inst);
	llsim_register_register("sp", "opcode", 5, 0, &spro->opcode, &sprn->opcode);
	llsim_register_register("sp", "dst", 3, 0, &spro->dst, &sprn->dst);
	llsim_register_register("sp", "src0", 3, 0, &spro->src0, &sprn->src0);
	llsim_register_register("sp", "src1", 3, 0, &spro->src1, &sprn->src1);
	llsim_register_register("sp", "alu0", 32, 0, &spro->alu0, &sprn->alu0);
	llsim_register_register("sp", "alu1", 32, 0, &spro->alu1, &sprn->alu1);
	llsim_register_register("sp", "aluout", 32, 0, &spro->aluout, &sprn->aluout);
	llsim_register_register("sp", "immediate", 32, 0, &spro->immediate, &sprn->immediate);
	llsim_register_register("sp", "cycle_counter", 32, 0, &spro->cycle_counter, &sprn->cycle_counter);
	llsim_register_register("sp", "ctl_state", 3, 0, &spro->ctl_state, &sprn->ctl_state);
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

	sp->sram = llsim_allocate_memory(llsim_sp_unit, "sram", 32, SP_SRAM_HEIGHT, 0);
	sp_generate_sram_memory_image(sp, program_name);

	sp->start = 1;

	sp_register_all_registers(sp);
}
