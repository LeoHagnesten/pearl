#include <windows.h>
#include "lyrics/lyrics.hh"
#include "net/http.hh"
#include "base/json.hh"

namespace lyrics {

	struct song {

		uint64_t key = 0;
		base::text title;
		base::text artist;
		base::text album;
		double duration = 0.0;

	};

	static SRWLOCK lock = SRWLOCK_INIT;
	static HANDLE thread = nullptr;
	static HANDLE wake = nullptr;
	static song wanted;
	static bool waiting = false;
	static sheet current;
	static base::list < sheet > cache;
	static uint32_t counter = 0;

	static bool digit ( wchar_t value ) {

		return value >= L'0' && value <= L'9';

	}

	static void add_line ( base::list < line >& out, double time, const wchar_t* start, const wchar_t* end ) {

		while ( start < end && ( *start == L' ' || *start == L'\t' ) )
			start++;

		while ( end > start && ( end [ -1 ] == L' ' || end [ -1 ] == L'\r' || end [ -1 ] == L'\t' ) )
			end--;

		bool notes = start < end;

		for ( const wchar_t* at = start; at < end && notes; at++ )
			notes = *at == 0x266A || *at == 0x266B || *at == 0x266C || *at == L' ';

		if ( notes )
			start = end;

		bool empty = start == end;

		if ( empty && ( out.empty ( ) || out.back ( ).words.empty ( ) ) )
			return;

		wchar_t words [ 600 ];
		int32_t length = 0;

		for ( ; start < end && length < 599; start++ )
			words [ length++ ] = *start;

		words [ length ] = 0;

		line& item = out.emplace ( );

		item.time = time;
		item.words.set ( words );

	}

	static void parse_synced ( const wchar_t* text, base::list < line >& out ) {

		for ( const wchar_t* at = text; *at; ) {

			const wchar_t* end = at;

			while ( *end && *end != L'\n' )
				end++;

			base::list < double > stamps;
			const wchar_t* cursor = at;

			while ( cursor < end && *cursor == L'[' ) {

				const wchar_t* inner = cursor + 1;
				int32_t minutes = 0;
				double seconds = 0.0;

				if ( !digit ( *inner ) )
					break;

				while ( inner < end && digit ( *inner ) )
					minutes = minutes * 10 + ( *inner++ - L'0' );

				if ( inner >= end || *inner != L':' )
					break;

				inner++;

				while ( inner < end && digit ( *inner ) )
					seconds = seconds * 10.0 + static_cast < double > ( *inner++ - L'0' );

				if ( inner < end && ( *inner == L'.' || *inner == L':' ) ) {

					double scale = 0.1;

					inner++;

					while ( inner < end && digit ( *inner ) ) {

						seconds += static_cast < double > ( *inner++ - L'0' ) * scale;
						scale *= 0.1;

					}

				}

				if ( inner >= end || *inner != L']' )
					break;

				stamps.push ( static_cast < double > ( minutes ) * 60.0 + seconds );
				cursor = inner + 1;

			}

			for ( double stamp : stamps )
				add_line ( out, stamp, cursor, end );

			at = *end ? end + 1 : end;

		}

		base::sort ( out.data ( ), out.count ( ), [ ] ( const line& a, const line& b ) {

			return a.time < b.time;

		} );

		while ( !out.empty ( ) && out.back ( ).words.empty ( ) )
			out.pop ( );

	}

	static void parse_plain ( const wchar_t* text, base::list < line >& out ) {

		for ( const wchar_t* at = text; *at; ) {

			const wchar_t* end = at;

			while ( *end && *end != L'\n' )
				end++;

			add_line ( out, -1.0, at, end );
			at = *end ? end + 1 : end;

		}

		while ( !out.empty ( ) && out.back ( ).words.empty ( ) )
			out.pop ( );

	}

	static bool parse_entry ( const json::document& doc, int32_t item, sheet& out ) {

		out.lines.clear ( );
		out.synced = false;
		out.instrumental = false;

		if ( doc.flag ( doc.find ( item, "instrumental" ) ) ) {

			out.found = true;
			out.instrumental = true;

			return true;

		}

		const wchar_t* synced = doc.text ( doc.find ( item, "syncedLyrics" ) );

		if ( synced [ 0 ] ) {

			parse_synced ( synced, out.lines );

			if ( !out.lines.empty ( ) ) {

				out.found = true;
				out.synced = true;

				return true;

			}

		}

		const wchar_t* plain = doc.text ( doc.find ( item, "plainLyrics" ) );

		if ( plain [ 0 ] )
			parse_plain ( plain, out.lines );

		out.found = !out.lines.empty ( );

		return out.found;

	}

	static void add_param ( base::writer& url, const char* name, const wchar_t* value ) {

		base::list < char > encoded;

		net::encode ( encoded, value );
		encoded.push ( 0 );
		url.add ( name ).add ( encoded.data ( ) );

	}

	static void clean_title ( const wchar_t* title, wchar_t* out, int32_t room ) {

		int32_t length = 0;

		for ( ; title [ length ] && length < room - 1; length++ ) {

			wchar_t a = title [ length ];
			wchar_t b = title [ length + 1 ];

			if ( length > 0 && a == L' ' && ( b == L'(' || b == L'[' || ( b == L'-' && title [ length + 2 ] == L' ' ) ) )
				break;

			out [ length ] = a;

		}

		while ( length > 0 && out [ length - 1 ] == L' ' )
			length--;

		out [ length ] = 0;

	}

	static bool fetch ( const song& wish, sheet& out ) {

		net::response reply;
		json::document doc;
		bool reached = false;

		if ( !wish.album.empty ( ) && wish.duration > 0.0 ) {

			base::writer url;

			url.add ( L"https://lrclib.net/api/get?" );
			add_param ( url, "track_name=", wish.title.get ( ) );
			add_param ( url, "&artist_name=", wish.artist.get ( ) );
			add_param ( url, "&album_name=", wish.album.get ( ) );
			url.add ( L"&duration=" ).number ( static_cast < int64_t > ( wish.duration + 0.5 ) );

			if ( net::request ( L"GET", url.text ( ), nullptr, nullptr, 0, reply ) ) {

				reached = true;

				if ( reply.status == 200 && doc.parse ( reply.body.data ( ), reply.body.count ( ) ) && parse_entry ( doc, doc.root ( ), out ) )
					return true;

			}

		}

		wchar_t title [ 200 ];
		base::writer url;

		clean_title ( wish.title.get ( ), title, 200 );
		url.add ( L"https://lrclib.net/api/search?" );
		add_param ( url, "track_name=", title );
		add_param ( url, "&artist_name=", wish.artist.get ( ) );

		if ( !net::request ( L"GET", url.text ( ), nullptr, nullptr, 0, reply ) )
			return reached;

		if ( reply.status != 200 || !doc.parse ( reply.body.data ( ), reply.body.count ( ) ) )
			return true;

		int32_t best = -1;
		double best_score = -1000.0;

		for ( int32_t item = doc.first ( doc.root ( ) ); item >= 0; item = doc.next ( item ) ) {

			double length = doc.number ( doc.find ( item, "duration" ) );
			double gap = wish.duration > 0.0 && length > 0.0 ? ( length > wish.duration ? length - wish.duration : wish.duration - length ) : 0.0;
			bool synced = doc.text ( doc.find ( item, "syncedLyrics" ) ) [ 0 ] != 0;
			bool plain = doc.text ( doc.find ( item, "plainLyrics" ) ) [ 0 ] != 0;
			bool instrumental = doc.flag ( doc.find ( item, "instrumental" ) );

			if ( gap > 6.0 || ( !synced && !plain && !instrumental ) )
				continue;

			double score = ( synced ? 3.0 : ( plain ? 1.5 : 1.0 ) ) - gap * 0.2;

			if ( score > best_score ) {

				best_score = score;
				best = item;

			}

		}

		if ( best >= 0 )
			parse_entry ( doc, best, out );

		return true;

	}

	static DWORD __stdcall worker ( void* ) {

		while ( true ) {

			WaitForSingleObject ( wake, INFINITE );

			while ( true ) {

				song wish;
				bool work = false;

				AcquireSRWLockExclusive ( &lock );

				if ( waiting ) {

					wish = wanted;
					waiting = false;
					work = true;

				}

				ReleaseSRWLockExclusive ( &lock );

				if ( !work )
					break;

				sheet result;

				result.key = wish.key;

				bool reached = fetch ( wish, result );

				AcquireSRWLockExclusive ( &lock );

				if ( reached ) {

					if ( cache.count ( ) >= 40 )
						cache.remove_at ( 0 );

					cache.push ( result );

				}

				if ( current.key == wish.key ) {

					current = result;
					current.loading = false;
					current.revision = ++counter;

				}

				ReleaseSRWLockExclusive ( &lock );

			}

		}

		return 0;

	}

	void request ( uint64_t key, const wchar_t* title, const wchar_t* artist, const wchar_t* album, double duration ) {

		if ( !key || !title || !title [ 0 ] )
			return;

		AcquireSRWLockExclusive ( &lock );

		for ( const sheet& entry : cache ) {

			if ( entry.key == key ) {

				current = entry;
				current.loading = false;
				current.revision = ++counter;
				ReleaseSRWLockExclusive ( &lock );

				return;

			}

		}

		wanted.key = key;
		wanted.title.set ( title );
		wanted.artist.set ( artist );
		wanted.album.set ( album );
		wanted.duration = duration;
		waiting = true;
		current = sheet ( );
		current.key = key;
		current.loading = true;
		current.revision = ++counter;

		if ( !thread ) {

			wake = CreateEventW ( nullptr, FALSE, FALSE, nullptr );
			thread = CreateThread ( nullptr, 0, &worker, nullptr, 0, nullptr );

		}

		ReleaseSRWLockExclusive ( &lock );
		SetEvent ( wake );

	}

	uint32_t revision ( ) {

		AcquireSRWLockShared ( &lock );

		uint32_t value = current.revision;

		ReleaseSRWLockShared ( &lock );

		return value;

	}

	void read ( sheet& out ) {

		AcquireSRWLockShared ( &lock );
		out = current;
		ReleaseSRWLockShared ( &lock );

	}

#ifdef PEARL_DEBUG

	void provide ( uint64_t key, const wchar_t* text ) {

		sheet entry;

		entry.key = key;
		parse_synced ( text, entry.lines );
		entry.found = !entry.lines.empty ( );
		entry.synced = entry.found;
		AcquireSRWLockExclusive ( &lock );

		for ( int32_t i = cache.count ( ) - 1; i >= 0; i-- ) {

			if ( cache [ static_cast < size_t > ( i ) ].key == key )
				cache.remove_at ( static_cast < size_t > ( i ) );

		}

		cache.push ( entry );

		if ( current.key == key ) {

			current = entry;
			current.revision = ++counter;

		}

		ReleaseSRWLockExclusive ( &lock );

	}

#endif

}