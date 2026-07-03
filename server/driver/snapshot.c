/* NetHackDCC engine driver -- state snapshot (Phase 0, final task)
 *
 * Like glyphdecode.c, this translation unit includes hack.h (main.c stays
 * header-free) and must be compiled with the same defines libnh.a itself
 * was built with (see Makefile's ENGINEFLAGS). Exposes dcc_emit_snapshot(),
 * callable from any shim callback: u and gi.invent are process globals,
 * coherent at any point once a game is in progress, not just inside
 * whichever callback happens to be running.
 */

#include "hack.h"

/* Inventory letters/object names are always plain ASCII with no quote or
 * backslash characters, but escape defensively rather than assume it. */
static void
json_string(const char *s)
{
    putchar('"');
    for (const unsigned char *p = (const unsigned char *) s; *p; p++) {
        unsigned char c = *p;
        if (c == '"' || c == '\\')
            putchar('\\');
        if (c >= 0x20)
            putchar((int) c);
    }
    putchar('"');
}

/* One NDJSON line: {"cb":"__snapshot","u":{x,y,hp,hpmax,ac,level,exp},
 * "invent":[{letter,otyp,quan,oclass,name}, ...]} */
void
dcc_emit_snapshot(void)
{
    struct obj *otmp;

    printf("{\"cb\":\"__snapshot\",\"u\":{\"x\":%d,\"y\":%d,\"hp\":%d,"
           "\"hpmax\":%d,\"ac\":%d,\"level\":%d,\"exp\":%ld},\"invent\":[",
           (int) u.ux, (int) u.uy, u.uhp, u.uhpmax, (int) u.uac, u.ulevel,
           u.uexp);

    for (otmp = gi.invent; otmp; otmp = otmp->nobj) {
        if (otmp != gi.invent)
            putchar(',');
        printf("{\"letter\":\"%c\",\"otyp\":%d,\"quan\":%ld,\"oclass\":%d,"
               "\"name\":",
               otmp->invlet, (int) otmp->otyp, otmp->quan,
               (int) otmp->oclass);
        json_string(OBJ_NAME(objects[otmp->otyp]));
        putchar('}');
    }

    fputs("]}\n", stdout);
    fflush(stdout);
}
