#include "audio/engine.hh"
#include "audio/decoder.hh"
#include "base/math.hh"
#include <windows.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>

namespace audio {

	struct shared_state {

		SRWLOCK lock;
		synth::song song;
		bool file;
		wchar_t path [ 1024 ];
		double duration;
		float levels [ 3 ];
		bool has_song;
		double start;
		uint32_t generation;
		bool want_play;
		float volume;
		double reported;
		int64_t stamp;
		bool advancing;
		uint32_t applied;
		bool ended;

	};

	static shared_state shared;
	static HANDLE worker = nullptr;
	static HANDLE quit_event = nullptr;
	static HANDLE wake_event = nullptr;
	static HANDLE buffer_event = nullptr;
	static volatile bool device_changed = false;
	static bool virtual_mode = true;
	static bool device_open = false;
	static double virtual_position = 0.0;
	static bool virtual_ended = false;

	static const GUID float_format = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };

	class device_watch : public IMMNotificationClient {

	public:

		ULONG __stdcall AddRef ( ) override {

			return 1;

		}

		ULONG __stdcall Release ( ) override {

			return 1;

		}

		HRESULT __stdcall QueryInterface ( REFIID id, void** result ) override {

			if ( id == __uuidof ( IUnknown ) || id == __uuidof ( IMMNotificationClient ) ) {

				*result = this;

				return S_OK;

			}

			*result = nullptr;

			return E_NOINTERFACE;

		}

		HRESULT __stdcall OnDefaultDeviceChanged ( EDataFlow flow, ERole role, LPCWSTR ) override {

			if ( flow == eRender && role == eConsole ) {

				device_changed = true;
				SetEvent ( wake_event );

			}

			return S_OK;

		}

		HRESULT __stdcall OnDeviceAdded ( LPCWSTR ) override {

			return S_OK;

		}

		HRESULT __stdcall OnDeviceRemoved ( LPCWSTR ) override {

			return S_OK;

		}

		HRESULT __stdcall OnDeviceStateChanged ( LPCWSTR, DWORD ) override {

			return S_OK;

		}

		HRESULT __stdcall OnPropertyValueChanged ( LPCWSTR, const PROPERTYKEY ) override {

			return S_OK;

		}

	};

	static device_watch watcher;

	struct voice {

		synth::song song;
		bool file = false;
		wchar_t path [ 1024 ] = { };
		double duration = 0.0;
		uint32_t ticket = 0;
		int32_t rate = 0;
		bool valid = false;
		int64_t frame = 0;
		float gain = 0.0f;
		float level = 0.0f;
		uint32_t seen = 0;
		bool switching = false;
		bool ended = false;

	};

	static voice active;
	static uint32_t tickets = 0;
	static float band_low = 0.0f;
	static float band_mid = 0.0f;
	static float band_level [ 3 ] = { };

	static int64_t now_ticks ( ) {

		LARGE_INTEGER value;

		QueryPerformanceCounter ( &value );

		return value.QuadPart;

	}

	static double tick_seconds ( int64_t ticks ) {

		static int64_t frequency = 0;

		if ( !frequency ) {

			LARGE_INTEGER value;

			QueryPerformanceFrequency ( &value );
			frequency = value.QuadPart;

		}

		return static_cast < double > ( ticks ) / static_cast < double > ( frequency );

	}

	static void process ( float* out, int32_t frames, int32_t rate ) {

		int32_t done = 0;

		while ( done < frames ) {

			int32_t chunk = base::min ( 64, frames - done );
			float* block = out + done * 2;
			bool want = false;
			float target_volume = 1.0f;

			AcquireSRWLockExclusive ( &shared.lock );

			if ( shared.generation != active.seen )
				active.switching = true;

			if ( active.switching && active.gain <= 0.0f ) {

				active.song = shared.song;
				active.file = shared.file;
				active.duration = shared.duration;

				for ( int32_t i = 0; i < 1024; i++ )
					active.path [ i ] = shared.path [ i ];

				active.valid = shared.has_song;
				active.frame = static_cast < int64_t > ( shared.start * static_cast < double > ( rate ) );
				active.seen = shared.generation;
				active.switching = false;
				active.ended = false;

				if ( active.file && active.valid ) {

					active.ticket = ++tickets;
					active.rate = rate;
					decoder_request ( active.path, shared.start, rate, active.ticket );

				}

			}

			want = shared.want_play;
			target_volume = shared.volume;
			ReleaseSRWLockExclusive ( &shared.lock );

			if ( active.file && active.valid && active.rate != rate && active.rate > 0 ) {

				double at = static_cast < double > ( active.frame ) / static_cast < double > ( active.rate );

				active.ticket = ++tickets;
				active.rate = rate;
				active.frame = static_cast < int64_t > ( at * static_cast < double > ( rate ) );
				decoder_request ( active.path, at, rate, active.ticket );

			}

			float target = ( !active.switching && want && active.valid && !active.ended ) ? 1.0f : 0.0f;
			float step = active.switching ? 1.0f / ( 0.012f * static_cast < float > ( rate ) ) : ( target > active.gain ? 1.0f / ( 0.06f * static_cast < float > ( rate ) ) : 1.0f / ( 0.11f * static_cast < float > ( rate ) ) );
			float follow = 1.0f - math::exp ( -1.0f / ( 0.03f * static_cast < float > ( rate ) ) );

			if ( active.gain <= 0.0f && target <= 0.0f ) {

				__builtin_memset ( block, 0, sizeof ( float ) * 2 * static_cast < size_t > ( chunk ) );
				active.level = target_volume;
				done += chunk;

				continue;

			}

			if ( active.file ) {

				bool drained = false;
				int64_t frame_now = active.frame;
				int32_t produced = decoder_pull ( active.ticket, block, chunk, drained, frame_now );

				if ( produced < chunk )
					__builtin_memset ( block + produced * 2, 0, sizeof ( float ) * 2 * static_cast < size_t > ( chunk - produced ) );

				if ( drained || decoder_failed ( active.ticket ) )
					active.ended = true;

				if ( produced == 0 && !active.ended ) {

					done += chunk;

					continue;

				}

				active.frame = frame_now - chunk;

			} else
				synth::render ( active.song, active.frame, chunk, rate, block );

			float low_rate = 1.0f - math::exp ( -6.2831853f * 160.0f / static_cast < float > ( rate ) );
			float mid_rate = 1.0f - math::exp ( -6.2831853f * 2400.0f / static_cast < float > ( rate ) );
			float energy [ 3 ] = { };

			for ( int32_t i = 0; i < chunk; i++ ) {

				float mono = ( block [ i * 2 ] + block [ i * 2 + 1 ] ) * 0.5f;

				band_low += ( mono - band_low ) * low_rate;
				band_mid += ( mono - band_mid ) * mid_rate;

				float low = band_low;
				float mid = band_mid - band_low;
				float high = mono - band_mid;

				energy [ 0 ] += low * low;
				energy [ 1 ] += mid * mid;
				energy [ 2 ] += high * high;

			}

			float weights [ 3 ] = { 3.2f, 4.6f, 9.0f };

			for ( int32_t b = 0; b < 3; b++ ) {

				float measured = math::saturate ( math::sqrt ( energy [ b ] / static_cast < float > ( chunk ) ) * weights [ b ] );

				band_level [ b ] = base::max ( measured, band_level [ b ] * 0.985f );

			}

			for ( int32_t i = 0; i < chunk; i++ ) {

				if ( active.gain < target )
					active.gain = base::min ( active.gain + step, target );
				else if ( active.gain > target )
					active.gain = base::max ( active.gain - step, target );

				active.level += ( target_volume - active.level ) * follow;

				float g = active.gain * active.gain * ( 3.0f - 2.0f * active.gain );
				float amplitude = g * active.level * active.level;

				block [ i * 2 ] *= amplitude;
				block [ i * 2 + 1 ] *= amplitude;

			}

			active.frame += chunk;

			if ( !active.file && static_cast < double > ( active.frame ) >= active.duration * static_cast < double > ( rate ) )
				active.ended = true;

			done += chunk;

		}

	}

	static void publish ( int32_t latency, int32_t rate ) {

		bool advancing = active.valid && !active.switching && active.gain > 0.0f;
		int64_t frame = advancing ? active.frame - latency : active.frame;

		AcquireSRWLockExclusive ( &shared.lock );
		shared.reported = static_cast < double > ( base::max ( frame, static_cast < int64_t > ( 0 ) ) ) / static_cast < double > ( rate );
		shared.stamp = now_ticks ( );
		shared.advancing = advancing && !active.ended;
		shared.applied = active.seen;
		shared.ended = active.ended;

		for ( int32_t b = 0; b < 3; b++ )
			shared.levels [ b ] = active.gain > 0.0f ? band_level [ b ] : 0.0f;

		ReleaseSRWLockExclusive ( &shared.lock );

	}

	static void close_device ( IAudioClient*& client, IAudioRenderClient*& renderer ) {

		if ( client )
			client->Stop ( );

		if ( renderer ) {

			renderer->Release ( );
			renderer = nullptr;

		}

		if ( client ) {

			client->Release ( );
			client = nullptr;

		}

		device_open = false;

	}

	static bool open_device ( IMMDeviceEnumerator* enumerator, IAudioClient*& client, IAudioRenderClient*& renderer, UINT32& buffer_frames, int32_t& rate ) {

		IMMDevice* endpoint = nullptr;

		if ( FAILED ( enumerator->GetDefaultAudioEndpoint ( eRender, eConsole, &endpoint ) ) )
			return false;

		HRESULT result = endpoint->Activate ( __uuidof ( IAudioClient ), CLSCTX_ALL, nullptr, reinterpret_cast < void** > ( &client ) );

		endpoint->Release ( );

		if ( FAILED ( result ) )
			return false;

		WAVEFORMATEX* mix = nullptr;

		if ( SUCCEEDED ( client->GetMixFormat ( &mix ) ) && mix ) {

			rate = static_cast < int32_t > ( mix->nSamplesPerSec );
			CoTaskMemFree ( mix );

		}

		if ( rate < 8000 || rate > 384000 )
			rate = 48000;

		WAVEFORMATEXTENSIBLE format = { };

		format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
		format.Format.nChannels = 2;
		format.Format.nSamplesPerSec = static_cast < DWORD > ( rate );
		format.Format.wBitsPerSample = 32;
		format.Format.nBlockAlign = 8;
		format.Format.nAvgBytesPerSec = static_cast < DWORD > ( rate ) * 8;
		format.Format.cbSize = 22;
		format.Samples.wValidBitsPerSample = 32;
		format.dwChannelMask = 3;
		format.SubFormat = float_format;

		DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY | AUDCLNT_STREAMFLAGS_NOPERSIST;

		if ( FAILED ( client->Initialize ( AUDCLNT_SHAREMODE_SHARED, flags, 300000, 0, &format.Format, nullptr ) ) )
			return false;

		if ( FAILED ( client->SetEventHandle ( buffer_event ) ) || FAILED ( client->GetBufferSize ( &buffer_frames ) ) )
			return false;

		if ( FAILED ( client->GetService ( __uuidof ( IAudioRenderClient ), reinterpret_cast < void** > ( &renderer ) ) ) )
			return false;

		BYTE* data = nullptr;

		if ( SUCCEEDED ( renderer->GetBuffer ( buffer_frames, &data ) ) )
			renderer->ReleaseBuffer ( buffer_frames, AUDCLNT_BUFFERFLAGS_SILENT );

		if ( FAILED ( client->Start ( ) ) )
			return false;

		device_open = true;

		return true;

	}

	static DWORD __stdcall audio_thread ( void* ) {

		CoInitializeEx ( nullptr, COINIT_MULTITHREADED );

		DWORD task = 0;
		HANDLE priority = AvSetMmThreadCharacteristicsW ( L"Pro Audio", &task );
		IMMDeviceEnumerator* enumerator = nullptr;
		IAudioClient* client = nullptr;
		IAudioRenderClient* renderer = nullptr;
		UINT32 buffer_frames = 0;
		int32_t rate = 48000;
		double retry_at = 0.0;
		float* scratch = static_cast < float* > ( runtime_allocate ( sizeof ( float ) * 2 * 48000 ) );
		int64_t last = now_ticks ( );

		CoCreateInstance ( __uuidof ( MMDeviceEnumerator ), nullptr, CLSCTX_ALL, __uuidof ( IMMDeviceEnumerator ), reinterpret_cast < void** > ( &enumerator ) );

		if ( enumerator )
			enumerator->RegisterEndpointNotificationCallback ( &watcher );

		while ( true ) {

			double now = tick_seconds ( now_ticks ( ) );

			if ( !client && enumerator && now >= retry_at ) {

				if ( !open_device ( enumerator, client, renderer, buffer_frames, rate ) ) {

					close_device ( client, renderer );
					retry_at = now + 1.0;

				}

				last = now_ticks ( );

			}

			HANDLE handles [ 3 ] = { quit_event, wake_event, buffer_event };
			DWORD waited = WaitForMultipleObjects ( client ? 3 : 2, handles, FALSE, client ? 100 : 10 );

			if ( waited == WAIT_OBJECT_0 )
				break;

			if ( device_changed ) {

				device_changed = false;
				close_device ( client, renderer );
				retry_at = 0.0;

				continue;

			}

			if ( !client ) {

				int64_t current = now_ticks ( );
				int32_t frames = base::min ( static_cast < int32_t > ( tick_seconds ( current - last ) * static_cast < double > ( rate ) ), 48000 );

				if ( frames > 0 ) {

					process ( scratch, frames, rate );
					publish ( 0, rate );
					last = current;

				}

				continue;

			}

			UINT32 padding = 0;

			if ( FAILED ( client->GetCurrentPadding ( &padding ) ) ) {

				close_device ( client, renderer );
				retry_at = now + 0.25;

				continue;

			}

			UINT32 available = buffer_frames > padding ? buffer_frames - padding : 0;

			if ( available > 0 ) {

				BYTE* data = nullptr;

				if ( FAILED ( renderer->GetBuffer ( available, &data ) ) ) {

					close_device ( client, renderer );
					retry_at = now + 0.25;

					continue;

				}

				process ( reinterpret_cast < float* > ( data ), static_cast < int32_t > ( available ), rate );
				renderer->ReleaseBuffer ( available, 0 );
				padding += available;

			}

			publish ( static_cast < int32_t > ( padding ), rate );
			last = now_ticks ( );

		}

		close_device ( client, renderer );

		if ( enumerator ) {

			enumerator->UnregisterEndpointNotificationCallback ( &watcher );
			enumerator->Release ( );

		}

		runtime_release ( scratch );

		if ( priority )
			AvRevertMmThreadCharacteristics ( priority );

		CoUninitialize ( );

		return 0;

	}

	bool start ( bool device ) {

		InitializeSRWLock ( &shared.lock );
		shared.volume = 0.8f;
		virtual_mode = !device;

		if ( virtual_mode )
			return true;

		quit_event = CreateEventW ( nullptr, TRUE, FALSE, nullptr );
		wake_event = CreateEventW ( nullptr, FALSE, FALSE, nullptr );
		buffer_event = CreateEventW ( nullptr, FALSE, FALSE, nullptr );
		worker = CreateThread ( nullptr, 0, &audio_thread, nullptr, 0, nullptr );

		return worker != nullptr;

	}

	void stop ( ) {

		if ( !worker )
			return;

		play ( false );
		Sleep ( 120 );
		SetEvent ( quit_event );
		WaitForSingleObject ( worker, 2000 );
		CloseHandle ( worker );
		worker = nullptr;

	}

	void open ( const synth::song& source, double at, bool play_now ) {

		AcquireSRWLockExclusive ( &shared.lock );
		shared.song = source;
		shared.file = false;
		shared.duration = source.duration;
		shared.has_song = true;
		shared.start = math::saturate ( static_cast < float > ( at / source.duration ) ) * source.duration;
		shared.generation++;
		shared.want_play = play_now;
		shared.ended = false;
		ReleaseSRWLockExclusive ( &shared.lock );

		virtual_position = shared.start;
		virtual_ended = false;

		if ( wake_event )
			SetEvent ( wake_event );

	}

	void play ( bool playing_now ) {

		AcquireSRWLockExclusive ( &shared.lock );
		shared.want_play = playing_now;
		ReleaseSRWLockExclusive ( &shared.lock );

		if ( wake_event )
			SetEvent ( wake_event );

	}

	void seek ( double seconds ) {

		AcquireSRWLockExclusive ( &shared.lock );

		if ( shared.has_song ) {

			shared.start = seconds < 0.0 ? 0.0 : ( shared.duration > 0.0 && seconds > shared.duration ? shared.duration : seconds );
			shared.generation++;
			shared.ended = false;
			virtual_position = shared.start;
			virtual_ended = false;

		}

		ReleaseSRWLockExclusive ( &shared.lock );

	}

	void volume ( float value ) {

		AcquireSRWLockExclusive ( &shared.lock );
		shared.volume = math::saturate ( value );
		ReleaseSRWLockExclusive ( &shared.lock );

	}

	void tick ( double seconds ) {

		if ( !virtual_mode || !shared.has_song || !shared.want_play || virtual_ended )
			return;

		virtual_position += seconds;

		if ( shared.duration > 0.0 && virtual_position >= shared.duration ) {

			virtual_position = shared.duration;
			virtual_ended = true;

		}

	}

	double position ( ) {

		if ( virtual_mode )
			return virtual_position;

		AcquireSRWLockExclusive ( &shared.lock );

		double result = shared.start;

		if ( shared.applied == shared.generation ) {

			result = shared.reported;

			if ( shared.advancing && shared.want_play ) {

				double elapsed = tick_seconds ( now_ticks ( ) - shared.stamp );

				result += elapsed < 0.15 ? elapsed : 0.15;

			}

		}

		double limit = shared.has_song && shared.duration > 0.0 ? shared.duration : 1e9;

		ReleaseSRWLockExclusive ( &shared.lock );

		return result > limit ? limit : result;

	}

	bool playing ( ) {

		AcquireSRWLockExclusive ( &shared.lock );

		bool result = shared.want_play;

		ReleaseSRWLockExclusive ( &shared.lock );

		return result;

	}

	bool finished ( ) {

		if ( virtual_mode )
			return virtual_ended;

		AcquireSRWLockExclusive ( &shared.lock );

		bool result = shared.ended && shared.applied == shared.generation;

		ReleaseSRWLockExclusive ( &shared.lock );

		return result;

	}

	bool live ( ) {

		return device_open;

	}

	void open_file ( const wchar_t* path, double duration, double at, bool play_now ) {

		AcquireSRWLockExclusive ( &shared.lock );
		shared.file = true;

		int32_t length = 0;

		for ( ; path [ length ] && length < 1023; length++ )
			shared.path [ length ] = path [ length ];

		shared.path [ length ] = 0;
		shared.duration = duration;
		shared.has_song = true;
		shared.start = at < 0.0 ? 0.0 : at;
		shared.generation++;
		shared.want_play = play_now;
		shared.ended = false;
		ReleaseSRWLockExclusive ( &shared.lock );

		virtual_position = shared.start;
		virtual_ended = false;

		if ( wake_event )
			SetEvent ( wake_event );

	}

	double duration ( ) {

		AcquireSRWLockExclusive ( &shared.lock );

		double result = shared.duration;
		bool file = shared.file;

		ReleaseSRWLockExclusive ( &shared.lock );

		if ( result <= 0.0 && file )
			result = decoder_duration ( );

		return result;

	}

	void levels ( float* out ) {

		AcquireSRWLockExclusive ( &shared.lock );

		for ( int32_t b = 0; b < 3; b++ )
			out [ b ] = shared.levels [ b ];

		ReleaseSRWLockExclusive ( &shared.lock );

	}

}