cbuffer frame : register ( b0 ) {

	float2 target_size;
	float2 source_size;
	float4 light;
	float4 tuning;

};

Texture2D source_texture : register ( t0 );
Texture2D blur_texture : register ( t1 );
Texture2DArray cover_texture : register ( t2 );
Texture2DArray thumb_texture : register ( t3 );
SamplerState linear_clamp : register ( s0 );

struct instance {

	float4 rect : RECT;
	float4 shape : SHAPE;
	float4 fill : FILL;
	float4 stroke : STROKE;
	float4 data : DATA;
	float4 extra : EXTRA;
	float4 clip : CLIP;

};

struct vs_out {

	float4 position : SV_Position;
	float2 pixel : PIXEL;
	nointerpolation float4 rect : RECT;
	nointerpolation float4 shape : SHAPE;
	nointerpolation float4 fill : FILL;
	nointerpolation float4 stroke : STROKE;
	nointerpolation float4 data : DATA;
	nointerpolation float4 extra : EXTRA;
	nointerpolation float4 clip : CLIP;

};

static const uint KIND_FILL = 0;
static const uint KIND_SHADOW = 1;
static const uint KIND_IMAGE = 2;
static const uint KIND_TEXTURE = 3;
static const uint KIND_GLASS = 4;
static const uint KIND_GRADIENT = 5;
static const uint KIND_POLYGON = 6;
static const uint KIND_RING = 7;

vs_out vs_main ( instance input, uint vertex : SV_VertexID ) {

	vs_out output;
	uint kind = ( uint ) input.shape.w;
	float pad = 1.5;

	if ( kind == KIND_SHADOW )
		pad = input.shape.y * 3.0 + 2.0;

	float2 corner = float2 ( vertex & 1, vertex >> 1 );
	float2 low = input.rect.xy - pad;
	float2 high = input.rect.xy + input.rect.zw + pad;

	low = max ( low, input.clip.xy - 1.0 );
	high = min ( high, input.clip.zw + 1.0 );
	high = max ( high, low );

	float2 p = lerp ( low, high, corner );

	output.position = float4 ( p / target_size * float2 ( 2.0, -2.0 ) + float2 ( -1.0, 1.0 ), 0.0, 1.0 );
	output.pixel = p;
	output.rect = input.rect;
	output.shape = input.shape;
	output.fill = input.fill;
	output.stroke = input.stroke;
	output.data = input.data;
	output.extra = input.extra;
	output.clip = input.clip;

	return output;

}

float rounded_box ( float2 p, float2 half_size, float radius ) {

	float2 q = abs ( p ) - half_size + radius;

	return length ( max ( q, 0.0 ) ) + min ( max ( q.x, q.y ), 0.0 ) - radius;

}

float2 rounded_box_normal ( float2 p, float2 half_size, float radius ) {

	float2 s = float2 ( p.x >= 0.0 ? 1.0 : -1.0, p.y >= 0.0 ? 1.0 : -1.0 );
	float2 q = abs ( p ) - half_size + radius;

	if ( q.x > 0.0 && q.y > 0.0 )
		return s * normalize ( q );

	if ( q.x > q.y )
		return float2 ( s.x, 0.0 );

	return float2 ( 0.0, s.y );

}

float gaussian ( float x, float sigma ) {

	return exp ( -( x * x ) / ( 2.0 * sigma * sigma ) ) / ( 2.5066282746 * sigma );

}

float2 erf2 ( float2 x ) {

	float2 s = sign ( x );
	float2 a = abs ( x );

	x = 1.0 + ( 0.278393 + ( 0.230389 + 0.078108 * ( a * a ) ) * a ) * a;
	x *= x;

	return s - s / ( x * x );

}

float shadow_x ( float x, float y, float sigma, float corner, float2 half_size ) {

	float delta = min ( half_size.y - corner - abs ( y ), 0.0 );
	float curved = half_size.x - corner + sqrt ( max ( 0.0, corner * corner - delta * delta ) );
	float2 integral = 0.5 + 0.5 * erf2 ( ( x + float2 ( -curved, curved ) ) * ( 0.7071067 / sigma ) );

	return integral.y - integral.x;

}

float rounded_box_shadow ( float2 p, float2 half_size, float sigma, float corner ) {

	float low = p.y - half_size.y;
	float high = p.y + half_size.y;
	float start = clamp ( -3.0 * sigma, low, high );
	float end = clamp ( 3.0 * sigma, low, high );
	float step_size = ( end - start ) / 6.0;
	float y = start + step_size * 0.5;
	float value = 0.0;

	[ unroll ]
	for ( int i = 0; i < 6; i++ ) {

		value += shadow_x ( p.x, p.y - y, sigma, corner, half_size ) * gaussian ( y, sigma ) * step_size;
		y += step_size;

	}

	return value;

}

float polygon_distance ( float2 p, float2 v0, float2 v1, float2 v2, float2 v3 ) {

	float2 v [ 4 ] = { v0, v1, v2, v3 };
	float d = dot ( p - v [ 0 ], p - v [ 0 ] );
	float s = 1.0;

	[ unroll ]
	for ( int i = 0; i < 4; i++ ) {

		int j = ( i + 3 ) % 4;
		float2 e = v [ j ] - v [ i ];
		float2 w = p - v [ i ];
		float2 b = w - e * clamp ( dot ( w, e ) / max ( dot ( e, e ), 1e-6 ), 0.0, 1.0 );

		d = min ( d, dot ( b, b ) );

		bool c0 = p.y >= v [ i ].y;
		bool c1 = p.y < v [ j ].y;
		bool c2 = e.x * w.y > e.y * w.x;

		if ( ( c0 && c1 && c2 ) || ( !c0 && !c1 && !c2 ) )
			s *= -1.0;

	}

	return s * sqrt ( d );

}

float clip_mask ( float2 p, float4 clip ) {

	float inside = min ( min ( p.x - clip.x, clip.z - p.x ), min ( p.y - clip.y, clip.w - p.y ) );

	return saturate ( inside + 0.5 );

}

float4 sample_glass ( float2 at, float2 shift, float frost, float dispersion ) {

	float2 scale = 1.0 / source_size;
	float2 r_uv = ( at + shift * ( 1.0 - dispersion ) ) * scale;
	float2 g_uv = ( at + shift ) * scale;
	float2 b_uv = ( at + shift * ( 1.0 + dispersion ) ) * scale;
	float4 sharp_g = source_texture.Sample ( linear_clamp, g_uv );
	float4 soft_g = blur_texture.Sample ( linear_clamp, g_uv );
	float4 sharp = float4 ( source_texture.Sample ( linear_clamp, r_uv ).r, sharp_g.g, source_texture.Sample ( linear_clamp, b_uv ).b, sharp_g.a );
	float4 soft = float4 ( blur_texture.Sample ( linear_clamp, r_uv ).r, soft_g.g, blur_texture.Sample ( linear_clamp, b_uv ).b, soft_g.a );

	return lerp ( sharp, soft, frost );

}

float4 shade_glass ( vs_out input, float2 local, float2 half_size, float radius, float d ) {

	float bevel = max ( input.data.x, 1.0 );
	float refraction = input.data.y;
	float frost = input.data.z;
	float dispersion = input.data.w;
	float highlight = input.extra.x;
	float opacity = input.extra.y;
	float2 n = rounded_box_normal ( local, half_size, max ( radius, bevel ) );
	float edge = saturate ( 1.0 + d / bevel );
	float bend = edge * edge * ( 3.0 - 2.0 * edge );
	float2 shift = n * refraction * bend * bend;
	float local_frost = frost * ( 1.0 - 0.55 * bend );
	float4 base = sample_glass ( input.pixel, shift, local_frost, dispersion * bend );
	float4 lit = base * ( 1.0 - input.fill.a ) + input.fill;
	float2 light_direction = normalize ( float2 ( -0.45, -1.0 ) );
	float facing = dot ( n, light_direction );
	float rim_band = saturate ( 1.0 - abs ( d + 0.85 ) / 1.1 );
	float rim_soft = saturate ( 1.0 - abs ( d + 2.2 ) / 2.6 ) * 0.35;
	float rim_light = pow ( saturate ( facing ), 1.6 ) * 0.9 + pow ( saturate ( -facing ), 2.0 ) * 0.45 + 0.18;
	float2 to_light = input.pixel - light.xy;
	float glow = exp ( -dot ( to_light, to_light ) / ( 2.0 * 140.0 * 140.0 ) ) * light.z;
	float rim_glow = exp ( -dot ( to_light, to_light ) / ( 2.0 * 90.0 * 90.0 ) ) * light.z * rim_band;
	float top = saturate ( ( local.y + half_size.y ) / max ( half_size.y * 2.0, 1.0 ) );
	float shine = ( rim_band + rim_soft ) * rim_light * highlight * 0.62;

	shine += saturate ( bend * bend * facing ) * 0.075 * highlight;
	shine += glow * 0.045 * highlight + rim_glow * 0.5 * highlight;
	shine += ( 1.0 - top ) * 0.025 * highlight;
	lit += shine;
	lit.rgb -= bend * bend * saturate ( -facing ) * 0.06 * highlight;
	lit = saturate ( lit );
	lit.rgb = min ( lit.rgb, lit.a );

	float coverage = saturate ( 0.5 - d ) * opacity;

	return lit * coverage;

}

float4 ps_main ( vs_out input ) : SV_Target {

	uint kind = ( uint ) input.shape.w;
	float2 p = input.pixel;
	float2 half_size = input.rect.zw * 0.5;
	float2 center = input.rect.xy + half_size;
	float2 local = p - center;
	float radius = min ( input.shape.x, min ( half_size.x, half_size.y ) );
	float mask = clip_mask ( p, input.clip );
	float4 result = 0.0;

	if ( mask <= 0.0 )
		discard;

	if ( kind == KIND_FILL ) {

		float soft = max ( input.shape.y, 1.0 );
		float d = rounded_box ( local, half_size, radius );
		float coverage = saturate ( 0.5 - d / soft );

		result = input.fill * coverage;

		if ( input.shape.z > 0.0 ) {

			float inner = saturate ( 0.5 - ( d + input.shape.z ) / soft );

			result = input.fill * inner + input.stroke * max ( coverage - inner, 0.0 );

		}

	} else if ( kind == KIND_SHADOW ) {

		float sigma = max ( input.shape.y, 0.5 );
		float value = rounded_box_shadow ( local, half_size, sigma, radius );

		result = input.fill * saturate ( value );

	} else if ( kind == KIND_IMAGE ) {

		float d = rounded_box ( local, half_size, radius );
		float coverage = saturate ( 0.5 - d );
		float2 q = ( p - center ) / max ( input.rect.zw, 1.0 );
		float spin_c = cos ( input.extra.y );
		float spin_s = sin ( input.extra.y );
		float2 t = float2 ( spin_c * q.x - spin_s * q.y, spin_s * q.x + spin_c * q.y ) + 0.5;
		float2 uv = lerp ( input.data.xy, input.data.zw, t );
		float4 large = cover_texture.Sample ( linear_clamp, float3 ( uv, input.extra.x ) );
		float4 small = thumb_texture.Sample ( linear_clamp, float3 ( uv, input.extra.x ) );
		float4 texel = input.extra.z > 0.5 ? small : large;

		result = texel * input.fill * coverage;

	} else if ( kind == KIND_TEXTURE ) {

		float d = rounded_box ( local, half_size, radius );
		float coverage = saturate ( 0.5 - d );
		float2 t = ( p - input.rect.xy ) / max ( input.rect.zw, 1.0 );
		float2 uv = lerp ( input.data.xy, input.data.zw, t );
		float4 texel = source_texture.Sample ( linear_clamp, uv );

		result = texel * input.fill.a * coverage;

	} else if ( kind == KIND_GLASS ) {

		float d = rounded_box ( local, half_size, radius );

		if ( d > 1.0 )
			discard;

		result = shade_glass ( input, local, half_size, radius, d );

	} else if ( kind == KIND_GRADIENT ) {

		float d = rounded_box ( local, half_size, radius );
		float coverage = saturate ( 0.5 - d );
		float2 a = input.data.xy;
		float2 b = input.data.zw;
		float2 ab = b - a;
		float t = saturate ( dot ( p - a, ab ) / max ( dot ( ab, ab ), 1e-4 ) );

		t = t * t * ( 3.0 - 2.0 * t );
		result = lerp ( input.fill, input.stroke, t ) * coverage;

	} else if ( kind == KIND_POLYGON ) {

		float d = polygon_distance ( p, input.data.xy, input.data.zw, input.extra.xy, input.extra.zw ) - input.shape.x;
		float coverage = saturate ( 0.5 - d );

		result = input.fill * coverage;

	} else if ( kind == KIND_RING ) {

		float d = length ( local ) - ( half_size.x - input.shape.z * 0.5 );
		float coverage = saturate ( 0.5 - ( abs ( d ) - input.shape.z * 0.5 ) );
		float angle = frac ( atan2 ( local.x, -local.y ) / 6.2831853 + 1.0 );
		float start = input.data.x;
		float sweep = input.data.y;
		float along = frac ( angle - start + 1.0 );

		if ( sweep < 0.999 ) {

			float circumference = 6.2831853 * max ( half_size.x - input.shape.z * 0.5, 1.0 );
			float head = ( sweep - along ) * circumference;
			float tail = ( along > 0.5 + sweep * 0.5 ? along - 1.0 : along ) * circumference;

			coverage *= saturate ( head + 0.5 ) * saturate ( tail + 0.5 );

		}
		result = input.fill * coverage;

	}

	float dither = frac ( sin ( dot ( p, float2 ( 12.9898, 78.233 ) ) ) * 43758.5453 ) - 0.5;

	result.rgb += dither * ( 1.5 / 255.0 ) * result.a;

	return result * mask;

}