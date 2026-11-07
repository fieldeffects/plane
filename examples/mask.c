/* mask.c — the README example: write and re-read an 8x8 bitplane mask. */
#include "../plane.h"

#include <assert.h>

int main(void) {
    Plane m;
    if (plane_alloc(&m, 8, 8, 0)) {        /* 8x8 bitplane mask */
        plane_set(&m, 3, 5, 1);
        assert(plane_get(&m, 3, 5) == 1);
        m.flags = 1;                       /* your convention: 1 channel */
        if (!plane_save(&m, "fog_mask.plane")) return 1;
        plane_free(&m);
    }

    /* read it back */
    FILE *f = fopen("fog_mask.plane", "rb");
    if (!f) return 1;
    uint8_t buf[16 + 8];
    if (fread(buf, 1, sizeof buf, f) != sizeof buf) return 1;
    fclose(f);

    Plane r;
    if (!plane_load(&r, buf, sizeof buf)) return 1;
    assert(r.width == 8 && r.height == 8 && r.shift == 0 && r.flags == 1);
    assert(plane_get(&r, 3, 5) == 1);
    assert(plane_get(&r, 0, 0) == 0);
    printf("fog_mask.plane: %ux%u, shift=%u, flags=%u, bit(3,5)=%llu\n",
           r.width, r.height, r.shift, r.flags,
           (unsigned long long)plane_get(&r, 3, 5));
    remove("fog_mask.plane");
    return 0;
}
