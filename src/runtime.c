#include "runtime.h"
#include "repl.h"
#include <stdio.h>
#include <string.h>

static const char STARTUP_MESSAGE[] = " BASIC 1.1\n\n";

void initialise_memory_model(BasicState *state) {
    state->program_line_redundant_spaces_flag = false;
    state->current_line_address = 0;
    state->resume_address = 0;
    state->auto_mode = false;
    state->program_protection_flag = false;
    state->error_number = 0;
    state->resume_line_number = 0;
    state->auto_active_flag = false;
    state->auto_line_number = 10;
    state->auto_increment_step = 10;
    state->program_running = false;
    state->current_program_line_index = 0;
    state->for_stack_top = 0;
    state->data_line_index = 0;
    state->data_item_offset = 0;
    state->program_line_count = 0;
    state->string_heap_top = 0;
    state->execution_stack_top = 0;
    memset(state->numeric_variables, 0, sizeof(state->numeric_variables));
    memset(state->string_variables, 0, sizeof(state->string_variables));
    memset(state->array_defined, 0, sizeof(state->array_defined));
    memset(state->array_sizes, 0, sizeof(state->array_sizes));
    memset(state->array_variables, 0, sizeof(state->array_variables));
    memset(state->memory, 0, sizeof(state->memory));
}

void zero_current_line_address(BasicState *state) {
    state->current_line_address = 0;
}

void clear_errors_and_set_resume_addr_to_current(BasicState *state) {
    state->resume_address = state->current_line_address;
}

void cancel_AUTO_mode(BasicState *state) {
    state->auto_active_flag = false;
    state->auto_mode = false;
}

void reset_basic(BasicState *state) {
    state->program_line_redundant_spaces_flag = false;
    state->current_line_address = 0;
    state->resume_address = 0;
    state->auto_mode = false;
    state->program_protection_flag = false;
    state->error_number = 0;
    state->resume_line_number = 0;
    state->auto_active_flag = false;
    state->program_running = false;
    state->current_program_line_index = 0;
    state->for_stack_top = 0;
    state->program_line_count = 0;
    state->string_heap_top = 0;
    state->execution_stack_top = 0;
    memset(state->numeric_variables, 0, sizeof(state->numeric_variables));
    memset(state->string_variables, 0, sizeof(state->string_variables));
    memset(state->array_defined, 0, sizeof(state->array_defined));
    memset(state->array_sizes, 0, sizeof(state->array_sizes));
    memset(state->array_variables, 0, sizeof(state->array_variables));
}

void symbol_after(BasicState *state, uint16_t value) {
    (void)state;
    (void)value;
}

void init_streams_and_display_ASCIIZ_string(BasicState *state, const char *message) {
    (void)state;
    printf("%s", message);
}

void reset_string_stack_and_fn_params(BasicState *state) {
    (void)state;
}

void sound_hold(BasicState *state) {
    (void)state;
}

void on_break_cont(BasicState *state) {
    (void)state;
}

void turn_display_on(BasicState *state) {
    (void)state;
}

uint16_t get_current_line_number(BasicState *state) {
    (void)state;
    return 0;
}

uint16_t get_resume_line_number(BasicState *state) {
    return state->resume_line_number;
}

uint16_t next_AUTO_line_number(BasicState *state) {
    if (!state->auto_active_flag) {
        return 0;
    }
    uint16_t next_number = state->auto_line_number;
    state->auto_line_number += state->auto_increment_step;
    return next_number;
}

void output_ASCIIZ_string(BasicState *state, const char *message) {
    (void)state;
    printf("%s", message);
}

bool input_text_to_BASIC_input_area(BasicState *state) {
    if (fgets(state->input_buffer, sizeof(state->input_buffer), stdin) == NULL) {
        return false;
    }
    return true;
}

bool find_program_line(BasicState *state, uint16_t line_number, size_t *index) {
    for (size_t i = 0; i < state->program_line_count; ++i) {
        if (state->program_lines[i].line_number == line_number) {
            if (index) {
                *index = i;
            }
            return true;
        }
    }
    return false;
}

void insert_or_delete_program_line(BasicState *state, uint16_t line_number, const char *statement) {
    size_t index = 0;
    bool found = find_program_line(state, line_number, &index);
    bool delete_line = statement == NULL || statement[0] == '\0' || statement[0] == '\n';

    if (found) {
        if (delete_line) {
            for (size_t i = index; i + 1 < state->program_line_count; ++i) {
                state->program_lines[i] = state->program_lines[i + 1];
            }
            state->program_line_count -= 1;
            return;
        }
        strncpy(state->program_lines[index].text, statement, MAX_INPUT_LINE - 1);
        state->program_lines[index].text[MAX_INPUT_LINE - 1] = '\0';
        return;
    }

    if (delete_line) {
        return;
    }

    if (state->program_line_count >= MAX_PROGRAM_LINES) {
        return;
    }

    index = state->program_line_count;
    for (size_t i = 0; i < state->program_line_count; ++i) {
        if (state->program_lines[i].line_number > line_number) {
            index = i;
            break;
        }
    }

    for (size_t i = state->program_line_count; i > index; --i) {
        state->program_lines[i] = state->program_lines[i - 1];
    }

    state->program_lines[index].line_number = line_number;
    strncpy(state->program_lines[index].text, statement, MAX_INPUT_LINE - 1);
    state->program_lines[index].text[MAX_INPUT_LINE - 1] = '\0';
    state->program_line_count += 1;
}

void list_program(BasicState *state) {
    for (size_t i = 0; i < state->program_line_count; ++i) {
        printf("%u %s", state->program_lines[i].line_number, state->program_lines[i].text);
        if (state->program_lines[i].text[0] != '\0' && state->program_lines[i].text[strlen(state->program_lines[i].text) - 1] != '\n') {
            putchar('\n');
        }
    }
}

void basic_init(BasicState *state) {
    memset(state, 0, sizeof(*state));
    state->pc = 0;
    state->sp = MEMORY_SIZE - 1;
    state->accumulator.type = VALUE_TYPE_NONE;
    state->string_heap_top = 0;
    state->execution_stack_top = 0;
}

void basic_run(BasicState *state) {
    firmware_init();
    initialise_memory_model(state);
    init_streams_and_display_ASCIIZ_string(state, STARTUP_MESSAGE);
    zero_current_line_address(state);
    clear_errors_and_set_resume_addr_to_current(state);
    firmware_call_stub("REAL_init_random_number_generator");
    cancel_AUTO_mode(state);
    reset_basic(state);
    symbol_after(state, 240);
    repl_read_eval_print_loop(state);
}
