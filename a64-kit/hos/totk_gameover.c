/* TOTK 1.0.0: finish the Game Over event before its requested pause takes effect.

   DmF_SY_GameOver requests GameOverPause and immediately waits one game frame.
   At the original 30 Hz that wait completes in its first update. At 60 Hz its
   timer advances by only 0.5, then the pause prevents another event update:
   Continue has already selected Retry, but the event never ends.

   Treat only the next one-frame wait in the requesting actor's AI instance as
   a completion barrier. Keep the pause and the ordinary timing of all other
   waits. No RomFS changes. HOS_GAMEOVER_FENCE=0 disables this compatibility fix.
   HOS_GAMEOVER_FENCE_LOG=1 logs each corrected barrier for reproduction tests.
*/
#include "hos.h"
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#if HOS_GAME_VERSION == 100
/* Actions of the same AI instance can run on different worker threads. */
static volatile LONG64 pause_owner;
static _Thread_local int fence_mode;

static int fence_enabled(void) {
    if (!fence_mode) {
        const char *e = getenv("HOS_GAMEOVER_FENCE");
        fence_mode = e && *e == '0' ? -1 : 1;
    }
    return fence_mode > 0;
}

/* OneShotPauseMgrRequestPause: +32 is the tagged PauseType string parameter;
   +8 identifies the AI instance shared with its following wait action. */
ENTRY_HOOK(0x09e73d60) {
    if (!fence_enabled()) return 0;
    const uint64_t node = ARG(0);
    const uint64_t parameter = G64(node + 32) & ~3ULL;
    if (!parameter) return 0;
    const uint64_t name = G64(parameter);
    if (name && !strcmp((const char *)GP(name), "GameOverPause")) {
        const uint64_t owner = G64(node + 8);
        InterlockedExchange64(&pause_owner, (LONG64)owner);
        if (getenv("HOS_GAMEOVER_FENCE_LOG"))
            hos_log("gameover: armed pause barrier (AI %#llx)", (unsigned long long)owner);
    }
    return 0;  /* The original pause request must still run. */
}

/* ExecuteGenericWaitFramByPlug: +56 is the tagged frame-count parameter;
   +32 is elapsed game frames. Consume the marker on this AI's next wait only. */
ENTRY_HOOK(0x0a85e774) {
    const uint64_t owner = (uint64_t)InterlockedCompareExchange64(&pause_owner, 0, 0);
    if (!owner) return 0;
    const uint64_t node = ARG(0);
    if (G64(node + 8) != owner) return 0;
    if ((uint64_t)InterlockedCompareExchange64(&pause_owner, 0, (LONG64)owner) != owner) return 0;
    const uint64_t parameter = G64(node + 56) & ~3ULL;
    if (!parameter || G32(parameter) != 1) return 0;
    float elapsed;
    const uint32_t bits = G32(node + 32);
    memcpy(&elapsed, &bits, sizeof elapsed);
    if (elapsed >= 0.0f && elapsed < 1.0f) {
        recomp_store32(c, node + 32, 0x3f800000u); /* 1.0f */
        if (getenv("HOS_GAMEOVER_FENCE_LOG"))
            hos_log("gameover: completed one-frame pause barrier (AI %#llx)",
                    (unsigned long long)owner);
    }
    return 0;  /* Let the guest perform its normal completion transition. */
}
#endif
