#pragma once
// Umbrella header for games built on Lapis.

#include "Lapis/Core/Assert.h"
#include "Lapis/Core/Base.h"
#include "Lapis/Core/Event.h"
#include "Lapis/Core/FileSystem.h"
#include "Lapis/Core/JobSystem.h"
#include "Lapis/Core/Log.h"
#include "Lapis/Core/Time.h"

#include "Lapis/Math/Geometry.h"
#include "Lapis/Math/Math.h"

#include "Lapis/Platform/Input.h"
#include "Lapis/Platform/Window.h"

#include "Lapis/Renderer/Camera.h"
#include "Lapis/Renderer/Material.h"
#include "Lapis/Renderer/Mesh.h"
#include "Lapis/Renderer/Renderer.h"
#include "Lapis/Renderer/Texture.h"

#include "Lapis/Assets/AssetManager.h"
#include "Lapis/Assets/ImageLoader.h"
#include "Lapis/Assets/ObjLoader.h"

#include "Lapis/Physics/PhysicsWorld.h"

#include "Lapis/Scene/Components.h"
#include "Lapis/Scene/Entity.h"
#include "Lapis/Scene/Scene.h"
#include "Lapis/Scene/SceneSerializer.h"
#include "Lapis/Scene/ScriptableEntity.h"

#include "Lapis/Scripting/ScriptEngine.h"

#include "Lapis/Runtime/Application.h"
#include "Lapis/Runtime/Project.h"

namespace Lapis {
constexpr const char* EngineVersion = "0.2.0";
}
