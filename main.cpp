// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief OpenXR playground, exercising many areas of the OpenXR API.
 * Advanced version of the openxr-simple-example
 * @author Christoph Haag <christoph.haag@collabora.com>
 */

#include <assert.h>
#include <getopt.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#ifdef __linux__

// Required headers for OpenGL rendering, as well as for including openxr_platform
#include "GL/glcorearb.h"
#include "GL/glext.h"

// Required headers for windowing, as well as the XrGraphicsBindingOpenGLXlibKHR struct.
#include <X11/Xlib.h>
#include <GL/glx.h>

#include <EGL/egl.h>

#define XR_USE_PLATFORM_XLIB
#define XR_USE_PLATFORM_EGL
#define XR_USE_GRAPHICS_API_OPENGL

#else

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <unknwn.h>
#include <winsock.h>

// #define GL_GLEXT_PROTOTYPES
// #define GL3_PROTOTYPES
//  #include <SDL_opengl.h>
#include <SDL_video.h>
#include <GL/gl.h>
#include "GL/glext.h"
#define XR_USE_GRAPHICS_API_OPENGL
#define XR_USE_PLATFORM_WIN32

// MSVC defines this in winsock2.h!?
/*
typedef struct timeval {
    long tv_sec;
    long tv_usec;
} timeval;
*/


// MSVC needs timezone to be declared
struct timezone;

int
gettimeofday(struct timeval* tp, struct timezone* tzp)
{
	// Note: some broken versions only have 8 trailing zero's, the correct epoch has 9 trailing zero's
	// This magic number is the number of 100 nanosecond intervals since January 1, 1601 (UTC)
	// until 00:00:00 January 1, 1970
	static const uint64_t EPOCH = ((uint64_t)116444736000000000ULL);

	SYSTEMTIME system_time;
	FILETIME file_time;
	uint64_t time;

	GetSystemTime(&system_time);
	SystemTimeToFileTime(&system_time, &file_time);
	time = ((uint64_t)file_time.dwLowDateTime);
	time += ((uint64_t)file_time.dwHighDateTime) << 32;

	tp->tv_sec = (long)((time - EPOCH) / 10000000L);
	tp->tv_usec = (long)(system_time.wMilliseconds * 1000);
	return 0;
}

#endif

#ifndef _WIN32
#include <sys/time.h>
#endif
#include <string.h>

#include "external/openxr_headers/openxr.h"
#include "external/openxr_headers/openxr_platform.h"
#include "external/openxr_headers/openxr_reflection.h"

#include "external/openxr_headers/XR_MNDX_xdev_space.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_events.h>

#define GL_DECL(TYPE, FUNC) static TYPE FUNC = NULL;

#define LOAD_GL_FUNC(TYPE, FUNC) *(void**)&(FUNC) = SDL_GL_GetProcAddress(#FUNC);

#define FOR_EACH_GL_FUNC(_)                                                                        \
	_(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers)                                             \
	_(PFNGLDEBUGMESSAGECALLBACKPROC, glDebugMessageCallback)                                         \
	_(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers)                                                   \
	_(PFNGLCREATESHADERPROC, glCreateShader)                                                         \
	_(PFNGLSHADERSOURCEPROC, glShaderSource)                                                         \
	_(PFNGLCOMPILESHADERPROC, glCompileShader)                                                       \
	_(PFNGLGETSHADERIVPROC, glGetShaderiv)                                                           \
	_(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                                                 \
	_(PFNGLCREATEPROGRAMPROC, glCreateProgram)                                                       \
	_(PFNGLATTACHSHADERPROC, glAttachShader)                                                         \
	_(PFNGLLINKPROGRAMPROC, glLinkProgram)                                                           \
	_(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                                                         \
	_(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                                               \
	_(PFNGLDELETESHADERPROC, glDeleteShader)                                                         \
	_(PFNGLGENBUFFERSPROC, glGenBuffers)                                                             \
	_(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                                                   \
	_(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                                                   \
	_(PFNGLBINDBUFFERPROC, glBindBuffer)                                                             \
	_(PFNGLBUFFERDATAPROC, glBufferData)                                                             \
	_(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)                                           \
	_(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)                                   \
	_(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)                                             \
	_(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                                                   \
	_(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D)                                         \
	_(PFNGLUSEPROGRAMPROC, glUseProgram)                                                             \
	_(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv)                                                 \
	_(PFNGLBLITNAMEDFRAMEBUFFERPROC, glBlitNamedFramebuffer)                                         \
	_(PFNGLUNIFORM4FPROC, glUniform4f)                                                               \
	_(PFNGLACTIVETEXTUREPROC, glActiveTextureARB)                                                    \
	_(PFNGLUNIFORM1IPROC, glUniform1i)


// HACK: Use the ARB extension version because on windows glActiveTexture is not declared and on
// linux glx.h pulls in system gl.h that declares it.

// creates a global declaration for each gl fun listed in FOR_EACH_GL_FUNC
FOR_EACH_GL_FUNC(GL_DECL)

// initializes each global declaration with SDL_GL_GetProcAddress
void
init_gl_funcs(void){FOR_EACH_GL_FUNC(LOAD_GL_FUNC)}


/*
This file contains expansion macros (X Macros) for OpenXR enumerations and structures.
Example of how to use expansion macros to make an enum-to-string function:
*/

#define XR_ENUM_UNKNOWN_VALUE 0x7FFFFFFFLL

#define XR_ENUM_CASE_STR(name, val)                                                                \
	case name: return #name;
#define XR_ENUM_STR(enumType)                                                                      \
	constexpr const char* XrStr_##enumType(enumType e)                                               \
	{                                                                                                \
		switch (e) {                                                                                   \
			XR_LIST_ENUM_##enumType(XR_ENUM_CASE_STR) default : return "Unknown";                        \
		}                                                                                              \
	}

#define XR_STR_IF_ENUM(name, val)                                                                  \
	if (strcmp(#name, e) == 0)                                                                       \
		return static_cast<int64_t>(val);
#define XR_STR_ENUM(enumType)                                                                      \
	inline int64_t XrEnumValue_##enumType(const char* e)                                             \
	{                                                                                                \
		XR_LIST_ENUM_##enumType(XR_STR_IF_ENUM) return XR_ENUM_UNKNOWN_VALUE;                          \
	}                                                                                                \
	inline enumType XrEnum_##enumType(const char* e)                                                 \
	{                                                                                                \
		return static_cast<enumType>(XrEnumValue_##enumType(e));                                       \
	}

#define XR_PRINT_ENUM(name, val)                                                                   \
	if ((val) != XR_ENUM_UNKNOWN_VALUE)                                                              \
		printf("\t\t%s\n", #name);
#define XR_ENUM_PRINT_VALS(enumType)                                                               \
	void XrPrintEnum_##enumType(void)                                                                \
	{                                                                                                \
		XR_LIST_ENUM_##enumType(XR_PRINT_ENUM)                                                         \
	}

// instead of invoking each of the macros for every type, make a meta macro
#define XR_MACROS(enumType)                                                                        \
	XR_ENUM_STR(enumType)                                                                            \
	XR_STR_ENUM(enumType)                                                                            \
	XR_ENUM_PRINT_VALS(enumType)

// clang-format being annoying, even when disabled for this block, it indents the typedef below...
XR_MACROS(XrResult)                     //
    XR_MACROS(XrFormFactor)             //
    XR_MACROS(XrReferenceSpaceType)     //
    XR_MACROS(XrViewConfigurationType)  //
    XR_MACROS(XrEnvironmentBlendMode)   //
    XR_MACROS(XrPlaneDetectionStateEXT) //


#define degrees_to_radians(angle_degrees) ((angle_degrees) * M_PI / 180.0)
#define radians_to_degrees(angle_radians) ((angle_radians) * 180.0 / M_PI)

    // we need an identity pose for creating spaces without offsets
    static XrPosef identity_pose = {
	    .orientation = {.x = 0, .y = 0, .z = 0, .w = 1.0},
	    .position = {.x = 0, .y = 0, .z = 0}
    };

#define HAND_LEFT_INDEX 0
#define HAND_RIGHT_INDEX 1
#define HAND_COUNT 2

// =============================================================================
// math code adapted from
// https://github.com/KhronosGroup/OpenXR-SDK-Source/blob/master/src/common/xr_linear.h
// Copyright (c) 2017 The Khronos Group Inc.
// Copyright (c) 2016 Oculus VR, LLC.
// SPDX-License-Identifier: Apache-2.0
// =============================================================================

typedef enum
{
	GRAPHICS_VULKAN,
	GRAPHICS_OPENGL,
	GRAPHICS_OPENGL_ES
} GraphicsAPI;

typedef struct XrMatrix4x4f
{
	float m[16];
} XrMatrix4x4f;

inline static void
XrMatrix4x4f_CreateProjectionFov(XrMatrix4x4f* result,
                                 GraphicsAPI graphicsApi,
                                 const XrFovf fov,
                                 const float nearZ,
                                 const float farZ)
{
	const float tanAngleLeft = tanf(fov.angleLeft);
	const float tanAngleRight = tanf(fov.angleRight);

	const float tanAngleDown = tanf(fov.angleDown);
	const float tanAngleUp = tanf(fov.angleUp);

	const float tanAngleWidth = tanAngleRight - tanAngleLeft;

	// Set to tanAngleDown - tanAngleUp for a clip space with positive Y
	// down (Vulkan). Set to tanAngleUp - tanAngleDown for a clip space with
	// positive Y up (OpenGL / D3D / Metal).
	const float tanAngleHeight =
	    graphicsApi == GRAPHICS_VULKAN ? (tanAngleDown - tanAngleUp) : (tanAngleUp - tanAngleDown);

	// Set to nearZ for a [-1,1] Z clip space (OpenGL / OpenGL ES).
	// Set to zero for a [0,1] Z clip space (Vulkan / D3D / Metal).
	const float offsetZ =
	    (graphicsApi == GRAPHICS_OPENGL || graphicsApi == GRAPHICS_OPENGL_ES) ? nearZ : 0;

	if (farZ <= nearZ) {
		// place the far plane at infinity
		result->m[0] = 2 / tanAngleWidth;
		result->m[4] = 0;
		result->m[8] = (tanAngleRight + tanAngleLeft) / tanAngleWidth;
		result->m[12] = 0;

		result->m[1] = 0;
		result->m[5] = 2 / tanAngleHeight;
		result->m[9] = (tanAngleUp + tanAngleDown) / tanAngleHeight;
		result->m[13] = 0;

		result->m[2] = 0;
		result->m[6] = 0;
		result->m[10] = -1;
		result->m[14] = -(nearZ + offsetZ);

		result->m[3] = 0;
		result->m[7] = 0;
		result->m[11] = -1;
		result->m[15] = 0;
	} else {
		// normal projection
		result->m[0] = 2 / tanAngleWidth;
		result->m[4] = 0;
		result->m[8] = (tanAngleRight + tanAngleLeft) / tanAngleWidth;
		result->m[12] = 0;

		result->m[1] = 0;
		result->m[5] = 2 / tanAngleHeight;
		result->m[9] = (tanAngleUp + tanAngleDown) / tanAngleHeight;
		result->m[13] = 0;

		result->m[2] = 0;
		result->m[6] = 0;
		result->m[10] = -(farZ + offsetZ) / (farZ - nearZ);
		result->m[14] = -(farZ * (nearZ + offsetZ)) / (farZ - nearZ);

		result->m[3] = 0;
		result->m[7] = 0;
		result->m[11] = -1;
		result->m[15] = 0;
	}
}

inline static void
XrMatrix4x4f_CreateFromQuaternion(XrMatrix4x4f* result, const XrQuaternionf* quat)
{
	const float x2 = quat->x + quat->x;
	const float y2 = quat->y + quat->y;
	const float z2 = quat->z + quat->z;

	const float xx2 = quat->x * x2;
	const float yy2 = quat->y * y2;
	const float zz2 = quat->z * z2;

	const float yz2 = quat->y * z2;
	const float wx2 = quat->w * x2;
	const float xy2 = quat->x * y2;
	const float wz2 = quat->w * z2;
	const float xz2 = quat->x * z2;
	const float wy2 = quat->w * y2;

	result->m[0] = 1.0f - yy2 - zz2;
	result->m[1] = xy2 + wz2;
	result->m[2] = xz2 - wy2;
	result->m[3] = 0.0f;

	result->m[4] = xy2 - wz2;
	result->m[5] = 1.0f - xx2 - zz2;
	result->m[6] = yz2 + wx2;
	result->m[7] = 0.0f;

	result->m[8] = xz2 + wy2;
	result->m[9] = yz2 - wx2;
	result->m[10] = 1.0f - xx2 - yy2;
	result->m[11] = 0.0f;

	result->m[12] = 0.0f;
	result->m[13] = 0.0f;
	result->m[14] = 0.0f;
	result->m[15] = 1.0f;
}

inline static void
XrMatrix4x4f_CreateTranslation(XrMatrix4x4f* result, const float x, const float y, const float z)
{
	result->m[0] = 1.0f;
	result->m[1] = 0.0f;
	result->m[2] = 0.0f;
	result->m[3] = 0.0f;
	result->m[4] = 0.0f;
	result->m[5] = 1.0f;
	result->m[6] = 0.0f;
	result->m[7] = 0.0f;
	result->m[8] = 0.0f;
	result->m[9] = 0.0f;
	result->m[10] = 1.0f;
	result->m[11] = 0.0f;
	result->m[12] = x;
	result->m[13] = y;
	result->m[14] = z;
	result->m[15] = 1.0f;
}

inline static void
XrMatrix4x4f_Multiply(XrMatrix4x4f* result, const XrMatrix4x4f* a, const XrMatrix4x4f* b)
{
	result->m[0] = a->m[0] * b->m[0] + a->m[4] * b->m[1] + a->m[8] * b->m[2] + a->m[12] * b->m[3];
	result->m[1] = a->m[1] * b->m[0] + a->m[5] * b->m[1] + a->m[9] * b->m[2] + a->m[13] * b->m[3];
	result->m[2] = a->m[2] * b->m[0] + a->m[6] * b->m[1] + a->m[10] * b->m[2] + a->m[14] * b->m[3];
	result->m[3] = a->m[3] * b->m[0] + a->m[7] * b->m[1] + a->m[11] * b->m[2] + a->m[15] * b->m[3];

	result->m[4] = a->m[0] * b->m[4] + a->m[4] * b->m[5] + a->m[8] * b->m[6] + a->m[12] * b->m[7];
	result->m[5] = a->m[1] * b->m[4] + a->m[5] * b->m[5] + a->m[9] * b->m[6] + a->m[13] * b->m[7];
	result->m[6] = a->m[2] * b->m[4] + a->m[6] * b->m[5] + a->m[10] * b->m[6] + a->m[14] * b->m[7];
	result->m[7] = a->m[3] * b->m[4] + a->m[7] * b->m[5] + a->m[11] * b->m[6] + a->m[15] * b->m[7];

	result->m[8] = a->m[0] * b->m[8] + a->m[4] * b->m[9] + a->m[8] * b->m[10] + a->m[12] * b->m[11];
	result->m[9] = a->m[1] * b->m[8] + a->m[5] * b->m[9] + a->m[9] * b->m[10] + a->m[13] * b->m[11];
	result->m[10] = a->m[2] * b->m[8] + a->m[6] * b->m[9] + a->m[10] * b->m[10] + a->m[14] * b->m[11];
	result->m[11] = a->m[3] * b->m[8] + a->m[7] * b->m[9] + a->m[11] * b->m[10] + a->m[15] * b->m[11];

	result->m[12] =
	    a->m[0] * b->m[12] + a->m[4] * b->m[13] + a->m[8] * b->m[14] + a->m[12] * b->m[15];
	result->m[13] =
	    a->m[1] * b->m[12] + a->m[5] * b->m[13] + a->m[9] * b->m[14] + a->m[13] * b->m[15];
	result->m[14] =
	    a->m[2] * b->m[12] + a->m[6] * b->m[13] + a->m[10] * b->m[14] + a->m[14] * b->m[15];
	result->m[15] =
	    a->m[3] * b->m[12] + a->m[7] * b->m[13] + a->m[11] * b->m[14] + a->m[15] * b->m[15];
}

inline static void
XrMatrix4x4f_Invert(XrMatrix4x4f* result, const XrMatrix4x4f* src)
{
	result->m[0] = src->m[0];
	result->m[1] = src->m[4];
	result->m[2] = src->m[8];
	result->m[3] = 0.0f;
	result->m[4] = src->m[1];
	result->m[5] = src->m[5];
	result->m[6] = src->m[9];
	result->m[7] = 0.0f;
	result->m[8] = src->m[2];
	result->m[9] = src->m[6];
	result->m[10] = src->m[10];
	result->m[11] = 0.0f;
	result->m[12] = -(src->m[0] * src->m[12] + src->m[1] * src->m[13] + src->m[2] * src->m[14]);
	result->m[13] = -(src->m[4] * src->m[12] + src->m[5] * src->m[13] + src->m[6] * src->m[14]);
	result->m[14] = -(src->m[8] * src->m[12] + src->m[9] * src->m[13] + src->m[10] * src->m[14]);
	result->m[15] = 1.0f;
}

inline static void
XrMatrix4x4f_CreateViewMatrix(XrMatrix4x4f* result,
                              const XrVector3f* translation,
                              const XrQuaternionf* rotation)
{

	XrMatrix4x4f rotationMatrix;
	XrMatrix4x4f_CreateFromQuaternion(&rotationMatrix, rotation);

	XrMatrix4x4f translationMatrix;
	XrMatrix4x4f_CreateTranslation(&translationMatrix, translation->x, translation->y,
	                               translation->z);

	XrMatrix4x4f viewMatrix;
	XrMatrix4x4f_Multiply(&viewMatrix, &translationMatrix, &rotationMatrix);

	XrMatrix4x4f_Invert(result, &viewMatrix);
}

inline static void
XrMatrix4x4f_CreateScale(XrMatrix4x4f* result, const float x, const float y, const float z)
{
	result->m[0] = x;
	result->m[1] = 0.0f;
	result->m[2] = 0.0f;
	result->m[3] = 0.0f;
	result->m[4] = 0.0f;
	result->m[5] = y;
	result->m[6] = 0.0f;
	result->m[7] = 0.0f;
	result->m[8] = 0.0f;
	result->m[9] = 0.0f;
	result->m[10] = z;
	result->m[11] = 0.0f;
	result->m[12] = 0.0f;
	result->m[13] = 0.0f;
	result->m[14] = 0.0f;
	result->m[15] = 1.0f;
}

inline static void
XrMatrix4x4f_CreateModelMatrix(XrMatrix4x4f* result,
                               const XrVector3f* translation,
                               const XrQuaternionf* rotation,
                               const XrVector3f* scale)
{
	XrMatrix4x4f scaleMatrix;
	XrMatrix4x4f_CreateScale(&scaleMatrix, scale->x, scale->y, scale->z);

	XrMatrix4x4f rotationMatrix;
	XrMatrix4x4f_CreateFromQuaternion(&rotationMatrix, rotation);

	XrMatrix4x4f translationMatrix;
	XrMatrix4x4f_CreateTranslation(&translationMatrix, translation->x, translation->y,
	                               translation->z);

	XrMatrix4x4f combinedMatrix;
	XrMatrix4x4f_Multiply(&combinedMatrix, &rotationMatrix, &scaleMatrix);
	XrMatrix4x4f_Multiply(result, &translationMatrix, &combinedMatrix);
}
// =============================================================================



// =============================================================================
// OpenGL rendering code at the end of the file
// =============================================================================
struct gl_renderer_t
{
	// To render into a texture we need a framebuffer (one per texture to make it easy)
	GLuint** framebuffers;

	float near_z;
	float far_z;

	GLuint shader_program_id;
	GLuint VAO;

	struct
	{
		bool initialized;
		GLuint texture;
		GLuint fbo;
	} quad;

	int modelLoc;
	int colorLoc;
	int viewLoc;
	int projLoc;
	int busyLoopsLoc;
};

struct hand_tracking_t;

bool
init_sdl_window(int w, int h);

int
init_gl(uint32_t view_count, uint32_t* swapchain_lengths, struct gl_renderer_t* gl_renderer);

struct ApplicationState;

void
render_frame(struct ApplicationState* app,
             struct gl_renderer_t* gl_renderer,
             XrTime predictedDisplayTime,
             XrSpaceLocation* hand_locations,
             struct hand_tracking_t* hand_tracking,
             bool depth_supported);

struct quad_layer_t;
void
render_quad(struct gl_renderer_t* gl_renderer,
            struct quad_layer_t* quad,
            uint32_t swapchain_index,
            XrTime predictedDisplayTime);

// =============================================================================



// true if XrResult is a success code, else print error message and return false
bool
xr_check(XrInstance instance, XrResult result, const char* format, ...)
{
	if (XR_SUCCEEDED(result))
		return true;

	char resultString[XR_MAX_RESULT_STRING_SIZE];
	char formatRes[XR_MAX_RESULT_STRING_SIZE + 1024];

	if (instance && XR_SUCCEEDED(xrResultToString(instance, result, resultString)))
		snprintf(formatRes, XR_MAX_RESULT_STRING_SIZE + 1023, "%s [%s]\n", format, resultString);
	else
		snprintf(formatRes, XR_MAX_RESULT_STRING_SIZE + 1023, "%s\n", format);

	va_list args;
	va_start(args, format);
	vprintf(formatRes, args);
	va_end(args);

	return false;
}


static void
print_system_properties(XrSystemProperties* system_properties)
{
	printf("System properties for system %lu: \"%s\", vendor ID %d\n", system_properties->systemId,
	       system_properties->systemName, system_properties->vendorId);
	printf("\tMax layers          : %d\n", system_properties->graphicsProperties.maxLayerCount);
	printf("\tMax swapchain height: %d\n",
	       system_properties->graphicsProperties.maxSwapchainImageHeight);
	printf("\tMax swapchain width : %d\n",
	       system_properties->graphicsProperties.maxSwapchainImageWidth);
	printf("\tOrientation Tracking: %d\n", system_properties->trackingProperties.orientationTracking);
	printf("\tPosition Tracking   : %d\n", system_properties->trackingProperties.positionTracking);

	const XrBaseInStructure* next = static_cast<const XrBaseInStructure*>(system_properties->next);
	while (next) {
		if (next->type == XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT) {
			XrSystemHandTrackingPropertiesEXT* ht = (XrSystemHandTrackingPropertiesEXT*)next;
			printf("\tHand Tracking       : %d\n", ht->supportsHandTracking);
		}
		if (next->type == XR_TYPE_SYSTEM_XDEV_SPACE_PROPERTIES_MNDX) {
			XrSystemXDevSpacePropertiesMNDX* xd = (XrSystemXDevSpacePropertiesMNDX*)next;
			printf("\txdev space          : %d\n", xd->supportsXDevSpace);
		}
		next = next->next;
	}
}

static void
print_supported_view_configs(XrInstance instance, XrSystemId system_id)
{
	XrResult result;

	uint32_t view_config_count;
	result = xrEnumerateViewConfigurations(instance, system_id, 0, &view_config_count, NULL);
	if (!xr_check(instance, result, "Failed to get view configuration count"))
		return;

	printf("Runtime supports %d view configurations\n", view_config_count);

	XrViewConfigurationType* view_configs =
	    (XrViewConfigurationType*)malloc(view_config_count * sizeof(XrViewConfigurationType));
	result = xrEnumerateViewConfigurations(instance, system_id, view_config_count, &view_config_count,
	                                       view_configs);
	if (!xr_check(instance, result, "Failed to enumerate view configurations!"))
		return;

	for (uint32_t i = 0; i < view_config_count; ++i) {
		XrViewConfigurationProperties props{
		    .type = XR_TYPE_VIEW_CONFIGURATION_PROPERTIES,
		    .next = nullptr,
		    .viewConfigurationType = view_configs[i],
		    .fovMutable = false,
		};

		result = xrGetViewConfigurationProperties(instance, system_id, view_configs[i], &props);
		if (!xr_check(instance, result, "Failed to get view configuration info %d!", i))
			return;

		printf("type %d: FOV mutable: %d\n", props.viewConfigurationType, props.fovMutable);
	}
	free(view_configs);
}

static void
print_viewconfig_view_info(uint32_t view_count, XrViewConfigurationView* viewconfig_views)
{
	for (uint32_t i = 0; i < view_count; i++) {
		printf("View Configuration View %d:\n", i);
		printf("\tResolution       : Recommended %dx%d, Max: %dx%d\n",
		       viewconfig_views[0].recommendedImageRectWidth,
		       viewconfig_views[0].recommendedImageRectHeight, viewconfig_views[0].maxImageRectWidth,
		       viewconfig_views[0].maxImageRectHeight);
		printf("\tSwapchain Samples: Recommended: %d, Max: %d)\n",
		       viewconfig_views[0].recommendedSwapchainSampleCount,
		       viewconfig_views[0].maxSwapchainSampleCount);
	}
}

static void
print_reference_spaces(XrInstance instance, XrSession session)
{
	XrResult result;

	uint32_t ref_space_count;
	result = xrEnumerateReferenceSpaces(session, 0, &ref_space_count, NULL);
	if (!xr_check(instance, result, "Getting number of reference spaces failed!"))
		return;

	auto* ref_spaces =
	    static_cast<XrReferenceSpaceType*>(malloc(sizeof(XrReferenceSpaceType) * ref_space_count));
	result = xrEnumerateReferenceSpaces(session, ref_space_count, &ref_space_count, ref_spaces);
	if (!xr_check(instance, result, "Enumerating reference spaces failed!"))
		return;

	printf("Runtime supports %d reference spaces:\n", ref_space_count);
	for (uint32_t i = 0; i < ref_space_count; i++) {
		if (ref_spaces[i] == XR_REFERENCE_SPACE_TYPE_LOCAL) {
			printf("\tXR_REFERENCE_SPACE_TYPE_LOCAL\n");
		} else if (ref_spaces[i] == XR_REFERENCE_SPACE_TYPE_STAGE) {
			printf("\tXR_REFERENCE_SPACE_TYPE_STAGE\n");
		} else if (ref_spaces[i] == XR_REFERENCE_SPACE_TYPE_VIEW) {
			printf("\tXR_REFERENCE_SPACE_TYPE_VIEW\n");
		} else {
			printf("\tOther (extension?) refspace %u\\n", ref_spaces[i]);
		}
	}
	free(ref_spaces);
}

static bool
check_opengl_version(XrGraphicsRequirementsOpenGLKHR* opengl_reqs)
{
	XrVersion desired_opengl_version = XR_MAKE_VERSION(3, 3, 0);
	if (desired_opengl_version > opengl_reqs->maxApiVersionSupported ||
	    desired_opengl_version < opengl_reqs->minApiVersionSupported) {
		printf(
		    "We want OpenGL %d.%d.%d, but runtime only supports OpenGL "
		    "%d.%d.%d - %d.%d.%d!\n",
		    XR_VERSION_MAJOR(desired_opengl_version), XR_VERSION_MINOR(desired_opengl_version),
		    XR_VERSION_PATCH(desired_opengl_version),
		    XR_VERSION_MAJOR(opengl_reqs->minApiVersionSupported),
		    XR_VERSION_MINOR(opengl_reqs->minApiVersionSupported),
		    XR_VERSION_PATCH(opengl_reqs->minApiVersionSupported),
		    XR_VERSION_MAJOR(opengl_reqs->maxApiVersionSupported),
		    XR_VERSION_MINOR(opengl_reqs->maxApiVersionSupported),
		    XR_VERSION_PATCH(opengl_reqs->maxApiVersionSupported));
		return false;
	}
	return true;
}

// returns the preferred swapchain format if it is supported
// else:
// - if fallback is true, return the first supported format
// - if fallback is false, return -1
static int64_t
get_swapchain_format(XrInstance instance,
                     XrSession session,
                     int64_t preferred_format,
                     bool fallback)
{
	XrResult result;

	uint32_t swapchain_format_count;
	result = xrEnumerateSwapchainFormats(session, 0, &swapchain_format_count, NULL);
	if (!xr_check(instance, result, "Failed to get number of supported swapchain formats"))
		return -1;

	printf("Runtime supports %d swapchain formats\n", swapchain_format_count);
	auto* swapchain_formats = static_cast<int64_t*>(malloc(sizeof(int64_t) * swapchain_format_count));
	result = xrEnumerateSwapchainFormats(session, swapchain_format_count, &swapchain_format_count,
	                                     swapchain_formats);
	if (!xr_check(instance, result, "Failed to enumerate swapchain formats"))
		return -1;

	int64_t chosen_format = fallback ? swapchain_formats[0] : -1;

	for (uint32_t i = 0; i < swapchain_format_count; i++) {
		printf("Supported GL format: %#lx\n", swapchain_formats[i]);
		if (swapchain_formats[i] == preferred_format) {
			chosen_format = swapchain_formats[i];
			printf("Using preferred swapchain format %#lx\n", chosen_format);
			break;
		}
	}
	if (fallback && chosen_format != preferred_format) {
		printf("Falling back to non preferred swapchain format %#lx\n", chosen_format);
	}

	free(swapchain_formats);

	return chosen_format;
}

struct base_extension_t;

// function that initializes an extension struct basics like ext_name_string
typedef bool (*init_ext_struct)(struct base_extension_t** out_base);

// function that gets the function pointers for an extension struct
typedef XrResult (*init_ext_fp)(XrInstance instance, struct base_extension_t* base);

struct base_extension_t
{
	bool supported;
	uint32_t version;

	char* ext_name_string;

	init_ext_fp init_fp;
};

#define LOAD_OR_RETURN(NAME, LOCATION)                                                             \
	{                                                                                                \
		XrResult result =                                                                              \
		    xrGetInstanceProcAddr(instance, #NAME, (PFN_xrVoidFunction*)&LOCATION->NAME);              \
		if (!xr_check(instance, result, "Failed to get %s function!", #NAME))                          \
			return result;                                                                               \
	}


struct opengl_t
{
	struct base_extension_t base;

	// functions belonging to extensions must be loaded with xrGetInstanceProcAddr before use
	PFN_xrGetOpenGLGraphicsRequirementsKHR xrGetOpenGLGraphicsRequirementsKHR;
};
static XrResult
init_opengl_fp(XrInstance instance, struct base_extension_t* base)
{
	struct opengl_t* opengl = (struct opengl_t*)base;
	LOAD_OR_RETURN(xrGetOpenGLGraphicsRequirementsKHR, opengl)
	return XR_SUCCESS;
}
static bool
init_opengl_t(struct base_extension_t** out_base)
{
	*out_base = static_cast<base_extension_t*>(malloc(sizeof(struct opengl_t)));
	(*out_base)->ext_name_string = XR_KHR_OPENGL_ENABLE_EXTENSION_NAME;
	(*out_base)->init_fp = &init_opengl_fp;

	return true;
}

// EGL does not have function pointers and we don't store associated state here
static bool
init_egl_t(struct base_extension_t** out_base)
{
	*out_base = static_cast<base_extension_t*>(malloc(sizeof(struct base_extension_t)));
#ifdef XR_USE_PLATFORM_EGL
	(*out_base)->ext_name_string = XR_MNDX_EGL_ENABLE_EXTENSION_NAME;
#else
	// just a random string that will never be accidentally a valid extension name
	(*out_base)->ext_name_string = "INVALID_EXTENSION";
#endif
	(*out_base)->init_fp = NULL;

	return true;
}


struct hand_tracking_t
{
	struct base_extension_t base;

	// whether the current VR system in use has hand tracking
	bool system_supported;
	XrHandTrackerEXT trackers[HAND_COUNT];

	// out data
	XrHandJointLocationEXT joints[HAND_COUNT][XR_HAND_JOINT_COUNT_EXT];
	XrHandJointLocationsEXT joint_locations[HAND_COUNT];

	// optional
	XrHandJointVelocitiesEXT joint_velocities[HAND_COUNT];
	XrHandJointVelocityEXT joint_velocities_arr[HAND_COUNT][XR_HAND_JOINT_COUNT_EXT];

	PFN_xrLocateHandJointsEXT xrLocateHandJointsEXT;
	PFN_xrCreateHandTrackerEXT xrCreateHandTrackerEXT;
};
static XrResult
init_hand_tracking_fp(XrInstance instance, struct base_extension_t* base)
{
	struct hand_tracking_t* hand_tracking = (struct hand_tracking_t*)base;
	LOAD_OR_RETURN(xrLocateHandJointsEXT, hand_tracking)
	LOAD_OR_RETURN(xrCreateHandTrackerEXT, hand_tracking)
	return XR_SUCCESS;
}
static bool
init_hand_tracking_t(struct base_extension_t** out_base)
{
	*out_base = static_cast<base_extension_t*>(malloc(sizeof(struct hand_tracking_t)));

	(*out_base)->ext_name_string = XR_EXT_HAND_TRACKING_EXTENSION_NAME;
	(*out_base)->init_fp = &init_hand_tracking_fp;
	return true;
}


struct depth_t
{
	struct base_extension_t base;
	XrCompositionLayerDepthInfoKHR* infos;
};
static bool
init_depth_t(struct base_extension_t** out_base)
{
	*out_base = static_cast<base_extension_t*>(malloc(sizeof(struct depth_t)));

	(*out_base)->ext_name_string = XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME;
	(*out_base)->init_fp = NULL;
	return true;
}


struct refresh_rate_t
{
	struct base_extension_t base;
	PFN_xrEnumerateDisplayRefreshRatesFB xrEnumerateDisplayRefreshRatesFB;
	PFN_xrGetDisplayRefreshRateFB xrGetDisplayRefreshRateFB;
	PFN_xrRequestDisplayRefreshRateFB xrRequestDisplayRefreshRateFB;
};
static XrResult
init_refresh_rate_fp(XrInstance instance, struct base_extension_t* base)
{
	struct refresh_rate_t* refresh_rate = (struct refresh_rate_t*)base;
	LOAD_OR_RETURN(xrEnumerateDisplayRefreshRatesFB, refresh_rate)
	LOAD_OR_RETURN(xrGetDisplayRefreshRateFB, refresh_rate)
	LOAD_OR_RETURN(xrRequestDisplayRefreshRateFB, refresh_rate)
	return XR_SUCCESS;
}
static bool
init_refresh_rate_t(struct base_extension_t** out_base)
{
	*out_base = static_cast<base_extension_t*>(malloc(sizeof(struct refresh_rate_t)));

	(*out_base)->ext_name_string = XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME;
	(*out_base)->init_fp = &init_refresh_rate_fp;
	return true;
}

struct polygon_t
{
	uint32_t vertex_count;
	XrVector2f* vertices;
};
struct plane_data_t
{
	uint64_t plane_id;
	uint32_t polygon_count;
	struct polygon_t* polygons;
	struct plane_data_t* next;
};
struct plane_detection_t
{
	struct base_extension_t base;

	PFN_xrCreatePlaneDetectorEXT xrCreatePlaneDetectorEXT;
	PFN_xrDestroyPlaneDetectorEXT xrDestroyPlaneDetectorEXT;
	PFN_xrBeginPlaneDetectionEXT xrBeginPlaneDetectionEXT;
	PFN_xrGetPlaneDetectionStateEXT xrGetPlaneDetectionStateEXT;
	PFN_xrGetPlaneDetectionsEXT xrGetPlaneDetectionsEXT;
	PFN_xrGetPlanePolygonBufferEXT xrGetPlanePolygonBufferEXT;

	XrPlaneDetectorEXT pd;
	XrPlaneDetectionStateEXT state;

	XrPlaneDetectorLocationsEXT locs;

	struct plane_data_t* plane_data_list;
};
static XrResult
init_plane_detection_fp(XrInstance instance, struct base_extension_t* base)
{
	struct plane_detection_t* plane_detection = (struct plane_detection_t*)base;
	LOAD_OR_RETURN(xrCreatePlaneDetectorEXT, plane_detection)
	LOAD_OR_RETURN(xrDestroyPlaneDetectorEXT, plane_detection)
	LOAD_OR_RETURN(xrBeginPlaneDetectionEXT, plane_detection)
	LOAD_OR_RETURN(xrGetPlaneDetectionStateEXT, plane_detection)
	LOAD_OR_RETURN(xrGetPlaneDetectionsEXT, plane_detection)
	LOAD_OR_RETURN(xrGetPlanePolygonBufferEXT, plane_detection)

	return XR_SUCCESS;
}
static bool
init_plane_detection_t(struct base_extension_t** out_base)
{
	*out_base = static_cast<base_extension_t*>(malloc(sizeof(struct plane_detection_t)));

	(*out_base)->ext_name_string = XR_EXT_PLANE_DETECTION_EXTENSION_NAME;
	(*out_base)->init_fp = &init_plane_detection_fp;

	struct plane_detection_t* pd = (struct plane_detection_t*)*out_base;
	pd->pd = XR_NULL_HANDLE;
	pd->state = XR_PLANE_DETECTION_STATE_NONE_EXT;
	pd->locs.planeLocationCountOutput = 0;
	pd->locs.planeLocationCapacityInput = 0;
	pd->locs.planeLocations = NULL;

	return true;
}

#define PFN_DECL(FUNCTION) PFN_##FUNCTION FUNCTION

struct hand_interaction_t
{
	struct base_extension_t base;
};
static bool
init_hand_interaction_t(struct base_extension_t** out_base)
{
	*out_base = static_cast<base_extension_t*>(malloc(sizeof(struct hand_interaction_t)));

	(*out_base)->ext_name_string = XR_EXT_HAND_INTERACTION_EXTENSION_NAME;
	(*out_base)->init_fp = NULL;
	return true;
}

struct user_presence_t
{
	struct base_extension_t base;
};
static bool
init_user_presence_t(struct base_extension_t** out_base)
{
	*out_base = static_cast<base_extension_t*>(malloc(sizeof(struct user_presence_t)));

	(*out_base)->ext_name_string = XR_EXT_USER_PRESENCE_EXTENSION_NAME;
	(*out_base)->init_fp = NULL;
	return true;
}

// wrapper struct to store everything related to one xdev
struct xdev_space_element
{
	XrXDevIdMNDX xid;
	XrXDevPropertiesMNDX xprops;
	XrSpace space;
	struct xdev_space_element* next;
};
void
destroy_xdev_space(XrInstance instance, struct xdev_space_element** element)
{
	if ((*element)->space != XR_NULL_HANDLE) {

		XrResult result = xrDestroySpace((*element)->space);

		// can't really do anything about it
		xr_check(NULL, result, "Failed to destroy xdev space");
	}

	free(*element);
	*element = NULL;
}
void
destroy_xdev_space_list(XrInstance instance, struct xdev_space_element** list)
{
	struct xdev_space_element* element = *list;

	while (element) {
		struct xdev_space_element* next = element->next;

		destroy_xdev_space(instance, &element);

		element = next;
	}
	*list = NULL;
}
bool
try_move(struct xdev_space_element** old_list,
         struct xdev_space_element** new_list,
         XrXDevPropertiesMNDX* prop)
{
	// remove the element from old_list, if any, and cache it in removed_element
	struct xdev_space_element* removed_element = NULL;
	{
		struct xdev_space_element* prev = NULL;
		struct xdev_space_element* current = *old_list;
		while (current) {
			// serial is supposed to be globally unique in monado
			if (strcmp(current->xprops.serial, prop->serial) == 0) {
				printf("Keeping xdev %s [%s] alive", prop->name, prop->serial);
				if (prev == NULL) {
					*old_list = current->next;
					removed_element = current;
				} else {
					prev->next = current->next;
					removed_element = current;
					break;
				}
			}
		}
	}

	// if an element has been removed from old_list, add it to new_list and return true to indicate
	// the element was found
	if (removed_element) {
		// prepend for simplicity
		removed_element->next = *new_list;
		*new_list = removed_element;
		return true;
	}

	return false;
}

struct xdev_space_t
{
	struct base_extension_t base;

	PFN_xrCreateXDevListMNDX xrCreateXDevListMNDX;
	PFN_xrGetXDevListGenerationNumberMNDX xrGetXDevListGenerationNumberMNDX;
	PFN_xrEnumerateXDevsMNDX xrEnumerateXDevsMNDX;
	PFN_xrGetXDevPropertiesMNDX xrGetXDevPropertiesMNDX;
	PFN_xrDestroyXDevListMNDX xrDestroyXDevListMNDX;
	PFN_xrCreateXDevSpaceMNDX xrCreateXDevSpaceMNDX;

	struct xdev_space_element* xdev_space_list;

	// only need to check for new/disappeared xdevs when the generation id changes
	uint64_t last_generation_id;

	// true if both the runtime supports the extension and the current system (hardware) in use
	// supports it too.
	bool system_supported;
};
static XrResult
init_xdev_space_fp(XrInstance instance, struct base_extension_t* base)
{
	struct xdev_space_t* xdev_space = (struct xdev_space_t*)base;
	LOAD_OR_RETURN(xrCreateXDevListMNDX, xdev_space)
	LOAD_OR_RETURN(xrGetXDevListGenerationNumberMNDX, xdev_space)
	LOAD_OR_RETURN(xrEnumerateXDevsMNDX, xdev_space)
	LOAD_OR_RETURN(xrGetXDevPropertiesMNDX, xdev_space)
	LOAD_OR_RETURN(xrDestroyXDevListMNDX, xdev_space)
	LOAD_OR_RETURN(xrCreateXDevSpaceMNDX, xdev_space)
	return XR_SUCCESS;
}
static bool
init_xdev_space_t(struct base_extension_t** out_base)
{
	*out_base = static_cast<base_extension_t*>(malloc(sizeof(struct xdev_space_t)));

	(*out_base)->ext_name_string = XR_MNDX_XDEV_SPACE_EXTENSION_NAME;
	(*out_base)->init_fp = init_xdev_space_fp;

	((struct xdev_space_t*)*out_base)->xdev_space_list = NULL;
	((struct xdev_space_t*)*out_base)->last_generation_id = 0;
	((struct xdev_space_t*)*out_base)->system_supported = false;

	return true;
}
bool
cleanup_update_xdev_spaces(XrInstance instance,
                           XrSession session,
                           struct xdev_space_t* xdev_space,
                           XrXDevListMNDX* list,
                           XrXDevIdMNDX** xdevs,
                           struct xdev_space_element** old_list)
{
	XrResult result = XR_SUCCESS;

	// everything we knew from last time has been moved to the new list, so we can destroy everything
	// that is still on the old list
	destroy_xdev_space_list(instance, old_list);

	if (*list != XR_NULL_HANDLE) {
		result = xdev_space->xrDestroyXDevListMNDX(*list);
		xr_check(instance, result, "Failed to destroy xdev space list"); // we don't care
		*list = XR_NULL_HANDLE;
		// printf("Destroyed xdev list\n");
	}

	free(*xdevs);
	*xdevs = NULL;
	return true;
}
bool
update_xdev_spaces(XrInstance instance, XrSession session, struct xdev_space_t* xdev_space)
{
	XrResult result = XR_SUCCESS;
	XrXDevIdMNDX* xdevs = NULL;
	struct xdev_space_element* old_list = NULL;

	XrXDevListMNDX list = XR_NULL_HANDLE;
	XrCreateXDevListInfoMNDX create_info = {
	    .type = XR_TYPE_CREATE_XDEV_LIST_INFO_MNDX,
	    .next = nullptr,
	};
	result = xdev_space->xrCreateXDevListMNDX(session, &create_info, &list);
	if (!xr_check(instance, result, "Failed to create xdev list")) {
		return false;
	}

	uint64_t generation = 0;
	result = xdev_space->xrGetXDevListGenerationNumberMNDX(list, &generation);
	if (!xr_check(NULL, result, "Failed to get xdev list generation")) {
		return false;
	}

	// no new or disappeared xdevs, nothing to do
	if (generation == xdev_space->last_generation_id) {
		cleanup_update_xdev_spaces(instance, session, xdev_space, &list, &xdevs, &old_list);
		// printf("xdev list generation unchanged: %lu\n", generation);
		return true;
	}

	printf("xdev list generation changed: %lu -> %lu\n", xdev_space->last_generation_id, generation);

	uint32_t count = 0;
	result = xdev_space->xrEnumerateXDevsMNDX(list, 0, &count, NULL);
	if (!xr_check(instance, result, "Failed to enumerate xdev list capacity")) {
		cleanup_update_xdev_spaces(instance, session, xdev_space, &list, &xdevs, &old_list);
		return false;
	}

	assert(count > 0);

	xdevs = static_cast<XrXDevIdMNDX*>(malloc(sizeof(XrXDevIdMNDX) * count));

	result = xdev_space->xrEnumerateXDevsMNDX(list, count, &count, xdevs);
	if (!xr_check(instance, result, "Failed to enumerate xdev list")) {
		cleanup_update_xdev_spaces(instance, session, xdev_space, &list, &xdevs, &old_list);
		return false;
	}

	printf("enumerated %d devices\n", count);

	// crude: if we recognize an xdev id from the previous generation, just pick it out of the old
	// list into the new one to avoid recreating XrSpace etc.
	old_list = xdev_space->xdev_space_list;
	xdev_space->xdev_space_list = NULL;

	for (uint32_t i = 0; i < count; i++) {
		XrGetXDevInfoMNDX info = {
		    .type = XR_TYPE_GET_XDEV_INFO_MNDX,
		    .next = nullptr,
		    .id = xdevs[i],
		};
		XrXDevPropertiesMNDX prop = {
		    .type = XR_TYPE_XDEV_PROPERTIES_MNDX,
		    .next = nullptr,
		    .name = "",
		    .serial = "",
		    .canCreateSpace = false,
		};

		result = xdev_space->xrGetXDevPropertiesMNDX(list, &info, &prop);
		if (!xr_check(instance, result, "Failed to get xdev id %lu properties", xdevs[i])) {
			cleanup_update_xdev_spaces(instance, session, xdev_space, &list, &xdevs, &old_list);
			return false;
		}

		if (try_move(&old_list, &xdev_space->xdev_space_list, &prop)) {
			// we already know this xdev, we moved it to to the new list and don't need to do anything
			// else
			continue;
		}

		// we did not find this xdev in the old list, meaning we haven't seen it before
		auto* element = static_cast<xdev_space_element*>(malloc(sizeof(struct xdev_space_element)));
		element->xid = info.id;
		element->xprops = prop;
		printf("\tnew xdev: %s [%s], can create space: %d\n", prop.name, prop.serial,
		       prop.canCreateSpace);
		if (prop.canCreateSpace) {
			XrCreateXDevSpaceInfoMNDX space_create_info = {
			    .type = XR_TYPE_CREATE_XDEV_SPACE_INFO_MNDX,
			    .next = nullptr,
			    .xdevList = list,
			    .id = info.id,
			    .offset = identity_pose,
			};
			result = xdev_space->xrCreateXDevSpaceMNDX(session, &space_create_info, &element->space);
			if (!xr_check(instance, result, "Failed to create xdev space")) {
				cleanup_update_xdev_spaces(instance, session, xdev_space, &list, &xdevs, &old_list);
				return false;
			}
		}
		element->next = xdev_space->xdev_space_list;
		xdev_space->xdev_space_list = element;
	}

	xdev_space->last_generation_id = generation;

	cleanup_update_xdev_spaces(instance, session, xdev_space, &list, &xdevs, &old_list);

	return true;
}


static init_ext_struct ext_init_funcs[] = {
    &init_opengl_t,           //
    &init_hand_tracking_t,    //
    &init_depth_t,            //
    &init_refresh_rate_t,     //
    &init_plane_detection_t,  //
    &init_hand_interaction_t, //
    &init_user_presence_t,    //
    &init_xdev_space_t,       //
    &init_egl_t,              //
};

struct OpenXRState
{
	struct base_extension_t* ext[ARRAY_SIZE(ext_init_funcs)];

	XrFormFactor form_factor;
	XrViewConfigurationType view_type;
	XrReferenceSpaceType play_space_type;

	XrInstance instance;
	XrSession session;
	XrSystemId system_id;
	XrSessionState state;

	XrEnvironmentBlendMode blend_mode;
	bool blend_mode_explicitly_set;

	XrSpace play_space;

	// Each physical Display/Eye is described by a view
	uint32_t view_count;
	XrViewConfigurationView* viewconfig_views;
	XrCompositionLayerProjectionView* projection_views;
	XrView* views;

	XrViewState view_state;
	XrFrameState frameState;
};

struct action_t
{
	XrAction action;
	XrActionType action_type;
	union {
		XrActionStateFloat float_;
		XrActionStateBoolean boolean_;
		XrActionStatePose pose_;
		XrActionStateVector2f vec2f_;
	} states[HAND_COUNT];
	XrSpace pose_spaces[HAND_COUNT];
	XrSpaceLocation pose_locations[HAND_COUNT];
	XrSpaceVelocity pose_velocities[HAND_COUNT];
};

struct swapchain_t
{
	uint32_t* swapchain_lengths;
	XrSwapchainImageOpenGLKHR** images;
	XrSwapchain* swapchains;
	uint32_t swapchain_count;
};

enum Swapchain
{
	SWAPCHAIN_PROJECTION = 0,
	SWAPCHAIN_DEPTH,
	SWAPCHAIN_LAST
};

struct quad_layer_t
{
	// quad layers are placed into world space, no need to render them per eye
	struct swapchain_t swapchain;
	uint32_t pixel_width, pixel_height;
};

struct ApplicationState
{
	struct OpenXRState oxr;

	struct swapchain_t vr_swapchains[SWAPCHAIN_LAST];

	struct quad_layer_t quad_layer;

	bool is_steamvr;

	struct
	{
		// use optional XrSpaceVelocity in xrLocateHandJointsEXT and visualize linear velocity
		bool query_joint_velocities;

		// use optional XrSpaceVelocity in xrLocateSpace for controllers and visualize linear velocity
		bool query_hand_velocities;

		bool render_floor;

		bool pose_test;
		uint64_t busy_loops;

		bool xdev_space;
	} args;

	// array of view_count indices
	uint32_t* acquired_color;
	uint32_t* acquired_depth;

	struct
	{
		bool enabled;
		// cube moves in `velocity` direction until it hits `center_pos +- `bouncing_lengths, then
		// reverse `velocity
		XrVector3f center_pos;       // m
		XrVector3f current_pos;      // m
		XrTime pos_ts;               // ns
		XrVector3f velocity;         // m/s
		XrVector3f bouncing_lengths; // m
	} cube;

	// Grabbing objects is not actually implemented in this demo, it only gives some  haptic feebdack.
	struct action_t grab_action;

	// A 1D action that is fed by one axis of a 2D input (y axis of thumbstick).
	struct action_t accelerate_action;

	struct action_t hand_pose_action;
	struct action_t aim_action;

	struct action_t haptic_action;

	XrSpace ref_local_space;
	XrSpace ref_local_space_y1;

	XrSpace ref_stage_space;
	XrSpace ref_stage_space_y1;

	XrSpace ref_view_space;
	XrSpace ref_view_space_z1;

	struct gl_renderer_t gl_renderer;
};

// Convenience function
struct base_extension_t*
get_ext(struct ApplicationState* app, char* ext_name_string)
{
	for (uint32_t i = 0; i < ARRAY_SIZE(ext_init_funcs); i++) {
		// even using the the EXTENSION_NAME defines doesn't guarantee same string address
		if (app->oxr.ext[i]->ext_name_string == ext_name_string ||
		    strcmp(app->oxr.ext[i]->ext_name_string, ext_name_string) == 0) {
			return app->oxr.ext[i];
		}
	}
	return NULL;
}

static bool
_check_extension_support(struct base_extension_t* e,
                         XrExtensionProperties* extension_props,
                         uint32_t ext_count)
{
	for (uint32_t i = 0; i < ext_count; i++) {
		if (strcmp(e->ext_name_string, extension_props[i].extensionName) == 0) {
			e->supported = true;
			e->version = extension_props[i].extensionVersion;
			return true;
		}
	}
	return false;
}

static bool
alloc_extensions(struct ApplicationState* app)
{
	for (uint32_t i = 0; i < ARRAY_SIZE(ext_init_funcs); i++) {
		if (!ext_init_funcs[i](&app->oxr.ext[i])) {
			return false;
		}
	}
	return true;
}

static XrResult
check_extensions(struct ApplicationState* app)
{
	XrResult result;

	uint32_t ext_count = 0;
	result = xrEnumerateInstanceExtensionProperties(NULL, 0, &ext_count, NULL);

	/* TODO: instance null will not be able to convert XrResult to string */
	if (!xr_check(NULL, result, "Failed to enumerate number of extension properties"))
		return result;


	auto* ext_props =
	    static_cast<XrExtensionProperties*>(malloc(sizeof(XrExtensionProperties) * ext_count));
	for (uint16_t i = 0; i < ext_count; i++) {
		// we usually have to fill in the type (for validation) and set
		// next to NULL (or a pointer to an extension specific struct)
		ext_props[i].type = XR_TYPE_EXTENSION_PROPERTIES;
		ext_props[i].next = NULL;
	}

	result = xrEnumerateInstanceExtensionProperties(NULL, ext_count, &ext_count, ext_props);
	if (!xr_check(NULL, result, "Failed to enumerate extension properties")) {
		free(ext_props);
		return result;
	}

	printf("Runtime supports %d extensions\n", ext_count);
	for (uint32_t i = 0; i < ext_count; i++) {
		printf("\t%s v%d\n", ext_props[i].extensionName, ext_props[i].extensionVersion);
	}

	for (uint32_t i = 0; i < ARRAY_SIZE(ext_init_funcs); i++) {
		app->oxr.ext[i]->supported = _check_extension_support(app->oxr.ext[i], ext_props, ext_count);

		if (app->oxr.ext[i]->init_fp == init_opengl_fp && !app->oxr.ext[i]->supported) {
			printf("%s is required\n", app->oxr.ext[i]->ext_name_string);
			free(ext_props);
			return XR_ERROR_EXTENSION_NOT_PRESENT;
		}
	}

	free(ext_props);
	return XR_SUCCESS;
}

static XrResult
_init_extensions(struct ApplicationState* app)
{
	XrResult result;
	XrInstance instance = app->oxr.instance;

	for (uint32_t i = 0; i < ARRAY_SIZE(ext_init_funcs); i++) {
		if (app->oxr.ext[i]->supported) {
			printf("Loading function pointers for extension %s\n", app->oxr.ext[i]->ext_name_string);
			if (app->oxr.ext[i]->init_fp != NULL) { // some extensions need no init
				result = app->oxr.ext[i]->init_fp(instance, app->oxr.ext[i]);

				if (!xr_check(instance, result, "Failed to load function pointers for ext %s\n",
				              app->oxr.ext[i]->ext_name_string)) {
					return result;
				}
			}
		} else {
			printf("Not loading function pointers for unsupported extension %s\n",
			       app->oxr.ext[i]->ext_name_string);
		}
	}
	return XR_SUCCESS;
}

// --- Create swapchain
static bool
_create_swapchain(XrInstance instance,
                  XrSession session,
                  struct swapchain_t* swapchain,
                  int num_swapchain,
                  int64_t format,
                  uint32_t sample_count,
                  uint32_t w,
                  uint32_t h,
                  XrSwapchainUsageFlags usage_flags)
{
	XrSwapchainCreateInfo swapchain_create_info = {
	    .type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
	    .next = nullptr,
	    .createFlags = 0,
	    .usageFlags = usage_flags,
	    .format = format,
	    .sampleCount = sample_count,
	    .width = w,
	    .height = h,
	    .faceCount = 1,
	    .arraySize = 1,
	    .mipCount = 1,
	};

	XrResult result;

	result =
	    xrCreateSwapchain(session, &swapchain_create_info, &swapchain->swapchains[num_swapchain]);
	if (!xr_check(instance, result, "Failed to create swapchain!"))
		return false;

	// The runtime controls how many textures we have to be able to render to
	// (e.g. "triple buffering")
	result = xrEnumerateSwapchainImages(swapchain->swapchains[num_swapchain], 0,
	                                    &swapchain->swapchain_lengths[num_swapchain], NULL);
	if (!xr_check(instance, result, "Failed to enumerate swapchains"))
		return false;

	swapchain->images[num_swapchain] = static_cast<XrSwapchainImageOpenGLKHR*>(
	    malloc(sizeof(XrSwapchainImageOpenGLKHR) * swapchain->swapchain_lengths[num_swapchain]));
	for (uint32_t j = 0; j < swapchain->swapchain_lengths[num_swapchain]; j++) {
		swapchain->images[num_swapchain][j].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
		swapchain->images[num_swapchain][j].next = NULL;
	}
	result = xrEnumerateSwapchainImages(
	    swapchain->swapchains[num_swapchain], swapchain->swapchain_lengths[num_swapchain],
	    &swapchain->swapchain_lengths[num_swapchain],
	    (XrSwapchainImageBaseHeader*)swapchain->images[num_swapchain]);
	if (!xr_check(instance, result, "Failed to enumerate swapchain images"))
		return false;

	return true;
}

static bool
create_one_swapchain(XrInstance instance,
                     XrSession session,
                     struct swapchain_t* swapchain,
                     int64_t format,
                     uint32_t sample_count,
                     uint32_t w,
                     uint32_t h,
                     XrSwapchainUsageFlags usage_flags)
{
	swapchain->swapchains = static_cast<XrSwapchain*>(malloc(sizeof(XrSwapchain)));
	swapchain->swapchain_lengths = static_cast<uint32_t*>(malloc(sizeof(uint32_t)));
	swapchain->images =
	    static_cast<XrSwapchainImageOpenGLKHR**>(malloc(sizeof(XrSwapchainImageOpenGLKHR*)));
	swapchain->swapchain_count = 1;

	return _create_swapchain(instance, session, swapchain, 0, format, sample_count, w, h,
	                         usage_flags);
}

static bool
create_swapchain_from_views(XrInstance instance,
                            XrSession session,
                            struct swapchain_t* swapchain,
                            uint32_t view_count,
                            int64_t format,
                            XrViewConfigurationView* viewconfig_views,
                            XrSwapchainUsageFlags usage_flags)
{
	swapchain->swapchains = static_cast<XrSwapchain*>(malloc(sizeof(XrSwapchain) * view_count));
	swapchain->swapchain_lengths = static_cast<uint32_t*>(malloc(sizeof(uint32_t) * view_count));
	swapchain->images = static_cast<XrSwapchainImageOpenGLKHR**>(
	    malloc(sizeof(XrSwapchainImageOpenGLKHR*) * view_count));
	swapchain->swapchain_count = view_count;

	for (uint32_t i = 0; i < view_count; i++) {
		uint32_t sample_count = viewconfig_views[i].recommendedSwapchainSampleCount;
		uint32_t w = viewconfig_views[i].recommendedImageRectWidth;
		uint32_t h = viewconfig_views[i].recommendedImageRectHeight;

		if (!_create_swapchain(instance, session, swapchain, i, format, sample_count, w, h,
		                       usage_flags))
			return false;
	}

	return true;
}

static bool
acquire_swapchain(XrInstance instance,
                  struct swapchain_t* swapchain,
                  int num_swapchain,
                  uint32_t* index)
{
	XrResult result;
	XrSwapchainImageAcquireInfo acquire_info = {.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO,
	                                            .next = NULL};
	result = xrAcquireSwapchainImage(swapchain->swapchains[num_swapchain], &acquire_info, index);
	if (!xr_check(instance, result, "failed to acquire swapchain image!"))
		return false;

	float timeout_ms = 100.0;
	XrSwapchainImageWaitInfo wait_info = {.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO,
	                                      .next = NULL,
	                                      .timeout =
	                                          static_cast<XrDuration>(timeout_ms * 1000.0 * 1000.0)};
	while ((result = xrWaitSwapchainImage(swapchain->swapchains[num_swapchain], &wait_info)) ==
	       XR_TIMEOUT_EXPIRED) {
		printf("xrWaitSwapchainImage timed out after %f ms\n", timeout_ms);
	}
	if (!xr_check(instance, result, "failed to wait for swapchain image!"))
		return false;

	return true;
}

static void
destroy_swapchain(struct swapchain_t* swapchain)
{
	free(swapchain->swapchains);
	free(swapchain->images);
	free(swapchain->swapchain_lengths);
}


bool
create_action(XrInstance instance,
              XrActionType type,
              char* name,
              char* localized_name,
              XrActionSet set,
              int subaction_count,
              XrPath* subactions,
              XrAction* out_action)
{
	XrActionCreateInfo actionInfo = {
	    .type = XR_TYPE_ACTION_CREATE_INFO,
	    .next = nullptr,
	    .actionName = "",
	    .actionType = type,
	    .countSubactionPaths = static_cast<uint32_t>(subaction_count),
	    .subactionPaths = subactions,
	    .localizedActionName = "",
	};
	strcpy(actionInfo.actionName, name);
	strcpy(actionInfo.localizedActionName, localized_name);

	XrResult result = xrCreateAction(set, &actionInfo, out_action);
	if (!xr_check(instance, result, "Failed to create action %s", name))
		return false;

	return true;
}

struct Binding
{
	XrAction action;
	char* paths[HAND_COUNT];
	int path_count;
};

bool
suggest_actions(XrInstance instance, char* profile, struct Binding* b, int binding_count)
{
	XrPath interactionProfilePath;
	XrResult result = xrStringToPath(instance, profile, &interactionProfilePath);
	if (!xr_check(instance, result, "Failed to get interaction profile path %s", profile))
		return false;

	int total = 0;
	for (int i = 0; i < binding_count; i++) {
		struct Binding* binding = &b[i];
		total += binding->path_count;
	}

	auto* bindings =
	    static_cast<XrActionSuggestedBinding*>(malloc(sizeof(XrActionSuggestedBinding) * total));

	printf("Suggesting %d actions for %s\n", binding_count, profile);

	int processed_bindings = 0;
	for (int i = 0; i < binding_count; i++) {
		struct Binding* binding = &b[i];

		for (int j = 0; j < binding->path_count; j++) {
			XrPath path;
			result = xrStringToPath(instance, binding->paths[j], &path);
			if (!xr_check(instance, result, "Failed to get binding path %s", binding->paths[j]))
				return false;

			int current = processed_bindings++;
			bindings[current].action = binding->action;
			bindings[current].binding = path;

			printf("%p (%d): %s\n", (void*)binding->action, j, binding->paths[j]);
		}
	};

	const XrInteractionProfileSuggestedBinding suggestedBindings = {
	    .type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING,
	    .next = nullptr,
	    .interactionProfile = interactionProfilePath,
	    .countSuggestedBindings = static_cast<uint32_t>(total),
	    .suggestedBindings = bindings};

	result = xrSuggestInteractionProfileBindings(instance, &suggestedBindings);
	if (!xr_check(instance, result, "Failed to suggest actions"))
		return false;

	free(bindings);

	return true;
}

bool
create_action_space(XrInstance instance,
                    XrSession session,
                    struct action_t* action,
                    XrPath* hand_paths)
{
	// poses can't be queried directly, we need to create a space for each
	for (int hand = 0; hand < HAND_COUNT; hand++) {
		XrActionSpaceCreateInfo action_space_info = {.type = XR_TYPE_ACTION_SPACE_CREATE_INFO,
		                                             .next = nullptr,
		                                             .action = action->action,
		                                             .subactionPath = hand_paths[hand],
		                                             .poseInActionSpace = identity_pose};

		XrResult result;
		result = xrCreateActionSpace(session, &action_space_info, &action->pose_spaces[hand]);
		if (!xr_check(instance, result, "failed to create hand %d pose space", hand))
			return false;
	}
	return true;
}

bool
get_action_data(XrInstance instance,
                XrSession session,
                struct action_t* action,
                int hand,
                XrPath* subaction_paths,
                XrSpace space,
                XrTime time,
                bool velocities)
{
	XrActionStateGetInfo info = {
	    .type = XR_TYPE_ACTION_STATE_GET_INFO,
	    .next = NULL,
	    .action = action->action,
	    .subactionPath = subaction_paths[hand],
	};
	XrResult result = XR_ERROR_VALIDATION_FAILURE;
	if (action->action_type == XR_ACTION_TYPE_FLOAT_INPUT) {
		action->states[hand].float_.type = XR_TYPE_ACTION_STATE_FLOAT;
		action->states[hand].float_.next = NULL;
		result = xrGetActionStateFloat(session, &info, &action->states[hand].float_);
		if (!xr_check(instance, result, "Failed to get float"))
			return false;
	}
	if (action->action_type == XR_ACTION_TYPE_BOOLEAN_INPUT) {
		action->states[hand].boolean_.type = XR_TYPE_ACTION_STATE_BOOLEAN;
		action->states[hand].boolean_.next = NULL;
		result = xrGetActionStateBoolean(session, &info, &action->states[hand].boolean_);
		if (!xr_check(instance, result, "Failed to get bool"))
			return false;
	}
	if (action->action_type == XR_ACTION_TYPE_VECTOR2F_INPUT) {
		action->states[hand].vec2f_.type = XR_TYPE_ACTION_STATE_VECTOR2F;
		action->states[hand].vec2f_.next = NULL;
		result = xrGetActionStateVector2f(session, &info, &action->states[hand].vec2f_);
		if (!xr_check(instance, result, "Failed to get vec2f"))
			return false;
	}
	if (action->action_type == XR_ACTION_TYPE_POSE_INPUT) {
		action->states[hand].pose_.type = XR_TYPE_ACTION_STATE_POSE;
		action->states[hand].pose_.next = NULL;
		result = xrGetActionStatePose(session, &info, &action->states[hand].pose_);
		if (!xr_check(instance, result, "Failed to get action state pose"))
			return false;

		if (action->states[hand].pose_.isActive) {
			action->pose_locations[hand].type = XR_TYPE_SPACE_LOCATION;
			action->pose_locations[hand].next = NULL;

			if (velocities) {
				action->pose_velocities[hand].type = XR_TYPE_SPACE_VELOCITY;
				action->pose_velocities[hand].next = NULL;
				action->pose_locations[hand].next = &action->pose_velocities[hand];
			} else {
				action->pose_locations[hand].next = NULL;
			}

			result = xrLocateSpace(action->pose_spaces[hand], space, time, &action->pose_locations[hand]);
			if (!xr_check(instance, result, "Failed to locate hand space"))
				return false;
		}
	}

	if (!xr_check(instance, result, "Failed to get action state")) {
		return false;
	}

	return true;
}

static bool
create_hand_trackers(XrInstance instance, XrSession session, struct hand_tracking_t* hand_tracking)
{
	XrResult result;

	result = xrGetInstanceProcAddr(instance, "xrLocateHandJointsEXT",
	                               (PFN_xrVoidFunction*)&hand_tracking->xrLocateHandJointsEXT);

	XrHandEXT hands[HAND_COUNT] = {XR_HAND_LEFT_EXT, XR_HAND_RIGHT_EXT};

	for (int i = 0; i < HAND_COUNT; i++) {
		XrHandTrackerCreateInfoEXT hand_tracker_create_info = {
		    .type = XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT,
		    .next = NULL,
		    .hand = hands[i],
		    .handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT};
		result = hand_tracking->xrCreateHandTrackerEXT(session, &hand_tracker_create_info,
		                                               &hand_tracking->trackers[i]);
		if (!xr_check(instance, result, "Failed to create hand tracker %d", i)) {
			return false;
		}

		hand_tracking->joint_locations[i] = XrHandJointLocationsEXT{
		    .type = XR_TYPE_HAND_JOINT_LOCATIONS_EXT,
		    .next = nullptr,
		    .isActive = false,
		    .jointCount = XR_HAND_JOINT_COUNT_EXT,
		    .jointLocations = hand_tracking->joints[i],
		};

		printf("Created hand tracker %d\n", i);
	}
	return true;
}

static bool
get_hand_tracking(XrInstance instance,
                  XrSpace space,
                  XrTime time,
                  bool query_joint_velocities,
                  struct hand_tracking_t* hand_tracking,
                  int hand)
{
	if (query_joint_velocities) {
		hand_tracking->joint_velocities[hand].type = XR_TYPE_HAND_JOINT_VELOCITIES_EXT;
		hand_tracking->joint_velocities[hand].next = NULL;
		hand_tracking->joint_velocities[hand].jointCount = XR_HAND_JOINT_COUNT_EXT;
		hand_tracking->joint_velocities[hand].jointVelocities =
		    hand_tracking->joint_velocities_arr[hand];
		hand_tracking->joint_locations[hand].next = &hand_tracking->joint_velocities[hand];
	} else {
		hand_tracking->joint_locations[hand].next = NULL;
	}

	XrHandJointsLocateInfoEXT locateInfo = {
	    .type = XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT, .next = NULL, .baseSpace = space, .time = time};

	XrResult result;
	result = hand_tracking->xrLocateHandJointsEXT(hand_tracking->trackers[hand], &locateInfo,
	                                              &hand_tracking->joint_locations[hand]);
	if (!xr_check(instance, result, "failed to locate hand joints!"))
		return false;

	return true;
}

static struct option long_options[] = {{"help", no_argument, 0, 'h'},
                                       {"velocities", no_argument, 0, 'v'},
                                       {"jointvelocities", no_argument, 0, 'j'},
                                       {"formfactor", required_argument, 0, 'f'},
                                       {"blendmode", required_argument, 0, 'b'},
                                       {"space", required_argument, 0, 's'},
                                       {"movingcube", required_argument, 0, 'c'},
                                       {"floor", no_argument, 0, 'z'},
                                       {"posetest", no_argument, 0, 't'},
                                       {"busyloops", required_argument, 0, 'l'},
                                       {"xdev_space", no_argument, 0, 'x'},
                                       {0, 0, 0, 0}};
void
parse_opts(int argc, char** argv, struct ApplicationState* app)
{
	while (1) {
		int c;
		int option_index = 0;
		c = getopt_long(argc, argv, "tjhxf:b:s:c:l:p", long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 0:
			/* If this option set a flag, do nothing else now. */
			if (long_options[option_index].flag != 0)
				break;
			printf("option %s", long_options[option_index].name);
			if (optarg)
				printf(" with arg %s", optarg);
			printf("\n");
			break;

		case 'h':
		case '?':
			printf("%s:\n", argv[0]);
			printf("\t-v|--velocities             (visualizes linear velocity of controller)\n");
			printf("\t-j|--jointvelocities        (visualizes linear velocity of hand joints)\n");
			printf("\t-f|--formfactor <XrFormFactor>\n");
			XrPrintEnum_XrFormFactor();
			printf("\t-b|--blendmode <XrEnvironmentBlendMode>\n");
			XrPrintEnum_XrEnvironmentBlendMode();
			printf("\t-s|--space <XrReferenceSpaceType>\n");
			XrPrintEnum_XrReferenceSpaceType();
			printf("\t-c|--movingcube <direction> (adds a cube with a movement pattern)\n");
			printf("\t\thorizontal\n");
			printf("\t\tdiagonal\n");
			printf("\t\tvertical\n");
			printf("\t-z|--floor                  (renders a 1m x 1m square at floor level)\n");
			printf(
			    "\t-t|--posetest               (renders only a headlocked cube in world space for pose "
			    "testing)\n");
			printf(
			    "\t-l|--busyloops <iterations> (Loop iterations to keep the GPU busy. Only useful for "
			    "testing.)\n");
			printf(
			    "\t-x|--xdev_space.               (Renders yellow cubes at each supported xdev via "
			    "XR_MNDX_xdev_space)\n");
			exit(0);

		case 'b':
			app->oxr.blend_mode = XrEnum_XrEnvironmentBlendMode(optarg);
			app->oxr.blend_mode_explicitly_set = true;
			printf("ARG: Blend Mode %s -> %d\n", optarg, app->oxr.blend_mode);
			break;

		case 'f':
			app->oxr.form_factor = XrEnum_XrFormFactor(optarg);
			printf("ARG: Form Factor %s -> %d\n", optarg, app->oxr.form_factor);
			break;

		case 's':
			app->oxr.play_space_type = XrEnum_XrReferenceSpaceType(optarg);
			printf("ARG: Reference Space %s -> %d\n", optarg, app->oxr.play_space_type);
			break;

		case 'c': {
			app->cube.enabled = true;
			app->cube.center_pos = XrVector3f{.x = 0, .y = 0, .z = -1};
			app->cube.current_pos = app->cube.center_pos;
			app->cube.bouncing_lengths = XrVector3f{.x = 0.75f, .y = 0.75f, .z = 0.75f};

			float velocity = 0.5;
			if (strcmp(optarg, "horizontal") == 0) {
				app->cube.velocity = XrVector3f{.x = velocity, .y = 0, .z = 0};
			} else if (strcmp(optarg, "diagonal") == 0) {
				app->cube.velocity = XrVector3f{.x = static_cast<float>(velocity * sqrt(2)),
				                                .y = static_cast<float>(velocity * sqrt(2)),
				                                .z = 0};
			} else if (strcmp(optarg, "vertical") == 0) {
				app->cube.velocity = XrVector3f{.x = 0, .y = velocity, .z = 0};
			}
			printf("ARG: Enable moving cube %s -> %f, %f, %f\n", optarg, app->cube.velocity.x,
			       app->cube.velocity.y, app->cube.velocity.z);
			break;
		}

		case 'j':
			printf("ARG: Enabling joint velocities\n");
			app->args.query_joint_velocities = true;
			break;

		case 'v':
			printf("ARG: Enabling hand velocities\n");
			app->args.query_hand_velocities = true;
			break;

		case 'z':
			printf("ARG: Enabling floor\n");
			app->args.render_floor = true;
			break;
		case 't':
			printf("ARG: Enabling pose test\n");
			app->args.pose_test = true;
			break;

		case 'l':
			app->args.busy_loops = atoi(optarg);
			printf("ARG: Enabling busy loop test with %lu iterations\n", app->args.busy_loops);
			break;

		case 'x':
			app->args.xdev_space = true;
			printf("ARG: Enabling xdev_space\n");
			break;

		default: abort();
		}
	}
}

int
main(int argc, char** argv)
{
	auto* app = static_cast<ApplicationState*>(malloc(sizeof(ApplicationState)));
	*app = ApplicationState{
	    .oxr = {.form_factor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY,
	            .view_type = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
	            .play_space_type = XR_REFERENCE_SPACE_TYPE_STAGE,

	            .instance = XR_NULL_HANDLE,
	            .session = XR_NULL_HANDLE,
	            .system_id = XR_NULL_SYSTEM_ID,
	            .state = XR_SESSION_STATE_UNKNOWN,
	            .play_space = XR_NULL_HANDLE,

	            .view_count = 0,
	            .viewconfig_views = nullptr,
	            .projection_views = nullptr,
	            .views = nullptr},
	    .quad_layer = {.pixel_width = 320, .pixel_height = 240},
	    .is_steamvr = false,
	    .args =
	        {
	            .query_joint_velocities = false,
	            .query_hand_velocities = false,
	            .render_floor = false,
	            .pose_test = false,
	            .busy_loops = 0,
	            .xdev_space = false,
	        },
	    .acquired_color = nullptr,
	    .acquired_depth = nullptr,
	    .cube =
	        {
	            .enabled = false,
	            .center_pos = {0, 0, 0},
	            .current_pos = {0, 0, 0},
	            .pos_ts = 0,
	            .velocity = {0, 0, 0},
	            .bouncing_lengths = {0, 0, 0},
	        },
	    .gl_renderer =
	        {
	            .near_z = 0.01f,
	            .far_z = 100.0f,
	        },
	};

	parse_opts(argc, argv, app);

	void* gl_graphics_binding = NULL;

	// each graphics API requires the use of a specialized struct.
#ifdef _WIN32
	XrGraphicsBindingOpenGLWin32KHR wgl_graphics_binding = {
	    .type = XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR,
	    .next = nullptr,
	};
	gl_graphics_binding = &wgl_graphics_binding;
#else
	XrGraphicsBindingOpenGLXlibKHR glx_graphics_binding = {
	    .type = XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR,
	    .next = nullptr,
	};

	XrGraphicsBindingEGLMNDX egl_graphics_binding = {
	    .type = XR_TYPE_GRAPHICS_BINDING_EGL_MNDX,
	    .next = nullptr,
	};

	// default may be overwritten later once we detect we are not on glx
	gl_graphics_binding = &glx_graphics_binding;
#endif

	XrPath hand_paths[HAND_COUNT];

	XrResult result = XR_SUCCESS;

	if (!alloc_extensions(app)) {
		printf("allocating ext structs failed");
		return 1;
	}

	result = check_extensions(app);
	if (!xr_check(app->oxr.instance, result, "Extensions check failed!")) {
		return 1;
	}

	// disable extensions selectively
	get_ext(app, XR_EXT_PLANE_DETECTION_EXTENSION_NAME)->supported = false;

	// --- Create XrInstance
	int enabled_ext_count = 0;
	const char* enabled_exts[ARRAY_SIZE(ext_init_funcs)] = {0};

	for (uint32_t i = 0; i < ARRAY_SIZE(ext_init_funcs); i++) {
		if (app->oxr.ext[i]->supported) {
			enabled_exts[enabled_ext_count++] = app->oxr.ext[i]->ext_name_string;
			printf("enabling extension %s\n", app->oxr.ext[i]->ext_name_string);
		}
	}

	// same can be done for API layers, but API layers can also be enabled by env var

	XrInstanceCreateInfo instance_create_info = {
	    .type = XR_TYPE_INSTANCE_CREATE_INFO,
	    .next = nullptr,
	    .createFlags = 0,
	    .applicationInfo =
	        {
	            .applicationName = "",
	            .applicationVersion = 1,
	            .engineName = "",
	            .engineVersion = 0,
	            .apiVersion = XR_MAKE_VERSION(1, 0, XR_VERSION_PATCH(XR_CURRENT_API_VERSION)),
	        },
	    .enabledApiLayerCount = 0,
	    .enabledApiLayerNames = nullptr,
	    .enabledExtensionCount = enabled_ext_count,
	    .enabledExtensionNames = enabled_exts,
	};
	strncpy(instance_create_info.applicationInfo.applicationName, "OpenXR OpenGL Example",
	        XR_MAX_APPLICATION_NAME_SIZE);
	strncpy(instance_create_info.applicationInfo.engineName, "Custom", XR_MAX_ENGINE_NAME_SIZE);

	result = xrCreateInstance(&instance_create_info, &app->oxr.instance);
	if (!xr_check(NULL, result, "Failed to create XR instance."))
		return 1;

	result = _init_extensions(app);
	if (!xr_check(app->oxr.instance, result, "Failed to init extensions!")) {
		return 1;
	}


	// Optionally get runtime name and version
	XrInstanceProperties instance_props = {
	    .type = XR_TYPE_INSTANCE_PROPERTIES,
	    .next = NULL,
	};

	result = xrGetInstanceProperties(app->oxr.instance, &instance_props);
	if (!xr_check(NULL, result, "Failed to get instance info"))
		return 1;

	printf("Runtime Name: %s\n", instance_props.runtimeName);
	printf("Runtime Version: %d.%d.%d\n", XR_VERSION_MAJOR(instance_props.runtimeVersion),
	       XR_VERSION_MINOR(instance_props.runtimeVersion),
	       XR_VERSION_PATCH(instance_props.runtimeVersion));

	if (strcmp(instance_props.runtimeName, "SteamVR/OpenXR") == 0) {
		app->is_steamvr = true;
	}
	printf("Runtime is SteamVR: %d\n", app->is_steamvr);


	// --- Create XrSystem
	XrSystemGetInfo system_get_info = {
	    .type = XR_TYPE_SYSTEM_GET_INFO,
	    .next = nullptr,
	    .formFactor = app->oxr.form_factor,
	};

	result = xrGetSystem(app->oxr.instance, &system_get_info, &app->oxr.system_id);
	if (!xr_check(app->oxr.instance, result, "Failed to get system for HMD form factor."))
		return 1;

	printf("Successfully got XrSystem with id %lu for HMD form factor\n", app->oxr.system_id);


	// checking system properties is generally  optional, but we are interested in hand tracking
	// support
	{
		XrSystemProperties system_props = {
		    .type = XR_TYPE_SYSTEM_PROPERTIES,
		    .next = NULL,
		    .graphicsProperties = {0},
		    .trackingProperties = {0},
		};
		struct hand_tracking_t* ht_ext =
		    (struct hand_tracking_t*)get_ext(app, XR_EXT_HAND_TRACKING_EXTENSION_NAME);

		XrSystemHandTrackingPropertiesEXT ht = {
		    .type = XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT,
		    .next = system_props.next,
		};
		if (ht_ext->base.supported) {
			system_props.next = &ht;
		}


		struct xdev_space_t* xdev_ext =
		    (struct xdev_space_t*)get_ext(app, XR_MNDX_XDEV_SPACE_EXTENSION_NAME);

		XrSystemXDevSpacePropertiesMNDX xd = {
		    .type = XR_TYPE_SYSTEM_XDEV_SPACE_PROPERTIES_MNDX,
		    .next = system_props.next,
		};

		if (app->args.xdev_space && xdev_ext && xdev_ext->base.supported) {
			system_props.next = &xd;
		}

		result = xrGetSystemProperties(app->oxr.instance, app->oxr.system_id, &system_props);
		if (!xr_check(app->oxr.instance, result, "Failed to get System properties"))
			return 1;

		if (ht_ext->base.supported) {
			ht_ext->system_supported = ht.supportsHandTracking;
		}

		if (app->args.xdev_space && xdev_ext && xdev_ext->base.supported) {
			xdev_ext->system_supported = xd.supportsXDevSpace;
		}

		print_system_properties(&system_props);
	}

	print_supported_view_configs(app->oxr.instance, app->oxr.system_id);

	// view_count usually depends on the form_factor / view_type.
	// dynamically allocating all view related structs hopefully allows this app to scale easily to
	// different view_counts.

	result = xrEnumerateViewConfigurationViews(app->oxr.instance, app->oxr.system_id,
	                                           app->oxr.view_type, 0, &app->oxr.view_count, NULL);
	if (!xr_check(app->oxr.instance, result, "Failed to get view configuration view count!"))
		return 1;

	app->oxr.viewconfig_views = static_cast<XrViewConfigurationView*>(
	    malloc(sizeof(XrViewConfigurationView) * app->oxr.view_count));
	for (uint32_t i = 0; i < app->oxr.view_count; i++) {
		app->oxr.viewconfig_views[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
		app->oxr.viewconfig_views[i].next = NULL;
	}

	result = xrEnumerateViewConfigurationViews(app->oxr.instance, app->oxr.system_id,
	                                           app->oxr.view_type, app->oxr.view_count,
	                                           &app->oxr.view_count, app->oxr.viewconfig_views);
	if (!xr_check(app->oxr.instance, result, "Failed to enumerate view configuration views!"))
		return 1;
	print_viewconfig_view_info(app->oxr.view_count, app->oxr.viewconfig_views);


	// OpenXR requires checking graphics requirements before creating a session.
	XrGraphicsRequirementsOpenGLKHR opengl_reqs = {.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR,
	                                               .next = NULL};

	// this function pointer was loaded with xrGetInstanceProcAddr
	struct opengl_t* opengl_ext = (struct opengl_t*)get_ext(app, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);
	result = opengl_ext->xrGetOpenGLGraphicsRequirementsKHR(app->oxr.instance, app->oxr.system_id,
	                                                        &opengl_reqs);
	if (!xr_check(app->oxr.instance, result, "Failed to get OpenGL graphics requirements!"))
		return 1;

	// On OpenGL we never fail this check because the version requirement is not useful.
	// Other APIs may have more useful requirements.
	check_opengl_version(&opengl_reqs);


	uint32_t blend_mode_count = 0;
	result = xrEnumerateEnvironmentBlendModes(app->oxr.instance, app->oxr.system_id,
	                                          app->oxr.view_type, 0, &blend_mode_count, NULL);
	if (!xr_check(app->oxr.instance, result, "failed to enumerate blend mode count!"))
		return 1;

	XrEnvironmentBlendMode* blend_modes = static_cast<XrEnvironmentBlendMode*>(
	    malloc(sizeof(XrEnvironmentBlendMode) * blend_mode_count));
	result =
	    xrEnumerateEnvironmentBlendModes(app->oxr.instance, app->oxr.system_id, app->oxr.view_type,
	                                     blend_mode_count, &blend_mode_count, blend_modes);
	if (!xr_check(app->oxr.instance, result, "failed to enumerate blend modes!"))
		return 1;

	// the environment blend modes our application can deal with
	XrEnvironmentBlendMode supported_blend_modes[] = {
	    XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND,
	    XR_ENVIRONMENT_BLEND_MODE_ADDITIVE,
	    XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
	};
	uint32_t supported_blend_mode_count = ARRAY_SIZE(supported_blend_modes);

	printf("Runtime supported blend modes:\n");
	for (uint32_t i = 0; i < blend_mode_count; i++) {
		printf("\t%s\n", XrStr_XrEnvironmentBlendMode(blend_modes[i]));
	}

	// use the most preferred blend mode of the runtime that this app supports too
	if (!app->oxr.blend_mode_explicitly_set) {
		bool matched = false;
		for (uint32_t i = 0; i < blend_mode_count; i++) {
			for (uint32_t supported_i = 0; supported_i < supported_blend_mode_count; supported_i++) {
				if (blend_modes[i] == supported_blend_modes[supported_i]) {
					app->oxr.blend_mode = blend_modes[i];
					matched = true;
					break;
				}
			}
			if (matched) {
				break;
			}
		}
	}

	printf("Using blend mode: %s\n", XrStr_XrEnvironmentBlendMode(app->oxr.blend_mode));
	free(blend_modes);


	// --- Create session

	// create SDL window the size of the left eye & fill GL graphics binding info
	if (!init_sdl_window(app->oxr.viewconfig_views[0].recommendedImageRectWidth,
	                     app->oxr.viewconfig_views[0].recommendedImageRectHeight)) {
		printf("GLX init failed!\n");
		return 1;
	}

	// HACK? OpenXR wants us to report these values, so "work around" SDL and get the underlying
	// current gl platform stuff.
#ifdef _WIN32
	// only one config, gl_graphics_binding already set at startup
	wgl_graphics_binding.hDC = wglGetCurrentDC();
	wgl_graphics_binding.hGLRC = wglGetCurrentContext();
#else
	// everywhere else need to check what platform SDL uses
	if (glXGetCurrentContext() != NULL) {
		gl_graphics_binding = &glx_graphics_binding;
		printf("Using GLX with context %p\n", (void*)glXGetCurrentContext());

		glx_graphics_binding.xDisplay = XOpenDisplay(NULL);
		glx_graphics_binding.glxContext = glXGetCurrentContext();
		glx_graphics_binding.glxDrawable = glXGetCurrentDrawable();

		// Get visualid from window SDL created for us.
		XWindowAttributes attr;
		XGetWindowAttributes(glx_graphics_binding.xDisplay, glx_graphics_binding.glxDrawable, &attr);
		glx_graphics_binding.visualid = XVisualIDFromVisual(attr.visual);

		// Get fbconfig from glx context SDL created for us.
		// Ideally you get this from your setup code or your framework.
		int fbconfig_id = 0;
		glXQueryContext(glx_graphics_binding.xDisplay, glx_graphics_binding.glxContext, GLX_FBCONFIG_ID,
		                &fbconfig_id);

		int fbconfig_count = 0;
		GLXFBConfig* fbconfigs =
		    glXGetFBConfigs(glx_graphics_binding.xDisplay, DefaultScreen(glx_graphics_binding.xDisplay),
		                    &fbconfig_count);

		if (fbconfigs) {
			glx_graphics_binding.glxFBConfig = NULL;
			for (int i = 0; i < fbconfig_count; i++) {
				int curr_id;
				glXGetFBConfigAttrib(glx_graphics_binding.xDisplay, fbconfigs[i], GLX_FBCONFIG_ID,
				                     &curr_id);
				if (curr_id == fbconfig_id) {
					glx_graphics_binding.glxFBConfig = fbconfigs[i];
					break;
				}
			}
			XFree(fbconfigs);
		}

		if (!glx_graphics_binding.glxFBConfig) {
			printf("Did not find GLXFBConfig that SDL used. Weird. Trying to continue with placeholder.");
			glx_graphics_binding.glxFBConfig = static_cast<GLXFBConfig>((void*)0x1);
		}
	}
	if (eglGetCurrentContext() != NULL) {
		gl_graphics_binding = &egl_graphics_binding;
		printf("Using EGL with context %p\n", eglGetCurrentContext());

		egl_graphics_binding.context = eglGetCurrentContext();
		egl_graphics_binding.display = eglGetCurrentDisplay();
		egl_graphics_binding.getProcAddress = eglGetProcAddress;
	}
#endif


	printf("Using OpenGL version: %s\n", glGetString(GL_VERSION));
	printf("Using OpenGL Renderer: %s\n", glGetString(GL_RENDERER));

	app->oxr.state = XR_SESSION_STATE_UNKNOWN;

	XrSessionCreateInfo session_create_info = {.type = XR_TYPE_SESSION_CREATE_INFO,
	                                           .next = gl_graphics_binding,
	                                           .systemId = app->oxr.system_id};

	result = xrCreateSession(app->oxr.instance, &session_create_info, &app->oxr.session);
	if (!xr_check(app->oxr.instance, result, "Failed to create session"))
		return 1;

	printf("Successfully created a session with OpenGL!\n");

	// Many runtimes support at least STAGE and LOCAL but not all do.
	// Sophisticated apps might check if the chosen one is supported and try another one if not.
	// Here we will get an error from xrCreateReferenceSpace() and exit.
	print_reference_spaces(app->oxr.instance, app->oxr.session);
	XrReferenceSpaceCreateInfo play_space_create_info = {.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
	                                                     .next = NULL,
	                                                     .referenceSpaceType =
	                                                         app->oxr.play_space_type,
	                                                     .poseInReferenceSpace = identity_pose};

	result = xrCreateReferenceSpace(app->oxr.session, &play_space_create_info, &app->oxr.play_space);
	if (!xr_check(app->oxr.instance, result, "Failed to create play space!"))
		return 1;


	XrPosef y1 = {.orientation = {0, 0, 0, 1}, .position = {0, 1, 0}};

	XrPosef z1 = {.orientation = {0, 0, 0, 1}, .position = {0, 0, -1}};

	{
		XrReferenceSpaceCreateInfo space_create_info = {.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
		                                                .next = NULL,
		                                                .referenceSpaceType =
		                                                    XR_REFERENCE_SPACE_TYPE_LOCAL,
		                                                .poseInReferenceSpace = identity_pose};

		result = xrCreateReferenceSpace(app->oxr.session, &space_create_info, &app->ref_local_space);
		if (!xr_check(app->oxr.instance, result, "Failed to create play space!"))
			return 1;
	}
	{
		XrReferenceSpaceCreateInfo space_create_info = {.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
		                                                .next = NULL,
		                                                .referenceSpaceType =
		                                                    XR_REFERENCE_SPACE_TYPE_LOCAL,
		                                                .poseInReferenceSpace = y1};

		result = xrCreateReferenceSpace(app->oxr.session, &space_create_info, &app->ref_local_space_y1);
		if (!xr_check(app->oxr.instance, result, "Failed to create play space!"))
			return 1;
	}
	{
		XrReferenceSpaceCreateInfo space_create_info = {.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
		                                                .next = NULL,
		                                                .referenceSpaceType =
		                                                    XR_REFERENCE_SPACE_TYPE_STAGE,
		                                                .poseInReferenceSpace = identity_pose};

		result = xrCreateReferenceSpace(app->oxr.session, &space_create_info, &app->ref_stage_space);
		if (!xr_check(app->oxr.instance, result, "Failed to create play space!"))
			return 1;
	}
	{
		XrReferenceSpaceCreateInfo space_create_info = {.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
		                                                .next = NULL,
		                                                .referenceSpaceType =
		                                                    XR_REFERENCE_SPACE_TYPE_STAGE,
		                                                .poseInReferenceSpace = y1};

		result = xrCreateReferenceSpace(app->oxr.session, &space_create_info, &app->ref_stage_space_y1);
		if (!xr_check(app->oxr.instance, result, "Failed to create play space!"))
			return 1;
	}
	{
		XrReferenceSpaceCreateInfo space_create_info = {.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
		                                                .next = NULL,
		                                                .referenceSpaceType =
		                                                    XR_REFERENCE_SPACE_TYPE_VIEW,
		                                                .poseInReferenceSpace = identity_pose};

		result = xrCreateReferenceSpace(app->oxr.session, &space_create_info, &app->ref_view_space);
		if (!xr_check(app->oxr.instance, result, "Failed to create play space!"))
			return 1;
	}
	{
		XrReferenceSpaceCreateInfo space_create_info = {.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
		                                                .next = NULL,
		                                                .referenceSpaceType =
		                                                    XR_REFERENCE_SPACE_TYPE_VIEW,
		                                                .poseInReferenceSpace = z1};

		result = xrCreateReferenceSpace(app->oxr.session, &space_create_info, &app->ref_view_space_z1);
		if (!xr_check(app->oxr.instance, result, "Failed to create play space!"))
			return 1;
	}

	// --- Create Swapchains
	uint32_t swapchain_format_count;
	result = xrEnumerateSwapchainFormats(app->oxr.session, 0, &swapchain_format_count, NULL);
	if (!xr_check(app->oxr.instance, result, "Failed to get number of supported swapchain formats"))
		return 1;

	printf("Runtime supports %d swapchain formats\n", swapchain_format_count);
	int64_t* swapchain_formats =
	    static_cast<int64_t*>(malloc(sizeof(int64_t) * swapchain_format_count));

	result = xrEnumerateSwapchainFormats(app->oxr.session, swapchain_format_count,
	                                     &swapchain_format_count, swapchain_formats);

	free(swapchain_formats);
	if (!xr_check(app->oxr.instance, result, "Failed to enumerate swapchain formats"))
		return 1;

	// SRGB is usually a better choice than linear
	// a more sophisticated approach would iterate supported swapchain formats and choose from them
	int64_t color_format =
	    get_swapchain_format(app->oxr.instance, app->oxr.session, GL_SRGB8_ALPHA8_EXT, true);

	int64_t quad_format =
	    get_swapchain_format(app->oxr.instance, app->oxr.session, GL_RGBA8_EXT, true);

	int64_t depth_format =
	    get_swapchain_format(app->oxr.instance, app->oxr.session, GL_DEPTH_COMPONENT16, true);
	if (depth_format < 0) {
		printf("Preferred depth format GL_DEPTH_COMPONENT16 not supported, disabling depth\n");
		struct depth_t* depth_ext =
		    (struct depth_t*)get_ext(app, XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME);
		if (depth_ext) {
			depth_ext->base.supported = false;
		}
	}

	XrSwapchainUsageFlags color_flags =
	    XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
	if (!create_swapchain_from_views(app->oxr.instance, app->oxr.session,
	                                 &app->vr_swapchains[SWAPCHAIN_PROJECTION], app->oxr.view_count,
	                                 color_format, app->oxr.viewconfig_views, color_flags))
		return 1;

	struct depth_t* depth_ext =
	    (struct depth_t*)get_ext(app, XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME);
	if (depth_ext->base.supported) {
		XrSwapchainUsageFlags depth_flags = XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		if (!create_swapchain_from_views(app->oxr.instance, app->oxr.session,
		                                 &app->vr_swapchains[SWAPCHAIN_DEPTH], app->oxr.view_count,
		                                 depth_format, app->oxr.viewconfig_views, depth_flags)) {
			return 1;
		}
	}

	if (!create_one_swapchain(app->oxr.instance, app->oxr.session, &app->quad_layer.swapchain,
	                          quad_format, 1, app->quad_layer.pixel_width,
	                          app->quad_layer.pixel_height, color_flags))
		return 1;

	// Do not allocate these every frame to save some resources
	app->oxr.views = (XrView*)malloc(sizeof(XrView) * app->oxr.view_count);
	app->oxr.projection_views = (XrCompositionLayerProjectionView*)malloc(
	    sizeof(XrCompositionLayerProjectionView) * app->oxr.view_count);
	for (uint32_t i = 0; i < app->oxr.view_count; i++) {
		app->oxr.views[i].type = XR_TYPE_VIEW;
		app->oxr.views[i].next = NULL;

		app->oxr.projection_views[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
		app->oxr.projection_views[i].next = NULL;

		app->oxr.projection_views[i].subImage.swapchain =
		    app->vr_swapchains[SWAPCHAIN_PROJECTION].swapchains[i];
		app->oxr.projection_views[i].subImage.imageArrayIndex = 0;
		app->oxr.projection_views[i].subImage.imageRect.offset.x = 0;
		app->oxr.projection_views[i].subImage.imageRect.offset.y = 0;
		app->oxr.projection_views[i].subImage.imageRect.extent.width =
		    app->oxr.viewconfig_views[i].recommendedImageRectWidth;
		app->oxr.projection_views[i].subImage.imageRect.extent.height =
		    app->oxr.viewconfig_views[i].recommendedImageRectHeight;

		// projection_views[i].{pose, fov} have to be filled every frame in frame loop
	};


	if (depth_ext->base.supported) {
		depth_ext->infos = (XrCompositionLayerDepthInfoKHR*)malloc(
		    sizeof(XrCompositionLayerDepthInfoKHR) * app->oxr.view_count);
		for (uint32_t i = 0; i < app->oxr.view_count; i++) {
			depth_ext->infos[i].type = XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR;
			depth_ext->infos[i].next = NULL;
			depth_ext->infos[i].minDepth = 0.f;
			depth_ext->infos[i].maxDepth = 1.f;
			depth_ext->infos[i].nearZ = app->gl_renderer.near_z;
			depth_ext->infos[i].farZ = app->gl_renderer.far_z;

			depth_ext->infos[i].subImage.swapchain = app->vr_swapchains[SWAPCHAIN_DEPTH].swapchains[i];

			depth_ext->infos[i].subImage.imageArrayIndex = 0;
			depth_ext->infos[i].subImage.imageRect.offset.x = 0;
			depth_ext->infos[i].subImage.imageRect.offset.y = 0;
			depth_ext->infos[i].subImage.imageRect.extent.width =
			    app->oxr.viewconfig_views[i].recommendedImageRectWidth;
			depth_ext->infos[i].subImage.imageRect.extent.height =
			    app->oxr.viewconfig_views[i].recommendedImageRectHeight;

			app->oxr.projection_views[i].next = &depth_ext->infos[i];
		};
	}



	struct refresh_rate_t* refresh_rate_ext =
	    (struct refresh_rate_t*)get_ext(app, XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
	// get info from fb_refresh_rate
	if (refresh_rate_ext->base.supported) {
		uint32_t refresh_rate_count;
		result = refresh_rate_ext->xrEnumerateDisplayRefreshRatesFB(app->oxr.session, 0,
		                                                            &refresh_rate_count, NULL);
		if (!xr_check(app->oxr.instance, result, "failed to enumerate refresh rate count"))
			return 1;

		if (refresh_rate_count > 0) {
			float* refresh_rates = static_cast<float*>(malloc(sizeof(float) * refresh_rate_count));
			result = refresh_rate_ext->xrEnumerateDisplayRefreshRatesFB(
			    app->oxr.session, refresh_rate_count, &refresh_rate_count, refresh_rates);
			if (!xr_check(app->oxr.instance, result, "failed to enumerate refresh rates")) {
				free(refresh_rates);
				return 1;
			}

			printf("Supported refresh rates:\n");
			for (uint32_t i = 0; i < refresh_rate_count; i++) {
				printf("\t%f Hz\n", refresh_rates[i]);
			}

			// refresh rates are ordered lowest to highest
			printf("Requesting refresh rate %f\n", refresh_rates[refresh_rate_count - 1]);
			result = refresh_rate_ext->xrRequestDisplayRefreshRateFB(
			    app->oxr.session, refresh_rates[refresh_rate_count - 1]);
			if (!xr_check(app->oxr.instance, result, "failed to request refresh rate %f",
			              refresh_rates[refresh_rate_count - 1]))
				return 1;

			free(refresh_rates);
		}

		float refresh_rate = 0;
		result = refresh_rate_ext->xrGetDisplayRefreshRateFB(app->oxr.session, &refresh_rate);
		if (!xr_check(app->oxr.instance, result, "failed to get refresh rate"))
			return 1;

		printf("Current refresh rate: %f Hz\n", refresh_rate);
	}


	// --- Set up input (actions)

	xrStringToPath(app->oxr.instance, "/user/hand/left", &hand_paths[HAND_LEFT_INDEX]);
	xrStringToPath(app->oxr.instance, "/user/hand/right", &hand_paths[HAND_RIGHT_INDEX]);

	XrActionSetCreateInfo gameplay_actionset_info = {
	    .type = XR_TYPE_ACTION_SET_CREATE_INFO, .next = NULL, .priority = 0};
	strcpy(gameplay_actionset_info.actionSetName, "gameplay_actionset");
	strcpy(gameplay_actionset_info.localizedActionSetName, "Gameplay Actions");

	XrActionSet gameplay_actionset;
	result = xrCreateActionSet(app->oxr.instance, &gameplay_actionset_info, &gameplay_actionset);
	if (!xr_check(app->oxr.instance, result, "failed to create actionset"))
		return 1;


	// Grabbing objects is not actually implemented in this demo, it only gives some  haptic feebdack.
	app->grab_action =
	    (struct action_t){.action = XR_NULL_HANDLE, .action_type = XR_ACTION_TYPE_FLOAT_INPUT};
	if (!create_action(app->oxr.instance, XR_ACTION_TYPE_FLOAT_INPUT, "grabobjectfloat",
	                   "Grab Object", gameplay_actionset, HAND_COUNT, hand_paths,
	                   &app->grab_action.action))
		return 1;

	// A 1D action that is fed by one axis of a 2D input (y axis of thumbstick).
	app->accelerate_action =
	    (struct action_t){.action = XR_NULL_HANDLE, .action_type = XR_ACTION_TYPE_FLOAT_INPUT};
	if (!create_action(app->oxr.instance, XR_ACTION_TYPE_FLOAT_INPUT, "accelerate", "Accelerate",
	                   gameplay_actionset, HAND_COUNT, hand_paths, &app->accelerate_action.action))
		return 1;

	app->hand_pose_action =
	    (struct action_t){.action = XR_NULL_HANDLE, .action_type = XR_ACTION_TYPE_POSE_INPUT};
	if (!create_action(app->oxr.instance, XR_ACTION_TYPE_POSE_INPUT, "handpose", "Hand Pose",
	                   gameplay_actionset, HAND_COUNT, hand_paths, &app->hand_pose_action.action))
		return 1;
	if (!create_action_space(app->oxr.instance, app->oxr.session, &app->hand_pose_action, hand_paths))
		return 1;

	app->aim_action =
	    (struct action_t){.action = XR_NULL_HANDLE, .action_type = XR_ACTION_TYPE_POSE_INPUT};
	if (!create_action(app->oxr.instance, XR_ACTION_TYPE_POSE_INPUT, "aim", "Aim Pose",
	                   gameplay_actionset, HAND_COUNT, hand_paths, &app->aim_action.action))
		return 1;
	if (!create_action_space(app->oxr.instance, app->oxr.session, &app->aim_action, hand_paths))
		return 1;

	app->haptic_action =
	    (struct action_t){.action = XR_NULL_HANDLE, .action_type = XR_ACTION_TYPE_VIBRATION_OUTPUT};
	if (!create_action(app->oxr.instance, XR_ACTION_TYPE_VIBRATION_OUTPUT, "haptic",
	                   "Haptic Vibration", gameplay_actionset, HAND_COUNT, hand_paths,
	                   &app->haptic_action.action))
		return 1;


	struct Binding simple_bindings[] = {
	    {.action = app->grab_action.action,
	     .paths = {"/user/hand/left/input/select/click", "/user/hand/right/input/select/click"},
	     .path_count = 2},
	    {.action = app->hand_pose_action.action,
	     .paths = {"/user/hand/left/input/grip/pose", "/user/hand/right/input/grip/pose"},
	     .path_count = 2},
	    {.action = app->aim_action.action,
	     .paths = {"/user/hand/left/input/aim/pose", "/user/hand/right/input/aim/pose"},
	     .path_count = 2},
	    {.action = app->haptic_action.action,
	     .paths = {"/user/hand/left/output/haptic", "/user/hand/right/output/haptic"},
	     .path_count = 2},
	};
	if (!suggest_actions(app->oxr.instance, "/interaction_profiles/khr/simple_controller",
	                     simple_bindings, ARRAY_SIZE(simple_bindings)))
		return 1;


	struct Binding touch_bindings[] = {
	    {.action = app->grab_action.action,
	     .paths = {"/user/hand/left/input/trigger/value", "/user/hand/right/input/trigger/value"},
	     .path_count = 2},
	    {.action = app->accelerate_action.action,
	     .paths = {"/user/hand/left/input/thumbstick/y", "/user/hand/right/input/thumbstick/y"},
	     .path_count = 2},
	    {.action = app->hand_pose_action.action,
	     .paths = {"/user/hand/left/input/grip/pose", "/user/hand/right/input/grip/pose"},
	     .path_count = 2},
	    {.action = app->haptic_action.action,
	     .paths = {"/user/hand/left/output/haptic", "/user/hand/right/output/haptic"},
	     .path_count = 2},
	};
	if (!suggest_actions(app->oxr.instance, "/interaction_profiles/oculus/touch_controller",
	                     touch_bindings, ARRAY_SIZE(touch_bindings)))
		return 1;

	struct Binding index_bindings[] = {
	    {.action = app->grab_action.action,
	     .paths = {"/user/hand/left/input/trigger", "/user/hand/right/input/trigger"},
	     .path_count = 2},
	    {.action = app->accelerate_action.action,
	     .paths = {"/user/hand/left/input/thumbstick/y", "/user/hand/right/input/thumbstick/y"},
	     .path_count = 2},
	    {.action = app->hand_pose_action.action,
	     .paths = {"/user/hand/left/input/grip/pose", "/user/hand/right/input/grip/pose"},
	     .path_count = 2},
	    {.action = app->aim_action.action,
	     .paths = {"/user/hand/left/input/aim/pose", "/user/hand/right/input/aim/pose"},
	     .path_count = 2},
	    {.action = app->haptic_action.action,
	     .paths = {"/user/hand/left/output/haptic", "/user/hand/right/output/haptic"},
	     .path_count = 2},
	};
	if (!suggest_actions(app->oxr.instance, "/interaction_profiles/valve/index_controller",
	                     index_bindings, ARRAY_SIZE(index_bindings)))
		return 1;


	struct Binding vive_bindings[] = {
	    {.action = app->grab_action.action,
	     .paths = {"/user/hand/left/input/trigger/value", "/user/hand/right/input/trigger/value"},
	     .path_count = 2},
	    {.action = app->hand_pose_action.action,
	     .paths = {"/user/hand/left/input/grip/pose", "/user/hand/right/input/grip/pose"},
	     .path_count = 2},
	    {.action = app->aim_action.action,
	     .paths = {"/user/hand/left/input/aim/pose", "/user/hand/right/input/aim/pose"},
	     .path_count = 2},
	    {.action = app->haptic_action.action,
	     .paths = {"/user/hand/left/output/haptic", "/user/hand/right/output/haptic"},
	     .path_count = 2},
	};
	if (!suggest_actions(app->oxr.instance, "/interaction_profiles/htc/vive_controller",
	                     vive_bindings, ARRAY_SIZE(vive_bindings)))
		return 1;

	struct hand_interaction_t* hand_interaction_ext =
	    (struct hand_interaction_t*)get_ext(app, XR_EXT_HAND_INTERACTION_EXTENSION_NAME);
	if (hand_interaction_ext && hand_interaction_ext->base.supported) {
		struct Binding hi_bindings[] = {
		    {.action = app->grab_action.action,
		     .paths = {"/user/hand/left/input/pinch_ext/value",
		               "/user/hand/right/input/pinch_ext/value"},
		     .path_count = 2},
		    {.action = app->hand_pose_action.action,
		     .paths = {"/user/hand/left/input/grip/pose", "/user/hand/right/input/grip/pose"},
		     .path_count = 2},
		    {.action = app->aim_action.action,
		     .paths = {"/user/hand/left/input/aim/pose", "/user/hand/right/input/aim/pose"},
		     .path_count = 2},
		};
		if (!suggest_actions(app->oxr.instance, "/interaction_profiles/ext/hand_interaction_ext",
		                     hi_bindings, ARRAY_SIZE(hi_bindings)))
			return 1;
	}

	struct hand_tracking_t* hand_tracking_ext =
	    (struct hand_tracking_t*)get_ext(app, XR_EXT_HAND_TRACKING_EXTENSION_NAME);
	if (hand_tracking_ext->system_supported) {
		if (!create_hand_trackers(app->oxr.instance, app->oxr.session, hand_tracking_ext))
			return 1;
	}


	struct plane_detection_t* plane_detection_ext =
	    (struct plane_detection_t*)get_ext(app, XR_EXT_PLANE_DETECTION_EXTENSION_NAME);
	if (plane_detection_ext->base.supported) {
		XrPlaneDetectorCreateInfoEXT create_info = {
		    .type = XR_TYPE_PLANE_DETECTOR_CREATE_INFO_EXT,
		    .flags = XR_PLANE_DETECTOR_ENABLE_CONTOUR_BIT_EXT,
		};
		result = plane_detection_ext->xrCreatePlaneDetectorEXT(app->oxr.session, &create_info,
		                                                       &plane_detection_ext->pd);
		if (!xr_check(app->oxr.instance, result, "failed to create plane detector set"))
			return 1;
	}

	// Set up rendering (compile shaders, ...) before starting the app->oxr.session
	if (init_gl(app->oxr.view_count, app->vr_swapchains[SWAPCHAIN_PROJECTION].swapchain_lengths,
	            &app->gl_renderer) != 0) {
		printf("OpenGl setup failed!\n");
		return 1;
	}


	XrSessionActionSetsAttachInfo actionset_attach_info = {
	    .type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO,
	    .next = NULL,
	    .countActionSets = 1,
	    .actionSets = &gameplay_actionset};
	result = xrAttachSessionActionSets(app->oxr.session, &actionset_attach_info);
	if (!xr_check(app->oxr.instance, result, "failed to attach action set"))
		return 1;

	XrEventDataBuffer* runtime_event = NULL;

	bool quit_renderloop = false;
	bool session_running = false; // to avoid beginning an already running app->oxr.session
	while (!quit_renderloop) {

		// --- Poll SDL for events so we can exit with esc
		SDL_Event sdl_event;
		while (SDL_PollEvent(&sdl_event)) {
			if (sdl_event.type == SDL_QUIT ||
			    (sdl_event.type == SDL_KEYDOWN && sdl_event.key.keysym.sym == SDLK_ESCAPE)) {
				printf("Requesting exit...\n");
				xrRequestExitSession(app->oxr.session);
			}
		}


		// for several app->oxr.session app->oxr.states we want to skip the render loop
		bool skip_renderloop = false;

		// --- Handle runtime Events
		// we do this before xrWaitFrame() so we can go idle or
		// break out of the main render loop as early as possible and don't have to
		// uselessly render or submit one. Calling xrWaitFrame commits you to
		// calling xrBeginFrame eventually.
		if (!runtime_event)
			runtime_event = (XrEventDataBuffer*)malloc(sizeof(XrEventDataBuffer));
		runtime_event->type = XR_TYPE_EVENT_DATA_BUFFER;
		runtime_event->next = NULL;
		XrResult poll_result = xrPollEvent(app->oxr.instance, runtime_event);
		while (poll_result == XR_SUCCESS) {
			switch (runtime_event->type) {
			case XR_TYPE_EVENT_DATA_EVENTS_LOST: {
				XrEventDataEventsLost* event = (XrEventDataEventsLost*)runtime_event;
				printf("EVENT: %d events data lost!\n", event->lostEventCount);
				// do we care if the runtime loses events?
				break;
			}
			case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
				XrEventDataInstanceLossPending* event = (XrEventDataInstanceLossPending*)runtime_event;
				printf("EVENT: app->oxr.instance loss pending at %lu! Destroying app->oxr.instance.\n",
				       event->lossTime);
				quit_renderloop = true;
				continue;
			}
			case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
				XrEventDataSessionStateChanged* event = (XrEventDataSessionStateChanged*)runtime_event;
				printf("EVENT: app->oxr.session app->oxr.state changed from %d to %d\n", app->oxr.state,
				       event->state);
				app->oxr.state = event->state;

				// react to app->oxr.session app->oxr.state changes, see OpenXR spec 9.3 diagram
				switch (app->oxr.state) {

				// just keep polling, skip render loop
				case XR_SESSION_STATE_MAX_ENUM:
					// must be a bug, just keep polling
				case XR_SESSION_STATE_IDLE:
				case XR_SESSION_STATE_UNKNOWN: {
					skip_renderloop = true;
					break; // app->oxr.state handling switch
				}

				// do nothing, run render loop normally
				case XR_SESSION_STATE_FOCUSED:
				case XR_SESSION_STATE_SYNCHRONIZED:
				case XR_SESSION_STATE_VISIBLE: {
					skip_renderloop = false;
					break; // app->oxr.state handling switch
				}

				// begin app->oxr.session and then run render loop
				case XR_SESSION_STATE_READY: {
					// start app->oxr.session only if it is not running, i.e. not when we already called
					// xrBeginSession but the runtime did not switch to the next app->oxr.state yet
					if (!session_running) {
						XrSessionBeginInfo session_begin_info = {.type = XR_TYPE_SESSION_BEGIN_INFO,
						                                         .next = NULL,
						                                         .primaryViewConfigurationType =
						                                             app->oxr.view_type};
						result = xrBeginSession(app->oxr.session, &session_begin_info);
						if (!xr_check(app->oxr.instance, result, "Failed to begin session!"))
							return 1;
						printf("Session started!\n");
						session_running = true;
					}
					skip_renderloop = false;
					break; // app->oxr.state handling switch
				}

				// end app->oxr.session, skip render loop, keep polling for next app->oxr.state change
				case XR_SESSION_STATE_STOPPING: {
					// end app->oxr.session only if it is running, i.e. not when we already called
					// xrEndSession but the runtime did not switch to the next app->oxr.state yet
					if (session_running) {
						result = xrEndSession(app->oxr.session);
						if (!xr_check(app->oxr.instance, result, "Failed to end app->oxr.session!"))
							return 1;
						session_running = false;
					}
					skip_renderloop = true;
					break; // app->oxr.state handling switch
				}

				// destroy app->oxr.session, skip render loop, exit render loop and quit
				case XR_SESSION_STATE_LOSS_PENDING:
				case XR_SESSION_STATE_EXITING:
					result = xrDestroySession(app->oxr.session);
					if (!xr_check(app->oxr.instance, result, "Failed to destroy app->oxr.session!"))
						return 1;
					quit_renderloop = true;
					skip_renderloop = true;
					break; // app->oxr.state handling switch
				}
				break;
			}
			case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
				printf("EVENT: reference space change pending!\n");
				XrEventDataReferenceSpaceChangePending* event =
				    (XrEventDataReferenceSpaceChangePending*)runtime_event;
				(void)event;
				// TODO: do something
				break;
			}
			case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED: {
				printf("EVENT: interaction profile changed!\n");
				XrEventDataInteractionProfileChanged* event =
				    (XrEventDataInteractionProfileChanged*)runtime_event;
				(void)event;

				XrInteractionProfileState state = {.type = XR_TYPE_INTERACTION_PROFILE_STATE};

				for (int i = 0; i < 2; i++) {
					XrResult res = xrGetCurrentInteractionProfile(app->oxr.session, hand_paths[i], &state);
					if (!xr_check(app->oxr.instance, res, "Failed to get interaction profile for %d", i))
						continue;

					XrPath prof = state.interactionProfile;

					uint32_t strl;
					char profile_str[XR_MAX_PATH_LENGTH];

					if (prof != XR_NULL_PATH) {
						res = xrPathToString(app->oxr.instance, prof, XR_MAX_PATH_LENGTH, &strl, profile_str);
						if (!xr_check(app->oxr.instance, res,
						              "Failed to get interaction profile path str for %d", i))
							continue;
					} else {
						strncpy(profile_str, "[UNBOUND]", XR_MAX_PATH_LENGTH);
					}

					printf("Event: Interaction profile changed for %d: %s\n", i, profile_str);
				}
				// TODO: do something
				break;
			}

			case XR_TYPE_EVENT_DATA_VISIBILITY_MASK_CHANGED_KHR: {
				printf("EVENT: visibility mask changed!!\n");
				XrEventDataVisibilityMaskChangedKHR* event =
				    (XrEventDataVisibilityMaskChangedKHR*)runtime_event;
				(void)event;
				// this event is from an extension
				break;
			}
			case XR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT: {
				printf("EVENT: perf settings!\n");
				XrEventDataPerfSettingsEXT* event = (XrEventDataPerfSettingsEXT*)runtime_event;
				(void)event;
				// this event is from an extension
				break;
			}
			case XR_TYPE_EVENT_DATA_USER_PRESENCE_CHANGED_EXT: {
				printf("EVENT: user presence changed!!\n");
				XrEventDataUserPresenceChangedEXT* event =
				    (XrEventDataUserPresenceChangedEXT*)runtime_event;
				printf("Event: user presence changed: isUserPresent=%s\n",
				       event->isUserPresent ? "true" : "false");
				// this event is from an extension
				break;
			}
			default: printf("Unhandled event type %d\n", runtime_event->type);
			}

			runtime_event->type = XR_TYPE_EVENT_DATA_BUFFER;
			poll_result = xrPollEvent(app->oxr.instance, runtime_event);
		}
		if (poll_result == XR_EVENT_UNAVAILABLE) {
			// processed all events in the queue
		} else {
			printf("Failed to poll events!\n");
			break;
		}

		if (skip_renderloop) {
			continue;
		}

		// --- Wait for our turn to do head-pose dependent computation and render a frame
		app->oxr.frameState = (XrFrameState){.type = XR_TYPE_FRAME_STATE, .next = NULL};
		XrFrameWaitInfo frameWaitInfo = {.type = XR_TYPE_FRAME_WAIT_INFO, .next = NULL};
		result = xrWaitFrame(app->oxr.session, &frameWaitInfo, &app->oxr.frameState);
		if (!xr_check(app->oxr.instance, result, "xrWaitFrame() was not successful, exiting..."))
			break;


		// Maybe updating new/disappeared xdevs is not necessary every frame. The XrSpaces are located
		// independently.
		struct xdev_space_t* xdev_ext =
		    (struct xdev_space_t*)get_ext(app, XR_MNDX_XDEV_SPACE_EXTENSION_NAME);
		if (app->args.xdev_space && xdev_ext && xdev_ext->base.supported &&
		    xdev_ext->system_supported) {
			if (!update_xdev_spaces(app->oxr.instance, app->oxr.session, xdev_ext)) {
				return 1;
			}
		}


		// --- Create projection matrices and view matrices for each eye
		XrViewLocateInfo view_locate_info = {.type = XR_TYPE_VIEW_LOCATE_INFO,
		                                     .next = NULL,
		                                     .viewConfigurationType =
		                                         XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
		                                     .displayTime = app->oxr.frameState.predictedDisplayTime,
		                                     .space = app->oxr.play_space};

		for (uint32_t i = 0; i < app->oxr.view_count; i++) {
			app->oxr.views[i].type = XR_TYPE_VIEW;
			app->oxr.views[i].next = NULL;
		};

		app->oxr.view_state = (XrViewState){.type = XR_TYPE_VIEW_STATE, .next = NULL};
		result = xrLocateViews(app->oxr.session, &view_locate_info, &app->oxr.view_state,
		                       app->oxr.view_count, &app->oxr.view_count, app->oxr.views);

		if (!xr_check(app->oxr.instance, result, "Could not locate views"))
			break;


		//! @todo Move this action processing to before xrWaitFrame, probably.
		const XrActiveActionSet active_actionsets[] = {
		    {.actionSet = gameplay_actionset, .subactionPath = XR_NULL_PATH}};

		XrActionsSyncInfo actions_sync_info = {
		    .type = XR_TYPE_ACTIONS_SYNC_INFO,
		    .countActiveActionSets = sizeof(active_actionsets) / sizeof(active_actionsets[0]),
		    .activeActionSets = active_actionsets,
		};
		result = xrSyncActions(app->oxr.session, &actions_sync_info);
		xr_check(app->oxr.instance, result, "failed to sync actions!");

		for (int i = 0; i < HAND_COUNT; i++) {
			if (!get_action_data(app->oxr.instance, app->oxr.session, &app->hand_pose_action, i,
			                     hand_paths, app->oxr.play_space,
			                     app->oxr.frameState.predictedDisplayTime,
			                     app->args.query_hand_velocities))
				printf("Failed to get hand pose action data for hand %d\n", i);

			if (!get_action_data(app->oxr.instance, app->oxr.session, &app->aim_action, i, hand_paths,
			                     app->oxr.play_space, app->oxr.frameState.predictedDisplayTime,
			                     app->args.query_hand_velocities))
				printf("Failed to get aim pose action data for hand %d\n", i);


			if (!get_action_data(app->oxr.instance, app->oxr.session, &app->grab_action, i, hand_paths,
			                     XR_NULL_HANDLE, 0, false))
				printf("Failed to get grab action data for hand %d\n", i);

			if (!get_action_data(app->oxr.instance, app->oxr.session, &app->accelerate_action, i,
			                     hand_paths, XR_NULL_HANDLE, 0, false))
				printf("Failed to get accelerate action data for hand %d\n", i);


			if (app->grab_action.states[i].float_.isActive &&
			    app->grab_action.states[i].float_.currentState > 0.75) {
				XrHapticVibration vibration = {.type = XR_TYPE_HAPTIC_VIBRATION,
				                               .next = nullptr,
				                               .duration = XR_MIN_HAPTIC_DURATION,
				                               .frequency = XR_FREQUENCY_UNSPECIFIED,
				                               .amplitude = 0.5};
				XrHapticActionInfo haptic_action_info = {.type = XR_TYPE_HAPTIC_ACTION_INFO,
				                                         .next = NULL,
				                                         .action = app->haptic_action.action,
				                                         .subactionPath = hand_paths[i]};
				result = xrApplyHapticFeedback(app->oxr.session, &haptic_action_info,
				                               (const XrHapticBaseHeader*)&vibration);
				xr_check(app->oxr.instance, result, "failed to apply haptic feedback!");
				// printf("Sent haptic output to hand %d\n", i);
			}


			if (app->accelerate_action.states[i].float_.isActive &&
			    app->accelerate_action.states[i].float_.currentState != 0) {
				printf("Throttle value %d: changed %d: %f\n", i,
				       app->accelerate_action.states[i].float_.changedSinceLastSync,
				       app->accelerate_action.states[i].float_.currentState);
			}

			if (hand_tracking_ext->system_supported) {
				get_hand_tracking(app->oxr.instance, app->oxr.play_space,
				                  app->oxr.frameState.predictedDisplayTime,
				                  app->args.query_joint_velocities, hand_tracking_ext, i);
			}
		};

		if (plane_detection_ext->base.supported) {
			result = plane_detection_ext->xrGetPlaneDetectionStateEXT(plane_detection_ext->pd,
			                                                          &plane_detection_ext->state);
			if (!xr_check(app->oxr.instance, result, "failed to query plane detector state"))
				return 1;

			// printf("Plane detection state %s\n",
			// XrStr_XrPlaneDetectionStateEXT(plane_detection_ext->state));

			// if a plane detection is finished, copy the results before a new one is started.
			if (plane_detection_ext->state == XR_PLANE_DETECTION_STATE_DONE_EXT) {
				XrPlaneDetectorGetInfoEXT get_info = {
				    .type = XR_TYPE_PLANE_DETECTOR_GET_INFO_EXT,
				    .next = nullptr,
				    .baseSpace = app->oxr.play_space,
				    .time = app->oxr.frameState.predictedDisplayTime,
				};

				plane_detection_ext->locs.type = XR_TYPE_PLANE_DETECTOR_LOCATIONS_EXT;
				plane_detection_ext->locs.next = NULL;
				plane_detection_ext->locs.planeLocationCapacityInput = 0;


				result = plane_detection_ext->xrGetPlaneDetectionsEXT(plane_detection_ext->pd, &get_info,
				                                                      &plane_detection_ext->locs);
				if (!xr_check(app->oxr.instance, result, "failed to get plane detections 1"))
					return 1;

				XrPlaneDetectorLocationEXT* loc_arr = static_cast<XrPlaneDetectorLocationEXT*>(
				    malloc(sizeof(XrPlaneDetectorLocationEXT) *
				           plane_detection_ext->locs.planeLocationCountOutput));
				plane_detection_ext->locs.planeLocations = loc_arr;
				plane_detection_ext->locs.planeLocationCapacityInput =
				    plane_detection_ext->locs.planeLocationCountOutput;

				result = plane_detection_ext->xrGetPlaneDetectionsEXT(plane_detection_ext->pd, &get_info,
				                                                      &plane_detection_ext->locs);
				if (!xr_check(app->oxr.instance, result, "failed to get plane detections 2"))
					return 1;

				struct plane_data_t* new_list = NULL;

				for (uint32_t i_loc = 0; i_loc < plane_detection_ext->locs.planeLocationCountOutput;
				     i_loc++) {
					XrPlaneDetectorLocationEXT* loc = &loc_arr[i_loc];

					struct plane_data_t* data = NULL;

					// steal plane_data_t from list if it has our plane_id
					if (plane_detection_ext->plane_data_list != NULL &&
					    plane_detection_ext->plane_data_list->plane_id == loc->planeId) {
						data = plane_detection_ext->plane_data_list;
						plane_detection_ext->plane_data_list = plane_detection_ext->plane_data_list->next;
					} else {
						for (struct plane_data_t* l = plane_detection_ext->plane_data_list; l; l = l->next) {
							if (l->next != NULL && l->next->plane_id == loc->planeId) {
								data = l->next;
								l->next = l->next->next;
								break;
							}
						}
					}

					if (data == NULL) {
						data = static_cast<plane_data_t*>(malloc(sizeof(struct plane_data_t)));
						data->plane_id = loc->planeId;
						data->polygon_count = 0;
						data->polygons = NULL;
					}

					for (uint32_t i_poly = 0; i_poly < data->polygon_count; i_poly++) {
						free(data->polygons[i_poly].vertices);
					}
					free(data->polygons);
					data->polygon_count = 0;

					data->polygons =
					    static_cast<polygon_t*>(malloc(sizeof(struct polygon_t) * loc->polygonBufferCount));
					data->polygon_count = loc->polygonBufferCount;

					for (uint32_t i_poly = 0; i_poly < loc->polygonBufferCount; i_poly++) {
						XrPlaneDetectorPolygonBufferEXT buf = {
						    .type = XR_TYPE_PLANE_DETECTOR_POLYGON_BUFFER_EXT,
						    .vertexCapacityInput = 0,
						};
						result = plane_detection_ext->xrGetPlanePolygonBufferEXT(plane_detection_ext->pd,
						                                                         loc->planeId, i_poly, &buf);
						if (!xr_check(app->oxr.instance, result, "failed to get plane detection polygon buf 1"))
							return 1;

						data->polygons[i_poly].vertices =
						    static_cast<XrVector2f*>(malloc(sizeof(XrVector2f) * buf.vertexCountOutput));
						buf.vertices = data->polygons[i_poly].vertices;
						buf.vertexCapacityInput = buf.vertexCountOutput;

						result = plane_detection_ext->xrGetPlanePolygonBufferEXT(plane_detection_ext->pd,
						                                                         loc->planeId, i_poly, &buf);
						if (!xr_check(app->oxr.instance, result, "failed to get plane detection polygon buf 2"))
							return 1;
					}
				}

				free(loc_arr); // we copied all info we need

				// delete old list of planes that should now have only planes not detected anymore
				struct plane_data_t* l = plane_detection_ext->plane_data_list;
				while (l != NULL) {
					l = l->next;
					struct plane_data_t* tmp = l;
					for (uint32_t i_poly = 0; i_poly < tmp->polygon_count; i_poly++) {
						free(tmp->polygons[i_poly].vertices);
					}
					free(tmp);
				}
				plane_detection_ext->plane_data_list = new_list;

				printf("Got %d planes\n", plane_detection_ext->locs.planeLocationCountOutput);
			}

			// if a plane detection is running, do nothing until next loop. Otherwise start a new query.
			if (plane_detection_ext->state != XR_PLANE_DETECTION_STATE_PENDING_EXT) {
				XrPlaneDetectorBeginInfoEXT begin_info = {
				    .type = XR_TYPE_PLANE_DETECTOR_BEGIN_INFO_EXT,
				    .baseSpace = app->oxr.play_space,
				    .time = app->oxr.frameState.predictedDisplayTime,
				    .orientationCount = 0,
				    .semanticTypeCount = 0,
				    .maxPlanes = 1000,
				    .minArea = 0.0f,
				    .boundingBoxPose = identity_pose,
				    .boundingBoxExtent = (XrExtent3DfEXT){100.0f, 100.0f, 100.0f},
				};
				result =
				    plane_detection_ext->xrBeginPlaneDetectionEXT(plane_detection_ext->pd, &begin_info);
				if (!xr_check(app->oxr.instance, result, "failed to start plane detector query"))
					return 1;

				printf("Begin plane detection\n");
			}
		}

		if (app->cube.enabled) {
			if (app->cube.pos_ts != 0) {
				XrDuration diff_ns = app->oxr.frameState.predictedDisplayTime - app->cube.pos_ts;
				float diff_s = (double)diff_ns * 1. / 1000. * 1. / 1000. * 1. / 1000.;
				XrVector3f next_pos = {
				    .x = app->cube.current_pos.x += app->cube.velocity.x * diff_s,
				    .y = app->cube.current_pos.y += app->cube.velocity.y * diff_s,
				    .z = app->cube.current_pos.z += app->cube.velocity.z * diff_s,
				};
				if (next_pos.x > app->cube.center_pos.x + app->cube.bouncing_lengths.x || //
				    next_pos.y > app->cube.center_pos.y + app->cube.bouncing_lengths.y || //
				    next_pos.z > app->cube.center_pos.z + app->cube.bouncing_lengths.z || //
				    next_pos.x < app->cube.center_pos.x - app->cube.bouncing_lengths.x || //
				    next_pos.y < app->cube.center_pos.y - app->cube.bouncing_lengths.y || //
				    next_pos.z < app->cube.center_pos.z - app->cube.bouncing_lengths.z) {
					app->cube.velocity.x *= -1;
					app->cube.velocity.y *= -1;
					app->cube.velocity.z *= -1;

					next_pos = (XrVector3f){
					    .x = app->cube.current_pos.x += app->cube.velocity.x * diff_s,
					    .y = app->cube.current_pos.y += app->cube.velocity.y * diff_s,
					    .z = app->cube.current_pos.z += app->cube.velocity.z * diff_s,
					};
				}
				app->cube.current_pos = next_pos;
				// printf("render cube at %f %f %f\n", app->cube.current_pos.x, app->cube.current_pos.y,
				// app->cube.current_pos.z);
			}

			app->cube.pos_ts = app->oxr.frameState.predictedDisplayTime;
		}

		// --- Begin frame
		XrFrameBeginInfo frame_begin_info = {.type = XR_TYPE_FRAME_BEGIN_INFO, .next = NULL};

		result = xrBeginFrame(app->oxr.session, &frame_begin_info);
		if (!xr_check(app->oxr.instance, result, "failed to begin frame!"))
			break;

		// all swapchain release infos happen to be the same
		XrSwapchainImageReleaseInfo release_info = {.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO,
		                                            .next = NULL};

		if (!app->acquired_color) {
			app->acquired_color = static_cast<uint32_t*>(malloc(app->oxr.view_count * sizeof(uint32_t)));
		}
		if (!app->acquired_depth) {
			app->acquired_depth = static_cast<uint32_t*>(malloc(app->oxr.view_count * sizeof(uint32_t)));
		}

		for (uint32_t i = 0; i < app->oxr.view_count; i++) {
			if (!acquire_swapchain(app->oxr.instance, &app->vr_swapchains[SWAPCHAIN_PROJECTION], i,
			                       &app->acquired_color[i]))
				break;

			if (depth_ext->base.supported) {
				if (!acquire_swapchain(app->oxr.instance, &app->vr_swapchains[SWAPCHAIN_DEPTH], i,
				                       &app->acquired_depth[i]))
					break;
			}
		}

		// render projection layer (once per view) and fill projection_views with the result


		render_frame(app, &app->gl_renderer, app->oxr.frameState.predictedDisplayTime,
		             app->hand_pose_action.pose_locations, hand_tracking_ext,
		             depth_ext->base.supported);

		for (uint32_t i = 0; i < app->oxr.view_count; i++) {
			result = xrReleaseSwapchainImage(app->vr_swapchains[SWAPCHAIN_PROJECTION].swapchains[i],
			                                 &release_info);
			if (!xr_check(app->oxr.instance, result, "failed to release swapchain image!"))
				break;

			if (depth_ext->base.supported) {
				result = xrReleaseSwapchainImage(app->vr_swapchains[SWAPCHAIN_DEPTH].swapchains[i],
				                                 &release_info);
				if (!xr_check(app->oxr.instance, result, "failed to release swapchain image!"))
					break;
			}

			app->oxr.projection_views[i].pose = app->oxr.views[i].pose;
			app->oxr.projection_views[i].fov = app->oxr.views[i].fov;
		}


		uint32_t quad_index = 0;
		if (!acquire_swapchain(app->oxr.instance, &app->quad_layer.swapchain, 0, &quad_index))
			break;

		render_quad(&app->gl_renderer, &app->quad_layer, quad_index,
		            app->oxr.frameState.predictedDisplayTime);

		result = xrReleaseSwapchainImage(app->quad_layer.swapchain.swapchains[0], &release_info);
		if (!xr_check(app->oxr.instance, result, "failed to release swapchain image!"))
			break;


		// projectionLayers struct reused for every frame
		XrCompositionLayerProjection projection_layer = {
		    .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
		    .next = NULL,
		    .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
		    .space = app->oxr.play_space,
		    .viewCount = app->oxr.view_count,
		    .views = app->oxr.projection_views,
		};


		float quad_aspect = (float)app->quad_layer.pixel_width / (float)app->quad_layer.pixel_height;
		float quad_width = 1.f;
		XrCompositionLayerQuad quad_comp_layer = {
		    .type = XR_TYPE_COMPOSITION_LAYER_QUAD,
		    .next = nullptr,
		    .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
		    .space = app->oxr.play_space,
		    .eyeVisibility = XR_EYE_VISIBILITY_BOTH,
		    .subImage = {.swapchain = app->quad_layer.swapchain.swapchains[0],
		                 .imageRect =
		                     {
		                         .offset = {.x = 0, .y = 0},
		                         .extent = {.width = app->quad_layer.pixel_width,
		                                    .height = app->quad_layer.pixel_height},
		                     }},
		    .pose = {.orientation = {.x = 0.f, .y = 0.f, .z = 0.f, .w = 1.f},
		             .position = {.x = 1.5f, .y = .7f, .z = -1.5f}},
		    .size = {.width = quad_width, .height = quad_width / quad_aspect},
		};


		int submitted_layer_count = 1;
		const XrCompositionLayerBaseHeader* submitted_layers[2] = {
		    (const XrCompositionLayerBaseHeader* const)&projection_layer};
		// already set projection_views[i].next = &depth.infos[i]; if depth supported


		submitted_layers[submitted_layer_count++] =
		    (const XrCompositionLayerBaseHeader* const)&quad_comp_layer;

		if ((app->oxr.view_state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) == 0) {
			printf("Not submitting layers because orientation is invalid\n");
			submitted_layer_count = 0;
		}

		XrFrameEndInfo frameEndInfo = {.type = XR_TYPE_FRAME_END_INFO,
		                               .next = nullptr,
		                               .displayTime = app->oxr.frameState.predictedDisplayTime,
		                               .environmentBlendMode = app->oxr.blend_mode,
		                               .layerCount = submitted_layer_count,
		                               .layers = submitted_layers};
		result = xrEndFrame(app->oxr.session, &frameEndInfo);
		if (!xr_check(app->oxr.instance, result, "failed to end frame!"))
			break;
	}



	// --- Clean up after render loop quits
	if (runtime_event)
		free(runtime_event);

	for (uint32_t i = 0; i < app->oxr.view_count; i++) {
		free(app->vr_swapchains[SWAPCHAIN_PROJECTION].images[i]);
		if (depth_ext->base.supported) {
			free(app->vr_swapchains[SWAPCHAIN_DEPTH].images[i]);
		}

		glDeleteFramebuffers(app->vr_swapchains[SWAPCHAIN_PROJECTION].swapchain_lengths[i],
		                     app->gl_renderer.framebuffers[i]);
		free(app->gl_renderer.framebuffers[i]);
	}
	xrDestroyInstance(app->oxr.instance);

	free(app->oxr.viewconfig_views);
	free(app->oxr.projection_views);
	free(app->oxr.views);

	destroy_swapchain(&app->vr_swapchains[SWAPCHAIN_PROJECTION]);
	destroy_swapchain(&app->vr_swapchains[SWAPCHAIN_DEPTH]);
	free(app->gl_renderer.framebuffers);

	free(app->acquired_color);
	free(app->acquired_depth);

	free(depth_ext->infos);

	free(app);

	printf("Cleaned up!\n");
	return 0;
}


// =============================================================================
// OpenGL rendering code
// =============================================================================

// Header-only math library for rendering transforms.
#include "external/HandmadeMath.h"

static SDL_Window* desktop_window;
static SDL_GLContext gl_context;

void
MessageCallback(GLenum source,
                GLenum type,
                GLuint id,
                GLenum severity,
                GLsizei length,
                const GLchar* message,
                const void* userParam)
{
	if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
		return;
	fprintf(stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
	        (type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : ""), type, severity, message);
}


bool
init_sdl_window(int w, int h)
{
	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		printf("Unable to initialize SDL");
		return false;
	}

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 0);


	/* Create our window centered at half the VR resolution */
	desktop_window =
	    SDL_CreateWindow("OpenXR Example", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w / 2,
	                     h / 2, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
	if (!desktop_window) {
		printf("Unable to create window");
		return false;
	}

	gl_context = SDL_GL_CreateContext(desktop_window);

	init_gl_funcs();

	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(MessageCallback, 0);

	SDL_GL_SetSwapInterval(0);
	return true;
}


static const char* vertexshader =
    "#version 330 core\n"
    "#extension GL_ARB_explicit_uniform_location : require\n"
    "layout(location = 0) in vec3 aPos;\n"
    "layout(location = 2) uniform mat4 model;\n"
    "layout(location = 3) uniform mat4 view;\n"
    "layout(location = 4) uniform mat4 proj;\n"
    "layout(location = 5) in vec2 aColor;\n"
    "out vec2 vertexColor;\n"
    "void main() {\n"
    "	gl_Position = proj * view * model * vec4(aPos.x, aPos.y, aPos.z, "
    "1.0);\n"
    "	vertexColor = aColor;\n"
    "}\n";

static const char* fragmentshader =
    "#version 330 core\n"
    "#extension GL_ARB_explicit_uniform_location : require\n"
    "layout(location = 0) out vec4 FragColor;\n"
    "layout(location = 1) uniform vec4 uniformColor;\n"
    "layout(location = 6) uniform int busyLoops;\n"
    "in vec2 vertexColor;\n"
    "void main() {\n"
    "	FragColor = "
    "(uniformColor.x < 0.01 && "
    "uniformColor.y < 0.01 && "
    "uniformColor.z < 0.01 && "
    "uniformColor.w < 0.01 ? vec4(vertexColor, 1.0, 1.0) : uniformColor);\n"
    "if (busyLoops > 0) { float x = 500000.f;\n for (int i = 0; i < busyLoops; i++) { x = sqrt(x) "
    "* 2.f; }\n FragColor.x = x;\n}"
    "}\n";

int
init_gl(uint32_t view_count, uint32_t* swapchain_lengths, struct gl_renderer_t* gl_renderer)
{

	/* Allocate resources that we use for our own rendering.
	 * We will bind framebuffers to the runtime provided textures for rendering.
	 * For this, we create one framebuffer per OpenGL texture.
	 * This is not mandated by OpenXR, other ways to render to textures will work too.
	 */
	gl_renderer->framebuffers = static_cast<GLuint**>(malloc(sizeof(GLuint*) * view_count));
	for (uint32_t i = 0; i < view_count; i++) {
		gl_renderer->framebuffers[i] =
		    static_cast<GLuint*>(malloc(sizeof(GLuint) * swapchain_lengths[i]));
		glGenFramebuffers(swapchain_lengths[i], gl_renderer->framebuffers[i]);
	}

	GLuint vertex_shader_id = glCreateShader(GL_VERTEX_SHADER);
	const GLchar* vertex_shader_source[1];
	vertex_shader_source[0] = vertexshader;
	// printf("Vertex Shader:\n%s\n", vertexShaderSource);
	glShaderSource(vertex_shader_id, 1, vertex_shader_source, NULL);
	glCompileShader(vertex_shader_id);
	int vertex_compile_res;
	glGetShaderiv(vertex_shader_id, GL_COMPILE_STATUS, &vertex_compile_res);
	if (!vertex_compile_res) {
		char info_log[512];
		glGetShaderInfoLog(vertex_shader_id, 512, NULL, info_log);
		printf("Vertex Shader failed to compile: %s\n", info_log);
		return 1;
	} else {
		printf("Successfully compiled vertex shader!\n");
	}

	GLuint fragment_shader_id = glCreateShader(GL_FRAGMENT_SHADER);
	const GLchar* fragment_shader_source[1];
	fragment_shader_source[0] = fragmentshader;
	glShaderSource(fragment_shader_id, 1, fragment_shader_source, NULL);
	glCompileShader(fragment_shader_id);
	int fragment_compile_res;
	glGetShaderiv(fragment_shader_id, GL_COMPILE_STATUS, &fragment_compile_res);
	if (!fragment_compile_res) {
		char info_log[512];
		glGetShaderInfoLog(fragment_shader_id, 512, NULL, info_log);
		printf("Fragment Shader failed to compile: %s\n", info_log);
		return 1;
	} else {
		printf("Successfully compiled fragment shader!\n");
	}

	gl_renderer->shader_program_id = glCreateProgram();
	glAttachShader(gl_renderer->shader_program_id, vertex_shader_id);
	glAttachShader(gl_renderer->shader_program_id, fragment_shader_id);
	glLinkProgram(gl_renderer->shader_program_id);
	GLint shader_program_res;
	glGetProgramiv(gl_renderer->shader_program_id, GL_LINK_STATUS, &shader_program_res);
	if (!shader_program_res) {
		char info_log[512];
		glGetProgramInfoLog(gl_renderer->shader_program_id, 512, NULL, info_log);
		printf("Shader Program failed to link: %s\n", info_log);
		return 1;
	} else {
		printf("Successfully linked shader program!\n");
	}

	glDeleteShader(vertex_shader_id);
	glDeleteShader(fragment_shader_id);

	float vertices[] = {-0.5f, -0.5f, -0.5f, 0.0f, 0.0f, 0.5f,  -0.5f, -0.5f, 1.0f, 0.0f,
	                    0.5f,  0.5f,  -0.5f, 1.0f, 1.0f, 0.5f,  0.5f,  -0.5f, 1.0f, 1.0f,
	                    -0.5f, 0.5f,  -0.5f, 0.0f, 1.0f, -0.5f, -0.5f, -0.5f, 0.0f, 0.0f,

	                    -0.5f, -0.5f, 0.5f,  0.0f, 0.0f, 0.5f,  -0.5f, 0.5f,  1.0f, 0.0f,
	                    0.5f,  0.5f,  0.5f,  1.0f, 1.0f, 0.5f,  0.5f,  0.5f,  1.0f, 1.0f,
	                    -0.5f, 0.5f,  0.5f,  0.0f, 1.0f, -0.5f, -0.5f, 0.5f,  0.0f, 0.0f,

	                    -0.5f, 0.5f,  0.5f,  1.0f, 0.0f, -0.5f, 0.5f,  -0.5f, 1.0f, 1.0f,
	                    -0.5f, -0.5f, -0.5f, 0.0f, 1.0f, -0.5f, -0.5f, -0.5f, 0.0f, 1.0f,
	                    -0.5f, -0.5f, 0.5f,  0.0f, 0.0f, -0.5f, 0.5f,  0.5f,  1.0f, 0.0f,

	                    0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 0.5f,  0.5f,  -0.5f, 1.0f, 1.0f,
	                    0.5f,  -0.5f, -0.5f, 0.0f, 1.0f, 0.5f,  -0.5f, -0.5f, 0.0f, 1.0f,
	                    0.5f,  -0.5f, 0.5f,  0.0f, 0.0f, 0.5f,  0.5f,  0.5f,  1.0f, 0.0f,

	                    -0.5f, -0.5f, -0.5f, 0.0f, 1.0f, 0.5f,  -0.5f, -0.5f, 1.0f, 1.0f,
	                    0.5f,  -0.5f, 0.5f,  1.0f, 0.0f, 0.5f,  -0.5f, 0.5f,  1.0f, 0.0f,
	                    -0.5f, -0.5f, 0.5f,  0.0f, 0.0f, -0.5f, -0.5f, -0.5f, 0.0f, 1.0f,

	                    -0.5f, 0.5f,  -0.5f, 0.0f, 1.0f, 0.5f,  0.5f,  -0.5f, 1.0f, 1.0f,
	                    0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
	                    -0.5f, 0.5f,  0.5f,  0.0f, 0.0f, -0.5f, 0.5f,  -0.5f, 0.0f, 1.0f};

	GLuint VBOs[1];
	glGenBuffers(1, VBOs);

	glGenVertexArrays(1, &gl_renderer->VAO);

	glBindVertexArray(gl_renderer->VAO);
	glBindBuffer(GL_ARRAY_BUFFER, VBOs[0]);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);

	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
	glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(5);

	glEnable(GL_DEPTH_TEST);

	gl_renderer->modelLoc = glGetUniformLocation(gl_renderer->shader_program_id, "model");
	gl_renderer->colorLoc = glGetUniformLocation(gl_renderer->shader_program_id, "uniformColor");
	gl_renderer->viewLoc = glGetUniformLocation(gl_renderer->shader_program_id, "view");
	gl_renderer->projLoc = glGetUniformLocation(gl_renderer->shader_program_id, "proj");
	gl_renderer->busyLoopsLoc = glGetUniformLocation(gl_renderer->shader_program_id, "busyLoops");

	return 0;
}

static void
render_block(XrVector3f* position, XrQuaternionf* orientation, XrVector3f* radi, int modelLoc)
{
	XrMatrix4x4f model_matrix;
	XrMatrix4x4f_CreateModelMatrix(&model_matrix, position, orientation, radi);
	glUniformMatrix4fv(modelLoc, 1, GL_FALSE, (float*)model_matrix.m);
	glDrawArrays(GL_TRIANGLES, 0, 36);
}

static void
render_cube(XrVector3f* position, XrQuaternionf* orientation, float cube_size, int modelLoc)
{
	XrVector3f s = {cube_size, cube_size, cube_size};
	render_block(position, orientation, &s, modelLoc);
}

static void
render_simple_cube(HMM_Vec3 position, HMM_Vec3 cube_size, int modelLoc)
{
	HMM_Mat4 modelmatrix =
	    HMM_MulM4(HMM_Translate(position), HMM_Scale(HMM_V3(cube_size.X, cube_size.Y, cube_size.Z)));

	glUniformMatrix4fv(modelLoc, 1, GL_FALSE, (float*)modelmatrix.Elements);
	glDrawArrays(GL_TRIANGLES, 0, 36);
}



static float
vec3_mag(XrVector3f* vec)
{
	return sqrt(vec->x * vec->x + vec->y * vec->y + vec->z * vec->z);
}

static XrVector3f
vec3_norm(XrVector3f* vec)
{
	float mag = vec3_mag(vec);
	XrVector3f r = {.x = vec->x / mag, .y = vec->y / mag, .z = vec->z / mag};
	return r;
}

static HMM_Mat4
m4_dir_to_matrix(HMM_Vec3 dir)
{
	HMM_Vec3 z_axis = HMM_V3(0, 0, -1);
	HMM_Vec3 rot_axis = HMM_Cross(z_axis, dir);
	float angle = acos(HMM_DotV3(z_axis, dir));
	return HMM_Rotate_RH(angle, rot_axis);
}

static void
render_vec(struct ApplicationState* app, XrVector3f* vec, XrVector3f* start)
{
	float width = 0.005;
	float lin_len = vec3_mag(vec);

	XrVector3f lin_direction = vec3_norm(vec);

	HMM_Mat4 m = HMM_M4D(1.0f);
	m = HMM_MulM4(m, HMM_Translate(HMM_V3(0, 0, -lin_len / 2.)));
	m = HMM_MulM4(m, HMM_Scale(HMM_V3(width, width, lin_len)));
	m = HMM_MulM4(m4_dir_to_matrix(HMM_V3(lin_direction.x, lin_direction.y, lin_direction.z)), m);
	m = HMM_MulM4(HMM_Translate(HMM_V3(start->x, start->y, start->z)), m);

	glUniformMatrix4fv(app->gl_renderer.modelLoc, 1, GL_FALSE, (float*)m.Elements);
	glDrawArrays(GL_TRIANGLES, 0, 36);
}

static void
render_line(struct ApplicationState* app, XrVector3f* v1, XrVector3f* v2)
{
	XrVector3f v1tov2 = {v2->x - v1->x, v2->y - v1->y, v2->z - v1->z};
	render_vec(app, &v1tov2, v1);
}

void
render_rotated_cube(HMM_Vec3 position, float cube_size, float rotation, int modelLoc)
{
	HMM_Mat4 rotationmatrix = HMM_Rotate_RH(degrees_to_radians(rotation), HMM_V3(0.0f, 1.0f, 0.0f));
	HMM_Mat4 modelmatrix = HMM_MulM4(
	    HMM_Translate(position), HMM_Scale(HMM_V3(cube_size / 2., cube_size / 2., cube_size / 2.)));
	modelmatrix = HMM_MulM4(modelmatrix, rotationmatrix);

	glUniformMatrix4fv(modelLoc, 1, GL_FALSE, (float*)modelmatrix.Elements);
	glDrawArrays(GL_TRIANGLES, 0, 36);
}

static void
visualize_velocity(XrPosef* base,
                   XrVector3f* linearVelocity,
                   XrVector3f* angularVelocity,
                   int modelLoc,
                   float size)
{
	float cube_radius = size / 2;
	float lin_len = vec3_mag(linearVelocity);
	float block_radius = lin_len / 2.;
	XrVector3f lin_direction = vec3_norm(linearVelocity);

#if 0
	for (float i = 0; i < lin_len / size; i++) {
		XrVector3f pos = base->position;
		pos.x += lin_direction.x * size * i;
		pos.y += lin_direction.y * size * i;
		pos.z += lin_direction.z * size * i;
		render_cube(&pos, &identity, cube_radius, modelLoc);
}
#endif


// linear velocity
#if 1
	{
		/* create matrix that translates lin_len / 2. in lin_direction (because
		 * block origin is in the middle), scales to lin_len in "z" direction and
		 * rotates in lin_direction. Used as model matrix for unit cube this renders
		 * a block of length lin_len in lin_direction starting at the base pose.
		 */
		HMM_Vec3 from = HMM_V3(base->position.x + lin_direction.x * block_radius / 2.,
		                       base->position.y + lin_direction.y * block_radius / 2.,
		                       base->position.z + lin_direction.z * block_radius / 2.);
		HMM_Vec3 to = HMM_V3(base->position.x + lin_direction.x, base->position.y + lin_direction.y,
		                     base->position.z + lin_direction.z);
		HMM_Mat4 look_at = HMM_InvGeneralM4(HMM_LookAt_RH(from, to, HMM_V3(0, 1, 0)));

		HMM_Mat4 scale = HMM_Scale(HMM_V3(cube_radius, cube_radius, block_radius));
		look_at = HMM_MulM4(look_at, scale);

		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, (float*)look_at.Elements);
		glDrawArrays(GL_TRIANGLES, 0, 36);
	}
#endif

// angular velocity - block is axis, length is velocity
#if 0
{
HMM_Vec3 from = HMM_V3(0, 0, 0);
HMM_Vec3 to = HMM_V3(angularVelocity->x / 2., angularVelocity->y / 2., angularVelocity->z / 2.0);
HMM_Mat4 look_at = HMM_InvGeneralM4(HMM_LookAt_RH(from, to, HMM_V3(0, 1, 0)));

float ang_len = vec3_mag(angularVelocity);
HMM_Mat4 scale = HMM_Scale(HMM_V3(cube_radius, cube_radius, ang_len / 2.));
look_at = HMM_MulM4(look_at, scale);

HMM_Vec3 pos = HMM_V3(base->position.x, base->position.y, base->position.z);
HMM_Mat4 model = HMM_MulM4(HMM_Translate(pos), look_at);
glUniformMatrix4fv(modelLoc, 1, GL_FALSE, (float*)model.Elements);
glDrawArrays(GL_TRIANGLES, 0, 36);
}
#endif
}

void
render_frame(struct ApplicationState* app,
             struct gl_renderer_t* gl_renderer,
             XrTime predictedDisplayTime,
             XrSpaceLocation* hand_locations,
             struct hand_tracking_t* hand_tracking,
             bool depth_supported)
{
	for (uint32_t view_index = 0; view_index < app->oxr.view_count; view_index++) {
		GLuint color_image = app->vr_swapchains[SWAPCHAIN_PROJECTION]
		                         .images[view_index][app->acquired_color[view_index]]
		                         .image;
		struct depth_t* depth_ext =
		    (struct depth_t*)get_ext(app, XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME);
		GLuint depth_image = depth_ext->base.supported
		                         ? app->vr_swapchains[SWAPCHAIN_DEPTH]
		                               .images[view_index][app->acquired_depth[view_index]]
		                               .image
		                         : 0;

		GLuint framebuffer = gl_renderer->framebuffers[view_index][app->acquired_color[view_index]];
		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

		uint32_t w = app->oxr.viewconfig_views[view_index].recommendedImageRectWidth;
		uint32_t h = app->oxr.viewconfig_views[view_index].recommendedImageRectHeight;

		glViewport(0, 0, w, h);
		glScissor(0, 0, w, h);

		// should be fast noop after the first time
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_image, 0);
		if (depth_supported) {
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_image, 0);
		} else {
			// TODO: need a depth attachment for depth test when rendering to fbo
		}

		glClearColor(.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glUseProgram(gl_renderer->shader_program_id);
		glBindVertexArray(gl_renderer->VAO);

		if (app->args.busy_loops > 0) {
			// printf("Setting busyLoops to %d\n", app->busy_loops);
			glUniform1i(app->gl_renderer.busyLoopsLoc, app->args.busy_loops);
		}

		XrView* view = &app->oxr.views[view_index];

		XrMatrix4x4f projection_matrix;
		XrMatrix4x4f_CreateProjectionFov(&projection_matrix, GRAPHICS_OPENGL, view->fov,
		                                 gl_renderer->near_z, gl_renderer->far_z);

		XrMatrix4x4f view_matrix;
		XrMatrix4x4f_CreateViewMatrix(&view_matrix, &view->pose.position, &view->pose.orientation);


		glUniformMatrix4fv(gl_renderer->viewLoc, 1, GL_FALSE, (float*)view_matrix.m);
		glUniformMatrix4fv(gl_renderer->projLoc, 1, GL_FALSE, (float*)projection_matrix.m);


		if (app->args.pose_test) {
			glUniform4f(gl_renderer->colorLoc, 0.f, 1.0f, 0.f, 1.0);

			XrVector3f scale = {.x = 1.f, .y = 1.f, .z = 1.f};

			XrMatrix4x4f left_view_model;
			XrMatrix4x4f_CreateModelMatrix(&left_view_model, &app->oxr.views[0].pose.position,
			                               &app->oxr.views[0].pose.orientation, &scale);

			XrMatrix4x4f z_4;
			XrMatrix4x4f_CreateTranslation(&z_4, 0, 0, -4);

			XrMatrix4x4f left_view_z_4;
			XrMatrix4x4f_Multiply(&left_view_z_4, &left_view_model, &z_4);

			XrMatrix4x4f cube = left_view_z_4;

			glUniformMatrix4fv(gl_renderer->modelLoc, 1, GL_FALSE, (float*)cube.m);
			glDrawArrays(GL_TRIANGLES, 0, 36);

			continue;
		}


		if (app->args.render_floor) {
			// render floor at y = 0
			glUniform4f(gl_renderer->colorLoc, 0.25, 0.25, 0.25, 1.0);
			render_simple_cube(HMM_V3(0, 0, 0), HMM_V3(1, 0.001, 1), gl_renderer->modelLoc);
		}


		// render scene with 4 colorful cubes
		{
			// the special color value (0, 0, 0) will get replaced by some UV color in the shader
			glUniform4f(gl_renderer->colorLoc, 0.0, 0.0, 0.0, 0.0);

			double display_time_seconds = ((double)predictedDisplayTime) / (1000. * 1000. * 1000.);
			const float rotations_per_sec = .25;
			float angle = ((long)(display_time_seconds * 360. * rotations_per_sec)) % 360;

			float dist = 1.5f;
			float height = 0.5f;
			render_rotated_cube(HMM_V3(0, height, -dist), .33f, angle, gl_renderer->modelLoc);
			render_rotated_cube(HMM_V3(0, height, dist), .33f, angle, gl_renderer->modelLoc);
			render_rotated_cube(HMM_V3(dist, height, 0), .33f, angle, gl_renderer->modelLoc);
			render_rotated_cube(HMM_V3(-dist, height, 0), .33f, angle, gl_renderer->modelLoc);
		}

		// render controllers / hand joints
		for (int hand = 0; hand < 2; hand++) {
			if (hand == 0) {
				glUniform4f(gl_renderer->colorLoc, 1.0, 0.5, 0.5, 1.0);
			} else {
				glUniform4f(gl_renderer->colorLoc, 0.5, 1.0, 0.5, 1.0);
			}

			if (hand_tracking && hand_tracking->base.supported && hand_tracking->system_supported) {
				struct XrHandJointLocationsEXT* joint_locations = &hand_tracking->joint_locations[hand];
				if (joint_locations->isActive) {
					for (uint32_t i = 0; i < joint_locations->jointCount; i++) {
						struct XrHandJointLocationEXT* joint_location = &joint_locations->jointLocations[i];

						if (!(joint_location->locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
							// printf("Hand %d Joint %d: Position not valid\n", hand, i);
							continue;
						}

						float size = joint_location->radius;
						render_cube(&joint_location->pose.position, &joint_location->pose.orientation, size,
						            gl_renderer->modelLoc);

						if (joint_locations->next != NULL) {
							// we set .next only to null or XrHandJointVelocitiesEXT in main
							XrHandJointVelocitiesEXT* vel = (XrHandJointVelocitiesEXT*)joint_locations->next;
							if ((vel->jointVelocities[i].velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) !=
							    0) {
								visualize_velocity(&joint_location->pose, &vel->jointVelocities[i].linearVelocity,
								                   &vel->jointVelocities[i].angularVelocity, gl_renderer->modelLoc,
								                   0.005);
							} else {
								printf("Joint velocities %d invalid\n", i);
							}
						}
					}
				}
			}

			bool hand_location_valid =
			    app->hand_pose_action.states[hand].pose_.isActive &&
			    //(spaceLocation[hand].locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
			    (hand_locations[hand].locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
			bool aim_location_valid =
			    app->aim_action.states[hand].pose_.isActive &&
			    //(spaceLocation[hand].locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
			    (app->aim_action.pose_locations[hand].locationFlags &
			     XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;

			if (hand_location_valid) {
				XrVector3f scale = {.x = .02f, .y = .02f, .z = .2f};
				render_block(&hand_locations[hand].pose.position, &hand_locations[hand].pose.orientation,
				             &scale, gl_renderer->modelLoc);
			}

			if (aim_location_valid) {
				// always draw an aim pose
				XrMatrix4x4f aim_model_matrix;
				XrVector3f aim_model_scale = {.x = 1, .y = 1, .z = 1};
				XrPosef* aim_pose = &app->aim_action.pose_locations[hand].pose;
				XrMatrix4x4f_CreateModelMatrix(&aim_model_matrix, &aim_pose->position,
				                               &aim_pose->orientation, &aim_model_scale);

				XrMatrix4x4f zminus1_matrix;
				XrMatrix4x4f_CreateTranslation(&zminus1_matrix, 0, 0, -1);

				XrMatrix4x4f aim_zminus1;
				XrMatrix4x4f_Multiply(&aim_zminus1, &aim_model_matrix, &zminus1_matrix);

				XrVector3f aim_vec = {
				    .x = aim_zminus1.m[12], .y = aim_zminus1.m[13], .z = aim_zminus1.m[14]};
				glUniform4f(gl_renderer->colorLoc, 1.0, 0.0, 0.0, 1.0);
				render_line(app, &aim_pose->position, &aim_vec);
				// render_simple_cube(HMM_V3(aim_vec.x, aim_vec.y, aim_vec.z), HMM_V3(0.1, 0.1, 0.1),
				// app->gl_renderer.modelLoc);
			} else if (hand_location_valid && !aim_location_valid) {
				printf("Hand location %d valid but not aim location\n", hand);
			}



			// controller velocities are always drawn if available
			if (hand_locations[hand].next != NULL) {
				// we set .next only to null or XrSpaceVelocity in main
				XrSpaceVelocity* vel = (XrSpaceVelocity*)hand_locations[hand].next;
				if ((vel->velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) != 0) {
					visualize_velocity(&hand_locations[hand].pose, &vel->linearVelocity,
					                   &vel->angularVelocity, gl_renderer->modelLoc, 0.005);
				}
			}
		}

		struct xdev_space_t* xdev_ext =
		    (struct xdev_space_t*)get_ext(app, XR_MNDX_XDEV_SPACE_EXTENSION_NAME);
		if (app->args.xdev_space && xdev_ext && xdev_ext->base.supported &&
		    xdev_ext->system_supported) {
			glUniform4f(gl_renderer->colorLoc, 1.0, 1.0, 0.5, 1.0);

			for (struct xdev_space_element* element = xdev_ext->xdev_space_list; element;
			     element = element->next) {
				if (element->space != XR_NULL_HANDLE) {
					XrSpaceLocation l = {.type = XR_TYPE_SPACE_LOCATION};
					xrLocateSpace(element->space, app->oxr.play_space,
					              app->oxr.frameState.predictedDisplayTime, &l);

					XrVector3f scale = {.x = .05f, .y = .05f, .z = .05f};
					bool l_valid =
					    //(l.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
					    (l.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;

					char* name_hmd = strstr(element->xprops.name, "HMD");
					char* serial_hmd = strstr(element->xprops.serial, "HMD");

					/*
					printf("%s [%s] l valid %d %f %f %f\n", element->xprops.name, element->xprops.serial,
					       l_valid, l.pose.position.x, l.pose.position.y, l.pose.position.z);
					*/

					// skip rendering for everything that has HMD in the name to avoid blocking the view.
					// TODO: devices are not guaranted to have HMD in the name. Perhaps don't render cubes
					// very close to view space?
					if (name_hmd == NULL && serial_hmd == NULL && l_valid) {
						render_block(&l.pose.position, &l.pose.orientation, &scale, gl_renderer->modelLoc);
					}
				}
			}
		}

		if (app->cube.enabled) {
			if (app->cube.pos_ts != 0) {

				glUniform4f(app->gl_renderer.colorLoc, 1, 1, 1, 1.0);
				render_simple_cube(
				    HMM_V3(app->cube.current_pos.x, app->cube.current_pos.y, app->cube.current_pos.z),
				    HMM_V3(0.1, 0.1, 0.1), app->gl_renderer.modelLoc);
			}
		}

		// blit left eye to desktop window
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		if (view_index == 0) {
			glBlitNamedFramebuffer((GLuint)framebuffer,             // readFramebuffer
			                       (GLuint)0,                       // backbuffer     // drawFramebuffer
			                       (GLint)0,                        // srcX0
			                       (GLint)0,                        // srcY0
			                       (GLint)w,                        // srcX1
			                       (GLint)h,                        // srcY1
			                       (GLint)0,                        // dstX0
			                       (GLint)0,                        // dstY0
			                       (GLint)w / 2,                    // dstX1
			                       (GLint)h / 2,                    // dstY1
			                       (GLbitfield)GL_COLOR_BUFFER_BIT, // mask
			                       (GLenum)GL_LINEAR);              // filter

			SDL_GL_SwapWindow(desktop_window);
		}
	}
}

void
initialze_quad(struct gl_renderer_t* gl_renderer, struct quad_layer_t* quad)
{
	glGenTextures(1, &gl_renderer->quad.texture);

	glActiveTextureARB(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, gl_renderer->quad.texture);

	int w = quad->pixel_width;
	int h = quad->pixel_height;
	glViewport(0, 0, w, h);
	glScissor(0, 0, w, h);

	uint8_t* rgb = static_cast<uint8_t*>(malloc(sizeof(uint8_t) * w * h * 4));
	for (int row = 0; row < h; row++) {
		for (int col = 0; col < w; col++) {
			uint8_t* base = &rgb[(row * w * 4 + col * 4)];
			*(base + 0) = (((float)row / (float)h)) * 255.;
			*(base + 1) = 0;
			*(base + 2) = 0;
			*(base + 3) = 255;

			if (abs(row - col) < 3) {
				*(base + 0) = 255.;
				*(base + 1) = 255;
				*(base + 2) = 255;
				*(base + 3) = 255;
			}

			if (abs((w - col) - (row)) < 3) {
				*(base + 0) = 0.;
				*(base + 1) = 0;
				*(base + 2) = 0;
				*(base + 3) = 255;
			}
		}
	}

	// TODO respect format
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
	             (GLvoid*)rgb);
	free(rgb);

	glGenFramebuffers(1, &gl_renderer->quad.fbo);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, gl_renderer->quad.fbo);
	glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
	                       gl_renderer->quad.texture, 0);

	gl_renderer->quad.initialized = true;
}

void
render_quad(struct gl_renderer_t* gl_renderer,
            struct quad_layer_t* quad,
            uint32_t swapchain_index,
            XrTime predictedDisplayTime)
{
	if (!gl_renderer->quad.initialized) {
		printf("Creating Quad texture\n");
		initialze_quad(gl_renderer, quad);
	}

	glBindFramebuffer(GL_READ_FRAMEBUFFER, gl_renderer->quad.fbo);

	GLuint texture = quad->swapchain.images[0][swapchain_index].image;
	glBindTexture(GL_TEXTURE_2D, texture);
	glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, quad->pixel_width, quad->pixel_height);

	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}
