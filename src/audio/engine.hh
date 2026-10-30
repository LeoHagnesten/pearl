#pragma once

#include "audio/synth.hh"

namespace audio {

	bool start ( bool device );

	void stop ( );

	void open ( const synth::song& source, double at, bool play );

	void open_file ( const wchar_t* path, double duration, double at, bool play );

	double duration ( );

	void levels ( float* out );

	void play ( bool playing );

	void seek ( double seconds );

	void volume ( float value );

	void tick ( double seconds );

	double position ( );

	bool playing ( );

	bool finished ( );

	bool live ( );

}