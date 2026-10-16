# bench — JSON candidates on live InnerTube answers

The stand behind `docs/specs/2026-10-16-json-design.md` §5. `corpus.tar.zst` holds 21 real
answers (search, browse, next, comments, player for five clients, json3), scrubbed of the
capturing client's IP and visitorData (`scrub.py`). `oracle.py` prints the canonical result
of a fixed extraction; every candidate must print it byte for byte or it is not measured.

    ./run.sh            # fetch, build, JSONTestSuite, run each candidate on one core, print the table
    TC=<armv7-eabihf toolchain> ./arm.sh   # ARMv7 builds checked under qemu (correctness only)
    ./capture.sh        # a fresh corpus from the live service (needs ../build), scrubbed

Not part of the CMake build. Times vary with the CPU clock; cycles (perf counters) do not —
`summarize.py` prints both IPC and time, the raw per-document rows are in `out/r_*.tsv`.
