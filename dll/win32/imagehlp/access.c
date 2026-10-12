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
#include <string.h>
#include "windef.h"
#include "winbase.h"
#include "winnt.h"
#include "winternl.h"
#include "winerror.h"
#include "winver.h"
#include "wine/debug.h"
#include "imagehlp.h"
#include "bind_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(imagehlp);

/***********************************************************************
 *           Data
 */
static LIST_ENTRY image_list = { &image_list, &image_list };


/***********************************************************************
 *		GetImageConfigInformation (IMAGEHLP.@)
 */
BOOL WINAPI GetImageConfigInformation(
  PLOADED_IMAGE LoadedImage,
  PIMAGE_LOAD_CONFIG_DIRECTORY ImageConfigInformation)
{
    struct image_file image;
    const DWORD prefix = FIELD_OFFSET(IMAGE_LOAD_CONFIG_DIRECTORY, SEHandlerTable);
    ULONG size;
    DWORD declared;
    ULONG_PTR offset;
    BYTE *data;

    if (!LoadedImage || !ImageConfigInformation || !LoadedImage->MappedAddress ||
        !IMAGEHLP_ParseFile(LoadedImage->MappedAddress, LoadedImage->SizeOfImage, &image) ||
        image.magic != IMAGE_NT_OPTIONAL_HDR_MAGIC)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    data = ImageDirectoryEntryToData(image.data, FALSE, IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG, &size);
    if (!data || size != prefix || (ULONG_PTR)data < (ULONG_PTR)image.data)
    {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    offset = (ULONG_PTR)data - (ULONG_PTR)image.data;
    if (offset > image.size || prefix > image.size - offset)
    {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    memcpy(&declared, data, sizeof(declared));
    if (declared && declared < prefix)
    {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    memcpy(ImageConfigInformation, data, prefix);
    return TRUE;
}

/***********************************************************************
 *		GetImageUnusedHeaderBytes (IMAGEHLP.@)
 */
DWORD WINAPI GetImageUnusedHeaderBytes(
  PLOADED_IMAGE LoadedImage,
  LPDWORD SizeUnusedHeaderBytes)
{
    struct image_file image;
    DWORD offset, size;

    if (!LoadedImage || !SizeUnusedHeaderBytes || !LoadedImage->MappedAddress ||
        !IMAGEHLP_ParseFile(LoadedImage->MappedAddress, LoadedImage->SizeOfImage, &image) ||
        !IMAGEHLP_HeaderSpace(&image, TRUE, &offset, &size))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    *SizeUnusedHeaderBytes = size;
    return offset;
}

/***********************************************************************
 *		ImageLoad (IMAGEHLP.@)
 */
PLOADED_IMAGE WINAPI ImageLoad(PCSTR dll_name, PCSTR dll_path)
{
    LOADED_IMAGE *image;

    TRACE("(%s, %s)\n", dll_name, dll_path);

    image = HeapAlloc(GetProcessHeap(), 0, sizeof(*image));
    if (!image) return NULL;

    if (!MapAndLoad(dll_name, dll_path, image, TRUE, TRUE))
    {
        HeapFree(GetProcessHeap(), 0, image);
        return NULL;
    }

    image->Links.Flink = image_list.Flink;
    image->Links.Blink = &image_list;
    image_list.Flink->Blink = &image->Links;
    image_list.Flink = &image->Links;

    return image;
}

/***********************************************************************
 *		ImageUnload (IMAGEHLP.@)
 */
BOOL WINAPI ImageUnload(PLOADED_IMAGE loaded_image)
{
    LIST_ENTRY *entry, *mark;
    PLOADED_IMAGE image;

    TRACE("(%p)\n", loaded_image);

    /* FIXME: do we really need to check this? */
    mark = &image_list;
    for (entry = mark->Flink; entry != mark; entry = entry->Flink)
    {
        image = CONTAINING_RECORD(entry, LOADED_IMAGE, Links);
        if (image == loaded_image)
            break;
    }

    if (entry == mark)
    {
        /* Not found */
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    entry->Blink->Flink = entry->Flink;
    entry->Flink->Blink = entry->Blink;

    UnMapAndLoad(loaded_image);
    HeapFree(GetProcessHeap(), 0, loaded_image);

    return TRUE;
}

/***********************************************************************
 *		MapAndLoad (IMAGEHLP.@)
 */
BOOL WINAPI MapAndLoad(PCSTR pszImageName, PCSTR pszDllPath, PLOADED_IMAGE pLoadedImage,
                       BOOL bDotDll, BOOL bReadOnly)
{
    CHAR szFileName[MAX_PATH];
    HANDLE hFile = INVALID_HANDLE_VALUE;
    HANDLE hFileMapping = NULL;
    PVOID mapping = NULL;
    PIMAGE_NT_HEADERS pNtHeader = NULL;
    struct image_file image;
    LARGE_INTEGER size;
    DWORD length, error;
    char *name = NULL;

    TRACE("(%s, %s, %p, %d, %d)\n",
          pszImageName, pszDllPath, pLoadedImage, bDotDll, bReadOnly);

    length = SearchPathA(pszDllPath, pszImageName, bDotDll ? ".DLL" : ".EXE",
                         sizeof(szFileName), szFileName, NULL);
    if (!length)
    {
        SetLastError(ERROR_FILE_NOT_FOUND);
        goto Error;
    }

    if (length >= sizeof(szFileName))
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        goto Error;
    }

    hFile = CreateFileA(szFileName,
                        GENERIC_READ | (bReadOnly ? 0 : GENERIC_WRITE),
                        FILE_SHARE_READ,
                        NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        WARN("CreateFile: Error = %ld\n", GetLastError());
        goto Error;
    }

    if (!GetFileSizeEx(hFile, &size)) goto Error;
    if (size.QuadPart > MAXDWORD)
    {
        SetLastError(ERROR_FILE_TOO_LARGE);
        goto Error;
    }

    hFileMapping = CreateFileMappingA(hFile, NULL, 
                                      (bReadOnly ? PAGE_READONLY : PAGE_READWRITE) | SEC_COMMIT,
                                      0, 0, NULL);
    if (!hFileMapping)
    {
        WARN("CreateFileMapping: Error = %ld\n", GetLastError());
        goto Error;
    }

    mapping = MapViewOfFile(hFileMapping, bReadOnly ? FILE_MAP_READ : FILE_MAP_WRITE, 0, 0, 0);
    if (!mapping)
    {
        WARN("MapViewOfFile: Error = %ld\n", GetLastError());
        goto Error;
    }
    if (!CloseHandle(hFileMapping)) goto Error;
    hFileMapping = NULL;

    if (!IMAGEHLP_ParseFile(mapping, size.LowPart, &image))
    {
        SetLastError(ERROR_BAD_FORMAT);
        goto Error;
    }
    if (image.magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC &&
        image.data[image.optional_offset + FIELD_OFFSET(IMAGE_OPTIONAL_HEADER32, MajorLinkerVersion)] < 3 &&
        image.data[image.optional_offset + FIELD_OFFSET(IMAGE_OPTIONAL_HEADER32, MinorLinkerVersion)] < 5)
    {
        SetLastError(ERROR_BAD_FORMAT);
        goto Error;
    }
    pNtHeader = (PIMAGE_NT_HEADERS)(image.data + image.nt_offset);
    name = HeapAlloc(GetProcessHeap(), 0, length + 1);
    if (!name)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        goto Error;
    }
    memcpy(name, szFileName, length + 1);
    pLoadedImage->ModuleName       = name;
    pLoadedImage->hFile            = hFile;
    pLoadedImage->MappedAddress    = mapping;
    pLoadedImage->FileHeader       = pNtHeader;
    pLoadedImage->Sections         = (PIMAGE_SECTION_HEADER)(image.data + image.sections_offset);
    pLoadedImage->NumberOfSections = pNtHeader->FileHeader.NumberOfSections;
    pLoadedImage->SizeOfImage      = size.LowPart;
    pLoadedImage->Characteristics  = pNtHeader->FileHeader.Characteristics;
    pLoadedImage->LastRvaSection   = pLoadedImage->Sections;

    pLoadedImage->fSystemImage     = FALSE; /* FIXME */
    pLoadedImage->fDOSImage        = FALSE; /* FIXME */
    pLoadedImage->fReadOnly        = !!bReadOnly;
    pLoadedImage->Version          = 1;

    pLoadedImage->Links.Flink      = &pLoadedImage->Links;
    pLoadedImage->Links.Blink      = &pLoadedImage->Links;

    SetLastError(ERROR_SUCCESS);
    return TRUE;

Error:
    error = GetLastError();
    HeapFree(GetProcessHeap(), 0, name);
    if (mapping) UnmapViewOfFile(mapping);
    if (hFileMapping) CloseHandle(hFileMapping);
    if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
    SetLastError(error);
    return FALSE;
}

/***********************************************************************
 *		SetImageConfigInformation (IMAGEHLP.@)
 */
BOOL WINAPI SetImageConfigInformation(
  PLOADED_IMAGE LoadedImage,
  PIMAGE_LOAD_CONFIG_DIRECTORY ImageConfigInformation)
{
    struct image_file image;
    IMAGE_DATA_DIRECTORY directory;
    const DWORD prefix = FIELD_OFFSET(IMAGE_LOAD_CONFIG_DIRECTORY, SEHandlerTable);
    DWORD error = GetLastError(), supplied, existing, offset, available;
    ULONG directory_size;
    ULONG_PTR source_offset;
    BYTE *data;
    BOOL result = FALSE;

    if (!LoadedImage || !ImageConfigInformation || !LoadedImage->MappedAddress || LoadedImage->fReadOnly ||
        !IMAGEHLP_ParseFile(LoadedImage->MappedAddress, LoadedImage->SizeOfImage, &image) ||
        image.magic != IMAGE_NT_OPTIONAL_HDR_MAGIC || image.directory_count <= IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG)
        goto done;
    memcpy(&supplied, &ImageConfigInformation->Size, sizeof(supplied));
    data = ImageDirectoryEntryToData(image.data, FALSE, IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG, &directory_size);
    if (data && directory_size == prefix && (ULONG_PTR)data >= (ULONG_PTR)image.data)
    {
        source_offset = (ULONG_PTR)data - (ULONG_PTR)image.data;
        if (source_offset > image.size || prefix > image.size - source_offset) goto done;
        memcpy(&existing, data, sizeof(existing));
        if (existing > supplied) goto done;
        if (existing == supplied)
        {
            if (!supplied) supplied = prefix;
            if (supplied > image.size - source_offset) goto done;
            memmove(data, ImageConfigInformation, supplied);
            result = TRUE;
            goto done;
        }
    }
    if (!IMAGEHLP_HeaderSpace(&image, TRUE, &offset, &available) ||
        prefix > available || offset > image.size ||
        sizeof(*ImageConfigInformation) > image.size - offset)
        goto done;
    memmove(image.data + offset, ImageConfigInformation, sizeof(*ImageConfigInformation));
    directory.VirtualAddress = offset;
    directory.Size = prefix;
    memcpy(image.data + image.directories_offset + IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG * sizeof(directory),
           &directory, sizeof(directory));
    result = TRUE;
done:
    SetLastError(error);
    return result;
}

/***********************************************************************
 *		UnMapAndLoad (IMAGEHLP.@)
 */
BOOL WINAPI UnMapAndLoad(PLOADED_IMAGE pLoadedImage)
{
    struct image_file image;
    DWORD error = ERROR_SUCCESS, saved = GetLastError(), old_checksum, checksum;

    HeapFree(GetProcessHeap(), 0, pLoadedImage->ModuleName);
    /* FIXME: MSDN states that a new checksum is computed and stored into the file */
    if (!pLoadedImage->fReadOnly && pLoadedImage->MappedAddress)
    {
        if (!IMAGEHLP_ParseFile(pLoadedImage->MappedAddress, pLoadedImage->SizeOfImage, &image) ||
            !CheckSumMappedFile(image.data, image.size, &old_checksum, &checksum))
            error = ERROR_BAD_FORMAT;
        else
        {
            memcpy(image.data + image.checksum_offset, &checksum, sizeof(checksum));
            if (!FlushViewOfFile(image.data, image.size)) error = GetLastError();
            if (pLoadedImage->hFile != INVALID_HANDLE_VALUE &&
                !FlushFileBuffers(pLoadedImage->hFile) && !error) error = GetLastError();
        }
    }
    if (pLoadedImage->MappedAddress && !UnmapViewOfFile(pLoadedImage->MappedAddress) && !error)
        error = GetLastError();
    if (pLoadedImage->hFile != INVALID_HANDLE_VALUE && !CloseHandle(pLoadedImage->hFile) && !error)
        error = GetLastError();
    SetLastError(error ? error : saved);
    return !error;
}
