// WolfPro: out-pointer-style wrappers around a handful of cimplot functions
// that return small structs (ImVec2_c/ImPlotPoint_c) BY VALUE. The real C++
// types behind those C mirrors (ImVec2/ImPlotPoint) have user-provided
// constructors, which under MSVC's x64 ABI can make struct-return
// classification disagree between a C caller (sees a plain, constructor-less
// mirror struct) and the C++-compiled callee (sees a non-trivial type) --
// shifting every subsequent register-passed argument by one slot. Out-pointer
// parameters sidestep the hazard entirely, matching the convention cimgui's
// own generator already uses for exactly this reason (e.g. igGetContentRegionAvail).
#ifndef __CIMPLOT_WOLFPRO_SAFE__
#define __CIMPLOT_WOLFPRO_SAFE__

#include "cimplot.h"

#ifdef __cplusplus
extern "C" {
#endif

void ImPlot_GetPlotMousePos_Safe( ImPlotPoint_c *pOut, ImAxis x_axis, ImAxis y_axis );
void ImPlot_PlotToPixels_double_Safe( ImVec2_c *pOut, double x, double y, ImAxis x_axis, ImAxis y_axis );
void ImPlot_GetPlotPos_Safe( ImVec2_c *pOut );
void ImPlot_GetPlotSize_Safe( ImVec2_c *pOut );

#ifdef __cplusplus
}
#endif

#endif
