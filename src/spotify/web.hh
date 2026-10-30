#pragma once

#include "base/base.hh"
#include "base/json.hh"
#include "net/http.hh"

namespace spotify {

	bool api ( const wchar_t* method, const wchar_t* path, const char* body, net::response& reply );

	int32_t pick_image ( const json::document& doc, int32_t images, double wanted );

	void join_names ( const json::document& doc, int32_t list, base::writer& out );

	void remember_album ( const wchar_t* track_uri, const wchar_t* album_uri );

	void note ( const char* text );

	void notify ( );

	void observe ( const json::document& doc, int32_t item, double progress, bool playing );

	void start_taste ( );

	bool fetch_json ( const wchar_t* path, json::document& doc );

}