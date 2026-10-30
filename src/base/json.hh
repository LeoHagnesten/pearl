#pragma once

#include "base/base.hh"

namespace json {

	enum class kind : int32_t {

		NONE,
		EMPTY,
		BOOLEAN,
		NUMBER,
		TEXT,
		ARRAY,
		OBJECT,

	};

	struct node {

		kind type = kind::NONE;
		int32_t key = -1;
		int32_t value = -1;
		int32_t child = -1;
		int32_t next = -1;
		int32_t count = 0;
		double number = 0.0;

	};

	class document {

	public:

		bool parse ( const char* text, int32_t size );

		int32_t root ( ) const;

		int32_t find ( int32_t at, const char* name ) const;

		int32_t item ( int32_t at, int32_t index ) const;

		int32_t first ( int32_t at ) const;

		int32_t next ( int32_t at ) const;

		int32_t count ( int32_t at ) const;

		kind type ( int32_t at ) const;

		const wchar_t* text ( int32_t at ) const;

		const wchar_t* key ( int32_t at ) const;

		double number ( int32_t at, double fallback = 0.0 ) const;

		bool flag ( int32_t at, bool fallback = false ) const;

		int32_t path ( int32_t at, const char* first_name, const char* second_name ) const;

	private:

		int32_t parse_value ( );

		int32_t parse_text ( );

		void skip ( );

		base::list < node > nodes;
		base::list < wchar_t > pool;
		const char* cursor = nullptr;
		const char* end = nullptr;
		bool failed = false;
		int32_t depth = 0;

	};

}