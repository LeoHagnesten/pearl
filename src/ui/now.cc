#include "ui/app.hh"

namespace ui {

	static math::rect lerp_rect ( math::rect a, math::rect b, float t ) {

		return math::rect ( a.x + ( b.x - a.x ) * t, a.y + ( b.y - a.y ) * t, a.w + ( b.w - a.w ) * t, a.h + ( b.h - a.h ) * t );

	}

	void app::update_lyrics ( ) {

		if ( track >= 0 ) {

			const library::track& item = library::track_at ( track );

			if ( item.key && item.key != lyric_asked ) {

				if ( lyric_since < 0.0 )
					lyric_since = clock;

				if ( item.duration > 0.0f || clock - lyric_since > 1.5 ) {

					const library::album& group = library::album_at ( item.album );
					wchar_t artist [ 160 ];
					int32_t length = 0;

					while ( group.artist [ length ] && length < 159 && !( group.artist [ length ] == L',' && group.artist [ length + 1 ] == L' ' ) ) {

						artist [ length ] = group.artist [ length ];
						length++;

					}

					artist [ length ] = 0;
					lyrics::request ( item.key, item.title, artist, group.title, static_cast < double > ( item.duration ) );
					lyric_asked = item.key;
					lyric_since = -10.0;
					lyric_nudge = 0.0f;
					lyric_touch = -10.0;

				}

			}

		}

		uint32_t revision = lyrics::revision ( );

		if ( revision != lyric_seen ) {

			lyric_seen = revision;
			lyrics::read ( lyric_sheet );
			lyric_tops.clear ( );
			lyric_layout_width = 0.0f;
			lyric_ready = false;
			pending = base::max ( pending, 3 );

		}

	}

	float app::draw_lyrics ( render::canvas& c, float x, float y, float width, bool interactive ) {

		float top = y;
		float bottom = h - 40.0f;
		float column = base::clamp ( width - 24.0f, 200.0f, 640.0f );
		render::font role = column >= 460.0f ? render::font::TITLE : render::font::LARGE;
		bool ready = track >= 0 && lyric_sheet.key == library::track_at ( track ).key && !lyric_sheet.loading && lyric_sheet.revision > 0;

		if ( !ready ) {

			float pulse = 0.5f + 0.5f * math::sin ( clock_wrapped ( ) * 3.2f );
			float widths [ 6 ] = { 0.72f, 0.5f, 0.84f, 0.38f, 0.66f, 0.56f };

			for ( int32_t k = 0; k < 6; k++ )
				c.fill ( math::rect ( x, top + 26.0f + static_cast < float > ( k ) * 54.0f, column * widths [ k ], 24.0f ), 10.0f, theme::shade.fade ( 0.035f + 0.025f * pulse ) );

			animating = true;

			return top + 360.0f;

		}

		if ( lyric_sheet.instrumental || !lyric_sheet.found || lyric_sheet.lines.empty ( ) ) {

			bool instrumental = lyric_sheet.instrumental;

			equalizer ( c, math::vec2 ( x + 16.0f, top + 48.0f ), 22.0f, theme::ink.fade ( playing ? 0.75f : 0.35f ) );
			c.text ( instrumental ? L"Instrumental" : L"No lyrics for this one", render::font::LARGE, math::vec2 ( x, top + 96.0f ), theme::ink.fade ( 0.78f ) );
			c.text ( instrumental ? L"Nothing to sing along to. Just listen." : L"Just enjoy the music.", render::font::BODY, math::vec2 ( x + 1.0f, top + 128.0f ), theme::ink_soft );

			return top + 160.0f;

		}

		if ( lyric_layout_width != column || lyric_tops.count ( ) != lyric_sheet.lines.count ( ) ) {

			float at = 0.0f;

			lyric_tops.clear ( );

			for ( const lyrics::line& item : lyric_sheet.lines ) {

				lyric_tops.push ( at );
				at += ( item.words.empty ( ) ? 40.0f : gpu->layout ( item.words.get ( ), role, column, true )->height ) + ( lyric_sheet.synced ? 22.0f : 10.0f );

			}

			lyric_tops.push ( at );
			lyric_layout_width = column;

		}

		bool snapped = c.snap;

		c.snap = false;

		if ( !lyric_sheet.synced ) {

			for ( int32_t i = 0; i < lyric_sheet.lines.count ( ); i++ ) {

				const lyrics::line& item = lyric_sheet.lines [ static_cast < size_t > ( i ) ];
				float line_y = top + 20.0f + lyric_tops [ static_cast < size_t > ( i ) ];

				if ( !item.words.empty ( ) )
					c.paragraph ( item.words.get ( ), role, math::vec2 ( x, line_y ), column, theme::ink.fade ( 0.82f ) );

			}

			c.snap = snapped;

			return top + 20.0f + lyric_tops.back ( ) + 40.0f;

		}

		double heard = ( seeking ? static_cast < double > ( seek_value * library::track_at ( track ).duration ) : position ) + 0.25;
		int32_t current = -1;

		for ( int32_t i = 0; i < lyric_sheet.lines.count ( ); i++ ) {

			if ( lyric_sheet.lines [ static_cast < size_t > ( i ) ].time <= heard )
				current = i;

		}

		float span = base::max ( bottom - top, 200.0f );
		float anchor = top + span * 0.3f;
		float focus = 0.0f;

		if ( current >= 0 )
			focus = ( lyric_tops [ static_cast < size_t > ( current ) ] + lyric_tops [ static_cast < size_t > ( current + 1 ) ] - 22.0f ) * 0.5f;

		if ( clock - lyric_touch > 2.5 )
			approach ( lyric_nudge, 0.0f, 4.0f );

		float opening = ( anchor - top ) * 0.5f;
		float resting = base::max ( focus, opening );

		lyric_nudge = base::clamp ( lyric_nudge, opening - resting - span * 0.15f, lyric_tops.back ( ) - resting );

		float target = resting + lyric_nudge;

		if ( !lyric_ready ) {

			lyric_scroll = target;
			lyric_velocity = 0.0f;
			lyric_ready = true;

		}

		spring ( lyric_scroll, lyric_velocity, target, 120.0f, 22.0f );
		c.push_clip ( math::rect ( x - 30.0f, top - 6.0f, width + 60.0f, bottom - top + 6.0f ) );

		for ( int32_t i = 0; i < lyric_sheet.lines.count ( ); i++ ) {

			const lyrics::line& item = lyric_sheet.lines [ static_cast < size_t > ( i ) ];
			float line_y = anchor - lyric_scroll + lyric_tops [ static_cast < size_t > ( i ) ];
			float line_h = lyric_tops [ static_cast < size_t > ( i + 1 ) ] - lyric_tops [ static_cast < size_t > ( i ) ] - 22.0f;

			if ( line_y + line_h < top - 60.0f || line_y > bottom + 60.0f )
				continue;

			float middle = line_y + line_h * 0.5f;
			float fade = math::smoothstep ( 0.0f, 1.0f, ( middle - top + 6.0f ) / 80.0f ) * math::smoothstep ( 0.0f, 1.0f, ( bottom - middle ) / ( span * 0.32f ) );
			uint64_t id = make_id ( "lyric_line", i ) ^ lyric_sheet.key;
			widget& line_state = state ( id );
			math::rect hit ( x - 14.0f, line_y - 8.0f, column + 28.0f, line_h + 16.0f );
			bool clicked = interactive && fade > 0.3f && interact ( id, hit, line_state );

			line_state.seen = frame_index;
			approach ( line_state.value, i == current ? 1.0f : 0.0f, 10.0f );

			float lit = line_state.value;
			float rest = i < current ? 0.24f : 0.34f;
			float alpha = ( rest + ( 1.0f - rest ) * lit + 0.22f * line_state.hover * ( 1.0f - lit ) ) * math::saturate ( fade );

			if ( line_state.hover > 0.01f )
				c.fill ( hit, 16.0f, theme::shade.fade ( 0.035f * line_state.hover * math::saturate ( fade ) ) );

			if ( item.words.empty ( ) ) {

				for ( int32_t k = 0; k < 3; k++ ) {

					float beat = 0.5f + 0.5f * math::sin ( ( spin_phase ( 0.8 ) - static_cast < float > ( k ) * 0.12f ) * math::tau );
					float grow = 1.0f + 0.35f * beat * lit;

					c.circle ( math::vec2 ( x + 8.0f + static_cast < float > ( k ) * 18.0f, line_y + 20.0f ), 4.5f * grow, theme::ink.fade ( alpha ) );

				}

				if ( lit > 0.01f )
					animating = true;

			} else
				c.paragraph ( item.words.get ( ), role, math::vec2 ( x, line_y ), column, theme::ink.fade ( alpha ) );

			if ( clicked && item.time >= 0.0 ) {

				seek_to ( item.time );
				lyric_nudge = 0.0f;
				lyric_touch = -10.0;

			}

		}

		c.pop_clip ( );
		c.snap = snapped;

		return top + 200.0f;

	}

	void app::draw_now ( render::canvas& c ) {

		if ( now <= 0.001f || track < 0 )
			return;

		const library::track& current = library::track_at ( track );
		float e = math::saturate ( now );
		float reveal = math::smoothstep ( 0.62f, 1.0f, now );
		math::rect from ( capsule.x + 10.0f, capsule.y + 10.0f, 52.0f, 52.0f );
		math::rect target = cover_slot;
		math::rect art = lerp_rect ( from, target, now );
		float radius = 26.0f + ( 20.0f - 26.0f ) * e;
		float paused = 1.0f + ( art_scale - 1.0f ) * e;
		wchar_t left_time [ 32 ];
		wchar_t right_time [ 32 ];

		c.push ( art.center ( ), paused, 1.0f );
		c.shadow ( art, radius, 6.0f + 30.0f * e, theme::shade.fade ( 0.16f + 0.1f * e ), math::vec2 ( 0.0f, 3.0f + 20.0f * e ) );
		c.shadow ( art, radius, 3.0f + 2.0f * e, theme::shade.fade ( 0.1f ), math::vec2 ( 0.0f, 2.0f ) );
		float unwind = spin > math::tau * 0.5f ? spin - math::tau : spin;
		float rest = 1.0f - e;

		if ( math::abs ( unwind ) < 0.35f )
			cover ( c, art, radius, current.album, 1.0f, unwind * rest * rest );
		else {

			float settle = math::smoothstep ( 0.05f, 0.55f, e );

			cover ( c, art, radius, current.album, 1.0f, 0.0f );

			if ( settle < 0.999f )
				cover ( c, art, radius, current.album, 1.0f - settle, spin );

		}
		c.pop ( );

		if ( reveal <= 0.001f )
			return;

		float x = target.x;
		float size = target.w;
		float base_y = target.bottom ( );
		float middle = target.center ( ).x;

		c.push ( math::vec2 ( middle, base_y ), 0.96f + 0.04f * reveal, reveal, math::vec2 ( 0.0f, 14.0f * ( 1.0f - reveal ) ) );

		float mix = math::saturate ( switch_mix );
		float travel = 22.0f * static_cast < float > ( switch_direction );

		c.push_clip ( math::rect ( x - 4.0f, base_y + 16.0f, size - 40.0f, 64.0f ) );

		for ( int32_t layer = 0; layer < 2; layer++ ) {

			bool incoming = layer == 1;
			int32_t index = incoming ? track : old_track;

			if ( index < 0 || ( !incoming && mix >= 0.999f ) )
				continue;

			const library::track& shown = library::track_at ( index );
			float alpha = incoming ? mix : 1.0f - mix;
			float slide = incoming ? travel * ( 1.0f - switch_mix ) : -travel * mix;

			c.push ( math::vec2 ( x, base_y ), 1.0f, alpha, math::vec2 ( slide, 0.0f ) );
			c.text ( shown.title, render::font::LARGE, math::vec2 ( x, base_y + 40.0f ), theme::ink, render::align::LEFT, size - 52.0f );
			c.text ( library::album_at ( shown.album ).artist, render::font::BODY, math::vec2 ( x, base_y + 66.0f ), theme::ink_soft, render::align::LEFT, size - 52.0f );
			c.pop ( );

		}

		c.pop_clip ( );

		math::vec2 heart_at ( target.right ( ) - 16.0f, base_y + 52.0f );

		if ( heart_button ( c, make_id ( "now_heart" ), heart_at, 38.0f, 17.0f, current.liked, current.liked ? L"Remove from Liked Songs" : L"Add to Liked Songs" ) )
			toggle_like ( );

		math::rect progress ( x, base_y + 88.0f, size, 18.0f );
		float duration = current.duration;
		float value = seeking ? seek_value : ( duration > 0.0f ? static_cast < float > ( position ) / duration : 0.0f );
		bool was_seeking = seeking;
		float result = slider ( c, make_id ( "now_progress" ), progress, math::saturate ( value ), seeking, theme::ink.fade ( 0.85f ) );

		if ( seeking )
			seek_value = result;

		if ( was_seeking && !seeking ) {

			seek_to ( static_cast < double > ( seek_value * duration ) );

		}

		double shown_time = seeking ? static_cast < double > ( seek_value * duration ) : position;

		c.text ( format_time ( shown_time, left_time ), render::font::DIGITS, math::vec2 ( x, base_y + 114.0f ), theme::ink_soft );
		c.text ( format_time ( duration, right_time ), render::font::DIGITS, math::vec2 ( target.right ( ), base_y + 114.0f ), theme::ink_soft, render::align::RIGHT );

		float cy = base_y + 154.0f;

		bool shuffled = shuffle_on ( );
		repeat_mode repeating = repeat_on ( );

		if ( icon_button ( c, make_id ( "now_shuffle" ), math::vec2 ( middle - 136.0f, cy ), 40.0f, theme::glyph::SHUFFLE, 17.0f, shuffled ? theme::ink : theme::ink_soft, shuffled ? L"Disable shuffle" : L"Enable shuffle" ) )
			toggle_shuffle ( );

		if ( shuffled )
			c.circle ( math::vec2 ( middle - 136.0f, cy + 15.0f ), 2.0f, theme::ink );

		if ( skip_button ( c, make_id ( "now_previous" ), math::vec2 ( middle - 70.0f, cy ), 50.0f, false, theme::ink, L"Previous" ) )
			previous_track ( );

		if ( play_button ( c, make_id ( "now_play" ), math::vec2 ( middle, cy ), 31.0f, playing, true, playing ? L"Pause" : L"Play" ) )
			toggle_play ( );

		if ( skip_button ( c, make_id ( "now_next" ), math::vec2 ( middle + 70.0f, cy ), 50.0f, true, theme::ink, L"Next" ) )
			next_track ( false );

		theme::glyph repeat_icon = repeating == repeat_mode::ONE ? theme::glyph::REPEAT_ONE : theme::glyph::REPEAT_ALL;
		const wchar_t* repeat_text = repeating == repeat_mode::OFF ? L"Enable repeat" : ( repeating == repeat_mode::ALL ? L"Repeat one" : L"Disable repeat" );

		if ( icon_button ( c, make_id ( "now_repeat" ), math::vec2 ( middle + 136.0f, cy ), 40.0f, repeat_icon, 17.0f, repeating == repeat_mode::OFF ? theme::ink_soft : theme::ink, repeat_text ) )
			cycle_repeat ( );

		if ( repeating != repeat_mode::OFF )
			c.circle ( math::vec2 ( middle + 136.0f, cy + 15.0f ), 2.0f, theme::ink );

		float volume_w = base::min ( size * 0.56f, 220.0f );
		math::rect volume_area ( middle - volume_w * 0.5f, cy + 46.0f, volume_w, 16.0f );
		float shown_volume = muted ? 0.0f : volume;

		speaker_shape ( c, math::vec2 ( volume_area.x - 20.0f, volume_area.center ( ).y ), 14.0f, 0.0f, 0.0f, 0.0f, theme::ink_soft );
		speaker_shape ( c, math::vec2 ( volume_area.right ( ) + 24.0f, volume_area.center ( ).y ), 14.0f, 1.0f, 1.0f, 0.0f, theme::ink_soft );

		float new_volume = slider ( c, make_id ( "now_volume" ), volume_area, shown_volume, volume_drag, theme::ink.fade ( 0.72f ) );

		if ( volume_drag )
			set_volume ( new_volume );

		c.pop ( );

	}

}