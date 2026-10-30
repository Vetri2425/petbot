# 0012. Body: SolidWorks and 3D printing

Status: Accepted

## Context
The pet needs a custom body that holds off-the-shelf electronics.

## Decision
- Design in SolidWorks.
- 3D print: PETG frame, PLA shell.
- Electronics are off-the-shelf modules (no custom PCB).

## Consequences
- STL exports live in `cad/`; native SolidWorks files will use Git LFS later.
- Body layout decides touch pad zones and mounting.
