#pragma once

#include "base/base.hh"
#include "library/library.hh"

namespace spotify {

	struct snapshot {

		bool started = false;
		bool configured = false;
		bool connected = false;
		bool waiting = false;
		bool active = false;
		bool playing = false;
		bool shuffle = false;
		bool liked = false;
		bool has_track = false;
		int32_t repeat = 0;
		int32_t volume = -1;
		double progress = 0.0;
		double stamp = 0.0;
		double duration = 0.0;
		uint64_t track = 0;
		uint64_t art = 0;
		wchar_t title [ 256 ] = { };
		wchar_t artist [ 256 ] = { };
		wchar_t album [ 256 ] = { };
		wchar_t uri [ 64 ] = { };
		wchar_t art_url [ 512 ] = { };
		wchar_t account [ 128 ] = { };
		wchar_t device [ 128 ] = { };
		wchar_t message [ 256 ] = { };
		uint32_t revision = 0;

	};

	struct result {

		wchar_t title [ 160 ] = { };
		wchar_t artist [ 160 ] = { };
		wchar_t album [ 160 ] = { };
		wchar_t uri [ 64 ] = { };
		wchar_t art_url [ 256 ] = { };
		wchar_t context [ 64 ] = { };
		double duration = 0.0;
		uint64_t art = 0;
		int32_t number = 0;

	};

	struct home {

		uint32_t generation = 0;
		uint32_t finished = 0;
		bool loading = false;
		bool personal = false;
		bool history = true;
		int64_t made = 0;
		wchar_t blurb [ 240 ] = { };
		base::list < result > mix;
		base::list < result > repeat;
		base::list < result > albums;
		base::list < result > artists;
		base::list < result > fresh;

	};

	struct detail {

		int32_t kind = 0;
		uint32_t generation = 0;
		bool loading = false;
		wchar_t uri [ 64 ] = { };
		wchar_t title [ 200 ] = { };
		wchar_t artist [ 200 ] = { };
		wchar_t date [ 32 ] = { };
		wchar_t type [ 32 ] = { };
		wchar_t art_url [ 256 ] = { };
		wchar_t copyright [ 240 ] = { };
		wchar_t genres [ 200 ] = { };
		wchar_t bio [ 1800 ] = { };
		int32_t total = 0;
		base::list < result > tracks;
		base::list < result > albums;

	};

	struct page {

		int32_t count = 0;
		uint32_t generation = 0;
		bool more = false;
		bool loading = false;
		int32_t albums = 0;
		int32_t artists = 0;

	};

	void start ( void* window, uint32_t message );

	void search ( const wchar_t* query );

	void search_more ( );

	page results ( );

	bool result_at ( int32_t index, result& out );

	bool extra_at ( int32_t group, int32_t index, result& out );

	void request_art ( const wchar_t* url, uint64_t key );

	void play_tracks ( const wchar_t* uris, int32_t offset );

	void open_detail ( int32_t kind, const wchar_t* uri );

	uint32_t detail_revision ( );

	void read_detail ( detail& out );

	void read ( snapshot& out );

	double position ( const snapshot& source );

	bool is_link ( const wchar_t* text );

	void set_client ( const wchar_t* client );

	void connect ( );

	void cancel ( );

	void disconnect ( );

	void open_link ( const wchar_t* text );

	void play ( bool value );

	void next ( );

	void previous ( );

	void seek ( double seconds );

	void volume ( float value );

	void shuffle ( bool value );

	void repeat ( int32_t mode );

	bool take_art ( library::art& result );

	void refresh_home ( const wchar_t* liked, bool force );

	void note_like ( const wchar_t* uri );

	uint32_t home_revision ( );

	void read_home ( home& out );

	int64_t wall_seconds ( );

}