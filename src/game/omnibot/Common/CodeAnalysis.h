#ifndef CODE_ANALYSIS_MACROS
#define CODE_ANALYSIS_MACROS

#ifdef CODE_ANALYSIS
#include <sal.h>
#define CHECK_PRINTF_ARGS			_Printf_format_string_
#define CHECK_PARAM_VALID			_In_
#define CHECK_VALID_BYTES(parm)		_In_reads_bytes_(parm)
#else
#define CHECK_PRINTF_ARGS
#define CHECK_PARAM_VALID
#define CHECK_VALID_BYTES(parm)
#endif

#endif
