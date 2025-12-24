#ifndef BRZ_SIM_H
#define BRZ_SIM_H

#include "brz_dsl.h"

/* Simulation runner.
   Executes vocations/rules/tasks over a number of cycles and prints
   interactions and key values over time. */
int brz_run(const ParsedConfig* cfg);

#endif /* BRZ_SIM_H */
