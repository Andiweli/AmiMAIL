#include "app.h"
#include "amigmail.h"

/* Kept as a real binary string on purpose so it is easy to find in a
 * hex editor or text scan of the executable. main.o is linked first. */
static const char client_identification[] __attribute__((used)) =
    "AmiMail Client 1.0 RC2 by Andreas 'Andiweli' St\374rmer";
static const char version[] __attribute__((used)) =
    "$VER: AmiMail 1.0 RC2 (15.08.2026)";

int main(int argc, char **argv)
{
    (void)client_identification;
    (void)version;
    return amg_app_run(argc, argv);
}
