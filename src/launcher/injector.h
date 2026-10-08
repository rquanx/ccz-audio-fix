#ifndef CCZ_AUDIO_FIX_INJECTOR_H
#define CCZ_AUDIO_FIX_INJECTOR_H

#include <windows.h>

DWORD install_audio_fix(DWORD process_id, const WCHAR *dll_path);

#endif
