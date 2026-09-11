/* Runs the shared app logic tests on the host. */
#include <stdio.h>
#include "kapi.h"
static int pass, fail;
#define CHECK(x) do { if (x) pass++; else { fail++; printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)
#define APP_TEST_PHASE(s) puts("APP CORE: " s)
#include "newapps_tests.inc"
int main(void) { t_newapps(); printf("APP CORES: %d pass %d fail\n", pass, fail); return fail != 0; }
