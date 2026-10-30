#include "audio/decoder.hh"
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

namespace audio {

	struct ring_state {

		SRWLOCK lock;
		float* data;
		int64_t read;
		int64_t write;
		int64_t origin;
		uint32_t wanted;
		uint32_t served;
		bool ended;
		bool failed;
		wchar_t path [ 1024 ];
		double seconds;
		int32_t rate;
		double duration;

	};

	static constexpr int32_t room = 1 << 18;
	static ring_state ring;
	static HANDLE thread = nullptr;
	static HANDLE wake = nullptr;

	struct converter {

		int32_t channels = 2;
		int32_t source_rate = 48000;
		int32_t target_rate = 48000;
		double phase = 0.0;
		float last [ 2 ] = { };
		bool primed = false;

	};

	static void push_frames ( const float* frames, int32_t count, uint32_t ticket ) {

		int32_t done = 0;

		while ( done < count ) {

			AcquireSRWLockExclusive ( &ring.lock );

			if ( ring.wanted != ticket ) {

				ReleaseSRWLockExclusive ( &ring.lock );

				return;

			}

			int32_t space = room - static_cast < int32_t > ( ring.write - ring.read );
			int32_t take = base::min ( space, count - done );

			for ( int32_t i = 0; i < take; i++ ) {

				int64_t slot = ( ring.write + i ) & ( room - 1 );

				ring.data [ slot * 2 ] = frames [ ( done + i ) * 2 ];
				ring.data [ slot * 2 + 1 ] = frames [ ( done + i ) * 2 + 1 ];

			}

			ring.write += take;
			ReleaseSRWLockExclusive ( &ring.lock );
			done += take;

			if ( done < count )
				WaitForSingleObject ( wake, 5 );

		}

	}

	static void convert ( converter& state, const float* source, int32_t count, base::list < float >& out ) {

		out.clear ( );

		if ( state.channels <= 0 )
			return;

		auto frame_at = [ & ] ( int32_t index, float* stereo ) {

			const float* p = source + static_cast < size_t > ( index ) * static_cast < size_t > ( state.channels );

			if ( state.channels == 1 ) {

				stereo [ 0 ] = p [ 0 ];
				stereo [ 1 ] = p [ 0 ];

			} else {

				stereo [ 0 ] = p [ 0 ];
				stereo [ 1 ] = p [ 1 ];

			}

		};

		if ( state.source_rate == state.target_rate ) {

			out.resize ( static_cast < size_t > ( count ) * 2 );

			for ( int32_t i = 0; i < count; i++ )
				frame_at ( i, out.data ( ) + i * 2 );

			return;

		}

		double step = static_cast < double > ( state.source_rate ) / static_cast < double > ( state.target_rate );

		if ( !state.primed && count > 0 ) {

			frame_at ( 0, state.last );
			state.primed = true;

		}

		while ( true ) {

			int32_t index = static_cast < int32_t > ( state.phase );

			if ( index >= count )
				break;

			float t = static_cast < float > ( state.phase - static_cast < double > ( index ) );
			float a [ 2 ];
			float b [ 2 ];

			if ( index == 0 ) {

				a [ 0 ] = state.last [ 0 ];
				a [ 1 ] = state.last [ 1 ];

			} else
				frame_at ( index - 1, a );

			frame_at ( index, b );
			out.push ( a [ 0 ] + ( b [ 0 ] - a [ 0 ] ) * t );
			out.push ( a [ 1 ] + ( b [ 1 ] - a [ 1 ] ) * t );
			state.phase += step;

		}

		state.phase -= static_cast < double > ( count );

		if ( count > 0 )
			frame_at ( count - 1, state.last );

	}

	static IMFSourceReader* open_reader ( const wchar_t* path, int32_t rate, converter& state, double& duration ) {

		IMFSourceReader* reader = nullptr;

		if ( FAILED ( MFCreateSourceReaderFromURL ( path, nullptr, &reader ) ) )
			return nullptr;

		reader->SetStreamSelection ( static_cast < DWORD > ( MF_SOURCE_READER_ALL_STREAMS ), FALSE );
		reader->SetStreamSelection ( static_cast < DWORD > ( MF_SOURCE_READER_FIRST_AUDIO_STREAM ), TRUE );

		IMFMediaType* type = nullptr;
		bool converted = false;

		if ( SUCCEEDED ( MFCreateMediaType ( &type ) ) ) {

			type->SetGUID ( MF_MT_MAJOR_TYPE, MFMediaType_Audio );
			type->SetGUID ( MF_MT_SUBTYPE, MFAudioFormat_Float );
			type->SetUINT32 ( MF_MT_AUDIO_NUM_CHANNELS, 2 );
			type->SetUINT32 ( MF_MT_AUDIO_SAMPLES_PER_SECOND, static_cast < UINT32 > ( rate ) );
			type->SetUINT32 ( MF_MT_AUDIO_BITS_PER_SAMPLE, 32 );
			type->SetUINT32 ( MF_MT_AUDIO_BLOCK_ALIGNMENT, 8 );
			type->SetUINT32 ( MF_MT_AUDIO_AVG_BYTES_PER_SECOND, static_cast < UINT32 > ( rate ) * 8 );
			type->SetUINT32 ( MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE );
			converted = SUCCEEDED ( reader->SetCurrentMediaType ( static_cast < DWORD > ( MF_SOURCE_READER_FIRST_AUDIO_STREAM ), nullptr, type ) );
			type->Release ( );

		}

		if ( !converted && SUCCEEDED ( MFCreateMediaType ( &type ) ) ) {

			type->SetGUID ( MF_MT_MAJOR_TYPE, MFMediaType_Audio );
			type->SetGUID ( MF_MT_SUBTYPE, MFAudioFormat_Float );
			reader->SetCurrentMediaType ( static_cast < DWORD > ( MF_SOURCE_READER_FIRST_AUDIO_STREAM ), nullptr, type );
			type->Release ( );

		}

		IMFMediaType* current = nullptr;

		if ( FAILED ( reader->GetCurrentMediaType ( static_cast < DWORD > ( MF_SOURCE_READER_FIRST_AUDIO_STREAM ), &current ) ) ) {

			reader->Release ( );

			return nullptr;

		}

		UINT32 channels = 2;
		UINT32 source_rate = static_cast < UINT32 > ( rate );
		GUID subtype = { };

		current->GetUINT32 ( MF_MT_AUDIO_NUM_CHANNELS, &channels );
		current->GetUINT32 ( MF_MT_AUDIO_SAMPLES_PER_SECOND, &source_rate );
		current->GetGUID ( MF_MT_SUBTYPE, &subtype );
		current->Release ( );

		if ( subtype != MFAudioFormat_Float || channels == 0 || source_rate == 0 ) {

			reader->Release ( );

			return nullptr;

		}

		state = converter ( );
		state.channels = static_cast < int32_t > ( channels );
		state.source_rate = static_cast < int32_t > ( source_rate );
		state.target_rate = rate;

		PROPVARIANT value;

		__builtin_memset ( &value, 0, sizeof ( value ) );

		if ( SUCCEEDED ( reader->GetPresentationAttribute ( static_cast < DWORD > ( MF_SOURCE_READER_MEDIASOURCE ), MF_PD_DURATION, &value ) ) ) {

			duration = static_cast < double > ( value.uhVal.QuadPart ) / 10000000.0;
			PropVariantClear ( &value );

		}

		return reader;

	}

	static DWORD __stdcall decode_thread ( void* ) {

		CoInitializeEx ( nullptr, COINIT_MULTITHREADED );
		MFStartup ( MF_VERSION, MFSTARTUP_LITE );

		IMFSourceReader* reader = nullptr;
		converter state;
		uint32_t ticket = 0;
		bool exhausted = true;
		int64_t skip = 0;
		double target = 0.0;
		bool full = false;
		wchar_t opened [ 1024 ] = { };
		int32_t opened_rate = 0;
		base::list < float > scratch;

		while ( true ) {

			WaitForSingleObject ( wake, exhausted || full ? INFINITE : 4 );
			full = false;

			AcquireSRWLockExclusive ( &ring.lock );

			bool fresh = ring.wanted != ring.served;
			wchar_t path [ 1024 ];
			double seconds = 0.0;
			int32_t rate = 48000;
			bool room_left = ( room - static_cast < int32_t > ( ring.write - ring.read ) ) > 16384;

			if ( fresh ) {

				ring.served = ring.wanted;
				ring.read = 0;
				ring.write = 0;
				ring.ended = false;
				ring.failed = false;
				ticket = ring.wanted;

				for ( int32_t i = 0; i < 1024; i++ )
					path [ i ] = ring.path [ i ];

				seconds = ring.seconds;
				rate = ring.rate;
				ring.origin = static_cast < int64_t > ( seconds * static_cast < double > ( rate ) );

			}

			ReleaseSRWLockExclusive ( &ring.lock );

			if ( fresh ) {

				if ( !reader || !base::same_text ( path, opened ) || rate != opened_rate ) {

					if ( reader )
						reader->Release ( );

					double length = 0.0;

					reader = open_reader ( path, rate, state, length );

					for ( int32_t i = 0; i < 1024; i++ )
						opened [ i ] = path [ i ];

					opened_rate = rate;

					AcquireSRWLockExclusive ( &ring.lock );
					ring.duration = length;
					ring.failed = reader == nullptr;
					ReleaseSRWLockExclusive ( &ring.lock );

				}

				if ( reader ) {

					PROPVARIANT position;

					__builtin_memset ( &position, 0, sizeof ( position ) );
					position.vt = VT_I8;
					position.hVal.QuadPart = static_cast < LONGLONG > ( seconds * 10000000.0 );
					reader->SetCurrentPosition ( GUID_NULL, position );
					state.phase = 0.0;
					state.primed = false;
					skip = -1;

				}

				exhausted = reader == nullptr;

				if ( !reader ) {

					AcquireSRWLockExclusive ( &ring.lock );
					ring.ended = true;
					ReleaseSRWLockExclusive ( &ring.lock );

				}

				target = seconds < 0.0 ? 0.0 : seconds;

			}

			if ( !reader || exhausted || !room_left ) {

				full = !room_left;

				continue;

			}

			for ( int32_t pass = 0; pass < 8; pass++ ) {

				DWORD flags = 0;
				LONGLONG timestamp = 0;
				IMFSample* sample = nullptr;

				if ( FAILED ( reader->ReadSample ( static_cast < DWORD > ( MF_SOURCE_READER_FIRST_AUDIO_STREAM ), 0, nullptr, &flags, &timestamp, &sample ) ) || ( flags & MF_SOURCE_READERF_ENDOFSTREAM ) ) {

					if ( sample )
						sample->Release ( );

					exhausted = true;
					AcquireSRWLockExclusive ( &ring.lock );

					if ( ring.wanted == ticket )
						ring.ended = true;

					ReleaseSRWLockExclusive ( &ring.lock );

					break;

				}

				if ( !sample )
					continue;

				IMFMediaBuffer* buffer = nullptr;

				if ( SUCCEEDED ( sample->ConvertToContiguousBuffer ( &buffer ) ) ) {

					BYTE* bytes = nullptr;
					DWORD length = 0;

					if ( SUCCEEDED ( buffer->Lock ( &bytes, nullptr, &length ) ) ) {

						int32_t count = static_cast < int32_t > ( length / ( sizeof ( float ) * static_cast < DWORD > ( state.channels ) ) );
						const float* source = reinterpret_cast < const float* > ( bytes );

						if ( skip < 0 ) {

							double wanted = target;
							double start = static_cast < double > ( timestamp ) / 10000000.0;

							skip = wanted > start ? static_cast < int64_t > ( ( wanted - start ) * static_cast < double > ( state.source_rate ) ) : 0;

						}

						if ( skip > 0 ) {

							int32_t drop = static_cast < int32_t > ( base::min ( skip, static_cast < int64_t > ( count ) ) );

							source += static_cast < size_t > ( drop ) * static_cast < size_t > ( state.channels );
							count -= drop;
							skip -= drop;

						}

						if ( count > 0 ) {

							convert ( state, source, count, scratch );
							push_frames ( scratch.data ( ), scratch.count ( ) / 2, ticket );

						}

						buffer->Unlock ( );

					}

					buffer->Release ( );

				}

				sample->Release ( );

				AcquireSRWLockExclusive ( &ring.lock );

				full = ( room - static_cast < int32_t > ( ring.write - ring.read ) ) <= 16384;

				bool moved = ring.wanted != ticket;

				ReleaseSRWLockExclusive ( &ring.lock );

				if ( full || moved )
					break;

			}

		}

		return 0;

	}

	void decoder_request ( const wchar_t* path, double seconds, int32_t rate, uint32_t ticket ) {

		if ( !thread ) {

			InitializeSRWLock ( &ring.lock );
			ring.data = static_cast < float* > ( runtime_allocate ( sizeof ( float ) * 2 * room ) );
			wake = CreateEventW ( nullptr, FALSE, FALSE, nullptr );
			thread = CreateThread ( nullptr, 0, &decode_thread, nullptr, 0, nullptr );

		}

		AcquireSRWLockExclusive ( &ring.lock );

		for ( int32_t i = 0; i < 1023 && path [ i ]; i++ ) {

			ring.path [ i ] = path [ i ];
			ring.path [ i + 1 ] = 0;

		}

		ring.seconds = seconds;
		ring.rate = rate;
		ring.wanted = ticket;
		ReleaseSRWLockExclusive ( &ring.lock );
		SetEvent ( wake );

	}

	int32_t decoder_pull ( uint32_t ticket, float* out, int32_t frames, bool& ended, int64_t& frame ) {

		ended = false;

		if ( !thread )
			return 0;

		AcquireSRWLockExclusive ( &ring.lock );

		if ( ring.served != ticket || ring.wanted != ticket ) {

			ReleaseSRWLockExclusive ( &ring.lock );

			return 0;

		}

		int32_t available = static_cast < int32_t > ( ring.write - ring.read );
		int32_t take = base::min ( available, frames );

		for ( int32_t i = 0; i < take; i++ ) {

			int64_t slot = ( ring.read + i ) & ( room - 1 );

			out [ i * 2 ] = ring.data [ slot * 2 ];
			out [ i * 2 + 1 ] = ring.data [ slot * 2 + 1 ];

		}

		ring.read += take;
		frame = ring.origin + ring.read;
		ended = ring.ended && ring.read == ring.write;
		ReleaseSRWLockExclusive ( &ring.lock );

		if ( take > 0 )
			SetEvent ( wake );

		return take;

	}

	double decoder_duration ( ) {

		if ( !thread )
			return 0.0;

		AcquireSRWLockExclusive ( &ring.lock );

		double result = ring.duration;

		ReleaseSRWLockExclusive ( &ring.lock );

		return result;

	}

	bool decoder_failed ( uint32_t ticket ) {

		if ( !thread )
			return false;

		AcquireSRWLockExclusive ( &ring.lock );

		bool result = ring.served == ticket && ring.failed;

		ReleaseSRWLockExclusive ( &ring.lock );

		return result;

	}

}