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
     * @brief Constructor.
     * @param[in] vrEngine The VR engine abstraction object
     */
    VrManager( Dali::Integration::VrEngine* vrEngine );
    /**
     * @brief Destructor.
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

    /**
     * @brief todor
     */
    void GetVrViewportDimensions( Rect<int>& viewportDimensions );

  private:

    Dali::Integration::VrEngine*  mVrEngine;          ///< todor
    SceneGraph::Node*             mHeadNode;          ///< todor
    bool                          mEnabled;           ///< todor
    bool                          mEngineInitialized; ///< todor

};


} // Internal

} // Dali

#endif // DALI_VR_MANAGER_H


