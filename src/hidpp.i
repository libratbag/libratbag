/* wrapping libratbag functions from hidpp*.h using SWIG. */

%module hidpp
%{
	/* the resulting C file should be built as a python extension */
	#define SWIG_FILE_WITH_INIT
	/*  Includes the header in the wrapper code */
	#include <shared.h>
	#include <hidpp-generic.h>
	#include <hidpp20.h>
%}
#define __attribute__(x)
#define _Static_assert(a,b)


/*  Parse the header file to generate wrappers */
%include "stdint.i"
%include "cpointer.i"
%pointer_functions(int, intp);
%pointer_functions(uint8_t, uint8_tp);
%pointer_functions(uint16_t, uint16_tp);
%include "hidpp-generic.h"
%include "hidpp20.h"
