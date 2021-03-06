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
	int unused			: 2;
	Opcode opcode;
	unsigned int dst	: 3;
	unsigned int src0	: 3;
	unsigned int src1	: 3;
	int immediate		: 16;	
	int val0;
	int val1;
} Instruction;

char* toOpcodeName(Opcode opcode) {
   switch (opcode) {
   case ADD:	return "ADD";
   case SUB:	return "SUB";
   case LSF:	return "LSF";
   case RSF:	return "RSF";
   case AND:	return "AND";
   case OR:		return "OR";
   case XOR:	return "XOR";
   case LHI:	return "LHI";
   case LD:		return "LD";
   case ST: 	return "ST";
   case JLT:	return "JLT";
   case JLE: 	return "JLE";
   case JEQ: 	return "JEQ";
   case JNE: 	return "JNE";
   case JIN: 	return "JIN";
   case HLT: 	return "HLT";
   default:
	printf("Illegal opcode %d!\n", opcode);
	exit(1);
   }
   return NULL;
}

Instruction fetch(unsigned int inst) {
	Instruction out;
	out.opcode = (Opcode)(unsigned int)((inst >> 25) & 0x1F);
	out.dst = (inst >> 22) & 0x7;
	out.src0 = (inst >> 19) & 0x7; 
	out.src1 = (inst >> 16) & 0x7;
	out.immediate = (inst) & 0xffff;
	return out;
}

void decode(Instruction* inst, int* regs) {
	inst->val0 = (inst->src0 == 1 || inst->opcode == LHI) ? inst->immediate : regs[inst->src0];
	inst->val1 = (inst->src1 == 1) ? inst->immediate : regs[inst->src1];
}

void execute(Instruction inst, unsigned short* pc, unsigned int* mem, int* regs, FILE* outFile) {
	switch(inst.opcode) {	
	case ADD:
		regs[inst.dst] = inst.val0 + inst.val1;
		fprintf(outFile, ">>>> EXEC: R[%d] = %d ADD %d <<<<\n\n", inst.dst, inst.val0, inst.val1);
		break;
	case SUB:
		regs[inst.dst] = inst.val0 - inst.val1;
		fprintf(outFile, ">>>> EXEC: R[%d] = %d SUB %d <<<<\n\n", inst.dst, inst.val0, inst.val1);
		break;
	case LSF:
		regs[inst.dst] = inst.val0 << inst.val1;
		fprintf(outFile, ">>>> EXEC: R[%d] = %d LSF %d <<<<\n\n", inst.dst, inst.val0, inst.val1);
		break;
	case RSF:
		regs[inst.dst] = inst.val0 >> inst.val1;
		fprintf(outFile, ">>>> EXEC: R[%d] = %d RSF %d <<<<\n\n", inst.dst, inst.val0, inst.val1);
		break;
	case AND:
		regs[inst.dst] = inst.val0 & inst.val1;
		fprintf(outFile, ">>>> EXEC: R[%d] = %d AND %d <<<<\n\n", inst.dst, inst.val0, inst.val1);
		break;
	case OR:
		regs[inst.dst] = inst.val0 | inst.val1;
		fprintf(outFile, ">>>> EXEC: R[%d] = %d OR %d <<<<\n\n", inst.dst, inst.val0, inst.val1);
		break;
	case XOR:
		regs[inst.dst] = inst.val0 ^ inst.val1;
		fprintf(outFile,">>>> EXEC: R[%d] = %d XOR %d <<<<\n\n", inst.dst, inst.val0, inst.val1);
		break;
	case LHI:
		regs[inst.dst] = (inst.val0 << 16) | (regs[inst.dst] & 0xffff);
		fprintf(outFile,">>>> EXEC: R[%d][31:16] = %d <<<<\n\n", inst.dst, inst.val0);
		break;
	case LD:
		regs[inst.dst] = mem[inst.val1 & 0xffff];
		fprintf(outFile, ">>>> EXEC: R[%d] = MEM[%d] = %08x <<<<\n\n", inst.dst, inst.val1, mem[inst.val1 & 0xffff]);
		break;
	case ST:
		mem[inst.val1 & 0xffff] = inst.val0;
		fprintf(outFile, ">>>> EXEC: MEM[%d] = R[%d] = %08x <<<<\n\n", inst.val1, inst.src0, inst.val0);
		break;
	case JLT:
		if (inst.val0 < inst.val1) {
			regs[7] = *pc - 1;
			*pc = inst.immediate;
		}
		fprintf(outFile, ">>>> EXEC: JLT %d, %d, %d <<<<\n\n", inst.val0, inst.val1, *pc);
		break;
	case JLE:
		if (inst.val0 <= inst.val1) {
			regs[7] = *pc - 1;
			*pc = inst.immediate;
		}
		fprintf(outFile, ">>>> EXEC: JLE %d, %d, %d <<<<\n\n", inst.val0, inst.val1, *pc);
		break;
	case JEQ:
		if (inst.val0 == inst.val1) {
			regs[7] = *pc - 1;
			*pc = inst.immediate;
		}
		fprintf(outFile, ">>>> EXEC: JEQ %d, %d, %d <<<<\n\n", inst.val0, inst.val1, *pc);
		break;
	case JNE:
		if (inst.val0 != inst.val1) {
			regs[7] = *pc - 1;
			*pc = inst.immediate;
		}
		fprintf(outFile, ">>>> EXEC: JNE %d, %d, %d <<<<\n\n", inst.val0, inst.val1, *pc);
		break;
	case JIN:
		regs[7] = *pc - 1;
		*pc = inst.val0;
		fprintf(outFile, ">>>> EXEC: JIN R[%d] = %08x <<<<\n\n", inst.src0, inst.val0);
		break;
	case HLT:
		// Intentionally print one line break
		fprintf(outFile, ">>>> EXEC: HALT at PC %04x<<<<\n", *pc - 1);
		break;
	default:
		printf("Illegal opcode %d!\n", inst.opcode);
		exit(1);
		break;
	}
	// Suppress assignments to R0 and R1
	regs[0] = 0;
	regs[1] = 0;
}

void printFetch(Instruction inst, int instCount, unsigned short pc, unsigned int* mem, int* regs, FILE* outFile) {
	fprintf(outFile, "--- instruction %d (%04x) @ PC %d (%04x) -----------------------------------------------------------\n", 
		instCount, instCount, pc, pc);
	
	fprintf(outFile, "pc = %04x, inst = %08x, opcode = %d (%s), dst = %d, src0 = %d, src1 = %d, immediate = %08x\n",
		pc, mem[pc], inst.opcode, toOpcodeName(inst.opcode), inst.dst, inst.src0, inst.src1, inst.immediate);
	
	fprintf(outFile, "r[0] = 00000000 r[1] = %08x r[2] = %08x r[3] = %08x \n", inst.immediate, regs[2], regs[3]);
	
	fprintf(outFile, "r[4] = %08x r[5] = %08x r[6] = %08x r[7] = %08x \n\n", regs[4], regs[5], regs[6], regs[7]);
}

int main(int argc, char** argv) {
	int regs[REG_COUNT] = {0};
	char lineBuffer[CMD_SIZE];
	char* inFilename = argv[1];
	char* outFilename = "trace.txt";
	unsigned int mem[MAX_MEMORY_SIZE] = {0};
	int memIndex = 0;
	FILE* inFile;
	FILE* outFile;
	unsigned short pc = 0;
	int instCount = 0;
	Instruction inst = {0};

	inFile = fopen(inFilename, "r");
	if (inFile == NULL) {
		printf("Error opening file %s, exit\n", inFilename);
		return 1;
	}

	outFile = fopen(outFilename, "w");

	while (fgets(lineBuffer, CMD_SIZE, inFile) != NULL) {
		if (sscanf(lineBuffer, "%08x", &mem[memIndex]) != 1) {
			printf("Error reading from file %s, exit\n", inFilename);
			fclose(inFile);
			return 1;			
		}
		memIndex++;
	} 

	if (ferror(inFile)) {
		printf("Error reading from file %s, exit\n", inFilename);
		fclose(inFile);
		return 1;	
	}

	fclose(inFile);
	fprintf(outFile, "program %s loaded, %d lines\n\n", inFilename, memIndex);

	do {
		inst = fetch(mem[pc]);
		printFetch(inst, instCount, pc, mem, regs, outFile);
		decode(&inst, regs);
		pc++;
		execute(inst, &pc, mem, regs, outFile);
		instCount++;
	} while (inst.opcode != HLT);

	fprintf(outFile, "sim finished at pc %d, %d instructions\n", pc - 1, instCount);
	fclose(outFile);

	return 0;
}
