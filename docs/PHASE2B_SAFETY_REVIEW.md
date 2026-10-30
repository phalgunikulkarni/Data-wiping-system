# DataGuard Phase 2B — Dataset Generator Safety Design and Test Plan

**Status:** Proposed design only. The owner has authorized planning, not
dataset generation. This document does not authorize image creation or
modification, reference-corpus or manifest creation, labels, recovery, model
training, physical-device access, or wiping.

## 1. Repository state and source evidence

### Current state

- HEAD: `59951f4c61d7386012bd4fc442130072d1eb90fa`.
- At inspection, `git status --short` showed `?? docs/`; the planning documents
  are untracked. The tracked working tree has no reported diff.
- `docs/ML_PROTOCOL.md` exists, SHA-256
  `68A4DF9F7FBC9E807DE79C6D08D3B08ACF3270CE7CB7162411F891F430ADB82F`.
- `docs/PHASE2_DATASET_PLAN.md` exists, SHA-256
  `C342CF4C0889C59331313E70A21A555DC7E701DFA5F67CBAB1203AFC54361E6B`.
  Its content differs from the version observed in the prior planning turn;
  this is preserved as an existing user change. The hashes were recorded
  before and must be compared after this document is written.
- `docs/PHASE2B_SAFETY_REVIEW.md` did not exist before this task.
- The protocol still labels itself “Draft for human review” and retains open
  approval questions, while the task states that the owner approved the
  protocol. The Phase 2A plan calls out this discrepancy. This review does not
  revise the protocol or resolve scientific questions.

No contents under `dataset/baseline/` or any ground-truth manifest were
inspected. No disk image, physical device, mounted volume, or user storage was
opened.

### Existing I/O boundary

**Verified:** `IReadOnlyVolumeReader` exposes identity, filesystem, size,
sector-size, revalidation, and bounded volume-relative `readAt` operations. It
has no write method. FAT32 parsing uses that interface and checks exact read
lengths. The test `FakeVolume` keeps bytes in a `std::vector`, enforces bounds,
and can simulate short reads, failures, and stale identity.

**Critical separation:** `FileScanner::openMountedVolumeReadOnly` constructs
`WindowsMountedVolumeReader`. That implementation calls `CreateFileW` on a
Windows volume namespace path derived from a drive letter, requests
`GENERIC_READ`, and revalidates the mounted volume. The MainWindow scan path
calls this mounted-volume factory. It is not a disk-image reader and must not
be called by a dataset generator or its tests.

No regular image-file implementation of `IReadOnlyVolumeReader` was found.
There is no dataset generator, image copier, generic dataset hash/custody
utility, or dataset manifest parser in the inspected project. `QCryptographicHash`
is used by wiping code, not as a dataset integrity utility. `FileScanner` also
has `scanExistingRoot`, which enumerates a filesystem directory; it is not an
image reader and is outside the future generator boundary.

### Existing destructive boundary

`WipeEngine::wipeVolumeExtents` accepts an injected `IStorageDevice` and calls
its `writeAt` after target mapping, revalidation, and volume locking. This is a
destructive interface even though the raw-path, raw-sector, and ambiguous
extent entry points return `Unsupported`. No production storage implementation
or runtime invocation was established by this inspection. The dataset workflow
must not link to, construct, or call `WipeEngine`, `IStorageDevice`, mounted
volume readers, or operation UI paths. All scenario changes in any future
approved work must be confined to regular working-image files through a
separate image-file writer abstraction.

### Test and dependency evidence

- `tests/FileScannerSafetyTests.cpp` constructs a synthetic FAT32 byte vector
  and a mock `FakeVolume`; it also uses `QTemporaryDir` and `QFile` for regular
  temporary filesystem entries.
- `tests/WipeSafetyTests.cpp` uses an in-memory `MemoryStorage` and a temporary
  directory. This demonstrates fake-I/O patterns only; the wipe test is not a
  dataset-generation facility and must not be reused to invoke wiping.
- CMake specifies C++17 and Qt 6, with Widgets/Sql and Windows bcrypt in the
  application configuration. No FAT formatter, image-mounting package,
  dataset library, or JSON manifest library is declared. External tool
  availability is unknown and was not checked or assumed.
- The only manifest-like project file found is `src/WipeEngine.manifest`, a
  Windows application manifest. It is not a dataset or ground-truth manifest.

## 2. Proposed minimal safe implementation boundary

This is a future sequence for review, not authorization or implementation.

### Step 1 — Freeze locations and image contract

Require explicit owner approval for a dedicated workspace subtree with
separate canonical roots for immutable baselines, references/manifests,
per-experiment working copies, and outputs/logs. Reject any root overlapping
`dataset/baseline/`, existing manifest locations, source files, build folders,
or user directories. Select a flat regular image-file format and a maximum
image size only after tool and FAT32 geometry review. Do not select an external
formatter or dependency without separate approval.

### Step 2 — Implement a path gate before any open

Use a small allowlist API that accepts a configured root plus a relative path,
not arbitrary user-supplied absolute paths. Canonicalize the root and every
existing path component; reject `..`, empty/dot components where ambiguous,
Windows alternate data streams, reserved device names, UNC paths, `\\.\`,
`\\?\GLOBALROOT`, volume GUID/device namespaces, drive roots, mounted-volume
paths, and any path that resolves outside the approved root.

On Windows, inspect each component without following reparse points (including
symlinks, junctions, and mount points); reject any reparse point in the path.
For an opened file, use a handle-based final-path check and file identity check
to defend against path replacement between validation and open. Require the
handle to identify a regular disk file, not a directory or device. Revalidate
identity immediately before writes and cleanup. If canonicalization, file type,
or identity is unavailable, fail closed.

### Step 3 — Separate reader and writer from application storage backends

Create a future regular-image reader that implements only
`IReadOnlyVolumeReader`, binds to one open regular-file handle and immutable
file identity, reports the exact file size, and uses checked image-relative
offset/length bounds before every read. It must not call
`openMountedVolumeReadOnly` or accept `DriveInfo`/drive letters.

If scenario construction is later approved, use a different
`RegularImageWorkingCopy` interface that can write only to an already-created,
validated working-copy file below its experiment root. Do not make it an
`IStorageDevice`, do not use physical extent mapping, volume locking, raw device
handles, or `WipeEngine`, and do not expose a conversion to those interfaces.
Separate build targets/modules so generator code cannot accidentally call the
MainWindow device scan or wipe code. Tests inject in-memory readers/writers and
must not have a fallback to production storage.

### Step 4 — Bounded file I/O and integrity

Before allocation or I/O, validate that file size is nonzero and at most an
approved fixed limit; check every addition and multiplication for overflow;
bound individual reads, total bytes, allocations, iterations, recursion/depth,
and cancellation points. Require exact read/write counts and report short I/O
as failure. Recheck file size and stable file identity after reads/copies.
Use a cryptographic hash for baseline, references, copies, and outputs; compare
against a previously approved expected digest. A mismatch stops the run and
preserves evidence. Hashing must be streamed in bounded chunks and must not
change the input.

### Step 5 — Baseline and working-copy lifecycle

Treat baseline and reference files as read-only inputs. Verify their expected
size/hash before copying. Create a new unique working file with exclusive
creation semantics; never reuse or truncate an existing path. Verify its hash
against the baseline immediately after copying. Scenario writes, if separately
authorized, apply only to that working file. Write outputs and logs to distinct
new paths, never to a source image, reference, or manifest. Rehash immutable
inputs after each run and fail if any digest or identity changed.

### Step 6 — Cancellation and cleanup

Cancellation is checked between bounded chunks and scenario steps. On
cancellation or error, flush/close handles, record an incomplete/unscorable
state, preserve inputs and logs, and leave the working copy quarantined for
review. Do not automatically resume a partial experiment. Cleanup may remove
only a newly created scratch artifact whose recorded file identity and
canonical path remain beneath the exact experiment scratch root. Never use a
wildcard or parent-level recursive delete. On any path, identity, reparse, or
ownership ambiguity, retain the artifact and stop for human review.

## 3. Metadata-only feature candidate schema

The initial model direction is metadata-only. This table does not approve
features; each requires evidence review before schema freeze. Record source
values and missingness explicitly. Never substitute numeric zero for missing.

| Candidate field | Current structured source | Evidence required before approval | Risk/handling |
|---|---|---|---|
| `entry_status` / deleted-candidate cohort | `FileEntry::status` is structured. | Define cohort inclusion, confirm status is available before recovery, and decide whether all eligible rows are deleted candidates. | Status may be constant within the cohort or encode scenario selection; use as cohort/filter unless variation and value are justified. Unknown status is missing/rejected, not zero. |
| `directory_size_known`, `directory_size_bytes` | `FileEntry::sizeKnown` and `sizeBytes` are structured. | Confirm parser reliability for the selected deleted-entry cases; compare with original manifest only for validation, never use reference size as a feature; audit correlation with scenario and labels. | Directory size is not authoritative original size. Preserve the known flag; masked value remains missing. |
| `record_confidence` | `FileEntry::confidence` is structured. | Define how `Candidate`, `Ambiguous`, and `Invalid` rows are handled; confirm ambiguous/invalid cases are excluded as unscorable rather than learned as outcomes. | May largely encode eligibility/quality, not recovery outcome. Keep as explicit categorical metadata only if cohort and label rules support it. |
| `filesystem_type` | `DriveInfo::fileSystem` is structured outside `FileEntry`. | Bind the value to the scanned image metadata and define unsupported handling. | FAT32-only cohort makes it constant; likely a validation gate rather than a predictor. Missing is an explicit unknown category/flag. |
| `volume_identity_validated` | `FileScanResult::volumeIdentityValidated` is structured at scan level. | Confirm it is known before inference and reject stale/failed scans before feature construction. | Prefer an eligibility gate, not a predictor. Never include raw volume ID. Missing/false should make the sample ineligible or explicitly missing per approved policy. |
| `entry_identity_available` | `FileEntry::entryIdentityAvailable` is structured. | Define when identity availability is required for inclusion and verify identity is stable for the experiment. | Prefer eligibility/audit linkage, not raw identity as a feature. Missing identity must not become numeric zero. |

Do not include raw file names, paths, image IDs, volume IDs, entry IDs,
directory offsets, hashes, timestamps, reason strings, reference bytes,
recovered bytes, scoring results, or post-recovery metadata as model inputs.
`FileEntry` does not expose a dedicated validated starting-cluster,
allocation/reuse, or fragmentation feature; these are not part of this minimal
candidate schema. A FAT32-only corpus has no filesystem-type variation. Any
additional field needs a structured source, validation semantics, missing-value
encoding, and leakage review before approval. Byte-content features remain
excluded.

## 4. Proposed synthetic-only safety tests (not implemented or run)

All tests use in-memory fakes or disposable regular files in an isolated
temporary workspace. They must not call mounted-volume enumeration, open a
device namespace, read a volume, or invoke `WipeEngine`/`IStorageDevice`.

| Test | Synthetic fixture and required assertion |
|---|---|
| Path traversal | Try `..`, encoded/separator variants, absolute paths, ADS, and paths escaping each allowlisted root. Assert rejection occurs before a file-open callback. |
| Device and mounted-volume paths | Try `\\.\PhysicalDriveN`, `\\.\X:`, `\\?\GLOBALROOT`, volume GUID paths, UNC paths, and drive roots. Assert rejected before any OS open; use an injected open counter that remains zero. |
| Symlink/reparse point | In a temporary directory, place a symlink/junction/reparse component pointing outside the root. Assert rejection without following it. Skip only if the OS cannot create the fixture, and report that as untested rather than passed. |
| Protected baseline paths | Supply protected root and descendant paths, plus canonical aliases/case variants where applicable. Assert writes, replacement, rename, and cleanup are refused. Assert sentinel hashes are unchanged. |
| Hash mismatch | Give a fake expected digest that differs from a regular temporary fixture. Assert stop before scenario mutation and preserve source/copy evidence. |
| Truncated image | Use a regular file shorter than the minimum boot-sector/BPB or declared geometry. Assert bounded read failure and no continuation/clamping. |
| Oversized input | Use a sparse or mocked file-size report above the configured maximum. Assert refusal before allocation or full read. |
| Arithmetic overflow | Exercise offsets/lengths near integer maxima and geometry multiplication/addition overflow. Assert error before read/write callback; no wrapping or zero substitution. |
| Bounded I/O and short transfer | Fake reader/writer returns short counts or I/O error. Assert failed/incomplete state, no label, and no further operations. |
| Cancellation | Cancel before open, between copy chunks, before scenario write, during read, and before cleanup. Assert handles close, state is incomplete/unscorable, source hash is unchanged, and no unauthorized cleanup occurs. |
| Cleanup boundaries | Create sentinel files in sibling, parent, baseline, and reference roots; attempt cleanup with stale/aliased path identity. Assert only a specifically owned scratch file can be removed and all sentinels remain. |
| No device/wipe backend | Link/use test-only fakes and assert forbidden backend factories are unavailable or fail closed; assert zero calls to mounted reader, enumeration, `IStorageDevice`, and wipe methods. |

Tests above are acceptance proposals only. None were added or executed in this
task.

## 5. Gates before dataset generation

All of the following are required, in order:

1. **Plan review:** owner reviews the Phase 2A plan and this safety review,
   including the current protocol “Draft” status discrepancy, and approves or
   records any revisions. Do not resolve scientific questions by implication.
2. **Safety design review:** approve canonical roots, Windows path/reparse
   handling, regular-file/image format, maximum sizes, I/O limits, hash/copy
   lifecycle, cancellation, cleanup, and strict separation from mounted-volume
   and wiping backends.
3. **Implementation review:** only after separate authorization, implement the
   minimal image-only reader/generator boundary. Inspect every file-open and
   cleanup path; verify no route reaches device or wipe APIs.
4. **Synthetic-test review:** add and run the proposed adversarial tests on
   temporary regular files and in-memory fakes. Review actual results; do not
   claim coverage for tests skipped due to OS fixture limitations.
5. **Protected-hash confirmation:** before and after generator testing, verify
   approved hashes for protected baselines, references, and manifests without
   modifying them. Any mismatch stops work and requires investigation.
6. **Explicit generation authorization:** the owner separately authorizes
   dataset generation after reviewing implementation, test evidence, tool
   versions, hashes, and open risks. This document is not that authorization.

## 6. Unresolved decisions and risks

- The protocol document remains marked as a draft and retains scientific and
  evaluation approval questions, despite the stated owner approval. Its status
  needs reconciliation; this review does not amend it.
- Image format, FAT32 creation tool, exact image sizes, resource limits, and
  availability/licensing of any tooling remain unverified and require approval.
- Recovery is not implemented; `scanDeleted` is unsupported and there are no
  recovered bytes or verified labels. The recovery procedure and its
  byte-position provenance need separate design and approval.
- Current scanner/device UI is coupled to a mounted-volume reader. A new
  image-file adapter and build boundary must be reviewed to ensure no fallback
  to physical or mounted storage.
- Dataset hashing, manifest parsing, immutability/custody, path gating, and
  safe working-copy lifecycle utilities do not currently exist in the project.
- The injected `wipeVolumeExtents` write path remains in source. Its presence
  requires a strict generator boundary; no production backend invocation was
  established here, and no wiping is authorized.
- Metadata features may be constant, ambiguous, or scenario-correlated. Their
  prediction value, missing-value encoding, cohort, and leakage behavior need
  evidence and approval before schema freeze.
- A defensible train/validation/test split may not be possible if source-file,
  image-lineage, and scenario groups collapse into too few independent groups.
  Do not weaken grouping to force a split.

## 7. Phase boundary

This is documentation-only planning. No code, tests, image, dataset, manifest,
label, or model was created or changed. No tests, builds, network access,
physical-device access, mounted-volume access, recovery, or wiping were run.
Stop here until the gates above are satisfied and dataset generation is
explicitly authorized.
