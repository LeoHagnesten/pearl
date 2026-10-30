#ifdef PEARL_DEBUG

#include "ui/app.hh"
#include "audio/engine.hh"
#include "base/files.hh"
#include <windows.h>
#include <shellapi.h>

static int32_t argument_count = 0;
static wchar_t** arguments = nullptr;

static const wchar_t* argument ( const wchar_t* name ) {

	for ( int32_t i = 1; i + 1 < argument_count; i++ ) {

		if ( base::same_text ( arguments [ i ], name ) )
			return arguments [ i + 1 ];

	}

	return nullptr;

}

static bool flag ( const wchar_t* name ) {

	for ( int32_t i = 1; i < argument_count; i++ ) {

		if ( base::same_text ( arguments [ i ], name ) )
			return true;

	}

	return false;

}

static float parse_float ( const wchar_t* text ) {

	float value = 0.0f;
	float scale = 0.0f;
	bool negative = false;

	if ( *text == L'-' ) {

		negative = true;
		text++;

	}

	for ( ; *text; text++ ) {

		if ( *text == L'.' ) {

			scale = 0.1f;

			continue;

		}

		if ( *text < L'0' || *text > L'9' )
			break;

		if ( scale > 0.0f ) {

			value += static_cast < float > ( *text - L'0' ) * scale;
			scale *= 0.1f;

		} else
			value = value * 10.0f + static_cast < float > ( *text - L'0' );

	}

	return negative ? -value : value;

}

struct script_reader {

	const wchar_t* cursor;

	bool word ( wchar_t* out, int32_t room ) {

		while ( *cursor == L' ' || *cursor == L';' || *cursor == L'\n' || *cursor == L'\r' || *cursor == L'\t' )
			cursor++;

		if ( !*cursor )
			return false;

		int32_t length = 0;

		while ( *cursor && *cursor != L' ' && *cursor != L';' && *cursor != L'\n' && *cursor != L'\r' && length < room - 1 )
			out [ length++ ] = *cursor++;

		out [ length ] = 0;

		return true;

	}

	float number ( ) {

		wchar_t token [ 64 ];

		if ( !word ( token, 64 ) )
			return 0.0f;

		return parse_float ( token );

	}

};

static uint32_t key_code ( const wchar_t* name, bool& control ) {

	control = false;

	if ( name [ 0 ] == L'c' && name [ 1 ] == L't' && name [ 2 ] == L'r' && name [ 3 ] == L'l' && name [ 4 ] == L'+' ) {

		control = true;
		name += 5;

	}

	if ( base::same_text ( name, L"space" ) )
		return VK_SPACE;

	if ( base::same_text ( name, L"left" ) )
		return VK_LEFT;

	if ( base::same_text ( name, L"right" ) )
		return VK_RIGHT;

	if ( base::same_text ( name, L"up" ) )
		return VK_UP;

	if ( base::same_text ( name, L"down" ) )
		return VK_DOWN;

	if ( base::same_text ( name, L"escape" ) )
		return VK_ESCAPE;

	if ( name [ 0 ] && !name [ 1 ] )
		return static_cast < uint32_t > ( name [ 0 ] >= L'a' && name [ 0 ] <= L'z' ? name [ 0 ] - 32 : name [ 0 ] );

	return 0;

}

static void output_path ( wchar_t* target, const wchar_t* prefix, const wchar_t* name ) {

	base::writer line;

	line.add ( prefix ).add ( L"_" ).add ( name ).add ( L".png" );

	for ( int32_t i = 0; i <= line.size ( ); i++ )
		target [ i ] = line.text ( ) [ i ];

}

static bool capture_mode ( ) {

	const wchar_t* prefix = argument ( L"--capture" );

	if ( !prefix )
		return false;

	float width = 1240.0f;
	float height = 800.0f;
	float scale = 1.0f;
	const wchar_t* size_text = argument ( L"--size" );
	const wchar_t* scale_text = argument ( L"--scale" );
	const wchar_t* script = argument ( L"--script" );
	base::list < char > script_file;
	base::list < wchar_t > script_wide;

	if ( size_text ) {

		width = parse_float ( size_text );

		for ( const wchar_t* p = size_text; *p; p++ ) {

			if ( *p == L'x' ) {

				height = parse_float ( p + 1 );

				break;

			}

		}

	}

	if ( scale_text )
		scale = parse_float ( scale_text );

	const wchar_t* script_path = argument ( L"--script-file" );

	if ( script_path && files::read ( script_path, script_file ) ) {

		for ( char value : script_file )
			script_wide.push ( static_cast < wchar_t > ( static_cast < uint8_t > ( value ) ) );

		script_wide.push ( 0 );
		script = script_wide.data ( );

	}

	render::gpu* gpu = new render::gpu ( );

	if ( !gpu->setup ( flag ( L"--software" ) ) )
		return true;

	ui::app* app = new ui::app ( );

	app->fixed_step = true;

	const wchar_t* folder = argument ( L"--folder" );

	if ( folder ) {

		app->demo_library = false;
		library::add_folder ( folder );
		library::start_scan ( );

	}

	if ( !app->setup ( gpu, false ) )
		return true;

	app->resize ( static_cast < int32_t > ( width * scale ), static_cast < int32_t > ( height * scale ), scale );

	double clock = 0.0;
	auto step = [ & ] ( int32_t frames ) {

		for ( int32_t i = 0; i < frames; i++ ) {

			clock += 1.0 / 60.0;
			app->frame ( clock );

		}

	};

	step ( 30 );

	for ( int32_t i = 0; i < 400 && library::scanning ( ); i++ ) {

		Sleep ( 10 );
		step ( 1 );

	}

	for ( int32_t i = 0; i < 30; i++ ) {

		Sleep ( 15 );
		step ( 1 );

	}

	script_reader reader = { script ? script : L"shot main" };
	wchar_t command [ 64 ];

	while ( reader.word ( command, 64 ) ) {

		if ( base::same_text ( command, L"wait" ) )
			step ( base::max ( static_cast < int32_t > ( reader.number ( ) * 60.0f + 0.5f ), 1 ) );
		else if ( base::same_text ( command, L"real" ) ) {

			double until = base::seconds ( ) + static_cast < double > ( reader.number ( ) );

			while ( base::seconds ( ) < until ) {

				Sleep ( 16 );
				step ( 1 );

			}

		} else if ( base::same_text ( command, L"spotify" ) ) {

			spotify::start ( nullptr, 0 );

			double until = base::seconds ( ) + 10.0;

			while ( base::seconds ( ) < until && !( app->remote_state.connected && app->remote_state.account [ 0 ] ) ) {

				Sleep ( 30 );
				step ( 1 );

			}

			base::writer line;
			wchar_t path [ 600 ];
			base::list < char > text;

			line.add ( L"configured " ).number ( app->remote_state.configured ? 1 : 0 ).add ( L" connected " ).number ( app->remote_state.connected ? 1 : 0 ).add ( L" account " ).number ( base::text_length ( app->remote_state.account ) ).add ( L" message " ).add ( app->remote_state.message );

			for ( int32_t i = 0; i < line.size ( ); i++ )
				text.push ( static_cast < char > ( line.text ( ) [ i ] < 128 ? line.text ( ) [ i ] : L'?' ) );

			text.push ( '\n' );
			output_path ( path, prefix, L"spotify" );
			path [ base::text_length ( path ) - 3 ] = L't';
			path [ base::text_length ( path ) - 2 ] = L'x';
			path [ base::text_length ( path ) - 1 ] = L't';
			files::write ( path, text.data ( ), text.count ( ) );

		} else if ( base::same_text ( command, L"trace" ) ) {

			wchar_t name [ 64 ];

			reader.word ( name, 64 );

			int32_t frames = base::max ( static_cast < int32_t > ( reader.number ( ) ), 1 );
			base::writer line;
			wchar_t path [ 600 ];
			base::list < char > text;

			if ( base::same_text ( name, L"now" ) )
				app->toggle_now ( );
			else if ( base::same_text ( name, L"songs" ) )
				app->navigate ( ui::page::SONGS );
			else if ( base::same_text ( name, L"liked" ) )
				app->navigate ( ui::page::LIKED );

			for ( int32_t i = 0; i < frames; i++ ) {

				double started = base::seconds ( );

				step ( 1 );

				double drawn = base::seconds ( );

				gpu->finish ( );
				line.fixed ( ( drawn - started ) * 1000.0, 2 ).add ( L"/" ).fixed ( ( base::seconds ( ) - started ) * 1000.0, 2 ).add ( L" " );

			}

			for ( int32_t i = 0; i < line.size ( ); i++ )
				text.push ( static_cast < char > ( line.text ( ) [ i ] ) );

			text.push ( '\n' );
			output_path ( path, prefix, L"trace" );
			path [ base::text_length ( path ) - 3 ] = L't';
			path [ base::text_length ( path ) - 2 ] = L'x';
			path [ base::text_length ( path ) - 1 ] = L't';
			files::write ( path, text.data ( ), text.count ( ) );

		} else if ( base::same_text ( command, L"type" ) ) {

			wchar_t text [ 64 ];

			reader.word ( text, 64 );

			for ( int32_t i = 0; text [ i ]; i++ ) {

				app->character ( text [ i ] == L'_' ? L' ' : text [ i ] );
				step ( 3 );

			}

		} else if ( base::same_text ( command, L"remote_play" ) ) {

			int32_t pick = static_cast < int32_t > ( reader.number ( ) );

			if ( pick >= 0 && pick < app->found_remote.count ( ) )
				app->play_catalog ( app->found_remote [ static_cast < size_t > ( pick ) ] );

			step ( 1 );

		} else if ( base::same_text ( command, L"load" ) ) {

			app->load_state ( );
			step ( 2 );

		} else if ( base::same_text ( command, L"remote_pause" ) ) {

			spotify::play ( false );
			step ( 1 );

		} else if ( base::same_text ( command, L"panel" ) ) {

			app->spotify_open = !app->spotify_open;
			step ( 1 );

		} else if ( base::same_text ( command, L"settle" ) ) {

			for ( int32_t i = 0; i < 40; i++ ) {

				Sleep ( 15 );
				step ( 1 );

			}

		} else if ( base::same_text ( command, L"rebuild" ) ) {

			render::gpu* fresh = new render::gpu ( );

			if ( fresh->setup ( flag ( L"--software" ) ) ) {

				app->rebuild_gpu ( fresh );
				gpu->shutdown ( );
				delete gpu;
				gpu = fresh;

			} else {

				fresh->shutdown ( );
				delete fresh;

			}

			step ( 1 );

		} else if ( base::same_text ( command, L"frames" ) )
			step ( static_cast < int32_t > ( reader.number ( ) ) );
		else if ( base::same_text ( command, L"move" ) ) {

			float x = reader.number ( );
			float y = reader.number ( );

			app->mouse_move ( x * scale, y * scale );
			step ( 1 );

		} else if ( base::same_text ( command, L"click" ) ) {

			float x = reader.number ( );
			float y = reader.number ( );

			app->mouse_move ( x * scale, y * scale );
			step ( 2 );
			app->mouse_button ( 0, true );
			step ( 1 );
			app->mouse_button ( 0, false );
			step ( 1 );

		} else if ( base::same_text ( command, L"double" ) ) {

			float x = reader.number ( );
			float y = reader.number ( );

			app->mouse_move ( x * scale, y * scale );
			step ( 2 );
			app->mouse_button ( 0, true );
			step ( 1 );
			app->mouse_button ( 0, false );
			step ( 1 );
			app->mouse_button ( 0, true );
			step ( 1 );
			app->mouse_button ( 0, false );
			step ( 1 );

		} else if ( base::same_text ( command, L"drag" ) ) {

			float x0 = reader.number ( );
			float y0 = reader.number ( );
			float x1 = reader.number ( );
			float y1 = reader.number ( );
			int32_t frames = base::max ( static_cast < int32_t > ( reader.number ( ) * 60.0f ), 2 );

			app->mouse_move ( x0 * scale, y0 * scale );
			step ( 2 );
			app->mouse_button ( 0, true );
			step ( 1 );

			for ( int32_t i = 1; i <= frames; i++ ) {

				float t = static_cast < float > ( i ) / static_cast < float > ( frames );

				app->mouse_move ( ( x0 + ( x1 - x0 ) * t ) * scale, ( y0 + ( y1 - y0 ) * t ) * scale );
				step ( 1 );

			}

			wchar_t name [ 64 ];
			wchar_t path [ 600 ];

			reader.word ( name, 64 );
			output_path ( path, prefix, name );
			gpu->save ( gpu->chrome, path, theme::underlay, app->width, app->height );
			app->mouse_button ( 0, false );
			step ( 1 );

		} else if ( base::same_text ( command, L"bench" ) ) {

			int32_t frames = base::max ( static_cast < int32_t > ( reader.number ( ) ), 1 );
			double started = base::seconds ( );
			FILETIME created;
			FILETIME exited;
			FILETIME kernel_before;
			FILETIME user_before;
			FILETIME kernel_after;
			FILETIME user_after;

			GetThreadTimes ( GetCurrentThread ( ), &created, &exited, &kernel_before, &user_before );

			for ( int32_t i = 0; i < frames; i++ ) {

				step ( 1 );
				gpu->finish ( );

			}

			GetThreadTimes ( GetCurrentThread ( ), &created, &exited, &kernel_after, &user_after );

			double wall = ( base::seconds ( ) - started ) * 1000.0 / static_cast < double > ( frames );
			auto ticks = [ ] ( FILETIME value ) {

				return static_cast < double > ( ( static_cast < uint64_t > ( value.dwHighDateTime ) << 32 ) | value.dwLowDateTime );

			};
			double cpu = ( ticks ( user_after ) - ticks ( user_before ) + ticks ( kernel_after ) - ticks ( kernel_before ) ) / 10000.0 / static_cast < double > ( frames );
			base::writer line;
			wchar_t path [ 600 ];
			base::list < char > text;

			line.add ( L"frames " ).number ( frames ).add ( L" wall " ).fixed ( wall, 3 ).add ( L" ms cpu " ).fixed ( cpu, 3 ).add ( L" ms per frame" );

			for ( int32_t i = 0; i < line.size ( ); i++ )
				text.push ( static_cast < char > ( line.text ( ) [ i ] ) );

			text.push ( '\n' );
			output_path ( path, prefix, L"bench" );
			path [ base::text_length ( path ) - 3 ] = L't';
			path [ base::text_length ( path ) - 2 ] = L'x';
			path [ base::text_length ( path ) - 1 ] = L't';
			files::write ( path, text.data ( ), text.count ( ) );

		} else if ( base::same_text ( command, L"icon" ) ) {

			wchar_t name [ 64 ];
			wchar_t path [ 600 ];

			reader.word ( name, 64 );

			int32_t size = static_cast < int32_t > ( reader.number ( ) );
			render::surface target;
			render::canvas canvas;
			float side = static_cast < float > ( size );

			gpu->make_surface ( target, size, size, false );
			canvas.begin ( gpu, 1.0f, math::vec2 ( 0.0f, 0.0f ), 0.0f );
			app->draw_brand ( canvas, math::vec2 ( side * 0.5f, side * 0.47f ), side * 0.4f );
			gpu->draw ( canvas, target, nullptr, true, render::color ( 0.0f, 0.0f, 0.0f, 0.0f ) );
			output_path ( path, prefix, name );
			gpu->save ( target, path, render::color ( 0.0f, 0.0f, 0.0f, 0.0f ), size, size );
			gpu->free_surface ( target );

		} else if ( base::same_text ( command, L"mini" ) ) {

			wchar_t name [ 64 ];
			wchar_t path [ 600 ];

			reader.word ( name, 64 );

			float hover_x = reader.number ( );
			float hover_y = reader.number ( );
			int32_t mini_w = static_cast < int32_t > ( ( 484.0f + 48.0f ) * scale );
			int32_t mini_h = static_cast < int32_t > ( ( 60.0f + 48.0f ) * scale );
			render::surface target;

			gpu->make_surface ( target, mini_w, mini_h, true );
			app->mini_dpi = scale;
			app->open_mini ( true );

			if ( hover_x >= 0.0f )
				app->mouse_move ( hover_x * scale, hover_y * scale );
			else
				app->mouse_leave ( );

			for ( int32_t i = 0; i < 45; i++ ) {

				clock += 1.0 / 60.0;
				app->frame_mini ( clock, target, mini_w, mini_h, scale, 24.0f );

			}

			output_path ( path, prefix, name );
			gpu->save ( target, path, render::rgb ( 0x2B2D31 ), mini_w, mini_h );
			gpu->free_surface ( target );

		} else if ( base::same_text ( command, L"snapcheck" ) ) {

			render::canvas probe;
			base::writer line;
			wchar_t path [ 600 ];
			base::list < char > text;
			float scales [ 3 ] = { 1.0f, 1.25f, 1.5f };
			float worst_drift = 0.0f;
			float worst_rest = 0.0f;

			for ( float dpi_scale : scales ) {

				float first_x = 0.0f;
				float first_y = 0.0f;

				for ( int32_t k = 0; k <= 90; k++ ) {

					float t = static_cast < float > ( k ) / 90.0f;
					float e = t * t * ( 3.0f - 2.0f * t );
					math::vec2 pivot ( 78.0f, 236.37f );
					math::vec2 at ( 92.0f, 236.37f );

					probe.begin ( gpu, dpi_scale, math::vec2 ( 0.0f, 0.0f ), 0.0f );
					probe.push ( pivot, 0.9f + 0.1f * e, 1.0f, math::vec2 ( -8.0f * ( 1.0f - e ), 0.0f ) );
					probe.text ( L"playlist #1", render::font::BODY_STRONG, at, theme::ink );

					render::text_entry* entry = gpu->layout ( L"playlist #1", render::font::BODY_STRONG, 0.0f );
					math::vec2 exact = probe.screen ( math::vec2 ( at.x, at.y - ( entry->baseline - entry->cap * 0.5f ) ) );
					float dx = probe.texts [ 0 ].x - exact.x;
					float dy = probe.texts [ 0 ].y - exact.y;

					if ( k == 0 ) {

						first_x = dx;
						first_y = dy;

					}

					worst_drift = base::max ( worst_drift, base::max ( math::abs ( dx - first_x ), math::abs ( dy - first_y ) ) );

					if ( k == 90 ) {

						worst_rest = base::max ( worst_rest, math::abs ( probe.texts [ 0 ].x - math::round ( probe.texts [ 0 ].x ) ) );
						worst_rest = base::max ( worst_rest, math::abs ( probe.texts [ 0 ].y - math::round ( probe.texts [ 0 ].y ) ) );

					}

				}

			}

			line.add ( L"drift " ).fixed ( worst_drift, 4 ).add ( L" rest " ).fixed ( worst_rest, 4 );

			for ( int32_t i = 0; i < line.size ( ); i++ )
				text.push ( static_cast < char > ( line.text ( ) [ i ] ) );

			text.push ( '\n' );
			output_path ( path, prefix, L"snapcheck" );
			path [ base::text_length ( path ) - 3 ] = L't';
			path [ base::text_length ( path ) - 2 ] = L'x';
			path [ base::text_length ( path ) - 1 ] = L't';
			files::write ( path, text.data ( ), text.count ( ) );

		} else if ( base::same_text ( command, L"pretend" ) ) {

			static wchar_t names [ 3 ] [ 64 ];

			for ( int32_t f = 0; f < 3; f++ ) {

				reader.word ( names [ f ], 64 );

				for ( int32_t i = 0; names [ f ] [ i ]; i++ ) {

					if ( names [ f ] [ i ] == L'_' )
						names [ f ] [ i ] = L' ';

				}

			}

			float seconds = reader.number ( );
			float at = reader.number ( );

			if ( app->track >= 0 ) {

				library::track& item = library::track_at ( app->track );
				library::album& group = library::album_at ( item.album );

				item.title = names [ 0 ];
				item.duration = seconds;
				item.key = library::hash ( names [ 0 ], true );
				group.artist = names [ 1 ];
				group.title = names [ 2 ];
				audio::seek ( static_cast < double > ( at ) );

			}

			step ( 2 );

		} else if ( base::same_text ( command, L"track" ) ) {

			int32_t index = static_cast < int32_t > ( reader.number ( ) );
			const library::album& album = library::album_at ( library::track_at ( index ).album );
			base::list < int32_t > list;

			for ( int32_t i = 0; i < album.count; i++ )
				list.push ( album.first + i );

			app->play_list ( list, index - album.first, { ui::origin::ALBUM, library::track_at ( index ).album } );
			step ( 1 );

		} else if ( base::same_text ( command, L"volume" ) ) {

			float level = reader.number ( );

			app->muted = level < 0.0f;
			audio::volume ( app->muted ? 0.0f : level );
			app->volume = app->muted ? app->volume : level;
			step ( 1 );

		} else if ( base::same_text ( command, L"down" ) ) {

			app->mouse_button ( 0, true );
			step ( 1 );

		} else if ( base::same_text ( command, L"right" ) ) {

			app->mouse_button ( 1, true );
			step ( 1 );

		} else if ( base::same_text ( command, L"up" ) ) {

			app->mouse_button ( 0, false );
			step ( 1 );

		} else if ( base::same_text ( command, L"leave" ) ) {

			app->mouse_leave ( );
			step ( 1 );

		} else if ( base::same_text ( command, L"wheel" ) ) {

			app->mouse_wheel ( reader.number ( ) );
			step ( 1 );

		} else if ( base::same_text ( command, L"key" ) ) {

			wchar_t name [ 64 ];
			bool control = false;

			reader.word ( name, 64 );

			uint32_t code = key_code ( name, control );

			if ( code )
				app->key ( code, control, false );

			step ( 1 );

		} else if ( base::same_text ( command, L"size" ) ) {

			width = reader.number ( );
			height = reader.number ( );
			app->resize ( static_cast < int32_t > ( width * scale ), static_cast < int32_t > ( height * scale ), scale );
			step ( 2 );

		} else if ( base::same_text ( command, L"maximized" ) ) {

			app->set_maximized ( reader.number ( ) > 0.5f );
			step ( 1 );

		} else if ( base::same_text ( command, L"play" ) ) {

			app->toggle_play ( );
			step ( 1 );

		} else if ( base::same_text ( command, L"next" ) ) {

			app->next_track ( false );
			step ( 1 );

		} else if ( base::same_text ( command, L"now" ) ) {

			app->toggle_now ( );
			step ( 1 );

		} else if ( base::same_text ( command, L"seek" ) ) {

			audio::seek ( static_cast < double > ( reader.number ( ) ) );
			step ( 1 );

		} else if ( base::same_text ( command, L"page" ) ) {

			wchar_t name [ 64 ];

			reader.word ( name, 64 );

			if ( base::same_text ( name, L"now" ) )
				app->navigate ( ui::page::NOW );
			else if ( base::same_text ( name, L"liked" ) )
				app->navigate ( ui::page::LIKED );
			else if ( base::same_text ( name, L"songs" ) )
				app->navigate ( ui::page::SONGS );
			else if ( base::same_text ( name, L"albums" ) )
				app->navigate ( ui::page::ALBUMS );
			else if ( base::same_text ( name, L"home" ) )
				app->navigate ( ui::page::HOME );
			else if ( base::same_text ( name, L"mix" ) )
				app->navigate ( ui::page::MIX );
			else if ( base::same_text ( name, L"playlists" ) )
				app->navigate ( ui::page::PLAYLISTS );
			else if ( base::same_text ( name, L"playlist" ) )
				app->navigate ( ui::page::PLAYLIST, static_cast < int32_t > ( reader.number ( ) ) );
			else
				app->navigate ( ui::page::ALBUM, static_cast < int32_t > ( parse_float ( name ) ) );

			step ( 1 );

		} else if ( base::same_text ( command, L"search" ) ) {

			wchar_t name [ 64 ];

			reader.word ( name, 64 );

			for ( int32_t i = 0; name [ i ]; i++ ) {

				if ( name [ i ] == L'_' )
					name [ i ] = L' ';

			}

			app->type_search ( base::same_text ( name, L"-" ) ? L"" : name );
			step ( 1 );

		} else if ( base::same_text ( command, L"playlist" ) ) {

			wchar_t name [ 64 ];

			reader.word ( name, 64 );

			for ( int32_t i = 0; name [ i ]; i++ ) {

				if ( name [ i ] == L'_' )
					name [ i ] = L' ';

			}

			int32_t made = app->create_playlist ( name );
			int32_t songs = static_cast < int32_t > ( reader.number ( ) );

			for ( int32_t i = 0; i < songs && i < library::track_count ( ); i++ )
				app->playlists [ static_cast < size_t > ( made ) ].keys.push ( library::track_at ( ( i * 7 + made * 3 ) % library::track_count ( ) ).key );

			step ( 1 );

		} else if ( base::same_text ( command, L"shot" ) ) {

			wchar_t name [ 64 ];
			wchar_t path [ 600 ];

			reader.word ( name, 64 );
			output_path ( path, prefix, name );
			gpu->save ( gpu->chrome, path, theme::underlay, app->width, app->height );

		} else if ( base::same_text ( command, L"overlay" ) ) {

			wchar_t name [ 64 ];
			wchar_t path [ 600 ];

			reader.word ( name, 64 );

			float overlay_scale = reader.number ( );
			float overlay_alpha = reader.number ( );
			float margin = 72.0f * scale;
			render::surface target;
			render::canvas canvas;

			gpu->make_surface ( target, app->width + static_cast < int32_t > ( margin * 2.0f ), app->height + static_cast < int32_t > ( margin * 2.0f ), false );
			canvas.begin ( gpu, 1.0f, math::vec2 ( 0.0f, 0.0f ), 0.0f );
			app->draw_snapshot ( canvas, margin, overlay_scale, overlay_alpha, false );
			gpu->draw ( canvas, target, &gpu->chrome, true, render::rgb ( 0x5B6B82 ) );
			output_path ( path, prefix, name );
			gpu->save ( target, path, render::color ( 0.0f, 0.0f, 0.0f, 0.0f ), target.width, target.height );
			gpu->free_surface ( target );

		}

	}

	return true;

}

static void put_u32 ( base::list < uint8_t >& out, uint32_t value ) {

	for ( int32_t i = 0; i < 4; i++ )
		out.push ( static_cast < uint8_t > ( value >> ( i * 8 ) ) );

}

static void put_u16 ( base::list < uint8_t >& out, uint32_t value ) {

	out.push ( static_cast < uint8_t > ( value ) );
	out.push ( static_cast < uint8_t > ( value >> 8 ) );

}

static bool wav_mode ( ) {

	const wchar_t* path = argument ( L"--wav" );

	if ( !path )
		return false;

	const wchar_t* track_text = argument ( L"--track" );
	const wchar_t* seconds_text = argument ( L"--seconds" );
	const wchar_t* start_text = argument ( L"--start" );
	int32_t index = track_text ? static_cast < int32_t > ( parse_float ( track_text ) ) : 0;
	float seconds = seconds_text ? parse_float ( seconds_text ) : 20.0f;
	float start = start_text ? parse_float ( start_text ) : 0.0f;

	library::build_demo ( );

	const library::track& item = library::track_at ( index );
	const wchar_t* rate_text = argument ( L"--rate" );
	const wchar_t* tag_text = argument ( L"--tag" );
	int32_t rate = rate_text ? static_cast < int32_t > ( parse_float ( rate_text ) ) : 48000;
	int32_t frames = static_cast < int32_t > ( seconds * static_cast < float > ( rate ) );
	base::list < uint8_t > info;
	base::list < float > samples;
	base::list < uint8_t > out;
	float peak = 0.0f;
	double energy = 0.0;

	samples.resize ( static_cast < size_t > ( frames ) * 2 );
	synth::render ( item.song, static_cast < int64_t > ( start * static_cast < float > ( rate ) ), frames, rate, samples.data ( ) );

	out.push ( 'R' );
	out.push ( 'I' );
	out.push ( 'F' );
	out.push ( 'F' );
	if ( tag_text ) {

		const char* names [ 4 ] = { "INAM", "IART", "IPRD", "ITRK" };
		const wchar_t* cursor = tag_text;

		info.push ( 'I' );
		info.push ( 'N' );
		info.push ( 'F' );
		info.push ( 'O' );

		for ( int32_t field = 0; field < 4 && *cursor; field++ ) {

			char value [ 256 ];
			int32_t length = 0;

			while ( *cursor && *cursor != L'|' && length < 254 )
				value [ length++ ] = static_cast < char > ( *cursor++ );

			if ( *cursor == L'|' )
				cursor++;

			value [ length++ ] = 0;

			for ( int32_t k = 0; k < 4; k++ )
				info.push ( static_cast < uint8_t > ( names [ field ] [ k ] ) );

			put_u32 ( info, static_cast < uint32_t > ( length ) );

			for ( int32_t k = 0; k < length; k++ )
				info.push ( static_cast < uint8_t > ( value [ k ] ) );

			if ( length & 1 )
				info.push ( 0 );

		}

	}

	put_u32 ( out, static_cast < uint32_t > ( 36 + frames * 4 + ( info.empty ( ) ? 0 : 8 + info.count ( ) ) ) );
	out.push ( 'W' );
	out.push ( 'A' );
	out.push ( 'V' );
	out.push ( 'E' );
	out.push ( 'f' );
	out.push ( 'm' );
	out.push ( 't' );
	out.push ( ' ' );
	put_u32 ( out, 16 );
	put_u16 ( out, 1 );
	put_u16 ( out, 2 );
	put_u32 ( out, static_cast < uint32_t > ( rate ) );
	put_u32 ( out, static_cast < uint32_t > ( rate * 4 ) );
	put_u16 ( out, 4 );
	put_u16 ( out, 16 );
	out.push ( 'd' );
	out.push ( 'a' );
	out.push ( 't' );
	out.push ( 'a' );
	put_u32 ( out, static_cast < uint32_t > ( frames * 4 ) );

	for ( size_t i = 0; i < samples.size ( ); i++ ) {

		float value = math::saturate ( samples [ i ] * 0.5f + 0.5f ) * 2.0f - 1.0f;
		int32_t pcm = static_cast < int32_t > ( value * 32767.0f );

		peak = base::max ( peak, math::abs ( samples [ i ] ) );
		energy += static_cast < double > ( samples [ i ] ) * static_cast < double > ( samples [ i ] );
		put_u16 ( out, static_cast < uint32_t > ( static_cast < uint16_t > ( static_cast < int16_t > ( pcm ) ) ) );

	}

	if ( !info.empty ( ) ) {

		out.push ( 'L' );
		out.push ( 'I' );
		out.push ( 'S' );
		out.push ( 'T' );
		put_u32 ( out, static_cast < uint32_t > ( info.count ( ) ) );

		for ( uint8_t value : info )
			out.push ( value );

	}

	files::write ( path, reinterpret_cast < const char* > ( out.data ( ) ), out.count ( ) );

	base::writer report;
	wchar_t report_path [ 600 ];
	base::list < char > text;

	report.add ( L"peak " ).fixed ( peak, 4 ).add ( L" rms " ).fixed ( math::sqrt ( static_cast < float > ( energy / static_cast < double > ( samples.size ( ) ) ) ), 4 ).add ( L" bpm " ).fixed ( item.song.bpm, 1 ).add ( L" duration " ).fixed ( item.duration, 1 );

	for ( int32_t i = 0; i < report.size ( ); i++ )
		text.push ( static_cast < char > ( report.text ( ) [ i ] ) );

	base::writer name;

	name.add ( path ).add ( L".txt" );

	for ( int32_t i = 0; i <= name.size ( ); i++ )
		report_path [ i ] = name.text ( ) [ i ];

	files::write ( report_path, text.data ( ), text.count ( ) );

	return true;

}

bool debug_main ( ) {

	arguments = CommandLineToArgvW ( GetCommandLineW ( ), &argument_count );

	if ( !arguments || argument_count < 2 )
		return false;

	return capture_mode ( ) || wav_mode ( );

}

#endif