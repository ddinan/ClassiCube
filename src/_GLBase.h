/* Either a:
- OpenGL 1.5 or OpenGL 1.2 + GL_ARB_vertex_buffer_object fixed function backend
- OpenGL 1.1 backend (completely lacking GPU, fallbacks to say Windows built-in software rasteriser)
*/
#include "_GraphicsBase.h"
#include "String.h"
#include "Logger.h"
#include "Window.h"
#include "Errors.h"

/* ===================== BEGIN OPENGL HEADERS ===================== */
#ifdef CC_BUILD_WIN
/* OpenGL functions use stdcall instead of cdecl on Windows */
#define GLAPI __stdcall
#else
#define GLAPI
#endif

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned char GLubyte;
typedef int GLint;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef cc_uintptr GLintptr;

#define GL_DEPTH_BUFFER_BIT       0x00000100
#define GL_COLOR_BUFFER_BIT       0x00004000
#define GL_UNSIGNED_BYTE          0x1401
#define GL_VIEWPORT               0x0BA2
#define GL_MAX_TEXTURE_SIZE       0x0D33
#define GL_DEPTH_BITS             0x0D56
#define GL_TEXTURE_2D             0x0DE1
#define GL_TEXTURE                0x1702
#define GL_RGBA                   0x1908
#define GL_VENDOR                 0x1F00
#define GL_RENDERER               0x1F01
#define GL_VERSION                0x1F02
#define GL_EXTENSIONS             0x1F03
#define GL_NEAREST                0x2600
#define GL_NEAREST_MIPMAP_LINEAR  0x2702
#define GL_TEXTURE_MAG_FILTER     0x2800
#define GL_TEXTURE_MIN_FILTER     0x2801
#define GL_REPEAT                 0x2901
#define GL_BGRA_EXT               0x80E1
/* Not present in gl.h on Windows (only up to OpenGL 1.1) */
#define GL_TEXTURE_MAX_LEVEL        0x813D
#define GL_UNSIGNED_INT_8_8_8_8_REV 0x8367

/* Apologies for the ugly external symbol definitions here */
extern void GLAPI glBindTexture(GLenum target, GLuint texture);
extern void GLAPI glClear(GLenum mask);
extern void GLAPI glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
extern void GLAPI glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
extern void GLAPI glDeleteTextures(GLint n, const GLuint* textures);
extern void GLAPI glDepthMask(GLboolean flag);
extern void GLAPI glDrawArrays(GLenum mode, GLint first, GLint count);
extern void GLAPI glDrawElements(GLenum mode, GLint count, GLenum type, const void* indices);
extern void GLAPI glGenTextures(GLint n, GLuint* textures);
extern void GLAPI glGetIntegerv(GLenum pname, GLint* params);
extern const char* GLAPI glGetString(GLenum name);
extern void GLAPI glReadPixels(GLint x, GLint y, GLint width, GLint height, GLenum format, GLenum type, void* pixels);
extern void GLAPI glTexImage2D(GLenum target, GLint level, GLint internalformat, GLint width, GLint height, GLint border, GLenum format, GLenum type, const void* pixels);
extern void GLAPI glTexParameteri(GLenum target, GLenum pname, GLint param);
extern void GLAPI glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint width, GLint height, GLenum format, GLenum type, const void* pixels);
extern void GLAPI glViewport(GLint x, GLint y, GLint width, GLint height);
/* ===================== END OPENGL HEADERS ===================== */

#if defined CC_BUILD_WEB || defined CC_BUILD_ANDROID
#define PIXEL_FORMAT GL_RGBA
#else
#define PIXEL_FORMAT GL_BGRA_EXT
#endif

#if defined CC_BIG_ENDIAN
/* Pixels are stored in memory as A,R,G,B but GL_UNSIGNED_BYTE will interpret as B,G,R,A */
/* So use GL_UNSIGNED_INT_8_8_8_8_REV instead to remedy this */
#define TRANSFER_FORMAT GL_UNSIGNED_INT_8_8_8_8_REV
#else
/* Pixels are stored in memory as B,G,R,A and GL_UNSIGNED_BYTE will interpret as B,G,R,A */
/* So fine to just use GL_UNSIGNED_BYTE here */
#define TRANSFER_FORMAT GL_UNSIGNED_BYTE
#endif

static cc_bool gfx_vsync;
static void GL_UpdateVsync(void) {
	GLContext_SetFpsLimit(gfx_vsync, gfx_minFrameMs);
}

static void GL_Create(void) {
	GLContext_Create();
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &Gfx.MaxTexWidth);
	Gfx.MaxTexHeight = Gfx.MaxTexWidth;
	Gfx.Created      = true;
}

cc_bool Gfx_TryRestoreContext(void) {
	return GLContext_TryRestore();
}


/*########################################################################################################################*
*---------------------------------------------------------Textures--------------------------------------------------------*
*#########################################################################################################################*/
static void Gfx_DoMipmaps(int x, int y, struct Bitmap* bmp, int rowWidth, cc_bool partial) {
	BitmapCol* prev = bmp->scan0;
	BitmapCol* cur;

	int lvls = CalcMipmapsLevels(bmp->width, bmp->height);
	int lvl, width = bmp->width, height = bmp->height;

	for (lvl = 1; lvl <= lvls; lvl++) {
		x /= 2; y /= 2;
		if (width > 1)  width /= 2;
		if (height > 1) height /= 2;

		cur = (BitmapCol*)Mem_Alloc(width * height, 4, "mipmaps");
		GenMipmaps(width, height, cur, prev, rowWidth);

		if (partial) {
			glTexSubImage2D(GL_TEXTURE_2D, lvl, x, y, width, height, PIXEL_FORMAT, TRANSFER_FORMAT, cur);
		} else {
			glTexImage2D(GL_TEXTURE_2D, lvl, GL_RGBA, width, height, 0, PIXEL_FORMAT, TRANSFER_FORMAT, cur);
		}

		if (prev != bmp->scan0) Mem_Free(prev);
		prev    = cur;
		rowWidth = width;
	}
	if (prev != bmp->scan0) Mem_Free(prev);
}

GfxResourceID Gfx_CreateTexture(struct Bitmap* bmp, cc_bool managedPool, cc_bool mipmaps) {
	GLuint texId;
	glGenTextures(1, &texId);
	glBindTexture(GL_TEXTURE_2D, texId);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	if (!Math_IsPowOf2(bmp->width) || !Math_IsPowOf2(bmp->height)) {
		Logger_Abort("Textures must have power of two dimensions");
	}
	if (Gfx.LostContext) return 0;

	if (mipmaps) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
		if (customMipmapsLevels) {
			int lvls = CalcMipmapsLevels(bmp->width, bmp->height);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, lvls);
		}
	} else {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	}

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bmp->width, bmp->height, 0, PIXEL_FORMAT, TRANSFER_FORMAT, bmp->scan0);

	if (mipmaps) Gfx_DoMipmaps(0, 0, bmp, bmp->width, false);
	return texId;
}

#define UPDATE_FAST_SIZE (64 * 64)
static CC_NOINLINE void UpdateTextureSlow(int x, int y, struct Bitmap* part, int rowWidth) {
	BitmapCol buffer[UPDATE_FAST_SIZE];
	void* ptr = (void*)buffer;
	int count = part->width * part->height;

	/* cannot allocate memory on the stack for very big updates */
	if (count > UPDATE_FAST_SIZE) {
		ptr = Mem_Alloc(count, 4, "Gfx_UpdateTexture temp");
	}

	CopyTextureData(ptr, part->width << 2, part, rowWidth << 2);
	glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, part->width, part->height, PIXEL_FORMAT, TRANSFER_FORMAT, ptr);
	if (count > UPDATE_FAST_SIZE) Mem_Free(ptr);
}

void Gfx_UpdateTexture(GfxResourceID texId, int x, int y, struct Bitmap* part, int rowWidth, cc_bool mipmaps) {
	glBindTexture(GL_TEXTURE_2D, (GLuint)texId);
	/* TODO: Use GL_UNPACK_ROW_LENGTH for Desktop OpenGL */

	if (part->width == rowWidth) {
		glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, part->width, part->height, PIXEL_FORMAT, TRANSFER_FORMAT, part->scan0);
	} else {
		UpdateTextureSlow(x, y, part, rowWidth);
	}
	if (mipmaps) Gfx_DoMipmaps(x, y, part, rowWidth, true);
}

void Gfx_BindTexture(GfxResourceID texId) {
	glBindTexture(GL_TEXTURE_2D, (GLuint)texId);
}

void Gfx_DeleteTexture(GfxResourceID* texId) {
	GLuint id = (GLuint)(*texId);
	if (!id) return;
	glDeleteTextures(1, &id);
	*texId = 0;
}

void Gfx_EnableMipmaps(void) { }
void Gfx_DisableMipmaps(void) { }


/*########################################################################################################################*
*-----------------------------------------------------State management----------------------------------------------------*
*#########################################################################################################################*/
static PackedCol gfx_clearCol;
void Gfx_SetAlphaArgBlend(cc_bool enabled) { }

static void GL_ClearCol(PackedCol col) {
	glClearColor(PackedCol_R(col) / 255.0f, PackedCol_G(col) / 255.0f,
				 PackedCol_B(col) / 255.0f, PackedCol_A(col) / 255.0f);
}
void Gfx_ClearCol(PackedCol col) {
	if (col == gfx_clearCol) return;
	GL_ClearCol(col);
	gfx_clearCol = col;
}

void Gfx_SetColWriteMask(cc_bool r, cc_bool g, cc_bool b, cc_bool a) {
	glColorMask(r, g, b, a);
}

void Gfx_SetDepthWrite(cc_bool enabled) { glDepthMask(enabled); }


/*########################################################################################################################*
*-----------------------------------------------------------Misc----------------------------------------------------------*
*#########################################################################################################################*/
/* OpenGL stores bitmap in bottom-up order, so flip order when saving */
static int SelectRow(struct Bitmap* bmp, int y) { return (bmp->height - 1) - y; }
cc_result Gfx_TakeScreenshot(struct Stream* output) {
	struct Bitmap bmp;
	cc_result res;
	GLint vp[4];
	
	glGetIntegerv(GL_VIEWPORT, vp); /* { x, y, width, height } */
	bmp.width  = vp[2]; 
	bmp.height = vp[3];

	bmp.scan0  = (BitmapCol*)Mem_TryAlloc(bmp.width * bmp.height, 4);
	if (!bmp.scan0) return ERR_OUT_OF_MEMORY;
	glReadPixels(0, 0, bmp.width, bmp.height, PIXEL_FORMAT, TRANSFER_FORMAT, bmp.scan0);

	res = Png_Encode(&bmp, output, SelectRow, false);
	Mem_Free(bmp.scan0);
	return res;
}

static void AppendVRAMStats(cc_string* info) {
	static const cc_string memExt = String_FromConst("GL_NVX_gpu_memory_info");
	GLint totalKb, curKb;
	float total, cur;

	/* NOTE: glGetString returns UTF8, but I just treat it as code page 437 */
	cc_string exts = String_FromReadonly((const char*)glGetString(GL_EXTENSIONS));
	if (!String_CaselessContains(&exts, &memExt)) return;

	glGetIntegerv(0x9048, &totalKb);
	glGetIntegerv(0x9049, &curKb);
	if (totalKb <= 0 || curKb <= 0) return;

	total = totalKb / 1024.0f; cur = curKb / 1024.0f;
	String_Format2(info, "Video memory: %f2 MB total, %f2 free\n", &total, &cur);
}

void Gfx_GetApiInfo(cc_string* info) {
	GLint depthBits;
	int pointerSize = sizeof(void*) * 8;

	glGetIntegerv(GL_DEPTH_BITS, &depthBits);
	String_Format1(info, "-- Using OpenGL (%i bit) --\n", &pointerSize);
	String_Format1(info, "Vendor: %c\n",     glGetString(GL_VENDOR));
	String_Format1(info, "Renderer: %c\n",   glGetString(GL_RENDERER));
	String_Format1(info, "GL version: %c\n", glGetString(GL_VERSION));
	AppendVRAMStats(info);
	String_Format2(info, "Max texture size: (%i, %i)\n", &Gfx.MaxTexWidth, &Gfx.MaxTexHeight);
	String_Format1(info, "Depth buffer bits: %i\n",      &depthBits);
	GLContext_GetApiInfo(info);
}

void Gfx_SetFpsLimit(cc_bool vsync, float minFrameMs) {
	gfx_minFrameMs = minFrameMs;
	gfx_vsync      = vsync;
	if (Gfx.Created) GL_UpdateVsync();
}

void Gfx_BeginFrame(void) { frameStart = Stopwatch_Measure(); }
void Gfx_Clear(void) { glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); }
void Gfx_EndFrame(void) { 
	if (!GLContext_SwapBuffers()) Gfx_LoseContext("GLContext lost");
	if (gfx_minFrameMs) LimitFPS();
}

void Gfx_OnWindowResize(void) {
	GLContext_Update();
	/* In case GLContext_Update changes window bounds */
	/* TODO: Eliminate this nasty hack.. */
	Game_UpdateDimensions();
	glViewport(0, 0, Game.Width, Game.Height);
}
