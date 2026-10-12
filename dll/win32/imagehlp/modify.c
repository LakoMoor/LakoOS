/*
 *	IMAGEHLP library
 *
 *	Copyright 1998	Patrik Stridvall
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "windef.h"
#include "winbase.h"
#include "winternl.h"
#include "winerror.h"
#include "winver.h"
#include "wine/debug.h"
#include "imagehlp.h"
#include "bind_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(imagehlp);

static WORD CalcCheckSum(DWORD StartValue, LPVOID BaseAddress, DWORD WordCount);

static BOOL image_span(DWORD total, DWORD offset, SIZE_T size)
{
    return offset <= total && size <= total - offset;
}

BOOL IMAGEHLP_ParseFile(BYTE *data, DWORD size, struct image_file *image)
{
    IMAGE_DOS_HEADER dos;
    IMAGE_OPTIONAL_HEADER32 optional32;
    IMAGE_OPTIONAL_HEADER64 optional64;
    IMAGE_SECTION_HEADER section;
    DWORD signature, optional_size, i;
    SIZE_T fixed_size;

    memset(image, 0, sizeof(*image));
    if (!data || !image_span(size, 0, sizeof(dos))) return FALSE;
    memcpy(&dos, data, sizeof(dos));
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0) return FALSE;
    image->nt_offset = dos.e_lfanew;
    if (!image_span(size, image->nt_offset, sizeof(signature) + sizeof(image->header))) return FALSE;
    memcpy(&signature, data + image->nt_offset, sizeof(signature));
    if (signature != IMAGE_NT_SIGNATURE) return FALSE;
    memcpy(&image->header, data + image->nt_offset + sizeof(signature), sizeof(image->header));
    image->optional_offset = image->nt_offset + sizeof(signature) + sizeof(image->header);
    optional_size = image->header.SizeOfOptionalHeader;
    if (optional_size < sizeof(image->magic) || !image_span(size, image->optional_offset, optional_size)) return FALSE;
    memcpy(&image->magic, data + image->optional_offset, sizeof(image->magic));
    if (image->magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
    {
        fixed_size = FIELD_OFFSET(IMAGE_OPTIONAL_HEADER32, DataDirectory);
        if (optional_size < fixed_size) return FALSE;
        memset(&optional32, 0, sizeof(optional32));
        memcpy(&optional32, data + image->optional_offset, fixed_size);
        image->directory_count = optional32.NumberOfRvaAndSizes;
        image->headers_size = optional32.SizeOfHeaders;
        image->image_size = optional32.SizeOfImage;
        image->file_alignment = optional32.FileAlignment;
        image->image_base = optional32.ImageBase;
        image->checksum_offset = image->optional_offset + FIELD_OFFSET(IMAGE_OPTIONAL_HEADER32, CheckSum);
    }
    else if (image->magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        fixed_size = FIELD_OFFSET(IMAGE_OPTIONAL_HEADER64, DataDirectory);
        if (optional_size < fixed_size) return FALSE;
        memset(&optional64, 0, sizeof(optional64));
        memcpy(&optional64, data + image->optional_offset, fixed_size);
        image->directory_count = optional64.NumberOfRvaAndSizes;
        image->headers_size = optional64.SizeOfHeaders;
        image->image_size = optional64.SizeOfImage;
        image->file_alignment = optional64.FileAlignment;
        image->image_base = optional64.ImageBase;
        image->checksum_offset = image->optional_offset + FIELD_OFFSET(IMAGE_OPTIONAL_HEADER64, CheckSum);
    }
    else return FALSE;
    if (image->directory_count > (optional_size - fixed_size) / sizeof(IMAGE_DATA_DIRECTORY)) return FALSE;
    image->directories_offset = image->optional_offset + fixed_size;
    image->sections_offset = image->optional_offset + optional_size;
    if (!image_span(size, image->sections_offset, image->header.NumberOfSections * sizeof(section)) ||
        image->headers_size > size || image->sections_offset > image->headers_size ||
        image->header.NumberOfSections > (image->headers_size - image->sections_offset) / sizeof(section)) return FALSE;
    for (i = 0; i < image->header.NumberOfSections; ++i)
    {
        memcpy(&section, data + image->sections_offset + i * sizeof(section), sizeof(section));
        if (section.SizeOfRawData && !image_span(size, section.PointerToRawData, section.SizeOfRawData)) return FALSE;
        if ((ULONGLONG)section.VirtualAddress + section.Misc.VirtualSize > MAXDWORD ||
            (ULONGLONG)section.VirtualAddress + section.SizeOfRawData > MAXDWORD) return FALSE;
    }
    image->data = data;
    image->size = size;
    return TRUE;
}

static BOOL image_directory(const struct image_file *image, unsigned index, IMAGE_DATA_DIRECTORY *directory)
{
    memset(directory, 0, sizeof(*directory));
    if (index >= image->directory_count) return FALSE;
    memcpy(directory, image->data + image->directories_offset + index * sizeof(*directory), sizeof(*directory));
    return TRUE;
}

BOOL IMAGEHLP_HeaderSpace(const struct image_file *image, BOOL include_bound, DWORD *offset, DWORD *available)
{
    IMAGE_SECTION_HEADER section;
    IMAGE_DATA_DIRECTORY directory;
    DWORD start = image->sections_offset + image->header.NumberOfSections * sizeof(section);
    DWORD limit = image->headers_size, i, end;

    for (i = 0; i < image->header.NumberOfSections; ++i)
    {
        memcpy(&section, image->data + image->sections_offset + i * sizeof(section), sizeof(section));
        if (section.SizeOfRawData && section.PointerToRawData < limit) limit = section.PointerToRawData;
    }
    if (start > limit) return FALSE;
    for (i = 0; i < image->directory_count; ++i)
    {
        if (!include_bound && i == IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT) continue;
        image_directory(image, i, &directory);
        if (!directory.Size || directory.VirtualAddress >= limit) continue;
        end = directory.Size > limit - directory.VirtualAddress ? limit : directory.VirtualAddress + directory.Size;
        if (end > start) start = end;
    }
    *offset = start;
    *available = limit - start;
    return TRUE;
}

static BYTE *image_section_rva(const struct image_file *image, DWORD rva, SIZE_T size, DWORD *available)
{
    IMAGE_SECTION_HEADER section;
    DWORD i, offset, remaining;

    for (i = 0; i < image->header.NumberOfSections; ++i)
    {
        memcpy(&section, image->data + image->sections_offset + i * sizeof(section), sizeof(section));
        if (!section.SizeOfRawData || rva < section.VirtualAddress || rva - section.VirtualAddress >= section.SizeOfRawData) continue;
        offset = rva - section.VirtualAddress;
        remaining = section.SizeOfRawData - offset;
        if (size > remaining) return NULL;
        if (available) *available = remaining;
        return image->data + section.PointerToRawData + offset;
    }
    return NULL;
}

static BYTE *image_rva(const struct image_file *image, DWORD rva, SIZE_T size, DWORD *available)
{
    DWORD remaining;

    if (rva < image->headers_size)
    {
        remaining = image->headers_size - rva;
        if (size > remaining) return NULL;
        if (available) *available = remaining;
        return image->data + rva;
    }
    return image_section_rva(image, rva, size, available);
}

static const char *image_string(const struct image_file *image, DWORD rva)
{
    DWORD available;
    const char *value = (const char *)image_rva(image, rva, 1, &available);
    return value && memchr(value, 0, available) ? value : NULL;
}

struct bind_write
{
    DWORD offset;
    unsigned size;
    BYTE bytes[sizeof(ULONGLONG)];
};

struct bind_module
{
    struct bind_module *next, *forwarders;
    DWORD timestamp, name_offset;
    char name[1];
};

struct bind_context
{
    struct image_file image;
    struct bind_write *writes;
    SIZE_T write_count, write_capacity;
    struct bind_module *modules, **last_module;
    PIMAGEHLP_STATUS_ROUTINE callback;
    const char *name, *dll_path;
    DWORD flags, error, import_error, header_used;
    BOOL changed, successful, failed;
};

struct bind_lookup_frame
{
    struct bind_lookup_frame *parent;
    LOADED_IMAGE loaded;
    struct image_file image;
    const char *module;
    DWORD index;
    char forward_name[MAX_PATH];
};

struct bind_export
{
    ULONGLONG address, forwarder_address;
    DWORD index, name_index;
    BOOL forwarded;
};

static ULONG_PTR bind_complete_parameter(const struct bind_context *context)
{
    return context->successful;
}
static ULONGLONG bind_forwarder_link64(const struct bind_export *export, DWORD next)
{
    return (export->forwarder_address & ~((ULONGLONG)MAXDWORD)) | next;
}

static void bind_abort_import(struct bind_context *context, struct bind_module *module,
                               SIZE_T first_write, BOOL prior_changed);

static BOOL bind_error(struct bind_context *context, DWORD error)
{
    if (!context->error) context->error = error;
    return FALSE;
}

static BYTE *bind_import_rva(struct bind_context *context, DWORD rva, SIZE_T size, DWORD *available)
{
    BYTE *data = image_section_rva(&context->image, rva, size, available);

    if (!data && context->callback)
        context->callback(BindRvaToVaFailed, context->name, NULL, rva, 0);
    return data;
}

static const char *bind_import_string(struct bind_context *context, DWORD rva, DWORD prefix, BYTE **record)
{
    DWORD available;
    BYTE *data = bind_import_rva(context, rva, (SIZE_T)prefix + 1, &available);

    if (record) *record = data;
    if (!data)
    {
        if (context->callback) context->callback(BindRvaToVaFailed, context->name, NULL, rva, 0);
        return NULL;
    }
    if (!memchr(data + prefix, 0, available - prefix))
    {
        bind_error(context, ERROR_BAD_EXE_FORMAT);
        return NULL;
    }
    return (const char *)data + prefix;
}

static void bind_use_header(struct bind_context *context, DWORD offset, SIZE_T size)
{
    DWORD end;

    if (offset >= context->image.headers_size) return;
    end = size > context->image.headers_size - offset ? context->image.headers_size : offset + size;
    if (end > context->header_used) context->header_used = end;
}

static BOOL bind_place_table(struct bind_context *context, DWORD size, DWORD *offset)
{
    DWORD available;

    if (!IMAGEHLP_HeaderSpace(&context->image, FALSE, offset, &available))
        return bind_error(context, ERROR_BAD_EXE_FORMAT);
    if (context->header_used > *offset) *offset = context->header_used;
    if (size > MAXDWORD - *offset) return bind_error(context, ERROR_ARITHMETIC_OVERFLOW);
    return TRUE;
}

static void *bind_alloc(struct bind_context *context, SIZE_T size)
{
    void *memory = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
    if (!memory)
    {
        bind_error(context, ERROR_NOT_ENOUGH_MEMORY);
        if (context->callback) context->callback(BindOutOfMemory, context->name, NULL, 0, size);
    }
    return memory;
}

static BOOL bind_stage(struct bind_context *context, DWORD offset, const void *value, unsigned size)
{
    struct bind_write *writes;
    SIZE_T capacity, bytes;

    if (size > sizeof(context->writes->bytes) || !image_span(context->image.size, offset, size))
        return bind_error(context, ERROR_BAD_EXE_FORMAT);
    if (context->write_count == context->write_capacity)
    {
        if (context->write_capacity > ~(SIZE_T)0 / 2 / sizeof(*writes))
            return bind_error(context, ERROR_NOT_ENOUGH_MEMORY);
        capacity = context->write_capacity ? context->write_capacity * 2 : 32;
        bytes = capacity * sizeof(*writes);
        writes = context->writes ? HeapReAlloc(GetProcessHeap(), 0, context->writes, bytes) : HeapAlloc(GetProcessHeap(), 0, bytes);
        if (!writes)
        {
            if (context->callback) context->callback(BindOutOfMemory, context->name, NULL, 0, bytes);
            return bind_error(context, ERROR_NOT_ENOUGH_MEMORY);
        }
        context->writes = writes;
        context->write_capacity = capacity;
    }
    bind_use_header(context, offset, size);
    writes = &context->writes[context->write_count++];
    writes->offset = offset;
    writes->size = size;
    memcpy(writes->bytes, value, size);
    if (memcmp(context->image.data + offset, value, size)) context->changed = TRUE;
    return TRUE;
}

static BOOL bind_stage_thunk(struct bind_context *context, DWORD offset, ULONGLONG value)
{
    DWORD narrow;
    if (context->image.magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return bind_stage(context, offset, &value, sizeof(value));
    if (value > MAXDWORD) return bind_error(context, ERROR_ARITHMETIC_OVERFLOW);
    narrow = value;
    return bind_stage(context, offset, &narrow, sizeof(narrow));
}

static struct bind_module *bind_new_module(struct bind_context *context, const char *name, DWORD timestamp)
{
    struct bind_module *module;
    SIZE_T size = strlen(name);

    if (size > ~(SIZE_T)0 - sizeof(*module))
    {
        bind_error(context, ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    module = bind_alloc(context, sizeof(*module) + size);
    if (!module) return NULL;
    module->timestamp = timestamp;
    memcpy(module->name, name, size + 1);
    return module;
}

static void bind_free_modules(struct bind_module *module)
{
    struct bind_module *next;
    while (module)
    {
        next = module->next;
        bind_free_modules(module->forwarders);
        HeapFree(GetProcessHeap(), 0, module);
        module = next;
    }
}

static void bind_abort_import(struct bind_context *context, struct bind_module *module,
                               SIZE_T first_write, BOOL prior_changed)
{
    context->write_count = first_write;
    context->changed = prior_changed;
    module->timestamp = 0;
    bind_free_modules(module->forwarders);
    module->forwarders = NULL;
}

static BOOL bind_add_forwarder(struct bind_context *context, struct bind_module *module,
                               const char *name, DWORD timestamp)
{
    struct bind_module **next;
    for (next = &module->forwarders; *next; next = &(*next)->next)
        if (!stricmp((*next)->name, name)) return TRUE;
    *next = bind_new_module(context, name, timestamp);
    return *next != NULL;
}

static BOOL bind_close_image(struct bind_context *context, LOADED_IMAGE *image)
{
    DWORD saved = GetLastError();
    if (!UnMapAndLoad(image)) return bind_error(context, GetLastError());
    SetLastError(saved);
    return TRUE;
}

static BOOL bind_lookup_export(const struct image_file *image, const char *name, DWORD ordinal,
                               struct bind_export *result)
{
    IMAGE_DATA_DIRECTORY directory;
    IMAGE_EXPORT_DIRECTORY exports;
    BYTE *data, *functions, *names, *ordinals;
    DWORD index, name_rva, function_rva, i;
    WORD name_ordinal;

    memset(result, 0, sizeof(*result));
    if (!image_directory(image, IMAGE_DIRECTORY_ENTRY_EXPORT, &directory) ||
        directory.Size < sizeof(exports) || !(data = image_rva(image, directory.VirtualAddress, directory.Size, NULL))) return FALSE;
    memcpy(&exports, data, sizeof(exports));
    if (exports.NumberOfFunctions > image->size / sizeof(DWORD) || exports.NumberOfNames > image->size / sizeof(DWORD)) return FALSE;
    functions = image_rva(image, exports.AddressOfFunctions, exports.NumberOfFunctions * sizeof(DWORD), NULL);
    if (!functions) return FALSE;
    if (name)
    {
        names = image_rva(image, exports.AddressOfNames, exports.NumberOfNames * sizeof(DWORD), NULL);
        ordinals = image_rva(image, exports.AddressOfNameOrdinals, exports.NumberOfNames * sizeof(WORD), NULL);
        if (!names || !ordinals) return FALSE;
        for (i = 0; i < exports.NumberOfNames; ++i)
        {
            const char *export_name;
            memcpy(&name_rva, names + i * sizeof(DWORD), sizeof(name_rva));
            export_name = image_string(image, name_rva);
            if (!export_name) return FALSE;
            if (!strcmp(export_name, name)) break;
        }
        if (i == exports.NumberOfNames) return FALSE;
        memcpy(&name_ordinal, ordinals + i * sizeof(WORD), sizeof(name_ordinal));
        index = name_ordinal;
        result->name_index = i;
    }
    else
    {
        if (ordinal < exports.Base) return FALSE;
        index = ordinal - exports.Base;
    }
    if (index >= exports.NumberOfFunctions) return FALSE;
    memcpy(&function_rva, functions + index * sizeof(DWORD), sizeof(function_rva));
    if (!function_rva || image->image_base > ~(ULONGLONG)0 - function_rva) return FALSE;
    result->index = index;
    result->address = image->image_base + function_rva;
    if (function_rva >= directory.VirtualAddress && function_rva - directory.VirtualAddress < directory.Size)
    {
        const char *forwarder = image_string(image, function_rva), *separator;

        if (!forwarder || strlen(forwarder) >= directory.Size - (function_rva - directory.VirtualAddress) ||
            !(separator = strrchr(forwarder, '.')) || separator == forwarder || !separator[1]) return FALSE;
        result->forwarded = TRUE;
        result->forwarder_address = result->address;
    }
    return TRUE;
}

static BOOL bind_lookup(struct bind_context *context, const struct image_file *image,
                        const char *module_name, const char *name, DWORD ordinal,
                        struct bind_module *module, struct bind_export *result)
{
    struct bind_lookup_frame root, *current, *next, *previous;
    struct bind_export resolved;
    const char *forwarder, *separator;
    char *ordinal_end;
    SIZE_T length;
    BOOL ret = FALSE;

    memset(&root, 0, sizeof(root));
    root.image = *image;
    root.module = module_name;
    current = &root;
    for (;;)
    {
        if (!bind_lookup_export(&current->image, name, ordinal, &resolved)) goto done;
        current->index = resolved.index;
        for (previous = current->parent; previous; previous = previous->parent)
            if (previous->index == current->index && !stricmp(previous->module, current->module)) goto done;
        if (current == &root) *result = resolved;
        if (!resolved.forwarded || (context->flags & BIND_NO_BOUND_IMPORTS))
        {
            result->address = resolved.address;
            ret = TRUE;
            break;
        }
        forwarder = image_string(&current->image, (DWORD)(resolved.forwarder_address - current->image.image_base));
        separator = strrchr(forwarder, '.');
        length = separator - forwarder;
        next = bind_alloc(context, sizeof(*next));
        if (!next) goto done;
        if (length > sizeof(next->forward_name) - sizeof(".DLL"))
        {
            HeapFree(GetProcessHeap(), 0, next);
            goto done;
        }
        memcpy(next->forward_name, forwarder, length);
        next->forward_name[length] = 0;
        if (!strchr(next->forward_name, '.')) memcpy(next->forward_name + length, ".DLL", sizeof(".DLL"));
        name = separator + 1;
        ordinal = 0;
        if (*name == '#')
        {
            ordinal = strtoul(name + 1, &ordinal_end, 10);
            if (ordinal_end == name + 1 || *ordinal_end || ordinal > 0xffff)
            {
                HeapFree(GetProcessHeap(), 0, next);
                goto done;
            }
            name = NULL;
        }
        if (!MapAndLoad(next->forward_name, context->dll_path, &next->loaded, TRUE, TRUE))
        {
            HeapFree(GetProcessHeap(), 0, next);
            goto done;
        }
        if (!IMAGEHLP_ParseFile(next->loaded.MappedAddress, next->loaded.SizeOfImage, &next->image))
        {
            bind_close_image(context, &next->loaded);
            HeapFree(GetProcessHeap(), 0, next);
            goto done;
        }
        next->parent = current;
        next->module = next->loaded.ModuleName;
        current = next;
    }
    for (previous = current; previous != &root; previous = previous->parent)
        if (!bind_add_forwarder(context, module, previous->forward_name, previous->image.header.TimeDateStamp))
        {
            ret = FALSE;
            break;
        }
done:
    while (current != &root)
    {
        previous = current->parent;
        if (!bind_close_image(context, &current->loaded)) ret = FALSE;
        HeapFree(GetProcessHeap(), 0, current);
        current = previous;
    }
    return ret;
}

static BOOL bind_missing_module(struct bind_context *context, const char *name)
{
    struct bind_module *module;
    if (context->flags & BIND_NO_BOUND_IMPORTS) return TRUE;
    module = bind_new_module(context, name, 0);
    if (!module) return FALSE;
    *context->last_module = module;
    context->last_module = &module->next;
    return TRUE;
}

static BOOL bind_import(struct bind_context *context, DWORD descriptor_offset,
                        const IMAGE_IMPORT_DESCRIPTOR *descriptor)
{
    const char *dll_name;
    char full_path[MAX_PATH];
    LOADED_IMAGE loaded;
    struct image_file dependency;
    struct bind_module *module = NULL;
    struct bind_export resolved, previous_forwarder;
    BYTE *lookup, *iat, *name_data;
    ULONGLONG *thunks = NULL, thunk;
    DWORD lookup_bytes, index, width, count, slots, stamp, chain = MAXDWORD;
    DWORD last_forwarder = MAXDWORD, offset, path_length;
    DWORD lookup_rva = descriptor->OriginalFirstThunk ? descriptor->OriginalFirstThunk : descriptor->FirstThunk;
    BOOL ret = FALSE, failure = FALSE;
    BOOL prior_changed = context->changed;
    SIZE_T first_write = context->write_count;

    dll_name = bind_import_string(context, descriptor->Name, 0, NULL);
    if (!dll_name) return !context->error;
    bind_use_header(context, (DWORD)((const BYTE *)dll_name - context->image.data), strlen(dll_name) + 1);
    path_length = SearchPathA(context->dll_path, dll_name, NULL, sizeof(full_path), full_path, NULL);
    if (!path_length || path_length >= sizeof(full_path))
    {
        context->import_error = path_length ? ERROR_INSUFFICIENT_BUFFER : GetLastError();
        if (context->callback) context->callback(BindImportModuleFailed, context->name, dll_name, 0, 0);
        context->failed = TRUE;
        return bind_missing_module(context, dll_name);
    }
    if (!MapAndLoad(full_path, NULL, &loaded, TRUE, TRUE))
    {
        context->import_error = GetLastError();
        if (context->callback) context->callback(BindImportModuleFailed, context->name, dll_name, 0, 0);
        context->failed = TRUE;
        return bind_missing_module(context, dll_name);
    }
    context->import_error = ERROR_SUCCESS;
    if (!IMAGEHLP_ParseFile(loaded.MappedAddress, loaded.SizeOfImage, &dependency))
    {
        bind_error(context, ERROR_BAD_EXE_FORMAT);
        goto done;
    }
    if (context->callback) context->callback(BindImportModule, context->name, dll_name, 0, 0);
    if ((context->flags & BIND_NO_BOUND_IMPORTS) && descriptor->TimeDateStamp &&
        descriptor->TimeDateStamp != MAXDWORD && descriptor->TimeDateStamp == context->image.header.TimeDateStamp)
    {
        ret = TRUE;
        goto done;
    }
    width = context->image.magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC ? sizeof(IMAGE_THUNK_DATA64) : sizeof(IMAGE_THUNK_DATA32);
    lookup = bind_import_rva(context, lookup_rva, width, &lookup_bytes);
    if (!lookup)
    {
        ret = TRUE;
        goto done;
    }
    slots = lookup_bytes / width;
    for (count = 0; count < slots; ++count)
    {
        thunk = 0;
        memcpy(&thunk, lookup + count * width, width);
        if (!thunk) break;
    }
    if (count == slots)
    {
        bind_error(context, ERROR_BAD_EXE_FORMAT);
        goto done;
    }
    bind_use_header(context, (DWORD)(lookup - context->image.data), (count + 1) * width);
    if (!count)
    {
        ret = TRUE;
        goto done;
    }
    if (sizeof(*thunks) > ~(SIZE_T)0 / count)
    {
        bind_error(context, ERROR_NOT_ENOUGH_MEMORY);
        goto done;
    }
    thunks = bind_alloc(context, (SIZE_T)count * sizeof(*thunks));
    if (!thunks) goto done;
    module = bind_new_module(context, dll_name, dependency.header.TimeDateStamp);
    if (!module) goto done;
    for (index = 0; index < count; ++index)
    {
        ULONGLONG value;
        const char *procedure = NULL;
        char ordinal_name[32];
        DWORD ordinal = 0;
        WORD hint;
        BOOL missing_name = FALSE;

        thunk = 0;
        memcpy(&thunk, lookup + index * width, width);
        if (thunk & (width == sizeof(IMAGE_THUNK_DATA64) ? IMAGE_ORDINAL_FLAG64 : IMAGE_ORDINAL_FLAG32))
            ordinal = (WORD)thunk;
        else
        {
            if (thunk > MAXDWORD - FIELD_OFFSET(IMAGE_IMPORT_BY_NAME, Name))
            {
                bind_error(context, ERROR_BAD_EXE_FORMAT);
                goto done;
            }
            procedure = bind_import_string(context, (DWORD)thunk, FIELD_OFFSET(IMAGE_IMPORT_BY_NAME, Name), &name_data);
            if (!procedure)
            {
                if (context->error) goto done;
                missing_name = TRUE;
            }
        }
        if (procedure)
            bind_use_header(context, (DWORD)(name_data - context->image.data), FIELD_OFFSET(IMAGE_IMPORT_BY_NAME, Name) + strlen(procedure) + 1);
        if (missing_name || !bind_lookup(context, &dependency, loaded.ModuleName, procedure, ordinal, module, &resolved))
        {
            ULONG_PTR parameter;

            if (context->error) goto done;
            if (missing_name)
                parameter = FIELD_OFFSET(IMAGE_IMPORT_BY_NAME, Name);
            else
            {
                if (!procedure)
                {
                    sprintf(ordinal_name, "Ordinal%lx", ordinal);
                    procedure = ordinal_name;
                }
                parameter = (ULONG_PTR)procedure;
            }
            if (context->callback) context->callback(BindImportProcedureFailed, context->name, full_path, 0, parameter);
            failure = context->failed = TRUE;
            break;
        }
        if (!procedure)
        {
            sprintf(ordinal_name, "Ordinal%lx", resolved.index);
            procedure = ordinal_name;
        }
        else if (resolved.name_index <= 0xffff)
        {
            hint = resolved.name_index;
            if (!bind_stage(context, name_data - context->image.data, &hint, sizeof(hint))) goto done;
        }
        value = resolved.address;
        if (resolved.forwarded && (context->flags & BIND_NO_BOUND_IMPORTS))
        {
            if (chain == MAXDWORD) chain = index;
            if (last_forwarder != MAXDWORD)
                thunks[last_forwarder] = width == sizeof(IMAGE_THUNK_DATA64) ? bind_forwarder_link64(&previous_forwarder, index) : index;
            value = width == sizeof(IMAGE_THUNK_DATA64) ? bind_forwarder_link64(&resolved, MAXDWORD) : MAXDWORD;
            previous_forwarder = resolved;
            last_forwarder = index;
        }
        thunks[index] = value;
        if (context->callback)
        {
            if (resolved.forwarded && (context->flags & BIND_NO_BOUND_IMPORTS))
                context->callback(BindForwarderNOT, context->name, full_path, (ULONG_PTR)resolved.forwarder_address, (ULONG_PTR)procedure);
            else if (resolved.forwarded)
                context->callback(BindForwarder, context->name, procedure, (ULONG_PTR)resolved.address, (ULONG_PTR)procedure);
            else context->callback(BindImportProcedure, context->name, full_path, (ULONG_PTR)resolved.address, (ULONG_PTR)procedure);
        }
    }
    if (failure)
    {
        bind_abort_import(context, module, first_write, prior_changed);
    }
    else
    {
        iat = bind_import_rva(context, descriptor->FirstThunk, (count + 1) * width, NULL);
        if (!iat)
        {
            context->write_count = first_write;
            context->changed = prior_changed;
            context->failed = TRUE;
        }
        else
        {
            for (index = 0; index < count; ++index)
            {
                offset = (DWORD)(iat - context->image.data) + index * width;
                if (!bind_stage_thunk(context, offset, thunks[index])) goto done;
            }
            bind_use_header(context, (DWORD)(iat - context->image.data), (count + 1) * width);
            stamp = context->flags & BIND_NO_BOUND_IMPORTS ? context->image.header.TimeDateStamp : MAXDWORD;
            if (!bind_stage(context, descriptor_offset + FIELD_OFFSET(IMAGE_IMPORT_DESCRIPTOR, TimeDateStamp), &stamp, sizeof(stamp)) ||
                !bind_stage(context, descriptor_offset + FIELD_OFFSET(IMAGE_IMPORT_DESCRIPTOR, ForwarderChain), &chain, sizeof(chain))) goto done;
            context->successful = TRUE;
        }
    }
    if (!(context->flags & BIND_NO_BOUND_IMPORTS))
    {
        *context->last_module = module;
        context->last_module = &module->next;
        module = NULL;
    }
    ret = TRUE;
done:
    HeapFree(GetProcessHeap(), 0, thunks);
    bind_free_modules(module);
    if (!bind_close_image(context, &loaded)) ret = FALSE;
    return ret;
}

static BOOL bind_name_position(struct bind_context *context, struct bind_module *module, DWORD *cursor)
{
    SIZE_T length = strlen(module->name) + 1;

    if (*cursor > 0xffff || length > MAXDWORD - *cursor)
        return bind_error(context, ERROR_ARITHMETIC_OVERFLOW);
    module->name_offset = *cursor;
    *cursor += length;
    return TRUE;
}

static BOOL bind_layout_table(struct bind_context *context, DWORD records_size, DWORD *size)
{
    struct bind_module *module, *forwarder;
    DWORD cursor = records_size;

    for (module = context->modules; module; module = module->next)
    {
        if (!bind_name_position(context, module, &cursor)) return FALSE;
        for (forwarder = module->forwarders; forwarder; forwarder = forwarder->next)
            if (!bind_name_position(context, forwarder, &cursor)) return FALSE;
    }
    if (cursor > MAXDWORD - (sizeof(DWORD) - 1)) return bind_error(context, ERROR_ARITHMETIC_OVERFLOW);
    *size = (cursor + sizeof(DWORD) - 1) & ~(sizeof(DWORD) - 1);
    return TRUE;
}

static BOOL bind_make_table(struct bind_context *context, BYTE **table, DWORD *size)
{
    struct bind_module *module, *forwarder;
    SIZE_T records = 1;
    DWORD cursor, record_bytes, count;
    IMAGE_BOUND_IMPORT_DESCRIPTOR descriptor;
    IMAGE_BOUND_FORWARDER_REF reference;
    BYTE *data;

    for (module = context->modules; module; module = module->next)
    {
        if (records == MAXDWORD / sizeof(descriptor)) return bind_error(context, ERROR_ARITHMETIC_OVERFLOW);
        ++records;
        count = 0;
        for (forwarder = module->forwarders; forwarder; forwarder = forwarder->next)
        {
            if (++count > 0xffff || records == MAXDWORD / sizeof(descriptor))
                return bind_error(context, ERROR_ARITHMETIC_OVERFLOW);
            ++records;
        }
    }
    record_bytes = records * sizeof(descriptor);
    if (!context->modules) *size = record_bytes;
    else if (!bind_layout_table(context, record_bytes, size)) return FALSE;
    if (*size < record_bytes) return bind_error(context, ERROR_INVALID_DATA);
    data = bind_alloc(context, *size);
    if (!data) return FALSE;
    cursor = 0;
    for (module = context->modules; module; module = module->next)
    {
        count = 0;
        for (forwarder = module->forwarders; forwarder; forwarder = forwarder->next) ++count;
        if (module->name_offset < record_bytes || module->name_offset > 0xffff ||
            !image_span(*size, module->name_offset, strlen(module->name) + 1)) goto invalid;
        descriptor.TimeDateStamp = module->timestamp;
        descriptor.OffsetModuleName = module->name_offset;
        descriptor.NumberOfModuleForwarderRefs = count;
        memcpy(data + cursor, &descriptor, sizeof(descriptor));
        cursor += sizeof(descriptor);
        memcpy(data + module->name_offset, module->name, strlen(module->name) + 1);
        for (forwarder = module->forwarders; forwarder; forwarder = forwarder->next)
        {
            if (forwarder->name_offset < record_bytes || forwarder->name_offset > 0xffff ||
                !image_span(*size, forwarder->name_offset, strlen(forwarder->name) + 1)) goto invalid;
            reference.TimeDateStamp = forwarder->timestamp;
            reference.OffsetModuleName = forwarder->name_offset;
            reference.Reserved = 0;
            memcpy(data + cursor, &reference, sizeof(reference));
            cursor += sizeof(reference);
            memcpy(data + forwarder->name_offset, forwarder->name, strlen(forwarder->name) + 1);
        }
    }
    for (module = context->modules; module; module = module->next)
    {
        if (memcmp(data + module->name_offset, module->name, strlen(module->name) + 1)) goto invalid;
        for (forwarder = module->forwarders; forwarder; forwarder = forwarder->next)
            if (memcmp(data + forwarder->name_offset, forwarder->name, strlen(forwarder->name) + 1)) goto invalid;
    }
    *table = data;
    return TRUE;
invalid:
    HeapFree(GetProcessHeap(), 0, data);
    return bind_error(context, ERROR_INVALID_DATA);
}

static BOOL bind_shift_pointer(struct bind_context *context, DWORD *pointer, DWORD insert, DWORD delta)
{
    if (delta && *pointer && *pointer >= insert)
    {
        if (*pointer > context->image.size || *pointer > MAXDWORD - delta)
            return bind_error(context, ERROR_BAD_EXE_FORMAT);
        *pointer += delta;
    }
    return TRUE;
}

static BOOL bind_prepare_output(struct bind_context *context, const BYTE *table, DWORD table_size,
                                DWORD table_offset, BYTE **output, DWORD *output_size, DWORD *expanded_headers)
{
    const struct image_file *image = &context->image;
    IMAGE_SECTION_HEADER section;
    IMAGE_DATA_DIRECTORY directory;
    IMAGE_DEBUG_DIRECTORY debug;
    struct image_file result;
    struct bind_write *write;
    BYTE *data, *debug_data;
    DWORD i, insert = image->size, first_rva = image->image_size, delta = 0;
    DWORD headers = image->headers_size, needed, alignment, offset, checksum, old_checksum, symbols;
    SIZE_T j;

    if (!(context->flags & BIND_NO_BOUND_IMPORTS))
    {
        if (IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT >= image->directory_count)
            return bind_error(context, ERROR_BAD_EXE_FORMAT);
        if (table_offset > MAXDWORD - table_size) return bind_error(context, ERROR_ARITHMETIC_OVERFLOW);
        needed = table_offset + table_size;
        if (needed > headers)
        {
            alignment = image->file_alignment;
            if (!alignment || (alignment & (alignment - 1)) || needed > MAXDWORD - (alignment - 1))
                return bind_error(context, ERROR_BAD_EXE_FORMAT);
            headers = (needed + alignment - 1) & ~(alignment - 1);
        }
    }
    for (i = 0; i < image->header.NumberOfSections; ++i)
    {
        memcpy(&section, image->data + image->sections_offset + i * sizeof(section), sizeof(section));
        if (section.SizeOfRawData && section.PointerToRawData < insert) insert = section.PointerToRawData;
        if (section.VirtualAddress && section.VirtualAddress < first_rva) first_rva = section.VirtualAddress;
    }
    if (insert < image->headers_size) return bind_error(context, ERROR_BAD_EXE_FORMAT);
    if (headers > first_rva)
    {
        if (context->callback) context->callback(BindNoRoomInImage, context->name, NULL, 0, 0);
        return bind_error(context, ERROR_BUFFER_OVERFLOW);
    }
    if (headers > insert) delta = headers - insert;
    if (image->size > MAXDWORD - delta) return bind_error(context, ERROR_ARITHMETIC_OVERFLOW);
    *output_size = image->size + delta;
    data = bind_alloc(context, *output_size);
    if (!data) return FALSE;
    memcpy(data, image->data, image->size);
    for (j = 0; j < context->write_count; ++j)
    {
        write = &context->writes[j];
        offset = write->offset;
        if (offset < insert && write->size > insert - offset) goto bad_image;
        if (!image_span(image->size, offset, write->size)) goto bad_image;
        memcpy(data + offset, write->bytes, write->size);
    }
    if (delta) memmove(data + insert + delta, data + insert, image->size - insert);
    for (i = 0; i < image->header.NumberOfSections; ++i)
    {
        offset = image->sections_offset + i * sizeof(section);
        memcpy(&section, data + offset, sizeof(section));
        if (!bind_shift_pointer(context, &section.PointerToRawData, insert, delta) ||
            !bind_shift_pointer(context, &section.PointerToRelocations, insert, delta) ||
            !bind_shift_pointer(context, &section.PointerToLinenumbers, insert, delta)) goto failed;
        memcpy(data + offset, &section, sizeof(section));
    }
    symbols = image->header.PointerToSymbolTable;
    if (!bind_shift_pointer(context, &symbols, insert, delta)) goto failed;
    memcpy(data + image->nt_offset + sizeof(DWORD) + FIELD_OFFSET(IMAGE_FILE_HEADER, PointerToSymbolTable), &symbols, sizeof(symbols));
    offset = image->optional_offset + (image->magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC ?
             FIELD_OFFSET(IMAGE_OPTIONAL_HEADER64, SizeOfHeaders) : FIELD_OFFSET(IMAGE_OPTIONAL_HEADER32, SizeOfHeaders));
    memcpy(data + offset, &headers, sizeof(headers));
    if (delta && image_directory(image, IMAGE_DIRECTORY_ENTRY_SECURITY, &directory) && directory.VirtualAddress)
    {
        if (!image_span(image->size, directory.VirtualAddress, directory.Size) ||
            !bind_shift_pointer(context, &directory.VirtualAddress, insert, delta)) goto bad_image;
        memcpy(data + image->directories_offset + IMAGE_DIRECTORY_ENTRY_SECURITY * sizeof(directory), &directory, sizeof(directory));
    }
    if (!IMAGEHLP_ParseFile(data, *output_size, &result)) goto bad_image;
    if (delta && image_directory(&result, IMAGE_DIRECTORY_ENTRY_DEBUG, &directory) && directory.Size)
    {
        if (directory.Size % sizeof(debug) || !(debug_data = image_rva(&result, directory.VirtualAddress, directory.Size, NULL))) goto bad_image;
        for (offset = 0; offset < directory.Size; offset += sizeof(debug))
        {
            memcpy(&debug, debug_data + offset, sizeof(debug));
            if (debug.PointerToRawData && !image_span(image->size, debug.PointerToRawData, debug.SizeOfData)) goto bad_image;
            if (!bind_shift_pointer(context, &debug.PointerToRawData, insert, delta)) goto failed;
            memcpy(debug_data + offset, &debug, sizeof(debug));
        }
    }
    if (!(context->flags & BIND_NO_BOUND_IMPORTS))
    {
        if (table_size)
        {
            if (!image_span(headers, table_offset, table_size) || !image_span(*output_size, table_offset, table_size)) goto bad_image;
            memcpy(data + table_offset, table, table_size);
        }
        directory.VirtualAddress = table_size ? table_offset : 0;
        directory.Size = table_size;
        memcpy(data + image->directories_offset + IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT * sizeof(directory), &directory, sizeof(directory));
    }
    if (!CheckSumMappedFile(data, *output_size, &old_checksum, &checksum)) goto bad_image;
    memcpy(data + image->checksum_offset, &checksum, sizeof(checksum));
    *expanded_headers = headers > image->headers_size ? headers : 0;
    *output = data;
    return TRUE;
bad_image:
    bind_error(context, ERROR_BAD_EXE_FORMAT);
failed:
    HeapFree(GetProcessHeap(), 0, data);
    return FALSE;
}

static BOOL bind_write_file(struct bind_context *context, const char *path, const BYTE *output, DWORD output_size)
{
    HANDLE file;
    LARGE_INTEGER size, position;
    BYTE buffer[4096];
    DWORD offset = 0, requested, transferred, error = ERROR_SUCCESS;

    file = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return bind_error(context, GetLastError());
    if (!GetFileSizeEx(file, &size)) { error = GetLastError(); goto done; }
    if (size.QuadPart != context->image.size) { error = ERROR_FILE_INVALID; goto done; }
    while (offset < context->image.size)
    {
        requested = context->image.size - offset;
        if (requested > sizeof(buffer)) requested = sizeof(buffer);
        if (!ReadFile(file, buffer, requested, &transferred, NULL)) { error = GetLastError(); goto done; }
        if (transferred != requested || memcmp(buffer, context->image.data + offset, requested))
        {
            error = ERROR_FILE_INVALID;
            goto done;
        }
        offset += transferred;
    }
    position.QuadPart = 0;
    if (!SetFilePointerEx(file, position, NULL, FILE_BEGIN)) { error = GetLastError(); goto done; }
    offset = 0;
    while (offset < output_size)
    {
        if (!WriteFile(file, output + offset, output_size - offset, &transferred, NULL)) { error = GetLastError(); goto done; }
        if (!transferred) { error = ERROR_WRITE_FAULT; goto done; }
        offset += transferred;
    }
    if (!SetEndOfFile(file)) { error = GetLastError(); goto done; }
    if (!FlushFileBuffers(file)) error = GetLastError();
done:
    if (!CloseHandle(file) && !error) error = GetLastError();
    return !error || bind_error(context, error);
}

/***********************************************************************
 *		BindImage (IMAGEHLP.@)
 */
BOOL WINAPI BindImage(
  PCSTR ImageName, PCSTR DllPath, PCSTR SymbolPath)
{
  return BindImageEx(0, ImageName, DllPath, SymbolPath, NULL);
}

/***********************************************************************
 *		BindImageEx (IMAGEHLP.@)
 */
BOOL WINAPI BindImageEx(DWORD flags, const char *module, const char *dll_path,
        const char *symbol_path, PIMAGEHLP_STATUS_ROUTINE cb)
{
    struct bind_context context;
    LOADED_IMAGE loaded;
    IMAGE_DATA_DIRECTORY imports, old_directory;
    IMAGE_IMPORT_DESCRIPTOR descriptor;
    BYTE *source = NULL, *table = NULL, *output = NULL, *import_data, *old_table;
    char *path = NULL;
    DWORD table_size = 0, disk_size = 0, table_offset = 0, output_size = 0;
    DWORD expanded_headers = 0, offset, count;
    SIZE_T path_size;
    BOOL open = FALSE, ret = FALSE, terminated = FALSE, notify;

    TRACE("flags %#lx, module %s, dll_path %s, symbol_path %s, cb %p.\n",
          flags, debugstr_a(module), debugstr_a(dll_path), debugstr_a(symbol_path), cb);
    memset(&context, 0, sizeof(context));
    context.flags = flags;
    context.name = module;
    context.dll_path = dll_path;
    context.callback = cb;
    context.last_module = &context.modules;
    if (!MapAndLoad(module, dll_path, &loaded, TRUE, TRUE)) return FALSE;
    open = TRUE;
    if (!IMAGEHLP_ParseFile(loaded.MappedAddress, loaded.SizeOfImage, &context.image))
    {
        bind_error(&context, ERROR_BAD_EXE_FORMAT);
        goto done;
    }
    if (!loaded.ModuleName)
    {
        bind_error(&context, ERROR_NOT_ENOUGH_MEMORY);
        goto done;
    }
    path_size = strlen(loaded.ModuleName) + 1;
    path = bind_alloc(&context, path_size);
    source = bind_alloc(&context, context.image.size);
    if (!path || !source) goto done;
    memcpy(path, loaded.ModuleName, path_size);
    memcpy(source, context.image.data, context.image.size);
    context.image.data = source;
    if (!bind_close_image(&context, &loaded)) { open = FALSE; goto done; }
    open = FALSE;
    if (!image_directory(&context.image, IMAGE_DIRECTORY_ENTRY_IMPORT, &imports) || !imports.VirtualAddress || !imports.Size)
    {
        ret = TRUE;
        goto done;
    }
    import_data = image_rva(&context.image, imports.VirtualAddress, imports.Size, NULL);
    if (!import_data || imports.Size < sizeof(descriptor))
    {
        bind_error(&context, ERROR_BAD_EXE_FORMAT);
        goto done;
    }
    bind_use_header(&context, (DWORD)(import_data - context.image.data), imports.Size);
    count = imports.Size / sizeof(descriptor);
    for (offset = 0; offset < count; ++offset)
    {
        memcpy(&descriptor, import_data + offset * sizeof(descriptor), sizeof(descriptor));
        if (!descriptor.Name && !descriptor.FirstThunk)
        {
            terminated = TRUE;
            break;
        }
        if (!descriptor.Name || !descriptor.FirstThunk)
        {
            bind_error(&context, ERROR_BAD_EXE_FORMAT);
            goto done;
        }
        if (!bind_import(&context, import_data - context.image.data + offset * sizeof(descriptor), &descriptor)) goto done;
    }
    if (!terminated)
    {
        bind_error(&context, ERROR_BAD_EXE_FORMAT);
        goto done;
    }
    if (!(flags & BIND_NO_BOUND_IMPORTS))
    {
        if (!bind_make_table(&context, &table, &table_size)) goto done;
        disk_size = context.successful ? table_size : 0;
        if (disk_size && !bind_place_table(&context, disk_size, &table_offset)) goto done;
        image_directory(&context.image, IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT, &old_directory);
        if (disk_size != old_directory.Size || (disk_size && table_offset != old_directory.VirtualAddress)) context.changed = TRUE;
        if (disk_size && disk_size == old_directory.Size)
        {
            old_table = image_rva(&context.image, old_directory.VirtualAddress, disk_size, NULL);
            if (!old_table || memcmp(old_table, table, disk_size)) context.changed = TRUE;
        }
    }
    notify = flags & BIND_NO_BOUND_IMPORTS ? context.changed && !(flags & BIND_NO_UPDATE) : context.changed || context.failed;
    if (notify && cb) cb(BindImageModified, module, NULL, 0, 0);
    if (notify && !(flags & BIND_NO_UPDATE))
    {
        if (!bind_prepare_output(&context, table, disk_size, table_offset, &output, &output_size, &expanded_headers) ||
            !bind_write_file(&context, path, output, output_size)) goto done;
        if (expanded_headers && cb) cb(BindExpandFileHeaders, module, NULL, 0, expanded_headers);
    }
    if (notify && !(flags & BIND_NO_BOUND_IMPORTS) && cb)
        cb(BindImageComplete, module, NULL, (ULONG_PTR)table, bind_complete_parameter(&context));
    ret = TRUE;
done:
    if (open && !bind_close_image(&context, &loaded)) ret = FALSE;
    bind_free_modules(context.modules);
    HeapFree(GetProcessHeap(), 0, context.writes);
    HeapFree(GetProcessHeap(), 0, output);
    HeapFree(GetProcessHeap(), 0, table);
    HeapFree(GetProcessHeap(), 0, source);
    HeapFree(GetProcessHeap(), 0, path);
    if (context.error) ret = FALSE;
    SetLastError(context.error ? context.error : context.import_error);
    return ret;
}


/***********************************************************************
 *		CheckSum (internal)
 */
static WORD CalcCheckSum(
  DWORD StartValue, LPVOID BaseAddress, DWORD WordCount)
{
   LPWORD Ptr;
   DWORD Sum;
   DWORD i;

   Sum = StartValue;
   Ptr = (LPWORD)BaseAddress;
   for (i = 0; i < WordCount; i++)
     {
	Sum += *Ptr;
	if (HIWORD(Sum) != 0)
	  {
	     Sum = LOWORD(Sum) + HIWORD(Sum);
	  }
	Ptr++;
     }

   return (WORD)(LOWORD(Sum) + HIWORD(Sum));
}


/***********************************************************************
 *		CheckSumMappedFile (IMAGEHLP.@)
 */
PIMAGE_NT_HEADERS WINAPI CheckSumMappedFile(
  LPVOID BaseAddress, DWORD FileLength,
  LPDWORD HeaderSum, LPDWORD CheckSum)
{
  PIMAGE_NT_HEADERS header;
  DWORD CalcSum;
  DWORD HdrSum;

  TRACE("(%p, %ld, %p, %p)\n", BaseAddress, FileLength, HeaderSum, CheckSum);

  if (!BaseAddress || !FileLength)
  {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }

  CalcSum = CalcCheckSum(0, BaseAddress, FileLength / sizeof(WORD));
  if (FileLength & 1)
  {
    CalcSum += ((BYTE *)BaseAddress)[FileLength - 1];
    CalcSum = LOWORD(CalcSum) + HIWORD(CalcSum);
  }
  header = RtlImageNtHeader(BaseAddress);

  if (!header)
  {
    *HeaderSum = 0;
    *CheckSum = CalcSum + FileLength;
    return NULL;
  }

  *HeaderSum = HdrSum = header->OptionalHeader.CheckSum;

  /* Subtract image checksum from calculated checksum. */
  /* fix low word of checksum */
  if (LOWORD(CalcSum) >= LOWORD(HdrSum))
  {
    CalcSum -= LOWORD(HdrSum);
  }
  else
  {
    CalcSum = ((LOWORD(CalcSum) - LOWORD(HdrSum)) & 0xFFFF) - 1;
  }

   /* fix high word of checksum */
  if (LOWORD(CalcSum) >= HIWORD(HdrSum))
  {
    CalcSum -= HIWORD(HdrSum);
  }
  else
  {
    CalcSum = ((LOWORD(CalcSum) - HIWORD(HdrSum)) & 0xFFFF) - 1;
  }

  /* add file length */
  CalcSum += FileLength;

  *CheckSum = CalcSum;

  return header;
}

/***********************************************************************
 *		MapFileAndCheckSumA (IMAGEHLP.@)
 */
DWORD WINAPI MapFileAndCheckSumA(
  PCSTR Filename, PDWORD HeaderSum, PDWORD CheckSum)
{
  HANDLE hFile;
  HANDLE hMapping;
  LPVOID BaseAddress;
  DWORD FileLength;

  TRACE("(%s, %p, %p): stub\n",
    debugstr_a(Filename), HeaderSum, CheckSum
  );

  hFile = CreateFileA(Filename,
		      GENERIC_READ,
		      FILE_SHARE_READ | FILE_SHARE_WRITE,
		      NULL,
		      OPEN_EXISTING,
		      FILE_ATTRIBUTE_NORMAL,
		      0);
  if (hFile == INVALID_HANDLE_VALUE)
  {
    return CHECKSUM_OPEN_FAILURE;
  }

  hMapping = CreateFileMappingW(hFile,
			       NULL,
			       PAGE_READONLY,
			       0,
			       0,
			       NULL);
  if (hMapping == 0)
  {
    CloseHandle(hFile);
    return CHECKSUM_MAP_FAILURE;
  }

  BaseAddress = MapViewOfFile(hMapping,
			      FILE_MAP_READ,
			      0,
			      0,
			      0);
  if (BaseAddress == 0)
  {
    CloseHandle(hMapping);
    CloseHandle(hFile);
    return CHECKSUM_MAPVIEW_FAILURE;
  }

  FileLength = GetFileSize(hFile,
			   NULL);

  CheckSumMappedFile(BaseAddress,
		     FileLength,
		     HeaderSum,
		     CheckSum);

  UnmapViewOfFile(BaseAddress);
  CloseHandle(hMapping);
  CloseHandle(hFile);

  return 0;
}

/***********************************************************************
 *		MapFileAndCheckSumW (IMAGEHLP.@)
 */
DWORD WINAPI MapFileAndCheckSumW(
  PCWSTR Filename, PDWORD HeaderSum, PDWORD CheckSum)
{
  HANDLE hFile;
  HANDLE hMapping;
  LPVOID BaseAddress;
  DWORD FileLength;

  TRACE("(%s, %p, %p): stub\n",
    debugstr_w(Filename), HeaderSum, CheckSum
  );

  hFile = CreateFileW(Filename,
		      GENERIC_READ,
		      FILE_SHARE_READ | FILE_SHARE_WRITE,
		      NULL,
		      OPEN_EXISTING,
		      FILE_ATTRIBUTE_NORMAL,
		      0);
  if (hFile == INVALID_HANDLE_VALUE)
  {
  return CHECKSUM_OPEN_FAILURE;
  }

  hMapping = CreateFileMappingW(hFile,
			       NULL,
			       PAGE_READONLY,
			       0,
			       0,
			       NULL);
  if (hMapping == 0)
  {
    CloseHandle(hFile);
    return CHECKSUM_MAP_FAILURE;
  }

  BaseAddress = MapViewOfFile(hMapping,
			      FILE_MAP_READ,
			      0,
			      0,
			      0);
  if (BaseAddress == 0)
  {
    CloseHandle(hMapping);
    CloseHandle(hFile);
    return CHECKSUM_MAPVIEW_FAILURE;
  }

  FileLength = GetFileSize(hFile,
			   NULL);

  CheckSumMappedFile(BaseAddress,
		     FileLength,
		     HeaderSum,
		     CheckSum);

  UnmapViewOfFile(BaseAddress);
  CloseHandle(hMapping);
  CloseHandle(hFile);

  return 0;
}

/***********************************************************************
 *		ReBaseImage (IMAGEHLP.@)
 */
BOOL WINAPI ReBaseImage(
  PCSTR CurrentImageName, PCSTR SymbolPath, BOOL fReBase,
  BOOL fRebaseSysfileOk, BOOL fGoingDown, ULONG CheckImageSize,
  ULONG *OldImageSize, ULONG_PTR *OldImageBase, ULONG *NewImageSize,
  ULONG_PTR *NewImageBase, ULONG TimeStamp)
{
  FIXME(
    "(%s, %s, %d, %d, %d, %ld, %p, %p, %p, %p, %ld): stub\n",
      debugstr_a(CurrentImageName),debugstr_a(SymbolPath), fReBase,
      fRebaseSysfileOk, fGoingDown, CheckImageSize, OldImageSize,
      OldImageBase, NewImageSize, NewImageBase, TimeStamp
  );
  SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
  return FALSE;
}

/***********************************************************************
 *		ReBaseImage64 (IMAGEHLP.@)
 */
BOOL WINAPI ReBaseImage64(
  const char *image_name, const char *symbol_path, BOOL rebase,
  BOOL rebase_sys, BOOL down, ULONG max_image_size, ULONG *old_image_size,
  ULONG64 *old_image_base, ULONG *new_image_size, ULONG64 *new_image_base,
  ULONG timestamp)
{
  FIXME("(%s, %s, %d, %d, %d, %ld, %p, %p, %p, %p, %ld): stub\n",
        debugstr_a(image_name), debugstr_a(symbol_path), rebase, rebase_sys,
        down, max_image_size, old_image_size, old_image_base, new_image_size,
        new_image_base, timestamp);
  SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
  return FALSE;
}

/***********************************************************************
 *		RemovePrivateCvSymbolic (IMAGEHLP.@)
 */
BOOL WINAPI RemovePrivateCvSymbolic(
  PCHAR DebugData, PCHAR *NewDebugData, ULONG *NewDebugSize)
{
  FIXME("(%p, %p, %p): stub\n",
    DebugData, NewDebugData, NewDebugSize
  );
  SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
  return FALSE;
}

/***********************************************************************
 *		RemoveRelocations (IMAGEHLP.@)
 */
VOID WINAPI RemoveRelocations(PCHAR ImageName)
{
  FIXME("(%p): stub\n", ImageName);
  SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
}

/***********************************************************************
 *		SplitSymbols (IMAGEHLP.@)
 */
BOOL WINAPI SplitSymbols(
  PSTR ImageName, PCSTR SymbolsPath,
  PSTR SymbolFilePath, ULONG Flags)
{
  FIXME("(%s, %s, %s, %ld): stub\n",
    debugstr_a(ImageName), debugstr_a(SymbolsPath),
    debugstr_a(SymbolFilePath), Flags
  );
  SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
  return FALSE;
}

/***********************************************************************
 *		UpdateDebugInfoFile (IMAGEHLP.@)
 */
BOOL WINAPI UpdateDebugInfoFile(
  PCSTR ImageFileName, PCSTR SymbolPath,
  PSTR DebugFilePath, PIMAGE_NT_HEADERS32 NtHeaders)
{
  FIXME("(%s, %s, %s, %p): stub\n",
    debugstr_a(ImageFileName), debugstr_a(SymbolPath),
    debugstr_a(DebugFilePath), NtHeaders
  );
  SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
  return FALSE;
}

/***********************************************************************
 *		UpdateDebugInfoFileEx (IMAGEHLP.@)
 */
BOOL WINAPI UpdateDebugInfoFileEx(
  PCSTR ImageFileName, PCSTR SymbolPath, PSTR DebugFilePath,
  PIMAGE_NT_HEADERS32 NtHeaders, DWORD OldChecksum)
{
  FIXME("(%s, %s, %s, %p, %ld): stub\n",
    debugstr_a(ImageFileName), debugstr_a(SymbolPath),
    debugstr_a(DebugFilePath), NtHeaders, OldChecksum
  );
  SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
  return FALSE;
}
