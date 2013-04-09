/*
 * SP ASM: Simple Processor assembler
 *
 * usage: asm
 */
#include <stdio.h>
#include <stdlib.h>

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
#define DMA 10
#define DMP 11
#define JLT 16
#define JLE 17
#define JEQ 18
#define JNE 19
#define JIN 20
#define HLT 24

#define MEM_SIZE_BITS  (16)
#define MEM_SIZE	(1 << MEM_SIZE_BITS)
#define MEM_MASK	(MEM_SIZE - 1)
unsigned int mem[MEM_SIZE];

int pc = 0;

static void asm_cmd(int opcode, int dst, int src0, int src1, int immediate)
{
	int inst;

	inst = ((opcode & 0x1f) << 25) | ((dst & 7) << 22) | ((src0 & 7) << 19) | ((src1 & 7) << 16) | (immediate & 0xffff);
	mem[pc++] = inst;
}

static void assemble_program(char *program_name)
{
	FILE *fp;
	int addr, i, last_addr;

	for (addr = 0; addr < MEM_SIZE; addr++)
		mem[addr] = 0;

	pc = 0;

	/*
	 * Program starts here
	 */
	asm_cmd(ADD, 2, 1, 0, 101); // 0: R2 = 101
	asm_cmd(ADD, 3, 1, 0, 201); // 1: R3 = 201
	asm_cmd(ADD, 4, 1, 0, 301); // 2: R4 = 301 
	asm_cmd(ADD, 6, 1, 0, 10);  // 3: R6 = 10; 
	
	asm_cmd(DMA, 3, 2, 1, 100); // 4: DMA from 101 to 201, len=100
	
	asm_cmd(LD,  5, 0, 2, 0);   // 5: R5 = MEM[R2]
	asm_cmd(ST,  0, 5, 4, 0);   // 6: MEM[R4] = R5
	asm_cmd(ADD, 2, 2, 1, 1);   // 7: R2 += 1
	asm_cmd(ADD, 4, 4, 1, 1);   // 8: R4 += 1 
	asm_cmd(SUB, 6, 6, 1, 1);   // 9: R6 -= 1
	asm_cmd(JLT, 0, 0, 6, 5);   // 10: if (0 < R6), GOTO 5
	
	asm_cmd(DMP, 0, 0, 0, 13);  // 11: DMP, should jump to line 5
	asm_cmd(JIN, 0, 0, 0, 11);  // 12: GOTO DMP
	asm_cmd(HLT, 0, 0, 0, 0);   // 13: HLT
	
	//MEM[101] = 1 ... MEM[200] = 100
	for (i = 1; i <= 100; i++) {
	  mem[100+i] = i;
	};

	last_addr = 350;

	fp = fopen(program_name, "w");
	if (fp == NULL) {
		printf("couldn't open file %s\n", program_name);
		exit(1);
	}
	addr = 0;
	while (addr < last_addr) {
		fprintf(fp, "%08x\n", mem[addr]);
		addr++;
	}
}


int main(int argc, char *argv[])
{
	printf("SP assembler\n");
	if (argc != 2)
		printf("usage: asm program_name\n");
	assemble_program(argv[1]);
	return 0;
}
