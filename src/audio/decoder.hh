#pragma once

#include "base/base.hh"

namespace audio {

	void decoder_request ( const wchar_t* path, double seconds, int32_t rate, uint32_t ticket );

	int32_t decoder_pull ( uint32_t ticket, float* out, int32_t frames, bool& ended, int64_t& frame );

	double decoder_duration ( );

	bool decoder_failed ( uint32_t ticket );

}