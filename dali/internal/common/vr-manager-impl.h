#ifndef DALI_VR_MANAGER_H
#define DALI_VR_MANAGER_H

//todor
#include <dali/integration-api/vr-engine.h>
#include <dali/internal/update/nodes/node.h>
#include <dali/public-api/actors/camera-actor.h>
#include <cstring>

namespace Dali
{

namespace Internal
{


/**
 * Object managing access to VR abstraction.
 */
class VrManager
{
  public:

    /**
     * @brief todor
     */
    VrManager( Dali::Integration::VrEngine* vrEngine );
    /**
     * @brief todor
     */
    ~VrManager();

    /**
     * @brief todor
     */
    void SetEnabled( bool enabled );

    /**
     * @brief todor
     */
    bool IsEnabled() const;

    /**
     * @brief todor
     */
    void SetHeadNode( SceneGraph::Node* node );

    /**
     * @brief todor
     */
    void UpdateHeadOrientation();

    /**
     * @brief todor
     */
    void PrepareRender( int surfaceWidth, int surfaceHeight );

    /**
     * @brief todor
     */
    bool RenderEyes( Context& context, Dali::Camera::Type cameraType );

    /**
     * @brief todor
     */
    void SubmitFrame( Context& context );

  private:

    Dali::Integration::VrEngine*  mVrEngine;          ///< todor
    SceneGraph::Node*             mHeadNode;
    bool                          mEnabled;
    bool                          mEngineInitialized;

};


} // Internal

} // Dali

#endif // DALI_VR_MANAGER_H


