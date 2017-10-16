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

/* custom typemap for handling ratbag_resolution_get_dpi */
%typemap(in) (unsigned int *resolutions, size_t nres) {
  unsigned int i;
  if (!PyList_Check($input)) {
    PyErr_SetString(PyExc_ValueError, "Expecting a list");
    return NULL;
  }
  $2 = PyList_Size($input);
  $1 = (unsigned int *) malloc(($2 + 1) * sizeof(unsigned int));
  for (i = 0; i < $2; i++) {
    PyObject *s = PyList_GetItem($input, i);
    if (!PyInt_Check(s)) {
       free($1);
       PyErr_SetString(PyExc_ValueError, "List items must be integers");
       return NULL;
    }
    $1[i] = PyInt_AsLong(s);
  }
  $1[i] = 0;
}

%typemap(argout) (unsigned int *resolutions, size_t nres) {
  unsigned int i;
  for (i = 0; i < $2; i++) {
    PyList_SetItem($input, i, PyInt_FromLong($1[i]));
  }
}

%typemap(freearg) (unsigned int *resolutions, size_t nres) {
  if ($1)
    free($1);
}
/* END OF custom typemap for handling ratbag_resolution_get_dpi */

/*  Parse the header file to generate wrappers */
%include "libratbag.h"
%include "libratbag-enums.h"
%include "shared.h"
