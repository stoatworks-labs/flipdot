#pragma once
// Stand-ins for the FFGL SDK, GL and ffglex, for demo/tools/refsign.cpp ONLY.
//
// source/Controls.h includes <FFGLSDK.h>; check_port.mjs puts this directory
// first on the include path so the plugin's own Controls.cpp, Dither.cpp and
// the text cut from Flipdot.cpp compile with no SDK and no GL context. Nothing
// here computes anything the plugin computes: the GL calls record what they
// are handed (uniforms, the cos/sin upload) or hand back what the script says
// (glReadPixels returns the script's means), and the parameter declarations
// are recorded so the page's list can be compared with the constructor's.

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

typedef uint32_t FFUInt32;
typedef uint32_t FFResult;
typedef unsigned int GLuint;
typedef int GLint;
typedef unsigned int GLenum;
typedef int GLsizei;
typedef unsigned char GLubyte;

constexpr FFResult FF_SUCCESS = 0;
constexpr FFResult FF_FAIL    = 1;

enum : unsigned int
{
	FF_TYPE_BOOLEAN  = 0,
	FF_TYPE_EVENT    = 1,
	FF_TYPE_RED      = 2,
	FF_TYPE_STANDARD = 10,
	FF_TYPE_OPTION   = 11,
	FF_TYPE_BUFFER   = 12,
	FF_TYPE_INTEGER  = 13,
	FF_TYPE_TEXT     = 100,
	FF_USAGE_FFT     = 1,
};

enum : GLenum
{
	GL_VIEWPORT = 1,
	GL_TEXTURE_2D,
	GL_RED,
	GL_RG,
	GL_FLOAT,
	GL_R32F,
	GL_RG32F,
	GL_RGBA8,
	GL_TEXTURE_MIN_FILTER,
	GL_TEXTURE_MAG_FILTER,
	GL_TEXTURE_WRAP_S,
	GL_TEXTURE_WRAP_T,
	GL_NEAREST,
	GL_CLAMP_TO_EDGE,
	GL_VENDOR,
	GL_RENDERER,
	GL_VERSION,
};

struct FFGLTextureStruct
{
	FFUInt32 Width, Height;
	FFUInt32 HardwareWidth, HardwareHeight;
	GLuint Handle;
};
struct ProcessOpenGLStruct
{
	FFUInt32 numInputTextures;
	FFGLTextureStruct** inputTextures;
	GLuint HostFBO;
};
struct FFGLViewportStruct
{
	FFUInt32 x, y, width, height;
};
struct FFGLTexCoords
{
	float s, t;
};
inline FFGLTexCoords GetMaxGLTexCoords( const FFGLTextureStruct& t )
{
	return { static_cast< float >( t.Width ) / static_cast< float >( t.HardwareWidth ),
	         static_cast< float >( t.Height ) / static_cast< float >( t.HardwareHeight ) };
}

namespace stub
{
inline int viewportW = 0, viewportH = 0;
inline std::vector< float > means;///< what the next glReadPixels returns
inline bool readMismatch = false;
inline int readCount     = 0;
inline std::vector< float > turn;///< the last cos/sin upload
inline int turnW = 0, turnH = 0;
inline GLuint nextTexture = 1;

/// FindUniform hands out an index into this, which glUniform2i reads back.
struct UniformRef
{
	std::map< std::string, std::vector< double > >* into;
	std::string name;
};
inline std::vector< UniformRef > uniformRefs;
} // namespace stub

inline void glGetIntegerv( GLenum, GLint* out )
{
	out[ 0 ] = 0;
	out[ 1 ] = 0;
	out[ 2 ] = stub::viewportW;
	out[ 3 ] = stub::viewportH;
}
inline const GLubyte* glGetString( GLenum )
{
	return nullptr;
}
inline void glViewport( GLint, GLint, GLsizei, GLsizei ) {}
inline void glGenTextures( GLsizei, GLuint* t )
{
	*t = stub::nextTexture++;
}
inline void glBindTexture( GLenum, GLuint ) {}
inline void glDeleteTextures( GLsizei, const GLuint* ) {}
inline void glTexParameteri( GLenum, GLenum, GLint ) {}
inline void glTexImage2D( GLenum, GLint, GLint, GLsizei w, GLsizei h, GLint, GLenum, GLenum, const void* data )
{
	const float* f = static_cast< const float* >( data );
	stub::turn.assign( f, f + static_cast< size_t >( w ) * static_cast< size_t >( h ) * 2 );
	stub::turnW = w;
	stub::turnH = h;
}
inline void glTexSubImage2D( GLenum, GLint, GLint, GLint, GLsizei w, GLsizei h, GLenum, GLenum, const void* data )
{
	glTexImage2D( GL_TEXTURE_2D, 0, 0, w, h, 0, 0, 0, data );
}
inline void glUniform2i( GLint where, GLint x, GLint y )
{
	if( where < 0 || static_cast< size_t >( where ) >= stub::uniformRefs.size() )
		return;
	const stub::UniformRef& ref = stub::uniformRefs[ static_cast< size_t >( where ) ];
	( *ref.into )[ ref.name ] = { static_cast< double >( x ), static_cast< double >( y ) };
}
inline void glReadPixels( GLint, GLint, GLsizei w, GLsizei h, GLenum, GLenum, void* out )
{
	++stub::readCount;
	const size_t n = static_cast< size_t >( w ) * static_cast< size_t >( h );
	if( n != stub::means.size() )
	{
		stub::readMismatch = true;
		return;
	}
	std::copy( stub::means.begin(), stub::means.end(), static_cast< float* >( out ) );
}

namespace FFGLLog
{
inline void LogToHost( const char* ) {}
} // namespace FFGLLog

/// The declarations, recorded as the constructor makes them.
class CFFGLPlugin
{
public:
	struct Element
	{
		std::string name;
		float value = 0.0f;
	};
	struct ParamInfo
	{
		unsigned int id   = 0;
		std::string name;
		unsigned int type = 0;
		std::string group;
		float defaultValue = 0.0f;
		std::string defaultText;
		float min = 0.0f, max = 1.0f;
		bool ranged = false;
		unsigned int usage = 0;
		std::vector< Element > elements;
	};

	std::vector< ParamInfo > declared;

	virtual ~CFFGLPlugin() = default;

	ParamInfo* FindParamInfo( unsigned int id )
	{
		for( ParamInfo& p : declared )
			if( p.id == id )
				return &p;
		return nullptr;
	}

protected:
	void SetMinInputs( int ) {}
	void SetMaxInputs( int ) {}
	void SetTimeSupported( bool ) {}
	ParamInfo& slot( unsigned int index )
	{
		if( ParamInfo* p = FindParamInfo( index ) )
			return *p;
		declared.emplace_back();
		declared.back().id = index;
		return declared.back();
	}
	void SetParamInfo( unsigned int index, const char* name, unsigned int type, float def )
	{
		ParamInfo& p   = slot( index );
		p.name         = name;
		p.type         = type;
		p.defaultValue = def;
	}
	void SetParamInfo( unsigned int index, const char* name, unsigned int type, bool def )
	{
		SetParamInfo( index, name, type, def ? 1.0f : 0.0f );
	}
	void SetParamInfo( unsigned int index, const char* name, unsigned int type, const char* def )
	{
		ParamInfo& p  = slot( index );
		p.name        = name;
		p.type        = type;
		p.defaultText = def ? def : "";
	}
	void SetParamRange( unsigned int index, float lo, float hi )
	{
		ParamInfo& p = slot( index );
		p.min        = lo;
		p.max        = hi;
		p.ranged     = true;
	}
	void SetOptionParamInfo( unsigned int index, const char* name, unsigned int count, float def )
	{
		ParamInfo& p   = slot( index );
		p.name         = name;
		p.type         = FF_TYPE_OPTION;
		p.defaultValue = def;
		p.elements.assign( count, Element {} );
	}
	void SetBufferParamInfo( unsigned int index, const char* name, unsigned int count, unsigned int usage )
	{
		ParamInfo& p = slot( index );
		p.name       = name;
		p.type       = FF_TYPE_BUFFER;
		p.usage      = usage;
		p.elements.assign( count, Element {} );
	}
	void SetParamElementInfo( unsigned int index, unsigned int element, const char* name, float value )
	{
		ParamInfo& p = slot( index );
		if( element < p.elements.size() )
			p.elements[ element ] = { name, value };
	}
	void SetParamGroup( unsigned int index, std::string group )
	{
		slot( index ).group = group;
	}
	FFResult SetTextParameter( unsigned int, const char* )
	{
		return FF_FAIL;
	}
	char* GetTextParameter( unsigned int )
	{
		return nullptr;
	}
};

namespace ffglex
{
struct FFGLShader
{
	std::map< std::string, std::vector< double > > set;
	GLuint GetGLID() const
	{
		return 1;
	}
	GLint FindUniform( const char* name )
	{
		stub::uniformRefs.push_back( { &set, name } );
		return static_cast< GLint >( stub::uniformRefs.size() - 1 );
	}
	void Set( const char* name, int v )
	{
		set[ name ] = { static_cast< double >( v ) };
	}
	void Set( const char* name, float v )
	{
		set[ name ] = { static_cast< double >( v ) };
	}
	void Set( const char* name, float a, float b )
	{
		set[ name ] = { a, b };
	}
	void Set( const char* name, float a, float b, float c )
	{
		set[ name ] = { a, b, c };
	}
};
struct FFGLScreenQuad
{
	void Draw() {}
};
struct ScopedShaderBinding
{
	explicit ScopedShaderBinding( GLuint ) {}
};
struct ScopedSamplerActivation
{
	explicit ScopedSamplerActivation( int ) {}
};
struct Scoped2DTextureBinding
{
	explicit Scoped2DTextureBinding( GLuint ) {}
};
struct ScopedFBOBinding
{
	enum RevertMode
	{
		RB_REVERT
	};
	ScopedFBOBinding( GLuint, RevertMode ) {}
};
} // namespace ffglex
