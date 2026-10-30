#include "ui/app.hh"
#include "audio/engine.hh"
#include "library/catalog.hh"
#include <windows.h>

namespace ui {

	bool same_context ( context a, context b ) {

		if ( a.kind != b.kind )
			return false;

		return a.kind != origin::ALBUM && a.kind != origin::PLAYLIST && a.kind != origin::MIX ? true : a.index == b.index;

	}

	template < typename less >
	static void merge_sort ( base::list < int32_t >& items, less before ) {

		int32_t count = items.count ( );
		base::list < int32_t > spare;

		spare.resize ( items.size ( ) );

		for ( int32_t width = 1; width < count; width *= 2 ) {

			for ( int32_t start = 0; start < count; start += width * 2 ) {

				int32_t middle = base::min ( start + width, count );
				int32_t end = base::min ( start + width * 2, count );
				int32_t a = start;
				int32_t b = middle;
				int32_t out = start;

				while ( a < middle && b < end ) {

					if ( before ( items [ static_cast < size_t > ( b ) ], items [ static_cast < size_t > ( a ) ] ) )
						spare [ static_cast < size_t > ( out++ ) ] = items [ static_cast < size_t > ( b++ ) ];
					else
						spare [ static_cast < size_t > ( out++ ) ] = items [ static_cast < size_t > ( a++ ) ];

				}

				while ( a < middle )
					spare [ static_cast < size_t > ( out++ ) ] = items [ static_cast < size_t > ( a++ ) ];

				while ( b < end )
					spare [ static_cast < size_t > ( out++ ) ] = items [ static_cast < size_t > ( b++ ) ];

			}

			base::swap ( items, spare );

		}

	}

	static bool contains ( const wchar_t* text, const wchar_t* value, int32_t length ) {

		if ( !text || !text [ 0 ] )
			return false;

		return FindNLSStringEx ( LOCALE_NAME_USER_DEFAULT, FIND_FROMSTART | LINGUISTIC_IGNORECASE | LINGUISTIC_IGNOREDIACRITIC, text, -1, value, length, nullptr, nullptr, nullptr, 0 ) >= 0;

	}

	static bool starts ( const wchar_t* text, const wchar_t* value, int32_t length ) {

		if ( !text || !text [ 0 ] )
			return false;

		return FindNLSStringEx ( LOCALE_NAME_USER_DEFAULT, FIND_STARTSWITH | LINGUISTIC_IGNORECASE | LINGUISTIC_IGNOREDIACRITIC, text, -1, value, length, nullptr, nullptr, nullptr, 0 ) >= 0;

	}

	bool app::remote_mode ( ) const {

		return library::is_remote ( track ) || library::is_saved ( track );

	}

	void app::send_run ( ) {

		if ( !library::is_saved ( track ) )
			return;

		base::writer uris;
		int32_t count = 1;

		uris.add ( library::uri_of ( track ) );

		for ( int32_t k = order_index + 1; k < order.count ( ) && count < 40; k++ ) {

			int32_t candidate = queue [ static_cast < size_t > ( order [ static_cast < size_t > ( k ) ] ) ];

			if ( !library::is_saved ( candidate ) )
				break;

			uris.add ( L" " ).add ( library::uri_of ( candidate ) );
			count++;

		}

		spotify::play_tracks ( uris.text ( ), 0 );
		remote_sent = clock;
		remote_started = false;
		remote_near_end = false;

	}

	void app::update_queued ( ) {

		const spotify::snapshot& s = remote_state;
		library::track& current = library::track_at ( track );
		bool ours = s.has_track && s.track == current.key;

		if ( ours ) {

			remote_started = true;
			playing = s.playing;

			if ( !seeking )
				position = spotify::position ( s );

			if ( current.duration <= 0.0f && s.duration > 0.0 )
				current.duration = static_cast < float > ( s.duration );

			if ( remote_near_end && !s.playing && ( position < 1.5 || position >= s.duration - 1.0 ) ) {

				remote_near_end = false;
				remote_started = false;

				if ( repeat == repeat_mode::ONE )
					send_run ( );
				else
					next_track ( true );

				return;

			}

			remote_near_end = s.playing && s.duration > 0.0 && position >= s.duration - 2.5;

			return;

		}

		if ( !remote_started ) {

			if ( playing && clock - remote_sent > 20.0 )
				playing = false;

			return;

		}

		if ( !s.has_track )
			return;

		for ( int32_t k = order_index + 1; k < order.count ( ) && k <= order_index + 40; k++ ) {

			int32_t candidate = queue [ static_cast < size_t > ( order [ static_cast < size_t > ( k ) ] ) ];

			if ( !library::is_saved ( candidate ) )
				break;

			if ( library::track_at ( candidate ).key == s.track ) {

				order_index = k;
				begin_switch ( candidate, 1 );
				playing = s.playing;
				position = spotify::position ( s );
				remote_near_end = false;

				return;

			}

		}

		remote_started = false;

		if ( remote_near_end ) {

			remote_near_end = false;

			if ( s.playing )
				spotify::play ( false );

			if ( repeat == repeat_mode::ONE )
				send_run ( );
			else
				next_track ( true );

			return;

		}

		enter_remote ( );

	}

	void app::update_remote ( ) {

		spotify::read ( remote_state );

		const spotify::snapshot& s = remote_state;
		bool started = s.playing && !remote_was_playing;
		uint64_t message = s.message [ 0 ] ? library::hash ( s.message, false ) : 0;

		if ( message != remote_message ) {

			remote_message = message;

			if ( message && !spotify_open )
				show_toast ( s.message );

		}

		remote_was_playing = s.playing;

		if ( !s.connected ) {

			remote_follow = false;

			if ( library::is_saved ( track ) )
				playing = false;

			if ( library::is_remote ( track ) ) {

				int32_t found = restore_key ? lookup_track ( restore_key ) : -1;

				remote_key = 0;
				queue_from = local_from;

				if ( found >= 0 )
					open_track ( found, -1, false, restore_position );
				else {

					track = -1;
					old_track = -1;
					playing = false;

				}

			}

			return;

		}

		if ( library::is_saved ( track ) ) {

			update_queued ( );

			return;

		}

		if ( !library::is_remote ( track ) ) {

			if ( s.has_track && ( remote_follow || ( started && !playing ) ) )
				enter_remote ( );

			return;

		}

		library::track& current = library::track_at ( track );

		if ( s.has_track && s.track != remote_key ) {

			int32_t slot = track == library::remote ? library::remote + 1 : library::remote;

			library::set_remote ( slot, s.title, s.artist, s.album, static_cast < float > ( s.duration ), s.track, s.art );
			begin_switch ( slot, 1 );
			remote_key = s.track;

		} else if ( s.has_track && ( library::album_at ( current.album ).key != s.art || current.duration != static_cast < float > ( s.duration ) ) )
			library::set_remote ( track, s.title, s.artist, s.album, static_cast < float > ( s.duration ), s.track, s.art );

		library::track_at ( track ).liked = library::liked_key ( s.track );
		playing = s.playing;

		if ( !seeking )
			position = spotify::position ( s );

		if ( s.volume >= 0 && s.volume != remote_volume ) {

			remote_volume = s.volume;

			if ( clock - volume_touch > 2.5 && !volume_drag && s.volume > 0 ) {

				volume = static_cast < float > ( s.volume ) / 100.0f;
				muted = false;

			}

		}

	}

	void app::enter_remote ( ) {

		const spotify::snapshot& s = remote_state;

		if ( track >= 0 ) {

			restore_key = library::track_at ( track ).key;
			restore_position = position;

		}

		if ( playing )
			audio::play ( false );

		local_from = queue_from;
		queue_from = context ( );
		library::set_remote ( library::remote, s.title, s.artist, s.album, static_cast < float > ( s.duration ), s.track, s.art );
		begin_switch ( library::remote, 1 );
		remote_key = s.track;
		remote_follow = false;
		remote_volume = s.volume;
		playing = s.playing;
		position = spotify::position ( s );

		if ( s.volume > 0 ) {

			volume = static_cast < float > ( s.volume ) / 100.0f;
			muted = false;

		}

	}

	void app::seek_to ( double seconds ) {

		if ( remote_mode ( ) ) {

			if ( library::is_saved ( track ) && !remote_started )
				return;

			spotify::seek ( seconds );
			spotify::read ( remote_state );
			position = seconds;
			remote_near_end = false;

			return;

		}

		audio::seek ( seconds );
		position = audio::position ( );

	}

	int32_t app::keep ( int32_t index ) {

		if ( library::is_catalog ( index ) ) {

			const library::track& item = library::track_at ( index );
			const library::album& group = library::album_at ( item.album );

			return library::save_remote ( library::uri_of ( index ), item.title, group.artist, group.title, item.duration, group.art_path );

		}

		if ( library::is_remote ( index ) ) {

			if ( !remote_state.uri [ 0 ] || library::track_at ( index ).key != library::hash ( remote_state.uri, false ) )
				return -1;

			return library::save_remote ( remote_state.uri, remote_state.title, remote_state.artist, remote_state.album, static_cast < float > ( remote_state.duration ), remote_state.art_url );

		}

		return index;

	}

	void app::like_index ( int32_t index, bool value ) {

		int32_t target = keep ( index );

		if ( target < 0 )
			return;

		library::set_liked ( target, value );

		if ( library::is_remote ( index ) )
			library::track_at ( index ).liked = value;

		if ( value && library::is_saved ( target ) )
			spotify::note_like ( library::uri_of ( target ) );

		if ( live && persist )
			save_state ( );

	}

	void app::play_context ( int32_t index ) {

		const wchar_t* uri = library::uri_of ( index );

		if ( !uri [ 0 ] || !remote_state.connected )
			return;

		if ( playing && !remote_mode ( ) ) {

			audio::play ( false );
			playing = false;

		}

		spotify::open_link ( uri );
		remote_follow = true;
		show_toast ( L"Playing ", library::track_at ( index ).title );

		const library::album& group = library::album_at ( library::track_at ( index ).album );
		bool album = uri [ 8 ] == L'a' && uri [ 9 ] == L'l';

		remember ( album ? recent_kind::REMOTE_ALBUM : recent_kind::REMOTE_ARTIST, library::hash ( uri, false ), uri, library::track_at ( index ).title, album ? group.artist : L"Artist", group.art_path );

	}

	void app::artist_tracks ( int32_t album, base::list < int32_t >& result ) {

		const wchar_t* name = library::album_at ( album ).artist;

		result.clear ( );

		for ( int32_t t : all_songs ) {

			if ( library::compare_text ( library::album_at ( library::track_at ( t ).album ).artist, name ) == 0 )
				result.push ( t );

		}

	}

	uint64_t app::fresh_id ( ) {

		base::random random;

		random.seed ( base::ticks ( ) ^ ( static_cast < uint64_t > ( playlists.count ( ) + 1 ) * 0x9E3779B97F4A7C15ull ) );

		uint64_t value = random.next ( ) ^ random.next ( );

		return value ? value : 1;

	}

	void app::place_recents ( ) {

		int32_t next = 541;

		for ( recent_item& item : recents ) {

			item.slot = -1;

			if ( ( item.kind == recent_kind::REMOTE_ALBUM || item.kind == recent_kind::REMOTE_ARTIST ) && next < 600 ) {

				item.slot = next++;
				library::set_catalog ( item.slot, item.uri.get ( ), item.title.get ( ), item.subtitle.get ( ), item.title.get ( ), 0.0f, item.art.get ( ) );

			}

		}

	}

	void app::remember ( recent_kind kind, uint64_t key, const wchar_t* uri, const wchar_t* title, const wchar_t* subtitle, const wchar_t* art ) {

		if ( !key )
			return;

		for ( int32_t i = 0; i < recents.count ( ); i++ ) {

			if ( recents [ static_cast < size_t > ( i ) ].kind == kind && recents [ static_cast < size_t > ( i ) ].key == key ) {

				recents.remove_at ( static_cast < size_t > ( i ) );

				break;

			}

		}

		recent_item& item = recents.emplace ( );

		item.kind = kind;
		item.key = key;
		item.uri.set ( uri );
		item.title.set ( title );
		item.subtitle.set ( subtitle );
		item.art.set ( art );

		for ( int32_t i = recents.count ( ) - 1; i > 0; i-- )
			base::swap ( recents [ static_cast < size_t > ( i ) ], recents [ static_cast < size_t > ( i - 1 ) ] );

		while ( recents.count ( ) > 40 )
			recents.pop ( );

		place_recents ( );

		if ( live && persist )
			save_state ( );

	}

	void app::play_virtual ( const base::list < int32_t >& list, int32_t start, context from ) {

		base::list < int32_t > kept;
		int32_t at = 0;

		for ( int32_t i = 0; i < list.count ( ) && kept.count ( ) < 160; i++ ) {

			int32_t value = keep ( list [ static_cast < size_t > ( i ) ] );

			if ( value < 0 )
				continue;

			if ( i == start )
				at = kept.count ( );

			kept.push ( value );

		}

		if ( !kept.empty ( ) )
			play_list ( kept, at, from );

	}

	void app::open_detail ( int32_t index, int32_t kind ) {

		const wchar_t* uri = library::uri_of ( index );

		if ( !uri [ 0 ] || !remote_state.connected )
			return;

		if ( here.where != page::DETAIL ) {

			detail_back = here;
			detail_stack.clear ( );
			detail_kinds.clear ( );

		}

		detail_stack.emplace ( ).set ( uri );
		detail_kinds.push ( kind );
		detail_tracks.clear ( );
		detail_albums.clear ( );
		spotify::open_detail ( kind, uri );
		navigate ( page::DETAIL, detail_stack.count ( ) );

	}

	void app::close_detail ( ) {

		if ( detail_stack.count ( ) > 1 ) {

			detail_stack.pop ( );
			detail_kinds.pop ( );
			detail_tracks.clear ( );
			detail_albums.clear ( );
			spotify::open_detail ( detail_kinds.back ( ), detail_stack.back ( ).get ( ) );
			navigate ( page::DETAIL, detail_stack.count ( ) );

			return;

		}

		detail_stack.clear ( );
		detail_kinds.clear ( );
		navigate ( detail_back.where == page::DETAIL ? page::HOME : detail_back.where, detail_back.where == page::PLAYLIST ? detail_back.playlist : detail_back.album );

	}

	void app::ingest_detail ( ) {

		uint32_t revision = spotify::detail_revision ( );

		if ( revision == detail_seen )
			return;

		detail_seen = revision;
		spotify::read_detail ( detail_view );
		detail_tracks.clear ( );
		detail_albums.clear ( );

		for ( int32_t i = 0; i < detail_view.tracks.count ( ) && i < 160; i++ ) {

			const spotify::result& entry = detail_view.tracks [ static_cast < size_t > ( i ) ];
			int32_t slot = 320 + i;

			library::set_catalog ( slot, entry.uri, entry.title, entry.artist, entry.album, static_cast < float > ( entry.duration ), entry.art_url );
			library::track_at ( library::catalog + slot ).number = entry.number;
			detail_tracks.push ( library::catalog + slot );

		}

		for ( int32_t i = 0; i < detail_view.albums.count ( ) && i < 60; i++ ) {

			const spotify::result& entry = detail_view.albums [ static_cast < size_t > ( i ) ];
			int32_t slot = 480 + i;
			int32_t year = 0;

			library::set_catalog ( slot, entry.uri, entry.title, entry.artist, entry.title, 0.0f, entry.art_url );

			for ( int32_t k = 0; k < 4 && entry.album [ k ] >= L'0' && entry.album [ k ] <= L'9'; k++ )
				year = year * 10 + ( entry.album [ k ] - L'0' );

			library::album_at ( library::catalog + slot ).year = year;
			detail_albums.push ( library::catalog + slot );

		}

		library::set_catalog ( 540, detail_view.uri, detail_view.title, detail_view.artist, detail_view.title, 0.0f, detail_view.art_url );
		pending = base::max ( pending, 3 );

	}

	void app::play_catalog ( int32_t index ) {

		for ( int32_t i = 0; i < found_remote.count ( ); i++ ) {

			if ( found_remote [ static_cast < size_t > ( i ) ] == index ) {

				play_virtual ( found_remote, i, { origin::SEARCH, 1 } );

				return;

			}

		}

	}

	void app::toggle_mute ( ) {

		muted = !muted;
		volume_touch = clock;

		if ( remote_mode ( ) )
			spotify::volume ( muted ? 0.0f : volume );
		else
			audio::volume ( muted ? 0.0f : volume );

	}

	void app::toggle_like ( ) {

		if ( track < 0 )
			return;

		like_index ( track, !library::track_at ( track ).liked );

	}

	void app::toggle_shuffle ( ) {

		if ( library::is_remote ( track ) ) {

			spotify::shuffle ( !remote_state.shuffle );
			spotify::read ( remote_state );

			return;

		}

		shuffle = !shuffle;

		if ( !queue.empty ( ) )
			build_order ( order [ static_cast < size_t > ( order_index ) ] );

	}

	void app::cycle_repeat ( ) {

		if ( library::is_remote ( track ) ) {

			spotify::repeat ( ( remote_state.repeat + 1 ) % 3 );
			spotify::read ( remote_state );

			return;

		}

		repeat = static_cast < repeat_mode > ( ( static_cast < int32_t > ( repeat ) + 1 ) % 3 );

	}

	bool app::shuffle_on ( ) const {

		return library::is_remote ( track ) ? remote_state.shuffle : shuffle;

	}

	repeat_mode app::repeat_on ( ) const {

		return library::is_remote ( track ) ? static_cast < repeat_mode > ( base::clamp ( remote_state.repeat, 0, 2 ) ) : repeat;

	}

	void app::paste_link ( ) {

		wchar_t text [ 1024 ];

		if ( !read_clipboard ( text, 1024 ) || !spotify::is_link ( text ) ) {

			show_toast ( L"Copy a song link, then press Ctrl+V" );

			return;

		}

		if ( !remote_state.connected ) {

			spotify_open = true;

			return;

		}

		if ( playing && !remote_mode ( ) ) {

			audio::play ( false );
			playing = false;

		}

		spotify::open_link ( text );
		remote_follow = true;
		show_toast ( L"Loading song" );

	}

	void app::play_next ( int32_t index ) {

		index = keep ( index );

		if ( index < 0 )
			return;

		if ( track < 0 || library::is_remote ( track ) || queue.empty ( ) ) {

			base::list < int32_t > single;

			single.push ( index );
			play_list ( single, 0, context ( ) );

			return;

		}

		queue.push ( index );
		order.push ( queue.count ( ) - 1 );

		for ( int32_t i = order.count ( ) - 1; i > order_index + 1; i-- )
			base::swap ( order [ static_cast < size_t > ( i ) ], order [ static_cast < size_t > ( i - 1 ) ] );

		show_toast ( L"Playing next: ", library::track_at ( index ).title );

	}

	void app::add_to_playlist ( int32_t list, int32_t index ) {

		index = keep ( index );

		if ( list < 0 || list >= playlists.count ( ) || index < 0 )
			return;

		playlist& target = playlists [ static_cast < size_t > ( list ) ];
		uint64_t key = library::track_at ( index ).key;

		for ( uint64_t value : target.keys ) {

			if ( value == key ) {

				show_toast ( L"Already in ", target.title.get ( ) );

				return;

			}

		}

		target.keys.push ( key );
		show_toast ( L"Added to ", target.title.get ( ) );

		if ( live && persist )
			save_state ( );

	}

	void app::remove_from_playlist ( int32_t list, int32_t index ) {

		if ( list < 0 || list >= playlists.count ( ) || index < 0 )
			return;

		playlist& target = playlists [ static_cast < size_t > ( list ) ];
		uint64_t key = library::track_at ( index ).key;
		int32_t row = -1;

		for ( int32_t i = 0; i < target.keys.count ( ) && row < 0; i++ ) {

			if ( target.keys [ static_cast < size_t > ( i ) ] == key )
				row = i;

		}

		if ( row < 0 )
			return;

		target.keys.remove_at ( static_cast < size_t > ( row ) );
		show_toast ( L"Removed from ", target.title.get ( ) );

		if ( live && persist )
			save_state ( );

	}

	void app::delete_playlist ( int32_t list ) {

		if ( list < 0 || list >= playlists.count ( ) )
			return;

		wchar_t title [ 160 ];
		const wchar_t* source = playlists [ static_cast < size_t > ( list ) ].title.get ( );
		int32_t length = 0;

		for ( ; source [ length ] && length < 159; length++ )
			title [ length ] = source [ length ];

		title [ length ] = 0;

		if ( focus_id == make_id ( "title" ) )
			focus_id = 0;

		uint64_t gone = playlists [ static_cast < size_t > ( list ) ].id;

		for ( int32_t i = recents.count ( ) - 1; i >= 0; i-- ) {

			if ( recents [ static_cast < size_t > ( i ) ].kind == recent_kind::PLAYLIST && recents [ static_cast < size_t > ( i ) ].key == gone )
				recents.remove_at ( static_cast < size_t > ( i ) );

		}

		playlists.remove_at ( static_cast < size_t > ( list ) );

		context* sources [ 2 ] = { &queue_from, &local_from };

		for ( context* value : sources ) {

			if ( value->kind != origin::PLAYLIST )
				continue;

			if ( value->index == list )
				value->kind = origin::NONE;
			else if ( value->index > list )
				value->index--;

		}

		if ( here.where == page::PLAYLIST && here.playlist == list ) {

			here.where = page::HOME;
			navigate ( page::HOME );
			previous_place = here;

		} else if ( here.playlist > list )
			here.playlist--;

		if ( previous_place.where == page::PLAYLIST )
			previous_place.where = page::HOME;

		if ( before_now.where == page::PLAYLIST )
			before_now.where = page::HOME;

		show_toast ( L"Deleted ", title );

		if ( live && persist )
			save_state ( );

	}

	void app::playlist_tracks ( int32_t list, base::list < int32_t >& result ) {

		result.clear ( );

		if ( list < 0 || list >= playlists.count ( ) )
			return;

		for ( uint64_t key : playlists [ static_cast < size_t > ( list ) ].keys ) {

			int32_t found = lookup_track ( key );

			if ( found >= 0 )
				result.push ( found );

		}

	}

	int32_t app::create_playlist ( const wchar_t* title ) {

		playlist& list = playlists.emplace ( );

		list.id = fresh_id ( );

		if ( title && title [ 0 ] )
			list.title.set ( title );
		else {

			for ( int32_t n = 1; n < 1000; n++ ) {

				base::writer name;
				bool taken = false;

				name.add ( L"New Playlist" );

				if ( n > 1 )
					name.add ( L" " ).number ( n );

				for ( int32_t i = 0; i < playlists.count ( ) - 1; i++ )
					taken = taken || base::same_text ( playlists [ static_cast < size_t > ( i ) ].title.get ( ), name.text ( ) );

				if ( !taken ) {

					list.title.set ( name.text ( ) );

					break;

				}

			}

		}

		int32_t index = playlists.count ( ) - 1;

		navigate ( page::PLAYLIST, index );

		if ( !title )
			focus_field ( make_id ( "title" ), title_field, playlists [ static_cast < size_t > ( index ) ].title.get ( ), true );

		title_list = index;
		remember ( recent_kind::PLAYLIST, list.id, L"", list.title.get ( ), L"Playlist", L"" );
		rail_scroll_target = 0.0f;

		if ( live && persist )
			save_state ( );

		return index;

	}

	void app::show_toast ( const wchar_t* first, const wchar_t* second ) {

		base::writer line;

		line.add ( first );

		if ( second )
			line.add ( second );

		int32_t i = 0;

		for ( ; i < line.size ( ) && i < 159; i++ )
			toast_text [ i ] = line.text ( ) [ i ];

		toast_text [ i ] = 0;
		toast_time = clock;
		pending = base::max ( pending, 3 );

	}

	int32_t app::lookup_track ( uint64_t key ) const {

		int32_t room = key_table.count ( );

		if ( !key )
			return -1;

		int32_t slot = static_cast < int32_t > ( ( key * 0x9E3779B97F4A7C15ull ) >> 40 ) & ( room - 1 );

		for ( int32_t probe = 0; probe < room; probe++ ) {

			uint64_t value = key_table [ static_cast < size_t > ( slot ) ];

			if ( value == key )
				return key_slots [ static_cast < size_t > ( slot ) ];

			if ( value == 0 )
				break;

			slot = ( slot + 1 ) & ( room - 1 );

		}

		return library::find_saved ( key );

	}

	void app::rebuild_songs ( ) {

		int32_t count = library::track_count ( );
		int32_t room = 16;

		all_songs.clear ( );

		for ( int32_t i = 0; i < count; i++ )
			all_songs.push ( i );

		merge_sort ( all_songs, [ ] ( int32_t a, int32_t b ) {

			const library::track& x = library::track_at ( a );
			const library::track& y = library::track_at ( b );
			int32_t order_by = library::compare_text ( x.title, y.title );

			if ( order_by == 0 )
				order_by = library::compare_text ( library::album_at ( x.album ).artist, library::album_at ( y.album ).artist );

			return order_by < 0;

		} );

		while ( room < count * 2 )
			room *= 2;

		key_table.fill ( static_cast < size_t > ( room ), 0 );
		key_slots.fill ( static_cast < size_t > ( room ), -1 );

		for ( int32_t i = 0; i < count; i++ ) {

			uint64_t key = library::track_at ( i ).key;

			if ( !key )
				continue;

			int32_t slot = static_cast < int32_t > ( ( key * 0x9E3779B97F4A7C15ull ) >> 40 ) & ( room - 1 );

			while ( key_table [ static_cast < size_t > ( slot ) ] && key_table [ static_cast < size_t > ( slot ) ] != key )
				slot = ( slot + 1 ) & ( room - 1 );

			key_table [ static_cast < size_t > ( slot ) ] = key;
			key_slots [ static_cast < size_t > ( slot ) ] = i;

		}

		songs_revision++;

	}

	void app::type_search ( const wchar_t* text ) {

		int32_t i = 0;

		for ( ; text && text [ i ] && i < 127; i++ )
			search_field.text [ i ] = text [ i ];

		search_field.text [ i ] = 0;
		field_set ( search_field, search_field.text, clock, false );
		search_field.typed = clock - 1.0;

	}

	void app::run_search ( ) {

		const wchar_t* query = search_field.text;

		while ( *query == L' ' )
			query++;

		int32_t query_length = base::text_length ( query );

		while ( query_length > 0 && query [ query_length - 1 ] == L' ' )
			query_length--;

		bool want_remote = remote_state.connected && query_length >= 2;

		if ( remote_search_sent != search_field.revision ) {

			if ( !want_remote ) {

				if ( remote_state.connected && remote_known > 0 )
					spotify::search ( L"" );

				remote_search_sent = search_field.revision;

			} else if ( clock - search_field.typed > 0.3 ) {

				wchar_t trimmed [ 128 ];

				for ( int32_t i = 0; i < query_length; i++ )
					trimmed [ i ] = query [ i ];

				trimmed [ query_length ] = 0;
				spotify::search ( trimmed );
				remote_search_sent = search_field.revision;
				remote_loading = true;

			} else
				animating = true;

		}

		if ( remote_state.connected ) {

			spotify::page info = spotify::results ( );

			if ( info.generation != remote_generation ) {

				remote_generation = info.generation;
				remote_known = 0;
				found_remote.clear ( );
				found_remote_albums.clear ( );
				found_remote_artists.clear ( );

			}

			base::list < int32_t >* extras [ 2 ] = { &found_remote_albums, &found_remote_artists };
			int32_t extra_counts [ 2 ] = { info.albums, info.artists };

			for ( int32_t group = 0; group < 2; group++ ) {

				while ( extras [ group ]->count ( ) < base::min ( extra_counts [ group ], 10 ) ) {

					spotify::result entry;
					int32_t index = extras [ group ]->count ( );
					int32_t slot = 300 + group * 10 + index;

					if ( !spotify::extra_at ( group, index, entry ) )
						break;

					library::set_catalog ( slot, entry.uri, entry.title, group == 0 ? entry.artist : L"Artist", entry.title, 0.0f, entry.art_url );

					if ( group == 0 && entry.album [ 0 ] ) {

						int32_t year = 0;

						for ( int32_t k = 0; k < 4 && entry.album [ k ] >= L'0' && entry.album [ k ] <= L'9'; k++ )
							year = year * 10 + ( entry.album [ k ] - L'0' );

						library::album_at ( library::catalog + slot ).year = year;

					}

					extras [ group ]->push ( library::catalog + slot );

				}

			}

			while ( remote_known < info.count && remote_known < 300 ) {

				spotify::result entry;

				if ( !spotify::result_at ( remote_known, entry ) )
					break;

				library::set_catalog ( remote_known, entry.uri, entry.title, entry.artist, entry.album, static_cast < float > ( entry.duration ), entry.art_url );
				found_remote.push ( library::catalog + remote_known );
				remote_known++;

			}

			remote_more = info.more;

			if ( remote_search_sent == search_field.revision )
				remote_loading = info.loading;

		} else if ( remote_known > 0 ) {

			remote_known = 0;
			found_remote.clear ( );

		}

		if ( search_done == search_field.revision )
			return;

		search_done = search_field.revision;
		found_songs.clear ( );
		found_albums.clear ( );
		found_lists.clear ( );

		if ( query_length == 0 ) {

			search_kind = 0;

			if ( here.where == page::SEARCH ) {

				location back = before_search;

				navigate ( back.where == page::SEARCH ? page::HOME : back.where, back.where == page::PLAYLIST ? back.playlist : back.album );

			}

			return;

		}

		if ( here.where != page::SEARCH )
			navigate ( page::SEARCH );

		int32_t starts_at [ 8 ];
		int32_t lengths [ 8 ];
		int32_t tokens = 0;

		for ( int32_t i = 0; i < query_length && tokens < 8; ) {

			while ( i < query_length && query [ i ] == L' ' )
				i++;

			int32_t begin = i;

			while ( i < query_length && query [ i ] != L' ' )
				i++;

			if ( i > begin ) {

				starts_at [ tokens ] = begin;
				lengths [ tokens ] = i - begin;
				tokens++;

			}

		}

		base::list < int32_t > buckets [ 3 ];

		for ( int32_t t : all_songs ) {

			const library::track& item = library::track_at ( t );
			const library::album& album = library::album_at ( item.album );
			bool every = true;

			for ( int32_t k = 0; k < tokens && every; k++ ) {

				const wchar_t* token = query + starts_at [ k ];

				every = contains ( item.title, token, lengths [ k ] ) || contains ( album.artist, token, lengths [ k ] ) || contains ( album.title, token, lengths [ k ] );

			}

			if ( !every )
				continue;

			int32_t rank = starts ( item.title, query, query_length ) ? 0 : ( contains ( item.title, query, query_length ) || starts ( album.artist, query, query_length ) ? 1 : 2 );

			buckets [ rank ].push ( t );

		}

		for ( int32_t b = 0; b < 3; b++ ) {

			for ( int32_t t : buckets [ b ] ) {

				if ( found_songs.count ( ) < 200 )
					found_songs.push ( t );

			}

		}

		for ( int32_t a = 0; a < library::album_count ( ) && found_albums.count ( ) < 24; a++ ) {

			const library::album& album = library::album_at ( a );
			bool every = true;

			for ( int32_t k = 0; k < tokens && every; k++ ) {

				const wchar_t* token = query + starts_at [ k ];

				every = contains ( album.title, token, lengths [ k ] ) || contains ( album.artist, token, lengths [ k ] );

			}

			if ( every )
				found_albums.push ( a );

		}

		found_artists.clear ( );

		for ( int32_t a = 0; a < library::album_count ( ) && found_artists.count ( ) < 24; a++ ) {

			const library::album& album = library::album_at ( a );
			bool every = album.artist [ 0 ] != 0;
			bool known = false;

			for ( int32_t k = 0; k < tokens && every; k++ )
				every = contains ( album.artist, query + starts_at [ k ], lengths [ k ] );

			for ( int32_t other : found_artists )
				known = known || library::compare_text ( library::album_at ( other ).artist, album.artist ) == 0;

			if ( every && !known )
				found_artists.push ( a );

		}

		for ( int32_t p = 0; p < playlists.count ( ); p++ ) {

			if ( contains ( playlists [ static_cast < size_t > ( p ) ].title.get ( ), query, query_length ) )
				found_lists.push ( p );

		}

		scroll_target = 0.0f;
		pending = base::max ( pending, 3 );

	}

}