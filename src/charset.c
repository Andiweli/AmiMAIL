#include "charset.h"
#include "codec.h"

/* Charset conversion currently uses the built-in codec implementation.
 * codesets.library remains optional and is not required at runtime. */
int amg_charset_module_present(void){return 1;}
