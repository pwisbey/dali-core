/*
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

// CLASS HEADER
#include <dali/internal/render/common/render-manager.h>

// INTERNAL INCLUDES
#include <dali/public-api/actors/sampling.h>
#include <dali/public-api/common/dali-common.h>
#include <dali/public-api/common/stage.h>
#include <dali/public-api/render-tasks/render-task.h>
#include <dali/integration-api/debug.h>
#include <dali/integration-api/core.h>
#include <dali/internal/common/owner-pointer.h>
#include <dali/internal/render/common/render-algorithms.h>
#include <dali/internal/render/common/render-debug.h>
#include <dali/internal/render/common/render-tracker.h>
#include <dali/internal/render/common/render-instruction-container.h>
#include <dali/internal/render/common/render-instruction.h>
#include <dali/internal/render/data-providers/uniform-name-cache.h>
#include <dali/internal/render/gl-resources/context.h>
#include <dali/internal/render/gl-resources/frame-buffer-texture.h>
#include <dali/internal/render/gl-resources/texture-cache.h>
#include <dali/internal/render/queue/render-queue.h>
#include <dali/internal/render/renderers/render-frame-buffer.h>
#include <dali/internal/render/renderers/render-geometry.h>
#include <dali/internal/render/renderers/render-renderer.h>
#include <dali/internal/render/renderers/render-sampler.h>
#include <dali/internal/render/shaders/program-controller.h>

#include <dali/internal/render/common/renderer-vr.h>

#include <cstdio>
namespace Dali
{

namespace Internal
{

namespace SceneGraph
{

typedef OwnerContainer< Render::Renderer* >    RendererOwnerContainer;
typedef RendererOwnerContainer::Iterator       RendererOwnerIter;

typedef OwnerContainer< Render::Geometry* >    GeometryOwnerContainer;
typedef GeometryOwnerContainer::Iterator       GeometryOwnerIter;

typedef OwnerContainer< Render::Sampler* >    SamplerOwnerContainer;
typedef SamplerOwnerContainer::Iterator       SamplerOwnerIter;

typedef OwnerContainer< Render::NewTexture* >   TextureOwnerContainer;
typedef TextureOwnerContainer::Iterator         TextureOwnerIter;

typedef OwnerContainer< Render::FrameBuffer* >  FrameBufferOwnerContainer;
typedef FrameBufferOwnerContainer::Iterator     FrameBufferOwnerIter;

typedef OwnerContainer< Render::PropertyBuffer* > PropertyBufferOwnerContainer;
typedef PropertyBufferOwnerContainer::Iterator    PropertyBufferOwnerIter;

typedef OwnerContainer< Render::RenderTracker* > RenderTrackerContainer;
typedef RenderTrackerContainer::Iterator         RenderTrackerIter;
typedef RenderTrackerContainer::ConstIterator    RenderTrackerConstIter;

/**
 * Structure to contain VR rendering data
 */
struct VrImpl
{
  VrImpl()
  : mainFrameBufferAttachments( ),
    mainFrameBuffer( 0 ),
    mainVRProgramGL( 0 ),
    vrModeEnabled( true )
  {
  }

  enum VrUniform
  {
    VR_UNIFORM_TEXTURE = 0,
    VR_UNIFORM_MVP = 1,
    VR_UNIFORM_MAX
  };

  struct Vertex
  {
    float aPosition[3];
    float aTexCoord[2];
  };

  // in case of VR rendering to texture first then applying barrel distortion
  unsigned int                  mainFrameBufferAttachments[2];
  unsigned int                  mainFrameBuffer;
  unsigned int                  mainVRProgramGL;
  unsigned int                  vertexBuffer; // contains 'cage' data, vec3, vec2
  unsigned int                  indexBuffer; // contains indices
  unsigned int                  indicesCount;
  Matrix                        MVP;
  int                           uniformLocations[VR_UNIFORM_MAX];

  bool                          vrModeEnabled;
};

/**
 * Structure to contain internal data
 */
struct RenderManager::Impl
{
  Impl( Integration::GlAbstraction& glAbstraction,
        Integration::GlSyncAbstraction& glSyncAbstraction,
        LockedResourceQueue& textureUploadedQ,
        TextureUploadedDispatcher& postProcessDispatcher,
        GeometryBatcher& geometryBatcher )
  : context( glAbstraction ),
    glSyncAbstraction( glSyncAbstraction ),
    renderQueue(),
    textureCache( renderQueue, postProcessDispatcher, context ),
    textureUploadedQueue( textureUploadedQ ),
    instructions(),
    backgroundColor( Dali::Stage::DEFAULT_BACKGROUND_COLOR ),
    frameCount( 0 ),
    renderBufferIndex( SceneGraphBuffers::INITIAL_UPDATE_BUFFER_INDEX ),
    defaultSurfaceRect(),
    rendererContainer(),
    samplerContainer(),
    textureContainer(),
    frameBufferContainer(),
    renderersAdded( false ),
    firstRenderCompleted( false ),
    defaultShader( NULL ),
    programController( glAbstraction ),
    geometryBatcher( geometryBatcher )
  {
  }

  ~Impl()
  {
  }

  void AddRenderTracker( Render::RenderTracker* renderTracker )
  {
    DALI_ASSERT_DEBUG( renderTracker != NULL );
    mRenderTrackers.PushBack( renderTracker );
  }

  void RemoveRenderTracker( Render::RenderTracker* renderTracker )
  {
    DALI_ASSERT_DEBUG( renderTracker != NULL );
    for(RenderTrackerIter iter = mRenderTrackers.Begin(), end = mRenderTrackers.End(); iter != end; ++iter)
    {
      if( *iter == renderTracker )
      {
        mRenderTrackers.Erase( iter );
        break;
      }
    }
  }

  void UpdateTrackers()
  {
    for(RenderTrackerIter iter = mRenderTrackers.Begin(), end = mRenderTrackers.End(); iter != end; ++iter)
    {
      (*iter)->PollSyncObject();
    }
  }

  // the order is important for destruction,
  // programs are owned by context at the moment.
  Context                       context;                  ///< holds the GL state
  Integration::GlSyncAbstraction& glSyncAbstraction;      ///< GL sync abstraction
  RenderQueue                   renderQueue;              ///< A message queue for receiving messages from the update-thread.
  TextureCache                  textureCache;             ///< Cache for all GL textures
  Render::UniformNameCache      uniformNameCache;         ///< Cache to provide unique indices for uniforms
  LockedResourceQueue&          textureUploadedQueue;     ///< A queue for requesting resource post processing in update thread

  // Render instructions describe what should be rendered during RenderManager::Render()
  // Owned by RenderManager. Update manager updates instructions for the next frame while we render the current one
  RenderInstructionContainer    instructions;

  Vector4                       backgroundColor;          ///< The glClear color used at the beginning of each frame.

  unsigned int                  frameCount;               ///< The current frame count
  BufferIndex                   renderBufferIndex;        ///< The index of the buffer to read from; this is opposite of the "update" buffer

  Rect<int>                     defaultSurfaceRect;       ///< Rectangle for the default surface we are rendering to

  RendererOwnerContainer        rendererContainer;        ///< List of owned renderers
  SamplerOwnerContainer         samplerContainer;         ///< List of owned samplers
  TextureOwnerContainer         textureContainer;         ///< List of owned textures
  FrameBufferOwnerContainer     frameBufferContainer;     ///< List of owned framebuffers
  PropertyBufferOwnerContainer  propertyBufferContainer;  ///< List of owned property buffers
  GeometryOwnerContainer        geometryContainer;        ///< List of owned Geometries

  bool                          renderersAdded;

  RenderTrackerContainer        mRenderTrackers;          ///< List of render trackers

  bool                          firstRenderCompleted;     ///< False until the first render is done
  Shader*                       defaultShader;            ///< Default shader to use
  ProgramController             programController;        ///< Owner of the GL programs

  SceneGraph::GeometryBatcher&  geometryBatcher;          ///< Instance of geometry batcher

  VrImpl                        vrImpl;
};

RenderManager* RenderManager::New( Integration::GlAbstraction& glAbstraction,
                                   Integration::GlSyncAbstraction& glSyncAbstraction,
                                   SceneGraph::GeometryBatcher& geometryBatcher,
                                   LockedResourceQueue& textureUploadedQ )
{
  RenderManager* manager = new RenderManager;
  manager->mImpl = new Impl( glAbstraction, glSyncAbstraction, textureUploadedQ, *manager, geometryBatcher );
  return manager;
}

RenderManager::RenderManager()
: mImpl(NULL)
{
}

RenderManager::~RenderManager()
{
  for ( TextureOwnerIter iter = mImpl->textureContainer.Begin(); iter != mImpl->textureContainer.End(); ++iter )
  {
    (*iter)->Destroy( mImpl->context );
  }

  for ( FrameBufferOwnerIter iter = mImpl->frameBufferContainer.Begin(); iter != mImpl->frameBufferContainer.End(); ++iter )
  {
    (*iter)->Destroy( mImpl->context );
  }

  delete mImpl;
}

RenderQueue& RenderManager::GetRenderQueue()
{
  return mImpl->renderQueue;
}

TextureCache& RenderManager::GetTextureCache()
{
  return mImpl->textureCache;
}

void RenderManager::ContextCreated()
{
  mImpl->context.GlContextCreated();
  mImpl->programController.GlContextCreated();

  // renderers, textures and gpu buffers cannot reinitialize themselves
  // so they rely on someone reloading the data for them
}

void RenderManager::ContextDestroyed()
{
  mImpl->context.GlContextDestroyed();
  mImpl->programController.GlContextDestroyed();

  // inform texture cache
  mImpl->textureCache.GlContextDestroyed(); // Clears gl texture ids

  // inform renderers
  RendererOwnerContainer::Iterator end = mImpl->rendererContainer.End();
  RendererOwnerContainer::Iterator iter = mImpl->rendererContainer.Begin();
  for( ; iter != end; ++iter )
  {
    GlResourceOwner* renderer = *iter;
    renderer->GlContextDestroyed(); // Clear up vertex buffers
  }
}

void RenderManager::DispatchTextureUploaded(ResourceId request)
{
  mImpl->textureUploadedQueue.PushBack( request );
}

void RenderManager::SetShaderSaver( ShaderSaver& upstream )
{
  mImpl->programController.SetShaderSaver( upstream );
}

RenderInstructionContainer& RenderManager::GetRenderInstructionContainer()
{
  return mImpl->instructions;
}

void RenderManager::SetBackgroundColor( const Vector4& color )
{
  mImpl->backgroundColor = color;
}

void RenderManager::SetDefaultSurfaceRect(const Rect<int>& rect)
{
  mImpl->defaultSurfaceRect = rect;
}

void RenderManager::AddRenderer( Render::Renderer* renderer )
{
  // Initialize the renderer as we are now in render thread
  renderer->Initialize( mImpl->context, mImpl->textureCache, mImpl->uniformNameCache );

  mImpl->rendererContainer.PushBack( renderer );

  if( !mImpl->renderersAdded )
  {
    mImpl->renderersAdded = true;
  }
}

void RenderManager::RemoveRenderer( Render::Renderer* renderer )
{
  DALI_ASSERT_DEBUG( NULL != renderer );

  RendererOwnerContainer& renderers = mImpl->rendererContainer;

  // Find the renderer
  for ( RendererOwnerIter iter = renderers.Begin(); iter != renderers.End(); ++iter )
  {
    if ( *iter == renderer )
    {
      renderers.Erase( iter ); // Renderer found; now destroy it
      break;
    }
  }
}

void RenderManager::AddSampler( Render::Sampler* sampler )
{
  mImpl->samplerContainer.PushBack( sampler );
}

void RenderManager::RemoveSampler( Render::Sampler* sampler )
{
  DALI_ASSERT_DEBUG( NULL != sampler );

  SamplerOwnerContainer& samplers = mImpl->samplerContainer;

  // Find the sampler
  for ( SamplerOwnerIter iter = samplers.Begin(); iter != samplers.End(); ++iter )
  {
    if ( *iter == sampler )
    {
      samplers.Erase( iter ); // Sampler found; now destroy it
      break;
    }
  }
}

void RenderManager::AddTexture( Render::NewTexture* texture )
{
  mImpl->textureContainer.PushBack( texture );
  texture->Initialize(mImpl->context);
}

void RenderManager::RemoveTexture( Render::NewTexture* texture )
{
  DALI_ASSERT_DEBUG( NULL != texture );

  TextureOwnerContainer& textures = mImpl->textureContainer;

  // Find the texture
  for ( TextureOwnerIter iter = textures.Begin(); iter != textures.End(); ++iter )
  {
    if ( *iter == texture )
    {
      texture->Destroy( mImpl->context );
      textures.Erase( iter ); // Texture found; now destroy it
      break;
    }
  }
}

void RenderManager::UploadTexture( Render::NewTexture* texture, PixelDataPtr pixelData, const NewTexture::UploadParams& params )
{
  texture->Upload( mImpl->context, pixelData, params );
}

void RenderManager::GenerateMipmaps( Render::NewTexture* texture )
{
  texture->GenerateMipmaps( mImpl->context );
}

void RenderManager::SetFilterMode( Render::Sampler* sampler, unsigned int minFilterMode, unsigned int magFilterMode )
{
  sampler->mMinificationFilter = static_cast<Dali::FilterMode::Type>(minFilterMode);
  sampler->mMagnificationFilter = static_cast<Dali::FilterMode::Type>(magFilterMode );
}

void RenderManager::SetWrapMode( Render::Sampler* sampler, unsigned int rWrapMode, unsigned int sWrapMode, unsigned int tWrapMode )
{
  sampler->mRWrapMode = static_cast<Dali::WrapMode::Type>(rWrapMode);
  sampler->mSWrapMode = static_cast<Dali::WrapMode::Type>(sWrapMode);
  sampler->mTWrapMode = static_cast<Dali::WrapMode::Type>(tWrapMode);
}

void RenderManager::AddFrameBuffer( Render::FrameBuffer* frameBuffer )
{
  mImpl->frameBufferContainer.PushBack( frameBuffer );
  frameBuffer->Initialize(mImpl->context);
}

void RenderManager::RemoveFrameBuffer( Render::FrameBuffer* frameBuffer )
{
  DALI_ASSERT_DEBUG( NULL != frameBuffer );

  FrameBufferOwnerContainer& framebuffers = mImpl->frameBufferContainer;

  // Find the sampler
  for ( FrameBufferOwnerIter iter = framebuffers.Begin(); iter != framebuffers.End(); ++iter )
  {
    if ( *iter == frameBuffer )
    {
      frameBuffer->Destroy( mImpl->context );
      framebuffers.Erase( iter ); // frameBuffer found; now destroy it
      break;
    }
  }
}

void RenderManager::AttachColorTextureToFrameBuffer( Render::FrameBuffer* frameBuffer, Render::NewTexture* texture, unsigned int mipmapLevel, unsigned int layer )
{
  frameBuffer->AttachColorTexture( mImpl->context, texture, mipmapLevel, layer );
}

void RenderManager::AddPropertyBuffer( Render::PropertyBuffer* propertyBuffer )
{
  mImpl->propertyBufferContainer.PushBack( propertyBuffer );
}

void RenderManager::RemovePropertyBuffer( Render::PropertyBuffer* propertyBuffer )
{
  DALI_ASSERT_DEBUG( NULL != propertyBuffer );

  PropertyBufferOwnerContainer& propertyBuffers = mImpl->propertyBufferContainer;

  // Find the sampler
  for ( PropertyBufferOwnerIter iter = propertyBuffers.Begin(); iter != propertyBuffers.End(); ++iter )
  {
    if ( *iter == propertyBuffer )
    {
      propertyBuffers.Erase( iter ); // Property buffer found; now destroy it
      break;
    }
  }
}

void RenderManager::SetPropertyBufferFormat(Render::PropertyBuffer* propertyBuffer, Render::PropertyBuffer::Format* format )
{
  propertyBuffer->SetFormat( format );
}

void RenderManager::SetPropertyBufferData( Render::PropertyBuffer* propertyBuffer, Dali::Vector<char>* data, size_t size )
{
  propertyBuffer->SetData( data, size );
}

void RenderManager::SetIndexBuffer( Render::Geometry* geometry, Dali::Vector<unsigned short>& indices )
{
  geometry->SetIndexBuffer( indices );
}

void RenderManager::AddGeometry( Render::Geometry* geometry )
{
  mImpl->geometryContainer.PushBack( geometry );
}

void RenderManager::RemoveGeometry( Render::Geometry* geometry )
{
  DALI_ASSERT_DEBUG( NULL != geometry );

  GeometryOwnerContainer& geometries = mImpl->geometryContainer;

  // Find the geometry
  for ( GeometryOwnerIter iter = geometries.Begin(); iter != geometries.End(); ++iter )
  {
    if ( *iter == geometry )
    {
      geometries.Erase( iter ); // Geometry found; now destroy it
      break;
    }
  }
}

void RenderManager::AddVertexBuffer( Render::Geometry* geometry, Render::PropertyBuffer* propertyBuffer )
{
  DALI_ASSERT_DEBUG( NULL != geometry );

  GeometryOwnerContainer& geometries = mImpl->geometryContainer;

  // Find the renderer
  for ( GeometryOwnerIter iter = geometries.Begin(); iter != geometries.End(); ++iter )
  {
    if ( *iter == geometry )
    {
      (*iter)->AddPropertyBuffer( propertyBuffer );
      break;
    }
  }
}

void RenderManager::RemoveVertexBuffer( Render::Geometry* geometry, Render::PropertyBuffer* propertyBuffer )
{
  DALI_ASSERT_DEBUG( NULL != geometry );

  GeometryOwnerContainer& geometries = mImpl->geometryContainer;

  // Find the renderer
  for ( GeometryOwnerIter iter = geometries.Begin(); iter != geometries.End(); ++iter )
  {
    if ( *iter == geometry )
    {
      (*iter)->RemovePropertyBuffer( propertyBuffer );
      break;
    }
  }
}

void RenderManager::SetGeometryType( Render::Geometry* geometry, unsigned int geometryType )
{
  geometry->SetType( Render::Geometry::Type(geometryType) );
}

void RenderManager::AddRenderTracker( Render::RenderTracker* renderTracker )
{
  mImpl->AddRenderTracker(renderTracker);
}

void RenderManager::RemoveRenderTracker( Render::RenderTracker* renderTracker )
{
  mImpl->RemoveRenderTracker(renderTracker);
}

void RenderManager::SetDefaultShader( Shader* shader )
{
  mImpl->defaultShader = shader;
}

ProgramCache* RenderManager::GetProgramCache()
{
  return &(mImpl->programController);
}

#define GL(x) { x; int err = mImpl->context.GetError(); if(err) { DALI_LOG_ERROR( "GL_ERROR: '%s', %x\n", #x, (unsigned)err);fflush(stderr);fflush(stdout);} else { /*DALI_LOG_ERROR("GL Call: %s\n", #x); fflush(stdout);*/} }

bool RenderManager::Render( Integration::RenderStatus& status )
{
  DALI_PRINT_RENDER_START( mImpl->renderBufferIndex );

  // Core::Render documents that GL context must be current before calling Render
  DALI_ASSERT_DEBUG( mImpl->context.IsGlContextCreated() );

  // Increment the frame count at the beginning of each frame
  ++(mImpl->frameCount);

  // Process messages queued during previous update
  mImpl->renderQueue.ProcessMessages( mImpl->renderBufferIndex );

  VrImpl& vr = mImpl->vrImpl;

  // No need to make any gl calls if we've done 1st glClear & don't have any renderers to render during startup.
  if( !mImpl->firstRenderCompleted || mImpl->renderersAdded )
  {
    // check if vr, if true create new main framebuffer, it's not the
    // right place to do it, but will do for now
    if( mImpl->vrImpl.vrModeEnabled )
    {
      if( !vr.mainFrameBuffer )
      {
        SetupVRMode();
      }
    }

    // switch rendering to adaptor provided (default) buffer
    GL( mImpl->context.BindFramebuffer( GL_FRAMEBUFFER, vr.vrModeEnabled ? vr.mainFrameBuffer : 0 ) );

    int err = mImpl->context.GetError();
    err = err;
    mImpl->context.Viewport( mImpl->defaultSurfaceRect.x,
                             mImpl->defaultSurfaceRect.y,
                             mImpl->defaultSurfaceRect.width,
                             mImpl->defaultSurfaceRect.height );

    mImpl->context.ClearColor( mImpl->backgroundColor.r,
                               mImpl->backgroundColor.g,
                               mImpl->backgroundColor.b,
                               mImpl->backgroundColor.a );

    mImpl->context.ClearStencil( 0 );

    // Clear the entire color, depth and stencil buffers for the default framebuffer.
    // It is important to clear all 3 buffers, for performance on deferred renderers like Mali
    // e.g. previously when the depth & stencil buffers were NOT cleared, it caused the DDK to exceed a "vertex count limit",
    // and then stall. That problem is only noticeable when rendering a large number of vertices per frame.
    mImpl->context.SetScissorTest( false );
    mImpl->context.ColorMask( true );
    mImpl->context.DepthMask( true );
    mImpl->context.StencilMask( 0xFF ); // 8 bit stencil mask, all 1's
    mImpl->context.Clear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT,  Context::FORCE_CLEAR );

    // reset the program matrices for all programs once per frame
    // this ensures we will set view and projection matrix once per program per camera
    mImpl->programController.ResetProgramMatrices();

    // if we don't have default shader, no point doing the render calls
    if( mImpl->defaultShader )
    {
      size_t count = mImpl->instructions.Count( mImpl->renderBufferIndex );
      for ( size_t i = 0; i < count; ++i )
      {
        RenderInstruction& instruction = mImpl->instructions.At( mImpl->renderBufferIndex, i );

        DoRender( instruction, *mImpl->defaultShader );
      }
      GLenum attachments[] = { GL_DEPTH, GL_STENCIL };
      mImpl->context.InvalidateFramebuffer(GL_FRAMEBUFFER, 2, attachments);

      mImpl->UpdateTrackers();

      mImpl->firstRenderCompleted = true;
    }

    if( vr.vrModeEnabled )
    {
      RenderVR();
    }


  }

  //Notify RenderGeometries that rendering has finished
  for ( GeometryOwnerIter iter = mImpl->geometryContainer.Begin(); iter != mImpl->geometryContainer.End(); ++iter )
  {
    (*iter)->OnRenderFinished();
  }

  /**
   * The rendering has finished; swap to the next buffer.
   * Ideally the update has just finished using this buffer; otherwise the render thread
   * should block until the update has finished.
   */
  mImpl->renderBufferIndex = (0 != mImpl->renderBufferIndex) ? 0 : 1;

  DALI_PRINT_RENDER_END();

  // check if anything has been posted to the update thread, if IsEmpty then no update required.
  return !mImpl->textureUploadedQueue.IsEmpty();
}

void RenderManager::DoRender( RenderInstruction& instruction, Shader& defaultShader )
{
  Rect<int> viewportRect;
  Vector4   clearColor;

  if ( instruction.mIsClearColorSet )
  {
    clearColor = instruction.mClearColor;
  }
  else
  {
    clearColor = Dali::RenderTask::DEFAULT_CLEAR_COLOR;
  }

  FrameBufferTexture* offscreen = NULL;

  if ( instruction.mOffscreenTextureId != 0 )
  {
    offscreen = mImpl->textureCache.GetFramebuffer( instruction.mOffscreenTextureId );
    DALI_ASSERT_DEBUG( NULL != offscreen );

    if( NULL != offscreen &&
        offscreen->Prepare() )
    {
      // Check whether a viewport is specified, otherwise the full surface size is used
      if ( instruction.mIsViewportSet )
      {
        // For glViewport the lower-left corner is (0,0)
        const int y = ( offscreen->GetHeight() - instruction.mViewport.height ) - instruction.mViewport.y;
        viewportRect.Set( instruction.mViewport.x,  y, instruction.mViewport.width, instruction.mViewport.height );
      }
      else
      {
        viewportRect.Set( 0, 0, offscreen->GetWidth(), offscreen->GetHeight() );
      }
    }
    else
    {
      // Offscreen is NULL or could not be prepared.
      return;
    }
  }
  else if( instruction.mFrameBuffer != 0 )
  {
    instruction.mFrameBuffer->Bind( mImpl->context );
    if ( instruction.mIsViewportSet )
    {
      // For glViewport the lower-left corner is (0,0)
      const int y = ( instruction.mFrameBuffer->GetHeight() - instruction.mViewport.height ) - instruction.mViewport.y;
      viewportRect.Set( instruction.mViewport.x,  y, instruction.mViewport.width, instruction.mViewport.height );
    }
    else
    {
      viewportRect.Set( 0, 0, instruction.mFrameBuffer->GetWidth(), instruction.mFrameBuffer->GetHeight() );
    }
  }
  else // !(instruction.mOffscreenTexture)
  {


    // switch rendering to adaptor provided (default) buffer
    mImpl->context.BindFramebuffer( GL_FRAMEBUFFER, mImpl->vrImpl.vrModeEnabled ? mImpl->vrImpl.mainFrameBuffer : 0 );

    // Check whether a viewport is specified, otherwise the full surface size is used
    if ( instruction.mIsViewportSet )
    {
      // For glViewport the lower-left corner is (0,0)
      const int y = ( mImpl->defaultSurfaceRect.height - instruction.mViewport.height ) - instruction.mViewport.y;
      viewportRect.Set( instruction.mViewport.x,  y, instruction.mViewport.width, instruction.mViewport.height );
    }
    else
    {
      viewportRect = mImpl->defaultSurfaceRect;
    }
  }

  mImpl->context.Viewport(viewportRect.x, viewportRect.y, viewportRect.width, viewportRect.height);

  if ( instruction.mIsClearColorSet )
  {
    mImpl->context.ClearColor( clearColor.r,
                               clearColor.g,
                               clearColor.b,
                               clearColor.a );

    // Clear the viewport area only
    mImpl->context.SetScissorTest( true );
    mImpl->context.Scissor( viewportRect.x, viewportRect.y, viewportRect.width, viewportRect.height );
    mImpl->context.ColorMask( true );
    mImpl->context.Clear( GL_COLOR_BUFFER_BIT , Context::CHECK_CACHED_VALUES );
    mImpl->context.SetScissorTest( false );
  }

  Render::ProcessRenderInstruction( instruction,
                                    mImpl->context,
                                    mImpl->textureCache,
                                    defaultShader,
                                    mImpl->geometryBatcher,
                                    mImpl->renderBufferIndex );

  if(instruction.mOffscreenTextureId != 0)
  {
    GLenum attachments[] = { GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT };
    mImpl->context.InvalidateFramebuffer(GL_FRAMEBUFFER, 2, attachments);
  }

  if( instruction.mRenderTracker && offscreen != NULL )
  {
    // This will create a sync object every frame this render tracker
    // is alive (though it should be now be created only for
    // render-once render tasks)
    instruction.mRenderTracker->CreateSyncObject( mImpl->glSyncAbstraction );
    instruction.mRenderTracker = NULL; // Only create once.
  }
}

















namespace {
inline void Orthographic(Matrix& result, float left, float right, float bottom, float top, float near, float far, bool invertYAxis)
{
  if ( Equals(right, left) || Equals(top, bottom) || Equals(far, near) )
  {
    DALI_LOG_ERROR( "Cannot create orthographic projection matrix with a zero dimension.\n" );
    DALI_ASSERT_DEBUG( "Cannot create orthographic projection matrix with a zero dimension." );
    return;
  }

  float deltaX = right - left;
  float deltaY = invertYAxis ? bottom - top : top - bottom;
  float deltaZ = far - near;

  float *m = result.AsFloat();
  m[0] = -2.0f / deltaX;
  m[1] = 0.0f;
  m[2] = 0.0f;
  m[3] = 0.0f;

  m[4] = 0.0f;
  m[5] = -2.0f / deltaY;
  m[6] = 0.0f;
  m[7] = 0.0f;

  m[8] = 0.0f;
  m[9] = 0.0f;
  m[10] = 2.0f / deltaZ;
  m[11] = 0.0f;
  m[12] = -(right + left) / deltaX;
  m[13] = -(top + bottom) / deltaY;
  m[14] = -(near + far)   / deltaZ;
  m[15] = 1.0f;
}

void LookAt(Matrix& result, const Vector3& eye, const Vector3& target, const Vector3& up)
{
  Vector3 vZ = target - eye;
  vZ.Normalize();

  Vector3 vX = up.Cross(vZ);
  vX.Normalize();

  Vector3 vY = vZ.Cross(vX);
  vY.Normalize();

  result.SetInverseTransformComponents(vX, vY, vZ, eye);
}

}

void RenderManager::SetupVRMode()
{
  VrImpl& vr = mImpl->vrImpl;
  Context& ctx =mImpl->context;
  Integration::GlAbstraction& gl = ctx.GetAbstraction();

  GL( ctx.GenFramebuffers( 1, &vr.mainFrameBuffer ) );
  GL( ctx.GenTextures( 1, &vr.mainFrameBufferAttachments[0] ) );
  GL( ctx.BindFramebuffer( GL_FRAMEBUFFER, vr.mainFrameBuffer ) );

  // color attachment
  GL( ctx.Bind2dTexture( vr.mainFrameBufferAttachments[0] ) );
  GL( ctx.TexImage2D( GL_TEXTURE_2D, 0, GL_RGBA,
                             mImpl->defaultSurfaceRect.width,
                             mImpl->defaultSurfaceRect.height,
                             0, GL_RGBA, GL_UNSIGNED_BYTE, 0 ) );
  GL( ctx.TexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST ) );
  GL( ctx.TexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST ) );
  GL( ctx.FramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, vr.mainFrameBufferAttachments[0], 0 ) );

  // depth/stencil attachment
  // if gles3 render depth to the texture
#if 0
  GL( mImpl->context.GenTextures( 1, &mImpl->mainFrameBufferAttachments[1] ) );
  GL( mImpl->context.Bind2dTexture( mImpl->mainFrameBufferAttachments[1] ) );
  GL( mImpl->context.TexImage2D( GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                             mImpl->defaultSurfaceRect.width,
                             mImpl->defaultSurfaceRect.height,
                             0, GL_DEPTH_COMPONENT, GL_FLOAT, 0 ) );
  GL( mImpl->context.TexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST ) );
  GL( mImpl->context.TexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST ) );
  GL( mImpl->context.FramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, mImpl->mainFrameBufferAttachments[1], 0 ) );
#else
  GL( ctx.GenRenderbuffers( 1, &vr.mainFrameBufferAttachments[1] ) );
  GL( ctx.BindRenderbuffer( GL_RENDERBUFFER, vr.mainFrameBufferAttachments[1] ) );
  GL( ctx.RenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH_COMPONENT16,
                                          mImpl->defaultSurfaceRect.width,
                                          mImpl->defaultSurfaceRect.height ) );
  GL( ctx.FramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, vr.mainFrameBufferAttachments[1] ) );
#endif

  GLenum drawBuffers[1] = { GL_COLOR_ATTACHMENT0 };
  gl.DrawBuffers( 1, drawBuffers );

  // prepare shader and buffer to render barrel distortion in one go ( with time warp it may need to do with 2 goes )
  const char* shaderSource[2] =
  {
    // vertex shader
    "attribute vec3 aPosition;\n"
    "attribute vec2 aTexCoord;\n"
    "uniform highp mat4 mvp;\n"
    "varying mediump vec2  vTexCoord;\n"
    "void main()\n"
    "{"
    "  vec3 pos = aPosition;\n"
    //"  pos.y -= 1.0;\n"
    "  gl_Position = mvp * vec4( pos, 1.0 );\n"
    "  vTexCoord = aTexCoord;\n"
    "}\n\0",

    // fragment shader
    "uniform sampler2D texSampler;\n"
    "\n"
    "varying mediump vec2 vTexCoord;\n"
    "void main()\n"
    "{ \n"
//    "  mediump vec4 mulcol = vec4( 1.0, 0.5, 0.5, 1.0 );\n"
//    "  if(vTexCoord.y < 0.5 ) { mulcol = vec4(0.5, 1.0, 0.5, 1.0); }\n"
//    "  gl_FragColor = texture2D( texSampler, vTexCoord ) * mulcol;\n"
    "  gl_FragColor = texture2D( texSampler, vTexCoord );\n"
    "}\n\0",
  };

  // using GL explicitly in order to create shaders and program
  GLuint shaders[2];
  GLuint program;
  for( int i = 0; i < 2; ++i )
  {
    GL( shaders[ i ] = ctx.CreateShader( i ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER ) );
    GL( ctx.ShaderSource( shaders[i], 1, &shaderSource[i], NULL ) );
    GL( ctx.CompileShader( shaders[i] ) );
  }

  GL( program = mImpl->context.CreateProgram() );
  GL( mImpl->context.AttachShader( program, shaders[0] ) );
  GL( mImpl->context.AttachShader( program, shaders[1] ) );
  GL( mImpl->context.LinkProgram( program) );

  vr.mainVRProgramGL = program;
  //const float w = 1.0f;//(float)mImpl->defaultSurfaceRect.width;
  //const float h = 1.0f;//(float)mImpl->defaultSurfaceRect.height;

  // create vertex buffer
  std::vector<float> vertices;
  std::vector<uint16_t> indices;
  GenerateGridVertexBuffer( vertices );
  GenerateGridIndexBuffer( indices );

  // vertex buffer
  GL( ctx.GenBuffers( 1, &vr.vertexBuffer ) );
  GL( ctx.BindArrayBuffer( vr.vertexBuffer ) );
  GL( ctx.BufferData( GL_ARRAY_BUFFER, vertices.size()*sizeof(float), vertices.data(), GL_STATIC_DRAW ) );

  // index buffer
  GL( ctx.GenBuffers( 1, &vr.indexBuffer ) );
  GL( ctx.BindArrayBuffer( vr.indexBuffer ) );
  GL( ctx.BufferData( GL_ARRAY_BUFFER, indices.size()*sizeof(uint16_t), indices.data(), GL_STATIC_DRAW ) );
  vr.indicesCount = indices.size();

  GL( vr.uniformLocations[ VrImpl::VR_UNIFORM_MVP ] = ctx.GetUniformLocation( program, "mvp" ) );
  GL( vr.uniformLocations[ VrImpl::VR_UNIFORM_TEXTURE ] = ctx.GetUniformLocation( program, "texSampler" ) );

  // mvp
  Matrix proj, view;
  Orthographic( proj, 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, false );
  //LookAt( view, Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 1.0f ), Vector3( 0.0f, 1.0f, 0.0f ) );
  //mat.SetTranslation( Vector3(0.0f, 0.0f, 1.0f ) );
  Matrix::Multiply( vr.MVP, view, proj );
  vr.MVP = proj;

  //vr.MVP.Multiply( vr.MVP, vp, mat );
  return;
}


void RenderManager::RenderVR()
{
  static float f = 1.0f;
  VrImpl& vr = mImpl->vrImpl;
  Context& ctx = mImpl->context;
  Integration::GlAbstraction& gl = ctx.GetAbstraction();
  GL( ctx.BindFramebuffer( GL_FRAMEBUFFER, 0 ) );
  gl.Viewport( 0, 0, mImpl->defaultSurfaceRect.width, mImpl->defaultSurfaceRect.height );
  //gl.Disable( GL_SCISSOR_TEST );
  //gl.Disable( GL_DEPTH_TEST );
  //gl.ClearColor( 0.0f, f, 0.0f, 1.0f );
  f -= 0.2f;
  if( f < 0 )
    f = 1.0f;
  //GL( gl.Clear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT) );
  // store old program
  Program* program = GetProgramCache()->GetCurrentProgram();
  GetProgramCache()->SetCurrentProgram( NULL );
  GL( ctx.UseProgram( vr.mainVRProgramGL ) );

  // attributes
  GL( ctx.BindArrayBuffer( vr.vertexBuffer ) );
  GL( ctx.VertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, sizeof(VrImpl::Vertex), 0 ) );
  GL( ctx.VertexAttribPointer( 1, 2, GL_FLOAT, GL_FALSE, sizeof(VrImpl::Vertex), (const void*)(sizeof(float)*3)) );
  GL( gl.EnableVertexAttribArray( 0 ) );
  GL( gl.EnableVertexAttribArray( 1 ) );

  GL( ctx.BindElementArrayBuffer( vr.indexBuffer ) );

  // uniforms
  // texture
  GL( ctx.ActiveTexture( TEXTURE_UNIT_IMAGE ) );
  GL( ctx.Bind2dTexture( vr.mainFrameBufferAttachments[0] ) );
  GL( gl.Uniform1i( vr.uniformLocations[ VrImpl::VR_UNIFORM_TEXTURE], 0 ) );

  // matrix
  GL( gl.UniformMatrix4fv( vr.uniformLocations[ VrImpl::VR_UNIFORM_MVP ],
      1, GL_FALSE, vr.MVP.AsFloat() ) );

  GL( gl.DrawElements( GL_TRIANGLES, vr.indicesCount, GL_UNSIGNED_SHORT, 0 ) );

  if( program )
    program->Use();

}

} // namespace SceneGraph

} // namespace Internal

} // namespace Dali
