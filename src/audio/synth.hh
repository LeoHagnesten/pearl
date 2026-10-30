#pragma once

#include "base/base.hh"

namespace synth {

	struct song {

		uint32_t seed = 1;
		float duration = 180.0f;
		float bpm = 100.0f;
		int32_t root = 57;
		int32_t mode = 0;
		int32_t progression [ 4 ] = { 0, 5, 3, 4 };
		uint32_t bass_pattern = 0x11;
		uint32_t arp_pattern = 0xFFFF;
		uint32_t hat_pattern = 0x5555;
		bool four_floor = false;
		bool sixteenth_hats = false;
		float fm_ratio = 2.0f;
		float fm_depth = 2.0f;
		float weirdness = 0.5f;
		float warmth = 0.5f;
		float arp_octave = 12.0f;

	};

	struct levels {

		float bass = 0.0f;
		float pad = 0.0f;
		float lead = 0.0f;
		float drums = 0.0f;

	};

	void compose ( song& result, uint32_t seed, float duration, int32_t flavor );

	void render ( const song& source, int64_t first_frame, int32_t frames, int32_t rate, float* out );

	levels measure ( const song& source, double seconds );

}