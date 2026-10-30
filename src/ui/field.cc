#include "ui/app.hh"
#include <windows.h>

namespace ui {

	bool read_clipboard ( wchar_t* out, int32_t room ) {

		out [ 0 ] = 0;

		if ( !IsClipboardFormatAvailable ( CF_UNICODETEXT ) || !OpenClipboard ( nullptr ) )
			return false;

		HANDLE data = GetClipboardData ( CF_UNICODETEXT );
		const wchar_t* text = data ? static_cast < const wchar_t* > ( GlobalLock ( data ) ) : nullptr;
		int32_t length = 0;

		if ( text ) {

			while ( *text == L' ' || *text == L'\t' || *text == L'\r' || *text == L'\n' )
				text++;

			for ( ; text [ length ] && length < room - 1; length++ )
				out [ length ] = text [ length ] == L'\r' || text [ length ] == L'\n' || text [ length ] == L'\t' ? L' ' : text [ length ];

			while ( length > 0 && out [ length - 1 ] == L' ' )
				length--;

			out [ length ] = 0;
			GlobalUnlock ( data );

		}

		CloseClipboard ( );

		return length > 0;

	}

	void write_clipboard ( const wchar_t* text ) {

		int32_t length = base::text_length ( text );
		HGLOBAL memory = GlobalAlloc ( GMEM_MOVEABLE, static_cast < SIZE_T > ( length + 1 ) * sizeof ( wchar_t ) );

		if ( !memory )
			return;

		wchar_t* target = static_cast < wchar_t* > ( GlobalLock ( memory ) );

		for ( int32_t i = 0; i <= length; i++ )
			target [ i ] = text [ i ];

		GlobalUnlock ( memory );

		if ( !OpenClipboard ( GetActiveWindow ( ) ) ) {

			GlobalFree ( memory );

			return;

		}

		EmptyClipboard ( );

		if ( !SetClipboardData ( CF_UNICODETEXT, memory ) )
			GlobalFree ( memory );

		CloseClipboard ( );

	}

	void field_erase ( field& value, int32_t from, int32_t to, double now ) {

		from = base::clamp ( from, 0, value.length );
		to = base::clamp ( to, from, value.length );

		if ( to <= from )
			return;

		for ( int32_t i = from; i < to; i++ ) {

			if ( value.place [ i ] < -1e6f || value.text [ i ] == L' ' )
				continue;

			int32_t slot = value.ghost_count;

			if ( slot >= 24 ) {

				slot = 0;

				for ( int32_t k = 1; k < 24; k++ ) {

					if ( value.ghosts [ k ].died < value.ghosts [ slot ].died )
						slot = k;

				}

			} else
				value.ghost_count++;

			value.ghosts [ slot ].value = value.text [ i ];
			value.ghosts [ slot ].x = value.place [ i ];
			value.ghosts [ slot ].died = now;

		}

		for ( int32_t i = to; i <= value.length; i++ ) {

			value.text [ from + ( i - to ) ] = value.text [ i ];
			value.born [ from + ( i - to ) ] = value.born [ i ];
			value.place [ from + ( i - to ) ] = value.place [ i ];

		}

		value.length -= to - from;
		value.caret = from;
		value.anchor = from;
		value.revision++;

	}

	void field_insert ( field& value, const wchar_t* text, double now ) {

		field_erase ( value, base::min ( value.caret, value.anchor ), base::max ( value.caret, value.anchor ), now );

		int32_t count = base::min ( base::text_length ( text ), 127 - value.length );

		if ( count <= 0 )
			return;

		for ( int32_t i = value.length; i >= value.caret; i-- ) {

			value.text [ i + count ] = value.text [ i ];
			value.born [ i + count ] = value.born [ i ];
			value.place [ i + count ] = value.place [ i ];

		}

		for ( int32_t i = 0; i < count; i++ ) {

			value.text [ value.caret + i ] = text [ i ];
			value.born [ value.caret + i ] = now;
			value.place [ value.caret + i ] = -1e9f;

		}

		value.length += count;
		value.caret += count;
		value.anchor = value.caret;
		value.revision++;

	}

	void field_set ( field& value, const wchar_t* text, double now, bool animate ) {

		if ( animate )
			field_erase ( value, 0, value.length, now );

		int32_t i = 0;

		for ( ; text && text [ i ] && i < 127; i++ ) {

			value.text [ i ] = text [ i ];
			value.born [ i ] = animate ? now : now - 10.0;
			value.place [ i ] = -1e9f;

		}

		value.text [ i ] = 0;
		value.length = i;
		value.caret = i;
		value.anchor = i;
		value.revision++;

		if ( !animate ) {

			value.ghost_count = 0;
			value.caret_x = -1.0f;

		}

	}

	static int32_t word_edge ( const field& value, int32_t from, int32_t step ) {

		int32_t at = from;

		if ( step < 0 ) {

			while ( at > 0 && value.text [ at - 1 ] == L' ' )
				at--;

			while ( at > 0 && value.text [ at - 1 ] != L' ' )
				at--;

		} else {

			while ( at < value.length && value.text [ at ] != L' ' )
				at++;

			while ( at < value.length && value.text [ at ] == L' ' )
				at++;

		}

		return at;

	}

	void app::character ( wchar_t value ) {

		typed.push ( value );
		pending = base::max ( pending, 3 );

	}

	void app::focus_field ( uint64_t id, field& value, const wchar_t* text, bool select ) {

		if ( text )
			field_set ( value, text, clock, false );

		focus_id = id;
		value.caret = value.length;
		value.anchor = select ? 0 : value.length;
		value.typed = clock;
		pending = base::max ( pending, 3 );

	}

	void app::commit_title ( ) {

		if ( focus_id != make_id ( "title" ) )
			return;

		focus_id = 0;

		if ( title_list < 0 || title_list >= playlists.count ( ) )
			return;

		const wchar_t* start = title_field.text;
		int32_t length = title_field.length;

		while ( *start == L' ' ) {

			start++;
			length--;

		}

		while ( length > 0 && start [ length - 1 ] == L' ' )
			length--;

		if ( length <= 0 )
			return;

		wchar_t clean [ 128 ];

		for ( int32_t i = 0; i < length; i++ )
			clean [ i ] = start [ i ];

		clean [ length ] = 0;
		playlists [ static_cast < size_t > ( title_list ) ].title.set ( clean );

		if ( live && persist )
			save_state ( );

	}

	bool app::edit_keys ( field& value, uint32_t code ) {

		int32_t from = base::min ( value.caret, value.anchor );
		int32_t to = base::max ( value.caret, value.anchor );
		bool is_search = &value == &search_field;

		switch ( code ) {

			case VK_BACK:

				if ( to > from )
					field_erase ( value, from, to, clock );
				else if ( value.caret > 0 )
					field_erase ( value, key_control ? word_edge ( value, value.caret, -1 ) : value.caret - 1, value.caret, clock );

				value.typed = clock;

				return true;

			case VK_DELETE:

				if ( to > from )
					field_erase ( value, from, to, clock );
				else if ( value.caret < value.length )
					field_erase ( value, value.caret, key_control ? word_edge ( value, value.caret, 1 ) : value.caret + 1, clock );

				value.typed = clock;

				return true;

			case VK_LEFT:
			case VK_RIGHT: {

				int32_t step = code == VK_LEFT ? -1 : 1;

				if ( !key_shift && to > from && !key_control )
					value.caret = step < 0 ? from : to;
				else if ( key_control )
					value.caret = word_edge ( value, value.caret, step );
				else
					value.caret = base::clamp ( value.caret + step, 0, value.length );

				if ( !key_shift )
					value.anchor = value.caret;

				value.typed = clock;

				return true;

			}

			case VK_HOME:
			case VK_END:
				value.caret = code == VK_HOME ? 0 : value.length;

				if ( !key_shift )
					value.anchor = value.caret;

				value.typed = clock;

				return true;

			case VK_RETURN:

				if ( is_search ) {

					if ( !found_songs.empty ( ) )
						play_list ( found_songs, 0, { origin::SEARCH, 0 } );
					else if ( !found_remote.empty ( ) )
						play_catalog ( found_remote [ 0 ] );

				} else
					commit_title ( );

				return true;

			case VK_ESCAPE:

				if ( is_search && value.length > 0 ) {

					field_erase ( value, 0, value.length, clock );
					value.typed = clock;

				} else
					focus_id = 0;

				return true;

			case VK_TAB:

				if ( is_search )
					focus_id = 0;
				else
					commit_title ( );

				return true;

			default:
				break;

		}

		if ( !key_control )
			return code == VK_SPACE || ( code >= '0' && code <= 'Z' ) || code >= VK_OEM_1;

		if ( code == 'A' ) {

			value.anchor = 0;
			value.caret = value.length;

			return true;

		}

		if ( ( code == 'C' || code == 'X' ) && to > from ) {

			wchar_t piece [ 128 ];

			for ( int32_t i = from; i < to; i++ )
				piece [ i - from ] = value.text [ i ];

			piece [ to - from ] = 0;
			write_clipboard ( piece );

			if ( code == 'X' ) {

				field_erase ( value, from, to, clock );
				value.typed = clock;

			}

			return true;

		}

		if ( code == 'V' ) {

			wchar_t text [ 256 ];

			if ( read_clipboard ( text, 256 ) ) {

				field_insert ( value, text, clock );
				value.typed = clock;

			}

			return true;

		}

		return code != 'F' && code != 'K';

	}

	bool app::text_field ( render::canvas& c, uint64_t id, field& value, math::rect area, const wchar_t* placeholder, render::font role, render::color tint ) {

		widget& item = state ( id );
		bool focus = focus_id == id;
		wchar_t prefix [ 128 ];

		interact ( id, area, item );

		auto measure_to = [ & ] ( int32_t count ) {

			for ( int32_t i = 0; i < count; i++ )
				prefix [ i ] = value.text [ i ];

			prefix [ count ] = 0;

			return count > 0 ? c.measure ( prefix, role ) : 0.0f;

		};

		auto index_at = [ & ] ( float x ) {

			float local = x - area.x + value.offset;
			int32_t best = 0;
			float best_distance = 1e30f;

			for ( int32_t i = 0; i <= value.length; i++ ) {

				float distance = math::abs ( measure_to ( i ) - local );

				if ( distance < best_distance ) {

					best_distance = distance;
					best = i;

				}

			}

			return best;

		};

		if ( press_frame == frame_index && pressed_id == id ) {

			if ( !focus && &value == &title_field )
				focus_field ( id, value, nullptr, false );

			focus_id = id;
			focus = true;
			value.caret = index_at ( mouse.x );

			if ( !key_shift )
				value.anchor = value.caret;

			if ( double_event ) {

				value.anchor = 0;
				value.caret = value.length;

			}

			value.typed = clock;

		} else if ( focus && mouse_down && active_id == id && !double_event ) {

			value.caret = index_at ( mouse.x );
			value.typed = clock;

		}

		float targets [ 129 ];

		for ( int32_t i = 0; i <= value.length; i++ )
			targets [ i ] = measure_to ( i );

		float caret_target = targets [ value.caret ];
		float room = area.w - 4.0f;

		if ( caret_target - value.offset > room )
			value.offset = caret_target - room;

		if ( caret_target - value.offset < 0.0f )
			value.offset = caret_target;

		value.offset = base::clamp ( value.offset, 0.0f, base::max ( targets [ value.length ] - room, 0.0f ) );

		float glide = 1.0f - math::exp ( -26.0f * dt );

		for ( int32_t i = 0; i < value.length; i++ ) {

			if ( value.place [ i ] < -1e6f )
				value.place [ i ] = targets [ i ];
			else if ( math::abs ( targets [ i ] - value.place [ i ] ) > 0.03f ) {

				value.place [ i ] += ( targets [ i ] - value.place [ i ] ) * glide;
				animating = true;

			} else
				value.place [ i ] = targets [ i ];

		}

		if ( value.caret_x < 0.0f )
			value.caret_x = caret_target;
		else if ( math::abs ( caret_target - value.caret_x ) > 0.03f ) {

			value.caret_x += ( caret_target - value.caret_x ) * ( 1.0f - math::exp ( -34.0f * dt ) );
			animating = true;

		} else
			value.caret_x = caret_target;

		float hint_target = value.length == 0 ? 1.0f : 0.0f;

		if ( math::abs ( hint_target - value.hint ) > 0.002f ) {

			value.hint += ( hint_target - value.hint ) * ( 1.0f - math::exp ( -18.0f * dt ) );
			animating = true;

		} else
			value.hint = hint_target;

		float cy = area.center ( ).y;
		float cap = gpu->cap_height ( role );
		float left = area.x - value.offset;
		bool snapped = c.snap;

		c.push_clip ( math::rect ( area.x - 2.0f, area.y, area.w + 4.0f, area.h ) );
		c.snap = false;

		if ( focus && value.caret != value.anchor ) {

			float a = targets [ base::min ( value.caret, value.anchor ) ];
			float b = targets [ base::max ( value.caret, value.anchor ) ];

			c.fill ( math::rect ( left + a - 1.0f, cy - cap * 0.95f, b - a + 2.0f, cap * 1.9f ), 3.0f, render::rgb ( 0x0A84FF, 0.22f ) );

		}

		if ( placeholder && value.hint > 0.002f ) {

			float shown = value.hint * value.hint * ( 3.0f - 2.0f * value.hint );

			c.push ( math::vec2 ( area.x, cy ), 1.0f, shown, math::vec2 ( 10.0f * ( 1.0f - shown ), 0.0f ) );
			c.text ( placeholder, role, math::vec2 ( area.x, cy ), theme::ink_soft.fade ( focus ? 0.55f : 0.85f ) );
			c.pop ( );

		}

		for ( int32_t i = 0; i < value.length; i++ ) {

			if ( value.text [ i ] == L' ' )
				continue;

			wchar_t single [ 2 ] = { value.text [ i ], 0 };
			float age = static_cast < float > ( clock - value.born [ i ] );
			float t = math::saturate ( age / 0.17f );
			float e = 1.0f - ( 1.0f - t ) * ( 1.0f - t ) * ( 1.0f - t );
			float x = left + value.place [ i ];
			float width_i = targets [ i + 1 ] - targets [ i ];

			if ( x + width_i < area.x - 30.0f || x > area.right ( ) + 30.0f )
				continue;

			if ( t < 1.0f ) {

				animating = true;
				c.push ( math::vec2 ( x + width_i * 0.5f, cy ), 0.72f + 0.28f * e, e, math::vec2 ( 0.0f, 7.0f * ( 1.0f - e ) ) );
				c.scale_glyphs = true;
				c.text ( single, role, math::vec2 ( x, cy ), tint );
				c.scale_glyphs = false;
				c.pop ( );

			} else
				c.text ( single, role, math::vec2 ( x, cy ), tint );

		}

		for ( int32_t k = 0; k < value.ghost_count; ) {

			ghost& item = value.ghosts [ k ];
			float t = static_cast < float > ( clock - item.died ) / 0.15f;

			if ( t >= 1.0f || t < 0.0f ) {

				value.ghosts [ k ] = value.ghosts [ value.ghost_count - 1 ];
				value.ghost_count--;

				continue;

			}

			wchar_t single [ 2 ] = { item.value, 0 };
			float e = t * t * ( 3.0f - 2.0f * t );
			float x = left + item.x;

			c.push ( math::vec2 ( x + 4.0f, cy ), 1.0f - 0.3f * e, 1.0f - e, math::vec2 ( 0.0f, -6.0f * e ) );
			c.scale_glyphs = true;
			c.text ( single, role, math::vec2 ( x, cy ), tint );
			c.scale_glyphs = false;
			c.pop ( );
			animating = true;
			k++;

		}

		if ( focus ) {

			float since = static_cast < float > ( clock - value.typed );
			float blink = since < 0.6f || since > 5.627f ? 1.0f : 0.55f + 0.45f * math::cos ( ( since - 0.6f ) * 5.0f );

			c.fill ( math::rect ( left + value.caret_x - 0.75f, cy - cap * 0.9f, 1.5f, cap * 1.8f ), 0.75f, theme::ink.fade ( 0.9f * blink ) );

			if ( since < 5.7f )
				animating = true;

		}

		c.snap = snapped;
		c.pop_clip ( );

		return focus;

	}

}