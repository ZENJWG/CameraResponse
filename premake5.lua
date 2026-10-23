-- Camera Response | Win32 Visual Studio ASI using prebuilt plugin-sdk.
-- Generate: premake5.exe --file=premake5.lua vs2022 --pluginsdkdir="deps\plugin-sdk"

newoption {
    trigger = "pluginsdkdir",
    value = "PATH",
    description = "Path to DK22Pac/plugin-sdk"
}

local rootDir = path.getabsolute(path.getdirectory(_SCRIPT))
local sdkDir = _OPTIONS["pluginsdkdir"]

if sdkDir == nil or sdkDir == "" then
    sdkDir = os.getenv("PLUGIN_SDK_DIR")
end

if sdkDir == nil or sdkDir == "" then
    error("plugin-sdk directory was not supplied. Use --pluginsdkdir=<path> or set PLUGIN_SDK_DIR.")
end

sdkDir = path.getabsolute(sdkDir)

local pluginLib = path.join(sdkDir, "output/lib/Plugin.lib")
if not os.isfile(pluginLib) then
    -- Allow alternate library filename casing.
    pluginLib = path.join(sdkDir, "output/lib/plugin.lib")
end

if not os.isfile(pluginLib) then
    error("plugin-sdk GTA SA library was not found at " .. path.join(sdkDir, "output/lib/Plugin.lib"))
end

workspace "CameraResponse"
    location (path.join(rootDir, "build"))
    configurations { "Debug", "Release" }
    platforms { "Win32" }
    startproject "CameraResponse"

    filter "platforms:Win32"
        architecture "x86"
    filter {}

project "CameraResponse"
    kind "SharedLib"
    language "C++"
    characterset "MBCS"
    staticruntime "On"

    location (path.join(rootDir, "build"))
    targetdir (path.join(rootDir, "bin"))
    objdir (path.join(rootDir, "build/obj/%{cfg.platform}/%{cfg.buildcfg}"))

    files {
        path.join(rootDir, "source/**.h"),
        path.join(rootDir, "source/**.hpp"),
        path.join(rootDir, "source/**.cpp"),
        path.join(rootDir, "source/**.rc")
    }

    includedirs {
        path.join(rootDir, "source"),
        path.join(sdkDir, "plugin_sa"),
        path.join(sdkDir, "plugin_sa/game_sa"),
        path.join(sdkDir, "plugin_sa/game_sa/enums"),
        path.join(sdkDir, "plugin_sa/game_sa/rw"),
        path.join(sdkDir, "shared"),
        path.join(sdkDir, "shared/game"),
        path.join(sdkDir, "injector"),
        path.join(sdkDir, "hooking"),
        path.join(sdkDir, "modutils"),
        path.join(sdkDir, "safetyhook")
    }

    libdirs {
        path.join(sdkDir, "output/lib")
    }

    links { "Plugin" }

    defines {
        "GTASA",
        "PLUGIN_SGV_10US",
        "RW",
        "_CRT_SECURE_NO_WARNINGS",
        "_CRT_NON_CONFORMING_SWPRINTFS",
        "NOMINMAX"
    }

    targetname "CameraResponse"
    targetextension ".asi"
    implibdir (path.join(rootDir, "build/lib"))

    filter "action:vs*"
        systemversion "latest"
        -- /std:c++latest is required by pinned plugin-sdk/SafetyHook; /MP enables parallel builds.
        -- Direct switches avoid newer Premake API dependencies.
        buildoptions { "/std:c++latest", "/Zc:__cplusplus", "/GS", "/sdl", "/MP" }
        linkoptions { "/DYNAMICBASE", "/NXCOMPAT" }

    filter "configurations:Debug"
        symbols "On"
        defines { "DEBUG" }

    filter "configurations:Release"
        optimize "Speed"
        symbols "Off"
        -- Split/strip unused COMDATs; /Brepro makes release output reproducible.
        buildoptions { "/Gy", "/Gw", "/GL", "/Brepro" }
        linkoptions { "/LTCG", "/OPT:REF", "/OPT:ICF", "/Brepro", "/INCREMENTAL:NO", "/RELEASE" }

    filter {}
