#pragma once

#ifndef ReleaseCom
#define ReleaseCom(x) { if (x){ x->Release(); x = NULL; } }
#endif

#ifndef Align
#define Align(alignment, val) (((val + alignment - 1) / alignment) * alignment)
#endif