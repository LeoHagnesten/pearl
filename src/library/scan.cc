#include "library/catalog.hh"
#include "library/library.hh"
#include "base/files.hh"
#include "base/math.hh"
#include <windows.h>
#include <shobjidl.h>
#include <propsys.h>
#include <wincodec.h>

namespace library {

	static const GUID music_keys = { 0x56A3372E, 0xCE9C, 0x11D2, { 0x9F, 0x0E, 0x00, 0x60, 0x97, 0xC6, 0x86, 0xF6 } };
	static const PROPERTYKEY key_title = { { 0xF29F85E0, 0x4FF9, 0x1068, { 0xAB, 0x91, 0x08, 0x00, 0x2B, 0x27, 0xB3, 0xD9 } }, 2 };
	static const PROPERTYKEY key_artist = { music_keys, 2 };
	static const PROPERTYKEY key_album = { music_keys, 4 };
	static const PROPERTYKEY key_year = { music_keys, 5 };
	static const PROPERTYKEY key_track = { music_keys, 7 };
	static const PROPERTYKEY key_album_artist = { music_keys, 13 };
	static const PROPERTYKEY key_duration = { { 0x64440490, 0x4C8B, 0x11D1, { 0x8B, 0x70, 0x08, 0x00, 0x36, 0xB1, 0x1A, 0x03 } }, 3 };
	static const uint32_t cache_magic = 0x42494C50;
	static const uint32_t cache_version = 1;

	int32_t compare_text ( const wchar_t* a, const wchar_t* b ) {

		int32_t result = CompareStringEx ( LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE | SORT_DIGITSASNUMBERS, a, -1, b, -1, nullptr, nullptr, 0 );

		return result == CSTR_LESS_THAN ? -1 : ( result == CSTR_GREATER_THAN ? 1 : 0 );

	}

	static bool ends_with ( const wchar_t* text, const wchar_t* tail ) {

		int32_t length = base::text_length ( text );
		int32_t tail_length = base::text_length ( tail );

		if ( tail_length > length )
			return false;

		return CompareStringOrdinal ( text + length - tail_length, tail_length, tail, tail_length, TRUE ) == CSTR_EQUAL;

	}

	static bool audio_file ( const wchar_t* name ) {

		const wchar_t* kinds [ ] = { L".mp3", L".m4a", L".aac", L".flac", L".wav", L".wma", L".ogg", L".opus" };

		for ( const wchar_t* kind : kinds ) {

			if ( ends_with ( name, kind ) )
				return true;

		}

		return false;

	}

	static void join ( base::writer& line, const wchar_t* folder, const wchar_t* name ) {

		line.clear ( );
		line.add ( folder );

		int32_t length = line.size ( );

		if ( length > 0 && line.text ( ) [ length - 1 ] != L'\\' )
			line.add ( L'\\' );

		line.add ( name );

	}

	static void file_stem ( const wchar_t* path, base::writer& out ) {

		int32_t length = base::text_length ( path );
		int32_t start = 0;
		int32_t stop = length;

		for ( int32_t i = 0; i < length; i++ ) {

			if ( path [ i ] == L'\\' || path [ i ] == L'/' )
				start = i + 1;

		}

		for ( int32_t i = length - 1; i > start; i-- ) {

			if ( path [ i ] == L'.' ) {

				stop = i;

				break;

			}

		}

		out.clear ( );

		for ( int32_t i = start; i < stop; i++ )
			out.add ( path [ i ] );

	}

	static void parent_name ( const wchar_t* path, base::writer& out ) {

		int32_t length = base::text_length ( path );
		int32_t last = -1;
		int32_t before = -1;

		for ( int32_t i = 0; i < length; i++ ) {

			if ( path [ i ] == L'\\' || path [ i ] == L'/' ) {

				before = last;
				last = i;

			}

		}

		out.clear ( );

		for ( int32_t i = before + 1; i < last; i++ )
			out.add ( path [ i ] );

	}

	static void read_text ( IPropertyStore* store, const PROPERTYKEY& key, base::text& out ) {

		PROPVARIANT value;

		__builtin_memset ( &value, 0, sizeof ( value ) );

		if ( FAILED ( store->GetValue ( key, &value ) ) )
			return;

		if ( value.vt == VT_LPWSTR && value.pwszVal )
			out.set ( value.pwszVal );
		else if ( value.vt == ( VT_VECTOR | VT_LPWSTR ) && value.calpwstr.cElems > 0 ) {

			base::writer line;

			for ( ULONG i = 0; i < value.calpwstr.cElems; i++ ) {

				if ( i )
					line.add ( L", " );

				line.add ( value.calpwstr.pElems [ i ] );

			}

			out.set ( line.text ( ) );

		}

		PropVariantClear ( &value );

	}

	static uint64_t read_number ( IPropertyStore* store, const PROPERTYKEY& key ) {

		PROPVARIANT value;
		uint64_t result = 0;

		__builtin_memset ( &value, 0, sizeof ( value ) );

		if ( FAILED ( store->GetValue ( key, &value ) ) )
			return 0;

		if ( value.vt == VT_UI4 || value.vt == VT_I4 )
			result = value.ulVal;
		else if ( value.vt == VT_UI8 || value.vt == VT_I8 )
			result = value.uhVal.QuadPart;
		else if ( value.vt == VT_UI2 )
			result = value.uiVal;

		PropVariantClear ( &value );

		return result;

	}

	static void describe ( entry& item ) {

		IPropertyStore* store = nullptr;

		if ( SUCCEEDED ( SHGetPropertyStoreFromParsingName ( item.path.get ( ), nullptr, GPS_DEFAULT, __uuidof ( IPropertyStore ), reinterpret_cast < void** > ( &store ) ) ) ) {

			read_text ( store, key_title, item.title );
			read_text ( store, key_artist, item.artist );
			read_text ( store, key_album_artist, item.album_artist );
			read_text ( store, key_album, item.album );
			item.number = static_cast < int32_t > ( read_number ( store, key_track ) );
			item.year = static_cast < int32_t > ( read_number ( store, key_year ) );
			item.duration = static_cast < float > ( static_cast < double > ( read_number ( store, key_duration ) ) / 10000000.0 );
			store->Release ( );

		}

	}

	static void tidy ( entry& item ) {

		base::writer line;

		if ( item.title.empty ( ) ) {

			file_stem ( item.path.get ( ), line );
			item.title.set ( line.text ( ) );

		}

		if ( item.album.empty ( ) ) {

			parent_name ( item.path.get ( ), line );
			item.album.set ( line.size ( ) ? line.text ( ) : L"Unknown album" );

		}

		if ( item.artist.empty ( ) )
			item.artist.set ( item.album_artist.empty ( ) ? L"Unknown artist" : item.album_artist.get ( ) );

		if ( item.album_artist.empty ( ) )
			item.album_artist.set ( item.artist.get ( ) );

		item.sort_artist.set ( item.album_artist.get ( ) );

	}

	static void put_u32 ( base::list < char >& out, uint32_t value ) {

		for ( int32_t i = 0; i < 4; i++ )
			out.push ( static_cast < char > ( value >> ( i * 8 ) ) );

	}

	static void put_u64 ( base::list < char >& out, uint64_t value ) {

		for ( int32_t i = 0; i < 8; i++ )
			out.push ( static_cast < char > ( value >> ( i * 8 ) ) );

	}

	static void put_text ( base::list < char >& out, const base::text& value ) {

		int32_t length = value.size ( );

		put_u32 ( out, static_cast < uint32_t > ( length ) );

		for ( int32_t i = 0; i < length; i++ ) {

			uint16_t c = static_cast < uint16_t > ( value.get ( ) [ i ] );

			out.push ( static_cast < char > ( c & 0xFF ) );
			out.push ( static_cast < char > ( c >> 8 ) );

		}

	}

	struct reader {

		const uint8_t* at;
		const uint8_t* end;
		bool failed = false;

		uint32_t u32 ( ) {

			if ( end - at < 4 ) {

				failed = true;

				return 0;

			}

			uint32_t value = static_cast < uint32_t > ( at [ 0 ] ) | ( static_cast < uint32_t > ( at [ 1 ] ) << 8 ) | ( static_cast < uint32_t > ( at [ 2 ] ) << 16 ) | ( static_cast < uint32_t > ( at [ 3 ] ) << 24 );

			at += 4;

			return value;

		}

		uint64_t u64 ( ) {

			uint64_t low = u32 ( );
			uint64_t high = u32 ( );

			return low | ( high << 32 );

		}

		void text ( base::text& out ) {

			uint32_t length = u32 ( );

			if ( failed || length > 4096 || static_cast < uint32_t > ( end - at ) < length * 2 ) {

				failed = true;

				return;

			}

			base::list < wchar_t > chars;

			for ( uint32_t i = 0; i < length; i++ ) {

				chars.push ( static_cast < wchar_t > ( at [ 0 ] | ( at [ 1 ] << 8 ) ) );
				at += 2;

			}

			chars.push ( 0 );
			out.set ( chars.data ( ) );

		}

	};

	static void cache_path ( wchar_t* path ) {

		base::writer line;

		line.add ( files::folder ( ) ).add ( L"\\library.dat" );

		for ( int32_t i = 0; i <= line.size ( ); i++ )
			path [ i ] = line.text ( ) [ i ];

	}

	static void load_cache ( base::list < entry >& cache ) {

		wchar_t path [ 600 ];
		base::list < char > data;

		cache_path ( path );

		if ( !files::read ( path, data ) || data.count ( ) < 12 )
			return;

		reader in = { reinterpret_cast < const uint8_t* > ( data.data ( ) ), reinterpret_cast < const uint8_t* > ( data.data ( ) ) + data.count ( ) - 1 };

		if ( in.u32 ( ) != cache_magic || in.u32 ( ) != cache_version )
			return;

		uint32_t count = in.u32 ( );

		for ( uint32_t i = 0; i < count && !in.failed; i++ ) {

			entry& item = cache.emplace ( );
			uint32_t duration_bits = 0;

			item.stamp = in.u64 ( );
			item.size = in.u64 ( );
			item.number = static_cast < int32_t > ( in.u32 ( ) );
			item.year = static_cast < int32_t > ( in.u32 ( ) );
			duration_bits = in.u32 ( );
			__builtin_memcpy ( &item.duration, &duration_bits, 4 );
			in.text ( item.path );
			in.text ( item.title );
			in.text ( item.artist );
			in.text ( item.album_artist );
			in.text ( item.album );

		}

		if ( in.failed )
			cache.clear ( );

	}

	static void save_cache ( const base::list < entry >& entries ) {

		base::list < char > out;
		wchar_t path [ 600 ];

		put_u32 ( out, cache_magic );
		put_u32 ( out, cache_version );
		put_u32 ( out, static_cast < uint32_t > ( entries.count ( ) ) );

		for ( const entry& item : entries ) {

			uint32_t duration_bits = 0;

			__builtin_memcpy ( &duration_bits, &item.duration, 4 );
			put_u64 ( out, item.stamp );
			put_u64 ( out, item.size );
			put_u32 ( out, static_cast < uint32_t > ( item.number ) );
			put_u32 ( out, static_cast < uint32_t > ( item.year ) );
			put_u32 ( out, duration_bits );
			put_text ( out, item.path );
			put_text ( out, item.title );
			put_text ( out, item.artist );
			put_text ( out, item.album_artist );
			put_text ( out, item.album );

		}

		cache_path ( path );
		files::write ( path, out.data ( ), out.count ( ) );

	}

	struct walker {

		const base::list < entry >* cache;
		base::list < entry >* result;
		int32_t limit = 20000;

		void visit ( const wchar_t* folder, int32_t depth ) {

			if ( depth > 16 || result->count ( ) >= limit )
				return;

			base::writer pattern;
			WIN32_FIND_DATAW data;

			join ( pattern, folder, L"*" );

			HANDLE find = FindFirstFileExW ( pattern.text ( ), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH );

			if ( find == INVALID_HANDLE_VALUE )
				return;

			do {

				const wchar_t* name = data.cFileName;

				if ( name [ 0 ] == L'.' || ( data.dwFileAttributes & ( FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM ) ) )
					continue;

				base::writer full;

				join ( full, folder, name );

				if ( data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) {

					if ( !( data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT ) )
						visit ( full.text ( ), depth + 1 );

					continue;

				}

				if ( !audio_file ( name ) )
					continue;

				uint64_t stamp = ( static_cast < uint64_t > ( data.ftLastWriteTime.dwHighDateTime ) << 32 ) | data.ftLastWriteTime.dwLowDateTime;
				uint64_t size = ( static_cast < uint64_t > ( data.nFileSizeHigh ) << 32 ) | data.nFileSizeLow;
				entry& item = result->emplace ( );
				bool reused = false;

				for ( const entry& old : *cache ) {

					if ( old.stamp == stamp && old.size == size && CompareStringOrdinal ( old.path.get ( ), -1, full.text ( ), -1, TRUE ) == CSTR_EQUAL ) {

						item = old;
						reused = true;

						break;

					}

				}

				if ( !reused ) {

					item.path.set ( full.text ( ) );
					item.stamp = stamp;
					item.size = size;
					describe ( item );

				}

				tidy ( item );

			} while ( FindNextFileW ( find, &data ) && result->count ( ) < limit );

			FindClose ( find );

		}

	};

	void scan_folders ( const base::list < base::text >& roots, base::list < entry >& result ) {

		base::list < entry > cache;

		load_cache ( cache );

		walker walk = { &cache, &result };

		for ( const base::text& root : roots )
			walk.visit ( root.get ( ), 0 );

		save_cache ( result );

	}

	static IWICBitmapSource* folder_image ( IWICImagingFactory* imaging, const wchar_t* path ) {

		const wchar_t* names [ ] = { L"cover.jpg", L"cover.png", L"folder.jpg", L"folder.png", L"front.jpg", L"front.png", L"album.jpg", L"albumart.jpg" };
		base::writer folder;
		int32_t length = base::text_length ( path );
		int32_t cut = 0;

		for ( int32_t i = 0; i < length; i++ ) {

			if ( path [ i ] == L'\\' || path [ i ] == L'/' )
				cut = i;

		}

		for ( int32_t i = 0; i < cut; i++ )
			folder.add ( path [ i ] );

		for ( const wchar_t* name : names ) {

			base::writer candidate;
			IWICBitmapDecoder* decoder = nullptr;
			IWICBitmapFrameDecode* frame = nullptr;

			join ( candidate, folder.text ( ), name );

			if ( GetFileAttributesW ( candidate.text ( ) ) == INVALID_FILE_ATTRIBUTES )
				continue;

			if ( SUCCEEDED ( imaging->CreateDecoderFromFilename ( candidate.text ( ), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder ) ) && SUCCEEDED ( decoder->GetFrame ( 0, &frame ) ) ) {

				decoder->Release ( );

				return frame;

			}

			if ( decoder )
				decoder->Release ( );

		}

		return nullptr;

	}

	static void build_palette ( const uint8_t* pixels, int32_t size, render::color* palette, render::color& accent ) {

		float best = -1.0f;

		for ( int32_t q = 0; q < 4; q++ ) {

			int32_t x0 = ( q % 2 ) * size / 2;
			int32_t y0 = ( q / 2 ) * size / 2;
			float r = 0.0f;
			float g = 0.0f;
			float b = 0.0f;
			int32_t samples = 0;

			for ( int32_t y = y0; y < y0 + size / 2; y += 6 ) {

				for ( int32_t x = x0; x < x0 + size / 2; x += 6 ) {

					const uint8_t* p = pixels + ( static_cast < size_t > ( y ) * static_cast < size_t > ( size ) + static_cast < size_t > ( x ) ) * 4;

					b += static_cast < float > ( p [ 0 ] );
					g += static_cast < float > ( p [ 1 ] );
					r += static_cast < float > ( p [ 2 ] );
					samples++;

				}

			}

			float scale = 1.0f / ( 255.0f * static_cast < float > ( base::max ( samples, 1 ) ) );
			render::color average ( r * scale, g * scale, b * scale );
			float gray = ( average.r + average.g + average.b ) / 3.0f;
			float spread = base::max ( base::max ( average.r, average.g ), average.b ) - base::min ( base::min ( average.r, average.g ), average.b );

			average = render::color ( math::saturate ( gray + ( average.r - gray ) * 1.3f ), math::saturate ( gray + ( average.g - gray ) * 1.3f ), math::saturate ( gray + ( average.b - gray ) * 1.3f ) );
			palette [ q ] = average;

			float score = spread * 2.0f + ( 1.0f - math::abs ( gray - 0.45f ) );

			if ( score > best ) {

				best = score;
				accent = average;

			}

		}

		float luminance = accent.r * 0.299f + accent.g * 0.587f + accent.b * 0.114f;

		if ( luminance > 0.5f ) {

			float shrink = 0.5f / luminance;

			accent = render::color ( accent.r * shrink, accent.g * shrink, accent.b * shrink );

		}

	}

	static bool finish_art ( IWICImagingFactory* imaging, IWICBitmapSource* source, base::list < uint8_t >& pixels, render::color* palette, render::color& accent );

	bool extract_art ( const wchar_t* path, base::list < uint8_t >& pixels, render::color* palette, render::color& accent ) {

		static IWICImagingFactory* imaging = nullptr;

		if ( !imaging && FAILED ( CoCreateInstance ( CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, __uuidof ( IWICImagingFactory ), reinterpret_cast < void** > ( &imaging ) ) ) )
			return false;

		IWICBitmapSource* source = nullptr;
		IShellItemImageFactory* shell = nullptr;
		HBITMAP bitmap = nullptr;

		if ( SUCCEEDED ( SHCreateItemFromParsingName ( path, nullptr, __uuidof ( IShellItemImageFactory ), reinterpret_cast < void** > ( &shell ) ) ) ) {

			SIZE wanted = { art_size, art_size };

			if ( SUCCEEDED ( shell->GetImage ( wanted, SIIGBF_THUMBNAILONLY | SIIGBF_BIGGERSIZEOK, &bitmap ) ) && bitmap ) {

				IWICBitmap* captured = nullptr;

				if ( SUCCEEDED ( imaging->CreateBitmapFromHBITMAP ( bitmap, nullptr, WICBitmapIgnoreAlpha, &captured ) ) )
					source = captured;

				DeleteObject ( bitmap );

			}

			shell->Release ( );

		}

		if ( !source )
			source = folder_image ( imaging, path );

		if ( !source )
			return false;

		return finish_art ( imaging, source, pixels, palette, accent );

	}

	static IWICImagingFactory* wic ( ) {

		static IWICImagingFactory* imaging = nullptr;
		static SRWLOCK guard = SRWLOCK_INIT;

		AcquireSRWLockExclusive ( &guard );

		if ( !imaging )
			CoCreateInstance ( CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, __uuidof ( IWICImagingFactory ), reinterpret_cast < void** > ( &imaging ) );

		IWICImagingFactory* result = imaging;

		ReleaseSRWLockExclusive ( &guard );

		return result;

	}

	bool art_from_memory ( const uint8_t* data, size_t size, base::list < uint8_t >& pixels, render::color* palette, render::color& accent ) {

		IWICImagingFactory* imaging = wic ( );
		IWICStream* stream = nullptr;
		IWICBitmapDecoder* decoder = nullptr;
		IWICBitmapFrameDecode* frame = nullptr;
		bool ok = false;

		if ( imaging && SUCCEEDED ( imaging->CreateStream ( &stream ) ) && SUCCEEDED ( stream->InitializeFromMemory ( const_cast < BYTE* > ( data ), static_cast < DWORD > ( size ) ) ) && SUCCEEDED ( imaging->CreateDecoderFromStream ( stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder ) ) && SUCCEEDED ( decoder->GetFrame ( 0, &frame ) ) )
			ok = finish_art ( imaging, frame, pixels, palette, accent );

		if ( decoder )
			decoder->Release ( );

		if ( stream )
			stream->Release ( );

		return ok;

	}

	static bool finish_art ( IWICImagingFactory* imaging, IWICBitmapSource* source, base::list < uint8_t >& pixels, render::color* palette, render::color& accent ) {

		UINT width = 0;
		UINT height = 0;

		source->GetSize ( &width, &height );

		UINT side = width < height ? width : height;
		WICRect crop = { static_cast < INT > ( ( width - side ) / 2 ), static_cast < INT > ( ( height - side ) / 2 ), static_cast < INT > ( side ), static_cast < INT > ( side ) };
		IWICBitmapClipper* clipper = nullptr;
		IWICBitmapScaler* scaler = nullptr;
		IWICFormatConverter* converter = nullptr;
		bool ok = false;

		if ( side > 0 && SUCCEEDED ( imaging->CreateBitmapClipper ( &clipper ) ) && SUCCEEDED ( clipper->Initialize ( source, &crop ) ) && SUCCEEDED ( imaging->CreateBitmapScaler ( &scaler ) ) && SUCCEEDED ( scaler->Initialize ( clipper, art_size, art_size, side > static_cast < UINT > ( art_size ) ? WICBitmapInterpolationModeFant : WICBitmapInterpolationModeHighQualityCubic ) ) && SUCCEEDED ( imaging->CreateFormatConverter ( &converter ) ) && SUCCEEDED ( converter->Initialize ( scaler, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom ) ) ) {

			pixels.resize ( static_cast < size_t > ( art_size ) * art_size * 4 );
			ok = SUCCEEDED ( converter->CopyPixels ( nullptr, art_size * 4, static_cast < UINT > ( pixels.size ( ) ), pixels.data ( ) ) );

		}

		if ( converter )
			converter->Release ( );

		if ( scaler )
			scaler->Release ( );

		if ( clipper )
			clipper->Release ( );

		source->Release ( );

		if ( !ok )
			return false;

		for ( size_t i = 3; i < pixels.size ( ); i += 4 )
			pixels [ i ] = 255;

		build_palette ( pixels.data ( ), art_size, palette, accent );

		return true;

	}

}