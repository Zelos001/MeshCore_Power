/* ----------------------------------------------------------------------------
 *  Companion-radio-with-bot firmware entry point.
 *
 *  Reuses the stock companion's main.cpp verbatim (textual include) instead of
 *  carrying a copy, so upstream changes to board/interface setup are picked up
 *  automatically. The only difference vs. stock: the global mesh object is a
 *  BotMesh (our subclass) named `the_bot_mesh`.
 *
 *  Other translation units (MyMesh.cpp, UITask.cpp) link against the symbol
 *  `the_mesh` declared extern in MyMesh.h; the bot env aliases it to our
 *  instance with:  -Wl,--defsym,the_mesh=the_bot_mesh
 *  (C++ forbids defining a BotMesh under an `extern MyMesh the_mesh;`
 *  declaration, hence the linker-level alias.)
 * ------------------------------------------------------------------------- */

#include "BotMesh.h"

// Pre-include headers that upstream main.cpp pulls in, so they are processed
// BEFORE the token renames below and can never be rewritten by them.
#ifdef DISPLAY_CLASS
  #include "UITask.h"
#endif

#define MyMesh   BotMesh       // make upstream main.cpp instantiate our subclass
#define the_mesh the_bot_mesh  // ...under a name that doesn't clash with the extern

#include "../companion_radio/main.cpp"

#undef the_mesh
#undef MyMesh
