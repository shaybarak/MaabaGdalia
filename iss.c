
#include <stdio.h>
#include <stdlib.h>

#define REG_COUNT 8
#define CMD_SIZE 32
#define MAX_CMD_COUNT 65536
#define MAX_MEMORY_SIZE 65536

typedef enum Opcode {
	ADD = 0,
	SUB,
	LSF,
	RSF,
	AND,
	OR,
	XOR,
	LHI,
	LD,
	ST,
	JLT = 16,
	JLE,
	JEQ,
	JNE,
	JIN,
	HLT = 24,
} Opcode;

typedef struct {
	int unused		: 2;
	Opcode opcode	: 5;
	int dst			: 3;
	int src0		: 3;
	int src1		: 3;
	int immediate	: 16;	
	int val0;
	int val1;
} Instruction;

char* toOpcodeName(Opcode opcode) {
   switch (opcode) {
   case ADD: return "ADD";
   case SUB: return "SUB";
   case LSF: return "LSF";
   case RSF: return "RSF";
   case AND: return "AND";
   case OR: return "OR";
   case XOR: return "XOR";
   case LHI: return "LHI";
   case LD: return "LD";
   case ST: return "ST";
   case JLT: return "JLT";
   case JLE: return "JLE";
   case JEQ: return "JEQ";
   case JNE: return "JNE";
   case JIN: return "JIN";
   case HLT: return "HLT";
   }
}

Instruction fetch(int inst) {
	Instruction out;
	out.opcode = (Opcode)((inst >> 25) & 0x1F);
	out.dst = (inst >> 22) & 0x7;
	out.src0 = (inst >> 19) & 0x7; 
	out.src1 = (inst >> 16) & 0x7;
	out.immediate = (inst) & 0xffff;

	//TODO remove
	/*if(out.opcode == ADD || out.opcode == SUB) {
		int sign = (out.immediate >> 16) & 0x1;
		int ext = (sign == 0) ? 0 : 0xffff0000;
		out.immediate = ext | out.immediate;
	}*/
	return out;
}


void decode(Instruction inst, int* regs) {
	inst.val0 = (inst.src0 == 1 || inst.opcode == LHI) ? inst.immediate : regs[inst.src0];
	inst.val1 = (inst.src1 == 1) ? inst.immediate : regs[inst.src1];
}

int execute(Instruction inst, int* pc, int* mem, int* regs, FILE* outFile) {

		switch(inst.opcode) {	
		case ADD:
			regs[inst.dst] = inst.val0 + inst.val1;
			fprintf(outFile, ">>>> EXEC: R[%d] = %d ADD %d <<<<", inst.dst, inst.val0, inst.val1);
			break;
		case SUB:
			regs[inst.dst] = inst.val0 - inst.val1;
			fprintf(outFile, ">>>> EXEC: R[%d] = %d SUB %d <<<<", inst.dst, inst.val0, inst.val1);
			break;
		case LSF:
			regs[inst.dst] = inst.val0 << inst.val1;
			fprintf(outFile, ">>>> EXEC: R[%d] = %d LSF %d <<<<", inst.dst, inst.val0, inst.val1);
			break;
		case RSF:
			regs[inst.dst] = inst.val0 >> inst.val1;
			fprintf(outFile, ">>>> EXEC: R[%d] = %d RSF %d <<<<", inst.dst, inst.val0, inst.val1);
			break;
		case AND:
			regs[inst.dst] = inst.val0 & inst.val1;
			fprintf(outFile, ">>>> EXEC: R[%d] = %d AND %d <<<<", inst.dst, inst.val0, inst.val1);
			break;
		case OR:
			regs[inst.dst] = inst.val0 | inst.val1;
			fprintf(outFile, ">>>> EXEC: R[%d] = %d OR %d <<<<", inst.dst, inst.val0, inst.val1);
			break;
		case XOR:
			regs[inst.dst] = inst.val0 ^ inst.val1;
			fprintf(outFile,">>>> EXEC: R[%d] = %d XOR %d <<<<", inst.dst, inst.val0, inst.val1);
			break;
		case LHI:
			regs[inst.dst] = inst.val0 | (regs[inst.dst] & 0xffff);
			fprintf(outFile,">>>> EXEC: R[%d][31:16] = %d <<<<", inst.dst, inst.val0);
			break;
		case LD:
			regs[inst.dst] = mem[inst.val1];
			fprintf(outFile, ">>>> EXEC: R[%d] = MEM[%d] = %08x <<<<", inst.dst, inst.val1);
			break;
		case ST:
			mem[inst.val1] = inst.val0;
			fprintf(outFile, ">>>> EXEC: MEM[%d] = R[%d] = %08x <<<<", inst.val1, inst.val0);
			break;
		case JLT:
			*pc = (inst.val0 < inst.val1) ? inst.immediate : *pc;
			break;
		case JLE:
			*pc = (inst.val0 <= inst.val1) ? inst.immediate : *pc;
			break;
		case JEQ:
			*pc = (inst.val0 == inst.val1) ? inst.immediate : *pc;
			break;
		case JNE:
			*pc = (inst.val0 != inst.val1) ? inst.immediate : *pc;
			break;
		case JIN:
		case HLT:
		default:
			break;;
		}	      
}

void printFetch(Instruction inst, int instCount, int pc, int* mem, int* regs, FILE* outFile) {
	
	fprintf(outFile, "--- instruction %d (%04x) @ PC %d (%04x) -----------------------------------------------------------\n", 
		instCount, instCount, pc, pc);
	
	fprintf(outFile, "pc = %04x, inst = 0088000f, opcode = %d (%s), dst = %d, src0 = %d, src1 = %d, immediate = %08x\n",
		pc, mem[pc], inst.opcode, toOpcodeName(inst.opcode), inst.dst, inst.src0, inst.src1, inst.immediate);
	
	fprintf(outFile, "r[0] = 00000000 r[1] = %08x r[2] = %08x r[3] = %08x\n", inst.immediate, regs[2], regs[3]);
	
	fprintf(outFile, "r[4] = %08x r[5] = %08x r[6] = %08x r[7] = %08x\n\n", regs[4], regs[5], regs[6], regs[7]);

}


int main(int argc, char** argv) {
	int regs[REG_COUNT] = {0};
	char lineBuffer[CMD_SIZE];
	char* inFilename = argv[1];
	char* outFilename = "tarce.txt";
	int mem[MAX_MEMORY_SIZE] = {0};
	int memIndex = 0;

	FILE* input = fopen(inFilename, "r");
	if (input == NULL) {
		printf("Error openning file %s, exit\n", inFilename);
		return 1;
	}

	FILE* outFile = fopen(outFilename, "w");

	while (fgets(lineBuffer, CMD_SIZE, input) != NULL) {
		if (sscanf(lineBuffer, "%08x", &mem[memIndex]) != 1) {
			printf("Error reading from file %s, exit\n", inFilename);
			fclose(input);
			return 1;			
		}
		memIndex++;

	} 

	if (ferror(input)) {
		printf("Error reading from file %s, exit\n", inFilename);
		fclose(input);
		return 1;	
	}
	fclose(input);
	fprintf(outFile, "program %s loaded, %d lines\n\n", inFilename, memIndex);

	int pc = 0;
	int instCount = 0;
	Instruction inst;

	do {
		inst = fetch(mem[pc]);
		printFetch(inst, instCount, pc, mem, regs, outFile);
		decode(inst, regs);
		execute(inst, &pc, mem, regs, outFile);
		instCount++;
	} while (inst.opcode != HLT);

	return 0;

}

