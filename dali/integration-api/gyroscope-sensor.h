#ifndef __DALI_GYROSCOPE_SENSOR_H__
#define __DALI_GYROSCOPE_SENSOR_H__

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

namespace Dali
{
class Vector4;

namespace Integration
{

// EXTERNAL INCLUDES
class GyroscopeSensor
{
public:

  GyroscopeSensor() {}
  virtual ~GyroscopeSensor() {}

  virtual void Enable() = 0;
  virtual void Disable() = 0;
  virtual void Read( Dali::Vector4& data ) = 0;
  virtual void ReadPackets( Dali::Vector4* data, int count ) = 0;
  virtual bool IsEnabled() = 0;
  virtual bool IsSupported() = 0;
};

} // Integration
} // Dali

#endif
