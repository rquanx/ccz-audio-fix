#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../common/audio_fix_protocol.h"
#include "injector.h"

static DWORD run_remote_thread(
    HANDLE process,
    LPTHREAD_START_ROUTINE function,
    LPVOID parameter,
    DWORD timeout
) {
    HANDLE thread;
    DWORD result = ERROR_GEN_FAILURE;

    if (function == NULL) {
        return ERROR_PROC_NOT_FOUND;
    }
    thread = CreateRemoteThread(
        process,
        NULL,
        0,
        function,
        parameter,
        0,
        NULL
    );
    if (thread == NULL) {
        return GetLastError();
    }
    if (WaitForSingleObject(thread, timeout) == WAIT_OBJECT_0) {
        if (!GetExitCodeThread(thread, &result)) {
            result = GetLastError();
        }
    } else {
        result = ERROR_TIMEOUT;
    }
    CloseHandle(thread);
    return result;
}

DWORD install_audio_fix(DWORD process_id, const WCHAR *dll_path) {
    HANDLE process;
    SIZE_T path_size;
    LPVOID remote_path = NULL;
    LPVOID remote_request = NULL;
    DWORD remote_module;
    HMODULE local_module = NULL;
    FARPROC local_export;
    LPTHREAD_START_ROUTINE remote_export;
    AudioFixRequest request;
    DWORD result = ERROR_GEN_FAILURE;

    process = OpenProcess(
        PROCESS_CREATE_THREAD
            | PROCESS_QUERY_INFORMATION
            | PROCESS_VM_OPERATION
            | PROCESS_VM_WRITE
            | PROCESS_VM_READ,
        FALSE,
        process_id
    );
    if (process == NULL) {
        return GetLastError();
    }

    path_size = ((SIZE_T)lstrlenW(dll_path) + 1) * sizeof(WCHAR);
    remote_path = VirtualAllocEx(
        process,
        NULL,
        path_size,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE
    );
    if (
        remote_path == NULL
        || !WriteProcessMemory(
            process,
            remote_path,
            dll_path,
            path_size,
            NULL
        )
    ) {
        result = GetLastError();
        goto cleanup;
    }
    remote_module = run_remote_thread(
        process,
        (LPTHREAD_START_ROUTINE)GetProcAddress(
            GetModuleHandleW(L"kernel32.dll"),
            "LoadLibraryW"
        ),
        remote_path,
        15000
    );
    if (remote_module < 0x10000) {
        result = remote_module != 0
            ? remote_module
            : ERROR_DLL_INIT_FAILED;
        goto cleanup;
    }

    local_module = LoadLibraryExW(
        dll_path,
        NULL,
        DONT_RESOLVE_DLL_REFERENCES
    );
    if (local_module == NULL) {
        result = GetLastError();
        goto cleanup;
    }
    local_export = GetProcAddress(local_module, "InstallAudioFix");
    if (local_export == NULL) {
        result = GetLastError();
        goto cleanup;
    }
    remote_export = (LPTHREAD_START_ROUTINE)(
        (BYTE *)(UINT_PTR)remote_module
        + ((BYTE *)local_export - (BYTE *)local_module)
    );

    request.action = CCZ_AUDIO_FIX_INSTALL;
    remote_request = VirtualAllocEx(
        process,
        NULL,
        sizeof(request),
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE
    );
    if (
        remote_request == NULL
        || !WriteProcessMemory(
            process,
            remote_request,
            &request,
            sizeof(request),
            NULL
        )
    ) {
        result = GetLastError();
        goto cleanup;
    }
    result = run_remote_thread(
        process,
        remote_export,
        remote_request,
        15000
    );

cleanup:
    if (local_module != NULL) {
        FreeLibrary(local_module);
    }
    if (remote_request != NULL) {
        VirtualFreeEx(process, remote_request, 0, MEM_RELEASE);
    }
    if (remote_path != NULL) {
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
    }
    CloseHandle(process);
    return result;
}
