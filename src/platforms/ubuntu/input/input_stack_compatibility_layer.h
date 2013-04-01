/*
 * Copyright (C) 2012 Canonical Ltd
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Thomas Voss <thomas.voss@canonical.com>
 */

#ifndef INPUT_STACK_COMPATIBILITY_LAYER_H_
#define INPUT_STACK_COMPATIBILITY_LAYER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

    static const size_t MAX_POINTER_COUNT = 16;

    typedef int64_t nsecs_t;

    typedef enum
    {
        KEY_EVENT_TYPE,
        MOTION_EVENT_TYPE,
        HW_SWITCH_EVENT_TYPE
    } EventType;

    struct Event
    {
        // Generic event properties
        EventType type;
        int32_t device_id;
        int32_t source_id;
        int32_t action;
        int32_t flags;
        int32_t meta_state;
        // Information specific to key/motion event types
        union
        {
            struct HardwareSwitchEvent
            {
                nsecs_t event_time;
                uint32_t policy_flags;
                int32_t switch_values;
                int32_t switch_mask;
            } hw_switch;
            struct KeyEvent
            {
                int32_t key_code;
                int32_t scan_code;
                int32_t repeat_count;
                nsecs_t down_time;
                nsecs_t event_time;
                bool is_system_key;
            } key;
            struct MotionEvent
            {
                int32_t edge_flags;
                int32_t button_state;
                float x_offset;
                float y_offset;
                float x_precision;
                float y_precision;
                nsecs_t down_time;
                nsecs_t event_time;

                size_t pointer_count;
                struct PointerCoordinates
                {
                    int id;
                    float x, raw_x;
                    float y, raw_y;
                    float touch_major;
                    float touch_minor;
                    float size;
                    float pressure;
                    float orientation;
                };
                PointerCoordinates pointer_coordinates[MAX_POINTER_COUNT];
            } motion;
        } details;
    };

    struct AndroidEventListener
    {
        typedef void (*on_new_event_callback)(Event* event, void* context);

        on_new_event_callback on_new_event;
        void* context;
    };

    struct InputStackConfiguration
    {
        bool enable_touch_point_visualization;
        int default_layer_for_touch_point_visualization;
    };

    void android_input_stack_initialize(
        AndroidEventListener* listener,
        InputStackConfiguration* input_stack_configuration);

    void android_input_stack_loop_once();
    void android_input_stack_start();
    void android_input_stack_start_waiting_for_flag(bool* flag);
    void android_input_stack_stop();
    void android_input_stack_shutdown();

#ifdef __cplusplus
}
#endif

#endif // INPUT_STACK_COMPATIBILITY_LAYER_H_
