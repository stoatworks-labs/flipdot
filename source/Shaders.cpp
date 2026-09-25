#include "Shaders.h"

namespace flipdot
{

//---------------------------------------------------------------------------
// Vertex: straight through in 0..1 picture space. MaxUV is folded in once in
// the copy pass, and every later pass works on a texture we allocated.
//---------------------------------------------------------------------------
const char* const kVertexShader = R"(#version 410 core

layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;

out vec2 uv;

void main()
{
	gl_Position = vPosition;
	uv = vUV;
}
)";

//---------------------------------------------------------------------------
// Pass 1: copy.
//---------------------------------------------------------------------------
const char* const kCopyShader = R"(#version 410 core

uniform sampler2D InputTexture;
uniform vec2 MaxUV;

in vec2 uv;
out vec4 fragColor;

void main()
{
	fragColor = texture( InputTexture, uv * MaxUV );
}
)";

//---------------------------------------------------------------------------
// Pass 2: means. One fragment per disc, Columns x Rows, R32F. Texel row r is
// the sign's row r counted from the TOP, so a read-back is in reading order.
//---------------------------------------------------------------------------
const char* const kMeansShader = R"(#version 410 core

uniform sampler2D Picture;    //the clip, mipmapped
uniform ivec2 Grid;
uniform int OffsetRows;       //1: odd rows sit half a pitch to the right
uniform vec2 BoardOrigin;     //the sign's top-left corner, in pixels from the picture's top-left
uniform float Pitch;          //pixels from one disc to the next
uniform vec2 PictureSize;     //pixels
uniform float PictureLod;     //the whole mip level whose texel is at most an eighth of the pitch

out vec4 fragColor;

//The picture uv of a point in disc (c, r), offset in pitch units from its
//centre (x right, y down).
vec2 discUV( ivec2 disc, vec2 offset )
{
	float shift = ( OffsetRows == 1 && ( disc.y & 1 ) == 1 ) ? 0.5 : 0.0;
	vec2 px = BoardOrigin + ( vec2( disc ) + vec2( 0.5 + shift, 0.5 ) + offset ) * Pitch;
	return vec2( px.x / PictureSize.x, 1.0 - px.y / PictureSize.y );
}

void main()
{
	ivec2 disc = ivec2( gl_FragCoord.xy );

	//Sixteen bilinear taps at +-1/8 and +-3/8 of the pitch, at a level whose
	//texel is no wider than an eighth of it: each tap's footprint reaches the
	//cell's edge and stops, so the mean is this disc's and not a neighbour's.
	vec4 sum = vec4( 0.0 );
	for( int j = 0; j < 4; ++j )
		for( int i = 0; i < 4; ++i )
			sum += textureLod( Picture, discUV( disc, vec2( -0.375 + 0.25 * float( i ), -0.375 + 0.25 * float( j ) ) ), PictureLod );
	vec4 m = sum / 16.0;

	//Where the clip is transparent the sign sees black.
	float luma = dot( m.rgb, vec3( 0.2126, 0.7152, 0.0722 ) ) * m.a;
	fragColor = vec4( luma, 0.0, 0.0, 1.0 );
}
)";

//---------------------------------------------------------------------------
// Pass 3: board.
//---------------------------------------------------------------------------
const char* const kBoardShader = R"(#version 410 core

uniform sampler2D Picture;    //the clip, for Mix
uniform sampler2D Turn;       //Columns x Rows, RG32F: cos and sin of each disc's angle from the black stop; row 0 the top
uniform ivec2 Grid;
uniform int OffsetRows;
uniform vec2 BoardOrigin;     //pixels from the output's top-left
uniform vec2 BoardSize;       //pixels
uniform float Pitch;          //pixels
uniform vec2 OutSize;         //pixels
uniform float DiscSize;       //diameter over pitch
uniform vec3 FaceColour;      //the fluorescent face
uniform vec2 LightSinCos;     //sin and cos of the light's angle: 0 from the viewer, toward grazing from the left
uniform float MixAmount;

in vec2 uv;
out vec4 fragColor;

const float kAmbient = 0.30;
const vec3 kSignFace = vec3( 0.035 );  //the sign's matte black front
const vec3 kHole = vec3( 0.012 );      //the recess each disc sits in
const vec3 kBlackFace = vec3( 0.060 ); //the disc's black side
const vec3 kRimPaint = vec3( 0.120 );  //the disc's edge

//Signed distance in pixels to an ellipse of semi-axes a, to first order.
//Exact on the horizontal through the centre, which is where --rotation
//measures the disc's width.
float ellipseDistance( vec2 p, vec2 a )
{
	vec2 q = p / a;
	float k = length( q );
	float g = length( p / ( a * a ) );
	return ( k - 1.0 ) * k / max( g, 1.0e-6 );
}

float coverage( float d )
{
	return clamp( 0.5 - d, 0.0, 1.0 );
}

void main()
{
	vec4 source = texture( Picture, uv );

	//Pixels from the output's top-left, pixel centres at .5.
	vec2 px = vec2( gl_FragCoord.x, OutSize.y - gl_FragCoord.y );
	vec2 b = px - BoardOrigin;

	vec4 board = vec4( 0.0 );
	if( all( greaterThanEqual( b, vec2( 0.0 ) ) ) && all( lessThan( b, BoardSize ) ) )
	{
		vec3 colour = kSignFace;
		vec2 g = b / Pitch;
		int row = min( int( floor( g.y ) ), Grid.y - 1 );
		float shift = ( OffsetRows == 1 && ( row & 1 ) == 1 ) ? 0.5 : 0.0;
		float gx = g.x - shift;
		int col = int( floor( gx ) );
		if( gx >= 0.0 && col < Grid.x )
		{
			//Pixels from the disc's centre, x right, y down.
			vec2 p = ( vec2( gx, g.y ) - vec2( float( col ), float( row ) ) - 0.5 ) * Pitch;
			float r = 0.5 * DiscSize * Pitch;

			//cos and sin come from the CPU in double: GLSL leaves the
			//precision of its own trigonometry to the implementation.
			vec2 turn = texelFetch( Turn, ivec2( col, row ), 0 ).rg;
			float c = turn.x;
			float s = turn.y;

			//The recess, a little wider than the disc.
			float rh = r + max( 0.05 * r, 1.0 );
			colour = mix( colour, kHole, coverage( length( p ) - rh ) );

			//Light: toward the light, from the left of the sign at LightAngle
			//off the viewer's axis. The hole's rim shades the far side of the
			//disc from an oblique light: the lit side.
			vec3 toLight = vec3( -LightSinCos.x, 0.0, LightSinCos.y );
			float lee = 1.0 - 0.45 * LightSinCos.x * smoothstep( 0.1, 1.0, p.x / r );

			//The plate turned by phi about a vertical axle: the black face's
			//normal is ( sin phi, 0, cos phi ). The face toward the viewer is
			//the black one while cos phi >= 0, the colour one after.
			float w = r * abs( c );
			float t = max( 0.06 * r, 0.75 );
			float wr = w + t * abs( s );

			float rimLight = kAmbient + ( 1.0 - kAmbient ) * LightSinCos.x * 0.8;
			colour = mix( colour, kRimPaint * rimLight * lee, coverage( ellipseDistance( p, vec2( max( wr, 1.0e-4 ), r ) ) ) );

			if( w > 1.0e-4 )
			{
				float facing = c >= 0.0 ? 1.0 : -1.0;
				vec3 normal = facing * vec3( s, 0.0, c );
				float shade = kAmbient + ( 1.0 - kAmbient ) * max( 0.0, dot( normal, toLight ) );
				vec3 paint = c >= 0.0 ? kBlackFace : FaceColour;
				colour = mix( colour, paint * shade * lee, coverage( ellipseDistance( p, vec2( w, r ) ) ) );
			}
		}
		board = vec4( colour, 1.0 );
	}

	fragColor = mix( source, board, MixAmount );
}
)";

} // namespace flipdot
