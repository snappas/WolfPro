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

void Prof_ShutdownThread( void ) {
	prof_currentThread = NULL;
}

// Prof_EndDuration never re-checks prof.paused -- it only replays what
// Prof_BeginDuration already recorded on this same thread's own stack, so
// each thread's Begin/End pair is self-consistent regardless of when
// prof.paused changes or which thread changes it. No cross-thread
// coordination needed.
void Prof_BeginDuration( const char *name, int32_t index ) {
	profThread_t *t = prof_currentThread;
	profEvent_t *ev;
	uint32_t slot;

	if ( !t || t->depth >= PROF_MAX_DEPTH ) {
		return;
	}

	if ( prof.paused ) {
		// keep the depth stack balanced even while paused, so the matching
		// End() doesn't desync -- just record no data for this span
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

	t->durationIndexStack[t->depth] = (int32_t)slot;
	t->depth++;
	t->eventWriteIndex++;
}

void Prof_EndDuration( void ) {
	profThread_t *t = prof_currentThread;
	int32_t slot;

	if ( !t || t->depth <= 0 ) {
		return;
	}

	t->depth--;
	slot = t->durationIndexStack[t->depth];
	if ( slot >= 0 ) {
		t->events[(uint32_t)slot].end = Sys_Microseconds();
	}
}

void Prof_Moment( const char *name ) {
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
	prof.paused = paused;
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

int32_t Prof_AnalyzeFunctions( profFunctionStat_t *out, int32_t maxCount, qboolean hasSelectedFrame, int64_t selectedFrameBeginUs, int64_t selectedFrameEndUs ) {
	int32_t statCount = 0;
	int32_t threadIndex;

	for ( threadIndex = 0; threadIndex < prof.threadCount; threadIndex++ ) {
		profThread_t *t = prof.threads[threadIndex];
		uint32_t count = t->eventWriteIndex < PROF_MAX_EVENTS ? t->eventWriteIndex : PROF_MAX_EVENTS;
		uint32_t i;

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
