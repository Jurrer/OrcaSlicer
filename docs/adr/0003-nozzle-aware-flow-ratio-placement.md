# Batch Flow Ratio placement is nozzle-aware but not nozzle-binding

Batch Flow Ratio calibration on a two-nozzle printer can use different reachable areas for each nozzle. We added a per-filament **Calibration Nozzle** choice, but kept it as placement metadata only: it does not change filament, printer, process, or preset settings and does not bind the filament to that nozzle for slicing.

The choice defaults every filament to the left nozzle and is not persisted. Each selection uses its configured extruder printable area, falling back to the full bed when a printer does not provide per-extruder areas. Mixed batches place right-nozzle assemblies first and then pack left-nozzle assemblies into the remaining bed. If no valid placement exists, calibration fails with the batch's one-plate fit error rather than silently producing an invalid batch.

The feature is deliberately limited to Batch Flow Ratio calibration: its custom assembly arranger can carry per-assembly bounds without changing shared arrangement behavior for Temperature Tower or non-calibration objects.
