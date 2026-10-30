#include "base/json.hh"

namespace json {

	void document::skip ( ) {

		while ( cursor < end && ( *cursor == ' ' || *cursor == '\n' || *cursor == '\r' || *cursor == '\t' ) )
			cursor++;

	}

	static int32_t hex_value ( char value ) {

		if ( value >= '0' && value <= '9' )
			return value - '0';

		if ( value >= 'a' && value <= 'f' )
			return value - 'a' + 10;

		if ( value >= 'A' && value <= 'F' )
			return value - 'A' + 10;

		return -1;

	}

	int32_t document::parse_text ( ) {

		if ( cursor >= end || *cursor != '"' ) {

			failed = true;

			return -1;

		}

		cursor++;

		int32_t start = pool.count ( );

		while ( cursor < end && *cursor != '"' ) {

			uint32_t code = static_cast < uint8_t > ( *cursor++ );

			if ( code == '\\' ) {

				if ( cursor >= end ) {

					failed = true;

					return -1;

				}

				char escape = *cursor++;

				switch ( escape ) {

					case 'n':
						code = '\n';
						break;

					case 't':
						code = '\t';
						break;

					case 'r':
						code = '\r';
						break;

					case 'b':
						code = 8;
						break;

					case 'f':
						code = 12;
						break;

					case 'u': {

						if ( end - cursor < 4 ) {

							failed = true;

							return -1;

						}

						code = 0;

						for ( int32_t i = 0; i < 4; i++ ) {

							int32_t digit = hex_value ( *cursor++ );

							if ( digit < 0 ) {

								failed = true;

								return -1;

							}

							code = code * 16 + static_cast < uint32_t > ( digit );

						}

						pool.push ( static_cast < wchar_t > ( code ) );

						continue;

					}

					default:
						code = static_cast < uint8_t > ( escape );
						break;

				}

				pool.push ( static_cast < wchar_t > ( code ) );

				continue;

			}

			if ( code >= 0x80 ) {

				int32_t extra = code >= 0xF0 ? 3 : ( code >= 0xE0 ? 2 : ( code >= 0xC0 ? 1 : 0 ) );

				code &= extra == 3 ? 0x07 : ( extra == 2 ? 0x0F : 0x1F );

				for ( int32_t i = 0; i < extra && cursor < end; i++ )
					code = ( code << 6 ) | ( static_cast < uint8_t > ( *cursor++ ) & 0x3F );

				if ( code >= 0x10000 ) {

					code -= 0x10000;
					pool.push ( static_cast < wchar_t > ( 0xD800 + ( code >> 10 ) ) );
					pool.push ( static_cast < wchar_t > ( 0xDC00 + ( code & 0x3FF ) ) );

					continue;

				}

			}

			pool.push ( static_cast < wchar_t > ( code ) );

		}

		if ( cursor >= end ) {

			failed = true;

			return -1;

		}

		cursor++;
		pool.push ( 0 );

		return start;

	}

	int32_t document::parse_value ( ) {

		skip ( );

		if ( cursor >= end || failed || depth > 64 ) {

			failed = true;

			return -1;

		}

		int32_t index = nodes.count ( );

		nodes.emplace ( );

		char value = *cursor;

		if ( value == '{' || value == '[' ) {

			bool object = value == '{';
			int32_t last = -1;

			nodes [ static_cast < size_t > ( index ) ].type = object ? kind::OBJECT : kind::ARRAY;
			cursor++;
			depth++;
			skip ( );

			if ( cursor < end && *cursor == ( object ? '}' : ']' ) ) {

				cursor++;
				depth--;

				return index;

			}

			while ( !failed ) {

				int32_t name = -1;

				skip ( );

				if ( object ) {

					name = parse_text ( );
					skip ( );

					if ( cursor >= end || *cursor != ':' ) {

						failed = true;

						break;

					}

					cursor++;

				}

				int32_t child = parse_value ( );

				if ( child < 0 )
					break;

				nodes [ static_cast < size_t > ( child ) ].key = name;

				if ( last < 0 )
					nodes [ static_cast < size_t > ( index ) ].child = child;
				else
					nodes [ static_cast < size_t > ( last ) ].next = child;

				last = child;
				nodes [ static_cast < size_t > ( index ) ].count++;
				skip ( );

				if ( cursor < end && *cursor == ',' ) {

					cursor++;

					continue;

				}

				if ( cursor < end && *cursor == ( object ? '}' : ']' ) ) {

					cursor++;

					break;

				}

				failed = true;

			}

			depth--;

			return failed ? -1 : index;

		}

		if ( value == '"' ) {

			int32_t text = parse_text ( );

			nodes [ static_cast < size_t > ( index ) ].type = kind::TEXT;
			nodes [ static_cast < size_t > ( index ) ].value = text;

			return failed ? -1 : index;

		}

		if ( value == 't' || value == 'f' || value == 'n' ) {

			int32_t length = value == 'f' ? 5 : 4;

			if ( end - cursor < length ) {

				failed = true;

				return -1;

			}

			nodes [ static_cast < size_t > ( index ) ].type = value == 'n' ? kind::EMPTY : kind::BOOLEAN;
			nodes [ static_cast < size_t > ( index ) ].number = value == 't' ? 1.0 : 0.0;
			cursor += length;

			return index;

		}

		bool negative = false;
		double number = 0.0;
		double scale = 0.0;
		int32_t exponent = 0;
		bool exponent_negative = false;

		if ( *cursor == '-' ) {

			negative = true;
			cursor++;

		}

		while ( cursor < end ) {

			char digit = *cursor;

			if ( digit >= '0' && digit <= '9' ) {

				if ( scale > 0.0 ) {

					number += static_cast < double > ( digit - '0' ) * scale;
					scale *= 0.1;

				} else
					number = number * 10.0 + static_cast < double > ( digit - '0' );

				cursor++;

			} else if ( digit == '.' ) {

				scale = 0.1;
				cursor++;

			} else if ( digit == 'e' || digit == 'E' ) {

				cursor++;

				if ( cursor < end && ( *cursor == '-' || *cursor == '+' ) )
					exponent_negative = *cursor++ == '-';

				while ( cursor < end && *cursor >= '0' && *cursor <= '9' )
					exponent = exponent * 10 + ( *cursor++ - '0' );

				break;

			} else
				break;

		}

		for ( int32_t i = 0; i < exponent && i < 300; i++ )
			number = exponent_negative ? number * 0.1 : number * 10.0;

		nodes [ static_cast < size_t > ( index ) ].type = kind::NUMBER;
		nodes [ static_cast < size_t > ( index ) ].number = negative ? -number : number;

		return index;

	}

	bool document::parse ( const char* text, int32_t size ) {

		nodes.clear ( );
		pool.clear ( );
		cursor = text;
		end = text + size;
		failed = false;
		depth = 0;

		if ( !text || size <= 0 )
			return false;

		parse_value ( );

		return !failed && !nodes.empty ( );

	}

	int32_t document::root ( ) const {

		return nodes.empty ( ) || failed ? -1 : 0;

	}

	static bool matches ( const wchar_t* name, const char* wanted ) {

		while ( *name && *wanted && *name == static_cast < wchar_t > ( static_cast < uint8_t > ( *wanted ) ) ) {

			name++;
			wanted++;

		}

		return *name == 0 && *wanted == 0;

	}

	int32_t document::find ( int32_t at, const char* name ) const {

		if ( at < 0 || nodes [ static_cast < size_t > ( at ) ].type != kind::OBJECT )
			return -1;

		for ( int32_t child = nodes [ static_cast < size_t > ( at ) ].child; child >= 0; child = nodes [ static_cast < size_t > ( child ) ].next ) {

			int32_t key = nodes [ static_cast < size_t > ( child ) ].key;

			if ( key >= 0 && matches ( pool.data ( ) + key, name ) )
				return child;

		}

		return -1;

	}

	int32_t document::item ( int32_t at, int32_t index ) const {

		int32_t child = first ( at );

		for ( int32_t i = 0; child >= 0 && i < index; i++ )
			child = next ( child );

		return child;

	}

	int32_t document::first ( int32_t at ) const {

		return at < 0 ? -1 : nodes [ static_cast < size_t > ( at ) ].child;

	}

	int32_t document::next ( int32_t at ) const {

		return at < 0 ? -1 : nodes [ static_cast < size_t > ( at ) ].next;

	}

	int32_t document::count ( int32_t at ) const {

		return at < 0 ? 0 : nodes [ static_cast < size_t > ( at ) ].count;

	}

	kind document::type ( int32_t at ) const {

		return at < 0 ? kind::NONE : nodes [ static_cast < size_t > ( at ) ].type;

	}

	const wchar_t* document::text ( int32_t at ) const {

		if ( at < 0 || nodes [ static_cast < size_t > ( at ) ].type != kind::TEXT || nodes [ static_cast < size_t > ( at ) ].value < 0 )
			return L"";

		return pool.data ( ) + nodes [ static_cast < size_t > ( at ) ].value;

	}

	const wchar_t* document::key ( int32_t at ) const {

		if ( at < 0 || nodes [ static_cast < size_t > ( at ) ].key < 0 )
			return L"";

		return pool.data ( ) + nodes [ static_cast < size_t > ( at ) ].key;

	}

	double document::number ( int32_t at, double fallback ) const {

		if ( at < 0 || ( nodes [ static_cast < size_t > ( at ) ].type != kind::NUMBER && nodes [ static_cast < size_t > ( at ) ].type != kind::BOOLEAN ) )
			return fallback;

		return nodes [ static_cast < size_t > ( at ) ].number;

	}

	bool document::flag ( int32_t at, bool fallback ) const {

		if ( at < 0 || nodes [ static_cast < size_t > ( at ) ].type != kind::BOOLEAN )
			return fallback;

		return nodes [ static_cast < size_t > ( at ) ].number != 0.0;

	}

	int32_t document::path ( int32_t at, const char* first_name, const char* second_name ) const {

		return find ( find ( at, first_name ), second_name );

	}

}