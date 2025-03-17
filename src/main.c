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
#include <string.h>

#include "errout.h"
#include "io.h"
#include "layout.h"
#include "lexer.h"
#include "parser.h"

static byte *makefnt(Vector *filesystem, u32 size);
static void makerom(const char *template, const char *banner, ROMSpec *spec, ROMLayout *layout);

int main(int argc, char **argv)
{
    if (argc == 1) {
        fprintf(stderr, "Usage: ndsmake TEMPLATE BANNER SPECFILE\n");
    }

    const char *template = argv[1];
    const char *banner = argv[2];
    const char *specfile = argv[3];
    String source = fload(specfile);

    LexResult lexed = lex(source.p, source.len);
    if (!lexed.ok) {
        report_lex_err(specfile, source.p, &lexed);
        exit(EXIT_FAILURE);
    }

    ParseResult parsed = parse(lexed.tokens, lexed.len, source.p);
    if (!parsed.ok) {
        report_parse_err(specfile, source.p, &parsed, &lexed);
        free(lexed.tokens);
        free(source.p);
        exit(EXIT_FAILURE);
    }
    free(lexed.tokens);
    free(source.p);

    LayoutResult laidout = compute_rom_layout(parsed.spec);
    makerom(template, banner, parsed.spec, laidout.layout);

    byte *fnt = makefnt(laidout.layout->filesystem, laidout.layout->fnt_size);
    FILE *fnt_f = fopen("fnt.sbin", "wb");
    fwrite(fnt, 1, laidout.layout->fnt_size, fnt_f);
    fclose(fnt_f);

    dspec(parsed.spec);
    dlayout(laidout.layout);
    free(fnt);
}

static u32 to512(u32 i)
{
    return (i + 511) & -512;
}

static u32 fsize(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "could not open file “%s”\n", filename);
        exit(EXIT_FAILURE);
    }

    fseek(f, 0, SEEK_END);
    u32 fsize = ftell(f);
    fclose(f);

    return fsize;
}

static u32 emit_file(u32 start, u16 filesys_id, const char *source_file, const char *target_file)
{
    u32 size = fsize(source_file);
    printf("%08X,%08X,FF,%04X,%s,%s\n", start, start + size, filesys_id, source_file, target_file ? target_file : "*");
    return to512(start + size);
}

static u32 emit_table(u32 start, const char *descriptor, u32 size)
{
    printf("%08X,%08X,FF,FFFF,%s,*\n", start, start + size, descriptor);
    return to512(start + size);
}

static inline void put_leword(byte *p, u32 v)
{
    p[0] = (v & 0x000000FF);
    p[1] = (v & 0x0000FF00) >> 8;
    p[2] = (v & 0x00FF0000) >> 16;
    p[3] = (v & 0xFF000000) >> 24;
}

static inline void put_lehalf(byte *p, u16 v)
{
    p[0] = (v & 0x00FF);
    p[1] = (v & 0xFF00) >> 8;
}

static byte *makefnt(Vector *filesystem, u32 size)
{
    // The FNT is composed of two sections: Headers and Contents. Each directory
    // in the filesystem contains one of each:
    //
    // struct Header {
    //     u32 offset_to_contents; // originates from the start of the FNT
    //     u16 first_file_id;      // includes files in sub-directories
    //     u16 parent_dir_id;      // for root, this is the count of directories
    // }
    //
    // struct Contents {
    //     u8   name_len : 7;
    //     u8   is_dir   : 1;
    //     char name[name_len];
    //     u16  dir_id; // only filled when is_dir is 1
    // }
    //
    // We know how large the Header section will be based on the total count of
    // directories; to build the full table, we thus keep two write-heads and
    // iteratively build both section-entries for each directory in tandem.

    byte *fnt = calloc(size, 1);
    byte *p_header = fnt;
    byte *p_contents = fnt + (8 * filesystem->len);

    for (u32 i = 0; i < filesystem->len; i++) { // i == 29
        FilesysDirectory *directory = get(filesystem, FilesysDirectory, i);
        put_leword(p_header, p_contents - fnt);
        put_lehalf(p_header + 4, directory->first_file_id);
        put_lehalf(p_header + 6, directory->parent);

        printf("dir: /%.*s\n", directory->full_name_len, directory->full_name);
        for (u32 j = 0; j < directory->children.len; j++) {
            FilesysNode *child = get(&directory->children, FilesysNode, j);
            printf("  - child: %.*s\n", child->name_len, child->name);

            u8 len_type = child->name_len | (child->is_subdir << 7);
            p_contents[0] = len_type;
            memcpy(p_contents + 1, child->name, child->name_len);

            p_contents += 1 + child->name_len;
            if (child->is_subdir) {
                put_lehalf(p_contents, child->subdir_id);
                p_contents += 2;
            }
        }

        p_header += 8;
        p_contents += 1; // single null-terminator ends the contents entry
    }

    return fnt;
}

static void makerom(const char *template, const char *banner, ROMSpec *spec, ROMLayout *layout)
{
    u32 cursor = 0, file_id = 0;

    cursor = emit_file(0, 0xFFFF, template, null);
    cursor = emit_file(cursor, 0xFFFF, spec->arm9.code_binary_fpath, null);
    if (layout->arm9_defs.num_overlays) {
        cursor = emit_file(cursor, 0xFFFF, spec->arm9.overlay_table_fpath, null);
        for (u32 i = 0; i < layout->arm9_defs.num_overlays; i++, file_id++) {
            cursor = emit_file(cursor, file_id, layout->arm9_defs.overlay_filenames[i], null);
        }
    }

    cursor = emit_file(cursor, 0xFFFF, spec->arm7.code_binary_fpath, null);
    if (layout->arm7_defs.num_overlays) {
        cursor = emit_file(cursor, 0xFFFF, spec->arm7.overlay_table_fpath, null);
        for (u32 i = 0; i < layout->arm7_defs.num_overlays; i++, file_id++) {
            cursor = emit_file(cursor, file_id, layout->arm7_defs.overlay_filenames[i], null);
        }
    }

    // TODO: Build actual FNT, FAT
    cursor = emit_table(cursor, "*FILENAMES*", layout->fnt_size);
    cursor = emit_table(cursor, "*FILEALLOC*", 8 * (file_id + spec->len_files));

    cursor = emit_file(cursor, 0xFFFF, banner, null);
    for (u32 i = 0; i < spec->len_files; i++) {
        File *file = (File *)spec->files + i;
        cursor = emit_file(cursor, file->filesys_id, file->source_path, file->target_path);
    }
}
