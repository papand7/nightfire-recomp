// The game's settings as the launcher shows them: settings.toml (file 's', app::Settings in
// sdk/runtime/app/settings.cpp, the game's own in src/common/game_info.cpp) and graphics.toml (file 'g',
// gpu::GraphicsStyle in sdk/runtime/gpu/style.cpp). Defaults and ranges are the code's; a key the file lacks shows
// its default. Grouped as the game's F1 window.
const onOff = null;  // a bool: a switch
const pick = (...pairs) => pairs.map(([value, label]) => ({ value, label }));
const KEYS = pick([0, 'None'], [112, 'F1'], [113, 'F2'], [114, 'F3'], [115, 'F4'], [116, 'F5'], [117, 'F6'], [118, 'F7'], [119, 'F8'], [120, 'F9'], [121, 'F10'], [122, 'F11'], [123, 'F12'], [44, 'Print Screen']);

window.SCHEMA = [
    {
        id: 'display', title: 'Display', icon: 'monitor', groups: [
            {
                title: 'Screen', items: [
                    { f: 's', key: 'display_mode', label: 'Display mode', def: 'borderless', options: pick(['fullscreen', 'Fullscreen'], ['borderless', 'Borderless window'], ['windowed', 'Window']) },
                    { f: 's', key: 'resolution', label: 'Resolution', def: '0x0', virtual: true, options: pick(['0x0', 'Desktop'], ['1280x720', '1280 x 720'], ['1600x900', '1600 x 900'], ['1920x1080', '1920 x 1080'], ['2560x1080', '2560 x 1080'], ['2560x1440', '2560 x 1440'], ['3440x1440', '3440 x 1440'], ['3840x2160', '3840 x 2160']) },
                    { f: 's', key: 'monitor', label: 'Monitor', def: 0, type: 'int', min: 0, max: 8, help: '0 is the primary display, 1 and up the others.' },
                    { f: 's', key: 'aspect', label: 'Aspect ratio', def: 'auto', restart: true, options: pick(['auto', 'From the screen\'s shape'], ['4:3', '4:3'], ['16:9', '16:9 (widescreen)'], ['screen', 'Fill the screen (ultrawide)']) },
                    { f: 's', key: 'vsync', label: 'Vertical sync', def: true },
                    { f: 's', key: 'fps_cap', label: 'Frame rate cap', def: 60, options: pick([30, '30'], [60, '60'], [120, '120'], [144, '144'], [240, '240'], [0, 'Unlimited']) },
                    { f: 's', key: 'pause_unfocused', label: 'Pause in background', def: false, help: 'Pause and mute when the window is not in front.' },
                ],
            },
        ],
    },
    {
        id: 'graphics', title: 'Graphics', icon: 'chip', groups: [
            {
                title: 'Quality', items: [
                    { f: 'g', key: 'quality', label: 'Graphics quality', def: 'custom', options: pick(['low', 'Low'], ['medium', 'Medium'], ['high', 'High'], ['ultra', 'Ultra'], ['custom', 'Custom']), help: 'Sets filtering, anti-aliasing, ambient occlusion and bloom together.' },
                    { f: 's', key: 'renderer', label: 'Renderer', def: 'd3d12', restart: true, options: pick(['vulkan', 'Vulkan (all effects)'], ['d3d12', 'Direct3D 12'], ['d3d11', 'Direct3D 11']), help: 'Shader styles and effects need Vulkan.' },
                    { f: 's', key: 'render_percent', label: 'Render scale', def: 100, type: 'range', min: 25, max: 200, step: 5, unit: '%' },
                    { f: 's', key: 'anisotropic_filtering', label: 'Texture filtering', def: 0, options: pick([0, 'As the game asks'], [2, '2x'], [4, '4x'], [8, '8x'], [16, '16x']) },
                    { f: 's', key: 'fxaa', label: 'FXAA', def: false },
                    { f: 's', key: 'smaa', label: 'SMAA', def: false, vulkan: true },
                    { f: 's', key: 'msaa', label: 'MSAA', def: 'off', options: pick(['off', 'Off'], ['msaa2', '2x'], ['msaa4', '4x'], ['msaa8', '8x']), help: 'Direct3D only.' },
                    { f: 's', key: 'sharpen', label: 'Sharpening', def: 0, type: 'range', min: 0, max: 100, step: 5, unit: '%' },
                ],
            },
            {
                title: 'Screen', items: [
                    { f: 's', key: 'show_fps', label: 'Frame counter', def: false },
                    { f: 's', key: 'perf_overlay', label: 'Performance overlay', def: false, help: 'A corner panel: FPS, a frame time graph, GPU time and the render size.' },
                    { f: 's', key: 'sharp_2d', label: 'Sharp text and HUD', def: true },
                    { f: 's', key: 'hit_feedback', label: 'Hit feedback', def: 2, options: pick([0, 'Off'], [1, 'Marker'], [2, 'Marker and sound']) },
                    { f: 's', key: 'menu_colors', label: 'Menu colours', def: 0, options: pick([0, 'Original'], [1, 'PC Blue']) },
                    { f: 's', key: 'join_ip', label: 'Host address (network play)', def: '', type: 'text', help: 'The PC to join: its address on your network or the internet (UDP port 27015 open there).' },
                    { f: 's', key: 'modern_menus', label: 'Menus', def: true, options: pick([false, "Classic (the game's own)"], [true, 'Modern (blue, made for the mouse)']) },
                ],
            },
        ],
    },
    {
        id: 'style', title: 'Shader Style', icon: 'palette', vulkan: true, groups: [
            {
                title: 'Style', items: [
                    { f: 'g', key: 'mode', label: 'Rendering', def: 'original', options: pick(['original', 'Original'], ['enhanced', 'Enhanced'], ['toon', 'Toon'], ['retro', 'Retro']) },
                    { f: 'g', key: 'texture_filter', label: 'Scene texture filtering', def: 'game', options: pick(['game', 'As the game'], ['nearest', 'Nearest (pixelated)'], ['anisotropic', 'Anisotropic 16x']) },
                ],
            },
            {
                title: 'Post-processing', when: g => g.mode !== 'original', items: [
                    { f: 'g', key: 'tonemap', label: 'Tone mapping', def: 'off', options: pick(['off', 'Off'], ['filmic', 'Filmic'], ['reinhard', 'Reinhard']) },
                    { f: 'g', key: 'exposure', label: 'Exposure', def: 1, type: 'range', min: 0.25, max: 4, step: 0.05 },
                    { f: 'g', key: 'contrast', label: 'Contrast', def: 1, type: 'range', min: 0.5, max: 2, step: 0.01 },
                    { f: 'g', key: 'saturation', label: 'Saturation', def: 1, type: 'range', min: 0, max: 2.5, step: 0.01 },
                    { f: 'g', key: 'temperature', label: 'Temperature', def: 0, type: 'range', min: -1, max: 1, step: 0.01 },
                    { f: 'g', key: 'ambient_occlusion', label: 'Ambient occlusion', def: 0, type: 'range', min: 0, max: 2, step: 0.05 },
                    { f: 'g', key: 'ao_radius', label: 'AO radius', def: 1, type: 'range', min: 0.25, max: 4, step: 0.05 },
                    { f: 'g', key: 'bloom', label: 'Bloom', def: 0, type: 'range', min: 0, max: 2, step: 0.05 },
                    { f: 'g', key: 'bloom_threshold', label: 'Bloom threshold', def: 0.75, type: 'range', min: 0, max: 1, step: 0.01 },
                    { f: 'g', key: 'sharpen', label: 'Detail (sharpening)', def: 0, type: 'range', min: 0, max: 1, step: 0.01 },
                    { f: 'g', key: 'vignette', label: 'Vignette', def: 0, type: 'range', min: 0, max: 1, step: 0.01 },
                    { f: 'g', key: 'grain', label: 'Film grain', def: 0, type: 'range', min: 0, max: 1, step: 0.01 },
                ],
            },
            {
                title: 'Toon', when: g => g.mode === 'toon', items: [
                    { f: 'g', key: 'bands', label: 'Lighting bands', def: 4, type: 'range', min: 2, max: 12, step: 1 },
                    { f: 'g', key: 'outline', label: 'Outline thickness', def: 1.5, type: 'range', min: 0, max: 6, step: 0.1 },
                    { f: 'g', key: 'internal_edges', label: 'Inner lines', def: true },
                    { f: 'g', key: 'edge_threshold', label: 'Edge threshold', def: 0.2, type: 'range', min: 0.02, max: 1, step: 0.01 },
                    { f: 'g', key: 'halftone', label: 'Halftone', def: 0, type: 'range', min: 0, max: 1, step: 0.01 },
                ],
            },
            {
                title: 'Retro', when: g => g.mode === 'retro', items: [
                    { f: 'g', key: 'pixel', label: 'Pixel size', def: 3, type: 'range', min: 1, max: 8, step: 1 },
                    { f: 'g', key: 'color_bits', label: 'Colour depth (bits)', def: 5, type: 'range', min: 2, max: 8, step: 1 },
                    { f: 'g', key: 'dither', label: 'Ordered dithering', def: true },
                    { f: 'g', key: 'retro_tint', label: 'Retro grading', def: 0.4, type: 'range', min: 0, max: 1, step: 0.01 },
                    { f: 'g', key: 'vertex_snap', label: 'Vertex snapping', def: true },
                ],
            },
        ],
    },
    {
        id: 'effects', title: 'Effects', icon: 'spark', vulkan: true, groups: [
            {
                title: 'Lighting', items: [
                    { f: 'g', key: 'quality_ao', label: 'Ambient occlusion', def: 0, options: pick([0, 'Off'], [1, 'Low'], [2, 'High']) },
                    { f: 'g', key: 'quality_bloom', label: 'Bloom', def: false },
                    { f: 'g', key: 'atmosphere', label: 'Fog', def: true },
                    { f: 'g', key: 'fog_strength', label: 'Fog strength', def: 0.5, type: 'range', min: 0, max: 1, step: 0.01 },
                    { f: 'g', key: 'shaft_strength', label: 'Light shafts', def: 0, type: 'range', min: 0, max: 3, step: 0.05 },
                    { f: 'g', key: 'bounce_light', label: 'Bounced light', def: true },
                    { f: 'g', key: 'bounce_strength', label: 'Bounce strength', def: 1, type: 'range', min: 0, max: 3, step: 0.05 },
                ],
            },
            {
                title: 'Reflections', items: [
                    { f: 'g', key: 'reflections', label: 'Reflections', def: 'off', options: pick(['off', 'Off'], ['low', 'Low'], ['high', 'High']) },
                    { f: 'g', key: 'reflection_strength', label: 'Reflection strength', def: 0.5, type: 'range', min: 0, max: 1, step: 0.01 },
                    { f: 'g', key: 'reflections_auto', label: 'Find glass by itself', def: true, help: 'Off: only glass and the surfaces mods/reflections.toml lists reflect.' },
                    { f: 'g', key: 'water', label: 'Water', def: true, help: 'Ripples, reflections, depth tint and shore foam on water surfaces (Vulkan).' },
                ],
            },
            {
                title: 'Particles', items: [
                    { f: 'g', key: 'soft_particles', label: 'Soft particles', def: false },
                    { f: 'g', key: 'particle_fade', label: 'Fade depth', def: 0.03, type: 'range', min: 0.005, max: 0.2, step: 0.005 },
                    { f: 'g', key: 'effect_lights', label: 'Effect lights', def: false, help: 'Flashes, fire and lamps light what is near them.' },
                    { f: 'g', key: 'light_strength', label: 'Light strength', def: 1, type: 'range', min: 0, max: 3, step: 0.05 },
                    { f: 'g', key: 'particles', label: 'GPU particles', def: true, help: 'Sparks, debris, smoke and fireballs from shots and explosions (Vulkan).' },
                    { f: 'g', key: 'taa', label: 'TAA (temporal anti-aliasing)', def: false, help: 'Smooth, steady edges over frames; SMAA is left out with it (Vulkan).' },
                    { f: 'g', key: 'upscaler', label: 'Upscaler', def: 'off', options: pick(['off', 'Off'], ['fsr', 'AMD FSR 2'], ['dlss', 'NVIDIA DLSS (RTX)']), alias: { xess: 'off' }, help: 'The 3D scene drawn smaller and rebuilt over frames, the HUD sharp; TAA and SMAA are left out with it (Vulkan). DLSS needs an NVIDIA RTX graphics card: elsewhere the game uses FSR 2.' },
                    { f: 'g', key: 'upscale_quality', label: 'Upscale quality', def: 'native', options: pick(['native', 'Native AA / DLAA (1x)'], ['quality', 'Quality (1.5x)'], ['balanced', 'Balanced (1.7x)'], ['performance', 'Performance (2x)'], ['ultra_performance', 'Ultra Performance (3x)']) },
                    { f: 'g', key: 'upscale_sharpness', label: 'Upscale sharpness', def: 0.3, type: 'range', min: 0, max: 1, step: 0.05, help: 'FSR 2 only (DLSS has no sharpening of its own).' },
                    { f: 'g', key: 'movie_upscale', label: 'Upscale movies', def: true },
                    { f: 'g', key: 'movie_43', label: 'Movies in 4:3', def: true },
                ],
            },
            {
                title: 'Materials', items: [
                    { f: 'g', key: 'materials', label: 'Normal and roughness maps (PBR)', def: false, restart: true, help: 'Needs an HD texture pack made with PBR maps (Textures).' },
                    { f: 'g', key: 'material_strength', label: 'Material strength', def: 0.7, type: 'range', min: 0, max: 1.5, step: 0.05 },
                    { f: 'g', key: 'surface_depth', label: 'Relief depth', def: 0, type: 'range', min: 0, max: 2, step: 0.05, help: 'Parallax: stones and mortar look modelled. 0 is off. Needs the PBR maps on.' },
                    { f: 'g', key: 'displacement', label: 'Ground geometry', def: false, help: 'Snow, grass, dirt and rock underfoot cut finer into real lumps and hollows, shaded (Vulkan).' },
                    { f: 'g', key: 'wall_displacement', label: 'Wall geometry', def: false, help: 'Rock and concrete walls and cliffs made a little uneven; corners and edges kept (Vulkan).' },
                    { f: 'g', key: 'displace_depth', label: 'Geometry depth', def: 1, type: 'range', min: 0, max: 3, step: 0.05 },
                    { f: 'g', key: 'ground_detail', label: 'Ground detail', def: true },
                    { f: 'g', key: 'detail_strength', label: 'Detail strength', def: 0.6, type: 'range', min: 0, max: 1, step: 0.01 },
                    { f: 'g', key: 'detail_distance', label: 'Detail distance', def: 14, type: 'range', min: 2, max: 60, step: 1, unit: ' m' },
                ],
            },
            {
                title: 'Ray tracing', items: [
                    { f: 'g', key: 'ray_tracing', label: 'Ray-traced ambient occlusion', def: false, help: 'Needs a GPU with ray queries (RTX, RX 6000 and later, Arc).' },
                    { f: 'g', key: 'rt_ao', label: 'Occlusion strength', def: 1, type: 'range', min: 0, max: 2, step: 0.05 },
                    { f: 'g', key: 'rt_radius', label: 'Ray reach', def: 1.5, type: 'range', min: 0.25, max: 6, step: 0.05, unit: ' m' },
                    { f: 'g', key: 'rt_rays', label: 'Rays per pixel', def: 4, type: 'range', min: 1, max: 16, step: 1 },
                    { f: 'g', key: 'rt_shadows', label: 'Traced shadows from effect lights', def: true },
                    { f: 'g', key: 'rt_denoise', label: 'Ray tracing denoise', def: true, help: 'The traced bounced and volumetric light built up over frames: less grain.' },
                ],
            },
        ],
    },
    {
        id: 'game', title: 'Game', icon: 'target', groups: [
            {
                title: 'Game', items: [
                    { f: 's', key: 'skip_intro', label: 'Skip intro movies', def: false },
                    { f: 's', key: 'skip_paris', label: 'Start in the menus, not Paris', def: true },
                    { f: 's', key: 'hide_hud', label: 'Hide the HUD', def: false },
                    { f: 's', key: 'view_motion', label: 'Weapon sway and head bob', def: true },
                    { f: 's', key: 'aim_down_sights', label: 'Aim down sights', def: true, help: 'Right mouse button or L.' },
                    { f: 's', key: 'ads_zoom', label: 'Aim zoom', def: 130, type: 'range', min: 100, max: 200, step: 5, unit: '%', help: 'How far aiming down the sights zooms in. Scopes keep their own.' },
                    { f: 's', key: 'fov_percent', label: 'Field of view', def: 100, type: 'range', min: 50, max: 150, step: 5, unit: '%' },
                    { f: 's', key: 'modern_buttons', label: 'Modern button pictures', def: true },
                    { f: 's', key: 'rounded_font', label: 'Rounded font', def: false, restart: true },
                ],
            },
            {
                title: 'Discord', items: [
                    { f: 's', key: 'discord', label: 'Discord status', def: true, help: 'Shows the mission you play on your Discord profile while Discord runs.' },
                    { f: 's', key: 'discord_app_id', label: 'Discord application ID', def: '', type: 'text', help: "Leave empty for the game's own Discord application." },
                ],
            },
        ],
    },
    {
        id: 'controls', title: 'Controls', icon: 'pad', groups: [
            {
                title: 'Gameplay', items: [
                    { f: 's', key: 'tight_movement', label: 'Tighter movement', def: false,
                        help: 'Quicker starts and direction changes, less floaty jumps with original jump height. Also applies in single-player. In multiplayer the host decides for everyone.' },
                    { f: 'g', key: 'blood', label: 'Blood on hit', def: true,
                        help: 'Blood at character impacts, plus red view edges when hurt. Works with every renderer.' },
                    { f: 's', key: 'pickup_physics', label: 'Pickup physics', def: true,
                        help: 'Dropped weapons and ammo are tossed out, bounce and tumble before they lie flat; you can push them and explosions throw them. Off in network play.' },
                ],
            },
            {
                title: 'Joypad', items: [
                    { f: 's', key: 'controller_scheme', label: 'Original control scheme', def: 0,
                        options: pick([0, 'NightFire (Default)'], [1, 'Moonraker'], [2, 'Octopussy'], [3, 'Goldfinger'],
                            [4, 'Dr. No'], [5, 'Thunderball'], [6, 'GoldenEye'], [7, 'Classic Bond']),
                        help: 'The original on-foot layouts for controller 1. Keyboard and mouse retain their own controls.' },
                ],
            },
            {
                title: 'Keyboard and mouse', items: [
                    { f: 's', key: 'keyboard', label: 'Keyboard plays as controller 1', def: true },
                    { f: 's', key: 'keyboard_alone', label: 'Keyboard alone (pads start at 2)', def: false },
                    { f: 's', key: 'mouse_look', label: 'Mouse look', def: true },
                    { f: 's', key: 'mouse_sensitivity', label: 'Mouse sensitivity', def: 100, type: 'range', min: 10, max: 1000, step: 10, unit: '%' },
                    { f: 's', key: 'mouse_invert_y', label: 'Invert mouse Y', def: false },
                ],
            },
            {
                title: 'Hotkeys', items: [
                    { f: 's', key: 'screenshot_key', label: 'Screenshot', def: 123, options: KEYS, help: 'A PNG in the Screenshots folder beside the game.' },
                    { f: 's', key: 'photo_key', label: 'Photo mode', def: 121, options: KEYS },
                    { f: 's', key: 'restart_key', label: 'Restart mission', def: 0, options: KEYS, help: 'Restarts the mission at once, no question.' },
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
        note: 'Keys and mouse buttons: in the game, Settings > More Settings > Key Bindings.',
    },
];

// the quality presets (kQuality in style.cpp): high and ultra take SMAA under Vulkan, FXAA under Direct3D
window.QUALITY = {
    low: { aniso: 0, aa: 'none', ao: 0, bloom: false },
    medium: { aniso: 4, aa: 'fxaa', ao: 0, bloom: true },
    high: { aniso: 8, aa: 'smaa', ao: 1, bloom: true },
    ultra: { aniso: 16, aa: 'smaa', ao: 2, bloom: true },
};
