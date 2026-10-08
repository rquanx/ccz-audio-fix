#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <wchar.h>

#include "config.h"

#define MAX_CONFIG_SIZE 65536

static BOOL file_exists(const WCHAR *path) {
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES
        && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

BOOL path_join(
    WCHAR *output,
    DWORD capacity,
    const WCHAR *directory,
    const WCHAR *name
) {
    return _snwprintf_s(
        output,
        capacity,
        _TRUNCATE,
        L"%s\\%s",
        directory,
        name
    ) >= 0;
}

void path_remove_file_name(WCHAR *path) {
    WCHAR *cursor = path + lstrlenW(path);
    while (cursor > path) {
        cursor--;
        if (*cursor == L'\\' || *cursor == L'/') {
            *cursor = L'\0';
            return;
        }
    }
}

static void trim_value(WCHAR *value) {
    WCHAR *start = value;
    WCHAR *end;
    size_t length;

    while (*start == L' ' || *start == L'\t') {
        start++;
    }
    if (start != value) {
        MoveMemory(
            value,
            start,
            ((size_t)lstrlenW(start) + 1) * sizeof(WCHAR)
        );
    }
    end = value + lstrlenW(value);
    while (
        end > value
        && (
            end[-1] == L' '
            || end[-1] == L'\t'
            || end[-1] == L'\r'
            || end[-1] == L'\n'
        )
    ) {
        *--end = L'\0';
    }
    length = (size_t)lstrlenW(value);
    if (
        length >= 2
        && (
            (value[0] == L'"' && value[length - 1] == L'"')
            || (value[0] == L'\'' && value[length - 1] == L'\'')
        )
    ) {
        MoveMemory(
            value,
            value + 1,
            (length - 2) * sizeof(WCHAR)
        );
        value[length - 2] = L'\0';
    }
}

static BOOL read_file_bytes(
    const WCHAR *path,
    BYTE **bytes,
    DWORD *size
) {
    HANDLE file;
    DWORD bytes_read = 0;

    *bytes = NULL;
    *size = 0;
    file = CreateFileW(
        path,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (file == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    *size = GetFileSize(file, NULL);
    if (
        *size == INVALID_FILE_SIZE
        || *size == 0
        || *size > MAX_CONFIG_SIZE
    ) {
        CloseHandle(file);
        return FALSE;
    }
    *bytes = (BYTE *)HeapAlloc(GetProcessHeap(), 0, *size);
    if (
        *bytes == NULL
        || !ReadFile(file, *bytes, *size, &bytes_read, NULL)
        || bytes_read != *size
    ) {
        if (*bytes != NULL) {
            HeapFree(GetProcessHeap(), 0, *bytes);
            *bytes = NULL;
        }
        CloseHandle(file);
        return FALSE;
    }
    CloseHandle(file);
    return TRUE;
}

static WCHAR *decode_config(const BYTE *bytes, DWORD size) {
    const BYTE *encoded = bytes;
    DWORD encoded_size = size;
    UINT code_page = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    int characters;
    WCHAR *decoded;

    if (size >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        DWORD wide_characters = (size - 2) / sizeof(WCHAR);
        decoded = (WCHAR *)HeapAlloc(
            GetProcessHeap(),
            HEAP_ZERO_MEMORY,
            ((SIZE_T)wide_characters + 1) * sizeof(WCHAR)
        );
        if (decoded != NULL) {
            CopyMemory(
                decoded,
                bytes + 2,
                wide_characters * sizeof(WCHAR)
            );
        }
        return decoded;
    }
    if (
        size >= 3
        && bytes[0] == 0xEF
        && bytes[1] == 0xBB
        && bytes[2] == 0xBF
    ) {
        encoded += 3;
        encoded_size -= 3;
    }
    characters = MultiByteToWideChar(
        code_page,
        flags,
        (LPCCH)encoded,
        encoded_size,
        NULL,
        0
    );
    if (characters == 0) {
        code_page = CP_ACP;
        flags = 0;
        characters = MultiByteToWideChar(
            code_page,
            flags,
            (LPCCH)encoded,
            encoded_size,
            NULL,
            0
        );
    }
    if (characters == 0) {
        return NULL;
    }
    decoded = (WCHAR *)HeapAlloc(
        GetProcessHeap(),
        HEAP_ZERO_MEMORY,
        ((SIZE_T)characters + 1) * sizeof(WCHAR)
    );
    if (
        decoded == NULL
        || MultiByteToWideChar(
            code_page,
            flags,
            (LPCCH)encoded,
            encoded_size,
            decoded,
            characters
        ) != characters
    ) {
        if (decoded != NULL) {
            HeapFree(GetProcessHeap(), 0, decoded);
        }
        return NULL;
    }
    return decoded;
}

static BOOL read_configured_executable(
    const WCHAR *path,
    WCHAR *configured,
    DWORD capacity
) {
    BYTE *bytes;
    DWORD size;
    WCHAR *text;
    WCHAR *line;
    BOOL game_section = FALSE;
    BOOL found = FALSE;

    configured[0] = L'\0';
    if (!read_file_bytes(path, &bytes, &size)) {
        return FALSE;
    }
    text = decode_config(bytes, size);
    HeapFree(GetProcessHeap(), 0, bytes);
    if (text == NULL) {
        return FALSE;
    }

    line = text;
    while (*line != L'\0') {
        WCHAR *next = line;
        WCHAR *separator;
        while (*next != L'\0' && *next != L'\r' && *next != L'\n') {
            next++;
        }
        if (*next != L'\0') {
            *next++ = L'\0';
            while (*next == L'\r' || *next == L'\n') {
                next++;
            }
        }
        trim_value(line);
        if (line[0] == L'[') {
            game_section = lstrcmpiW(line, L"[game]") == 0;
        } else if (
            game_section
            && line[0] != L'\0'
            && line[0] != L';'
            && line[0] != L'#'
        ) {
            separator = wcschr(line, L'=');
            if (separator != NULL) {
                *separator = L'\0';
                trim_value(line);
                if (lstrcmpiW(line, L"exe") == 0) {
                    trim_value(separator + 1);
                    lstrcpynW(configured, separator + 1, capacity);
                    found = configured[0] != L'\0';
                    break;
                }
            }
        }
        line = next;
    }
    HeapFree(GetProcessHeap(), 0, text);
    return found;
}

static BOOL resolve_configured_game(
    WCHAR *game,
    DWORD capacity,
    const WCHAR *launcher_directory,
    BOOL *configured_present
) {
    WCHAR config[MAX_PATH];
    WCHAR configured[MAX_PATH];
    WCHAR candidate[MAX_PATH];
    BOOL absolute;

    if (!path_join(
            config,
            ARRAYSIZE(config),
            launcher_directory,
            L"ccz_audio_fix.ini"
        )) {
        return FALSE;
    }
    if (!read_configured_executable(
            config,
            configured,
            ARRAYSIZE(configured)
        )) {
        return FALSE;
    }
    *configured_present = TRUE;
    absolute = (
        (configured[0] != L'\0' && configured[1] == L':')
        || (configured[0] == L'\\' && configured[1] == L'\\')
    );
    if (absolute) {
        lstrcpynW(candidate, configured, ARRAYSIZE(candidate));
    } else if (!path_join(
            candidate,
            ARRAYSIZE(candidate),
            launcher_directory,
            configured
        )) {
        return FALSE;
    }
    if (!file_exists(candidate)) {
        return FALSE;
    }
    return GetFullPathNameW(candidate, capacity, game, NULL) > 0;
}

BOOL resolve_game_path(
    WCHAR *game,
    DWORD capacity,
    const WCHAR *launcher_directory
) {
    int argument_count = 0;
    LPWSTR *arguments = CommandLineToArgvW(
        GetCommandLineW(),
        &argument_count
    );
    BOOL resolved = FALSE;
    BOOL configured_present = FALSE;

    if (arguments != NULL && argument_count >= 2) {
        resolved = GetFullPathNameW(
            arguments[1],
            capacity,
            game,
            NULL
        ) > 0;
    } else if (resolve_configured_game(
            game,
            capacity,
            launcher_directory,
            &configured_present
        )) {
        resolved = TRUE;
    } else if (!configured_present) {
        resolved = path_join(
            game,
            capacity,
            launcher_directory,
            L"Ekd5.exe"
        );
    }
    if (arguments != NULL) {
        LocalFree(arguments);
    }
    return resolved && file_exists(game);
}
