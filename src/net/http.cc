#include "net/http.hh"
#include <windows.h>
#include <winhttp.h>

namespace net {

	static HINTERNET session = nullptr;
	static SRWLOCK lock = SRWLOCK_INIT;

	static HINTERNET open_session ( ) {

		AcquireSRWLockExclusive ( &lock );

		if ( !session ) {

			session = WinHttpOpen ( L"Pearl/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );

			if ( !session )
				session = WinHttpOpen ( L"Pearl/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );

			if ( session )
				WinHttpSetTimeouts ( session, 8000, 8000, 10000, 15000 );

		}

		ReleaseSRWLockExclusive ( &lock );

		return session;

	}

	bool request ( const wchar_t* method, const wchar_t* url, const wchar_t* headers, const char* body, int32_t body_size, response& result ) {

		result.status = 0;
		result.body.clear ( );
		result.location [ 0 ] = 0;
		result.retry_after = 0;
		result.error = 0;

		HINTERNET handle = open_session ( );

		if ( !handle )
			return false;

		URL_COMPONENTS parts = { };
		wchar_t host [ 256 ] = { };
		wchar_t path [ 2048 ] = { };

		parts.dwStructSize = sizeof ( parts );
		parts.lpszHostName = host;
		parts.dwHostNameLength = 255;
		parts.lpszUrlPath = path;
		parts.dwUrlPathLength = 1024;

		wchar_t extra [ 1024 ] = { };

		parts.lpszExtraInfo = extra;
		parts.dwExtraInfoLength = 1023;

		if ( !WinHttpCrackUrl ( url, 0, 0, &parts ) ) {

			result.error = GetLastError ( );

			return false;

		}

		int32_t length = base::text_length ( path );

		for ( int32_t i = 0; extra [ i ] && length < 2047; i++ )
			path [ length++ ] = extra [ i ];

		path [ length ] = 0;

		HINTERNET connection = WinHttpConnect ( handle, host, parts.nPort, 0 );

		if ( !connection ) {

			result.error = GetLastError ( );

			return false;

		}

		DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
		HINTERNET call = WinHttpOpenRequest ( connection, method, path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags );
		bool ok = false;

		if ( call ) {

			DWORD header_length = headers ? static_cast < DWORD > ( -1L ) : 0;

			if ( WinHttpSendRequest ( call, headers ? headers : WINHTTP_NO_ADDITIONAL_HEADERS, header_length, const_cast < char* > ( body ), static_cast < DWORD > ( body_size ), static_cast < DWORD > ( body_size ), 0 ) && WinHttpReceiveResponse ( call, nullptr ) ) {

				DWORD status = 0;
				DWORD size = sizeof ( status );

				WinHttpQueryHeaders ( call, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX );
				result.status = static_cast < int32_t > ( status );

				DWORD retry = 0;

				size = sizeof ( retry );

				if ( WinHttpQueryHeaders ( call, WINHTTP_QUERY_RETRY_AFTER | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &retry, &size, WINHTTP_NO_HEADER_INDEX ) )
					result.retry_after = static_cast < int32_t > ( retry );

				DWORD url_size = sizeof ( result.location );

				WinHttpQueryOption ( call, WINHTTP_OPTION_URL, result.location, &url_size );

				while ( true ) {

					DWORD available = 0;

					if ( !WinHttpQueryDataAvailable ( call, &available ) || available == 0 )
						break;

					size_t start = result.body.size ( );
					DWORD read = 0;

					result.body.resize ( start + available );

					if ( !WinHttpReadData ( call, result.body.data ( ) + start, available, &read ) )
						break;

					result.body.resize ( start + read );

					if ( result.body.size ( ) > 8 * 1024 * 1024 )
						break;

				}

				result.body.push ( 0 );
				result.body.resize ( result.body.size ( ) - 1 );
				ok = true;

			} else
				result.error = GetLastError ( );

			WinHttpCloseHandle ( call );

		}

		WinHttpCloseHandle ( connection );

		return ok;

	}

	void encode ( base::list < char >& out, const wchar_t* text ) {

		const char* digits = "0123456789ABCDEF";

		for ( int32_t i = 0; text && text [ i ]; i++ ) {

			char utf8 [ 8 ];
			int32_t count = WideCharToMultiByte ( CP_UTF8, 0, text + i, 1, utf8, 8, nullptr, nullptr );

			if ( text [ i ] >= 0xD800 && text [ i ] <= 0xDBFF && text [ i + 1 ] ) {

				count = WideCharToMultiByte ( CP_UTF8, 0, text + i, 2, utf8, 8, nullptr, nullptr );
				i++;

			}

			for ( int32_t k = 0; k < count; k++ ) {

				uint8_t c = static_cast < uint8_t > ( utf8 [ k ] );
				bool plain = ( c >= 'A' && c <= 'Z' ) || ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '-' || c == '_' || c == '.' || c == '~';

				if ( plain )
					out.push ( static_cast < char > ( c ) );
				else {

					out.push ( '%' );
					out.push ( digits [ c >> 4 ] );
					out.push ( digits [ c & 15 ] );

				}

			}

		}

	}

}