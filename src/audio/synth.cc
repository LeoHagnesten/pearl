#include "audio/synth.hh"
#include "base/math.hh"

namespace synth {

	static const int32_t scales [ 4 ] [ 7 ] = {
		{ 0, 2, 3, 5, 7, 8, 10 },
		{ 0, 2, 4, 5, 7, 9, 11 },
		{ 0, 2, 3, 5, 7, 9, 10 },
		{ 0, 2, 4, 6, 7, 9, 11 }
	};

	static const int32_t progressions [ 6 ] [ 4 ] = {
		{ 0, 5, 3, 4 },
		{ 0, 3, 5, 4 },
		{ 5, 3, 0, 4 },
		{ 0, 4, 5, 3 },
		{ 0, 2, 5, 4 },
		{ 3, 4, 0, 5 }
	};

	static const uint32_t bass_patterns [ 6 ] = { 0x11, 0x55, 0x99, 0x49, 0xB5, 0x91 };
	static const uint32_t arp_patterns [ 6 ] = { 0xFFFF, 0x5555, 0xB6DB, 0xAAAB, 0x7777, 0x9249 };
	static const uint32_t hat_patterns [ 3 ] = { 0xFFFF, 0x5555, 0xAAAA };
	static const float fm_ratios [ 5 ] = { 1.0f, 2.0f, 3.0f, 3.5f, 1.5f };

	struct section {

		bool bass;
		bool arp;
		bool kick;
		bool hats;
		bool clap;
		float pad;

	};

	static double floor_double ( double value ) {

		return __builtin_floor ( value );

	}

	static float frequency ( float note ) {

		return 440.0f * math::exp ( ( note - 69.0f ) * 0.0577622650f );

	}

	static int32_t degree_note ( const song& source, int32_t degree ) {

		int32_t octave = degree >= 0 ? degree / 7 : ( degree - 6 ) / 7;
		int32_t index = degree - octave * 7;

		return source.root + octave * 12 + scales [ source.mode ] [ index ];

	}

	static float tone ( double hz, double t, double phase = 0.0 ) {

		double cycles = hz * t + phase;

		cycles -= floor_double ( cycles );

		return math::sin ( static_cast < float > ( cycles ) * math::tau );

	}

	static uint64_t mix_bits ( uint64_t n ) {

		n ^= n >> 33;
		n *= 0xFF51AFD7ED558CCDull;
		n ^= n >> 33;
		n *= 0xC4CEB9FE1A85EC53ull;
		n ^= n >> 33;

		return n;

	}

	static float noise ( uint64_t n ) {

		return static_cast < float > ( mix_bits ( n ) >> 40 ) / 8388608.0f - 1.0f;

	}

	static float soft_clip ( float x ) {

		if ( x > 3.0f )
			return 1.0f;

		if ( x < -3.0f )
			return -1.0f;

		return x * ( 27.0f + x * x ) / ( 27.0f + 9.0f * x * x );

	}

	static section arrange ( const song& source, int32_t bar, int32_t total_bars ) {

		section result = { false, false, false, false, false, 1.0f };
		int32_t phrase = bar / 8;
		int32_t last_phrase = base::max ( ( total_bars - 1 ) / 8, 1 );

		if ( phrase == 0 ) {

			result.bass = bar >= 4;
			result.hats = bar >= 6;
			result.pad = 0.8f + 0.2f * static_cast < float > ( bar ) / 8.0f;

			return result;

		}

		if ( phrase >= last_phrase ) {

			result.arp = true;
			result.bass = bar < total_bars - 4;
			result.hats = bar < total_bars - 6;

			return result;

		}

		switch ( ( phrase - 1 + static_cast < int32_t > ( source.seed & 1 ) ) % 4 ) {

			case 0:
				result.bass = true;
				result.hats = true;
				result.kick = true;
				result.arp = ( source.seed & 2 ) != 0;
				break;

			case 1:
			case 2:
				result.bass = true;
				result.arp = true;
				result.kick = true;
				result.hats = true;
				result.clap = true;
				break;

			default:
				result.arp = true;
				result.pad = 1.15f;
				break;

		}

		return result;

	}

	static bool find_step ( uint32_t pattern, int32_t steps, double position, int32_t& start, int32_t& next ) {

		int32_t current = base::min ( static_cast < int32_t > ( position ), steps - 1 );

		start = -1;

		for ( int32_t k = current; k >= 0; k-- ) {

			if ( ( pattern >> k ) & 1 ) {

				start = k;

				break;

			}

		}

		if ( start < 0 )
			return false;

		next = steps;

		for ( int32_t k = current + 1; k < steps; k++ ) {

			if ( ( pattern >> k ) & 1 ) {

				next = k;

				break;

			}

		}

		return true;

	}

	static void pad_voice ( const song& source, int32_t chord, double t, float gain, float& left, float& right ) {

		if ( gain <= 0.0001f )
			return;

		for ( int32_t k = 0; k < 3; k++ ) {

			float hz = frequency ( static_cast < float > ( degree_note ( source, chord + k * 2 ) ) );
			float a = tone ( hz * 1.0035, t );
			float b = tone ( hz * 0.9965, t, 0.21 );
			float c = tone ( hz, t, 0.37 * k );
			float h = tone ( hz * 2.0, t, 0.11 );
			float shared = c * 0.4f + h * 0.14f * source.warmth;

			left += ( a * 0.6f + shared ) * gain;
			right += ( b * 0.6f + shared ) * gain;

		}

	}

	void compose ( song& result, uint32_t seed, float duration, int32_t flavor ) {

		base::random random;

		random.seed ( static_cast < uint64_t > ( seed ) * 0x9E3779B97F4A7C15ull + 17 );
		result.seed = seed;
		result.duration = duration;
		result.bpm = 76.0f + random.unit ( ) * 48.0f;
		result.root = 52 + static_cast < int32_t > ( random.next ( ) % 9 );
		result.mode = static_cast < int32_t > ( ( random.next ( ) + static_cast < uint64_t > ( flavor ) ) % 4 );

		const int32_t* progression = progressions [ random.next ( ) % 6 ];

		for ( int32_t i = 0; i < 4; i++ )
			result.progression [ i ] = progression [ i ];

		result.bass_pattern = bass_patterns [ random.next ( ) % 6 ] | 1u;
		result.arp_pattern = arp_patterns [ random.next ( ) % 6 ] | 1u;
		result.hat_pattern = hat_patterns [ random.next ( ) % 3 ];
		result.four_floor = ( flavor % 3 ) == 1 || random.unit ( ) < 0.25f;
		result.sixteenth_hats = random.unit ( ) < 0.4f;
		result.fm_ratio = fm_ratios [ random.next ( ) % 5 ];
		result.fm_depth = 0.8f + random.unit ( ) * 2.4f;
		result.weirdness = 0.2f + random.unit ( ) * 0.7f;
		result.warmth = 0.3f + random.unit ( ) * 0.7f;
		result.arp_octave = random.unit ( ) < 0.6f ? 12.0f : 24.0f;

	}

	void render ( const song& source, int64_t first_frame, int32_t frames, int32_t rate, float* out ) {

		double beat = 60.0 / static_cast < double > ( source.bpm );
		double bar_length = beat * 4.0;
		int32_t total_bars = base::max ( static_cast < int32_t > ( source.duration / bar_length ), 1 );
		double inverse_rate = 1.0 / static_cast < double > ( rate );
		uint64_t salt = static_cast < uint64_t > ( source.seed ) * 0x632BE59BD9B4E019ull;

		for ( int32_t i = 0; i < frames; i++ ) {

			int64_t n = first_frame + i;
			double t = static_cast < double > ( n ) * inverse_rate;

			if ( n < 0 || t >= source.duration ) {

				out [ i * 2 ] = 0.0f;
				out [ i * 2 + 1 ] = 0.0f;

				continue;

			}

			double beats = t / beat;
			int32_t bar = static_cast < int32_t > ( beats / 4.0 );
			double in_bar = beats - static_cast < double > ( bar ) * 4.0;
			section part = arrange ( source, bar, total_bars );
			int32_t chord = source.progression [ bar & 3 ];
			int32_t previous = source.progression [ ( bar + 3 ) & 3 ];
			float bar_progress = static_cast < float > ( in_bar * 0.25 );
			float cross = bar > 0 ? math::smoothstep ( 0.0f, 0.12f, bar_progress ) : 1.0f;
			float tremolo = 0.86f + 0.14f * tone ( 0.21, t );
			float pad_gain = 0.05f * tremolo * part.pad;
			float pad_left = 0.0f;
			float pad_right = 0.0f;
			float lead_left = 0.0f;
			float lead_right = 0.0f;
			float left = 0.0f;
			float right = 0.0f;
			uint64_t bar_hash = mix_bits ( static_cast < uint64_t > ( bar ) * 7919 + salt );
			bool glitch = static_cast < float > ( bar_hash % 1000 ) < source.weirdness * 220.0f && in_bar >= 3.0 && bar > 4;

			pad_voice ( source, chord, t, pad_gain * cross, pad_left, pad_right );
			pad_voice ( source, previous, t, pad_gain * ( 1.0f - cross ), pad_left, pad_right );

			int32_t start = 0;
			int32_t next = 0;

			if ( part.bass && find_step ( source.bass_pattern, 8, in_bar * 2.0, start, next ) ) {

				double since = ( in_bar * 2.0 - start ) * beat * 0.5;
				double until = ( next - in_bar * 2.0 ) * beat * 0.5;
				float envelope = math::exp ( static_cast < float > ( -since * 4.2 ) ) * math::saturate ( static_cast < float > ( since * 250.0 ) ) * math::saturate ( static_cast < float > ( until / 0.008 ) );
				float hz = frequency ( static_cast < float > ( degree_note ( source, chord ) - 12 ) );
				float value = tone ( hz, t ) + 0.32f * tone ( hz * 2.0, t, 0.25 ) + 0.1f * tone ( hz * 3.0, t );
				float bass = soft_clip ( value * 1.5f ) * envelope * 0.15f;

				left += bass;
				right += bass;

			}

			if ( part.arp && find_step ( source.arp_pattern, 16, in_bar * 4.0, start, next ) ) {

				static const int32_t shape [ 8 ] = { 0, 2, 4, 7, 9, 7, 4, 2 };
				double sixteenth = beat * 0.25;
				double since = ( in_bar * 4.0 - start ) * sixteenth;
				double until = ( next - in_bar * 4.0 ) * sixteenth;
				int32_t degree = chord + shape [ ( start + bar * 3 ) & 7 ];
				float hz = frequency ( static_cast < float > ( degree_note ( source, degree ) ) + source.arp_octave );
				float envelope = math::exp ( static_cast < float > ( -since * 6.5 ) ) * math::saturate ( static_cast < float > ( since * 400.0 ) ) * math::saturate ( static_cast < float > ( until / 0.006 ) );
				float index = source.fm_depth * math::exp ( static_cast < float > ( -since * 5.0 ) );
				double cycles = hz * t;

				cycles -= floor_double ( cycles );

				float value = math::sin ( static_cast < float > ( cycles ) * math::tau + index * tone ( hz * source.fm_ratio, t ) ) * envelope * 0.07f;
				float pan = ( start & 1 ) ? 0.32f : -0.32f;

				lead_left += value * ( 1.0f - pan );
				lead_right += value * ( 1.0f + pan );

			}

			if ( glitch ) {

				float gate = ( static_cast < int32_t > ( in_bar * 12.0 ) & 1 ) ? 0.12f : 1.0f;

				pad_left *= gate;
				pad_right *= gate;
				lead_left *= gate;
				lead_right *= gate;

			}

			left += pad_left + lead_left;
			right += pad_right + lead_right;

			if ( part.kick ) {

				double period = source.four_floor ? 1.0 : 2.0;
				double since = ( in_bar - floor_double ( in_bar / period ) * period ) * beat;
				float envelope = math::exp ( static_cast < float > ( -since * 7.5 ) ) * math::saturate ( static_cast < float > ( since * 900.0 ) );
				double phase = 46.0 * since + ( 150.0 - 46.0 ) * ( 1.0 - static_cast < double > ( math::exp ( static_cast < float > ( -since * 28.0 ) ) ) ) / 28.0;

				phase -= floor_double ( phase );

				float kick = math::sin ( static_cast < float > ( phase ) * math::tau ) * envelope * 0.3f;

				left += kick;
				right += kick;

			}

			if ( part.hats ) {

				double division = source.sixteenth_hats ? 4.0 : 2.0;
				double position = in_bar * division;
				int32_t step = static_cast < int32_t > ( position );

				if ( ( source.hat_pattern >> ( step & 15 ) ) & 1 ) {

					double since = ( position - step ) * beat / division;
					bool open = ( step % 4 ) == 2 && ( source.seed & 4 );
					float envelope = math::exp ( static_cast < float > ( -since * ( open ? 13.0 : 62.0 ) ) );
					uint64_t u = static_cast < uint64_t > ( n );
					float hiss_left = noise ( u * 2 + salt ) - noise ( ( u - 1 ) * 2 + salt );
					float hiss_right = noise ( u * 2 + 1 + salt ) - noise ( ( u - 1 ) * 2 + 1 + salt );

					left += hiss_left * envelope * 0.032f;
					right += hiss_right * envelope * 0.032f;

				}

			}

			if ( part.clap ) {

				double shifted = in_bar + 1.0;
				double since = ( shifted - floor_double ( shifted / 2.0 ) * 2.0 ) * beat;
				uint64_t u = static_cast < uint64_t > ( n );
				float burst = since < 0.03 ? 0.55f + 0.45f * math::cos ( static_cast < float > ( since * 700.0 ) ) : 1.0f;
				float envelope = math::exp ( static_cast < float > ( -since * 19.0 ) ) * burst;
				float body = ( noise ( u * 3 + 7 + salt ) - noise ( ( u - 1 ) * 3 + 7 + salt ) ) * 0.5f + noise ( u * 3 + 11 + salt ) * 0.4f;
				float snap = tone ( 188.0, since ) * math::exp ( static_cast < float > ( -since * 36.0 ) );

				left += ( body * envelope * 0.085f + snap * 0.045f );
				right += ( body * envelope * 0.08f + snap * 0.045f );

			}

			if ( bar % 8 == 7 && bar / 8 < ( total_bars - 1 ) / 8 ) {

				float x = bar_progress;
				uint64_t u = static_cast < uint64_t > ( n );
				float sweep = 0.5f + 0.5f * tone ( 260.0 + 2600.0 * x * x, t );
				float riser = noise ( u * 5 + 3 + salt ) * x * x * x * sweep * 0.05f * source.weirdness;

				left += riser;
				right += riser * 0.9f;

			}

			float crackle = 0.0f;

			for ( int32_t k = 0; k < 4; k++ ) {

				uint64_t h = mix_bits ( static_cast < uint64_t > ( n - k ) ^ salt );

				if ( ( h & 0xFFFF ) < 5 )
					crackle += ( ( h >> 20 ) & 1 ? 0.22f : -0.22f ) * ( k == 0 ? 1.0f : ( k == 1 ? 0.5f : ( k == 2 ? 0.25f : 0.12f ) ) );

			}

			float hiss = noise ( static_cast < uint64_t > ( n ) * 13 + salt ) * 0.0022f;

			left += crackle * source.weirdness + hiss;
			right += crackle * source.weirdness * 0.8f + hiss;

			float fade = math::saturate ( static_cast < float > ( t / 1.5 ) ) * math::saturate ( static_cast < float > ( ( source.duration - t ) / 3.5 ) );

			left = soft_clip ( left * 1.15f ) * fade;
			right = soft_clip ( right * 1.15f ) * fade;

			if ( glitch && in_bar >= 3.5 ) {

				left = math::round ( left * 14.0f ) / 14.0f;
				right = math::round ( right * 14.0f ) / 14.0f;

			}

			out [ i * 2 ] = left;
			out [ i * 2 + 1 ] = right;

		}

	}

	levels measure ( const song& source, double seconds ) {

		levels result;

		if ( seconds < 0.0 || seconds >= source.duration )
			return result;

		double beat = 60.0 / static_cast < double > ( source.bpm );
		int32_t total_bars = base::max ( static_cast < int32_t > ( source.duration / ( beat * 4.0 ) ), 1 );
		double beats = seconds / beat;
		int32_t bar = static_cast < int32_t > ( beats / 4.0 );
		double in_bar = beats - static_cast < double > ( bar ) * 4.0;
		section part = arrange ( source, bar, total_bars );
		float fade = math::saturate ( static_cast < float > ( seconds / 1.5 ) ) * math::saturate ( static_cast < float > ( ( source.duration - seconds ) / 3.5 ) );
		int32_t start = 0;
		int32_t next = 0;

		result.pad = ( 0.45f + 0.15f * tone ( 0.21, seconds ) ) * part.pad * fade;

		if ( part.bass && find_step ( source.bass_pattern, 8, in_bar * 2.0, start, next ) ) {

			double since = ( in_bar * 2.0 - start ) * beat * 0.5;

			result.bass = math::exp ( static_cast < float > ( -since * 4.2 ) ) * fade;

		}

		if ( part.arp && find_step ( source.arp_pattern, 16, in_bar * 4.0, start, next ) ) {

			double since = ( in_bar * 4.0 - start ) * beat * 0.25;

			result.lead = math::exp ( static_cast < float > ( -since * 6.5 ) ) * fade;

		}

		float drums = 0.0f;

		if ( part.kick ) {

			double period = source.four_floor ? 1.0 : 2.0;
			double since = ( in_bar - floor_double ( in_bar / period ) * period ) * beat;

			drums = math::exp ( static_cast < float > ( -since * 7.5 ) );

		}

		if ( part.hats ) {

			double division = source.sixteenth_hats ? 4.0 : 2.0;
			double position = in_bar * division;
			int32_t step = static_cast < int32_t > ( position );

			if ( ( source.hat_pattern >> ( step & 15 ) ) & 1 )
				drums = base::max ( drums, math::exp ( static_cast < float > ( -( position - step ) * beat / division * 30.0 ) ) * 0.55f );

		}

		if ( part.clap ) {

			double shifted = in_bar + 1.0;
			double since = ( shifted - floor_double ( shifted / 2.0 ) * 2.0 ) * beat;

			drums = base::max ( drums, math::exp ( static_cast < float > ( -since * 12.0 ) ) * 0.8f );

		}

		result.drums = drums * fade;

		return result;

	}

}