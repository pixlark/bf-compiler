#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "stretchy_buffer.h"

// : Generate ASM

typedef enum {
	INSTR_ADVANCE_CURSOR,
	INSTR_RETREAT_CURSOR,
	INSTR_INCREMENT,
	INSTR_DECREMENT,
	INSTR_READ,
	INSTR_WRITE,
	INSTR_START_LOOP,
	INSTR_END_LOOP,
	INSTR_JUMP_TO_END_IF_ZERO,
	INSTR_JUMP_TO_START,
} Instruction;

const char * instruction_to_string[] = {
	"ADVANCE_CURSOR",
	"RETREAT_CURSOR",
	"INCREMENT",
	"DECREMENT",
	"READ",
	"WRITE",
	"START_LOOP",
	"END_LOOP",
	"JUMP_TO_END_IF_IS_ZERO",
	"JUMP_TO_START",
};

typedef struct {
	Instruction instruction;
	union {
		size_t loop_index;
	};
} ASM;

ASM asm_new_no_args(Instruction instruction)
{
	return (ASM) { instruction };
}

ASM asm_new_loop_index(Instruction instruction, size_t loop_index)
{
	return (ASM) { instruction, .loop_index = loop_index };
}

typedef struct {
	size_t loop_index;
	size_t * loops;
} Compiler;

Compiler * compiler_alloc()
{
	Compiler * compiler = malloc(sizeof(Compiler));
	compiler->loop_index = 0;
	compiler->loops = NULL;
	return compiler;
}

ASM * compile_brainfuck_to_asm(Compiler * compiler, const char * program_text)
{
	size_t text_cursor = 0;
	ASM * code = NULL;

	bool running = true;
	while (running) {
		char c = program_text[text_cursor++];
		switch (c) {
		case '\0':
			running = false;
			break;
		case '>':
			sb_push(code, asm_new_no_args(INSTR_ADVANCE_CURSOR));
			break;
		case '<':
			sb_push(code, asm_new_no_args(INSTR_RETREAT_CURSOR));
			break;
		case '+':
			sb_push(code, asm_new_no_args(INSTR_INCREMENT));
			break;
		case '-':
			sb_push(code, asm_new_no_args(INSTR_DECREMENT));
			break;
		case ',':
			sb_push(code, asm_new_no_args(INSTR_READ));
			break;
		case '.':
			sb_push(code, asm_new_no_args(INSTR_WRITE));
			break;
		case '[': {
			size_t index = compiler->loop_index++;
			sb_push(compiler->loops, index);
			
			sb_push(code, asm_new_loop_index(INSTR_START_LOOP, index));
			sb_push(code, asm_new_loop_index(INSTR_JUMP_TO_END_IF_ZERO, index));
		} break;
		case ']': {
			size_t index = sb_pop(compiler->loops);
			
			sb_push(code, asm_new_loop_index(INSTR_JUMP_TO_START, index));
			sb_push(code, asm_new_loop_index(INSTR_END_LOOP, index));
		} break;
		}
	}
	
	return code;
}

// :\ Generate ASM

// : Output ASM

void output_asm_initialization(FILE * file, size_t memory_size)
{
	fprintf(file, "movq $-1, %%r13\n"); // Set offset value
	fprintf(file, "movq $%llu, %%r8\n", memory_size); // Memory limit
	fprintf(file, "__zeroing_loop:\n"); // Start of zeroing loop
	fprintf(file,
			"movq %%rbp, %%r12\n"
			"addq %%r13, %%r12\n"
			"movb $0, 0(%%r12)\n"
			"subq $1, %%r8\n"
			"subq $1, %%r13\n"); // Zero out this spot
	fprintf(file,
			"cmpq $0, %%r8\n"
			"jnz __zeroing_loop\n"); // If there's memory left, jump back to start of loop
	fprintf(file,
			"movq %%rbp, %%r12\n"); // Set cursor to the stack pointer
	fprintf(file,
			"subq $2, %%r12\n"); // Nudge cursor to the start of memory
}

void output_asm_instruction(FILE * file, ASM chunk, size_t instr_id)
{
	switch (chunk.instruction) {
	case INSTR_ADVANCE_CURSOR:
		// Moves the cursor away from the stack
		fprintf(file, "addq $-1, %%r12\n");
		break;
	case INSTR_RETREAT_CURSOR:
		// Moves the cursor back towards the stack
		fprintf(file, "addq $1, %%r12\n");
		break;
	case INSTR_INCREMENT:
		// Increment the value at %r12
		fprintf(file, "addb $1, 0(%%r12)\n");
		break;
	case INSTR_DECREMENT:
		// Decrement the value at %r12
		fprintf(file, "addb $-1, 0(%%r12)\n");
		break;
	case INSTR_WRITE:
		// Write the byte at %r12
		fprintf(file,
				"movq $1, %%rax\n"    // syscall 1 (write)
				"movq $1, %%rdi\n"    // fd 1 (stdout)
				"movq %%r12, %%rsi\n" // print from %r12
				"movq $1, %%rdx\n"    // 1 char long
				"syscall\n");
		break;
	case INSTR_READ:
		// Read a byte into -1(%rbp) and if it's not an EOF, write it to %r12
		fprintf(file,
				"movq $0, %%rax\n"    // syscall 0 (read)
				"movq $0, %%rdi\n"    // fd 0 (stdin)
				"movq %%rbp, %%rsi\n" // read into -1(%rbp)
				"addq $-1, %%rsi\n"
				"movq $1, %%rdx\n"    // 1 char long
				"syscall\n"
				"cmpq $0, %%rax\n"    // If we didn't read an EOF, copy the value over to %r12
				"jz __eof_skip_%d\n"
				"movb -1(%%rbp), %%al\n"
				"movb %%al, 0(%%r12)\n"
				"__eof_skip_%d:\n", instr_id, instr_id);
		break;
	case INSTR_START_LOOP:
		// Set down a label for this loop index
		fprintf(file, "__loop_%d:\n", chunk.loop_index);
		break;
	case INSTR_END_LOOP:
		// Set down a label for this loop index
		fprintf(file, "__end_loop_%d:\n", chunk.loop_index);
		break;
	case INSTR_JUMP_TO_START:
		// Jump to the start label for this loop index
		fprintf(file, "jmp __loop_%d\n", chunk.loop_index);
		break;
	case INSTR_JUMP_TO_END_IF_ZERO:
		// If the value at %r12 is zero, jump to the end label for this loop index
		fprintf(file,
				"cmpb $0, 0(%%r12)\n"
				"jz __end_loop_%d\n", chunk.loop_index);
		break;
	default:
		assert(false);
		break;
	}
}

void output_asm_to_file(FILE * file, ASM * code)
{
	// Write header
	fprintf(file,
			".text\n"
			".globl main\n"
			"main:\n"
			"pushq %%rbp\n"
			"movq %%rsp, %%rbp\n");

	output_asm_initialization(file, 30000);

	for (int i = 0; i < sb_count(code); i++) {
		output_asm_instruction(file, code[i], i);
	}
	
	// Write footer
	fprintf(file,
			"movl $0, %%eax\n"
			"popq %%rbp\n"
			"ret\n");
}

// :\ Output ASM

char * load_string_from_file(const char * path)
{
	FILE * file = fopen(path, "r");
	if (file == NULL) return NULL;
	int file_len = 0;
	while (fgetc(file) != EOF) file_len++;
	char * str = (char*) malloc(file_len + 1);
	str[file_len] = '\0';
	fseek(file, 0, SEEK_SET);
	for (int i = 0; i < file_len; i++) str[i] = fgetc(file);
	fclose(file);
	return str;
}

int main(int argc, char ** argv)
{
	if (argc < 2 || argc > 3) {
		printf("Usage: bf source [output]\n");
		return 1;
	}
	const char * read_file_name = argv[1];
	const char * program_text = load_string_from_file(read_file_name);

	Compiler * compiler = compiler_alloc();
	ASM * code = compile_brainfuck_to_asm(compiler, program_text);
	
	for (int i = 0; i < sb_count(code); i++) {
		printf("%s\n", instruction_to_string[code[i].instruction]);
	}

	const char * asm_file_name = argc == 3 ? argv[2] : "output.s";
	FILE * asm_file = fopen(asm_file_name, "w");
	output_asm_to_file(asm_file, code);
	fclose(asm_file);
	
	return 0;
}
