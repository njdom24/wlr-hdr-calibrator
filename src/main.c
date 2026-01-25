#define _POSIX_C_SOURCE 200809L
#include "wlr-gamma-control-unstable-v1-client-protocol.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>

#define HDR_MAX_NITS 10000.0
#define MAX_LUT_POINTS 1024

struct lut_point {
    double input;
    double output;
};

struct output {
    struct wl_output *wl_output;
    struct zwlr_gamma_control_v1 *gamma_control;
    uint32_t ramp_size;
    int table_fd;
    uint16_t *table;
    struct wl_list link;
};

static struct zwlr_gamma_control_manager_v1 *gamma_control_manager = NULL;
static struct wl_display *display = NULL;
static struct wl_list outputs;

static struct lut_point lut[MAX_LUT_POINTS];
static size_t lut_size = 0;

// ST2084 constants
static const double PQ_M1 = 2610.0 / 16384.0;
static const double PQ_M2 = 2523.0 / 32.0;
static const double PQ_C1 = 3424.0 / 4096.0;
static const double PQ_C2 = 2413.0 / 128.0;
static const double PQ_C3 = 2392.0 / 128.0;

static double pq_decode(double pq) {
    // PQ signal [0..1] -> linear nits normalized to 10000
    double x = pow(pq, 1.0 / PQ_M2);
    double num = fmax(x - PQ_C1, 0.0);
    double den = PQ_C2 - PQ_C3 * x;
    double l = pow(num / den, 1.0 / PQ_M1);
    return l; // 0..1 relative to 10000 nits
}

static double pq_encode(double l) {
    // linear nits normalized to 10000 -> PQ signal [0..1]
    l = fmax(l, 0.0);
    double x = pow(l, PQ_M1);
    double num = PQ_C1 + PQ_C2 * x;
    double den = 1.0 + PQ_C3 * x;
    return pow(num / den, PQ_M2);
}

// Interpolate LUT for a normalized 0-1 position
static double interpolate_lut(double pq_in) {
    if (lut_size == 0)
        return pq_in;

    // Convert PQ signal -> linear nits (0..1 relative to 10k)
    double lin_in = pq_decode(pq_in) * HDR_MAX_NITS;

    // Clamp to LUT domain
    if (lin_in <= lut[0].input)
        return pq_encode(lut[0].output / HDR_MAX_NITS);

    if (lin_in >= lut[lut_size-1].input)
        return pq_encode(lut[lut_size-1].output / HDR_MAX_NITS);

    // Find interval in nits
    for (size_t i = 0; i < lut_size - 1; i++) {
        if (lin_in >= lut[i].input && lin_in <= lut[i+1].input) {
            double t = (lin_in - lut[i].input) /
                       (lut[i+1].input - lut[i].input);

            double lin_out =
                lut[i].output +
                t * (lut[i+1].output - lut[i].output);

            // Convert corrected linear nits -> PQ signal
            return pq_encode(lin_out / HDR_MAX_NITS);
        }
    }

    return pq_in;
}

static int create_anonymous_file(off_t size) {
    char template[] = "/tmp/wlroots-shared-XXXXXX";
    int fd = mkstemp(template);
    if (fd < 0) return -1;
    int ret;
    do { errno=0; ret=ftruncate(fd,size); } while (errno==EINTR);
    if (ret < 0) { close(fd); return -1; }
    unlink(template);
    return fd;
}

static int create_gamma_table(uint32_t ramp_size, uint16_t **table) {
    size_t table_size = ramp_size * 3 * sizeof(uint16_t);
    int fd = create_anonymous_file(table_size);
    if (fd < 0) return -1;
    void *data = mmap(NULL, table_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) { close(fd); return -1; }
    *table = data;
    return fd;
}

static void fill_gamma_table(uint16_t *table, uint32_t ramp_size) {
    uint16_t *r = table;
    uint16_t *g = table + ramp_size;
    uint16_t *b = table + 2*ramp_size;

    for (uint32_t i = 0; i < ramp_size; i++) {
        double pq_in = (double)i / (ramp_size - 1);
        double pq_out = interpolate_lut(pq_in);

        pq_out = fmin(fmax(pq_out, 0.0), 1.0);

        uint16_t v = (uint16_t)(UINT16_MAX * pq_out);
        r[i] = g[i] = b[i] = v;
    }
}

static void gamma_control_handle_gamma_size(void *data,
        struct zwlr_gamma_control_v1 *gamma_control,
        uint32_t ramp_size) {
    struct output *output = data;
    output->ramp_size = ramp_size;
}

static void gamma_control_handle_failed(void *data,
        struct zwlr_gamma_control_v1 *gamma_control) {
    fprintf(stderr, "failed to set gamma table\n");
    exit(EXIT_FAILURE);
}

static const struct zwlr_gamma_control_v1_listener gamma_control_listener = {
    .gamma_size = gamma_control_handle_gamma_size,
    .failed = gamma_control_handle_failed
};

static void registry_handle_global(void *data, struct wl_registry *registry,
        uint32_t name, const char *interface, uint32_t version) {
    if (strcmp(interface, wl_output_interface.name) == 0) {
        struct output *out = calloc(1,sizeof(struct output));
        out->wl_output = wl_registry_bind(registry,name,&wl_output_interface,1);
        wl_list_insert(&outputs,&out->link);
    } else if (strcmp(interface, zwlr_gamma_control_manager_v1_interface.name) == 0) {
        gamma_control_manager = wl_registry_bind(registry,name,
                &zwlr_gamma_control_manager_v1_interface,1);
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry,
        uint32_t name) { }

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove
};

static void apply_lut() {
    struct output *out;
    wl_list_for_each(out,&outputs,link) {
        out->table_fd = create_gamma_table(out->ramp_size,&out->table);
        if (out->table_fd < 0) exit(EXIT_FAILURE);
        fill_gamma_table(out->table,out->ramp_size);
        zwlr_gamma_control_v1_set_gamma(out->gamma_control,out->table_fd);
        close(out->table_fd);
    }
    wl_display_roundtrip(display);
}

static int load_lut_file(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) { perror("fopen"); return -1; }
    lut_size = 0;
    while (fscanf(f,"%lf %lf",&lut[lut_size].input,&lut[lut_size].output) == 2) {
        lut_size++;
        if (lut_size >= MAX_LUT_POINTS) break;
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <lut.txt>\n",argv[0]);
        return 1;
    }

    if (load_lut_file(argv[1]) != 0) return 1;

    wl_list_init(&outputs);
    display = wl_display_connect(NULL);
    if (!display) { fprintf(stderr, "cannot connect to display\n"); return 1; }

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry,&registry_listener,NULL);
    wl_display_dispatch(display);
    wl_display_roundtrip(display);

    if (!gamma_control_manager) { fprintf(stderr, "compositor does not support wlr-gamma-control\n"); return 1; }

    struct output *out;
    wl_list_for_each(out,&outputs,link) {
        out->gamma_control = zwlr_gamma_control_manager_v1_get_gamma_control(
                gamma_control_manager,out->wl_output);
        zwlr_gamma_control_v1_add_listener(out->gamma_control,
                &gamma_control_listener,out);
    }
    wl_display_roundtrip(display);

    apply_lut();

    // Keep running so gamma stays applied
    while (wl_display_dispatch(display) != -1) { }
    return 0;
}
