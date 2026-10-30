#include "ui/app.hh"
#include "audio/engine.hh"
#include "base/files.hh"
#include <windows.h>

namespace ui {

	uint64_t make_id ( const char* name, int64_t index ) {

		uint64_t hash = 0xCBF29CE484222325ull;

		for ( ; *name; name++ ) {

			hash ^= static_cast < uint8_t > ( *name );
			hash *= 0x100000001B3ull;

		}

		hash ^= static_cast < uint64_t > ( index ) * 0x9E3779B97F4A7C15ull;
		hash *= 0x100000001B3ull;

		return hash ? hash : 1;

	}

	static render::color vivid ( render::color value ) {

		float luminance = value.r * 0.299f + value.g * 0.587f + value.b * 0.114f;

		if ( luminance < 0.22f )
			return render::mix ( value, theme::white, 0.5f );

		return render::mix ( value, theme::white, 0.08f );

	}

	bool app::setup ( render::gpu* device, bool live_audio ) {

		gpu = device;
		live = live_audio;
		widgets = new widget [ widget_room ];

		if ( !live_audio && demo_library )
			library::build_demo ( );

		render::cover_spec specs [ 16 ];

		library::covers ( specs );
		art_base = library::generated_count ( );

		if ( !gpu->make_covers ( specs, art_base, art_base + art_room, library::art_size ) || !gpu->make_thumbs ( thumb_room, thumb_edge ) )
			return false;

		audio::start ( live_audio );

		if ( live_audio ) {

			load_state ( );
			place_recents ( );

		}

		audio::volume ( muted ? 0.0f : volume );

		if ( !live_audio && library::album_count ( ) > 0 ) {

			base::list < int32_t > list;

			album_tracks ( 0, list );
			queue = list;
			queue_from = { origin::ALBUM, 0 };
			build_order ( 0 );
			open_track ( queue [ 0 ], 1, false );

		}

		render::color neutral [ 4 ] = { render::rgb ( 0xD9D4F5 ), render::rgb ( 0xF3D3E4 ), render::rgb ( 0xCFE3F7 ), render::rgb ( 0xF5E6D3 ) };

		for ( int32_t i = 0; i < 4; i++ ) {

			ambient [ i ] = track >= 0 ? vivid ( library::album_at ( library::track_at ( track ).album ).palette [ i ] ) : neutral [ i ];
			ambient_from [ i ] = ambient [ i ];

		}

		ambient_mix = 1.0f;
		switch_mix = 1.0f;
		here.album = track >= 0 ? library::track_at ( track ).album : 0;
		rebuild_songs ( );

		return true;

	}

	void app::rebuild_gpu ( render::gpu* device ) {

		render::cover_spec specs [ 16 ];

		gpu->free_surface ( mini_layer );
		gpu = device;
		library::covers ( specs );
		gpu->make_covers ( specs, art_base, art_base + art_room, library::art_size );
		gpu->make_thumbs ( thumb_room, thumb_edge );

		for ( art_slot& slot : art_slots )
			slot = art_slot ( );

		for ( art_slot& slot : thumb_slots )
			slot = art_slot ( );

		art_waiting.clear ( );
		big_wanted.clear ( );
		pending = base::max ( pending, 3 );

	}

	void app::adopt_library ( ) {

		bool remote_now = remote_mode ( );
		uint64_t current_key = track >= 0 && !remote_now ? library::track_at ( track ).key : 0;
		uint64_t focus_key = library::album_at ( here.album ).key;
		context source = remote_now ? local_from : queue_from;
		uint64_t source_key = source.kind == origin::ALBUM ? library::album_at ( source.index ).key : 0;
		base::list < uint64_t > queue_keys;

		for ( int32_t index : queue )
			queue_keys.push ( library::track_at ( index ).key );

		if ( !library::take_scan ( ) )
			return;

		if ( !remote_now ) {

			track = current_key ? library::find_track ( current_key ) : -1;
			old_track = -1;
			switch_mix = 1.0f;

		} else if ( !library::is_remote ( old_track ) )
			old_track = -1;

		selected_track = -1;
		queue.clear ( );

		for ( uint64_t key : queue_keys ) {

			int32_t found = library::find_track ( key );

			if ( found >= 0 )
				queue.push ( found );

		}

		if ( source.kind == origin::ALBUM ) {

			source.index = library::find_album ( source_key );

			if ( source.index < 0 )
				source.kind = origin::NONE;

		}

		if ( remote_now )
			local_from = source;
		else
			queue_from = source;

		int32_t focus = library::find_album ( focus_key );

		here.album = base::max ( focus, 0 );

		if ( here.where == page::ALBUM && focus < 0 )
			here.where = page::HOME;

		previous_place = here;
		rebuild_songs ( );
		search_done = 0;

		if ( !remote_now && track >= 0 ) {

			int32_t at = 0;

			for ( int32_t i = 0; i < queue.count ( ); i++ ) {

				if ( queue [ static_cast < size_t > ( i ) ] == track )
					at = i;

			}

			if ( queue.empty ( ) ) {

				queue.push ( track );
				at = 0;

			}

			bool shuffled = shuffle;

			shuffle = false;
			build_order ( at );
			shuffle = shuffled;

		} else if ( current_key ) {

			playing = false;
			audio::play ( false );
			queue.clear ( );
			order.clear ( );

		}

		if ( track < 0 && restore_key ) {

			int32_t found = library::find_track ( restore_key );

			if ( found >= 0 ) {

				base::list < int32_t > list;
				int32_t album = library::track_at ( found ).album;
				int32_t start = 0;

				album_tracks ( album, list );

				for ( int32_t i = 0; i < list.count ( ); i++ ) {

					if ( list [ static_cast < size_t > ( i ) ] == found )
						start = i;

				}

				queue = list;
				queue_from = { origin::ALBUM, album };
				build_order ( start );

				double at = restore_position < library::track_at ( found ).duration - 2.0f ? restore_position : 0.0;

				open_track ( found, 1, false, at );
				here.album = album;

			}

			restore_key = 0;

		}

		if ( autoplay && track < 0 && library::album_count ( ) > 0 ) {

			base::list < int32_t > list;

			album_tracks ( 0, list );
			play_list ( list, 0, { origin::ALBUM, 0 } );
			autoplay = false;

		}

		pending = base::max ( pending, 3 );

	}

	int32_t app::art_lookup ( uint64_t key, float& appear ) {

		appear = 0.0f;

		for ( int32_t i = 0; i < art_room; i++ ) {

			art_slot& slot = art_slots [ i ];

			if ( slot.key == key ) {

				slot.used = frame_index;
				appear = slot.appear;

				return slot.found ? art_base + i : -1;

			}

		}

		return -2;

	}

	int32_t app::thumb_lookup ( uint64_t key, float& appear ) {

		appear = 0.0f;

		for ( int32_t i = 0; i < thumb_room; i++ ) {

			art_slot& slot = thumb_slots [ i ];

			if ( slot.key == key ) {

				slot.used = frame_index;
				appear = slot.appear;

				return slot.found ? i : -1;

			}

		}

		return -2;

	}

	void app::request_cover ( int32_t album ) {

		const library::album& item = library::album_at ( album );

		if ( !item.key )
			return;

		for ( uint64_t waiting : art_waiting ) {

			if ( waiting == item.key )
				return;

		}

		if ( library::is_virtual ( album ) ) {

			if ( item.art_path [ 0 ] ) {

				art_waiting.push ( item.key );
				spotify::request_art ( item.art_path, item.key );

			}

			return;

		}

		art_waiting.push ( item.key );
		library::request_art ( item.key, item.art_path );

	}

	static int32_t pick_slot ( art_slot* slots, int32_t room, uint64_t key, uint32_t frame ) {

		uint32_t oldest = 0xFFFFFFFFu;
		int32_t choice = -1;

		for ( int32_t i = 0; i < room; i++ ) {

			if ( slots [ i ].key == key )
				return i;

		}

		for ( int32_t i = 0; i < room; i++ ) {

			if ( slots [ i ].key == 0 )
				return i;

			if ( slots [ i ].used < oldest && slots [ i ].used + 1 < frame ) {

				oldest = slots [ i ].used;
				choice = i;

			}

		}

		return choice;

	}

	void app::receive_art ( ) {

		library::art result;
		int32_t allowed = math::abs ( scroll_velocity ) > 40.0f ? 2 : 4;

		for ( int32_t budget = 0; budget < allowed && ( library::take_art ( result ) || spotify::take_art ( result ) ); budget++ ) {

			for ( int32_t i = 0; i < art_waiting.count ( ); i++ ) {

				if ( art_waiting [ static_cast < size_t > ( i ) ] == result.key ) {

					art_waiting.remove_at ( static_cast < size_t > ( i ) );

					break;

				}

			}

			bool want_big = false;

			for ( int32_t i = 0; i < big_wanted.count ( ); i++ ) {

				if ( big_wanted [ static_cast < size_t > ( i ) ] == result.key ) {

					big_wanted.remove_at ( static_cast < size_t > ( i ) );
					want_big = true;

					break;

				}

			}

			int32_t thumb_choice = pick_slot ( thumb_slots, thumb_room, result.key, frame_index );

			if ( thumb_choice >= 0 ) {

				art_slot& slot = thumb_slots [ thumb_choice ];
				bool fresh = slot.key != result.key || !slot.found;
				bool usable = result.found && result.thumb.count ( ) == thumb_edge * thumb_edge * 4;

				slot.key = result.key;
				slot.used = frame_index;

				if ( fresh )
					slot.appear = 0.0f;

				slot.found = usable && gpu->upload_thumb ( thumb_choice, result.thumb.data ( ) );

			}

			if ( want_big && result.found ) {

				int32_t choice = pick_slot ( art_slots, art_room, result.key, frame_index );

				if ( choice >= 0 ) {

					art_slot& slot = art_slots [ choice ];

					slot.key = result.key;
					slot.used = frame_index;
					slot.appear = 0.0f;
					slot.found = gpu->upload_cover ( art_base + choice, result.pixels.data ( ) );

				}

			}

			if ( result.found ) {

				tone* memory = nullptr;

				for ( tone& item : tones ) {

					if ( item.key == result.key )
						memory = &item;

				}

				if ( !memory ) {

					if ( tones.count ( ) >= 256 )
						tones.remove_at ( 0 );

					memory = &tones.emplace ( );

				}

				memory->key = result.key;
				memory->accent = result.accent;

				for ( int32_t c = 0; c < 4; c++ )
					memory->palette [ c ] = result.palette [ c ];

				for ( int32_t k = 0; k < home_mix.count ( ) && k < 6; k++ )
					apply_tone ( library::track_at ( home_mix [ static_cast < size_t > ( k ) ] ).album );

			}

			int32_t matches [ 3 ] = { library::find_album ( result.key ), library::remote, library::remote + 1 };

			for ( int32_t album : matches ) {

				if ( album < 0 || !result.found || library::album_at ( album ).key != result.key )
					continue;

				library::album& target = library::album_at ( album );

				for ( int32_t c = 0; c < 4; c++ )
					target.palette [ c ] = result.palette [ c ];

				target.accent = result.accent;
				target.painted = true;

				if ( track >= 0 && library::track_at ( track ).album == album ) {

					for ( int32_t c = 0; c < 4; c++ ) {

						ambient_from [ c ] = render::mix ( ambient_from [ c ], ambient [ c ], ambient_mix );
						ambient [ c ] = vivid ( target.palette [ c ] );

					}

					ambient_mix = 0.0f;

				}

			}

		}

		art_slot* banks [ 2 ] = { art_slots, thumb_slots };
		int32_t rooms [ 2 ] = { art_room, thumb_room };

		for ( int32_t b = 0; b < 2; b++ ) {

			for ( int32_t i = 0; i < rooms [ b ]; i++ ) {

				art_slot& slot = banks [ b ] [ i ];

				if ( slot.found && slot.appear < 1.0f ) {

					slot.appear = base::min ( slot.appear + dt / 0.28f, 1.0f );
					animating = true;

				}

			}

		}

	}

	void app::cover ( render::canvas& c, math::rect area, float radius, int32_t album, float alpha, float angle ) {

		const library::album& item = library::album_at ( album );

		if ( item.cover >= 0 ) {

			c.image ( area, radius, item.cover, alpha, angle );

			return;

		}

		bool small = area.w * c.scale ( ) <= 112.0f;
		float thumb_appear = 0.0f;
		float big_appear = 0.0f;
		int32_t thumb = item.key ? thumb_lookup ( item.key, thumb_appear ) : -1;
		int32_t big = item.key && !small ? art_lookup ( item.key, big_appear ) : -2;
		bool missing = thumb == -2 || ( !small && big == -2 && thumb != -1 );

		if ( item.key && missing ) {

			if ( !small && big == -2 ) {

				bool known = false;

				for ( uint64_t key : big_wanted )
					known = known || key == item.key;

				if ( !known )
					big_wanted.push ( item.key );

			}

			request_cover ( album );

		}

		float base_appear = thumb >= 0 ? thumb_appear : ( big >= 0 ? big_appear : 0.0f );
		float shown = base_appear * base_appear * ( 3.0f - 2.0f * base_appear );

		if ( shown < 0.999f ) {

			c.gradient ( area, radius, math::vec2 ( area.x, area.y ), math::vec2 ( area.right ( ), area.bottom ( ) ), item.palette [ 0 ].fade ( alpha ), item.palette [ 2 ].fade ( alpha ) );
			c.icon ( theme::code ( theme::glyph::MUSIC ), area.center ( ), area.w * 0.3f, theme::white.fade ( 0.85f * alpha * ( 1.0f - shown ) ) );

		}

		if ( thumb >= 0 && shown > 0.001f && ( small || big < 0 || big_appear < 1.0f ) )
			c.thumb ( area, radius, thumb, alpha * shown, angle );

		if ( !small && big >= 0 ) {

			float sharp = thumb >= 0 ? big_appear * big_appear * ( 3.0f - 2.0f * big_appear ) : shown;

			if ( sharp > 0.001f )
				c.image ( area, radius, big, alpha * sharp, angle );

		}

	}

	void app::shutdown ( ) {

		if ( live && persist )
			save_state ( );

		files::settle ( );
		audio::stop ( );

	}

	void app::resize ( int32_t width_px, int32_t height_px, float dpi_scale ) {

		width = base::max ( width_px, 1 );
		height = base::max ( height_px, 1 );
		dpi = dpi_scale;
		pending = base::max ( pending, 2 );

	}

	widget& app::state ( uint64_t id ) {

		int32_t slot = static_cast < int32_t > ( id & static_cast < uint64_t > ( widget_room - 1 ) );

		for ( int32_t probe = 0; probe < widget_room; probe++ ) {

			widget& item = widgets [ slot ];

			if ( item.id == id )
				return item;

			if ( item.id == 0 ) {

				item = widget ( );
				item.id = id;

				return item;

			}

			slot = ( slot + 1 ) & ( widget_room - 1 );

		}

		widget* fresh = new widget [ widget_room ];

		for ( int32_t i = 0; i < widget_room; i++ ) {

			if ( widgets [ i ].id && widgets [ i ].seen + 600 > frame_index ) {

				int32_t target = static_cast < int32_t > ( widgets [ i ].id & static_cast < uint64_t > ( widget_room - 1 ) );

				while ( fresh [ target ].id )
					target = ( target + 1 ) & ( widget_room - 1 );

				fresh [ target ] = widgets [ i ];

			}

		}

		delete [ ] widgets;
		widgets = fresh;

		return state ( id );

	}

	void app::approach ( float& value, float target, float rate ) {

		float delta = target - value;

		if ( math::abs ( delta ) < 0.0015f ) {

			value = target;

			return;

		}

		value += delta * ( 1.0f - math::exp ( -rate * dt ) );
		animating = true;

	}

	void app::spring ( float& value, float& velocity, float target, float stiffness, float damping ) {

		if ( value == target && velocity == 0.0f )
			return;

		int32_t steps = base::max ( static_cast < int32_t > ( math::ceil ( dt * 240.0f ) ), 1 );
		float step = dt / static_cast < float > ( steps );

		for ( int32_t i = 0; i < steps; i++ ) {

			float force = stiffness * ( target - value ) - damping * velocity;

			velocity += force * step;
			value += velocity * step;

		}

		if ( math::abs ( target - value ) < 0.0007f && math::abs ( velocity ) < 0.01f ) {

			value = target;
			velocity = 0.0f;

			return;

		}

		animating = true;

	}

	bool app::over ( math::rect area ) const {

		if ( !mouse_inside || !area.contains ( mouse ) )
			return false;

		for ( const math::rect& item : blocked ) {

			if ( item.contains ( mouse ) )
				return false;

		}

		return true;

	}

	bool app::interact ( uint64_t id, math::rect area, widget& item, hit kind ) {

		item.seen = frame_index;

		if ( mouse_inside && area.contains ( mouse ) )
			next_hot = id;

		hit_rect record = { id, area, kind };

		next_hits.push ( record );

		bool hovered = hot == id && mouse_inside;
		bool held = active_id == id;

		float glide_step = dt / ( hovered ? 0.2f : 0.17f );

		item.glide = hovered ? base::min ( item.glide + glide_step, 1.0f ) : base::max ( item.glide - glide_step, 0.0f );
		item.hover = item.glide * item.glide * item.glide * ( item.glide * ( item.glide * 6.0f - 15.0f ) + 10.0f );

		if ( item.glide > 0.0f && item.glide < 1.0f )
			animating = true;

		approach ( item.press, held && hovered ? 1.0f : 0.0f, 30.0f );

		return clicked_id == id;

	}

	void app::block ( math::rect area ) {

		if ( mouse_inside && area.contains ( mouse ) )
			next_hot = make_id ( "block", static_cast < int64_t > ( area.x * 7.0f + area.y * 13.0f ) );

	}

	void app::tip ( uint64_t id, const wchar_t* text, math::rect anchor ) {

		if ( hot == id && !mouse_down && text && !mini_mode ) {

			tip_candidate = id;
			tip_candidate_text = text;
			tip_candidate_anchor = anchor;

		}

	}

	void app::mouse_move ( float x, float y ) {

		float scale = mini_mode ? mini_dpi : dpi;

		mouse = math::vec2 ( x / scale, y / scale );
		mouse_inside = true;
		pending = base::max ( pending, 3 );

	}

	void app::mouse_button ( int32_t button, bool down ) {

		if ( button == 0 ) {

			mouse_down = down;

			if ( down )
				press_event = true;
			else
				release_event = true;

		} else if ( button == 1 && down )
			right_event = true;

		pending = base::max ( pending, 3 );

	}

	void app::mouse_leave ( ) {

		mouse_inside = false;
		pending = base::max ( pending, 3 );

	}

	void app::mouse_wheel ( float steps ) {

		wheel_steps += steps;
		pending = base::max ( pending, 3 );

	}

	void app::key ( uint32_t code, bool control, bool shift ) {

		keys.push ( code );
		key_control = control;
		key_shift = shift;
		pending = base::max ( pending, 3 );

	}

	request app::take_request ( ) {

		request result = pending_request;

		pending_request = request::NONE;

		return result;

	}

	void app::set_maximized ( bool value ) {

		maximized = value;

	}

	void app::set_focus ( bool value ) {

		focused = value;
		pending = base::max ( pending, 2 );

	}

	void app::pop_in ( ) {

		pending = base::max ( pending, 3 );

	}

	bool app::busy ( ) const {

		return animating || pending > 0 || playing || seeking || marquee_active;

	}

	bool app::steady ( ) const {

		return ( playing || marquee_active ) && !animating && pending <= 0 && !seeking && !cap_drag;

	}

	hit app::hit_test ( float x, float y ) const {

		math::vec2 p ( x / dpi, y / dpi );

		if ( mini_mode )
			p = math::vec2 ( x / mini_dpi, y / mini_dpi );

		for ( int32_t i = hits.count ( ) - 1; i >= 0; i-- ) {

			if ( hits [ static_cast < size_t > ( i ) ].area.contains ( p ) )
				return hits [ static_cast < size_t > ( i ) ].kind;

		}

		if ( mini_mode )
			return mini_rect.contains ( p ) ? hit::CAPTION : hit::NONE;

		if ( p.y < 58.0f )
			return hit::CAPTION;

		return hit::NONE;

	}

	void app::begin_input ( ) {

		hits.clear ( );

		for ( const hit_rect& item : next_hits )
			hits.push ( item );

		next_hits.clear ( );
		hot = next_hot;
		next_hot = 0;
		clicked_id = 0;
		double_id = 0;
		right_id = 0;
		tip_candidate = 0;

		if ( press_event ) {

			active_id = hot;
			pressed_id = hot;
			press_frame = frame_index;

			if ( focus_id && hot != focus_id ) {

				if ( focus_id == make_id ( "title" ) )
					commit_title ( );
				else
					focus_id = 0;

			}

			if ( clock - last_press < 0.4 && math::distance ( mouse, press_at ) < 6.0f && hot )
				double_event = true;

			last_press = clock;
			press_at = mouse;

		}

		if ( release_event ) {

			if ( active_id && active_id == hot ) {

				clicked_id = active_id;

				if ( double_event )
					double_id = active_id;

			}

			active_id = 0;
			double_event = false;

		}

		if ( right_event )
			right_id = hot;

		press_event = false;
		release_event = false;
		right_event = false;

	}

	void app::end_input ( ) {

		wheel_steps = 0.0f;
		keys.clear ( );

		if ( tip_candidate && tip_candidate == tip_id ) {

			tip_time += dt;

			if ( tip_time > 0.55f ) {

				tip_text = tip_candidate_text;
				tip_anchor = tip_candidate_anchor;

			}

		} else {

			tip_id = tip_candidate;
			tip_time = 0.0f;

		}

		bool showing = tip_candidate && tip_time > 0.55f && !mouse_down;

		approach ( tip_show, showing ? 1.0f : 0.0f, showing ? 14.0f : 22.0f );

		if ( tip_candidate && !showing && tip_time > 0.0f )
			animating = true;

	}

	void app::handle_keys ( ) {

		field* editing = focus_id == make_id ( "search" ) ? &search_field : ( focus_id == make_id ( "title" ) ? &title_field : nullptr );

		if ( editing ) {

			for ( wchar_t value : typed ) {

				if ( value < 32 || value == 127 )
					continue;

				wchar_t single [ 2 ] = { value, 0 };

				field_insert ( *editing, single, clock );
				editing->typed = clock;

			}

		}

		typed.clear ( );

		for ( uint32_t code : keys ) {

			const library::track& current = library::track_at ( track );

			if ( editing && edit_keys ( *editing, code ) )
				continue;

			if ( editing && code != VK_MEDIA_PLAY_PAUSE && code != VK_MEDIA_NEXT_TRACK && code != VK_MEDIA_PREV_TRACK )
				continue;

			if ( key_control && ( code == 'F' || code == 'K' ) ) {

				focus_field ( make_id ( "search" ), search_field, nullptr, true );

				continue;

			}

			if ( key_control && code == 'V' ) {

				paste_link ( );

				continue;

			}

			if ( key_control && code == 'N' ) {

				create_playlist ( nullptr );

				continue;

			}

			switch ( code ) {

				case VK_SPACE:
					toggle_play ( );
					break;

				case VK_LEFT:

					if ( key_control )
						previous_track ( );
					else
						seek_to ( base::max ( position - 5.0, 0.0 ) );

					break;

				case VK_RIGHT:

					if ( key_control )
						next_track ( false );
					else
						seek_to ( base::min ( position + 5.0, static_cast < double > ( current.duration ) - 0.5 ) );

					break;

				case VK_UP:
					set_volume ( volume + 0.05f );
					break;

				case VK_DOWN:
					set_volume ( volume - 0.05f );
					break;

				case 'M':
					toggle_mute ( );
					break;

				case 'L':
					toggle_like ( );
					break;

				case VK_OEM_2:
					focus_field ( make_id ( "search" ), search_field, nullptr, true );
					break;

				case VK_ESCAPE:

					if ( menu != menu_kind::NONE )
						menu = menu_kind::NONE;
					else if ( spotify_open )
						spotify_open = false;
					else if ( here.where == page::DETAIL )
						close_detail ( );
					else if ( here.where == page::NOW )
						toggle_now ( );

					break;

				case VK_MEDIA_PLAY_PAUSE:
					toggle_play ( );
					break;

				case VK_MEDIA_NEXT_TRACK:
					next_track ( false );
					break;

				case VK_MEDIA_PREV_TRACK:
					previous_track ( );
					break;

				case VK_BROWSER_BACK:

					if ( here.where == page::NOW )
						toggle_now ( );

					break;

				default:
					break;

			}

		}

	}

	void app::set_volume ( float value ) {

		volume = math::saturate ( value );
		muted = false;
		volume_touch = clock;

		if ( remote_mode ( ) )
			spotify::volume ( volume );
		else
			audio::volume ( volume );

	}

	void app::open_track ( int32_t index, int32_t direction, bool play_now, double at ) {

		if ( library::is_saved ( index ) ) {

			if ( playing && !remote_mode ( ) )
				audio::play ( false );

			remote_follow = false;
			remote_key = 0;
			begin_switch ( index, direction );
			position = at;
			playing = play_now;
			remote_started = false;
			remote_near_end = false;

			if ( play_now )
				send_run ( );

			pending = base::max ( pending, 3 );

			return;

		}

		if ( library::track_count ( ) <= 0 )
			return;

		index = base::clamp ( index, 0, library::track_count ( ) - 1 );

		if ( remote_mode ( ) ) {

			if ( play_now && remote_state.playing )
				spotify::play ( false );

			remote_key = 0;
			remote_follow = false;

		}

		begin_switch ( index, direction );
		position = at;
		playing = play_now;

		const library::track& item = library::track_at ( index );

		if ( item.demo )
			audio::open ( item.song, at, play_now );
		else
			audio::open_file ( item.path, item.duration, at, play_now );

		audio::volume ( muted ? 0.0f : volume );
		pending = base::max ( pending, 3 );

	}

	void app::begin_switch ( int32_t index, int32_t direction ) {

		if ( track >= 0 && index != track ) {

			old_track = track;
			switch_direction = direction;
			switch_mix = 0.0f;
			switch_velocity = 0.0f;

			int32_t old_album = library::track_at ( track ).album;
			int32_t new_album = library::track_at ( index ).album;

			if ( old_album != new_album || library::album_at ( old_album ).key != library::album_at ( new_album ).key ) {

				for ( int32_t i = 0; i < 4; i++ )
					ambient_from [ i ] = render::mix ( ambient_from [ i ], ambient [ i ], ambient_mix );

				const library::album& next = library::album_at ( new_album );

				for ( int32_t i = 0; i < 4; i++ )
					ambient [ i ] = vivid ( next.palette [ i ] );

				ambient_mix = 0.0f;

			}

		} else if ( track < 0 ) {

			const library::album& next = library::album_at ( library::track_at ( index ).album );

			for ( int32_t i = 0; i < 4; i++ ) {

				ambient_from [ i ] = render::mix ( ambient_from [ i ], ambient [ i ], ambient_mix );
				ambient [ i ] = vivid ( next.palette [ i ] );

			}

			ambient_mix = 0.0f;

		}

		track = index;
		marquee_start = clock;
		pending = base::max ( pending, 3 );

	}

	void app::build_order ( int32_t first ) {

		int32_t count = queue.count ( );

		order.clear ( );

		for ( int32_t i = 0; i < count; i++ )
			order.push ( i );

		order_index = base::clamp ( first, 0, base::max ( count - 1, 0 ) );

		if ( !shuffle || count < 2 )
			return;

		base::random random;

		random.seed ( static_cast < uint64_t > ( clock * 1000.0 ) + static_cast < uint64_t > ( first ) * 31 + 5 );
		base::swap ( order [ 0 ], order [ static_cast < size_t > ( order_index ) ] );

		for ( int32_t i = count - 1; i > 1; i-- ) {

			int32_t j = 1 + static_cast < int32_t > ( random.next ( ) % static_cast < uint64_t > ( i ) );

			base::swap ( order [ static_cast < size_t > ( i ) ], order [ static_cast < size_t > ( j ) ] );

		}

		order_index = 0;

	}

	void app::play_list ( const base::list < int32_t >& list, int32_t start, context from ) {

		if ( list.empty ( ) )
			return;

		queue = list;
		queue_from = from;
		build_order ( start );

		int32_t first = queue [ static_cast < size_t > ( order [ static_cast < size_t > ( order_index ) ] ) ];

		open_track ( first, 1, true );

		if ( from.kind == origin::PLAYLIST && from.index >= 0 && from.index < playlists.count ( ) ) {

			const playlist& item = playlists [ static_cast < size_t > ( from.index ) ];

			remember ( recent_kind::PLAYLIST, item.id, L"", item.title.get ( ), L"Playlist", L"" );

		} else if ( from.kind == origin::LIKED )
			remember ( recent_kind::LIKED, 1, L"", L"Liked Songs", L"Playlist", L"" );
		else if ( from.kind == origin::ALBUM && here.where != page::ARTIST && !library::is_virtual ( from.index ) ) {

			const library::album& album = library::album_at ( from.index );

			remember ( recent_kind::ALBUM, album.key, L"", album.title, album.artist, L"" );

		} else if ( from.kind == origin::SEARCH && from.index == 2 && detail_view.kind == 1 && detail_view.uri [ 0 ] )
			remember ( recent_kind::REMOTE_ALBUM, library::hash ( detail_view.uri, false ), detail_view.uri, detail_view.title, detail_view.artist, detail_view.art_url );
		else {

			const library::track& item = library::track_at ( first );

			remember ( recent_kind::SONG, item.key, L"", item.title, library::album_at ( item.album ).artist, L"" );

		}

	}

	void app::toggle_play ( ) {

		if ( track < 0 )
			return;

		if ( library::is_saved ( track ) ) {

			bool ours = remote_started && remote_state.has_track && remote_state.track == library::track_at ( track ).key;

			playing = !playing;

			if ( ours )
				spotify::play ( playing );
			else if ( playing )
				send_run ( );

			pending = base::max ( pending, 3 );

			return;

		}

		if ( remote_mode ( ) ) {

			playing = !playing;
			spotify::play ( playing );
			pending = base::max ( pending, 3 );

			return;

		}

		if ( audio::finished ( ) ) {

			audio::seek ( 0.0 );
			position = 0.0;

		}

		playing = !playing;
		audio::play ( playing );
		pending = base::max ( pending, 3 );

	}

	void app::next_track ( bool automatic ) {

		if ( library::is_remote ( track ) ) {

			if ( !automatic )
				spotify::next ( );

			return;

		}

		if ( queue.empty ( ) )
			return;

		if ( order_index + 1 < order.count ( ) )
			order_index++;
		else {

			if ( repeat == repeat_mode::OFF ) {

				order_index = 0;
				open_track ( queue [ static_cast < size_t > ( order [ 0 ] ) ], 1, false );

				if ( library::is_saved ( track ) && remote_state.playing )
					spotify::play ( false );

				return;

			}

			if ( shuffle ) {

				int32_t keep = order [ static_cast < size_t > ( order_index ) ];

				build_order ( keep );

				if ( order.count ( ) > 1 )
					order_index = 1;

			} else
				order_index = 0;

		}

		open_track ( queue [ static_cast < size_t > ( order [ static_cast < size_t > ( order_index ) ] ) ], 1, true );

	}

	void app::previous_track ( ) {

		if ( library::is_remote ( track ) ) {

			spotify::previous ( );

			return;

		}

		if ( queue.empty ( ) )
			return;

		if ( position > 3.0 || ( order_index == 0 && ( repeat == repeat_mode::OFF || order.count ( ) < 2 ) ) ) {

			seek_to ( 0.0 );

			return;

		}

		order_index = order_index == 0 ? order.count ( ) - 1 : order_index - 1;
		open_track ( queue [ static_cast < size_t > ( order [ static_cast < size_t > ( order_index ) ] ) ], -1, true );

	}

	void app::album_tracks ( int32_t album, base::list < int32_t >& result ) {

		const library::album& source = library::album_at ( album );

		result.clear ( );

		for ( int32_t i = 0; i < source.count; i++ )
			result.push ( source.first + i );

	}

	void app::navigate ( page where, int32_t index ) {

		location target = here;

		target.where = where;

		if ( where == page::ALBUM || where == page::ARTIST ) {

			if ( library::album_count ( ) <= 0 )
				return;

			target.album = base::clamp ( index, 0, library::album_count ( ) - 1 );

		}

		if ( where == page::PLAYLIST ) {

			if ( playlists.empty ( ) )
				return;

			target.playlist = base::clamp ( index, 0, playlists.count ( ) - 1 );

		}

		if ( where == page::DETAIL )
			target.album = index;

		bool same = here.where == where && ( ( where != page::ALBUM && where != page::ARTIST && where != page::DETAIL ) || here.album == target.album ) && ( where != page::PLAYLIST || here.playlist == target.playlist );

		if ( focus_id == make_id ( "title" ) && !same )
			commit_title ( );

		menu = menu_kind::NONE;

		if ( same ) {

			scroll_target = 0.0f;

			return;

		}

		if ( where == page::NOW && here.where != page::NOW )
			before_now = here;

		if ( where == page::SEARCH && here.where != page::SEARCH )
			before_search = here.where == page::NOW ? before_now : here;

		if ( where == page::HOME ) {

			home_time = clock;

			if ( home_asked >= 0.0 && clock - home_asked > 1800.0 )
				ask_home ( false );

		}

		previous_place = here;
		previous_scroll = scroll;
		here = target;
		page_mix = 0.0f;
		page_velocity = 0.0f;
		scroll = 0.0f;
		scroll_target = 0.0f;
		scroll_velocity = 0.0f;
		selected_track = -1;
		pending = base::max ( pending, 3 );

	}

	void app::toggle_now ( ) {

		if ( here.where == page::NOW ) {

			location back = before_now.where == page::NOW ? location ( ) : before_now;

			navigate ( back.where, back.where == page::PLAYLIST ? back.playlist : back.album );

		} else
			navigate ( page::NOW );

	}

	void app::update_playback ( ) {

		if ( !remote_mode ( ) )
			position = audio::position ( );

		if ( track >= 0 && playing && !remote_mode ( ) && audio::finished ( ) ) {

			if ( repeat == repeat_mode::ONE ) {

				audio::seek ( 0.0 );
				position = 0.0;

			} else
				next_track ( true );

		}

		spring ( switch_mix, switch_velocity, 1.0f, 150.0f, 22.0f );
		approach ( ambient_mix, 1.0f, 1.8f );
		spring ( art_scale, art_velocity, playing ? 1.0f : 0.86f, 170.0f, 19.0f );
		approach ( spin_speed, playing ? 1.0f : 0.0f, 2.2f );

		if ( spin_speed > 0.0f ) {

			spin += spin_speed * dt * 0.55f;

			if ( spin > math::tau )
				spin -= math::tau;

		}

		float target [ 3 ] = { 0.16f, 0.16f, 0.16f };

		if ( track >= 0 && !remote_mode ( ) && library::track_at ( track ).duration <= 0.0f ) {

			double length = audio::duration ( );

			if ( length > 0.0 )
				library::track_at ( track ).duration = static_cast < float > ( length );

		}

		if ( playing && remote_mode ( ) ) {

			float t = clock_wrapped ( );

			for ( int32_t i = 0; i < 3; i++ ) {

				float fi = static_cast < float > ( i );
				float wave = 0.5f + 0.25f * math::sin ( t * ( 5.1f + 2.3f * fi ) + fi * 1.9f ) + 0.25f * math::sin ( t * ( 8.7f + 1.7f * fi ) + fi * 0.7f );

				target [ i ] = 0.24f + 0.7f * wave;

			}

		} else if ( playing && track >= 0 && audio::live ( ) ) {

			float measured [ 3 ] = { };

			audio::levels ( measured );

			for ( int32_t i = 0; i < 3; i++ )
				target [ i ] = 0.18f + 0.82f * measured [ i ];

		} else if ( playing && track >= 0 && library::track_at ( track ).demo ) {

			synth::levels sample = synth::measure ( library::track_at ( track ).song, position );

			target [ 0 ] = 0.18f + 0.82f * base::max ( sample.bass, sample.drums * 0.7f );
			target [ 1 ] = 0.18f + 0.82f * base::max ( sample.lead, sample.pad * 0.8f );
			target [ 2 ] = 0.18f + 0.82f * base::max ( sample.drums, sample.pad * 0.45f );

		}

		int32_t level_steps = base::max ( static_cast < int32_t > ( math::ceil ( dt * 240.0f ) ), 1 );
		float level_step = dt / static_cast < float > ( level_steps );

		for ( int32_t i = 0; i < 3; i++ ) {

			for ( int32_t k = 0; k < level_steps; k++ ) {

				level_velocity [ i ] += ( 520.0f * ( target [ i ] - levels [ i ] ) - 26.0f * level_velocity [ i ] ) * level_step;
				levels [ i ] += level_velocity [ i ] * level_step;

			}

		}

		if ( playing )
			ambient_time += dt;

	}

	void app::idle ( double seconds ) {

		if ( !live )
			audio::tick ( seconds - ( last < 0.0 ? seconds : last ) );

		last = seconds;

		if ( remote_mode ( ) || remote_state.connected ) {

			update_remote ( );

			if ( remote_mode ( ) )
				return;

		}

		position = audio::position ( );

		if ( track >= 0 && playing && audio::finished ( ) ) {

			if ( repeat == repeat_mode::ONE )
				audio::seek ( 0.0 );
			else
				next_track ( true );

		}

	}

	math::rect app::dock_rect ( int32_t index ) const {

		float left = 96.0f;
		float room = w - left - 18.0f;
		float wide = base::clamp ( room * 0.62f, 600.0f, 780.0f );
		float compact = 336.0f;
		float tall = 72.0f;

		wide = base::min ( wide, room );

		math::rect spot;

		switch ( index ) {

			case 1:
				spot = math::rect ( left, h - 18.0f - tall, compact, tall );
				break;

			case 2:
				spot = math::rect ( w - 18.0f - compact, h - 18.0f - tall, compact, tall );
				break;

			case 3:
				spot = math::rect ( left, 68.0f, compact, tall );
				break;

			case 4:
				spot = math::rect ( w - 18.0f - compact, 68.0f, compact, tall );
				break;

			default:
				spot = math::rect ( left + room * 0.5f - wide * 0.5f, h - 18.0f - tall, wide, tall );
				break;

		}

		spot.x = math::round ( spot.x * dpi ) / dpi;
		spot.y = math::round ( spot.y * dpi ) / dpi;
		spot.w = math::round ( spot.w * dpi ) / dpi;

		return spot;

	}

	void app::update_capsule ( ) {

		math::vec2 moved = mouse - last_mouse;

		if ( dt > 0.0f && mouse_inside ) {

			math::vec2 instant = moved * ( 1.0f / dt );

			mouse_velocity = mouse_velocity + ( instant - mouse_velocity ) * 0.35f;

		}

		last_mouse = mouse;

		uint64_t body = make_id ( "capsule" );
		bool grabbing = active_id == body || active_id == make_id ( "capsule_art" ) || active_id == make_id ( "capsule_text" );

		drag_finished = false;

		if ( !cap_drag && grabbing && mouse_down && math::distance ( mouse, press_at ) > 5.0f && now < 0.5f ) {

			cap_drag = true;
			grab = press_at - math::vec2 ( cap_x, cap_y );

		}

		if ( cap_drag && !mouse_down ) {

			cap_drag = false;
			drag_finished = true;

			math::vec2 center ( cap_x + cap_w * 0.5f, cap_y + 36.0f );
			math::vec2 projected = center + mouse_velocity * 0.22f;
			float best = 1e30f;

			for ( int32_t i = 0; i < 5; i++ ) {

				math::vec2 target = dock_rect ( i ).center ( );
				float distance = math::distance ( projected, target );

				if ( distance < best ) {

					best = distance;
					dock = i;

				}

			}

			pending = base::max ( pending, 3 );

		}

		capsule_home = dock_rect ( dock );

		if ( !cap_ready ) {

			cap_x = capsule_home.x;
			cap_y = capsule_home.y;
			cap_w = capsule_home.w;
			cap_ready = true;

		}

		if ( cap_drag ) {

			math::vec2 want = mouse - grab;
			math::vec2 center ( want.x + cap_w * 0.5f, want.y + 36.0f );
			float best = 1e30f;
			float width_goal = capsule_home.w;

			for ( int32_t i = 0; i < 5; i++ ) {

				math::rect target = dock_rect ( i );
				float distance = math::distance ( center, target.center ( ) );

				if ( distance < best ) {

					best = distance;
					width_goal = target.w;

				}

			}

			float grab_ratio = cap_w > 1.0f ? grab.x / cap_w : 0.5f;

			spring ( cap_w, cap_vw, width_goal, 260.0f, 30.0f );
			grab.x = grab_ratio * cap_w;
			want = mouse - grab;
			want.x = base::clamp ( want.x, -cap_w * 0.25f, w - cap_w * 0.75f );
			want.y = base::clamp ( want.y, 50.0f, h - 60.0f );
			spring ( cap_x, cap_vx, want.x, 900.0f, 56.0f );
			spring ( cap_y, cap_vy, want.y, 900.0f, 56.0f );
			animating = true;

		} else {

			spring ( cap_x, cap_vx, capsule_home.x, 230.0f, 23.0f );
			spring ( cap_y, cap_vy, capsule_home.y, 230.0f, 23.0f );
			spring ( cap_w, cap_vw, capsule_home.w, 230.0f, 26.0f );

		}

		spring ( lift, lift_velocity, cap_drag ? 1.0f : 0.0f, 300.0f, 24.0f );
		capsule = math::rect ( cap_x, cap_y, cap_w, 72.0f );

		bool bottom = dock <= 2;

		approach ( bottom_inset, bottom ? 112.0f : 30.0f, 10.0f );
		approach ( top_inset, dock >= 3 ? 86.0f : 0.0f, 10.0f );

	}

	void app::layout ( ) {

		w = static_cast < float > ( width ) / dpi;
		h = static_cast < float > ( height ) / dpi;
		top = math::rect ( 0.0f, 0.0f, w, 60.0f );
		controls = math::rect ( w - 12.0f - 132.0f, 12.0f, 132.0f, 40.0f );

		float focus_ease = search_focus * search_focus * ( 3.0f - 2.0f * search_focus );
		float search_w = base::min ( base::clamp ( w * 0.34f, 300.0f, 500.0f ) + 60.0f * focus_ease, w - 360.0f );

		search_box = math::rect ( w * 0.5f - search_w * 0.5f, 12.0f, search_w, 40.0f );
		rail = math::rect ( 0.0f, 64.0f, 88.0f, h - 64.0f );
		content = math::rect ( 100.0f, 64.0f, w - 100.0f - 28.0f, h - 64.0f );
		list_area = content;

		float stage_w = base::clamp ( content.w * 0.48f, 360.0f, 700.0f );

		stage = math::rect ( content.x, 64.0f + top_inset, stage_w, h - 64.0f - top_inset - base::max ( bottom_inset - 8.0f, 24.0f ) );

		float size = base::min ( base::min ( stage.w * 0.72f, stage.h - 210.0f ), 520.0f );

		size = base::max ( size, 180.0f );
		cover_slot = math::rect ( stage.center ( ).x - size * 0.5f, stage.y + base::max ( 8.0f, ( stage.h - size - 210.0f ) * 0.5f ), size, size );
		update_capsule ( );
		blocked.clear ( );
		blocked.push ( search_box );
		blocked.push ( controls );

		if ( menu_show > 0.05f )
			blocked.push ( menu_rect );

		if ( spotify_show > 0.05f )
			blocked.push ( spotify_panel );

		if ( now < 0.98f && track >= 0 )
			blocked.push ( capsule );

	}

	void app::draw_background ( render::canvas& c ) {

		struct orbit {

			float fx;
			float fy;
			float px;
			float py;
			float size;
			float strength;
			int32_t tone;

		};

		static const orbit orbits [ 7 ] = {
			{ 0.031f, 0.023f, 0.0f, 1.3f, 0.30f, 0.40f, 0 },
			{ 0.019f, 0.029f, 2.1f, 0.4f, 0.26f, 0.36f, 1 },
			{ 0.025f, 0.017f, 4.2f, 2.8f, 0.28f, 0.32f, 2 },
			{ 0.015f, 0.027f, 1.0f, 4.9f, 0.22f, 0.30f, 3 },
			{ 0.027f, 0.021f, 5.3f, 3.6f, 0.20f, 0.30f, 0 },
			{ 0.021f, 0.033f, 3.3f, 0.9f, 0.18f, 0.28f, 1 },
			{ 0.017f, 0.015f, 0.6f, 5.7f, 0.24f, 0.24f, 2 }
		};

		const render::color pastels [ 4 ] = { render::rgb ( 0xC9B8FF ), render::rgb ( 0xFFC2D6 ), render::rgb ( 0xB5DEFF ), render::rgb ( 0xFFDDB5 ) };
		float blend = math::smoothstep ( 0.0f, 1.0f, ambient_mix );
		float span = base::max ( w, h );
		float t = drift_time;

		for ( int32_t k = 0; k < 7; k++ ) {

			const orbit& o = orbits [ k ];
			float fk = static_cast < float > ( k );
			render::color tone = render::mix ( ambient_from [ o.tone ], ambient [ o.tone ], blend );
			render::color tint = render::mix ( tone, pastels [ ( k + o.tone ) % 4 ], k >= 4 ? 0.55f : 0.25f );
			float x = 0.5f + 0.34f * math::sin ( t * o.fx + o.px ) + 0.12f * math::sin ( t * o.fx * 2.3f + o.py );
			float y = 0.5f + 0.32f * math::sin ( t * o.fy + o.py ) + 0.12f * math::cos ( t * o.fy * 1.9f + o.px );
			float r = span * o.size * ( 1.0f + 0.1f * math::sin ( t * 0.11f + fk * 1.7f ) );
			math::vec2 p ( w * x, h * y );

			c.shadow ( math::rect ( p.x - r, p.y - r * 0.82f, r * 2.0f, r * 1.64f ), r * 0.82f, r * 0.55f, tint.fade ( o.strength * ( 1.0f + 0.5f * now ) ) );

		}

	}

	bool app::begin_frame ( double seconds ) {

		if ( fixed_step ) {

			dt = 1.0f / 60.0f;
			clock += dt;

		} else {

			dt = last < 0.0 ? 1.0f / 60.0f : static_cast < float > ( base::clamp ( seconds - last, 0.0, 0.05 ) );
			clock = seconds;

		}

		last = seconds;

		if ( !live )
			audio::tick ( dt );

		frame_index++;
		drift_time += dt;

		if ( drift_time > 10000.0f )
			drift_time -= 10000.0f;

		animating = false;
		marquee_active = false;
		begin_input ( );
		adopt_library ( );
		update_remote ( );
		ingest_detail ( );
		ingest_home ( );
		receive_art ( );
		handle_keys ( );
		run_search ( );
		update_playback ( );
		update_lyrics ( );

		return true;

	}

	void app::open_mini ( bool value ) {

		mini_target = value;

		if ( value && !mini_mode ) {

			mini_mode = true;
			mini_show = 0.0f;
			mini_show_velocity = 0.0f;
			mouse_inside = false;
			mouse_down = false;
			active_id = 0;
			hot = 0;

		}

		pending = base::max ( pending, 3 );

	}

	bool app::mini_settled ( ) const {

		return !mini_target && mini_show <= 0.001f && mini_show_velocity == 0.0f;

	}

	void app::frame_mini ( double seconds, render::surface& target, int32_t width_px, int32_t height_px, float scale, float margin ) {

		mini_dpi = scale;
		begin_frame ( seconds );
		spring ( mini_show, mini_show_velocity, mini_target ? 1.0f : 0.0f, 300.0f, 26.0f );
		blocked.clear ( );
		float full_w = static_cast < float > ( width_px ) / scale - margin * 2.0f;
		float pill_h = static_cast < float > ( height_px ) / scale - margin * 2.0f;
		float room_rest = 96.0f;

		if ( track >= 0 ) {

			const library::track& item = library::track_at ( track );
			float title_w = gpu->layout ( item.title, render::font::BODY_STRONG, 0.0f )->width;
			float artist_w = gpu->layout ( library::album_at ( item.album ).artist, render::font::SMALL, 0.0f )->width;

			room_rest = base::clamp ( base::max ( title_w, artist_w ) + 6.0f, 96.0f, 170.0f );

		}

		approach ( mini_text_room, room_rest, 12.0f );
		approach ( mini_align, mini_align_target, 10.0f );

		float open = math::saturate ( mini_hover );

		open = open * open * ( 3.0f - 2.0f * open );

		float room = mini_text_room + ( base::max ( mini_text_room, 190.0f ) - mini_text_room ) * open;
		float pill_w = base::min ( 294.0f + room, full_w );

		mini_rect = math::rect ( math::round ( ( margin + ( full_w - pill_w ) * mini_align ) * scale ) / scale, margin, math::round ( pill_w * scale ) / scale, pill_h );
		gpu->next_frame ( );

		render::canvas& c = mini_canvas;
		float shown = math::saturate ( mini_show );

		c.begin ( gpu, scale, math::vec2 ( 0.0f, 0.0f ), clock_wrapped ( ) );
		spring ( mini_lift, mini_lift_velocity, mini_lifted ? 1.0f : 0.0f, 320.0f, 24.0f );

		if ( track < 0 && mini_target )
			open_mini ( false );

		math::rect reach ( mini_rect.x - 14.0f, mini_rect.y - 18.0f, mini_rect.w + 28.0f, mini_rect.h + 32.0f );
		bool hovering = mouse_inside && reach.contains ( mouse ) && mini_target && track >= 0;

		approach ( mini_hover, hovering || mini_lifted ? 1.0f : 0.0f, hovering ? 18.0f : 11.0f );

		if ( wheel_steps != 0.0f && mouse_inside && mini_rect.contains ( mouse ) )
			set_volume ( volume + wheel_steps * 0.05f );

		draw_mini ( c, mini_rect );

		float badge = track >= 0 ? math::saturate ( mini_hover ) : 0.0f;

		if ( badge > 0.002f ) {

			for ( int32_t k = 0; k < 2; k++ ) {

				bool expand = k == 0;
				math::vec2 at ( mini_rect.right ( ) - ( expand ? 38.0f : 8.0f ), mini_rect.y + 2.0f );
				uint64_t id = make_id ( expand ? "mini_open" : "mini_hide" );
				widget& item = state ( id );
				bool clicked = badge > 0.5f && interact ( id, math::rect ( at.x - 13.0f, at.y - 13.0f, 26.0f, 26.0f ), item );
				float e = expand ? math::saturate ( badge * 1.15f - 0.15f ) : badge;
				float eased = e * e * ( 3.0f - 2.0f * e );
				math::rect disc_area ( at.x - 11.0f, at.y - 11.0f, 22.0f, 22.0f );

				c.push ( at, ( 0.5f + 0.5f * eased ) * ( 1.0f + 0.06f * item.hover - 0.06f * item.press ), eased );
				c.shadow ( disc_area, 11.0f, 5.0f, theme::shade.fade ( 0.2f ), math::vec2 ( 0.0f, 2.0f ) );
				c.circle ( at, 11.0f, render::mix ( theme::white, render::rgb ( 0xEDEDF2 ), item.hover ) );
				c.outline ( disc_area, 11.0f, render::color ( 0.0f, 0.0f, 0.0f, 0.0f ), 1.0f, theme::shade.fade ( 0.08f ) );

				if ( expand ) {

					math::vec2 a = at + math::vec2 ( -1.9f, 1.9f );
					math::vec2 b = at + math::vec2 ( 1.9f, -1.9f );
					math::vec2 side ( 0.55f, 0.55f );

					c.polygon ( a + side, b + side, b - side, a - side, 0.1f, theme::ink );
					c.polygon ( at + math::vec2 ( 4.1f, -4.1f ), at + math::vec2 ( 4.1f, -0.9f ), at + math::vec2 ( 4.1f, -0.9f ), at + math::vec2 ( 0.9f, -4.1f ), 0.35f, theme::ink );
					c.polygon ( at + math::vec2 ( -4.1f, 4.1f ), at + math::vec2 ( -0.9f, 4.1f ), at + math::vec2 ( -0.9f, 4.1f ), at + math::vec2 ( -4.1f, 0.9f ), 0.35f, theme::ink );

				} else
					c.fill ( math::rect ( at.x - 4.5f, at.y - 0.85f, 9.0f, 1.7f ), 0.85f, theme::ink );

				c.pop ( );

				if ( clicked ) {

					if ( expand )
						pending_request = request::RESTORE;
					else
						open_mini ( false );

				}

			}

		}

		if ( mini_layer.width < width_px || mini_layer.height < height_px )
			gpu->make_surface ( mini_layer, width_px, height_px, true );

		gpu->draw ( c, mini_layer, nullptr, true, render::color ( 0.0f, 0.0f, 0.0f, 0.0f ) );

		render::canvas& compose = mini_compose;
		float zoom = ( 0.86f + 0.14f * mini_show ) * ( 1.0f + 0.025f * mini_lift );
		math::vec2 pivot = mini_rect.center ( ) * scale;

		compose.begin ( gpu, 1.0f, math::vec2 ( 0.0f, 0.0f ), clock_wrapped ( ) );
		compose.push ( pivot, zoom, shown, math::vec2 ( 0.0f, -10.0f * scale * ( 1.0f - shown ) ) );
		compose.texture ( math::rect ( 0.0f, 0.0f, static_cast < float > ( width_px ), static_cast < float > ( height_px ) ), 0.0f, math::rect ( 0.0f, 0.0f, static_cast < float > ( width_px ) / static_cast < float > ( mini_layer.width ), static_cast < float > ( height_px ) / static_cast < float > ( mini_layer.height ) ), 1.0f );
		compose.pop ( );
		gpu->draw ( compose, target, &mini_layer, true, render::color ( 0.0f, 0.0f, 0.0f, 0.0f ) );
		end_input ( );

		if ( mini_settled ( ) )
			mini_mode = false;

		if ( animating )
			pending = base::max ( pending, 2 );
		else if ( pending > 0 )
			pending--;

	}

	void app::frame ( double seconds ) {

		begin_frame ( seconds );
		spring ( now, now_velocity, here.where == page::NOW ? 1.0f : 0.0f, 190.0f, 25.0f );
		spring ( page_mix, page_velocity, 1.0f, 230.0f, 28.0f );
		spring ( window_pop, window_pop_velocity, 1.0f, 260.0f, 26.0f );
		approach ( search_focus, focus_id == make_id ( "search" ) || search_field.length > 0 ? 1.0f : 0.0f, 14.0f );
		layout ( );
		library::liked ( liked_list );

		bool over_menu = menu_show > 0.05f && menu_rect.contains ( mouse );

		if ( wheel_steps != 0.0f && !over_menu ) {

			if ( mouse_inside && capsule.contains ( mouse ) && now < 0.5f && track >= 0 )
				set_volume ( volume + wheel_steps * 0.05f );
			else if ( mouse_inside && rail.contains ( mouse ) )
				rail_scroll_target -= wheel_steps * 80.0f;
			else if ( here.where == page::NOW && lyric_sheet.synced && lyric_ready && over ( content ) ) {

				lyric_nudge -= wheel_steps * 90.0f;
				lyric_touch = clock;

			} else if ( over ( content ) )
				scroll_target -= wheel_steps * 110.0f;

		}

		scroll_target = math::round ( base::clamp ( scroll_target, 0.0f, scroll_limit ) * dpi ) / dpi;
		spring ( scroll, scroll_velocity, scroll_target, 90.0f, 19.0f );
		rail_scroll_target = math::round ( base::clamp ( rail_scroll_target, 0.0f, rail_limit ) * dpi ) / dpi;
		spring ( rail_scroll, rail_velocity, rail_scroll_target, 90.0f, 19.0f );

		gpu->next_frame ( );
		gpu->prepare ( width, height );

		float pop = math::saturate ( window_pop );
		math::vec2 middle ( w * 0.5f, h * 0.5f );
		math::rect page_clip ( 0.0f, 0.0f, w, h );
		render::canvas& s = scene_canvas;

		s.begin ( gpu, dpi, math::vec2 ( 0.0f, 0.0f ), clock_wrapped ( ) );
		draw_background ( s );
		s.push ( middle, 0.97f + 0.03f * pop, 0.35f + 0.65f * pop );
		s.push_clip ( page_clip );

		if ( page_mix < 0.999f ) {

			s.push ( content.center ( ), 1.0f - 0.02f * page_mix, 1.0f - math::saturate ( page_mix * 2.6f ), math::vec2 ( 0.0f, -10.0f * page_mix ) );
			s.snap_origin = math::vec2 ( 0.0f, -previous_scroll * dpi );
			draw_page ( s, previous_place, previous_scroll, false );
			s.snap_origin = math::vec2 ( 0.0f, 0.0f );
			s.pop ( );

		}

		s.push ( content.center ( ), 1.02f - 0.02f * page_mix, math::saturate ( ( page_mix - 0.25f ) / 0.75f ), math::vec2 ( 0.0f, 16.0f * ( 1.0f - math::saturate ( page_mix ) ) ) );
		s.snap_origin = math::vec2 ( 0.0f, -scroll * dpi );
		draw_page ( s, here, scroll, true );
		s.snap_origin = math::vec2 ( 0.0f, 0.0f );
		s.pop ( );
		s.pop_clip ( );

		float fade_top = math::saturate ( scroll / 30.0f );
		render::color edge = theme::base;

		s.gradient ( math::rect ( 0.0f, 0.0f, w, 92.0f ), 0.0f, math::vec2 ( 0.0f, 0.0f ), math::vec2 ( 0.0f, 92.0f ), edge.fade ( 0.9f * fade_top ), edge.fade ( 0.0f ) );
		s.pop ( );
		gpu->draw ( s, gpu->scene, nullptr, true, theme::base.fade ( base_alpha ) );
		gpu->blur ( );

		render::canvas& c = chrome_canvas;

		c.begin ( gpu, dpi, math::vec2 ( 0.0f, 0.0f ), clock_wrapped ( ) );
		c.texture ( math::rect ( 0.0f, 0.0f, w, h ), 0.0f, math::rect ( 0.0f, 0.0f, static_cast < float > ( width ) / static_cast < float > ( gpu->scene.width ), static_cast < float > ( height ) / static_cast < float > ( gpu->scene.height ) ), 1.0f );
		c.light ( mouse, mouse_inside ? 1.0f : 0.0f );
		c.push ( middle, 0.97f + 0.03f * pop, 0.35f + 0.65f * pop );
		draw_rail ( c );
		draw_now ( c );
		c.snap_origin = math::vec2 ( capsule.x * dpi, capsule.y * dpi );
		draw_capsule ( c, capsule );
		c.snap_origin = math::vec2 ( 0.0f, 0.0f );
		draw_topbar ( c );
		draw_controls ( c );
		draw_spotify ( c );
		draw_menu ( c );
		draw_drag ( c );
		draw_toast ( c );
		draw_tip ( c );
		c.pop ( );
		gpu->draw ( c, gpu->chrome, &gpu->scene, true, render::color ( 0.0f, 0.0f, 0.0f, 0.0f ) );
		end_input ( );

		if ( animating )
			pending = base::max ( pending, 2 );
		else if ( pending > 0 )
			pending--;

	}

	void app::draw_snapshot ( render::canvas& target, float margin, float scale_by, float alpha_by, bool is_maximized ) {

		float pw = static_cast < float > ( width );
		float ph = static_cast < float > ( height );
		math::rect panel ( margin, margin, pw, ph );
		float radius = is_maximized ? 0.0f : 8.0f * dpi;

		target.push ( panel.center ( ), scale_by, alpha_by );

		if ( !is_maximized ) {

			target.shadow ( panel, radius, 22.0f * dpi, theme::shade.fade ( 0.30f ), math::vec2 ( 0.0f, 10.0f * dpi ) );
			target.shadow ( panel, radius, 3.0f * dpi, theme::shade.fade ( 0.14f ), math::vec2 ( 0.0f, 1.0f * dpi ) );

		}

		target.fill ( panel, radius, theme::underlay );
		target.texture ( panel, radius, math::rect ( 0.0f, 0.0f, pw / static_cast < float > ( gpu->chrome.width ), ph / static_cast < float > ( gpu->chrome.height ) ), 1.0f );

		if ( !is_maximized )
			target.outline ( panel, radius, render::color ( 0.0f, 0.0f, 0.0f, 0.0f ), 1.0f, theme::shade.fade ( 0.1f ) );

		target.pop ( );

	}

	static void put_text ( base::list < char >& out, const char* text ) {

		while ( *text )
			out.push ( *text++ );

	}

	static void put_number ( base::list < char >& out, int64_t value ) {

		char digits [ 24 ];
		int32_t count = 0;

		if ( value < 0 ) {

			out.push ( '-' );
			value = -value;

		}

		do {

			digits [ count++ ] = static_cast < char > ( '0' + value % 10 );
			value /= 10;

		} while ( value );

		while ( count )
			out.push ( digits [ --count ] );

	}

	static int64_t read_number ( const char*& cursor ) {

		bool negative = false;
		int64_t value = 0;

		if ( *cursor == '-' ) {

			negative = true;
			cursor++;

		}

		while ( *cursor >= '0' && *cursor <= '9' )
			value = value * 10 + ( *cursor++ - '0' );

		return negative ? -value : value;

	}

	static void state_path ( wchar_t* path ) {

		base::writer line;

		line.add ( files::folder ( ) ).add ( L"\\state.ini" );

		for ( int32_t i = 0; i <= line.size ( ); i++ )
			path [ i ] = line.text ( ) [ i ];

	}

	static void put_key ( base::list < char >& out, uint64_t value ) {

		char digits [ 24 ];
		int32_t count = 0;

		do {

			digits [ count++ ] = static_cast < char > ( '0' + value % 10 );
			value /= 10;

		} while ( value );

		while ( count )
			out.push ( digits [ --count ] );

	}

	static uint64_t read_key ( const char*& cursor ) {

		uint64_t value = 0;

		while ( *cursor >= '0' && *cursor <= '9' )
			value = value * 10 + static_cast < uint64_t > ( *cursor++ - '0' );

		return value;

	}

	void app::save_state ( ) {

		base::list < char > out;
		wchar_t path [ 600 ];
		base::list < uint64_t > likes;

		put_text ( out, "volume=" );
		put_number ( out, static_cast < int64_t > ( volume * 1000.0f ) );
		put_text ( out, "\nmuted=" );
		put_number ( out, muted ? 1 : 0 );
		put_text ( out, "\nshuffle=" );
		put_number ( out, shuffle ? 1 : 0 );
		put_text ( out, "\nrepeat=" );
		put_number ( out, static_cast < int64_t > ( repeat ) );
		put_text ( out, "\ncurrent=" );
		bool local = track >= 0 && !remote_mode ( );

		put_key ( out, local ? library::track_at ( track ).key : restore_key );
		put_text ( out, "\nposition=" );
		put_number ( out, static_cast < int64_t > ( ( local ? position : restore_position ) * 1000.0 ) );

		if ( placement_valid ) {

			put_text ( out, "\nwindow=" );

			for ( int32_t i = 0; i < 4; i++ ) {

				if ( i )
					out.push ( ',' );

				put_number ( out, placement [ i ] );

			}

			put_text ( out, "\nmaximized=" );
			put_number ( out, placement_maximized ? 1 : 0 );

		}

		put_text ( out, "\nmini=" );
		put_number ( out, mini_anchor );
		put_text ( out, "\ndock=" );
		put_number ( out, dock );
		put_text ( out, "\nwidened=" );
		put_number ( out, widen_asked ? 1 : 0 );
		library::liked_keys ( likes );

		for ( int32_t i = 0; i < library::saved_count ( ); i++ ) {

			int32_t index = library::saved + i;
			const library::track& item = library::track_at ( index );
			const library::album& group = library::album_at ( item.album );
			bool used = false;

			for ( uint64_t key : likes )
				used = used || key == item.key;

			for ( const recent_item& recent : recents )
				used = used || ( recent.kind == recent_kind::SONG && recent.key == item.key );

			for ( const playlist& list : playlists ) {

				for ( uint64_t key : list.keys )
					used = used || key == item.key;

			}

			if ( !used )
				continue;

			const wchar_t* fields [ 5 ] = { library::uri_of ( index ), item.title, group.artist, group.title, group.art_path };

			put_text ( out, "\nremote=" );

			for ( int32_t f = 0; f < 5; f++ ) {

				char utf8 [ 1024 ];
				int32_t length = WideCharToMultiByte ( CP_UTF8, 0, fields [ f ], -1, utf8, 1023, nullptr, nullptr );

				for ( int32_t k = 0; k < length - 1; k++ )
					out.push ( utf8 [ k ] == '\t' || utf8 [ k ] == '\n' || utf8 [ k ] == '\r' ? ' ' : utf8 [ k ] );

				out.push ( '\t' );

				if ( f == 3 ) {

					put_number ( out, static_cast < int64_t > ( item.duration * 1000.0f ) );
					out.push ( '\t' );

				}

			}

		}

		put_text ( out, "\nliked=" );

		for ( int32_t i = 0; i < likes.count ( ); i++ ) {

			if ( i )
				out.push ( ',' );

			put_key ( out, likes [ static_cast < size_t > ( i ) ] );

		}

		for ( const playlist& list : playlists ) {

			char utf8 [ 1024 ];
			int32_t length = WideCharToMultiByte ( CP_UTF8, 0, list.title.get ( ), -1, utf8, 1023, nullptr, nullptr );

			put_text ( out, "\nplaylist=" );

			if ( length > 1 ) {

				utf8 [ length ] = 0;
				put_text ( out, utf8 );

			}

			put_text ( out, "\ntag=" );
			put_key ( out, list.id );
			put_text ( out, "\nsongs=" );

			for ( int32_t i = 0; i < list.keys.count ( ); i++ ) {

				if ( i )
					out.push ( ',' );

				put_key ( out, list.keys [ static_cast < size_t > ( i ) ] );

			}

		}

		for ( const recent_item& item : recents ) {

			const wchar_t* fields [ 4 ] = { item.uri.get ( ), item.title.get ( ), item.subtitle.get ( ), item.art.get ( ) };

			put_text ( out, "\nrecent=" );
			put_number ( out, static_cast < int64_t > ( item.kind ) );
			out.push ( '\t' );
			put_key ( out, item.key );

			for ( const wchar_t* field : fields ) {

				char utf8 [ 1024 ];
				int32_t length = WideCharToMultiByte ( CP_UTF8, 0, field, -1, utf8, 1023, nullptr, nullptr );

				out.push ( '\t' );

				for ( int32_t k = 0; k < length - 1; k++ )
					out.push ( utf8 [ k ] == '\t' || utf8 [ k ] == '\n' || utf8 [ k ] == '\r' ? ' ' : utf8 [ k ] );

			}

		}

		put_text ( out, "\n" );
		state_path ( path );
		files::write_later ( path, out.data ( ), out.count ( ) );

	}

	void app::load_state ( ) {

		base::list < char > data;
		wchar_t path [ 600 ];

		state_path ( path );

		if ( !files::read ( path, data ) || data.count ( ) < 2 )
			return;

		const char* cursor = data.data ( );

		while ( *cursor ) {

			const char* name = cursor;

			while ( *cursor && *cursor != '=' && *cursor != '\n' )
				cursor++;

			if ( *cursor != '=' ) {

				while ( *cursor && *cursor != '\n' )
					cursor++;

				if ( *cursor )
					cursor++;

				continue;

			}

			int32_t length = static_cast < int32_t > ( cursor - name );

			cursor++;

			auto named = [ & ] ( const char* key ) {

				int32_t i = 0;

				for ( ; key [ i ]; i++ ) {

					if ( i >= length || key [ i ] != name [ i ] )
						return false;

				}

				return i == length;

			};

			if ( named ( "volume" ) )
				volume = math::saturate ( static_cast < float > ( read_number ( cursor ) ) / 1000.0f );
			else if ( named ( "muted" ) )
				muted = read_number ( cursor ) != 0;
			else if ( named ( "shuffle" ) )
				shuffle = read_number ( cursor ) != 0;
			else if ( named ( "repeat" ) )
				repeat = static_cast < repeat_mode > ( base::clamp ( static_cast < int32_t > ( read_number ( cursor ) ), 0, 2 ) );
			else if ( named ( "current" ) )
				restore_key = read_key ( cursor );
			else if ( named ( "position" ) )
				restore_position = static_cast < double > ( read_number ( cursor ) ) / 1000.0;
			else if ( named ( "window" ) ) {

				for ( int32_t i = 0; i < 4; i++ ) {

					placement [ i ] = static_cast < int32_t > ( read_number ( cursor ) );

					if ( *cursor == ',' )
						cursor++;

				}

				placement_valid = placement [ 2 ] > 200 && placement [ 3 ] > 200;

			} else if ( named ( "maximized" ) )
				placement_maximized = read_number ( cursor ) != 0;
			else if ( named ( "mini" ) )
				mini_anchor = base::clamp ( static_cast < int32_t > ( read_number ( cursor ) ), 0, 7 );
			else if ( named ( "dock" ) )
				dock = base::clamp ( static_cast < int32_t > ( read_number ( cursor ) ), 0, 4 );
			else if ( named ( "widened" ) )
				widen_asked = read_number ( cursor ) != 0;
			else if ( named ( "remote" ) ) {

				wchar_t fields [ 6 ] [ 512 ];

				for ( int32_t f = 0; f < 6; f++ ) {

					const char* start = cursor;

					while ( *cursor && *cursor != '\t' && *cursor != '\n' && *cursor != '\r' )
						cursor++;

					int32_t bytes = static_cast < int32_t > ( cursor - start );
					int32_t made = bytes > 0 ? MultiByteToWideChar ( CP_UTF8, 0, start, bytes, fields [ f ], 511 ) : 0;

					fields [ f ] [ made > 0 ? made : 0 ] = 0;

					if ( *cursor == '\t' )
						cursor++;

				}

				const wchar_t* digits = fields [ 4 ];
				int64_t milliseconds = 0;

				while ( *digits >= L'0' && *digits <= L'9' )
					milliseconds = milliseconds * 10 + ( *digits++ - L'0' );

				library::save_remote ( fields [ 0 ], fields [ 1 ], fields [ 2 ], fields [ 3 ], static_cast < float > ( milliseconds ) / 1000.0f, fields [ 5 ] );

			} else if ( named ( "liked" ) ) {

				base::list < uint64_t > keys;

				while ( *cursor >= '0' && *cursor <= '9' ) {

					keys.push ( read_key ( cursor ) );

					if ( *cursor == ',' )
						cursor++;

				}

				library::restore_liked ( keys );

			} else if ( named ( "playlist" ) ) {

				const char* start = cursor;

				while ( *cursor && *cursor != '\n' && *cursor != '\r' )
					cursor++;

				int32_t bytes = static_cast < int32_t > ( cursor - start );
				wchar_t wide [ 512 ];
				int32_t made = bytes > 0 ? MultiByteToWideChar ( CP_UTF8, 0, start, bytes, wide, 511 ) : 0;
				playlist& list = playlists.emplace ( );

				wide [ made > 0 ? made : 0 ] = 0;
				list.title.set ( made > 0 ? wide : L"Untitled" );
				list.id = fresh_id ( );

			} else if ( named ( "tag" ) && !playlists.empty ( ) ) {

				uint64_t tag = read_key ( cursor );

				if ( tag )
					playlists.back ( ).id = tag;

			} else if ( named ( "recent" ) ) {

				wchar_t fields [ 6 ] [ 512 ];

				for ( int32_t f = 0; f < 6; f++ ) {

					const char* start = cursor;

					while ( *cursor && *cursor != '\t' && *cursor != '\n' && *cursor != '\r' )
						cursor++;

					int32_t bytes = static_cast < int32_t > ( cursor - start );
					int32_t made = bytes > 0 ? MultiByteToWideChar ( CP_UTF8, 0, start, bytes, fields [ f ], 511 ) : 0;

					fields [ f ] [ made > 0 ? made : 0 ] = 0;

					if ( *cursor == '\t' )
						cursor++;

				}

				recent_item& item = recents.emplace ( );
				uint64_t key = 0;

				for ( const wchar_t* digit = fields [ 1 ]; *digit >= L'0' && *digit <= L'9'; digit++ )
					key = key * 10 + static_cast < uint64_t > ( *digit - L'0' );

				item.kind = static_cast < recent_kind > ( base::clamp ( static_cast < int32_t > ( fields [ 0 ] [ 0 ] - L'0' ), 0, 5 ) );
				item.key = key;
				item.uri.set ( fields [ 2 ] );
				item.title.set ( fields [ 3 ] );
				item.subtitle.set ( fields [ 4 ] );
				item.art.set ( fields [ 5 ] );

			} else if ( named ( "songs" ) && !playlists.empty ( ) ) {

				playlist& list = playlists.back ( );

				while ( *cursor >= '0' && *cursor <= '9' ) {

					list.keys.push ( read_key ( cursor ) );

					if ( *cursor == ',' )
						cursor++;

				}

			}

			while ( *cursor && *cursor != '\n' )
				cursor++;

			if ( *cursor )
				cursor++;

		}

	}

	const wchar_t* app::format_time ( double seconds, wchar_t* buffer ) {

		int32_t total = static_cast < int32_t > ( seconds < 0.0 ? 0.0 : seconds );
		int32_t minutes = total / 60;
		int32_t rest = total % 60;
		base::writer line;

		line.number ( minutes ).add ( L':' );

		if ( rest < 10 )
			line.add ( L'0' );

		line.number ( rest );

		for ( int32_t i = 0; i <= line.size ( ); i++ )
			buffer [ i ] = line.text ( ) [ i ];

		return buffer;

	}

}