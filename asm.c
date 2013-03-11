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
	/* Multiplication program starts here */
	asm_cmd(LD,  2, 0, 1, 1000);	// 0:  Load multiplicand
	asm_cmd(LD,  3, 0, 1, 1001);	// 1:  Load multiplier
	asm_cmd(ADD, 4, 0, 0, 0);		// 2:  sum = 0
	asm_cmd(SUB, 6, 0, 1, 1);		// 3:  Prepare const -1
	asm_cmd(JLT, 0, 3, 0, 12);		// 4:  If multiplier < 0 goto negative
	// loop_positive:
	asm_cmd(JEQ, 0, 3, 0, 19);		// 5:  Exit condition (multiplier == 0)
	asm_cmd(AND, 5, 3, 1, 1);		// 6:  Get multiplier lower bit
	asm_cmd(JEQ, 0, 5, 0, 9);		// 7:  Test multiplier lower bit
	asm_cmd(ADD, 4, 4, 2, 0);		// 8:  sum += multiplicand
	asm_cmd(LSF, 2, 2, 1, 1);		// 9:  multiplicand << 1
	asm_cmd(RSF, 3, 3, 1, 1);		// 10: multiplier >> 1
	asm_cmd(JEQ, 0, 0, 0, 5);		// 11: goto loop_positive
	// loop_negative:
	asm_cmd(AND, 5, 3, 1, 1);		// 12: Get multiplier lower bit
	asm_cmd(JEQ, 0, 5, 0, 15);		// 13: Test multiplier lower bit
	asm_cmd(SUB, 4, 4, 2, 0);		// 14: sum -= multiplicand
	asm_cmd(JEQ, 0, 3, 6, 19);		// 15: Exit condition (multiplier == -1)
	asm_cmd(LSF, 2, 2, 1, 1);		// 16: multiplicand << 1
	asm_cmd(RSF, 3, 3, 1, 1);		// 17: multiplier >> 1
	asm_cmd(JEQ, 0, 0, 0, 12);		// 18: goto loop_negative
	// done:
	asm_cmd(ST,  0, 4, 1, 1002);	// 19: output result
	asm_cmd(HLT, 0, 0, 0, 0);		// 20: halt
	/* Multiplication table program starts here
	   R2 = row
	   R3 = column
	   R4 = result
	   R5 = memory pointer
	   R6 = memory boundary
	*/

	/*asm_cmd(ADD, 2, 0, 1, 10);		// 0:  row = 10
	asm_cmd(ADD, 3, 0, 1, 10);		// 1:  column = 10
	asm_cmd(ADD, 5, 0, 1, 2099);	// 2:  ptr = 2099
	asm_cmd(ADD, 6, 0, 1, 10);		// 3:  saved_column = 10
	// loop:
	asm_cmd(JEQ, 0, 2, 0, 18);		// 4:  Exit condition
	asm_cmd(ADD, 4, 0, 1, 0);		// 5:  result = 0
	// mult:
	asm_cmd(JEQ, 0, 3, 0, 10);		// 6:  if (column == 0) goto store
	asm_cmd(ADD, 4, 4, 2, 0);		// 7:  result += row
	asm_cmd(SUB, 3, 3, 1, 1);		// 8:  column -= 1;
	asm_cmd(JEQ, 0, 0, 0, 6);		// 9:  goto mult
	// store:
	asm_cmd(ST,  0, 4, 5, 0);		// 10: mem[ptr] = result
	asm_cmd(SUB, 5, 5, 1, 1);		// 11: ptr -= 1
	asm_cmd(SUB, 6, 6, 1, 1);		// 12: saved_column -= 1
	asm_cmd(JNE, 0, 6, 0, 16);		// 13: if (saved_column != 0) goto restore
	asm_cmd(ADD, 6, 0, 1, 10);		// 14: saved_column = 10
	asm_cmd(SUB, 2, 2, 1, 1);		// 15: row -= 1
	// restore:
	asm_cmd(ADD, 3, 6, 0, 0);		// 16: column = saved_column
	asm_cmd(JEQ, 0, 0, 0, 4);		// 17: goto test
	asm_cmd(HLT, 0, 0, 0, 0);		// 18: halt*/

	// Original program memory init
	//for (i = 0; i < 5; i++)
	//	mem[15+i] = i;
	// Multiplication program memory init
	mem[1000] = 5;
	mem[1001] = -2;

	// Original program memory init
	//last_addr = 20;
	// Multiplication program memory init
	//last_addr = 1002;
	last_addr = 2100;

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
