# Filament-profile pressure advance

In Advanced mode, edit a filament preset and open **Filament → Basic information**.
Use the extruder/nozzle selector above the settings to select **Direct Drive: Standard**,
check **Enable pressure advance**, and enter its calibrated value in **Pressure advance**.
Then select **Bowden: Standard** and enter the Bowden value. Save the preset and re-slice.
If you use a High Flow or E3D nozzle variant, configure that selection instead.

For a two-cube test on both X2D nozzles, select this preset in both filament slots
used by the cubes, with one slot mapped to the main nozzle and the other to the
auxiliary nozzle. Editing one slot's preset does not enable PA in another slot
that still uses a different preset, such as Generic PLA.

Each extruder/nozzle variant has its own value and enable checkbox in the same
filament preset. The override is disabled by default. When enabled, it replaces the printer's
calibrated pressure advance after machine startup and after each filament change.
It is stored in the filament preset, using OrcaSlicer's existing
`enable_pressure_advance` and `pressure_advance` settings; it does not update the
printer's saved AMS-slot calibration.

The X2D's direct-drive and Bowden extruders can therefore use different PA values
from one profile, including when the filament's extruder mapping changes during a
print. The active extruder and nozzle type select both the value and its enable
checkbox. The extruder determines which command is emitted:

- Bambu direct drive: `M900 K<value> L1000 M10`, as in OrcaSlicer.
- Bambu Bowden: `M901 P0.75 K<value>`, as in Bambu Studio's Bowden PA calibration.
- Other printers retain their existing Klipper, RepRapFirmware, or Marlin command.

Commands are emitted after the machine/filament start scripts and after filament
changes, including the prime-tower path, so those scripts cannot overwrite the
profile value before model printing resumes. Disabling the override emits no
profile PA command.

Older profiles with one shared PA value keep that value for every variant until
you edit them independently. Older projects retain each filament's own shared value.

Reference implementation reviewed at OrcaSlicer commit
[`f83bfa17ff878a6689b875353df7200ab2d30053`](https://github.com/OrcaSlicer/OrcaSlicer/tree/f83bfa17ff878a6689b875353df7200ab2d30053):
[GCode.cpp](https://github.com/OrcaSlicer/OrcaSlicer/blob/f83bfa17ff878a6689b875353df7200ab2d30053/src/libslic3r/GCode.cpp),
[GCodeWriter.cpp](https://github.com/OrcaSlicer/OrcaSlicer/blob/f83bfa17ff878a6689b875353df7200ab2d30053/src/libslic3r/GCodeWriter.cpp),
and [Tab.cpp](https://github.com/OrcaSlicer/OrcaSlicer/blob/f83bfa17ff878a6689b875353df7200ab2d30053/src/slic3r/GUI/Tab.cpp).
