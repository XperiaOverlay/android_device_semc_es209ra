/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#define LOG_NDEBUG 1
#define LOG_TAG "lights"

#include <cutils/log.h>

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>

#include <sys/ioctl.h>
#include <sys/types.h>

#include <hardware/lights.h>

#define MANUAL         "i2c"
#define AUTOMATIC      "als"

/******************************************************************************/

static pthread_once_t g_init = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct light_state_t g_notification;
static struct light_state_t g_battery;
static int g_backlight = 255;
static int g_buttons = 1;
static int g_attention = 0;

char const*const RED_LED_FILE 		= "/sys/class/leds/lv5219lg:rgb1:red/brightness";
char const*const GREEN_LED_FILE 	= "/sys/class/leds/lv5219lg:rgb1:green/brightness";
char const*const BLUE_LED_FILE 		= "/sys/class/leds/lv5219lg:rgb1:blue/brightness";

char const*const RED_LED_BLINK_ON	= "/sys/class/leds/lv5219lg:rgb1:red/blink_on";
char const*const GREEN_LED_BLINK_ON	= "/sys/class/leds/lv5219lg:rgb1:green/blink_on";
char const*const BLUE_LED_BLINK_ON	= "/sys/class/leds/lv5219lg:rgb1:blue/blink_on";

char const*const RED_LED_BLINK_OFF	= "/sys/class/leds/lv5219lg:rgb1:red/blink_off";
char const*const GREEN_LED_BLINK_OFF	= "/sys/class/leds/lv5219lg:rgb1:green/blink_off";
char const*const BLUE_LED_BLINK_OFF	= "/sys/class/leds/lv5219lg:rgb1:blue/blink_off";

char const*const BUTTON_FILE	= "/sys/class/leds/lv5219lg:sled/brightness";
char const*const LCD_FILE	= "/sys/class/leds/lv5219lg:mled/brightness";

char const*const ALS_FILE		= "/sys/class/leds/lv5219lg:mled/als_enable";

/**
 * device methods
 */

void init_globals(void)
{
    // init the mutex
    pthread_mutex_init(&g_lock, NULL);
}

static int
write_int(char const* path, int value)
{
    int fd;
    static int already_warned = 0;

    fd = open(path, O_RDWR);
    if (fd >= 0) {
        char buffer[20];
        int bytes = sprintf(buffer, "%d\n", value);
        int amt = write(fd, buffer, bytes);
        close(fd);
        return amt == -1 ? -errno : 0;
    } else {
        if (already_warned == 0) {
            LOGE("write_int failed to open %s\n", path);
            already_warned = 1;
        }
        return -errno;
    }
}

static int
is_lit(struct light_state_t const* state)
{
    return state->color & 0x00ffffff;
}

static int
rgb_to_brightness(struct light_state_t const* state)
{
    int color = state->color & 0x00ffffff;
    return ((77*((color>>16)&0x00ff))
            + (150*((color>>8)&0x00ff)) + (29*(color&0x00ff))) >> 8;
}

static int
set_light_backlight(struct light_device_t* dev,
        struct light_state_t const* state)
{
    int err = 0;
    int als_mode;

    int brightness = rgb_to_brightness(state);

    switch(state->brightnessMode) {
        case BRIGHTNESS_MODE_SENSOR:
            als_mode = AUTOMATIC;
            break;
        case BRIGHTNESS_MODE_USER:
        default:
            als_mode = MANUAL;
            break;
    }

    pthread_mutex_lock(&g_lock);
    err = write_int(ALS_FILE, als_mode);
    err = write_int(LCD_FILE, brightness);
    pthread_mutex_unlock(&g_lock);

    return err;
}

static int
set_light_buttons(struct light_device_t* dev,
        struct light_state_t const* state)
{
    int err = 0;
    int on = is_lit(state);
    pthread_mutex_lock(&g_lock);
    g_buttons = on;
    err = write_int(BUTTON_FILE, on?1:0);
    pthread_mutex_unlock(&g_lock);
    return err;
}

static int
set_speaker_light_locked(struct light_device_t* dev,
        struct light_state_t const* state)
{
    int len;
    int alpha, red, green, blue;
    int blink, led_off, led_on;
    int onMS, offMS;
    unsigned int colorRGB;

    switch (state->flashMode) {
        case LIGHT_FLASH_TIMED:
            onMS = state->flashOnMS;
            offMS = state->flashOffMS;
            break;
        case LIGHT_FLASH_NONE:
        default:
            onMS = 0;
            offMS = 0;
            break;
    }

    colorRGB = state->color;

#if 0
    LOGD("set_speaker_light_locked colorRGB=%08X, onMS=%d, offMS=%d\n",
            colorRGB, onMS, offMS);
#endif

    red = (colorRGB >> 16) & 0xFF;
    green = (colorRGB >> 8) & 0xFF;
    blue = colorRGB & 0xFF;

        write_int(RED_LED_FILE, red);
        write_int(GREEN_LED_FILE, green);
        write_int(BLUE_LED_FILE, blue);
        write_int(RED_LED_BLINK_ON, 0);
        write_int(GREEN_LED_BLINK_ON, 0);
        write_int(BLUE_LED_BLINK_ON, 0);
        write_int(RED_LED_BLINK_OFF, 0);
        write_int(GREEN_LED_BLINK_OFF, 0);
        write_int(BLUE_LED_BLINK_OFF, 0);


    if (onMS > 0 && offMS > 0) {
        int totalMS = onMS + offMS;

        // the LED appears to blink about once per second if led_off is 20
        // 1000ms / 20 = 50
        led_off = offMS ;
        // led_on specifies the ratio of ON versus OFF
        // led_on = 0 -> always off
        // led_on = 255 => always on
        led_on = onMS;

        blink = 1;
    } else {
        blink = 0;
        led_off = 0;
        led_on = 0;
    }

        if (blink) {
            write_int(RED_LED_BLINK_ON, led_on);
            write_int(GREEN_LED_BLINK_ON, led_on);
            write_int(BLUE_LED_BLINK_ON, led_on);
            write_int(RED_LED_BLINK_OFF, led_off);
            write_int(GREEN_LED_BLINK_OFF, led_off);
            write_int(BLUE_LED_BLINK_OFF, led_off);
        }

    return 0;
}

static void
handle_speaker_battery_locked(struct light_device_t* dev)
{
    if (is_lit(&g_battery)) {
        set_speaker_light_locked(dev, &g_battery);
    } else {
        set_speaker_light_locked(dev, &g_notification);
    }
}

static int
set_light_battery(struct light_device_t* dev,
        struct light_state_t const* state)
{
    pthread_mutex_lock(&g_lock);
    g_battery = *state;
    handle_speaker_battery_locked(dev);
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int
set_light_notifications(struct light_device_t* dev,
        struct light_state_t const* state)
{
    pthread_mutex_lock(&g_lock);
    g_notification = *state;
    handle_speaker_battery_locked(dev);
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int
set_light_attention(struct light_device_t* dev,
        struct light_state_t const* state)
{
    pthread_mutex_lock(&g_lock);
    if (state->flashMode == LIGHT_FLASH_HARDWARE) {
        g_attention = state->flashOnMS;
    } else if (state->flashMode == LIGHT_FLASH_NONE) {
        g_attention = 0;
    }
    pthread_mutex_unlock(&g_lock);
    return 0;
}


/** Close the lights device */
static int
close_lights(struct light_device_t *dev)
{
    if (dev) {
        free(dev);
    }
    return 0;
}


/******************************************************************************/

/**
 * module methods
 */

/** Open a new instance of a lights device using name */
static int open_lights(const struct hw_module_t* module, char const* name,
        struct hw_device_t** device)
{
    int (*set_light)(struct light_device_t* dev,
            struct light_state_t const* state);

    if (0 == strcmp(LIGHT_ID_BACKLIGHT, name)) {
        set_light = set_light_backlight;
    }
    else if (0 == strcmp(LIGHT_ID_BUTTONS, name)) {
        set_light = set_light_buttons;
    }
    else if (0 == strcmp(LIGHT_ID_BATTERY, name)) {
        set_light = set_light_battery;
    }
    else if (0 == strcmp(LIGHT_ID_NOTIFICATIONS, name)) {
        set_light = set_light_notifications;
    }
    else if (0 == strcmp(LIGHT_ID_ATTENTION, name)) {
        set_light = set_light_attention;
    }
    else {
        return -EINVAL;
    }

    pthread_once(&g_init, init_globals);

    struct light_device_t *dev = malloc(sizeof(struct light_device_t));
    memset(dev, 0, sizeof(*dev));

    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = 0;
    dev->common.module = (struct hw_module_t*)module;
    dev->common.close = (int (*)(struct hw_device_t*))close_lights;
    dev->set_light = set_light;

    *device = (struct hw_device_t*)dev;
    return 0;
}


static struct hw_module_methods_t lights_module_methods = {
    .open =  open_lights,
};

/*
 * The lights Module
 */
const struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 1,
    .version_minor = 0,
    .id = LIGHTS_HARDWARE_MODULE_ID,
    .name = "QCT MSM7K lights Module",
    .author = "Google, Inc.",
    .methods = &lights_module_methods,
};
