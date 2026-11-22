# Batch Flow Ratio calibration uses custom assemblies

Batch Flow Ratio calibration must place one complete flow test per filament on one plate. Treating each test as a group of separate `ModelObject`s inflates ByObject sequential-clearance checks between every member and makes even one or two groups fail to fit. The generic UI "Assemble" command merges objects but deliberately drops most object-level config, so per-test flow ratios and calibration settings would be lost.

We decided to build a **custom `ModelObject` assembly per filament** in batch calibration code: each test member becomes a `ModelVolume`, per-member `PrintRegionConfig` values are copied to volume configs, and shared `PrintObjectConfig` values are validated as identical. The assembly is one sequential-print unit, so clearance is checked between assemblies, not between members.

Trade-off: this requires a calibration-specific merge path and per-volume config handling, and it couples batch calibration to the `PrintRegionConfig`/`PrintObjectConfig` split. The alternative of arranging separate objects with a rotation-candidate set would touch the shared arrangement API and still pay per-member clearance. Batch Calibration also keeps the prime tower disabled because the ByObject validation rejects prime towers.
