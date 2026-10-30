#pragma once

#include "base/base.hh"

namespace net {

	struct response {

		int32_t status = 0;
		base::list < char > body;
		wchar_t location [ 1024 ] = { };
		int32_t retry_after = 0;
		uint32_t error = 0;

	};

	bool request ( const wchar_t* method, const wchar_t* url, const wchar_t* headers, const char* body, int32_t body_size, response& result );

	void encode ( base::list < char >& out, const wchar_t* text );

}