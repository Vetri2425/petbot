# sim

Planned simulators for sim-first development (nothing implemented yet):
- LVGL desktop simulator to develop the face without hardware.
- Fake ESP32 script on one end of a socat virtual serial pair, so `brain/` can run against it.
- arm64 Docker container capped at 2GB RAM (`--memory=2g`) to test the Phase 2 voice pipeline.
No Gazebo. Models are downloaded at run time, never committed.
