#ifndef __DALI_INTERNAL_SCENE_GRAPH_POST_PROCESS_RESOURCE_DISPATCHER_H__
#define __DALI_INTERNAL_SCENE_GRAPH_POST_PROCESS_RESOURCE_DISPATCHER_H__

/*
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
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

// INTERNAL INCLUDES
#include <dali/integration-api/resource-declarations.h>

namespace Dali
{
namespace Internal
{
typedef Integration::ResourceId ResourceId;

namespace SceneGraph
{

/**
 * Interface which offers a dispatch mechanism for uploaded notifications on textures
 * Should be offered by a Render Thread object
 */
class TextureUploadedDispatcher
{
public:
  /**
   * Dispatch a post process resource uploaded request to the Update thread
   * @param[in] resource A post process request
   */
  virtual void DispatchTextureUploaded( ResourceId resource ) = 0;

protected:
  /**
   * Virtual destructor
   */
  virtual ~TextureUploadedDispatcher(){}
};


} // namespaces
}
}


#endif // __DALI_INTERNAL_SCENE_GRAPH_POST_PROCESS_RESOURCE_DISPATCHER_H__
