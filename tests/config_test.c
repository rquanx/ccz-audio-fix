#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

#include "../src/launcher/config.h"

static BOOL write_bytes(
    const WCHAR *path,
    const BYTE *data,
    DWORD size
) {
    HANDLE file = CreateFileW(
        path,
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    DWORD written = 0;
    BOOL success;

    if (file == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    success = WriteFile(file, data, size, &written, NULL)
        && written == size;
    CloseHandle(file);
    return success;
}

int wmain(void) {
    static const BYTE utf8_config[] = {
        '[', 'g', 'a', 'm', 'e', ']', '\r', '\n',
        'e', 'x', 'e', '=', '"',
        0xE4, 0xB8, 0xAD,
        0xE6, 0x96, 0x87,
        0xE6, 0xB8, 0xB8,
        0xE6, 0x88, 0x8F,
        '.', 'e', 'x', 'e', '"', '\r', '\n'
    };
    WCHAR config[MAX_PATH];
    WCHAR target[MAX_PATH];
    WCHAR resolved[MAX_PATH];
    WCHAR fixture_directory[MAX_PATH];
    HANDLE target_file;

    if (
        GetCurrentDirectoryW(
            ARRAYSIZE(fixture_directory),
            fixture_directory
        ) == 0
    ) {
        return 2;
    }
    if (
        !path_join(
            config,
            ARRAYSIZE(config),
            fixture_directory,
            L"ccz_audio_fix.ini"
        )
        || !path_join(
            target,
            ARRAYSIZE(target),
            fixture_directory,
            L"中文游戏.exe"
        )
    ) {
        return 3;
    }
    target_file = CreateFileW(
        target,
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (target_file == INVALID_HANDLE_VALUE) {
        return 4;
    }
    CloseHandle(target_file);
    if (!write_bytes(config, utf8_config, sizeof(utf8_config))) {
        return 5;
    }
    if (
        !resolve_game_path(
            resolved,
            ARRAYSIZE(resolved),
            fixture_directory
        )
        || lstrcmpiW(resolved, target) != 0
    ) {
        return 6;
    }
    DeleteFileW(config);
    DeleteFileW(target);
    return 0;
}
