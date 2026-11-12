// Agent Under Fire's settings as the launcher shows them: all in settings.toml (app::Settings in that game's
// sdk/runtime/app/settings.cpp; its renderer is Vulkan only, so no graphics.toml). Defaults and ranges are the code's;
// grouped as the game's F1 window. pick() is schema.js's.
window.SCHEMA_AUF = [
    {
        id: 'display', title: 'Display', icon: 'monitor', groups: [
            {
                title: 'Screen', items: [
                    { f: 's', key: 'display_mode', label: 'Display mode', def: 'borderless', options: pick(['fullscreen', 'Fullscreen'], ['borderless', 'Borderless window'], ['windowed', 'Window']) },
                    { f: 's', key: 'resolution', label: 'Resolution', def: '0x0', virtual: true, options: pick(['0x0', 'Desktop'], ['1280x720', '1280 x 720'], ['1600x900', '1600 x 900'], ['1920x1080', '1920 x 1080'], ['2560x1080', '2560 x 1080'], ['2560x1440', '2560 x 1440'], ['3440x1440', '3440 x 1440'], ['3840x2160', '3840 x 2160']) },
                    { f: 's', key: 'monitor', label: 'Monitor', def: 0, type: 'int', min: 0, max: 8, help: '0 is the primary display, 1 and up the others.' },
                    { f: 's', key: 'aspect', label: 'Aspect ratio', def: 'auto', restart: true, options: pick(['auto', 'From the screen\'s shape'], ['4:3', '4:3'], ['16:9', '16:9 (widescreen)'], ['screen', 'Fill the screen (ultrawide)']) },
                    { f: 's', key: 'vsync', label: 'Vertical sync', def: true },
                    { f: 's', key: 'low_latency', label: 'Low latency', def: false, restart: true, help: 'Holds one frame in the swap chain.' },
                    { f: 's', key: 'fps_cap', label: 'Frame rate cap', def: 60, options: pick([30, '30'], [60, '60'], [120, '120'], [144, '144'], [240, '240'], [0, 'Unlimited']) },
                    { f: 's', key: 'pause_unfocused', label: 'Pause in background', def: false, help: 'Pause and mute when the window is not in front.' },
                    { f: 's', key: 'show_fps', label: 'Frame counter', def: false },
                    { f: 's', key: 'language', label: 'Language', def: 1, restart: true, options: pick([1, 'English'], [3, 'German'], [4, 'French']), help: 'The USA disc is English only.' },
                ],
            },
        ],
    },
    {
        id: 'graphics', title: 'Graphics', icon: 'chip', groups: [
            {
                title: 'Quality', items: [
                    { f: 's', key: 'render_percent', label: 'Render scale', def: 100, type: 'range', min: 25, max: 200, step: 5, unit: '%' },
                    { f: 's', key: 'scale_filter', label: 'Scaling filter', def: 0, options: pick([0, 'Smooth (bilinear)'], [1, 'Sharp (Catmull-Rom)']) },
                    { f: 's', key: 'anisotropic_filtering', label: 'Texture filtering', def: 0, options: pick([0, 'As the game asks'], [2, '2x'], [4, '4x'], [8, '8x'], [16, '16x']) },
                    { f: 's', key: 'msaa', label: 'MSAA', def: 'off', options: pick(['off', 'Off'], ['msaa2', '2x'], ['msaa4', '4x'], ['msaa8', '8x']) },
                    { f: 's', key: 'alpha_coverage', label: 'Smooth fences and leaves', def: false, help: 'With MSAA: alpha-tested edges get partial coverage.' },
                    { f: 's', key: 'fxaa', label: 'FXAA', def: false },
                    { f: 's', key: 'smaa', label: 'SMAA', def: false, help: 'In place of FXAA.' },
                    { f: 's', key: 'temporal_aa', label: 'Temporal anti-aliasing', def: false, help: 'Smoothest edges; the weapon and moving characters can ghost.' },
                    { f: 's', key: 'upscaler', label: 'Upscaler', def: 0, options: pick([0, 'Off'], [1, 'AMD FSR 2']), help: 'The 3D scene drawn smaller and rebuilt over frames, the HUD sharp; Temporal AA and SMAA are left out with it.' },
                    { f: 's', key: 'upscale_quality', label: 'Upscale quality', def: 0, options: pick([0, 'Quality (1.5x)'], [1, 'Balanced (1.7x)'], [2, 'Performance (2x)'], [3, 'Ultra Performance (3x)']) },
                    { f: 's', key: 'upscale_sharpness', label: 'Upscale sharpness', def: 30, type: 'range', min: 0, max: 100, step: 5, unit: '%', help: "FSR 2's own sharpening." },
                    { f: 's', key: 'sharpen', label: 'Sharpening', def: 0, type: 'range', min: 0, max: 100, step: 5, unit: '%' },
                    { f: 's', key: 'world_textures', label: 'Enlarge world textures', def: false, help: 'The 3D scene\'s textures enlarged twice in the background.' },
                    { f: 's', key: 'sharp_2d', label: 'Sharp text and HUD', def: true },
                    { f: 's', key: 'movie_upscale', label: 'Upscale movies', def: true },
                ],
            },
        ],
    },
    {
        id: 'effects', title: 'Effects', icon: 'palette', groups: [
            {
                title: 'Shader style', items: [
                    { f: 's', key: 'shader_style', label: 'Shader style', def: 0, options: pick([0, 'Original'], [1, 'Enhanced'], [2, 'Toon'], [3, 'Retro']) },
                    { f: 's', key: 'tonemap', label: 'Tone mapping', def: 0, options: pick([0, 'Off'], [1, 'Filmic'], [2, 'Reinhard']), help: 'With a style other than Original.' },
                    { f: 's', key: 'style_clarity', label: 'Clarity (Enhanced)', def: 30, type: 'range', min: 0, max: 100, step: 5, unit: '%' },
                    { f: 's', key: 'toon_bands', label: 'Lighting bands (Toon)', def: 4, type: 'range', min: 2, max: 12, step: 1 },
                    { f: 's', key: 'toon_outline', label: 'Outline thickness (Toon)', def: 15, type: 'range', min: 0, max: 60, step: 1 },
                    { f: 's', key: 'toon_inner_lines', label: 'Inner lines (Toon)', def: 1, options: pick([1, 'On'], [0, 'Off']) },
                    { f: 's', key: 'toon_edge_threshold', label: 'Edge threshold (Toon)', def: 20, type: 'range', min: 2, max: 100, step: 2, unit: '%' },
                    { f: 's', key: 'toon_halftone', label: 'Halftone (Toon)', def: 0, type: 'range', min: 0, max: 100, step: 5, unit: '%' },
                    { f: 's', key: 'retro_pixel', label: 'Pixel size (Retro)', def: 3, type: 'range', min: 1, max: 8, step: 1 },
                    { f: 's', key: 'retro_color_bits', label: 'Colour depth (Retro)', def: 5, type: 'range', min: 2, max: 8, step: 1, unit: ' bits' },
                    { f: 's', key: 'retro_dither', label: 'Ordered dithering (Retro)', def: 1, options: pick([1, 'On'], [0, 'Off']) },
                    { f: 's', key: 'retro_tint', label: 'Retro grading', def: 40, type: 'range', min: 0, max: 100, step: 5, unit: '%' },
                ],
            },
            {
                title: 'Style', items: [
                    { f: 's', key: 'color_style', label: 'Colour style', def: 0, options: pick([0, 'Original'], [1, 'Enhanced colour'], [2, 'Toon'], [3, 'Retro'], [4, 'Noir']) },
                    { f: 's', key: 'bloom', label: 'Bloom', def: 0, type: 'range', min: 0, max: 100, step: 5, unit: '%' },
                    { f: 's', key: 'light_shafts', label: 'Light shafts', def: 0, type: 'range', min: 0, max: 100, step: 5, unit: '%' },
                    { f: 's', key: 'atmospheric_fog', label: 'Atmospheric fog', def: 0, type: 'range', min: 0, max: 100, step: 5, unit: '%' },
                    { f: 's', key: 'ambient_occlusion', label: 'Ambient occlusion', def: false, help: 'Ray traced on GPUs that can, else screen-space.' },
                    { f: 's', key: 'vignette', label: 'Vignette', def: 0, type: 'range', min: 0, max: 100, step: 5, unit: '%' },
                    { f: 's', key: 'film_grain', label: 'Film grain', def: 0, type: 'range', min: 0, max: 100, step: 5, unit: '%' },
                ],
            },
            {
                title: 'Camera', items: [
                    { f: 's', key: 'depth_of_field', label: 'Depth of field', def: false },
                    { f: 's', key: 'focus_distance', label: 'Focus distance', def: 10, type: 'range', min: 1, max: 100, step: 1, unit: ' m' },
                    { f: 's', key: 'dof_blur', label: 'Blur size', def: 6, type: 'range', min: 0, max: 12, step: 1, unit: ' px' },
                    { f: 's', key: 'motion_blur', label: 'Motion blur', def: false, help: 'From the camera\'s movement.' },
                    { f: 's', key: 'motion_blur_strength', label: 'Motion blur strength', def: 60, type: 'range', min: 0, max: 150, step: 5, unit: '%' },
                    { f: 's', key: 'motion_blur_limit', label: 'Motion blur limit', def: 8, type: 'range', min: 1, max: 16, step: 1, unit: ' px' },
                ],
            },
            {
                title: 'Colour', items: [
                    { f: 's', key: 'auto_exposure', label: 'Auto exposure', def: false, help: 'Metered exposure and tone mapping.' },
                    { f: 's', key: 'exposure', label: 'Brightness', def: 100, type: 'range', min: 50, max: 150, step: 5, unit: '%' },
                    { f: 's', key: 'contrast', label: 'Contrast', def: 100, type: 'range', min: 50, max: 150, step: 5, unit: '%' },
                    { f: 's', key: 'saturation', label: 'Saturation', def: 100, type: 'range', min: 0, max: 200, step: 5, unit: '%' },
                    { f: 's', key: 'temperature', label: 'Colour temperature', def: 0, type: 'range', min: -100, max: 100, step: 5, help: 'Cool to warm.' },
                ],
            },
        ],
    },
    {
        id: 'controls', title: 'Controls', icon: 'pad', groups: [
            {
                title: 'Mouse and keyboard', items: [
                    { f: 's', key: 'keyboard', label: 'Keyboard plays as controller 1', def: true },
                    { f: 's', key: 'mouse_look', label: 'Mouse look', def: true },
                    { f: 's', key: 'mouse_sensitivity', label: 'Mouse sensitivity', def: 100, type: 'range', min: 10, max: 400, step: 5, unit: '%' },
                    { f: 's', key: 'mouse_invert_y', label: 'Invert mouse Y', def: false },
                    { f: 's', key: 'join_ip', label: 'Host address (network play)', def: '', type: 'text', help: 'A co-op host past your network that the system link search also asks.' },
                ],
            },
            {
                title: 'Wheel (driving missions)', items: [
                    { f: 's', key: 'wheel_range', label: 'Wheel rotation', def: 900, type: 'range', min: 180, max: 2880, step: 10, unit: '°' },
                    { f: 's', key: 'wheel_lock', label: 'Full steering at', def: 360, type: 'range', min: 90, max: 1080, step: 10, unit: '°' },
                    { f: 's', key: 'wheel_deadzone', label: 'Dead zone', def: 0, type: 'range', min: 0, max: 20, step: 1, unit: '%' },
                    { f: 's', key: 'ffb_strength', label: 'Force feedback', def: 70, type: 'range', min: 0, max: 100, step: 5, unit: '%' },
                    { f: 's', key: 'ffb_road', label: 'Road and impacts', def: 50, type: 'range', min: 0, max: 100, step: 5, unit: '%' },
                ],
            },
        ],
        note: 'Keys and pad buttons: in the game, F1 > Controls.',
    },
];
