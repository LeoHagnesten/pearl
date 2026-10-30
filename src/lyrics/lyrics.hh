#pragma once

#include "base/base.hh"

namespace lyrics {

	struct line {

		double time = -1.0;
		base::text words;

	};

	struct sheet {

		uint64_t key = 0;
		uint32_t revision = 0;
		bool loading = false;
		bool found = false;
		bool synced = false;
		bool instrumental = false;
		base::list < line > lines;

	};

	void request ( uint64_t key, const wchar_t* title, const wchar_t* artist, const wchar_t* album, double duration );

	uint32_t revision ( );

	void read ( sheet& out );

}