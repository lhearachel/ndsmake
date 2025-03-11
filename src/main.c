/*
 * Copyright 2025 <lhearachel@proton.me>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#include <stdio.h>
#include <stdlib.h>

#include "layout.h"
#include "lexer.h"
#include "parser.h"

typedef struct {
    const char *p_line;
    int line_num;
    int pre_len;
    int sub_len;
    int post_len;
} ErrorCoord;

static String fload(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "cannot open file\n");
        exit(EXIT_FAILURE);
    }

    fseek(f, 0, SEEK_END);
    isize fsize = ftell(f);
    rewind(f);

    char *dest = malloc(fsize + 1);
    fread(dest, 1, fsize, f);
    fclose(f);

    dest[fsize] = '\0';
    return (String){
        .p = dest,
        .len = fsize,
    };
}

static ErrorCoord find_err(const char *source, int err_begin, int err_end)
{
    ErrorCoord err = {
        .p_line = source,
        .line_num = 0,
        .pre_len = 0,
        .sub_len = 0,
        .post_len = 0,
    };

    int i;
    for (i = 0; i < err_begin; i++) {
        if (source[i] == '\n') {
            err.p_line = source + i + 1;
            err.line_num++;
            err.pre_len = 0;
        } else {
            err.pre_len++;
        }
    }

    do {
        err.sub_len++;
        i++;
    } while (i < err_end);

    while (source[i++] != '\n') {
        err.post_len++;
    }

    return err;
}

static void report_err(const char *filename, const char *err_msg, const char *err_substr, ErrorCoord err)
{
    //               BOLD[file:line:char]    BOLD_RED[error:]        error message        BOLD_RED[token]
    fprintf(stderr, "\x1B[1m%s:%d:%d:\x1B[0m \x1B[1;31merror:\x1B[0m %s: \x1B[1;31m“%.*s”\x1B[0m\n",
            filename, err.line_num, err.pre_len + 1,
            err_msg,
            err.sub_len, err_substr);

    //               line_num | BOLD[line_to_err]BOLD_RED[err]BOLD[line_from_err]
    fprintf(stderr, "%5d | \x1B[1m%.*s\x1B[1;31m%.*s\x1B[0m\x1B[1m%.*s\x1B[0m\n",
            err.line_num,
            err.pre_len, err.p_line,
            err.sub_len, err.p_line + err.pre_len,
            err.post_len, err.p_line + err.pre_len + err.sub_len);

    // red squiggle under the error span
    fprintf(stderr, "      | ");
    for (int i = 0; i < err.pre_len; i++) {
        fputc(' ', stderr);
    }
    fputs("\x1B[1;31m", stderr);
    for (int i = 0; i < err.sub_len - 1; i++) {
        fputc('~', stderr);
    }
    fprintf(stderr, "^\x1B[0m\n");
}

static void report_lex_err(const char *filename, const char *source, LexResult *result)
{
    ErrorCoord err = find_err(source, result->err_begin, result->err_end);

    // bump the error length by 1 character for invalid filepaths, so that we highlight
    // the invalid token at the end.
    err.sub_len += (result->err_type == E_invalid_filepath);
    err.post_len -= (result->err_type == E_invalid_filepath);

    report_err(filename, lex_error_messages[result->err_type], source + result->err_begin, err);
}

static void report_parse_err(const char *filename, const char *source, ParseResult *parsed, LexResult *lexed)
{
    Token *bad_token = lexed->tokens + parsed->err_idx;
    ErrorCoord err = find_err(source, bad_token->begin, bad_token->begin + bad_token->len);

    report_err(filename, parse_error_messages[parsed->err_type], source + bad_token->begin, err);
}

int main(int argc, char **argv)
{
    if (argc == 1) {
        fprintf(stderr, "no file given\n");
    }

    const char *filename = argv[1];
    String source = fload(filename);

    LexResult lexed = lex(source.p, source.len);
    if (!lexed.ok) {
        report_lex_err(filename, source.p, &lexed);
        exit(EXIT_FAILURE);
    }

    ParseResult parsed = parse(lexed.tokens, lexed.len, source.p);
    if (!parsed.ok) {
        report_parse_err(filename, source.p, &parsed, &lexed);
        free(lexed.tokens);
        free(source.p);
        exit(EXIT_FAILURE);
    }
    free(lexed.tokens);
    free(source.p);

    LayoutResult laidout = compute_rom_layout(parsed.spec);
    ROMLayout *layout = laidout.layout;
    FilesysDirectory *directories = layout->filesystem->data;

    printf("== VIRTUAL FILESYSTEM ==\n");
    printf("Directory count: %d\n", directories[0].parent);
    for (u32 i = 0; i < directories[0].parent; i++) {
        printf("/%.*s -> d: 0x%X, f: 0x%04X\n", directories[i].full_name_len, directories[i].full_name, directories[i].dir_id, directories[i].first_file_id);

        Vector *children = &directories[i].children;
        for (u32 j = 0; j < children->len; j++) {
            FilesysNode *node = &((FilesysNode *)children->data)[j];
            printf("  - (%c) %.*s\n", node->is_subdir ? 'D' : 'F', node->name_len, node->name);
        }
    }

    printf("== MEMBER FILES ==\n");
    for (u32 i = 0; i < parsed.spec->len_files; i++) {
        File *file = &parsed.spec->files[i];
        printf("  - 0x%04X -> %s\n", file->filesys_id, file->target_path);
    }

    dspec(parsed.spec);
    dlayout(layout);
}
