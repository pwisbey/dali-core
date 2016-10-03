/*
 * Copyright (c) 2016 Samsung Electronics Co., Ltd.
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
#include <dali/internal/common/vr-manager-impl.h>

// EXTERNAL INCLUDES
#include <stdio.h>
#include <stdlib.h>

namespace Dali
{

namespace Internal
{

namespace
{

#define GL(x) { x; int err = context.GetError(); if(err) { DALI_LOG_ERROR( "GL_ERROR: [%d] '%s', %x\n", __LINE__, #x, (unsigned)err);fflush(stderr);fflush(stdout);} else { /*DALI_LOG_ERROR("GL Call: %s\n", #x); fflush(stdout);*/} }

// These dimensions must match those used by the Tizen VR engine.
// TODO: Get these from the Tizen VR engine when such an API is implemented.
const Rect<int> DEFAULT_VR_VIEWPORT_DIMENSIONS( 0, 0, 1024, 1024 );

} // Anonymous namespace


VrManager::VrManager( Dali::Integration::VrEngine* vrEngine )
: mVrEngine( vrEngine ),
  mHeadNode( NULL ),
  mEnabled( true ),
  mEngineInitialized( false )
{
}

VrManager::~VrManager()
{
}

void VrManager::SetEnabled( bool enabled )
{
  mEnabled = enabled;
}

bool VrManager::IsEnabled() const
{
  return mEnabled;
}

void VrManager::SetHeadNode( SceneGraph::Node* node )
{
  if( node )
  {
    mHeadNode = node;
  }
}

void VrManager::UpdateHeadOrientation()
{
  // We need VR to be enabled and this manager to be initialized to update the head.
  if( mEnabled && mVrEngine && mHeadNode )
  {
    Dali::Integration::Vr::VrEngineEyePose eyePose;

    if( mVrEngine->Get( Dali::Integration::VrEngine::EYE_CURRENT_POSE, &eyePose ) )
    {
      std::cout << "todor: VRMANAGER: UpdateHeadOrientation: Setting orientation: " << eyePose.rotation << std::endl;
      mHeadNode->SetOrientation( eyePose.rotation );
    }
    else
    {
      std::cout << "todor: VRMANAGER: UpdateHeadOrientation: Setting orientation: DEFAULT" << std::endl;
      mHeadNode->SetOrientation( Quaternion( 1.0f, 0.0f, 0.0f, 0.0f ) );
    }
  }
}

void VrManager::PrepareRender( int surfaceWidth, int surfaceHeight )
{
  // If VR is enabled and ready to setup (yet not yet initialized), initialize the engine now.
  if( mEnabled && mVrEngine )
  {
    if( !mEngineInitialized )
    {
      // Initialise VR engine.
      Dali::Integration::Vr::VrEngineInitParams params;
      memset( &params, 0, sizeof( Dali::Integration::Vr::VrEngineInitParams ) );
      params.screenWidth = surfaceWidth;
      params.screenHeight = surfaceHeight;

      mVrEngine->Initialize( &params );
      mEngineInitialized = true;

      // Start VR engine.
      mVrEngine->Start();
    }

    // Perform the Pre-Render.
    mVrEngine->PreRender();
  }
}

bool VrManager::RenderEyes( Context& context, Dali::Camera::Type cameraType )
{
  bool vrEye = false;

  if( mEnabled && mVrEngine )
  {
    context.GetError();

    // This reads all the required VR information in one go.
    int leftFrameBufferObject(-1);
    int rightFrameBufferObject(-1);
    int leftTexture(-1);
    int rightTexture(-1);
    int bufferWidth(-1);
    int bufferHeight(-1);

    static const int properties[] =
    {
      Dali::Integration::VrEngine::EYE_LEFT_CURRENT_FBO_ID,     Dali::Integration::VrEngine::EYE_RIGHT_CURRENT_FBO_ID,
      Dali::Integration::VrEngine::EYE_LEFT_CURRENT_TEXTURE_ID, Dali::Integration::VrEngine::EYE_RIGHT_CURRENT_TEXTURE_ID,
      Dali::Integration::VrEngine::EYE_BUFFER_WIDTH,            Dali::Integration::VrEngine::EYE_BUFFER_HEIGHT
    };

    void* values[] =
    {
      &leftFrameBufferObject, &rightFrameBufferObject, &leftTexture, &rightTexture, &bufferWidth, &bufferHeight
    };

    if( cameraType == Dali::Camera::VR_EYE_LEFT )
    {
      vrEye = true;
      mVrEngine->Get( properties, values, 6 );

      GL( context.BindFramebuffer( GL_FRAMEBUFFER, leftFrameBufferObject ) );
      GL( context.Bind2dTexture( leftTexture ) );
      GL( context.TexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, bufferWidth, bufferHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL ) );
    }
    else if( cameraType == Dali::Camera::VR_EYE_RIGHT )
    {
      vrEye = true;
      mVrEngine->Get( properties, values, 6 );

      GL( context.BindFramebuffer( GL_FRAMEBUFFER, rightFrameBufferObject) );
      GL( context.Bind2dTexture( rightTexture ) );
      GL( context.TexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, bufferWidth, bufferHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL ) );
    }
  }

  return vrEye;
}

void VrManager::SubmitFrame( Context& context )
{
  if( mEnabled && mVrEngine )
  {
    context.Flush();
    mVrEngine->SubmitFrame();
  }
}

void VrManager::GetVrViewportDimensions( Rect<int>& viewportDimensions )
{
  viewportDimensions = DEFAULT_VR_VIEWPORT_DIMENSIONS;
}


} // Internal

} // Dali
