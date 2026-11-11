scenery.dsf.7z is a synthetic fixture generated entirely by this project.
It contains the overlay constructed in tests/dsf_test.cpp: six polygons,
two objects and two road chains. No simulator scenery is redistributed.

To regenerate after changing that fixture:
1. Build and run dsf_test; it writes build/dsf-test-scratch/compressed-source.dsf.
2. In a disposable Python environment with py7zr installed:

   import py7zr
   with py7zr.SevenZipFile("tests/fixtures/scenery.dsf.7z", "w",
                          filters=[{"id": py7zr.FILTER_LZMA2, "preset": 6}]) as archive:
       archive.write("build/dsf-test-scratch/compressed-source.dsf", "+47+008.dsf")

The tests need only the checked-in binary fixture, not Python or py7zr.
