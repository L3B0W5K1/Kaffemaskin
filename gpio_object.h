#ifndef OBJECT_GPIO_H
#define OBJECT_GPIO_H

#include "liblwm2m.h"
#include <stdbool.h>
#include <stdint.h>

#define GPIO_OBJECT_ID 3200     /* object id shown as /3200 */
#define RES_GPIO_STATE 0        /* resource id inside object */

lwm2m_object_t *gpio_create_object(void);
bool gpio_add_instance(lwm2m_object_t *object, uint16_t instanceId);

#endif /* OBJECT_GPIO_H */
