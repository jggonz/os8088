/* Breadboard XT for os8088. Original implementation; SPEC.md 130.
 * One translation unit, exclusive modules composed here. Core host tests
 * define BB_HOST and supply opaque arithmetic adapters before this file. */
#ifndef BB_HOST
#include "os88.h"
#endif
#include "bb.h"
BBDoc bb_doc,bb_stage;
BBResult bb_result;
BBScope bb_scope;
BBByte bb_hnet[BB_HOLES];
BBWord bb_nnet,bb_error,bb_error_part,bb_error_net,bb_num_error;
BBWord bb_dirty,bb_running,bb_valid,bb_recompile;
#include "model.c"
#include "sim.c"
#include "file.c"
#include "examples.c"
#ifndef BB_HOST
#include "ui.c"
#endif
