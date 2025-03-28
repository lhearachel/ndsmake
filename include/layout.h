#ifndef LAYOUT_H
#define LAYOUT_H

#include "parser.h"
#include "vector.h"

typedef struct {
    u32 ram_load_address;
    u32 ram_main_address;
    u32 file_size;
    u32 callback_address;
    char **overlay_filenames;
    u32 num_overlays;
} ARMDefinitions;

typedef struct FilesysNode {
    u8 name_len : 7;
    u8 is_subdir : 1;
    u16 subdir_id;
    char *name;
} FilesysNode;

typedef struct FilesysDirectory {
    Vector children;

    char *full_name;
    u32 full_name_len;
    u16 first_file_id;
    u16 dir_id;
    u16 parent;
} FilesysDirectory;

typedef struct {
    ARMDefinitions arm9_defs;
    ARMDefinitions arm7_defs;
    Vector *filesystem;
    u32 fnt_size;
} ROMLayout;

typedef struct {
    ROMLayout *layout;
    u32 err_code;
} LayoutResult;

LayoutResult compute_rom_layout(ROMSpec *romspec);
void dlayout(ROMLayout *layout);

#endif // LAYOUT_H
