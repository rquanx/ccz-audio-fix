#ifndef CCZ_AUDIO_FIX_CONFIG_H
#define CCZ_AUDIO_FIX_CONFIG_H

#include <windows.h>

BOOL path_join(
    WCHAR *output,
    DWORD capacity,
    const WCHAR *directory,
    const WCHAR *name
);

void path_remove_file_name(WCHAR *path);

BOOL resolve_game_path(
    WCHAR *game,
    DWORD capacity,
    const WCHAR *launcher_directory
);

#endif
