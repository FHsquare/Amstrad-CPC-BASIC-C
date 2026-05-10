#include "repl.h"
#include "execution.h"
#include "runtime.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool is_blank_line(const char *line) {
    while (*line != '\0') {
        if (!isspace((unsigned char)*line)) {
            return false;
        }
        line++;
    }
    return true;
}

static bool parse_program_line_number(const char *line, uint16_t *line_number, const char **statement) {
    while (isspace((unsigned char)*line)) {
        line++;
    }

    if (!isdigit((unsigned char)*line)) {
        return false;
    }

    char *endptr = NULL;
    long parsed = strtol(line, &endptr, 10);
    if (parsed < 1 || parsed > 65535 || endptr == line) {
        return false;
    }

    while (*endptr != '\0' && isspace((unsigned char)*endptr)) {
        endptr++;
    }

    *line_number = (uint16_t)parsed;
    if (statement) {
        *statement = endptr;
    }
    return true;
}

static const char *skip_space(const char *text) {
    while (*text != '\0' && isspace((unsigned char)*text)) {
        text++;
    }
    return text;
}

static bool execute_statements_in_line(BasicState *state, const char *line) {
    char buffer[MAX_INPUT_LINE];
    strncpy(buffer, line, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    bool in_string = false;
    char *segment = buffer;

    for (char *cursor = buffer; *cursor != '\0'; ++cursor) {
        if (*cursor == '"') {
            in_string = !in_string;
            continue;
        }
        if (*cursor != ':' || in_string) {
            continue;
        }

        *cursor = '\0';
        const char *trimmed = skip_space(segment);
        if (*trimmed != '\0') {
            bool jumped = execute_statement(state, trimmed);
            if (!state->program_running || jumped) {
                return jumped;
            }
        }
        segment = cursor + 1;
    }

    const char *trimmed = skip_space(segment);
    if (*trimmed != '\0') {
        bool jumped = execute_statement(state, trimmed);
        if (!state->program_running || jumped) {
            return jumped;
        }
    }
    return false;
}

static void handle_direct_command_line(BasicState *state, const char *line);

static void display_ready_message(BasicState *state) {
    output_ASCIIZ_string(state, "Ready\n");
}

static void run_program(BasicState *state) {
    if (state->program_line_count == 0) {
        printf("No program to run\n");
        return;
    }

    state->program_running = true;
    state->current_program_line_index = 0;

    while (state->program_running && state->current_program_line_index < state->program_line_count) {
        size_t current_index = state->current_program_line_index;
        const char *statement = state->program_lines[current_index].text;
        bool jumped = execute_statements_in_line(state, statement);

        if (state->program_running) {
            if (!jumped) {
                state->current_program_line_index = current_index + 1;
            }
        }
    }
}

static void handle_direct_command_segment(BasicState *state, const char *segment) {
    char command[32];
    size_t i = 0;
    const char *cursor = segment;

    while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
        cursor++;
    }

    while (*cursor != '\0' && !isspace((unsigned char)*cursor) && i + 1 < sizeof(command)) {
        command[i++] = toupper((unsigned char)*cursor);
        cursor++;
    }
    command[i] = '\0';

    if (strcmp(command, "NEW") == 0) {
        reset_basic(state);
        printf("Program cleared\n");
        return;
    }

    if (strcmp(command, "CLEAR") == 0) {
        state->program_line_count = 0;
        state->resume_line_number = 0;
        cancel_AUTO_mode(state);
        printf("Clear not fully implemented\n");
        return;
    }

    if (strcmp(command, "LIST") == 0) {
        list_program(state);
        return;
    }

    if (strcmp(command, "RUN") == 0) {
        run_program(state);
        return;
    }

    if (strcmp(command, "AUTO") == 0) {
        printf("AUTO not implemented yet\n");
        return;
    }

    if (strcmp(command, "QUIT") == 0 || strcmp(command, "SYSTEM") == 0) {
        printf("Exiting BASIC\n");
        exit(0);
    }

    execute_statements_in_line(state, segment);
}

static void handle_direct_command_line(BasicState *state, const char *line) {
    char buffer[MAX_INPUT_LINE];
    strncpy(buffer, line, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    bool in_string = false;
    char *segment = buffer;

    for (char *cursor = buffer; *cursor != '\0'; ++cursor) {
        if (*cursor == '"') {
            in_string = !in_string;
            continue;
        }
        if (*cursor != ':' || in_string) {
            continue;
        }

        *cursor = '\0';
        const char *trimmed = skip_space(segment);
        if (*trimmed != '\0') {
            handle_direct_command_segment(state, trimmed);
        }
        segment = cursor + 1;
    }

    const char *trimmed = skip_space(segment);
    if (*trimmed != '\0') {
        handle_direct_command_segment(state, trimmed);
    }
}

static void handle_input_line(BasicState *state, const char *line) {
    uint16_t line_number;
    const char *statement = NULL;

    if (parse_program_line_number(line, &line_number, &statement)) {
        if (statement == NULL || *statement == '\0' || *statement == '\n') {
            insert_or_delete_program_line(state, line_number, "");
            printf("Deleted line %u\n", line_number);
            return;
        }

        insert_or_delete_program_line(state, line_number, statement);
        printf("Inserted line %u\n", line_number);
        return;
    }

    handle_direct_command_line(state, line);
}

static void repl_input_loop(BasicState *state) {
    while (true) {
        zero_current_line_address(state);

        if (!input_text_to_BASIC_input_area(state)) {
            printf("<EOF>\n");
            break;
        }

        if (is_blank_line(state->input_buffer)) {
            continue;
        }

        handle_input_line(state, state->input_buffer);
    }
}

void repl_read_eval_print_loop(BasicState *state) {
    reset_string_stack_and_fn_params(state);
    get_current_line_number(state);
    sound_hold(state);
    on_break_cont(state);
    turn_display_on(state);

    if (state->program_protection_flag) {
        reset_basic(state);
    }

    if (state->error_number != 2) {
        display_ready_message(state);
    } else {
        state->error_number = 0;
    }

    if (get_resume_line_number(state) != 0) {
        // In the original BASIC this branch can go to edit mode.
    }

    repl_input_loop(state);
}
