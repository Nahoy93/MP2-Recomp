#ifndef MP2_UI_FUNCS_H
#define MP2_UI_FUNCS_H

// recompui includes this file by a hardcoded relative path from inside the
// library (src/api/ui_api_events.cpp):
//
//     #include "../../../../../patches/ui_funcs.h"
//
// which resolves to recomp/patches/ui_funcs.h. It is where a port would declare
// game-side functions for recompui's mod API. This port exports none yet, so it
// only pulls in the event structures recompui needs.

#include "recompui/event_structs.h"

#endif  // MP2_UI_FUNCS_H
