#include "ui/app.hh"

namespace ui {

	static void list_meta ( base::writer& out, const base::list < int32_t >& list ) {

		double total = 0.0;

		for ( int32_t t : list )
			total += static_cast < double > ( library::track_at ( t ).duration );

		int32_t minutes = static_cast < int32_t > ( total / 60.0 + 0.5 );

		out.number ( list.count ( ) ).add ( list.count ( ) == 1 ? L" song" : L" songs" );

		if ( list.empty ( ) )
			return;

		out.add ( L"  \x00B7  " );

		if ( minutes >= 60 )
			out.number ( minutes / 60 ).add ( L" hr " ).number ( minutes % 60 ).add ( L" min" );
		else
			out.number ( base::max ( minutes, 1 ) ).add ( L" min" );

	}

	void app::mosaic ( render::canvas& c, math::rect area, float radius, const base::list < int32_t >& list, uint64_t seed ) {

		int32_t picks [ 4 ];
		uint64_t seen [ 4 ];
		int32_t count = 0;

		for ( int32_t i = 0; i < list.count ( ) && count < 4; i++ ) {

			int32_t album = library::track_at ( list [ static_cast < size_t > ( i ) ] ).album;
			uint64_t key = library::album_at ( album ).key;
			bool known = false;

			for ( int32_t k = 0; k < count; k++ )
				known = known || seen [ k ] == key;

			if ( known )
				continue;

			seen [ count ] = key;
			picks [ count++ ] = album;

		}

		if ( count == 0 ) {

			render::color palette [ 4 ];
			render::color accent;

			library::placeholder_palette ( seed * 0x9E3779B97F4A7C15ull + 3, palette, accent );
			c.gradient ( area, radius, math::vec2 ( area.x, area.y ), math::vec2 ( area.right ( ), area.bottom ( ) ), palette [ 1 ], palette [ 3 ] );
			c.icon ( theme::code ( theme::glyph::MUSIC ), area.center ( ), area.w * 0.28f, theme::white.fade ( 0.9f ) );

			return;

		}

		if ( count < 4 ) {

			cover ( c, area, radius, picks [ 0 ] );

			return;

		}

		float gap = base::max ( 2.0f, area.w * 0.016f );
		float half = ( area.w - gap ) * 0.5f;
		float inner = base::max ( radius * 0.55f, 4.0f );

		for ( int32_t k = 0; k < 4; k++ ) {

			math::rect tile ( area.x + static_cast < float > ( k % 2 ) * ( half + gap ), area.y + static_cast < float > ( k / 2 ) * ( half + gap ), half, half );

			cover ( c, tile, inner, picks [ k ] );

		}

	}

	float app::draw_header ( render::canvas& c, location place, const base::list < int32_t >& list, context from, float x, float y, float width, bool interactive ) {

		float art_size = width > 760.0f ? 208.0f : 156.0f;
		math::rect art ( x, y, art_size, art_size );
		float radius = place.where == page::ARTIST ? art_size * 0.5f : 18.0f;
		const wchar_t* kind = L"LIBRARY";
		const wchar_t* title = L"Songs";
		base::writer subtitle;
		base::writer meta;
		uint64_t id = make_id ( "header", static_cast < int64_t > ( place.where ) * 100000 + ( place.where == page::ALBUM ? place.album : place.playlist ) );
		bool context_now = same_context ( queue_from, from ) && track >= 0 && !library::is_remote ( track );
		bool remote_list = !list.empty ( ) && library::is_catalog ( list [ 0 ] );

		c.shadow ( art, radius, 30.0f, theme::shade.fade ( 0.2f ), math::vec2 ( 0.0f, 16.0f ) );
		c.shadow ( art, radius, 3.0f, theme::shade.fade ( 0.1f ), math::vec2 ( 0.0f, 1.5f ) );

		if ( place.where == page::ARTIST ) {

			const library::album& album = library::album_at ( place.album );

			kind = L"ARTIST";
			title = album.artist;
			subtitle.add ( L"Artist" );
			cover ( c, art, radius, place.album );

		} else if ( place.where == page::ALBUM ) {

			const library::album& album = library::album_at ( place.album );

			kind = L"ALBUM";
			title = album.title;
			subtitle.add ( album.artist );

			if ( album.year > 0 )
				subtitle.add ( L"  \x00B7  " ).number ( album.year );

			cover ( c, art, radius, place.album );

		} else if ( place.where == page::LIKED ) {

			kind = L"COLLECTION";
			title = L"Liked Songs";
			subtitle.add ( L"Everything you\x2019ve hearted" );
			c.image ( art, radius, library::liked_cover ( ) );
			heart_shape ( c, art.center ( ), art_size * 0.3f, 1.0f );

		} else if ( place.where == page::PLAYLIST ) {

			const playlist& item = playlists [ static_cast < size_t > ( place.playlist ) ];

			kind = L"PLAYLIST";
			title = item.title.get ( );
			subtitle.add ( L"Your playlist" );
			mosaic ( c, art, radius, list, item.id );

		} else if ( place.where == page::MIX ) {

			kind = home_view.personal ? L"MADE FOR YOU" : L"TRENDING";
			title = home_view.personal ? L"Your Mix" : L"Today\x2019s Hits";
			subtitle.add ( home_view.personal ? L"Fresh picks every time you open Pearl" : L"What everyone\x2019s playing right now" );
			mosaic ( c, art, radius, list, 0x5EED );

		} else {

			subtitle.add ( L"Every song in your library" );
			mosaic ( c, art, radius, list, 11 );

		}

		list_meta ( meta, list );

		math::vec2 play_at ( art.right ( ) - 34.0f, art.bottom ( ) - 34.0f );
		bool pausing = context_now && playing;

		if ( !list.empty ( ) ) {

			c.shadow ( math::rect ( play_at.x - 25.0f, play_at.y - 25.0f, 50.0f, 50.0f ), 25.0f, 12.0f, theme::shade.fade ( 0.3f ), math::vec2 ( 0.0f, 5.0f ) );

			if ( play_button ( c, id ^ 0x11, play_at, 25.0f, pausing, true, pausing ? L"Pause" : L"Play" ) && interactive ) {

				if ( context_now )
					toggle_play ( );
				else if ( remote_list )
					play_virtual ( list, 0, from );
				else
					play_list ( list, 0, from );

			}

		}

		float tx = art.right ( ) + 30.0f;
		float text_w = x + width - tx;
		float middle = y + art_size * 0.5f;

		c.text ( kind, render::font::TINY, math::vec2 ( tx + 2.0f, middle - 58.0f ), theme::ink_soft );

		uint64_t title_id = make_id ( "title" );

		if ( place.where == page::PLAYLIST && interactive ) {

			widget& title_state = state ( title_id ^ 0x3 );

			if ( focus_id == title_id ) {

				title_list = place.playlist;
				float typed_w = title_field.length > 0 ? c.measure ( title_field.text, render::font::HERO ) : 240.0f;
				float box_w = base::clamp ( typed_w + 64.0f, 340.0f, text_w + 4.0f );
				math::rect box ( tx - 12.0f, middle - 46.0f, box_w, 64.0f );

				c.fill ( box, 16.0f, theme::white.fade ( 0.72f ) );
				c.outline ( box, 16.0f, render::color ( 0.0f, 0.0f, 0.0f, 0.0f ), 1.0f, theme::ink.fade ( 0.18f ) );
				text_field ( c, title_id, title_field, math::rect ( tx, middle - 42.0f, box_w - 24.0f, 56.0f ), L"Playlist name", render::font::HERO, theme::ink );

			} else {

				float title_w = base::min ( c.measure ( title, render::font::HERO ), text_w - 44.0f );
				math::rect hit_area ( tx - 10.0f, middle - 44.0f, title_w + 52.0f, 60.0f );

				if ( interact ( title_id ^ 0x3, hit_area, title_state ) ) {

					title_list = place.playlist;
					focus_field ( title_id, title_field, title, true );

				}

				tip ( title_id ^ 0x3, L"Rename", hit_area );
				c.fill ( hit_area, 14.0f, theme::shade.fade ( 0.035f * title_state.hover ) );
				c.text ( title, render::font::HERO, math::vec2 ( tx, middle - 14.0f ), theme::ink, render::align::LEFT, text_w - 44.0f );
				c.icon ( theme::code ( theme::glyph::EDIT ), math::vec2 ( tx + title_w + 24.0f, middle - 12.0f ), 15.0f, theme::ink_soft.fade ( 0.3f + 0.7f * title_state.hover ) );

			}

		} else
			c.text ( title, render::font::HERO, math::vec2 ( tx, middle - 14.0f ), theme::ink, render::align::LEFT, text_w );

		if ( subtitle.size ( ) )
			c.text ( subtitle.text ( ), render::font::BODY, math::vec2 ( tx + 1.0f, middle + 28.0f ), theme::ink_soft, render::align::LEFT, text_w );

		float meta_y = art.bottom ( ) - 18.0f;
		float meta_w = c.measure ( meta.text ( ), render::font::SMALL );

		c.text ( meta.text ( ), render::font::SMALL, math::vec2 ( tx, meta_y ), theme::ink_soft );

		float bx = tx + meta_w + 30.0f;

		if ( !list.empty ( ) ) {

			bool shuffled = shuffle && context_now;

			if ( icon_button ( c, id ^ 0x22, math::vec2 ( bx, meta_y ), 34.0f, theme::glyph::SHUFFLE, 15.0f, shuffled ? theme::ink : theme::ink_soft, shuffled ? L"Shuffle is on" : L"Shuffle play" ) && interactive ) {

				if ( context_now )
					toggle_shuffle ( );
				else if ( remote_list ) {

					shuffle = true;
					play_virtual ( list, static_cast < int32_t > ( static_cast < uint64_t > ( clock * 977.0 ) % static_cast < uint64_t > ( list.count ( ) ) ), from );

				} else {

					shuffle = true;
					queue = list;
					queue_from = from;
					build_order ( static_cast < int32_t > ( static_cast < uint64_t > ( clock * 977.0 ) % static_cast < uint64_t > ( list.count ( ) ) ) );
					open_track ( queue [ static_cast < size_t > ( order [ static_cast < size_t > ( order_index ) ] ) ], 1, true );

				}

			}

			if ( shuffled )
				c.circle ( math::vec2 ( bx, meta_y + 13.0f ), 1.8f, theme::ink );

			bx += 38.0f;

			repeat_mode looping = repeat_on ( );
			theme::glyph loop_icon = looping == repeat_mode::ONE ? theme::glyph::REPEAT_ONE : theme::glyph::REPEAT_ALL;
			const wchar_t* loop_text = looping == repeat_mode::OFF ? L"Loop" : ( looping == repeat_mode::ALL ? L"Loop one song" : L"Stop looping" );

			if ( icon_button ( c, id ^ 0x44, math::vec2 ( bx, meta_y ), 34.0f, loop_icon, 15.0f, looping == repeat_mode::OFF ? theme::ink_soft : theme::ink, loop_text ) && interactive )
				cycle_repeat ( );

			if ( looping != repeat_mode::OFF )
				c.circle ( math::vec2 ( bx, meta_y + 13.0f ), 1.8f, theme::ink );

			bx += 38.0f;

		}

		if ( place.where == page::PLAYLIST ) {

			uint64_t delete_id = id ^ 0x33;
			uint64_t confirm_id = id ^ 0x35;
			bool armed = delete_armed == delete_id && clock - delete_time < 3.0;
			widget& reveal = state ( delete_id ^ 0x5 );
			float full_w = c.measure ( L"Delete playlist", render::font::SMALL_STRONG ) + 54.0f;

			reveal.seen = frame_index;
			spring ( reveal.value, reveal.velocity, armed ? 1.0f : 0.0f, 300.0f, 24.0f );

			float open = math::saturate ( reveal.value );
			float grow = base::max ( reveal.value, 0.0f );
			math::rect confirm ( bx - 17.0f, meta_y - 16.0f, 34.0f + ( full_w - 34.0f ) * grow, 32.0f );

			if ( armed && interactive && press_frame == frame_index && !confirm.contains ( mouse ) )
				delete_armed = 0;

			if ( open < 0.5f ) {

				c.push ( math::vec2 ( bx, meta_y ), 1.0f - 0.3f * open, 1.0f - open * 2.0f );

				if ( icon_button ( c, delete_id, math::vec2 ( bx, meta_y ), 34.0f, theme::glyph::TRASH, 14.0f, theme::ink_soft, L"Delete playlist" ) && interactive && !armed ) {

					delete_armed = delete_id;
					delete_time = clock;

				}

				c.pop ( );

			}

			if ( open > 0.005f ) {

				widget& confirm_state = state ( confirm_id );
				float label = math::saturate ( ( open - 0.35f ) / 0.65f );

				if ( armed && open > 0.6f && interact ( confirm_id, confirm, confirm_state ) && interactive ) {

					delete_armed = 0;
					delete_playlist ( place.playlist );

					return y + art_size + 34.0f;

				}

				c.push ( math::vec2 ( bx, meta_y ), 0.8f + 0.2f * open, open );
				c.shadow ( confirm, 16.0f, 10.0f, theme::danger.fade ( 0.3f * open ), math::vec2 ( 0.0f, 3.0f ) );
				c.fill ( confirm, 16.0f, render::mix ( theme::danger, render::rgb ( 0xC9373C ), confirm_state.hover ) );
				c.icon ( theme::code ( theme::glyph::TRASH ), math::vec2 ( bx, meta_y ), 13.0f, theme::white );
				c.push_clip ( confirm );
				c.push ( math::vec2 ( confirm.x + 36.0f, meta_y ), 1.0f, label, math::vec2 ( -8.0f * ( 1.0f - label ), 0.0f ) );
				c.text ( L"Delete playlist", render::font::SMALL_STRONG, math::vec2 ( confirm.x + 36.0f, meta_y ), theme::white );
				c.pop ( );
				c.pop_clip ( );
				c.pop ( );

			}

			if ( armed )
				animating = true;

		}

		return y + art_size + 34.0f;

	}

	float app::spin_phase ( double rate ) const {

		double turn = clock * rate;

		return static_cast < float > ( turn - static_cast < double > ( static_cast < int64_t > ( turn ) ) );

	}

	float app::clock_wrapped ( ) const {

		return static_cast < float > ( clock - static_cast < double > ( static_cast < int64_t > ( clock / 600.0 ) ) * 600.0 );

	}

	float app::cascade ( int32_t index ) {

		if ( here.where != page::SEARCH && here.where != page::HOME )
			return 1.0f;

		double start = here.where == page::SEARCH ? search_kind_time : home_time;
		float t = math::saturate ( ( static_cast < float > ( clock - start ) - static_cast < float > ( base::min ( index, 14 ) ) * 0.03f ) / 0.3f );

		if ( t < 1.0f )
			animating = true;

		return 1.0f - ( 1.0f - t ) * ( 1.0f - t ) * ( 1.0f - t );

	}

	void app::track_list ( uint64_t id, const base::list < int32_t >& list ) {

		list_memory* memory = nullptr;

		for ( int32_t m = memories.count ( ) - 1; m >= 0; m-- ) {

			list_memory& item = memories [ static_cast < size_t > ( m ) ];

			if ( item.id != id && item.frame + 240 < frame_index )
				memories.remove_at ( static_cast < size_t > ( m ) );

		}

		for ( list_memory& item : memories ) {

			if ( item.id == id )
				memory = &item;

		}

		bool tracked = memory && memory->frame + 1 >= frame_index;

		if ( !memory ) {

			memory = &memories.emplace ( );
			memory->id = id;

		}

		bool changed = memory->keys.count ( ) != list.count ( );

		for ( int32_t i = 0; i < list.count ( ) && !changed; i++ )
			changed = memory->keys [ static_cast < size_t > ( i ) ] != library::track_at ( list [ static_cast < size_t > ( i ) ] ).key;

		if ( tracked && changed && list.count ( ) <= 600 && memory->keys.count ( ) <= 600 ) {

			base::list < uint8_t > used;

			used.fill ( memory->keys.size ( ), 0 );

			for ( int32_t i = 0; i < list.count ( ); i++ ) {

				uint64_t key = library::track_at ( list [ static_cast < size_t > ( i ) ] ).key;
				int32_t before = -1;

				for ( int32_t j = 0; j < memory->keys.count ( ) && before < 0; j++ ) {

					if ( !used [ static_cast < size_t > ( j ) ] && memory->keys [ static_cast < size_t > ( j ) ] == key )
						before = j;

				}

				if ( before >= 0 )
					used [ static_cast < size_t > ( before ) ] = 1;

				if ( before == i )
					continue;

				row_motion* motion = nullptr;

				for ( row_motion& item : motions ) {

					if ( item.list == id && item.key == key )
						motion = &item;

				}

				if ( !motion ) {

					motion = &motions.emplace ( );
					motion->list = id;
					motion->key = key;

				}

				if ( before < 0 ) {

					motion->appear = 0.0f;
					motion->offset = -10.0f;

				} else
					motion->offset += static_cast < float > ( before - i ) * 56.0f;

			}

			for ( int32_t j = 0; j < memory->keys.count ( ); j++ ) {

				if ( used [ static_cast < size_t > ( j ) ] )
					continue;

				row_ghost& ghost = ghosts.emplace ( );

				ghost.list = id;
				ghost.track = memory->tracks [ static_cast < size_t > ( j ) ];
				ghost.y = static_cast < float > ( j ) * 56.0f;
				ghost.died = clock;

			}

		}

		if ( changed ) {

			memory->keys.clear ( );

			for ( int32_t t : list )
				memory->keys.push ( library::track_at ( t ).key );

			memory->tracks = list;

		}

		memory->frame = frame_index;

		for ( int32_t k = motions.count ( ) - 1; k >= 0; k-- ) {

			row_motion& motion = motions [ static_cast < size_t > ( k ) ];

			if ( motion.list != id )
				continue;

			spring ( motion.offset, motion.velocity, 0.0f, 260.0f, 28.0f );
			approach ( motion.appear, 1.0f, 12.0f );

			if ( motion.offset == 0.0f && motion.velocity == 0.0f && motion.appear >= 1.0f )
				motions.remove_at ( static_cast < size_t > ( k ) );

		}

	}

	void app::draw_ghosts ( render::canvas& c, uint64_t id, float x, float y, float width, row_style style ) {

		wchar_t buffer [ 32 ];

		for ( int32_t k = ghosts.count ( ) - 1; k >= 0; k-- ) {

			row_ghost& ghost = ghosts [ static_cast < size_t > ( k ) ];

			if ( ghost.list != id )
				continue;

			float t = static_cast < float > ( clock - ghost.died ) / 0.26f;

			if ( t >= 1.0f || t < 0.0f ) {

				ghosts.remove_at ( static_cast < size_t > ( k ) );

				continue;

			}

			const library::track& item = library::track_at ( ghost.track );
			const library::album& album = library::album_at ( item.album );
			float e = t * t * ( 3.0f - 2.0f * t );
			float ry = y + ghost.y;
			float tx = style.covers ? x + 62.0f : x + 52.0f;

			c.push ( math::vec2 ( x + width * 0.5f, ry + 28.0f ), 1.0f - 0.05f * e, 1.0f - e, math::vec2 ( -14.0f * e, 0.0f ) );

			if ( style.covers )
				cover ( c, math::rect ( x + 8.0f, ry + 8.0f, 40.0f, 40.0f ), 8.0f, item.album );

			c.text ( item.title, render::font::BODY, math::vec2 ( tx, ry + 20.0f ), theme::ink, render::align::LEFT, width * 0.45f );
			c.text ( album.artist, render::font::SMALL, math::vec2 ( tx, ry + 38.0f ), theme::ink_soft, render::align::LEFT, width * 0.45f );
			c.text ( format_time ( item.duration, buffer ), render::font::DIGITS, math::vec2 ( x + width - 22.0f, ry + 28.0f ), theme::ink_soft, render::align::RIGHT );
			c.pop ( );
			animating = true;

		}

	}

	float app::draw_rows ( render::canvas& c, uint64_t id, const base::list < int32_t >& list, context from, float x, float y, float width, row_style style, bool interactive ) {

		if ( interactive && style.limit > 0 && list.count ( ) > style.limit ) {

			base::list < int32_t > visible;

			for ( int32_t i = 0; i < style.limit; i++ )
				visible.push ( list [ static_cast < size_t > ( i ) ] );

			track_list ( id, visible );

		} else if ( interactive )
			track_list ( id, list );

		bool album_column = style.albums && width > 640.0f;
		float album_x = x + width * 0.54f;
		wchar_t buffer [ 32 ];
		float ry = y;
		math::vec2 origin = c.screen ( math::vec2 ( 0.0f, 0.0f ) );
		float scale_now = c.scale ( );

		int32_t shown_rows = style.limit > 0 ? base::min ( style.limit, list.count ( ) ) : list.count ( );

		for ( int32_t i = 0; i < shown_rows; i++ ) {

			int32_t t = list [ static_cast < size_t > ( i ) ];
			const library::track& item = library::track_at ( t );
			const library::album& album = library::album_at ( item.album );
			math::rect row ( x, ry, width, 56.0f );
			float screen_y = origin.y + ry * scale_now;

			if ( screen_y > static_cast < float > ( height ) + 80.0f || screen_y + 56.0f * scale_now < -80.0f ) {

				ry += 56.0f;

				continue;

			}

			uint64_t row_id = id ^ ( ( item.key ? item.key : static_cast < uint64_t > ( t + 1 ) ) * 0x9E3779B97F4A7C15ull ) ^ static_cast < uint64_t > ( i ) * 0x51;
			widget& row_state = state ( row_id );
			float shift = 0.0f;
			float appear = 1.0f;

			for ( const row_motion& motion : motions ) {

				if ( motion.list == id && motion.key == item.key ) {

					shift = motion.offset;
					appear = motion.appear;

				}

			}

			float reveal = interactive ? cascade ( i ) : 1.0f;
			float shown = appear * appear * ( 3.0f - 2.0f * appear ) * reveal;

			float travel = shift + 16.0f * ( 1.0f - reveal );

			c.push ( math::vec2 ( x + width * 0.5f, ry + 28.0f ), 0.97f + 0.03f * shown, shown, math::vec2 ( 0.0f, travel ) );
			bool is_current = t == track || ( track >= 0 && library::is_virtual ( t ) && library::track_at ( track ).key == item.key );
			bool liked = library::is_catalog ( t ) ? library::liked_key ( item.key ) : item.liked;
			bool hovered = interactive && over ( row ) && !track_drag;
			bool activate = false;

			if ( interactive ) {

				if ( interact ( row_id, row, row_state ) )
					selected_track = t;

				if ( double_id == row_id )
					activate = true;

				if ( right_id == row_id )
					open_menu ( menu_kind::TRACK, t, i, from, mouse );

				if ( active_id == row_id && mouse_down && !track_drag && math::distance ( mouse, press_at ) > 8.0f ) {

					track_drag = true;
					drag_track = t;
					drag_source = row_id;
					drag_show = 0.0f;

				}

			}

			approach ( row_state.value, hovered || ( menu != menu_kind::NONE && menu_track == t && menu_row == i ) ? 1.0f : 0.0f, 22.0f );

			float lift = row_state.value;
			float selected = selected_track == t ? 1.0f : 0.0f;
			float dragged = track_drag && drag_source == row_id ? 1.0f : 0.0f;

			c.fill ( row, 12.0f, theme::shade.fade ( 0.04f * lift + 0.05f * selected ) );

			if ( dragged > 0.0f )
				c.outline ( row, 12.0f, render::color ( 0.0f, 0.0f, 0.0f, 0.0f ), 1.2f, theme::ink.fade ( 0.16f ) );

			math::vec2 index_at ( x + 24.0f, ry + 28.0f );
			render::color accent = album.accent;
			float tx = x + 52.0f;

			if ( interactive ) {

				uint64_t index_id = row_id ^ 0x5151;
				widget& index_state = state ( index_id );
				math::rect index_area ( x, ry, style.covers ? 60.0f : 46.0f, 56.0f );

				if ( interact ( index_id, index_area, index_state ) ) {

					if ( is_current )
						toggle_play ( );
					else
						activate = true;

				}

				tip ( index_id, is_current && playing ? L"Pause" : L"Play", index_area );

			}

			if ( style.covers ) {

				math::rect thumb ( x + 8.0f, ry + 8.0f, 40.0f, 40.0f );

				cover ( c, thumb, 8.0f, item.album );

				float shade_amount = base::max ( lift, is_current ? 1.0f : 0.0f );

				if ( shade_amount > 0.01f ) {

					c.fill ( thumb, 8.0f, theme::shade.fade ( 0.38f * shade_amount ) );

					if ( is_current && playing && lift < 0.5f )
						equalizer ( c, thumb.center ( ), 14.0f, theme::white );
					else {

						c.push ( thumb.center ( ), 0.6f + 0.4f * shade_amount, shade_amount );
						play_shape ( c, thumb.center ( ) + math::vec2 ( 1.0f, 0.0f ), 14.0f, is_current && playing ? 1.0f : 0.0f, theme::white );
						c.pop ( );

					}

				}

				tx = x + 62.0f;

			} else if ( is_current && playing && lift < 0.5f ) {

				equalizer ( c, index_at, 14.0f, accent );

			} else if ( is_current && lift < 0.5f ) {

				c.text ( base::writer ( ).number ( item.number > 0 ? item.number : i + 1 ).text ( ), render::font::DIGITS, index_at, accent, render::align::CENTER );

			} else {

				float number_alpha = 1.0f - lift;

				if ( number_alpha > 0.01f ) {

					c.push ( index_at, 1.0f - 0.35f * lift, number_alpha );
					c.text ( base::writer ( ).number ( item.number > 0 ? item.number : i + 1 ).text ( ), render::font::DIGITS, index_at, theme::ink_soft, render::align::CENTER );
					c.pop ( );

				}

				if ( lift > 0.01f ) {

					c.push ( index_at, 0.6f + 0.4f * lift, lift );
					play_shape ( c, index_at + math::vec2 ( 1.0f, 0.0f ), 15.0f, is_current && playing ? 1.0f : 0.0f, is_current ? accent : theme::ink );
					c.pop ( );

				}

			}

			float actions_w = style.playlist >= 0 ? 172.0f : 140.0f;
			float text_limit = ( album_column ? album_x - 24.0f : x + width - actions_w ) - tx;

			c.text ( item.title, is_current ? render::font::BODY_STRONG : render::font::BODY, math::vec2 ( tx, ry + 20.0f ), is_current ? accent : theme::ink, render::align::LEFT, text_limit );
			if ( style.labeled ) {

				base::writer kind_line;

				kind_line.add ( L"Song  \x00B7  " ).add ( album.artist );
				c.text ( kind_line.text ( ), render::font::SMALL, math::vec2 ( tx, ry + 38.0f ), theme::ink_soft, render::align::LEFT, text_limit );

			} else
				c.text ( album.artist, render::font::SMALL, math::vec2 ( tx, ry + 38.0f ), theme::ink_soft, render::align::LEFT, text_limit );

			if ( album_column )
				c.text ( album.title, render::font::SMALL, math::vec2 ( album_x, ry + 28.0f ), theme::ink_soft, render::align::LEFT, x + width - actions_w - album_x );

			float right = x + width;
			math::vec2 heart_at ( right - 92.0f, ry + 28.0f );
			math::vec2 add_at ( right - 126.0f, ry + 28.0f );
			math::vec2 remove_at ( right - 160.0f, ry + 28.0f );
			uint64_t heart_id = row_id ^ 0xAEAE;
			uint64_t add_id = row_id ^ 0xADAD;
			uint64_t remove_id = row_id ^ 0xDEDE;
			float heart_alpha = base::max ( liked ? 1.0f : lift, state ( heart_id ).value );

			if ( lift > 0.01f && interactive ) {

				c.push_alpha ( lift );

				if ( icon_button ( c, add_id, add_at, 30.0f, theme::glyph::ADD, 13.0f, theme::ink_soft, L"Add to playlist" ) )
					open_menu ( menu_kind::ADD, t, i, from, add_at + math::vec2 ( -8.0f, 18.0f ) );

				if ( style.playlist >= 0 ) {

					if ( icon_button ( c, remove_id, remove_at, 30.0f, theme::glyph::REMOVE, 13.0f, theme::ink_soft, L"Remove from playlist" ) )
						remove_from_playlist ( style.playlist, t );

				}

				c.pop ( );

			}

			if ( heart_alpha > 0.01f ) {

				c.push_alpha ( math::saturate ( heart_alpha ) );

				if ( interactive ) {

					if ( heart_button ( c, heart_id, heart_at, 32.0f, 15.0f, liked, liked ? L"Remove from Liked Songs" : L"Add to Liked Songs" ) )
						like_index ( t, !liked );

				} else
					heart_shape ( c, heart_at, 15.0f, liked ? 1.0f : 0.0f );

				c.pop ( );

			}

			c.text ( format_time ( item.duration, buffer ), render::font::DIGITS, math::vec2 ( right - 22.0f, ry + 28.0f ), theme::ink_soft, render::align::RIGHT );

			if ( activate && library::is_catalog ( t ) ) {

				if ( is_current && !playing )
					toggle_play ( );
				else if ( !is_current )
					play_virtual ( list, i, from );

			} else if ( activate ) {

				if ( style.upcoming ) {

					order_index = base::min ( order_index + 1 + i, order.count ( ) - 1 );
					open_track ( t, 1, true );

				} else if ( !is_current )
					play_list ( list, i, from );
				else if ( !playing )
					toggle_play ( );

			}

			c.pop ( );
			ry += 56.0f;

		}

		if ( interactive )
			draw_ghosts ( c, id, x, y, width, style );

		return ry;

	}

	float app::draw_album_grid ( render::canvas& c, const base::list < int32_t >& albums, float x, float y, float width, int32_t limit_rows, bool interactive ) {

		float gap = 22.0f;
		int32_t columns = base::max ( 2, static_cast < int32_t > ( ( width + gap ) / ( 178.0f + gap ) ) );
		float cell = ( width - gap * static_cast < float > ( columns - 1 ) ) / static_cast < float > ( columns );
		float row_h = cell + 62.0f;
		int32_t count = albums.count ( );

		if ( limit_rows > 0 )
			count = base::min ( count, columns * limit_rows );

		math::vec2 origin = c.screen ( math::vec2 ( 0.0f, 0.0f ) );
		float scale_now = c.scale ( );

		for ( int32_t k = 0; k < count; k++ ) {

			int32_t a = albums [ static_cast < size_t > ( k ) ];
			int32_t column = k % columns;
			int32_t row = k / columns;
			math::rect art ( x + static_cast < float > ( column ) * ( cell + gap ), y + static_cast < float > ( row ) * row_h, cell, cell );
			float screen_y = origin.y + art.y * scale_now;

			if ( screen_y > static_cast < float > ( height ) + 60.0f || screen_y + row_h * scale_now < -60.0f )
				continue;

			const library::album& album = library::album_at ( a );
			math::rect card ( art.x, art.y, cell, cell + 52.0f );
			float reveal = interactive ? cascade ( k ) : 1.0f;

			c.push ( card.center ( ), 0.96f + 0.04f * reveal, reveal, math::vec2 ( 0.0f, 18.0f * ( 1.0f - reveal ) ) );
			uint64_t id = make_id ( "grid_album", a );
			widget& item = state ( id );
			bool open = interactive && interact ( id, card, item );

			spring ( item.value, item.velocity, interactive && over ( card ) ? 1.0f : 0.0f, 260.0f, 24.0f );

			float lift = item.value;
			bool remote_album = library::is_catalog ( a );
			bool context_now = !remote_album && queue_from.kind == origin::ALBUM && queue_from.index == a && track >= 0 && !remote_mode ( );
			float appear = base::max ( math::saturate ( lift ), context_now && playing ? 1.0f : 0.0f );

			c.push ( art.center ( ), 1.0f + 0.025f * lift, 1.0f, math::vec2 ( 0.0f, -4.0f * lift ) );
			c.shadow ( art, 16.0f, 18.0f + 10.0f * lift, theme::shade.fade ( 0.15f + 0.08f * lift ), math::vec2 ( 0.0f, 9.0f + 5.0f * lift ) );
			c.shadow ( art, 16.0f, 2.5f, theme::shade.fade ( 0.08f ), math::vec2 ( 0.0f, 1.0f ) );
			cover ( c, art, 16.0f, a );

			uint64_t play_id = id ^ 0x77;

			if ( appear > 0.01f || state ( play_id ).hover > 0.0f ) {

				math::vec2 play_at ( art.right ( ) - 28.0f, art.bottom ( ) - 28.0f );

				c.push ( play_at, 0.6f + 0.4f * appear, appear, math::vec2 ( 0.0f, 8.0f * ( 1.0f - appear ) ) );

				if ( play_button ( c, play_id, play_at, 19.0f, context_now && playing, true, context_now && playing ? L"Pause" : L"Play" ) && interactive ) {

					base::list < int32_t > list;

					if ( context_now )
						toggle_play ( );
					else if ( remote_album )
						play_context ( a );
					else {

						album_tracks ( a, list );
						play_list ( list, 0, { origin::ALBUM, a } );

					}

				}

				c.pop ( );

			}

			c.pop ( );

			base::writer meta;

			if ( here.where == page::SEARCH )
				meta.add ( L"Album  \x00B7  " );

			meta.add ( album.artist );

			if ( album.year > 0 )
				meta.add ( L"  \x00B7  " ).number ( album.year );

			c.text ( album.title, render::font::BODY_STRONG, math::vec2 ( art.x + 2.0f, art.bottom ( ) + 20.0f ), theme::ink, render::align::LEFT, cell - 4.0f );
			c.text ( meta.text ( ), render::font::SMALL, math::vec2 ( art.x + 2.0f, art.bottom ( ) + 38.0f ), theme::ink_soft, render::align::LEFT, cell - 4.0f );

			c.pop ( );

			if ( open && remote_album )
				open_detail ( a, 1 );
			else if ( open )
				navigate ( page::ALBUM, a );

		}

		int32_t rows = ( count + columns - 1 ) / columns;

		return y + static_cast < float > ( rows ) * row_h;

	}

	float app::draw_artist_grid ( render::canvas& c, const base::list < int32_t >& artists, float x, float y, float width, int32_t limit_rows, bool interactive ) {

		float gap = 22.0f;
		int32_t columns = base::max ( 2, static_cast < int32_t > ( ( width + gap ) / ( 150.0f + gap ) ) );
		float cell = ( width - gap * static_cast < float > ( columns - 1 ) ) / static_cast < float > ( columns );
		float row_h = cell + 58.0f;
		int32_t count = artists.count ( );

		if ( limit_rows > 0 )
			count = base::min ( count, columns * limit_rows );

		for ( int32_t k = 0; k < count; k++ ) {

			int32_t a = artists [ static_cast < size_t > ( k ) ];
			const library::album& album = library::album_at ( a );
			bool remote_artist = library::is_catalog ( a );
			const wchar_t* name = remote_artist ? album.title : album.artist;
			int32_t column = k % columns;
			int32_t row = k / columns;
			float side = cell * 0.86f;
			math::rect slot ( x + static_cast < float > ( column ) * ( cell + gap ), y + static_cast < float > ( row ) * row_h, cell, row_h - 8.0f );
			math::rect art ( slot.center ( ).x - side * 0.5f, slot.y, side, side );
			uint64_t id = make_id ( "grid_artist", a ) ^ library::hash ( name, true );
			widget& item = state ( id );
			bool open = interactive && interact ( id, slot, item );
			float reveal = interactive ? cascade ( k ) : 1.0f;

			c.push ( slot.center ( ), 0.96f + 0.04f * reveal, reveal, math::vec2 ( 0.0f, 18.0f * ( 1.0f - reveal ) ) );

			spring ( item.value, item.velocity, interactive && over ( slot ) ? 1.0f : 0.0f, 260.0f, 24.0f );

			float lift = item.value;

			c.push ( art.center ( ), 1.0f + 0.03f * lift, 1.0f, math::vec2 ( 0.0f, -4.0f * lift ) );
			c.shadow ( art, side * 0.5f, 16.0f + 8.0f * lift, theme::shade.fade ( 0.16f + 0.08f * lift ), math::vec2 ( 0.0f, 8.0f + 4.0f * lift ) );
			cover ( c, art, side * 0.5f, a );
			c.pop ( );
			c.text ( name, render::font::BODY_STRONG, math::vec2 ( slot.center ( ).x, art.bottom ( ) + 20.0f ), theme::ink, render::align::CENTER, cell - 6.0f );
			c.text ( remote_artist && album.artist [ 0 ] ? album.artist : L"Artist", render::font::SMALL, math::vec2 ( slot.center ( ).x, art.bottom ( ) + 38.0f ), theme::ink_soft, render::align::CENTER, cell - 6.0f );
			c.pop ( );

			if ( open && remote_artist )
				open_detail ( a, 2 );
			else if ( open )
				navigate ( page::ARTIST, a );

		}

		int32_t rows = ( count + columns - 1 ) / columns;

		return y + static_cast < float > ( rows ) * row_h;

	}

	float app::draw_playlist_grid ( render::canvas& c, float x, float y, float width, int32_t limit_rows, bool interactive ) {

		float gap = 22.0f;
		int32_t columns = base::max ( 2, static_cast < int32_t > ( ( width + gap ) / ( 178.0f + gap ) ) );
		float cell = ( width - gap * static_cast < float > ( columns - 1 ) ) / static_cast < float > ( columns );
		float row_h = cell + 62.0f;
		int32_t count = playlists.count ( ) + 1;
		bool newest = limit_rows > 0;

		if ( newest )
			count = base::min ( count, columns * limit_rows );

		for ( int32_t k = 0; k < count; k++ ) {

			int32_t column = k % columns;
			int32_t row = k / columns;
			math::rect art ( x + static_cast < float > ( column ) * ( cell + gap ), y + static_cast < float > ( row ) * row_h, cell, cell );
			math::rect card ( art.x, art.y, cell, cell + 52.0f );
			bool create = newest ? k == 0 : k == playlists.count ( );
			int32_t list_index = newest ? playlists.count ( ) - k : k;
			uint64_t id = create ? make_id ( "grid_new_playlist" ) : make_id ( "grid_playlist", static_cast < int64_t > ( playlists [ static_cast < size_t > ( list_index ) ].id ) );
			widget& item = state ( id );
			bool open = interactive && interact ( id, card, item );
			float reveal = interactive ? cascade ( k ) : 1.0f;

			spring ( item.value, item.velocity, interactive && over ( card ) ? 1.0f : 0.0f, 260.0f, 24.0f );

			float lift = item.value;

			c.push ( card.center ( ), 0.96f + 0.04f * reveal, reveal, math::vec2 ( 0.0f, 18.0f * ( 1.0f - reveal ) ) );
			c.push ( art.center ( ), 1.0f + 0.025f * lift, 1.0f, math::vec2 ( 0.0f, -4.0f * lift ) );

			if ( create ) {

				c.fill ( art, 16.0f, theme::white.fade ( 0.35f + 0.25f * lift ) );
				c.outline ( art, 16.0f, render::color ( 0.0f, 0.0f, 0.0f, 0.0f ), 1.4f, theme::ink.fade ( 0.14f + 0.1f * lift ) );
				c.fill ( math::rect ( art.center ( ).x - 14.0f, art.center ( ).y - 1.2f, 28.0f, 2.4f ), 1.2f, theme::ink_soft );
				c.fill ( math::rect ( art.center ( ).x - 1.2f, art.center ( ).y - 14.0f, 2.4f, 28.0f ), 1.2f, theme::ink_soft );

			} else {

				base::list < int32_t > tracks;

				playlist_tracks ( list_index, tracks );
				c.shadow ( art, 16.0f, 18.0f + 10.0f * lift, theme::shade.fade ( 0.15f + 0.08f * lift ), math::vec2 ( 0.0f, 9.0f + 5.0f * lift ) );
				mosaic ( c, art, 16.0f, tracks, playlists [ static_cast < size_t > ( list_index ) ].id );

			}

			c.pop ( );

			if ( create )
				c.text ( L"New playlist", render::font::BODY_STRONG, math::vec2 ( art.x + 2.0f, art.bottom ( ) + 20.0f ), theme::ink, render::align::LEFT, cell - 4.0f );
			else {

				const playlist& list = playlists [ static_cast < size_t > ( list_index ) ];
				base::writer meta;

				meta.number ( list.keys.count ( ) ).add ( list.keys.count ( ) == 1 ? L" song" : L" songs" );
				c.text ( list.title.get ( ), render::font::BODY_STRONG, math::vec2 ( art.x + 2.0f, art.bottom ( ) + 20.0f ), theme::ink, render::align::LEFT, cell - 4.0f );
				c.text ( meta.text ( ), render::font::SMALL, math::vec2 ( art.x + 2.0f, art.bottom ( ) + 38.0f ), theme::ink_soft, render::align::LEFT, cell - 4.0f );

				if ( interactive && right_id == id )
					open_menu ( menu_kind::PLAYLIST, -1, list_index, { origin::PLAYLIST, list_index }, mouse );

			}

			c.pop ( );

			if ( open && create )
				create_playlist ( nullptr );
			else if ( open )
				navigate ( page::PLAYLIST, list_index );

		}

		int32_t rows = ( count + columns - 1 ) / columns;

		return y + static_cast < float > ( rows ) * row_h;

	}

	float app::draw_search ( render::canvas& c, float x, float y, float width, bool interactive ) {

		base::writer heading;
		float cy = y;
		int32_t song_count = found_songs.count ( ) + found_remote.count ( );
		int32_t album_count = found_albums.count ( ) + found_remote_albums.count ( );
		int32_t artist_count = found_artists.count ( ) + found_remote_artists.count ( );
		int32_t list_count = found_lists.count ( );
		bool any = song_count + album_count + artist_count + list_count > 0;
		base::list < int32_t > albums;
		base::list < int32_t > artists;

		for ( int32_t a : found_albums )
			albums.push ( a );

		for ( int32_t a : found_remote_albums )
			albums.push ( a );

		for ( int32_t a : found_artists )
			artists.push ( a );

		for ( int32_t a : found_remote_artists )
			artists.push ( a );

		heading.add ( L"\x201C" ).add ( search_field.text ).add ( L"\x201D" );
		c.text ( L"SEARCH", render::font::TINY, math::vec2 ( x + 2.0f, cy + 14.0f ), theme::ink_soft );
		c.text ( heading.text ( ), render::font::HERO, math::vec2 ( x, cy + 50.0f ), theme::ink, render::align::LEFT, width );
		cy += 84.0f;

		const wchar_t* kinds [ 5 ] = { L"All", L"Songs", L"Albums", L"Artists", L"Playlists" };
		int32_t counts [ 5 ] = { -1, song_count, album_count, artist_count, list_count };
		float chip_x = x;
		float chip_target = x;
		float chip_width = 0.0f;
		float widths [ 5 ];

		for ( int32_t k = 0; k < 5; k++ ) {

			widths [ k ] = c.measure ( kinds [ k ], render::font::BODY_STRONG ) + 36.0f;

			if ( k == search_kind ) {

				chip_target = chip_x;
				chip_width = widths [ k ];

			}

			chip_x += widths [ k ] + 8.0f;

		}

		if ( search_kind_x <= 0.0f )
			search_kind_x = chip_target;

		spring ( search_kind_x, search_kind_velocity, chip_target, 320.0f, 28.0f );

		math::rect marker ( search_kind_x, cy, chip_width, 36.0f );

		c.shadow ( marker, 18.0f, 8.0f, theme::shade.fade ( 0.18f ), math::vec2 ( 0.0f, 3.0f ) );
		c.fill ( marker, 18.0f, theme::ink );
		chip_x = x;

		for ( int32_t k = 0; k < 5; k++ ) {

			math::rect chip ( chip_x, cy, widths [ k ], 36.0f );
			uint64_t id = make_id ( "search_kind", k );
			widget& chip_state = state ( id );
			bool selected = k == search_kind;
			bool empty = counts [ k ] == 0;
			float closeness = 1.0f - math::saturate ( math::abs ( search_kind_x - chip.x ) / base::max ( widths [ k ], 1.0f ) );

			if ( interactive && interact ( id, chip, chip_state ) && !empty ) {

				if ( search_kind != k )
					search_kind_time = clock;

				search_kind = k;
				scroll_target = 0.0f;

			}

			if ( !selected )
				c.fill ( chip, 18.0f, theme::shade.fade ( 0.045f + 0.04f * chip_state.hover ) );

			render::color tint = render::mix ( empty ? theme::ink_faint : theme::ink, theme::white, closeness );

			c.text ( kinds [ k ], render::font::BODY_STRONG, chip.center ( ), tint, render::align::CENTER );
			chip_x += widths [ k ] + 8.0f;

		}

		cy += 60.0f;

		if ( search_kind != 0 && counts [ search_kind ] == 0 && !remote_loading )
			search_kind = 0;

		row_style labeled;

		labeled.labeled = true;

		if ( search_kind == 0 ) {

			if ( song_count > 0 ) {

				base::list < int32_t > top_local;
				base::list < int32_t > top_remote;

				for ( int32_t i = 0; i < found_songs.count ( ) && i < 5; i++ )
					top_local.push ( found_songs [ static_cast < size_t > ( i ) ] );

				for ( int32_t i = 0; i < found_remote.count ( ) && top_local.count ( ) + top_remote.count ( ) < 5; i++ )
					top_remote.push ( found_remote [ static_cast < size_t > ( i ) ] );

				c.text ( L"Songs", render::font::HEADING, math::vec2 ( x, cy + 12.0f ), theme::ink );

				if ( song_count > 5 ) {

					math::rect link ( x + width - 110.0f, cy - 2.0f, 110.0f, 28.0f );
					uint64_t link_id = make_id ( "search_all_songs" );
					widget& link_state = state ( link_id );

					if ( interactive && interact ( link_id, link, link_state ) ) {

						search_kind = 1;
						search_kind_time = clock;
						scroll_target = 0.0f;

					}

					c.text ( L"See all", render::font::SMALL_STRONG, math::vec2 ( x + width - 6.0f, cy + 12.0f ), render::mix ( theme::ink_soft, theme::ink, link_state.hover ), render::align::RIGHT );

				}

				cy = draw_rows ( c, make_id ( "search_top" ), top_local, { origin::SEARCH, 0 }, x, cy + 34.0f, width, labeled, interactive );
				cy = draw_rows ( c, make_id ( "search_top_more" ), top_remote, { origin::SEARCH, 1 }, x, cy, width, labeled, interactive ) + 26.0f;

			}

			if ( artist_count > 0 ) {

				c.text ( L"Artists", render::font::HEADING, math::vec2 ( x, cy + 12.0f ), theme::ink );
				cy = draw_artist_grid ( c, artists, x, cy + 40.0f, width, 1, interactive ) + 16.0f;

			}

			if ( album_count > 0 ) {

				c.text ( L"Albums", render::font::HEADING, math::vec2 ( x, cy + 12.0f ), theme::ink );
				cy = draw_album_grid ( c, albums, x, cy + 38.0f, width, 1, interactive ) + 18.0f;

			}

		} else if ( search_kind == 1 ) {

			cy = draw_rows ( c, make_id ( "search_rows" ), found_songs, { origin::SEARCH, 0 }, x, cy, width, labeled, interactive );
			cy = draw_rows ( c, make_id ( "search_more" ), found_remote, { origin::SEARCH, 1 }, x, cy, width, labeled, interactive );

		} else if ( search_kind == 2 )
			cy = draw_album_grid ( c, albums, x, cy, width, -1, interactive ) + 18.0f;
		else if ( search_kind == 3 )
			cy = draw_artist_grid ( c, artists, x, cy, width, -1, interactive ) + 16.0f;

		if ( list_count > 0 && ( search_kind == 0 || search_kind == 4 ) ) {

			if ( search_kind == 0 ) {

				c.text ( L"Playlists", render::font::HEADING, math::vec2 ( x, cy + 12.0f ), theme::ink );
				cy += 38.0f;

			}

			float px = x;

			for ( int32_t p : found_lists ) {

				const playlist& item = playlists [ static_cast < size_t > ( p ) ];
				base::list < int32_t > tracks;
				base::writer meta;
				float label_w = base::min ( c.measure ( item.title.get ( ), render::font::BODY_STRONG ), 180.0f );
				math::rect chip ( px, cy, 76.0f + base::max ( label_w, 90.0f ), 60.0f );
				uint64_t id = make_id ( "found_list", static_cast < int64_t > ( item.id ) );
				widget& chip_state = state ( id );

				if ( chip.right ( ) > x + width ) {

					px = x;
					cy += 72.0f;
					chip.x = px;
					chip.y = cy;

				}

				playlist_tracks ( p, tracks );
				meta.add ( L"Playlist  \x00B7  " ).number ( tracks.count ( ) ).add ( tracks.count ( ) == 1 ? L" song" : L" songs" );

				if ( interact ( id, chip, chip_state ) && interactive )
					navigate ( page::PLAYLIST, p );

				c.fill ( chip, 30.0f, theme::white.fade ( 0.55f + 0.3f * chip_state.hover ) );
				c.outline ( chip, 30.0f, render::color ( 0.0f, 0.0f, 0.0f, 0.0f ), 1.0f, theme::shade.fade ( 0.06f ) );
				mosaic ( c, math::rect ( chip.x + 8.0f, chip.y + 8.0f, 44.0f, 44.0f ), 22.0f, tracks, item.id );
				c.text ( item.title.get ( ), render::font::BODY_STRONG, math::vec2 ( chip.x + 62.0f, chip.y + 23.0f ), theme::ink, render::align::LEFT, label_w + 2.0f );
				c.text ( meta.text ( ), render::font::SMALL, math::vec2 ( chip.x + 62.0f, chip.y + 41.0f ), theme::ink_soft, render::align::LEFT, chip.w - 70.0f );
				px = chip.right ( ) + 12.0f;

			}

			cy += 84.0f;

		}

		bool paging = search_kind == 1 && remote_more;

		if ( remote_loading || paging ) {

			float turn = spin_phase ( 0.9 );

			c.ring ( math::vec2 ( x + width * 0.5f, cy + 30.0f ), 11.0f, 2.2f, turn - math::floor ( turn ), 0.28f, theme::ink.fade ( 0.55f ) );
			animating = true;
			cy += 64.0f;

		} else if ( !any ) {

			c.text ( L"No matches.", render::font::BODY, math::vec2 ( x, cy + 12.0f ), theme::ink_soft );
			cy += 40.0f;

		}

		return cy;

	}

	static void release_date ( const wchar_t* date, base::writer& out ) {

		const wchar_t* months [ 12 ] = { L"January", L"February", L"March", L"April", L"May", L"June", L"July", L"August", L"September", L"October", L"November", L"December" };
		int32_t parts [ 3 ] = { 0, 0, 0 };
		int32_t count = 0;

		for ( const wchar_t* at = date; *at && count < 3; ) {

			while ( *at >= L'0' && *at <= L'9' )
				parts [ count ] = parts [ count ] * 10 + ( *at++ - L'0' );

			count++;

			if ( *at == L'-' )
				at++;
			else
				break;

		}

		if ( count >= 3 && parts [ 2 ] > 0 )
			out.number ( parts [ 2 ] ).add ( L" " );

		if ( count >= 2 && parts [ 1 ] >= 1 && parts [ 1 ] <= 12 )
			out.add ( months [ parts [ 1 ] - 1 ] ).add ( L" " );

		if ( parts [ 0 ] > 0 )
			out.number ( parts [ 0 ] );

	}

	float app::draw_detail ( render::canvas& c, float x, float y, float width, bool interactive ) {

		const spotify::detail& d = detail_view;
		bool ready = !d.loading && !detail_stack.empty ( ) && base::same_text ( d.uri, detail_stack.back ( ).get ( ) ) && d.title [ 0 ];

		if ( icon_button ( c, make_id ( "detail_back" ), math::vec2 ( x + 18.0f, y + 4.0f ), 36.0f, theme::glyph::BACK, 14.0f, theme::ink, L"Back" ) && interactive )
			close_detail ( );

		y += 34.0f;

		if ( !ready ) {

			float turn = spin_phase ( 0.9 );

			c.ring ( math::vec2 ( x + width * 0.5f, y + 140.0f ), 14.0f, 2.5f, turn - math::floor ( turn ), 0.28f, theme::ink.fade ( 0.6f ) );
			animating = true;

			return y + 240.0f;

		}

		bool artist = d.kind == 2;
		int32_t header = library::catalog + 540;
		float art_size = width > 760.0f ? 208.0f : 156.0f;
		math::rect art ( x, y, art_size, art_size );
		float radius = artist ? art_size * 0.5f : 18.0f;
		float total = 0.0f;
		base::writer kind;
		base::writer meta;
		context from = { origin::SEARCH, 2 };

		for ( int32_t t : detail_tracks )
			total += library::track_at ( t ).duration;

		c.shadow ( art, radius, 30.0f, theme::shade.fade ( 0.2f ), math::vec2 ( 0.0f, 16.0f ) );
		c.shadow ( art, radius, 3.0f, theme::shade.fade ( 0.1f ), math::vec2 ( 0.0f, 1.5f ) );
		cover ( c, art, radius, header );

		if ( artist ) {

			kind.add ( L"ARTIST" );
			meta.number ( detail_albums.count ( ) ).add ( detail_albums.count ( ) == 1 ? L" release" : L" releases" );

		} else {

			const wchar_t* type = d.type;

			kind.add ( base::same_text ( type, L"single" ) ? L"SINGLE" : ( base::same_text ( type, L"compilation" ) ? L"COMPILATION" : L"ALBUM" ) );

			int32_t year = 0;

			for ( int32_t k = 0; k < 4 && d.date [ k ] >= L'0' && d.date [ k ] <= L'9'; k++ )
				year = year * 10 + ( d.date [ k ] - L'0' );

			if ( year > 0 )
				meta.number ( year ).add ( L"  \x00B7  " );

			int32_t minutes = static_cast < int32_t > ( total / 60.0f + 0.5f );

			meta.number ( detail_tracks.count ( ) ).add ( detail_tracks.count ( ) == 1 ? L" song" : L" songs" ).add ( L"  \x00B7  " );

			if ( minutes >= 60 )
				meta.number ( minutes / 60 ).add ( L" hr " ).number ( minutes % 60 ).add ( L" min" );
			else
				meta.number ( base::max ( minutes, 1 ) ).add ( L" min" );

		}

		if ( !artist ) {

			math::vec2 play_at ( art.right ( ) - 34.0f, art.bottom ( ) - 34.0f );

			c.shadow ( math::rect ( play_at.x - 25.0f, play_at.y - 25.0f, 50.0f, 50.0f ), 25.0f, 12.0f, theme::shade.fade ( 0.3f ), math::vec2 ( 0.0f, 5.0f ) );

			if ( play_button ( c, make_id ( "detail_play" ), play_at, 25.0f, false, true, L"Play" ) && interactive )
				play_virtual ( detail_tracks, 0, from );

		}

		float tx = art.right ( ) + 30.0f;
		float text_w = x + width - tx;
		float middle = y + art_size * 0.5f;

		float settle = artist && !d.genres [ 0 ] ? 16.0f : 0.0f;

		c.text ( kind.text ( ), render::font::TINY, math::vec2 ( tx + 2.0f, middle - 58.0f + settle ), theme::ink_soft );
		c.text ( d.title, render::font::HERO, math::vec2 ( tx, middle - 14.0f + settle ), theme::ink, render::align::LEFT, text_w );
		c.text ( artist ? d.genres : d.artist, render::font::BODY, math::vec2 ( tx + 1.0f, middle + 28.0f ), theme::ink_soft, render::align::LEFT, text_w );

		if ( !artist )
			c.text ( meta.text ( ), render::font::SMALL, math::vec2 ( tx, art.bottom ( ) - 18.0f ), theme::ink_soft );

		float cy = y + art_size + 34.0f;

		if ( artist ) {

			if ( !detail_tracks.empty ( ) ) {

				row_style popular;

				popular.limit = 6;
				c.text ( L"Popular", render::font::HEADING, math::vec2 ( x, cy + 12.0f ), theme::ink );
				cy = draw_rows ( c, make_id ( "detail_popular" ) ^ library::hash ( d.uri, false ), detail_tracks, from, x, cy + 40.0f, width, popular, interactive ) + 34.0f;

			}

			if ( d.bio [ 0 ] ) {

				float column = base::min ( width, 760.0f );

				c.text ( L"About", render::font::HEADING, math::vec2 ( x, cy + 12.0f ), theme::ink );
				cy += 36.0f;
				cy += c.paragraph ( d.bio, render::font::BODY, math::vec2 ( x, cy ), column, theme::ink.fade ( 0.78f ) );
				cy += 30.0f;

			}

			if ( !detail_albums.empty ( ) ) {

				float heading_w = c.text ( L"Discography", render::font::HEADING, math::vec2 ( x, cy + 12.0f ), theme::ink );

				c.text ( meta.text ( ), render::font::SMALL, math::vec2 ( x + heading_w + 12.0f, cy + 13.0f ), theme::ink_soft );
				cy = draw_album_grid ( c, detail_albums, x, cy + 40.0f, width, -1, interactive ) + 18.0f;

			}

			return cy;

		}

		row_style style;

		style.covers = false;
		style.albums = false;
		style.numbers = true;
		cy = draw_rows ( c, make_id ( "detail_rows" ) ^ library::hash ( d.uri, false ), detail_tracks, from, x, cy, width, style, interactive );

		base::writer released;

		release_date ( d.date, released );

		if ( released.size ( ) ) {

			base::writer line;

			line.add ( L"Released " ).add ( released.text ( ) );
			c.text ( line.text ( ), render::font::SMALL_STRONG, math::vec2 ( x + 4.0f, cy + 30.0f ), theme::ink_soft );
			cy += 24.0f;

		}

		if ( d.copyright [ 0 ] ) {

			c.text ( d.copyright, render::font::SMALL, math::vec2 ( x + 4.0f, cy + 30.0f ), theme::ink_soft.fade ( 0.8f ), render::align::LEFT, width - 8.0f );
			cy += 24.0f;

		}

		return cy + 30.0f;

	}

	float app::draw_up_next ( render::canvas& c, float x, float y, float width, bool interactive ) {

		if ( library::is_remote ( track ) ) {

			base::writer line;

			line.add ( L"Playing on " ).add ( remote_state.device [ 0 ] ? remote_state.device : L"your device" );
			c.text ( L"Up Next", render::font::HEADING, math::vec2 ( x + 10.0f, y + 12.0f ), theme::ink );
			c.text ( line.text ( ), render::font::BODY, math::vec2 ( x + 10.0f, y + 46.0f ), theme::ink_soft, render::align::LEFT, width - 20.0f );

			return y + 72.0f;

		}

		base::list < int32_t > upcoming;
		base::writer source;
		const wchar_t* from_name = L"your library";

		for ( int32_t k = order_index + 1; k < order.count ( ); k++ )
			upcoming.push ( queue [ static_cast < size_t > ( order [ static_cast < size_t > ( k ) ] ) ] );

		if ( queue_from.kind == origin::ALBUM )
			from_name = library::album_at ( queue_from.index ).title;
		else if ( queue_from.kind == origin::LIKED )
			from_name = L"Liked Songs";
		else if ( queue_from.kind == origin::PLAYLIST && queue_from.index < playlists.count ( ) )
			from_name = playlists [ static_cast < size_t > ( queue_from.index ) ].title.get ( );
		else if ( queue_from.kind == origin::SEARCH )
			from_name = L"search results";
		else if ( queue_from.kind == origin::MIX )
			from_name = queue_from.index == 0 ? L"Your Mix" : L"On Repeat";

		source.add ( L"Playing from " ).add ( from_name );
		c.text ( L"Up Next", render::font::HEADING, math::vec2 ( x + 10.0f, y + 12.0f ), theme::ink );
		c.text ( source.text ( ), render::font::SMALL, math::vec2 ( x + width - 10.0f, y + 12.0f ), theme::ink_soft, render::align::RIGHT, width * 0.55f );

		row_style style;

		style.upcoming = true;
		style.albums = false;

		float end = draw_rows ( c, make_id ( "upcoming" ), upcoming, queue_from, x, y + 44.0f, width, style, interactive );

		if ( upcoming.empty ( ) ) {

			c.text ( repeat == repeat_mode::OFF ? L"Nothing queued after this song." : L"The queue starts over after this song.", render::font::BODY, math::vec2 ( x + 10.0f, y + 64.0f ), theme::ink_soft );
			end = y + 90.0f;

		}

		return end;

	}

	void app::draw_page ( render::canvas& c, location place, float offset, bool interactive ) {

		float x = content.x;
		float width = content.w;
		float y = 76.0f + top_inset - offset;
		float end = y;

		if ( interactive && !page_ready ( place ) ) {

			draw_loading ( c, place );
			content_height = h;
			scroll_limit = 0.0f;

			return;

		}

		float reveal = 1.0f;

		if ( interactive ) {

			float t = math::saturate ( static_cast < float > ( clock - gate_time ) / 0.45f );

			if ( t < 1.0f )
				animating = true;

			reveal = 1.0f - ( 1.0f - t ) * ( 1.0f - t ) * ( 1.0f - t );

		}

		c.push ( content.center ( ), 0.985f + 0.015f * reveal, reveal, math::vec2 ( 0.0f, 12.0f * ( 1.0f - reveal ) ) );

		switch ( place.where ) {

			case page::HOME:
				end = draw_home ( c, x, y, width, interactive );
				break;

			case page::MIX:

				if ( icon_button ( c, make_id ( "mix_back" ), math::vec2 ( x + 18.0f, y + 4.0f ), 36.0f, theme::glyph::BACK, 14.0f, theme::ink, L"Back" ) && interactive )
					navigate ( page::HOME );

				end = draw_header ( c, place, home_mix, { origin::MIX, 0 }, x, y + 34.0f, width, interactive );
				end = draw_rows ( c, make_id ( "mix_rows" ), home_mix, { origin::MIX, 0 }, x, end, width, row_style ( ), interactive );
				break;

			case page::SONGS:
				end = draw_header ( c, place, all_songs, { origin::SONGS, 0 }, x, y, width, interactive );
				end = draw_rows ( c, make_id ( "songs" ), all_songs, { origin::SONGS, 0 }, x, end, width, row_style ( ), interactive );
				break;

			case page::ALBUMS: {

				base::list < int32_t > albums;
				base::writer meta;

				for ( int32_t a = 0; a < library::album_count ( ); a++ )
					albums.push ( a );

				meta.number ( albums.count ( ) ).add ( albums.count ( ) == 1 ? L" album" : L" albums" );
				c.text ( L"LIBRARY", render::font::TINY, math::vec2 ( x + 2.0f, y + 14.0f ), theme::ink_soft );
				c.text ( L"Albums", render::font::HERO, math::vec2 ( x, y + 50.0f ), theme::ink );
				c.text ( meta.text ( ), render::font::SMALL, math::vec2 ( x + width - 6.0f, y + 54.0f ), theme::ink_soft, render::align::RIGHT );
				end = draw_album_grid ( c, albums, x, y + 100.0f, width, -1, interactive );
				break;

			}

			case page::ALBUM: {

				base::list < int32_t > list;
				row_style style;

				album_tracks ( place.album, list );
				style.covers = false;
				style.albums = false;
				style.numbers = true;
				end = draw_header ( c, place, list, { origin::ALBUM, place.album }, x, y, width, interactive );
				end = draw_rows ( c, make_id ( "album", place.album ), list, { origin::ALBUM, place.album }, x, end, width, style, interactive );
				break;

			}

			case page::ARTIST: {

				base::list < int32_t > list;

				artist_tracks ( place.album, list );
				end = draw_header ( c, place, list, { origin::ALBUM, place.album }, x, y, width, interactive );
				end = draw_rows ( c, make_id ( "artist", place.album ), list, { origin::ALBUM, place.album }, x, end, width, row_style ( ), interactive );
				break;

			}

			case page::LIKED:
				end = draw_header ( c, place, liked_list, { origin::LIKED, 0 }, x, y, width, interactive );
				end = draw_rows ( c, make_id ( "liked" ), liked_list, { origin::LIKED, 0 }, x, end, width, row_style ( ), interactive );

				if ( liked_list.empty ( ) ) {

					c.text ( L"Tap the heart on any song and it lands here.", render::font::BODY, math::vec2 ( x + 4.0f, end + 12.0f ), theme::ink_soft );
					end += 40.0f;

				}

				break;

			case page::PLAYLIST: {

				if ( place.playlist < 0 || place.playlist >= playlists.count ( ) )
					break;

				base::list < int32_t > list;
				row_style style;

				playlist_tracks ( place.playlist, list );
				style.playlist = place.playlist;
				end = draw_header ( c, place, list, { origin::PLAYLIST, place.playlist }, x, y, width, interactive );
				end = draw_rows ( c, make_id ( "playlist", static_cast < int64_t > ( playlists [ static_cast < size_t > ( place.playlist ) ].id ) ), list, { origin::PLAYLIST, place.playlist }, x, end, width, style, interactive );

				if ( list.empty ( ) ) {

					math::rect hint ( x, end, width, 92.0f );

					c.outline ( hint, 18.0f, render::color ( 0.0f, 0.0f, 0.0f, 0.0f ), 1.2f, theme::ink.fade ( 0.12f ) );
					c.text ( L"This playlist is empty", render::font::BODY_STRONG, math::vec2 ( hint.center ( ).x, hint.y + 36.0f ), theme::ink, render::align::CENTER );
					c.text ( L"Drag songs onto its pearl, or right-click a song and choose Add to playlist.", render::font::SMALL, math::vec2 ( hint.center ( ).x, hint.y + 58.0f ), theme::ink_soft, render::align::CENTER, width - 40.0f );
					end += 110.0f;

				}

				break;

			}

			case page::SEARCH:
				end = draw_search ( c, x, y, width, interactive );
				break;

			case page::DETAIL:
				end = draw_detail ( c, x, y, width, interactive );
				break;

			case page::PLAYLISTS: {

				base::writer meta;

				meta.number ( playlists.count ( ) ).add ( playlists.count ( ) == 1 ? L" playlist" : L" playlists" );

				if ( icon_button ( c, make_id ( "lists_back" ), math::vec2 ( x + 18.0f, y + 4.0f ), 36.0f, theme::glyph::BACK, 14.0f, theme::ink, L"Back" ) && interactive )
					navigate ( page::HOME );

				y += 34.0f;
				c.text ( L"COLLECTION", render::font::TINY, math::vec2 ( x + 2.0f, y + 14.0f ), theme::ink_soft );

				float title_w = c.text ( L"Your playlists", render::font::HERO, math::vec2 ( x, y + 50.0f ), theme::ink );

				c.text ( meta.text ( ), render::font::SMALL, math::vec2 ( x + title_w + 16.0f, y + 58.0f ), theme::ink_soft );
				end = draw_playlist_grid ( c, x, y + 100.0f, width, 0, interactive );
				break;

			}

			case page::NOW: {

				float side_x = stage.right ( ) + 36.0f;

				end = draw_lyrics ( c, side_x, y, content.right ( ) - side_x, interactive );
				break;

			}

		}

		c.pop ( );

		if ( interactive ) {

			content_height = end + offset + bottom_inset + 20.0f;
			scroll_limit = base::max ( content_height - h, 0.0f );

			if ( place.where == page::SEARCH && search_kind == 1 && remote_more && !remote_loading && scroll_limit - scroll_target < 900.0f ) {

				spotify::search_more ( );
				remote_loading = true;

			}

		}

	}

}