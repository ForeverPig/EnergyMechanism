#ifndef RESERVED_FEATURES_H
#define RESERVED_FEATURES_H
#include <stdbool.h>
/* Final LED topology and parameter persistence are intentionally unimplemented. */
void LedOutput_Disable(void);
bool ParameterStorage_Available(void);
#endif
