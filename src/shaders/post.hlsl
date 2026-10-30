cbuffer post : register ( b0 ) {

	float2 texel;
	float spread;
	float unused;
	float4 colors [ 4 ];
	float4 params;

};

Texture2D source_texture : register ( t0 );
SamplerState linear_clamp : register ( s0 );

struct full_out {

	float4 position : SV_Position;
	float2 uv : TEXCOORD0;

};

full_out vs_full ( uint id : SV_VertexID ) {

	full_out output;
	float2 uv = float2 ( ( id << 1 ) & 2, id & 2 );

	output.uv = uv;
	output.position = float4 ( uv * float2 ( 2.0, -2.0 ) + float2 ( -1.0, 1.0 ), 0.0, 1.0 );

	return output;

}

float4 ps_down ( full_out input ) : SV_Target {

	float2 h = texel * spread;
	float4 sum = source_texture.Sample ( linear_clamp, input.uv ) * 4.0;

	sum += source_texture.Sample ( linear_clamp, input.uv - h );
	sum += source_texture.Sample ( linear_clamp, input.uv + h );
	sum += source_texture.Sample ( linear_clamp, input.uv + float2 ( h.x, -h.y ) );
	sum += source_texture.Sample ( linear_clamp, input.uv - float2 ( h.x, -h.y ) );

	return sum / 8.0;

}

float4 ps_up ( full_out input ) : SV_Target {

	float2 h = texel * spread;
	float4 sum = source_texture.Sample ( linear_clamp, input.uv + float2 ( -h.x * 2.0, 0.0 ) );

	sum += source_texture.Sample ( linear_clamp, input.uv + float2 ( -h.x, h.y ) ) * 2.0;
	sum += source_texture.Sample ( linear_clamp, input.uv + float2 ( 0.0, h.y * 2.0 ) );
	sum += source_texture.Sample ( linear_clamp, input.uv + float2 ( h.x, h.y ) ) * 2.0;
	sum += source_texture.Sample ( linear_clamp, input.uv + float2 ( h.x * 2.0, 0.0 ) );
	sum += source_texture.Sample ( linear_clamp, input.uv + float2 ( h.x, -h.y ) ) * 2.0;
	sum += source_texture.Sample ( linear_clamp, input.uv + float2 ( 0.0, -h.y * 2.0 ) );
	sum += source_texture.Sample ( linear_clamp, input.uv + float2 ( -h.x, -h.y ) ) * 2.0;

	return sum / 12.0;

}

float hash ( float2 p ) {

	float3 q = frac ( float3 ( p.xyx ) * 0.1031 );

	q += dot ( q, q.yzx + 33.33 );

	return frac ( ( q.x + q.y ) * q.z );

}

float noise ( float2 p ) {

	float2 i = floor ( p );
	float2 f = frac ( p );
	float2 u = f * f * f * ( f * ( f * 6.0 - 15.0 ) + 10.0 );
	float a = hash ( i );
	float b = hash ( i + float2 ( 1.0, 0.0 ) );
	float c = hash ( i + float2 ( 0.0, 1.0 ) );
	float d = hash ( i + float2 ( 1.0, 1.0 ) );

	return lerp ( lerp ( a, b, u.x ), lerp ( c, d, u.x ), u.y );

}

float fbm ( float2 p ) {

	float value = 0.0;
	float amplitude = 0.5;

	[ unroll ]
	for ( int i = 0; i < 5; i++ ) {

		value += amplitude * noise ( p );
		p = float2 ( p.x * 1.6 - p.y * 1.2, p.x * 1.2 + p.y * 1.6 ) + 17.3;
		amplitude *= 0.5;

	}

	return value;

}

float3 ramp ( float t ) {

	t = saturate ( t ) * 3.0;

	if ( t < 1.0 )
		return lerp ( colors [ 0 ].rgb, colors [ 1 ].rgb, smoothstep ( 0.0, 1.0, t ) );

	if ( t < 2.0 )
		return lerp ( colors [ 1 ].rgb, colors [ 2 ].rgb, smoothstep ( 0.0, 1.0, t - 1.0 ) );

	return lerp ( colors [ 2 ].rgb, colors [ 3 ].rgb, smoothstep ( 0.0, 1.0, t - 2.0 ) );

}

float3 style_liquid ( float2 uv, float seed ) {

	float2 p = uv * 2.4 + seed * 7.13;
	float2 q = float2 ( fbm ( p ), fbm ( p + float2 ( 5.2, 1.3 ) ) );
	float2 r = float2 ( fbm ( p + 3.2 * q + float2 ( 1.7, 9.2 ) ), fbm ( p + 3.2 * q + float2 ( 8.3, 2.8 ) ) );
	float v = fbm ( p + 3.0 * r );
	float3 color = ramp ( v * 1.35 - 0.15 );

	color = lerp ( color, colors [ 3 ].rgb, saturate ( length ( q ) - 0.55 ) * 0.5 );
	color = lerp ( color, colors [ 0 ].rgb, saturate ( r.y * r.y * 1.4 - 0.2 ) * 0.4 );

	return color;

}

float3 style_orb ( float2 uv, float seed ) {

	float3 color = lerp ( colors [ 0 ].rgb, colors [ 1 ].rgb, smoothstep ( 0.0, 1.0, uv.y + ( noise ( uv * 3.0 + seed ) - 0.5 ) * 0.25 ) );
	float2 center = float2 ( 0.5 + ( hash ( float2 ( seed, 1.0 ) ) - 0.5 ) * 0.2, 0.47 );
	float radius = 0.27;
	float2 d = uv - center;
	float distance_to = length ( d );
	float shadow = exp ( -pow ( max ( length ( ( uv - center - float2 ( 0.03, 0.07 ) ) * float2 ( 1.0, 1.6 ) ) - radius * 0.8, 0.0 ), 2.0 ) * 60.0 );

	color = lerp ( color, color * 0.55, shadow * 0.45 );

	float inside = smoothstep ( radius + 0.002, radius - 0.002, distance_to );
	float3 normal = normalize ( float3 ( d / radius, sqrt ( saturate ( 1.0 - dot ( d, d ) / ( radius * radius ) ) ) ) );
	float3 light_direction = normalize ( float3 ( -0.5, -0.6, 0.65 ) );
	float diffuse = saturate ( dot ( normal, light_direction ) );
	float specular = pow ( saturate ( dot ( reflect ( -light_direction, normal ), float3 ( 0.0, 0.0, 1.0 ) ) ), 24.0 );
	float3 sphere = lerp ( colors [ 3 ].rgb, colors [ 2 ].rgb, diffuse );

	sphere += specular * 0.55;
	sphere += pow ( 1.0 - normal.z, 3.0 ) * colors [ 1 ].rgb * 0.35;

	return lerp ( color, sphere, inside );

}

float3 style_bands ( float2 uv, float seed ) {

	float wave = sin ( uv.x * 5.0 + seed * 3.0 ) * 0.045 + sin ( uv.x * 11.0 - seed ) * 0.02 + ( noise ( uv * 4.0 + seed ) - 0.5 ) * 0.05;
	float y = uv.y + wave;
	float bands = y * 5.0;
	float index = floor ( bands );
	float f = frac ( bands );
	float t = index / 5.0;
	float3 low = ramp ( t );
	float3 high = ramp ( t + 0.2 );
	float3 color = lerp ( low, high, smoothstep ( 0.94, 1.0, f ) );

	color *= 0.92 + 0.08 * f;

	return color;

}

float3 style_rings ( float2 uv, float seed ) {

	float2 center = float2 ( 0.22 + hash ( float2 ( seed, 3.0 ) ) * 0.2, 0.78 - hash ( float2 ( seed, 5.0 ) ) * 0.2 );
	float2 d = uv - center;
	float distance_to = length ( d ) + ( fbm ( uv * 3.0 + seed ) - 0.5 ) * 0.06;
	float rings = distance_to * 7.0;
	float index = floor ( rings );
	float f = frac ( rings );
	float3 a = ramp ( frac ( index * 0.27 + 0.1 ) );
	float3 b = ramp ( frac ( ( index + 1.0 ) * 0.27 + 0.1 ) );
	float3 color = lerp ( a, b, smoothstep ( 0.9, 1.0, f ) );

	color *= 1.0 - 0.12 * f;

	return color;

}

float3 style_mesh ( float2 uv, float seed ) {

	float2 points [ 4 ] = {
		float2 ( 0.15 + hash ( float2 ( seed, 11.0 ) ) * 0.2, 0.2 + hash ( float2 ( seed, 12.0 ) ) * 0.2 ),
		float2 ( 0.75 + hash ( float2 ( seed, 13.0 ) ) * 0.15, 0.15 + hash ( float2 ( seed, 14.0 ) ) * 0.25 ),
		float2 ( 0.2 + hash ( float2 ( seed, 15.0 ) ) * 0.25, 0.75 + hash ( float2 ( seed, 16.0 ) ) * 0.15 ),
		float2 ( 0.7 + hash ( float2 ( seed, 17.0 ) ) * 0.2, 0.7 + hash ( float2 ( seed, 18.0 ) ) * 0.2 )
	};
	float2 warped = uv + ( float2 ( fbm ( uv * 2.0 + seed ), fbm ( uv * 2.0 + seed + 4.0 ) ) - 0.5 ) * 0.25;
	float3 color = 0.0;
	float total = 0.0;

	[ unroll ]
	for ( int i = 0; i < 4; i++ ) {

		float2 d = warped - points [ i ];
		float weight = 1.0 / pow ( dot ( d, d ) + 0.01, 1.6 );

		color += colors [ i ].rgb * weight;
		total += weight;

	}

	return color / total;

}

float3 style_aurora ( float2 uv, float seed ) {

	float3 color = lerp ( colors [ 0 ].rgb * 1.3, colors [ 0 ].rgb * 0.5, uv.y );
	float2 grid = uv * 90.0;
	float2 cell = floor ( grid );
	float2 inside = frac ( grid ) - 0.5;
	float star = hash ( cell + seed * 13.0 );

	color += step ( 0.982, star ) * exp ( -dot ( inside, inside ) * 40.0 ) * ( 0.5 + 0.5 * hash ( cell + 7.0 ) ) * ( 1.0 - uv.y * 0.7 );

	[ unroll ]
	for ( int i = 0; i < 3; i++ ) {

		float fi = ( float ) i;
		float phase = seed * 2.3 + fi * 1.9;
		float center = 0.5 + 0.22 * sin ( uv.y * 2.4 + phase ) + 0.08 * sin ( uv.y * 6.1 - phase * 1.3 ) + ( fi - 1.0 ) * 0.2;
		float spread = 0.06 + 0.035 * sin ( uv.y * 3.0 + phase );
		float band = exp ( -pow ( ( uv.x - center ) / spread, 2.0 ) );
		float glow = exp ( -pow ( ( uv.x - center ) / ( spread * 3.0 ), 2.0 ) ) * 0.35;
		float rays = 0.7 + 0.3 * sin ( uv.x * 70.0 + uv.y * 9.0 + phase * 3.0 );
		float fade = smoothstep ( 0.02, 0.3, uv.y ) * smoothstep ( 1.0, 0.5, uv.y );

		color += colors [ i + 1 ].rgb * ( band * rays + glow ) * fade * 0.8;

	}

	return color;

}

float4 ps_cover ( full_out input ) : SV_Target {

	float2 uv = input.uv;
	float seed = params.x;
	uint style = ( uint ) params.y;
	float3 color;

	if ( style == 0 )
		color = style_liquid ( uv, seed );
	else if ( style == 1 )
		color = style_orb ( uv, seed );
	else if ( style == 2 )
		color = style_bands ( uv, seed );
	else if ( style == 3 )
		color = style_rings ( uv, seed );
	else if ( style == 4 )
		color = style_mesh ( uv, seed );
	else
		color = style_aurora ( uv, seed );

	float2 v = uv - 0.5;

	color *= 1.0 - dot ( v, v ) * params.w;
	color += ( hash ( uv * 512.0 + seed * 91.0 ) - 0.5 ) * params.z;

	return float4 ( saturate ( color ), 1.0 );

}