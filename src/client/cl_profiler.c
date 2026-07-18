#include "cl_profiler.h"

#if defined( ENABLE_PROFILER )

#include "cl_imgui.h"
#include "../cimplot/cimplot.h"
#include "../cimplot/cimplot_wolfpro_safe.h"
#include "../qcommon/qcommon.h"

static void DrawGraphsTab( void );   // Task 11
static void DrawTimelineTab( void ); // Task 9
static void DrawFunctionsTab( void ); // Task 10

// Timeline view state, also consumed by DrawGraphsTab (click-to-jump) and
// CL_ProfilerFrame (force-select the Timeline tab when a jump just happened)
static double s_timelineMinUs = 0.0;
static double s_timelineMaxUs = 0.0;
static qboolean s_timelineViewInitialized = qfalse;
static qboolean s_timelineJumpPending = qfalse;
static int64_t s_timelineJumpBeginUs = 0;
static int64_t s_timelineJumpEndUs = 0;

// Frame selected from the Graphs tab (paused click), consumed by
// DrawFunctionsTab's per-selected-frame column. -1 = none selected.
// Cleared the instant the profiler resumes (see CL_ProfilerFrame) so a
// stale selection can never silently reappear against a since-overwritten
// ring-buffer slot the next time the profiler is paused.
static int32_t s_selectedFrameIndex = -1;

// DrawFunctionsTab's sort mode, reset to stack-level (its default) whenever
// a new frame is selected from Graphs -- see the DrawGraphsTab click
// handler below and the fuller rationale above FunctionStatCompare.
static qboolean s_functionsSortByStackLevel = qtrue;

static bool s_profilerFullScreen = false;

void CL_ProfilerFrame( void ) {
	static bool active = false;
	static qboolean timelineWasActive = qfalse;
	static qboolean functionsWasActive = qfalse;
	static qboolean pausedLastFrame = qfalse;
	// qtrue once WE have force-paused recording because the profiler isn't
	// visible -- distinct from the user's own manual "Paused" state so a
	// deliberate pause (e.g. to inspect a captured hitch) survives the
	// window being hidden and reshown rather than always being clobbered
	static qboolean suspendedForVisibility = qfalse;
	static qboolean pausedBeforeHidden = qfalse;
	qboolean timelineActive = qfalse;
	qboolean functionsActive = qfalse;
	qboolean pausedNow;
	qboolean visible;
	ImGuiViewport *viewport;

	ToggleBooleanWithShortcut( (qbool *)&active, ImGuiKey_P, ImGUI_ShortcutOptions_Global );
	GUI_AddMainMenuItem( ImGUI_MainMenu_Perf, "Profiler", "Ctrl+Shift+P", (qbool *)&active, qtrue );

	// nothing this function builds ever reaches the screen unless the
	// window itself is open AND the whole debug-UI overlay is actually being
	// drawn (togglegui/r_debugUI only gates the final draw submission, see
	// RB_ImGUI_Draw) -- suspend recording and skip all Timeline/Graphs/
	// Functions work below in that case, not just the invisible drawing
	visible = active && ( Cvar_VariableIntegerValue( "r_debugUI" ) != 0 );

	if ( !visible ) {
		if ( !suspendedForVisibility ) {
			pausedBeforeHidden = Prof_IsPaused();
			Prof_SetPaused( qtrue );
			suspendedForVisibility = qtrue;
		}
		timelineWasActive = qfalse;
		functionsWasActive = qfalse;
		return;
	}
	if ( suspendedForVisibility ) {
		Prof_SetPaused( pausedBeforeHidden );
		suspendedForVisibility = qfalse;
	}

	// resuming invalidates whatever frame was selected for the Functions
	// tab's per-frame column -- once new frames start overwriting the ring
	// buffer, that logical index no longer points at the same data, so the
	// selection (and the column) must not silently survive a resume
	pausedNow = Prof_IsPaused();
	if ( pausedLastFrame && !pausedNow ) {
		s_selectedFrameIndex = -1;
	}
	pausedLastFrame = pausedNow;

	PROF_BEGIN( "CL_ProfilerFrame" );

	viewport = igGetMainViewport();
	if ( s_profilerFullScreen ) {
		igSetNextWindowPos( viewport->WorkPos, 0, (ImVec2){ 0, 0 } );
		igSetNextWindowSize( viewport->WorkSize, 0 );
	} else {
		float dy = floorf( viewport->WorkSize.y * ( 2.0f / 3.0f ) );

		igSetNextWindowPos( (ImVec2){ viewport->WorkPos.x, viewport->WorkPos.y + dy }, 0, (ImVec2){ 0, 0 } );
		igSetNextWindowSize( (ImVec2){ viewport->WorkSize.x, viewport->WorkSize.y - dy }, 0 );
	}

	if ( igBegin( "Profiler", &active, 0 ) ) {
		// Space toggles pause, matching cnq3's convention. No hover/focus
		// gate needed: Space is also the real gameplay "+moveup"/jump key,
		// but this engine routes ALL keyboard input to either ImGui or the
		// game via one global switch (r_debugInput / KEYCATCH_IMGUI), not
		// per-widget hover -- when that's off, Space never reaches ImGui's
		// input state at all (this check is simply never true); when it's
		// on, Space is already fully diverted away from gameplay regardless
		// of where the mouse is, so it's safe to catch here unconditionally.
		if ( igIsKeyPressed_Bool( ImGuiKey_Space, false ) ) {
			Prof_SetPaused( Prof_IsPaused() ? qfalse : qtrue );
		}

		// deep profiling: high-call-count instrumentation (PROF_BEGIN_D call
		// sites, e.g. per-RC_* render command or per-surface-batch) records
		// nothing unless its category is enabled here -- keeps day-to-day
		// profiling within PROF_MAX_EVENTS by default; flip a category on
		// only while actively drilling into that subsystem. Table-driven so
		// adding a new PROF_*_DETAIL category later is a one-line addition.
		{
			static const struct { const char *label; uint32_t flag; } detailCategories[] = {
				{ "Render Commands", PROF_RENDER_CMD_DETAIL },
				{ "Surfaces", PROF_SURF_DETAIL },
			};
			uint32_t detailMask = Prof_GetDetailMask();
			int32_t i;

			igText( "Deep profiling:" );
			for ( i = 0; i < (int32_t)ARRAY_LEN( detailCategories ); i++ ) {
				bool enabled = ( detailMask & detailCategories[i].flag ) != 0;

				igSameLine( 0.0f, -1.0f );
				if ( igCheckbox( detailCategories[i].label, &enabled ) ) {
					Prof_SetDetailEnabled( detailCategories[i].flag, enabled ? qtrue : qfalse );
				}
			}
		}

		if ( igBeginTabBar( "ProfilerTabs", 0 ) ) {
			if ( igBeginTabItem( "Graphs", NULL, 0 ) ) {
				DrawGraphsTab();
				igEndTabItem();
			}
			if ( igBeginTabItem( "Timeline", NULL, s_timelineJumpPending ? ImGuiTabItemFlags_SetSelected : 0 ) ) {
				timelineActive = qtrue;
				if ( !timelineWasActive ) {
					Prof_SetPaused( qtrue );
				}
				DrawTimelineTab();
				igEndTabItem();
			}
			if ( igBeginTabItem( "Functions", NULL, 0 ) ) {
				functionsActive = qtrue;
				if ( !functionsWasActive ) {
					Prof_SetPaused( qtrue );
				}
				DrawFunctionsTab();
				igEndTabItem();
			}
			if ( igTabItemButton( s_profilerFullScreen ? "Minimize" : "Maximize", ImGuiTabItemFlags_Trailing ) ) {
				s_profilerFullScreen = !s_profilerFullScreen;
			}
			igEndTabBar();
		}
	}
	igEnd();

	timelineWasActive = timelineActive;
	functionsWasActive = functionsActive;

	PROF_END();
}

static bool s_graphsAutoPauseOnSpike = true;
static float s_graphsSpikeThresholdMs = 33.0f; // ~30fps
static bool s_graphsAutoScroll = true;
static bool s_graphsAutoScaleY = true;
static qboolean s_imPlotContextCreated = qfalse;

static void DrawGraphsTab( void ) {
	static float frameDeltaMs[PROF_MAX_FRAMES];
	int32_t frameCount = Prof_GetFrameCount();
	// the newest retained frame is always still in progress -- its end
	// timestamp is only a placeholder (see Prof_NewFrame()) until the
	// FOLLOWING frame starts and closes it, so its measured duration would
	// always read as exactly 0 if it were included below, permanently
	// pinning min (and putting a spurious zero-height bar at the right
	// edge of the plot) regardless of actual frame times
	int32_t closedFrameCount = frameCount > 0 ? frameCount - 1 : 0;
	int32_t i;
	float minVal = 999999.0f, maxVal = 0.0f, sum = 0.0f, sumSq = 0.0f;

	if ( !s_imPlotContextCreated ) {
		ImPlot_CreateContext();
		s_imPlotContextCreated = qtrue;
	}

	{
		bool paused = ( Prof_IsPaused() != qfalse );
		if ( igCheckbox( "Paused", &paused ) ) {
			Prof_SetPaused( paused ? qtrue : qfalse );
		}
	}

	igSameLine( 0.0f, -1.0f );
	igCheckbox( "Auto-pause on spike", &s_graphsAutoPauseOnSpike );
	igSameLine( 0.0f, -1.0f );
	igCheckbox( "Auto-scroll", &s_graphsAutoScroll );
	igSameLine( 0.0f, -1.0f );
	// ImPlotAxisFlags_AutoFit locks manual zoom/pan on the Y axis while it's
	// set (see ImPlotAxis::IsInputLocked in implot_internal.h) -- with it on,
	// the Y range always snaps to the visible data every frame instead of
	// requiring the user to manually drag it back into view after zooming in,
	// which was the actual complaint this checkbox exists to fix
	igCheckbox( "Auto-scale Y", &s_graphsAutoScaleY );
	igSliderFloat( "Spike threshold (ms)", &s_graphsSpikeThresholdMs, 1.0f, 200.0f, "%.1f", 0 );

	for ( i = 0; i < closedFrameCount; i++ ) {
		profFrame_t *f = Prof_GetFrame( i );
		float ms;

		if ( !f ) {
			continue;
		}
		// derived from the frame's own begin/end (Sys_Microseconds(),
		// recorded by Prof_NewFrame()) rather than the "Frame delta (ms)"
		// frame value below -- that value comes from com_frameTime, an
		// int millisecond counter, so it quantizes to whole milliseconds
		// (3/4/5ms) instead of showing the true sub-millisecond delta
		ms = (float)( f->end - f->begin ) / 1000.0f;
		frameDeltaMs[i] = ms;
		if ( ms < minVal ) minVal = ms;
		if ( ms > maxVal ) maxVal = ms;
		sum += ms;
		sumSq += ms * ms;

		if ( s_graphsAutoPauseOnSpike && ms > s_graphsSpikeThresholdMs && !Prof_IsPaused() ) {
			Prof_SetPaused( qtrue );
		}
	}

	{
		float avgVal = closedFrameCount > 0 ? sum / closedFrameCount : 0.0f;
		// E[x^2] - E[x]^2; clamped at 0 since float rounding can otherwise
		// drive a near-zero variance (e.g. a perfectly steady frame time)
		// slightly negative, which sqrtf() would turn into NaN
		float variance = closedFrameCount > 0 ? ( sumSq / closedFrameCount ) - ( avgVal * avgVal ) : 0.0f;
		float stddevVal = sqrtf( variance > 0.0f ? variance : 0.0f );

		igText( "Frames: %d  min: %.2fms  max: %.2fms  avg: %.2fms  stddev: %.2fms",
			closedFrameCount, minVal, maxVal, avgVal, stddevVal );
	}

	// fill whatever vertical space is actually left in the tab instead of a
	// fixed height, so the plot fits inside the profiler's bottom-1/3-height
	// window without forcing a vertical scrollbar
	{
		ImVec2 plotAvail;

		igGetContentRegionAvail( &plotAvail );
		if ( plotAvail.y < 50.0f ) {
			plotAvail.y = 50.0f;
		}

		if ( ImPlot_BeginPlot( "Frame delta (ms)", (ImVec2){ -1, plotAvail.y }, 0 ) ) {
		ImPlotSpec *spec = ImPlotSpec_ImPlotSpec();

		ImPlot_SetupAxes( "Frame", "ms", 0, s_graphsAutoScaleY ? ImPlotAxisFlags_AutoFit : 0 );
		// pin the X-axis to the data range every frame (matches cnq3's
		// SetupAxisLimits(..., ImGuiCond_Always)) so mouse.x always maps 1:1
		// to a frame index for hover/click, regardless of user pan/zoom.
		// Only done while Auto-scroll is enabled; when disabled, ImPlot's
		// native drag-to-pan/scroll-to-zoom interaction takes over and the
		// hover/click mapping still works since ImPlot_GetPlotMousePos
		// already returns coordinates in (possibly zoomed/panned) plot space.
		if ( s_graphsAutoScroll ) {
			ImPlot_SetupAxisLimits( ImAxis_X1, 0.0, (double)( closedFrameCount > 0 ? closedFrameCount : 1 ), ImPlotCond_Always );
		}
		ImPlot_PlotBars_FloatPtrInt( "ms", frameDeltaMs, closedFrameCount, 0.67, 0, *spec );
		ImPlotSpec_destroy( spec );

		if ( ImPlot_IsPlotHovered() ) {
			ImPlotPoint_c mouse;
			int32_t hoveredIndex;

			ImPlot_GetPlotMousePos_Safe( &mouse, IMPLOT_AUTO, IMPLOT_AUTO );
			hoveredIndex = (int32_t)floor( mouse.x );

			if ( hoveredIndex >= 0 && hoveredIndex < closedFrameCount ) {
				profFrame_t *frame = Prof_GetFrame( hoveredIndex );

				mouse.x = floor( mouse.x );
				{
					ImVec2_c edgeLPoint, edgeRPoint, plotPos, plotSize;
					float edgeL, edgeR, edgeT, edgeB;
					ImDrawList *plotDrawList;

					ImPlot_PlotToPixels_double_Safe( &edgeLPoint, mouse.x - 0.5, mouse.y, IMPLOT_AUTO, IMPLOT_AUTO );
					ImPlot_PlotToPixels_double_Safe( &edgeRPoint, mouse.x + 0.5, mouse.y, IMPLOT_AUTO, IMPLOT_AUTO );
					edgeL = edgeLPoint.x;
					edgeR = edgeRPoint.x;
					ImPlot_GetPlotPos_Safe( &plotPos );
					ImPlot_GetPlotSize_Safe( &plotSize );
					edgeT = plotPos.y;
					edgeB = edgeT + plotSize.y;

					ImPlot_PushPlotClipRect( 0.0f );
					plotDrawList = ImPlot_GetPlotDrawList();
					// IM_COL32(255,255,0,64): translucent yellow highlight over the hovered bar's column
					ImDrawList_AddRectFilled( plotDrawList, (ImVec2){ edgeL, edgeT }, (ImVec2){ edgeR, edgeB }, 0x4000FFFFu, 0.0f, 0 );
					ImPlot_PopPlotClipRect();
				}

				if ( frame ) {
					int32_t v;

					igBeginTooltip();
					igText( "Frame: %d / %d", hoveredIndex, closedFrameCount );
					igText( "Full frame  : %6lld us", (long long)( frame->end - frame->begin ) );
					for ( v = 0; v < frame->valueCount; v++ ) {
						igText( "%-12s: %6.2f", frame->values[v].name, frame->values[v].value );
					}
					igEndTooltip();

					if ( Prof_IsPaused() && igIsMouseClicked_Bool( ImGuiMouseButton_Left, false ) ) {
						s_timelineJumpBeginUs = frame->begin;
						s_timelineJumpEndUs = frame->end;
						s_timelineJumpPending = qtrue;
						s_selectedFrameIndex = hoveredIndex;
						// fresh frame selection always starts the Functions
						// tab back at stack-level order, regardless of
						// whatever column sort was left over from
						// inspecting a previous selection
						s_functionsSortByStackLevel = qtrue;
					}
				}
			}
		}

		ImPlot_EndPlot();
		}
	}
}

// Clamps [s_timelineMinUs, s_timelineMaxUs] back within [recordedMinUs, recordedMaxUs]
// without distorting the range width, unless the range itself is wider than the
// recorded span (in which case it snaps to the full recorded span).
static void Timeline_ClampRange( double recordedMinUs, double recordedMaxUs ) {
	double rangeUs = s_timelineMaxUs - s_timelineMinUs;
	double recordedSpanUs = recordedMaxUs - recordedMinUs;

	if ( rangeUs >= recordedSpanUs ) {
		s_timelineMinUs = recordedMinUs;
		s_timelineMaxUs = recordedMaxUs;
		return;
	}
	if ( s_timelineMinUs < recordedMinUs ) {
		s_timelineMinUs = recordedMinUs;
		s_timelineMaxUs = s_timelineMinUs + rangeUs;
	}
	if ( s_timelineMaxUs > recordedMaxUs ) {
		s_timelineMaxUs = recordedMaxUs;
		s_timelineMinUs = s_timelineMaxUs - rangeUs;
	}
}

// Timeline row layout, top to bottom within a thread's block:
//   [0, PROF_TIMELINE_MARKER_LANES)   -- moment markers, one fixed row per
//     known marker name (Timeline_MarkerPreferredLane), so a given marker
//     always shows up in the same place run to run; only spills into
//     another marker row when its label would actually collide with one
//     already placed there
//   [PROF_TIMELINE_MARKER_LANES, ...) -- duration boxes, strictly one row
//     per call-stack depth, no stacking, no collision search -- sequential
//     siblings at the same depth simply share that row, matching cnq3
#define PROF_TIMELINE_MARKER_LANES 4 // 3 known markers + 1 fallback for an unrecognized name
#define PROF_TIMELINE_MAX_DEPTH_LEVELS 12
#define PROF_TIMELINE_MAX_LANES ( PROF_TIMELINE_MARKER_LANES + PROF_TIMELINE_MAX_DEPTH_LEVELS )

// converts a caller-supplied 0xRRGGBBAAu (the natural order to read/write a
// color literal in) to ImGui's packed 0xAABBGGRR ImU32 format used by
// ImDrawList_Add*() throughout this file
static ImU32 Timeline_PackColor( uint32_t rgba ) {
	uint32_t r = ( rgba >> 24 ) & 0xFFu;
	uint32_t g = ( rgba >> 16 ) & 0xFFu;
	uint32_t b = ( rgba >> 8 ) & 0xFFu;
	uint32_t a = rgba & 0xFFu;
	return ( a << 24 ) | ( b << 16 ) | ( g << 8 ) | r;
}

// fixed home row for each known moment marker, so it renders in the same
// place every time instead of wherever a collision search happens to land it
static int32_t Timeline_MarkerPreferredLane( const char *name ) {
	if ( !strcmp( name, "Frame Start" ) ) {
		return 0;
	}
	if ( !strcmp( name, "Input Sample" ) ) {
		return 1;
	}
	if ( !strcmp( name, "Present" ) ) {
		return 2;
	}
	return 3; // unrecognized marker: shared fallback row
}

// most recent "Frame Start" moment at or before beforeUs, on the same
// thread as the hovered marker -- events are stored in ring-buffer slot
// order, not chronological order, so this can't just walk backward from
// the hovered event's own index; only called from the rare, one-off
// "Submit" tooltip hover below, so an occasional full scan is cheap enough
static qboolean Timeline_FindFrameStartBefore( profThread_t *t, uint32_t count, int64_t beforeUs, int64_t *outBeginUs ) {
	uint32_t i;
	qboolean found = qfalse;
	int64_t bestUs = 0;

	for ( i = 0; i < count; i++ ) {
		profEvent_t *ev = &t->events[i];

		if ( !ev->name || !ev->isMoment || strcmp( ev->name, "Frame Start" ) ) {
			continue;
		}
		if ( ev->begin <= beforeUs && ( !found || ev->begin > bestUs ) ) {
			bestUs = ev->begin;
			found = qtrue;
		}
	}
	*outBeginUs = bestUs;
	return found;
}

static void DrawTimelineTab( void ) {
	ImVec2 origin;
	ImVec2 avail;
	ImDrawList *drawList;
	int32_t threadCount = Prof_GetThreadCount();
	int32_t threadIndex;
	int32_t frameCount = Prof_GetFrameCount();
	const float boxHeight = 18.0f;
	const float perThreadHeight = 20.0f + (float)PROF_TIMELINE_MAX_LANES * ( boxHeight + 2.0f );
	double recordedMinUs = 0.0;
	double recordedMaxUs = 1000.0; // fallback: 1ms window when there's no recorded data yet
	double viewRangeUs;
	float canvasWidth;
	float canvasHeight;
	float plotOriginX;
	float pxPerUs;

	igText( "Threads: %d  (paused: %s)", threadCount, Prof_IsPaused() ? "yes" : "no" );
	if ( igButton( "Resume", (ImVec2){ 0, 0 } ) ) {
		Prof_SetPaused( qfalse );
	}

	igGetContentRegionAvail( &avail );

	// total recorded time span, from the oldest retained frame to the newest
	if ( frameCount > 0 ) {
		profFrame_t *firstFrame = Prof_GetFrame( 0 );
		profFrame_t *lastFrame = Prof_GetFrame( frameCount - 1 );

		if ( firstFrame && lastFrame ) {
			recordedMinUs = (double)firstFrame->begin;
			recordedMaxUs = (double)lastFrame->end;
			if ( recordedMaxUs <= recordedMinUs ) {
				recordedMaxUs = recordedMinUs + 1.0; // degenerate single-instant guard
			}
		}
	}

	if ( s_timelineJumpPending ) {
		// matches cnq3's ZoomOnTimeRange: center on the frame, pad the half-range by 1.1x
		double center = (double)( s_timelineJumpBeginUs + s_timelineJumpEndUs ) * 0.5;
		double halfRange = (double)( s_timelineJumpEndUs - s_timelineJumpBeginUs ) * 0.5 * 1.1;

		if ( halfRange < 0.5 ) {
			halfRange = 0.5;
		}
		s_timelineMinUs = center - halfRange;
		s_timelineMaxUs = center + halfRange;
		s_timelineViewInitialized = qtrue;
		s_timelineJumpPending = qfalse;
		Timeline_ClampRange( recordedMinUs, recordedMaxUs );
	} else if ( !s_timelineViewInitialized ) {
		// one-time default view: same "100us/pixel" default width as before, ending at
		// the most recent recorded instant, clamped to the actual recorded span
		double recordedSpanUs = recordedMaxUs - recordedMinUs;
		double defaultRangeUs = (double)avail.x * 100.0;

		if ( defaultRangeUs > recordedSpanUs || defaultRangeUs <= 0.0 ) {
			defaultRangeUs = recordedSpanUs;
		}
		s_timelineMaxUs = recordedMaxUs;
		s_timelineMinUs = recordedMaxUs - defaultRangeUs;
		s_timelineViewInitialized = qtrue;
		Timeline_ClampRange( recordedMinUs, recordedMaxUs );
	}

	// scrollable region: lets deeply-nested call stacks (more lanes than fit
	// in the visible height) be reached by dragging the vertical scrollbar,
	// instead of silently clipping off the bottom. NoScrollWithMouse keeps
	// the mouse wheel free for our own horizontal zoom logic below rather
	// than fighting over it (the earlier Timeline zoom/scrollbar bug this
	// avoids reintroducing was an *oversized* canvas with an *uncontrolled*
	// scrollbar competing with that same zoom logic -- this scrollbar is
	// vertical-only, drag-only, and sized off real content, not a stray
	// igDummy()).
	igBeginChild_Str( "TimelineScrollRegion", (ImVec2){ avail.x, avail.y }, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollWithMouse );
	igGetContentRegionAvail( &avail ); // refresh: excludes the vertical scrollbar's reserved width

	canvasHeight = threadCount > 0 ? (float)threadCount * perThreadHeight : avail.y;
	if ( canvasHeight < avail.y ) {
		canvasHeight = avail.y; // fill the visible area when nothing needs scrolling
	}

	igInvisibleButton( "TimelineCanvas", (ImVec2){ avail.x, canvasHeight }, 0 );
	igGetItemRectMin( &origin );

	canvasWidth = avail.x > 1.0f ? avail.x : 1.0f;
	plotOriginX = origin.x + 80.0f; // existing WolfPro label-column convention, kept as-is

	if ( igIsItemHovered( 0 ) ) {
		ImGuiIO *io = igGetIO();
		double cursorPosPc = ( (double)io->MousePos.x - (double)plotOriginX ) / (double)canvasWidth;
		double cursorPosUs;

		if ( cursorPosPc < 0.0 ) {
			cursorPosPc = 0.0;
		}
		if ( cursorPosPc > 1.0 ) {
			cursorPosPc = 1.0;
		}
		cursorPosUs = s_timelineMinUs + ( s_timelineMaxUs - s_timelineMinUs ) * cursorPosPc;

		if ( io->MouseWheel != 0.0f ) {
			double oldRangeUs = s_timelineMaxUs - s_timelineMinUs;
			double recordedSpanUs = recordedMaxUs - recordedMinUs;
			double newRangeUs;

			if ( io->MouseWheel > 0.0f ) {
				// zoom in: shrink range by 1.5x, floor of 1us
				newRangeUs = oldRangeUs / 1.5;
				if ( newRangeUs < 1.0 ) {
					newRangeUs = 1.0;
				}
			} else {
				// zoom out: grow range by 1.5x, capped at the full recorded span
				newRangeUs = oldRangeUs * 1.5;
				if ( newRangeUs > recordedSpanUs ) {
					newRangeUs = recordedSpanUs;
				}
			}
			s_timelineMinUs = cursorPosUs - cursorPosPc * newRangeUs;
			s_timelineMaxUs = s_timelineMinUs + newRangeUs;
		}

		if ( igIsMouseDragging( ImGuiMouseButton_Left, 0.0f ) ) {
			ImVec2 delta;
			double usPerPx;

			igGetMouseDragDelta( &delta, ImGuiMouseButton_Left, 0.0f );
			usPerPx = ( s_timelineMaxUs - s_timelineMinUs ) / (double)canvasWidth;
			s_timelineMinUs -= (double)delta.x * usPerPx;
			s_timelineMaxUs -= (double)delta.x * usPerPx;
			igResetMouseDragDelta( ImGuiMouseButton_Left );
		}
	}

	// keep the persisted view pinned inside the currently retained ~512-frame
	// window every frame, not just while the canvas is hovered -- otherwise a
	// stale view left over from before a pause/resume cycle (e.g. Resume
	// clicked while the mouse isn't over the canvas) never gets reclamped,
	// and the per-event time-window filter below has a moving target to cull
	// against instead of a settled one
	Timeline_ClampRange( recordedMinUs, recordedMaxUs );

	viewRangeUs = s_timelineMaxUs - s_timelineMinUs;
	if ( viewRangeUs < 1e-6 ) {
		viewRangeUs = 1e-6; // guard against div-by-zero; Timeline_ClampRange should prevent this in practice
	}
	pxPerUs = (float)( (double)avail.x / viewRangeUs ); // uses full avail.x, not avail.x-80 (see label-column note above)

	drawList = igGetWindowDrawList();
	ImDrawList_PushClipRect( drawList, origin, (ImVec2){ origin.x + avail.x, origin.y + canvasHeight }, true );

	// "Frame Start" timestamps within the currently retained frame window
	// (thread 0, the main thread -- the only one that ever records one),
	// collected once up front rather than re-scanned per use. Used to
	// phase-align the background grid below to the first one on screen.
	static double frameStartTimes[PROF_MAX_FRAMES];
	int32_t frameStartCount = 0;

	if ( threadCount > 0 ) {
		profThread_t *mainThread = Prof_GetThread( 0 );

		if ( mainThread ) {
			uint32_t evCount = mainThread->eventWriteIndex < PROF_MAX_EVENTS ? mainThread->eventWriteIndex : PROF_MAX_EVENTS;
			uint32_t ei;

			for ( ei = 0; ei < evCount && frameStartCount < PROF_MAX_FRAMES; ei++ ) {
				profEvent_t *fev = &mainThread->events[ei];
				double beginUs;

				if ( !fev->name || !fev->isMoment || strcmp( fev->name, "Frame Start" ) != 0 ) {
					continue;
				}
				beginUs = (double)fev->begin;
				if ( beginUs < recordedMinUs || beginUs > recordedMaxUs ) {
					continue; // outside the currently retained frame window
				}
				frameStartTimes[frameStartCount++] = beginUs;
			}
		}
	}

	// cnq3-style background time-delineation columns
	{
		static const struct {
			double us;
			const char *label;
		} timeUnits[] = {
			{ 0.1, "100 ns" }, { 1.0, "1 us" }, { 10.0, "10 us" }, { 100.0, "100 us" },
			{ 1000.0, "1 ms" }, { 10000.0, "10 ms" }, { 100000.0, "100 ms" },
			{ 1000000.0, "1 s" }, { 10000000.0, "10 s" }, { 100000000.0, "100 s" }, { 1000000000.0, "1000 s" }
		};
		const int32_t unitCount = (int32_t)( sizeof( timeUnits ) / sizeof( timeUnits[0] ) );
		int32_t unitIndex = unitCount - 1;
		int32_t u;
		double unitUs;
		double firstColUs;
		double t;
		int32_t colIndex;
		const ImU32 colOdd = 0x18FFFFFFu; // subtle translucent-white banding on every other column

		for ( u = 0; u < unitCount; u++ ) {
			if ( viewRangeUs / timeUnits[u].us < 10.0 ) {
				unitIndex = u;
				break;
			}
		}
		if ( unitIndex > 0 && viewRangeUs / timeUnits[unitIndex].us < 2.5 ) {
			unitIndex--;
		}
		unitUs = timeUnits[unitIndex].us;

		// phase-align the grid to the leftmost "Frame Start" moment
		// currently visible instead of an arbitrary multiple of unitUs from
		// absolute time zero, so a grid line lands exactly on a frame
		// boundary instead of wherever process-start happened to fall. If
		// none is visible in the current pan/zoom, falls back to the
		// absolute-zero phase.
		// colIndex is seeded from the exact integer column offset between
		// firstColUs and the anchor (not reset to 0 at firstColUs) so the
		// alternating shading's parity is fixed relative to the anchor
		// itself -- otherwise the column immediately before the anchor
		// wasn't reliably the opposite shade from the one immediately after,
		// since that relationship depended on how far firstColUs (the
		// leftmost visible column) happened to be from the anchor.
		{
			double gridAnchorUs = 0.0;
			int32_t fi;
			int32_t colOffset;
			// content is drawn starting at plotOriginX (origin.x + 80px for
			// the label column), but pxPerUs is derived from the FULL
			// avail.x, not avail.x-80 -- so [s_timelineMinUs, s_timelineMaxUs]
			// does not correspond 1:1 to what's actually visible inside the
			// clipped canvas [origin.x, origin.x+avail.x]. The whole visible
			// window is really shifted left by this many microseconds (at a
			// typical ~1200px-wide canvas, 80px is roughly 6-7% of the width)
			// -- checking "is this Frame Start visible" against the raw
			// [s_timelineMinUs, s_timelineMaxUs] bounds was therefore testing
			// against a range that doesn't match what's on screen.
			double labelColumnUs = 80.0 / (double)pxPerUs;
			double visibleMinUs = s_timelineMinUs - labelColumnUs;
			double visibleMaxUs = s_timelineMaxUs - labelColumnUs;
			qboolean foundAnchor = qfalse;

			// scan every candidate and keep the true minimum, rather than
			// breaking on the first match under an assumed-ascending order:
			// t->events is a ring buffer, and once eventWriteIndex has
			// wrapped past PROF_MAX_EVENTS (routine within 10-20 seconds of
			// real play at this event rate), frameStartTimes[] is populated
			// in raw ring-buffer slot order, which is NOT chronological.
			// Trusting "first match found" under that assumption could pick
			// a Frame Start that isn't actually the leftmost visible one,
			// and flip which one gets picked abruptly as the view pans --
			// this is order-independent and always finds the true leftmost.
			for ( fi = 0; fi < frameStartCount; fi++ ) {
				if ( frameStartTimes[fi] >= visibleMinUs && frameStartTimes[fi] <= visibleMaxUs ) {
					if ( !foundAnchor || frameStartTimes[fi] < gridAnchorUs ) {
						gridAnchorUs = frameStartTimes[fi];
						foundAnchor = qtrue;
					}
				}
			}

			// use visibleMinUs (the true left edge of the visible canvas,
			// 80px further left than s_timelineMinUs), not s_timelineMinUs
			// itself, as the reference point here. Using s_timelineMinUs
			// meant that whenever the anchor's own timestamp happened to
			// fall between s_timelineMinUs and s_timelineMinUs+unitUs,
			// colOffset came out to exactly 0 -- so the drawn-column loop
			// started AT the anchor's own column and never drew the column
			// immediately to its left at all (that region just showed the
			// raw unpainted background instead of the shaded color it
			// should always have). Whether that happened flipped
			// unpredictably depending on which side of s_timelineMinUs the
			// anchor fell on as the view panned -- exactly the "light in
			// one screenshot, dark in the next" symptom reported. Anchoring
			// to visibleMinUs instead guarantees the loop always starts at
			// least one full column left of the true visible edge, so the
			// column left of the anchor is always drawn.
			colOffset = (int32_t)floor( ( visibleMinUs - gridAnchorUs ) / unitUs );
			firstColUs = gridAnchorUs + (double)colOffset * unitUs;
			colIndex = colOffset;
		}
		for ( t = firstColUs; t < s_timelineMaxUs; t += unitUs, colIndex++ ) {
			float cx0, cx1;

			if ( !( colIndex & 1 ) ) {
				continue;
			}
			cx0 = plotOriginX + (float)( ( t - s_timelineMinUs ) * pxPerUs );
			cx1 = plotOriginX + (float)( ( t + unitUs - s_timelineMinUs ) * pxPerUs );
			ImDrawList_AddRectFilled( drawList, (ImVec2){ cx0, origin.y }, (ImVec2){ cx1, origin.y + canvasHeight }, colOdd, 0.0f, 0 );
		}

		ImDrawList_AddText_Vec2( drawList, (ImVec2){ plotOriginX + 4.0f, origin.y + canvasHeight - 16.0f },
			igGetColorU32_Col( ImGuiCol_TextDisabled, 1.0f ), va( "grid: %s", timeUnits[unitIndex].label ), NULL );
	}

	for ( threadIndex = 0; threadIndex < threadCount; threadIndex++ ) {
		profThread_t *t = Prof_GetThread( threadIndex );
		uint32_t count;
		uint32_t i;
		float rowY = origin.y + threadIndex * perThreadHeight;
		float markerLaneEndX[PROF_TIMELINE_MARKER_LANES];
		float markerLaneLastDrawnX[PROF_TIMELINE_MARKER_LANES];
		float durationLaneLastCol[PROF_TIMELINE_MAX_DEPTH_LEVELS]; // last painted integer pixel column, per lane
		int32_t mi;

		if ( !t ) {
			continue;
		}

		for ( mi = 0; mi < PROF_TIMELINE_MARKER_LANES; mi++ ) {
			markerLaneEndX[mi] = -1.0e30f;
			markerLaneLastDrawnX[mi] = -1.0e30f;
		}
		for ( mi = 0; mi < PROF_TIMELINE_MAX_DEPTH_LEVELS; mi++ ) {
			durationLaneLastCol[mi] = -1.0e30f;
		}

		ImDrawList_AddText_Vec2( drawList, (ImVec2){ origin.x, rowY }, igGetColorU32_Col( ImGuiCol_Text, 1.0f ), t->name, NULL );

		count = t->eventWriteIndex < PROF_MAX_EVENTS ? t->eventWriteIndex : PROF_MAX_EVENTS;

		// two passes: all duration boxes first, then all moment markers on
		// top -- markers live in their own dedicated band above every
		// duration row (see the PROF_TIMELINE_* comment above), so the two
		// groups never share a row.
		for ( i = 0; i < count; i++ ) {
			profEvent_t *ev = &t->events[i];
			float x0, x1, y0, y1;
			ImU32 color;
			ImU32 borderColor;
			int32_t lane;
			float clipX0, clipX1;

			if ( !ev->name || ev->isMoment ) {
				continue; // skip moment events this pass
			}

			if ( (double)ev->begin < recordedMinUs || (double)ev->begin > recordedMaxUs ) {
				continue;
			}

			float trueX1;
			float endCol;

			x0 = plotOriginX + (float)( ( (double)ev->begin - s_timelineMinUs ) * pxPerUs );
			x1 = plotOriginX + (float)( ( (double)ev->end - s_timelineMinUs ) * pxPerUs );
			if ( x1 < origin.x || x0 > origin.x + avail.x ) {
				continue; // off-screen, skip
			}
			trueX1 = x1; // pre-clamp: used for dedup bookkeeping, not drawing
			if ( x1 - x0 < 1.0f ) {
				x1 = x0 + 1.0f; // visual-only widening so tiny/instant events stay hoverable
			}

			// row = this event's own call-stack depth, full stop -- no
			// collision search, no stacking. Two events at the same depth
			// can never overlap in time on a single thread (only one scope
			// at a given depth can be open at once), so this alone is a
			// stable, hierarchy-correct vertical key. Offset by the marker
			// band's height so markers always render above every duration row.
			lane = ev->depth;
			if ( lane < 0 ) {
				lane = 0;
			}
			if ( lane > PROF_TIMELINE_MAX_DEPTH_LEVELS - 1 ) {
				lane = PROF_TIMELINE_MAX_DEPTH_LEVELS - 1;
			}

			// pixel-column merge: skip boxes whose ENTIRE true extent
			// (start through end, both floorf()'d to integer columns)
			// falls within a pixel column already painted in this lane --
			// i.e. genuinely indistinguishable from what's already drawn,
			// not merely touching it. A dense burst of sub-pixel-wide
			// events (e.g. many thousands of calls recorded during a
			// hitch, then zoomed into) would otherwise each still emit
			// their own filled rect, border and label -- easily enough
			// vertices to overflow the ImGui vertex buffer
			// (RB_ImGUI_Draw's MAX_IMGUI_VERTS assert) despite being
			// visually indistinguishable on screen anyway.
			//
			// Checking endCol (not startCol) against the watermark is
			// deliberate: same-depth siblings are immediately adjacent
			// (one ends exactly where the next begins), so a box's START
			// column ties the previous box's END column constantly --
			// skipping on that tie alone wrongly treated every ordinary
			// adjacent sibling as a duplicate (this happened for real:
			// even the top-level "Frame" box vanished, since consecutive
			// frames touch by construction). A box is only truly
			// redundant if its own END doesn't extend past what's already
			// claimed; if it extends further, it contributes real new
			// visual information and must be drawn even though its start
			// ties or lands before the watermark. A genuine dense burst
			// still correctly collapses to ~1 draw per column, since each
			// sub-column candidate's OWN end also fails to extend past
			// the watermark until real time has actually advanced a
			// further column.
			endCol = floorf( trueX1 );
			if ( endCol <= durationLaneLastCol[lane] ) {
				continue;
			}
			durationLaneLastCol[lane] = endCol > durationLaneLastCol[lane] ? endCol : durationLaneLastCol[lane];

			lane += PROF_TIMELINE_MARKER_LANES;

			y0 = rowY + 20.0f + lane * ( boxHeight + 2.0f );
			y1 = y0 + boxHeight;

			if ( ev->color != 0 ) {
				// caller picked an explicit color at the PROF_BEGIN_C() call
				// site (e.g. common.c coloring "NET_Sleep"/"Spin Wait" the
				// same muted blue-gray) -- no string comparison needed here
				color = Timeline_PackColor( ev->color );
			} else {
				// deterministic per-call-site color from the event's name pointer,
				// no ImGui style-table dependency
				color = 0xFF3070A0u + ( (uint32_t)( ev->index * 2654435761u ) & 0x00303030u );
			}
			ImDrawList_AddRectFilled( drawList, (ImVec2){ x0, y0 }, (ImVec2){ x1, y1 }, color, 0.0f, 0 );

			// translucent white outline, NOT opaque -- an opaque border
			// around a 1px-wide instantaneous-looking duration would
			// otherwise read as a solid black sliver instead of a colored one
			borderColor = 0x90FFFFFFu;
			ImDrawList_AddRect( drawList, (ImVec2){ x0, y0 }, (ImVec2){ x1, y1 }, borderColor, 0.0f, 0, 1.0f );

			// label always drawn inside the box, starting at its own
			// (visible-clamped) left edge, clipped to the box's own
			// on-screen bounds -- a name too long for the box is simply
			// truncated by the clip rather than relocated beside it or
			// left to overflow into whatever comes next (matches cnq3)
			clipX0 = x0 > origin.x ? x0 : origin.x;
			clipX1 = x1 < origin.x + avail.x ? x1 : origin.x + avail.x;
			if ( clipX1 > clipX0 ) {
				ImDrawList_PushClipRect( drawList, (ImVec2){ clipX0, y0 }, (ImVec2){ clipX1, y1 }, true );
				ImDrawList_AddText_Vec2( drawList, (ImVec2){ clipX0 + 2.0f, y0 + 1.0f }, 0xFFFFFFFFu, ev->name, NULL );
				ImDrawList_PopClipRect( drawList );
			}

			if ( igIsMouseHoveringRect( (ImVec2){ x0, y0 }, (ImVec2){ x1, y1 }, true ) ) {
				igBeginTooltip();
				igText( "%s: %.3f ms", ev->name, (float)( ev->end - ev->begin ) / 1000.0f );
				igEndTooltip();
			}
		}

		for ( i = 0; i < count; i++ ) {
			profEvent_t *ev = &t->events[i];
			float x0, y0, y1, lineBottomY;
			int32_t lane;
			ImU32 markerColor;
			ImVec2 textSize;
			float reservedEndX;
			const float gapPx = 2.0f;

			if ( !ev->name || !ev->isMoment ) {
				continue; // skip duration events this pass
			}

			if ( (double)ev->begin < recordedMinUs || (double)ev->begin > recordedMaxUs ) {
				continue;
			}

			x0 = plotOriginX + (float)( ( (double)ev->begin - s_timelineMinUs ) * pxPerUs );
			if ( x0 < origin.x || x0 > origin.x + avail.x ) {
				continue; // off-screen, skip
			}

			// fixed home row per marker name -- only spills into another
			// marker row (searching forward) if its label would actually
			// collide with one already placed there this frame
			lane = Timeline_MarkerPreferredLane( ev->name );
			igCalcTextSize( &textSize, ev->name, NULL, false, -1.0f );
			reservedEndX = x0 + 4.0f + textSize.x;
			while ( lane < PROF_TIMELINE_MARKER_LANES - 1 && markerLaneEndX[lane] > x0 ) {
				lane++;
			}

			// pixel-level merge, same rationale as the duration-box lane
			// dedup above: a burst of markers landing in the same pixel
			// column is visually indistinguishable, so skip redrawing them
			// rather than emitting a circle+line+label per event.
			if ( x0 < markerLaneLastDrawnX[lane] + 1.0f ) {
				continue;
			}
			markerLaneLastDrawnX[lane] = x0;
			markerLaneEndX[lane] = reservedEndX + gapPx;

			y0 = rowY + 20.0f + lane * ( boxHeight + 2.0f );
			y1 = y0 + boxHeight;
			lineBottomY = origin.y + canvasHeight; // full-height guide line down through every row below

			// caller picked an explicit color at the PROF_MOMENT_C() call
			// site; no string comparison needed here
			if ( ev->color != 0 ) {
				markerColor = Timeline_PackColor( ev->color );
			} else {
				markerColor = 0xFFC0C0C0u; // unrecognized moment: light gray
			}

			// small filled circle + label at the marker's own row; the line
			// itself runs the rest of the way down to the bottom of the
			// window so it acts as a vertical guide through every duration
			// row below it
			ImDrawList_AddCircleFilled( drawList, (ImVec2){ x0, y0 }, 3.0f, markerColor, 0 );
			ImDrawList_AddLine( drawList, (ImVec2){ x0, y0 }, (ImVec2){ x0, lineBottomY }, markerColor, 2.0f );
			ImDrawList_AddText_Vec2( drawList, (ImVec2){ x0 + 4.0f, y0 }, markerColor, ev->name, NULL );

			if ( igIsMouseHoveringRect( (ImVec2){ x0 - 3.0f, y0 }, (ImVec2){ x0 + 3.0f, lineBottomY }, true ) ) {
				int64_t frameStartUs;

				igBeginTooltip();
				if ( !strcmp( ev->name, "Submit" ) && Timeline_FindFrameStartBefore( t, count, ev->begin, &frameStartUs ) ) {
					igText( "Submit: FS+%.3fms", (float)( ev->begin - frameStartUs ) / 1000.0f );
				} else {
					igText( "%s", ev->name );
				}
				igEndTooltip();
			}
		}
	}

	ImDrawList_PopClipRect( drawList );

	igEndChild();
}

// column index matches the Functions table's own layout: 0=Name, 1=Calls,
// 2=Total(us), 3=Avg(us), 4=Max(us), 5=Frame Calls, 6=Frame(us) (the last
// two only exist when showSelectedFrame is true; sorting by one of them
// while the columns aren't shown just falls through to the default case
// below, i.e. an inert no-op, not a crash). Column/direction are passed via
// file-scope statics rather than qsort's userdata-less comparator signature
// -- safe since this is single-threaded UI code.
//
// Stack-level sort is a separate mode, not just another column: it's the
// table's original/default appearance (Prof_AnalyzeFunctions' natural
// first-occurrence scan order happens to read roughly shallow-to-deep,
// since instrumentation is entered in call order) and is deliberately
// restorable via its own button rather than an ImGuiTableColumnFlags_
// DefaultSort column, since Dear ImGui persists whichever column the user
// last actually clicked across frames (via the table's own ID-keyed
// settings) -- once the user tries column-sorting once, there's no
// in-UI way back to the original order without this. (s_functionsSortByStackLevel
// itself lives up with the other cross-tab shared state near the top of
// this file, since DrawGraphsTab's click handler also resets it.)
static int32_t s_functionsSortColumn = -1;
static ImGuiSortDirection s_functionsSortDir = ImGuiSortDirection_None;

static int FunctionStatCompare( const void *a, const void *b ) {
	const profFunctionStat_t *sa = (const profFunctionStat_t *)a;
	const profFunctionStat_t *sb = (const profFunctionStat_t *)b;
	int cmp = 0;
	int64_t avgA, avgB;

	if ( s_functionsSortByStackLevel ) {
		return ( sa->depth > sb->depth ) - ( sa->depth < sb->depth );
	}

	switch ( s_functionsSortColumn ) {
	case 0:
		cmp = strcmp( sa->name, sb->name );
		break;
	case 1:
		cmp = ( sa->callCount > sb->callCount ) - ( sa->callCount < sb->callCount );
		break;
	case 2:
		cmp = ( sa->totalUS > sb->totalUS ) - ( sa->totalUS < sb->totalUS );
		break;
	case 3:
		avgA = sa->callCount > 0 ? sa->totalUS / sa->callCount : 0;
		avgB = sb->callCount > 0 ? sb->totalUS / sb->callCount : 0;
		cmp = ( avgA > avgB ) - ( avgA < avgB );
		break;
	case 4:
		cmp = ( sa->maxUS > sb->maxUS ) - ( sa->maxUS < sb->maxUS );
		break;
	case 5:
		cmp = ( sa->frameCallCount > sb->frameCallCount ) - ( sa->frameCallCount < sb->frameCallCount );
		break;
	case 6:
		cmp = ( sa->frameTotalUS > sb->frameTotalUS ) - ( sa->frameTotalUS < sb->frameTotalUS );
		break;
	default:
		return 0;
	}
	return s_functionsSortDir == ImGuiSortDirection_Descending ? -cmp : cmp;
}

static void DrawFunctionsTab( void ) {
	static profFunctionStat_t stats[PROF_MAX_FUNCTIONS];
	int32_t statCount;
	int32_t i;
	// the per-selected-frame columns only ever show while paused with an
	// active selection -- resuming clears s_selectedFrameIndex (see
	// CL_ProfilerFrame), so this can't display stale data from a frame
	// that's since been overwritten in the ring buffer
	qboolean showSelectedFrame = ( Prof_IsPaused() && s_selectedFrameIndex >= 0 ) ? qtrue : qfalse;
	profFrame_t *selectedFrame = showSelectedFrame ? Prof_GetFrame( s_selectedFrameIndex ) : NULL;
	int32_t columnCount;

	if ( !selectedFrame ) {
		showSelectedFrame = qfalse; // defensive: selection index no longer resolves to a retained frame
	}
	columnCount = showSelectedFrame ? 7 : 5;

	if ( igButton( "Refresh", (ImVec2){ 0, 0 } ) ) {
		Com_Memset( stats, 0, sizeof( stats ) );
	}
	igSameLine( 0.0f, -1.0f );
	if ( igButton( "Sort by Stack Level", (ImVec2){ 0, 0 } ) ) {
		s_functionsSortByStackLevel = qtrue;
	}
	statCount = Prof_AnalyzeFunctions( stats, PROF_MAX_FUNCTIONS, showSelectedFrame ? qtrue : qfalse,
		showSelectedFrame ? selectedFrame->begin : 0, showSelectedFrame ? selectedFrame->end : 0, qfalse );

	if ( showSelectedFrame ) {
		igText( "Selected frame: %d", s_selectedFrameIndex );
	}

	// arbitrary named per-frame stats attached via PROF_SET_FRAME_VALUE
	// (e.g. RB_DrawSurfs's Opaque/Transparent surface counts, or the
	// renderer's PSO/texture/scene/view counts) -- same array the Graphs
	// tab's hover tooltip reads, just always-visible here instead of
	// on-hover. Shows the selected frame while one's active (consistent
	// with the table below), otherwise the most recently retained frame.
	{
		profFrame_t *statsFrame = selectedFrame;

		if ( !statsFrame ) {
			int32_t frameCount = Prof_GetFrameCount();
			statsFrame = frameCount > 0 ? Prof_GetFrame( frameCount - 1 ) : NULL;
		}

		if ( statsFrame && statsFrame->valueCount > 0 ) {
			int32_t v;

			for ( v = 0; v < statsFrame->valueCount; v++ ) {
				if ( v > 0 ) {
					igSameLine( 0.0f, -1.0f );
				}
				igText( "%s: %.0f", statsFrame->values[v].name, statsFrame->values[v].value );
			}
		}
	}

	// NoSavedSettings: without it, Dear ImGui persists this table's last
	// clicked sort column/direction to imgui.ini and restores it on a
	// fresh launch, reporting it via igTableGetSortSpecs() with
	// SpecsDirty=true -- indistinguishable from a genuine fresh header
	// click by the check below, silently overriding the intended
	// stack-level default before the user ever sees it. Column widths
	// aren't meaningfully customized here anyway, so there's nothing
	// else worth persisting for this particular table.
	//
	// SortTristate: without it, imgui_tables.cpp unconditionally treats
	// SpecsCount==0 as "needs a default sort" (see TableUpdateLayout's
	// IsSortSpecsDirty force-set, and TableSortSpecsSanitize's fallback to
	// column 0) -- so every frame the stack-level branch below clears all
	// columns' sort direction to None, Dear ImGui immediately re-picks
	// column 0 ("Name") ascending as if it were a fresh user click, which
	// the check after igTableHeadersRow() can't tell apart from a real one
	// and flips s_functionsSortByStackLevel back off one frame later.
	// SortTristate makes SpecsCount==0 a legitimate resting state instead
	// of something Dear ImGui "fixes" on our behalf.
	if ( igBeginTable( "Functions", columnCount, ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate | ImGuiTableFlags_NoSavedSettings, (ImVec2){ 0, 0 }, 0.0f ) ) {
		// Inlined instead of using the shared TableHeader() helper: while in
		// stack-level mode, every active sort spec must be force-cleared
		// BEFORE igTableHeadersRow() processes this frame's header clicks --
		// SpecsDirty turned out not to be a reliable "the user just clicked"
		// signal (see below), and the only way to get one IS to guarantee
		// nothing else could have set SpecsCount>0 by this point except a
		// genuine click just now.
		{
			static const char *headers7[7] = { "Name", "Calls", "Total (us)", "Avg (us)", "Max (us)", "Frame Calls", "Frame (us)" };
			static const char *headers5[5] = { "Name", "Calls", "Total (us)", "Avg (us)", "Max (us)" };
			const char **headers = showSelectedFrame ? headers7 : headers5;
			int32_t hc;

			for ( hc = 0; hc < columnCount; hc++ ) {
				igTableSetupColumn( headers[hc], 0, 0, 0 );
			}
			if ( s_functionsSortByStackLevel ) {
				// re-cleared every frame, not just once on entry -- Dear
				// ImGui can reassert a sort on its own (confirmed via live
				// testing: it defaulted to column 0 "Name" ascending once
				// the earlier imgui.ini-persistence issue was fixed, i.e.
				// even a genuinely fresh/never-saved table still picks a
				// default sortable column on its own), and SpecsDirty is
				// set for THAT just as much as for a real user click, so
				// it can't be trusted to tell the two apart. Continuously
				// clearing means the only way SpecsCount can be nonzero
				// after igTableHeadersRow() below is a real click this
				// exact frame.
				for ( hc = 0; hc < columnCount; hc++ ) {
					igTableSetColumnSortDirection( hc, ImGuiSortDirection_None, false );
				}
			}
			igTableHeadersRow();
		}

		// the qsort itself is re-applied every frame a sort is active,
		// since `stats` is rebuilt from scratch by Prof_AnalyzeFunctions
		// above each draw -- there's no persisted row order to preserve
		// between frames.
		{
			ImGuiTableSortSpecs *sortSpecs = igTableGetSortSpecs();

			if ( sortSpecs && sortSpecs->SpecsCount > 0 ) {
				s_functionsSortByStackLevel = qfalse;
				s_functionsSortColumn = sortSpecs->Specs[0].ColumnIndex;
				s_functionsSortDir = sortSpecs->Specs[0].SortDirection;
				sortSpecs->SpecsDirty = false;
			}
			if ( s_functionsSortByStackLevel || ( sortSpecs && sortSpecs->SpecsCount > 0 ) ) {
				qsort( stats, (size_t)statCount, sizeof( stats[0] ), FunctionStatCompare );
			}
		}

		for ( i = 0; i < statCount; i++ ) {
			profFunctionStat_t *s = &stats[i];
			int64_t avgUS = s->callCount > 0 ? s->totalUS / s->callCount : 0;

			igTableNextRow( 0, 0.0f );
			igTableSetColumnIndex( 0 );
			// Selectable (not plain text) so the whole row highlights on
			// hover, matching Dear ImGui's standard row-selectable idiom --
			// AllowOverlap so it doesn't block hovering/clicking whatever
			// else is drawn in this row (nothing currently, but harmless).
			igSelectable_Bool( s->name, false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, (ImVec2){ 0, 0 } );
			igTableSetColumnIndex( 1 );
			igText( "%d", s->callCount );
			igTableSetColumnIndex( 2 );
			igText( "%lld", (long long)s->totalUS );
			igTableSetColumnIndex( 3 );
			igText( "%lld", (long long)avgUS );
			igTableSetColumnIndex( 4 );
			igText( "%lld", (long long)s->maxUS );
			if ( showSelectedFrame ) {
				igTableSetColumnIndex( 5 );
				igText( "%d", s->frameCallCount );
				igTableSetColumnIndex( 6 );
				igText( "%lld", (long long)s->frameTotalUS );
			}
		}
		igEndTable();
	}

	{
		static profFunctionStat_t gpuStats[PROF_MAX_FUNCTIONS];
		int32_t gpuStatCount = Prof_AnalyzeFunctions( gpuStats, PROF_MAX_FUNCTIONS, showSelectedFrame ? qtrue : qfalse,
			showSelectedFrame ? selectedFrame->begin : 0, showSelectedFrame ? selectedFrame->end : 0, qtrue );

		// hidden entirely until the GPU thread has actually been
		// registered (ENABLE_PROFILER off, or the renderer hasn't reached
		// its first RB_EndFrame bridge call yet) -- an empty "GPU" table
		// with a header row and nothing under it would be confusing noise
		if ( gpuStatCount > 0 ) {
			igNewLine();
			igText( "GPU" );
			if ( igBeginTable( "FunctionsGPU", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable | ImGuiTableFlags_NoSavedSettings, (ImVec2){ 0, 0 }, 0.0f ) ) {
				int32_t gi;

				TableHeader( 5, "Name", "Calls", "Total (us)", "Avg (us)", "Max (us)" );
				for ( gi = 0; gi < gpuStatCount; gi++ ) {
					profFunctionStat_t *s = &gpuStats[gi];
					int64_t avgUS = s->callCount > 0 ? s->totalUS / s->callCount : 0;

					igTableNextRow( 0, 0.0f );
					igTableSetColumnIndex( 0 );
					igText( "%s", s->name );
					igTableSetColumnIndex( 1 );
					igText( "%d", s->callCount );
					igTableSetColumnIndex( 2 );
					igText( "%lld", (long long)s->totalUS );
					igTableSetColumnIndex( 3 );
					igText( "%lld", (long long)avgUS );
					igTableSetColumnIndex( 4 );
					igText( "%lld", (long long)s->maxUS );
				}
				igEndTable();
			}
		}
	}
}

#else

void CL_ProfilerFrame( void ) {
}

#endif // ENABLE_PROFILER
