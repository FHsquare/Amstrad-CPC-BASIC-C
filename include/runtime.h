#ifndef CPC_BASIC_RUNTIME_H
#define CPC_BASIC_RUNTIME_H

#include "platform.h"

#define MEMORY_SIZE 0x10000
#define MAX_STRING_HEAP 0x4000
#define MAX_VAR_TABLE 1024
#define MAX_STACK_DEPTH 1024
#define MAX_PROGRAM_LINES 1024
#define MAX_INPUT_LINE 256
#define MAX_ARRAY_SIZE 1024

typedef enum {
    VALUE_TYPE_INT,
    VALUE_TYPE_REAL,
    VALUE_TYPE_STRING,
    VALUE_TYPE_NONE
} ValueType;

typedef struct {
    ValueType type;
    int32_t int_value;
    double real_value;
    uint16_t string_offset;
} Value;

typedef struct {
    uint16_t line_number;
    char text[MAX_INPUT_LINE];
} ProgramLine;

typedef struct {
    char variable;
    int32_t limit;
    int32_t step;
    size_t for_line_index;
} ForFrame;

typedef struct {
    uint8_t memory[MEMORY_SIZE];
    uint16_t pc;
    uint8_t a, f, b, c, d, e, h, l;
    uint16_t sp;
    uint16_t ix;
    uint16_t iy;
    
    uint8_t string_heap[MAX_STRING_HEAP];
    uint16_t string_heap_top;
    
    Value execution_stack[MAX_STACK_DEPTH];
    size_t execution_stack_top;
    ForFrame for_stack[MAX_STACK_DEPTH];
    size_t for_stack_top;

    Value accumulator;

    bool program_line_redundant_spaces_flag;
    uint16_t current_line_address;
    uint16_t resume_address;
    bool auto_mode;

    bool program_protection_flag;
    uint8_t error_number;
    uint16_t resume_line_number;
    bool auto_active_flag;
    uint16_t auto_line_number;
    uint16_t auto_increment_step;

    int32_t numeric_variables[26];
    char string_variables[26][MAX_INPUT_LINE];
    bool array_defined[26];
    int32_t array_sizes[26];
    int32_t array_variables[26][MAX_ARRAY_SIZE];
    bool program_running;
    size_t current_program_line_index;
    size_t data_line_index;
    size_t data_item_offset;

    ProgramLine program_lines[MAX_PROGRAM_LINES];
    size_t program_line_count;
    char input_buffer[MAX_INPUT_LINE];
} BasicState;

void basic_init(BasicState *state);
void basic_run(BasicState *state);

void initialise_memory_model(BasicState *state);
void zero_current_line_address(BasicState *state);
void clear_errors_and_set_resume_addr_to_current(BasicState *state);
void cancel_AUTO_mode(BasicState *state);
void reset_basic(BasicState *state);
void symbol_after(BasicState *state, uint16_t value);
void init_streams_and_display_ASCIIZ_string(BasicState *state, const char *message);
void repl_read_eval_print_loop(BasicState *state);

void reset_string_stack_and_fn_params(BasicState *state);
void sound_hold(BasicState *state);
void on_break_cont(BasicState *state);
void turn_display_on(BasicState *state);
uint16_t get_current_line_number(BasicState *state);
uint16_t get_resume_line_number(BasicState *state);
uint16_t next_AUTO_line_number(BasicState *state);
void output_ASCIIZ_string(BasicState *state, const char *message);
bool input_text_to_BASIC_input_area(BasicState *state);
void insert_or_delete_program_line(BasicState *state, uint16_t line_number, const char *statement);
bool find_program_line(BasicState *state, uint16_t line_number, size_t *index);
void list_program(BasicState *state);

void firmware_init(void);
void firmware_call_stub(const char *name);

#endif // CPC_BASIC_RUNTIME_H
