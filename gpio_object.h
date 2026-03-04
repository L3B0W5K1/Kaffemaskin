#ifndef GPIO_OBJECT_H
#define GPIO_OBJECT_H

#include <anjay/anjay.h>

/**
 * Custom LwM2M Object: GPIO Controller
 * Object ID: 26241 (custom/private range)
 *
 * Resources:
 *   0 - GPIO Pin Number    (Integer, RW)    - BCM pin number to control
 *   1 - GPIO State         (Boolean, RW)    - Current HIGH/LOW state
 *   2 - Activate           (Execute)        - Set GPIO HIGH for pulse_duration_ms
 *   3 - Deactivate         (Execute)        - Force GPIO LOW immediately
 *   4 - Pulse Duration ms  (Integer, RW)    - How long Activate keeps pin HIGH (default 1000)
 *   5 - Description        (String, R)      - Human-readable label
 */

#define GPIO_OBJECT_ID 26241

/* Resource IDs */
#define RID_GPIO_PIN          0
#define RID_GPIO_STATE        1
#define RID_ACTIVATE          2
#define RID_DEACTIVATE        3
#define RID_PULSE_DURATION    4
#define RID_DESCRIPTION       5

/**
 * Create and install the GPIO object into the Anjay instance.
 * Returns 0 on success, negative on error.
 */
int gpio_object_install(anjay_t *anjay);

/**
 * Call periodically from the event loop to handle timed pulse deactivation.
 */
void gpio_object_update(anjay_t *anjay);

/**
 * Cleanup GPIO resources (unexport pins, free memory).
 */
void gpio_object_cleanup(void);

#endif /* GPIO_OBJECT_H */
