#include "ui/app.hh"
#include "audio/engine.hh"
#include "base/files.hh"
#include <windows.h>
#include <objbase.h>
#include <dwmapi.h>
#include <timeapi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shellapi.h>

#ifdef PEARL_DEBUG

bool debug_main ( );

#endif

enum class phase : int32_t {

	SHOWN,
	HIDING,
	HIDDEN,
	SHOWING,

};

static ui::app* program = nullptr;
static render::gpu* device = nullptr;
static render::output main_output;
static render::output overlay_output;
static render::canvas* overlay_canvas = nullptr;
static HWND main_window = nullptr;
static HWND overlay_window = nullptr;
static phase window_phase = phase::HIDDEN;
static double phase_start = 0.0;
static bool phase_close = false;
static bool tracking = false;
static bool frame_tracking = false;
static int32_t overlay_margin = 0;
static constexpr UINT show_message = WM_APP + 1;
static constexpr UINT spotify_message = WM_APP + 2;
static constexpr UINT_PTR resize_timer = 7;
static int32_t refresh_rate = 60;
static bool start_maximized = false;
static HWND mini_window = nullptr;
static render::output mini_output;
static render::surface mini_surface;
static render::surface overlay_surface;
static HANDLE frame_timer = nullptr;
static int64_t frame_last = 0;
static bool mini_on = false;
static bool hide_to_mini = false;
static bool mini_tracking = false;
static bool mini_frame_tracking = false;
static bool mini_snapping = false;
static float mini_x = 0.0f;
static float mini_y = 0.0f;
static float mini_vx = 0.0f;
static float mini_vy = 0.0f;
static float mini_goal_x = 0.0f;
static float mini_goal_y = 0.0f;
static double mini_step_time = 0.0;
static POINT mini_last_position = { 0, 0 };
static double mini_last_time = 0.0;
static float mini_fling_x = 0.0f;
static float mini_fling_y = 0.0f;
static constexpr float mini_pill_w = 484.0f;
static constexpr float mini_pill_h = 60.0f;
static constexpr float mini_margin = 24.0f;
static constexpr UINT_PTR mini_timer = 8;
static bool device_lost = false;
static double device_retry = 0.0;
static bool software_only = false;

typedef UINT ( __stdcall* window_dpi_call ) ( HWND );
typedef UINT ( __stdcall* system_dpi_call ) ( );
typedef int ( __stdcall* metric_dpi_call ) ( int, UINT );
typedef BOOL ( __stdcall* context_call ) ( DPI_AWARENESS_CONTEXT );
typedef HRESULT ( __stdcall* awareness_call ) ( int );
typedef HRESULT ( __stdcall* monitor_dpi_call ) ( HMONITOR, int, UINT*, UINT* );

static window_dpi_call dpi_of_window = nullptr;
static system_dpi_call dpi_of_system = nullptr;
static metric_dpi_call metric_of_dpi = nullptr;
static monitor_dpi_call dpi_of_monitor = nullptr;

template < typename call >
static call find_call ( HMODULE module, const char* name ) {

	return module ? reinterpret_cast < call > ( reinterpret_cast < void* > ( GetProcAddress ( module, name ) ) ) : nullptr;

}

static void load_dpi ( ) {

	HMODULE user = GetModuleHandleW ( L"user32.dll" );
	HMODULE shell = LoadLibraryExW ( L"shcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32 );
	context_call context = find_call < context_call > ( user, "SetProcessDpiAwarenessContext" );
	awareness_call awareness = find_call < awareness_call > ( shell, "SetProcessDpiAwareness" );

	dpi_of_window = find_call < window_dpi_call > ( user, "GetDpiForWindow" );
	dpi_of_system = find_call < system_dpi_call > ( user, "GetDpiForSystem" );
	metric_of_dpi = find_call < metric_dpi_call > ( user, "GetSystemMetricsForDpi" );
	dpi_of_monitor = find_call < monitor_dpi_call > ( shell, "GetDpiForMonitor" );

	if ( context && context ( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ) )
		return;

	if ( awareness && SUCCEEDED ( awareness ( 2 ) ) )
		return;

	SetProcessDPIAware ( );

}

static UINT system_dpi ( ) {

	if ( dpi_of_system )
		return dpi_of_system ( );

	HDC screen = GetDC ( nullptr );
	int32_t dpi = screen ? GetDeviceCaps ( screen, LOGPIXELSX ) : 96;

	if ( screen )
		ReleaseDC ( nullptr, screen );

	return dpi > 0 ? static_cast < UINT > ( dpi ) : 96;

}

static UINT window_dpi ( HWND window ) {

	UINT dpi = dpi_of_window ? dpi_of_window ( window ) : 0;
	UINT x = 0;
	UINT y = 0;

	if ( !dpi && dpi_of_monitor && SUCCEEDED ( dpi_of_monitor ( MonitorFromWindow ( window, MONITOR_DEFAULTTONEAREST ), 0, &x, &y ) ) )
		dpi = x;

	return dpi ? dpi : system_dpi ( );

}

static int32_t scaled_metric ( int32_t index, UINT dpi ) {

	if ( metric_of_dpi )
		return metric_of_dpi ( index, dpi );

	return MulDiv ( GetSystemMetrics ( index ), static_cast < int32_t > ( dpi ), static_cast < int32_t > ( system_dpi ( ) ) );

}

static int32_t frame_thickness ( HWND window ) {

	UINT dpi = window_dpi ( window );

	return scaled_metric ( SM_CYFRAME, dpi ) + scaled_metric ( SM_CXPADDEDBORDER, dpi );

}

static void cloak ( bool value ) {

	BOOL flag = value ? TRUE : FALSE;

	DwmSetWindowAttribute ( main_window, DWMWA_CLOAK, &flag, sizeof ( flag ) );

}

static void transitions ( bool enabled ) {

	BOOL disabled = enabled ? FALSE : TRUE;

	DwmSetWindowAttribute ( main_window, DWMWA_TRANSITIONS_FORCEDISABLED, &disabled, sizeof ( disabled ) );

}

static void sync_size ( ) {

	RECT client = { };

	GetClientRect ( main_window, &client );

	int32_t width = client.right - client.left;
	int32_t height = client.bottom - client.top;

	if ( width <= 0 || height <= 0 )
		return;

	device->resize ( main_output, width, height );
	program->resize ( width, height, static_cast < float > ( window_dpi ( main_window ) ) / 96.0f );
	program->set_maximized ( IsZoomed ( main_window ) != FALSE );

}

static void measure_refresh ( ) {

	MONITORINFOEXW info = { };
	DEVMODEW mode = { };

	info.cbSize = sizeof ( info );
	mode.dmSize = sizeof ( mode );

	if ( GetMonitorInfoW ( MonitorFromWindow ( main_window, MONITOR_DEFAULTTONEAREST ), &info ) && EnumDisplaySettingsW ( info.szDevice, ENUM_CURRENT_SETTINGS, &mode ) && mode.dmDisplayFrequency > 1 )
		refresh_rate = static_cast < int32_t > ( mode.dmDisplayFrequency );

}

static int64_t clock_now ( ) {

	LARGE_INTEGER value;

	QueryPerformanceCounter ( &value );

	return value.QuadPart;

}

#ifdef PEARL_DEBUG

static double trace_period = 0.0;
static double trace_delay = 0.0;
static int32_t trace_rate = 0;
static int32_t trace_wakes = 0;
static int32_t trace_arms = 0;
static uint32_t trace_message = 0;
static base::writer trace_events;

#endif

static bool frame_armed = false;
static int64_t frame_due = 0;
static bool fine_timer = false;

static DWORD frame_wait ( ) {

	if ( !frame_armed )
		return 250;

	LARGE_INTEGER frequency;

	QueryPerformanceFrequency ( &frequency );

	int64_t left = frame_due - clock_now ( );

	if ( left <= 0 )
		return 0;

	return static_cast < DWORD > ( base::min ( left * 1000 / frequency.QuadPart + 1, static_cast < int64_t > ( 250 ) ) );

}

static void schedule_frame ( int32_t rate ) {

	LARGE_INTEGER frequency;
	DWM_TIMING_INFO timing = { };
	int64_t now = clock_now ( );

	QueryPerformanceFrequency ( &frequency );
	timing.cbSize = sizeof ( timing );

	int64_t period = frequency.QuadPart / base::max ( refresh_rate, 30 );
	int64_t vblank = now;

	if ( SUCCEEDED ( DwmGetCompositionTimingInfo ( nullptr, &timing ) ) && timing.qpcRefreshPeriod > 0 ) {

		period = static_cast < int64_t > ( timing.qpcRefreshPeriod );
		vblank = static_cast < int64_t > ( timing.qpcVBlank );

	}

	int64_t every = rate > 0 ? base::clamp ( ( refresh_rate + rate / 2 ) / rate, 1, 16 ) : 1;
	int64_t earliest = base::max ( now + period / 20, frame_last + every * period - period / 2 );
	int64_t target = vblank;

	if ( target < earliest )
		target += ( ( earliest - target ) / period + 1 ) * period;

	bool fine = rate == 0;

	if ( fine != fine_timer ) {

		if ( fine )
			timeBeginPeriod ( 1 );
		else
			timeEndPeriod ( 1 );

		fine_timer = fine;

	}

	if ( frame_armed && frame_due <= target + period / 4 )
		return;

	LARGE_INTEGER due;

	due.QuadPart = -base::max ( static_cast < int64_t > ( static_cast < double > ( target - now ) * 10000000.0 / static_cast < double > ( frequency.QuadPart ) ), static_cast < int64_t > ( 1 ) );
	SetWaitableTimer ( frame_timer, &due, 0, nullptr, nullptr, FALSE );
	frame_armed = true;
	frame_due = target;

#ifdef PEARL_DEBUG

	trace_period = static_cast < double > ( period ) * 1000.0 / static_cast < double > ( frequency.QuadPart );
	trace_delay = static_cast < double > ( target - now ) * 1000.0 / static_cast < double > ( frequency.QuadPart );
	trace_rate = rate;
	trace_arms++;

#endif

}

#ifdef PEARL_DEBUG

static const wchar_t* trace_path = nullptr;
static double trace_start = -1.0;
static double trace_toggle = 0.0;
static double trace_last = 0.0;
static base::list < char > trace_out;

static DWORD __stdcall trace_mouse ( void* ) {

	for ( int32_t i = 0; trace_path; i++ ) {

		PostMessageW ( main_window, WM_MOUSEMOVE, 0, MAKELPARAM ( 900 + ( i % 200 ), 500 + ( i % 37 ) ) );
		Sleep ( 1 );

	}

	return 0;

}

static void trace_frame ( double begin, double work, double present_time ) {

	if ( trace_start < 0.0 ) {

		if ( GetEnvironmentVariableW ( L"PEARL_TRACE_MOUSE", nullptr, 0 ) )
			CreateThread ( nullptr, 0, &trace_mouse, nullptr, 0, nullptr );


		trace_start = begin;
		trace_toggle = begin + 1.5;
		trace_last = begin;
	trace_wakes = 0;
	trace_arms = 0;
	trace_message = 0;
	trace_events.clear ( );

	}

	base::writer line;

	line.fixed ( ( begin - trace_start ) * 1000.0, 1 ).add ( L" gap " ).fixed ( ( begin - trace_last ) * 1000.0, 2 ).add ( L" frame " ).fixed ( work * 1000.0, 2 ).add ( L" present " ).fixed ( present_time * 1000.0, 2 ).add ( L" period " ).fixed ( trace_period, 2 ).add ( L" delay " ).fixed ( trace_delay, 2 ).add ( L" rate " ).number ( trace_rate ).add ( L" wakes " ).number ( trace_wakes ).add ( L" arms " ).number ( trace_arms ).add ( L" msg " ).number ( trace_message ).add ( L" [" ).add ( trace_events.text ( ) ).add ( L"]" ).add ( program->busy ( ) ? L" busy" : L" idle" ).add ( program->steady ( ) ? L" steady\n" : L"\n" );

	for ( int32_t i = 0; i < line.size ( ); i++ )
		trace_out.push ( static_cast < char > ( line.text ( ) [ i ] ) );

	trace_last = begin;
	trace_wakes = 0;
	trace_arms = 0;
	trace_message = 0;
	trace_events.clear ( );

	if ( begin >= trace_toggle ) {

		const char* mark = "TOGGLE\n";

		for ( int32_t i = 0; mark [ i ]; i++ )
			trace_out.push ( mark [ i ] );

		program->toggle_now ( );
		program->pop_in ( );
		trace_toggle = begin + 1.5;

	}

	if ( begin - trace_start > 12.0 ) {

		files::write ( trace_path, trace_out.data ( ), trace_out.count ( ) );
		trace_path = nullptr;
		PostMessageW ( main_window, WM_CLOSE, 0, 0 );

	}

}

#endif

static void render_main ( ) {

	if ( device_lost )
		return;

#ifdef PEARL_DEBUG

	double begin = base::seconds ( );

	program->frame ( begin );

	double built = base::seconds ( );

	device->present ( main_output, device->chrome );

	if ( trace_path )
		trace_frame ( begin, built - begin, base::seconds ( ) - built );

#else

	program->frame ( base::seconds ( ) );
	device->present ( main_output, device->chrome );

#endif

	switch ( program->take_request ( ) ) {

		case ui::request::MINIMIZE:
			PostMessageW ( main_window, WM_SYSCOMMAND, SC_MINIMIZE, 0 );
			break;

		case ui::request::MAXIMIZE:
			ShowWindow ( main_window, IsZoomed ( main_window ) ? SW_RESTORE : SW_MAXIMIZE );
			break;

		case ui::request::CLOSE:
			PostMessageW ( main_window, WM_CLOSE, 0, 0 );
			break;

		default:
			break;

	}

}

static void begin_show ( );

static float mini_scale ( ) {

	return static_cast < float > ( window_dpi ( mini_window ) ) / 96.0f;

}

static void mini_size ( int32_t& width, int32_t& height ) {

	float scale = mini_scale ( );

	width = static_cast < int32_t > ( ( mini_pill_w + mini_margin * 2.0f ) * scale );
	height = static_cast < int32_t > ( ( mini_pill_h + mini_margin * 2.0f ) * scale );

}

static POINT mini_anchor_position ( int32_t anchor, HMONITOR monitor ) {

	MONITORINFO info = { };
	int32_t width = 0;
	int32_t height = 0;
	float scale = mini_scale ( );

	info.cbSize = sizeof ( info );
	GetMonitorInfoW ( monitor, &info );
	mini_size ( width, height );

	RECT work = info.rcWork;
	int32_t gap = static_cast < int32_t > ( 14.0f * scale ) - static_cast < int32_t > ( mini_margin * scale );
	int32_t left = work.left + gap;
	int32_t right = work.right - gap - width;
	int32_t middle_x = ( work.left + work.right - width ) / 2;
	int32_t top = work.top + gap;
	int32_t bottom = work.bottom - gap - height;
	int32_t middle_y = ( work.top + work.bottom - height ) / 2;
	POINT positions [ 8 ] = { { right, top }, { middle_x, top }, { left, top }, { left, middle_y }, { left, bottom }, { middle_x, bottom }, { right, bottom }, { right, middle_y } };

	return positions [ base::clamp ( anchor, 0, 7 ) ];

}

static float anchor_align ( int32_t anchor ) {

	if ( anchor == 1 || anchor == 5 )
		return 0.5f;

	return anchor >= 2 && anchor <= 4 ? 0.0f : 1.0f;

}

static void shape_mini ( ) {

	static int32_t shaped = -1;
	static RECT last = { };
	float scale = mini_scale ( );
	bool expanded = program->mini_hover > 0.001f || program->mini_lifted || program->mouse_down;

	if ( expanded ) {

		if ( shaped != 1 ) {

			SetWindowRgn ( mini_window, nullptr, TRUE );
			shaped = 1;

		}

		return;

	}

	float pad = 22.0f;
	RECT area = { static_cast < LONG > ( ( program->mini_rect.x - pad ) * scale ), static_cast < LONG > ( ( program->mini_rect.y - pad ) * scale ), static_cast < LONG > ( ( program->mini_rect.right ( ) + pad ) * scale ) + 1, static_cast < LONG > ( ( program->mini_rect.bottom ( ) + pad ) * scale ) + 1 };

	if ( shaped == 0 && area.left == last.left && area.right == last.right && area.top == last.top && area.bottom == last.bottom )
		return;

	int32_t round = static_cast < int32_t > ( ( program->mini_rect.h + pad * 2.0f ) * scale );

	SetWindowRgn ( mini_window, CreateRoundRectRgn ( area.left, area.top, area.right, area.bottom, round, round ), TRUE );
	last = area;
	shaped = 0;

}

static void render_mini ( ) {

	if ( !mini_on || device_lost )
		return;

	RECT client = { };

	GetClientRect ( mini_window, &client );
	program->frame_mini ( base::seconds ( ), mini_surface, client.right, client.bottom, mini_scale ( ), mini_margin );
	shape_mini ( );
	device->present ( mini_output, mini_surface );

	if ( program->take_request ( ) == ui::request::RESTORE && window_phase == phase::HIDDEN )
		begin_show ( );

	if ( program->mini_settled ( ) ) {

		ShowWindow ( mini_window, SW_HIDE );
		mini_on = false;
		mini_snapping = false;

	}

}

static void fit_mini ( ) {

	int32_t width = 0;
	int32_t height = 0;

	mini_size ( width, height );
	device->resize ( mini_output, width, height );

	if ( mini_surface.width < width || mini_surface.height < height )
		device->make_surface ( mini_surface, width, height, true );

}

static void recover_device ( ) {

	RECT client = { };
	render::gpu* fresh = new render::gpu ( );

	device_retry = base::seconds ( ) + 1.0;
	device->release ( main_output );
	device->release ( overlay_output );
	device->release ( mini_output );
	device->free_surface ( mini_surface );
	device->free_surface ( overlay_surface );
	GetClientRect ( main_window, &client );

	if ( !fresh->setup ( software_only ) || !fresh->attach ( main_output, main_window, client.right, client.bottom ) || !fresh->attach ( overlay_output, overlay_window, 64, 64 ) || !fresh->attach ( mini_output, mini_window, 64, 64 ) ) {

		fresh->release ( main_output );
		fresh->release ( overlay_output );
		fresh->release ( mini_output );
		fresh->shutdown ( );
		delete fresh;

		return;

	}

	program->rebuild_gpu ( fresh );
	device->shutdown ( );
	delete device;
	device = fresh;
	device_lost = false;
	sync_size ( );

	if ( mini_on )
		fit_mini ( );

	if ( window_phase == phase::SHOWN )
		render_main ( );

}

static void show_mini ( ) {

	if ( !mini_window || program->track < 0 )
		return;

	if ( !mini_on ) {

		HMONITOR monitor = MonitorFromWindow ( main_window, MONITOR_DEFAULTTONEAREST );
		POINT at = mini_anchor_position ( program->mini_anchor, monitor );
		int32_t width = 0;
		int32_t height = 0;

		mini_size ( width, height );
		SetWindowPos ( mini_window, HWND_TOPMOST, at.x, at.y, width, height, SWP_NOACTIVATE );
		at = mini_anchor_position ( program->mini_anchor, monitor );
		mini_size ( width, height );
		SetWindowPos ( mini_window, HWND_TOPMOST, at.x, at.y, width, height, SWP_NOACTIVATE );
		fit_mini ( );
		mini_x = static_cast < float > ( at.x );
		mini_y = static_cast < float > ( at.y );
		mini_snapping = false;
		program->mini_align_target = anchor_align ( program->mini_anchor );
		program->mini_align = program->mini_align_target;
		ShowWindow ( mini_window, SW_SHOWNOACTIVATE );
		mini_on = true;

	}

	program->open_mini ( true );
	render_mini ( );

}

static void hide_mini ( ) {

	if ( mini_on )
		program->open_mini ( false );

}

static void step_snap ( ) {

	double now = base::seconds ( );
	float elapsed = static_cast < float > ( base::clamp ( now - mini_step_time, 0.0, 0.04 ) );

	mini_step_time = now;

	int32_t steps = base::max ( static_cast < int32_t > ( elapsed * 480.0f ), 1 );
	float step = elapsed / static_cast < float > ( steps );

	for ( int32_t i = 0; i < steps; i++ ) {

		mini_vx += ( 190.0f * ( mini_goal_x - mini_x ) - 21.0f * mini_vx ) * step;
		mini_vy += ( 190.0f * ( mini_goal_y - mini_y ) - 21.0f * mini_vy ) * step;
		mini_x += mini_vx * step;
		mini_y += mini_vy * step;

	}

	if ( math::abs ( mini_goal_x - mini_x ) < 0.5f && math::abs ( mini_goal_y - mini_y ) < 0.5f && math::abs ( mini_vx ) + math::abs ( mini_vy ) < 20.0f ) {

		mini_x = mini_goal_x;
		mini_y = mini_goal_y;
		mini_snapping = false;

	}

	SetWindowPos ( mini_window, nullptr, static_cast < int32_t > ( math::round ( mini_x ) ), static_cast < int32_t > ( math::round ( mini_y ) ), 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );

}

static void begin_snap ( ) {

	RECT area = { };

	GetWindowRect ( mini_window, &area );

	float width = static_cast < float > ( area.right - area.left );
	float height = static_cast < float > ( area.bottom - area.top );
	float center_x = static_cast < float > ( area.left ) + width * 0.5f + mini_fling_x * 0.16f;
	float center_y = static_cast < float > ( area.top ) + height * 0.5f + mini_fling_y * 0.16f;
	POINT probe = { static_cast < LONG > ( center_x ), static_cast < LONG > ( center_y ) };
	HMONITOR monitor = MonitorFromPoint ( probe, MONITOR_DEFAULTTONEAREST );
	float best = 1e30f;
	int32_t choice = program->mini_anchor;

	for ( int32_t i = 0; i < 8; i++ ) {

		POINT at = mini_anchor_position ( i, monitor );
		float dx = static_cast < float > ( at.x ) + width * 0.5f - center_x;
		float dy = static_cast < float > ( at.y ) + height * 0.5f - center_y;
		float distance = dx * dx + dy * dy;

		if ( distance < best ) {

			best = distance;
			choice = i;

		}

	}

	POINT goal = mini_anchor_position ( choice, monitor );

	program->mini_anchor = choice;
	program->mini_align_target = anchor_align ( choice );
	mini_goal_x = static_cast < float > ( goal.x );
	mini_goal_y = static_cast < float > ( goal.y );
	mini_x = static_cast < float > ( area.left );
	mini_y = static_cast < float > ( area.top );
	mini_vx = base::clamp ( mini_fling_x, -6000.0f, 6000.0f ) * 0.6f;
	mini_vy = base::clamp ( mini_fling_y, -6000.0f, 6000.0f ) * 0.6f;
	mini_step_time = base::seconds ( );
	mini_snapping = true;

}

static void sync_mini_cursor ( ) {

	if ( !mini_on || !program->mini_mode || window_phase != phase::HIDDEN )
		return;

	POINT at = { };
	RECT area = { };

	GetCursorPos ( &at );
	GetWindowRect ( mini_window, &area );

	float scale = mini_scale ( );
	float pad = 14.0f;
	float x = static_cast < float > ( at.x - area.left );
	float y = static_cast < float > ( at.y - area.top );
	math::rect pill = program->mini_rect;
	bool inside = x >= ( pill.x - pad ) * scale && x < ( pill.right ( ) + pad ) * scale && y >= ( pill.y - pad - 6.0f ) * scale && y < ( pill.bottom ( ) + pad ) * scale;

	static float last_x = -1.0f;
	static float last_y = -1.0f;

	if ( inside ) {

		if ( x != last_x || y != last_y || !program->mouse_inside )
			program->mouse_move ( x, y );

		last_x = x;
		last_y = y;

	} else if ( program->mouse_inside && !program->mouse_down )
		program->mouse_leave ( );

}

static LRESULT __stdcall mini_procedure ( HWND window, UINT message, WPARAM wide, LPARAM extra ) {

	if ( !program )
		return DefWindowProcW ( window, message, wide, extra );

	switch ( message ) {

		case WM_NCHITTEST: {

			POINT at = { static_cast < int16_t > ( LOWORD ( extra ) ), static_cast < int16_t > ( HIWORD ( extra ) ) };

			ScreenToClient ( window, &at );

			ui::hit kind = program->hit_test ( static_cast < float > ( at.x ), static_cast < float > ( at.y ) );

			if ( kind == ui::hit::CLIENT || kind == ui::hit::MAXIMIZE )
				return HTCLIENT;

			if ( kind == ui::hit::CAPTION )
				return HTCAPTION;

			return HTTRANSPARENT;

		}

		case WM_MOUSEACTIVATE:
			return MA_NOACTIVATE;

		case WM_MOUSEMOVE:

			if ( !mini_tracking ) {

				TRACKMOUSEEVENT track = { };

				track.cbSize = sizeof ( track );
				track.dwFlags = TME_LEAVE;
				track.hwndTrack = window;
				TrackMouseEvent ( &track );
				mini_tracking = true;

			}

			program->mouse_move ( static_cast < float > ( static_cast < int16_t > ( LOWORD ( extra ) ) ), static_cast < float > ( static_cast < int16_t > ( HIWORD ( extra ) ) ) );

			return 0;

		case WM_NCMOUSEMOVE: {

			POINT at = { static_cast < int16_t > ( LOWORD ( extra ) ), static_cast < int16_t > ( HIWORD ( extra ) ) };

			if ( !mini_frame_tracking ) {

				TRACKMOUSEEVENT track = { };

				track.cbSize = sizeof ( track );
				track.dwFlags = TME_LEAVE | TME_NONCLIENT;
				track.hwndTrack = window;
				TrackMouseEvent ( &track );
				mini_frame_tracking = true;

			}

			ScreenToClient ( window, &at );
			program->mouse_move ( static_cast < float > ( at.x ), static_cast < float > ( at.y ) );

			break;

		}

		case WM_MOUSELEAVE:
			mini_tracking = false;
			sync_mini_cursor ( );
			return 0;

		case WM_NCMOUSELEAVE:
			mini_frame_tracking = false;
			sync_mini_cursor ( );
			break;

		case WM_LBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
			SetCapture ( window );
			program->mouse_button ( 0, true );
			return 0;

		case WM_LBUTTONUP:
			ReleaseCapture ( );
			program->mouse_button ( 0, false );
			return 0;

		case WM_MOUSEWHEEL:
			program->mouse_wheel ( static_cast < float > ( static_cast < int16_t > ( HIWORD ( wide ) ) ) / 120.0f );
			return 0;

		case WM_ENTERSIZEMOVE: {

			RECT area = { };

			GetWindowRect ( window, &area );
			mini_snapping = false;
			mini_last_position = { area.left, area.top };
			mini_last_time = base::seconds ( );
			mini_fling_x = 0.0f;
			mini_fling_y = 0.0f;
			program->mini_lifted = true;
			SetTimer ( window, mini_timer, 8, nullptr );

			break;

		}

		case WM_MOVING: {

			RECT* area = reinterpret_cast < RECT* > ( extra );
			double now = base::seconds ( );
			float elapsed = static_cast < float > ( now - mini_last_time );

			if ( elapsed > 0.001f ) {

				float vx = static_cast < float > ( area->left - mini_last_position.x ) / elapsed;
				float vy = static_cast < float > ( area->top - mini_last_position.y ) / elapsed;

				mini_fling_x += ( vx - mini_fling_x ) * 0.45f;
				mini_fling_y += ( vy - mini_fling_y ) * 0.45f;
				mini_last_position = { area->left, area->top };
				mini_last_time = now;

			}

			break;

		}

		case WM_EXITSIZEMOVE:
			KillTimer ( window, mini_timer );
			program->mini_lifted = false;

			if ( base::seconds ( ) - mini_last_time > 0.08 ) {

				mini_fling_x = 0.0f;
				mini_fling_y = 0.0f;

			}

			begin_snap ( );
			break;

		case WM_TIMER:

			if ( wide == mini_timer )
				render_mini ( );

			return 0;

		case WM_DPICHANGED:
			fit_mini ( );
			break;

		case WM_SETCURSOR:
			SetCursor ( LoadCursorW ( nullptr, IDC_ARROW ) );
			return TRUE;

		default:
			break;

	}

	return DefWindowProcW ( window, message, wide, extra );

}

static void place_overlay ( ) {

	POINT origin = { 0, 0 };
	RECT client = { };

	ClientToScreen ( main_window, &origin );
	GetClientRect ( main_window, &client );
	overlay_margin = static_cast < int32_t > ( 72.0f * static_cast < float > ( window_dpi ( main_window ) ) / 96.0f );

	int32_t width = client.right + overlay_margin * 2;
	int32_t height = client.bottom + overlay_margin * 2;

	SetWindowPos ( overlay_window, HWND_TOPMOST, origin.x - overlay_margin, origin.y - overlay_margin, width, height, SWP_NOACTIVATE );
	device->resize ( overlay_output, width, height );

	if ( overlay_surface.width < width || overlay_surface.height < height || overlay_surface.width > width + 256 || overlay_surface.height > height + 256 )
		device->make_surface ( overlay_surface, width, height, false );

}

static void draw_overlay ( float scale, float alpha ) {

	if ( device_lost )
		return;

	overlay_canvas->begin ( device, 1.0f, math::vec2 ( 0.0f, 0.0f ), 0.0f );
	program->draw_snapshot ( *overlay_canvas, static_cast < float > ( overlay_margin ), scale, alpha, IsZoomed ( main_window ) != FALSE );
	device->draw ( *overlay_canvas, overlay_surface, &device->chrome, true, render::color ( 0.0f, 0.0f, 0.0f, 0.0f ) );
	device->present ( overlay_output, overlay_surface );

}

static void begin_hide ( bool close ) {

	if ( window_phase == phase::HIDDEN && close ) {

		DestroyWindow ( main_window );

		return;

	}

	if ( window_phase != phase::SHOWN )
		return;

	place_overlay ( );
	draw_overlay ( 1.0f, 1.0f );
	ShowWindow ( overlay_window, SW_SHOWNOACTIVATE );
	device->commit ( );
	DwmFlush ( );
	cloak ( true );
	window_phase = phase::HIDING;
	phase_close = close;
	phase_start = base::seconds ( );

}

static void finish_hide ( ) {

	draw_overlay ( 0.9f, 0.0f );
	ShowWindow ( overlay_window, SW_HIDE );

	if ( phase_close ) {

		window_phase = phase::HIDDEN;
		DestroyWindow ( main_window );

		return;

	}

	transitions ( false );
	window_phase = phase::HIDDEN;
	ShowWindow ( main_window, SW_MINIMIZE );
	cloak ( false );

	if ( hide_to_mini )
		show_mini ( );

	hide_to_mini = false;

}

static void begin_show ( ) {

	if ( window_phase == phase::SHOWN || window_phase == phase::SHOWING )
		return;

	window_phase = phase::SHOWING;
	hide_mini ( );
	cloak ( true );
	transitions ( false );

	if ( IsIconic ( main_window ) )
		ShowWindow ( main_window, SW_RESTORE );
	else if ( !IsWindowVisible ( main_window ) )
		ShowWindow ( main_window, start_maximized ? SW_SHOWMAXIMIZED : SW_SHOW );

	SetForegroundWindow ( main_window );
	sync_size ( );
	program->pending = 3;
	render_main ( );
	place_overlay ( );
	draw_overlay ( 0.9f, 0.0f );
	ShowWindow ( overlay_window, SW_SHOWNOACTIVATE );
	phase_start = base::seconds ( );

}

static void finish_show ( ) {

	render_main ( );
	draw_overlay ( 1.0f, 1.0f );
	DwmFlush ( );
	cloak ( false );
	DwmFlush ( );
	ShowWindow ( overlay_window, SW_HIDE );
	transitions ( true );
	window_phase = phase::SHOWN;

}

static void step_overlay ( ) {

	float elapsed = static_cast < float > ( base::seconds ( ) - phase_start );

	if ( window_phase == phase::HIDING ) {

		float t = elapsed / 0.2f;

		if ( t >= 1.0f ) {

			finish_hide ( );

			return;

		}

		float e = t * t * ( 3.0f - 2.0f * t );

		draw_overlay ( 1.0f - 0.08f * e, 1.0f - math::smoothstep ( 0.05f, 1.0f, t ) );

	} else if ( window_phase == phase::SHOWING ) {

		float t = elapsed / 0.38f;

		if ( t >= 1.0f ) {

			finish_show ( );

			return;

		}

		float u = t - 1.0f;
		float back = 1.0f + 2.1f * u * u * u + 1.1f * u * u;

		draw_overlay ( 0.9f + 0.1f * back, math::smoothstep ( 0.0f, 0.55f, t ) );

	}

}

static void toggle_visibility ( ) {

	if ( window_phase == phase::SHOWN ) {

		if ( GetForegroundWindow ( ) == main_window ) {

			hide_to_mini = true;
			begin_hide ( false );

		}
		else {

			SetForegroundWindow ( main_window );
			program->pop_in ( );

		}

	} else if ( window_phase == phase::HIDDEN ) {

		if ( mini_on && program->mini_target )
			hide_mini ( );
		else
			show_mini ( );

	}

}

static LRESULT __stdcall overlay_procedure ( HWND window, UINT message, WPARAM wide, LPARAM extra ) {

	if ( message == WM_NCHITTEST )
		return HTTRANSPARENT;

	if ( message == WM_MOUSEACTIVATE )
		return MA_NOACTIVATE;

	return DefWindowProcW ( window, message, wide, extra );

}

static LRESULT __stdcall procedure ( HWND window, UINT message, WPARAM wide, LPARAM extra ) {

	if ( message == WM_NCCALCSIZE && wide ) {

		NCCALCSIZE_PARAMS* sizes = reinterpret_cast < NCCALCSIZE_PARAMS* > ( extra );
		LONG top = sizes->rgrc [ 0 ].top;
		LRESULT result = DefWindowProcW ( window, message, wide, extra );

		sizes->rgrc [ 0 ].top = top + ( IsZoomed ( window ) ? frame_thickness ( window ) : 0 );

		return result;

	}

	if ( !program || window != main_window )
		return DefWindowProcW ( window, message, wide, extra );

	switch ( message ) {

		case WM_NCHITTEST: {

			LRESULT edge = DefWindowProcW ( window, message, wide, extra );

			if ( edge != HTCLIENT )
				return edge;

			POINT at = { static_cast < int16_t > ( LOWORD ( extra ) ), static_cast < int16_t > ( HIWORD ( extra ) ) };
			RECT client = { };

			ScreenToClient ( window, &at );
			GetClientRect ( window, &client );

			ui::hit kind = program->hit_test ( static_cast < float > ( at.x ), static_cast < float > ( at.y ) );
			int32_t frame = frame_thickness ( window );

			if ( kind == ui::hit::MAXIMIZE )
				return HTMAXBUTTON;

			if ( kind == ui::hit::CLIENT )
				return HTCLIENT;

			if ( !IsZoomed ( window ) && at.y < frame ) {

				if ( at.x < frame * 2 )
					return HTTOPLEFT;

				if ( at.x > client.right - frame * 2 )
					return HTTOPRIGHT;

				return HTTOP;

			}

			return kind == ui::hit::CAPTION ? HTCAPTION : HTCLIENT;

		}

		case WM_NCMOUSEMOVE:

			if ( wide == HTMAXBUTTON ) {

				POINT at = { static_cast < int16_t > ( LOWORD ( extra ) ), static_cast < int16_t > ( HIWORD ( extra ) ) };

				if ( !frame_tracking ) {

					TRACKMOUSEEVENT track = { };

					track.cbSize = sizeof ( track );
					track.dwFlags = TME_LEAVE | TME_NONCLIENT;
					track.hwndTrack = window;
					TrackMouseEvent ( &track );
					frame_tracking = true;

				}

				ScreenToClient ( window, &at );
				program->mouse_move ( static_cast < float > ( at.x ), static_cast < float > ( at.y ) );

				return 0;

			}

			break;

		case WM_NCMOUSELEAVE:
			frame_tracking = false;
			program->mouse_leave ( );
			break;

		case WM_NCLBUTTONDOWN:
		case WM_NCLBUTTONDBLCLK:

			if ( wide == HTMAXBUTTON ) {

				program->mouse_button ( 0, true );

				return 0;

			}

			break;

		case WM_NCLBUTTONUP:

			if ( wide == HTMAXBUTTON ) {

				program->mouse_button ( 0, false );

				return 0;

			}

			break;

		case WM_MOUSEMOVE:

			if ( !tracking ) {

				TRACKMOUSEEVENT track = { };

				track.cbSize = sizeof ( track );
				track.dwFlags = TME_LEAVE;
				track.hwndTrack = window;
				TrackMouseEvent ( &track );
				tracking = true;

			}

			program->mouse_move ( static_cast < float > ( static_cast < int16_t > ( LOWORD ( extra ) ) ), static_cast < float > ( static_cast < int16_t > ( HIWORD ( extra ) ) ) );

			return 0;

		case WM_MOUSELEAVE:
			tracking = false;
			program->mouse_leave ( );
			return 0;

		case WM_LBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
			SetCapture ( window );
			program->mouse_button ( 0, true );
			return 0;

		case WM_LBUTTONUP:
			ReleaseCapture ( );
			program->mouse_button ( 0, false );
			return 0;

		case WM_RBUTTONDOWN:
			program->mouse_button ( 1, true );
			return 0;

		case WM_MOUSEWHEEL:
			program->mouse_wheel ( static_cast < float > ( static_cast < int16_t > ( HIWORD ( wide ) ) ) / 120.0f );
			return 0;

		case WM_CHAR:
			program->character ( static_cast < wchar_t > ( wide ) );
			return 0;

		case WM_ENDSESSION:

			if ( wide && program->persist ) {

				program->save_state ( );
				files::settle ( );

			}

			return 0;

		case spotify_message:
			program->pop_in ( );
			return 0;

		case WM_KEYDOWN:
			program->key ( static_cast < uint32_t > ( wide ), ( GetKeyState ( VK_CONTROL ) & 0x8000 ) != 0, ( GetKeyState ( VK_SHIFT ) & 0x8000 ) != 0 );
			return 0;

		case WM_APPCOMMAND: {

			int32_t command = GET_APPCOMMAND_LPARAM ( extra );

			if ( command == APPCOMMAND_MEDIA_PLAY_PAUSE || command == APPCOMMAND_MEDIA_PLAY || command == APPCOMMAND_MEDIA_PAUSE )
				program->key ( VK_MEDIA_PLAY_PAUSE, false, false );
			else if ( command == APPCOMMAND_MEDIA_NEXTTRACK )
				program->key ( VK_MEDIA_NEXT_TRACK, false, false );
			else if ( command == APPCOMMAND_MEDIA_PREVIOUSTRACK )
				program->key ( VK_MEDIA_PREV_TRACK, false, false );
			else if ( command == APPCOMMAND_BROWSER_BACKWARD )
				program->key ( VK_BROWSER_BACK, false, false );
			else
				break;

			return TRUE;

		}

		case WM_HOTKEY:

			if ( wide == 1 )
				toggle_visibility ( );

			return 0;

		case show_message:

			if ( window_phase == phase::HIDDEN )
				begin_show ( );
			else
				SetForegroundWindow ( window );

			return 0;

		case WM_SIZE:

			if ( wide == SIZE_MINIMIZED )
				return 0;

			sync_size ( );

			if ( window_phase == phase::HIDDEN && IsWindowVisible ( window ) ) {

				hide_mini ( );
				window_phase = phase::SHOWN;
				cloak ( false );
				transitions ( true );
				program->pop_in ( );

			}

			if ( window_phase == phase::SHOWN || window_phase == phase::SHOWING )
				render_main ( );

			return 0;

		case WM_DISPLAYCHANGE:
			measure_refresh ( );
			break;

		case WM_WINDOWPOSCHANGED:
			measure_refresh ( );
			break;

		case WM_DPICHANGED: {

			RECT* suggested = reinterpret_cast < RECT* > ( extra );

			SetWindowPos ( window, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE );

			return 0;

		}

		case WM_GETMINMAXINFO: {

			MINMAXINFO* info = reinterpret_cast < MINMAXINFO* > ( extra );
			float scale = static_cast < float > ( window_dpi ( window ) ) / 96.0f;
			MONITORINFO monitor = { };

			monitor.cbSize = sizeof ( monitor );
			info->ptMinTrackSize.x = static_cast < LONG > ( 940.0f * scale );
			info->ptMinTrackSize.y = static_cast < LONG > ( 620.0f * scale );

			if ( GetMonitorInfoW ( MonitorFromWindow ( window, MONITOR_DEFAULTTONEAREST ), &monitor ) ) {

				info->ptMinTrackSize.x = base::min ( info->ptMinTrackSize.x, monitor.rcWork.right - monitor.rcWork.left );
				info->ptMinTrackSize.y = base::min ( info->ptMinTrackSize.y, monitor.rcWork.bottom - monitor.rcWork.top );

			}

			return 0;

		}

		case WM_ACTIVATE:
			program->set_focus ( LOWORD ( wide ) != WA_INACTIVE );
			break;

		case WM_SYSCOMMAND:

			if ( ( wide & 0xFFF0 ) == SC_MINIMIZE ) {

				begin_hide ( false );

				return 0;

			}

			if ( ( wide & 0xFFF0 ) == SC_RESTORE && IsIconic ( window ) ) {

				begin_show ( );

				return 0;

			}

			break;

		case WM_CLOSE:
			begin_hide ( true );
			return 0;

		case WM_DESTROY: {

			WINDOWPLACEMENT place = { };

			place.length = sizeof ( place );

			if ( GetWindowPlacement ( window, &place ) ) {

				program->placement [ 0 ] = place.rcNormalPosition.left;
				program->placement [ 1 ] = place.rcNormalPosition.top;
				program->placement [ 2 ] = place.rcNormalPosition.right - place.rcNormalPosition.left;
				program->placement [ 3 ] = place.rcNormalPosition.bottom - place.rcNormalPosition.top;
				program->placement_maximized = IsZoomed ( window ) != FALSE || ( place.flags & WPF_RESTORETOMAXIMIZED ) != 0;
				program->placement_valid = true;

			}

			PostQuitMessage ( 0 );

			return 0;

		}

		case WM_ENTERSIZEMOVE:
			SetTimer ( window, resize_timer, 15, nullptr );
			break;

		case WM_EXITSIZEMOVE:
			KillTimer ( window, resize_timer );
			break;

		case WM_TIMER:

			if ( wide == resize_timer && window_phase == phase::SHOWN && program->busy ( ) )
				render_main ( );

			return 0;

		case WM_ERASEBKGND:
			return 1;

		case WM_PAINT:
			ValidateRect ( window, nullptr );

			if ( window_phase == phase::SHOWN )
				render_main ( );

			return 0;

		case WM_SETCURSOR:

			if ( LOWORD ( extra ) == HTCLIENT ) {

				SetCursor ( LoadCursorW ( nullptr, IDC_ARROW ) );

				return TRUE;

			}

			break;

		default:
			break;

	}

	return DefWindowProcW ( window, message, wide, extra );

}

int program_main ( ) {

	load_dpi ( );
	CoInitializeEx ( nullptr, COINIT_APARTMENTTHREADED );

#ifdef PEARL_DEBUG

	if ( debug_main ( ) ) {

		CoUninitialize ( );

		return 0;

	}

#endif

	HANDLE single = CreateMutexW ( nullptr, TRUE, L"pearl_music_player_instance" );

	if ( single && GetLastError ( ) == ERROR_ALREADY_EXISTS ) {

		HWND existing = FindWindowW ( L"pearl", nullptr );

		if ( existing ) {

			AllowSetForegroundWindow ( ASFW_ANY );
			PostMessageW ( existing, show_message, 0, 0 );

		}

		return 0;

	}

	int32_t switch_count = 0;
	wchar_t** switches = CommandLineToArgvW ( GetCommandLineW ( ), &switch_count );

	for ( int32_t i = 1; switches && i < switch_count; i++ )
		software_only = software_only || base::same_text ( switches [ i ], L"--software" );

	if ( switches )
		LocalFree ( switches );

	device = new render::gpu ( );

	if ( !device->setup ( software_only ) ) {

		MessageBoxW ( nullptr, L"Pearl could not start its renderer.", L"Pearl", MB_ICONERROR );

		return 1;

	}

	HINSTANCE instance = GetModuleHandleW ( nullptr );
	WNDCLASSEXW type = { };

	type.cbSize = sizeof ( type );
	type.lpfnWndProc = &procedure;
	type.hInstance = instance;
	type.hCursor = LoadCursorW ( nullptr, IDC_ARROW );
	type.hIcon = LoadIconW ( instance, MAKEINTRESOURCEW ( 1 ) );
	type.hIconSm = static_cast < HICON > ( LoadImageW ( instance, MAKEINTRESOURCEW ( 1 ), IMAGE_ICON, GetSystemMetrics ( SM_CXSMICON ), GetSystemMetrics ( SM_CYSMICON ), 0 ) );
	type.lpszClassName = L"pearl";
	RegisterClassExW ( &type );

	WNDCLASSEXW overlay_type = { };

	overlay_type.cbSize = sizeof ( overlay_type );
	overlay_type.lpfnWndProc = &overlay_procedure;
	overlay_type.hInstance = instance;
	overlay_type.hCursor = LoadCursorW ( nullptr, IDC_ARROW );
	overlay_type.lpszClassName = L"pearl_overlay";
	RegisterClassExW ( &overlay_type );

	WNDCLASSEXW mini_type = { };

	mini_type.cbSize = sizeof ( mini_type );
	mini_type.lpfnWndProc = &mini_procedure;
	mini_type.hInstance = instance;
	mini_type.hCursor = LoadCursorW ( nullptr, IDC_ARROW );
	mini_type.style = CS_DBLCLKS;
	mini_type.lpszClassName = L"pearl_mini";
	RegisterClassExW ( &mini_type );

	float scale = static_cast < float > ( system_dpi ( ) ) / 96.0f;
	RECT work = { };

	SystemParametersInfoW ( SPI_GETWORKAREA, 0, &work, 0 );

	int32_t frame = scaled_metric ( SM_CXFRAME, system_dpi ( ) ) + scaled_metric ( SM_CXPADDEDBORDER, system_dpi ( ) );
	int32_t client_w = base::min ( static_cast < int32_t > ( 1240.0f * scale ), static_cast < int32_t > ( work.right - work.left ) - 60 );
	int32_t client_h = base::min ( static_cast < int32_t > ( 800.0f * scale ), static_cast < int32_t > ( work.bottom - work.top ) - 60 );
	int32_t left = work.left + ( work.right - work.left - client_w ) / 2 - frame;
	int32_t top = work.top + ( work.bottom - work.top - client_h ) / 2;

	main_window = CreateWindowExW ( WS_EX_NOREDIRECTIONBITMAP | WS_EX_APPWINDOW, L"pearl", L"Pearl", WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX, left, top, client_w + frame * 2, client_h + frame, nullptr, nullptr, instance, nullptr );
	overlay_window = CreateWindowExW ( WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, L"pearl_overlay", L"", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, instance, nullptr );

	mini_window = CreateWindowExW ( WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, L"pearl_mini", L"Pearl", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, instance, nullptr );

	if ( !main_window || !overlay_window || !mini_window )
		return 1;

	MARGINS margins = { -1, -1, -1, -1 };
	int32_t backdrop = 3;
	int32_t corners = 2;
	BOOL light = FALSE;
	BOOL disabled = TRUE;
	int32_t policy = DWMNCRP_DISABLED;

	DwmExtendFrameIntoClientArea ( main_window, &margins );
	DwmSetWindowAttribute ( main_window, DWMWA_USE_IMMERSIVE_DARK_MODE, &light, sizeof ( light ) );
	DwmSetWindowAttribute ( main_window, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof ( corners ) );

	bool acrylic = SUCCEEDED ( DwmSetWindowAttribute ( main_window, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof ( backdrop ) ) );

	DwmSetWindowAttribute ( overlay_window, DWMWA_TRANSITIONS_FORCEDISABLED, &disabled, sizeof ( disabled ) );
	DwmSetWindowAttribute ( overlay_window, DWMWA_NCRENDERING_POLICY, &policy, sizeof ( policy ) );
	DwmSetWindowAttribute ( mini_window, DWMWA_TRANSITIONS_FORCEDISABLED, &disabled, sizeof ( disabled ) );
	DwmSetWindowAttribute ( mini_window, DWMWA_NCRENDERING_POLICY, &policy, sizeof ( policy ) );
	SetWindowPos ( main_window, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );

	program = new ui::app ( );

	if ( !acrylic )
		program->base_alpha = 1.0f;

	if ( !program->setup ( device, true ) ) {

		MessageBoxW ( nullptr, L"Pearl could not load its library.", L"Pearl", MB_ICONERROR );

		return 1;

	}

	SetWindowPos ( main_window, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
	spotify::start ( main_window, spotify_message );

#ifdef PEARL_DEBUG

	int32_t argument_count = 0;
	wchar_t** arguments = CommandLineToArgvW ( GetCommandLineW ( ), &argument_count );

	for ( int32_t i = 1; arguments && i + 1 < argument_count; i++ ) {

		if ( base::same_text ( arguments [ i ], L"--folder" ) ) {

			library::clear_folders ( );
			library::add_folder ( arguments [ i + 1 ] );
			program->persist = false;

		}

		if ( base::same_text ( arguments [ i ], L"--page" ) && base::same_text ( arguments [ i + 1 ], L"liked" ) ) {

			program->navigate ( ui::page::LIKED );
			program->persist = false;

		}

		if ( base::same_text ( arguments [ i ], L"--trace" ) ) {

			trace_path = arguments [ i + 1 ];
			program->persist = false;

		}

		if ( base::same_text ( arguments [ i ], L"--autoplay" ) || base::same_text ( arguments [ i + 1 ], L"--autoplay" ) ) {

			program->autoplay = true;

		}

	}

#endif

	if ( library::folder_count ( ) > 0 )
		library::start_scan ( );

	if ( program->placement_valid ) {

		RECT saved = { program->placement [ 0 ], program->placement [ 1 ], program->placement [ 0 ] + program->placement [ 2 ], program->placement [ 1 ] + program->placement [ 3 ] };

		if ( MonitorFromRect ( &saved, MONITOR_DEFAULTTONULL ) ) {

			WINDOWPLACEMENT place = { };

			place.length = sizeof ( place );
			place.showCmd = SW_HIDE;
			place.rcNormalPosition = saved;
			SetWindowPlacement ( main_window, &place );
			start_maximized = program->placement_maximized;

		}

	}

	RECT client = { };

	GetClientRect ( main_window, &client );

	if ( !device->attach ( main_output, main_window, client.right, client.bottom ) || !device->attach ( overlay_output, overlay_window, 64, 64 ) || !device->attach ( mini_output, mini_window, 64, 64 ) ) {

		MessageBoxW ( nullptr, L"Pearl could not create its window surface.", L"Pearl", MB_ICONERROR );

		return 1;

	}

	overlay_canvas = new render::canvas ( );
	sync_size ( );
	RegisterHotKey ( main_window, 1, MOD_NOREPEAT, VK_INSERT );
	frame_timer = CreateWaitableTimerExW ( nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS );

	if ( !frame_timer )
		frame_timer = CreateWaitableTimerW ( nullptr, FALSE, nullptr );

	measure_refresh ( );
	begin_show ( );

	MSG message;
	bool running = true;

	while ( running ) {

		if ( !device_lost && !device->healthy ( ) )
			device_lost = true;

		if ( device_lost && base::seconds ( ) >= device_retry )
			recover_device ( );

		bool overlay_active = window_phase == phase::HIDING || window_phase == phase::SHOWING;
		bool render = window_phase == phase::SHOWN && program->busy ( );
		bool drift = window_phase == phase::SHOWN && !render && !IsIconic ( main_window );
		bool mini_busy = mini_on && ( program->busy ( ) || mini_snapping );
		bool need = overlay_active || render || mini_busy || drift;
		DWORD result = WAIT_TIMEOUT;

		if ( need ) {

			int32_t rate = 0;

			if ( drift && !overlay_active && !mini_busy )
				rate = GetForegroundWindow ( ) == main_window ? 24 : 12;
			else if ( program->steady ( ) && !overlay_active && !mini_snapping )
				rate = 60;

			schedule_frame ( rate );
#ifdef PEARL_DEBUG

			double wait_begin = base::seconds ( );

#endif

			result = MsgWaitForMultipleObjectsEx ( 1, &frame_timer, frame_wait ( ), QS_ALLINPUT, MWMO_INPUTAVAILABLE );

#ifdef PEARL_DEBUG

			if ( trace_path && trace_start >= 0.0 && trace_events.size ( ) < 1800 )
				trace_events.add ( L" w" ).fixed ( ( wait_begin - trace_last ) * 1000.0, 1 ).add ( L"+" ).fixed ( ( base::seconds ( ) - wait_begin ) * 1000.0, 1 ).add ( L"r" ).number ( static_cast < int64_t > ( result ) );

#endif

		} else {

			if ( frame_armed ) {

				CancelWaitableTimer ( frame_timer );
				frame_armed = false;

			}

			if ( fine_timer ) {

				timeEndPeriod ( 1 );
				fine_timer = false;

			}

			MsgWaitForMultipleObjectsEx ( 0, nullptr, device_lost ? 250 : ( mini_on ? 30 : ( window_phase == phase::HIDDEN ? 250 : INFINITE ) ), QS_ALLINPUT, MWMO_INPUTAVAILABLE );

		}

		while ( PeekMessageW ( &message, nullptr, 0, 0, PM_REMOVE ) ) {

			if ( message.message == WM_QUIT ) {

				running = false;

				break;

			}

#ifdef PEARL_DEBUG

			trace_wakes++;
			trace_message = message.message;

#endif

			TranslateMessage ( &message );
			DispatchMessageW ( &message );

		}

		if ( !running )
			break;

		bool late = frame_armed && clock_now ( ) >= frame_due;
		bool tick = need && ( result == WAIT_OBJECT_0 || late || WaitForSingleObject ( frame_timer, 0 ) == WAIT_OBJECT_0 );

		if ( tick ) {

			frame_last = clock_now ( );

			if ( frame_armed )
				CancelWaitableTimer ( frame_timer );

			frame_armed = false;

		}

		if ( mini_on ) {

			sync_mini_cursor ( );

			if ( tick && mini_snapping )
				step_snap ( );

			if ( tick && ( mini_snapping || program->busy ( ) ) )
				render_mini ( );

		}

		if ( window_phase == phase::HIDING || window_phase == phase::SHOWING ) {

			if ( tick )
				step_overlay ( );

			continue;

		}

		if ( window_phase == phase::SHOWN && ( program->busy ( ) || drift ) ) {

			if ( tick )
				render_main ( );

		} else if ( window_phase == phase::HIDDEN && !mini_on )
			program->idle ( base::seconds ( ) );

	}

	UnregisterHotKey ( main_window, 1 );
	program->shutdown ( );
	CoUninitialize ( );

	if ( single )
		CloseHandle ( single );

	return 0;

}