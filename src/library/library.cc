#include "library/library.hh"
#include "library/catalog.hh"
#include "base/math.hh"
#include <windows.h>
#include <objbase.h>

namespace library {

	struct album_seed {

		const wchar_t* title;
		const wchar_t* artist;
		int32_t year;
		int32_t style;
		uint32_t colors [ 4 ];
		uint32_t accent;
		const wchar_t* tracks [ 10 ];

	};

	static const album_seed seeds [ ] = {
		{ L"Glass Garden", L"Halcyon Drift", 2025, 0, { 0xF6B9D3, 0xC6B4FF, 0x8FB4FF, 0xFFD6B0 }, 0xB06BD8, { L"Morning Refraction", L"Petal Static", L"Lumen", L"Slow Bloom", L"Halo Drift", L"Silver Rain", L"Greenhouse Hymn", L"Kaleidoscope Heart", nullptr } },
		{ L"Night Static", L"Vera Lumen", 2024, 5, { 0x0D1330, 0x3FE0C0, 0x7A5CFF, 0xFF6FB5 }, 0x5E5CE6, { L"Signal Lost", L"Neon Breathing", L"Overpass", L"Cathode Lullaby", L"Midnight Index", L"Satellites", L"Static Bloom", nullptr } },
		{ L"Paper Moons", L"The Quiet Arcade", 2023, 1, { 0xF4E7D7, 0xE2C9B0, 0xF7C08A, 0xB45F33 }, 0xC7773E, { L"Paper Moon", L"Ink & Ivory", L"Lanterns", L"Folded Light", L"Origami Sky", L"Tea Ceremony", L"Quiet Hours", L"Moth Parade", L"Last Tram Home", nullptr } },
		{ L"Tidal Hours", L"Oslo Bloom", 2025, 2, { 0x0B3954, 0x087E8B, 0xBFD7EA, 0xFF6B6B }, 0x0A84A8, { L"Low Tide", L"Salt Lines", L"Harbor Lights", L"Undertow", L"Breakwater", L"Sea Glass", nullptr } },
		{ L"Soft Machines", L"Kite & Kernel", 2024, 4, { 0xFF9A8B, 0xFF6A88, 0x8FE3CF, 0xFFE29A }, 0xF2547D, { L"Hello, Circuit", L"Warm Boot", L"Velvet Logic", L"Sugar Cache", L"Daydream Protocol (Extended Sunrise Mix)", L"Pastel Engine", L"Firmware Love", L"Sleep Mode", nullptr } },
		{ L"Afterglow Club", L"Mira Sol", 2022, 3, { 0xFFB347, 0xFF6961, 0x6A4C93, 0x1982C4 }, 0xE8743B, { L"Sunset Strip", L"Golden Hour", L"Disco Ember", L"Vinyl Heart", L"Last Dance", L"Coastline FM", L"Afterglow", nullptr } }
	};

	static constexpr int32_t demo_total = 6;

	class arena {

	public:

		~arena ( ) {

			for ( wchar_t* block : blocks )
				runtime_release ( block );

		}

		const wchar_t* keep ( const wchar_t* text ) {

			int32_t length = base::text_length ( text ) + 1;

			if ( length > block_room )
				length = block_room;

			if ( blocks.empty ( ) || used + length > block_room ) {

				blocks.push ( static_cast < wchar_t* > ( runtime_allocate ( sizeof ( wchar_t ) * block_room ) ) );
				used = 0;

			}

			wchar_t* target = blocks.back ( ) + used;

			for ( int32_t i = 0; i < length - 1; i++ )
				target [ i ] = text [ i ];

			target [ length - 1 ] = 0;
			used += length;

			return target;

		}

	private:

		static constexpr int32_t block_room = 32768;

		base::list < wchar_t* > blocks;
		int32_t used = 0;

	};

	struct store {

		base::list < track > tracks;
		base::list < album > albums;
		arena strings;
		bool demo = false;

	};

	static store* current = new store ( );
	static store* pending = nullptr;
	static SRWLOCK lock = SRWLOCK_INIT;
	static base::list < base::text > folders;
	static base::list < uint64_t > liked_order;
	static volatile long scan_running = 0;
	static base::list < art* > art_done;
	static base::list < uint64_t > art_keys;
	static base::list < base::text > art_paths;
	static HANDLE art_thread = nullptr;
	static HANDLE art_wake = nullptr;
	static track blank_track;
	static album blank_album;
	static track remote_tracks [ 2 ];
	static album remote_albums [ 2 ];
	static wchar_t remote_text [ 2 ] [ 3 ] [ 256 ];
	static track catalog_tracks [ catalog_room ];
	static album catalog_albums [ catalog_room ];
	static wchar_t catalog_text [ catalog_room ] [ 3 ] [ 160 ];
	static wchar_t catalog_urls [ catalog_room ] [ 256 ];
	static wchar_t catalog_uris [ catalog_room ] [ 64 ];

	struct saved_entry {

		track item;
		album group;
		wchar_t title [ 160 ];
		wchar_t artist [ 160 ];
		wchar_t album_title [ 160 ];
		wchar_t uri [ 64 ];
		wchar_t art [ 256 ];

	};

	static base::list < saved_entry* > saved_entries;

	uint64_t hash ( const wchar_t* text, bool fold ) {

		uint64_t value = 0xCBF29CE484222325ull;

		for ( int32_t i = 0; text && text [ i ]; i++ ) {

			wchar_t c = text [ i ];

			if ( fold && c >= L'A' && c <= L'Z' )
				c = static_cast < wchar_t > ( c + 32 );

			value ^= static_cast < uint64_t > ( c );
			value *= 0x100000001B3ull;

		}

		return value ? value : 1;

	}

	static render::color hsv ( float hue, float saturation, float value ) {

		hue = hue - math::floor ( hue );

		float h = hue * 6.0f;
		int32_t sector = static_cast < int32_t > ( h );
		float f = h - static_cast < float > ( sector );
		float p = value * ( 1.0f - saturation );
		float q = value * ( 1.0f - saturation * f );
		float t = value * ( 1.0f - saturation * ( 1.0f - f ) );

		switch ( sector % 6 ) {

			case 0:
				return render::color ( value, t, p );

			case 1:
				return render::color ( q, value, p );

			case 2:
				return render::color ( p, value, t );

			case 3:
				return render::color ( p, q, value );

			case 4:
				return render::color ( t, p, value );

			default:
				return render::color ( value, p, q );

		}

	}

	void placeholder_palette ( uint64_t key, render::color* palette, render::color& accent ) {

		float hue = static_cast < float > ( key % 3600 ) / 3600.0f;

		palette [ 0 ] = hsv ( hue, 0.32f, 0.96f );
		palette [ 1 ] = hsv ( hue + 0.08f, 0.42f, 0.92f );
		palette [ 2 ] = hsv ( hue - 0.09f, 0.38f, 0.9f );
		palette [ 3 ] = hsv ( hue + 0.18f, 0.3f, 0.95f );
		accent = hsv ( hue, 0.62f, 0.62f );

	}

	static void finish_store ( store* target ) {

		for ( track& item : target->tracks )
			item.liked = false;

		for ( int32_t i = 0; i < liked_order.count ( ); i++ ) {

			uint64_t key = liked_order [ static_cast < size_t > ( i ) ];

			for ( track& item : target->tracks ) {

				if ( item.key == key ) {

					item.liked = true;
					item.liked_order = static_cast < uint32_t > ( i + 1 );

				}

			}

		}

	}

	void build_demo ( ) {

		store* target = new store ( );
		base::random random;

		random.seed ( 0x5EA5ull );
		target->demo = true;

		for ( int32_t a = 0; a < demo_total; a++ ) {

			const album_seed& seed = seeds [ a ];
			album& item = target->albums.emplace ( );

			item.title = seed.title;
			item.artist = seed.artist;
			item.year = seed.year;
			item.cover = a;
			item.style = seed.style;
			item.accent = render::rgb ( seed.accent );
			item.painted = true;
			item.key = hash ( seed.title, true );

			for ( int32_t c = 0; c < 4; c++ )
				item.palette [ c ] = render::rgb ( seed.colors [ c ] );

			item.first = target->tracks.count ( );

			for ( int32_t t = 0; t < 10 && seed.tracks [ t ]; t++ ) {

				track& song = target->tracks.emplace ( );
				float seconds = 148.0f + static_cast < float > ( random.next ( ) % 104 );

				song.title = seed.tracks [ t ];
				song.album = a;
				song.number = t + 1;
				song.duration = seconds;
				song.demo = true;
				song.key = hash ( seed.tracks [ t ], true ) ^ item.key;
				synth::compose ( song.song, static_cast < uint32_t > ( a * 131 + t * 17 + 7 ), seconds, a );
				item.count++;

			}

		}

		if ( liked_order.empty ( ) ) {

			int32_t starred [ ] = { 0, 3, 9, 12, 17, 22, 27, 31, 36, 41 };

			for ( int32_t index : starred )
				liked_order.push ( target->tracks [ static_cast < size_t > ( index ) ].key );

		}

		finish_store ( target );
		delete current;
		current = target;

	}

	bool demo ( ) {

		return current->demo;

	}

	void add_folder ( const wchar_t* path ) {

		for ( const base::text& existing : folders ) {

			if ( hash ( existing.get ( ), true ) == hash ( path, true ) )
				return;

		}

		folders.push ( base::text ( path ) );

	}

	int32_t folder_count ( ) {

		return folders.count ( );

	}

	const wchar_t* folder_at ( int32_t index ) {

		return index >= 0 && index < folders.count ( ) ? folders [ static_cast < size_t > ( index ) ].get ( ) : L"";

	}

	void clear_folders ( ) {

		folders.clear ( );

	}

	static bool entry_before ( const entry* a, const entry* b ) {

		int32_t order = compare_text ( a->sort_artist.get ( ), b->sort_artist.get ( ) );

		if ( order != 0 )
			return order < 0;

		if ( a->year != b->year )
			return a->year < b->year;

		order = compare_text ( a->album.get ( ), b->album.get ( ) );

		if ( order != 0 )
			return order < 0;

		if ( a->number != b->number )
			return a->number < b->number;

		return compare_text ( a->title.get ( ), b->title.get ( ) ) < 0;

	}

	static store* assemble ( base::list < entry >& entries ) {

		store* target = new store ( );
		base::list < entry* > order;

		for ( entry& item : entries )
			order.push ( &item );

		base::sort ( order.data ( ), order.count ( ), [ ] ( entry* a, entry* b ) {

			return entry_before ( a, b );

		} );

		uint64_t last_key = 0;

		for ( entry* item : order ) {

			uint64_t album_key = hash ( item->album.get ( ), true ) * 31 + hash ( item->sort_artist.get ( ), true );

			if ( target->albums.empty ( ) || album_key != last_key ) {

				album& group = target->albums.emplace ( );

				group.title = target->strings.keep ( item->album.get ( ) );
				group.artist = target->strings.keep ( item->album_artist.get ( ) );
				group.art_path = target->strings.keep ( item->path.get ( ) );
				group.year = item->year;
				group.key = album_key;
				group.first = target->tracks.count ( );
				placeholder_palette ( album_key, group.palette, group.accent );
				last_key = album_key;

			}

			album& group = target->albums.back ( );
			track& song = target->tracks.emplace ( );

			song.title = target->strings.keep ( item->title.get ( ) );
			song.path = target->strings.keep ( item->path.get ( ) );
			song.album = target->albums.count ( ) - 1;
			song.number = item->number;
			song.duration = item->duration;
			song.key = hash ( item->path.get ( ), true );
			group.count++;

			if ( group.year == 0 )
				group.year = item->year;

		}

		finish_store ( target );

		return target;

	}

	static DWORD __stdcall scan_thread ( void* ) {

		CoInitializeEx ( nullptr, COINIT_MULTITHREADED );

		base::list < base::text > roots;

		AcquireSRWLockExclusive ( &lock );

		for ( const base::text& folder : folders )
			roots.push ( folder );

		ReleaseSRWLockExclusive ( &lock );

		base::list < entry > entries;

		scan_folders ( roots, entries );

		store* built = assemble ( entries );

		AcquireSRWLockExclusive ( &lock );
		delete pending;
		pending = built;
		ReleaseSRWLockExclusive ( &lock );
		InterlockedExchange ( &scan_running, 0 );
		CoUninitialize ( );

		return 0;

	}

	void start_scan ( ) {

		if ( InterlockedCompareExchange ( &scan_running, 1, 0 ) != 0 )
			return;

		HANDLE worker = CreateThread ( nullptr, 0, &scan_thread, nullptr, 0, nullptr );

		if ( worker )
			CloseHandle ( worker );
		else
			InterlockedExchange ( &scan_running, 0 );

	}

	bool scanning ( ) {

		return scan_running != 0;

	}

	bool take_scan ( ) {

		AcquireSRWLockExclusive ( &lock );

		store* fresh = pending;

		pending = nullptr;
		ReleaseSRWLockExclusive ( &lock );

		if ( !fresh )
			return false;

		finish_store ( fresh );

		for ( album& item : fresh->albums ) {

			for ( const album& old : current->albums ) {

				if ( old.key == item.key && old.painted ) {

					for ( int32_t c = 0; c < 4; c++ )
						item.palette [ c ] = old.palette [ c ];

					item.accent = old.accent;
					item.painted = true;

				}

			}

		}

		delete current;
		current = fresh;

		return true;

	}

	int32_t album_count ( ) {

		return current->albums.count ( );

	}

	bool is_remote ( int32_t index ) {

		return index == remote || index == remote + 1;

	}

	void set_remote ( int32_t index, const wchar_t* title, const wchar_t* artist, const wchar_t* album_title, float duration, uint64_t track_key, uint64_t art_key ) {

		if ( !is_remote ( index ) )
			return;

		int32_t slot = index - remote;
		const wchar_t* sources [ 3 ] = { title, artist, album_title };

		for ( int32_t field = 0; field < 3; field++ ) {

			int32_t i = 0;

			for ( ; sources [ field ] && sources [ field ] [ i ] && i < 255; i++ )
				remote_text [ slot ] [ field ] [ i ] = sources [ field ] [ i ];

			remote_text [ slot ] [ field ] [ i ] = 0;

		}

		track& item = remote_tracks [ slot ];
		album& group = remote_albums [ slot ];
		const album& other = remote_albums [ 1 - slot ];

		item.title = remote_text [ slot ] [ 0 ];
		item.path = L"";
		item.album = index;
		item.number = 1;
		item.duration = duration;
		item.demo = false;
		item.key = track_key;
		group.title = remote_text [ slot ] [ 2 ];
		group.artist = remote_text [ slot ] [ 1 ];
		group.art_path = L"";
		group.cover = -1;
		group.first = index;
		group.count = 1;

		if ( group.key == art_key )
			return;

		group.key = art_key;
		group.painted = false;

		if ( art_key && other.key == art_key && other.painted ) {

			for ( int32_t c = 0; c < 4; c++ )
				group.palette [ c ] = other.palette [ c ];

			group.accent = other.accent;
			group.painted = true;

			return;

		}

		placeholder_palette ( art_key ? art_key : track_key, group.palette, group.accent );

	}

	bool is_catalog ( int32_t index ) {

		return index >= catalog && index < catalog + catalog_room;

	}

	bool is_virtual ( int32_t index ) {

		return is_remote ( index ) || is_catalog ( index ) || ( index >= saved && index < saved + saved_entries.count ( ) );

	}

	void set_catalog ( int32_t slot, const wchar_t* uri, const wchar_t* title, const wchar_t* artist, const wchar_t* album_title, float duration, const wchar_t* art_url ) {

		if ( slot < 0 || slot >= catalog_room )
			return;

		uint64_t track_key = hash ( uri, false );
		uint64_t art_key = art_url && art_url [ 0 ] ? hash ( art_url, false ) : 0;
		int32_t uri_length = 0;

		for ( ; uri && uri [ uri_length ] && uri_length < 63; uri_length++ )
			catalog_uris [ slot ] [ uri_length ] = uri [ uri_length ];

		catalog_uris [ slot ] [ uri_length ] = 0;

		const wchar_t* sources [ 3 ] = { title, artist, album_title };

		for ( int32_t field = 0; field < 3; field++ ) {

			int32_t i = 0;

			for ( ; sources [ field ] && sources [ field ] [ i ] && i < 159; i++ )
				catalog_text [ slot ] [ field ] [ i ] = sources [ field ] [ i ];

			catalog_text [ slot ] [ field ] [ i ] = 0;

		}

		int32_t length = 0;

		for ( ; art_url && art_url [ length ] && length < 255; length++ )
			catalog_urls [ slot ] [ length ] = art_url [ length ];

		catalog_urls [ slot ] [ length ] = 0;

		track& item = catalog_tracks [ slot ];
		album& group = catalog_albums [ slot ];

		item.title = catalog_text [ slot ] [ 0 ];
		item.path = L"";
		item.album = catalog + slot;
		item.number = slot + 1;
		item.duration = duration;
		item.demo = false;
		item.liked = false;
		item.key = track_key;
		group.title = catalog_text [ slot ] [ 2 ];
		group.artist = catalog_text [ slot ] [ 1 ];
		group.art_path = catalog_urls [ slot ];
		group.cover = -1;
		group.first = catalog + slot;
		group.count = 1;
		group.key = art_key;
		group.painted = false;
		placeholder_palette ( art_key ? art_key : track_key, group.palette, group.accent );

	}

	album& album_at ( int32_t index ) {

		if ( is_remote ( index ) )
			return remote_albums [ index - remote ];

		if ( is_catalog ( index ) )
			return catalog_albums [ index - catalog ];

		if ( index >= saved && index < saved + saved_entries.count ( ) )
			return saved_entries [ static_cast < size_t > ( index - saved ) ]->group;
		if ( index < 0 || index >= current->albums.count ( ) )
			return blank_album;

		return current->albums [ static_cast < size_t > ( index ) ];

	}

	int32_t track_count ( ) {

		return current->tracks.count ( );

	}

	track& track_at ( int32_t index ) {

		if ( is_remote ( index ) )
			return remote_tracks [ index - remote ];

		if ( is_catalog ( index ) )
			return catalog_tracks [ index - catalog ];

		if ( index >= saved && index < saved + saved_entries.count ( ) )
			return saved_entries [ static_cast < size_t > ( index - saved ) ]->item;

		if ( index < 0 || index >= current->tracks.count ( ) )
			return blank_track;

		return current->tracks [ static_cast < size_t > ( index ) ];

	}

	int32_t find_track ( uint64_t key ) {

		for ( int32_t i = 0; i < current->tracks.count ( ); i++ ) {

			if ( current->tracks [ static_cast < size_t > ( i ) ].key == key )
				return i;

		}

		return -1;

	}

	int32_t find_album ( uint64_t key ) {

		for ( int32_t i = 0; i < current->albums.count ( ); i++ ) {

			if ( current->albums [ static_cast < size_t > ( i ) ].key == key )
				return i;

		}

		return -1;

	}

	int32_t generated_count ( ) {

		return current->demo ? demo_total + 1 : 1;

	}

	int32_t liked_cover ( ) {

		return current->demo ? demo_total : 0;

	}

	void covers ( render::cover_spec* result ) {

		int32_t at = 0;

		if ( current->demo ) {

			for ( int32_t a = 0; a < demo_total; a++ ) {

				render::cover_spec& spec = result [ at++ ];

				for ( int32_t c = 0; c < 4; c++ )
					spec.colors [ c ] = render::rgb ( seeds [ a ].colors [ c ] );

				spec.seed = static_cast < float > ( a ) * 1.37f + 0.42f;
				spec.style = seeds [ a ].style;
				spec.grain = 0.045f;
				spec.vignette = 0.55f;

			}

		}

		render::cover_spec& liked_spec = result [ at ];

		liked_spec.colors [ 0 ] = render::rgb ( 0xFFC2D6 );
		liked_spec.colors [ 1 ] = render::rgb ( 0xFF5E8A );
		liked_spec.colors [ 2 ] = render::rgb ( 0xB79CFF );
		liked_spec.colors [ 3 ] = render::rgb ( 0xFFD9C2 );
		liked_spec.seed = 9.1f;
		liked_spec.style = 4;
		liked_spec.grain = 0.03f;
		liked_spec.vignette = 0.3f;

	}

	static void mark_saved ( ) {

		for ( saved_entry* entry : saved_entries ) {

			entry->item.liked = false;
			entry->item.liked_order = 0;

			for ( int32_t i = 0; i < liked_order.count ( ); i++ ) {

				if ( liked_order [ static_cast < size_t > ( i ) ] == entry->item.key ) {

					entry->item.liked = true;
					entry->item.liked_order = static_cast < uint32_t > ( i + 1 );

				}

			}

		}

	}

	bool is_saved ( int32_t index ) {

		return index >= saved && index < saved + saved_entries.count ( );

	}

	int32_t saved_count ( ) {

		return saved_entries.count ( );

	}

	int32_t find_saved ( uint64_t key ) {

		for ( int32_t i = 0; i < saved_entries.count ( ); i++ ) {

			if ( saved_entries [ static_cast < size_t > ( i ) ]->item.key == key )
				return saved + i;

		}

		return -1;

	}

	int32_t save_remote ( const wchar_t* uri, const wchar_t* title, const wchar_t* artist, const wchar_t* album_title, float duration, const wchar_t* art_url ) {

		if ( !uri || !uri [ 0 ] )
			return -1;

		uint64_t key = hash ( uri, false );
		int32_t found = find_saved ( key );

		if ( found >= 0 )
			return found;

		saved_entry* entry = new saved_entry ( );
		int32_t index = saved + saved_entries.count ( );
		const wchar_t* sources [ 5 ] = { title, artist, album_title, uri, art_url };
		wchar_t* targets [ 5 ] = { entry->title, entry->artist, entry->album_title, entry->uri, entry->art };
		int32_t rooms [ 5 ] = { 160, 160, 160, 64, 256 };

		for ( int32_t field = 0; field < 5; field++ ) {

			int32_t i = 0;

			for ( ; sources [ field ] && sources [ field ] [ i ] && i < rooms [ field ] - 1; i++ )
				targets [ field ] [ i ] = sources [ field ] [ i ];

			targets [ field ] [ i ] = 0;

		}

		entry->item.title = entry->title;
		entry->item.path = L"";
		entry->item.album = index;
		entry->item.duration = duration;
		entry->item.key = key;
		entry->group.title = entry->album_title;
		entry->group.artist = entry->artist;
		entry->group.art_path = entry->art;
		entry->group.cover = -1;
		entry->group.first = index;
		entry->group.count = 1;
		entry->group.key = entry->art [ 0 ] ? hash ( entry->art, false ) : 0;
		placeholder_palette ( entry->group.key ? entry->group.key : key, entry->group.palette, entry->group.accent );

		for ( int32_t i = 0; i < liked_order.count ( ); i++ ) {

			if ( liked_order [ static_cast < size_t > ( i ) ] == key ) {

				entry->item.liked = true;
				entry->item.liked_order = static_cast < uint32_t > ( i + 1 );

			}

		}

		saved_entries.push ( entry );

		return index;

	}

	const wchar_t* uri_of ( int32_t index ) {

		if ( is_catalog ( index ) )
			return catalog_uris [ index - catalog ];

		if ( is_saved ( index ) )
			return saved_entries [ static_cast < size_t > ( index - saved ) ]->uri;

		return L"";

	}

	bool liked_key ( uint64_t key ) {

		for ( uint64_t value : liked_order ) {

			if ( value == key )
				return true;

		}

		return false;

	}

	void liked ( base::list < int32_t >& result ) {

		result.clear ( );

		for ( int32_t i = 0; i < current->tracks.count ( ); i++ ) {

			if ( current->tracks [ static_cast < size_t > ( i ) ].liked )
				result.push ( i );

		}

		for ( int32_t i = 0; i < saved_entries.count ( ); i++ ) {

			if ( saved_entries [ static_cast < size_t > ( i ) ]->item.liked )
				result.push ( saved + i );

		}

		base::sort ( result.data ( ), result.count ( ), [ ] ( int32_t a, int32_t b ) {

			return track_at ( a ).liked_order > track_at ( b ).liked_order;

		} );

	}

	void set_liked ( int32_t index, bool value ) {

		if ( !is_saved ( index ) && ( index < 0 || index >= current->tracks.count ( ) ) )
			return;

		track& item = track_at ( index );

		for ( int32_t i = 0; i < liked_order.count ( ); i++ ) {

			if ( liked_order [ static_cast < size_t > ( i ) ] == item.key ) {

				liked_order.remove_at ( static_cast < size_t > ( i ) );

				break;

			}

		}

		if ( value )
			liked_order.push ( item.key );

		finish_store ( current );
		mark_saved ( );

	}

	void liked_keys ( base::list < uint64_t >& result ) {

		result = liked_order;

	}

	void restore_liked ( const base::list < uint64_t >& keys ) {

		liked_order = keys;
		finish_store ( current );
		mark_saved ( );

	}

	float album_seconds ( int32_t index ) {

		const album& source = album_at ( index );
		float total = 0.0f;

		for ( int32_t i = 0; i < source.count; i++ )
			total += track_at ( source.first + i ).duration;

		return total;

	}

	void make_thumb ( art& result ) {

		result.thumb.clear ( );

		if ( !result.found || result.pixels.count ( ) < art_size * art_size * 4 )
			return;

		int32_t step = art_size / thumb_size;
		int32_t area = step * step;

		result.thumb.resize ( static_cast < size_t > ( thumb_size * thumb_size * 4 ) );

		for ( int32_t y = 0; y < thumb_size; y++ ) {

			for ( int32_t x = 0; x < thumb_size; x++ ) {

				int32_t sum [ 4 ] = { };

				for ( int32_t dy = 0; dy < step; dy++ ) {

					const uint8_t* row = result.pixels.data ( ) + ( ( y * step + dy ) * art_size + x * step ) * 4;

					for ( int32_t dx = 0; dx < step; dx++ ) {

						for ( int32_t k = 0; k < 4; k++ )
							sum [ k ] += row [ dx * 4 + k ];

					}

				}

				for ( int32_t k = 0; k < 4; k++ )
					result.thumb [ static_cast < size_t > ( ( y * thumb_size + x ) * 4 + k ) ] = static_cast < uint8_t > ( sum [ k ] / area );

			}

		}

	}

	static DWORD __stdcall art_worker ( void* ) {

		CoInitializeEx ( nullptr, COINIT_MULTITHREADED );

		while ( true ) {

			WaitForSingleObject ( art_wake, INFINITE );

			while ( true ) {

				uint64_t key = 0;
				base::text path;

				AcquireSRWLockExclusive ( &lock );

				if ( !art_keys.empty ( ) ) {

					key = art_keys [ 0 ];
					path = art_paths [ 0 ];
					art_keys.remove_at ( 0 );
					art_paths.remove_at ( 0 );

				}

				ReleaseSRWLockExclusive ( &lock );

				if ( !key )
					break;

				art* result = new art ( );

				result->key = key;
				result->found = extract_art ( path.get ( ), result->pixels, result->palette, result->accent );
				make_thumb ( *result );

				AcquireSRWLockExclusive ( &lock );
				art_done.push ( result );
				ReleaseSRWLockExclusive ( &lock );

			}

		}

		return 0;

	}

	void request_art ( uint64_t key, const wchar_t* path ) {

		if ( !path || !path [ 0 ] )
			return;

		if ( !art_thread ) {

			art_wake = CreateEventW ( nullptr, FALSE, FALSE, nullptr );
			art_thread = CreateThread ( nullptr, 0, &art_worker, nullptr, 0, nullptr );

		}

		AcquireSRWLockExclusive ( &lock );

		bool queued = false;

		for ( uint64_t existing : art_keys )
			queued = queued || existing == key;

		if ( !queued ) {

			art_keys.push ( key );
			art_paths.push ( base::text ( path ) );

		}

		ReleaseSRWLockExclusive ( &lock );
		SetEvent ( art_wake );

	}

	bool take_art ( art& result ) {

		AcquireSRWLockExclusive ( &lock );

		art* ready = nullptr;

		if ( !art_done.empty ( ) ) {

			ready = art_done [ 0 ];
			art_done.remove_at ( 0 );

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