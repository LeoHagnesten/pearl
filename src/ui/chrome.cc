#include "ui/app.hh"

namespace ui {

	static constexpr float rail_mark_x = 44.0f;

	void app::draw_topbar ( render::canvas& c ) {

		pearl_mark ( c, math::vec2 ( rail_mark_x, 32.0f ), 10.0f );
		c.text ( L"Pearl", render::font::BRAND, math::vec2 ( rail_mark_x + 19.0f, 32.0f ), theme::ink );
		draw_search_box ( c );

	}

	void app::draw_search_box ( render::canvas& c ) {

		uint64_t field_id = make_id ( "search" );
		uint64_t box_id = make_id ( "search_box" );
		widget& box_state = state ( box_id );
		bool focus = focus_id == field_id;
		float focus_ease = search_focus * search_focus * ( 3.0f - 2.0f * search_focus );
		math::rect box = search_box;
		float radius = box.h * 0.5f;

		c.shadow ( box, radius, 12.0f + 8.0f * focus_ease, theme::shade.fade ( 0.08f + 0.06f * focus_ease ), math::vec2 ( 0.0f, 4.0f + 2.0f * focus_ease ) );
		c.glass ( box, radius, theme::control ( ) );
		c.fill ( box, radius, theme::white.fade ( 0.42f * focus_ease + 0.12f * box_state.hover ) );

		if ( focus )
			c.outline ( box, radius, render::color ( 0.0f, 0.0f, 0.0f, 0.0f ), 1.2f, theme::ink.fade ( 0.16f ) );

		if ( interact ( box_id, box, box_state ) )
			focus_field ( field_id, search_field, nullptr, false );

		c.icon ( theme::code ( theme::glyph::SEARCH ), math::vec2 ( box.x + 22.0f, box.center ( ).y ), 14.0f, render::mix ( theme::ink_soft, theme::ink, focus_ease ) );

		bool has_text = search_field.length > 0;
		float text_right = box.right ( ) - ( has_text ? 44.0f : 84.0f );

		text_field ( c, field_id, search_field, math::rect ( box.x + 42.0f, box.y + 4.0f, text_right - box.x - 42.0f, box.h - 8.0f ), L"Search songs, albums, playlists", render::font::BODY, theme::ink );

		if ( has_text ) {

			math::vec2 clear_at ( box.right ( ) - 22.0f, box.center ( ).y );

			if ( icon_button ( c, make_id ( "search_clear" ), clear_at, 26.0f, theme::glyph::CLOSE, 9.0f, theme::ink_soft, L"Clear search" ) ) {

				field_set ( search_field, L"", clock, true );
				focus_id = field_id;

			}

		} else if ( !focus ) {

			math::rect hint ( box.right ( ) - 66.0f, box.center ( ).y - 11.0f, 50.0f, 22.0f );

			c.fill ( hint, 7.0f, theme::shade.fade ( 0.05f ) );
			c.text ( L"Ctrl K", render::font::TINY, hint.center ( ), theme::ink_soft, render::align::CENTER );

		}

	}

	void app::draw_controls ( render::canvas& c ) {

		c.shadow ( controls, 20.0f, 12.0f, theme::shade.fade ( 0.08f ), math::vec2 ( 0.0f, 4.0f ) );
		c.glass ( controls, 20.0f, theme::control ( ) );
		block ( controls );

		float cy = controls.center ( ).y;
		float step = 40.0f;
		math::vec2 minimize_at ( controls.x + 26.0f, cy );
		math::vec2 maximize_at ( controls.x + 26.0f + step, cy );
		math::vec2 close_at ( controls.x + 26.0f + step * 2.0f, cy );

		struct window_button {

			math::vec2 at;
			theme::glyph icon;
			const char* name;
			math::rect area;
			hit kind;

		};

		window_button buttons [ 3 ] = {
			{ minimize_at, theme::glyph::MINIMIZE, "minimize", math::rect ( minimize_at.x - 20.0f, 0.0f, 40.0f, controls.bottom ( ) + 4.0f ), hit::CLIENT },
			{ maximize_at, maximized ? theme::glyph::RESTORE : theme::glyph::MAXIMIZE, "maximize", math::rect ( maximize_at.x - 20.0f, 0.0f, 40.0f, controls.bottom ( ) + 4.0f ), hit::MAXIMIZE },
			{ close_at, theme::glyph::CLOSE, "close", math::rect ( close_at.x - 20.0f, 0.0f, w - ( close_at.x - 20.0f ), controls.bottom ( ) + 4.0f ), hit::CLIENT }
		};

		for ( int32_t i = 0; i < 3; i++ ) {

			const window_button& button = buttons [ i ];
			uint64_t id = make_id ( button.name );
			widget& item = state ( id );
			bool clicked = interact ( id, button.area, item, button.kind );
			bool danger = i == 2;
			float radius = 14.0f * ( 0.85f + 0.15f * item.hover );

			if ( danger )
				c.circle ( button.at, radius, theme::danger.fade ( item.hover * ( 0.92f + 0.08f * item.press ) ) );
			else
				c.circle ( button.at, radius, theme::shade.fade ( 0.06f * item.hover + 0.05f * item.press ) );

			render::color tint = danger ? render::mix ( theme::ink, theme::white, item.hover ) : theme::ink;

			c.push ( button.at, 1.0f - 0.08f * item.press, 1.0f );
			c.icon ( theme::code ( button.icon ), button.at, 10.0f, tint.fade ( focused ? 1.0f : 0.55f ) );
			c.pop ( );

			if ( clicked ) {

				if ( i == 0 )
					pending_request = request::MINIMIZE;
				else if ( i == 1 )
					pending_request = request::MAXIMIZE;
				else
					pending_request = request::CLOSE;

			}

		}

	}

	void app::draw_capsule ( render::canvas& c, math::rect area ) {

		float hide = math::saturate ( now * 1.7f );

		if ( hide >= 0.999f || track < 0 )
			return;

		const library::track& current = library::track_at ( track );
		float wide_w = dock_rect ( 0 ).w;
		float compact = math::saturate ( ( wide_w - area.w ) / base::max ( wide_w - 336.0f, 1.0f ) );
		float wide = 1.0f - compact;
		uint64_t body = make_id ( "capsule" );
		widget& body_state = state ( body );
		wchar_t left_time [ 32 ];
		bool drag_click = cap_drag || drag_finished;
		float raise = lift;

		c.push ( area.center ( ), ( 1.0f - 0.06f * hide ) * ( 1.0f + 0.03f * raise ), 1.0f - hide, math::vec2 ( 0.0f, 16.0f * hide ) );

		c.shadow ( area, 36.0f, 24.0f + 18.0f * raise, theme::shade.fade ( 0.13f + 0.08f * raise ), math::vec2 ( 0.0f, 10.0f + 12.0f * raise ) );
		c.shadow ( area, 36.0f, 3.0f, theme::shade.fade ( 0.05f ), math::vec2 ( 0.0f, 1.0f ) );
		c.glass ( area, 36.0f, theme::panel ( ) );
		interact ( body, area, body_state );


		math::rect art ( area.x + 10.0f, area.y + 10.0f, 52.0f, 52.0f );
		math::vec2 art_center = art.center ( );
		float progress = current.duration > 0.0f ? math::saturate ( static_cast < float > ( ( seeking ? seek_value * current.duration : position ) / current.duration ) ) : 0.0f;
		uint64_t art_id = make_id ( "capsule_art" );
		widget& art_state = state ( art_id );

		if ( interact ( art_id, art, art_state ) && !drag_click )
			toggle_now ( );

		tip ( art_id, L"Now Playing", art );
		c.ring ( art_center, 31.0f, 2.2f, 0.0f, 1.0f, theme::shade.fade ( 0.07f ) );
		c.ring ( art_center, 31.0f, 2.2f, 0.0f, progress, theme::ink.fade ( 0.78f ) );

		float mix = math::saturate ( switch_mix );
		float travel = 16.0f * static_cast < float > ( switch_direction );
		float right = area.right ( );
		float text_x = art.right ( ) + 16.0f;
		float text_end = right - ( 132.0f * compact + 372.0f * wide );
		float limit = base::max ( text_end - text_x, 60.0f );
		math::rect text_area ( text_x, area.y + 8.0f, limit, 56.0f );
		uint64_t text_id = make_id ( "capsule_text" );
		widget& text_state = state ( text_id );

		if ( interact ( text_id, text_area, text_state ) && !drag_click )
			toggle_now ( );

		bool on_text = mouse_inside && math::rect ( text_x - 6.0f, area.y, limit + 12.0f, area.h ).contains ( mouse ) && !cap_drag;

		approach ( text_state.value, on_text || seeking ? 1.0f : 0.0f, on_text ? 16.0f : 9.0f );

		float scrub = text_state.value;

		if ( cap_drag )
			scrub = 0.0f;

		for ( int32_t layer = 0; layer < 2; layer++ ) {

			bool incoming = layer == 1;
			int32_t index = incoming ? track : old_track;

			if ( index < 0 || ( !incoming && mix >= 0.999f ) )
				continue;

			const library::track& shown = library::track_at ( index );
			const library::album& shown_album = library::album_at ( shown.album );
			float alpha = incoming ? mix : 1.0f - mix;
			float zoom = incoming ? 1.15f - 0.15f * switch_mix : 1.0f - 0.25f * mix;

			c.push ( art_center, zoom * ( 1.0f + 0.05f * art_state.hover ), alpha * ( 1.0f - math::saturate ( now * 8.0f ) ) );
			disc ( c, art, shown.album, spin, 1.0f );
			c.pop ( );

			float slide = incoming ? travel * ( 1.0f - switch_mix ) : -travel * mix;

			c.push_clip ( math::rect ( text_x - 4.0f, area.y, limit + 8.0f, area.h ) );
			c.push ( math::vec2 ( text_x, area.center ( ).y ), 1.0f, alpha, math::vec2 ( slide, 0.0f ) );
			marquee ( c, shown.title, render::font::BODY_STRONG, math::vec2 ( text_x, area.y + 27.0f ), theme::ink, limit, false, theme::white );
			c.push_alpha ( 1.0f - scrub );
			marquee ( c, shown_album.artist, render::font::SMALL, math::vec2 ( text_x, area.y + 46.0f ), theme::ink_soft, limit, false, theme::white );
			c.pop ( );
			c.pop ( );
			c.pop_clip ( );

		}

		if ( scrub > 0.01f ) {

			math::rect bar_area ( text_x, area.y + 36.0f, limit - 44.0f, 20.0f );
			float value = seeking ? seek_value : progress;
			bool was_seeking = seeking;

			c.push ( math::vec2 ( text_x, bar_area.center ( ).y ), 1.0f, scrub, math::vec2 ( 0.0f, 4.0f * ( 1.0f - scrub ) ) );

			float result = slider ( c, make_id ( "capsule_progress" ), bar_area, value, seeking, theme::ink.fade ( 0.82f ) );

			if ( seeking )
				seek_value = result;

			if ( was_seeking && !seeking ) {

				seek_to ( static_cast < double > ( seek_value * current.duration ) );

			}

			double remaining = static_cast < double > ( current.duration ) - ( seeking ? static_cast < double > ( seek_value * current.duration ) : position );

			left_time [ 0 ] = L'\x2212';
			format_time ( remaining, left_time + 1 );
			c.text ( left_time, render::font::DIGITS, math::vec2 ( text_x + limit, bar_area.center ( ).y ), theme::ink_soft, render::align::RIGHT );
			c.pop ( );

		}

		float cy = area.center ( ).y;
		math::vec2 next_at ( right - ( 78.0f * wide + 34.0f * compact ), cy );
		math::vec2 play_at ( right - ( 126.0f * wide + 82.0f * compact ), cy );

		if ( skip_button ( c, make_id ( "next" ), next_at, 40.0f, true, theme::ink, L"Next" ) )
			next_track ( false );

		if ( play_button ( c, make_id ( "capsule_play" ), play_at, 21.0f, playing, true, playing ? L"Pause" : L"Play" ) )
			toggle_play ( );

		if ( wide > 0.02f ) {

			c.push ( math::vec2 ( right - 200.0f, cy ), 0.85f + 0.15f * wide, wide );

			if ( skip_button ( c, make_id ( "previous" ), math::vec2 ( right - 174.0f, cy ), 40.0f, false, theme::ink, L"Previous" ) )
				previous_track ( );

			if ( icon_button ( c, make_id ( "expand" ), math::vec2 ( right - 32.0f, cy ), 34.0f, theme::glyph::EXPAND, 13.0f, theme::ink_soft, L"Now Playing" ) )
				toggle_now ( );

			math::rect volume_area ( right - 306.0f, cy - 8.0f, 86.0f, 16.0f );
			math::vec2 speaker_at ( volume_area.x - 18.0f, cy );
			float shown_volume = muted ? 0.0f : volume;

			if ( speaker_button ( c, make_id ( "mute" ), speaker_at, 30.0f, 15.0f, theme::ink_soft, muted ? L"Unmute" : L"Mute" ) )
				toggle_mute ( );

			float new_volume = slider ( c, make_id ( "volume" ), volume_area, shown_volume, volume_drag, theme::ink.fade ( 0.72f ) );

			if ( volume_drag )
				set_volume ( new_volume );

			if ( heart_button ( c, make_id ( "capsule_heart" ), math::vec2 ( right - 352.0f, cy ), 34.0f, 15.0f, current.liked, current.liked ? L"Remove from Liked Songs" : L"Add to Liked Songs" ) )
				toggle_like ( );

			c.pop ( );

		}

		c.pop ( );

	}

	void app::draw_mini ( render::canvas& c, math::rect area ) {

		if ( track < 0 )
			return;

		const library::track& current = library::track_at ( track );
		float radius = area.h * 0.5f;
		float raise = mini_lift;
		wchar_t left_time [ 32 ];

		c.shadow ( area, radius, 14.0f + 8.0f * raise, theme::shade.fade ( 0.26f + 0.06f * raise ), math::vec2 ( 0.0f, 6.0f + 6.0f * raise ) );
		c.shadow ( area, radius, 2.5f, theme::shade.fade ( 0.14f ), math::vec2 ( 0.0f, 1.0f ) );
		c.fill ( area, radius, render::rgb ( 0xF6F6F9, 0.97f ) );
		c.gradient ( area, radius, math::vec2 ( 0.0f, area.y ), math::vec2 ( 0.0f, area.bottom ( ) ), theme::white.fade ( 0.7f ), theme::white.fade ( 0.0f ) );
		c.outline ( area, radius, render::color ( 0.0f, 0.0f, 0.0f, 0.0f ), 1.2f, theme::white );

		math::rect art ( area.x + 9.0f, area.y + 9.0f, area.h - 18.0f, area.h - 18.0f );
		math::vec2 art_center = art.center ( );
		float progress = current.duration > 0.0f ? math::saturate ( static_cast < float > ( ( seeking ? seek_value * current.duration : position ) / current.duration ) ) : 0.0f;
		float ring = art.w * 0.5f + 4.5f;

		c.ring ( art_center, ring, 2.0f, 0.0f, 1.0f, theme::shade.fade ( 0.07f ) );
		c.ring ( art_center, ring, 2.0f, 0.0f, progress, theme::ink.fade ( 0.78f ) );

		float right = area.right ( );
		float cy = area.center ( ).y;
		float text_x = art.right ( ) + 13.0f;
		float limit = base::max ( right - 230.0f - text_x, 60.0f );
		uint64_t text_id = make_id ( "mini_text" );
		widget& text_state = state ( text_id );
		bool on_pill = mouse_inside && area.contains ( mouse );
		float mix = math::saturate ( switch_mix );
		float travel = 14.0f * static_cast < float > ( switch_direction );

		text_state.seen = frame_index;
		approach ( text_state.value, on_pill || seeking ? 1.0f : 0.0f, on_pill ? 16.0f : 9.0f );

		float scrub = text_state.value;

		for ( int32_t layer = 0; layer < 2; layer++ ) {

			bool incoming = layer == 1;
			int32_t index = incoming ? track : old_track;

			if ( index < 0 || ( !incoming && mix >= 0.999f ) )
				continue;

			const library::track& shown = library::track_at ( index );
			const library::album& shown_album = library::album_at ( shown.album );
			float alpha = incoming ? mix : 1.0f - mix;
			float zoom = incoming ? 1.15f - 0.15f * switch_mix : 1.0f - 0.25f * mix;
			float slide = incoming ? travel * ( 1.0f - switch_mix ) : -travel * mix;

			c.push ( art_center, zoom, alpha );
			disc ( c, art, shown.album, spin, 1.0f );
			c.pop ( );
			c.push_clip ( math::rect ( text_x - 4.0f, area.y, limit + 8.0f, area.h ) );
			c.push ( math::vec2 ( text_x, cy ), 1.0f, alpha, math::vec2 ( slide, 0.0f ) );
			marquee ( c, shown.title, render::font::BODY_STRONG, math::vec2 ( text_x, area.y + 22.0f ), theme::ink, limit, true, render::rgb ( 0xF9F9FB ) );
			c.push_alpha ( 1.0f - scrub );
			marquee ( c, shown_album.artist, render::font::SMALL, math::vec2 ( text_x, area.y + 39.0f ), theme::ink_soft, limit, true, render::rgb ( 0xF8F8FA ) );
			c.pop ( );
			c.pop ( );
			c.pop_clip ( );

		}

		if ( scrub > 0.01f ) {

			math::rect bar_area ( text_x, area.y + 29.0f, limit - 44.0f, 20.0f );
			float value = seeking ? seek_value : progress;
			bool was_seeking = seeking;

			c.push ( math::vec2 ( text_x, bar_area.center ( ).y ), 1.0f, scrub, math::vec2 ( 0.0f, 3.0f * ( 1.0f - scrub ) ) );

			float result = slider ( c, make_id ( "mini_progress" ), bar_area, value, seeking, theme::ink.fade ( 0.82f ) );

			if ( seeking )
				seek_value = result;

			if ( was_seeking && !seeking ) {

				seek_to ( static_cast < double > ( seek_value * current.duration ) );

			}

			double remaining = static_cast < double > ( current.duration ) - ( seeking ? static_cast < double > ( seek_value * current.duration ) : position );

			left_time [ 0 ] = L'\x2212';
			format_time ( remaining, left_time + 1 );
			c.text ( left_time, render::font::DIGITS, math::vec2 ( text_x + limit, bar_area.center ( ).y ), theme::ink_soft, render::align::RIGHT );
			c.pop ( );

		}

		math::rect volume_area ( right - 190.0f, cy - 7.0f, 60.0f, 14.0f );
		float shown_volume = muted ? 0.0f : volume;

		if ( speaker_button ( c, make_id ( "mini_mute" ), math::vec2 ( volume_area.x - 15.0f, cy ), 26.0f, 14.0f, theme::ink_soft, nullptr ) )
			toggle_mute ( );

		float new_volume = slider ( c, make_id ( "mini_volume" ), volume_area, shown_volume, volume_drag, theme::ink.fade ( 0.72f ) );

		if ( volume_drag )
			set_volume ( new_volume );

		if ( skip_button ( c, make_id ( "mini_previous" ), math::vec2 ( right - 104.0f, cy ), 34.0f, false, theme::ink, nullptr ) )
			previous_track ( );

		if ( play_button ( c, make_id ( "mini_play" ), math::vec2 ( right - 66.0f, cy ), 17.0f, playing, true, nullptr ) )
			toggle_play ( );

		if ( skip_button ( c, make_id ( "mini_next" ), math::vec2 ( right - 28.0f, cy ), 34.0f, true, theme::ink, nullptr ) )
			next_track ( false );

	}

}