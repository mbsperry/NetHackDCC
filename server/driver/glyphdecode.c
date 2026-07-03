/* NetHackDCC engine driver -- glyph decoding (Phase 0, task 4)
 *
 * main.c stays header-free (see its own file comment); this translation
 * unit is a deliberate exception (see also snapshot.c), because decoding
 * a glyph_info into {ch, color, monIdx} needs the engine's own
 * glyph_info/glyph_map struct layouts and the glyph-band macros in
 * include/display.h -- both live behind hack.h. It MUST be compiled with
 * the same preprocessor defines
 * used to build libnh.a (see Makefile's ENGINEFLAGS), since some of those
 * defines gate struct fields (e.g. ENHANCED_SYMBOLS on glyph_map) and a
 * mismatch would silently misalign the struct the engine actually wrote.
 *
 * Exposed as a small opaque-pointer API (dcc_decode_glyph) so main.c can
 * call it without including hack.h itself.
 */

#include "hack.h"

/* ch: the resolved display character for the current symset (already
 *     picked by the engine's map_glyphinfo(), same value tty/curses would
 *     print).
 * color: the tty/ANSI color index (CLR_*) the engine chose for this glyph.
 * monIdx: monster index (0..NUMMONS-1, glyph_to_mon() via the glyph-band
 *     macros in include/display.h) if this glyph is any monster band
 *     (normal/pet/ridden/detected, male or female); -1 otherwise. */
void
dcc_decode_glyph(const void *glyphinfo_ptr, int *ch, int *color, int *monIdx)
{
    const glyph_info *gi = (const glyph_info *) glyphinfo_ptr;

    if (!gi) {
        *ch = ' ';
        *color = NO_COLOR;
        *monIdx = -1;
        return;
    }

    *ch = (int) gi->ttychar;
    *color = (int) gi->gm.sym.color;
    *monIdx = glyph_is_monster(gi->glyph) ? glyph_to_mon(gi->glyph) : -1;
}
