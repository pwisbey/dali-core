#ifndef DALI_VR_ENGINE_H
#define DALI_VR_ENGINE_H

#include <dali/public-api/math/quaternion.h>

#include <stdint.h>

namespace Dali
{
class EglInterface;

namespace Integration
{

// using struct so in case of a need to pass more args only structure
// has to be modified.
namespace Vr
{
struct VrEngineInitParams
{
  // screen settings
  int           screenWidth;
  int           screenHeight;

  // eye settings
  uint32_t*     fbos; // 2 values
  uint32_t*     colorTextures;
  uint32_t*     depthTextures; // may be null
};

struct VrEngineEyePose
{
  Quaternion rotation;
  double timestamp;
};

struct VrEngineRenderTargetInfo
{
  int fbos[2];
  int colorTextures[2]; // if -1, then engine will create those buffers
  int depthTextures[2];
};
} // Anonymous namespace

/**
 * @brief The VrEngine class
 */
class VrEngine
{
public:

  enum EYE_ENUM
  {
    LEFT = 0,
    RIGHT = 1
  };

  enum VR_PROPERTY_ENUM
  {
    CURRENT_POSE_QUATERNION,        // outputs float[4], rw
    EYE_BUFFER_COUNT,               // int, number of buffers ( 1 buffer for both eyes ), rw

    // render target by data structure
    EYE_RENDER_TARGETS,             // [VrEngineRenderTargetInfo*]

    EYE_BUFFER_WIDTH,               // [int] width of single eye renderable area
    EYE_BUFFER_HEIGHT,              // [int] height of single eye renderable area

    EYE_CURRENT_POSE,               // [ structure VrEngineEyePose ]

    /**
     * Framebuffers per eye getter properties
     */
    EYE_LEFT_CURRENT_TEXTURE_ID,    // [int] ro, texture id to be written to ( updated with frame index )
    EYE_LEFT_TEXTURE_ID = 1000,     // [int] rw, left eye texture id for buffer ( must be passed as EYE_LEFT_TEXTURE_ID+N )
    // helper enums
    EYE_LEFT_TEXTURE0_ID = EYE_LEFT_TEXTURE_ID,
    EYE_LEFT_TEXTURE1_ID = EYE_LEFT_TEXTURE_ID+1,
    EYE_LEFT_TEXTURE2_ID = EYE_LEFT_TEXTURE_ID+2,
    EYE_LEFT_TEXTURE3_ID = EYE_LEFT_TEXTURE_ID+3,

    EYE_RIGHT_CURRENT_TEXTURE_ID,    // [int] ro, texture id to be written to ( updated with frame index )
    EYE_RIGHT_TEXTURE_ID = 1065,     // [int] rw, right eye texture id for buffer ( must be passed as EYE_RIGHT_TEXTURE_ID+N )
    // helper enums
    EYE_RIGHT_TEXTURE0_ID = EYE_RIGHT_TEXTURE_ID,
    EYE_RIGHT_TEXTURE1_ID = EYE_RIGHT_TEXTURE_ID+1,
    EYE_RIGHT_TEXTURE2_ID = EYE_RIGHT_TEXTURE_ID+2,
    EYE_RIGHT_TEXTURE3_ID = EYE_RIGHT_TEXTURE_ID+3,

    EYE_LEFT_CURRENT_FBO_ID = 1999,
    EYE_LEFT_FBO_ID = 2000, // [int] left eye fbo for buffer (EYE_LEFT_FBO_ID+N)
    EYE_RIGHT_CURRENT_FBO_ID = 2099,
    EYE_RIGHT_FBO_ID = 2100,// [int] right eye fbo for buffer (EYE_RIGHT_FBO_ID+N)

    EYE_LEFT_DEPTH_TEXTURE_ID = 3000, // [int] left eye depth texture for buffer (EYE_LEFT_FBO_ID+N)
    EYE_RIGHT_DEPTH_TEXTURE_ID = 3064,// [int] right eye depth texture for buffer (EYE_RIGHT_FBO_ID+N)
  };

  VrEngine()
  {
  }
  virtual ~VrEngine() {}

  virtual bool Initialize( Vr::VrEngineInitParams* initParams ) = 0;
  virtual void Start() = 0;
  virtual void Stop() = 0;
  virtual void PreRender() = 0;
  virtual void PostRender() = 0;
  virtual void SubmitFrame() = 0;

  // client must perform output validation if necessary
  virtual bool Get( const int property, void* output, int count ) = 0;
  virtual bool Set( const int property, const void* input, int count ) = 0;

  // helper functions
  bool Get( int property, void* output )
  {
    return Get( property, output, 1 );
  }

  bool Set( int property, void* input )
  {
    return Set( property, input, 1 );
  }

  inline bool Get( const int* properties, void** outputs, int count )
  {
    // fill output array
    for( int i = 0; i < count; ++i )
    {
      Get( properties[i], outputs[i], 1 );
    }
    return true;
  }

  inline bool Set( const int* properties, const void** inputs, int count )
  {
    // fill output array
    for( int i = 0; i < count; ++i )
    {
      return Set( properties[i], inputs[i], 1 );
    }
    return true;
  }

private:

};

} // Integration

} // Dali

#endif
