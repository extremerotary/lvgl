/**
 * @file lv_scale.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_scale_private.h"
#include "../../core/lv_obj_private.h"
#include "../../core/lv_obj_class_private.h"
#if LV_USE_SCALE != 0

#include "../../core/lv_group.h"
#include "../../misc/lv_assert.h"
#include "../../misc/lv_math.h"
#include "../../misc/lv_text_private.h"
#include "../../core/lv_observer_private.h"
#include "../../draw/lv_draw_arc.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*********************
 *      DEFINES
 *********************/
#define MY_CLASS (&lv_scale_class)

#define LV_SCALE_LABEL_TXT_LEN          (20U)
#define LV_SCALE_DEFAULT_ANGLE_RANGE    ((uint32_t) 270U)
#define LV_SCALE_DEFAULT_ROTATION       ((int32_t) 135U)
#define LV_SCALE_TICK_IDX_DEFAULT_ID    ((uint32_t) 255U)
#define LV_SCALE_DEFAULT_LABEL_GAP      ((uint32_t) 15U)

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void lv_scale_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void lv_scale_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void lv_scale_event(const lv_obj_class_t * class_p, lv_event_t * event);

static void scale_draw_main(lv_obj_t * obj, lv_event_t * event);
static void scale_draw_indicator(lv_obj_t * obj, lv_event_t * event);
static void scale_draw_label(lv_obj_t * obj, lv_event_t * event, lv_draw_label_dsc_t * label_dsc,
                             const uint32_t major_tick_idx, const int32_t tick_value, lv_point_t * tick_point_b, const uint32_t tick_idx);
static void scale_calculate_main_compensation(lv_obj_t * obj);

static void scale_get_center(const lv_obj_t * obj, lv_point_t * center, int32_t * arc_r);
static void scale_get_tick_points(lv_obj_t * obj, const uint32_t tick_idx, bool is_major_tick,
                                  lv_point_t * tick_point_a, lv_point_t * tick_point_b);
static void scale_get_tick_points_uncached(lv_obj_t * obj, const uint32_t tick_idx, bool is_major_tick,
                                           lv_point_t * tick_point_a, lv_point_t * tick_point_b);
static void scale_get_label_coords(lv_obj_t * obj, lv_draw_label_dsc_t * label_dsc, lv_point_t * tick_point,
                                   lv_area_t * label_coords);
static void scale_set_indicator_label_properties(lv_obj_t * obj, lv_draw_label_dsc_t * label_dsc,
                                                 const lv_style_t * indicator_section_style);
static void scale_set_line_properties(lv_obj_t * obj, lv_draw_line_dsc_t * line_dsc, const lv_style_t * section_style,
                                      lv_part_t part);
static void scale_set_arc_properties(lv_obj_t * obj, lv_draw_arc_dsc_t * arc_dsc, const lv_style_t * section_style);

static void scale_mark_geom_dirty(lv_obj_t * obj);
static void scale_free_geom_cache(lv_obj_t * obj);
static void scale_ensure_tick_geom_cache(lv_obj_t * obj);
static bool scale_clip_hits_visible_content(lv_obj_t * obj, lv_event_t * event);
static int32_t scale_get_computed_ext_draw(lv_obj_t * obj);
static bool area_fully_inside_circle(const lv_area_t * a, const lv_point_t * c, int32_t r);

/* Helpers */
static void scale_find_section_tick_idx(lv_obj_t * obj);
static void scale_store_main_line_tick_width_compensation(lv_obj_t * obj, const uint32_t tick_idx,
                                                          const bool is_major_tick, const int32_t major_tick_width, const int32_t minor_tick_width);
static void scale_store_section_line_tick_width_compensation(lv_obj_t * obj, const bool is_major_tick,
                                                             lv_draw_line_dsc_t * major_tick_dsc, lv_draw_line_dsc_t * minor_tick_dsc,
                                                             const int32_t tick_value, const uint8_t tick_idx, lv_point_t * tick_point_a);
static void scale_build_custom_label_text(lv_obj_t * obj, lv_draw_label_dsc_t * label_dsc,
                                          const uint16_t major_tick_idx);

static void scale_free_line_needle_points_cb(lv_event_t * e);

static bool scale_is_major_tick(lv_scale_t * scale, uint32_t tick_idx);

#if LV_USE_OBSERVER
static void scale_section_min_value_observer_cb(lv_observer_t * observer, lv_subject_t * subject);
static void scale_section_max_value_observer_cb(lv_observer_t * observer, lv_subject_t * subject);
#endif /*LV_USE_OBSERVER*/

/**********************
 *  STATIC VARIABLES
 **********************/

const lv_obj_class_t lv_scale_class  = {
    .constructor_cb = lv_scale_constructor,
    .destructor_cb = lv_scale_destructor,
    .event_cb = lv_scale_event,
    .instance_size = sizeof(lv_scale_t),
    .editable = LV_OBJ_CLASS_EDITABLE_TRUE,
    .base_class = &lv_obj_class,
    .name = "lv_scale",
};

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

static inline float clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

lv_obj_t * lv_scale_create(lv_obj_t * parent)
{
    LV_LOG_INFO("begin");
    lv_obj_t * obj = lv_obj_class_create_obj(MY_CLASS, parent);
    lv_obj_class_init_obj(obj);
    return obj;
}

/*======================
 * Add/remove functions
 *=====================*/

/*=====================
 * Setter functions
 *====================*/

static void scale_mark_geom_dirty(lv_obj_t * obj)
{
    lv_scale_t * scale = (lv_scale_t *)obj;
    scale->geom_dirty = true;
    scale->section_dirty = true;
    scale->ext_draw_dirty = true;
}

void lv_scale_set_mode(lv_obj_t * obj, lv_scale_mode_t mode)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_scale_t * scale = (lv_scale_t *)obj;
    if(scale->mode == mode) return;

    scale->mode = mode;
    scale_mark_geom_dirty(obj);
    lv_obj_invalidate(obj);
}

void lv_scale_set_total_tick_count(lv_obj_t * obj, uint32_t total_tick_count)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_scale_t * scale = (lv_scale_t *)obj;
    if(scale->total_tick_count == total_tick_count) return;

    scale->total_tick_count = total_tick_count;
    scale_mark_geom_dirty(obj);
    lv_obj_invalidate(obj);
}

void lv_scale_set_major_tick_every(lv_obj_t * obj, uint32_t major_tick_every)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_scale_t * scale = (lv_scale_t *)obj;
    if(scale->major_tick_every == major_tick_every) return;

    scale->major_tick_every = major_tick_every;
    scale_mark_geom_dirty(obj);
    lv_obj_invalidate(obj);
}

void lv_scale_set_label_show(lv_obj_t * obj, bool show_label)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_scale_t * scale = (lv_scale_t *)obj;
    if(scale->label_enabled == show_label) return;

    scale->label_enabled = show_label;
    scale_mark_geom_dirty(obj);
    lv_obj_invalidate(obj);
}

void lv_scale_set_range(lv_obj_t * obj, int32_t min, int32_t max)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_scale_t * scale = (lv_scale_t *)obj;
    if(scale->range_min == min && scale->range_max == max) return;

    scale->range_min = min;
    scale->range_max = max;
    scale_mark_geom_dirty(obj);
    lv_obj_invalidate(obj);
}

void lv_scale_set_min_value(lv_obj_t * obj, int32_t min)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_scale_t * scale = (lv_scale_t *)obj;
    if(scale->range_min == min) return;

    scale->range_min = min;
    scale_mark_geom_dirty(obj);
    lv_obj_invalidate(obj);
}

void lv_scale_set_max_value(lv_obj_t * obj, int32_t max)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_scale_t * scale = (lv_scale_t *)obj;
    if(scale->range_max == max) return;

    scale->range_max = max;
    scale_mark_geom_dirty(obj);
    lv_obj_invalidate(obj);
}

void lv_scale_set_angle_range(lv_obj_t * obj, uint32_t angle_range)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_scale_t * scale = (lv_scale_t *)obj;
    if(scale->angle_range == angle_range) return;

    scale->angle_range = angle_range;
    scale_mark_geom_dirty(obj);
    lv_obj_invalidate(obj);
}

void lv_scale_set_rotation(lv_obj_t * obj, int32_t rotation)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_scale_t * scale = (lv_scale_t *)obj;

    int32_t normalized_angle = rotation;
    if(normalized_angle < 0 || normalized_angle > 360) {
        normalized_angle = rotation % 360;
        if(normalized_angle < 0) normalized_angle += 360;
    }

    if(scale->rotation == normalized_angle) return;

    scale->rotation = normalized_angle;
    scale_mark_geom_dirty(obj);
    lv_obj_invalidate(obj);
}

void lv_scale_update_vertical_needle(lv_obj_t * scale, lv_obj_t * needle_line, int32_t needle_length, int32_t value)
{
    int32_t sw = lv_obj_get_width(scale);
    int32_t sh = lv_obj_get_height(scale);
    if(sw <= 0 || sh <= 0) return;

    lv_scale_t * s = (lv_scale_t *)scale;
    int32_t minv = s->range_min;
    int32_t maxv = s->range_max;
    int32_t range = maxv - minv;
    if(range <= 0) return;

    int32_t rel = (value <= minv) ? 0 : (value >= maxv ? range : value - minv);

    int32_t new_local_y = sh - ((rel * sh) / range);
    if(new_local_y < 0) new_local_y = 0;
    if(new_local_y > sh) new_local_y = sh;

    int32_t lw = lv_obj_get_style_line_width(needle_line, LV_PART_MAIN);
    if(lw < 1) lw = 1;
    int32_t half_w = lw / 2;

    int32_t clamped_len = LV_MIN(needle_length, sw);

    int32_t obj_h = lw;
    int32_t obj_w = clamped_len;

    int32_t new_obj_x = (s->mode == LV_SCALE_MODE_VERTICAL_LEFT) ? 0 : sw - obj_w;

    int32_t new_obj_y = new_local_y - half_w;
    if(new_obj_y < 0) new_obj_y = 0;
    if(new_obj_y > sh - obj_h) new_obj_y = sh - obj_h;

    lv_obj_set_width(needle_line, obj_w);
    lv_obj_set_height(needle_line, obj_h);
    lv_obj_set_x(needle_line, new_obj_x);
    lv_obj_set_y(needle_line, new_obj_y);

    lv_point_precise_t * pts = lv_line_get_points_mutable(needle_line);
    if(!pts || lv_line_get_point_count(needle_line) < 2) {
        pts = lv_malloc(sizeof(lv_point_precise_t) * 2);
        LV_ASSERT_MALLOC(pts);
        if(!pts) return;
        lv_line_set_points_mutable(needle_line, pts, 2);
        lv_obj_add_event_cb(needle_line, scale_free_line_needle_points_cb, LV_EVENT_DELETE, pts);
    }

    int32_t line_y = half_w;
    pts[0].x = 0;
    pts[0].y = line_y;
    pts[1].x = obj_w;
    pts[1].y = line_y;

    lv_line_set_points_mutable(needle_line, pts, 2);
}

void lv_scale_update_horizontal_needle(lv_obj_t * scale, lv_obj_t * needle_line, int32_t needle_length, int32_t value)
{
    int32_t sw = lv_obj_get_width(scale);
    int32_t sh = lv_obj_get_height(scale);
    if(sw <= 0 || sh <= 0) return;

    lv_scale_t * s = (lv_scale_t *)scale;
    int32_t minv = s->range_min;
    int32_t maxv = s->range_max;
    int32_t range = maxv - minv;
    if(range <= 0) return;

    int32_t rel = (value <= minv) ? 0 : (value >= maxv ? range : value - minv);
    int32_t new_local_x = (rel * sw) / range;
    if(new_local_x < 0) new_local_x = 0;
    if(new_local_x > sw) new_local_x = sw;

    int32_t lw = lv_obj_get_style_line_width(needle_line, LV_PART_MAIN);
    if(lw < 1) lw = 1;
    int32_t half_w = lw / 2;

    int32_t clamped_len = LV_MIN(needle_length, sh);

    int32_t obj_w = lw;
    int32_t obj_h = clamped_len;
    int32_t new_obj_x = new_local_x - half_w;
    if(new_obj_x < 0) new_obj_x = 0;
    if(new_obj_x > sw - obj_w) new_obj_x = sw - obj_w;

    lv_obj_set_width(needle_line, obj_w);
    lv_obj_set_height(needle_line, obj_h);
    lv_obj_set_x(needle_line, new_obj_x);

    if(s->mode == LV_SCALE_MODE_HORIZONTAL_BOTTOM) lv_obj_set_y(needle_line, 0);
    else lv_obj_set_y(needle_line, sh - obj_h);

    lv_point_precise_t * pts = lv_line_get_points_mutable(needle_line);
    if(!pts || lv_line_get_point_count(needle_line) < 2) {
        pts = lv_malloc(sizeof(lv_point_precise_t) * 2);
        LV_ASSERT_MALLOC(pts);
        if(!pts) return;
        lv_line_set_points_mutable(needle_line, pts, 2);
        lv_obj_add_event_cb(needle_line, scale_free_line_needle_points_cb, LV_EVENT_DELETE, pts);
    }

    int32_t line_x = half_w;
    pts[0].x = line_x;
    pts[0].y = 0;
    pts[1].x = line_x;
    pts[1].y = obj_h;

    lv_line_set_points_mutable(needle_line, pts, 2);
}

void lv_scale_set_line_needle_value_f(lv_obj_t * obj,
                                      lv_obj_t * needle_line,
                                      int32_t needle_length,
                                      float value_f)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_scale_t * scale = (lv_scale_t *)obj;

    if((scale->mode == LV_SCALE_MODE_HORIZONTAL_TOP) ||
       (scale->mode == LV_SCALE_MODE_HORIZONTAL_BOTTOM)) {
        lv_scale_update_horizontal_needle(obj, needle_line, needle_length, (int32_t)lroundf(value_f));
        return;
    }

    if((scale->mode == LV_SCALE_MODE_VERTICAL_LEFT) ||
       (scale->mode == LV_SCALE_MODE_VERTICAL_RIGHT)) {
        lv_scale_update_vertical_needle(obj, needle_line, needle_length, (int32_t)lroundf(value_f));
        return;
    }

    if((scale->mode != LV_SCALE_MODE_ROUND_INNER) &&
       (scale->mode != LV_SCALE_MODE_ROUND_OUTER)) {
        return;
    }

    int32_t scale_w = lv_obj_get_style_width(obj, LV_PART_MAIN);
    int32_t scale_h = lv_obj_get_style_height(obj, LV_PART_MAIN);
    if(scale_w != scale_h) return;

    const float cx_f = (float)scale_w * 0.5f;
    const float cy_f = (float)scale_h * 0.5f;

    int32_t max_len = scale_w / 2;
    int32_t actual_len = needle_length;
    if(actual_len >= max_len) actual_len = max_len;
    else if(actual_len < 0) {
        if(needle_length + max_len < 0) actual_len = 0;
        else actual_len = max_len + needle_length;
    }

    const float rmin = (float)scale->range_min;
    const float rmax = (float)scale->range_max;
    const float span = rmax - rmin;

    float t = 0.0f;
    if(span > 0.0f) {
        float v = clampf(value_f, rmin, rmax);
        t = (v - rmin) / span;
    }

    const float angle_deg = t * (float)scale->angle_range;
    const float final_deg = (float)scale->rotation + angle_deg;
    const float rad = final_deg * (float)(M_PI / 180.0);

    const float dx_f = (float)actual_len * cosf(rad);
    const float dy_f = (float)actual_len * sinf(rad);

    const float ex_f = cx_f + dx_f;
    const float ey_f = cy_f + dy_f;

    const int32_t cx = (int32_t)lroundf(cx_f);
    const int32_t cy = (int32_t)lroundf(cy_f);
    const int32_t ex = (int32_t)lroundf(ex_f);
    const int32_t ey = (int32_t)lroundf(ey_f);

    const int32_t line_w = lv_obj_get_style_line_width(needle_line, LV_PART_MAIN);
    const int32_t aa_pad = 2;
    int32_t PAD = (line_w / 2) + aa_pad;
    if(scale->mode == LV_SCALE_MODE_ROUND_OUTER) {
        int32_t arc_w = lv_obj_get_style_arc_width(obj, LV_PART_MAIN);
        if(arc_w > 0) PAD += arc_w;
    }

    int32_t minx = LV_MIN(cx, ex) - PAD;
    int32_t miny = LV_MIN(cy, ey) - PAD;
    int32_t maxx = LV_MAX(cx, ex) + PAD;
    int32_t maxy = LV_MAX(cy, ey) + PAD;

    if(minx < 0) minx = 0;
    if(miny < 0) miny = 0;
    if(maxx > scale_w - 1) maxx = scale_w - 1;
    if(maxy > scale_h - 1) maxy = scale_h - 1;

    int32_t box_w = maxx - minx + 1;
    int32_t box_h = maxy - miny + 1;
    if(box_w <= 0) box_w = 2;
    if(box_h <= 0) box_h = 2;

    lv_obj_set_pos(needle_line, minx, miny);
    lv_obj_set_size(needle_line, box_w, box_h);

    lv_point_precise_t * pts = NULL;
    if(lv_line_is_point_array_mutable(needle_line) && lv_line_get_point_count(needle_line) >= 2) {
        pts = lv_line_get_points_mutable(needle_line);
    }
    else {
        uint32_t ev_cnt = lv_obj_get_event_count(needle_line);
        for(int32_t i = (int32_t)ev_cnt - 1; i >= 0; i--) {
            lv_event_dsc_t * dsc = lv_obj_get_event_dsc(needle_line, (uint32_t)i);
            if(dsc && lv_event_dsc_get_cb(dsc) == scale_free_line_needle_points_cb) {
                pts = (lv_point_precise_t *)lv_event_dsc_get_user_data(dsc);
                break;
            }
        }
    }

    if(pts == NULL) {
        pts = (lv_point_precise_t *)lv_malloc(sizeof(lv_point_precise_t) * 2);
        LV_ASSERT_MALLOC(pts);
        if(pts == NULL) return;
        lv_obj_add_event_cb(needle_line, scale_free_line_needle_points_cb, LV_EVENT_DELETE, pts);
        lv_line_set_points_mutable(needle_line, pts, 2);
    }

    const int32_t rel_cx = cx - minx;
    const int32_t rel_cy = cy - miny;
    const int32_t rel_ex = ex - minx;
    const int32_t rel_ey = ey - miny;

    pts[0].x = rel_cx;
    pts[0].y = rel_cy;
    pts[1].x = rel_ex;
    pts[1].y = rel_ey;

    lv_line_set_points_mutable(needle_line, pts, 2);
}

void lv_scale_set_line_needle_value(lv_obj_t * obj, lv_obj_t * needle_line, int32_t needle_length, int32_t value)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_scale_t * scale = (lv_scale_t *)obj;

    if((scale->mode == LV_SCALE_MODE_HORIZONTAL_TOP) ||
       (scale->mode == LV_SCALE_MODE_HORIZONTAL_BOTTOM)) {
        lv_scale_update_horizontal_needle(obj, needle_line, needle_length, value);
        return;
    }

    if((scale->mode == LV_SCALE_MODE_VERTICAL_LEFT) ||
       (scale->mode == LV_SCALE_MODE_VERTICAL_RIGHT)) {
        lv_scale_update_vertical_needle(obj, needle_line, needle_length, value);
        return;
    }

    if((scale->mode != LV_SCALE_MODE_ROUND_INNER) &&
       (scale->mode != LV_SCALE_MODE_ROUND_OUTER)) {
        return;
    }

    int32_t scale_w = lv_obj_get_style_width(obj, LV_PART_MAIN);
    int32_t scale_h = lv_obj_get_style_height(obj, LV_PART_MAIN);
    if(scale_w != scale_h) return;
    int32_t cx = scale_w / 2;
    int32_t cy = scale_h / 2;

    int32_t max_len = scale_w / 2;
    int32_t actual_len = needle_length;
    if(actual_len >= max_len) actual_len = max_len;
    else if(actual_len < 0) {
        if(needle_length + max_len < 0) actual_len = 0;
        else actual_len = max_len + needle_length;
    }

    int32_t angle = 0;
    int32_t range_span = scale->range_max - scale->range_min;
    if(range_span > 0) {
        if(value <= scale->range_min) angle = 0;
        else if(value >= scale->range_max) angle = scale->angle_range;
        else angle = (int32_t)((int64_t)scale->angle_range * (value - scale->range_min) / range_span);
    }

    int32_t final_angle = scale->rotation + angle;

    int32_t dx = (actual_len * lv_trigo_cos(final_angle)) >> LV_TRIGO_SHIFT;
    int32_t dy = (actual_len * lv_trigo_sin(final_angle)) >> LV_TRIGO_SHIFT;

    int32_t ex = cx + dx;
    int32_t ey = cy + dy;

    const int32_t line_w = lv_obj_get_style_line_width(needle_line, LV_PART_MAIN);
    const int32_t aa_pad = 2;
    int32_t PAD = (line_w / 2) + aa_pad;
    if(scale->mode == LV_SCALE_MODE_ROUND_OUTER) {
        int32_t arc_w = lv_obj_get_style_arc_width(obj, LV_PART_MAIN);
        if(arc_w > 0) PAD += arc_w;
    }
    int32_t minx = LV_MIN(cx, ex) - PAD;
    int32_t miny = LV_MIN(cy, ey) - PAD;
    int32_t maxx = LV_MAX(cx, ex) + PAD;
    int32_t maxy = LV_MAX(cy, ey) + PAD;

    if(minx < 0) minx = 0;
    if(miny < 0) miny = 0;
    if(maxx > scale_w - 1) maxx = scale_w - 1;
    if(maxy > scale_h - 1) maxy = scale_h - 1;

    int32_t box_w = maxx - minx + 1;
    int32_t box_h = maxy - miny + 1;
    if(box_w <= 0) box_w = 2;
    if(box_h <= 0) box_h = 2;

    lv_obj_set_pos(needle_line, minx, miny);
    lv_obj_set_size(needle_line, box_w, box_h);

    lv_point_precise_t * pts = NULL;
    if(lv_line_is_point_array_mutable(needle_line) && lv_line_get_point_count(needle_line) >= 2) {
        pts = lv_line_get_points_mutable(needle_line);
    }
    else {
        uint32_t ev_cnt = lv_obj_get_event_count(needle_line);
        for(int32_t i = (int32_t)ev_cnt - 1; i >= 0; i--) {
            lv_event_dsc_t * dsc = lv_obj_get_event_dsc(needle_line, (uint32_t)i);
            if(dsc && lv_event_dsc_get_cb(dsc) == scale_free_line_needle_points_cb) {
                pts = (lv_point_precise_t *)lv_event_dsc_get_user_data(dsc);
                break;
            }
        }
    }

    if(pts == NULL) {
        pts = lv_malloc(sizeof(lv_point_precise_t) * 2);
        LV_ASSERT_MALLOC(pts);
        if(pts == NULL) return;
        lv_obj_add_event_cb(needle_line, scale_free_line_needle_points_cb, LV_EVENT_DELETE, pts);
        lv_line_set_points_mutable(needle_line, pts, 2);
    }

    int32_t rel_cx = cx - minx;
    int32_t rel_cy = cy - miny;
    int32_t rel_ex = ex - minx;
    int32_t rel_ey = ey - miny;

    pts[0].x = rel_cx;
    pts[0].y = rel_cy;
    pts[1].x = rel_ex;
    pts[1].y = rel_ey;

    lv_line_set_points_mutable(needle_line, pts, 2);
}

void lv_scale_set_image_needle_value(lv_obj_t * obj, lv_obj_t * needle_img, int32_t value)
{
    int32_t angle;
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_scale_t * scale = (lv_scale_t *)obj;
    if((scale->mode != LV_SCALE_MODE_ROUND_INNER) &&
       (scale->mode != LV_SCALE_MODE_ROUND_OUTER)) {
        return;
    }

    if(value < scale->range_min) angle = 0;
    else if(value > scale->range_max) angle = scale->angle_range;
    else angle = scale->angle_range * (value - scale->range_min) / (scale->range_max - scale->range_min);

    lv_image_set_rotation(needle_img, (scale->rotation + angle) * 10);
}

void lv_scale_set_text_src(lv_obj_t * obj, const char * txt_src[])
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_scale_t * scale = (lv_scale_t *)obj;

    scale->txt_src = txt_src;
    scale->custom_label_cnt = 0;
    if(scale->txt_src) {
        int32_t idx;
        for(idx = 0; txt_src[idx]; ++idx) scale->custom_label_cnt++;
    }

    scale_mark_geom_dirty(obj);
    lv_obj_invalidate(obj);
}

void lv_scale_set_post_draw(lv_obj_t * obj, bool en)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_scale_t * scale = (lv_scale_t *)obj;
    if(scale->post_draw == en) return;

    scale->post_draw = en;
    scale_mark_geom_dirty(obj);
    lv_obj_invalidate(obj);
}

void lv_scale_set_draw_ticks_on_top(lv_obj_t * obj, bool en)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_scale_t * scale = (lv_scale_t *)obj;
    if(scale->draw_ticks_on_top == en) return;

    scale->draw_ticks_on_top = en;
    scale_mark_geom_dirty(obj);
    lv_obj_invalidate(obj);
}

lv_scale_section_t * lv_scale_add_section(lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_scale_t * scale = (lv_scale_t *)obj;
    lv_scale_section_t * section = lv_ll_ins_head(&scale->section_ll);
    LV_ASSERT_MALLOC(section);
    if(section == NULL) return NULL;

    lv_memzero(section, sizeof(lv_scale_section_t));
    section->first_tick_idx_in_section = LV_SCALE_TICK_IDX_DEFAULT_ID;
    section->last_tick_idx_in_section = LV_SCALE_TICK_IDX_DEFAULT_ID;
    section->range_max = -1;

    scale_mark_geom_dirty(obj);
    lv_obj_invalidate(obj);
    return section;
}

void lv_scale_set_section_range(lv_obj_t * scale, lv_scale_section_t * section, int32_t min, int32_t max)
{
    LV_ASSERT_OBJ(scale, MY_CLASS);
    LV_ASSERT_NULL(section);

    lv_scale_set_section_min_value(scale, section, min);
    lv_scale_set_section_max_value(scale, section, max);
}

void lv_scale_set_section_min_value(lv_obj_t * scale, lv_scale_section_t * section, int32_t min)
{
    LV_ASSERT_OBJ(scale, MY_CLASS);
    LV_ASSERT_NULL(section);

    if(section->range_min == min) return;
    section->range_min = min;
    scale_mark_geom_dirty(scale);
    lv_obj_invalidate(scale);
}

void lv_scale_set_section_max_value(lv_obj_t * scale, lv_scale_section_t * section, int32_t max)
{
    LV_ASSERT_OBJ(scale, MY_CLASS);
    LV_ASSERT_NULL(section);

    if(section->range_max == max) return;
    section->range_max = max;
    scale_mark_geom_dirty(scale);
    lv_obj_invalidate(scale);
}

void lv_scale_section_set_range(lv_scale_section_t * section, int32_t min, int32_t max)
{
    if(NULL == section) return;
    section->range_min = min;
    section->range_max = max;
}

void lv_scale_set_section_style_main(lv_obj_t * scale, lv_scale_section_t * section, const lv_style_t * style)
{
    LV_ASSERT_OBJ(scale, MY_CLASS);
    LV_ASSERT_NULL(section);

    section->main_style = style;
    scale_mark_geom_dirty(scale);
    lv_obj_invalidate(scale);
}

void lv_scale_set_section_style_indicator(lv_obj_t * scale, lv_scale_section_t * section, const lv_style_t * style)
{
    LV_ASSERT_OBJ(scale, MY_CLASS);
    LV_ASSERT_NULL(section);

    section->indicator_style = style;
    scale_mark_geom_dirty(scale);
    lv_obj_invalidate(scale);
}

void lv_scale_set_section_style_items(lv_obj_t * scale, lv_scale_section_t * section, const lv_style_t * style)
{
    LV_ASSERT_OBJ(scale, MY_CLASS);
    LV_ASSERT_NULL(section);

    section->items_style = style;
    scale_mark_geom_dirty(scale);
    lv_obj_invalidate(scale);
}

void lv_scale_section_set_style(lv_scale_section_t * section, lv_part_t part, lv_style_t * section_part_style)
{
    LV_LOG_WARN("Deprecated, use lv_scale_set_section_style_main/indicator/items instead");

    if(NULL == section) return;

    switch(part) {
        case LV_PART_MAIN: section->main_style = section_part_style; break;
        case LV_PART_INDICATOR: section->indicator_style = section_part_style; break;
        case LV_PART_ITEMS: section->items_style = section_part_style; break;
        default: break;
    }
}

/*=====================
 * Getter functions
 *====================*/

lv_scale_mode_t lv_scale_get_mode(lv_obj_t * obj)
{
    return ((lv_scale_t *)obj)->mode;
}

int32_t lv_scale_get_total_tick_count(lv_obj_t * obj)
{
    return ((lv_scale_t *)obj)->total_tick_count;
}

int32_t lv_scale_get_major_tick_every(lv_obj_t * obj)
{
    return ((lv_scale_t *)obj)->major_tick_every;
}

int32_t lv_scale_get_rotation(lv_obj_t * obj)
{
    return ((lv_scale_t *)obj)->rotation;
}

bool lv_scale_get_label_show(lv_obj_t * obj)
{
    return ((lv_scale_t *)obj)->label_enabled;
}

uint32_t lv_scale_get_angle_range(lv_obj_t * obj)
{
    return ((lv_scale_t *)obj)->angle_range;
}

int32_t lv_scale_get_range_min_value(lv_obj_t * obj)
{
    return ((lv_scale_t *)obj)->range_min;
}

int32_t lv_scale_get_range_max_value(lv_obj_t * obj)
{
    return ((lv_scale_t *)obj)->range_max;
}

/*=====================
 * Other functions
 *====================*/

#if LV_USE_OBSERVER
lv_observer_t * lv_scale_bind_section_min_value(lv_obj_t * obj, lv_scale_section_t * section, lv_subject_t * subject)
{
    LV_ASSERT_NULL(subject);
    LV_ASSERT_OBJ(obj, MY_CLASS);
    LV_ASSERT_NULL(section);

    if(subject->type != LV_SUBJECT_TYPE_INT) {
        LV_LOG_WARN("Incompatible subject type: %d", subject->type);
        return NULL;
    }

    return lv_subject_add_observer_obj(subject, scale_section_min_value_observer_cb, obj, section);
}

lv_observer_t * lv_scale_bind_section_max_value(lv_obj_t * obj, lv_scale_section_t * section, lv_subject_t * subject)
{
    LV_ASSERT_NULL(subject);
    LV_ASSERT_OBJ(obj, MY_CLASS);
    LV_ASSERT_NULL(section);

    if(subject->type != LV_SUBJECT_TYPE_INT) {
        LV_LOG_WARN("Incompatible subject type: %d", subject->type);
        return NULL;
    }

    return lv_subject_add_observer_obj(subject, scale_section_max_value_observer_cb, obj, section);
}
#endif /*LV_USE_OBSERVER*/

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void scale_free_geom_cache(lv_obj_t * obj)
{
    lv_scale_t * scale = (lv_scale_t *)obj;
    if(scale->tick_cache) {
        lv_free(scale->tick_cache);
        scale->tick_cache = NULL;
    }
    scale->tick_cache_cnt = 0;
}

static void lv_scale_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_UNUSED(class_p);
    LV_TRACE_OBJ_CREATE("begin");

    lv_scale_t * scale = (lv_scale_t *)obj;

    lv_ll_init(&scale->section_ll, sizeof(lv_scale_section_t));

    scale->total_tick_count = LV_SCALE_TOTAL_TICK_COUNT_DEFAULT;
    scale->major_tick_every = LV_SCALE_MAJOR_TICK_EVERY_DEFAULT;
    scale->mode = LV_SCALE_MODE_HORIZONTAL_BOTTOM;
    scale->label_enabled = LV_SCALE_LABEL_ENABLED_DEFAULT;
    scale->angle_range = LV_SCALE_DEFAULT_ANGLE_RANGE;
    scale->rotation = LV_SCALE_DEFAULT_ROTATION;
    scale->range_min = 0;
    scale->range_max = 100;
    scale->last_tick_width = 0;
    scale->first_tick_width = 0;
    scale->post_draw = false;
    scale->draw_ticks_on_top = false;
    scale->custom_label_cnt = 0;
    scale->txt_src = NULL;

    scale->geom_dirty = true;
    scale->section_dirty = true;
    scale->ext_draw_dirty = true;
    scale->cached_w = -1;
    scale->cached_h = -1;
    scale->cached_arc_r = 0;
    scale->cached_ext_draw = 0;
    scale->tick_cache_cnt = 0;
    scale->tick_cache = NULL;
    scale->cached_center.x = 0;
    scale->cached_center.y = 0;

    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    LV_TRACE_OBJ_CREATE("finished");
}

static void lv_scale_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_UNUSED(class_p);
    LV_TRACE_OBJ_CREATE("begin");

    scale_free_geom_cache(obj);

    lv_scale_t * scale = (lv_scale_t *)obj;
    lv_scale_section_t * section;
    while(scale->section_ll.head) {
        section = lv_ll_get_head(&scale->section_ll);
        lv_ll_remove(&scale->section_ll, section);
        lv_free(section);
    }
    lv_ll_clear(&scale->section_ll);

    LV_TRACE_OBJ_CREATE("finished");
}

static bool area_fully_inside_circle(const lv_area_t * a, const lv_point_t * c, int32_t r)
{
    if(r <= 0) return false;

    lv_point_t pts[4] = {
        { a->x1, a->y1 },
        { a->x2, a->y1 },
        { a->x1, a->y2 },
        { a->x2, a->y2 },
    };

    int64_t rr = (int64_t)r * (int64_t)r;

    for(uint32_t i = 0; i < 4; i++) {
        int32_t dx = pts[i].x - c->x;
        int32_t dy = pts[i].y - c->y;
        int64_t d2 = (int64_t)dx * dx + (int64_t)dy * dy;
        if(d2 > rr) return false;
    }

    return true;
}

static void scale_ensure_tick_geom_cache(lv_obj_t * obj)
{
    lv_scale_t * scale = (lv_scale_t *)obj;

    int32_t w = lv_obj_get_width(obj);
    int32_t h = lv_obj_get_height(obj);

    if(!scale->geom_dirty &&
       scale->tick_cache &&
       scale->tick_cache_cnt == scale->total_tick_count &&
       scale->cached_w == w &&
       scale->cached_h == h) {
        return;
    }

    if(scale->tick_cache_cnt != scale->total_tick_count) {
        lv_free(scale->tick_cache);
        scale->tick_cache = NULL;
        scale->tick_cache_cnt = 0;

        if(scale->total_tick_count > 0) {
            scale->tick_cache = lv_malloc(sizeof(lv_scale_tick_geom_t) * scale->total_tick_count);
            LV_ASSERT_MALLOC(scale->tick_cache);
            if(scale->tick_cache == NULL) return;
            scale->tick_cache_cnt = scale->total_tick_count;
        }
    }

    scale_get_center(obj, &scale->cached_center, &scale->cached_arc_r);

    for(uint32_t i = 0; i < scale->total_tick_count; i++) {
        bool is_major = scale_is_major_tick(scale, i);
        scale->tick_cache[i].is_major = (uint8_t)is_major;
        scale_get_tick_points_uncached(obj, i, is_major, &scale->tick_cache[i].p1, &scale->tick_cache[i].p2);
    }

    scale->cached_w = w;
    scale->cached_h = h;
    scale->geom_dirty = false;
}

static bool scale_clip_hits_visible_content(lv_obj_t * obj, lv_event_t * event)
{
    lv_scale_t * scale = (lv_scale_t *)obj;

    if(scale->mode != LV_SCALE_MODE_ROUND_INNER &&
       scale->mode != LV_SCALE_MODE_ROUND_OUTER) {
        return true;
    }

    lv_layer_t * layer = lv_event_get_layer(event);
    if(layer == NULL) return true;

    const lv_area_t * clip = &layer->_clip_area;
    if(lv_area_get_width(clip) <= 0 || lv_area_get_height(clip) <= 0) return false;

    scale_ensure_tick_geom_cache(obj);

    int32_t major_tick_len = lv_obj_get_style_length(obj, LV_PART_INDICATOR);
    int32_t main_line_width = lv_obj_get_style_line_width(obj, LV_PART_MAIN);

    int32_t empty_r;

    if(scale->mode == LV_SCALE_MODE_ROUND_INNER) {
        empty_r = scale->cached_arc_r - major_tick_len;
    }
    else {
        int32_t inward_guard = LV_MAX(main_line_width, 2) + 2;
        empty_r = scale->cached_arc_r - inward_guard;
    }

    if(empty_r <= 0) return true;

    if(area_fully_inside_circle(clip, &scale->cached_center, empty_r)) {
        return false;
    }

    return true;
}

static int32_t scale_get_computed_ext_draw(lv_obj_t * obj)
{
    lv_scale_t * scale = (lv_scale_t *)obj;

    if(!scale->ext_draw_dirty) return scale->cached_ext_draw;

    int32_t major_tick_len = lv_obj_get_style_length(obj, LV_PART_INDICATOR);
    int32_t minor_tick_len = lv_obj_get_style_length(obj, LV_PART_ITEMS);
    int32_t main_line_width = lv_obj_get_style_line_width(obj, LV_PART_MAIN);

    const lv_font_t * font = lv_obj_get_style_text_font(obj, LV_PART_INDICATOR);
    int32_t font_h = font ? lv_font_get_line_height(font) : 0;

    int32_t label_gap = LV_SCALE_DEFAULT_LABEL_GAP;
    int32_t ext = main_line_width + LV_MAX(major_tick_len, minor_tick_len);

    if(scale->label_enabled) ext += label_gap + font_h;

    ext += 4;

    if(ext < 4) ext = 4;
    if(ext > 48) ext = 48;

    scale->cached_ext_draw = ext;
    scale->ext_draw_dirty = false;
    return ext;
}

static void lv_scale_event(const lv_obj_class_t * class_p, lv_event_t * event)
{
    LV_UNUSED(class_p);

    lv_result_t res = lv_obj_event_base(MY_CLASS, event);
    if(res != LV_RESULT_OK) return;

    lv_event_code_t event_code = lv_event_get_code(event);
    lv_obj_t * obj = lv_event_get_current_target(event);
    lv_scale_t * scale = (lv_scale_t *) obj;

    if(event_code == LV_EVENT_DRAW_MAIN) {
        if(scale->post_draw == false) {
            if(!scale_clip_hits_visible_content(obj, event)) return;

            if(scale->section_dirty) {
                scale_find_section_tick_idx(obj);
                scale->section_dirty = false;
            }

            scale_calculate_main_compensation(obj);

            if(scale->draw_ticks_on_top) {
                scale_draw_main(obj, event);
                scale_draw_indicator(obj, event);
            }
            else {
                scale_draw_indicator(obj, event);
                scale_draw_main(obj, event);
            }
        }
    }
    else if(event_code == LV_EVENT_DRAW_POST) {
        if(scale->post_draw == true) {
            if(!scale_clip_hits_visible_content(obj, event)) return;

            if(scale->section_dirty) {
                scale_find_section_tick_idx(obj);
                scale->section_dirty = false;
            }

            scale_calculate_main_compensation(obj);

            if(scale->draw_ticks_on_top) {
                scale_draw_main(obj, event);
                scale_draw_indicator(obj, event);
            }
            else {
                scale_draw_indicator(obj, event);
                scale_draw_main(obj, event);
            }
        }
    }
    else if(event_code == LV_EVENT_REFR_EXT_DRAW_SIZE) {
        lv_event_set_ext_draw_size(event, scale_get_computed_ext_draw(obj));
    }
    else if(event_code == LV_EVENT_SIZE_CHANGED ||
            event_code == LV_EVENT_STYLE_CHANGED) {
        scale_mark_geom_dirty(obj);
    }
    else {
        /* Nothing to do */
    }
}

static void scale_draw_indicator(lv_obj_t * obj, lv_event_t * event)
{
    lv_scale_t * scale = (lv_scale_t *)obj;
    lv_layer_t * layer = lv_event_get_layer(event);

    if(scale->total_tick_count <= 1) return;

    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.base.layer = layer;
    lv_obj_init_draw_label_dsc(obj, LV_PART_INDICATOR, &label_dsc);

    lv_draw_line_dsc_t major_tick_dsc;
    lv_draw_line_dsc_init(&major_tick_dsc);
    major_tick_dsc.base.layer = layer;
    lv_obj_init_draw_line_dsc(obj, LV_PART_INDICATOR, &major_tick_dsc);
    if(LV_SCALE_MODE_ROUND_OUTER == scale->mode || LV_SCALE_MODE_ROUND_INNER == scale->mode) {
        major_tick_dsc.raw_end = 0;
    }

    lv_draw_line_dsc_t minor_tick_dsc;
    lv_draw_line_dsc_init(&minor_tick_dsc);
    minor_tick_dsc.base.layer = layer;
    lv_obj_init_draw_line_dsc(obj, LV_PART_ITEMS, &minor_tick_dsc);

    const int32_t total_tick_count = scale->total_tick_count;
    int32_t tick_idx = 0;
    uint32_t major_tick_idx = 0U;
    for(tick_idx = 0; tick_idx < total_tick_count; tick_idx++) {
        bool is_major_tick = scale_is_major_tick(scale, tick_idx);
        if(is_major_tick) major_tick_idx++;

        const int32_t tick_value = lv_map(tick_idx, 0, total_tick_count - 1, scale->range_min, scale->range_max);

        label_dsc.base.id1 = tick_idx;
        label_dsc.base.id2 = tick_value;
        label_dsc.base.layer = layer;

        lv_scale_section_t * section;
        LV_LL_READ_BACK(&scale->section_ll, section) {
            if(section->range_min <= tick_value && section->range_max >= tick_value) {
                if(is_major_tick) {
                    scale_set_indicator_label_properties(obj, &label_dsc, section->indicator_style);
                    scale_set_line_properties(obj, &major_tick_dsc, section->indicator_style, LV_PART_INDICATOR);
                }
                else {
                    scale_set_line_properties(obj, &minor_tick_dsc, section->items_style, LV_PART_ITEMS);
                }
                break;
            }
            else {
                lv_obj_init_draw_label_dsc(obj, LV_PART_INDICATOR, &label_dsc);
                lv_obj_init_draw_line_dsc(obj, LV_PART_INDICATOR, &major_tick_dsc);
                lv_obj_init_draw_line_dsc(obj, LV_PART_ITEMS, &minor_tick_dsc);
            }
        }

        lv_point_t tick_point_a;
        lv_point_t tick_point_b;
        scale_get_tick_points(obj, tick_idx, is_major_tick, &tick_point_a, &tick_point_b);

        if(scale->label_enabled && is_major_tick) {
            scale_draw_label(obj, event, &label_dsc, major_tick_idx, tick_value, &tick_point_b, tick_idx);
        }

        if(is_major_tick) {
            major_tick_dsc.p1 = lv_point_to_precise(&tick_point_a);
            major_tick_dsc.p2 = lv_point_to_precise(&tick_point_b);
            major_tick_dsc.base.id1 = tick_idx;
            major_tick_dsc.base.id2 = tick_value;
            lv_draw_line(layer, &major_tick_dsc);
        }
        else {
            minor_tick_dsc.p1 = lv_point_to_precise(&tick_point_a);
            minor_tick_dsc.p2 = lv_point_to_precise(&tick_point_b);
            minor_tick_dsc.base.id1 = tick_idx;
            minor_tick_dsc.base.id2 = tick_value;
            lv_draw_line(layer, &minor_tick_dsc);
        }
    }
}

static void scale_draw_label(lv_obj_t * obj, lv_event_t * event, lv_draw_label_dsc_t * label_dsc,
                             const uint32_t major_tick_idx, const int32_t tick_value, lv_point_t * tick_point_b,
                             const uint32_t tick_idx)
{
    lv_scale_t * scale = (lv_scale_t *)obj;
    lv_layer_t * layer = lv_event_get_layer(event);

    char text_buffer[LV_SCALE_LABEL_TXT_LEN] = {0};
    lv_area_t label_coords;

    if(scale->txt_src) {
        scale_build_custom_label_text(obj, label_dsc, major_tick_idx);
    }
    else {
        lv_snprintf(text_buffer, sizeof(text_buffer), "%" LV_PRId32, tick_value);
        label_dsc->text = text_buffer;
        label_dsc->text_local = 1;
    }

    int32_t translate_x = lv_obj_get_style_translate_x(obj, LV_PART_INDICATOR);
    int32_t translate_y = lv_obj_get_style_translate_y(obj, LV_PART_INDICATOR);
    int32_t label_rotation = lv_obj_get_style_transform_rotation(obj, LV_PART_INDICATOR);
    int32_t translate_rotation = 0;

    if((LV_SCALE_MODE_VERTICAL_LEFT == scale->mode || LV_SCALE_MODE_VERTICAL_RIGHT == scale->mode)
       || (LV_SCALE_MODE_HORIZONTAL_BOTTOM == scale->mode || LV_SCALE_MODE_HORIZONTAL_TOP == scale->mode)) {
        lv_point_t label_origin;
        label_origin.x = tick_point_b->x + translate_x;
        label_origin.y = tick_point_b->y + translate_y;
        scale_get_label_coords(obj, label_dsc, &label_origin, &label_coords);
        label_rotation = (label_rotation & LV_SCALE_ROTATION_ANGLE_MASK);
    }
    else if(LV_SCALE_MODE_ROUND_OUTER == scale->mode || LV_SCALE_MODE_ROUND_INNER == scale->mode) {
        translate_rotation = lv_obj_get_style_translate_radial(obj, LV_PART_INDICATOR);
        uint32_t label_gap = lv_obj_get_style_pad_radial(obj, LV_PART_INDICATOR) + LV_SCALE_DEFAULT_LABEL_GAP;

        lv_area_t scale_area;
        lv_obj_get_content_coords(obj, &scale_area);

        lv_point_t center_point;
        int32_t radius_edge = LV_MIN(lv_area_get_width(&scale_area) / 2, lv_area_get_height(&scale_area) / 2);
        center_point.x = scale_area.x1 + radius_edge;
        center_point.y = scale_area.y1 + radius_edge;

        const int32_t major_len = lv_obj_get_style_length(obj, LV_PART_INDICATOR);

        int32_t angle_upscale = ((tick_idx * scale->angle_range) * 10U) / (scale->total_tick_count - 1U) +
                                (translate_rotation * 10);
        angle_upscale += scale->rotation * 10;

        uint32_t radius_text = 0;
        if(LV_SCALE_MODE_ROUND_INNER == scale->mode) {
            radius_text = (radius_edge - major_len) - (label_gap + label_dsc->letter_space);
        }
        else {
            radius_text = (radius_edge + major_len) + (label_gap + label_dsc->letter_space);
        }

        lv_point_t point;
        point.x = center_point.x + radius_text + translate_x;
        point.y = center_point.y + translate_y;
        int32_t label_rotation_temp = 0;

        if(label_rotation & LV_SCALE_LABEL_ROTATE_MATCH_TICKS) {
            label_rotation_temp = (label_rotation & LV_SCALE_ROTATION_ANGLE_MASK) + angle_upscale;

            if(label_rotation & LV_SCALE_LABEL_ROTATE_KEEP_UPRIGHT) {
                while(label_rotation_temp > 3600) label_rotation_temp -= 3600;
                if(label_rotation_temp > 900 && label_rotation_temp < 2400) {
                    label_rotation_temp += 1800;
                }
            }
            label_rotation = label_rotation_temp;
        }
        else {
            label_rotation = label_rotation & LV_SCALE_ROTATION_ANGLE_MASK;
        }

        lv_point_transform(&point, angle_upscale, LV_SCALE_NONE, LV_SCALE_NONE, &center_point, false);
        scale_get_label_coords(obj, label_dsc, &point, &label_coords);
    }
    else {
        return;
    }

    if(label_rotation > 0) {
        lv_layer_t * layer_label = lv_draw_layer_create(layer, LV_COLOR_FORMAT_ARGB8888, &label_coords);
        lv_draw_label(layer_label, label_dsc, &label_coords);

        lv_point_t pivot_point;
        pivot_point.x = lv_area_get_width(&label_coords) / 2;
        pivot_point.y = lv_area_get_height(&label_coords) / 2;

        lv_draw_image_dsc_t layer_draw_dsc;
        lv_draw_image_dsc_init(&layer_draw_dsc);
        layer_draw_dsc.src = layer_label;
        layer_draw_dsc.rotation = label_rotation;
        layer_draw_dsc.pivot = pivot_point;
        lv_draw_layer(layer, &layer_draw_dsc, &label_coords);
    }
    else {
        lv_draw_label(layer, label_dsc, &label_coords);
    }

    if(label_dsc->text_local) {
        label_dsc->text = NULL;
        label_dsc->text_local = false;
    }
}

static void scale_calculate_main_compensation(lv_obj_t * obj)
{
    lv_scale_t * scale = (lv_scale_t *)obj;
    const uint32_t total_tick_count = scale->total_tick_count;

    if(total_tick_count <= 1) return;
    if(LV_SCALE_MODE_ROUND_OUTER == scale->mode || LV_SCALE_MODE_ROUND_INNER == scale->mode) return;

    lv_draw_line_dsc_t major_tick_dsc;
    lv_draw_line_dsc_init(&major_tick_dsc);
    lv_obj_init_draw_line_dsc(obj, LV_PART_INDICATOR, &major_tick_dsc);

    lv_draw_line_dsc_t minor_tick_dsc;
    lv_draw_line_dsc_init(&minor_tick_dsc);
    lv_obj_init_draw_line_dsc(obj, LV_PART_ITEMS, &minor_tick_dsc);

    for(uint32_t tick_idx = 0; tick_idx < total_tick_count; tick_idx++) {
        const bool is_major_tick = scale_is_major_tick(scale, tick_idx);
        const int32_t tick_value = lv_map(tick_idx, 0, total_tick_count - 1, scale->range_min, scale->range_max);

        lv_scale_section_t * section;
        LV_LL_READ_BACK(&scale->section_ll, section) {
            if(section->range_min <= tick_value && section->range_max >= tick_value) {
                if(is_major_tick) scale_set_line_properties(obj, &major_tick_dsc, section->indicator_style, LV_PART_INDICATOR);
                else scale_set_line_properties(obj, &minor_tick_dsc, section->items_style, LV_PART_ITEMS);
                break;
            }
            else {
                lv_obj_init_draw_line_dsc(obj, LV_PART_INDICATOR, &major_tick_dsc);
                lv_obj_init_draw_line_dsc(obj, LV_PART_ITEMS, &minor_tick_dsc);
            }
        }

        lv_point_t tick_point_a;
        lv_point_t tick_point_b;
        scale_get_tick_points(obj, tick_idx, is_major_tick, &tick_point_a, &tick_point_b);

        scale_store_main_line_tick_width_compensation(obj, tick_idx, is_major_tick, major_tick_dsc.width, minor_tick_dsc.width);
        scale_store_section_line_tick_width_compensation(obj, is_major_tick, &major_tick_dsc, &minor_tick_dsc,
                                                         tick_value, (uint8_t)tick_idx, &tick_point_a);
    }
}

static void scale_draw_main(lv_obj_t * obj, lv_event_t * event)
{
    lv_scale_t * scale = (lv_scale_t *)obj;
    lv_layer_t * layer = lv_event_get_layer(event);

    if(scale->total_tick_count <= 1) return;

    if((LV_SCALE_MODE_VERTICAL_LEFT == scale->mode || LV_SCALE_MODE_VERTICAL_RIGHT == scale->mode)
       || (LV_SCALE_MODE_HORIZONTAL_BOTTOM == scale->mode || LV_SCALE_MODE_HORIZONTAL_TOP == scale->mode)) {

        lv_draw_line_dsc_t line_dsc;
        lv_draw_line_dsc_init(&line_dsc);
        line_dsc.base.layer = layer;
        lv_obj_init_draw_line_dsc(obj, LV_PART_MAIN, &line_dsc);

        const int32_t border_width = lv_obj_get_style_border_width(obj, LV_PART_MAIN);
        const int32_t pad_top = lv_obj_get_style_pad_top(obj, LV_PART_MAIN) + border_width;
        const int32_t pad_bottom = lv_obj_get_style_pad_bottom(obj, LV_PART_MAIN) + border_width;
        const int32_t pad_left = lv_obj_get_style_pad_left(obj, LV_PART_MAIN) + border_width;
        const int32_t pad_right = lv_obj_get_style_pad_right(obj, LV_PART_MAIN) + border_width;

        int32_t x_ofs = 0;
        int32_t y_ofs = 0;

        if(LV_SCALE_MODE_VERTICAL_LEFT == scale->mode) {
            x_ofs = obj->coords.x2 + (line_dsc.width / 2) - pad_right;
            y_ofs = obj->coords.y1 + pad_top;
        }
        else if(LV_SCALE_MODE_VERTICAL_RIGHT == scale->mode) {
            x_ofs = obj->coords.x1 + (line_dsc.width / 2) + pad_left;
            y_ofs = obj->coords.y1 + pad_top;
        }
        if(LV_SCALE_MODE_HORIZONTAL_BOTTOM == scale->mode) {
            x_ofs = obj->coords.x1 + pad_right;
            y_ofs = obj->coords.y1 + (line_dsc.width / 2) + pad_top;
        }
        else if(LV_SCALE_MODE_HORIZONTAL_TOP == scale->mode) {
            x_ofs = obj->coords.x1 + pad_left;
            y_ofs = obj->coords.y2 + (line_dsc.width / 2) - pad_bottom;
        }

        lv_point_t main_line_point_a;
        lv_point_t main_line_point_b;

        if(LV_SCALE_MODE_VERTICAL_LEFT == scale->mode || LV_SCALE_MODE_VERTICAL_RIGHT == scale->mode) {
            main_line_point_a.x = x_ofs - 1;
            main_line_point_a.y = y_ofs;
            main_line_point_b.x = x_ofs - 1;
            main_line_point_b.y = obj->coords.y2 - pad_bottom;

            main_line_point_a.y -= scale->last_tick_width / 2;
            main_line_point_b.y += scale->first_tick_width / 2;
        }
        else {
            main_line_point_a.x = x_ofs;
            main_line_point_a.y = y_ofs;
            main_line_point_b.x = obj->coords.x2 - pad_left;
            main_line_point_b.y = y_ofs;

            main_line_point_a.x -= scale->last_tick_width / 2;
            main_line_point_b.x += scale->first_tick_width / 2;
        }

        line_dsc.p1 = lv_point_to_precise(&main_line_point_a);
        line_dsc.p2 = lv_point_to_precise(&main_line_point_b);
        lv_draw_line(layer, &line_dsc);

        lv_scale_section_t * section;
        LV_LL_READ_BACK(&scale->section_ll, section) {
            lv_draw_line_dsc_t section_line_dsc;
            lv_draw_line_dsc_init(&section_line_dsc);
            section_line_dsc.base.layer = layer;
            lv_obj_init_draw_line_dsc(obj, LV_PART_MAIN, &section_line_dsc);

            lv_point_t section_point_a;
            lv_point_t section_point_b;

            const int32_t first_tick_width_halved = (int32_t)(section->first_tick_in_section_width / 2);
            const int32_t last_tick_width_halved = (int32_t)(section->last_tick_in_section_width / 2);

            if(LV_SCALE_MODE_VERTICAL_LEFT == scale->mode || LV_SCALE_MODE_VERTICAL_RIGHT == scale->mode) {
                section_point_a.x = main_line_point_a.x;
                section_point_a.y = section->first_tick_in_section.y + first_tick_width_halved;
                section_point_b.x = main_line_point_a.x;
                section_point_b.y = section->last_tick_in_section.y - last_tick_width_halved;
            }
            else {
                section_point_a.x = section->first_tick_in_section.x - first_tick_width_halved;
                section_point_a.y = main_line_point_a.y;
                section_point_b.x = section->last_tick_in_section.x + last_tick_width_halved;
                section_point_b.y = main_line_point_a.y;
            }

            scale_set_line_properties(obj, &section_line_dsc, section->main_style, LV_PART_MAIN);

            section_line_dsc.p1.x = section_point_a.x;
            section_line_dsc.p1.y = section_point_a.y;
            section_line_dsc.p2.x = section_point_b.x;
            section_line_dsc.p2.y = section_point_b.y;
            lv_draw_line(layer, &section_line_dsc);
        }
    }
    else if(LV_SCALE_MODE_ROUND_OUTER == scale->mode || LV_SCALE_MODE_ROUND_INNER == scale->mode) {
        lv_draw_arc_dsc_t arc_dsc;
        lv_draw_arc_dsc_init(&arc_dsc);
        arc_dsc.base.layer = layer;
        lv_obj_init_draw_arc_dsc(obj, LV_PART_MAIN, &arc_dsc);

        lv_point_t arc_center;
        int32_t arc_radius;
        scale_get_center(obj, &arc_center, &arc_radius);

        const int32_t start_angle = lv_map(scale->range_min, scale->range_min, scale->range_max, scale->rotation,
                                           scale->rotation + scale->angle_range);
        const int32_t end_angle = lv_map(scale->range_max, scale->range_min, scale->range_max, scale->rotation,
                                         scale->rotation + scale->angle_range);

        arc_dsc.center = arc_center;
        arc_dsc.radius = arc_radius;
        arc_dsc.start_angle = start_angle;
        arc_dsc.end_angle = end_angle;

        lv_draw_arc(layer, &arc_dsc);

        lv_scale_section_t * section;
        LV_LL_READ_BACK(&scale->section_ll, section) {
            lv_draw_arc_dsc_t main_arc_section_dsc;
            lv_draw_arc_dsc_init(&main_arc_section_dsc);
            main_arc_section_dsc.base.layer = layer;
            lv_obj_init_draw_arc_dsc(obj, LV_PART_MAIN, &main_arc_section_dsc);

            lv_point_t section_arc_center;
            int32_t section_arc_radius;
            scale_get_center(obj, &section_arc_center, &section_arc_radius);

            const int32_t section_start_angle = lv_map(section->range_min, scale->range_min, scale->range_max, scale->rotation,
                                                       scale->rotation + scale->angle_range);
            const int32_t section_end_angle = lv_map(section->range_max, scale->range_min, scale->range_max, scale->rotation,
                                                     scale->rotation + scale->angle_range);

            scale_set_arc_properties(obj, &main_arc_section_dsc, section->main_style);

            main_arc_section_dsc.center = section_arc_center;
            main_arc_section_dsc.radius = section_arc_radius;
            main_arc_section_dsc.start_angle = section_start_angle;
            main_arc_section_dsc.end_angle = section_end_angle;

            lv_draw_arc(layer, &main_arc_section_dsc);
        }
    }
}

static void scale_get_center(const lv_obj_t * obj, lv_point_t * center, int32_t * arc_r)
{
    int32_t left_bg = lv_obj_get_style_pad_left(obj, LV_PART_MAIN);
    int32_t right_bg = lv_obj_get_style_pad_right(obj, LV_PART_MAIN);
    int32_t top_bg = lv_obj_get_style_pad_top(obj, LV_PART_MAIN);
    int32_t bottom_bg = lv_obj_get_style_pad_bottom(obj, LV_PART_MAIN);

    int32_t r = (LV_MIN(lv_obj_get_width(obj) - left_bg - right_bg, lv_obj_get_height(obj) - top_bg - bottom_bg)) / 2;

    center->x = obj->coords.x1 + r + left_bg;
    center->y = obj->coords.y1 + r + top_bg;

    if(arc_r) *arc_r = r;
}

static void scale_get_tick_points(lv_obj_t * obj, const uint32_t tick_idx, bool is_major_tick,
                                  lv_point_t * tick_point_a, lv_point_t * tick_point_b)
{
    lv_scale_t * scale = (lv_scale_t *)obj;

    scale_ensure_tick_geom_cache(obj);

    if(scale->tick_cache &&
       tick_idx < scale->tick_cache_cnt &&
       scale->tick_cache[tick_idx].is_major == (uint8_t)is_major_tick) {
        *tick_point_a = scale->tick_cache[tick_idx].p1;
        *tick_point_b = scale->tick_cache[tick_idx].p2;
        return;
    }

    scale_get_tick_points_uncached(obj, tick_idx, is_major_tick, tick_point_a, tick_point_b);
}

static void scale_get_tick_points_uncached(lv_obj_t * obj, const uint32_t tick_idx, bool is_major_tick,
                                           lv_point_t * tick_point_a, lv_point_t * tick_point_b)
{
    lv_scale_t * scale = (lv_scale_t *)obj;

    lv_draw_line_dsc_t main_line_dsc;
    lv_draw_line_dsc_init(&main_line_dsc);
    lv_obj_init_draw_line_dsc(obj, LV_PART_MAIN, &main_line_dsc);

    int32_t minor_len = 0;
    int32_t major_len = 0;
    int32_t radial_offset = 0;

    if(is_major_tick) {
        major_len = lv_obj_get_style_length(obj, LV_PART_INDICATOR);
        radial_offset = lv_obj_get_style_radial_offset(obj, LV_PART_INDICATOR);
    }
    else {
        minor_len = lv_obj_get_style_length(obj, LV_PART_ITEMS);
        radial_offset = lv_obj_get_style_radial_offset(obj, LV_PART_ITEMS);
    }

    if((LV_SCALE_MODE_VERTICAL_LEFT == scale->mode || LV_SCALE_MODE_VERTICAL_RIGHT == scale->mode)
       || (LV_SCALE_MODE_HORIZONTAL_BOTTOM == scale->mode || LV_SCALE_MODE_HORIZONTAL_TOP == scale->mode)) {

        const int32_t border_width = lv_obj_get_style_border_width(obj, LV_PART_MAIN);
        const int32_t pad_top = lv_obj_get_style_pad_top(obj, LV_PART_MAIN) + border_width;
        const int32_t pad_bottom = lv_obj_get_style_pad_bottom(obj, LV_PART_MAIN) + border_width;
        const int32_t pad_right = lv_obj_get_style_pad_right(obj, LV_PART_MAIN) + border_width;
        const int32_t pad_left = lv_obj_get_style_pad_left(obj, LV_PART_MAIN) + border_width;
        const int32_t tick_pad_right = lv_obj_get_style_pad_right(obj, LV_PART_ITEMS);
        const int32_t tick_pad_left = lv_obj_get_style_pad_left(obj, LV_PART_ITEMS);
        const int32_t tick_pad_top = lv_obj_get_style_pad_top(obj, LV_PART_ITEMS);
        const int32_t tick_pad_bottom = lv_obj_get_style_pad_bottom(obj, LV_PART_ITEMS);

        int32_t x_ofs = 0;
        int32_t y_ofs = 0;

        if(LV_SCALE_MODE_VERTICAL_LEFT == scale->mode) {
            x_ofs = obj->coords.x2 + (main_line_dsc.width / 2) - pad_right;
            y_ofs = obj->coords.y1 + (pad_top + tick_pad_top);
        }
        else if(LV_SCALE_MODE_VERTICAL_RIGHT == scale->mode) {
            x_ofs = obj->coords.x1 + (main_line_dsc.width / 2) + pad_left;
            y_ofs = obj->coords.y1 + (pad_top + tick_pad_top);
        }
        else if(LV_SCALE_MODE_HORIZONTAL_BOTTOM == scale->mode) {
            x_ofs = obj->coords.x1 + (pad_right + tick_pad_right);
            y_ofs = obj->coords.y1 + (main_line_dsc.width / 2) + pad_top;
        }
        else {
            x_ofs = obj->coords.x1 + (pad_left + tick_pad_left);
            y_ofs = obj->coords.y2 + (main_line_dsc.width / 2) - pad_bottom;
        }

        if((LV_SCALE_MODE_HORIZONTAL_TOP == scale->mode) || (LV_SCALE_MODE_VERTICAL_RIGHT == scale->mode)) {
            if(is_major_tick) major_len *= -1;
            else minor_len *= -1;
        }

        const int32_t tick_length = is_major_tick ? major_len : minor_len;
        const uint32_t tmp_tick_count = scale->total_tick_count - 1U;

        if(LV_SCALE_MODE_VERTICAL_LEFT == scale->mode || LV_SCALE_MODE_VERTICAL_RIGHT == scale->mode) {
            int32_t vertical_position = obj->coords.y2 - (pad_bottom + tick_pad_bottom);

            if(tmp_tick_count == tick_idx) {
                vertical_position = y_ofs;
            }
            else if(0 != tick_idx) {
                const int32_t scale_total_height = lv_obj_get_height(obj) - (pad_top + pad_bottom + tick_pad_top + tick_pad_bottom);
                const int32_t offset = ((int32_t) tick_idx * (int32_t) scale_total_height) / (int32_t)(tmp_tick_count);
                vertical_position -= offset;
            }

            tick_point_a->x = x_ofs - 1;
            tick_point_a->y = vertical_position;
            tick_point_b->x = tick_point_a->x - tick_length;
            tick_point_b->y = vertical_position;
        }
        else {
            int32_t horizontal_position = x_ofs;

            if(tmp_tick_count == tick_idx) {
                horizontal_position = obj->coords.x2 - (pad_left + tick_pad_left);
            }
            else if(0U != tick_idx) {
                const int32_t scale_total_width = lv_obj_get_width(obj) - (pad_right + pad_left + tick_pad_right + tick_pad_left);
                const int32_t offset = ((int32_t) tick_idx * (int32_t) scale_total_width) / (int32_t)(tmp_tick_count);
                horizontal_position += offset;
            }

            tick_point_a->x = horizontal_position;
            tick_point_a->y = y_ofs;
            tick_point_b->x = horizontal_position;
            tick_point_b->y = tick_point_a->y + tick_length;
        }
    }
    else if(LV_SCALE_MODE_ROUND_OUTER == scale->mode || LV_SCALE_MODE_ROUND_INNER == scale->mode) {
        lv_area_t scale_area;
        lv_obj_get_content_coords(obj, &scale_area);

        lv_point_t center_point;
        const int32_t radius_edge = LV_MIN(lv_area_get_width(&scale_area) / 2, lv_area_get_height(&scale_area) / 2);
        center_point.x = scale_area.x1 + radius_edge;
        center_point.y = scale_area.y1 + radius_edge;

        int32_t angle_upscale = (int32_t)((tick_idx * scale->angle_range) * 10U) / (scale->total_tick_count - 1U);
        angle_upscale += scale->rotation * 10;

        int32_t point_closer_to_arc = 0;
        int32_t adjusted_radio_with_tick_len = 0;
        if(LV_SCALE_MODE_ROUND_INNER == scale->mode) {
            point_closer_to_arc = radius_edge - main_line_dsc.width;
            adjusted_radio_with_tick_len = point_closer_to_arc - (is_major_tick ? major_len : minor_len);
        }
        else {
            point_closer_to_arc = radius_edge - main_line_dsc.width;
            adjusted_radio_with_tick_len = point_closer_to_arc + (is_major_tick ? major_len : minor_len);
        }

        tick_point_a->x = center_point.x + point_closer_to_arc + radial_offset;
        tick_point_a->y = center_point.y;
        lv_point_transform(tick_point_a, angle_upscale, LV_SCALE_NONE, LV_SCALE_NONE, &center_point, false);

        tick_point_b->x = center_point.x + adjusted_radio_with_tick_len + radial_offset;
        tick_point_b->y = center_point.y;
        lv_point_transform(tick_point_b, angle_upscale, LV_SCALE_NONE, LV_SCALE_NONE, &center_point, false);
    }
}

static void scale_get_label_coords(lv_obj_t * obj, lv_draw_label_dsc_t * label_dsc, lv_point_t * tick_point,
                                   lv_area_t * label_coords)
{
    lv_scale_t * scale = (lv_scale_t *)obj;

    lv_text_attributes_t attributes = {0};
    attributes.letter_space = label_dsc->letter_space;
    attributes.line_space = label_dsc->line_space;
    attributes.max_width = LV_COORD_MAX;
    attributes.text_flags = LV_TEXT_FLAG_NONE;

    lv_point_t label_size;

    if(label_dsc->text != NULL) {
        lv_text_get_size_attributes(&label_size, label_dsc->text, label_dsc->font, &attributes);
    }
    else {
        label_size.x = 0;
        label_size.y = 0;
    }

    if((LV_SCALE_MODE_HORIZONTAL_BOTTOM == scale->mode) || (LV_SCALE_MODE_HORIZONTAL_TOP == scale->mode)) {
        label_coords->x1 = tick_point->x - (label_size.x / 2);
        label_coords->x2 = tick_point->x + (label_size.x / 2);

        if(LV_SCALE_MODE_HORIZONTAL_BOTTOM == scale->mode) {
            label_coords->y1 = tick_point->y + lv_obj_get_style_pad_bottom(obj, LV_PART_INDICATOR);
            label_coords->y2 = label_coords->y1 + label_size.y;
        }
        else {
            label_coords->y2 = tick_point->y - lv_obj_get_style_pad_top(obj, LV_PART_INDICATOR);
            label_coords->y1 = label_coords->y2 - label_size.y;
        }
    }
    else if((LV_SCALE_MODE_VERTICAL_LEFT == scale->mode) || (LV_SCALE_MODE_VERTICAL_RIGHT == scale->mode)) {
        label_coords->y1 = tick_point->y - (label_size.y / 2);
        label_coords->y2 = tick_point->y + (label_size.y / 2);

        if(LV_SCALE_MODE_VERTICAL_LEFT == scale->mode) {
            label_coords->x1 = tick_point->x - label_size.x - lv_obj_get_style_pad_left(obj, LV_PART_INDICATOR);
            label_coords->x2 = tick_point->x - lv_obj_get_style_pad_left(obj, LV_PART_INDICATOR);
        }
        else {
            label_coords->x1 = tick_point->x + lv_obj_get_style_pad_right(obj, LV_PART_INDICATOR);
            label_coords->x2 = tick_point->x + label_size.x + lv_obj_get_style_pad_right(obj, LV_PART_INDICATOR);
        }
    }
    else if(LV_SCALE_MODE_ROUND_OUTER == scale->mode || LV_SCALE_MODE_ROUND_INNER == scale->mode) {
        label_coords->x1 = tick_point->x - (label_size.x / 2);
        label_coords->y1 = tick_point->y - (label_size.y / 2);
        label_coords->x2 = label_coords->x1 + label_size.x;
        label_coords->y2 = label_coords->y1 + label_size.y;
    }
}

static void scale_set_line_properties(lv_obj_t * obj, lv_draw_line_dsc_t * line_dsc, const lv_style_t * section_style,
                                      lv_part_t part)
{
    if(section_style) {
        lv_style_value_t value;
        lv_style_res_t res;

        res = lv_style_get_prop(section_style, LV_STYLE_LINE_WIDTH, &value);
        if(res == LV_STYLE_RES_FOUND) line_dsc->width = (int32_t)value.num;
        else line_dsc->width = lv_obj_get_style_line_width(obj, part);

        res = lv_style_get_prop(section_style, LV_STYLE_LINE_COLOR, &value);
        if(res == LV_STYLE_RES_FOUND) line_dsc->color = value.color;
        else line_dsc->color = lv_obj_get_style_line_color(obj, part);

        res = lv_style_get_prop(section_style, LV_STYLE_LINE_OPA, &value);
        if(res == LV_STYLE_RES_FOUND) line_dsc->opa = (lv_opa_t)value.num;
        else line_dsc->opa = lv_obj_get_style_line_opa(obj, part);
    }
    else {
        line_dsc->color = lv_obj_get_style_line_color(obj, part);
        line_dsc->opa = lv_obj_get_style_line_opa(obj, part);
        line_dsc->width = lv_obj_get_style_line_width(obj, part);
    }
}

static void scale_set_arc_properties(lv_obj_t * obj, lv_draw_arc_dsc_t * arc_dsc, const lv_style_t * section_style)
{
    if(section_style) {
        lv_style_value_t value;
        lv_style_res_t res;

        res = lv_style_get_prop(section_style, LV_STYLE_ARC_WIDTH, &value);
        if(res == LV_STYLE_RES_FOUND) arc_dsc->width = (int32_t)value.num;
        else arc_dsc->width = lv_obj_get_style_arc_width(obj, LV_PART_MAIN);

        res = lv_style_get_prop(section_style, LV_STYLE_ARC_COLOR, &value);
        if(res == LV_STYLE_RES_FOUND) arc_dsc->color = value.color;
        else arc_dsc->color = lv_obj_get_style_arc_color(obj, LV_PART_MAIN);

        res = lv_style_get_prop(section_style, LV_STYLE_ARC_OPA, &value);
        if(res == LV_STYLE_RES_FOUND) arc_dsc->opa = (lv_opa_t)value.num;
        else arc_dsc->opa = lv_obj_get_style_arc_opa(obj, LV_PART_MAIN);

        res = lv_style_get_prop(section_style, LV_STYLE_ARC_ROUNDED, &value);
        if(res == LV_STYLE_RES_FOUND) arc_dsc->rounded = (uint8_t)value.num;
        else arc_dsc->rounded = lv_obj_get_style_arc_rounded(obj, LV_PART_MAIN);

        res = lv_style_get_prop(section_style, LV_STYLE_ARC_IMAGE_SRC, &value);
        if(res == LV_STYLE_RES_FOUND) arc_dsc->img_src = (const void *)value.ptr;
        else arc_dsc->img_src = lv_obj_get_style_arc_image_src(obj, LV_PART_MAIN);
    }
    else {
        arc_dsc->color = lv_obj_get_style_arc_color(obj, LV_PART_MAIN);
        arc_dsc->opa = lv_obj_get_style_arc_opa(obj, LV_PART_MAIN);
        arc_dsc->width = lv_obj_get_style_arc_width(obj, LV_PART_MAIN);
        arc_dsc->rounded = lv_obj_get_style_arc_rounded(obj, LV_PART_MAIN);
        arc_dsc->img_src = lv_obj_get_style_arc_image_src(obj, LV_PART_MAIN);
    }
}

static void scale_set_indicator_label_properties(lv_obj_t * obj, lv_draw_label_dsc_t * label_dsc,
                                                 const lv_style_t * indicator_section_style)
{
    if(indicator_section_style) {
        lv_style_value_t value;
        lv_style_res_t res;

        res = lv_style_get_prop(indicator_section_style, LV_STYLE_TEXT_COLOR, &value);
        if(res == LV_STYLE_RES_FOUND) label_dsc->color = value.color;
        else label_dsc->color = lv_obj_get_style_text_color(obj, LV_PART_INDICATOR);

        res = lv_style_get_prop(indicator_section_style, LV_STYLE_TEXT_OPA, &value);
        if(res == LV_STYLE_RES_FOUND) label_dsc->opa = (lv_opa_t)value.num;
        else label_dsc->opa = lv_obj_get_style_text_opa(obj, LV_PART_INDICATOR);

        res = lv_style_get_prop(indicator_section_style, LV_STYLE_TEXT_LETTER_SPACE, &value);
        if(res == LV_STYLE_RES_FOUND) label_dsc->letter_space = (int32_t)value.num;
        else label_dsc->letter_space = lv_obj_get_style_text_letter_space(obj, LV_PART_INDICATOR);

        res = lv_style_get_prop(indicator_section_style, LV_STYLE_TEXT_FONT, &value);
        if(res == LV_STYLE_RES_FOUND) label_dsc->font = (const lv_font_t *)value.ptr;
        else label_dsc->font = lv_obj_get_style_text_font(obj, LV_PART_INDICATOR);
    }
    else {
        label_dsc->color = lv_obj_get_style_text_color(obj, LV_PART_INDICATOR);
        label_dsc->opa = lv_obj_get_style_text_opa(obj, LV_PART_INDICATOR);
        label_dsc->letter_space = lv_obj_get_style_text_letter_space(obj, LV_PART_INDICATOR);
        label_dsc->font = lv_obj_get_style_text_font(obj, LV_PART_INDICATOR);
    }
}

static void scale_find_section_tick_idx(lv_obj_t * obj)
{
    lv_scale_t * scale = (lv_scale_t *)obj;

    lv_scale_section_t * section_reset;
    LV_LL_READ_BACK(&scale->section_ll, section_reset) {
        section_reset->first_tick_idx_in_section = LV_SCALE_TICK_IDX_DEFAULT_ID;
        section_reset->last_tick_idx_in_section = LV_SCALE_TICK_IDX_DEFAULT_ID;
        section_reset->first_tick_idx_is_major = 0;
        section_reset->last_tick_idx_is_major = 0;
    }

    const int32_t min_out = scale->range_min;
    const int32_t max_out = scale->range_max;
    const uint32_t total_tick_count = scale->total_tick_count;

    for(uint32_t tick_idx = 0; tick_idx < total_tick_count; tick_idx++) {
        bool is_major_tick = scale_is_major_tick(scale, tick_idx);
        const int32_t tick_value = lv_map(tick_idx, 0, total_tick_count - 1, min_out, max_out);

        lv_scale_section_t * section;
        LV_LL_READ_BACK(&scale->section_ll, section) {
            if(section->range_min <= tick_value && section->range_max >= tick_value) {
                if(LV_SCALE_TICK_IDX_DEFAULT_ID == section->first_tick_idx_in_section) {
                    section->first_tick_idx_in_section = tick_idx;
                    section->first_tick_idx_is_major = is_major_tick;
                }
                if(LV_SCALE_TICK_IDX_DEFAULT_ID == section->last_tick_idx_in_section) {
                    section->last_tick_idx_in_section = tick_idx;
                    section->last_tick_idx_is_major = is_major_tick;
                }
                else if(section->first_tick_idx_in_section != tick_idx) {
                    section->last_tick_idx_in_section = tick_idx;
                    section->last_tick_idx_is_major = is_major_tick;
                }
            }
        }
    }
}

static void scale_store_main_line_tick_width_compensation(lv_obj_t * obj, const uint32_t tick_idx,
                                                          const bool is_major_tick, const int32_t major_tick_width, const int32_t minor_tick_width)
{
    lv_scale_t * scale = (lv_scale_t *)obj;
    const bool is_first_tick = 0U == tick_idx;
    const bool is_last_tick = (scale->total_tick_count - 1U) == tick_idx;
    const int32_t tick_width = is_major_tick ? major_tick_width : minor_tick_width;

    if(((!is_last_tick) && (!is_first_tick))
       || ((LV_SCALE_MODE_ROUND_INNER == scale->mode) || (LV_SCALE_MODE_ROUND_OUTER == scale->mode))) {
        return;
    }

    if(is_last_tick) {
        if((LV_SCALE_MODE_VERTICAL_LEFT == scale->mode) || (LV_SCALE_MODE_VERTICAL_RIGHT == scale->mode)) {
            scale->last_tick_width = tick_width;
        }
        else {
            scale->first_tick_width = tick_width;
        }
    }
    else {
        if((LV_SCALE_MODE_VERTICAL_LEFT == scale->mode) || (LV_SCALE_MODE_VERTICAL_RIGHT == scale->mode)) {
            scale->first_tick_width = tick_width;
        }
        else {
            scale->last_tick_width = tick_width;
        }
    }
}

static void scale_build_custom_label_text(lv_obj_t * obj, lv_draw_label_dsc_t * label_dsc,
                                          const uint16_t major_tick_idx)
{
    lv_scale_t * scale = (lv_scale_t *) obj;

    if(major_tick_idx <= scale->custom_label_cnt) {
        if(scale->txt_src[major_tick_idx - 1U]) {
            label_dsc->text = scale->txt_src[major_tick_idx - 1U];
            label_dsc->text_local = 0;
        }
        else {
            label_dsc->text = NULL;
        }
    }
    else {
        label_dsc->text = NULL;
    }
}

static void scale_store_section_line_tick_width_compensation(lv_obj_t * obj, const bool is_major_tick,
                                                             lv_draw_line_dsc_t * major_tick_dsc, lv_draw_line_dsc_t * minor_tick_dsc,
                                                             const int32_t tick_value, const uint8_t tick_idx, lv_point_t * tick_point_a)
{
    lv_scale_t * scale = (lv_scale_t *) obj;
    lv_scale_section_t * section;

    LV_LL_READ_BACK(&scale->section_ll, section) {
        if(section->range_min <= tick_value && section->range_max >= tick_value) {
            if(is_major_tick) scale_set_line_properties(obj, major_tick_dsc, section->indicator_style, LV_PART_INDICATOR);
            else scale_set_line_properties(obj, minor_tick_dsc, section->items_style, LV_PART_ITEMS);
        }

        int32_t tmp_width = 0;

        if(tick_idx == section->first_tick_idx_in_section) {
            if(section->first_tick_idx_is_major) tmp_width = major_tick_dsc->width;
            else tmp_width = minor_tick_dsc->width;

            section->first_tick_in_section = *tick_point_a;
            if(tmp_width & 0x01U) {
                if(LV_SCALE_MODE_VERTICAL_LEFT == scale->mode || LV_SCALE_MODE_VERTICAL_RIGHT == scale->mode) tmp_width += 1;
                else tmp_width -= 1;
            }
            section->first_tick_in_section_width = tmp_width;
        }

        if(tick_idx == section->last_tick_idx_in_section) {
            if(section->last_tick_idx_is_major) tmp_width = major_tick_dsc->width;
            else tmp_width = minor_tick_dsc->width;

            section->last_tick_in_section = *tick_point_a;
            if(tmp_width & 0x01U) {
                if(LV_SCALE_MODE_VERTICAL_LEFT == scale->mode || LV_SCALE_MODE_VERTICAL_RIGHT == scale->mode) tmp_width -= 1;
                else tmp_width += 1;
            }
            section->last_tick_in_section_width = tmp_width;
        }
    }
}

static void scale_free_line_needle_points_cb(lv_event_t * e)
{
    lv_point_precise_t * needle_line_points = lv_event_get_user_data(e);
    lv_free(needle_line_points);
}

static bool scale_is_major_tick(lv_scale_t * scale, uint32_t tick_idx)
{
    return scale->major_tick_every != 0 && tick_idx % scale->major_tick_every == 0;
}

#if LV_USE_OBSERVER
static void scale_section_min_value_observer_cb(lv_observer_t * observer, lv_subject_t * subject)
{
    lv_scale_section_t * section = observer->user_data;
    lv_scale_set_section_min_value(observer->target, section, subject->value.num);
}

static void scale_section_max_value_observer_cb(lv_observer_t * observer, lv_subject_t * subject)
{
    lv_scale_section_t * section = observer->user_data;
    lv_scale_set_section_max_value(observer->target, section, subject->value.num);
}
#endif /*LV_USE_OBSERVER*/

#endif
