#pragma once
// Audio. Suggested library: miniaudio (single header, FetchContent-able) - or FMOD/Wwise if the budget allows.
//  - Streaming music with crossfades chosen by biome/depth/time/event/boss state.
//  - 3D positional SFX (steps by block material, mining/placing/breaking, combat, mobs, ambience, liquids),
//    distance attenuation, simple occlusion (raycast through blocks to muffle underground sounds) and
//    reverb in caves (cheap: switch a reverb preset from the "enclosed volume" size).
//  - Sound events referenced by name from block/mob/item JSON; the server sends events ("play sound X at pos"),
//    the client plays them. Voice pooling & priority so many sounds don't clip.
//  - Volume groups: master / music / sfx / ambient / ui from ClientSettings.

namespace t3d
{
    class AudioSystem
    {
      public:
        // TODO: init(), update(listener), play(name, pos), play_music(track), shutdown().
    };
} // namespace t3d
