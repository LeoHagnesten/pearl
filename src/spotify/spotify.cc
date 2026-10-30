#include <winsock2.h>
#include <windows.h>
#include "spotify/spotify.hh"
#include "spotify/web.hh"
#include "library/catalog.hh"
#include "net/http.hh"
#include "base/json.hh"
#include "base/files.hh"
#include <objbase.h>
#include <wincrypt.h>
#include <dpapi.h>
#include <bcrypt.h>
#include <shellapi.h>

namespace spotify {

	enum class order : int32_t {

		CLIENT,
		CONNECT,
		DISCONNECT,
		OPEN,
		PLAY,
		PAUSE,
		NEXT,
		PREVIOUS,
		SEEK,
		VOLUME,
		SHUFFLE,
		REPEAT,
		SEARCH,
		MORE,
		TRACKS,
		DETAIL,

	};

	struct command {

		order kind = order::PLAY;
		double number = 0.0;
		base::text text;

	};

	static SRWLOCK lock = SRWLOCK_INIT;
	static SRWLOCK token_lock = SRWLOCK_INIT;
	static SRWLOCK album_lock = SRWLOCK_INIT;
	static snapshot state;
	static base::list < command > commands;
	static base::list < library::art* > art_ready;

	struct cached_art {

		uint64_t key = 0;
		base::list < char > bytes;

	};

	static base::list < cached_art* > art_cache;
	static size_t art_cache_bytes = 0;
	static HANDLE thread = nullptr;
	static HANDLE wake = nullptr;
	static volatile long cancelling = 0;
	static base::text client_id;
	static base::text refresh_token;
	static base::text access_token;
	static double access_expiry = 0.0;
	static wchar_t art_url [ 512 ] = { };
	static wchar_t track_id [ 64 ] = { };
	static double poll_at = 0.0;
	static double last_progress = -1.0;
	static base::list < result > found;
	static detail detail_data;
	static base::list < result > found_albums;
	static base::list < result > found_artists;
	static base::text found_query;
	static uint32_t found_generation = 0;
	static bool found_more = false;
	static bool found_loading = false;
	static int32_t page_size = 10;
	static base::list < uint64_t > album_keys;
	static base::list < base::text > album_values;
	static base::list < base::text > art_queue;
	static base::list < uint64_t > art_queue_keys;
	static base::list < uint64_t > art_flight;
	static int32_t art_threads = 0;
	static HANDLE art_wake = nullptr;
	static double command_stamp = 0.0;
	static HWND notify_window = nullptr;
	static UINT notify_message = 0;
	static const wchar_t* redirect = L"http://127.0.0.1:43821/callback";

	static double now_seconds ( ) {

		static LARGE_INTEGER frequency = { };
		LARGE_INTEGER value;

		if ( !frequency.QuadPart )
			QueryPerformanceFrequency ( &frequency );

		QueryPerformanceCounter ( &value );

		return static_cast < double > ( value.QuadPart ) / static_cast < double > ( frequency.QuadPart );

	}

	static void copy_text ( wchar_t* target, int32_t room, const wchar_t* source ) {

		int32_t i = 0;

		for ( ; source && source [ i ] && i < room - 1; i++ )
			target [ i ] = source [ i ];

		target [ i ] = 0;

	}

	void note ( const char* text ) {

		static bool fresh = true;
		wchar_t path [ 600 ];
		base::writer line;

		line.add ( files::folder ( ) ).add ( L"\\spotify.log" );

		for ( int32_t i = 0; i <= line.size ( ); i++ )
			path [ i ] = line.text ( ) [ i ];

		WIN32_FILE_ATTRIBUTE_DATA info = { };
		bool large = GetFileAttributesExW ( path, GetFileExInfoStandard, &info ) && ( info.nFileSizeHigh || info.nFileSizeLow > 262144 );
		HANDLE file = CreateFileW ( path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, fresh && large ? CREATE_ALWAYS : OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );

		fresh = false;

		if ( file == INVALID_HANDLE_VALUE )
			return;

		SYSTEMTIME now = { };
		char stamp [ 16 ];
		DWORD done = 0;

		GetLocalTime ( &now );
		stamp [ 0 ] = static_cast < char > ( '0' + now.wHour / 10 );
		stamp [ 1 ] = static_cast < char > ( '0' + now.wHour % 10 );
		stamp [ 2 ] = ':';
		stamp [ 3 ] = static_cast < char > ( '0' + now.wMinute / 10 );
		stamp [ 4 ] = static_cast < char > ( '0' + now.wMinute % 10 );
		stamp [ 5 ] = ':';
		stamp [ 6 ] = static_cast < char > ( '0' + now.wSecond / 10 );
		stamp [ 7 ] = static_cast < char > ( '0' + now.wSecond % 10 );
		stamp [ 8 ] = ' ';
		WriteFile ( file, stamp, 9, &done, nullptr );
		WriteFile ( file, text, static_cast < DWORD > ( base::text_length ( text ) ), &done, nullptr );
		WriteFile ( file, "\r\n", 2, &done, nullptr );
		CloseHandle ( file );

	}

	static void note_call ( const wchar_t* method, const wchar_t* path, const net::response& reply, bool ok ) {

		char line [ 900 ];
		int32_t length = 0;
		auto put = [ & ] ( const char* text ) {

			for ( int32_t i = 0; text [ i ] && length < 880; i++ )
				line [ length++ ] = text [ i ];

		};
		auto put_wide = [ & ] ( const wchar_t* text ) {

			for ( int32_t i = 0; text [ i ] && text [ i ] != L'?' && length < 880; i++ )
				line [ length++ ] = text [ i ] < 128 ? static_cast < char > ( text [ i ] ) : '?';

		};
		auto put_number = [ & ] ( uint32_t value ) {

			char digits [ 12 ];
			int32_t count = 0;

			do {

				digits [ count++ ] = static_cast < char > ( '0' + value % 10 );
				value /= 10;

			} while ( value );

			while ( count && length < 880 )
				line [ length++ ] = digits [ --count ];

		};

		put_wide ( method );
		put ( " " );
		put_wide ( path );

		if ( !ok ) {

			put ( " -> failed, error " );
			put_number ( reply.error );

		} else {

			put ( " -> " );
			put_number ( static_cast < uint32_t > ( reply.status ) );

			if ( reply.status == 429 ) {

				put ( " retry " );
				put_number ( static_cast < uint32_t > ( reply.retry_after ) );

			}

			if ( reply.status >= 300 ) {

				put ( " " );

				for ( int32_t i = 0; i < reply.body.count ( ) && i < 300 && length < 880; i++ ) {

					char c = reply.body [ static_cast < size_t > ( i ) ];

					line [ length++ ] = c == '\r' || c == '\n' ? ' ' : c;

				}

			}

		}

		line [ length ] = 0;
		note ( line );

	}

	void notify ( ) {

		if ( notify_window )
			PostMessageW ( notify_window, notify_message, 0, 0 );

	}

	static void publish ( ) {

		state.revision++;
		ReleaseSRWLockExclusive ( &lock );
		notify ( );

	}

	static void say ( const wchar_t* text ) {

		AcquireSRWLockExclusive ( &lock );
		copy_text ( state.message, 256, text );
		publish ( );

	}

	static void push ( order kind, double number = 0.0, const wchar_t* text = nullptr ) {

		AcquireSRWLockExclusive ( &lock );
		command_stamp = now_seconds ( );

		if ( kind == order::VOLUME || kind == order::SEEK || kind == order::SEARCH || kind == order::MORE || kind == order::DETAIL ) {

			for ( int32_t i = commands.count ( ) - 1; i >= 0; i-- ) {

				if ( commands [ static_cast < size_t > ( i ) ].kind == kind )
					commands.remove_at ( static_cast < size_t > ( i ) );

			}

		}

		command& item = commands.emplace ( );

		item.kind = kind;
		item.number = number;

		if ( text )
			item.text.set ( text );

		ReleaseSRWLockExclusive ( &lock );
		SetEvent ( wake );

	}

	static void base64_url ( const uint8_t* data, int32_t size, base::list < char >& out ) {

		const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
		int32_t i = 0;

		for ( ; i + 2 < size; i += 3 ) {

			uint32_t chunk = ( static_cast < uint32_t > ( data [ i ] ) << 16 ) | ( static_cast < uint32_t > ( data [ i + 1 ] ) << 8 ) | data [ i + 2 ];

			out.push ( alphabet [ ( chunk >> 18 ) & 63 ] );
			out.push ( alphabet [ ( chunk >> 12 ) & 63 ] );
			out.push ( alphabet [ ( chunk >> 6 ) & 63 ] );
			out.push ( alphabet [ chunk & 63 ] );

		}

		if ( size - i == 1 ) {

			uint32_t chunk = static_cast < uint32_t > ( data [ i ] ) << 16;

			out.push ( alphabet [ ( chunk >> 18 ) & 63 ] );
			out.push ( alphabet [ ( chunk >> 12 ) & 63 ] );

		} else if ( size - i == 2 ) {

			uint32_t chunk = ( static_cast < uint32_t > ( data [ i ] ) << 16 ) | ( static_cast < uint32_t > ( data [ i + 1 ] ) << 8 );

			out.push ( alphabet [ ( chunk >> 18 ) & 63 ] );
			out.push ( alphabet [ ( chunk >> 12 ) & 63 ] );
			out.push ( alphabet [ ( chunk >> 6 ) & 63 ] );

		}

	}

	static void random_token ( int32_t bytes, base::list < char >& out ) {

		uint8_t data [ 64 ];

		BCryptGenRandom ( nullptr, data, static_cast < ULONG > ( bytes ), BCRYPT_USE_SYSTEM_PREFERRED_RNG );
		out.clear ( );
		base64_url ( data, bytes, out );

	}

	static void sha256 ( const char* data, int32_t size, uint8_t* digest ) {

		BCRYPT_ALG_HANDLE algorithm = nullptr;

		if ( BCryptOpenAlgorithmProvider ( &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0 ) == 0 ) {

			BCryptHash ( algorithm, nullptr, 0, reinterpret_cast < PUCHAR > ( const_cast < char* > ( data ) ), static_cast < ULONG > ( size ), digest, 32 );
			BCryptCloseAlgorithmProvider ( algorithm, 0 );

		}

	}

	static void store_path ( wchar_t* path ) {

		base::writer line;

		line.add ( files::folder ( ) ).add ( L"\\spotify.dat" );

		for ( int32_t i = 0; i <= line.size ( ); i++ )
			path [ i ] = line.text ( ) [ i ];

	}

	static void save_secrets ( ) {

		base::list < wchar_t > plain;
		wchar_t path [ 600 ];

		for ( int32_t i = 0; i < client_id.size ( ); i++ )
			plain.push ( client_id.get ( ) [ i ] );

		plain.push ( 0 );

		for ( int32_t i = 0; i < refresh_token.size ( ); i++ )
			plain.push ( refresh_token.get ( ) [ i ] );

		plain.push ( 0 );

		DATA_BLOB input = { static_cast < DWORD > ( plain.size ( ) * sizeof ( wchar_t ) ), reinterpret_cast < BYTE* > ( plain.data ( ) ) };
		DATA_BLOB output = { };

		store_path ( path );

		if ( CryptProtectData ( &input, L"Pearl Spotify", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output ) ) {

			files::write ( path, reinterpret_cast < const char* > ( output.pbData ), static_cast < int32_t > ( output.cbData ) );
			LocalFree ( output.pbData );

		}

		for ( wchar_t& c : plain )
			c = 0;

	}

	static void load_secrets ( ) {

		base::list < char > data;
		wchar_t path [ 600 ];

		store_path ( path );

		if ( !files::read ( path, data ) || data.count ( ) < 2 )
			return;

		DATA_BLOB input = { static_cast < DWORD > ( data.count ( ) - 1 ), reinterpret_cast < BYTE* > ( data.data ( ) ) };
		DATA_BLOB output = { };

		if ( !CryptUnprotectData ( &input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output ) )
			return;

		const wchar_t* text = reinterpret_cast < const wchar_t* > ( output.pbData );
		int32_t count = static_cast < int32_t > ( output.cbData / sizeof ( wchar_t ) );
		int32_t split = 0;

		while ( split < count && text [ split ] )
			split++;

		client_id.set ( text );

		if ( split + 1 < count )
			refresh_token.set ( text + split + 1 );

		SecureZeroMemory ( output.pbData, output.cbData );
		LocalFree ( output.pbData );

	}

	static void publish_flags ( ) {

		AcquireSRWLockExclusive ( &lock );
		state.started = true;
		state.configured = !client_id.empty ( );
		state.connected = !refresh_token.empty ( );
		publish ( );

	}

	static bool token_call ( const char* form ) {

		net::response reply;
		const wchar_t* headers = L"Content-Type: application/x-www-form-urlencoded\r\n";

		if ( !net::request ( L"POST", L"https://accounts.spotify.com/api/token", headers, form, base::text_length ( form ), reply ) ) {

			note_call ( L"POST", L"/api/token", reply, false );

			return false;

		}

		if ( reply.status != 200 )
			note_call ( L"POST", L"/api/token", reply, true );

		json::document doc;

		if ( !doc.parse ( reply.body.data ( ), reply.body.count ( ) ) )
			return false;

		int32_t root = doc.root ( );

		if ( reply.status != 200 ) {

			if ( reply.status == 400 && base::same_text ( doc.text ( doc.find ( root, "error" ) ), L"invalid_grant" ) ) {

				refresh_token.set ( L"" );
				save_secrets ( );
				publish_flags ( );
				say ( L"Spotify sign-in expired. Connect again." );

			}

			return false;

		}

		access_token.set ( doc.text ( doc.find ( root, "access_token" ) ) );
		access_expiry = now_seconds ( ) + doc.number ( doc.find ( root, "expires_in" ), 3600.0 );

		const wchar_t* fresh = doc.text ( doc.find ( root, "refresh_token" ) );

		if ( fresh [ 0 ] ) {

			refresh_token.set ( fresh );
			save_secrets ( );

		}

		return !access_token.empty ( );

	}

	static bool ensure_token ( ) {

		if ( refresh_token.empty ( ) || client_id.empty ( ) )
			return false;

		if ( !access_token.empty ( ) && now_seconds ( ) < access_expiry - 60.0 )
			return true;

		base::list < char > form;
		const char* head = "grant_type=refresh_token&refresh_token=";

		for ( int32_t i = 0; head [ i ]; i++ )
			form.push ( head [ i ] );

		net::encode ( form, refresh_token.get ( ) );

		const char* middle = "&client_id=";

		for ( int32_t i = 0; middle [ i ]; i++ )
			form.push ( middle [ i ] );

		net::encode ( form, client_id.get ( ) );
		form.push ( 0 );

		return token_call ( form.data ( ) );

	}

	bool api ( const wchar_t* method, const wchar_t* path, const char* body, net::response& reply ) {

		for ( int32_t attempt = 0; attempt < 2; attempt++ ) {

			base::writer url;
			base::writer headers;

			AcquireSRWLockExclusive ( &token_lock );

			bool ready = ensure_token ( );

			if ( ready )
				headers.add ( L"Authorization: Bearer " ).add ( access_token.get ( ) ).add ( L"\r\nContent-Type: application/json\r\n" );

			ReleaseSRWLockExclusive ( &token_lock );

			if ( !ready )
				return false;

			url.add ( L"https://api.spotify.com/v1" ).add ( path );

			if ( !net::request ( method, url.text ( ), headers.text ( ), body, body ? base::text_length ( body ) : 0, reply ) ) {

				note_call ( method, path, reply, false );

				return false;

			}

			if ( reply.status >= 300 || !base::same_text ( method, L"GET" ) )
				note_call ( method, path, reply, true );

			if ( body && !base::same_text ( method, L"GET" ) ) {

				char line [ 420 ];
				int32_t length = 0;
				const char* head = "  body ";

				for ( int32_t i = 0; head [ i ]; i++ )
					line [ length++ ] = head [ i ];

				for ( int32_t i = 0; body [ i ] && length < 410; i++ )
					line [ length++ ] = body [ i ];

				line [ length ] = 0;
				note ( line );

			}

			if ( reply.status == 401 ) {

				AcquireSRWLockExclusive ( &token_lock );
				access_token.set ( L"" );
				ReleaseSRWLockExclusive ( &token_lock );

				continue;

			}

			if ( reply.status == 429 && attempt == 0 && reply.retry_after >= 1 && reply.retry_after <= 3 ) {

				Sleep ( static_cast < DWORD > ( base::clamp ( reply.retry_after, 1, 5 ) * 1000 ) );

				continue;

			}

			return true;

		}

		return false;

	}

	static void read_account ( ) {

		net::response reply;

		if ( !api ( L"GET", L"/me", nullptr, reply ) || reply.status != 200 )
			return;

		json::document doc;

		if ( !doc.parse ( reply.body.data ( ), reply.body.count ( ) ) )
			return;

		AcquireSRWLockExclusive ( &lock );
		copy_text ( state.account, 128, doc.text ( doc.find ( doc.root ( ), "display_name" ) ) );
		publish ( );

	}

	static bool parse_query ( const char* request, const char* name, char* out, int32_t room ) {

		const char* line_end = request;

		while ( *line_end && *line_end != '\r' && *line_end != '\n' )
			line_end++;

		int32_t length = base::text_length ( name );

		for ( const char* at = request; at < line_end; at++ ) {

			if ( ( at [ -1 ] == '?' || at [ -1 ] == '&' ) && __builtin_memcmp ( at, name, static_cast < size_t > ( length ) ) == 0 && at [ length ] == '=' ) {

				const char* value = at + length + 1;
				int32_t count = 0;

				while ( value < line_end && *value != '&' && *value != ' ' && count < room - 1 )
					out [ count++ ] = *value++;

				out [ count ] = 0;

				return true;

			}

		}

		return false;

	}

	static void respond ( SOCKET client, bool success ) {

		const char* body_ok = "<!doctype html><html><head><meta charset=\"utf-8\"><title>Pearl</title><style>body{margin:0;height:100vh;display:flex;align-items:center;justify-content:center;background:#f5f5f7;font-family:'Segoe UI Variable Display','Segoe UI',sans-serif;color:#1d1d1f}div{text-align:center}h1{font-size:28px;margin:0 0 8px}p{margin:0;color:#6e6e73}</style></head><body><div><h1>Pearl is connected to Spotify</h1><p>You can close this tab.</p></div></body></html>";
		const char* body_fail = "<!doctype html><html><head><meta charset=\"utf-8\"><title>Pearl</title><style>body{margin:0;height:100vh;display:flex;align-items:center;justify-content:center;background:#f5f5f7;font-family:'Segoe UI Variable Display','Segoe UI',sans-serif;color:#1d1d1f}div{text-align:center}h1{font-size:28px;margin:0 0 8px}p{margin:0;color:#6e6e73}</style></head><body><div><h1>Spotify did not connect</h1><p>Return to Pearl and try again.</p></div></body></html>";
		const char* body = success ? body_ok : body_fail;
		base::list < char > out;
		base::writer head;

		head.add ( L"HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: " ).number ( base::text_length ( body ) ).add ( L"\r\n\r\n" );

		for ( int32_t i = 0; i < head.size ( ); i++ )
			out.push ( static_cast < char > ( head.text ( ) [ i ] ) );

		for ( int32_t i = 0; body [ i ]; i++ )
			out.push ( body [ i ] );

		send ( client, out.data ( ), out.count ( ), 0 );

	}

	static void run_login ( ) {

		if ( client_id.empty ( ) ) {

			say ( L"Paste your Spotify Client ID first." );

			return;

		}

		WSADATA startup;

		if ( WSAStartup ( MAKEWORD ( 2, 2 ), &startup ) != 0 )
			return;

		SOCKET listener = socket ( AF_INET, SOCK_STREAM, IPPROTO_TCP );
		sockaddr_in address = { };

		address.sin_family = AF_INET;
		address.sin_port = htons ( 43821 );
		address.sin_addr.s_addr = htonl ( INADDR_LOOPBACK );

		if ( listener == INVALID_SOCKET || bind ( listener, reinterpret_cast < sockaddr* > ( &address ), sizeof ( address ) ) != 0 || listen ( listener, 4 ) != 0 ) {

			if ( listener != INVALID_SOCKET )
				closesocket ( listener );

			WSACleanup ( );
			say ( L"Port 43821 is busy. Close other Pearl windows and try again." );

			return;

		}

		base::list < char > verifier;
		base::list < char > challenge;
		base::list < char > nonce;
		uint8_t digest [ 32 ] = { };

		random_token ( 48, verifier );
		verifier.push ( 0 );
		sha256 ( verifier.data ( ), verifier.count ( ) - 1, digest );
		base64_url ( digest, 32, challenge );
		challenge.push ( 0 );
		random_token ( 16, nonce );
		nonce.push ( 0 );

		base::writer url;

		url.add ( L"https://accounts.spotify.com/authorize?response_type=code&client_id=" ).add ( client_id.get ( ) );
		url.add ( L"&scope=user-read-playback-state%20user-modify-playback-state%20user-read-currently-playing%20user-library-read%20user-library-modify%20user-top-read%20user-read-recently-played" );
		url.add ( L"&redirect_uri=http%3A%2F%2F127.0.0.1%3A43821%2Fcallback&code_challenge_method=S256&code_challenge=" ).add ( challenge.data ( ) ).add ( L"&state=" ).add ( nonce.data ( ) );

		AcquireSRWLockExclusive ( &lock );
		state.waiting = true;
		publish ( );
		say ( L"Approve Pearl in your browser" );
		InterlockedExchange ( &cancelling, 0 );
		ShellExecuteW ( nullptr, L"open", url.text ( ), nullptr, nullptr, SW_SHOWNORMAL );

		char code [ 1024 ] = { };
		bool received = false;
		double deadline = now_seconds ( ) + 180.0;

		while ( !received && now_seconds ( ) < deadline && !cancelling ) {

			fd_set ready;
			timeval wait_time = { 0, 250000 };

			FD_ZERO ( &ready );
			FD_SET ( listener, &ready );

			if ( select ( 0, &ready, nullptr, nullptr, &wait_time ) <= 0 )
				continue;

			SOCKET client = accept ( listener, nullptr, nullptr );

			if ( client == INVALID_SOCKET )
				continue;

			char request [ 8192 ] = { };
			int32_t total = 0;
			DWORD timeout = 3000;

			setsockopt ( client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast < const char* > ( &timeout ), sizeof ( timeout ) );

			while ( total < 8191 ) {

				int32_t got = recv ( client, request + total, 8191 - total, 0 );

				if ( got <= 0 )
					break;

				total += got;
				request [ total ] = 0;

				if ( total >= 4 && __builtin_memcmp ( request + total - 4, "\r\n\r\n", 4 ) == 0 )
					break;

			}

			char returned [ 128 ] = { };
			bool path_ok = __builtin_memcmp ( request, "GET /callback", 13 ) == 0;
			bool has_code = path_ok && parse_query ( request + 4, "code", code, 1024 );
			bool state_ok = path_ok && parse_query ( request + 4, "state", returned, 128 ) && base::same_text ( returned, nonce.data ( ) );

			respond ( client, has_code && state_ok );
			closesocket ( client );

			if ( !path_ok )
				continue;

			received = true;

			if ( !has_code || !state_ok )
				code [ 0 ] = 0;

		}

		closesocket ( listener );
		WSACleanup ( );

		bool success = false;

		if ( received && code [ 0 ] ) {

			base::list < char > form;
			base::writer wide_code;
			const char* parts [ 5 ] = { "grant_type=authorization_code&code=", "&redirect_uri=", "&client_id=", "&code_verifier=", nullptr };

			wide_code.add ( code );

			for ( int32_t i = 0; parts [ 0 ] [ i ]; i++ )
				form.push ( parts [ 0 ] [ i ] );

			net::encode ( form, wide_code.text ( ) );

			for ( int32_t i = 0; parts [ 1 ] [ i ]; i++ )
				form.push ( parts [ 1 ] [ i ] );

			net::encode ( form, redirect );

			for ( int32_t i = 0; parts [ 2 ] [ i ]; i++ )
				form.push ( parts [ 2 ] [ i ] );

			net::encode ( form, client_id.get ( ) );

			for ( int32_t i = 0; parts [ 3 ] [ i ]; i++ )
				form.push ( parts [ 3 ] [ i ] );

			for ( int32_t i = 0; verifier [ static_cast < size_t > ( i ) ]; i++ )
				form.push ( verifier [ static_cast < size_t > ( i ) ] );

			form.push ( 0 );
			AcquireSRWLockExclusive ( &token_lock );
			success = token_call ( form.data ( ) ) && !refresh_token.empty ( );
			ReleaseSRWLockExclusive ( &token_lock );

		}

		AcquireSRWLockExclusive ( &lock );
		state.waiting = false;
		publish ( );
		publish_flags ( );

		if ( success ) {

			say ( L"" );
			read_account ( );
			poll_at = 0.0;

		} else
			say ( cancelling ? L"" : L"Spotify did not connect. Check the Client ID and redirect URI." );

	}

	static bool parse_link ( const wchar_t* text, wchar_t* kind, wchar_t* id ) {

		const wchar_t* kinds [ 6 ] = { L"track", L"album", L"playlist", L"artist", L"episode", L"show" };
		int32_t length = base::text_length ( text );

		for ( int32_t i = 0; i < length; i++ ) {

			const wchar_t* at = text + i;
			const wchar_t* rest = nullptr;
			wchar_t separator = L'/';

			if ( CompareStringOrdinal ( at, base::min ( 17, length - i ), L"open.spotify.com/", 17, TRUE ) == CSTR_EQUAL )
				rest = at + 17;
			else if ( CompareStringOrdinal ( at, base::min ( 8, length - i ), L"spotify:", 8, TRUE ) == CSTR_EQUAL ) {

				rest = at + 8;
				separator = L':';

			}

			if ( !rest )
				continue;

			if ( rest [ 0 ] == L'i' && rest [ 1 ] == L'n' && rest [ 2 ] == L't' && rest [ 3 ] == L'l' && rest [ 4 ] == L'-' ) {

				while ( *rest && *rest != L'/' )
					rest++;

				if ( *rest == L'/' )
					rest++;

			}

			for ( const wchar_t* candidate : kinds ) {

				int32_t size = base::text_length ( candidate );

				if ( CompareStringOrdinal ( rest, size, candidate, size, TRUE ) == CSTR_EQUAL && rest [ size ] == separator ) {

					const wchar_t* value = rest + size + 1;
					int32_t count = 0;

					while ( count < 62 && ( ( value [ count ] >= L'0' && value [ count ] <= L'9' ) || ( value [ count ] >= L'a' && value [ count ] <= L'z' ) || ( value [ count ] >= L'A' && value [ count ] <= L'Z' ) ) )
						count++;

					if ( count < 10 )
						continue;

					copy_text ( kind, 16, candidate );

					for ( int32_t k = 0; k < count; k++ )
						id [ k ] = value [ k ];

					id [ count ] = 0;

					return true;

				}

			}

		}

		return false;

	}

	static bool short_link ( const wchar_t* text ) {

		int32_t length = base::text_length ( text );

		for ( int32_t i = 0; i < length; i++ ) {

			if ( CompareStringOrdinal ( text + i, base::min ( 13, length - i ), L"spotify.link/", 13, TRUE ) == CSTR_EQUAL )
				return true;

			if ( CompareStringOrdinal ( text + i, base::min ( 17, length - i ), L"spotify.app.link/", 17, TRUE ) == CSTR_EQUAL )
				return true;

		}

		return false;

	}

	bool is_link ( const wchar_t* text ) {

		wchar_t kind [ 16 ];
		wchar_t id [ 64 ];

		return text && ( parse_link ( text, kind, id ) || short_link ( text ) );

	}

	static bool resolve ( const wchar_t* text, wchar_t* kind, wchar_t* id ) {

		if ( parse_link ( text, kind, id ) )
			return true;

		if ( !short_link ( text ) )
			return false;

		base::writer url;
		const wchar_t* start = text;

		while ( *start == L' ' )
			start++;

		if ( !( start [ 0 ] == L'h' && start [ 1 ] == L't' && start [ 2 ] == L't' && start [ 3 ] == L'p' ) )
			url.add ( L"https://" );

		for ( int32_t i = 0; start [ i ] && start [ i ] != L' ' && start [ i ] != L'\r' && start [ i ] != L'\n'; i++ )
			url.add ( start [ i ] );

		net::response reply;

		if ( !net::request ( L"GET", url.text ( ), nullptr, nullptr, 0, reply ) )
			return false;

		if ( parse_link ( reply.location, kind, id ) )
			return true;

		base::list < wchar_t > body;

		for ( char c : reply.body )
			body.push ( static_cast < wchar_t > ( static_cast < uint8_t > ( c ) ) );

		body.push ( 0 );

		return parse_link ( body.data ( ), kind, id );

	}

	static void launch_app ( ) {

		wchar_t path [ 600 ];
		DWORD length = GetEnvironmentVariableW ( L"APPDATA", path, 560 );

		if ( length && length < 560 ) {

			const wchar_t* tail = L"\\Spotify\\Spotify.exe";

			for ( int32_t i = 0; tail [ i ]; i++ )
				path [ length++ ] = tail [ i ];

			path [ length ] = 0;

			if ( GetFileAttributesW ( path ) != INVALID_FILE_ATTRIBUTES ) {

				ShellExecuteW ( nullptr, L"open", path, L"--minimized", nullptr, SW_SHOWMINNOACTIVE );

				return;

			}

		}

		ShellExecuteW ( nullptr, L"open", L"spotify:", nullptr, nullptr, SW_SHOWMINNOACTIVE );

	}

	static bool find_device ( wchar_t* id, bool& active, bool& local ) {

		net::response reply;

		active = false;
		local = false;

		if ( !api ( L"GET", L"/me/player/devices", nullptr, reply ) || reply.status != 200 )
			return false;

		json::document doc;

		if ( !doc.parse ( reply.body.data ( ), reply.body.count ( ) ) )
			return false;

		int32_t list = doc.find ( doc.root ( ), "devices" );
		int32_t best = -1;
		int32_t best_score = -1;
		wchar_t computer [ 64 ] = { };
		DWORD size = 63;

		GetComputerNameW ( computer, &size );

		for ( int32_t item = doc.first ( list ); item >= 0; item = doc.next ( item ) ) {

			if ( doc.flag ( doc.find ( item, "is_restricted" ) ) )
				continue;

			bool here_pc = CompareStringOrdinal ( doc.text ( doc.find ( item, "name" ) ), -1, computer, -1, TRUE ) == CSTR_EQUAL;
			bool desktop = base::same_text ( doc.text ( doc.find ( item, "type" ) ), L"Computer" );

			if ( !here_pc && !desktop )
				continue;

			int32_t score = here_pc ? 200 : 50;

			if ( doc.flag ( doc.find ( item, "is_active" ) ) )
				score += 10;

			if ( score > best_score ) {

				best_score = score;
				best = item;

			}

		}

		if ( best < 0 )
			return false;

		copy_text ( id, 128, doc.text ( doc.find ( best, "id" ) ) );
		active = doc.flag ( doc.find ( best, "is_active" ) );
		local = best_score >= 200;

		return id [ 0 ] != 0;

	}

	static bool player_state ( const wchar_t* device, bool& on_device, bool& playing ) {

		net::response reply;
		json::document doc;

		on_device = false;
		playing = false;

		if ( !api ( L"GET", L"/me/player", nullptr, reply ) || reply.status != 200 || !doc.parse ( reply.body.data ( ), reply.body.count ( ) ) )
			return false;

		int32_t root = doc.root ( );

		on_device = !device || base::same_text ( doc.text ( doc.path ( root, "device", "id" ) ), device );
		playing = doc.flag ( doc.find ( root, "is_playing" ) );

		char line [ 300 ];
		int32_t length = 0;
		const char* parts [ 2 ] = { on_device ? "state on-device " : "state other-device ", playing ? "playing " : "stopped " };

		for ( const char* part : parts ) {

			for ( int32_t i = 0; part [ i ] && length < 200; i++ )
				line [ length++ ] = part [ i ];

		}

		const wchar_t* uri = doc.text ( doc.path ( root, "item", "uri" ) );

		for ( int32_t i = 0; uri [ i ] && length < 290; i++ )
			line [ length++ ] = uri [ i ] < 128 ? static_cast < char > ( uri [ i ] ) : '?';

		line [ length ] = 0;
		note ( line );

		return true;

	}

	static void probe ( ) {

		const wchar_t* paths [ 2 ] = { L"/me/player/devices", L"/me/player?additional_types=episode" };

		for ( const wchar_t* path : paths ) {

			net::response reply;
			char line [ 1400 ];
			int32_t length = 0;

			if ( !api ( L"GET", path, nullptr, reply ) )
				continue;

			for ( int32_t i = 0; path [ i ] && length < 60; i++ )
				line [ length++ ] = static_cast < char > ( path [ i ] );

			line [ length++ ] = ' ';

			for ( int32_t i = 0; i < reply.body.count ( ) && length < 1390; i++ ) {

				char c = reply.body [ static_cast < size_t > ( i ) ];

				if ( c != '\r' && c != '\n' && !( c == ' ' && length > 0 && line [ length - 1 ] == ' ' ) )
					line [ length++ ] = c;

			}

			line [ length ] = 0;
			note ( line );

		}

	}

	static void transfer ( const wchar_t* device, bool play ) {

		base::writer body;
		base::list < char > bytes;
		net::response reply;

		body.add ( L"{\"device_ids\":[\"" ).add ( device ).add ( play ? L"\"],\"play\":true}" : L"\"],\"play\":false}" );

		for ( int32_t i = 0; i < body.size ( ); i++ )
			bytes.push ( static_cast < char > ( body.text ( ) [ i ] ) );

		bytes.push ( 0 );
		api ( L"PUT", L"/me/player", bytes.data ( ), reply );

	}

	static void refused ( const net::response& reply ) {

		json::document doc;
		const wchar_t* reason = L"";

		if ( doc.parse ( reply.body.data ( ), reply.body.count ( ) ) )
			reason = doc.text ( doc.path ( doc.root ( ), "error", "reason" ) );

		if ( base::same_text ( reason, L"PREMIUM_REQUIRED" ) )
			say ( L"Playback control needs Spotify Premium." );
		else {

			base::writer line;

			line.add ( L"Playback was refused (" ).number ( reply.status ).add ( L")." );
			say ( line.text ( ) );

		}

	}

	static bool start_playback ( const char* body ) {

		wchar_t device [ 128 ] = { };
		bool active = false;
		bool local = false;
		bool found = find_device ( device, active, local );

		if ( !found || !local ) {

			note ( "no device, launching player" );
			say ( L"Starting the player" );
			launch_app ( );

			wchar_t fallback [ 128 ];
			bool had = found;

			copy_text ( fallback, 128, device );
			found = false;

			for ( int32_t i = 0; i < ( had ? 15 : 45 ) && !local; i++ ) {

				Sleep ( 1000 );
				found = find_device ( device, active, local );

			}

			if ( !local && had ) {

				copy_text ( device, 128, fallback );
				found = true;
				active = false;

			}

			if ( !found ) {

				note ( "no device after launch" );
				say ( L"No player found. Open Spotify once, then try again." );

				return false;

			}

			Sleep ( 1500 );

		}

		bool on_device = false;
		bool playing = false;

		if ( !active ) {

			note ( "transferring to idle device" );
			transfer ( device, false );
			Sleep ( 700 );

		}

		net::response reply;

		for ( int32_t attempt = 0; attempt < 2; attempt++ ) {

			base::writer path;

			path.add ( L"/me/player/play?device_id=" ).add ( device );

			if ( !api ( L"PUT", path.text ( ), body, reply ) ) {

				say ( L"Couldn't reach the player. Check your connection." );

				return false;

			}

			if ( reply.status == 403 ) {

				refused ( reply );

				return false;

			}

			if ( reply.status == 404 && attempt == 0 ) {

				Sleep ( 800 );
				find_device ( device, active, local );
				transfer ( device, false );
				Sleep ( 1200 );

				continue;

			}

			if ( reply.status >= 300 ) {

				refused ( reply );

				return false;

			}

			for ( int32_t i = 0; i < 8; i++ ) {

				Sleep ( 400 );

				if ( player_state ( device, on_device, playing ) && on_device && playing )
					break;

			}

			if ( !playing ) {

				probe ( );
				say ( L"Spotify isn't responding. Quit and reopen the Spotify app." );

			} else
				say ( L"" );

			return true;

		}

		say ( L"Playback didn't start. Open Spotify once, then try again." );

		return false;

	}

	static void play_body ( const char* body ) {

		say ( L"" );

		if ( start_playback ( body ) ) {

			AcquireSRWLockExclusive ( &lock );
			state.active = true;
			state.playing = true;
			publish ( );

		}

		poll_at = now_seconds ( ) + 0.2;

	}

	void remember_album ( const wchar_t* track_uri, const wchar_t* album_uri ) {

		if ( !track_uri || !track_uri [ 0 ] || !album_uri || !album_uri [ 0 ] )
			return;

		uint64_t key = library::hash ( track_uri, false );

		AcquireSRWLockExclusive ( &album_lock );

		for ( int32_t i = 0; i < album_keys.count ( ); i++ ) {

			if ( album_keys [ static_cast < size_t > ( i ) ] == key ) {

				album_values [ static_cast < size_t > ( i ) ].set ( album_uri );
				ReleaseSRWLockExclusive ( &album_lock );

				return;

			}

		}

		if ( album_keys.count ( ) > 4000 ) {

			album_keys.remove_at ( 0 );
			album_values.remove_at ( 0 );

		}

		album_keys.push ( key );
		album_values.emplace ( ).set ( album_uri );
		ReleaseSRWLockExclusive ( &album_lock );

	}

	static bool album_of ( const wchar_t* track_uri, wchar_t* out ) {

		uint64_t key = library::hash ( track_uri, false );
		bool known = false;

		AcquireSRWLockShared ( &album_lock );

		for ( int32_t i = 0; i < album_keys.count ( ) && !known; i++ ) {

			if ( album_keys [ static_cast < size_t > ( i ) ] == key ) {

				copy_text ( out, 64, album_values [ static_cast < size_t > ( i ) ].get ( ) );
				known = true;

			}

		}

		ReleaseSRWLockShared ( &album_lock );

		if ( known )
			return out [ 0 ] != 0;

		const wchar_t* id = track_uri;

		for ( const wchar_t* at = track_uri; *at; at++ ) {

			if ( *at == L':' )
				id = at + 1;

		}

		base::writer path;
		net::response reply;
		json::document doc;

		path.add ( L"/tracks/" ).add ( id );

		if ( !api ( L"GET", path.text ( ), nullptr, reply ) || reply.status != 200 || !doc.parse ( reply.body.data ( ), reply.body.count ( ) ) )
			return false;

		const wchar_t* album = doc.text ( doc.path ( doc.root ( ), "album", "uri" ) );

		remember_album ( track_uri, album );
		copy_text ( out, 64, album );

		return out [ 0 ] != 0;

	}

	static void play_track ( const wchar_t* track_uri ) {

		wchar_t album [ 64 ] = { };
		base::writer request;
		base::list < char > body;

		if ( album_of ( track_uri, album ) )
			request.add ( L"{\"context_uri\":\"" ).add ( album ).add ( L"\",\"offset\":{\"uri\":\"" ).add ( track_uri ).add ( L"\"},\"position_ms\":0}" );
		else
			request.add ( L"{\"uris\":[\"" ).add ( track_uri ).add ( L"\"],\"position_ms\":0}" );

		for ( int32_t i = 0; i < request.size ( ); i++ ) {

			wchar_t c = request.text ( ) [ i ];

			body.push ( c < 128 ? static_cast < char > ( c ) : '?' );

		}

		body.push ( 0 );
		play_body ( body.data ( ) );

	}

	static void open_item ( const wchar_t* text ) {

		wchar_t kind [ 16 ] = { };
		wchar_t id [ 64 ] = { };

		if ( !resolve ( text, kind, id ) ) {

			say ( L"That link can't be played." );

			return;

		}

		base::writer uri;

		uri.add ( L"spotify:" ).add ( kind ).add ( L":" ).add ( id );

		if ( base::same_text ( kind, L"track" ) ) {

			play_track ( uri.text ( ) );

			return;

		}

		bool single = base::same_text ( kind, L"episode" );
		base::list < char > body;
		const char* head = single ? "{\"uris\":[\"" : "{\"context_uri\":\"";
		const char* tail = single ? "\"]}" : "\"}";

		for ( int32_t i = 0; head [ i ]; i++ )
			body.push ( head [ i ] );

		for ( int32_t i = 0; i < uri.size ( ); i++ )
			body.push ( static_cast < char > ( uri.text ( ) [ i ] ) );

		for ( int32_t i = 0; tail [ i ]; i++ )
			body.push ( tail [ i ] );

		body.push ( 0 );
		play_body ( body.data ( ) );

	}

	static void open_tracks ( const wchar_t* text, int32_t offset ) {

		wchar_t first [ 64 ] = { };
		int32_t index = 0;

		for ( const wchar_t* at = text; *at; ) {

			while ( *at == L' ' )
				at++;

			if ( !*at )
				break;

			int32_t length = 0;

			while ( *at && *at != L' ' ) {

				if ( index == offset && length < 63 )
					first [ length++ ] = *at;

				at++;

			}

			if ( index == offset ) {

				first [ length ] = 0;

				break;

			}

			index++;

		}

		if ( first [ 0 ] )
			play_track ( first );

	}

	static void fetch_art ( const wchar_t* url, uint64_t key ) {

		base::list < char > bytes;
		bool cached = false;

		AcquireSRWLockExclusive ( &lock );

		for ( int32_t i = 0; i < art_cache.count ( ) && !cached; i++ ) {

			if ( art_cache [ static_cast < size_t > ( i ) ]->key == key ) {

				cached_art* entry = art_cache [ static_cast < size_t > ( i ) ];

				bytes = entry->bytes;
				art_cache.remove_at ( static_cast < size_t > ( i ) );
				art_cache.push ( entry );
				cached = true;

			}

		}

		ReleaseSRWLockExclusive ( &lock );

		if ( !cached ) {

			net::response reply;

			if ( !net::request ( L"GET", url, nullptr, nullptr, 0, reply ) || reply.status != 200 || reply.body.empty ( ) )
				return;

			bytes = base::move ( reply.body );

			cached_art* entry = new cached_art ( );

			entry->key = key;
			entry->bytes = bytes;
			AcquireSRWLockExclusive ( &lock );
			art_cache.push ( entry );
			art_cache_bytes += bytes.size ( );

			while ( art_cache_bytes > 24u * 1024u * 1024u && art_cache.count ( ) > 1 ) {

				cached_art* oldest = art_cache [ 0 ];

				art_cache_bytes -= oldest->bytes.size ( );
				art_cache.remove_at ( 0 );
				delete oldest;

			}

			ReleaseSRWLockExclusive ( &lock );

		}

		library::art* result = new library::art ( );

		result->key = key;
		result->found = library::art_from_memory ( reinterpret_cast < const uint8_t* > ( bytes.data ( ) ), bytes.size ( ), result->pixels, result->palette, result->accent );
		library::make_thumb ( *result );

		AcquireSRWLockExclusive ( &lock );
		art_ready.push ( result );
		publish ( );

	}

	static void poll ( ) {

		net::response reply;
		double started = now_seconds ( );

		if ( !api ( L"GET", L"/me/player?additional_types=episode", nullptr, reply ) )
			return;

		if ( reply.status == 204 || reply.body.empty ( ) ) {

			AcquireSRWLockExclusive ( &lock );

			if ( started > command_stamp )
				state.playing = false;

			publish ( );

			return;

		}

		if ( reply.status != 200 )
			return;

		json::document doc;

		if ( !doc.parse ( reply.body.data ( ), reply.body.count ( ) ) )
			return;

		int32_t root = doc.root ( );
		int32_t item = doc.find ( root, "item" );
		int32_t album = doc.find ( item, "album" );
		int32_t show = doc.find ( item, "show" );
		int32_t images = album >= 0 ? doc.find ( album, "images" ) : doc.find ( item, "images" );
		int32_t chosen = -1;
		double chosen_width = 0.0;

		for ( int32_t image = doc.first ( images ); image >= 0; image = doc.next ( image ) ) {

			double width = doc.number ( doc.find ( image, "width" ), 0.0 );

			if ( chosen < 0 || ( width >= 300.0 && ( chosen_width < 300.0 || width < chosen_width ) ) ) {

				chosen = image;
				chosen_width = width;

			}

		}

		base::writer artists;
		int32_t artist_list = doc.find ( item, "artists" );

		for ( int32_t artist = doc.first ( artist_list ); artist >= 0; artist = doc.next ( artist ) ) {

			if ( artists.size ( ) )
				artists.add ( L", " );

			artists.add ( doc.text ( doc.find ( artist, "name" ) ) );

		}

		if ( !artists.size ( ) && show >= 0 )
			artists.add ( doc.text ( doc.find ( show, "publisher" ) ) );

		const wchar_t* id = doc.text ( doc.find ( item, "uri" ) );
		const wchar_t* image_url = doc.text ( doc.find ( chosen, "url" ) );
		const wchar_t* repeat_text = doc.text ( doc.find ( root, "repeat_state" ) );
		int32_t device = doc.find ( root, "device" );
		bool new_track = !base::same_text ( id, track_id );
		bool new_art = image_url [ 0 ] && !base::same_text ( image_url, art_url );
		double heard = doc.number ( doc.find ( root, "progress_ms" ) ) / 1000.0;

		observe ( doc, item, heard, doc.flag ( doc.find ( root, "is_playing" ) ) );
		AcquireSRWLockExclusive ( &lock );

		if ( started > command_stamp ) {

			double progress = heard;
			bool stalled = !new_track && progress == last_progress && progress > 0.0;

			last_progress = progress;
			state.playing = doc.flag ( doc.find ( root, "is_playing" ) ) && !stalled;
			state.progress = progress;
			state.stamp = now_seconds ( );
			state.shuffle = doc.flag ( doc.find ( root, "shuffle_state" ) );
			state.repeat = base::same_text ( repeat_text, L"context" ) ? 1 : ( base::same_text ( repeat_text, L"track" ) ? 2 : 0 );
			state.volume = static_cast < int32_t > ( doc.number ( doc.find ( device, "volume_percent" ), -1.0 ) );

		}

		state.has_track = item >= 0 && id [ 0 ];
		state.duration = doc.number ( doc.find ( item, "duration_ms" ) ) / 1000.0;
		state.track = library::hash ( id, false );
		state.art = image_url [ 0 ] ? library::hash ( image_url, false ) : 0;
		copy_text ( state.title, 256, doc.text ( doc.find ( item, "name" ) ) );
		copy_text ( state.uri, 64, id );
		copy_text ( state.art_url, 512, image_url );
		copy_text ( state.artist, 256, artists.text ( ) );
		copy_text ( state.album, 256, doc.text ( doc.find ( album >= 0 ? album : show, "name" ) ) );
		copy_text ( state.device, 128, doc.text ( doc.find ( device, "name" ) ) );
		publish ( );

		if ( new_track && id [ 0 ] ) {

			copy_text ( track_id, 64, id );
			remember_album ( id, doc.text ( doc.find ( album, "uri" ) ) );

		}

		if ( new_art ) {

			copy_text ( art_url, 512, image_url );
			fetch_art ( image_url, library::hash ( image_url, false ) );

		}

	}

	static void run_search ( const wchar_t* query, bool fresh ) {

		if ( fresh ) {

			bool empty = !query || !query [ 0 ];

			AcquireSRWLockExclusive ( &lock );
			found.clear ( );
			found_albums.clear ( );
			found_artists.clear ( );
			found_query.set ( query );
			found_generation++;
			found_more = !empty;
			found_loading = !empty;
			publish ( );

			if ( empty )
				return;

		} else {

			AcquireSRWLockExclusive ( &lock );

			bool idle = found_more && !found_loading && found.count ( ) < 300;

			found_loading = idle;

			if ( idle )
				publish ( );
			else
				ReleaseSRWLockExclusive ( &lock );

			if ( !idle )
				return;

		}

		base::list < char > encoded;
		net::response reply;
		json::document doc;
		uint32_t generation = 0;
		int32_t offset = 0;

		AcquireSRWLockExclusive ( &lock );
		net::encode ( encoded, found_query.get ( ) );
		generation = found_generation;
		offset = found.count ( );
		ReleaseSRWLockExclusive ( &lock );
		encoded.push ( 0 );

		bool fetched = false;

		for ( int32_t attempt = 0; attempt < 2 && !fetched; attempt++ ) {

			base::writer path;

			path.add ( offset == 0 ? L"/search?type=track,album,artist&limit=" : L"/search?type=track&limit=" ).number ( page_size ).add ( L"&offset=" ).number ( offset ).add ( L"&q=" ).add ( encoded.data ( ) );

			if ( !api ( L"GET", path.text ( ), nullptr, reply ) )
				break;

			if ( reply.status == 400 && page_size > 10 ) {

				page_size = 10;

				continue;

			}

			fetched = reply.status == 200 && doc.parse ( reply.body.data ( ), reply.body.count ( ) );

		}

		base::list < result > fresh_items;
		base::list < result > fresh_albums;
		base::list < result > fresh_artists;
		bool more = false;

		if ( fetched ) {

			int32_t tracks = doc.find ( doc.root ( ), "tracks" );
			int32_t items = doc.find ( tracks, "items" );

			more = doc.type ( doc.find ( tracks, "next" ) ) == json::kind::TEXT;

			for ( int32_t item = doc.first ( items ); item >= 0; item = doc.next ( item ) ) {

				result entry;
				base::writer artists;
				int32_t album = doc.find ( item, "album" );
				int32_t chosen = -1;
				double chosen_width = 0.0;

				for ( int32_t artist = doc.first ( doc.find ( item, "artists" ) ); artist >= 0; artist = doc.next ( artist ) ) {

					if ( artists.size ( ) )
						artists.add ( L", " );

					artists.add ( doc.text ( doc.find ( artist, "name" ) ) );

				}

				for ( int32_t image = doc.first ( doc.find ( album, "images" ) ); image >= 0; image = doc.next ( image ) ) {

					double width = doc.number ( doc.find ( image, "width" ), 0.0 );

					if ( chosen < 0 || ( width >= 160.0 && ( chosen_width < 160.0 || width < chosen_width ) ) ) {

						chosen = image;
						chosen_width = width;

					}

				}

				copy_text ( entry.title, 160, doc.text ( doc.find ( item, "name" ) ) );
				copy_text ( entry.artist, 160, artists.text ( ) );
				copy_text ( entry.album, 160, doc.text ( doc.find ( album, "name" ) ) );
				copy_text ( entry.uri, 64, doc.text ( doc.find ( item, "uri" ) ) );
				copy_text ( entry.art_url, 256, doc.text ( doc.find ( chosen, "url" ) ) );
				remember_album ( entry.uri, doc.text ( doc.find ( album, "uri" ) ) );
				entry.duration = doc.number ( doc.find ( item, "duration_ms" ) ) / 1000.0;
				entry.art = entry.art_url [ 0 ] ? library::hash ( entry.art_url, false ) : 0;

				if ( entry.uri [ 0 ] )
					fresh_items.push ( entry );

			}

			for ( int32_t group = 0; group < 2 && offset == 0; group++ ) {

				int32_t items_at = doc.path ( doc.root ( ), group == 0 ? "albums" : "artists", "items" );

				for ( int32_t item = doc.first ( items_at ); item >= 0; item = doc.next ( item ) ) {

					result entry;
					int32_t chosen = -1;
					double chosen_width = 0.0;

					for ( int32_t image = doc.first ( doc.find ( item, "images" ) ); image >= 0; image = doc.next ( image ) ) {

						double width = doc.number ( doc.find ( image, "width" ), 0.0 );

						if ( chosen < 0 || ( width >= 160.0 && ( chosen_width < 160.0 || width < chosen_width ) ) ) {

							chosen = image;
							chosen_width = width;

						}

					}

					copy_text ( entry.title, 160, doc.text ( doc.find ( item, "name" ) ) );
					copy_text ( entry.artist, 160, group == 0 ? doc.text ( doc.find ( doc.first ( doc.find ( item, "artists" ) ), "name" ) ) : L"" );
					copy_text ( entry.album, 160, group == 0 ? doc.text ( doc.find ( item, "release_date" ) ) : L"" );
					copy_text ( entry.uri, 64, doc.text ( doc.find ( item, "uri" ) ) );
					copy_text ( entry.art_url, 256, doc.text ( doc.find ( chosen, "url" ) ) );
					entry.art = entry.art_url [ 0 ] ? library::hash ( entry.art_url, false ) : 0;

					if ( entry.uri [ 0 ] )
						( group == 0 ? fresh_albums : fresh_artists ).push ( entry );

				}

			}

		}

		AcquireSRWLockExclusive ( &lock );

		if ( generation == found_generation ) {

			if ( offset == 0 ) {

				found_albums = fresh_albums;
				found_artists = fresh_artists;

			}

			for ( const result& entry : fresh_items ) {

				bool known = false;

				for ( const result& other : found )
					known = known || base::same_text ( other.uri, entry.uri );

				if ( !known && found.count ( ) < 300 )
					found.push ( entry );

			}

			found_more = fetched && more && found.count ( ) < 300;
			found_loading = false;

		}

		publish ( );

	}

	int32_t pick_image ( const json::document& doc, int32_t images, double wanted ) {

		int32_t chosen = -1;
		double chosen_width = 0.0;

		for ( int32_t image = doc.first ( images ); image >= 0; image = doc.next ( image ) ) {

			double width = doc.number ( doc.find ( image, "width" ), 0.0 );

			if ( chosen < 0 || ( width >= wanted && ( chosen_width < wanted || width < chosen_width ) ) || ( chosen_width < wanted && width > chosen_width ) ) {

				chosen = image;
				chosen_width = width;

			}

		}

		return chosen;

	}

	void join_names ( const json::document& doc, int32_t list, base::writer& out ) {

		for ( int32_t item = doc.first ( list ); item >= 0; item = doc.next ( item ) ) {

			if ( out.size ( ) )
				out.add ( L", " );

			out.add ( doc.text ( doc.find ( item, "name" ) ) );

		}

	}

	static bool musical ( const wchar_t* text ) {

		const wchar_t* words [ 16 ] = { L"rapper", L"singer", L"musician", L"band", L"producer", L"DJ", L"songwriter", L"group", L"duo", L"composer", L"vocalist", L"hip hop", L"rock", L"music", L"artist", L"record" };

		for ( const wchar_t* word : words ) {

			int32_t length = base::text_length ( word );

			if ( FindNLSStringEx ( LOCALE_NAME_INVARIANT, FIND_FROMSTART | LINGUISTIC_IGNORECASE, text, -1, word, length, nullptr, nullptr, nullptr, 0 ) >= 0 )
				return true;

		}

		return false;

	}

	static void wiki_summary ( const wchar_t* name, wchar_t* out, int32_t room ) {

		const wchar_t* suffixes [ 6 ] = { L"", L" (musician)", L" (rapper)", L" (singer)", L" (band)", L" (DJ)" };

		out [ 0 ] = 0;

		for ( const wchar_t* suffix : suffixes ) {

			base::writer title;
			base::list < char > encoded;
			base::writer url;
			net::response reply;
			json::document doc;

			title.add ( name ).add ( suffix );

			wchar_t underscored [ 300 ];
			int32_t length = 0;

			for ( ; title.text ( ) [ length ] && length < 299; length++ )
				underscored [ length ] = title.text ( ) [ length ] == L' ' ? L'_' : title.text ( ) [ length ];

			underscored [ length ] = 0;
			net::encode ( encoded, underscored );
			encoded.push ( 0 );
			url.add ( L"https://en.wikipedia.org/api/rest_v1/page/summary/" ).add ( encoded.data ( ) );

			if ( !net::request ( L"GET", url.text ( ), L"Accept: application/json\r\n", nullptr, 0, reply ) || reply.status != 200 || !doc.parse ( reply.body.data ( ), reply.body.count ( ) ) )
				continue;

			int32_t root = doc.root ( );

			if ( base::same_text ( doc.text ( doc.find ( root, "type" ) ), L"disambiguation" ) )
				continue;

			if ( !musical ( doc.text ( doc.find ( root, "description" ) ) ) && !musical ( doc.text ( doc.find ( root, "extract" ) ) ) )
				continue;

			copy_text ( out, room, doc.text ( doc.find ( root, "extract" ) ) );

			return;

		}

	}

	static void run_detail ( int32_t kind, const wchar_t* uri ) {

		const wchar_t* id = uri;

		for ( const wchar_t* at = uri; *at; at++ ) {

			if ( *at == L':' )
				id = at + 1;

		}

		AcquireSRWLockExclusive ( &lock );
		detail_data.kind = kind;
		detail_data.loading = true;
		detail_data.tracks.clear ( );
		detail_data.albums.clear ( );
		detail_data.title [ 0 ] = 0;
		detail_data.artist [ 0 ] = 0;
		detail_data.date [ 0 ] = 0;
		detail_data.type [ 0 ] = 0;
		detail_data.art_url [ 0 ] = 0;
		detail_data.copyright [ 0 ] = 0;
		detail_data.genres [ 0 ] = 0;
		detail_data.bio [ 0 ] = 0;
		detail_data.total = 0;
		copy_text ( detail_data.uri, 64, uri );
		detail_data.generation++;
		publish ( );

		detail fresh;
		base::writer path;
		net::response reply;
		json::document doc;

		fresh.kind = kind;
		copy_text ( fresh.uri, 64, uri );

		if ( kind == 1 ) {

			path.add ( L"/albums/" ).add ( id );

			if ( fetch_json ( path.text ( ), doc ) ) {

				int32_t root = doc.root ( );
				base::writer artists;
				int32_t big = pick_image ( doc, doc.find ( root, "images" ), 600.0 );
				int32_t little = pick_image ( doc, doc.find ( root, "images" ), 160.0 );
				wchar_t small_url [ 256 ];

				join_names ( doc, doc.find ( root, "artists" ), artists );
				copy_text ( fresh.title, 200, doc.text ( doc.find ( root, "name" ) ) );
				copy_text ( fresh.artist, 200, artists.text ( ) );
				copy_text ( fresh.date, 32, doc.text ( doc.find ( root, "release_date" ) ) );
				copy_text ( fresh.type, 32, doc.text ( doc.find ( root, "album_type" ) ) );
				copy_text ( fresh.art_url, 256, doc.text ( doc.find ( big, "url" ) ) );
				copy_text ( fresh.copyright, 240, doc.text ( doc.find ( doc.first ( doc.find ( root, "copyrights" ) ), "text" ) ) );
				copy_text ( small_url, 256, doc.text ( doc.find ( little, "url" ) ) );
				fresh.total = static_cast < int32_t > ( doc.number ( doc.find ( root, "total_tracks" ) ) );

				json::document page;
				bool first = true;

				for ( int32_t round = 0; round < 4; round++ ) {

					const json::document& source = first ? doc : page;
					int32_t tracks = first ? doc.path ( root, "tracks", "items" ) : page.find ( page.root ( ), "items" );

					for ( int32_t item = source.first ( tracks ); item >= 0; item = source.next ( item ) ) {

						result& entry = fresh.tracks.emplace ( );
						base::writer names;

						join_names ( source, source.find ( item, "artists" ), names );
						copy_text ( entry.title, 160, source.text ( source.find ( item, "name" ) ) );
						copy_text ( entry.artist, 160, names.text ( ) );
						copy_text ( entry.album, 160, fresh.title );
						copy_text ( entry.uri, 64, source.text ( source.find ( item, "uri" ) ) );
						copy_text ( entry.art_url, 256, small_url );
						entry.duration = source.number ( source.find ( item, "duration_ms" ) ) / 1000.0;
						entry.number = static_cast < int32_t > ( source.number ( source.find ( item, "track_number" ) ) );
						entry.art = small_url [ 0 ] ? library::hash ( small_url, false ) : 0;
						remember_album ( entry.uri, uri );

					}

					first = false;

					if ( fresh.tracks.count ( ) >= fresh.total || fresh.tracks.count ( ) >= 160 )
						break;

					base::writer more;

					more.add ( L"/albums/" ).add ( id ).add ( L"/tracks?limit=50&offset=" ).number ( fresh.tracks.count ( ) );

					if ( !fetch_json ( more.text ( ), page ) )
						break;

				}

			}

		} else {

			path.add ( L"/artists/" ).add ( id );

			if ( fetch_json ( path.text ( ), doc ) ) {

				int32_t root = doc.root ( );
				int32_t big = pick_image ( doc, doc.find ( root, "images" ), 600.0 );
				base::writer genres;

				for ( int32_t genre = doc.first ( doc.find ( root, "genres" ) ); genre >= 0 && genres.size ( ) < 160; genre = doc.next ( genre ) ) {

					if ( genres.size ( ) )
						genres.add ( L"  \x00B7  " );

					genres.add ( doc.text ( genre ) );

				}

				copy_text ( fresh.title, 200, doc.text ( doc.find ( root, "name" ) ) );
				copy_text ( fresh.artist, 200, L"Artist" );
				copy_text ( fresh.art_url, 256, doc.text ( doc.find ( big, "url" ) ) );
				copy_text ( fresh.genres, 200, genres.text ( ) );

			}

			auto take_album = [ & ] ( const json::document& page, int32_t item ) {

				const wchar_t* uri = page.text ( page.find ( item, "uri" ) );
				const wchar_t* name = page.text ( page.find ( item, "name" ) );
				const wchar_t* date = page.text ( page.find ( item, "release_date" ) );

				for ( const result& known : fresh.albums ) {

					if ( base::same_text ( known.uri, uri ) )
						return;

					if ( base::same_text ( known.album, date ) && CompareStringOrdinal ( known.title, -1, name, -1, TRUE ) == CSTR_EQUAL )
						return;

				}

				result& entry = fresh.albums.emplace ( );
				int32_t little = pick_image ( page, page.find ( item, "images" ), 160.0 );
				const wchar_t* group = page.text ( page.find ( item, "album_type" ) );

				copy_text ( entry.title, 160, page.text ( page.find ( item, "name" ) ) );
				copy_text ( entry.artist, 160, base::same_text ( group, L"single" ) ? L"Single" : ( base::same_text ( group, L"compilation" ) ? L"Compilation" : L"Album" ) );
				copy_text ( entry.album, 160, page.text ( page.find ( item, "release_date" ) ) );
				copy_text ( entry.uri, 64, uri );
				copy_text ( entry.art_url, 256, page.text ( page.find ( little, "url" ) ) );
				entry.art = entry.art_url [ 0 ] ? library::hash ( entry.art_url, false ) : 0;

			};

			auto credited = [ & ] ( const json::document& page, int32_t item ) {

				for ( int32_t artist = page.first ( page.find ( item, "artists" ) ); artist >= 0; artist = page.next ( artist ) ) {

					if ( base::same_text ( page.text ( page.find ( artist, "id" ) ), id ) )
						return true;

				}

				return false;

			};

			for ( int32_t offset = 0; offset < 60; ) {

				base::writer albums;
				json::document page;

				albums.add ( L"/artists/" ).add ( id ).add ( L"/albums?include_groups=album,single&limit=" ).number ( page_size ).add ( L"&offset=" ).number ( offset );

				if ( !fetch_json ( albums.text ( ), page ) )
					break;

				int32_t items = page.find ( page.root ( ), "items" );
				int32_t added = 0;

				for ( int32_t item = page.first ( items ); item >= 0; item = page.next ( item ) ) {

					take_album ( page, item );
					added++;

				}

				offset += added;

				if ( added == 0 || page.type ( page.find ( page.root ( ), "next" ) ) != json::kind::TEXT )
					break;

			}

			if ( fresh.title [ 0 ] ) {

				base::writer query;
				base::list < char > encoded;

				query.add ( L"artist:\"" );

				for ( int32_t i = 0; fresh.title [ i ]; i++ ) {

					if ( fresh.title [ i ] != L'"' )
						query.add ( fresh.title [ i ] );

				}

				query.add ( L"\"" );
				net::encode ( encoded, query.text ( ) );
				encoded.push ( 0 );

				for ( int32_t offset = 0; offset < 30; offset += 10 ) {

					base::writer search;
					json::document page;
					bool wanted_albums = fresh.albums.count ( ) < 30 && ( offset == 0 || fresh.albums.count ( ) >= offset );

					search.add ( offset == 0 ? L"/search?type=track,album&limit=10&offset=0&q=" : L"/search?type=album&limit=10&offset=" );

					if ( offset > 0 )
						search.number ( offset ).add ( L"&q=" );

					search.add ( encoded.data ( ) );

					if ( offset > 0 && !wanted_albums )
						break;

					if ( !fetch_json ( search.text ( ), page ) )
						break;

					int32_t root = page.root ( );

					if ( wanted_albums ) {

						for ( int32_t item = page.first ( page.path ( root, "albums", "items" ) ); item >= 0; item = page.next ( item ) ) {

							if ( credited ( page, item ) )
								take_album ( page, item );

						}

					}

					for ( int32_t item = page.first ( page.path ( root, "tracks", "items" ) ); item >= 0 && fresh.tracks.count ( ) < 10; item = page.next ( item ) ) {

						if ( !credited ( page, item ) )
							continue;

						int32_t album = page.find ( item, "album" );
						int32_t little = pick_image ( page, page.find ( album, "images" ), 160.0 );
						result& entry = fresh.tracks.emplace ( );
						base::writer names;

						join_names ( page, page.find ( item, "artists" ), names );
						copy_text ( entry.title, 160, page.text ( page.find ( item, "name" ) ) );
						copy_text ( entry.artist, 160, names.text ( ) );
						copy_text ( entry.album, 160, page.text ( page.find ( album, "name" ) ) );
						copy_text ( entry.uri, 64, page.text ( page.find ( item, "uri" ) ) );
						copy_text ( entry.art_url, 256, page.text ( page.find ( little, "url" ) ) );
						copy_text ( entry.context, 64, page.text ( page.find ( album, "uri" ) ) );
						entry.duration = page.number ( page.find ( item, "duration_ms" ) ) / 1000.0;
						entry.art = entry.art_url [ 0 ] ? library::hash ( entry.art_url, false ) : 0;
						remember_album ( entry.uri, entry.context );

					}

				}

				int32_t count = fresh.albums.count ( );

				for ( int32_t i = 1; i < count; i++ ) {

					for ( int32_t k = i; k > 0 && base::text_length ( fresh.albums [ static_cast < size_t > ( k ) ].album ) > 0; k-- ) {

						result& a = fresh.albums [ static_cast < size_t > ( k - 1 ) ];
						result& b = fresh.albums [ static_cast < size_t > ( k ) ];
						int32_t order = CompareStringOrdinal ( a.album, -1, b.album, -1, FALSE );

						if ( order != CSTR_LESS_THAN )
							break;

						base::swap ( a, b );

					}

				}

			}

			if ( fresh.title [ 0 ] )
				wiki_summary ( fresh.title, fresh.bio, 1800 );

		}

		AcquireSRWLockExclusive ( &lock );

		if ( base::same_text ( detail_data.uri, uri ) ) {

			uint32_t generation = detail_data.generation;

			detail_data = fresh;
			detail_data.loading = false;
			detail_data.generation = generation + 1;

		}

		publish ( );

	}

	static DWORD __stdcall art_worker ( void* ) {

		CoInitializeEx ( nullptr, COINIT_MULTITHREADED );

		while ( true ) {

			WaitForSingleObject ( art_wake, INFINITE );

			wchar_t url [ 256 ];
			uint64_t key = 0;

			AcquireSRWLockExclusive ( &lock );

			if ( !art_queue.empty ( ) ) {

				copy_text ( url, 256, art_queue.back ( ).get ( ) );
				key = art_queue_keys.back ( );
				art_queue.pop ( );
				art_queue_keys.pop ( );
				art_flight.push ( key );

			}

			ReleaseSRWLockExclusive ( &lock );

			if ( !key )
				continue;

			fetch_art ( url, key );
			AcquireSRWLockExclusive ( &lock );

			for ( int32_t i = 0; i < art_flight.count ( ); i++ ) {

				if ( art_flight [ static_cast < size_t > ( i ) ] == key ) {

					art_flight.remove_at ( static_cast < size_t > ( i ) );

					break;

				}

			}

			ReleaseSRWLockExclusive ( &lock );

		}

		return 0;

	}
	static void simple ( const wchar_t* method, const wchar_t* path, const char* body = nullptr ) {

		net::response reply;

		if ( api ( method, path, body, reply ) && reply.status == 403 )
			say ( L"Playback control needs Spotify Premium." );

		poll_at = now_seconds ( ) + 0.35;

	}

	static void execute ( const command& item ) {

		base::writer path;

		switch ( item.kind ) {

			case order::CLIENT:
				AcquireSRWLockExclusive ( &token_lock );
				client_id.set ( item.text.get ( ) );
				refresh_token.set ( L"" );
				access_token.set ( L"" );
				save_secrets ( );
				ReleaseSRWLockExclusive ( &token_lock );
				publish_flags ( );
				say ( L"" );
				break;

			case order::CONNECT:
				run_login ( );
				break;

			case order::DISCONNECT:
				AcquireSRWLockExclusive ( &token_lock );
				refresh_token.set ( L"" );
				access_token.set ( L"" );
				save_secrets ( );
				ReleaseSRWLockExclusive ( &token_lock );
				AcquireSRWLockExclusive ( &lock );
				state.active = false;
				state.playing = false;
				state.has_track = false;
				state.account [ 0 ] = 0;
				ReleaseSRWLockExclusive ( &lock );
				publish_flags ( );
				break;

			case order::OPEN:
				open_item ( item.text.get ( ) );
				break;

			case order::PLAY:
				AcquireSRWLockExclusive ( &lock );
				state.active = true;
				ReleaseSRWLockExclusive ( &lock );

				start_playback ( nullptr );
				poll_at = now_seconds ( ) + 0.35;
				break;

			case order::PAUSE:
				simple ( L"PUT", L"/me/player/pause" );
				break;

			case order::NEXT:
				simple ( L"POST", L"/me/player/next" );
				break;

			case order::PREVIOUS:
				simple ( L"POST", L"/me/player/previous" );
				break;

			case order::SEEK:
				path.add ( L"/me/player/seek?position_ms=" ).number ( static_cast < int64_t > ( item.number * 1000.0 ) );
				simple ( L"PUT", path.text ( ) );
				break;

			case order::VOLUME:
				path.add ( L"/me/player/volume?volume_percent=" ).number ( static_cast < int64_t > ( item.number * 100.0 + 0.5 ) );
				simple ( L"PUT", path.text ( ) );
				break;

			case order::SHUFFLE:
				path.add ( L"/me/player/shuffle?state=" ).add ( item.number > 0.5 ? L"true" : L"false" );
				simple ( L"PUT", path.text ( ) );
				break;

			case order::REPEAT:
				path.add ( L"/me/player/repeat?state=" ).add ( item.number > 1.5 ? L"track" : ( item.number > 0.5 ? L"context" : L"off" ) );
				simple ( L"PUT", path.text ( ) );
				break;

			case order::SEARCH:
				run_search ( item.text.get ( ), true );
				break;

			case order::MORE:
				run_search ( nullptr, false );
				break;

			case order::DETAIL:
				run_detail ( static_cast < int32_t > ( item.number ), item.text.get ( ) );
				break;

			case order::TRACKS:
				open_tracks ( item.text.get ( ), static_cast < int32_t > ( item.number ) );
				break;


		}

	}

	static DWORD __stdcall worker ( void* ) {

		CoInitializeEx ( nullptr, COINIT_MULTITHREADED );
		load_secrets ( );
		publish_flags ( );

		if ( !refresh_token.empty ( ) )
			read_account ( );

		while ( true ) {

			snapshot view;

			read ( view );

			double interval = !view.connected ? 1e9 : ( view.playing ? 1.0 : ( view.active ? 2.5 : 6.0 ) );
			double wait_for = poll_at > 0.0 ? poll_at - now_seconds ( ) : ( view.connected ? 0.0 : interval );

			if ( wait_for > 0.0 )
				WaitForSingleObject ( wake, wait_for > 1e8 ? INFINITE : static_cast < DWORD > ( wait_for * 1000.0 ) );

			while ( true ) {

				command item;
				bool found = false;

				AcquireSRWLockExclusive ( &lock );

				if ( !commands.empty ( ) ) {

					item = commands [ 0 ];
					commands.remove_at ( 0 );
					found = true;

				}

				ReleaseSRWLockExclusive ( &lock );

				if ( !found )
					break;

				execute ( item );

			}

			read ( view );

			if ( view.connected && ( poll_at <= 0.0 || now_seconds ( ) >= poll_at - 0.02 ) ) {

				poll ( );
				poll_at = now_seconds ( ) + interval;

			}

		}

		return 0;

	}

	void start ( void* window, uint32_t message ) {

		notify_window = static_cast < HWND > ( window );
		notify_message = message;

		if ( thread )
			return;

		wake = CreateEventW ( nullptr, FALSE, FALSE, nullptr );
		thread = CreateThread ( nullptr, 0, &worker, nullptr, 0, nullptr );
		start_taste ( );

	}

	void search ( const wchar_t* query ) {

		push ( order::SEARCH, 0.0, query );

	}

	void search_more ( ) {

		AcquireSRWLockExclusive ( &lock );

		bool wanted = found_more && !found_loading;

		ReleaseSRWLockExclusive ( &lock );

		if ( wanted )
			push ( order::MORE );

	}

	page results ( ) {

		page value;

		AcquireSRWLockExclusive ( &lock );
		value.count = found.count ( );
		value.generation = found_generation;
		value.more = found_more;
		value.albums = found_albums.count ( );
		value.artists = found_artists.count ( );
		value.loading = found_loading;
		ReleaseSRWLockExclusive ( &lock );

		return value;

	}

	bool result_at ( int32_t index, result& out ) {

		bool valid = false;

		AcquireSRWLockExclusive ( &lock );

		if ( index >= 0 && index < found.count ( ) ) {

			out = found [ static_cast < size_t > ( index ) ];
			valid = true;

		}

		ReleaseSRWLockExclusive ( &lock );

		return valid;

	}

	bool extra_at ( int32_t group, int32_t index, result& out ) {

		bool valid = false;

		AcquireSRWLockExclusive ( &lock );

		const base::list < result >& source = group == 0 ? found_albums : found_artists;

		if ( index >= 0 && index < source.count ( ) ) {

			out = source [ static_cast < size_t > ( index ) ];
			valid = true;

		}

		ReleaseSRWLockExclusive ( &lock );

		return valid;

	}

	void request_art ( const wchar_t* url, uint64_t key ) {

		if ( !url || !url [ 0 ] || !key )
			return;

		AcquireSRWLockExclusive ( &lock );

		bool known = false;

		for ( uint64_t value : art_queue_keys )
			known = known || value == key;

		for ( uint64_t value : art_flight )
			known = known || value == key;

		if ( !known ) {

			art_queue.emplace ( ).set ( url );
			art_queue_keys.push ( key );

		}

		if ( !art_wake )
			art_wake = CreateSemaphoreW ( nullptr, 0, 1 << 20, nullptr );

		while ( art_wake && art_threads < 6 ) {

			HANDLE handle = CreateThread ( nullptr, 0, &art_worker, nullptr, 0, nullptr );

			if ( !handle )
				break;

			CloseHandle ( handle );
			art_threads++;

		}

		ReleaseSRWLockExclusive ( &lock );

		if ( !known )
			ReleaseSemaphore ( art_wake, 1, nullptr );

	}

	void open_detail ( int32_t kind, const wchar_t* uri ) {

		push ( order::DETAIL, static_cast < double > ( kind ), uri );

	}

	uint32_t detail_revision ( ) {

		AcquireSRWLockExclusive ( &lock );

		uint32_t value = detail_data.generation;

		ReleaseSRWLockExclusive ( &lock );

		return value;

	}

	void read_detail ( detail& out ) {

		AcquireSRWLockExclusive ( &lock );
		out = detail_data;
		ReleaseSRWLockExclusive ( &lock );

	}

	void play_tracks ( const wchar_t* uris, int32_t offset ) {

		push ( order::TRACKS, static_cast < double > ( offset ), uris );

	}
	void read ( snapshot& out ) {

		AcquireSRWLockExclusive ( &lock );
		out = state;
		ReleaseSRWLockExclusive ( &lock );

	}

	double position ( const snapshot& source ) {

		double result = source.progress;

		if ( source.playing )
			result += now_seconds ( ) - source.stamp;

		if ( source.duration > 0.0 && result > source.duration )
			result = source.duration;

		return result < 0.0 ? 0.0 : result;

	}

	void set_client ( const wchar_t* client ) {

		push ( order::CLIENT, 0.0, client );

	}

	void connect ( ) {

		push ( order::CONNECT );

	}

	void cancel ( ) {

		InterlockedExchange ( &cancelling, 1 );

	}

	void disconnect ( ) {

		push ( order::DISCONNECT );

	}

	void open_link ( const wchar_t* text ) {

		push ( order::OPEN, 0.0, text );

	}

	void play ( bool value ) {

		AcquireSRWLockExclusive ( &lock );
		state.progress = position ( state );
		state.stamp = now_seconds ( );
		state.playing = value;
		publish ( );
		push ( value ? order::PLAY : order::PAUSE );

	}

	void next ( ) {

		push ( order::NEXT );

	}

	void previous ( ) {

		push ( order::PREVIOUS );

	}

	void seek ( double seconds ) {

		AcquireSRWLockExclusive ( &lock );
		state.progress = seconds;
		state.stamp = now_seconds ( );
		ReleaseSRWLockExclusive ( &lock );
		push ( order::SEEK, seconds );

	}

	void volume ( float value ) {

		push ( order::VOLUME, static_cast < double > ( value ) );

	}

	void shuffle ( bool value ) {

		AcquireSRWLockExclusive ( &lock );
		state.shuffle = value;
		ReleaseSRWLockExclusive ( &lock );
		push ( order::SHUFFLE, value ? 1.0 : 0.0 );

	}

	void repeat ( int32_t mode ) {

		AcquireSRWLockExclusive ( &lock );
		state.repeat = mode;
		ReleaseSRWLockExclusive ( &lock );
		push ( order::REPEAT, static_cast < double > ( mode ) );

	}

	bool take_art ( library::art& result ) {

		AcquireSRWLockExclusive ( &lock );

		library::art* ready = nullptr;

		if ( !art_ready.empty ( ) ) {

			ready = art_ready [ 0 ];
			art_ready.remove_at ( 0 );

		}

		ReleaseSRWLockExclusive ( &lock );

		if ( !ready )
			return false;

		result.key = ready->key;
		result.pixels = base::move ( ready->pixels );
		result.thumb = base::move ( ready->thumb );
		result.found = ready->found;
		result.accent = ready->accent;

		for ( int32_t c = 0; c < 4; c++ )
			result.palette [ c ] = ready->palette [ c ];

		delete ready;

		return true;

	}

}