# DataGuard Phase 2A — Controlled Dataset and Safety Plan

**Status:** Design proposal only. Dataset generation, image creation or
modification, label creation, recovery, model training, and wiping are not
authorized by this document. No dataset or ground-truth manifest is created
here.

## Evidence key

- **Verified repository fact**: directly observed in current files or Git
  metadata during this read-only inspection.
- **Proposed design**: a future approach for review, not an implemented tool or
  approved operation.
- **Requires human approval**: a choice or action that must be explicitly
  approved before implementation or execution.
- **Unimplemented**: repository code does not currently provide the capability.

## 1. Current state and approved protocol

**Verified repository facts:**

- HEAD is `59951f4c61d7386012bd4fc442130072d1eb90fa`.
- Before this plan was created, `git status --short` showed only `?? docs/`;
  the protocol was already untracked. No tracked-file diff was reported.
- `docs/ML_PROTOCOL.md` exists. Its SHA-256 at inspection was
  `68A4DF9F7FBC9E807DE79C6D08D3B08ACF3270CE7CB7162411F891F430ADB82F`.
  The hash should be checked again after this plan to confirm it remained
  unchanged during this task. Because it is untracked and there is no recorded
  approved-version hash in Git, its unchanged status relative to an earlier
  approval cannot be independently established here.
- The user explicitly approved the protocol's overall direction and
  authorized Phase 2 planning only; dataset generation remains unauthorized
  until the safety plan is reviewed and separately approved. The repository
  copy still labels itself **“Draft for human review”** and retains future-phase
  decisions marked “Requires approval.” Treat the human approval as approval
  of the protocol framework, not as blanket approval of every algorithm,
  feature, scenario, metric, threshold, or later operation. The protocol's
  status wording should be reconciled in a separate documentation-only edit;
  until then, preserve this distinction rather than silently treating every
  open decision as resolved.
- The protocol requires regular disk-image files only, immutable baselines,
  separate working copies and outputs, hash verification, checked arithmetic,
  bounded reads, and fail-closed handling. Its `recovery_fraction` requires
  independently established original byte-position provenance; content
  matching alone is insufficient. Unscorable outcomes must remain distinct
  from genuine zero-recovery outcomes.

The protected `dataset/baseline/` contents and existing ground-truth manifest
contents were not inspected. No physical device or mounted user volume was
accessed.

## 2. Existing repository capabilities and gaps

| Area | Verified capability | Status for dataset work |
|---|---|---|
| Image creation/copy/hash utilities | No dedicated image generator, image copier, or dataset hash/custody utility was found in the scoped repository file listing or source searches. | **Unimplemented.** Do not infer that a suitable external utility is installed. |
| FAT32 parser | `FileScanner::scanFat32` consumes an `IReadOnlyVolumeReader`; parser offsets are volume-relative and the interface provides bounded reads and identity revalidation. | Metadata parser exists. It is not an image-file reader or recovery engine. |
| Mounted-volume reader | `openMountedVolumeReadOnly` creates a Windows handle for a mounted volume with `GENERIC_READ`. | Physical-volume path exists but is forbidden for this dataset workflow and must not be called. |
| Deleted-file recovery | `FileScanner::scanDeleted` returns `Unsupported`; FAT32 scan results are directory metadata/candidates, not recovered contents. | **Unimplemented.** No labels can currently be produced by the specified recovery procedure. |
| Synthetic test utilities | `tests/FileScannerSafetyTests.cpp` uses an in-memory `FakeVolume` implementing `IReadOnlyVolumeReader`; tests construct synthetic FAT32 bytes. Tests also use `QTemporaryDir` for ordinary temporary files/directories. | Useful test patterns only. They do not create a regular FAT32 image-file dataset or establish byte recovery ground truth. |
| FAT32 formatter/tooling | No formatter, image-mounting tool, or FAT32 image-construction library is referenced by the project CMake configuration. CMake uses C++17 and Qt 6; existing project dependencies are Qt Widgets/Sql and Windows bcrypt. | Availability outside the project is unknown and not verified. No dependency/tool is selected here. |
| Manifest conventions | `src/WipeEngine.manifest` is a Windows application manifest. No tracked dataset or ground-truth manifest was found by `git ls-files dataset`. | No reusable dataset-manifest convention was verified. The application manifest is unrelated. |
| ML implementation | `RandomForest` has numeric `train(X,y)` and `predict(features)` methods. `FeatureExtractor` computes ten statistics from an explicitly supplied byte buffer. No training call site, serialization, evaluation runner, saved model, or recorded metrics were found in the inspected `src/`, `tests/`, CMake, or relevant Git history. | Model evaluation pipeline and trained model are **unimplemented/unverified**. |

The repository contains a `WipeEngine::wipeVolumeExtents` path that writes
through an injected `IStorageDevice::writeAt` interface. Legacy raw-path,
sector, and ambiguous-extent entry points return `Unsupported`. No production
storage implementation or invocation is established by this plan. The write
path must not be exercised or connected to dataset generation.

## 3. Proposed dataset design (no generation performed)

### A. Reference corpus

**Proposed design:** create original test files in a dedicated, allowlisted
regular-file directory using a deterministic generator. Include multiple
content patterns and sizes only after approving the exact corpus design. Give
each source file a random or collision-resistant `original_file_id`; give each
experiment a separate `experiment_id`. Do not encode the expected outcome in
identifiers or filenames.

Before any image construction, record each file's exact logical size, SHA-256,
generator/version/configuration, and provenance. Verify hashes by rereading
only the regular reference files in the approved directory. Detect duplicates
by exact SHA-256 plus size; place byte-identical files and all derived
experiments in one leakage group. A hash collision or conflicting identity is
an error requiring investigation, not a reason to merge by guess.

Keep references and the manifest in a separately access-controlled,
write-protected location. Record the approved location, permissions, and
custody transitions. Use immutable/versioned storage or write-once snapshots
only after the owner selects and approves the mechanism. Verify hashes before
and after each experiment; never repair or overwrite a mismatch automatically.

**Unimplemented:** no reference generator, reference corpus, or custody tool
was verified. **Requires approval:** corpus composition, generator, directory
layout, access control, and immutable-storage mechanism.

### B. Image construction and lifecycle

**Proposed design:** use a flat raw image file (for example, `.img`) as the
first candidate format, subject to a tool-compatibility review. Choose image
size only after calculating FAT32 geometry, parser bounds, expected fixture
size, and available workspace capacity. No exact size or external tool is
selected or assumed here.

Construct a baseline from approved generator inputs, close all handles, and
record its exact size and SHA-256. Mark the baseline read-only at the filesystem
and workflow level. Create a separate working copy for each experiment and
verify the copy hash against the baseline before applying its recorded
scenario. Recovery outputs, logs, and temporary files go in distinct output
directories and never inside or over source images or references.

Record hashes for baseline, working copy before/after scenario construction,
and output artifacts. A change to a working copy is allowed only as the
explicit scenario's controlled image-file operation; a baseline/reference
hash change is a hard stop. On interruption, stop work, close handles, preserve
the original and evidence, mark the experiment incomplete/unscorable, and
quarantine the working copy for review. Cleanup may remove only paths created
inside an approved experiment-specific scratch directory after canonical-path
and identity checks. Never attempt broad or recursive cleanup of a parent,
baseline, reference, or user directory.

**Unimplemented:** regular image-file creation, copying, immutable-baseline
protection, and lifecycle verification utilities. **Requires approval:** image
format, sizing, generator/tool, storage layout, and cancellation/recovery policy.

### C. Proposed scenarios

| Scenario | Intended observation | Ground truth required | Ambiguity/risk | Scoring suitability |
|---|---|---|---|---|
| Deleted file; no subsequent writes | Output of the frozen procedure when directory metadata and clusters have not been deliberately reused. | Original bytes/hash/size; image hashes; unique file-to-entry mapping; documented deletion sequence; output-to-original byte-position provenance. | FAT metadata may not preserve identity or complete chain; deletion does not establish recoverability. | Candidate for scoring only if the fixed procedure completes and mapping is unique; otherwise unscorable. |
| Controlled cluster reuse | Effect of a separately documented later allocation of some or all relevant clusters. | Above, plus exact image-copy history and replacement-file bytes/identity and cluster-allocation evidence. | Old/new bytes may coexist, overlap, or be indistinguishable; cluster reuse evidence can be incomplete. | Score only uniquely mapped original positions; ambiguous/reused mapping is unscorable. |
| Controlled overwrite | Effect of writing known patterns to targeted logical image regions on a working copy. | Above, plus exact target logical offsets, lengths, data, and operation log/hash. | A changed byte need not correspond to the original file unless mapping is established; partial writes or tool errors can confound the case. | Score only if procedure completion and per-position mapping are established; infrastructure/write uncertainty is unscorable. |
| Fragmented file | Whether the fixed procedure handles a file whose original allocation spans a validated FAT chain. | Original content/hash/size; validated original chain/layout before deletion; documented fragmentation construction; image hashes. | FAT chain may be altered on deletion/reuse; constructing reproducible fragmentation may require tooling not present. | Conditional; do not score unless original mapping and the recovery procedure's chain assumptions are validated. |
| Malformed/ambiguous metadata | Fail-closed behavior when filesystem records are intentionally inconsistent or ambiguous. | Exact mutation specification and immutable source/working-image hashes. | A parser rejection or partial read tests robustness, not file-recovery fraction. | Normally unscorable for regression; report separately as safety/parser behavior, not a target label. |
| Unsupported or infrastructure failure | Correct classification of unsupported formats, cancellation, I/O errors, stale identity, and tool failures. | Reproducible failure injection or documented unsupported image properties and complete execution logs. | Failure may be confused with true zero output if completion state is not recorded. | Unscorable with explicit reason code; never numeric zero. |

No scenario is asserted to be constructible or scoreable with current code.
All future scenario writes, if approved, apply only to disposable working image
files and never to mounted volumes or physical media.

### D. Proposed manifest and custody

**Proposed schema:** a versioned JSON Lines or equivalent serialization with
strict schema validation; the serialization format itself requires approval.
Each experiment record should include:

- `schema_version`, `experiment_id`, `original_file_id`, and leakage/group IDs.
- Original relative path for provenance only, logical size, SHA-256, source
  generator/version/configuration, and duplicate-content group.
- `image_id`, format, exact byte length, baseline SHA-256, working-copy ID,
  pre/post scenario hashes, and image-construction tool/version/configuration.
- Scenario family, complete parameters, operation sequence, and completion
  status.
- Recovery procedure ID/version/source revision/configuration when labeling
  is eventually authorized.
- Scoring tool/version, score only when eligible, status, byte-position mapping
  evidence reference, and explicit unscorable reason code/details otherwise.
- Timestamps and custody events as audit metadata; timestamps are not model
  features.

Serialize and validate through a maintained library available in the approved
implementation environment; no library is selected or added in this planning
phase. Canonicalize key ordering where the format supports it, define encoding
and newline rules, and hash the exact manifest bytes. Store a signed or
otherwise access-controlled immutable snapshot and retain the approval and
custody records separately. Verify the manifest hash and all referenced
artifact hashes before and after each run. Never edit a label in place; a
correction requires a new manifest version with an auditable supersession
record and explicit approval.

**Requires approval:** serialization, schema versioning, signing/custody
mechanism, reason-code vocabulary, storage path, and supersession process. No
manifest file is created by this plan.

### E. Metadata-only feature baseline

Only features available before recovery may be considered. Source/image IDs,
entry identity strings, raw offsets, paths, names, hashes, and timestamps are
primarily provenance/grouping data and should not automatically become model
inputs: they can memorize images or scenario construction and leak split
membership. Since the initial direction is metadata-only, byte-content
features are excluded from the baseline.

| Candidate | Inspected source and structured availability | Validation/ambiguity | Missing-value representation | Leakage assessment |
|---|---|---|---|---|
| Deleted/existing/invalid status | `FileEntry::status`; structured. | Parser classification only; invalid/ambiguous records must not silently be treated as valid candidates. Candidate population should be defined before training. | Separate `status_known`/category; missing is not numeric zero. Reject unknown categories unless explicitly handled. | Deleted status is likely constant in a deleted-entry cohort; status may define cohort rather than predict within-cohort outcome. Review before use. |
| Entry size and `sizeKnown` | `FileEntry::sizeBytes` and `sizeKnown`; structured. | Parser's directory-entry size can be unknown or inconsistent; it is not authoritative original size. | Keep `size_known` boolean and masked size; preserve genuine zero distinctly. | Could correlate with generated file sizes/scenarios. Group/split and review distribution before inclusion. |
| Filesystem type | `DriveInfo::fileSystem`; structured at selected-volume level, not a `FileEntry` field. | FAT32 support is checked; a dataset restricted to FAT32 has no within-dataset variation. | Explicit unknown category/flag. | May identify unsupported cases or scenario groups; likely constant for the initial cohort. |
| Volume identity validated | `FileScanResult::volumeIdentityValidated` and reader identity; structured at scan level. | True only after validation; stale or mismatched identity fails scan. | Separate boolean plus explicit scan status. | Do not use the raw identity string; it can memorize image/device. Validity may be a data-quality gate, not a predictive feature. |
| Entry identity availability | `FileEntry::entryIdentityAvailable`; structured. The identity includes volume and directory-slot metadata in the parser. | Availability is checked; the identity string itself is an identifier, not evidence of recoverability. | Explicit boolean/category; absent identity is not zero. | Raw identity/offset/path may leak image or scenario; retain for linkage/audit, exclude from model unless separately justified. |
| First-cluster validity | Parser evaluates starting cluster, but `FileEntry` has no dedicated structured first-cluster field. | May be represented only indirectly in `reason`/classification; no reliable numeric feature is currently exposed. | Not usable until represented explicitly; never parse reason prose or use default zero. | If later exposed, validity could be a meaningful pre-recovery metadata feature, but requires leakage review. |
| Reuse/allocation evidence | Parser examines allocated clusters and reuse ambiguity internally; no dedicated structured reuse field was found in `FileEntry`. | Current `reason` text and confidence are not a structured evidence contract. | Not usable until explicit validated enum/flag exists. | Directly related to scenario/target; could be legitimate but may encode the label-generation scenario. Review and group by scenario. |
| Fragmentation hint | Parser follows chains for existing entries but does not expose a dedicated candidate fragmentation feature in `FileEntry`. | Deleted chain may be unavailable or ambiguous. Do not infer from size or name. | Not usable until reliable structured evidence exists. | Scenario-correlated; may leak if scenario family appears in only one split. |
| Name, path, parent cluster, directory-slot offset | `FileEntry` fields exist. Some deleted names/paths are unknown; offsets are volume-relative. | Candidate metadata can be incomplete; deleted LFN/short-name evidence is conservative. | Preserve explicit known flags; unknown stays missing. | High risk of memorization/identity leakage; retain for audit and display, exclude from baseline model unless separately approved. |

This table identifies candidates, not an approved feature schema. The exact
training cohort, encodings, and feature inclusion require approval. Never
substitute numeric zero for missing metadata.

## 4. Split strategy and evaluation readiness

**Proposed grouping:** build leakage groups from original source files and
their exact-content duplicates, baseline images and their copies, and derived
scenarios/revisions. Any records linked by shared source content, image lineage,
or a derived experiment must remain together whenever that relationship could
leak information into evaluation. Assign complete groups to train, validation,
or test; never split related rows across splits.

Before selecting a split, inspect the resulting group relationships for
connected components. If combining source-file, image-lineage, and scenario
relationships collapses most or all records into one component, do not weaken
the leakage rules merely to obtain a convenient split. Instead, redesign the
prospective cohort or narrow the intended generalization claim, document the
trade-off, and obtain approval. If independent groups are insufficient, report
that no defensible split is currently available.

State the test's intended generalization claim separately for:

1. Unseen disk images built from known source content/scenarios.
2. Unseen original source files/content.
3. Unseen scenario families or parameters.
4. Combinations that are independently held out.

A held-out image alone supports, at most, an image-level claim for the sampled
content/scenario population. It does not establish unseen-content or
unseen-scenario generalization when those are shared. Because source, image, and
scenario grouping can interact, the split design must state exactly which
relationships are held out and which remain shared. If there are too few
independent groups to hold out each claimed dimension, report that limitation;
do not create random row splits, weaken grouping silently, or claim broad
generalization. A small dataset may support a procedural pilot only, not
defensible model-performance claims.

No sample counts, split ratios, metric results, or acceptance thresholds are
set here. **Requires approval:** grouping granularity, split proportions/seed,
independent-group sufficiency, generalization claim, and any threshold rationale.

## 5. Safety controls and future acceptance gates

Before any separately authorized generator exists or runs, it must pass these
preflight controls:

1. **Canonical allowlist:** resolve the approved workspace root and every
   input/output path; ensure all paths remain beneath their specifically
   approved directories. Reject traversal, alternate stream tricks, device
   namespaces, UNC/device paths, mounted-volume paths, physical-drive paths,
   symlinks, junctions, reparse points, and unexpected hard-link/alias
   conditions. Recheck immediately before opening and before cleanup.
2. **Regular-file confirmation:** accept only regular image files and regular
   reference files in approved locations. Reject directories where a file is
   expected, sparse/oversized/unexpected formats unless explicitly supported,
   and all special/device files. Never resolve an image identifier to a drive
   letter or mounted volume.
3. **Bounded checked I/O:** validate file size, offsets, lengths, sector and
   cluster geometry, and all multiplication/addition with checked arithmetic
   before allocation or read/write. Bound allocations and loops. Reject short
   reads/writes, integer overflow, malformed metadata, or size changes.
4. **Baseline verification:** verify expected baseline size/hash before making
   a working copy; hash-check the copy; keep source baseline open/read-only if
   possible. Re-hash baseline and references after every experiment. Any
   mismatch stops the run and preserves evidence.
5. **Separation:** use distinct allowlisted locations for protected baseline,
   original references/manifests, per-experiment working copies, logs, and
   outputs. The future generator must never write to baseline/reference paths.
6. **Cancellation and cleanup:** cancellation closes handles and marks the
   experiment incomplete/unscorable. Cleanup must be limited to uniquely
   created scratch paths after canonical identity revalidation; on any
   uncertainty, retain files and require human inspection rather than delete.
7. **No device backend:** confirm by code inspection and runtime test design
   that the generator, scanner adapter, recovery tool, and tests use only
   regular image files or in-memory fakes. Block physical/mounted-volume
   readers and all wipe/recovery UI or storage backends.
8. **Auditability:** emit a run record with tool versions, inputs/outputs,
   hashes, scenario, state transitions, errors, and explicit completion or
   unscorable reason. Avoid recording secrets or unrelated user data.

Before generation is permitted, independently review and verify the generator
implementation, path guard, image-format/geometry validator, hash/copy checks,
cancellation behavior, cleanup boundaries, and tests proving that device
paths and protected locations are rejected. Tests must demonstrate rejection
without opening or modifying any physical device, mounted volume, protected
baseline, or user-data path; use synthetic path fixtures and disposable regular
files only. Verify the selected tools and versions are actually installed or
obtain separate approval to add them. Run only synthetic regular-image tests in
an isolated workspace. Confirm the baseline and reference hashes are unchanged
afterward. Dataset generation requires explicit human authorization after these
checks; this Phase 2A plan does not provide it.

## 6. Decisions requiring human approval

Before implementation or generation, the owner must explicitly decide:

1. Reconcile the protocol document's status wording with the owner's
   approval of its overall framework. Keep unresolved algorithm, feature,
   scenario, metric, and threshold choices as future-phase approval gates;
   the framework approval does not resolve them automatically.
2. Reference corpus types/sizes/content patterns, generator and version,
   identifier scheme, duplicate policy, storage locations, access controls,
   and immutable custody mechanism.
3. Image format, exact image sizing/geometry, construction tooling, permitted
   scenario write sequences, baseline/working/output layout, and interrupted
   run retention/cleanup policy.
4. Which scenarios are in scope and what evidence is sufficient to score each,
   particularly reuse, overwrite, and fragmentation.
5. Manifest serialization/schema/versioning, hash/signature/custody records,
   reason-code vocabulary, and correction/supersession process.
6. Metadata feature cohort, encoding/missing-value representation, leakage
   exclusions, and whether any additional structured scanner metadata could be
   considered in a later separately authorized code task. Byte-content features
   remain excluded unless separately reviewed and approved.
7. Grouping/split policy, how connected leakage groups will be formed,
   minimum independent groups, intended generalization claims, evaluation
   metrics/baseline, and justified acceptance thresholds. If the grouping graph
   cannot support the intended split, approve a revised cohort or narrower
   claim rather than weakening leakage controls.
8. The future generator and its path/I/O safety design, exact dependency/tool
   versions, independent review plan, and explicit authorization to generate.
9. Separate authorization and safety review for any later recovery or wiping
   work. No real-device wiping is authorized here; the existing injected
   `wipeVolumeExtents` write path needs its own safety audit before any
   production backend is considered.

## 7. Phase boundary

This document is a proposal only. It creates no image, file corpus, manifest,
label, script, or model. No Phase 3 recovery implementation or evaluation is
started. Proceeding to dataset generation requires review of this plan, closure
of the approval items above, independent safety verification, and a separate
explicit authorization.
