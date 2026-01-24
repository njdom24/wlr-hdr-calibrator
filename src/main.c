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

// Interpolate LUT for a normalized 0-1 position
static double interpolate_lut(double pos) {
    if (lut_size == 0) return pos; // fallback
    double x = pos * HDR_MAX_NITS;
    // clamp outside bounds
    if (x <= lut[0].input) return lut[0].output / HDR_MAX_NITS;
    if (x >= lut[lut_size-1].input) return lut[lut_size-1].output / HDR_MAX_NITS;

    // find interval
    for (size_t i = 0; i < lut_size - 1; i++) {
        if (x >= lut[i].input && x <= lut[i+1].input) {
            double t = (x - lut[i].input) / (lut[i+1].input - lut[i].input);
            double val = lut[i].output + t * (lut[i+1].output - lut[i].output);
            return val / HDR_MAX_NITS;
        }
    }
    return pos; // fallback
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
        double pos = (double)i / (ramp_size - 1);
        double val = interpolate_lut(pos);

        if (val > 1.0) val = 1.0;
        if (val < 0.0) val = 0.0;

        r[i] = g[i] = b[i] = (uint16_t)(UINT16_MAX * val);
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
