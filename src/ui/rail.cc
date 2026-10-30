#include "ui/app.hh"
#include <windows.h>
#include <shellapi.h>

namespace ui {

	static constexpr float rail_x = 44.0f;
	static constexpr float pearl_r = 20.0f;
	static constexpr float pearl_gap = 54.0f;

	static float ease ( float value ) {

		float t = math::saturate ( value );

		return t * t * ( 3.0f - 2.0f * t );

	}

	void app::draw_rail ( render::canvas& c ) {

		struct pearl {

			uint64_t id;
			math::vec2 at;
			const wchar_t* label;
			const wchar_t* detail;
			theme::glyph icon;
			int32_t list;
			int32_t kind;
			bool active;
			int32_t album;
			int32_t recent;

		};

		float top_y = 98.0f;
		bool home_page = here.where == page::HOME || here.where == page::MIX || here.where == page::PLAYLISTS || here.where == page::SONGS || here.where == page::ALBUMS;
		pearl home_pearl = { make_id ( "rail_home" ), math::vec2 ( rail_x, top_y ), L"Home", nullptr, theme::glyph::HOME, -1, 0, home_page, -1, -1 };
		pearl liked_pearl = { make_id ( "rail_liked" ), math::vec2 ( rail_x, top_y + pearl_gap ), L"Liked Songs", L"Playlist", theme::glyph::HEART_FILL, -1, 2, here.where == page::LIKED, -1, -1 };
		float bottom_y = h - 44.0f;
		math::vec2 spotify_at ( rail_x, bottom_y );
		math::vec2 create_at ( rail_x, bottom_y - pearl_gap );
		float list_top = top_y + pearl_gap * 2.0f + 6.0f;
		float list_bottom = create_at.y - pearl_gap * 0.5f - 10.0f;
		base::list < pearl > shown;
		const wchar_t* top_uri = detail_stack.empty ( ) ? L"" : detail_stack.back ( ).get ( );

		for ( int32_t r = 0; r < recents.count ( ); r++ ) {

			const recent_item& item = recents [ static_cast < size_t > ( r ) ];
			pearl entry = { make_id ( "rail_recent", static_cast < int64_t > ( item.key ^ static_cast < uint64_t > ( item.kind ) ) ), math::vec2 ( rail_x, 0.0f ), item.title.get ( ), nullptr, theme::glyph::NONE, -1, 7, false, -1, r };

			if ( item.kind == recent_kind::LIKED )
				continue;

			if ( item.kind == recent_kind::SONG ) {

				int32_t t = lookup_track ( item.key );

				if ( t < 0 )
					continue;

				entry.album = library::track_at ( t ).album;
				entry.label = library::track_at ( t ).title;
				entry.detail = L"Song";

			} else if ( item.kind == recent_kind::ALBUM ) {

				int32_t a = library::find_album ( item.key );

				if ( a < 0 )
					continue;

				entry.album = a;
				entry.detail = L"Album";
				entry.active = here.where == page::ALBUM && here.album == a;

			} else if ( item.kind == recent_kind::PLAYLIST ) {

				int32_t p = -1;

				for ( int32_t k = 0; k < playlists.count ( ) && p < 0; k++ ) {

					if ( playlists [ static_cast < size_t > ( k ) ].id == item.key )
						p = k;

				}

				if ( p < 0 )
					continue;

				entry.kind = 3;
				entry.list = p;
				entry.label = playlists [ static_cast < size_t > ( p ) ].title.get ( );
				entry.detail = L"Playlist";
				entry.active = here.where == page::PLAYLIST && here.playlist == p;

			} else {

				if ( item.slot < 0 )
					continue;

				entry.album = library::catalog + item.slot;
				entry.detail = item.kind == recent_kind::REMOTE_ALBUM ? L"Album" : L"Artist";
				entry.active = here.where == page::DETAIL && base::same_text ( top_uri, item.uri.get ( ) );

			}

			shown.push ( entry );

		}

		float needed = static_cast < float > ( shown.count ( ) ) * pearl_gap;

		rail_limit = base::max ( needed - ( list_bottom - list_top ), 0.0f );

		float list_y = list_top + pearl_r + 4.0f - rail_scroll;
		float string_end = shown.empty ( ) ? liked_pearl.at.y : base::min ( list_y + static_cast < float > ( shown.count ( ) - 1 ) * pearl_gap, list_bottom - pearl_r );
		float marker_target = -1.0f;

		c.fill ( math::rect ( rail_x - 0.75f, top_y, 1.5f, string_end - top_y ), 0.75f, theme::ink.fade ( 0.09f ) );
		c.fill ( math::rect ( rail_x - 0.75f, create_at.y, 1.5f, spotify_at.y - create_at.y ), 0.75f, theme::ink.fade ( 0.09f ) );

		if ( track_drag )
			drop_target = -2;

		auto draw_pearl = [ & ] ( const pearl& item, float scale_extra ) {

			widget& state_item = state ( item.id );
			math::rect area ( item.at.x - pearl_r - 4.0f, item.at.y - pearl_r - 4.0f, pearl_r * 2.0f + 8.0f, pearl_r * 2.0f + 8.0f );
			bool clicked = interact ( item.id, area, state_item );
			bool dropping = track_drag && mouse_inside && area.contains ( mouse ) && ( item.kind == 2 || item.kind == 3 || item.kind == 4 );

			if ( clicked )
				state_item.pop_velocity += 7.0f;

			spring ( state_item.pop, state_item.pop_velocity, 0.0f, 420.0f, 20.0f );
			spring ( state_item.value, state_item.velocity, dropping ? 1.0f : 0.0f, 360.0f, 22.0f );
			approach ( state_item.wave, ( state_item.hover > 0.5f && !mouse_down ) || dropping ? 1.0f : 0.0f, 16.0f );

			if ( dropping )
				drop_target = item.kind == 2 ? -4 : ( item.kind == 3 ? item.list : -3 );

			float scale = ( 1.0f + 0.07f * state_item.hover - 0.06f * state_item.press + 0.16f * state_item.pop + 0.16f * state_item.value ) * scale_extra;
			math::rect disc_area ( item.at.x - pearl_r, item.at.y - pearl_r, pearl_r * 2.0f, pearl_r * 2.0f );

			c.push ( item.at, scale, 1.0f );
			c.shadow ( disc_area, pearl_r, 10.0f, theme::shade.fade ( 0.14f + 0.06f * state_item.hover ), math::vec2 ( 0.0f, 4.0f ) );

			if ( item.kind == 3 || item.kind == 7 ) {

				if ( item.kind == 7 )
					cover ( c, disc_area, pearl_r, item.album );
				else {

					base::list < int32_t > tracks;
					const playlist& source = playlists [ static_cast < size_t > ( item.list ) ];

					playlist_tracks ( item.list, tracks );

					if ( tracks.empty ( ) ) {

						render::color palette [ 4 ];
						render::color accent;
						wchar_t initial [ 2 ] = { source.title.get ( ) [ 0 ] ? source.title.get ( ) [ 0 ] : L'P', 0 };

						library::placeholder_palette ( source.id * 0x9E3779B97F4A7C15ull + 3, palette, accent );
						c.gradient ( disc_area, pearl_r, math::vec2 ( disc_area.x, disc_area.y ), math::vec2 ( disc_area.right ( ), disc_area.bottom ( ) ), palette [ 1 ], palette [ 3 ] );
						c.text ( initial, render::font::BODY_STRONG, item.at, theme::white, render::align::CENTER );

					} else
						cover ( c, disc_area, pearl_r, library::track_at ( tracks [ 0 ] ).album );

				}

				c.gradient ( disc_area, pearl_r, math::vec2 ( 0.0f, disc_area.y ), math::vec2 ( 0.0f, disc_area.bottom ( ) ), theme::white.fade ( 0.34f ), theme::white.fade ( 0.0f ) );
				c.outline ( disc_area, pearl_r, render::color ( 0.0f, 0.0f, 0.0f, 0.0f ), 1.2f, theme::white.fade ( 0.8f ) );

			} else {

				c.glass ( disc_area, pearl_r, theme::control ( ) );

				if ( item.kind == 4 ) {

					c.fill ( math::rect ( item.at.x - 6.5f, item.at.y - 0.8f, 13.0f, 1.6f ), 0.8f, theme::ink );
					c.fill ( math::rect ( item.at.x - 0.8f, item.at.y - 6.5f, 1.6f, 13.0f ), 0.8f, theme::ink );

				} else if ( item.kind == 2 )
					heart_shape ( c, item.at, 15.0f, item.active || dropping ? 1.0f : 0.0f );
				else
					c.icon ( theme::code ( item.icon ), item.at, 15.0f, theme::ink );

			}

			if ( state_item.value > 0.01f )
				c.ring ( item.at, pearl_r + 5.0f, 2.0f, 0.0f, 1.0f, theme::ink.fade ( 0.55f * state_item.value ) );

			c.pop ( );

			if ( item.active && item.kind != 5 )
				marker_target = item.at.y;

			if ( item.kind == 5 && spotify_show > 0.01f )
				c.ring ( item.at, pearl_r + 6.0f, 1.8f, 0.0f, 1.0f, theme::ink.fade ( 0.72f * ease ( spotify_show ) ) );

			float label = ease ( state_item.wave );

			if ( label > 0.01f && item.label ) {

				float title_w = base::min ( c.measure ( item.label, render::font::BODY_STRONG ), 220.0f );
				float detail_w = item.detail ? c.measure ( item.detail, render::font::SMALL ) + 10.0f : 0.0f;
				float label_w = title_w + detail_w + 28.0f;
				math::rect bubble ( item.at.x + pearl_r + 14.0f, item.at.y - 17.0f, label_w, 34.0f );

				c.push ( math::vec2 ( bubble.x, bubble.center ( ).y ), 0.9f + 0.1f * label, label, math::vec2 ( -8.0f * ( 1.0f - label ), 0.0f ) );
				c.shadow ( bubble, 17.0f, 12.0f, theme::shade.fade ( 0.14f ), math::vec2 ( 0.0f, 4.0f ) );
				c.glass ( bubble, 17.0f, theme::bubble ( ) );
				c.text ( item.label, render::font::BODY_STRONG, math::vec2 ( bubble.x + 14.0f, bubble.center ( ).y ), theme::ink, render::align::LEFT, title_w + 2.0f );

				if ( item.detail )
					c.text ( item.detail, render::font::SMALL, math::vec2 ( bubble.x + 14.0f + title_w + 10.0f, bubble.center ( ).y ), theme::ink_soft );

				c.pop ( );

			}

			if ( right_id == item.id && item.kind == 3 )
				open_menu ( menu_kind::PLAYLIST, -1, item.list, { origin::PLAYLIST, item.list }, item.at + math::vec2 ( pearl_r + 10.0f, -10.0f ) );

			return clicked;

		};

		if ( draw_pearl ( home_pearl, 1.0f ) )
			navigate ( page::HOME );

		if ( draw_pearl ( liked_pearl, 1.0f ) )
			navigate ( page::LIKED );

		c.push_clip ( math::rect ( 0.0f, list_top - 10.0f, rail.w + 300.0f, list_bottom - list_top + 20.0f ) );
		c.snap_origin = math::vec2 ( 0.0f, -rail_scroll * dpi );

		for ( int32_t i = 0; i < shown.count ( ); i++ ) {

			pearl item = shown [ static_cast < size_t > ( i ) ];
			float y = list_y + static_cast < float > ( i ) * pearl_gap;

			if ( y < list_top - pearl_gap || y > list_bottom + pearl_gap )
				continue;

			float edge = math::saturate ( base::min ( y - ( list_top - 6.0f ), list_bottom + 6.0f - y ) / 24.0f );

			item.at.y = y;
			c.push ( item.at, 1.0f, edge );

			if ( draw_pearl ( item, 0.82f + 0.18f * edge ) ) {

				const recent_item& recent = recents [ static_cast < size_t > ( item.recent ) ];

				if ( item.kind == 3 )
					navigate ( page::PLAYLIST, item.list );
				else if ( recent.kind == recent_kind::SONG ) {

					base::list < int32_t > single;
					int32_t t = lookup_track ( recent.key );

					single.push ( t );
					play_list ( single, 0, context ( ) );

				} else if ( recent.kind == recent_kind::ALBUM )
					navigate ( page::ALBUM, item.album );
				else
					open_detail ( item.album, recent.kind == recent_kind::REMOTE_ALBUM ? 1 : 2 );

			}

			c.pop ( );

		}

		c.snap_origin = math::vec2 ( 0.0f, 0.0f );
		c.pop_clip ( );

		pearl create_pearl = { make_id ( "rail_new" ), create_at, L"New playlist", nullptr, theme::glyph::ADD, -1, 4, false, -1, -1 };
		pearl spotify_pearl = { make_id ( "rail_spotify" ), spotify_at, remote_state.connected ? L"Spotify" : L"Connect Spotify", nullptr, theme::glyph::LINK, -1, 5, spotify_open, -1, -1 };

		if ( draw_pearl ( create_pearl, 1.0f ) )
			create_playlist ( nullptr );

		if ( draw_pearl ( spotify_pearl, 1.0f ) ) {

			spotify_open = !spotify_open;
			menu = menu_kind::NONE;

		}

		spotify_anchor = spotify_at;

		if ( remote_state.connected || remote_state.waiting ) {

			math::vec2 dot = spotify_at + math::vec2 ( 14.0f, 14.0f );
			float pulse = remote_state.waiting ? 0.55f + 0.45f * math::sin ( clock_wrapped ( ) * 5.0f ) : 1.0f;

			c.circle ( dot, 5.5f, theme::white );
			c.circle ( dot, 3.8f, ( remote_state.waiting ? render::rgb ( 0xFF9F0A ) : render::rgb ( 0x34C759 ) ).fade ( pulse ) );

			if ( remote_state.waiting )
				animating = true;

		}

		if ( marker_target >= 0.0f ) {

			if ( !rail_marker_ready ) {

				rail_marker = marker_target;
				rail_marker_ready = true;

			}

			spring ( rail_marker, rail_marker_velocity, marker_target, 320.0f, 26.0f );

			float stretch = base::min ( math::abs ( rail_marker_velocity ) * 0.012f, 10.0f );
			math::rect ring_area ( rail_x - pearl_r - 6.0f, rail_marker - pearl_r - 6.0f - stretch * 0.5f, pearl_r * 2.0f + 12.0f, pearl_r * 2.0f + 12.0f + stretch );

			c.outline ( ring_area, pearl_r + 6.0f, render::color ( 0.0f, 0.0f, 0.0f, 0.0f ), 1.8f, theme::ink.fade ( 0.72f ) );

		}

	}

	void app::open_menu ( menu_kind kind, int32_t index, int32_t row, context from, math::vec2 at ) {

		if ( kind == menu_kind::NONE )
			return;

		bool fresh = menu == menu_kind::NONE;

		menu = kind;
		menu_shown = kind;
		menu_track = index;
		menu_row = row;
		menu_list = kind == menu_kind::PLAYLIST ? row : ( from.kind == origin::PLAYLIST ? from.index : -1 );
		menu_from = from;
		menu_at = at;
		spotify_open = false;

		if ( fresh ) {

			menu_show = 0.0f;
			menu_velocity = 0.0f;

		}

		pending = base::max ( pending, 3 );

	}

	bool app::menu_item ( render::canvas& c, uint64_t id, math::rect area, const wchar_t* label, theme::glyph icon, bool danger ) {

		widget& item = state ( id );
		bool clicked = interact ( id, area, item );
		render::color tint = danger ? theme::danger : theme::ink;

		c.fill ( area, 9.0f, ( danger ? theme::danger : theme::shade ).fade ( ( danger ? 0.1f : 0.055f ) * item.hover + 0.04f * item.press ) );

		if ( icon != theme::glyph::NONE )
			c.icon ( theme::code ( icon ), math::vec2 ( area.x + 20.0f, area.center ( ).y ), 13.0f, tint.fade ( danger ? 1.0f : 0.75f ) );

		c.text ( label, render::font::BODY, math::vec2 ( area.x + 40.0f, area.center ( ).y ), tint, render::align::LEFT, area.w - 52.0f );

		return clicked;

	}

	void app::draw_menu ( render::canvas& c ) {

		spring ( menu_show, menu_velocity, menu != menu_kind::NONE ? 1.0f : 0.0f, 420.0f, 30.0f );

		if ( menu == menu_kind::NONE && menu_show <= 0.002f ) {

			menu_shown = menu_kind::NONE;
			menu_rect = math::rect ( -10.0f, -10.0f, 0.0f, 0.0f );

			return;

		}

		if ( menu != menu_kind::NONE )
			menu_shown = menu;

		const library::track& item = library::track_at ( menu_track );
		float item_h = 34.0f;
		float width = 248.0f;
		int32_t rows = 0;
		bool in_playlist = menu_from.kind == origin::PLAYLIST && menu_list >= 0;

		bool virtual_track = library::is_virtual ( menu_track );
		bool menu_liked = library::is_catalog ( menu_track ) ? library::liked_key ( item.key ) : item.liked;

		if ( menu_shown == menu_kind::TRACK )
			rows = ( virtual_track ? 4 : 5 ) + ( in_playlist ? 1 : 0 );
		else if ( menu_shown == menu_kind::ADD )
			rows = 1 + base::min ( playlists.count ( ), 8 );
		else
			rows = 3;

		float header = menu_shown == menu_kind::PLAYLIST ? 0.0f : 50.0f;
		float height_total = header + static_cast < float > ( rows ) * item_h + 12.0f + ( menu_shown == menu_kind::TRACK && ( !virtual_track || in_playlist ) ? 9.0f : 0.0f );
		math::vec2 at = menu_at;

		at.x = base::clamp ( at.x, 8.0f, w - width - 8.0f );

		if ( at.y + height_total > h - 8.0f )
			at.y = base::max ( at.y - height_total, 8.0f );

		menu_rect = math::rect ( at.x, at.y, width, height_total );

		float shown = ease ( menu_show );

		if ( press_frame == frame_index && !menu_rect.contains ( mouse ) && menu != menu_kind::NONE )
			menu = menu_kind::NONE;

		c.push ( math::vec2 ( at.x + 20.0f, at.y + 12.0f ), 0.92f + 0.08f * shown, shown );
		c.shadow ( menu_rect, 16.0f, 26.0f, theme::shade.fade ( 0.2f ), math::vec2 ( 0.0f, 10.0f ) );
		c.shadow ( menu_rect, 16.0f, 3.0f, theme::shade.fade ( 0.08f ), math::vec2 ( 0.0f, 1.0f ) );
		c.glass ( menu_rect, 16.0f, theme::bubble ( ) );
		block ( menu_rect );

		float x = at.x + 6.0f;
		float y = at.y + 6.0f;
		float inner = width - 12.0f;
		bool active = menu != menu_kind::NONE && menu_show > 0.6f;
		bool close = false;

		if ( menu_shown != menu_kind::PLAYLIST ) {

			math::rect thumb ( x + 6.0f, y + 6.0f, 32.0f, 32.0f );

			if ( menu_track >= 0 )
				cover ( c, thumb, 7.0f, item.album );

			c.text ( item.title, render::font::BODY_STRONG, math::vec2 ( x + 48.0f, y + 15.0f ), theme::ink, render::align::LEFT, inner - 56.0f );
			c.text ( library::album_at ( item.album ).artist, render::font::SMALL, math::vec2 ( x + 48.0f, y + 31.0f ), theme::ink_soft, render::align::LEFT, inner - 56.0f );
			c.fill ( math::rect ( x + 8.0f, y + header - 6.0f, inner - 16.0f, 1.0f ), 0.5f, theme::shade.fade ( 0.07f ) );
			y += header;

		}

		if ( menu_shown == menu_kind::TRACK ) {

			bool current = menu_track == track || ( track >= 0 && virtual_track && library::track_at ( track ).key == item.key );
			const wchar_t* play_label = current && playing ? L"Pause" : L"Play";

			if ( menu_item ( c, make_id ( "menu_play" ), math::rect ( x, y, inner, item_h ), play_label, theme::glyph::NONE, false ) && active ) {

				if ( current )
					toggle_play ( );
				else if ( library::is_catalog ( menu_track ) && menu_from.kind == origin::SEARCH && menu_from.index == 1 )
					play_catalog ( menu_track );
				else if ( library::is_catalog ( menu_track ) ) {

					base::list < int32_t > single;

					single.push ( menu_track );
					play_virtual ( single, 0, context ( ) );

				} else {

					base::list < int32_t > single;

					single.push ( menu_track );
					play_list ( single, 0, context ( ) );

				}

				close = true;

			}

			play_shape ( c, math::vec2 ( x + 21.0f, y + item_h * 0.5f ), 12.0f, current && playing ? 1.0f : 0.0f, theme::ink.fade ( 0.75f ) );
			y += item_h;

			if ( menu_item ( c, make_id ( "menu_next" ), math::rect ( x, y, inner, item_h ), L"Play next", theme::glyph::NEXT_UP, false ) && active ) {

				play_next ( menu_track );
				close = true;

			}

			y += item_h;

			if ( menu_item ( c, make_id ( "menu_add" ), math::rect ( x, y, inner, item_h ), L"Add to playlist", theme::glyph::ADD, false ) && active ) {

				menu = menu_kind::ADD;
				menu_shown = menu_kind::ADD;

			}

			c.icon ( theme::code ( theme::glyph::CHEVRON_RIGHT ), math::vec2 ( x + inner - 16.0f, y + item_h * 0.5f ), 10.0f, theme::ink_soft );
			y += item_h;

			if ( menu_item ( c, make_id ( "menu_like" ), math::rect ( x, y, inner, item_h ), menu_liked ? L"Remove from Liked Songs" : L"Add to Liked Songs", theme::glyph::NONE, false ) && active ) {

				like_index ( menu_track, !menu_liked );
				close = true;

			}

			heart_shape ( c, math::vec2 ( x + 20.0f, y + item_h * 0.5f ), 13.0f, menu_liked ? 1.0f : 0.0f );
			y += item_h;

			if ( !virtual_track || in_playlist ) {

				c.fill ( math::rect ( x + 8.0f, y + 4.0f, inner - 16.0f, 1.0f ), 0.5f, theme::shade.fade ( 0.07f ) );
				y += 9.0f;

			}

			if ( !virtual_track ) {

				if ( menu_item ( c, make_id ( "menu_album" ), math::rect ( x, y, inner, item_h ), L"Go to album", theme::glyph::ALBUM, false ) && active ) {

					navigate ( page::ALBUM, item.album );
					close = true;

				}

				y += item_h;

			}

			if ( in_playlist ) {

				if ( menu_item ( c, make_id ( "menu_remove" ), math::rect ( x, y, inner, item_h ), L"Remove from this playlist", theme::glyph::REMOVE, true ) && active ) {

					remove_from_playlist ( menu_list, menu_track );
					close = true;

				}

				y += item_h;

			}

		} else if ( menu_shown == menu_kind::ADD ) {

			if ( menu_item ( c, make_id ( "menu_new" ), math::rect ( x, y, inner, item_h ), L"New playlist", theme::glyph::ADD, false ) && active ) {

				int32_t song = menu_track;
				int32_t made = create_playlist ( nullptr );

				add_to_playlist ( made, song );
				close = true;

			}

			y += item_h;

			for ( int32_t p = 0; p < base::min ( playlists.count ( ), 8 ); p++ ) {

				const playlist& list = playlists [ static_cast < size_t > ( p ) ];

				if ( menu_item ( c, make_id ( "menu_list", static_cast < int64_t > ( list.id ) ), math::rect ( x, y, inner, item_h ), list.title.get ( ), theme::glyph::NONE, false ) && active ) {

					add_to_playlist ( p, menu_track );
					close = true;

				}

				render::color palette [ 4 ];
				render::color accent;

				library::placeholder_palette ( list.id * 0x9E3779B97F4A7C15ull + 3, palette, accent );
				c.circle ( math::vec2 ( x + 20.0f, y + item_h * 0.5f ), 7.0f, palette [ 1 ] );
				c.circle ( math::vec2 ( x + 18.5f, y + item_h * 0.5f - 2.0f ), 2.5f, theme::white.fade ( 0.6f ) );
				y += item_h;

			}

		} else {

			if ( menu_item ( c, make_id ( "menu_list_play" ), math::rect ( x, y, inner, item_h ), L"Play", theme::glyph::NONE, false ) && active ) {

				base::list < int32_t > tracks;

				playlist_tracks ( menu_list, tracks );

				if ( !tracks.empty ( ) )
					play_list ( tracks, 0, { origin::PLAYLIST, menu_list } );

				close = true;

			}

			play_shape ( c, math::vec2 ( x + 21.0f, y + item_h * 0.5f ), 12.0f, 0.0f, theme::ink.fade ( 0.75f ) );
			y += item_h;

			if ( menu_item ( c, make_id ( "menu_list_rename" ), math::rect ( x, y, inner, item_h ), L"Rename", theme::glyph::EDIT, false ) && active ) {

				navigate ( page::PLAYLIST, menu_list );
				title_list = menu_list;
				focus_field ( make_id ( "title" ), title_field, playlists [ static_cast < size_t > ( menu_list ) ].title.get ( ), true );
				close = true;

			}

			y += item_h;

			if ( menu_item ( c, make_id ( "menu_list_delete" ), math::rect ( x, y, inner, item_h ), L"Delete playlist", theme::glyph::TRASH, true ) && active ) {

				delete_playlist ( menu_list );
				close = true;

			}

		}

		c.pop ( );

		if ( close )
			menu = menu_kind::NONE;

	}

	void app::draw_drag ( render::canvas& c ) {

		if ( !track_drag ) {

			approach ( drag_show, 0.0f, 18.0f );

			return;

		}

		if ( !mouse_down ) {

			track_drag = false;

			if ( drop_target >= 0 )
				add_to_playlist ( drop_target, drag_track );
			else if ( drop_target == -3 ) {

				int32_t song = drag_track;
				int32_t made = create_playlist ( nullptr );

				add_to_playlist ( made, song );

			} else if ( drop_target == -4 ) {

				like_index ( drag_track, true );
				show_toast ( L"Added to Liked Songs" );

			}

			drop_target = -2;

			return;

		}

		approach ( drag_show, 1.0f, 18.0f );
		drag_at = mouse;

		const library::track& item = library::track_at ( drag_track );
		float shown = ease ( drag_show );
		float title_w = base::min ( c.measure ( item.title, render::font::BODY_STRONG ), 200.0f );
		math::rect pill ( drag_at.x + 14.0f, drag_at.y - 22.0f, title_w + 66.0f, 44.0f );

		c.push ( math::vec2 ( pill.x, pill.center ( ).y ), 0.85f + 0.15f * shown, shown );
		c.shadow ( pill, 22.0f, 18.0f, theme::shade.fade ( 0.22f ), math::vec2 ( 0.0f, 8.0f ) );
		c.glass ( pill, 22.0f, theme::bubble ( ) );
		cover ( c, math::rect ( pill.x + 6.0f, pill.y + 6.0f, 32.0f, 32.0f ), 16.0f, item.album );
		c.text ( item.title, render::font::BODY_STRONG, math::vec2 ( pill.x + 48.0f, pill.center ( ).y ), theme::ink, render::align::LEFT, title_w + 2.0f );
		c.pop ( );
		animating = true;

	}

	void app::draw_toast ( render::canvas& c ) {

		float age = static_cast < float > ( clock - toast_time );

		if ( age < 0.0f || age > 2.6f || !toast_text [ 0 ] )
			return;

		float shown = age < 0.18f ? ease ( age / 0.18f ) : ( age > 2.2f ? 1.0f - ease ( ( age - 2.2f ) / 0.4f ) : 1.0f );
		float text_w = base::min ( c.measure ( toast_text, render::font::BODY_STRONG ), w - 200.0f );
		float base_y = dock <= 2 && track >= 0 ? capsule.y - 34.0f : h - 46.0f;
		math::vec2 center ( content.center ( ).x, base_y );
		math::rect bubble ( center.x - text_w * 0.5f - 20.0f, center.y - 19.0f, text_w + 40.0f, 38.0f );

		c.push ( center, 0.9f + 0.1f * shown, shown, math::vec2 ( 0.0f, 10.0f * ( 1.0f - shown ) ) );
		c.shadow ( bubble, 19.0f, 18.0f, theme::shade.fade ( 0.2f ), math::vec2 ( 0.0f, 6.0f ) );
		c.fill ( bubble, 19.0f, render::rgb ( 0x1D1D1F, 0.92f ) );
		c.text ( toast_text, render::font::BODY_STRONG, math::vec2 ( bubble.x + 20.0f, center.y ), theme::white, render::align::LEFT, text_w + 2.0f );
		c.pop ( );
		animating = true;

	}

	void app::draw_spotify ( render::canvas& c ) {

		spring ( spotify_show, spotify_velocity, spotify_open ? 1.0f : 0.0f, 360.0f, 28.0f );

		if ( spotify_show <= 0.002f && !spotify_open ) {

			spotify_panel = math::rect ( -10.0f, -10.0f, 0.0f, 0.0f );

			return;

		}

		const spotify::snapshot& s = remote_state;
		float width = 344.0f;
		float height_total = !s.configured ? 300.0f : ( s.connected ? 228.0f : 168.0f );

		if ( s.message [ 0 ] )
			height_total += 30.0f;

		math::rect panel ( spotify_anchor.x + pearl_r + 16.0f, base::max ( spotify_anchor.y + 30.0f - height_total, 70.0f ), width, height_total );
		float shown = ease ( spotify_show );
		bool active = spotify_open && spotify_show > 0.6f;

		spotify_panel = panel;

		if ( press_frame == frame_index && spotify_open && !panel.contains ( mouse ) && math::distance ( mouse, spotify_anchor ) > pearl_r + 6.0f )
			spotify_open = false;

		c.push ( math::vec2 ( panel.x, panel.bottom ( ) - 24.0f ), 0.92f + 0.08f * shown, shown, math::vec2 ( -10.0f * ( 1.0f - shown ), 0.0f ) );
		c.shadow ( panel, 22.0f, 30.0f, theme::shade.fade ( 0.2f ), math::vec2 ( 0.0f, 12.0f ) );
		c.shadow ( panel, 22.0f, 3.0f, theme::shade.fade ( 0.08f ), math::vec2 ( 0.0f, 1.0f ) );
		c.glass ( panel, 22.0f, theme::bubble ( ) );
		block ( panel );

		float x = panel.x + 24.0f;
		float y = panel.y + 34.0f;
		float inner = width - 48.0f;
		wchar_t clip [ 256 ];

		c.text ( L"Spotify", render::font::HEADING, math::vec2 ( x, y ), theme::ink );
		y += 30.0f;

		if ( !s.configured ) {

			c.text ( L"Control Spotify Premium from Pearl.", render::font::BODY, math::vec2 ( x, y ), theme::ink_soft, render::align::LEFT, inner );
			y += 30.0f;
			c.text ( L"1  Create an app on the Spotify dashboard", render::font::SMALL, math::vec2 ( x, y ), theme::ink, render::align::LEFT, inner );
			y += 22.0f;
			c.text ( L"2  Add this redirect URI and tick Web API", render::font::SMALL, math::vec2 ( x, y ), theme::ink, render::align::LEFT, inner );
			y += 18.0f;

			math::rect uri ( x, y, inner, 34.0f );
			uint64_t copy_id = make_id ( "spotify_copy" );
			widget& copy_state = state ( copy_id );

			if ( interact ( copy_id, uri, copy_state ) && active ) {

				write_clipboard ( L"http://127.0.0.1:43821/callback" );
				show_toast ( L"Redirect URI copied" );

			}

			tip ( copy_id, L"Copy", uri );
			c.fill ( uri, 10.0f, theme::shade.fade ( 0.05f + 0.03f * copy_state.hover ) );
			c.text ( L"http://127.0.0.1:43821/callback", render::font::DIGITS, math::vec2 ( uri.x + 12.0f, uri.center ( ).y ), theme::ink, render::align::LEFT, inner - 40.0f );
			c.icon ( theme::code ( theme::glyph::COPY ), math::vec2 ( uri.right ( ) - 18.0f, uri.center ( ).y ), 13.0f, theme::ink_soft );
			y += 52.0f;
			c.text ( L"3  Copy its Client ID, then paste it here", render::font::SMALL, math::vec2 ( x, y ), theme::ink, render::align::LEFT, inner );
			y += 24.0f;

			if ( pill_button ( c, make_id ( "spotify_dashboard" ), math::rect ( x, y, inner * 0.5f - 5.0f, 38.0f ), L"Dashboard", theme::glyph::GLOBE, -1.0f, false, false ) && active )
				ShellExecuteW ( nullptr, L"open", L"https://developer.spotify.com/dashboard", nullptr, nullptr, SW_SHOWNORMAL );

			if ( pill_button ( c, make_id ( "spotify_client" ), math::rect ( x + inner * 0.5f + 5.0f, y, inner * 0.5f - 5.0f, 38.0f ), L"Paste ID", theme::glyph::NONE, -1.0f, true, false ) && active ) {

				int32_t length = 0;
				bool valid = read_clipboard ( clip, 256 );

				while ( valid && clip [ length ] ) {

					wchar_t ch = clip [ length ];

					valid = ( ch >= L'0' && ch <= L'9' ) || ( ch >= L'a' && ch <= L'f' ) || ( ch >= L'A' && ch <= L'F' );
					length++;

				}

				if ( valid && length == 32 ) {

					spotify::set_client ( clip );
					show_toast ( L"Client ID saved" );

				} else
					show_toast ( L"Copy the 32-character Client ID first" );

			}

			y += 38.0f;

		} else if ( s.waiting ) {

			float turn = spin_phase ( 0.9 );

			c.ring ( math::vec2 ( x + 10.0f, y + 2.0f ), 9.0f, 2.2f, turn - math::floor ( turn ), 0.3f, theme::ink.fade ( 0.7f ) );
			c.text ( L"Approve Pearl in your browser", render::font::BODY, math::vec2 ( x + 30.0f, y + 2.0f ), theme::ink, render::align::LEFT, inner - 30.0f );
			animating = true;
			y += 36.0f;

			if ( pill_button ( c, make_id ( "spotify_cancel" ), math::rect ( x, y, inner, 38.0f ), L"Cancel", theme::glyph::NONE, -1.0f, false, false ) && active )
				spotify::cancel ( );

			y += 38.0f;

		} else if ( !s.connected ) {

			c.text ( L"Sign in once and Pearl remembers you.", render::font::BODY, math::vec2 ( x, y ), theme::ink_soft, render::align::LEFT, inner );
			y += 26.0f;

			if ( pill_button ( c, make_id ( "spotify_connect" ), math::rect ( x, y, inner, 40.0f ), L"Connect", theme::glyph::LINK, -1.0f, true, false ) && active )
				spotify::connect ( );

			y += 48.0f;

			math::rect change ( x, y, inner, 22.0f );
			uint64_t change_id = make_id ( "spotify_change" );
			widget& change_state = state ( change_id );

			if ( interact ( change_id, change, change_state ) && active )
				spotify::set_client ( L"" );

			c.text ( L"Use a different Client ID", render::font::SMALL, math::vec2 ( x + inner * 0.5f, y + 8.0f ), render::mix ( theme::ink_soft, theme::ink, change_state.hover ), render::align::CENTER );
			y += 20.0f;

		} else {

			base::writer who;
			base::writer where;

			who.add ( L"Signed in as " ).add ( s.account [ 0 ] ? s.account : L"your account" );
			where.add ( s.device [ 0 ] ? s.device : L"No device playing yet" );
			c.text ( who.text ( ), render::font::BODY, math::vec2 ( x, y ), theme::ink, render::align::LEFT, inner );
			y += 22.0f;
			c.icon ( 0xE7F4, math::vec2 ( x + 7.0f, y ), 12.0f, theme::ink_soft );
			c.text ( where.text ( ), render::font::SMALL, math::vec2 ( x + 22.0f, y ), theme::ink_soft, render::align::LEFT, inner - 22.0f );
			y += 30.0f;

			if ( pill_button ( c, make_id ( "spotify_paste" ), math::rect ( x, y, inner, 40.0f ), L"Paste Spotify link", theme::glyph::LINK, -1.0f, true, false ) && active ) {

				paste_link ( );
				spotify_open = false;

			}

			y += 48.0f;

			if ( pill_button ( c, make_id ( "spotify_disconnect" ), math::rect ( x, y, inner, 36.0f ), L"Disconnect", theme::glyph::NONE, -1.0f, false, false ) && active )
				spotify::disconnect ( );

			y += 36.0f;

		}

		if ( s.message [ 0 ] )
			c.text ( s.message, render::font::SMALL, math::vec2 ( x, y + 20.0f ), theme::ink_soft, render::align::LEFT, inner );

		c.pop ( );

	}

}