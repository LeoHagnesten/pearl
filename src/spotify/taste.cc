#include <windows.h>
#include "spotify/spotify.hh"
#include "spotify/web.hh"
#include "base/files.hh"
#include "base/math.hh"

namespace spotify {

	struct listen {

		char kind = 'P';
		int64_t stamp = 0;
		base::text uri;
		base::text title;
		base::text names;
		base::text ids;
		base::text album_uri;
		base::text album;
		base::text art;
		int32_t duration = 0;

	};

	struct taste {

		uint64_t key = 0;
		base::text id;
		base::text name;
		base::text image;
		base::text genre;
		base::text reason;
		float affinity = 0.0f;
		float pull = 0.0f;
		bool seed = false;
		bool scouted = false;

	};

	struct release {

		base::text uri;
		base::text title;
		base::text artist;
		base::text artist_id;
		base::text type;
		base::text date;
		base::text art;
		int64_t day = 0;
		int32_t total = 0;
		bool discovery = false;
		float score = 0.0f;

	};

	struct candidate {

		result item;
		uint64_t artist = 0;
		uint64_t album = 0;
		uint64_t song = 0;
		int64_t day = 0;
		float score = 0.0f;
		bool discovery = false;
		bool taken = false;

	};

	struct favorite {

		listen meta;
		uint64_t key = 0;
		float score = 0.0f;

	};

	static SRWLOCK history_lock = SRWLOCK_INIT;
	static SRWLOCK home_lock = SRWLOCK_INIT;
	static base::list < listen > listens;
	static home home_data;
	static HANDLE home_thread = nullptr;
	static HANDLE home_wake = nullptr;
	static base::text wanted_liked;
	static bool wanted_build = false;
	static bool wanted_force = false;
	static base::list < base::text > like_queue;
	static uint32_t shuffle_count = 0;
	static wchar_t heard_uri [ 64 ] = { };
	static bool heard_counted = false;
	static double heard_peak = 0.0;
	static listen heard_meta;

	int64_t wall_seconds ( ) {

		FILETIME now = { };

		GetSystemTimeAsFileTime ( &now );

		uint64_t value = ( static_cast < uint64_t > ( now.dwHighDateTime ) << 32 ) | now.dwLowDateTime;

		return static_cast < int64_t > ( value / 10000000ull ) - 11644473600ll;

	}

	static void copy ( wchar_t* target, int32_t room, const wchar_t* source ) {

		int32_t i = 0;

		for ( ; source && source [ i ] && i < room - 1; i++ )
			target [ i ] = source [ i ];

		target [ i ] = 0;

	}

	static const wchar_t* id_of ( const wchar_t* uri ) {

		const wchar_t* id = uri;

		for ( const wchar_t* at = uri; at && *at; at++ ) {

			if ( *at == L':' )
				id = at + 1;

		}

		return id;

	}

	static bool begins ( const wchar_t* text, const wchar_t* prefix ) {

		for ( int32_t i = 0; prefix [ i ]; i++ ) {

			if ( text [ i ] != prefix [ i ] )
				return false;

		}

		return true;

	}

	static bool mentions ( const wchar_t* text, const wchar_t* word ) {

		int32_t length = base::text_length ( word );

		for ( int32_t i = 0; text && text [ i ]; i++ ) {

			int32_t k = 0;

			while ( k < length && text [ i + k ] && ( text [ i + k ] | 32 ) == ( word [ k ] | 32 ) )
				k++;

			if ( k == length )
				return true;

		}

		return false;

	}

	static int64_t day_number ( int32_t year, int32_t month, int32_t day ) {

		year -= month <= 2 ? 1 : 0;

		int64_t era = ( year >= 0 ? year : year - 399 ) / 400;
		int64_t year_of_era = year - era * 400;
		int64_t day_of_year = ( 153 * ( month + ( month > 2 ? -3 : 9 ) ) + 2 ) / 5 + day - 1;
		int64_t day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;

		return era * 146097 + day_of_era - 719468;

	}

	static int64_t parse_day ( const wchar_t* date, int32_t* clock = nullptr ) {

		int32_t parts [ 6 ] = { 0, 7, 1, 0, 0, 0 };
		int32_t count = 0;

		for ( const wchar_t* at = date; at && *at && count < 6; ) {

			if ( *at < L'0' || *at > L'9' )
				break;

			int32_t value = 0;

			while ( *at >= L'0' && *at <= L'9' )
				value = value * 10 + ( *at++ - L'0' );

			parts [ count++ ] = value;

			if ( *at == L'-' || *at == L'T' || *at == L':' )
				at++;
			else
				break;

		}

		if ( count == 0 || parts [ 0 ] < 1900 )
			return 0;

		if ( clock )
			*clock = parts [ 3 ] * 3600 + parts [ 4 ] * 60 + parts [ 5 ];

		return day_number ( parts [ 0 ], base::clamp ( parts [ 1 ], 1, 12 ), base::clamp ( parts [ 2 ], 1, 31 ) );

	}

	static void data_path ( const wchar_t* name, wchar_t* path ) {

		base::writer line;

		line.add ( files::folder ( ) ).add ( name );

		for ( int32_t i = 0; i <= line.size ( ); i++ )
			path [ i ] = line.text ( ) [ i ];

	}

	static void put_field ( base::list < char >& out, const wchar_t* value ) {

		char utf8 [ 1600 ];
		int32_t length = value && value [ 0 ] ? WideCharToMultiByte ( CP_UTF8, 0, value, -1, utf8, 1599, nullptr, nullptr ) : 0;

		for ( int32_t k = 0; k < length - 1; k++ )
			out.push ( utf8 [ k ] == '\t' || utf8 [ k ] == '\n' || utf8 [ k ] == '\r' ? ' ' : utf8 [ k ] );

	}

	static void put_integer ( base::list < char >& out, int64_t value ) {

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

	static int64_t read_integer ( const wchar_t* text ) {

		bool negative = *text == L'-';
		int64_t value = 0;

		if ( negative )
			text++;

		while ( *text >= L'0' && *text <= L'9' )
			value = value * 10 + ( *text++ - L'0' );

		return negative ? -value : value;

	}

	static int32_t read_fields ( const char*& cursor, wchar_t ( *fields ) [ 512 ], int32_t room ) {

		int32_t count = 0;

		while ( *cursor && *cursor != '\n' ) {

			const char* start = cursor;

			while ( *cursor && *cursor != '\t' && *cursor != '\n' && *cursor != '\r' )
				cursor++;

			if ( count < room ) {

				int32_t bytes = static_cast < int32_t > ( cursor - start );
				int32_t made = bytes > 0 ? MultiByteToWideChar ( CP_UTF8, 0, start, bytes, fields [ count ], 511 ) : 0;

				fields [ count ] [ made > 0 ? made : 0 ] = 0;
				count++;

			}

			if ( *cursor == '\t' )
				cursor++;
			else if ( *cursor == '\r' )
				cursor++;

		}

		if ( *cursor == '\n' )
			cursor++;

		return count;

	}

	static void put_listen ( base::list < char >& out, const listen& entry ) {

		const wchar_t* fields [ 7 ] = { entry.uri.get ( ), entry.title.get ( ), entry.names.get ( ), entry.ids.get ( ), entry.album_uri.get ( ), entry.album.get ( ), entry.art.get ( ) };

		out.push ( entry.kind );
		out.push ( '\t' );
		put_integer ( out, entry.stamp );

		for ( const wchar_t* field : fields ) {

			out.push ( '\t' );
			put_field ( out, field );

		}

		out.push ( '\t' );
		put_integer ( out, entry.duration );
		out.push ( '\n' );

	}

	static void append_listen ( const listen& entry ) {

		base::list < char > line;
		wchar_t path [ 600 ];

		put_listen ( line, entry );
		data_path ( L"\\listens.txt", path );

		HANDLE file = CreateFileW ( path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );

		if ( file == INVALID_HANDLE_VALUE )
			return;

		DWORD done = 0;

		WriteFile ( file, line.data ( ), static_cast < DWORD > ( line.count ( ) ), &done, nullptr );
		CloseHandle ( file );

	}

	static void load_listens ( ) {

		base::list < char > data;
		wchar_t path [ 600 ];

		data_path ( L"\\listens.txt", path );

		if ( !files::read ( path, data ) || data.count ( ) < 2 )
			return;

		base::list < listen > loaded;
		const char* cursor = data.data ( );
		wchar_t fields [ 10 ] [ 512 ];

		while ( *cursor ) {

			if ( read_fields ( cursor, fields, 10 ) < 10 )
				continue;

			listen& entry = loaded.emplace ( );

			entry.kind = static_cast < char > ( fields [ 0 ] [ 0 ] );
			entry.stamp = read_integer ( fields [ 1 ] );
			entry.uri.set ( fields [ 2 ] );
			entry.title.set ( fields [ 3 ] );
			entry.names.set ( fields [ 4 ] );
			entry.ids.set ( fields [ 5 ] );
			entry.album_uri.set ( fields [ 6 ] );
			entry.album.set ( fields [ 7 ] );
			entry.art.set ( fields [ 8 ] );
			entry.duration = static_cast < int32_t > ( read_integer ( fields [ 9 ] ) );

		}

		bool trim = loaded.count ( ) > 3500;

		while ( loaded.count ( ) > 3000 )
			loaded.remove_at ( 0 );

		if ( trim ) {

			base::list < char > out;

			for ( const listen& entry : loaded )
				put_listen ( out, entry );

			files::write ( path, out.data ( ), out.count ( ) );

		}

		AcquireSRWLockExclusive ( &history_lock );

		for ( const listen& entry : listens )
			loaded.push ( entry );

		listens = base::move ( loaded );
		ReleaseSRWLockExclusive ( &history_lock );

	}

	static void record ( char kind, const listen& source ) {

		listen entry = source;

		entry.kind = kind;
		entry.stamp = wall_seconds ( );

		if ( entry.uri.empty ( ) )
			return;

		AcquireSRWLockExclusive ( &history_lock );
		listens.push ( entry );

		if ( listens.count ( ) > 3200 )
			listens.remove_at ( 0 );

		ReleaseSRWLockExclusive ( &history_lock );
		append_listen ( entry );

	}

	static void add_clean ( base::writer& out, const wchar_t* text ) {

		for ( int32_t i = 0; text && text [ i ]; i++ )
			out.add ( text [ i ] == L'|' ? L'/' : text [ i ] );

	}

	static void read_track ( const json::document& doc, int32_t item, listen& out ) {

		int32_t album = doc.find ( item, "album" );
		base::writer names;
		base::writer ids;

		for ( int32_t artist = doc.first ( doc.find ( item, "artists" ) ); artist >= 0; artist = doc.next ( artist ) ) {

			if ( names.size ( ) ) {

				names.add ( L'|' );
				ids.add ( L'|' );

			}

			add_clean ( names, doc.text ( doc.find ( artist, "name" ) ) );
			add_clean ( ids, doc.text ( doc.find ( artist, "id" ) ) );

		}

		out.uri.set ( doc.text ( doc.find ( item, "uri" ) ) );
		out.title.set ( doc.text ( doc.find ( item, "name" ) ) );
		out.names.set ( names.text ( ) );
		out.ids.set ( ids.text ( ) );
		out.album_uri.set ( doc.text ( doc.find ( album, "uri" ) ) );
		out.album.set ( doc.text ( doc.find ( album, "name" ) ) );
		out.art.set ( doc.text ( doc.find ( pick_image ( doc, doc.find ( album, "images" ), 300.0 ), "url" ) ) );
		out.duration = static_cast < int32_t > ( doc.number ( doc.find ( item, "duration_ms" ) ) );

	}

	static bool segment ( const wchar_t* text, int32_t index, wchar_t* out, int32_t room ) {

		int32_t at = 0;

		for ( int32_t k = 0; k < index; k++ ) {

			while ( text [ at ] && text [ at ] != L'|' )
				at++;

			if ( !text [ at ] )
				return false;

			at++;

		}

		int32_t length = 0;

		while ( text [ at ] && text [ at ] != L'|' && length < room - 1 )
			out [ length++ ] = text [ at++ ];

		out [ length ] = 0;

		return length > 0;

	}

	static void display_names ( const wchar_t* text, wchar_t* out, int32_t room ) {

		int32_t length = 0;

		for ( int32_t i = 0; text [ i ] && length < room - 3; i++ ) {

			if ( text [ i ] == L'|' ) {

				out [ length++ ] = L',';
				out [ length++ ] = L' ';

			} else
				out [ length++ ] = text [ i ];

		}

		out [ length ] = 0;

	}

	static void listen_result ( const listen& source, result& out ) {

		copy ( out.title, 160, source.title.get ( ) );
		display_names ( source.names.get ( ), out.artist, 160 );
		copy ( out.album, 160, source.album.get ( ) );
		copy ( out.uri, 64, source.uri.get ( ) );
		copy ( out.art_url, 256, source.art.get ( ) );
		copy ( out.context, 64, source.album_uri.get ( ) );
		out.duration = static_cast < double > ( source.duration ) / 1000.0;
		out.art = out.art_url [ 0 ] ? library::hash ( out.art_url, false ) : 0;

	}

	void observe ( const json::document& doc, int32_t item, double progress, bool playing ) {

		const wchar_t* uri = doc.text ( doc.find ( item, "uri" ) );

		if ( !begins ( uri, L"spotify:track:" ) )
			return;

		if ( !base::same_text ( uri, heard_uri ) ) {

			if ( heard_uri [ 0 ] && !heard_counted && heard_peak > 3.0 && heard_peak < 25.0 )
				record ( 'S', heard_meta );

			copy ( heard_uri, 64, uri );
			heard_counted = false;
			heard_peak = 0.0;
			heard_meta = listen ( );
			read_track ( doc, item, heard_meta );

		}

		heard_peak = base::max ( heard_peak, progress );

		double length = static_cast < double > ( heard_meta.duration ) / 1000.0;

		if ( !heard_counted && playing && length > 0.0 && progress >= base::min ( 30.0, length * 0.5 ) ) {

			heard_counted = true;
			record ( 'P', heard_meta );

		}

	}

	struct stored {

		base::text path;
		int64_t stamp = 0;
		base::list < char > body;

	};

	static SRWLOCK cache_lock = SRWLOCK_INIT;
	static base::list < stored* > responses;
	static bool responses_dirty = false;
	static int64_t throttled [ 6 ] = { };

	static int64_t lifetime ( const wchar_t* path ) {

		if ( begins ( path, L"/albums/" ) || begins ( path, L"/tracks/" ) )
			return 30ll * 86400;

		if ( begins ( path, L"/artists/" ) )
			return mentions ( path, L"/albums" ) ? 3ll * 86400 : 7ll * 86400;

		if ( begins ( path, L"/me/top/" ) )
			return 6ll * 3600;

		if ( begins ( path, L"/search" ) )
			return 86400;

		return 0;

	}

	static int32_t family_of ( const wchar_t* path ) {

		if ( begins ( path, L"/artists/" ) )
			return mentions ( path, L"/albums" ) ? 0 : 1;

		if ( begins ( path, L"/albums/" ) || begins ( path, L"/tracks/" ) )
			return 2;

		if ( begins ( path, L"/me/" ) )
			return 3;

		return begins ( path, L"/search" ) ? 4 : 5;

	}

	static bool from_cache ( const wchar_t* path, int64_t life, json::document& doc ) {

		int64_t now = wall_seconds ( );
		bool hit = false;

		AcquireSRWLockShared ( &cache_lock );

		for ( const stored* entry : responses ) {

			if ( !base::same_text ( entry->path.get ( ), path ) )
				continue;

			if ( life < 0 || now - entry->stamp < life )
				hit = doc.parse ( entry->body.data ( ), entry->body.count ( ) );

			break;

		}

		ReleaseSRWLockShared ( &cache_lock );

		return hit;

	}

	static void to_cache ( const wchar_t* path, const base::list < char >& body ) {

		AcquireSRWLockExclusive ( &cache_lock );

		stored* entry = nullptr;

		for ( stored* item : responses ) {

			if ( base::same_text ( item->path.get ( ), path ) )
				entry = item;

		}

		if ( !entry ) {

			if ( responses.count ( ) >= 400 ) {

				int32_t oldest = 0;

				for ( int32_t i = 1; i < responses.count ( ); i++ ) {

					if ( responses [ static_cast < size_t > ( i ) ]->stamp < responses [ static_cast < size_t > ( oldest ) ]->stamp )
						oldest = i;

				}

				delete responses [ static_cast < size_t > ( oldest ) ];
				responses.remove_at ( static_cast < size_t > ( oldest ) );

			}

			entry = new stored ( );
			entry->path.set ( path );
			responses.push ( entry );

		}

		entry->stamp = wall_seconds ( );
		entry->body = body;
		responses_dirty = true;
		ReleaseSRWLockExclusive ( &cache_lock );

	}

	static void save_responses ( ) {

		base::list < char > out;
		wchar_t path [ 600 ];

		AcquireSRWLockShared ( &cache_lock );

		if ( !responses_dirty ) {

			ReleaseSRWLockShared ( &cache_lock );

			return;

		}

		for ( const stored* entry : responses ) {

			put_field ( out, entry->path.get ( ) );
			out.push ( '\t' );
			put_integer ( out, entry->stamp );
			out.push ( '\t' );
			put_integer ( out, entry->body.count ( ) );
			out.push ( '\n' );

			for ( char value : entry->body )
				out.push ( value );

			out.push ( '\n' );

		}

		ReleaseSRWLockShared ( &cache_lock );
		AcquireSRWLockExclusive ( &cache_lock );
		responses_dirty = false;
		ReleaseSRWLockExclusive ( &cache_lock );
		data_path ( L"\\responses.cache", path );
		files::write ( path, out.data ( ), out.count ( ) );

	}

	static void load_responses ( ) {

		base::list < char > data;
		wchar_t path [ 600 ];

		data_path ( L"\\responses.cache", path );

		if ( !files::read ( path, data ) || data.count ( ) < 2 )
			return;

		const char* cursor = data.data ( );
		const char* end = data.data ( ) + data.count ( ) - 1;
		int64_t now = wall_seconds ( );

		while ( cursor < end ) {

			wchar_t fields [ 3 ] [ 512 ];

			if ( read_fields ( cursor, fields, 3 ) < 3 )
				break;

			int64_t stamp = read_integer ( fields [ 1 ] );
			int64_t size = read_integer ( fields [ 2 ] );

			if ( size < 0 || cursor + size > end )
				break;

			if ( now - stamp < 30ll * 86400 ) {

				stored* entry = new stored ( );

				entry->path.set ( fields [ 0 ] );
				entry->stamp = stamp;
				entry->body.resize ( static_cast < size_t > ( size ) );

				for ( int64_t i = 0; i < size; i++ )
					entry->body [ static_cast < size_t > ( i ) ] = cursor [ i ];

				responses.push ( entry );

			}

			cursor += size;

			if ( cursor < end && *cursor == '\n' )
				cursor++;

		}

	}

	static bool get_json ( const wchar_t* path, json::document& doc, int32_t& status ) {

		int64_t life = lifetime ( path );
		int32_t family = family_of ( path );
		net::response reply;

		status = 0;

		if ( life > 0 && from_cache ( path, life, doc ) ) {

			status = 200;

			return true;

		}

		if ( static_cast < int64_t > ( GetTickCount64 ( ) ) < throttled [ family ] ) {

			status = 429;

			return life > 0 && from_cache ( path, -1, doc );

		}

		if ( !api ( L"GET", path, nullptr, reply ) )
			return life > 0 && from_cache ( path, -1, doc );

		status = reply.status;

		if ( reply.status == 429 ) {

			throttled [ family ] = static_cast < int64_t > ( GetTickCount64 ( ) ) + static_cast < int64_t > ( base::clamp ( reply.retry_after, 30, 3600 ) ) * 1000;

			return life > 0 && from_cache ( path, -1, doc );

		}

		bool parsed = !reply.body.empty ( ) && doc.parse ( reply.body.data ( ), reply.body.count ( ) );

		if ( parsed && reply.status == 200 ) {

			if ( life > 0 )
				to_cache ( path, reply.body );

			return true;

		}

		return reply.status >= 500 && life > 0 && from_cache ( path, -1, doc );

	}

	bool fetch_json ( const wchar_t* path, json::document& doc ) {

		int32_t status = 0;

		return get_json ( path, doc, status );

	}

	static bool resolve_track ( const wchar_t* uri, listen& out ) {

		base::writer path;
		json::document doc;
		int32_t status = 0;

		path.add ( L"/tracks/" ).add ( id_of ( uri ) );

		if ( !get_json ( path.text ( ), doc, status ) )
			return false;

		read_track ( doc, doc.root ( ), out );

		return !out.uri.empty ( );

	}

	static int32_t find_taste ( const base::list < taste >& table, uint64_t key ) {

		for ( int32_t i = 0; i < table.count ( ); i++ ) {

			if ( table [ static_cast < size_t > ( i ) ].key == key )
				return i;

		}

		return -1;

	}

	static int32_t add_taste ( base::list < taste >& table, const wchar_t* id, const wchar_t* name ) {

		if ( !id || !id [ 0 ] )
			return -1;

		uint64_t key = library::hash ( id, false );
		int32_t found = find_taste ( table, key );

		if ( found >= 0 ) {

			if ( table [ static_cast < size_t > ( found ) ].name.empty ( ) && name )
				table [ static_cast < size_t > ( found ) ].name.set ( name );

			return found;

		}

		taste& entry = table.emplace ( );

		entry.key = key;
		entry.id.set ( id );
		entry.name.set ( name );

		return table.count ( ) - 1;

	}

	static void credit ( base::list < taste >& table, const listen& entry, float weight ) {

		wchar_t id [ 64 ];
		wchar_t name [ 200 ];

		for ( int32_t k = 0; k < 6 && segment ( entry.ids.get ( ), k, id, 64 ); k++ ) {

			if ( !segment ( entry.names.get ( ), k, name, 200 ) )
				name [ 0 ] = 0;

			int32_t at = add_taste ( table, id, name );

			if ( at >= 0 )
				table [ static_cast < size_t > ( at ) ].affinity += weight * ( k == 0 ? 1.0f : 0.35f );

		}

	}

	static void adore ( base::list < favorite >& favorites, const listen& entry, float weight ) {

		uint64_t key = library::hash ( entry.uri.get ( ), false );

		for ( favorite& item : favorites ) {

			if ( item.key == key ) {

				item.score += weight;

				return;

			}

		}

		favorite& item = favorites.emplace ( );

		item.meta = entry;
		item.key = key;
		item.score = weight;

	}

	static bool contains ( const base::list < uint64_t >& values, uint64_t key ) {

		for ( uint64_t value : values ) {

			if ( value == key )
				return true;

		}

		return false;

	}

	static void read_release ( const json::document& doc, int32_t item, release& out ) {

		int32_t artist = doc.first ( doc.find ( item, "artists" ) );

		out.uri.set ( doc.text ( doc.find ( item, "uri" ) ) );
		out.title.set ( doc.text ( doc.find ( item, "name" ) ) );
		out.artist.set ( doc.text ( doc.find ( artist, "name" ) ) );
		out.artist_id.set ( doc.text ( doc.find ( artist, "id" ) ) );
		out.type.set ( doc.text ( doc.find ( item, "album_type" ) ) );
		out.date.set ( doc.text ( doc.find ( item, "release_date" ) ) );
		out.art.set ( doc.text ( doc.find ( pick_image ( doc, doc.find ( item, "images" ), 300.0 ), "url" ) ) );
		out.day = parse_day ( out.date.get ( ) );
		out.total = static_cast < int32_t > ( doc.number ( doc.find ( item, "total_tracks" ) ) );

	}

	struct job {

		base::text path;
		json::document doc;
		int32_t status = 0;
		bool ok = false;

	};

	static DWORD __stdcall run_job ( void* data ) {

		job* item = static_cast < job* > ( data );

		item->ok = get_json ( item->path.get ( ), item->doc, item->status );

		return 0;

	}

	static void fetch_many ( base::list < job >& jobs ) {

		for ( int32_t start = 0; start < jobs.count ( ); start += 8 ) {

			HANDLE threads [ 8 ];
			int32_t made = 0;

			for ( int32_t k = start; k < jobs.count ( ) && k < start + 8; k++ ) {

				HANDLE handle = CreateThread ( nullptr, 0, &run_job, &jobs [ static_cast < size_t > ( k ) ], 0, nullptr );

				if ( handle )
					threads [ made++ ] = handle;
				else
					run_job ( &jobs [ static_cast < size_t > ( k ) ] );

			}

			if ( made > 0 )
				WaitForMultipleObjects ( static_cast < DWORD > ( made ), threads, TRUE, INFINITE );

			for ( int32_t k = 0; k < made; k++ )
				CloseHandle ( threads [ k ] );

		}

	}

	static void add_job ( base::list < job >& jobs, const wchar_t* head, const wchar_t* id, const wchar_t* tail ) {

		base::writer path;

		path.add ( head ).add ( id ).add ( tail );
		jobs.emplace ( ).path.set ( path.text ( ) );

	}

	static bool lists_artist ( const json::document& doc, int32_t artists, const wchar_t* id ) {

		for ( int32_t artist = doc.first ( artists ); artist >= 0; artist = doc.next ( artist ) ) {

			if ( base::same_text ( doc.text ( doc.find ( artist, "id" ) ), id ) )
				return true;

		}

		return false;

	}

	static void add_search ( base::list < job >& jobs, const wchar_t* name, int32_t offset ) {

		base::writer query;
		base::list < char > encoded;
		base::writer path;

		query.add ( L"artist:\"" );

		for ( int32_t i = 0; name [ i ]; i++ ) {

			if ( name [ i ] != L'"' )
				query.add ( name [ i ] );

		}

		query.add ( L"\"" );
		net::encode ( encoded, query.text ( ) );
		encoded.push ( 0 );
		path.add ( L"/search?type=track,album,artist&limit=10&offset=" ).number ( offset ).add ( L"&q=" ).add ( encoded.data ( ) );
		jobs.emplace ( ).path.set ( path.text ( ) );

	}

	static int32_t take_search ( const json::document& doc, base::list < taste >& table, int32_t at, bool discovery, float seed_weight, base::list < release >& releases, base::list < candidate >& pool ) {

		base::text id;
		base::text name;
		int32_t root = doc.root ( );
		int32_t taken = 0;

		id.set ( table [ static_cast < size_t > ( at ) ].id.get ( ) );
		name.set ( table [ static_cast < size_t > ( at ) ].name.get ( ) );

		for ( int32_t item = doc.first ( doc.path ( root, "artists", "items" ) ); item >= 0; item = doc.next ( item ) ) {

			if ( !base::same_text ( doc.text ( doc.find ( item, "id" ) ), id.get ( ) ) )
				continue;

			taste& entry = table [ static_cast < size_t > ( at ) ];

			entry.scouted = true;

			if ( entry.image.empty ( ) )
				entry.image.set ( doc.text ( doc.find ( pick_image ( doc, doc.find ( item, "images" ), 300.0 ), "url" ) ) );

			if ( entry.genre.empty ( ) )
				entry.genre.set ( doc.text ( doc.first ( doc.find ( item, "genres" ) ) ) );

			break;

		}

		for ( int32_t item = doc.first ( doc.path ( root, "albums", "items" ) ); item >= 0; item = doc.next ( item ) ) {

			if ( !lists_artist ( doc, doc.find ( item, "artists" ), id.get ( ) ) )
				continue;

			release& entry = releases.emplace ( );

			read_release ( doc, item, entry );
			entry.discovery = discovery;

			if ( entry.uri.empty ( ) )
				releases.pop ( );

		}

		for ( int32_t item = doc.first ( doc.path ( root, "tracks", "items" ) ); item >= 0; item = doc.next ( item ) ) {

			const wchar_t* uri = doc.text ( doc.find ( item, "uri" ) );
			int32_t artists = doc.find ( item, "artists" );
			int32_t album = doc.find ( item, "album" );

			if ( !begins ( uri, L"spotify:track:" ) || !lists_artist ( doc, artists, id.get ( ) ) )
				continue;

			candidate& entry = pool.emplace ( );
			base::writer names;
			const wchar_t* album_uri = doc.text ( doc.find ( album, "uri" ) );

			join_names ( doc, artists, names );
			copy ( entry.item.title, 160, doc.text ( doc.find ( item, "name" ) ) );
			copy ( entry.item.artist, 160, names.text ( ) );
			copy ( entry.item.album, 160, doc.text ( doc.find ( album, "name" ) ) );
			copy ( entry.item.uri, 64, uri );
			copy ( entry.item.art_url, 256, doc.text ( doc.find ( pick_image ( doc, doc.find ( album, "images" ), 300.0 ), "url" ) ) );
			copy ( entry.item.context, 64, album_uri );
			entry.item.duration = doc.number ( doc.find ( item, "duration_ms" ) ) / 1000.0;
			entry.item.number = static_cast < int32_t > ( doc.number ( doc.find ( item, "track_number" ) ) );
			entry.item.art = entry.item.art_url [ 0 ] ? library::hash ( entry.item.art_url, false ) : 0;
			entry.artist = library::hash ( doc.text ( doc.find ( doc.first ( artists ), "id" ) ), false );
			entry.album = library::hash ( album_uri, false );
			entry.song = library::hash ( entry.item.title, true ) ^ entry.artist;
			entry.day = parse_day ( doc.text ( doc.find ( album, "release_date" ) ) );
			entry.discovery = discovery;
			remember_album ( uri, album_uri );
			taken++;

			if ( discovery )
				continue;

			for ( int32_t artist = doc.first ( artists ); artist >= 0; artist = doc.next ( artist ) ) {

				const wchar_t* other_id = doc.text ( doc.find ( artist, "id" ) );

				if ( base::same_text ( other_id, id.get ( ) ) )
					continue;

				int32_t other_at = add_taste ( table, other_id, doc.text ( doc.find ( artist, "name" ) ) );

				if ( other_at < 0 || table [ static_cast < size_t > ( other_at ) ].affinity >= 0.15f || table [ static_cast < size_t > ( other_at ) ].seed )
					continue;

				taste& other = table [ static_cast < size_t > ( other_at ) ];

				other.pull += seed_weight;

				if ( other.reason.empty ( ) ) {

					base::writer reason;

					reason.add ( L"With " ).add ( name.get ( ) );
					other.reason.set ( reason.text ( ) );

				}

			}

		}

		return taken;

	}

	static void apply_scout ( base::list < taste >& table, int32_t at, const json::document& doc ) {

		int32_t root = doc.root ( );
		taste& entry = table [ static_cast < size_t > ( at ) ];

		entry.scouted = true;
		entry.image.set ( doc.text ( doc.find ( pick_image ( doc, doc.find ( root, "images" ), 300.0 ), "url" ) ) );

		if ( entry.name.empty ( ) )
			entry.name.set ( doc.text ( doc.find ( root, "name" ) ) );

		if ( entry.genre.empty ( ) )
			entry.genre.set ( doc.text ( doc.first ( doc.find ( root, "genres" ) ) ) );

	}

	static void scout_many ( base::list < taste >& table, const base::list < int32_t >& picks ) {

		base::list < job > wave;

		for ( int32_t at : picks )
			add_job ( wave, L"/artists/", table [ static_cast < size_t > ( at ) ].id.get ( ), L"" );

		fetch_many ( wave );

		for ( int32_t k = 0; k < picks.count ( ); k++ ) {

			if ( wave [ static_cast < size_t > ( k ) ].ok )
				apply_scout ( table, picks [ static_cast < size_t > ( k ) ], wave [ static_cast < size_t > ( k ) ].doc );

		}

	}

	static int32_t open_artists ( const base::list < int32_t >& picks, base::list < taste >& table, base::list < release >& releases, base::list < candidate >& pool ) {

		base::list < job > wave;
		base::list < int32_t > named;

		for ( int32_t at : picks ) {

			if ( table [ static_cast < size_t > ( at ) ].name.empty ( ) )
				continue;

			add_search ( wave, table [ static_cast < size_t > ( at ) ].name.get ( ), 0 );
			named.push ( at );

		}

		fetch_many ( wave );

		int32_t opened = 0;

		for ( int32_t n = 0; n < named.count ( ); n++ ) {

			if ( wave [ static_cast < size_t > ( n ) ].ok && take_search ( wave [ static_cast < size_t > ( n ) ].doc, table, named [ static_cast < size_t > ( n ) ], true, 0.0f, releases, pool ) > 0 )
				opened++;

		}

		return opened;

	}

	static float freshness ( int64_t age ) {

		if ( age < 0 )
			return 1.0f;

		if ( age < 45 )
			return 1.3f;

		if ( age < 365 )
			return 1.1f;

		return age > 3650 ? 0.85f : 1.0f;

	}

	static float jitter ( base::random& random, float spread ) {

		float sum = random.unit ( ) + random.unit ( ) + random.unit ( ) - 1.5f;

		return math::exp ( spread * sum );

	}

	static void release_result ( const release& source, result& out ) {

		copy ( out.title, 160, source.title.get ( ) );
		copy ( out.artist, 160, source.artist.get ( ) );
		copy ( out.album, 160, source.date.get ( ) );
		copy ( out.uri, 64, source.uri.get ( ) );
		copy ( out.art_url, 256, source.art.get ( ) );
		copy ( out.context, 64, source.type.get ( ) );
		out.art = out.art_url [ 0 ] ? library::hash ( out.art_url, false ) : 0;

	}

	static void artist_result ( const taste& source, const wchar_t* reason, result& out ) {

		base::writer uri;

		uri.add ( L"spotify:artist:" ).add ( source.id.get ( ) );
		copy ( out.title, 160, source.name.get ( ) );
		copy ( out.artist, 160, reason );
		copy ( out.uri, 64, uri.text ( ) );
		copy ( out.art_url, 256, source.image.get ( ) );
		out.art = out.art_url [ 0 ] ? library::hash ( out.art_url, false ) : 0;

	}

	static void save_home ( const home& data ) {

		base::list < char > out;
		wchar_t path [ 600 ];
		const base::list < result >* sections [ 5 ] = { &data.mix, &data.repeat, &data.albums, &data.artists, &data.fresh };
		const char marks [ 5 ] = { 'M', 'R', 'A', 'T', 'F' };

		out.push ( 'H' );
		out.push ( '\t' );
		put_integer ( out, data.made );
		out.push ( '\t' );
		put_integer ( out, data.history ? 1 : 0 );
		out.push ( '\t' );
		put_integer ( out, data.personal ? 1 : 0 );
		out.push ( '\t' );
		put_field ( out, data.blurb );
		out.push ( '\n' );

		for ( int32_t s = 0; s < 5; s++ ) {

			for ( const result& item : *sections [ s ] ) {

				const wchar_t* fields [ 6 ] = { item.uri, item.title, item.artist, item.album, item.art_url, item.context };

				out.push ( marks [ s ] );

				for ( const wchar_t* field : fields ) {

					out.push ( '\t' );
					put_field ( out, field );

				}

				out.push ( '\t' );
				put_integer ( out, static_cast < int64_t > ( item.duration * 1000.0 ) );
				out.push ( '\t' );
				put_integer ( out, item.number );
				out.push ( '\n' );

			}

		}

		data_path ( L"\\home.txt", path );
		files::write ( path, out.data ( ), out.count ( ) );

	}

	static void load_home ( ) {

		base::list < char > data;
		wchar_t path [ 600 ];

		data_path ( L"\\home.txt", path );

		if ( !files::read ( path, data ) || data.count ( ) < 2 )
			return;

		home loaded;
		const char* cursor = data.data ( );
		wchar_t fields [ 9 ] [ 512 ];

		while ( *cursor ) {

			int32_t count = read_fields ( cursor, fields, 9 );

			if ( count >= 5 && fields [ 0 ] [ 0 ] == L'H' ) {

				loaded.made = read_integer ( fields [ 1 ] );
				loaded.history = read_integer ( fields [ 2 ] ) != 0;
				loaded.personal = read_integer ( fields [ 3 ] ) != 0;
				copy ( loaded.blurb, 240, fields [ 4 ] );

				continue;

			}

			if ( count < 9 )
				continue;

			base::list < result >* target = nullptr;

			switch ( fields [ 0 ] [ 0 ] ) {

				case L'M': target = &loaded.mix; break;
				case L'R': target = &loaded.repeat; break;
				case L'A': target = &loaded.albums; break;
				case L'T': target = &loaded.artists; break;
				case L'F': target = &loaded.fresh; break;
				default: break;

			}

			if ( !target )
				continue;

			result& item = target->emplace ( );

			copy ( item.uri, 64, fields [ 1 ] );
			copy ( item.title, 160, fields [ 2 ] );
			copy ( item.artist, 160, fields [ 3 ] );
			copy ( item.album, 160, fields [ 4 ] );
			copy ( item.art_url, 256, fields [ 5 ] );
			copy ( item.context, 64, fields [ 6 ] );
			item.duration = static_cast < double > ( read_integer ( fields [ 7 ] ) ) / 1000.0;
			item.number = static_cast < int32_t > ( read_integer ( fields [ 8 ] ) );
			item.art = item.art_url [ 0 ] ? library::hash ( item.art_url, false ) : 0;

			if ( target == &loaded.mix || target == &loaded.repeat )
				remember_album ( item.uri, item.context );

		}

		AcquireSRWLockExclusive ( &home_lock );

		uint32_t generation = home_data.generation;

		home_data = base::move ( loaded );
		home_data.generation = generation + 1;
		ReleaseSRWLockExclusive ( &home_lock );

	}

	static void search_tracks ( const wchar_t* query, int32_t offset, base::list < result >& out ) {

		base::list < char > encoded;
		base::writer path;
		json::document doc;
		int32_t status = 0;

		net::encode ( encoded, query );
		encoded.push ( 0 );
		path.add ( L"/search?type=track&limit=10&offset=" ).number ( offset ).add ( L"&q=" ).add ( encoded.data ( ) );

		if ( !get_json ( path.text ( ), doc, status ) )
			return;

		for ( int32_t item = doc.first ( doc.path ( doc.root ( ), "tracks", "items" ) ); item >= 0; item = doc.next ( item ) ) {

			listen entry;

			read_track ( doc, item, entry );

			if ( entry.uri.empty ( ) )
				continue;

			listen_result ( entry, out.emplace ( ) );
			remember_album ( entry.uri.get ( ), entry.album_uri.get ( ) );

		}

	}

	static void search_albums ( const wchar_t* query, base::list < result >& out ) {

		base::list < char > encoded;
		base::writer path;
		json::document doc;
		int32_t status = 0;

		net::encode ( encoded, query );
		encoded.push ( 0 );
		path.add ( L"/search?type=album&limit=10&q=" ).add ( encoded.data ( ) );

		if ( !get_json ( path.text ( ), doc, status ) )
			return;

		for ( int32_t item = doc.first ( doc.path ( doc.root ( ), "albums", "items" ) ); item >= 0; item = doc.next ( item ) ) {

			release entry;

			read_release ( doc, item, entry );

			if ( !entry.uri.empty ( ) )
				release_result ( entry, out.emplace ( ) );

		}

	}

	static void run_home ( const wchar_t* liked, bool force, uint32_t round ) {

		int64_t now = wall_seconds ( );
		int64_t today = now / 86400;
		base::list < listen > events;

		AcquireSRWLockShared ( &history_lock );
		events = listens;
		ReleaseSRWLockShared ( &history_lock );

		int32_t resolved = 0;
		wchar_t token [ 64 ];

		for ( const wchar_t* at = liked; at && *at && resolved < 10; ) {

			while ( *at == L' ' )
				at++;

			int32_t length = 0;

			while ( *at && *at != L' ' ) {

				if ( length < 63 )
					token [ length++ ] = *at;

				at++;

			}

			token [ length ] = 0;

			if ( length < 10 )
				continue;

			bool known = false;

			for ( const listen& entry : events )
				known = known || ( entry.kind == 'L' && base::same_text ( entry.uri.get ( ), token ) );

			if ( known )
				continue;

			listen entry;

			resolved++;

			if ( !resolve_track ( token, entry ) )
				continue;

			record ( 'L', entry );
			entry.kind = 'L';
			entry.stamp = now;
			events.push ( entry );

		}

		AcquireSRWLockShared ( &home_lock );

		int64_t made = home_data.made;
		bool filled = !home_data.mix.empty ( ) || !home_data.fresh.empty ( );
		bool generic = !home_data.personal;

		ReleaseSRWLockShared ( &home_lock );

		int32_t since = 0;

		for ( const listen& entry : events ) {

			if ( entry.stamp > made && entry.kind != 'S' )
				since++;

		}

		if ( !force && filled && now - made < 4 * 3600 && since < 12 && !( generic && since > 0 ) )
			return;

		uint64_t began = GetTickCount64 ( );

		AcquireSRWLockExclusive ( &home_lock );
		home_data.loading = true;
		home_data.generation++;
		ReleaseSRWLockExclusive ( &home_lock );
		notify ( );

		base::list < taste > table;
		base::list < favorite > favorites;
		base::list < uint64_t > heard_tracks;
		base::list < uint64_t > heard_albums;
		json::document doc;
		int32_t status = 0;
		bool history = true;
		const wchar_t* tops [ 3 ] = { L"/me/top/artists?time_range=short_term&limit=30", L"/me/top/artists?time_range=medium_term&limit=30", L"/me/top/artists?time_range=long_term&limit=30" };
		float top_weight [ 3 ] = { 3.0f, 2.0f, 1.3f };
		const wchar_t* top_tracks [ 2 ] = { L"/me/top/tracks?time_range=short_term&limit=30", L"/me/top/tracks?time_range=medium_term&limit=30" };
		float track_weight [ 2 ] = { 1.0f, 0.6f };
		base::list < job > wave;

		for ( const wchar_t* path : tops )
			add_job ( wave, path, L"", L"" );

		for ( const wchar_t* path : top_tracks )
			add_job ( wave, path, L"", L"" );

		add_job ( wave, L"/me/player/recently-played?limit=50", L"", L"" );
		fetch_many ( wave );

		for ( int32_t r = 0; r < 3 && history; r++ ) {

			const json::document& doc = wave [ static_cast < size_t > ( r ) ].doc;

			if ( !wave [ static_cast < size_t > ( r ) ].ok ) {

				if ( wave [ static_cast < size_t > ( r ) ].status == 403 && mentions ( doc.text ( doc.path ( doc.root ( ), "error", "message" ) ), L"scope" ) )
					history = false;

				continue;

			}

			int32_t rank = 0;

			for ( int32_t item = doc.first ( doc.find ( doc.root ( ), "items" ) ); item >= 0; item = doc.next ( item ), rank++ ) {

				int32_t at = add_taste ( table, doc.text ( doc.find ( item, "id" ) ), doc.text ( doc.find ( item, "name" ) ) );

				if ( at < 0 )
					continue;

				taste& entry = table [ static_cast < size_t > ( at ) ];

				entry.affinity += top_weight [ r ] * ( 1.0f - static_cast < float > ( rank ) / 36.0f );

				if ( entry.image.empty ( ) )
					entry.image.set ( doc.text ( doc.find ( pick_image ( doc, doc.find ( item, "images" ), 300.0 ), "url" ) ) );

				if ( entry.genre.empty ( ) )
					entry.genre.set ( doc.text ( doc.first ( doc.find ( item, "genres" ) ) ) );

			}

		}

		for ( int32_t r = 0; r < 2 && history; r++ ) {

			const json::document& doc = wave [ static_cast < size_t > ( 3 + r ) ].doc;

			if ( !wave [ static_cast < size_t > ( 3 + r ) ].ok )
				continue;

			int32_t rank = 0;

			for ( int32_t item = doc.first ( doc.find ( doc.root ( ), "items" ) ); item >= 0; item = doc.next ( item ), rank++ ) {

				listen entry;

				read_track ( doc, item, entry );

				if ( entry.uri.empty ( ) )
					continue;

				float weight = track_weight [ r ] * ( 1.0f - static_cast < float > ( rank ) / 36.0f );

				credit ( table, entry, 1.4f * weight );
				adore ( favorites, entry, 1.6f * weight );
				heard_tracks.push ( library::hash ( entry.uri.get ( ), false ) );
				heard_albums.push ( library::hash ( entry.album_uri.get ( ), false ) );

			}

		}

		if ( history && wave [ 5 ].ok ) {

			const json::document& doc = wave [ 5 ].doc;

			for ( int32_t item = doc.first ( doc.find ( doc.root ( ), "items" ) ); item >= 0; item = doc.next ( item ) ) {

				listen& entry = events.emplace ( );
				int32_t clock = 0;
				int64_t day = parse_day ( doc.text ( doc.find ( item, "played_at" ) ), &clock );

				read_track ( doc, doc.find ( item, "track" ), entry );
				entry.kind = 'R';
				entry.stamp = day > 0 ? day * 86400 + clock : now;

				if ( entry.uri.empty ( ) )
					events.pop ( );

			}

		}

		for ( const listen& entry : events ) {

			float age = base::max ( static_cast < float > ( now - entry.stamp ) / 86400.0f, 0.0f );
			float half = entry.kind == 'L' ? 120.0f : ( entry.kind == 'S' ? 21.0f : 45.0f );
			float weight = entry.kind == 'L' ? 2.5f : ( entry.kind == 'S' ? -0.3f : ( entry.kind == 'R' ? 0.6f : 1.0f ) );

			credit ( table, entry, weight * math::pow ( 0.5f, age / half ) );
			heard_tracks.push ( library::hash ( entry.uri.get ( ), false ) );

			if ( entry.kind == 'S' )
				continue;

			if ( !entry.album_uri.empty ( ) )
				heard_albums.push ( library::hash ( entry.album_uri.get ( ), false ) );

			adore ( favorites, entry, ( entry.kind == 'L' ? 0.4f : ( entry.kind == 'R' ? 0.7f : 1.0f ) ) * math::pow ( 0.5f, age / 10.0f ) );

		}

		base::list < int32_t > ranked;

		for ( int32_t i = 0; i < table.count ( ); i++ ) {

			if ( table [ static_cast < size_t > ( i ) ].affinity > 0.05f )
				ranked.push ( i );

		}

		base::sort ( ranked.data ( ), ranked.count ( ), [ & ] ( int32_t a, int32_t b ) {

			return table [ static_cast < size_t > ( a ) ].affinity > table [ static_cast < size_t > ( b ) ].affinity;

		} );

		float top = ranked.empty ( ) ? 0.0f : table [ static_cast < size_t > ( ranked [ 0 ] ) ].affinity;
		auto closeness = [ & ] ( uint64_t key ) {

			int32_t at = find_taste ( table, key );

			if ( at < 0 || top <= 0.0f )
				return 0.0f;

			return math::saturate ( table [ static_cast < size_t > ( at ) ].affinity / top );

		};

		base::random random;
		base::list < int32_t > seeds;
		base::list < int32_t > draw;

		random.seed ( static_cast < uint64_t > ( today ) * 0x9E3779B97F4A7C15ull ^ ( static_cast < uint64_t > ( round ) << 40 ) ^ 0x51EDull );

		if ( !ranked.empty ( ) )
			seeds.push ( ranked [ 0 ] );

		for ( int32_t k = 1; k < ranked.count ( ) && k < 25; k++ )
			draw.push ( ranked [ static_cast < size_t > ( k ) ] );

		while ( seeds.count ( ) < 6 && !draw.empty ( ) ) {

			float total = 0.0f;

			for ( int32_t at : draw )
				total += math::pow ( table [ static_cast < size_t > ( at ) ].affinity, 0.8f );

			float target = random.unit ( ) * total;
			int32_t pick = 0;

			for ( ; pick < draw.count ( ) - 1; pick++ ) {

				target -= math::pow ( table [ static_cast < size_t > ( draw [ static_cast < size_t > ( pick ) ] ) ].affinity, 0.8f );

				if ( target <= 0.0f )
					break;

			}

			seeds.push ( draw [ static_cast < size_t > ( pick ) ] );
			draw.remove_at ( static_cast < size_t > ( pick ) );

		}

		for ( int32_t at : seeds )
			table [ static_cast < size_t > ( at ) ].seed = true;

		home built;
		base::list < release > releases;
		base::list < candidate > pool;

		built.history = history;
		built.personal = !seeds.empty ( ) && top >= 0.5f;

		wave.clear ( );

		for ( int32_t n = 0; n < seeds.count ( ); n++ )
			add_search ( wave, table [ static_cast < size_t > ( seeds [ static_cast < size_t > ( n ) ] ) ].name.get ( ), static_cast < int32_t > ( ( round + static_cast < uint32_t > ( n ) ) % 2 ) * 10 );

		fetch_many ( wave );

		for ( int32_t n = 0; n < seeds.count ( ); n++ ) {

			if ( wave [ static_cast < size_t > ( n ) ].ok )
				take_search ( wave [ static_cast < size_t > ( n ) ].doc, table, seeds [ static_cast < size_t > ( n ) ], false, closeness ( table [ static_cast < size_t > ( seeds [ static_cast < size_t > ( n ) ] ) ].key ), releases, pool );

		}

		base::list < int32_t > scouts;

		for ( int32_t i = 0; i < table.count ( ); i++ ) {

			const taste& entry = table [ static_cast < size_t > ( i ) ];

			if ( !entry.seed && entry.affinity < 0.15f && entry.pull > 0.0f )
				scouts.push ( i );

		}

		base::sort ( scouts.data ( ), scouts.count ( ), [ & ] ( int32_t a, int32_t b ) {

			return table [ static_cast < size_t > ( a ) ].pull > table [ static_cast < size_t > ( b ) ].pull;

		} );

		base::list < int32_t > picks;

		for ( int32_t k = 0; k < scouts.count ( ) && picks.count ( ) < 5; k++ )
			picks.push ( scouts [ static_cast < size_t > ( k ) ] );

		int32_t opened = open_artists ( picks, table, releases, pool );

		if ( opened < 3 && !seeds.empty ( ) ) {

			int32_t source = seeds [ 0 ];

			for ( int32_t s : seeds ) {

				if ( table [ static_cast < size_t > ( source ) ].genre.empty ( ) && !table [ static_cast < size_t > ( s ) ].genre.empty ( ) )
					source = s;

			}

			if ( table [ static_cast < size_t > ( source ) ].genre.empty ( ) ) {

				picks.clear ( );
				picks.push ( source );
				scout_many ( table, picks );

			}

			if ( !table [ static_cast < size_t > ( source ) ].genre.empty ( ) ) {

				base::writer query;
				base::list < char > encoded;
				base::writer path;
				base::text seed_name;

				seed_name.set ( table [ static_cast < size_t > ( source ) ].name.get ( ) );
				query.add ( L"genre:\"" ).add ( table [ static_cast < size_t > ( source ) ].genre.get ( ) ).add ( L"\"" );
				net::encode ( encoded, query.text ( ) );
				encoded.push ( 0 );
				path.add ( L"/search?type=artist&limit=10&q=" ).add ( encoded.data ( ) );

				if ( get_json ( path.text ( ), doc, status ) ) {

					base::list < int32_t > found;

					for ( int32_t item = doc.first ( doc.path ( doc.root ( ), "artists", "items" ) ); item >= 0; item = doc.next ( item ) ) {

						int32_t at = add_taste ( table, doc.text ( doc.find ( item, "id" ) ), doc.text ( doc.find ( item, "name" ) ) );

						if ( at < 0 || table [ static_cast < size_t > ( at ) ].affinity >= 0.15f || table [ static_cast < size_t > ( at ) ].seed || table [ static_cast < size_t > ( at ) ].scouted )
							continue;

						taste& entry = table [ static_cast < size_t > ( at ) ];
						base::writer reason;

						reason.add ( L"Like " ).add ( seed_name.get ( ) );
						entry.reason.set ( reason.text ( ) );
						entry.pull = base::max ( entry.pull, 0.5f - 0.03f * static_cast < float > ( found.count ( ) ) );
						found.push ( at );

					}

					picks.clear ( );

					for ( int32_t k = 0; k < found.count ( ) && opened + picks.count ( ) < 5; k++ )
						picks.push ( found [ static_cast < size_t > ( k ) ] );

					opened += open_artists ( picks, table, releases, pool );

				}

			}

		}

		int32_t known_faces = 0;
		int32_t extra_looks = 0;

		for ( const taste& entry : table ) {

			if ( entry.scouted && !entry.seed && entry.affinity < 0.15f && !entry.image.empty ( ) )
				known_faces++;

		}

		picks.clear ( );

		for ( int32_t k = 0; k < scouts.count ( ) && known_faces + extra_looks < 14 && extra_looks < 10; k++ ) {

			if ( table [ static_cast < size_t > ( scouts [ static_cast < size_t > ( k ) ] ) ].scouted )
				continue;

			picks.push ( scouts [ static_cast < size_t > ( k ) ] );
			extra_looks++;

		}

		scout_many ( table, picks );

		float pull_top = 0.0f;

		for ( const taste& entry : table )
			pull_top = base::max ( pull_top, entry.pull );

		auto lure = [ & ] ( uint64_t key ) {

			int32_t at = find_taste ( table, key );

			if ( at < 0 || pull_top <= 0.0f )
				return 0.3f;

			return math::saturate ( table [ static_cast < size_t > ( at ) ].pull / pull_top );

		};

		for ( candidate& entry : pool ) {

			float base_score = entry.discovery ? 0.42f + 0.38f * lure ( entry.artist ) : 0.35f + 0.65f * closeness ( entry.artist );
			float novelty = contains ( heard_tracks, library::hash ( entry.item.uri, false ) ) ? 0.06f : 1.0f;
			float length = entry.item.duration < 70.0 ? 0.25f : ( entry.item.duration > 540.0 ? 0.6f : 1.0f );
			float place = 1.0f + 0.3f / static_cast < float > ( 1 + base::max ( entry.item.number, 0 ) );

			entry.score = base_score * novelty * freshness ( today - entry.day ) * length * place * jitter ( random, 0.5f );

		}

		for ( int32_t i = 0; i < pool.count ( ); i++ ) {

			for ( int32_t j = i + 1; j < pool.count ( ); j++ ) {

				candidate& a = pool [ static_cast < size_t > ( i ) ];
				candidate& b = pool [ static_cast < size_t > ( j ) ];

				if ( a.taken || b.taken || ( a.song != b.song && !base::same_text ( a.item.uri, b.item.uri ) ) )
					continue;

				if ( a.score >= b.score )
					b.taken = true;
				else
					a.taken = true;

			}

		}

		base::list < uint64_t > artist_keys;
		base::list < int32_t > artist_counts;
		base::list < uint64_t > album_keys;
		base::list < int32_t > album_counts;
		auto tally = [ ] ( base::list < uint64_t >& keys, base::list < int32_t >& counts, uint64_t key, bool add ) {

			for ( int32_t i = 0; i < keys.count ( ); i++ ) {

				if ( keys [ static_cast < size_t > ( i ) ] == key ) {

					if ( add )
						counts [ static_cast < size_t > ( i ) ]++;

					return counts [ static_cast < size_t > ( i ) ];

				}

			}

			if ( add ) {

				keys.push ( key );
				counts.push ( 1 );

			}

			return add ? 1 : 0;

		};

		for ( int32_t slot = 0; slot < 30; slot++ ) {

			bool want_new = slot % 3 == 2;
			int32_t best = -1;
			float best_score = 0.0f;

			for ( int32_t pass = 0; pass < 2 && best < 0; pass++ ) {

				for ( int32_t i = 0; i < pool.count ( ); i++ ) {

					const candidate& entry = pool [ static_cast < size_t > ( i ) ];

					if ( entry.taken || ( pass == 0 && entry.discovery != want_new ) )
						continue;

					int32_t by_artist = tally ( artist_keys, artist_counts, entry.artist, false );

					if ( by_artist >= 4 )
						continue;

					float adjusted = entry.score * math::pow ( 0.45f, static_cast < float > ( by_artist ) ) * math::pow ( 0.3f, static_cast < float > ( tally ( album_keys, album_counts, entry.album, false ) ) );

					if ( adjusted > best_score ) {

						best_score = adjusted;
						best = i;

					}

				}

			}

			if ( best < 0 )
				break;

			candidate& chosen = pool [ static_cast < size_t > ( best ) ];

			chosen.taken = true;
			tally ( artist_keys, artist_counts, chosen.artist, true );
			tally ( album_keys, album_counts, chosen.album, true );
			built.mix.push ( chosen.item );

		}

		base::list < int32_t > recent;
		base::list < uint64_t > shown;

		for ( int32_t i = 0; i < releases.count ( ); i++ ) {

			const release& entry = releases [ static_cast < size_t > ( i ) ];

			if ( !entry.discovery && entry.day > 0 && today - entry.day <= 150 && today - entry.day >= -1 )
				recent.push ( i );

		}

		base::sort ( recent.data ( ), recent.count ( ), [ & ] ( int32_t a, int32_t b ) {

			return releases [ static_cast < size_t > ( a ) ].day > releases [ static_cast < size_t > ( b ) ].day;

		} );

		artist_keys.clear ( );
		artist_counts.clear ( );

		for ( int32_t at : recent ) {

			const release& entry = releases [ static_cast < size_t > ( at ) ];
			uint64_t artist = library::hash ( entry.artist_id.get ( ), false );
			uint64_t title = library::hash ( entry.title.get ( ), true ) ^ artist;

			if ( built.fresh.count ( ) >= 10 || contains ( shown, title ) || contains ( shown, library::hash ( entry.uri.get ( ), false ) ) || tally ( artist_keys, artist_counts, artist, false ) >= 2 )
				continue;

			shown.push ( title );
			shown.push ( library::hash ( entry.uri.get ( ), false ) );
			tally ( artist_keys, artist_counts, artist, true );
			release_result ( entry, built.fresh.emplace ( ) );

		}

		for ( release& entry : releases ) {

			uint64_t artist = library::hash ( entry.artist_id.get ( ), false );
			const wchar_t* type = entry.type.get ( );
			float shape = base::same_text ( type, L"album" ) ? 1.0f : ( base::same_text ( type, L"compilation" ) ? 0.25f : ( entry.total >= 4 ? 0.8f : 0.5f ) );
			float base_score = entry.discovery ? 0.42f + 0.38f * lure ( artist ) : 0.35f + 0.65f * closeness ( artist );
			bool heard = contains ( heard_albums, library::hash ( entry.uri.get ( ), false ) );

			entry.score = heard ? 0.0f : base_score * shape * freshness ( today - entry.day ) * jitter ( random, 0.4f );

		}

		artist_keys.clear ( );
		artist_counts.clear ( );

		for ( int32_t pass = 0; pass < 2; pass++ ) {

			while ( built.albums.count ( ) < 12 ) {

				int32_t best = -1;

				for ( int32_t i = 0; i < releases.count ( ); i++ ) {

					const release& entry = releases [ static_cast < size_t > ( i ) ];
					uint64_t artist = library::hash ( entry.artist_id.get ( ), false );
					uint64_t title = library::hash ( entry.title.get ( ), true ) ^ artist;

					if ( entry.score <= 0.0f || contains ( shown, title ) || contains ( shown, library::hash ( entry.uri.get ( ), false ) ) || tally ( artist_keys, artist_counts, artist, false ) > pass )
						continue;

					if ( best < 0 || entry.score > releases [ static_cast < size_t > ( best ) ].score )
						best = i;

				}

				if ( best < 0 )
					break;

				release& chosen = releases [ static_cast < size_t > ( best ) ];
				uint64_t artist = library::hash ( chosen.artist_id.get ( ), false );

				chosen.score = 0.0f;
				shown.push ( library::hash ( chosen.title.get ( ), true ) ^ artist );
				shown.push ( library::hash ( chosen.uri.get ( ), false ) );
				tally ( artist_keys, artist_counts, artist, true );
				release_result ( chosen, built.albums.emplace ( ) );

			}

		}

		base::list < int32_t > faces;

		for ( int32_t i = 0; i < table.count ( ); i++ ) {

			const taste& entry = table [ static_cast < size_t > ( i ) ];

			if ( entry.scouted && !entry.seed && entry.affinity < 0.15f && !entry.image.empty ( ) && !entry.name.empty ( ) )
				faces.push ( i );

		}

		base::sort ( faces.data ( ), faces.count ( ), [ & ] ( int32_t a, int32_t b ) {

			return table [ static_cast < size_t > ( a ) ].pull > table [ static_cast < size_t > ( b ) ].pull;

		} );

		for ( int32_t k = 0; k < faces.count ( ) && built.artists.count ( ) < 12; k++ ) {

			const taste& entry = table [ static_cast < size_t > ( faces [ static_cast < size_t > ( k ) ] ) ];

			artist_result ( entry, entry.reason.empty ( ) ? L"New to you" : entry.reason.get ( ), built.artists.emplace ( ) );

		}

		picks.clear ( );

		for ( int32_t k = 0; k < ranked.count ( ) && k < 24 && picks.count ( ) < 8; k++ ) {

			int32_t at = ranked [ ( static_cast < size_t > ( k ) + 2 ) % static_cast < size_t > ( ranked.count ( ) ) ];

			if ( table [ static_cast < size_t > ( at ) ].image.empty ( ) )
				picks.push ( at );

		}

		scout_many ( table, picks );

		for ( int32_t k = 0; k < ranked.count ( ) && built.artists.count ( ) < 16; k++ ) {

			int32_t at = ranked [ ( static_cast < size_t > ( k ) + 2 ) % static_cast < size_t > ( ranked.count ( ) ) ];
			const taste& entry = table [ static_cast < size_t > ( at ) ];
			bool listed = false;

			for ( const result& face : built.artists )
				listed = listed || base::same_text ( id_of ( face.uri ), entry.id.get ( ) );

			if ( !listed && !entry.image.empty ( ) && !entry.name.empty ( ) )
				artist_result ( entry, L"In your rotation", built.artists.emplace ( ) );

		}

		base::list < int32_t > loved;

		for ( int32_t i = 0; i < favorites.count ( ); i++ ) {

			if ( favorites [ static_cast < size_t > ( i ) ].score >= 0.9f )
				loved.push ( i );

		}

		base::sort ( loved.data ( ), loved.count ( ), [ & ] ( int32_t a, int32_t b ) {

			return favorites [ static_cast < size_t > ( a ) ].score > favorites [ static_cast < size_t > ( b ) ].score;

		} );

		artist_keys.clear ( );
		artist_counts.clear ( );

		for ( int32_t at : loved ) {

			const favorite& entry = favorites [ static_cast < size_t > ( at ) ];
			wchar_t first [ 64 ];

			if ( built.repeat.count ( ) >= 8 )
				break;

			segment ( entry.meta.ids.get ( ), 0, first, 64 );

			uint64_t artist = library::hash ( first, false );

			if ( tally ( artist_keys, artist_counts, artist, false ) >= 2 )
				continue;

			tally ( artist_keys, artist_counts, artist, true );
			listen_result ( entry.meta, built.repeat.emplace ( ) );

		}

		if ( seeds.empty ( ) ) {

			SYSTEMTIME date = { };
			base::writer year;

			GetSystemTime ( &date );
			year.add ( L"year:" ).number ( date.wYear );
			search_tracks ( year.text ( ), 0, built.mix );
			search_tracks ( year.text ( ), 10, built.mix );
			search_albums ( L"tag:new", built.fresh );
			copy ( built.blurb, 240, L"Play a few songs and this page starts learning your taste." );

		} else {

			base::writer blurb;
			int32_t named = 0;

			blurb.add ( L"Built from " );

			for ( int32_t k = 0; k < seeds.count ( ) && named < 3; k++ ) {

				const wchar_t* name = table [ static_cast < size_t > ( seeds [ static_cast < size_t > ( k ) ] ) ].name.get ( );

				if ( !name [ 0 ] )
					continue;

				if ( named > 0 )
					blurb.add ( named == 2 || k == seeds.count ( ) - 1 ? L" and " : L", " );

				blurb.add ( name );
				named++;

			}

			blurb.add ( L", plus artists close to them" );
			copy ( built.blurb, 240, blurb.text ( ) );

		}

		char line [ 160 ];
		base::writer summary;

		summary.add ( L"home seeds " ).number ( seeds.count ( ) ).add ( L" pool " ).number ( pool.count ( ) ).add ( L" mix " ).number ( built.mix.count ( ) ).add ( L" albums " ).number ( built.albums.count ( ) ).add ( L" artists " ).number ( built.artists.count ( ) ).add ( L" fresh " ).number ( built.fresh.count ( ) ).add ( history ? L" history" : L" no-history" ).add ( L" ms " ).number ( static_cast < int64_t > ( GetTickCount64 ( ) - began ) );

		for ( int32_t i = 0; i <= summary.size ( ) && i < 159; i++ )
			line [ i ] = static_cast < char > ( summary.text ( ) [ i ] );

		line [ 159 ] = 0;
		note ( line );

		bool useful = !built.mix.empty ( ) || !built.albums.empty ( ) || !built.fresh.empty ( );

		AcquireSRWLockExclusive ( &home_lock );

		uint32_t generation = home_data.generation;
		uint32_t finished = home_data.finished;

		if ( useful ) {

			built.made = now;
			home_data = built;

		}

		home_data.history = history;
		home_data.loading = false;
		home_data.generation = generation + 1;
		home_data.finished = finished + 1;
		ReleaseSRWLockExclusive ( &home_lock );

		if ( useful )
			save_home ( built );

		notify ( );
		save_responses ( );

	}

	static DWORD __stdcall home_worker ( void* ) {

		load_listens ( );
		load_responses ( );
		load_home ( );
		notify ( );

		while ( true ) {

			WaitForSingleObject ( home_wake, INFINITE );

			while ( true ) {

				base::text liked;
				base::text like;
				bool build = false;
				bool force = false;
				uint32_t round = 0;

				AcquireSRWLockExclusive ( &home_lock );

				if ( !like_queue.empty ( ) ) {

					like = like_queue [ 0 ];
					like_queue.remove_at ( 0 );

				} else if ( wanted_build ) {

					build = true;
					force = wanted_force;
					round = shuffle_count;
					liked = wanted_liked;
					wanted_build = false;
					wanted_force = false;

				}

				ReleaseSRWLockExclusive ( &home_lock );

				if ( !like.empty ( ) ) {

					listen entry;

					if ( resolve_track ( like.get ( ), entry ) )
						record ( 'L', entry );

					continue;

				}

				if ( !build )
					break;

				run_home ( liked.get ( ), force, round );

			}

		}

		return 0;

	}

	void start_taste ( ) {

		if ( home_thread )
			return;

		shuffle_count = static_cast < uint32_t > ( GetTickCount64 ( ) ^ ( GetTickCount64 ( ) >> 20 ) );
		home_wake = CreateEventW ( nullptr, FALSE, FALSE, nullptr );
		home_thread = CreateThread ( nullptr, 0, &home_worker, nullptr, 0, nullptr );

	}

	void refresh_home ( const wchar_t* liked, bool force ) {

		AcquireSRWLockExclusive ( &home_lock );
		wanted_liked.set ( liked );
		wanted_build = true;
		wanted_force = wanted_force || force;

		if ( force )
			shuffle_count++;

		ReleaseSRWLockExclusive ( &home_lock );

		if ( home_wake )
			SetEvent ( home_wake );

	}

	void note_like ( const wchar_t* uri ) {

		if ( !uri || !begins ( uri, L"spotify:track:" ) )
			return;

		AcquireSRWLockExclusive ( &home_lock );
		like_queue.emplace ( ).set ( uri );
		ReleaseSRWLockExclusive ( &home_lock );

		if ( home_wake )
			SetEvent ( home_wake );

	}

	uint32_t home_revision ( ) {

		AcquireSRWLockShared ( &home_lock );

		uint32_t value = home_data.generation;

		ReleaseSRWLockShared ( &home_lock );

		return value;

	}

	void read_home ( home& out ) {

		AcquireSRWLockShared ( &home_lock );
		out = home_data;
		ReleaseSRWLockShared ( &home_lock );

	}

}