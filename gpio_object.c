#include "object_gpio.h"
#include "liblwm2m.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * GPIO object /3200
 *
 * Instance 0
 * Resource 0 = STATE (0 = OFF, 1 = ON)
 *
 * Read  -> returns state
 * Write -> sets state
 * Exec  -> toggles state
 */

/* ------------------------------------------------------------------ */
/* Replace this macro with your real GPIO control implementation       */
/* Example: for ESP-IDF you might define GPIO_PIN_SET(v) gpio_set_level(15, (v))
   For Linux sysfs, write to /sys/class/gpio/gpio15/value, etc.        */
/* ------------------------------------------------------------------ */
#ifndef GPIO_PIN_SET
#define GPIO_PIN_SET(v) \
    do { printf("GPIO PIN 15 set to %d\n", (v)); } while (0)
#endif

typedef struct _gpio_oi_t {
    struct _gpio_oi_t *next; /* required for lwm2m_list_t */
    uint16_t id;             /* instance id */
    uint16_t state;          /* 0 = OFF, 1 = ON */
} gpio_oi_t;

/* ------------------------------------------------------------------ */
/* READ                                                               */
/* ------------------------------------------------------------------ */
static uint8_t prv_read(lwm2m_context_t *contextP,
                        uint16_t instanceId,
                        int *numDataP,
                        lwm2m_data_t **dataArrayP,
                        lwm2m_object_t *objectP)
{
    (void)contextP;

    gpio_oi_t *inst = (gpio_oi_t *)lwm2m_list_find(objectP->instanceList, instanceId);
    if (inst == NULL)
        return COAP_404_NOT_FOUND;

    /* If server requests whole instance, return the single resource */
    if (*numDataP == 0) {
        *numDataP = 1;
        *dataArrayP = lwm2m_data_new(1);
        if (*dataArrayP == NULL)
            return COAP_500_INTERNAL_SERVER_ERROR;
        (*dataArrayP)[0].id = RES_GPIO_STATE;
    }

    for (int i = 0; i < *numDataP; i++) {
        if ((*dataArrayP)[i].id == RES_GPIO_STATE) {
            if ((*dataArrayP)[i].type == LWM2M_TYPE_MULTIPLE_RESOURCE)
                return COAP_404_NOT_FOUND;
            lwm2m_data_encode_int(inst->state, &(*dataArrayP)[i]);
        } else {
            return COAP_404_NOT_FOUND;
        }
    }

    return COAP_205_CONTENT;
}

/* ------------------------------------------------------------------ */
/* WRITE                                                              */
/* ------------------------------------------------------------------ */
static uint8_t prv_write(lwm2m_context_t *contextP,
                         uint16_t instanceId,
                         int numData,
                         lwm2m_data_t *dataArray,
                         lwm2m_object_t *objectP,
                         lwm2m_write_type_t writeType)
{
    (void)writeType;

    gpio_oi_t *inst = (gpio_oi_t *)lwm2m_list_find(objectP->instanceList, instanceId);
    if (inst == NULL)
        return COAP_404_NOT_FOUND;

    for (int i = 0; i < numData; i++) {
        if (dataArray[i].id == RES_GPIO_STATE) {
            int64_t value;
            if (1 != lwm2m_data_decode_int(&dataArray[i], &value))
                return COAP_400_BAD_REQUEST;
            if (value != 0 && value != 1)
                return COAP_406_NOT_ACCEPTABLE;

            inst->state = (uint16_t)value;
            GPIO_PIN_SET(inst->state);

            /* notify observers by building a lwm2m_uri_t and passing its pointer */
            lwm2m_uri_t uri;
            memset(&uri, 0, sizeof(lwm2m_uri_t));
            uri.objectId = objectP->objID;
            uri.instanceId = instanceId;
            uri.resourceId = RES_GPIO_STATE;
            lwm2m_resource_value_changed(contextP, &uri);

        } else {
            return COAP_404_NOT_FOUND;
        }
    }

    return COAP_204_CHANGED;
}

/* ------------------------------------------------------------------ */
/* EXECUTE (toggle)                                                   */
/* Note: this signature matches the Wakaama expected execute callback */
/* ------------------------------------------------------------------ */
static uint8_t prv_execute(lwm2m_context_t *contextP,
                           uint16_t instanceId,
                           uint16_t resourceId,
                           uint8_t *buffer,
                           int length,
                           lwm2m_object_t *objectP)
{
    (void)buffer;
    (void)length;

    if (resourceId != RES_GPIO_STATE)
        return COAP_405_METHOD_NOT_ALLOWED;

    gpio_oi_t *inst = (gpio_oi_t *)lwm2m_list_find(objectP->instanceList, instanceId);
    if (inst == NULL)
        return COAP_404_NOT_FOUND;

    inst->state ^= 1; /* toggle */
    GPIO_PIN_SET(inst->state);

    /* notify observers */
    lwm2m_uri_t uri;
    memset(&uri, 0, sizeof(lwm2m_uri_t));
    uri.objectId = objectP->objID;
    uri.instanceId = instanceId;
    uri.resourceId = RES_GPIO_STATE;
    lwm2m_resource_value_changed(contextP, &uri);

    return COAP_204_CHANGED;
}

/* ------------------------------------------------------------------ */
/* CREATE OBJECT                                                       */
/* ------------------------------------------------------------------ */
lwm2m_object_t *gpio_create_object(void)
{
    lwm2m_object_t *obj = (lwm2m_object_t *)lwm2m_malloc(sizeof(lwm2m_object_t));
    if (obj == NULL)
        return NULL;

    memset(obj, 0, sizeof(lwm2m_object_t));

    obj->objID = GPIO_OBJECT_ID;
    obj->readFunc = prv_read;
    obj->writeFunc = prv_write;
    obj->executeFunc = prv_execute;
    obj->createFunc = NULL;
    obj->deleteFunc = NULL;
    obj->instanceList = NULL;

    return obj;
}

/* ------------------------------------------------------------------ */
/* ADD INSTANCE                                                        */
/* ------------------------------------------------------------------ */
bool gpio_add_instance(lwm2m_object_t *object, uint16_t instanceId)
{
    if (object == NULL)
        return false;

    gpio_oi_t *inst = (gpio_oi_t *)lwm2m_malloc(sizeof(gpio_oi_t));
    if (inst == NULL)
        return false;

    memset(inst, 0, sizeof(gpio_oi_t));
    inst->id = instanceId;
    inst->state = 1; /* default ON or change as you like */

    object->instanceList = LWM2M_LIST_ADD(object->instanceList, inst);

    /* initialize hardware */
    GPIO_PIN_SET(inst->state);

    return true;
}
