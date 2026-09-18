TODO - Code review remarks for Emu68DeviceTree

Purpose: store the code-review items covering C89/SAS-C, AmigaOS conventions, maintainability and naming clarity.

High priority -- ALL RESOLVED (see items 1-3 below)

1) Reduce global symbol exposure -- RESOLVED
   - File: src/DeviceTree.c
   - Problem: typeNames[] is defined with external linkage but not declared in headers.
   - Action: make it static if only used in DeviceTree.c, or declare it in DeviceTree.h if intended public.
   - Rationale: limit symbol scope and prevent accidental collisions.
   - Done: typeNames[] is declared extern in DeviceTree.h (it is used from Emu68DeviceTree.c too, so
     genuinely public); the redundant local extern in Emu68DeviceTree.c was removed.

2) Fix quad formatting in FormatValue() -- RESOLVED
   - File: src/DeviceTree.c
   - Problem: when high == 0 the code prints both high and low hex words and also prints decimal of low inside parentheses. This is confusing.
   - Suggested change: print only the low word when high == 0. Example replacement snippet:

     if (high == 0)
     {
         SPrintf(buffer, size, "0x%08lx (%lu)", low, low);
     }
     else
     {
         SPrintf(buffer, size, "0x%08lx%08lx", high, low);
     }

   - Rationale: clearer output for common cases where the high word is zero.
   - Done: applied as suggested.

3) Guard against undefined behaviour in BitsAt() mask calculation -- RESOLVED
   - File: src/Utils.c
   - Problem: (1UL << count) is UB if count is 0 or >= width of type. Current usage makes it unlikely but better to be defensive.
   - Suggested change: handle count == 0 and large count explicitly. Example:

     if (count == 0) return 0;
     if (count >= (8 * sizeof(ULONG))) mask = ~0UL;
     else mask = ((1UL << count) - 1);
     return (value & mask);

   - Rationale: remove theoretical UB and make the helper robust.
   - Done: applied as suggested; BitsAt() is also now static (see item 5).

Medium priority -- ALL RESOLVED (see items 4-6 below)

4) Add const-correctness to traversal APIs -- RESOLVED
   - Files: src/DeviceTree.h, src/DeviceTree.c
   - Suggestion: use const where appropriate, e.g. ULONG CountNodes(const of_node_t *node);
   - Rationale: documents intent and enables safer usage with const pointers.
   - Done: CountNodes(), CountSubItems(), CountProperties(), EntryType(), FormatType(), FormatValue() all
     take const of_node_t */const of_property_t * now.

5) Ensure header visibility matches intended API -- RESOLVED
   - Files: headers (DeviceTree.h, Utils.h) and callers
   - Suggestion: Make functions static if internal; otherwise declare them in headers.
   - Rationale: improve encapsulation and reduce compile-time coupling.
   - Done: PutChar(), StringCompare2() and BitsAt() (only ever used within Utils.c) are now static and no
     longer declared in Utils.h.

6) Document IsAsciiValue assumptions -- RESOLVED
   - File: src/Utils.c
   - Problem: behaviour requires final NUL and at least two printable characters in a run; this is a device-tree convention but should be documented.
   - Action: add a short comment describing the assumption and edge-cases (length < 2, trailing NUL, consecutive NULs as padding).
   - Done: comment added above IsAsciiValue() covering exactly these cases.

Low priority / nice-to-have -- still open

7) Minor naming suggestions
   - BitsAt -> ExtractBits or BitsAtOffset (optional)
   - Add a short comment in DeviceTree.h explaining the on_/op_ naming convention used for node/property fields.

8) CI / build job
   - Add a lightweight GitHub Actions job that runs smake to build the project (SAS/C invocation) or a static C89 checker. Helps catch regressions early.

9) Documentation
   - README already good. Add a small CONTRIBUTING or DEVELOPMENT note describing the expected toolchain (SAS/C 6.59, NDK 3.2) and how to run smake.

10) Optional refactors
   - Consider marking internal helper functions static if not used across translation units.
   - Consider minor formatting/clarifying comments in FormatEpoch, IsAsciiValue and FormatInspect.

Status

Items 1-6 (high and medium priority) are all resolved. Only the low priority
items 7-10 above remain open.

