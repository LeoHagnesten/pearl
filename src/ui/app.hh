#pragma once

#include "render/render.hh"
#include "library/library.hh"
#include "spotify/spotify.hh"
#include "lyrics/lyrics.hh"
#include "ui/theme.hh"

namespace ui {

	enum class page : int32_t {

		SONGS,
		ALBUMS,
		ALBUM,
		LIKED,
		PLAYLIST,
		SEARCH,
		NOW,
		ARTIST,
		DETAIL,
		PLAYLISTS,
		HOME,
		MIX,

	};

	enum class recent_kind : int32_t {

		SONG,
		ALBUM,
		PLAYLIST,
		LIKED,
		REMOTE_ALBUM,
		REMOTE_ARTIST,

	};

	enum class origin : int32_t {

		NONE,
		SONGS,
		ALBUM,
		LIKED,
		PLAYLIST,
		SEARCH,
		MIX,

	};

	enum class repeat_mode : int32_t {

		OFF,
		ALL,
		ONE,

	};

	enum class request : int32_t {

		NONE,
		MINIMIZE,
		MAXIMIZE,
		CLOSE,
		RESTORE,

	};

	enum class hit : int32_t {

		NONE,
		CLIENT,
		CAPTION,
		MAXIMIZE,

	};

	enum class menu_kind : int32_t {

		NONE,
		TRACK,
		ADD,
		PLAYLIST,

	};

	struct widget {

		uint64_t id = 0;
		float hover = 0.0f;
		float press = 0.0f;
		float pop = 0.0f;
		float pop_velocity = 0.0f;
		float value = 0.0f;
		float velocity = 0.0f;
		float wave = 0.0f;
		float cross = 0.0f;
		float glide = 0.0f;
		uint32_t seen = 0;

	};

	struct art_slot {

		uint64_t key = 0;
		uint32_t used = 0;
		float appear = 0.0f;
		bool found = false;

	};

	struct hit_rect {

		uint64_t id;
		math::rect area;
		hit kind;

	};

	struct location {

		page where = page::HOME;
		int32_t album = 0;
		int32_t playlist = 0;

	};

	struct context {

		origin kind = origin::NONE;
		int32_t index = 0;

	};

	struct playlist {

		base::text title;
		base::list < uint64_t > keys;
		uint64_t id = 0;

	};

	struct ghost {

		wchar_t value = 0;
		float x = 0.0f;
		double died = 0.0;

	};

	struct field {

		wchar_t text [ 128 ] = { };
		double born [ 128 ] = { };
		float place [ 128 ] = { };
		ghost ghosts [ 24 ];
		int32_t ghost_count = 0;
		int32_t length = 0;
		int32_t caret = 0;
		int32_t anchor = 0;
		float offset = 0.0f;
		float caret_x = -1.0f;
		float hint = 1.0f;
		uint32_t revision = 0;
		double typed = 0.0;

	};

	struct recent_item {

		recent_kind kind = recent_kind::SONG;
		uint64_t key = 0;
		base::text uri;
		base::text title;
		base::text subtitle;
		base::text art;
		int32_t slot = -1;

	};

	struct tone {

		uint64_t key = 0;
		render::color palette [ 4 ];
		render::color accent;

	};

	struct list_memory {

		uint64_t id = 0;
		uint32_t frame = 0;
		base::list < uint64_t > keys;
		base::list < int32_t > tracks;

	};

	struct row_motion {

		uint64_t list = 0;
		uint64_t key = 0;
		float offset = 0.0f;
		float velocity = 0.0f;
		float appear = 1.0f;

	};

	struct row_ghost {

		uint64_t list = 0;
		int32_t track = -1;
		float y = 0.0f;
		double died = 0.0;

	};

	struct row_style {

		bool covers = true;
		bool albums = true;
		bool numbers = false;
		bool upcoming = false;
		int32_t playlist = -1;
		bool labeled = false;
		int32_t limit = 0;

	};

	uint64_t make_id ( const char* name, int64_t index = 0 );

	bool same_context ( context a, context b );

	bool read_clipboard ( wchar_t* out, int32_t room );

	void write_clipboard ( const wchar_t* text );

	void field_insert ( field& value, const wchar_t* text, double now );

	void field_erase ( field& value, int32_t from, int32_t to, double now );

	void field_set ( field& value, const wchar_t* text, double now, bool animate );

	class app {

	public:

		bool setup ( render::gpu* device, bool live_audio );

		void rebuild_gpu ( render::gpu* device );

		void shutdown ( );

		void resize ( int32_t width_px, int32_t height_px, float dpi_scale );

		void frame ( double seconds );

		void frame_mini ( double seconds, render::surface& target, int32_t width_px, int32_t height_px, float scale, float margin );

		void open_mini ( bool value );

		bool mini_settled ( ) const;

		void idle ( double seconds );

		bool busy ( ) const;

		bool steady ( ) const;

		void mouse_move ( float x, float y );

		void mouse_button ( int32_t button, bool down );

		void mouse_leave ( );

		void mouse_wheel ( float steps );

		void key ( uint32_t code, bool control, bool shift );

		void character ( wchar_t value );

		hit hit_test ( float x, float y ) const;

		request take_request ( );

		void set_maximized ( bool value );

		void set_focus ( bool value );

		void pop_in ( );

		void draw_snapshot ( render::canvas& target, float margin, float scale, float alpha, bool maximized );

		void load_state ( );

		void save_state ( );

		void toggle_play ( );

		void next_track ( bool automatic );

		void previous_track ( );

		void play_list ( const base::list < int32_t >& list, int32_t start, context from );

		void navigate ( page where, int32_t index = 0 );

		void toggle_now ( );

		void adopt_library ( );

		int32_t create_playlist ( const wchar_t* title );

		void type_search ( const wchar_t* text );

		void play_catalog ( int32_t index );

		void draw_brand ( render::canvas& c, math::vec2 center, float radius );

		render::gpu* gpu = nullptr;
		render::canvas scene_canvas;
		render::canvas chrome_canvas;
		int32_t width = 0;
		int32_t height = 0;
		float dpi = 1.0f;
		float w = 0.0f;
		float h = 0.0f;
		double clock = 0.0;
		double last = -1.0;
		float dt = 0.0f;
		bool fixed_step = false;
		bool demo_library = true;
		bool persist = true;
		bool autoplay = false;
		bool maximized = false;
		bool focused = true;
		bool live = false;
		mutable int32_t pending = 3;
		bool animating = false;
		float base_alpha = theme::base_alpha;
		bool mini_mode = false;
		bool mini_target = false;
		float mini_show = 0.0f;
		float mini_show_velocity = 0.0f;
		float mini_dpi = 1.0f;
		float mini_hover = 0.0f;
		float mini_hover_velocity = 0.0f;
		bool mini_lifted = false;
		float mini_lift = 0.0f;
		float mini_lift_velocity = 0.0f;
		float mini_align = 1.0f;
		float mini_align_target = 1.0f;
		float mini_text_room = 170.0f;
		double marquee_start = 0.0;
		bool marquee_active = false;
		int32_t mini_anchor = 0;
		math::rect mini_rect;
		render::canvas mini_canvas;
		render::canvas mini_compose;
		render::surface mini_layer;
		int32_t placement [ 4 ] = { };
		bool placement_valid = false;
		bool placement_maximized = false;

		math::vec2 mouse = math::vec2 ( -1000.0f, -1000.0f );
		bool mouse_inside = false;
		bool mouse_down = false;
		bool press_event = false;
		bool release_event = false;
		bool right_event = false;
		bool double_event = false;
		double last_press = -10.0;
		math::vec2 press_at;
		float wheel_steps = 0.0f;
		float wheel_carry = 0.0f;
		uint64_t hot = 0;
		uint64_t next_hot = 0;
		uint64_t active_id = 0;
		uint64_t clicked_id = 0;
		uint64_t double_id = 0;
		uint64_t right_id = 0;
		uint64_t pressed_id = 0;
		uint32_t press_frame = 0;
		base::list < hit_rect > hits;
		base::list < hit_rect > next_hits;
		base::list < uint32_t > keys;
		base::list < wchar_t > typed;
		base::list < math::rect > blocked;
		bool key_control = false;
		bool key_shift = false;
		request pending_request = request::NONE;

		widget* widgets = nullptr;
		static constexpr int32_t widget_room = 4096;
		uint32_t frame_index = 1;

		math::rect top;
		math::rect search_box;
		math::rect controls;
		math::rect rail;
		math::rect content;
		math::rect stage;
		math::rect list_area;
		math::rect capsule;
		math::rect cover_slot;

		location here;
		location previous_place;
		location before_search;
		float scroll = 0.0f;
		float scroll_target = 0.0f;
		float scroll_velocity = 0.0f;
		float rail_velocity = 0.0f;
		double search_kind_time = -10.0;
		float scroll_limit = 0.0f;
		float previous_scroll = 0.0f;
		float page_mix = 1.0f;
		float page_velocity = 0.0f;
		float content_height = 0.0f;
		int32_t selected_track = -1;
		float rail_scroll = 0.0f;
		float rail_scroll_target = 0.0f;
		float rail_limit = 0.0f;
		float rail_marker = 0.0f;
		float rail_marker_velocity = 0.0f;
		bool rail_marker_ready = false;
		int32_t dock = 0;
		math::rect capsule_home;
		float cap_x = 0.0f;
		float cap_y = 0.0f;
		float cap_w = 0.0f;
		float cap_vx = 0.0f;
		float cap_vy = 0.0f;
		float cap_vw = 0.0f;
		bool cap_ready = false;
		bool cap_drag = false;
		bool drag_finished = false;
		math::vec2 grab;
		math::vec2 last_mouse;
		math::vec2 mouse_velocity;
		float lift = 0.0f;
		float lift_velocity = 0.0f;
		float bottom_inset = 110.0f;
		float top_inset = 0.0f;

		int32_t track = -1;
		int32_t old_track = -1;
		int32_t switch_direction = 1;
		float switch_mix = 1.0f;
		float switch_velocity = 0.0f;
		base::list < int32_t > queue;
		base::list < int32_t > order;
		int32_t order_index = 0;
		context queue_from;
		bool shuffle = false;
		repeat_mode repeat = repeat_mode::OFF;
		bool playing = false;
		double position = 0.0;
		float volume = 0.8f;
		bool muted = false;
		bool seeking = false;
		float seek_value = 0.0f;
		bool volume_drag = false;
		float levels [ 3 ] = { };
		float level_velocity [ 3 ] = { };
		render::color ambient [ 4 ];
		render::color ambient_from [ 4 ];
		float ambient_mix = 1.0f;
		float ambient_time = 0.0f;
		float drift_time = 0.0f;
		float art_scale = 1.0f;
		float art_velocity = 0.0f;
		float spin = 0.0f;
		float spin_speed = 0.0f;

		float now = 0.0f;
		float now_velocity = 0.0f;
		location before_now;
		float window_pop = 1.0f;
		float window_pop_velocity = 0.0f;

		uint64_t tip_id = 0;
		float tip_time = 0.0f;
		float tip_show = 0.0f;
		const wchar_t* tip_text = nullptr;
		math::rect tip_anchor;
		uint64_t tip_candidate = 0;
		const wchar_t* tip_candidate_text = nullptr;
		math::rect tip_candidate_anchor;

		base::list < int32_t > liked_list;
		base::list < int32_t > all_songs;
		base::list < uint64_t > key_table;
		base::list < int32_t > key_slots;
		uint32_t songs_revision = 0;
		static constexpr int32_t art_room = 64;
		art_slot art_slots [ art_room ];
		static constexpr int32_t thumb_room = 512;
		static constexpr int32_t thumb_edge = library::thumb_size;
		art_slot thumb_slots [ thumb_room ];
		base::list < uint64_t > big_wanted;
		base::list < uint64_t > art_waiting;
		int32_t art_base = 0;
		uint64_t restore_key = 0;
		double restore_position = 0.0;

		base::list < playlist > playlists;
		base::list < recent_item > recents;
		spotify::home home_view;
		uint32_t home_seen = 0;
		double home_asked = -1.0;
		double home_time = -10.0;
		bool home_waiting = false;
		uint32_t home_expect = 0;
		bool home_login = false;
		bool widen_asked = false;
		base::list < tone > tones;
		lyrics::sheet lyric_sheet;
		uint32_t lyric_seen = 0;
		uint64_t lyric_asked = 0;
		double lyric_since = -10.0;
		base::list < float > lyric_tops;
		float lyric_layout_width = 0.0f;
		float lyric_scroll = 0.0f;
		float lyric_velocity = 0.0f;
		float lyric_nudge = 0.0f;
		double lyric_touch = -10.0;
		bool lyric_ready = false;
		location gate_place;
		uint32_t gate_stamp = 0;
		bool gate_open = false;
		double gate_start = -10.0;
		double gate_time = -10.0;
		base::list < int32_t > home_mix;
		base::list < int32_t > home_repeat;
		base::list < int32_t > home_albums;
		base::list < int32_t > home_artists;
		base::list < int32_t > home_fresh;
		uint64_t playlist_seed = 1;
		uint64_t focus_id = 0;
		field search_field;
		field title_field;
		int32_t title_list = -1;
		uint32_t search_done = 0;
		base::list < int32_t > found_songs;
		base::list < int32_t > found_albums;
		base::list < int32_t > found_lists;
		base::list < int32_t > found_remote;
		base::list < int32_t > found_artists;
		base::list < int32_t > found_remote_albums;
		base::list < int32_t > found_remote_artists;
		int32_t search_kind = 0;
		float search_kind_x = 0.0f;
		float search_kind_velocity = 0.0f;
		uint32_t remote_generation = 0;
		bool remote_more = false;
		bool remote_loading = false;
		uint32_t remote_search_sent = 0;
		float caret_time = 0.0f;
		float search_focus = 0.0f;

		menu_kind menu = menu_kind::NONE;
		menu_kind menu_shown = menu_kind::NONE;
		int32_t menu_track = -1;
		int32_t menu_row = -1;
		int32_t menu_list = -1;
		context menu_from;
		math::vec2 menu_at;
		float menu_show = 0.0f;
		float menu_velocity = 0.0f;
		math::rect menu_rect;

		int32_t drag_track = -1;
		bool track_drag = false;
		uint64_t drag_source = 0;
		int32_t drop_target = -2;
		float drag_show = 0.0f;
		math::vec2 drag_at;

		base::list < list_memory > memories;
		base::list < row_motion > motions;
		base::list < row_ghost > ghosts;
		wchar_t toast_text [ 160 ] = { };
		double toast_time = -10.0;
		uint64_t delete_armed = 0;
		double delete_time = -10.0;

		spotify::detail detail_view;
		uint32_t detail_seen = 0;
		base::list < int32_t > detail_tracks;
		base::list < int32_t > detail_albums;
		location detail_back;
		base::list < base::text > detail_stack;
		base::list < int32_t > detail_kinds;
		spotify::snapshot remote_state;
		uint64_t remote_key = 0;
		context local_from;
		bool remote_follow = false;
		uint64_t remote_message = 0;
		bool remote_started = false;
		bool remote_near_end = false;
		int32_t remote_known = 0;
		double remote_sent = -10.0;
		bool remote_was_playing = false;
		int32_t remote_volume = -1;
		double volume_touch = -10.0;
		bool spotify_open = false;
		float spotify_show = 0.0f;
		float spotify_velocity = 0.0f;
		math::vec2 spotify_anchor;
		math::rect spotify_panel;

	private:

		widget& state ( uint64_t id );

		bool over ( math::rect area ) const;

		bool interact ( uint64_t id, math::rect area, widget& item, hit kind = hit::CLIENT );

		void block ( math::rect area );

		void tip ( uint64_t id, const wchar_t* text, math::rect anchor );

		void approach ( float& value, float target, float rate );

		void spring ( float& value, float& velocity, float target, float stiffness, float damping );

		bool begin_frame ( double seconds );

		void begin_input ( );

		void end_input ( );

		void handle_keys ( );

		bool edit_keys ( field& value, uint32_t code );

		void update_playback ( );

		void open_track ( int32_t index, int32_t direction, bool play_now, double at = 0.0 );

		void build_order ( int32_t first );

		void begin_switch ( int32_t index, int32_t direction );

		bool remote_mode ( ) const;

		void update_remote ( );

		void enter_remote ( );

		void seek_to ( double seconds );

		void toggle_mute ( );

		void toggle_like ( );

		void toggle_shuffle ( );

		void cycle_repeat ( );

		bool shuffle_on ( ) const;

		repeat_mode repeat_on ( ) const;

		void paste_link ( );

		int32_t keep ( int32_t index );

		void like_index ( int32_t index, bool value );

		void send_run ( );

		void update_queued ( );

		void play_next ( int32_t index );

		void add_to_playlist ( int32_t list, int32_t index );

		void remove_from_playlist ( int32_t list, int32_t index );

		void delete_playlist ( int32_t list );

		void playlist_tracks ( int32_t list, base::list < int32_t >& result );

		void show_toast ( const wchar_t* first, const wchar_t* second = nullptr );

		void set_volume ( float value );

		void album_tracks ( int32_t album, base::list < int32_t >& result );

		void rebuild_songs ( );

		int32_t lookup_track ( uint64_t key ) const;

		void run_search ( );

		void focus_field ( uint64_t id, field& value, const wchar_t* text, bool select );

		void commit_title ( );

		void layout ( );

		int32_t art_lookup ( uint64_t key, float& appear );

		int32_t thumb_lookup ( uint64_t key, float& appear );

		void request_cover ( int32_t album );

		void receive_art ( );

		void cover ( render::canvas& c, math::rect area, float radius, int32_t album, float alpha = 1.0f, float angle = 0.0f );

		void mosaic ( render::canvas& c, math::rect area, float radius, const base::list < int32_t >& list, uint64_t seed );

		math::rect dock_rect ( int32_t index ) const;

		void update_capsule ( );

		void draw_background ( render::canvas& c );

		void draw_page ( render::canvas& c, location place, float offset, bool interactive );

		float draw_header ( render::canvas& c, location place, const base::list < int32_t >& list, context from, float x, float y, float width, bool interactive );

		float draw_rows ( render::canvas& c, uint64_t id, const base::list < int32_t >& list, context from, float x, float y, float width, row_style style, bool interactive );

		void track_list ( uint64_t id, const base::list < int32_t >& list );

		float cascade ( int32_t index );

		float spin_phase ( double rate ) const;

		float clock_wrapped ( ) const;

		void draw_ghosts ( render::canvas& c, uint64_t id, float x, float y, float width, row_style style );

		float draw_album_grid ( render::canvas& c, const base::list < int32_t >& albums, float x, float y, float width, int32_t limit_rows, bool interactive );

		float draw_artist_grid ( render::canvas& c, const base::list < int32_t >& artists, float x, float y, float width, int32_t limit_rows, bool interactive );

		void artist_tracks ( int32_t album, base::list < int32_t >& result );

		void play_context ( int32_t index );

		void remember ( recent_kind kind, uint64_t key, const wchar_t* uri, const wchar_t* title, const wchar_t* subtitle, const wchar_t* art );

		void place_recents ( );

		uint64_t fresh_id ( );

		float draw_playlist_grid ( render::canvas& c, float x, float y, float width, int32_t limit_rows, bool interactive );

		void ask_home ( bool force );

		void ingest_home ( );

		void apply_tone ( int32_t album );

		bool art_settled ( int32_t album, bool big );

		bool page_ready ( location place );

		void draw_loading ( render::canvas& c, location place );

		float draw_home ( render::canvas& c, float x, float y, float width, bool interactive );

		float draw_mix_card ( render::canvas& c, math::rect area, bool interactive );

		float section_title ( render::canvas& c, const wchar_t* title, float x, float y, float width, const wchar_t* link, uint64_t id, bool interactive, bool& followed );

		void open_detail ( int32_t index, int32_t kind );

		void close_detail ( );

		void ingest_detail ( );

		float draw_detail ( render::canvas& c, float x, float y, float width, bool interactive );

		void play_virtual ( const base::list < int32_t >& list, int32_t start, context from );

		float draw_search ( render::canvas& c, float x, float y, float width, bool interactive );

		float draw_up_next ( render::canvas& c, float x, float y, float width, bool interactive );

		void update_lyrics ( );

		float draw_lyrics ( render::canvas& c, float x, float y, float width, bool interactive );

		void draw_topbar ( render::canvas& c );

		void draw_search_box ( render::canvas& c );

		void draw_rail ( render::canvas& c );

		void draw_controls ( render::canvas& c );

		void draw_capsule ( render::canvas& c, math::rect area );

		void draw_now ( render::canvas& c );

		void draw_mini ( render::canvas& c, math::rect area );

		void draw_menu ( render::canvas& c );

		void draw_drag ( render::canvas& c );

		void draw_toast ( render::canvas& c );

		void draw_spotify ( render::canvas& c );

		void draw_tip ( render::canvas& c );

		void open_menu ( menu_kind kind, int32_t index, int32_t row, context from, math::vec2 at );

		bool menu_item ( render::canvas& c, uint64_t id, math::rect area, const wchar_t* label, theme::glyph icon, bool danger );

		bool text_field ( render::canvas& c, uint64_t id, field& value, math::rect area, const wchar_t* placeholder, render::font role, render::color tint );

		bool icon_button ( render::canvas& c, uint64_t id, math::vec2 center, float size, theme::glyph icon, float icon_size, render::color tint, const wchar_t* text, hit kind = hit::CLIENT );

		bool play_button ( render::canvas& c, uint64_t id, math::vec2 center, float radius, bool showing_pause, bool dark, const wchar_t* text );

		bool pill_button ( render::canvas& c, uint64_t id, math::rect area, const wchar_t* label, theme::glyph icon, float morph, bool dark, bool active );

		bool heart_button ( render::canvas& c, uint64_t id, math::vec2 center, float size, float icon_size, bool liked, const wchar_t* text );

		void heart_shape ( render::canvas& c, math::vec2 center, float size, float fill );

		bool speaker_button ( render::canvas& c, uint64_t id, math::vec2 center, float size, float icon_size, render::color tint, const wchar_t* text );

		void speaker_shape ( render::canvas& c, math::vec2 center, float size, float one, float two, float mute, render::color tint );

		bool skip_button ( render::canvas& c, uint64_t id, math::vec2 center, float size, bool forward, render::color tint, const wchar_t* text );

		float slider ( render::canvas& c, uint64_t id, math::rect area, float value, bool& dragging, render::color fill );

		void play_shape ( render::canvas& c, math::vec2 center, float size, float morph, render::color tint );

		void skip_shape ( render::canvas& c, math::vec2 center, float size, bool forward, render::color tint );

		void equalizer ( render::canvas& c, math::vec2 center, float size, render::color tint );

		void pearl_mark ( render::canvas& c, math::vec2 center, float radius );

		void disc ( render::canvas& c, math::rect area, int32_t album, float angle, float alpha );

		void marquee ( render::canvas& c, const wchar_t* value, render::font role, math::vec2 at, render::color tint, float limit, bool fade, render::color backdrop );

		const wchar_t* format_time ( double seconds, wchar_t* buffer );

	};

}