Lapis stress tests
==================
Each folder is a normal Lapis project. Build.bat exports them to
Export\StressTest_<Name>\StressTest_<Name>.exe automatically.

Physics    - waves of rigid bodies until FPS < 20 for 3 s (or 4000 bodies)
Rendering  - growing field of lit/shadowed objects + 16 moving lights
Scripting  - thousands of entities each running a Luau script

Results are written next to the exe in Saved\:
  stress_<test>_<date>.csv   one row every 0.5 s (fps, frame/physics/script/render ms, draws, ...)
  summary.txt                one block per finished run
Esc quits early.
