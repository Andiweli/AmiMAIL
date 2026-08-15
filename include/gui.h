#ifndef AMIGMAIL_GUI_H
#define AMIGMAIL_GUI_H

#include "account.h"
#include "mailto.h"

typedef struct AmgGui AmgGui;

AmgGui *amg_gui_create(AmgAccount *account, AmgError *error);
int amg_gui_run(AmgGui *gui, AmgMailtoServer *mailto_server,
                const char *startup_mailto, AmgError *error);
void amg_gui_destroy(AmgGui *gui);

#endif
