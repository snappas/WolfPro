// WolfPro: Task 1 vendored implot.cpp/implot_items.cpp (see src/cimplot/implot/) but not
// implot_demo.cpp, unlike cimgui's vendoring which does include imgui_demo.cpp
// (src/cimgui/imgui/imgui_demo.cpp). cimplot.cpp is auto-generated from generator.lua and
// unconditionally emits a C wrapper (ImPlot_ShowDemoWindow) around ImPlot::ShowDemoWindow,
// which is declared in implot.h but never defined anywhere in this tree - so linking any
// binary that pulls in cimplot.cpp.obj (i.e. anything that calls any cimplot function at
// all, since they all live in that one translation unit) fails with an unresolved external
// unless something provides the symbol. Nothing in this codebase calls the demo window, so
// this is a minimal stub rather than vendoring the ~2500 line upstream demo file.
#include "implot/implot.h"

namespace ImPlot {

void ShowDemoWindow( bool *p_open ) {
	(void)p_open;
}

} // namespace ImPlot
