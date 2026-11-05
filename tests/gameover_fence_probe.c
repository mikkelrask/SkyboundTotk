/* Scope/concurrency regression for the real compatibility hooks, without ROM
   data. This does not replace the death/Continue integration reproduction. */
#include "hos.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

int entry_0x09e73d60(Ctx *c);
int entry_0x0a85e774(Ctx *c);
void hos_log(const char *fmt, ...) { (void)fmt; }

enum { PAUSE = 0x1000, STRING_PARAM = 0x2000, NAME = 0x3000,
       OWNER = 0x4000, WAIT = 0x5000, FRAMES = 0x6000,
       OTHER_WAIT = 0x7000, OTHER_OWNER = 0x8000 };

static void *request(void *unused) {
    (void)unused;
    Ctx c = {0}; c.x[0] = PAUSE;
    assert(entry_0x09e73d60(&c) == 0);
    return NULL;
}
static void wait_node(uint64_t node) {
    Ctx c = {0}; c.x[0] = node;
    assert(entry_0x0a85e774(&c) == 0);
}
static void request_on_worker(void) {
    pthread_t worker;
    assert(pthread_create(&worker, NULL, request, NULL) == 0);
    assert(pthread_join(worker, NULL) == 0);
}

int main(void) {
    void *p = mmap((void *)A64_HOST_BASE, 0x10000, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(p != MAP_FAILED);
    G64(PAUSE + 8) = OWNER;
    G64(PAUSE + 32) = STRING_PARAM | 1;
    G64(STRING_PARAM) = NAME;
    strcpy(GP(NAME), "GameOverPause");
    G64(WAIT + 8) = OWNER;
    G64(WAIT + 56) = FRAMES | 2;
    G64(OTHER_WAIT + 8) = OTHER_OWNER;
    G64(OTHER_WAIT + 56) = FRAMES | 2;
    G32(FRAMES) = 1;

    /* A worker requests the pause; the main thread runs both waits. An
       unrelated AI must not steal the marker between the related actions. */
    request_on_worker();
    wait_node(OTHER_WAIT);
    assert(G32(OTHER_WAIT + 32) == 0);
    wait_node(WAIT);
    const char *disabled = getenv("HOS_GAMEOVER_FENCE");
    if (disabled && *disabled == '0') {
        assert(G32(WAIT + 32) == 0);
        puts("disabled: no timer changes");
        return 0;
    }
    assert(G32(WAIT + 32) == 0x3f800000u);

    G32(WAIT + 32) = 0;
    wait_node(WAIT);
    assert(G32(WAIT + 32) == 0); /* One use, not a lasting timing override. */
    G32(FRAMES) = 2;
    request_on_worker();
    wait_node(WAIT);
    assert(G32(WAIT + 32) == 0);
    G32(FRAMES) = 1;
    wait_node(WAIT);
    assert(G32(WAIT + 32) == 0); /* A rejected target also consumes the marker. */
    strcpy(GP(NAME), "MenuPause");
    request_on_worker();
    wait_node(WAIT);
    assert(G32(WAIT + 32) == 0);
    puts("cross-thread handoff, unrelated AI, one-use and pause/target guards passed");
    munmap(p, 0x10000);
    return 0;
}
