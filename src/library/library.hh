#pragma once

#include "audio/synth.hh"
#include "render/render.hh"

namespace library {

	struct track {

		const wchar_t* title = L"";
		const wchar_t* path = L"";
		int32_t album = 0;
		int32_t number = 0;
		float duration = 0.0f;
		synth::song song;
		bool demo = false;
		bool liked = false;
		uint32_t liked_order = 0;
		uint64_t key = 0;

	};

	struct album {

		const wchar_t* title = L"";
		const wchar_t* artist = L"";
		const wchar_t* art_path = L"";
		int32_t year = 0;
		int32_t cover = -1;
		int32_t style = 0;
		render::color palette [ 4 ];
		render::color accent;
		bool painted = false;
		int32_t first = 0;
		int32_t count = 0;
		uint64_t key = 0;

	};

	struct art {

		uint64_t key = 0;
		base::list < uint8_t > pixels;
		base::list < uint8_t > thumb;
		render::color palette [ 4 ];
		render::color accent;
		bool found = false;

	};

	constexpr int32_t art_size = 384;

	constexpr int32_t thumb_size = 96;

	void make_thumb ( art& result );

	constexpr int32_t remote = 0x40000000;

	constexpr int32_t catalog = 0x30000000;

	constexpr int32_t catalog_room = 720;

	constexpr int32_t saved = 0x28000000;

	void build_demo ( );

	bool demo ( );

	void add_folder ( const wchar_t* path );

	int32_t folder_count ( );

	const wchar_t* folder_at ( int32_t index );

	void clear_folders ( );

	void start_scan ( );

	bool scanning ( );

	bool take_scan ( );

	int32_t album_count ( );

	bool is_remote ( int32_t index );

	bool is_catalog ( int32_t index );

	bool is_virtual ( int32_t index );

	void set_catalog ( int32_t slot, const wchar_t* uri, const wchar_t* title, const wchar_t* artist, const wchar_t* album_title, float duration, const wchar_t* art_url );

	bool is_saved ( int32_t index );

	int32_t saved_count ( );

	int32_t find_saved ( uint64_t key );

	int32_t save_remote ( const wchar_t* uri, const wchar_t* title, const wchar_t* artist, const wchar_t* album_title, float duration, const wchar_t* art_url );

	const wchar_t* uri_of ( int32_t index );

	bool liked_key ( uint64_t key );

	void set_remote ( int32_t index, const wchar_t* title, const wchar_t* artist, const wchar_t* album_title, float duration, uint64_t track_key, uint64_t art_key );

	album& album_at ( int32_t index );

	int32_t track_count ( );

	track& track_at ( int32_t index );

	int32_t find_track ( uint64_t key );

	int32_t find_album ( uint64_t key );

	int32_t generated_count ( );

	int32_t liked_cover ( );

	void covers ( render::cover_spec* result );

	void liked ( base::list < int32_t >& result );

	void set_liked ( int32_t index, bool value );

	void liked_keys ( base::list < uint64_t >& result );

	void restore_liked ( const base::list < uint64_t >& keys );

	float album_seconds ( int32_t index );

	void request_art ( uint64_t key, const wchar_t* path );

	bool take_art ( art& result );

	uint64_t hash ( const wchar_t* text, bool fold );

	void placeholder_palette ( uint64_t key, render::color* palette, render::color& accent );

}