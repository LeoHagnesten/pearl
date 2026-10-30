#pragma once

#include "base/base.hh"
#include "render/render.hh"

namespace library {

	struct entry {

		base::text path;
		base::text title;
		base::text artist;
		base::text album_artist;
		base::text album;
		base::text sort_artist;
		int32_t number = 0;
		int32_t year = 0;
		float duration = 0.0f;
		uint64_t stamp = 0;
		uint64_t size = 0;

	};

	int32_t compare_text ( const wchar_t* a, const wchar_t* b );

	void scan_folders ( const base::list < base::text >& roots, base::list < entry >& result );

	bool extract_art ( const wchar_t* path, base::list < uint8_t >& pixels, render::color* palette, render::color& accent );

	bool art_from_memory ( const uint8_t* data, size_t size, base::list < uint8_t >& pixels, render::color* palette, render::color& accent );

}