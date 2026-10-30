# deploy

Planned robot deployment files (nothing implemented yet):
- systemd units to run the brain on boot on the Radxa ZERO 3W.
- An update script: the robot pulls the latest GitHub release over Wi-Fi, then can re-flash
  the ESP32-S3 over the UART link.
The Dell laptop is used only for first-time flashing.
