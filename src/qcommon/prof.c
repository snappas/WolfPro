#include "../game/q_shared.h"
#include "qcommon.h"

#if defined( ENABLE_PROFILER )

#if defined( _WIN32 )
#define PROF_THREAD_LOCAL __declspec( thread )
#else
#define PROF_THREAD_LOCAL __thread
#endif

typedef struct profiler_s {
	qboolean paused;
	uint32_t enabledDetailMask; // bitwise-OR of currently-enabled PROF_*_DETAIL flags, see Prof_SetDetailEnabled
	profThread_t *threads[PROF_MAX_THREADS];
	int32_t threadCount;
	profFrame_t frames[PROF_MAX_FRAMES];
	uint32_t frameWriteIndex;
	int32_t currentFrameIndex;
} profiler_t;

static profiler_t prof;
static PROF_THREAD_LOCAL profThread_t *prof_currentThread = NULL;

void Prof_Init( void ) {
	Com_Memset( &prof, 0, sizeof( prof ) );
	prof.currentFrameIndex = -1; // sentinel: no frame started yet, distinct from slot 0
}

// NOTE: not thread-safe against concurrent calls -- prof.threadCount/prof.threads[]
// are unprotected shared state. Safe today because the only call site (Com_Init)
// runs once, single-threaded, at startup. If a future effort registers multiple
// threads concurrently, this needs real synchronization first.
void Prof_InitThread( const char *name ) {
	profThread_t *t;

	if ( prof.threadCount >= PROF_MAX_THREADS ) {
		Com_Printf( "Prof_InitThread: PROF_MAX_THREADS exceeded, '%s' not registered\n", name );
		return;
	}

	t = (profThread_t *)Z_Malloc( sizeof( profThread_t ) );
	Com_Memset( t, 0, sizeof( *t ) );
	t->used = qtrue;
	t->isMainThread = ( prof.threadCount == 0 );
	Q_strncpyz( t->name, name, sizeof( t->name ) );

	prof.threads[prof.threadCount++] = t;
	prof_currentThread = t;
}

// Same "not thread-safe against concurrent calls" caveat as Prof_InitThread
// above -- prof.threadCount/prof.threads[] are unprotected shared state.
// Safe today because the only call site (the GPU bridging code) lazily
// registers once from the single render thread this engine already runs
// everything on.
int32_t Prof_InitVirtualThread( const char *name ) {
	profThread_t *t;
	int32_t index;

	if ( prof.threadCount >= PROF_MAX_THREADS ) {
		Com_Printf( "Prof_InitVirtualThread: PROF_MAX_THREADS exceeded, '%s' not registered\n", name );
		return -1;
	}

	t = (profThread_t *)Z_Malloc( sizeof( profThread_t ) );
	Com_Memset( t, 0, sizeof( *t ) );
	t->used = qtrue;
	t->isMainThread = qfalse;
	t->isGPUThread = qtrue;
	Q_strncpyz( t->name, name, sizeof( t->name ) );

	index = prof.threadCount;
	prof.threads[prof.threadCount++] = t;
	return index; // deliberately does NOT touch prof_currentThread
}

void Prof_ShutdownThread( void ) {
	prof_currentThread = NULL;
}

// Prof_EndDuration never re-checks prof.paused -- it only replays what
// Prof_BeginDuration already recorded on this same thread's own stack, so
// each thread's Begin/End pair is self-consistent regardless of when
// prof.paused changes or which thread changes it. No cross-thread
// coordination needed.
void Prof_BeginDuration( const char *name, int32_t index, uint32_t color, uint32_t detailMask ) {
	profThread_t *t = prof_currentThread;
	profEvent_t *ev;
	uint32_t slot;

	if ( !t ) {
		return;
	}

	if ( t->depth >= PROF_MAX_DEPTH ) {
		// can't push a durationIndexStack sentinel here (would be out of
		// bounds) -- track the rejection separately so the matching End()
		// can identify and no-op it instead of falling through to the
		// normal pop, which would otherwise close whatever real scope is
		// actually on top of the stack (see overflowDepth's comment)
		t->overflowDepth++;
		return;
	}

	// same "keep the depth stack balanced without recording" skip as the
	// overflow case above -- a detail-tagged site whose category isn't
	// currently enabled costs a stack push/pop, not a ring buffer slot
	if ( prof.paused || ( detailMask != 0 && !( prof.enabledDetailMask & detailMask ) ) ) {
		// keep the depth stack balanced even while paused/disabled, so the
		// matching End() doesn't desync -- just record no data for this span
		t->durationIndexStack[t->depth] = -1;
		t->depth++;
		return;
	}

	slot = t->eventWriteIndex & ( PROF_MAX_EVENTS - 1 );
	ev = &t->events[slot];
	ev->begin = Sys_Microseconds();
	ev->end = ev->begin;
	ev->name = name;
	ev->index = index;
	ev->depth = t->depth;
	ev->frameIndex = prof.currentFrameIndex;
	ev->isMoment = qfalse;
	ev->color = color;

	t->durationIndexStack[t->depth] = (int32_t)slot;
	t->depth++;
	t->eventWriteIndex++;
}

void Prof_EndDuration( void ) {
	profThread_t *t = prof_currentThread;
	int32_t slot;

	if ( !t ) {
		return;
	}

	if ( t->overflowDepth > 0 ) {
		// matches the most recently rejected (LIFO) overflowed Begin --
		// nothing was pushed for it, so there's nothing to pop
		t->overflowDepth--;
		return;
	}

	if ( t->depth <= 0 ) {
		return;
	}

	t->depth--;
	slot = t->durationIndexStack[t->depth];
	if ( slot >= 0 ) {
		t->events[(uint32_t)slot].end = Sys_Microseconds();
	}
}

void Prof_RecordCompletedDuration( int32_t threadIndex, const char *name, int64_t beginUs, int64_t endUs, int32_t depth ) {
	profThread_t *t;
	profEvent_t *ev;
	uint32_t slot;

	if ( prof.paused || threadIndex < 0 || threadIndex >= prof.threadCount ) {
		return;
	}
	t = prof.threads[threadIndex];
	if ( !t ) {
		return;
	}

	slot = t->eventWriteIndex & ( PROF_MAX_EVENTS - 1 );
	ev = &t->events[slot];
	ev->begin = beginUs;
	ev->end = endUs;
	ev->name = name;
	ev->index = 0;
	ev->depth = depth;
	ev->frameIndex = prof.currentFrameIndex;
	ev->isMoment = qfalse;
	ev->color = 0;
	t->eventWriteIndex++;
}

void Prof_Moment( const char *name, uint32_t color ) {
	profThread_t *t = prof_currentThread;
	profEvent_t *ev;
	uint32_t slot;
	int64_t now;

	if ( !t || prof.paused ) {
		return;
	}

	now = Sys_Microseconds();
	slot = t->eventWriteIndex & ( PROF_MAX_EVENTS - 1 );
	ev = &t->events[slot];
	ev->begin = now;
	ev->end = now;
	ev->name = name;
	ev->index = 0;
	ev->depth = t->depth;
	ev->frameIndex = prof.currentFrameIndex;
	ev->isMoment = qtrue;
	ev->color = color;
	t->eventWriteIndex++;
}

void Prof_NewFrame( void ) {
	uint32_t slot;
	int64_t now;

	if ( prof.paused ) {
		return;
	}

	now = Sys_Microseconds();

	// close out the frame that just finished -- its "end" was only ever a
	// begin==end placeholder until the next frame starts
	if ( prof.currentFrameIndex >= 0 ) {
		prof.frames[prof.currentFrameIndex].end = now;
	}

	slot = prof.frameWriteIndex & ( PROF_MAX_FRAMES - 1 );
	prof.frames[slot].begin = now;
	prof.frames[slot].end = now;
	prof.frames[slot].valueCount = 0;
	prof.frameWriteIndex++;
	prof.currentFrameIndex = (int32_t)slot;
}

void Prof_SetFrameValue( const char *name, float value ) {
	profFrame_t *f;

	if ( prof.paused || prof.currentFrameIndex < 0 ) {
		return;
	}

	f = &prof.frames[prof.currentFrameIndex];
	if ( f->valueCount >= PROF_MAX_FRAME_VALUES ) {
		return;
	}
	f->values[f->valueCount].name = name;
	f->values[f->valueCount].value = value;
	f->valueCount++;
}

qboolean Prof_IsPaused( void ) {
	return prof.paused;
}

void Prof_SetPaused( qboolean paused ) {
	// close out whatever frame is in progress at the instant we pause,
	// using the pause instant as its end -- otherwise it stays open for
	// the entire pause duration, and the first Prof_NewFrame() call after
	// resuming closes it using the POST-RESUME timestamp, attributing the
	// whole pause duration to that one frame as a spurious huge duration
	if ( paused && !prof.paused && prof.currentFrameIndex >= 0 ) {
		prof.frames[prof.currentFrameIndex].end = Sys_Microseconds();
		prof.currentFrameIndex = -1;
	}
	prof.paused = paused;
}

uint32_t Prof_GetDetailMask( void ) {
	return prof.enabledDetailMask;
}

void Prof_SetDetailEnabled( uint32_t detailFlag, qboolean enabled ) {
	if ( enabled ) {
		prof.enabledDetailMask |= detailFlag;
	} else {
		prof.enabledDetailMask &= ~detailFlag;
	}
}

int32_t Prof_GetThreadCount( void ) {
	return prof.threadCount;
}

profThread_t *Prof_GetThread( int32_t index ) {
	if ( index < 0 || index >= prof.threadCount ) {
		return NULL;
	}
	return prof.threads[index];
}

int32_t Prof_GetFrameCount( void ) {
	return prof.frameWriteIndex < PROF_MAX_FRAMES ? (int32_t)prof.frameWriteIndex : PROF_MAX_FRAMES;
}

profFrame_t *Prof_GetFrame( int32_t index ) {
	int32_t count = Prof_GetFrameCount();
	uint32_t slot;

	if ( index < 0 || index >= count ) {
		return NULL;
	}

	// oldest retained frame is at (frameWriteIndex - count), walking forward
	slot = ( prof.frameWriteIndex - (uint32_t)count + (uint32_t)index ) & ( PROF_MAX_FRAMES - 1 );
	return &prof.frames[slot];
}

int32_t Prof_AnalyzeFunctions( profFunctionStat_t *out, int32_t maxCount, qboolean hasSelectedFrame, int64_t selectedFrameBeginUs, int64_t selectedFrameEndUs, qboolean gpuOnly ) {
	int32_t statCount = 0;
	int32_t threadIndex;

	for ( threadIndex = 0; threadIndex < prof.threadCount; threadIndex++ ) {
		profThread_t *t = prof.threads[threadIndex];
		uint32_t count;
		uint32_t i;

		if ( t->isGPUThread != gpuOnly ) {
			continue;
		}
		count = t->eventWriteIndex < PROF_MAX_EVENTS ? t->eventWriteIndex : PROF_MAX_EVENTS;

		for ( i = 0; i < count; i++ ) {
			profEvent_t *ev = &t->events[i];
			int64_t duration = ev->end - ev->begin;
			int32_t statIndex = -1;
			int32_t j;

			if ( !ev->name || ev->isMoment ) {
				continue; // moments have no meaningful duration to aggregate
			}

			for ( j = 0; j < statCount; j++ ) {
				if ( !strcmp( out[j].name, ev->name ) ) {
					statIndex = j;
					break;
				}
			}

			if ( statIndex == -1 ) {
				if ( statCount >= maxCount ) {
					continue;
				}
				statIndex = statCount++;
				out[statIndex].name = ev->name;
				out[statIndex].depth = ev->depth;
				out[statIndex].callCount = 0;
				out[statIndex].totalUS = 0;
				out[statIndex].minUS = 0x7fffffffffffffffLL;
				out[statIndex].maxUS = 0;
				out[statIndex].frameCallCount = 0;
				out[statIndex].frameTotalUS = 0;
			}

			out[statIndex].callCount++;
			out[statIndex].totalUS += duration;
			if ( duration < out[statIndex].minUS ) {
				out[statIndex].minUS = duration;
			}
			if ( duration > out[statIndex].maxUS ) {
				out[statIndex].maxUS = duration;
			}
			// containment of the event's OWN begin timestamp in the frame's
			// half-open [begin,end) window, not an overlap test -- adjacent
			// frames share an exact boundary timestamp (Prof_NewFrame sets
			// frame[N-1].end and frame[N].begin to the identical value in
			// one call), and Sys_Microseconds()'s microsecond resolution
			// means an event recorded immediately after Prof_NewFrame() can
			// easily truncate to that same tied instant -- a closed-interval
			// overlap test (begin <= end && end >= begin) would then match
			// BOTH neighboring windows for that one event. Since every event
			// is now recorded strictly after its own frame's Prof_NewFrame()
			// call, begin-containment alone assigns it to exactly one frame.
			if ( hasSelectedFrame && ev->begin >= selectedFrameBeginUs && ev->begin < selectedFrameEndUs ) {
				out[statIndex].frameCallCount++;
				out[statIndex].frameTotalUS += duration;
			}
		}
	}

	return statCount;
}

static void Prof_TogglePause_f( void ) {
	prof.paused = !prof.paused;
	Com_Printf( "profiler %s\n", prof.paused ? "paused" : "resumed" );
}

void Prof_RegisterCommands( void ) {
	Cmd_AddCommand( "prof_togglepause", Prof_TogglePause_f );
}

#endif // ENABLE_PROFILER
