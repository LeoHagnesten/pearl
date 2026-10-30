#pragma once

#include "render/render.hh"

namespace theme {

	enum class glyph : uint32_t {

		NONE = 0,
		HOME = 0xE80F,
		HEART = 0xEB51,
		HEART_FILL = 0xEB52,
		SEARCH = 0xE721,
		SHUFFLE = 0xE8B1,
		REPEAT_ALL = 0xE8EE,
		REPEAT_ONE = 0xE8ED,
		MUTE = 0xE74F,
		VOLUME_0 = 0xE992,
		VOLUME_1 = 0xE993,
		VOLUME_2 = 0xE994,
		VOLUME_3 = 0xE995,
		MINIMIZE = 0xE921,
		MAXIMIZE = 0xE922,
		RESTORE = 0xE923,
		CLOSE = 0xE8BB,
		MORE = 0xE712,
		CLOCK = 0xE823,
		CHEVRON_DOWN = 0xE70D,
		CHEVRON_LEFT = 0xE76B,
		CHEVRON_RIGHT = 0xE76C,
		QUEUE = 0xE8FD,
		ALBUM = 0xE93C,
		EXPAND = 0xE740,
		SPEAKER = 0xE767,
		MUSIC = 0xE8D6,
		FOLDER = 0xE8B7,
		LINK = 0xE71B,
		ADD = 0xE710,
		REMOVE = 0xE738,
		TRASH = 0xE74D,
		EDIT = 0xE70F,
		BACK = 0xE72B,
		COPY = 0xE8C8,
		CHECK = 0xE73E,
		NEXT_UP = 0xE7F7,
		OPEN = 0xE8A7,
		GLOBE = 0xE774,
		LIBRARY = 0xE8F1,
		REFRESH = 0xE72C,

	};

	constexpr uint32_t code ( glyph value ) {

		return static_cast < uint32_t > ( value );

	}

	constexpr render::color base = render::rgb ( 0xF6F6F8 );
	constexpr float base_alpha = 0.78f;
	constexpr render::color underlay = render::rgb ( 0xECECF0 );
	constexpr render::color ink = render::rgb ( 0x1D1D1F );
	constexpr render::color ink_soft = render::rgb ( 0x6E6E73 );
	constexpr render::color ink_faint = render::rgb ( 0xAEAEB2 );
	constexpr render::color hairline = render::rgb ( 0x000000, 0.075f );
	constexpr render::color hover = render::rgb ( 0x000000, 0.045f );
	constexpr render::color pressed = render::rgb ( 0x000000, 0.085f );
	constexpr render::color heart = render::rgb ( 0xFF2D55 );
	constexpr render::color danger = render::rgb ( 0xE5484D );
	constexpr render::color white = render::rgb ( 0xFFFFFF );
	constexpr render::color shade = render::rgb ( 0x000000 );

	inline render::glass_style panel ( ) {

		render::glass_style style;

		style.tint = render::color ( 1.0f, 1.0f, 1.0f, 0.56f );
		style.frost = 1.0f;
		style.refraction = 26.0f;
		style.bevel = 20.0f;
		style.dispersion = 0.22f;
		style.highlight = 1.0f;

		return style;

	}

	inline render::glass_style control ( ) {

		render::glass_style style;

		style.tint = render::color ( 1.0f, 1.0f, 1.0f, 0.5f );
		style.frost = 0.85f;
		style.refraction = 12.0f;
		style.bevel = 11.0f;
		style.dispersion = 0.28f;
		style.highlight = 1.15f;

		return style;

	}

	inline render::glass_style bubble ( ) {

		render::glass_style style;

		style.tint = render::color ( 1.0f, 1.0f, 1.0f, 0.72f );
		style.frost = 1.0f;
		style.refraction = 10.0f;
		style.bevel = 9.0f;
		style.dispersion = 0.15f;
		style.highlight = 1.0f;

		return style;

	}

}