/* Unit tests for the name and stream0 parsers (built together with src/rate_bridge.c; RATE_BRIDGE_ASOUND points at
 * tests/fake_asound so card ids resolve). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/rate_bridge.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static char *slurp(const char *p) {
    FILE *f = fopen(p, "r"); if (!f) { printf("cannot read %s\n", p); exit(1); }
    static char b[8192]; size_t n = fread(b, 1, sizeof b - 1, f); fclose(f); b[n] = 0;
    return strdup(b);
}

int main(void) {
    int card, dev; char id[64];
    CHECK(rb_parse_hw_name("hw:2", &card, &dev, id, sizeof id) && card == 2 && dev == 0);
    CHECK(rb_parse_hw_name("hw:2,1", &card, &dev, id, sizeof id) && card == 2 && dev == 1);
    CHECK(rb_parse_hw_name("hw:CARD=mini,DEV=0", &card, &dev, id, sizeof id) && card == 2 && dev == 0 && !strcmp(id, "mini"));
    CHECK(rb_parse_hw_name("hw:CARD=mini", &card, &dev, id, sizeof id) && card == 2 && dev == 0);
    CHECK(rb_parse_hw_name("hw:mini", &card, &dev, id, sizeof id) && card == 2);
    CHECK(!rb_parse_hw_name("hw:nosuchcard", &card, &dev, id, sizeof id));
    CHECK(!rb_parse_hw_name("plughw:2", &card, &dev, id, sizeof id));
    CHECK(!rb_parse_hw_name("default", &card, &dev, id, sizeof id));
    CHECK(!rb_parse_hw_name("hw:2,0,1", &card, &dev, id, sizeof id));   /* subdevice: left alone */
    CHECK(!rb_parse_hw_name("hw:", &card, &dev, id, sizeof id));
    CHECK(!rb_parse_hw_name(NULL, &card, &dev, id, sizeof id));

    int has44; unsigned best;
    char *qc = slurp("tests/fake_asound/card2/stream0");
    CHECK(rb_stream_rates(qc, 0, &has44, &best) == 1 && !has44 && best == 48000);
    CHECK(rb_stream_rates(qc, 1, &has44, &best) == 1 && !has44 && best == 48000);
    char *both = slurp("tests/fake_asound/card3/stream0");
    CHECK(rb_stream_rates(both, 0, &has44, &best) == 1 && has44 && best == 48000);
    CHECK(rb_stream_rates(both, 1, &has44, &best) == 1 && has44);
    char *cont = slurp("tests/fake_asound/card4/stream0");
    CHECK(rb_stream_rates(cont, 0, &has44, &best) == 1 && has44);
    CHECK(rb_stream_rates(cont, 1, &has44, &best) == 0);   /* no capture section */
    CHECK(rb_stream_rates("Playback:\n    Rates: 96000\n", 0, &has44, &best) == 1 && !has44 && best == 96000);
    CHECK(rb_stream_rates("Capture:\n    Rates: 32000, 48000\nPlayback:\n    Rates: 44100\n", 1, &has44, &best) == 1 && !has44 && best == 48000);
    CHECK(rb_stream_rates("Capture:\n    Rates: 32000, 48000\nPlayback:\n    Rates: 44100\n", 0, &has44, &best) == 1 && has44);
    CHECK(rb_stream_rates("", 0, &has44, &best) == 0);

    free(qc); free(both); free(cont);
    printf(fails ? "unit: %d FAILED\n" : "unit: all passed\n", fails);
    return fails != 0;
}
