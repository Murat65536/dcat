# dcat

A terminal-based 3D model viewer

## Features

- 3D model rendering in the terminal
- Support for multiple model formats (via Assimp)
- Texture and normal mapping support
- Skydome/skybox background rendering
- Animation playback
- FPS camera controls
- Sixel and Kitty graphics protocol support

## Controls

- `q` - Quit the viewer
- `m` - Toggle wireframe
- Animation controls:
  - `p` - Play/Pause animation
  - `1` - Previous animation
  - `2` - Next animation
- When FPS controls are enabled:
  - `w/a/s/d` - Move forward/left/backward/right
  - `i/j/k/l` - Look around
  - `Space` - Move up
  - `Shift` - Move down
  - `v` - Slow down
  - `b` - Speed up
- When FPS controls are disabled
  - `w/a/s/d` - Rotate the camera around the model
  - `e/r` - Zoom in and out
