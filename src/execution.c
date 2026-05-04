#include "execution.h"
#include "runtime.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *skip_space(const char *text) {
    while (*text != '\0' && isspace((unsigned char)*text)) {
        text++;
    }
    return text;
}

static bool parse_expression(BasicState *state, const char *text, int32_t *value, const char **rest);
static bool parse_term(BasicState *state, const char *text, int32_t *value, const char **rest);
static bool parse_factor(BasicState *state, const char *text, int32_t *value, const char **rest);

static bool parse_integer(const char *text, int32_t *value, const char **rest) {
    text = skip_space(text);
    if (*text == '\0') {
        return false;
    }

    char *end = NULL;
    long result = strtol(text, &end, 10);
    if (end == text) {
        return false;
    }

    *value = (int32_t)result;
    if (rest) {
        *rest = end;
    }
    return true;
}

static bool parse_variable_name(const char *text, char *name, bool *is_string, const char **rest) {
    text = skip_space(text);
    if (!isalpha((unsigned char)*text)) {
        return false;
    }

    name[0] = toupper((unsigned char)*text);
    name[1] = '\0';
    text++;
    *is_string = false;

    if (*text == '$') {
        *is_string = true;
        text++;
    }

    if (rest) {
        *rest = text;
    }
    return true;
}

static bool parse_for_header(BasicState *state, const char *text, char *variable, int32_t *start_value, int32_t *limit_value, int32_t *step_value, const char **rest) {
    bool is_string = false;
    if (!parse_variable_name(text, variable, &is_string, &text) || is_string) {
        return false;
    }

    text = skip_space(text);
    if (*text != '=') {
        return false;
    }
    text++;

    if (!parse_expression(state, text, start_value, &text)) {
        return false;
    }

    text = skip_space(text);
    if (strncasecmp(text, "TO", 2) != 0 || (text[2] != '\0' && !isspace((unsigned char)text[2]))) {
        return false;
    }
    text += 2;

    if (!parse_expression(state, text, limit_value, &text)) {
        return false;
    }

    *step_value = 1;
    text = skip_space(text);
    if (strncasecmp(text, "STEP", 4) == 0 && isspace((unsigned char)text[4])) {
        text += 4;
        if (!parse_expression(state, text, step_value, &text)) {
            return false;
        }
    }

    if (rest) {
        *rest = text;
    }
    return true;
}

static int32_t get_numeric_variable(BasicState *state, char variable) {
    if (variable < 'A' || variable > 'Z') {
        return 0;
    }
    return state->numeric_variables[variable - 'A'];
}

static void set_numeric_variable(BasicState *state, char variable, int32_t value) {
    if (variable < 'A' || variable > 'Z') {
        return;
    }
    state->numeric_variables[variable - 'A'] = value;
}

static bool parse_string_literal(const char *text, const char **value_start, size_t *length, const char **rest) {
    text = skip_space(text);
    if (*text != '"') {
        return false;
    }
    text++;
    const char *end = text;
    while (*end != '\0' && *end != '"') {
        end++;
    }
    *value_start = text;
    *length = (size_t)(end - text);
    if (rest) {
        *rest = (*end == '"') ? end + 1 : end;
    }
    return true;
}

static bool is_array_defined(BasicState *state, char variable) {
    if (variable < 'A' || variable > 'Z') {
        return false;
    }
    return state->array_defined[variable - 'A'];
}

static bool get_array_element(BasicState *state, char variable, int32_t index, int32_t *value) {
    if (!is_array_defined(state, variable)) {
        return false;
    }
    size_t array_index = (size_t)index;
    size_t size = (size_t)state->array_sizes[variable - 'A'];
    if (index < 0 || array_index >= size) {
        return false;
    }
    *value = state->array_variables[variable - 'A'][array_index];
    return true;
}

static bool set_array_element(BasicState *state, char variable, int32_t index, int32_t value) {
    if (!is_array_defined(state, variable)) {
        return false;
    }
    size_t array_index = (size_t)index;
    size_t size = (size_t)state->array_sizes[variable - 'A'];
    if (index < 0 || array_index >= size) {
        return false;
    }
    state->array_variables[variable - 'A'][array_index] = value;
    return true;
}

static bool parse_array_index(BasicState *state, const char *text, int32_t *index, const char **rest) {
    text = skip_space(text);
    if (*text != '(') {
        if (rest) {
            *rest = text;
        }
        return false;
    }
    text++;

    if (!parse_expression(state, text, index, &text)) {
        return false;
    }

    text = skip_space(text);
    if (*text != ')') {
        return false;
    }
    text++;
    if (rest) {
        *rest = text;
    }
    return true;
}

static bool parse_data_item(BasicState *state, const char *text, bool *is_string, int32_t *int_value, const char **str_start, size_t *str_len, const char **rest) {
    text = skip_space(text);
    if (*text == '"') {
        if (!parse_string_literal(text, str_start, str_len, rest)) {
            return false;
        }
        *is_string = true;
        *int_value = 0;
        return true;
    }

    *is_string = false;
    if (!parse_expression(state, text, int_value, rest)) {
        return false;
    }
    return true;
}

static bool find_next_data_item(BasicState *state, bool *is_string, int32_t *int_value, const char **str_start, size_t *str_len) {
    size_t line_index = state->data_line_index;
    size_t item_offset = state->data_item_offset;

    while (line_index < state->program_line_count) {
        const char *cursor = skip_space(state->program_lines[line_index].text);
        if (strncasecmp(cursor, "DATA", 4) == 0 && (cursor[4] == '\0' || isspace((unsigned char)cursor[4]))) {
            cursor += 4;
            size_t current_item_index = 0;
            while (true) {
                cursor = skip_space(cursor);
                if (*cursor == '\0') {
                    break;
                }

                const char *item_rest = NULL;
                const char *item_str = NULL;
                size_t item_len = 0;
                int32_t item_value = 0;
                bool item_is_string = false;
                if (!parse_data_item(state, cursor, &item_is_string, &item_value, &item_str, &item_len, &item_rest)) {
                    return false;
                }

                if (item_offset == 0) {
                    *is_string = item_is_string;
                    *int_value = item_value;
                    *str_start = item_str;
                    *str_len = item_len;
                    if (*item_rest == ',') {
                        state->data_line_index = line_index;
                        state->data_item_offset = current_item_index + 1;
                        cursor = item_rest + 1;
                        while (*cursor == ' ' || *cursor == '\t') {
                            cursor++;
                        }
                        if (*cursor != '\0') {
                            return true;
                        }
                    }
                    state->data_line_index = line_index + 1;
                    state->data_item_offset = 0;
                    return true;
                }

                item_offset--;
                current_item_index++;
                if (*item_rest == ',') {
                    cursor = item_rest + 1;
                    continue;
                }
                break;
            }
        }

        line_index++;
        item_offset = 0;
    }

    return false;
}

static void restore_data_pointer(BasicState *state) {
    state->data_line_index = 0;
    state->data_item_offset = 0;
}

static bool push_for_frame(BasicState *state, char variable, int32_t limit, int32_t step, size_t for_line_index) {
    if (state->for_stack_top >= MAX_STACK_DEPTH) {
        return false;
    }
    state->for_stack[state->for_stack_top].variable = variable;
    state->for_stack[state->for_stack_top].limit = limit;
    state->for_stack[state->for_stack_top].step = step;
    state->for_stack[state->for_stack_top].for_line_index = for_line_index;
    state->for_stack_top += 1;
    return true;
}

static bool pop_for_frame(BasicState *state, ForFrame *frame) {
    if (state->for_stack_top == 0) {
        return false;
    }
    state->for_stack_top -= 1;
    if (frame) {
        *frame = state->for_stack[state->for_stack_top];
    }
    return true;
}

static bool peek_for_frame(BasicState *state, ForFrame *frame) {
    if (state->for_stack_top == 0) {
        return false;
    }
    if (frame) {
        *frame = state->for_stack[state->for_stack_top - 1];
    }
    return true;
}

static size_t find_matching_next_line_index(BasicState *state, size_t start_index) {
    size_t depth = 1;
    for (size_t i = start_index + 1; i < state->program_line_count; ++i) {
        const char *text = skip_space(state->program_lines[i].text);
        if (strncasecmp(text, "FOR", 3) == 0 && (text[3] == '\0' || isspace((unsigned char)text[3]))) {
            depth += 1;
            continue;
        }
        if (strncasecmp(text, "NEXT", 4) == 0 && (text[4] == '\0' || isspace((unsigned char)text[4]))) {
            depth -= 1;
            if (depth == 0) {
                return i;
            }
        }
    }
    return state->program_line_count;
}

static bool push_return_address(BasicState *state, size_t return_index) {
    if (state->execution_stack_top >= MAX_STACK_DEPTH) {
        return false;
    }
    state->execution_stack[state->execution_stack_top].type = VALUE_TYPE_INT;
    state->execution_stack[state->execution_stack_top].int_value = (int32_t)return_index;
    state->execution_stack_top += 1;
    return true;
}

static bool pop_return_address(BasicState *state, size_t *return_index) {
    if (state->execution_stack_top == 0) {
        return false;
    }
    state->execution_stack_top -= 1;
    Value value = state->execution_stack[state->execution_stack_top];
    if (value.type != VALUE_TYPE_INT) {
        return false;
    }
    *return_index = (size_t)value.int_value;
    return true;
}

static bool parse_factor(BasicState *state, const char *text, int32_t *value, const char **rest) {
    text = skip_space(text);
    if (*text == '(') {
        text++;
        if (!parse_expression(state, text, value, &text)) {
            return false;
        }
        if (*text != ')') {
            return false;
        }
        text++;
        if (rest) {
            *rest = text;
        }
        return true;
    }

    char name[3] = {0};
    bool is_string = false;
    const char *after = NULL;
    if (parse_variable_name(text, name, &is_string, &after)) {
        if (is_string) {
            return false;
        }

        const char *array_cursor = skip_space(after);
        if (*array_cursor == '(') {
            int32_t index = 0;
            const char *after_index = NULL;
            if (!parse_array_index(state, array_cursor, &index, &after_index)) {
                return false;
            }
            if (!get_array_element(state, name[0], index, value)) {
                return false;
            }
            if (rest) {
                *rest = after_index;
            }
            return true;
        }

        *value = get_numeric_variable(state, name[0]);
        if (rest) {
            *rest = after;
        }
        return true;
    }

    return parse_integer(text, value, rest);
}

static bool parse_term(BasicState *state, const char *text, int32_t *value, const char **rest) {
    if (!parse_factor(state, text, value, &text)) {
        return false;
    }

    while (true) {
        text = skip_space(text);
        if (*text != '*' && *text != '/') {
            break;
        }

        char op = *text++;
        int32_t rhs = 0;
        if (!parse_factor(state, text, &rhs, &text)) {
            return false;
        }

        if (op == '*') {
            *value *= rhs;
        } else if (op == '/') {
            if (rhs == 0) {
                return false;
            }
            *value /= rhs;
        }
    }

    if (rest) {
        *rest = text;
    }
    return true;
}

static bool parse_expression(BasicState *state, const char *text, int32_t *value, const char **rest) {
    text = skip_space(text);
    bool negative = false;
    if (*text == '+' || *text == '-') {
        negative = (*text == '-');
        text++;
    }

    if (!parse_term(state, text, value, &text)) {
        return false;
    }
    if (negative) {
        *value = -*value;
    }

    while (true) {
        text = skip_space(text);
        if (*text != '+' && *text != '-') {
            break;
        }
        char op = *text++;
        int32_t rhs = 0;
        if (!parse_term(state, text, &rhs, &text)) {
            return false;
        }
        if (op == '+') {
            *value += rhs;
        } else {
            *value -= rhs;
        }
    }

    if (rest) {
        *rest = text;
    }
    return true;
}

static bool parse_operand(BasicState *state, const char *text, int32_t *value, const char **rest) {
    text = skip_space(text);
    if (*text == '"') {
        const char *literal = NULL;
        size_t len = 0;
        if (!parse_string_literal(text, &literal, &len, rest)) {
            return false;
        }
        *value = 0;
        return true;
    }

    if (parse_expression(state, text, value, rest)) {
        return true;
    }

    return false;
}

static bool parse_print_item(BasicState *state, const char *text, const char **rest) {
    text = skip_space(text);
    if (*text == '\0') {
        if (rest) {
            *rest = text;
        }
        return true;
    }

    if (*text == '"') {
        const char *literal = NULL;
        size_t len = 0;
        if (!parse_string_literal(text, &literal, &len, &text)) {
            return false;
        }
        printf("%.*s", (int)len, literal);
        if (rest) {
            *rest = text;
        }
        return true;
    }

    if (isalpha((unsigned char)*text)) {
        char name[3] = {0};
        bool is_string = false;
        const char *after = NULL;
        if (parse_variable_name(text, name, &is_string, &after)) {
            if (is_string) {
                printf("%s", state->string_variables[name[0] - 'A']);
                if (rest) {
                    *rest = after;
                }
                return true;
            }
        }
    }

    int32_t value = 0;
    if (parse_expression(state, text, &value, &text)) {
        printf("%d", value);
        if (rest) {
            *rest = text;
        }
        return true;
    }

    return false;
}

static bool parse_relation(const char *text, char *relation, const char **rest) {
    text = skip_space(text);
    if (text[0] == '<') {
        if (text[1] == '>') {
            *relation = 'X';
            if (rest) *rest = text + 2;
            return true;
        }
        if (text[1] == '=') {
            *relation = 'L';
            if (rest) *rest = text + 2;
            return true;
        }
        *relation = '<';
        if (rest) *rest = text + 1;
        return true;
    }
    if (text[0] == '>') {
        if (text[1] == '=') {
            *relation = 'G';
            if (rest) *rest = text + 2;
            return true;
        }
        *relation = '>';
        if (rest) *rest = text + 1;
        return true;
    }
    if (text[0] == '=') {
        *relation = '=';
        if (rest) *rest = text + 1;
        return true;
    }
    return false;
}

static bool evaluate_condition(BasicState *state, const char *text, bool *result, const char **rest) {
    int32_t lhs = 0;
    const char *cursor = NULL;
    if (!parse_operand(state, text, &lhs, &cursor)) {
        return false;
    }

    char relation = 0;
    if (!parse_relation(cursor, &relation, &cursor)) {
        return false;
    }

    int32_t rhs = 0;
    if (!parse_operand(state, cursor, &rhs, &cursor)) {
        return false;
    }

    switch (relation) {
        case '=': *result = lhs == rhs; break;
        case '<': *result = lhs < rhs; break;
        case '>': *result = lhs > rhs; break;
        case 'L': *result = lhs <= rhs; break;
        case 'G': *result = lhs >= rhs; break;
        case 'X': *result = lhs != rhs; break;
        default: return false;
    }

    if (rest) {
        *rest = cursor;
    }
    return true;
}

static size_t find_program_line_index(BasicState *state, uint16_t line_number) {
    for (size_t i = 0; i < state->program_line_count; ++i) {
        if (state->program_lines[i].line_number == line_number) {
            return i;
        }
    }
    return state->program_line_count;
}

static bool execute_PRINT(BasicState *state, const char *statement) {
    const char *cursor = statement + 5;
    cursor = skip_space(cursor);
    bool newline = true;

    if (*cursor == '\0') {
        printf("\n");
        return false;
    }

    while (*cursor != '\0') {
        if (*cursor == ';') {
            newline = false;
            cursor++;
            continue;
        }
        if (*cursor == ',') {
            printf("        ");
            cursor++;
            continue;
        }

        if (!parse_print_item(state, cursor, &cursor)) {
            break;
        }

        cursor = skip_space(cursor);
        if (*cursor == ';') {
            newline = false;
            cursor++;
            continue;
        }
        if (*cursor == ',') {
            printf("        ");
            cursor++;
            continue;
        }
        break;
    }

    if (newline) {
        printf("\n");
    }
    return false;
}

static bool execute_LET(BasicState *state, const char *statement) {
    const char *cursor = statement + 3;
    char name[3] = {0};
    bool is_string = false;
    if (!parse_variable_name(cursor, name, &is_string, &cursor)) {
        return false;
    }

    cursor = skip_space(cursor);
    if (*cursor == '(') {
        int32_t index = 0;
        const char *after_index = NULL;
        if (!parse_array_index(state, cursor, &index, &after_index)) {
            return false;
        }
        cursor = skip_space(after_index);
        if (*cursor != '=') {
            return false;
        }
        cursor++;

        int32_t value = 0;
        if (!parse_expression(state, cursor, &value, &cursor)) {
            return false;
        }

        if (!set_array_element(state, name[0], index, value)) {
            return false;
        }
        return false;
    }

    cursor = skip_space(cursor);
    if (*cursor != '=') {
        return false;
    }
    cursor++;

    if (is_string) {
        const char *literal = NULL;
        size_t len = 0;
        if (!parse_string_literal(cursor, &literal, &len, &cursor)) {
            return false;
        }
        size_t copy_len = len < MAX_INPUT_LINE - 1 ? len : MAX_INPUT_LINE - 1;
        memcpy(state->string_variables[name[0] - 'A'], literal, copy_len);
        state->string_variables[name[0] - 'A'][copy_len] = '\0';
        return false;
    }

    int32_t value = 0;
    if (!parse_expression(state, cursor, &value, &cursor)) {
        return false;
    }

    set_numeric_variable(state, name[0], value);
    return false;
}

static bool execute_assignment(BasicState *state, const char *statement) {
    const char *cursor = statement;
    char name[3] = {0};
    bool is_string = false;
    if (!parse_variable_name(cursor, name, &is_string, &cursor)) {
        return false;
    }

    cursor = skip_space(cursor);
    if (*cursor == '(') {
        int32_t index = 0;
        const char *after_index = NULL;
        if (!parse_array_index(state, cursor, &index, &after_index)) {
            return false;
        }
        cursor = skip_space(after_index);
        if (*cursor != '=') {
            return false;
        }
        cursor++;

        int32_t value = 0;
        if (!parse_expression(state, cursor, &value, &cursor)) {
            return false;
        }

        if (!set_array_element(state, name[0], index, value)) {
            return false;
        }
        return true;
    }

    cursor = skip_space(cursor);
    if (*cursor != '=') {
        return false;
    }
    cursor++;

    if (is_string) {
        const char *literal = NULL;
        size_t len = 0;
        if (!parse_string_literal(cursor, &literal, &len, &cursor)) {
            return false;
        }
        size_t copy_len = len < MAX_INPUT_LINE - 1 ? len : MAX_INPUT_LINE - 1;
        memcpy(state->string_variables[name[0] - 'A'], literal, copy_len);
        state->string_variables[name[0] - 'A'][copy_len] = '\0';
        return true;
    }

    int32_t value = 0;
    if (!parse_expression(state, cursor, &value, &cursor)) {
        return false;
    }

    set_numeric_variable(state, name[0], value);
    return true;
}

static bool execute_DIM(BasicState *state, const char *statement) {
    const char *cursor = statement + 3;
    while (*cursor != '\0') {
        char name[3] = {0};
        bool is_string = false;
        if (!parse_variable_name(cursor, name, &is_string, &cursor) || is_string) {
            return false;
        }

        cursor = skip_space(cursor);
        if (*cursor != '(') {
            return false;
        }

        int32_t size = 0;
        if (!parse_array_index(state, cursor, &size, &cursor)) {
            return false;
        }

        if (size < 0 || size >= MAX_ARRAY_SIZE) {
            return false;
        }

        state->array_defined[name[0] - 'A'] = true;
        state->array_sizes[name[0] - 'A'] = size + 1;
        size_t array_size = (size_t)state->array_sizes[name[0] - 'A'];
        for (size_t i = 0; i < array_size; ++i) {
            state->array_variables[name[0] - 'A'][i] = 0;
        }

        cursor = skip_space(cursor);
        if (*cursor == ',') {
            cursor++;
            continue;
        }
        break;
    }
    return false;
}

static bool execute_FOR(BasicState *state, const char *statement) {
    const char *cursor = statement + 3;
    char variable[3] = {0};
    int32_t start_value = 0;
    int32_t limit_value = 0;
    int32_t step_value = 1;
    if (!parse_for_header(state, cursor, variable, &start_value, &limit_value, &step_value, &cursor)) {
        return false;
    }
    if (variable[0] < 'A' || variable[0] > 'Z') {
        return false;
    }

    set_numeric_variable(state, variable[0], start_value);
    size_t for_line_index = state->current_program_line_index;
    if (!push_for_frame(state, variable[0], limit_value, step_value, for_line_index)) {
        printf("FOR stack overflow\n");
        state->program_running = false;
        return false;
    }

    bool should_continue = true;
    if ((step_value > 0 && start_value > limit_value) || (step_value < 0 && start_value < limit_value)) {
        should_continue = false;
    }

    if (!should_continue) {
        size_t next_index = find_matching_next_line_index(state, for_line_index);
        state->current_program_line_index = next_index;
    }
    return false;
}

static bool execute_NEXT(BasicState *state, const char *statement) {
    const char *cursor = statement + 4;
    char variable[3] = {0};
    bool is_string = false;
    bool have_variable = false;
    if (parse_variable_name(cursor, variable, &is_string, &cursor)) {
        if (!is_string) {
            have_variable = true;
        }
    }

    ForFrame frame;
    if (!peek_for_frame(state, &frame)) {
        printf("NEXT without FOR\n");
        state->program_running = false;
        return false;
    }

    if (have_variable && variable[0] != frame.variable) {
        printf("NEXT variable mismatch\n");
        state->program_running = false;
        return false;
    }

    int32_t next_value = get_numeric_variable(state, frame.variable) + frame.step;
    set_numeric_variable(state, frame.variable, next_value);

    bool loop_again = (frame.step > 0) ? (next_value <= frame.limit) : (next_value >= frame.limit);
    if (loop_again) {
        state->current_program_line_index = frame.for_line_index + 1;
        return true;
    }

    pop_for_frame(state, NULL);
    return false;
}

static bool execute_GOTO(BasicState *state, const char *statement) {
    const char *cursor = statement + 4;
    int32_t line_number = 0;
    if (!parse_expression(state, cursor, &line_number, &cursor)) {
        state->program_running = false;
        return false;
    }

    if (line_number < 1) {
        state->program_running = false;
        return false;
    }

    size_t target = find_program_line_index(state, (uint16_t)line_number);
    if (target >= state->program_line_count) {
        printf("Line %d not found\n", (int)line_number);
        state->program_running = false;
        return false;
    }

    state->current_program_line_index = target;
    return true;
}

static bool execute_GOSUB(BasicState *state, const char *statement) {
    const char *cursor = statement + 6;
    int32_t line_number = 0;
    if (!parse_expression(state, cursor, &line_number, &cursor)) {
        state->program_running = false;
        return false;
    }

    if (line_number < 1) {
        state->program_running = false;
        return false;
    }

    size_t target = find_program_line_index(state, (uint16_t)line_number);
    if (target >= state->program_line_count) {
        printf("Line %d not found\n", (int)line_number);
        state->program_running = false;
        return false;
    }

    size_t return_index = state->current_program_line_index + 1;
    if (!push_return_address(state, return_index)) {
        printf("Stack overflow\n");
        state->program_running = false;
        return false;
    }

    state->current_program_line_index = target;
    return true;
}

static bool execute_ON(BasicState *state, const char *statement) {
    const char *cursor = statement + 2;
    int32_t index_value = 0;
    if (!parse_expression(state, cursor, &index_value, &cursor)) {
        state->program_running = false;
        return false;
    }

    cursor = skip_space(cursor);
    bool gosub = false;
    if (strncasecmp(cursor, "GOTO", 4) == 0) {
        gosub = false;
        cursor += 4;
    } else if (strncasecmp(cursor, "GOSUB", 5) == 0) {
        gosub = true;
        cursor += 5;
    } else {
        return false;
    }

    if (index_value <= 0) {
        return false;
    }

    size_t target_item = (size_t)(index_value - 1);
    size_t item_index = 0;

    while (*cursor != '\0') {
        cursor = skip_space(cursor);
        if (*cursor == '\0') {
            break;
        }

        int32_t line_number = 0;
        if (!parse_expression(state, cursor, &line_number, &cursor)) {
            return false;
        }

        if (line_number < 1) {
            return false;
        }

        if (item_index == target_item) {
            size_t target = find_program_line_index(state, (uint16_t)line_number);
            if (target >= state->program_line_count) {
                printf("Line %d not found\n", (int)line_number);
                state->program_running = false;
                return false;
            }
            if (gosub) {
                size_t return_index = state->current_program_line_index + 1;
                if (!push_return_address(state, return_index)) {
                    printf("Stack overflow\n");
                    state->program_running = false;
                    return false;
                }
            }
            state->current_program_line_index = target;
            return true;
        }

        item_index++;
        cursor = skip_space(cursor);
        if (*cursor == ',') {
            cursor++;
            continue;
        }
        break;
    }

    return false;
}

static bool execute_RETURN(BasicState *state) {
    size_t return_index = 0;
    if (!pop_return_address(state, &return_index)) {
        printf("RETURN without GOSUB\n");
        state->program_running = false;
        return false;
    }

    if (return_index > state->program_line_count) {
        state->program_running = false;
        return false;
    }

    state->current_program_line_index = return_index;
    return true;
}

static bool execute_DATA(BasicState *state, const char *statement) {
    (void)state;
    (void)statement;
    return false;
}

static bool execute_READ(BasicState *state, const char *statement) {
    const char *cursor = statement + 4;
    while (*cursor != '\0') {
        char name[3] = {0};
        bool is_string = false;
        const char *after = NULL;
        if (!parse_variable_name(cursor, name, &is_string, &after)) {
            return false;
        }

        cursor = skip_space(after);
        if (*cursor == ',') {
            cursor++;
        }

        bool data_is_string = false;
        int32_t data_value = 0;
        const char *data_str = NULL;
        size_t data_len = 0;
        if (!find_next_data_item(state, &data_is_string, &data_value, &data_str, &data_len)) {
            printf("Out of data\n");
            state->program_running = false;
            return false;
        }

        if (is_string) {
            size_t copy_len = data_len < MAX_INPUT_LINE - 1 ? data_len : MAX_INPUT_LINE - 1;
            memcpy(state->string_variables[name[0] - 'A'], data_str ? data_str : "", copy_len);
            state->string_variables[name[0] - 'A'][copy_len] = '\0';
        } else {
            set_numeric_variable(state, name[0], data_is_string ? 0 : data_value);
        }
    }
    return false;
}

static bool execute_RESTORE(BasicState *state) {
    restore_data_pointer(state);
    return false;
}

static bool execute_INPUT(BasicState *state, const char *statement) {
    const char *cursor = statement + 5;
    cursor = skip_space(cursor);

    const char *prompt = NULL;
    size_t prompt_len = 0;
    if (*cursor == '"') {
        if (!parse_string_literal(cursor, &prompt, &prompt_len, &cursor)) {
            return false;
        }
        cursor = skip_space(cursor);
        if (*cursor == ';' || *cursor == ',') {
            cursor++;
        }
    }

    if (prompt) {
        printf("%.*s ", (int)prompt_len, prompt);
    } else {
        printf("? ");
    }

    char input_line[MAX_INPUT_LINE];
    if (fgets(input_line, sizeof(input_line), stdin) == NULL) {
        state->program_running = false;
        return false;
    }

    char *line = input_line;
    char *newline = strchr(line, '\n');
    if (newline) {
        *newline = '\0';
    }

    while (*cursor != '\0') {
        char name[3] = {0};
        bool is_string = false;
        const char *after = NULL;
        if (!parse_variable_name(cursor, name, &is_string, &after)) {
            return false;
        }

        cursor = skip_space(after);
        if (*cursor == ',') {
            cursor++;
        }

        line = (char *)skip_space(line);
        if (is_string) {
            char *comma = strchr(line, ',');
            size_t copy_len = comma ? (size_t)(comma - line) : strlen(line);
            if (copy_len >= MAX_INPUT_LINE) {
                copy_len = MAX_INPUT_LINE - 1;
            }
            memcpy(state->string_variables[name[0] - 'A'], line, copy_len);
            state->string_variables[name[0] - 'A'][copy_len] = '\0';
            if (comma) {
                line = comma + 1;
                continue;
            }
            break;
        }

        char *endptr = NULL;
        long parsed = strtol(line, &endptr, 10);
        if (endptr == line) {
            parsed = 0;
        }
        set_numeric_variable(state, name[0], (int32_t)parsed);

        line = endptr;
        if (*line == ',') {
            line++;
            continue;
        }
        break;
    }

    return false;
}

static bool execute_IF(BasicState *state, const char *statement) {
    const char *cursor = statement + 2;
    bool condition = false;
    if (!evaluate_condition(state, cursor, &condition, &cursor)) {
        return false;
    }

    cursor = skip_space(cursor);
    if (strncasecmp(cursor, "THEN", 4) != 0) {
        return false;
    }
    cursor += 4;

    if (!condition) {
        return false;
    }

    uint16_t line_number = 0;
    if (!parse_integer(cursor, (int32_t *)&line_number, &cursor)) {
        state->program_running = false;
        return false;
    }

    size_t target = find_program_line_index(state, line_number);
    if (target >= state->program_line_count) {
        printf("Line %u not found\n", line_number);
        state->program_running = false;
        return false;
    }

    state->current_program_line_index = target;
    return true;
}

static bool execute_END(BasicState *state) {
    state->program_running = false;
    return false;
}

static bool execute_PLOT(BasicState *state, const char *statement) {
    const char *cursor = statement + 4;
    int32_t x = 0, y = 0, color = 0;
    if (!parse_expression(state, cursor, &x, &cursor)) {
        return false;
    }
    cursor = skip_space(cursor);
    if (*cursor != ',') {
        return false;
    }
    cursor++;
    if (!parse_expression(state, cursor, &y, &cursor)) {
        return false;
    }
    cursor = skip_space(cursor);
    if (*cursor == ',') {
        cursor++;
        if (!parse_expression(state, cursor, &color, &cursor)) {
            return false;
        }
    }
    printf("[PLOT] x=%d, y=%d, color=%d\n", (int)x, (int)y, (int)color);
    return false;
}

static bool execute_DRAW(BasicState *state, const char *statement) {
    const char *cursor = statement + 4;
    int32_t x = 0, y = 0, color = 0;
    if (!parse_expression(state, cursor, &x, &cursor)) {
        return false;
    }
    cursor = skip_space(cursor);
    if (*cursor != ',') {
        return false;
    }
    cursor++;
    if (!parse_expression(state, cursor, &y, &cursor)) {
        return false;
    }
    cursor = skip_space(cursor);
    if (*cursor == ',') {
        cursor++;
        if (!parse_expression(state, cursor, &color, &cursor)) {
            return false;
        }
    }
    printf("[DRAW] x=%d, y=%d, color=%d\n", (int)x, (int)y, (int)color);
    return false;
}

static bool execute_MOVE(BasicState *state, const char *statement) {
    const char *cursor = statement + 4;
    int32_t x = 0, y = 0;
    if (!parse_expression(state, cursor, &x, &cursor)) {
        return false;
    }
    cursor = skip_space(cursor);
    if (*cursor != ',') {
        return false;
    }
    cursor++;
    if (!parse_expression(state, cursor, &y, &cursor)) {
        return false;
    }
    printf("[MOVE] x=%d, y=%d\n", (int)x, (int)y);
    return false;
}

static bool execute_CIRCLE(BasicState *state, const char *statement) {
    const char *cursor = statement + 6;
    int32_t x = 0, y = 0, radius = 0, color = 0;
    if (!parse_expression(state, cursor, &x, &cursor)) {
        return false;
    }
    cursor = skip_space(cursor);
    if (*cursor != ',') {
        return false;
    }
    cursor++;
    if (!parse_expression(state, cursor, &y, &cursor)) {
        return false;
    }
    cursor = skip_space(cursor);
    if (*cursor != ',') {
        return false;
    }
    cursor++;
    if (!parse_expression(state, cursor, &radius, &cursor)) {
        return false;
    }
    cursor = skip_space(cursor);
    if (*cursor == ',') {
        cursor++;
        if (!parse_expression(state, cursor, &color, &cursor)) {
            return false;
        }
    }
    printf("[CIRCLE] x=%d, y=%d, radius=%d, color=%d\n", (int)x, (int)y, (int)radius, (int)color);
    return false;
}

static bool execute_FILL(BasicState *state, const char *statement) {
    const char *cursor = statement + 4;
    int32_t x = 0, y = 0, color = 0;
    if (!parse_expression(state, cursor, &x, &cursor)) {
        return false;
    }
    cursor = skip_space(cursor);
    if (*cursor != ',') {
        return false;
    }
    cursor++;
    if (!parse_expression(state, cursor, &y, &cursor)) {
        return false;
    }
    cursor = skip_space(cursor);
    if (*cursor == ',') {
        cursor++;
        if (!parse_expression(state, cursor, &color, &cursor)) {
            return false;
        }
    }
    printf("[FILL] x=%d, y=%d, color=%d\n", (int)x, (int)y, (int)color);
    return false;
}

static bool execute_LOCATE(BasicState *state, const char *statement) {
    const char *cursor = statement + 6;
    int32_t x = 0, y = 0;
    if (!parse_expression(state, cursor, &x, &cursor)) {
        return false;
    }
    cursor = skip_space(cursor);
    if (*cursor != ',') {
        return false;
    }
    cursor++;
    if (!parse_expression(state, cursor, &y, &cursor)) {
        return false;
    }
    printf("[LOCATE] x=%d, y=%d\n", (int)x, (int)y);
    return false;
}

static bool execute_MODE(BasicState *state, const char *statement) {
    const char *cursor = statement + 4;
    int32_t mode = 0;
    if (!parse_expression(state, cursor, &mode, &cursor)) {
        return false;
    }
    printf("[MODE] %d\n", (int)mode);
    return false;
}

static bool execute_CLS(BasicState *state) {
    (void)state;
    printf("[CLS] Screen cleared\n");
    return false;
}

static bool execute_PEN(BasicState *state, const char *statement) {
    const char *cursor = statement + 3;
    int32_t color = 0;
    if (!parse_expression(state, cursor, &color, &cursor)) {
        return false;
    }
    printf("[PEN] color=%d\n", (int)color);
    return false;
}

static bool execute_PAPER(BasicState *state, const char *statement) {
    const char *cursor = statement + 5;
    int32_t color = 0;
    if (!parse_expression(state, cursor, &color, &cursor)) {
        return false;
    }
    printf("[PAPER] color=%d\n", (int)color);
    return false;
}

static bool execute_INK(BasicState *state, const char *statement) {
    const char *cursor = statement + 3;
    int32_t color_idx = 0, color_val = 0;
    if (!parse_expression(state, cursor, &color_idx, &cursor)) {
        return false;
    }
    cursor = skip_space(cursor);
    if (*cursor == ',') {
        cursor++;
        if (!parse_expression(state, cursor, &color_val, &cursor)) {
            return false;
        }
        printf("[INK] color_index=%d, color_value=%d\n", (int)color_idx, (int)color_val);
    } else {
        printf("[INK] color=%d\n", (int)color_idx);
    }
    return false;
}

static bool execute_BORDER(BasicState *state, const char *statement) {
    const char *cursor = statement + 6;
    int32_t color = 0;
    if (!parse_expression(state, cursor, &color, &cursor)) {
        return false;
    }
    printf("[BORDER] color=%d\n", (int)color);
    return false;
}

static bool execute_PALETTE(BasicState *state, const char *statement) {
    (void)state;
    const char *cursor = statement + 7;
    cursor = skip_space(cursor);
    printf("[PALETTE] %s", (*cursor == '\0') ? "reset\n" : "custom\n");
    return false;
}

bool execute_statement(BasicState *state, const char *statement) {
    const char *cursor = skip_space(statement);
    if (*cursor == '\0') {
        return false;
    }

    if (toupper((unsigned char)cursor[0]) == 'R' && strncasecmp(cursor, "REM", 3) == 0) {
        return false;
    }

    if (strncasecmp(cursor, "PRINT", 5) == 0) {
        return execute_PRINT(state, cursor);
    }

    if (strncasecmp(cursor, "LET", 3) == 0) {
        return execute_LET(state, cursor);
    }

    if (strncasecmp(cursor, "DIM", 3) == 0) {
        return execute_DIM(state, cursor);
    }

    if (strncasecmp(cursor, "ON", 2) == 0) {
        return execute_ON(state, cursor);
    }

    if (strncasecmp(cursor, "GOTO", 4) == 0) {
        return execute_GOTO(state, cursor);
    }

    if (strncasecmp(cursor, "FOR", 3) == 0) {
        return execute_FOR(state, cursor);
    }

    if (strncasecmp(cursor, "NEXT", 4) == 0) {
        return execute_NEXT(state, cursor);
    }

    if (strncasecmp(cursor, "GOSUB", 5) == 0) {
        return execute_GOSUB(state, cursor);
    }

    if (strncasecmp(cursor, "RETURN", 6) == 0) {
        return execute_RETURN(state);
    }

    if (strncasecmp(cursor, "INPUT", 5) == 0) {
        return execute_INPUT(state, cursor);
    }

    if (strncasecmp(cursor, "DATA", 4) == 0) {
        return execute_DATA(state, cursor);
    }

    if (strncasecmp(cursor, "READ", 4) == 0) {
        return execute_READ(state, cursor);
    }

    if (strncasecmp(cursor, "RESTORE", 7) == 0) {
        return execute_RESTORE(state);
    }

    if (strncasecmp(cursor, "IF", 2) == 0) {
        return execute_IF(state, cursor);
    }

    if (strncasecmp(cursor, "END", 3) == 0) {
        return execute_END(state);
    }

    if (strncasecmp(cursor, "PLOT", 4) == 0) {
        return execute_PLOT(state, cursor);
    }

    if (strncasecmp(cursor, "DRAW", 4) == 0) {
        return execute_DRAW(state, cursor);
    }

    if (strncasecmp(cursor, "MOVE", 4) == 0) {
        return execute_MOVE(state, cursor);
    }

    if (strncasecmp(cursor, "CIRCLE", 6) == 0) {
        return execute_CIRCLE(state, cursor);
    }

    if (strncasecmp(cursor, "FILL", 4) == 0) {
        return execute_FILL(state, cursor);
    }

    if (strncasecmp(cursor, "LOCATE", 6) == 0) {
        return execute_LOCATE(state, cursor);
    }

    if (strncasecmp(cursor, "MODE", 4) == 0) {
        return execute_MODE(state, cursor);
    }

    if (strncasecmp(cursor, "CLS", 3) == 0) {
        return execute_CLS(state);
    }

    if (strncasecmp(cursor, "PEN", 3) == 0) {
        return execute_PEN(state, cursor);
    }

    if (strncasecmp(cursor, "PAPER", 5) == 0) {
        return execute_PAPER(state, cursor);
    }

    if (strncasecmp(cursor, "INK", 3) == 0) {
        return execute_INK(state, cursor);
    }

    if (strncasecmp(cursor, "BORDER", 6) == 0) {
        return execute_BORDER(state, cursor);
    }

    if (strncasecmp(cursor, "PALETTE", 7) == 0) {
        return execute_PALETTE(state, cursor);
    }

    if (execute_assignment(state, cursor)) {
        return false;
    }

    printf("Executing statement: %s", statement);
    if (statement[strlen(statement) - 1] != '\n') {
        printf("\n");
    }
    return false;
}
