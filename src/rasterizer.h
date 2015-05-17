// rasterizer.h
// (c) Copyright 2015 by Sergio Gonzalez

// This actually makes things faster
typedef struct LinkedList_Stroke_s
{
    Stroke* elem;
    struct LinkedList_Stroke_s* next;
} LinkedList_Stroke;

// Filter strokes and render them. See `render_strokes` for the one that should be called
static void render_strokes_in_rect(MiltonState* milton_state, Rect limits)
{
    uint32* pixels = (uint32*)milton_state->raster_buffer;
    Stroke* strokes = milton_state->strokes;

    Rect canvas_limits;
    canvas_limits.top_left = raster_to_canvas(milton_state->screen_size, milton_state->view_scale,
            limits.top_left);
    canvas_limits.bot_right = raster_to_canvas(milton_state->screen_size, milton_state->view_scale,
            limits.bot_right);

    LinkedList_Stroke* stroke_list = NULL;

    // Go backwards so that list is in the correct older->newer order.
    for (int stroke_i = milton_state->num_strokes; stroke_i >= 0; --stroke_i)
    {
        Stroke* stroke = NULL;
        if (stroke_i == milton_state->num_strokes)
        {
            stroke = &milton_state->working_stroke;
        }
        else
        {
            stroke = &strokes[stroke_i];
        }
        assert(stroke);
        Rect enlarged_limits = rect_enlarge(canvas_limits, stroke->brush.radius);
        stroke_clip_to_rect(stroke, enlarged_limits);
        if (stroke->num_clipped_points)
        {
            LinkedList_Stroke* list_elem = arena_alloc_elem(
                    milton_state->transient_arena, LinkedList_Stroke);

            LinkedList_Stroke* tail = stroke_list;
            list_elem->elem = stroke;
            list_elem->next = stroke_list;
            stroke_list = list_elem;
        }
        // TODO:
        // Check if `limits` lies completely inside stroke.
        // If so, don't add it to the list, just keep track of it so we can do
        // a cheap fill-pass.
        // TODO
        // Every stroke that fills and that is completely opaque resets every
        // stroke before it!
    }

    for (int j = limits.top; j < limits.bottom; ++j)
    {
        for (int i = limits.left; i < limits.right; ++i)
        {
            v2i raster_point = {i, j};
            v2i canvas_point = raster_to_canvas(
                    milton_state->screen_size, milton_state->view_scale, raster_point);

            // Clear color
            float dr = 1.0f;
            float dg = 1.0f;
            float db = 1.0f;
            float da = 1.0f;

            struct LinkedList_Stroke_s* list_iter = stroke_list;
            while(list_iter)
            {
                Stroke* stroke = list_iter->elem;

                assert (stroke);
                v2i* points = stroke->clipped_points;

                v2i min_point = {0};
                float min_dist = FLT_MAX;
                float dx = 0;
                float dy = 0;
                //int64 radius_squared = stroke->brush.radius * stroke->brush.radius;
                if (stroke->num_clipped_points == 1)
                {
                    min_point = points[0];
                    dx = (float) (canvas_point.x - min_point.x);
                    dy = (float) (canvas_point.y - min_point.y);
                    min_dist = dx * dx + dy * dy;
                }
                else
                {
                    // Find closest point.
                    for (int point_i = 0; point_i < stroke->num_clipped_points - 1; point_i += 2)
                    {
                        v2i a = points[point_i];
                        v2i b = points[point_i + 1];

                        v2f ab = {(float)b.x - a.x, (float)b.y - a.y};
                        float mag_ab2 = ab.x * ab.x + ab.y * ab.y;
                        if (mag_ab2 > 0)
                        {
                            float mag_ab = sqrtf(mag_ab2);
                            float d_x = ab.x / mag_ab;
                            float d_y = ab.y / mag_ab;
                            // TODO: Maybe store these and not do conversion in the hot loop?
                            float ax_x = (float)(canvas_point.x - a.x);
                            float ax_y = (float)(canvas_point.y - a.y);
                            float disc = d_x * ax_x + d_y * ax_y;
                            v2i point;
                            if (disc >= 0 && disc <= mag_ab)
                            {
                                point = (v2i)
                                {
                                    (int32)(a.x + disc * d_x), (int32)(a.y + disc * d_y),
                                };
                            }
                            else if (disc < 0)
                            {
                                point = a;
                            }
                            else
                            {
                                point = b;
                            }
                            float test_dx = (float) (canvas_point.x - point.x);
                            float test_dy = (float) (canvas_point.y - point.y);
                            float dist = test_dx * test_dx + test_dy * test_dy;
                            if (dist < min_dist)
                            {
                                min_dist = dist;
                                min_point = point;
                                dx = test_dx;
                                dy = test_dy;
                            }
                        }
                    }
                }


                if (min_dist < FLT_MAX)
                {
                    int samples = 0;
                    {
                        float u = 0.223607f * milton_state->view_scale;  // sin(arctan(1/2)) / 2
                        float v = 0.670820f * milton_state->view_scale;  // cos(arctan(1/2)) / 2 + u

                        float dists[4];
                        dists[0] = (dx - u) * (dx - u) + (dy - v) * (dy - v);
                        dists[1] = (dx - v) * (dx - v) + (dy + u) * (dy + u);
                        dists[2] = (dx + u) * (dx + u) + (dy + v) * (dy + v);
                        dists[3] = (dx + v) * (dx + v) + (dy + u) * (dy + u);
                        for (int i = 0; i < 4; ++i)
                        {
                            if (sqrtf(dists[i]) < stroke->brush.radius)
                            {
                                ++samples;
                            }
                        }
                    }

                    // If the stroke contributes to the pixel, do compositing.
                    if (samples > 0)
                    {
                        // Do compositing
                        // ---------------

                        float coverage = (float)samples / 4.0f;

                        float sr = stroke->brush.color.r;
                        float sg = stroke->brush.color.g;
                        float sb = stroke->brush.color.b;
                        float sa = stroke->brush.alpha;

                        sa *= coverage;

                        dr = (1 - sa) * dr + sa * sr;
                        dg = (1 - sa) * dg + sa * sg;
                        db = (1 - sa) * db + sa * sb;
                        da = sa + da * (1 - sa);
                    }
                }

                list_iter = list_iter->next;

            }
            // From [0, 1] to [0, 255]
            v4f d = {
                dr, dg, db, da
            };
            uint32 pixel = color_v4f_to_u32(milton_state->cm, d);
            pixels[j * milton_state->screen_size.w + i] = pixel;
        }
    }
}

static void render_strokes(MiltonState* milton_state, Rect limits)
{
    Rect* split_rects = NULL;
    int32 num_rects = rect_split(milton_state->transient_arena,
            limits, 20, 20, &split_rects);
    for (int i = 0; i < num_rects; ++i)
    {
        split_rects[i] = rect_clip_to_screen(split_rects[i], milton_state->screen_size);
        render_strokes_in_rect(milton_state, split_rects[i]);
    }
}

static void render_picker(ColorPicker* picker, ColorManagement cm,
        Rect draw_rect, uint32* pixels, v2i screen_size, int32 view_scale)
{
    v2f baseline = {1,0};

    v4f background_color =
    {
        0.5f,
        0.5f,
        0.55f,
        0.4f,
    };

    // Copy canvas buffer into picker buffer
    for (int j = draw_rect.top; j < draw_rect.bottom; ++j)
    {
        for (int i = draw_rect.left; i < draw_rect.right; ++i)
        {
            uint32 picker_i = (j - draw_rect.top) * (2*picker->bound_radius_px ) + (i - draw_rect.left);
            uint32 src = pixels[j * screen_size.w + i];
            picker->pixels[picker_i] = src;
        }
    }

    // Render background color.
    for (int j = draw_rect.top; j < draw_rect.bottom; ++j)
    {
        for (int i = draw_rect.left; i < draw_rect.right; ++i)
        {
            uint32 picker_i = (j - draw_rect.top) *( 2*picker->bound_radius_px ) + (i - draw_rect.left);
            v4f dest = color_u32_to_v4f(cm, picker->pixels[picker_i]);
            float alpha = background_color.a;
            v4f result =
            {
                dest.r * (1 - alpha) + background_color.r * alpha,
                dest.g * (1 - alpha) + background_color.g * alpha,
                dest.b * (1 - alpha) + background_color.b * alpha,
                dest.a + (alpha * (1 - dest.a)),
            };
            picker->pixels[picker_i] = color_v4f_to_u32(cm, result);
        }
    }

    // render wheel
    for (int j = draw_rect.top; j < draw_rect.bottom; ++j)
    {
        for (int i = draw_rect.left; i < draw_rect.right; ++i)
        {
            uint32 picker_i = (j - draw_rect.top) *( 2*picker->bound_radius_px ) + (i - draw_rect.left);
            v2f point = {(float)i, (float)j};
            uint32 dest_color = picker->pixels[picker_i];

            int samples = 0;
            {
                float u = 0.223607f;
                float v = 0.670820f;

                samples += (int)picker_hits_wheel(picker, add_v2f(point, (v2f){-u, -v}));
                samples += (int)picker_hits_wheel(picker, add_v2f(point, (v2f){-v, u}));
                samples += (int)picker_hits_wheel(picker, add_v2f(point, (v2f){u, v}));
                samples += (int)picker_hits_wheel(picker, add_v2f(point, (v2f){v, u}));
            }

            if (samples > 0)
            {
                float angle = picker_wheel_get_angle(picker, point);
                float degree = radians_to_degrees(angle);
                v3f hsv = { degree, 1.0f, 1.0f };
                v3f rgb = hsv_to_rgb(hsv);

                float contrib = samples / 4.0f;

                v4f d = color_u32_to_v4f(cm, dest_color);

                v4f result =
                {
                    ((1 - contrib) * (d.r)) + (contrib * (rgb.r)),
                    ((1 - contrib) * (d.g)) + (contrib * (rgb.g)),
                    ((1 - contrib) * (d.b)) + (contrib * (rgb.b)),
                    d.a + (contrib * (1 - d.a)),
                };
                uint32 color = color_v4f_to_u32(cm, result);
                picker->pixels[picker_i] = color;
            }
        }
    }

    // Render triangle
    for (int j = draw_rect.top; j < draw_rect.bottom; ++j)
    {
        for (int i = draw_rect.left; i < draw_rect.right; ++i)
        {
            v2f point = { (float)i, (float)j };
            uint32 picker_i = (j - draw_rect.top) *( 2*picker->bound_radius_px ) + (i - draw_rect.left);
            uint32 dest_color = picker->pixels[picker_i];
            // MSAA!!
            int samples = 0;
            {
                float u = 0.223607f;
                float v = 0.670820f;

                samples += (int)is_inside_triangle(add_v2f(point, (v2f){-u, -v}),
                        picker->a, picker->b, picker->c);
                samples += (int)is_inside_triangle(add_v2f(point, (v2f){-v, u}),
                        picker->a, picker->b, picker->c);
                samples += (int)is_inside_triangle(add_v2f(point, (v2f){u, v}),
                        picker->a, picker->b, picker->c);
                samples += (int)is_inside_triangle(add_v2f(point, (v2f){v, u}),
                        picker->a, picker->b, picker->c);
            }

            if (samples > 0)
            {
                v3f hsv = picker_hsv_from_point(picker, point);

                float contrib = samples / 4.0f;

                v4f d = color_u32_to_v4f(cm, dest_color);

                v3f rgb = hsv_to_rgb(hsv);

                v4f result =
                {
                    ((1 - contrib) * (d.r)) + (contrib * (rgb.r)),
                    ((1 - contrib) * (d.g)) + (contrib * (rgb.g)),
                    ((1 - contrib) * (d.b)) + (contrib * (rgb.b)),
                    d.a + (contrib * (1 - d.a)),
                };

                picker->pixels[picker_i] = color_v4f_to_u32(cm, result);
            }
        }
    }

    // Blit picker pixels
    uint32* to_blit = picker->pixels;
    for (int j = draw_rect.top; j < draw_rect.bottom; ++j)
    {
        for (int i = draw_rect.left; i < draw_rect.right; ++i)
        {
            uint32 linear_color = *to_blit++;
            v4f sRGB = linear_to_sRGB_v4(color_u32_to_v4f(cm, linear_color));
            uint32 color = color_v4f_to_u32(cm, sRGB);
            pixels[j * screen_size.w + i] = color;
        }
    }
}

typedef enum
{
    MiltonRenderFlags_none              = 0,
    MiltonRenderFlags_picker_updated    = (1 << 0),
    MiltonRenderFlags_full_redraw       = (1 << 1),
} MiltonRenderFlags;

static void milton_render(MiltonState* milton_state, MiltonRenderFlags render_flags)
{
    // `limits` is the part of the screen (in pixels) that should be updated
    // with what's on the canvas.
    Rect limits = { 0 };

    // Figure out what `limits` should be.
    {
        if (render_flags & MiltonRenderFlags_full_redraw)
        {
            limits.left = 0;
            limits.right = milton_state->screen_size.w;
            limits.top = 0;
            limits.bottom = milton_state->screen_size.h;
        }
        else if (milton_state->working_stroke.num_points > 1)
        {
            Stroke* stroke = &milton_state->working_stroke;
            v2i new_point = canvas_to_raster(
                    milton_state->screen_size, milton_state->view_scale,
                    stroke->points[stroke->num_points - 2]);

            limits.left =   min (milton_state->last_point.x, new_point.x);
            limits.right =  max (milton_state->last_point.x, new_point.x);
            limits.top =    min (milton_state->last_point.y, new_point.y);
            limits.bottom = max (milton_state->last_point.y, new_point.y);
            limits = rect_enlarge(limits, (stroke->brush.radius / milton_state->view_scale));
            limits = rect_clip_to_screen(limits, milton_state->screen_size);
        }
        else if (milton_state->working_stroke.num_points == 1)
        {
            Stroke* stroke = &milton_state->working_stroke;
            v2i point = canvas_to_raster(milton_state->screen_size, milton_state->view_scale,
                    stroke->points[0]);
            int32 raster_radius = stroke->brush.radius / milton_state->view_scale;
            limits.left = -raster_radius  + point.x;
            limits.right = raster_radius  + point.x;
            limits.top = -raster_radius   + point.y;
            limits.bottom = raster_radius + point.y;
            limits = rect_clip_to_screen(limits, milton_state->screen_size);
        }
    }

    render_strokes(milton_state, limits);

    // Render UI
    {
        Rect* split_rects = NULL;
        bool32 redraw = false;
        Rect draw_rect = picker_get_draw_rect(&milton_state->picker);
        int32 num_rects = rect_split(milton_state->transient_arena,
                draw_rect, 10, 10, &split_rects);
        for (int i = 0; i < num_rects; ++i)
        {
            Rect clipped = rect_intersect(split_rects[i], draw_rect);
            if ((clipped.left != clipped.right) && clipped.top != clipped.bottom)
            {
                redraw = true;
                break;
            }
        }
        if (redraw || (render_flags & MiltonRenderFlags_picker_updated))
        {
            render_strokes(milton_state, draw_rect);
            render_picker(&milton_state->picker, milton_state->cm,
                    draw_rect, (uint32*)milton_state->raster_buffer,
                    milton_state->screen_size, milton_state->view_scale);
        }
    }
}
