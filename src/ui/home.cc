#include "ui/app.hh"
#include <windows.h>

namespace ui {

	void app::ask_home ( bool force ) {

		if ( !remote_state.connected )
			return;

		base::writer liked;
		int32_t count = 0;

		for ( int32_t i = 0; i < library::saved_count ( ) && count < 40; i++ ) {

			int32_t index = library::saved + i;

			if ( !library::track_at ( index ).liked )
				continue;

			if ( liked.size ( ) )
				liked.add ( L" " );

			liked.add ( library::uri_of ( index ) );
			count++;

		}

		spotify::refresh_home ( liked.text ( ), force );
		home_asked = clock;

		if ( force ) {

			home_waiting = true;
			home_expect = home_view.finished + 1;

		}

	}

	void app::apply_tone ( int32_t album ) {

		library::album& target = library::album_at ( album );

		for ( const tone& item : tones ) {

			if ( item.key != target.key || !item.key )
				continue;

			for ( int32_t c = 0; c < 4; c++ )
				target.palette [ c ] = item.palette [ c ];

			target.accent = item.accent;
			target.painted = true;

			return;

		}

	}

	void app::ingest_home ( ) {

		if ( remote_state.waiting )
			home_login = true;

		if ( remote_state.connected && home_login && !remote_state.waiting ) {

			home_login = false;
			ask_home ( true );

		} else if ( remote_state.connected && home_asked < 0.0 )
			ask_home ( true );

		if ( live && persist && remote_state.connected && !remote_state.waiting && home_view.made > 0 && !home_view.history && !widen_asked ) {

			widen_asked = true;
			save_state ( );
			spotify::connect ( );

		}

		uint32_t revision = spotify::home_revision ( );

		if ( revision == home_seen )
			return;

		home_seen = revision;
		spotify::read_home ( home_view );

		const base::list < spotify::result >* sources [ 5 ] = { &home_view.mix, &home_view.repeat, &home_view.albums, &home_view.artists, &home_view.fresh };
		base::list < int32_t >* targets [ 5 ] = { &home_mix, &home_repeat, &home_albums, &home_artists, &home_fresh };
		int32_t firsts [ 5 ] = { 600, 630, 640, 652, 668 };
		int32_t rooms [ 5 ] = { 30, 10, 12, 16, 12 };

		for ( int32_t s = 0; s < 5; s++ ) {

			targets [ s ]->clear ( );

			for ( int32_t i = 0; i < sources [ s ]->count ( ) && i < rooms [ s ]; i++ ) {

				const spotify::result& entry = ( *sources [ s ] ) [ static_cast < size_t > ( i ) ];
				int32_t slot = firsts [ s ] + i;
				bool song = s < 2;
				int32_t year = 0;

				library::set_catalog ( slot, entry.uri, entry.title, entry.artist, song ? entry.album : entry.title, song ? static_cast < float > ( entry.duration ) : 0.0f, entry.art_url );

				for ( int32_t k = 0; k < 4 && !song && entry.album [ k ] >= L'0' && entry.album [ k ] <= L'9'; k++ )
					year = year * 10 + ( entry.album [ k ] - L'0' );

				library::album_at ( library::catalog + slot ).year = year;
				apply_tone ( library::catalog + slot );
				targets [ s ]->push ( library::catalog + slot );

			}

		}

		if ( home_waiting && home_view.finished >= home_expect )
			home_waiting = false;

		pending = base::max ( pending, 3 );

	}

	bool app::art_settled ( int32_t album, bool big ) {

		const library::album& item = library::album_at ( album );

		if ( item.cover >= 0 || !item.key || ( library::is_virtual ( album ) && !item.art_path [ 0 ] ) )
			return true;

		float appear = 0.0f;
		int32_t thumb = thumb_lookup ( item.key, appear );

		if ( thumb == -1 )
			return true;

		int32_t large = big ? art_lookup ( item.key, appear ) : 0;

		if ( thumb >= 0 && large != -2 )
			return true;

		if ( big && large == -2 ) {

			bool known = false;

			for ( uint64_t key : big_wanted )
				known = known || key == item.key;

			if ( !known )
				big_wanted.push ( item.key );

		}

		bool waiting = false;

		for ( uint64_t key : art_waiting )
			waiting = waiting || key == item.key;

		if ( !waiting )
			request_cover ( album );

		return false;

	}

	bool app::page_ready ( location place ) {

		uint32_t stamp = 0;
		bool data = true;

		switch ( place.where ) {

			case page::HOME:

				if ( remote_state.started && !remote_state.connected )
					return true;

				stamp = home_seen;
				data = remote_state.connected && home_asked >= 0.0 && !home_waiting && !home_view.loading;
				break;

			case page::MIX:
				stamp = home_seen;
				data = !home_view.loading && !home_waiting && ( home_asked >= 0.0 || ( remote_state.started && !remote_state.connected ) );
				break;

			case page::DETAIL:
				stamp = detail_seen;
				data = !detail_view.loading && !detail_stack.empty ( ) && base::same_text ( detail_view.uri, detail_stack.back ( ).get ( ) ) && detail_view.title [ 0 ];
				break;

			case page::LIKED:
				stamp = 1;
				break;

			case page::PLAYLIST:
				stamp = place.playlist >= 0 && place.playlist < playlists.count ( ) ? static_cast < uint32_t > ( playlists [ static_cast < size_t > ( place.playlist ) ].id ) : 2;
				break;

			default:
				return true;

		}

		bool same_place = gate_place.where == place.where && gate_place.playlist == place.playlist && gate_place.album == place.album && gate_stamp == stamp;

		if ( same_place && gate_open )
			return true;

		if ( !same_place ) {

			gate_place = place;
			gate_stamp = stamp;
			gate_open = false;
			gate_start = clock;

		}

		bool ready = data;

		auto rows = [ & ] ( const base::list < int32_t >& list, int32_t limit ) {

			for ( int32_t i = 0; i < list.count ( ) && i < limit; i++ )
				ready = art_settled ( library::track_at ( list [ static_cast < size_t > ( i ) ] ).album, false ) && ready;

		};

		auto tiles = [ & ] ( const base::list < int32_t >& list, int32_t limit ) {

			for ( int32_t i = 0; i < list.count ( ) && i < limit; i++ )
				ready = art_settled ( list [ static_cast < size_t > ( i ) ], true ) && ready;

		};

		if ( data ) {

			if ( place.where == page::HOME ) {

				for ( int32_t i = 0; i < home_mix.count ( ) && i < 6; i++ )
					ready = art_settled ( library::track_at ( home_mix [ static_cast < size_t > ( i ) ] ).album, i < 3 ) && ready;

				rows ( home_repeat, 5 );
				tiles ( home_albums, 12 );
				tiles ( home_artists, 16 );
				tiles ( home_fresh, 12 );

			} else if ( place.where == page::MIX )
				rows ( home_mix, 30 );
			else if ( place.where == page::DETAIL ) {

				ready = art_settled ( library::catalog + 540, true ) && ready;
				tiles ( detail_albums, 12 );

				if ( detail_view.kind == 2 )
					rows ( detail_tracks, 6 );

			} else if ( place.where == page::LIKED )
				rows ( liked_list, 16 );
			else if ( place.where == page::PLAYLIST ) {

				base::list < int32_t > list;

				playlist_tracks ( place.playlist, list );
				rows ( list, 16 );

			}

		}

		if ( ready || clock - gate_start > 12.0 ) {

			gate_open = true;
			gate_time = clock;

			if ( place.where == page::HOME )
				home_time = clock;

			return true;

		}

		return false;

	}

	void app::draw_loading ( render::canvas& c, location place ) {

		float y = 76.0f + top_inset;

		if ( place.where == page::DETAIL || place.where == page::MIX ) {

			if ( icon_button ( c, make_id ( place.where == page::DETAIL ? "detail_back" : "mix_back" ), math::vec2 ( content.x + 18.0f, y + 4.0f ), 36.0f, theme::glyph::BACK, 14.0f, theme::ink, L"Back" ) ) {

				if ( place.where == page::DETAIL )
					close_detail ( );
				else
					navigate ( page::HOME );

			}

		}

		float shown = math::saturate ( static_cast < float > ( clock - gate_start - 0.15 ) / 0.3f );
		math::vec2 middle ( content.center ( ).x, content.y + content.h * 0.42f );
		float turn = spin_phase ( 0.75 );
		float breathe = 0.5f + 0.5f * math::sin ( clock_wrapped ( ) * 2.4f );

		animating = true;

		if ( shown <= 0.0f )
			return;

		shown = shown * shown * ( 3.0f - 2.0f * shown );
		c.push ( middle, 0.9f + 0.1f * shown, shown );
		pearl_mark ( c, middle, 15.0f + 1.2f * breathe );
		c.ring ( middle, 30.0f, 2.2f, turn - math::floor ( turn ), 0.28f, theme::ink.fade ( 0.45f ) );
		c.pop ( );

	}

	float app::section_title ( render::canvas& c, const wchar_t* title, float x, float y, float width, const wchar_t* link, uint64_t id, bool interactive, bool& followed ) {

		followed = false;
		c.text ( title, render::font::HEADING, math::vec2 ( x, y + 12.0f ), theme::ink );

		if ( link ) {

			float link_w = c.measure ( link, render::font::SMALL_STRONG ) + 16.0f;
			math::rect area ( x + width - link_w, y - 2.0f, link_w, 28.0f );
			widget& link_state = state ( id );

			if ( interactive && interact ( id, area, link_state ) )
				followed = true;

			c.text ( link, render::font::SMALL_STRONG, math::vec2 ( x + width - 6.0f, y + 12.0f ), render::mix ( theme::ink_soft, theme::ink, link_state.hover ), render::align::RIGHT );

		}

		return y + 40.0f;

	}

	float app::draw_mix_card ( render::canvas& c, math::rect area, bool interactive ) {

		uint64_t id = make_id ( "home_mix_card" );
		widget& item = state ( id );
		bool open = interactive && interact ( id, area, item );
		bool context_now = queue_from.kind == origin::MIX && queue_from.index == 0 && track >= 0 && !library::is_remote ( track );
		bool pausing = context_now && playing;
		float reveal = interactive ? cascade ( 0 ) : 1.0f;
		bool flat = area.w > area.h * 1.4f;

		spring ( item.value, item.velocity, interactive && over ( area ) ? 1.0f : 0.0f, 240.0f, 22.0f );

		float lift = item.value;
		const library::album& lead = library::album_at ( library::track_at ( home_mix [ 0 ] ).album );
		const library::album& second = library::album_at ( library::track_at ( home_mix [ base::min ( 1, home_mix.count ( ) - 1 ) ] ).album );
		int32_t picks [ 3 ] = { -1, -1, -1 };
		uint64_t keys [ 3 ] = { };
		int32_t found = 0;

		for ( int32_t i = 0; i < home_mix.count ( ) && found < 3; i++ ) {

			int32_t album = library::track_at ( home_mix [ static_cast < size_t > ( i ) ] ).album;
			uint64_t key = library::album_at ( album ).key;
			bool known = false;

			for ( int32_t k = 0; k < found; k++ )
				known = known || keys [ k ] == key;

			if ( known )
				continue;

			keys [ found ] = key;
			picks [ found++ ] = album;

		}

		float frost_appear = 0.0f;
		int32_t frost = lead.key ? thumb_lookup ( lead.key, frost_appear ) : -1;

		c.push ( area.center ( ), ( 0.96f + 0.04f * reveal ) * ( 1.0f + 0.012f * lift ), reveal, math::vec2 ( 0.0f, 18.0f * ( 1.0f - reveal ) - 3.0f * lift ) );
		c.shadow ( area, 26.0f, 28.0f + 8.0f * lift, theme::shade.fade ( 0.14f + 0.06f * lift ), math::vec2 ( 0.0f, 12.0f + 3.0f * lift ) );
		c.gradient ( area, 26.0f, math::vec2 ( area.x, area.y ), math::vec2 ( area.right ( ), area.bottom ( ) ), render::mix ( lead.palette [ 1 ], theme::white, 0.55f ), render::mix ( second.palette [ 2 ], theme::white, 0.45f ) );

		if ( frost >= 0 && !flat )
			c.thumb ( area, 26.0f, frost, frost_appear );

		c.gradient ( area, 26.0f, math::vec2 ( 0.0f, area.y ), math::vec2 ( 0.0f, area.bottom ( ) ), theme::white.fade ( 0.56f ), theme::white.fade ( 0.72f ) );
		c.gradient ( math::rect ( area.x, area.y, area.w, area.h * 0.5f ), 26.0f, math::vec2 ( 0.0f, area.y ), math::vec2 ( 0.0f, area.y + area.h * 0.5f ), theme::white.fade ( 0.3f ), theme::white.fade ( 0.0f ) );
		c.outline ( area, 26.0f, render::color ( 0.0f, 0.0f, 0.0f, 0.0f ), 1.2f, theme::white.fade ( 0.75f ) );

		float side = flat ? base::min ( area.h - 52.0f, 122.0f ) : base::min ( area.w * 0.46f, 136.0f );
		math::vec2 fan = flat ? math::vec2 ( area.right ( ) - side * 1.05f - 24.0f, area.center ( ).y ) : math::vec2 ( area.center ( ).x, area.y + 26.0f + side * 0.55f );
		float spread = 1.0f + 0.35f * lift;
		int32_t layers [ 3 ] = { 1, 2, 0 };

		for ( int32_t layer : layers ) {

			if ( picks [ layer ] < 0 )
				continue;

			float direction = layer == 0 ? 0.0f : ( layer == 1 ? -1.0f : 1.0f );
			float angle = direction * 0.13f * spread;
			math::vec2 at = fan + math::vec2 ( direction * side * 0.36f * spread, layer == 0 ? -4.0f * lift : 6.0f );
			math::rect art ( at.x - side * 0.5f, at.y - side * 0.5f, side, side );
			float size_scale = layer == 0 ? 1.0f : 0.88f;

			c.push ( at, size_scale, 1.0f );
			c.shadow ( art, 14.0f, 16.0f, theme::shade.fade ( 0.2f ), math::vec2 ( 0.0f, 8.0f ) );
			cover ( c, art, 14.0f, picks [ layer ], 1.0f, angle );
			c.pop ( );

		}

		base::writer meta;

		meta.number ( home_mix.count ( ) ).add ( home_view.personal ? L" songs  \x00B7  new every day" : L" songs" );

		float text_x = area.x + 24.0f;
		float text_w = flat ? fan.x - side * 0.9f - text_x - 12.0f : area.w - 110.0f;
		float base_y = flat ? area.y + 86.0f : area.bottom ( ) - 30.0f;
		math::vec2 play_at = flat ? math::vec2 ( text_x + 25.0f, area.bottom ( ) - 38.0f ) : math::vec2 ( area.right ( ) - 44.0f, area.bottom ( ) - 44.0f );

		c.text ( home_view.personal ? L"MADE FOR YOU" : L"TRENDING", render::font::TINY, math::vec2 ( text_x + 1.0f, base_y - 54.0f ), theme::ink.fade ( 0.6f ) );
		c.text ( home_view.personal ? L"Your Mix" : L"Today\x2019s Hits", render::font::TITLE, math::vec2 ( text_x, base_y - 26.0f ), theme::ink, render::align::LEFT, text_w );
		c.text ( meta.text ( ), render::font::SMALL, math::vec2 ( text_x + 1.0f, base_y + 2.0f ), theme::ink.fade ( 0.62f ), render::align::LEFT, text_w );

		bool play = play_button ( c, id ^ 0x11, play_at, 25.0f, pausing, true, pausing ? L"Pause" : L"Play your mix" ) && interactive;

		c.pop ( );

		if ( play ) {

			if ( context_now )
				toggle_play ( );
			else
				play_virtual ( home_mix, 0, { origin::MIX, 0 } );

		} else if ( open )
			navigate ( page::MIX );

		return area.bottom ( );

	}

	float app::draw_home ( render::canvas& c, float x, float y, float width, bool interactive ) {

		SYSTEMTIME local = { };
		const wchar_t* days [ 7 ] = { L"SUNDAY", L"MONDAY", L"TUESDAY", L"WEDNESDAY", L"THURSDAY", L"FRIDAY", L"SATURDAY" };
		const wchar_t* months [ 12 ] = { L"JANUARY", L"FEBRUARY", L"MARCH", L"APRIL", L"MAY", L"JUNE", L"JULY", L"AUGUST", L"SEPTEMBER", L"OCTOBER", L"NOVEMBER", L"DECEMBER" };
		base::writer date;
		base::writer greeting;

		GetLocalTime ( &local );
		date.add ( days [ local.wDayOfWeek % 7 ] ).add ( L", " ).add ( months [ ( local.wMonth + 11 ) % 12 ] ).add ( L" " ).number ( local.wDay );
		greeting.add ( local.wHour < 5 ? L"Good night" : ( local.wHour < 12 ? L"Good morning" : ( local.wHour < 18 ? L"Good afternoon" : L"Good evening" ) ) );

		if ( remote_state.connected && remote_state.account [ 0 ] ) {

			wchar_t first [ 64 ];
			int32_t length = 0;

			while ( remote_state.account [ length ] && remote_state.account [ length ] != L' ' && length < 63 ) {

				first [ length ] = remote_state.account [ length ];
				length++;

			}

			first [ length ] = 0;
			greeting.add ( L", " ).add ( first );

		}

		c.text ( date.text ( ), render::font::TINY, math::vec2 ( x + 2.0f, y + 14.0f ), theme::ink_soft );
		c.text ( greeting.text ( ), render::font::HERO, math::vec2 ( x, y + 50.0f ), theme::ink, render::align::LEFT, width - 200.0f );

		float cy = y + 84.0f;

		if ( !remote_state.connected ) {

			math::rect card ( x, cy + 14.0f, base::min ( width, 720.0f ), 196.0f );

			c.shadow ( card, 26.0f, 26.0f, theme::shade.fade ( 0.1f ), math::vec2 ( 0.0f, 10.0f ) );
			c.fill ( card, 26.0f, theme::white.fade ( 0.62f ) );
			c.outline ( card, 26.0f, render::color ( 0.0f, 0.0f, 0.0f, 0.0f ), 1.0f, theme::white.fade ( 0.8f ) );
			pearl_mark ( c, math::vec2 ( card.x + 70.0f, card.center ( ).y - 4.0f ), 30.0f );
			c.text ( remote_state.waiting ? L"Approve Pearl in your browser" : L"Bring your music", render::font::TITLE, math::vec2 ( card.x + 128.0f, card.y + 58.0f ), theme::ink, render::align::LEFT, card.w - 150.0f );
			c.text ( L"Connect Spotify once and Pearl builds this page around what you play.", render::font::BODY, math::vec2 ( card.x + 129.0f, card.y + 92.0f ), theme::ink_soft, render::align::LEFT, card.w - 150.0f );

			if ( pill_button ( c, make_id ( "home_connect" ), math::rect ( card.x + 128.0f, card.y + 120.0f, 196.0f, 42.0f ), L"Connect Spotify", theme::glyph::LINK, -1.0f, true, false ) && interactive ) {

				spotify_open = true;
				menu = menu_kind::NONE;

			}

			return card.bottom ( ) + 30.0f;

		}

		bool loading = home_view.loading;
		math::vec2 refresh_at ( x + width - 20.0f, y + 50.0f );

		if ( loading ) {

			float turn = spin_phase ( 0.9 );

			c.ring ( refresh_at, 9.0f, 2.0f, turn - math::floor ( turn ), 0.28f, theme::ink.fade ( 0.55f ) );
			animating = true;

		} else if ( icon_button ( c, make_id ( "home_refresh" ), refresh_at, 36.0f, theme::glyph::REFRESH, 15.0f, theme::ink_soft, L"New picks" ) && interactive )
			ask_home ( true );

		if ( home_view.made > 0 ) {

			int64_t minutes = ( spotify::wall_seconds ( ) - home_view.made ) / 60;
			base::writer updated;

			if ( loading )
				updated.add ( L"Picking new music" );
			else if ( minutes < 1 )
				updated.add ( L"Updated just now" );
			else if ( minutes < 60 )
				updated.add ( L"Updated " ).number ( minutes ).add ( L" min ago" );
			else if ( minutes < 1440 )
				updated.add ( L"Updated " ).number ( minutes / 60 ).add ( L" hr ago" );
			else
				updated.add ( L"Updated " ).number ( minutes / 1440 ).add ( minutes / 1440 == 1 ? L" day ago" : L" days ago" );

			c.text ( updated.text ( ), render::font::SMALL, math::vec2 ( refresh_at.x - 26.0f, refresh_at.y ), theme::ink_soft, render::align::RIGHT );

		}

		cy += 18.0f;

		bool empty = home_mix.empty ( ) && home_albums.empty ( ) && home_fresh.empty ( );

		if ( empty ) {

			if ( loading || home_asked < 0.0 || clock - home_asked < 25.0 ) {

				float pulse = 0.5f + 0.5f * math::sin ( clock_wrapped ( ) * 3.2f );
				render::color block = theme::shade.fade ( 0.035f + 0.025f * pulse );
				float gap = 22.0f;
				int32_t columns = base::max ( 2, static_cast < int32_t > ( ( width + gap ) / ( 178.0f + gap ) ) );
				float cell = ( width - gap * static_cast < float > ( columns - 1 ) ) / static_cast < float > ( columns );

				c.fill ( math::rect ( x, cy, 170.0f, 22.0f ), 8.0f, block );
				c.fill ( math::rect ( x, cy + 40.0f, width >= 880.0f ? 300.0f : width, width >= 880.0f ? 280.0f : 168.0f ), 26.0f, block );

				for ( int32_t k = 0; k < 5 && width >= 880.0f; k++ ) {

					c.fill ( math::rect ( x + 324.0f, cy + 48.0f + static_cast < float > ( k ) * 56.0f, 40.0f, 40.0f ), 8.0f, block );
					c.fill ( math::rect ( x + 378.0f, cy + 56.0f + static_cast < float > ( k ) * 56.0f, ( width - 400.0f ) * 0.45f, 12.0f ), 6.0f, block );
					c.fill ( math::rect ( x + 378.0f, cy + 76.0f + static_cast < float > ( k ) * 56.0f, ( width - 400.0f ) * 0.25f, 10.0f ), 5.0f, block );

				}

				float grid_y = cy + 40.0f + ( width >= 880.0f ? 280.0f : 168.0f ) + 50.0f;

				c.fill ( math::rect ( x, grid_y, 190.0f, 22.0f ), 8.0f, block );

				for ( int32_t k = 0; k < columns; k++ )
					c.fill ( math::rect ( x + static_cast < float > ( k ) * ( cell + gap ), grid_y + 40.0f, cell, cell ), 16.0f, block );

				animating = true;

				return grid_y + 40.0f + cell + 40.0f;

			}

			c.text ( L"Your picks didn\x2019t load.", render::font::BODY_STRONG, math::vec2 ( x + 2.0f, cy + 14.0f ), theme::ink );
			c.text ( L"Check your connection, then try again.", render::font::BODY, math::vec2 ( x + 2.0f, cy + 38.0f ), theme::ink_soft );

			if ( pill_button ( c, make_id ( "home_retry" ), math::rect ( x, cy + 62.0f, 132.0f, 40.0f ), L"Try again", theme::glyph::REFRESH, -1.0f, true, false ) && interactive )
				ask_home ( true );

			return cy + 120.0f;

		}

		bool followed = false;

		if ( !home_mix.empty ( ) ) {

			bool wide = width >= 880.0f;
			row_style style;

			cy = section_title ( c, home_view.personal ? L"Made for you" : L"Popular right now", x, cy, width, L"See all", make_id ( "home_mix_all" ), interactive, followed );

			if ( followed )
				navigate ( page::MIX );

			math::rect card = wide ? math::rect ( x, cy, 300.0f, 280.0f ) : math::rect ( x, cy, width, 168.0f );

			draw_mix_card ( c, card, interactive );
			style.limit = 5;
			style.albums = false;

			float rows_end = draw_rows ( c, make_id ( "home_mix_rows" ), home_mix, { origin::MIX, 0 }, wide ? x + 324.0f : x, wide ? cy : card.bottom ( ) + 16.0f, wide ? width - 324.0f : width, style, interactive );

			cy = base::max ( card.bottom ( ), rows_end ) + 36.0f;

		}

		if ( !home_albums.empty ( ) ) {

			cy = section_title ( c, L"Albums for you", x, cy, width, nullptr, 0, interactive, followed );
			cy = draw_album_grid ( c, home_albums, x, cy, width, 1, interactive ) + 22.0f;

		}

		if ( home_repeat.count ( ) >= 3 ) {

			row_style style;

			style.limit = 5;
			cy = section_title ( c, L"On repeat", x, cy, width, nullptr, 0, interactive, followed );
			cy = draw_rows ( c, make_id ( "home_repeat_rows" ), home_repeat, { origin::MIX, 1 }, x, cy, width, style, interactive ) + 34.0f;

		}

		if ( !home_artists.empty ( ) ) {

			cy = section_title ( c, L"Artists for you", x, cy, width, nullptr, 0, interactive, followed );
			cy = draw_artist_grid ( c, home_artists, x, cy, width, 1, interactive ) + 18.0f;

		}

		if ( home_fresh.count ( ) >= 2 ) {

			cy = section_title ( c, home_view.personal ? L"New from artists you play" : L"New releases", x, cy, width, nullptr, 0, interactive, followed );
			cy = draw_album_grid ( c, home_fresh, x, cy, width, 1, interactive ) + 22.0f;

		}

		cy = section_title ( c, L"Your playlists", x, cy, width, playlists.count ( ) > 3 ? L"See all" : nullptr, make_id ( "home_lists_all" ), interactive, followed );

		if ( followed )
			navigate ( page::PLAYLISTS );

		cy = draw_playlist_grid ( c, x, cy, width, 1, interactive );

		return cy + 10.0f;

	}

}