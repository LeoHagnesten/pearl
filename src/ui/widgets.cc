#include "ui/app.hh"

namespace ui {

	static math::vec2 lerp_point ( math::vec2 a, math::vec2 b, float t ) {

		return math::vec2 ( a.x + ( b.x - a.x ) * t, a.y + ( b.y - a.y ) * t );

	}

	bool app::icon_button ( render::canvas& c, uint64_t id, math::vec2 center, float size, theme::glyph icon, float icon_size, render::color tint, const wchar_t* text, hit kind ) {

		math::rect area ( center.x - size * 0.5f, center.y - size * 0.5f, size, size );
		widget& item = state ( id );
		bool clicked = interact ( id, area, item, kind );

		if ( clicked )
			item.pop_velocity += 9.0f;

		spring ( item.pop, item.pop_velocity, 0.0f, 420.0f, 20.0f );
		tip ( id, text, area );

		float backdrop = item.hover * 0.055f + item.press * 0.05f;

		if ( backdrop > 0.001f )
			c.circle ( center, size * 0.5f * ( 0.86f + 0.14f * item.hover ), theme::shade.fade ( backdrop ) );

		float scale = 1.0f - 0.1f * item.press + 0.22f * item.pop + 0.1f * item.hover;

		c.push ( center, scale, 1.0f );
		c.icon ( theme::code ( icon ), center, icon_size, tint );
		c.pop ( );

		return clicked;

	}

	void app::speaker_shape ( render::canvas& c, math::vec2 center, float size, float one, float two, float mute, render::color tint ) {

		float s = size;
		math::vec2 o = center + math::vec2 ( -0.14f * s, 0.0f );
		float stroke = 0.085f * s;
		float voice = 1.0f - mute;

		c.fill ( math::rect ( o.x - 0.44f * s, o.y - 0.15f * s, 0.2f * s, 0.3f * s ), 0.05f * s, tint );
		c.polygon ( o + math::vec2 ( -0.27f * s, -0.15f * s ), o + math::vec2 ( 0.02f * s, -0.4f * s ), o + math::vec2 ( 0.02f * s, 0.4f * s ), o + math::vec2 ( -0.27f * s, 0.15f * s ), 0.04f * s, tint );

		if ( one * voice > 0.01f )
			c.ring ( o + math::vec2 ( 0.02f * s, 0.0f ), 0.28f * s * ( 0.8f + 0.2f * one ), stroke, 0.14f, 0.22f, tint.fade ( one * voice ) );

		if ( two * voice > 0.01f )
			c.ring ( o + math::vec2 ( 0.02f * s, 0.0f ), 0.5f * s * ( 0.86f + 0.14f * two ), stroke, 0.155f, 0.19f, tint.fade ( two * voice ) );

		if ( mute > 0.01f ) {

			math::vec2 at = o + math::vec2 ( 0.38f * s, 0.0f );
			float reach = 0.16f * s * ( 0.6f + 0.4f * mute );
			float half = stroke * 0.5f;

			c.polygon ( at + math::vec2 ( -reach - half, -reach + half ), at + math::vec2 ( -reach + half, -reach - half ), at + math::vec2 ( reach + half, reach - half ), at + math::vec2 ( reach - half, reach + half ), 0.01f * s, tint.fade ( mute ) );
			c.polygon ( at + math::vec2 ( reach - half, -reach - half ), at + math::vec2 ( reach + half, -reach + half ), at + math::vec2 ( -reach + half, reach + half ), at + math::vec2 ( -reach - half, reach - half ), 0.01f * s, tint.fade ( mute ) );

		}

	}

	bool app::speaker_button ( render::canvas& c, uint64_t id, math::vec2 center, float size, float icon_size, render::color tint, const wchar_t* text ) {

		math::rect area ( center.x - size * 0.5f, center.y - size * 0.5f, size, size );
		widget& item = state ( id );
		bool fresh = item.seen + 2 < frame_index;
		bool clicked = interact ( id, area, item );
		float shown = muted ? 0.0f : volume;
		float goals [ 3 ] = { shown > 0.001f ? 1.0f : 0.0f, shown > 0.5f ? 1.0f : 0.0f, shown <= 0.001f ? 1.0f : 0.0f };
		float* values [ 3 ] = { &item.value, &item.wave, &item.cross };
		float step = dt / 0.1f;

		for ( int32_t i = 0; i < 3; i++ ) {

			float& value = *values [ i ];

			if ( fresh )
				value = goals [ i ];
			else if ( value < goals [ i ] )
				value = base::min ( value + step, goals [ i ] );
			else if ( value > goals [ i ] )
				value = base::max ( value - step, goals [ i ] );

			if ( value != goals [ i ] )
				animating = true;

		}

		tip ( id, text, area );

		float backdrop = item.hover * 0.055f + item.press * 0.05f;

		if ( backdrop > 0.001f )
			c.circle ( center, size * 0.5f * ( 0.86f + 0.14f * item.hover ), theme::shade.fade ( backdrop ) );

		c.push ( center, 1.0f - 0.08f * item.press, 1.0f );
		speaker_shape ( c, center, icon_size, item.value, item.wave, item.cross, tint );
		c.pop ( );

		return clicked;

	}

	void app::play_shape ( render::canvas& c, math::vec2 center, float size, float morph, render::color tint ) {

		static const math::vec2 play_left [ 4 ] = { math::vec2 ( -0.27f, -0.40f ), math::vec2 ( 0.09f, -0.205f ), math::vec2 ( 0.09f, 0.205f ), math::vec2 ( -0.27f, 0.40f ) };
		static const math::vec2 play_right [ 4 ] = { math::vec2 ( 0.09f, -0.205f ), math::vec2 ( 0.45f, 0.0f ), math::vec2 ( 0.45f, 0.0f ), math::vec2 ( 0.09f, 0.205f ) };
		static const math::vec2 pause_left [ 4 ] = { math::vec2 ( -0.31f, -0.37f ), math::vec2 ( -0.09f, -0.37f ), math::vec2 ( -0.09f, 0.37f ), math::vec2 ( -0.31f, 0.37f ) };
		static const math::vec2 pause_right [ 4 ] = { math::vec2 ( 0.09f, -0.37f ), math::vec2 ( 0.31f, -0.37f ), math::vec2 ( 0.31f, 0.37f ), math::vec2 ( 0.09f, 0.37f ) };
		float t = math::saturate ( morph );
		math::vec2 left [ 4 ];
		math::vec2 right [ 4 ];

		for ( int32_t i = 0; i < 4; i++ ) {

			left [ i ] = center + lerp_point ( play_left [ i ], pause_left [ i ], t ) * size;
			right [ i ] = center + lerp_point ( play_right [ i ], pause_right [ i ], t ) * size;

		}

		float rounding = size * 0.065f;

		c.polygon ( left [ 0 ], left [ 1 ], left [ 2 ], left [ 3 ], rounding, tint );
		c.polygon ( right [ 0 ], right [ 1 ], right [ 2 ], right [ 3 ], rounding, tint );

	}

	void app::skip_shape ( render::canvas& c, math::vec2 center, float size, bool forward, render::color tint ) {

		float s = forward ? 1.0f : -1.0f;
		float rounding = size * 0.06f;

		for ( int32_t k = 0; k < 2; k++ ) {

			float base_x = k == 0 ? -0.44f : 0.0f;
			math::vec2 a = center + math::vec2 ( base_x * s, -0.29f ) * size;
			math::vec2 b = center + math::vec2 ( ( base_x + 0.42f ) * s, 0.0f ) * size;
			math::vec2 d = center + math::vec2 ( base_x * s, 0.29f ) * size;

			c.polygon ( a, b, b, d, rounding, tint );

		}

	}

	bool app::play_button ( render::canvas& c, uint64_t id, math::vec2 center, float radius, bool showing_pause, bool dark, const wchar_t* text ) {

		math::rect area ( center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f );
		widget& item = state ( id );
		bool fresh = item.seen + 2 < frame_index;
		bool clicked = interact ( id, area, item );

		if ( fresh ) {

			item.value = showing_pause ? 1.0f : 0.0f;
			item.velocity = 0.0f;

		}

		if ( clicked )
			item.pop_velocity -= 5.0f;

		spring ( item.value, item.velocity, showing_pause ? 1.0f : 0.0f, 360.0f, 24.0f );
		spring ( item.pop, item.pop_velocity, 0.0f, 380.0f, 17.0f );
		tip ( id, text, area );

		float scale = 1.0f + 0.07f * item.hover - 0.07f * item.press + 0.3f * item.pop;
		render::color body = dark ? theme::ink : theme::white;
		render::color glyph = dark ? theme::white : theme::ink;

		c.push ( center, scale, 1.0f );
		c.shadow ( area, radius, radius * 0.4f, theme::shade.fade ( 0.16f + 0.08f * item.hover ), math::vec2 ( 0.0f, radius * 0.22f ) );
		c.circle ( center, radius, body );

		if ( !dark )
			c.outline ( area, radius, render::color ( 0.0f, 0.0f, 0.0f, 0.0f ), 1.0f, theme::shade.fade ( 0.06f ) );

		play_shape ( c, center + math::vec2 ( ( 1.0f - item.value ) * radius * 0.04f, 0.0f ), radius * 0.78f, item.value, glyph );
		c.pop ( );

		return clicked;

	}

	bool app::skip_button ( render::canvas& c, uint64_t id, math::vec2 center, float size, bool forward, render::color tint, const wchar_t* text ) {

		math::rect area ( center.x - size * 0.5f, center.y - size * 0.5f, size, size );
		widget& item = state ( id );
		bool clicked = interact ( id, area, item );

		if ( clicked )
			item.pop_velocity += 10.0f;

		spring ( item.pop, item.pop_velocity, 0.0f, 360.0f, 19.0f );
		tip ( id, text, area );

		float backdrop = item.hover * 0.055f + item.press * 0.05f;

		if ( backdrop > 0.001f )
			c.circle ( center, size * 0.5f * ( 0.86f + 0.14f * item.hover ), theme::shade.fade ( backdrop ) );

		float scale = 1.0f - 0.1f * item.press + 0.08f * item.hover;
		float shift = item.pop * size * 0.14f * ( forward ? 1.0f : -1.0f );

		c.push ( center, scale, 1.0f, math::vec2 ( shift, 0.0f ) );
		skip_shape ( c, center, size * 0.46f, forward, tint );
		c.pop ( );

		return clicked;

	}

	float app::slider ( render::canvas& c, uint64_t id, math::rect area, float value, bool& dragging, render::color fill ) {

		widget& item = state ( id );

		interact ( id, area, item );

		dragging = active_id == id && mouse_down;

		if ( dragging )
			value = math::saturate ( ( mouse.x - area.x ) / base::max ( area.w, 1.0f ) );

		approach ( item.value, dragging || item.hover > 0.5f ? 1.0f : 0.0f, 18.0f );

		float thickness = 4.0f + 2.0f * item.value;
		float cy = area.y + area.h * 0.5f;
		math::rect track_area ( area.x, cy - thickness * 0.5f, area.w, thickness );

		c.fill ( track_area, thickness * 0.5f, theme::shade.fade ( 0.11f ) );
		c.fill ( math::rect ( area.x, track_area.y, base::max ( area.w * value, thickness ), thickness ), thickness * 0.5f, fill );

		float knob = 6.5f * item.value;

		if ( knob > 0.3f ) {

			math::vec2 at ( area.x + area.w * value, cy );
			math::rect knob_area ( at.x - knob, at.y - knob, knob * 2.0f, knob * 2.0f );

			c.shadow ( knob_area, knob, 2.5f, theme::shade.fade ( 0.28f * item.value ), math::vec2 ( 0.0f, 1.0f ) );
			c.circle ( at, knob, theme::white );

		}

		return value;

	}

	void app::equalizer ( render::canvas& c, math::vec2 center, float size, render::color tint ) {

		float bar = size * 0.2f;
		float gap = size * 0.12f;
		float left = center.x - ( bar * 3.0f + gap * 2.0f ) * 0.5f;
		float bottom = center.y + size * 0.5f;

		for ( int32_t i = 0; i < 3; i++ ) {

			float height_now = size * ( 0.3f + 0.7f * math::saturate ( levels [ i ] ) );
			float x = left + static_cast < float > ( i ) * ( bar + gap );

			height_now = base::max ( height_now, bar );
			c.fill ( math::rect ( x, bottom - height_now, bar, height_now ), bar * 0.5f, tint );

		}

	}

	void app::draw_brand ( render::canvas& c, math::vec2 center, float radius ) {

		pearl_mark ( c, center, radius );

	}

	void app::heart_shape ( render::canvas& c, math::vec2 center, float size, float fill ) {

		float amount = math::saturate ( fill );
		float bloom = 1.0f - ( 1.0f - amount ) * ( 1.0f - amount ) * ( 1.0f - amount );
		render::color outline = render::mix ( theme::ink_soft, theme::heart, math::saturate ( amount * 3.0f ) );

		c.icon ( theme::code ( theme::glyph::HEART ), center, size, outline );

		if ( bloom <= 0.001f )
			return;

		c.push ( center, bloom, math::saturate ( amount * 2.5f ) );
		c.icon ( theme::code ( theme::glyph::HEART_FILL ), center, size, theme::heart );
		c.pop ( );

	}

	bool app::heart_button ( render::canvas& c, uint64_t id, math::vec2 center, float size, float icon_size, bool liked, const wchar_t* text ) {

		math::rect area ( center.x - size * 0.5f, center.y - size * 0.5f, size, size );
		widget& item = state ( id );
		bool fresh = item.seen + 2 < frame_index;
		bool clicked = interact ( id, area, item );

		if ( fresh )
			item.value = liked ? 1.0f : 0.0f;

		float goal = liked ? 1.0f : 0.0f;

		if ( clicked )
			goal = 1.0f - goal;

		float step = dt / 0.1f;

		if ( item.value < goal )
			item.value = base::min ( item.value + step, goal );
		else if ( item.value > goal )
			item.value = base::max ( item.value - step, goal );

		if ( item.value != goal )
			animating = true;

		tip ( id, text, area );

		float backdrop = item.hover * 0.05f + item.press * 0.05f;

		if ( backdrop > 0.001f )
			c.circle ( center, size * 0.5f * ( 0.86f + 0.14f * item.hover ), theme::shade.fade ( backdrop ) );

		heart_shape ( c, center, icon_size, item.value );

		return clicked;

	}

	void app::pearl_mark ( render::canvas& c, math::vec2 center, float radius ) {

		math::rect area ( center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f );
		float r = radius;

		c.shadow ( area, r, r * 0.3f, theme::shade.fade ( 0.24f ), math::vec2 ( 0.0f, r * 0.22f ) );
		c.gradient ( area, r, math::vec2 ( area.x + r * 0.2f, area.y ), math::vec2 ( area.right ( ) - r * 0.2f, area.bottom ( ) ), render::rgb ( 0xF3F4F8 ), render::rgb ( 0xA7AEC2 ) );
		c.shadow ( math::rect ( center.x - r * 0.1f, center.y + r * 0.05f, r * 0.95f, r * 0.8f ), r * 0.4f, r * 0.22f, render::rgb ( 0xF4B8D6, 0.5f ) );
		c.shadow ( math::rect ( center.x - r * 0.85f, center.y + r * 0.1f, r * 0.7f, r * 0.6f ), r * 0.3f, r * 0.2f, render::rgb ( 0xB8D4F4, 0.35f ) );
		c.shadow ( math::rect ( center.x - r * 0.6f, center.y - r * 0.68f, r * 0.62f, r * 0.46f ), r * 0.23f, r * 0.07f, theme::white );
		c.shadow ( math::rect ( center.x - r * 0.48f, center.y - r * 0.6f, r * 0.3f, r * 0.2f ), r * 0.1f, r * 0.05f, theme::white );
		c.shadow ( math::rect ( center.x - r * 0.55f, center.y + r * 0.62f, r * 1.1f, r * 0.22f ), r * 0.11f, r * 0.12f, theme::white.fade ( 0.55f ) );
		c.outline ( area, r, render::color ( 0.0f, 0.0f, 0.0f, 0.0f ), base::max ( r * 0.035f, 1.0f ), theme::shade.fade ( 0.08f ) );

	}

	void app::marquee ( render::canvas& c, const wchar_t* value, render::font role, math::vec2 at, render::color tint, float limit, bool fade, render::color backdrop ) {

		float text_width = c.measure ( value, role );

		if ( text_width <= limit + 0.5f ) {

			c.text ( value, role, at, tint );

			return;

		}

		float gap = 40.0f;
		float speed = 30.0f;
		float pause = 2.2f;
		float travel = text_width + gap;
		double cycle = static_cast < double > ( pause + travel / speed );
		double elapsed = clock - marquee_start;
		double t = elapsed - __builtin_floor ( elapsed / cycle ) * cycle;
		float offset = t < pause ? 0.0f : static_cast < float > ( t - pause ) * speed;

		marquee_active = true;

		float edge = fade ? 18.0f : 0.0f;
		float lead = fade ? math::saturate ( offset / edge ) * math::saturate ( ( travel - offset ) / edge ) : 0.0f;
		int32_t strips = fade ? 9 : 0;
		float strip = edge / static_cast < float > ( base::max ( strips, 1 ) );
		float left_edge = edge * lead;

		c.snap = false;

		for ( int32_t part = -strips; part <= strips; part++ ) {

			float x0 = at.x + left_edge;
			float x1 = at.x + limit - edge;
			float alpha = 1.0f;

			if ( part < 0 ) {

				int32_t k = part + strips;
				float amount = ( static_cast < float > ( k ) + 0.5f ) / static_cast < float > ( strips );

				x0 = at.x + left_edge * static_cast < float > ( k ) / static_cast < float > ( strips );
				x1 = at.x + left_edge * static_cast < float > ( k + 1 ) / static_cast < float > ( strips );
				alpha = 1.0f - lead * ( 1.0f - amount );

			} else if ( part > 0 ) {

				int32_t k = part - 1;

				x0 = at.x + limit - edge + strip * static_cast < float > ( k );
				x1 = x0 + strip;
				alpha = 1.0f - ( static_cast < float > ( k ) + 0.5f ) / static_cast < float > ( strips );

			}

			if ( x1 - x0 < 0.25f || alpha <= 0.01f )
				continue;

			c.push_clip ( math::rect ( x0, at.y - 16.0f, x1 - x0, 32.0f ) );
			c.text ( value, role, math::vec2 ( at.x - offset, at.y ), tint.fade ( alpha ) );
			c.text ( value, role, math::vec2 ( at.x - offset + travel, at.y ), tint.fade ( alpha ) );
			c.pop_clip ( );

		}

		c.snap = true;

	}

	void app::draw_tip ( render::canvas& c ) {

		if ( tip_show <= 0.01f || !tip_text )
			return;

		float text_width = c.measure ( tip_text, render::font::SMALL_STRONG );
		float bubble_w = text_width + 22.0f;
		float bubble_h = 28.0f;
		float x = base::clamp ( tip_anchor.center ( ).x - bubble_w * 0.5f, 8.0f, w - bubble_w - 8.0f );
		float y = tip_anchor.bottom ( ) + 8.0f;
		bool above = y + bubble_h > h - 8.0f || tip_anchor.y > h * 0.6f;

		if ( above )
			y = tip_anchor.y - 8.0f - bubble_h;

		math::rect bubble ( x, y, bubble_w, bubble_h );
		float e = tip_show;
		math::vec2 pivot ( tip_anchor.center ( ).x, above ? bubble.bottom ( ) : bubble.y );

		c.push ( pivot, 0.86f + 0.14f * e, e, math::vec2 ( 0.0f, ( above ? 4.0f : -4.0f ) * ( 1.0f - e ) ) );
		c.shadow ( bubble, 14.0f, 10.0f, theme::shade.fade ( 0.14f ), math::vec2 ( 0.0f, 4.0f ) );
		c.glass ( bubble, 14.0f, theme::bubble ( ) );
		c.text ( tip_text, render::font::SMALL_STRONG, math::vec2 ( bubble.center ( ).x, bubble.center ( ).y ), theme::ink, render::align::CENTER );
		c.pop ( );

	}

	bool app::pill_button ( render::canvas& c, uint64_t id, math::rect area, const wchar_t* label, theme::glyph icon, float morph, bool dark, bool active ) {

		widget& item = state ( id );
		bool fresh = item.seen + 2 < frame_index;
		bool clicked = interact ( id, area, item );

		if ( fresh && morph >= 0.0f ) {

			item.value = morph;
			item.velocity = 0.0f;

		}

		if ( clicked )
			item.pop_velocity -= 4.0f;

		spring ( item.pop, item.pop_velocity, 0.0f, 380.0f, 18.0f );

		if ( morph >= 0.0f )
			spring ( item.value, item.velocity, morph, 360.0f, 24.0f );

		float scale = 1.0f + 0.03f * item.hover - 0.05f * item.press + 0.25f * item.pop;
		float radius = area.h * 0.5f;
		render::color ink = dark ? theme::white : theme::ink;

		c.push ( area.center ( ), scale, 1.0f );

		if ( dark ) {

			c.shadow ( area, radius, 10.0f, theme::shade.fade ( 0.16f + 0.06f * item.hover ), math::vec2 ( 0.0f, 4.0f ) );
			c.fill ( area, radius, render::mix ( theme::ink, render::rgb ( 0x3A3A3C ), item.hover * 0.6f ) );

		} else {

			c.shadow ( area, radius, 8.0f, theme::shade.fade ( 0.06f + 0.04f * item.hover ), math::vec2 ( 0.0f, 2.0f ) );
			c.fill ( area, radius, theme::white.fade ( 0.6f + 0.3f * item.hover ) );
			c.outline ( area, radius, render::color ( 0.0f, 0.0f, 0.0f, 0.0f ), 1.0f, active ? theme::ink.fade ( 0.55f ) : theme::shade.fade ( 0.07f ) );

		}

		float label_w = c.measure ( label, render::font::BODY_STRONG );
		bool has_icon = morph >= 0.0f || icon != theme::glyph::NONE;
		float icon_w = has_icon ? 14.0f : 0.0f;
		float gap = has_icon ? 9.0f : 0.0f;
		float start = area.center ( ).x - ( icon_w + gap + label_w ) * 0.5f;
		float cy = area.center ( ).y;

		if ( morph >= 0.0f )
			play_shape ( c, math::vec2 ( start + 7.0f, cy ), 15.0f, item.value, ink );
		else if ( icon != theme::glyph::NONE )
			c.icon ( theme::code ( icon ), math::vec2 ( start + 7.0f, cy ), 14.0f, ink );

		c.text ( label, render::font::BODY_STRONG, math::vec2 ( start + icon_w + gap, cy ), ink );
		c.pop ( );

		return clicked;

	}

	void app::disc ( render::canvas& c, math::rect area, int32_t album, float angle, float alpha ) {

		float radius = area.w * 0.5f;
		math::vec2 center = area.center ( );
		float hole = radius * 0.17f;
		math::rect hole_area ( center.x - hole, center.y - hole, hole * 2.0f, hole * 2.0f );

		cover ( c, area, radius, album, alpha, angle );
		c.soft ( math::rect ( area.x + radius * 0.2f, area.y + radius * 0.12f, radius * 0.9f, radius * 0.5f ), radius * 0.25f, radius * 0.35f, theme::white.fade ( 0.22f * alpha ) );
		c.outline ( area, radius, render::color ( 0.0f, 0.0f, 0.0f, 0.0f ), 1.0f, theme::white.fade ( 0.45f * alpha ) );
		c.circle ( center, hole, theme::base.fade ( 0.96f * alpha ) );
		c.outline ( hole_area, hole, render::color ( 0.0f, 0.0f, 0.0f, 0.0f ), 1.0f, theme::shade.fade ( 0.14f * alpha ) );

	}

}