// WWE '13 PC (native port, SDK 0.10)

#if defined(_WIN32)
// Ask NVIDIA Optimus / AMD switchable graphics to prefer the discrete GPU.
extern "C" __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
extern "C" __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
#endif

#include "generated/default/wwe13_init.h"

#include "wwe13_app.h"

REX_DEFINE_APP(wwe13, Wwe13App::Create)
