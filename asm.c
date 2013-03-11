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
#define JLT 16
#define JLE 17
#define JEQ 18
#define JNE 19
#define JIN 20
#define HLT 24

#define MEM_SIZE_BITS	(16)
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
	 * Original program starts here
	 */
	/*
	asm_cmd(ADD, 2, 1, 0, 15); // 0: R2 = 15
	asm_cmd(ADD, 5, 1, 0, 20); // 1: R5 = 20
	asm_cmd(ADD, 3, 1, 0, 0);  // 2: R3 = 0
	asm_cmd(LD,  4, 0, 2, 0);  // 3: R4 = MEM[R2]
	asm_cmd(ADD, 3, 3, 4, 0);  // 4: R3 += R4
	asm_cmd(ADD, 2, 2, 1, 1);  // 5: R2 += 1
	asm_cmd(JLT, 0, 2, 5, 3);  // 6: if R2 < R5 goto 3
	asm_cmd(ST,  0, 3, 1, 15); // 7: MEM[15] = R3
	asm_cmd(HLT, 0, 0, 0, 0);  // 8: Halt
	*/
	asm_cmd(LD,  2, 0, 1, 1000);	// 0:  Load multiplicand
	asm_cmd(LD,  3, 0, 1, 1001);	// 1:  Load multiplier
	asm_cmd(ADD, 4, 0, 0, 0);		// 2:  SUM = 0
	asm_cmd(JLT, 0, 3, 0, 8);		// 3:  If multiplier < 0 goto negative
	// loop_positive:
	asm_cmd(JEQ, 0, 3, 0, 12);		// 4:  Exit condition (multiplier == 0)
	asm_cmd(ADD, 4, 4, 2, 0);		// 5:  SUM += multiplicand
	asm_cmd(SUB, 3, 3, 1, 1);		// 6:  multiplier -= 1
	asm_cmd(JIN, 0, 0, 0, 4);		// 7:  goto loop_positive
	// loop_negative:
	asm_cmd(JEQ, 0, 3, 0, 12);		// 8:  Exit condition (multiplier == 0)
	asm_cmd(SUB, 4, 4, 2, 0);		// 9:  SUM += multiplicand
	asm_cmd(ADD, 3, 3, 1, 1);		// 10: multiplier -= 1
	asm_cmd(JIN, 0, 0, 0, 8);		// 11: goto loop_negative
	// done:
	asm_cmd(ST,  0, 4, 1, 1002);	// 12: output result
	asm_cmd(HLT, 0, 0, 0, 0);

	// Original program memory init
	//for (i = 0; i < 5; i++)
	//	mem[15+i] = i;
	mem[1000] = 5;
	mem[1001] = -2;

	// Original program memory init
	//last_addr = 20;
	last_addr = 1002;

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
