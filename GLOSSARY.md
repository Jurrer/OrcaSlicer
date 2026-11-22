# OrcaSlicer

3D slicer application. FFF (fused filament fabrication) print preparation: model slicing, G-code generation, printer profiles, calibration tools.

## Calibration

**Batch Calibration**:
Calibrating multiple loaded filaments in a single plate and G-code file, using existing calibration test types (PA Line, PA Pattern, Flow Ratio). Each filament gets its own test objects assigned to its extruder.
_Avoid_: Multi-filament calibration, sequential calibration

**CalibMode**:
Enum in `calib.hpp` identifying the calibration test type (PA Line, PA Pattern, PA Tower, Flow Rate, etc.). Stored per-object, not as a batch-level mode.

**Calib_Params**:
Parameter struct for a single calibration test: mode, extruder_id, start/end/step values, speeds, accelerations. In batch calibration, one per test object.

**Filament Slot**:
Zero-based loaded-filament position, such as an AMS tray selection. It identifies the material used by a batch calibration row; it is not the physical nozzle index.
_Avoid_: extruder index, nozzle index

**Calibration Assembly**:
One complete calibration test for one filament, printed as one ByObject sequential-print unit. Member-specific settings live on the member, while shared settings live on the assembly.
_Avoid_: rigid group, arrangement group, filament group, calibration object group

**Calibration Test Member**:
One test piece inside a Calibration Assembly. Member-specific settings belong to that member, while settings shared by every member belong to the assembly.
_Avoid_: test object, flow-test object, group member

**Calibration Nozzle**:
Nozzle whose reachable area constrains placement of one filament's Calibration Assembly. The selection is placement metadata only and does not bind the filament to that nozzle for slicing.
_Avoid_: extruder assignment, nozzle binding, print nozzle

**Physical Nozzle**:
Nozzle whose geometry provides values such as nozzle diameter and printable area. A Filament Slot may map to it, but a Physical Nozzle is not the placement-only Calibration Nozzle.
_Avoid_: Calibration Nozzle, Filament Slot

**Short-objects Exemption**:
Rule in `Print::is_all_objects_are_short()`: when every object on the plate is shorter than `nozzle_height` (default 2.5 mm), ByObject clearance drops from `extruder_clearance_radius` (40 mm) to `MAX_OUTER_NOZZLE_DIAMETER` (4 mm). Applies automatically; not user-configurable.

## Print Sequence

**ByObject**:
Sequential print mode — each object is printed fully before the next begins. Requires `extruder_clearance_radius` spacing between objects to avoid toolhead collisions.
_Avoid_: Sequential print, By-Object

**ByLayer**:
Default print mode — all objects are printed layer-by-layer together. Compact but interleaves filaments on every layer.
_Avoid_: Layer-by-layer, By-Layer

**extruder_clearance_radius**:
Physical radius around the extruder nozzle that must be kept clear of previously printed objects during ByObject travel. Default 40 mm. Determines minimum object spacing via `min_object_distance()`.
_Avoid_: Clearance radius, toolhead clearance

## G-code Generation

**Custom-gcode-per-object**:
Mechanism used by PA Pattern: calibration G-code is generated per `ModelObject` via `CalibPressureAdvancePattern::generate_custom_gcodes()`, then merged into per-layer `CustomGCode::Info` blocks. Batch calibration extends this to PA Line tests.
_Avoid_: Per-object gcode injection

**Prime Tower**:
Wipe/prime tower for multi-extruder tool changes. Config: `enable_prime_tower` (default false), `wipe_tower_x/y` position, `prime_tower_width` (default 60 mm). Batch Calibration keeps it disabled because it is incompatible with ByObject.
_Avoid_: Wipe tower, purge tower
