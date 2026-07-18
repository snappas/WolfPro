// WolfPro: see cimplot_wolfpro_safe.h for why these exist. Calls the real
// C++ ImPlot:: functions directly (not cimplot's own by-value wrappers) and
// writes the result through an out-pointer instead of returning by value.
#include "implot.h"
#include "implot_internal.h" // must precede cimplot.h (via cimplot_wolfpro_safe.h) in C++ mode -- see cimplot.h's comment on cimgui.h's internal-struct section needing real Dear ImGui internal types already visible
#include "cimplot_wolfpro_safe.h"

void ImPlot_GetPlotMousePos_Safe( ImPlotPoint_c *pOut, ImAxis x_axis, ImAxis y_axis )
{
	ImPlotPoint p = ImPlot::GetPlotMousePos( x_axis, y_axis );
	pOut->x = p.x;
	pOut->y = p.y;
}

void ImPlot_PlotToPixels_double_Safe( ImVec2_c *pOut, double x, double y, ImAxis x_axis, ImAxis y_axis )
{
	ImVec2 p = ImPlot::PlotToPixels( x, y, x_axis, y_axis );
	pOut->x = p.x;
	pOut->y = p.y;
}

void ImPlot_GetPlotPos_Safe( ImVec2_c *pOut )
{
	ImVec2 p = ImPlot::GetPlotPos();
	pOut->x = p.x;
	pOut->y = p.y;
}

void ImPlot_GetPlotSize_Safe( ImVec2_c *pOut )
{
	ImVec2 p = ImPlot::GetPlotSize();
	pOut->x = p.x;
	pOut->y = p.y;
}
