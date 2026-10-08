#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

#include "config.h"
#include "injector.h"

#define AUDIO_HOOK_RESOURCE 101
#define WINDOW_TITLE L"曹操传 MOD 音效流畅启动"

static BOOL extract_audio_hook(
    HINSTANCE instance,
    WCHAR *path,
    DWORD capacity
) {
    WCHAR temp[MAX_PATH];
    WCHAR directory[MAX_PATH];
    HRSRC resource;
    HGLOBAL loaded;
    const void *data;
    DWORD size;
    HANDLE file;
    DWORD written = 0;

    if (
        GetTempPathW(ARRAYSIZE(temp), temp) == 0
        || !path_join(
            directory,
            ARRAYSIZE(directory),
            temp,
            L"CCZAudioFix"
        )
    ) {
        return FALSE;
    }
    CreateDirectoryW(directory, NULL);
    if (_snwprintf_s(
            path,
            capacity,
            _TRUNCATE,
            L"%s\\ccz_audio_hook_%lu.dll",
            directory,
            GetCurrentProcessId()
        ) < 0) {
        return FALSE;
    }
    resource = FindResourceW(
        instance,
        MAKEINTRESOURCEW(AUDIO_HOOK_RESOURCE),
        RT_RCDATA
    );
    if (resource == NULL) {
        return FALSE;
    }
    loaded = LoadResource(instance, resource);
    data = loaded != NULL ? LockResource(loaded) : NULL;
    size = SizeofResource(instance, resource);
    if (data == NULL || size == 0) {
        return FALSE;
    }
    file = CreateFileW(
        path,
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY,
        NULL
    );
    if (file == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    if (!WriteFile(file, data, size, &written, NULL) || written != size) {
        CloseHandle(file);
        DeleteFileW(path);
        return FALSE;
    }
    CloseHandle(file);
    return TRUE;
}

static void show_error(const WCHAR *message) {
    MessageBoxW(NULL, message, WINDOW_TITLE, MB_OK | MB_ICONERROR);
}

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE previous_instance,
    PWSTR command_line,
    int show_command
) {
    WCHAR launcher_directory[MAX_PATH];
    WCHAR game[MAX_PATH];
    WCHAR game_directory[MAX_PATH];
    WCHAR audio_hook[MAX_PATH];
    WCHAR game_command[MAX_PATH + 3];
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    DWORD inject_result = ERROR_RETRY;
    int attempt;

    (void)previous_instance;
    (void)command_line;
    (void)show_command;

    if (!GetModuleFileNameW(
            NULL,
            launcher_directory,
            ARRAYSIZE(launcher_directory)
        )) {
        return 1;
    }
    path_remove_file_name(launcher_directory);
    if (!resolve_game_path(
            game,
            ARRAYSIZE(game),
            launcher_directory
        )) {
        show_error(
            L"没有找到目标游戏 EXE。请检查 ccz_audio_fix.ini，"
            L"或把本程序复制到 Ekd5.exe 所在目录；"
            L"也可以把游戏 EXE 拖到本程序上。"
        );
        return 2;
    }
    if (!extract_audio_hook(
            instance,
            audio_hook,
            ARRAYSIZE(audio_hook)
        )) {
        show_error(
            L"无法释放内置音效补丁，请检查临时目录权限或安全软件。"
        );
        return 3;
    }

    lstrcpynW(game_directory, game, ARRAYSIZE(game_directory));
    path_remove_file_name(game_directory);
    _snwprintf_s(
        game_command,
        ARRAYSIZE(game_command),
        _TRUNCATE,
        L"\"%s\"",
        game
    );
    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&process, sizeof(process));
    startup.cb = sizeof(startup);
    if (!CreateProcessW(
            game,
            game_command,
            NULL,
            NULL,
            FALSE,
            CREATE_SUSPENDED,
            NULL,
            game_directory,
            &startup,
            &process
        )) {
        DeleteFileW(audio_hook);
        show_error(L"无法启动游戏 EXE。");
        return 4;
    }

    if (ResumeThread(process.hThread) == (DWORD)-1) {
        TerminateProcess(process.hProcess, GetLastError());
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        DeleteFileW(audio_hook);
        return 5;
    }
    WaitForInputIdle(process.hProcess, 10000);
    for (attempt = 0; attempt < 30; attempt++) {
        inject_result = install_audio_fix(
            process.dwProcessId,
            audio_hook
        );
        if (inject_result == ERROR_SUCCESS) {
            break;
        }
        if (WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0) {
            break;
        }
        Sleep(100);
    }
    if (inject_result != ERROR_SUCCESS) {
        TerminateProcess(process.hProcess, inject_result);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        DeleteFileW(audio_hook);
        show_error(
            L"这个 MOD 没有找到兼容的 sndPlaySoundA 音效接口，"
            L"或补丁被安全软件阻止。游戏未启动。"
        );
        return 6;
    }

    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    CloseHandle(process.hProcess);
    DeleteFileW(audio_hook);
    return 0;
}
