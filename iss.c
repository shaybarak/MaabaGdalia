
#include <stdio.h>
#include <stdlib.h>

#define REG_COUNT 8
#define CMD_SIZE 32
#define MAX_CMD_COUNT 65536
#define MAX_MEMORY_SIZE 65536

#define ADD 0
#define SUB 1
#define LSF 2
#define RSF 3
#define AND 4
#define OR 	5
#define XOR 6
#define LHI 7
#define LD 	8
#define ST 	9
#define JLT 16
#define JLE 17
#define JEQ 18
#define JNE 19
#define JIN 20
#define HLT 24


int fetch(int inst, int* opcode, int* dst, int* src0, int* src1, int* immediate) {
	*opcode = (inst >> 25) & 0xF;
	*dst = (inst >> 22) & 0x7;
	*src0 = (inst >> 19) & 0x7; 
	*src1 = (inst >> 16) & 0x7;
	*immediate = (inst) & 0xffff;
	
	if(*opcode == ADD || *opcode == SUB) {
		int sign = (*immediate >> 16) & 0x1;
		int ext = (sign == 0) ? 0 : 0xffff0000;
		*immediate = ext | *immediate;
	}
	return 0;
}


void decode(int opcode, int src0, int src1, int immediate, int* val0, int* val1, int* regs) {
	*val0 = (src0 == 1 || opcode == LHI) ? immediate : regs[src0];
	*val1 = (src1 == 1) ? immediate : regs[src1];
}

int execute(int opcode, int dst, int val0, int val1, int immediate, 
	int* pc, int* mem, int* regs) {
	
	switch(opcode) {	
		case ADD:
			regs[dst] = val0 + val1;
		case SUB:
			regs[dst] = val0 - val1;
		case LSF:
			regs[dst] = val0 << val1;
		case RSF:
			regs[dst] = val0 >> val1;
		case AND:
			regs[dst] = val0 & val1;
		case OR:
			regs[dst] = val0 | val1;
		case XOR:
			regs[dst] = val0 ^ val1;
		case LHI:
			regs[dst] = val0 | (regs[dst] & 0xffff);
		case LD:
			regs[dst] = mem[val1];
		case ST:
			mem[val1] = val0;
		case JLT:
			*pc = (val0 < val1) ? immediate : *pc;
		case JLE:
			*pc = (val0 <= val1) ? immediate : *pc;
		case JEQ:
			*pc = (val0 == val1) ? immediate : *pc;
		case JNE:
			*pc = (val0 != val1) ? immediate : *pc;
		case JIN:
		case HLT:
		default:
			return;
	}	      
}

void printAction() {
  printf("-- instrcution %d (04%d

}


int main(int argc, char** argv) {
	int regs[REG_COUNT] = {0};
	char lineBuffer[CMD_SIZE];
	char* filename = argv[1];
	int mem[MAX_MEMORY_SIZE] = {0};
	int memIndex = 0;
	
	FILE* input = fopen(filename, "r");
	if (input == NULL) {
		printf("Error openning file %s, exit\n", filename);
		return 1;
	}
	
	while (fgets(lineBuffer, CMD_SIZE, input) != NULL) {
		if (sscanf(lineBuffer, "%08x", &mem[memIndex]) != 1) {
			printf("Error reading from file %s, exit\n", filename);
			fclose(input);
			return 1;			
		}
		memIndex++;
		
	} 
	
	if (ferror(input)) {
		printf("Error reading from file %s, exit\n", filename);
		fclose(input);
		return 1;	
	}
	fclose(input);
	
	int pc = 0;
	int opcode, dst, src0, src1, immediate, val0, val1;
	
	
	int isHalted = 0;
	while (!isHalted) {
		fetch(mem[pc], &opcode, &dst, &src0, &src1, &immediate);
		if (opcode == HLT) {
		  continue;
		}
		decode(opcode, src0, src1, immediate, &val0, &val1, regs);
		execute(opcode, dst, val0, val1, immediate, &pc, &mem, &regs);
	}

	return 0;
		
}

