/* wrapping libratbag functions from libratbag.h using SWIG. */

%module libratbag
%{
	/* the resulting C file should be built as a python extension */
	#define SWIG_FILE_WITH_INIT
	/*  Includes the header in the wrapper code */
	#include <libratbag.h>
	#include <shared.h>
%}
#define __attribute__(x)
/*  Parse the header file to generate wrappers */
%include "libratbag.h"
%include "libratbag-enums.h"
%include "shared.h"
