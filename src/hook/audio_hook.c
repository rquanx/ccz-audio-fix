#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>

#include "../common/audio_fix_protocol.h"

#pragma comment(linker, "/EXPORT:InstallAudioFix=_InstallAudioFix@4")
#pragma comment(lib, "winmm.lib")

#define SOUND_QUEUE_CAPACITY 32
#define SOUND_CACHE_CAPACITY 512
#define SOUND_OUTPUT_CAPACITY 32
#define SOUND_FORMAT_CAPACITY 64
#define SND_MEMORY_FLAG 0x0004

typedef BOOL (WINAPI *SndPlaySoundAFunction)(LPCSTR, UINT);

typedef struct CachedSound {
    char name[MAX_PATH];
    BYTE *data;
    DWORD size;
    BYTE *samples;
    DWORD samples_size;
    int output_index;
} CachedSound;

typedef struct SoundRequest {
    BOOL has_path;
    CachedSound *cached;
    UINT flags;
    char path[MAX_PATH];
} SoundRequest;

typedef struct WaveOutput {
    HWAVEOUT handle;
    BYTE format[SOUND_FORMAT_CAPACITY];
    DWORD format_size;
    WAVEHDR header;
    BOOL prepared;
} WaveOutput;

static SndPlaySoundAFunction original_snd_play_sound_a = NULL;
static CRITICAL_SECTION sound_lock;
static HANDLE sound_event = NULL;
static HANDLE sound_thread = NULL;
static SoundRequest sound_queue[SOUND_QUEUE_CAPACITY];
static int sound_head = 0;
static int sound_count = 0;
static volatile LONG audio_fix_installed = 0;
static CachedSound sound_cache[SOUND_CACHE_CAPACITY];
static int sound_cache_count = 0;
static WaveOutput sound_outputs[SOUND_OUTPUT_CAPACITY];
static int sound_output_count = 0;
static CachedSound *active_cached_sound = NULL;

static SndPlaySoundAFunction *find_snd_play_sound_slot(void) {
    BYTE *module = (BYTE *)GetModuleHandleW(NULL);
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS *nt;
    IMAGE_DATA_DIRECTORY imports;
    IMAGE_IMPORT_DESCRIPTOR *descriptor;
    HMODULE winmm;
    FARPROC target;

    if (module == NULL) {
        return NULL;
    }
    dos = (IMAGE_DOS_HEADER *)module;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return NULL;
    }
    nt = (IMAGE_NT_HEADERS *)(module + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return NULL;
    }
    winmm = GetModuleHandleW(L"winmm.dll");
    if (winmm == NULL) {
        return NULL;
    }
    target = GetProcAddress(winmm, "sndPlaySoundA");
    if (target == NULL) {
        return NULL;
    }

    imports = nt->OptionalHeader.DataDirectory[
        IMAGE_DIRECTORY_ENTRY_IMPORT
    ];
    if (imports.VirtualAddress != 0 && imports.Size != 0) {
        descriptor = (IMAGE_IMPORT_DESCRIPTOR *)(
            module + imports.VirtualAddress
        );
        while (descriptor->Name != 0) {
            IMAGE_THUNK_DATA *function_thunk = (IMAGE_THUNK_DATA *)(
                module + descriptor->FirstThunk
            );
            IMAGE_THUNK_DATA *name_thunk = (
                descriptor->OriginalFirstThunk != 0
                ? (IMAGE_THUNK_DATA *)(
                    module + descriptor->OriginalFirstThunk
                )
                : NULL
            );
            while (function_thunk->u1.Function != 0) {
                if (
                    name_thunk != NULL
                    && !IMAGE_SNAP_BY_ORDINAL(name_thunk->u1.Ordinal)
                ) {
                    IMAGE_IMPORT_BY_NAME *import_name = (
                        IMAGE_IMPORT_BY_NAME *)(
                            module + name_thunk->u1.AddressOfData
                        );
                    if (
                        lstrcmpA(
                            (LPCSTR)import_name->Name,
                            "sndPlaySoundA"
                        ) == 0
                    ) {
                        return (SndPlaySoundAFunction *)(
                            &function_thunk->u1.Function
                        );
                    }
                }
                if (
                    (FARPROC)(UINT_PTR)function_thunk->u1.Function
                    == target
                ) {
                    return (SndPlaySoundAFunction *)(
                        &function_thunk->u1.Function
                    );
                }
                function_thunk++;
                if (name_thunk != NULL) {
                    name_thunk++;
                }
            }
            descriptor++;
        }
    }

    {
        IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
        WORD section_index;
        for (
            section_index = 0;
            section_index < nt->FileHeader.NumberOfSections;
            section_index++, section++
        ) {
            DWORD size;
            DWORD offset;
            BYTE *start;
            if (
                (section->Characteristics & IMAGE_SCN_MEM_WRITE) == 0
                || section->VirtualAddress == 0
            ) {
                continue;
            }
            size = section->Misc.VirtualSize;
            start = module + section->VirtualAddress;
            for (
                offset = 0;
                offset + sizeof(UINT_PTR) <= size;
                offset += sizeof(UINT_PTR)
            ) {
                UINT_PTR *candidate = (UINT_PTR *)(start + offset);
                if ((FARPROC)*candidate == target) {
                    return (SndPlaySoundAFunction *)candidate;
                }
            }
        }
    }
    return NULL;
}

static DWORD read_u32(const BYTE *value) {
    return (DWORD)value[0]
        | ((DWORD)value[1] << 8)
        | ((DWORD)value[2] << 16)
        | ((DWORD)value[3] << 24);
}

static BOOL prepare_cached_sound(CachedSound *entry) {
    const BYTE *format = NULL;
    const BYTE *samples = NULL;
    DWORD format_size = 0;
    DWORD samples_size = 0;
    DWORD offset = 12;
    BYTE normalized_format[SOUND_FORMAT_CAPACITY];
    DWORD normalized_size;
    int output_index;

    while (offset + 8 <= entry->size) {
        DWORD chunk_size = read_u32(entry->data + offset + 4);
        DWORD payload = offset + 8;
        if (payload > entry->size || chunk_size > entry->size - payload) {
            return FALSE;
        }
        if (memcmp(entry->data + offset, "fmt ", 4) == 0) {
            format = entry->data + payload;
            format_size = chunk_size;
        } else if (memcmp(entry->data + offset, "data", 4) == 0) {
            samples = entry->data + payload;
            samples_size = chunk_size;
        }
        offset = payload + chunk_size + (chunk_size & 1);
    }
    if (format == NULL || format_size < 16 || samples == NULL) {
        return FALSE;
    }
    normalized_size = format_size == 16 ? 18 : format_size;
    if (normalized_size > SOUND_FORMAT_CAPACITY) {
        return FALSE;
    }
    ZeroMemory(normalized_format, sizeof(normalized_format));
    memcpy(normalized_format, format, format_size);

    for (output_index = 0; output_index < sound_output_count; output_index++) {
        WaveOutput *output = &sound_outputs[output_index];
        if (
            output->format_size == normalized_size
            && memcmp(
                output->format,
                normalized_format,
                normalized_size
            ) == 0
        ) {
            entry->samples = (BYTE *)samples;
            entry->samples_size = samples_size;
            entry->output_index = output_index;
            return TRUE;
        }
    }
    if (sound_output_count >= SOUND_OUTPUT_CAPACITY) {
        return FALSE;
    }

    output_index = sound_output_count;
    ZeroMemory(&sound_outputs[output_index], sizeof(WaveOutput));
    memcpy(
        sound_outputs[output_index].format,
        normalized_format,
        normalized_size
    );
    sound_outputs[output_index].format_size = normalized_size;
    if (
        waveOutOpen(
            &sound_outputs[output_index].handle,
            WAVE_MAPPER,
            (WAVEFORMATEX *)sound_outputs[output_index].format,
            0,
            0,
            CALLBACK_NULL
        ) != MMSYSERR_NOERROR
    ) {
        ZeroMemory(&sound_outputs[output_index], sizeof(WaveOutput));
        return FALSE;
    }
    sound_output_count++;
    entry->samples = (BYTE *)samples;
    entry->samples_size = samples_size;
    entry->output_index = output_index;
    return TRUE;
}

static void reset_cached_waves(void) {
    int index;
    for (index = 0; index < sound_output_count; index++) {
        WaveOutput *output = &sound_outputs[index];
        waveOutReset(output->handle);
        if (output->prepared) {
            waveOutUnprepareHeader(
                output->handle,
                &output->header,
                sizeof(output->header)
            );
            output->prepared = FALSE;
        }
        ZeroMemory(&output->header, sizeof(output->header));
    }
    active_cached_sound = NULL;
}

static BOOL same_cached_sound_is_playing(CachedSound *entry) {
    WaveOutput *output;
    if (
        active_cached_sound != entry
        || entry == NULL
        || entry->output_index < 0
        || entry->output_index >= sound_output_count
    ) {
        return FALSE;
    }
    output = &sound_outputs[entry->output_index];
    return output->prepared
        && (output->header.dwFlags & WHDR_DONE) == 0;
}

static BOOL play_cached_sound(CachedSound *entry) {
    WaveOutput *output;
    MMRESULT result;

    if (
        entry == NULL
        || entry->output_index < 0
        || entry->output_index >= sound_output_count
        || entry->samples == NULL
        || entry->samples_size == 0
    ) {
        return FALSE;
    }
    if (same_cached_sound_is_playing(entry)) {
        return TRUE;
    }
    reset_cached_waves();
    output = &sound_outputs[entry->output_index];
    output->header.lpData = (LPSTR)entry->samples;
    output->header.dwBufferLength = entry->samples_size;
    result = waveOutPrepareHeader(
        output->handle,
        &output->header,
        sizeof(output->header)
    );
    if (result != MMSYSERR_NOERROR) {
        return FALSE;
    }
    output->prepared = TRUE;
    result = waveOutWrite(
        output->handle,
        &output->header,
        sizeof(output->header)
    );
    if (result != MMSYSERR_NOERROR) {
        reset_cached_waves();
        return FALSE;
    }
    active_cached_sound = entry;
    return TRUE;
}

static const char *sound_file_name(const char *path) {
    const char *name = path;
    const char *cursor;

    if (path == NULL) {
        return NULL;
    }
    for (cursor = path; *cursor != '\0'; cursor++) {
        if (*cursor == '\\' || *cursor == '/') {
            name = cursor + 1;
        }
    }
    return name;
}

static CachedSound *find_cached_sound(const char *path) {
    const char *name = sound_file_name(path);
    int index;

    if (name == NULL || *name == '\0') {
        return NULL;
    }
    for (index = 0; index < sound_cache_count; index++) {
        if (lstrcmpiA(sound_cache[index].name, name) == 0) {
            return &sound_cache[index];
        }
    }
    return NULL;
}

static void preload_sound_cache(void) {
    WIN32_FIND_DATAA find_data;
    HANDLE search = FindFirstFileA("WAV\\*.wav", &find_data);

    if (search == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        char path[MAX_PATH];
        HANDLE file;
        LARGE_INTEGER size;
        BYTE *data;
        DWORD bytes_read = 0;
        CachedSound *entry;

        if (
            (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0
            || sound_cache_count >= SOUND_CACHE_CAPACITY
        ) {
            continue;
        }
        if (_snprintf_s(
                path,
                MAX_PATH,
                _TRUNCATE,
                "WAV\\%s",
                find_data.cFileName
            ) < 0) {
            continue;
        }
        file = CreateFileA(
            path,
            GENERIC_READ,
            FILE_SHARE_READ,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        if (file == INVALID_HANDLE_VALUE) {
            continue;
        }
        if (
            !GetFileSizeEx(file, &size)
            || size.QuadPart <= 0
            || size.QuadPart > MAXDWORD
        ) {
            CloseHandle(file);
            continue;
        }
        data = (BYTE *)HeapAlloc(
            GetProcessHeap(),
            0,
            (SIZE_T)size.QuadPart
        );
        if (
            data == NULL
            || !ReadFile(
                file,
                data,
                (DWORD)size.QuadPart,
                &bytes_read,
                NULL
            )
            || bytes_read != (DWORD)size.QuadPart
        ) {
            if (data != NULL) {
                HeapFree(GetProcessHeap(), 0, data);
            }
            CloseHandle(file);
            continue;
        }
        CloseHandle(file);
        if (
            bytes_read < 12
            || memcmp(data, "RIFF", 4) != 0
            || memcmp(data + 8, "WAVE", 4) != 0
        ) {
            HeapFree(GetProcessHeap(), 0, data);
            continue;
        }

        entry = &sound_cache[sound_cache_count++];
        lstrcpynA(entry->name, find_data.cFileName, MAX_PATH);
        entry->data = data;
        entry->size = bytes_read;
        entry->output_index = -1;
        prepare_cached_sound(entry);
    } while (FindNextFileA(search, &find_data));
    FindClose(search);
}

static DWORD WINAPI sound_worker(LPVOID parameter) {
    (void)parameter;
    for (;;) {
        WaitForSingleObject(sound_event, INFINITE);
        for (;;) {
            SoundRequest request;
            BOOL available = FALSE;

            ZeroMemory(&request, sizeof(request));
            EnterCriticalSection(&sound_lock);
            if (sound_count > 0) {
                request = sound_queue[sound_head];
                sound_head = (sound_head + 1) % SOUND_QUEUE_CAPACITY;
                sound_count--;
                available = TRUE;
            }
            LeaveCriticalSection(&sound_lock);
            if (!available) {
                break;
            }
            if (request.cached != NULL && play_cached_sound(request.cached)) {
                continue;
            }
            if (!request.has_path) {
                reset_cached_waves();
            }
            original_snd_play_sound_a(
                request.has_path ? request.path : NULL,
                request.flags
            );
        }
    }
}

static BOOL WINAPI queued_snd_play_sound_a(
    LPCSTR sound,
    UINT flags
) {
    SoundRequest request;
    CachedSound *cached;
    int tail;

    if ((flags & SND_MEMORY_FLAG) != 0 || sound_event == NULL) {
        return original_snd_play_sound_a(sound, flags);
    }
    cached = sound != NULL ? find_cached_sound(sound) : NULL;
    ZeroMemory(&request, sizeof(request));
    request.has_path = sound != NULL;
    request.cached = cached;
    request.flags = flags;
    if (sound != NULL) {
        lstrcpynA(request.path, sound, MAX_PATH);
    }

    EnterCriticalSection(&sound_lock);
    if (cached != NULL && sound_count > 0) {
        int previous = (
            sound_head + sound_count - 1
        ) % SOUND_QUEUE_CAPACITY;
        if (sound_queue[previous].cached == cached) {
            LeaveCriticalSection(&sound_lock);
            return TRUE;
        }
    }
    if (sound_count == SOUND_QUEUE_CAPACITY) {
        sound_head = (sound_head + 1) % SOUND_QUEUE_CAPACITY;
        sound_count--;
    }
    tail = (sound_head + sound_count) % SOUND_QUEUE_CAPACITY;
    sound_queue[tail] = request;
    sound_count++;
    LeaveCriticalSection(&sound_lock);
    SetEvent(sound_event);
    return TRUE;
}

static DWORD install_audio_hook(void) {
    SndPlaySoundAFunction *slot;
    SndPlaySoundAFunction current;
    DWORD old_protection = 0;
    DWORD ignored = 0;

    if (audio_fix_installed) {
        return ERROR_SUCCESS;
    }
    slot = find_snd_play_sound_slot();
    if (slot == NULL) {
        return ERROR_PROC_NOT_FOUND;
    }
    current = *slot;
    if (current == NULL) {
        return ERROR_INVALID_FUNCTION;
    }
    if (current == queued_snd_play_sound_a) {
        InterlockedExchange(&audio_fix_installed, 1);
        return ERROR_SUCCESS;
    }

    InitializeCriticalSection(&sound_lock);
    sound_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (sound_event == NULL) {
        DeleteCriticalSection(&sound_lock);
        return GetLastError();
    }
    original_snd_play_sound_a = current;
    preload_sound_cache();
    sound_thread = CreateThread(NULL, 0, sound_worker, NULL, 0, NULL);
    if (sound_thread == NULL) {
        DWORD error = GetLastError();
        CloseHandle(sound_event);
        sound_event = NULL;
        DeleteCriticalSection(&sound_lock);
        original_snd_play_sound_a = NULL;
        return error;
    }
    if (!VirtualProtect(
            slot,
            sizeof(*slot),
            PAGE_READWRITE,
            &old_protection
        )) {
        return GetLastError();
    }
    *slot = queued_snd_play_sound_a;
    VirtualProtect(slot, sizeof(*slot), old_protection, &ignored);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
    InterlockedExchange(&audio_fix_installed, 1);
    return ERROR_SUCCESS;
}

__declspec(dllexport) DWORD WINAPI InstallAudioFix(LPVOID parameter) {
    AudioFixRequest request;

    if (parameter == NULL) {
        return ERROR_INVALID_PARAMETER;
    }
    request = *(AudioFixRequest *)parameter;
    if (request.action != CCZ_AUDIO_FIX_INSTALL) {
        return ERROR_INVALID_PARAMETER;
    }
    return install_audio_hook();
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
