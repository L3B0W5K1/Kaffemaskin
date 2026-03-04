/**
 * gpio_object.c - Custom LwM2M Object 26241: GPIO Controller
 *
 * Uses Linux sysfs GPIO interface (/sys/class/gpio/) for broad compatibility
 * with Raspberry Pi models. No external libraries needed.
 */

#include "gpio_object.h"

#include <anjay/anjay.h>
#include <avsystem/commons/avs_list.h>
#include <avsystem/commons/avs_log.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/*  Low-level sysfs GPIO helpers                                      */
/* ------------------------------------------------------------------ */

static int gpio_export(int pin) {
    char buf[64];
    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd < 0) {
        snprintf(buf, sizeof(buf), "/sys/class/gpio/gpio%d/direction", pin);
        if (access(buf, F_OK) == 0) return 0;   /* already exported */
        avs_log(gpio_obj, ERROR, "Cannot open /sys/class/gpio/export: %s",
                 strerror(errno));
        return -1;
    }
    int len = snprintf(buf, sizeof(buf), "%d", pin);
    write(fd, buf, len);
    close(fd);
    usleep(100000);  /* give kernel time to create sysfs entries */
    return 0;
}

static int gpio_unexport(int pin) {
    char buf[64];
    int fd = open("/sys/class/gpio/unexport", O_WRONLY);
    if (fd < 0) return -1;
    int len = snprintf(buf, sizeof(buf), "%d", pin);
    write(fd, buf, len);
    close(fd);
    return 0;
}

static int gpio_set_direction(int pin, const char *dir) {
    char path[128];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", pin);
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        avs_log(gpio_obj, ERROR, "Cannot set direction for GPIO %d: %s",
                 pin, strerror(errno));
        return -1;
    }
    write(fd, dir, strlen(dir));
    close(fd);
    return 0;
}

static int gpio_write(int pin, int value) {
    char path[128];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        avs_log(gpio_obj, ERROR, "Cannot write GPIO %d: %s",
                 pin, strerror(errno));
        return -1;
    }
    const char *v = value ? "1" : "0";
    write(fd, v, 1);
    close(fd);
    return 0;
}

static int gpio_read(int pin) {
    char path[128], val[4] = {0};
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    read(fd, val, sizeof(val) - 1);
    close(fd);
    return atoi(val);
}

/* ------------------------------------------------------------------ */
/*  Instance data                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    anjay_iid_t iid;

    int      gpio_pin;           /* BCM pin number                   */
    bool     gpio_state;         /* current logical state             */
    int      pulse_duration_ms;  /* how long Activate keeps pin HIGH  */
    char     description[64];    /* human-readable label              */

    bool     pin_exported;       /* have we exported this pin?        */

    /* Pulse timer: when activate_deadline > 0, pin is in pulse mode  */
    struct timespec activate_deadline;
} gpio_instance_t;

typedef struct {
    const anjay_dm_object_def_t *def;
    gpio_instance_t instances[8]; /* support up to 8 GPIO instances */
    int num_instances;
} gpio_object_t;

static gpio_object_t *GPIO_OBJ = NULL;

/* ------------------------------------------------------------------ */
/*  Helper: get instance by IID                                        */
/* ------------------------------------------------------------------ */

static gpio_instance_t *find_instance(gpio_object_t *obj, anjay_iid_t iid) {
    for (int i = 0; i < obj->num_instances; i++) {
        if (obj->instances[i].iid == iid) return &obj->instances[i];
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Helper: setup GPIO hardware for an instance                        */
/* ------------------------------------------------------------------ */

static int setup_gpio_hw(gpio_instance_t *inst) {
    if (inst->pin_exported) {
        gpio_unexport(inst->gpio_pin);
        inst->pin_exported = false;
    }
    if (gpio_export(inst->gpio_pin) != 0) return -1;
    inst->pin_exported = true;
    if (gpio_set_direction(inst->gpio_pin, "out") != 0) return -1;
    gpio_write(inst->gpio_pin, inst->gpio_state ? 1 : 0);
    avs_log(gpio_obj, INFO, "GPIO %d configured as output (state=%d)",
             inst->gpio_pin, inst->gpio_state);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Time helpers                                                       */
/* ------------------------------------------------------------------ */

static struct timespec timespec_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts;
}

static bool timespec_is_zero(const struct timespec *ts) {
    return ts->tv_sec == 0 && ts->tv_nsec == 0;
}

static bool timespec_passed(const struct timespec *deadline) {
    struct timespec now = timespec_now();
    return (now.tv_sec > deadline->tv_sec) ||
           (now.tv_sec == deadline->tv_sec && now.tv_nsec >= deadline->tv_nsec);
}

/* ------------------------------------------------------------------ */
/*  LwM2M Object handlers                                             */
/* ------------------------------------------------------------------ */

static int list_instances(anjay_t *anjay, const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_dm_list_ctx_t *ctx) {
    (void)anjay;
    gpio_object_t *obj = (gpio_object_t *)obj_ptr;
    for (int i = 0; i < obj->num_instances; i++) {
        anjay_dm_emit(ctx, obj->instances[i].iid);
    }
    return 0;
}

static int list_resources(anjay_t *anjay, const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_iid_t iid, anjay_dm_resource_list_ctx_t *ctx) {
    (void)anjay; (void)obj_ptr; (void)iid;
    anjay_dm_emit_res(ctx, RID_GPIO_PIN,       ANJAY_DM_RES_RW,  ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_GPIO_STATE,     ANJAY_DM_RES_RW,  ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_ACTIVATE,       ANJAY_DM_RES_E,   ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_DEACTIVATE,     ANJAY_DM_RES_E,   ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_PULSE_DURATION, ANJAY_DM_RES_RW,  ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx, RID_DESCRIPTION,    ANJAY_DM_RES_R,   ANJAY_DM_RES_PRESENT);
    return 0;
}

static int resource_read(anjay_t *anjay, const anjay_dm_object_def_t *const *obj_ptr,
                         anjay_iid_t iid, anjay_rid_t rid, anjay_riid_t riid,
                         anjay_output_ctx_t *ctx) {
    (void)anjay; (void)riid;
    gpio_object_t *obj = (gpio_object_t *)obj_ptr;
    gpio_instance_t *inst = find_instance(obj, iid);
    if (!inst) return ANJAY_ERR_NOT_FOUND;

    switch (rid) {
    case RID_GPIO_PIN:
        return anjay_ret_i32(ctx, inst->gpio_pin);
    case RID_GPIO_STATE:
        if (inst->pin_exported) {
            inst->gpio_state = gpio_read(inst->gpio_pin) ? true : false;
        }
        return anjay_ret_bool(ctx, inst->gpio_state);
    case RID_PULSE_DURATION:
        return anjay_ret_i32(ctx, inst->pulse_duration_ms);
    case RID_DESCRIPTION:
        return anjay_ret_string(ctx, inst->description);
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int resource_write(anjay_t *anjay, const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_iid_t iid, anjay_rid_t rid, anjay_riid_t riid,
                          anjay_input_ctx_t *ctx) {
    (void)anjay; (void)riid;
    gpio_object_t *obj = (gpio_object_t *)obj_ptr;
    gpio_instance_t *inst = find_instance(obj, iid);
    if (!inst) return ANJAY_ERR_NOT_FOUND;

    switch (rid) {
    case RID_GPIO_PIN: {
        int32_t pin;
        int ret = anjay_get_i32(ctx, &pin);
        if (ret) return ret;
        if (pin < 0 || pin > 27) {
            avs_log(gpio_obj, WARNING, "Invalid BCM pin %d (valid: 0-27)", pin);
            return ANJAY_ERR_BAD_REQUEST;
        }
        inst->gpio_pin = pin;
        setup_gpio_hw(inst);
        return 0;
    }
    case RID_GPIO_STATE: {
        bool state;
        int ret = anjay_get_bool(ctx, &state);
        if (ret) return ret;
        inst->gpio_state = state;
        if (inst->pin_exported) {
            gpio_write(inst->gpio_pin, state ? 1 : 0);
        }
        avs_log(gpio_obj, INFO, "GPIO %d set to %s",
                 inst->gpio_pin, state ? "HIGH" : "LOW");
        return 0;
    }
    case RID_PULSE_DURATION: {
        int32_t ms;
        int ret = anjay_get_i32(ctx, &ms);
        if (ret) return ret;
        if (ms < 0) return ANJAY_ERR_BAD_REQUEST;
        inst->pulse_duration_ms = ms;
        return 0;
    }
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

static int resource_execute(anjay_t *anjay, const anjay_dm_object_def_t *const *obj_ptr,
                            anjay_iid_t iid, anjay_rid_t rid,
                            anjay_execute_ctx_t *ctx) {
    (void)anjay; (void)ctx;
    gpio_object_t *obj = (gpio_object_t *)obj_ptr;
    gpio_instance_t *inst = find_instance(obj, iid);
    if (!inst) return ANJAY_ERR_NOT_FOUND;

    switch (rid) {
    case RID_ACTIVATE: {
        avs_log(gpio_obj, INFO,
                 "EXECUTE Activate: GPIO %d HIGH for %d ms",
                 inst->gpio_pin, inst->pulse_duration_ms);

        if (!inst->pin_exported) {
            if (setup_gpio_hw(inst) != 0) return ANJAY_ERR_INTERNAL;
        }
        gpio_write(inst->gpio_pin, 1);
        inst->gpio_state = true;

        struct timespec now = timespec_now();
        inst->activate_deadline.tv_sec  = now.tv_sec + (inst->pulse_duration_ms / 1000);
        inst->activate_deadline.tv_nsec = now.tv_nsec +
                                          (long)(inst->pulse_duration_ms % 1000) * 1000000L;
        if (inst->activate_deadline.tv_nsec >= 1000000000L) {
            inst->activate_deadline.tv_sec  += 1;
            inst->activate_deadline.tv_nsec -= 1000000000L;
        }
        return 0;
    }
    case RID_DEACTIVATE: {
        avs_log(gpio_obj, INFO,
                 "EXECUTE Deactivate: GPIO %d LOW", inst->gpio_pin);
        if (inst->pin_exported) {
            gpio_write(inst->gpio_pin, 0);
        }
        inst->gpio_state = false;
        memset(&inst->activate_deadline, 0, sizeof(inst->activate_deadline));
        return 0;
    }
    default:
        return ANJAY_ERR_METHOD_NOT_ALLOWED;
    }
}

/* ------------------------------------------------------------------ */
/*  Object definition                                                  */
/* ------------------------------------------------------------------ */

static const anjay_dm_object_def_t GPIO_OBJ_DEF = {
    .oid = GPIO_OBJECT_ID,
    .handlers = {
        .list_instances   = list_instances,
        .list_resources   = list_resources,
        .resource_read    = resource_read,
        .resource_write   = resource_write,
        .resource_execute = resource_execute,
    }
};

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

int gpio_object_install(anjay_t *anjay) {
    GPIO_OBJ = (gpio_object_t *)calloc(1, sizeof(gpio_object_t));
    if (!GPIO_OBJ) return -1;

    GPIO_OBJ->def = &GPIO_OBJ_DEF;

    GPIO_OBJ->num_instances = 1;
    gpio_instance_t *inst = &GPIO_OBJ->instances[0];
    inst->iid              = 0;
    inst->gpio_pin         = 17;
    inst->gpio_state       = false;
    inst->pulse_duration_ms = 1000;
    snprintf(inst->description, sizeof(inst->description), "RPi GPIO %d", inst->gpio_pin);

    setup_gpio_hw(inst);

    int ret = anjay_register_object(anjay, &GPIO_OBJ->def);
    if (ret) {
        avs_log(gpio_obj, ERROR, "Failed to register GPIO object");
        free(GPIO_OBJ);
        GPIO_OBJ = NULL;
        return ret;
    }

    avs_log(gpio_obj, INFO,
             "GPIO Object /%d installed (instance 0, pin %d)",
             GPIO_OBJECT_ID, inst->gpio_pin);
    return 0;
}

void gpio_object_update(anjay_t *anjay) {
    if (!GPIO_OBJ) return;

    for (int i = 0; i < GPIO_OBJ->num_instances; i++) {
        gpio_instance_t *inst = &GPIO_OBJ->instances[i];

        if (!timespec_is_zero(&inst->activate_deadline) &&
            timespec_passed(&inst->activate_deadline)) {

            avs_log(gpio_obj, INFO,
                     "Pulse complete: GPIO %d -> LOW", inst->gpio_pin);
            gpio_write(inst->gpio_pin, 0);
            inst->gpio_state = false;
            memset(&inst->activate_deadline, 0, sizeof(inst->activate_deadline));

            anjay_notify_changed(anjay, GPIO_OBJECT_ID, inst->iid, RID_GPIO_STATE);
        }
    }
}

void gpio_object_cleanup(void) {
    if (!GPIO_OBJ) return;

    for (int i = 0; i < GPIO_OBJ->num_instances; i++) {
        gpio_instance_t *inst = &GPIO_OBJ->instances[i];
        if (inst->pin_exported) {
            gpio_write(inst->gpio_pin, 0);
            gpio_unexport(inst->gpio_pin);
        }
    }
    free(GPIO_OBJ);
    GPIO_OBJ = NULL;
    avs_log(gpio_obj, INFO, "GPIO object cleaned up");
}
