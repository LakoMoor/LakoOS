/*
 * PROJECT:     LakoOS Image Helper
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Bounded file-image access for image binding
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#ifndef LAKOOS_IMAGEHLP_BIND_PRIVATE_H
#define LAKOOS_IMAGEHLP_BIND_PRIVATE_H

struct image_file
{
    BYTE *data;
    DWORD size, nt_offset, optional_offset, sections_offset;
    DWORD directories_offset, directory_count, checksum_offset;
    DWORD headers_size, image_size, file_alignment;
    ULONGLONG image_base;
    IMAGE_FILE_HEADER header;
    WORD magic;
};

BOOL IMAGEHLP_ParseFile(BYTE *data, DWORD size, struct image_file *image);
BOOL IMAGEHLP_HeaderSpace(const struct image_file *image, BOOL include_bound, DWORD *offset, DWORD *available);

#endif
