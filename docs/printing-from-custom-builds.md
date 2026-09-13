# Printing from custom Bambu Studio builds

When the networking plugin reports that the software is not signed, a locally
built copy cannot use operations that require Bambu Lab's official authorization.
An ad hoc macOS signature (or your own Developer ID) does not make the application
an officially signed Bambu Lab client.

To keep using this fork for slicing while the printer is connected to the cloud:

1. Slice the plate in this application.
2. Choose **File > Export > Export plate sliced file** and save a `.gcode.3mf`.
3. Open the official **Bambu Connect** application and sign in to the Bambu account
   that owns the printer.
4. Import the sliced `.gcode.3mf`, select the printer, check the filament mapping
   and print options, and send from Connect.

Use the sliced export, rather than an unsliced project 3MF. The official Bambu
Studio can also be used to open the sliced file and send it. Re-signing this fork
does not remove the networking plugin's official-signature requirement.

References: [Bambu's authorization documentation](https://blog.bambulab.com/firmware-update-introducing-new-authorization-control-system-2/)
and [Bambu Connect integration](https://blog.bambulab.com/updates-and-third-party-integration-with-bambu-connect/).

## macOS crash when displaying a send failure

The September 11, 2026 crash reports show `Must only be used from the main thread`
in AppKit. `PrintJob::process()` called `Job::show_error_info()` on its worker
thread, and the progress indicator resized `SelectMachineDialog` immediately.
`Job::show_error_info()` now queues the error on the main event loop, just like
normal progress updates. This also covers `SendJob` failures.

Build the fixed application without closing an existing session:

```sh
./script/build_and_run.sh --build-only
```

After saving your project and quitting Bambu Studio, run
`./script/build_and_run.sh` to open the new build. The application is staged under
`build/<architecture>/printer-fix/BambuStudio.app`; the installed application is
not overwritten.

Run the error-dispatch regression check in a configured GUI build:

```sh
cmake -S . -B build/arm64 -DSLIC3R_BUILD_TESTS=ON
cmake --build build/arm64 --target gui_job_error_tests -j 4
ctest --test-dir build/arm64 -R '^gui_job_error_tests$' --output-on-failure
```

The test checks main-thread delivery, Unicode error details, ordering relative to
progress updates, and cleanup when a job is destroyed before dispatch. It does
not connect to a printer or start a print.
