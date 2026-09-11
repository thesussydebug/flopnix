/* An invalid extension used to test unresolved-symbol rejection. */
#include "kapi.h"
extern void missing_import(void);
const KextHeader kext_header={KEXT_MAGIC,KAPI_VERSION,KEXT_KIND_APP,0,"Import test"};
int kext_entry(const Kapi *k){(void)k;missing_import();return 0;}
